#include "includes.h"

#include "chislink_cache.h"
#include "chislink_nes_header.h"

#define PRG_GROUP_BYTES (32u * 1024u)
#define PRG_PAGE_BYTES (8u * 1024u)
#define PRG_GROUP_PAGES 4u
#define PRG_CACHE_GROUPS 4u
#define CHR_PAGE_BYTES 1024u
#define CHR_CACHE_PAGES_MIN 9u
#define CHR_CACHE_PAGES_MAX 17u
#define INVALID_GROUP 0xffffu

extern u8 __chislink_cache_start[];
extern u8 __chislink_cache_end[];
extern u8 __chislink_irq_stack_top[];

typedef struct chislink_rom_image {
    romheader menu;
    u8 header[16];
    u8 trainer[512];
} chislink_rom_image_t;

EWRAM_BSS static chislink_rom_image_t s_rom;
EWRAM_BSS static chislink_nes_header_t s_header;
EWRAM_BSS static u8 (*s_prg_cache)[PRG_GROUP_BYTES];
EWRAM_BSS static u8 *s_prg67_cache;
EWRAM_BSS static u8 (*s_chr_cache)[CHR_PAGE_BYTES];
EWRAM_BSS static u16 s_prg_group[PRG_CACHE_GROUPS];
EWRAM_BSS static u16 s_chr_page[CHR_CACHE_PAGES_MAX];
EWRAM_BSS static u32 s_prg_age[PRG_CACHE_GROUPS];
EWRAM_BSS static u32 s_chr_age[CHR_CACHE_PAGES_MAX];
EWRAM_BSS static u32 s_clock;
EWRAM_BSS static u16 s_prg67_page;
EWRAM_BSS static u8 s_chr_slots;
EWRAM_BSS static u8 s_runtime_ready;

typedef struct cache_pause_state {
    u16 ime;
    u16 ie;
    u8 active;
} cache_pause_state_t;

static u8 *chr_ram_page(u32 page);

static cache_pause_state_t pause_for_miss(void) {
    cache_pause_state_t state = {0};
    if (!s_runtime_ready) return state;

    state.ime = REG_IME;
    REG_IME = 0;
    state.ie = REG_IE;
    suspend_hdma();
    REG_IF = 0xffffu;
    REG_IE = IRQ_SERIAL;
    REG_IME = 1;
    state.active = 1;
    return state;
}

static void resume_after_miss(cache_pause_state_t state) {
    if (!state.active) return;

    REG_IME = 0;
    REG_IF = 0xffffu;
    REG_IE = state.ie;
    resume_hdma();
    REG_IME = state.ime;
}

int chislink_pocketnes_cache_reserve(u8 **cursor, u8 *limit) {
    if (!cursor || !*cursor || !limit) return -1;
    uintptr_t at = ((uintptr_t)*cursor + 31u) & ~(uintptr_t)31u;
    uintptr_t end = at + PRG_CACHE_GROUPS * PRG_GROUP_BYTES +
                    PRG_PAGE_BYTES +
                    CHR_CACHE_PAGES_MIN * CHR_PAGE_BYTES;
    if (at != (uintptr_t)__chislink_cache_start ||
        end != (uintptr_t)__chislink_cache_end || end < at ||
        (uintptr_t)__chislink_irq_stack_top > (uintptr_t)limit) return -2;
    s_prg_cache = (u8 (*)[PRG_GROUP_BYTES])at;
    at += PRG_CACHE_GROUPS * PRG_GROUP_BYTES;
    s_prg67_cache = (u8 *)at;
    /* MMC3 uses SRAM at $6000, so its otherwise-unused PRG67 page can double
     * the CHR cache without increasing the fixed multiboot workspace. */
    if (s_header.mapper == 4u) {
        s_chr_cache = (u8 (*)[CHR_PAGE_BYTES])at;
        s_chr_slots = CHR_CACHE_PAGES_MAX;
    } else {
        s_chr_cache = (u8 (*)[CHR_PAGE_BYTES])(at + PRG_PAGE_BYTES);
        s_chr_slots = CHR_CACHE_PAGES_MIN;
    }
    *cursor = (u8 *)end;
    return 0;
}

static int active_prg_group(u32 group) {
    for (u32 i = 1; i < 5u; ++i) {
        if ((u32)bank6[i] / PRG_GROUP_PAGES == group) {
            return 1;
        }
    }
    return 0;
}

u8 *chislink_pocketnes_prg67(u32 page) {
    u32 pages = (u32)rompages * 2u;
    if (!s_prg67_cache || pages == 0) return s_prg67_cache;
    page %= pages;
    if (s_prg67_page != page) {
        cache_pause_state_t pause = pause_for_miss();
        memset(s_prg67_cache, 0xff, PRG_PAGE_BYTES);
        (void)chislink_bridge_read(s_header.prg_offset +
                                  page * PRG_PAGE_BYTES,
                                  s_prg67_cache, PRG_PAGE_BYTES);
        s_prg67_page = (u16)page;
        resume_after_miss(pause);
    }
    return s_prg67_cache;
}

static int active_chr_page(u32 page) {
    u32 pages = s_header.chr_size / CHR_PAGE_BYTES;
    for (u32 i = 0; i < 8u; ++i) {
        u32 active = (u32)Cbank0[i];
        if (pages != 0 && chr_ram_page(active) == NULL &&
            active < (u32)vrompages * 8u &&
            active % pages == page) {
            return 1;
        }
    }
    return 0;
}

static int read_group(u32 file_offset, u32 file_bytes, u8 *dst,
                      u32 group_bytes, u32 group) {
    u32 offset = group * group_bytes;
    memset(dst, 0xff, group_bytes);
    if (offset >= file_bytes) {
        return -1;
    }
    u32 take = file_bytes - offset;
    if (take > group_bytes) {
        take = group_bytes;
    }
    return chislink_bridge_read(file_offset + offset, dst, take);
}

static void map_prg_group(u32 slot, u32 group) {
    u32 first = group * PRG_GROUP_PAGES;
    for (u32 i = 0; i < PRG_GROUP_PAGES; ++i) {
        if (first + i < (u32)rompages * 2u) {
            instant_prg_banks[first + i] =
                &s_prg_cache[slot][i * PRG_PAGE_BYTES];
        }
    }
}

static void unmap_prg_group(u32 slot) {
    if (s_prg_group[slot] == INVALID_GROUP) return;
    u32 first = (u32)s_prg_group[slot] * PRG_GROUP_PAGES;
    for (u32 i = 0; i < PRG_GROUP_PAGES; ++i) {
        if (first + i < (u32)rompages * 2u) {
            instant_prg_banks[first + i] = NULL;
        }
    }
}

static void unmap_chr_page(u32 slot) {
    if (s_chr_page[slot] == INVALID_GROUP) return;
    u32 table_pages = (u32)vrompages * 8u;
    if (mapper == TQROM) table_pages *= 2u;
    for (u32 i = 0; i < table_pages; ++i) {
        if (instant_chr_banks[i] == s_chr_cache[slot]) {
            instant_chr_banks[i] = NULL;
        }
    }
}

static u8 *chr_ram_page(u32 page) {
    if (mapper == 74) {
        u32 base = rompages == 32 ? 8u : 0u;
        if (page >= base && page < base + 2u) {
            return NES_VRAM + (page - base) * CHR_PAGE_BYTES;
        }
    }
    if (mapper == TQROM && page >= 64u && page < 128u) {
        return NES_VRAM + (page & 7u) * CHR_PAGE_BYTES;
    }
    return NULL;
}

static void map_chr_aliases(u32 page, u8 *address) {
    u32 pages = s_header.chr_size / CHR_PAGE_BYTES;
    u32 table_pages = (u32)vrompages * 8u;
    if (mapper == TQROM) table_pages *= 2u;
    for (u32 alias = page; alias < table_pages; alias += pages) {
        if (chr_ram_page(alias) == NULL) {
            instant_chr_banks[alias] = address;
        }
    }
}

static u32 choose_prg_slot(void) {
    u32 victim = 0;
    u32 oldest = 0xffffffffu;
    for (u32 i = 0; i < PRG_CACHE_GROUPS; ++i) {
        if (s_prg_group[i] == INVALID_GROUP) return i;
        if (!active_prg_group(s_prg_group[i]) && s_prg_age[i] < oldest) {
            oldest = s_prg_age[i];
            victim = i;
        }
    }
    return victim;
}

static u32 choose_chr_slot(void) {
    u32 victim = 0;
    u32 oldest = 0xffffffffu;
    for (u32 i = 0; i < s_chr_slots; ++i) {
        if (s_chr_page[i] == INVALID_GROUP) return i;
        if (!active_chr_page(s_chr_page[i]) && s_chr_age[i] < oldest) {
            oldest = s_chr_age[i];
            victim = i;
        }
    }
    return victim;
}

u8 *chislink_pocketnes_ensure_prg(u32 page) {
    u32 pages = (u32)rompages * 2u;
    if (pages == 0) return NULL;
    u32 requested = page;
    page %= pages;
    u32 group = page / PRG_GROUP_PAGES;
    for (u32 i = 0; i < PRG_CACHE_GROUPS; ++i) {
        if (s_prg_group[i] == group) {
            s_prg_age[i] = ++s_clock;
            if (requested != page) {
                instant_prg_banks[requested] = instant_prg_banks[page];
            }
            return &s_prg_cache[i][(page % PRG_GROUP_PAGES) * PRG_PAGE_BYTES];
        }
    }
    u32 slot = choose_prg_slot();
    cache_pause_state_t pause = pause_for_miss();
    unmap_prg_group(slot);
    if (read_group(s_header.prg_offset, s_header.prg_size,
                   s_prg_cache[slot], PRG_GROUP_BYTES, group) < 0) {
        resume_after_miss(pause);
        return NULL;
    }
    s_prg_group[slot] = (u16)group;
    s_prg_age[slot] = ++s_clock;
    map_prg_group(slot, group);
    if (requested != page) {
        instant_prg_banks[requested] = instant_prg_banks[page];
    }
    u8 *result =
        &s_prg_cache[slot][(page % PRG_GROUP_PAGES) * PRG_PAGE_BYTES];
    resume_after_miss(pause);
    return result;
}

void chislink_pocketnes_ensure_chr(u32 page) {
    u32 pages = s_header.chr_size / CHR_PAGE_BYTES;
    if (pages == 0) return;
    u32 requested = page;
    u8 *ram = chr_ram_page(requested);
    if (ram != NULL) {
        instant_chr_banks[requested] = ram;
        return;
    }
    page %= pages;
    for (u32 i = 0; i < s_chr_slots; ++i) {
        if (s_chr_page[i] == page) {
            s_chr_age[i] = ++s_clock;
            map_chr_aliases(page, s_chr_cache[i]);
            instant_chr_banks[requested] = s_chr_cache[i];
            return;
        }
    }
    u32 slot = choose_chr_slot();
    cache_pause_state_t pause = pause_for_miss();
    unmap_chr_page(slot);
    if (read_group(s_header.chr_offset, s_header.chr_size,
                   s_chr_cache[slot], CHR_PAGE_BYTES, page) < 0) {
        resume_after_miss(pause);
        return;
    }
    s_chr_page[slot] = (u16)page;
    s_chr_age[slot] = ++s_clock;
    map_chr_aliases(page, s_chr_cache[slot]);
    instant_chr_banks[requested] = s_chr_cache[slot];
    resume_after_miss(pause);
}

void chislink_pocketnes_ensure_chr_range(u32 page, u32 count) {
    while (count--) {
        chislink_pocketnes_ensure_chr(page++);
    }
}

int chislink_pocketnes_boot(void) {
    int ret = chislink_bridge_open(CLP_LAUNCH_NES);
    if (ret < 0) return ret;
    const chislink_launch_info_t *launch = chislink_bridge_launch();
    if (!launch || launch->size < 16u ||
        chislink_bridge_read(0, s_rom.header, sizeof(s_rom.header)) < 0) {
        return -20;
    }
    ret = chislink_nes_parse_header(s_rom.header, launch->size, &s_header);
    if (ret < 0) return ret - 30;

    memset(&s_rom.menu, 0, sizeof(s_rom.menu));
    const char *name = "ChisLink NES";
    if (launch->path[0] != '\0') {
        name = launch->path;
        for (const char *p = launch->path; *p; ++p) {
            if (*p == '/') name = p + 1;
        }
    }
    strncpy(s_rom.menu.name, name, sizeof(s_rom.menu.name) - 1u);
    s_rom.menu.filesize = launch->size;
    if ((s_header.flags6 & 0x04u) != 0 &&
        chislink_bridge_read(16u, s_rom.trainer,
                             sizeof(s_rom.trainer)) < 0) {
        return -40;
    }
    return 0;
}

u8 *chislink_pocketnes_rom(void) {
    return (u8 *)&s_rom;
}

int chislink_pocketnes_cache_init(void) {
    if (!s_prg_cache || !s_prg67_cache || !s_chr_cache) return -1;
    s_runtime_ready = 0;
    u32 prg_pages = (u32)rompages * 2u;
    u32 chr_pages = s_header.chr_size / CHR_PAGE_BYTES;
    u32 chr_table_pages = (u32)vrompages * 8u;
    if (mapper == TQROM) chr_table_pages *= 2u;
    for (u32 i = 0; i < prg_pages; ++i) instant_prg_banks[i] = NULL;
    for (u32 i = 0; i < chr_table_pages; ++i) instant_chr_banks[i] = NULL;
    for (u32 i = 0; i < PRG_CACHE_GROUPS; ++i) {
        s_prg_group[i] = INVALID_GROUP;
        s_prg_age[i] = 0;
    }
    for (u32 i = 0; i < s_chr_slots; ++i) {
        s_chr_page[i] = INVALID_GROUP;
        s_chr_age[i] = 0;
    }
    s_clock = 0;
    s_prg67_page = INVALID_GROUP;

    u32 prg_groups = (s_header.prg_size + PRG_GROUP_BYTES - 1u) /
                     PRG_GROUP_BYTES;
    for (u32 i = 0; i < PRG_CACHE_GROUPS && i < prg_groups; ++i) {
        chislink_pocketnes_ensure_prg(i * PRG_GROUP_PAGES);
    }
    if (prg_pages != 0 && instant_prg_banks[0] != NULL) {
        memcpy(s_prg67_cache, instant_prg_banks[0], PRG_PAGE_BYTES);
        s_prg67_page = 0;
    }
    for (u32 i = 0; i < s_chr_slots && i < chr_pages; ++i) {
        chislink_pocketnes_ensure_chr(i);
    }
    for (u32 i = 0; i < chr_table_pages; ++i) {
        u8 *ram = chr_ram_page(i);
        if (ram != NULL) instant_chr_banks[i] = ram;
    }
    if (chr_pages == 0) {
        for (u32 i = 0; i < 8u; ++i) {
            instant_chr_banks[i] = NES_VRAM + i * CHR_PAGE_BYTES;
        }
    }
    s_runtime_ready = 1;
    return 0;
}
