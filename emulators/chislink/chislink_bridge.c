#include "chislink_bridge.h"

#include <stddef.h>
#include <string.h>

#include "chislink/client.h"
#include "chislink/gba/hw.h"
#include "chislink/gba_sio_transport.h"
#include "chislink/ram_map.h"
#include "chislink/storage_client.h"

#define REG_DISPCNT (*(volatile uint16_t *)0x04000000u)
#define BG_PALETTE0 (*(volatile uint16_t *)0x05000000u)

#define BRIDGE_EWRAM __attribute__((section(".ewram")))

static BRIDGE_EWRAM cl_client_t s_client;
static BRIDGE_EWRAM cl_gba_sio_transport_t s_transport;
static BRIDGE_EWRAM cl_ram_map_t s_ram_map;
static BRIDGE_EWRAM cl_storage_client_t s_storage;
static BRIDGE_EWRAM chislink_launch_info_t s_launch;
static BRIDGE_EWRAM uint8_t s_storage_scratch[CLP_LAUNCH_VALUE_MAX_BYTES];
static BRIDGE_EWRAM uint64_t s_rom_position;
static BRIDGE_EWRAM uint16_t s_rom_handle;
static BRIDGE_EWRAM uint8_t s_open;
#ifdef CHISLINK_GOOMBACOLOR
static BRIDGE_EWRAM char s_save_path[72];
static BRIDGE_EWRAM uint16_t s_save_handle;
#endif

static void bridge_show_status(uint16_t color) {
    REG_DISPCNT = 0;
    BG_PALETTE0 = color;
}

static void bridge_abort(void) {
    bridge_show_status(0x001fu);
    for (;;) {}
}

static int bridge_parse_launch(uint32_t expected_type, uint32_t length) {
    if (length <= CLP_LAUNCH_VALUE_FIXED_BYTES ||
        length > CLP_LAUNCH_VALUE_MAX_BYTES ||
        clp_load_le32(s_storage_scratch, 4u) != CLP_LAUNCH_VALUE_VERSION) {
        return -1;
    }

    s_launch.type = clp_load_le32(s_storage_scratch + 4u, 4u);
    s_launch.flags = clp_load_le32(s_storage_scratch + 8u, 4u);
    s_launch.size = clp_load_le32(s_storage_scratch + 12u, 4u);
    uint32_t path_length = length - CLP_LAUNCH_VALUE_FIXED_BYTES;
    const uint8_t *path = s_storage_scratch + CLP_LAUNCH_VALUE_FIXED_BYTES;
    if (s_launch.type != expected_type || s_launch.size == 0 ||
        path_length == 0 || path_length > sizeof(s_launch.path) ||
        path[path_length - 1u] != '\0' || path[0] != '/') {
        return -2;
    }
    memcpy(s_launch.path, path, path_length);
    return 0;
}

void chislink_bridge_reinstall_transport(void) {
    cl_gba_sio_transport_install(&s_transport);
}

#ifdef CHISLINK_GOOMBACOLOR
void chislink_bridge_install_graphics_irqs(void (*vblank_handler)(void),
                                           void (*vcount_handler)(void)) {
    uint16_t ime = CL_GBA_REG_IME;
    CL_GBA_REG_IME = 0;
    CL_GBA_REG_IF = 0xffffu;
    cl_gba_irq_set_handler(CL_GBA_IRQ_VBLANK, vblank_handler);
    cl_gba_irq_set_handler(CL_GBA_IRQ_VCOUNT, vcount_handler);
    CL_GBA_REG_DISPSTAT = 0x0008u;
    cl_gba_irq_enable(CL_GBA_IRQ_VBLANK | CL_GBA_IRQ_VCOUNT |
                      CL_GBA_IRQ_SERIAL);
    CL_GBA_REG_IME = ime;
}
#endif

int chislink_bridge_open(uint32_t expected_type) {
    bridge_show_status(0x4000u);
    memset(&s_launch, 0, sizeof(s_launch));
    s_rom_handle = 0;
    s_rom_position = 0;
    s_open = 0;

    uint32_t idle_word = clp_make_word(
        CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0);
    if (!cl_gba_sio_transport_init_client(&s_transport, &s_client,
                                           idle_word, 0)) {
        return -2;
    }
    chislink_bridge_reinstall_transport();

    int online = 0;
    for (uint32_t attempt = 0; attempt < 8u; ++attempt) {
        if (cl_client_hello(&s_client)) {
            online = 1;
            break;
        }
        cl_gba_wait_vblank();
    }
    if (!online) {
        return -3;
    }
    if (cl_ram_map_init(&s_ram_map, &s_client) < 0 ||
        cl_storage_client_init(&s_storage, &s_client, s_storage_scratch,
                               sizeof(s_storage_scratch)) < 0) {
        return -4;
    }

    uint32_t launch_length = 0;
    int ret = cl_ram_map_get(&s_ram_map,
                             CLP_LAUNCH_MAP_KEY,
                             sizeof(CLP_LAUNCH_MAP_KEY) - 1u,
                             s_storage_scratch,
                             sizeof(s_storage_scratch),
                             &launch_length);
    if (ret < 0 || bridge_parse_launch(expected_type, launch_length) < 0) {
        return ret < 0 ? ret : -5;
    }
    ret = cl_storage_open(&s_storage, s_launch.path, CLP_OPEN_READ,
                          &s_rom_handle);
    if (ret < 0) {
        return -6;
    }
    s_open = 1u;
    return 0;
}

const chislink_launch_info_t *chislink_bridge_launch(void) {
    return s_open ? &s_launch : NULL;
}

#ifdef CHISLINK_GOOMBACOLOR
static void bridge_store_payload_word(void *ctx,
                                      uint32_t offset,
                                      uint32_t word,
                                      uint32_t valid_bytes) {
    uintptr_t address = (uintptr_t)ctx + offset;
    if ((address & 3u) == 0 && valid_bytes == 4u) {
        *(volatile uint32_t *)address = word;
        return;
    }

    for (uint32_t i = 0; i < valid_bytes;) {
        uintptr_t at = address + i;
        volatile uint16_t *half =
            (volatile uint16_t *)(at & ~(uintptr_t)1u);
        uint16_t value = *half;
        if (at & 1u) {
            value = (uint16_t)((value & 0x00ffu) |
                               ((word & 0xffu) << 8u));
            word >>= 8u;
            ++i;
        } else {
            value = (uint16_t)((value & 0xff00u) | (word & 0xffu));
            word >>= 8u;
            ++i;
            if (i < valid_bytes) {
                value = (uint16_t)((value & 0x00ffu) |
                                   ((word & 0xffu) << 8u));
                word >>= 8u;
                ++i;
            }
        }
        *half = value;
    }
}
#endif

static int bridge_read(uint32_t offset, void *dst, uint32_t length,
                       int use_word_writer) {
    if (!s_open || !dst || offset > s_launch.size ||
        length > s_launch.size - offset) {
        return -1;
    }
    if (s_rom_position != offset) {
        int ret = cl_storage_seek(&s_storage, s_rom_handle, offset);
        if (ret < 0) {
            bridge_abort();
        }
        s_rom_position = offset;
    }

    uint8_t *out = (uint8_t *)dst;
    uint32_t done = 0;
    while (done < length) {
        uint32_t chunk = length - done;
        if (chunk > CLP_FRAME_MAX_PAYLOAD_BYTES) {
            chunk = CLP_FRAME_MAX_PAYLOAD_BYTES;
        }
        uint32_t got = 0;
        int ret;
#ifdef CHISLINK_GOOMBACOLOR
        if (use_word_writer) {
            cl_payload_writer_t writer = {
                .store_word = bridge_store_payload_word,
                .ctx = out + done,
            };
            ret = cl_storage_read_with_writer(&s_storage, s_rom_handle,
                                              &writer, chunk, &got);
        } else
#else
        (void)use_word_writer;
#endif
        {
            ret = cl_storage_read(&s_storage, s_rom_handle, out + done,
                                  chunk, &got);
        }
        if (ret < 0 || got != chunk) {
            bridge_abort();
        }
        done += got;
        s_rom_position += got;
    }
    return 0;
}

int chislink_bridge_read(uint32_t offset, void *dst, uint32_t length) {
    return bridge_read(offset, dst, length, 0);
}

#ifdef CHISLINK_GOOMBACOLOR
int chislink_bridge_read_words(uint32_t offset, void *dst, uint32_t length) {
    return bridge_read(offset, dst, length, 1);
}
#endif

#ifdef CHISLINK_GOOMBACOLOR
static uint32_t bridge_path_hash(const char *path) {
    uint32_t hash = 2166136261u;
    while (path && *path) {
        hash ^= (uint8_t)*path++;
        hash *= 16777619u;
    }
    return hash;
}

static void bridge_build_save_path(void) {
    static const char prefix[] =
        "/sd/.chislink/emulators/goombacolor/";
    static const char hex[] = "0123456789abcdef";
    size_t at = 0;
    while (prefix[at] != '\0') {
        s_save_path[at] = prefix[at];
        ++at;
    }
    uint32_t hash = bridge_path_hash(s_launch.path);
    for (uint32_t shift = 28u;; shift -= 4u) {
        s_save_path[at++] = hex[(hash >> shift) & 0x0fu];
        if (shift == 0u) break;
    }
    memcpy(s_save_path + at, ".sav", 5u);
}

int chislink_bridge_save_open(void) {
    if (!s_open) return -1;
    if (s_save_handle) {
        (void)cl_storage_close(&s_storage, s_save_handle);
        s_save_handle = 0;
    }
    (void)cl_storage_mkdir(&s_storage, "/sd/.chislink");
    (void)cl_storage_mkdir(&s_storage, "/sd/.chislink/emulators");
    (void)cl_storage_mkdir(&s_storage,
                           "/sd/.chislink/emulators/goombacolor");
    bridge_build_save_path();
    return cl_storage_open(&s_storage, s_save_path,
                           CLP_OPEN_READ | CLP_OPEN_WRITE | CLP_OPEN_CREATE,
                           &s_save_handle);
}

int chislink_bridge_save_read(void *dst, uint32_t length,
                              uint32_t *out_length) {
    if (!s_save_handle || !dst || !out_length) return -1;
    if (cl_storage_seek(&s_storage, s_save_handle, 0) < 0) return -2;
    return cl_storage_read(&s_storage, s_save_handle, dst, length, out_length);
}

int chislink_bridge_save_write(uint32_t offset, const void *src,
                               uint32_t length) {
    if (!s_save_handle || (!src && length)) return -1;
    if (cl_storage_seek(&s_storage, s_save_handle, offset) < 0) return -2;
    uint32_t written = 0;
    int ret = cl_storage_write(&s_storage, s_save_handle, src, length,
                               &written);
    if (ret < 0 || written != length) return ret < 0 ? ret : -3;
    return cl_storage_flush(&s_storage, s_save_handle);
}

#endif

void chislink_bridge_close(void) {
#ifdef CHISLINK_GOOMBACOLOR
    if (s_save_handle) {
        (void)cl_storage_close(&s_storage, s_save_handle);
    }
    s_save_handle = 0;
#endif
    if (s_open) {
        (void)cl_storage_close(&s_storage, s_rom_handle);
    }
    s_open = 0;
    s_rom_handle = 0;
    s_rom_position = 0;
}
