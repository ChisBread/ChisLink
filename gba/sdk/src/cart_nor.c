#include "chislink/cart_nor.h"

#include <stdint.h>
#include <string.h>

#ifdef CL_CART_NOR_HOST_STUB
int cl_cart_nor_probe(cl_cart_nor_probe_t *out_probe) {
    if (!out_probe) {
        return -1;
    }
    memset(out_probe, 0, sizeof(*out_probe));
    return -2;
}

int cl_cart_nor_write(uint32_t offset,
                      const void *src,
                      uint32_t length,
                      uint32_t *out_length) {
    (void)offset;
    (void)src;
    (void)length;
    if (out_length) {
        *out_length = 0;
    }
    return -2;
}

int cl_cart_nor_write_stream(uint32_t offset,
                             const void *src,
                             uint32_t length,
                             uint32_t *out_length,
                             cl_cart_nor_progress_fn progress,
                             void *user) {
    (void)progress;
    (void)user;
    return cl_cart_nor_write(offset, src, length, out_length);
}

int cl_cart_nor_set_write_size(uint64_t size) {
    (void)size;
    return 0;
}

int cl_cart_nor_flush(void) {
    return 0;
}

void cl_cart_nor_invalidate(void) {
}
#else

#define CL_CART_NOR_ROM16 ((volatile uint16_t *)(uintptr_t)0x08000000u)

static uint8_t g_swap_d0d1;
static uint8_t g_is_intel;
static uint8_t g_probe_valid;
static int g_probe_status;
static cl_cart_nor_probe_t g_probe_cache;
static uint8_t g_write_active;
static uint32_t g_write_offset;
static uint32_t g_write_expected_size;
static uint8_t g_pre_erase_active;
static uint32_t g_pre_erased_addr;

static uint16_t swap_d0d1(uint16_t data) {
    return (uint16_t)((data & 0xfffcu) |
                      ((data & 0x0001u) << 1u) |
                      ((data & 0x0002u) >> 1u));
}

static uint16_t flash_read_raw(uint32_t addr) {
    return CL_CART_NOR_ROM16[addr >> 1u];
}

static void flash_write_raw(uint32_t addr, uint16_t data) {
    CL_CART_NOR_ROM16[addr >> 1u] = data;
    __asm__ volatile("nop");
}

static uint16_t flash_read_cmd(uint32_t addr) {
    uint16_t data = flash_read_raw(addr);
    return g_swap_d0d1 ? swap_d0d1(data) : data;
}

static void flash_write_cmd(uint32_t addr, uint16_t data) {
    flash_write_raw(addr, g_swap_d0d1 ? swap_d0d1(data) : data);
}

static void flash_reset(void) {
    if (g_is_intel) {
        flash_write_raw(0, 0x50u);
        flash_write_raw(0, 0xffu);
    } else {
        flash_write_cmd(0, 0xf0u);
    }
}

static uint32_t sector_size_at(const cl_cart_nor_probe_t *probe,
                               uint32_t addr) {
    if (!probe) {
        return 0;
    }
    if (probe->sector_region_count >= 2u) {
        for (uint8_t i = 0; i < probe->sector_region_count; ++i) {
            if (addr >= probe->sector_region_start[i] &&
                addr < probe->sector_region_end[i]) {
                return probe->sector_region_size[i];
            }
        }
    }
    return probe->sector_size;
}

static uint8_t is_sector_boundary(const cl_cart_nor_probe_t *probe,
                                  uint32_t addr) {
    uint32_t size = sector_size_at(probe, addr);
    if (!size) {
        return 0;
    }
    if (probe->sector_region_count >= 2u) {
        for (uint8_t i = 0; i < probe->sector_region_count; ++i) {
            if (addr >= probe->sector_region_start[i] &&
                addr < probe->sector_region_end[i]) {
                return ((addr - probe->sector_region_start[i]) % size) == 0;
            }
        }
    }
    return (addr % size) == 0;
}

static int wait_amd_erase(uint32_t addr) {
    for (;;) {
        uint16_t status1 = flash_read_cmd(addr);
        if (status1 & 0x80u) {
            uint16_t status2 = flash_read_cmd(addr);
            if (status2 & 0x80u) {
                break;
            }
        }
        if (status1 & 0x20u) {
            uint16_t status2 = flash_read_cmd(addr);
            if (status2 & 0x80u) {
                break;
            }
            flash_reset();
            return -1;
        }
        __asm__ volatile("nop");
    }
    flash_reset();
    return 0;
}

static int wait_amd_program(uint32_t addr, uint16_t expected) {
    for (;;) {
        uint16_t status1 = flash_read_cmd(addr);
        if ((status1 & 0x80u) == (expected & 0x80u)) {
            uint16_t status2 = flash_read_cmd(addr);
            if ((status2 & 0x80u) == (expected & 0x80u)) {
                break;
            }
        }
        if (status1 & 0x20u) {
            uint16_t status2 = flash_read_cmd(addr);
            if ((status2 & 0x80u) == (expected & 0x80u)) {
                break;
            }
            flash_reset();
            return -1;
        }
        __asm__ volatile("nop");
    }
    flash_reset();
    return 0;
}

static uint16_t wait_intel_ready(uint32_t addr, uint16_t command) {
    for (;;) {
        if (command) {
            flash_write_cmd(addr, command);
        }
        uint16_t status = flash_read_cmd(addr);
        if (status & 0x80u) {
            return status;
        }
        __asm__ volatile("nop");
    }
}

static int erase_sector(const cl_cart_nor_probe_t *probe, uint32_t addr) {
    (void)probe;
    if (g_is_intel) {
        flash_write_raw(0, 0x50u);
        flash_write_raw(0, 0xffu);
        flash_write_raw(addr, 0x60u);
        flash_write_raw(addr, 0xd0u);
        flash_write_raw(addr, 0x20u);
        flash_write_raw(addr, 0xd0u);
        uint16_t status = wait_intel_ready(addr, 0);
        flash_reset();
        return (status & 0x30u) ? -2 : 0;
    }
    flash_write_cmd(addr, 0xf0u);
    flash_write_cmd(0xaaau, 0xaau);
    flash_write_cmd(0x555u, 0x55u);
    flash_write_cmd(0xaaau, 0x80u);
    flash_write_cmd(0xaaau, 0xaau);
    flash_write_cmd(0x555u, 0x55u);
    flash_write_cmd(addr, 0x30u);
    return wait_amd_erase(addr);
}

static void pre_erase_sector(const cl_cart_nor_probe_t *probe, uint32_t addr) {
    (void)probe;
    if (g_is_intel) {
        flash_write_raw(0, 0x50u);
        flash_write_raw(0, 0xffu);
        flash_write_raw(addr, 0x60u);
        flash_write_raw(addr, 0xd0u);
        flash_write_raw(addr, 0x20u);
        flash_write_raw(addr, 0xd0u);
        return;
    }
    flash_write_cmd(addr, 0xf0u);
    flash_write_cmd(0xaaau, 0xaau);
    flash_write_cmd(0x555u, 0x55u);
    flash_write_cmd(0xaaau, 0x80u);
    flash_write_cmd(0xaaau, 0xaau);
    flash_write_cmd(0x555u, 0x55u);
    flash_write_cmd(addr, 0x30u);
}

static int wait_pre_erased(uint32_t addr) {
    int ret = 0;
    if (g_is_intel) {
        uint16_t status = wait_intel_ready(addr, 0);
        ret = (status & 0x30u) ? -2 : 0;
    } else {
        ret = wait_amd_erase(addr);
    }
    g_pre_erase_active = 0;
    g_pre_erased_addr = 0;
    return ret;
}

static uint16_t load_le16_or_ff(const uint8_t *src,
                                uint32_t length,
                                uint32_t offset) {
    uint16_t lo = offset < length ? src[offset] : 0xffu;
    uint16_t hi = offset + 1u < length ? src[offset + 1u] : 0xffu;
    return (uint16_t)(lo | (hi << 8u));
}

static int program_word(uint32_t addr, uint16_t data) {
    if (g_is_intel) {
        flash_write_cmd(0, 0x70u);
        flash_write_cmd(0, 0x10u);
        flash_write_raw(addr, data);
        uint16_t status = wait_intel_ready(addr, 0);
        flash_reset();
        return (status & 0x10u) ? -2 : 0;
    }
    flash_write_cmd(0xaaau, 0xaau);
    flash_write_cmd(0x555u, 0x55u);
    flash_write_cmd(0xaaau, 0xa0u);
    flash_write_raw(addr, data);
    return wait_amd_program(addr, data);
}

static int program_buffer(uint32_t addr,
                          const uint8_t *src,
                          uint32_t length,
                          uint32_t write_buffer_size) {
    uint32_t words = write_buffer_size >> 1u;
    if (!words || (write_buffer_size & 1u)) {
        return -1;
    }

    if (g_is_intel) {
        uint32_t flash_id = g_probe_cache.flash_id;
        if ((flash_id & 0xffffu) != 0x88b0u) {
            (void)wait_intel_ready(addr, 0xe8u);
        } else {
            (void)wait_intel_ready(addr, 0xe9u);
        }
        flash_write_cmd(addr, (uint16_t)(words - 1u));
    } else {
        flash_write_cmd(0xaaau, 0xaau);
        flash_write_cmd(0x555u, 0x55u);
        flash_write_cmd(addr, 0x25u);
        flash_write_cmd(addr, (uint16_t)(words - 1u));
    }

    for (uint32_t i = 0; i < write_buffer_size; i += 2u) {
        flash_write_raw(addr + i, load_le16_or_ff(src, length, i));
    }

    if (g_is_intel) {
        flash_write_cmd(addr, 0xd0u);
        uint16_t status = wait_intel_ready(addr, 0x70u);
        flash_reset();
        return (status & 0x10u) ? -2 : 0;
    }

    flash_write_cmd(addr, 0x29u);
    uint32_t last = addr + write_buffer_size - 2u;
    uint16_t expected = load_le16_or_ff(src, length, write_buffer_size - 2u);
    return wait_amd_program(last, expected);
}

static uint32_t choose_write_unit(const cl_cart_nor_probe_t *probe,
                                  uint32_t offset,
                                  uint32_t remaining) {
    uint32_t unit = probe->write_buffer_size;
    if (!unit || (unit & 1u) || (offset % unit) != 0 || remaining < unit) {
        unit = 2u;
    }
    return unit;
}

static void maybe_pre_erase_next_sector(const cl_cart_nor_probe_t *probe,
                                        uint32_t next_addr) {
    if (g_write_expected_size != 0 &&
        next_addr < g_write_expected_size &&
        next_addr < probe->flash_size &&
        is_sector_boundary(probe, next_addr) &&
        !g_pre_erase_active) {
        g_pre_erase_active = 1u;
        g_pre_erased_addr = next_addr;
        pre_erase_sector(probe, next_addr);
    }
}

static void clear_probe(cl_cart_nor_probe_t *probe) {
    memset(probe, 0, sizeof(*probe));
}

static int check_cfi_signature(void) {
    uint16_t q = flash_read_raw(0x20u);
    uint16_t r = flash_read_raw(0x22u);
    uint16_t y = flash_read_raw(0x24u);
    if (q == 0x51u && r == 0x52u && y == 0x59u) {
        g_swap_d0d1 = 0;
        return 1;
    }
    if (swap_d0d1(q) == 0x51u &&
        swap_d0d1(r) == 0x52u &&
        swap_d0d1(y) == 0x59u) {
        g_swap_d0d1 = 1;
        return 1;
    }
    return 0;
}

static int enter_cfi(uint8_t intel) {
    if (intel) {
        flash_write_raw(0, 0x50u);
        flash_write_raw(0, 0xffu);
    } else {
        flash_write_raw(0, 0xf0u);
    }
    flash_write_raw(0xaau, 0x98u);
    if (check_cfi_signature()) {
        return 1;
    }
    if (intel) {
        flash_write_raw(0, 0x50u);
        flash_write_raw(0, 0xffu);
    } else {
        flash_write_raw(0, 0xf0u);
    }
    flash_write_raw(0x00u, 0x98u);
    return check_cfi_signature();
}

static uint16_t get_mid_amdnor(void) {
    flash_write_cmd(0, 0xf0u);
    flash_write_cmd(0xaaau, 0xaau);
    flash_write_cmd(0x555u, 0x55u);
    flash_write_cmd(0xaaau, 0x90u);
    uint16_t data = flash_read_cmd(0);
    flash_write_cmd(0, 0xf0u);
    return data;
}

static uint16_t get_did_amdnor(void) {
    flash_write_cmd(0, 0xf0u);
    flash_write_cmd(0xaaau, 0xaau);
    flash_write_cmd(0x555u, 0x55u);
    flash_write_cmd(0xaaau, 0x90u);
    uint16_t data = flash_read_cmd(2u);
    flash_write_cmd(0, 0xf0u);
    return data;
}

static uint16_t get_mid_intel(void) {
    flash_write_cmd(0, 0x50u);
    flash_write_cmd(0, 0xffu);
    flash_write_cmd(0, 0x90u);
    uint16_t data = flash_read_cmd(0);
    flash_write_cmd(0, 0x50u);
    flash_write_cmd(0, 0xffu);
    return data;
}

static uint16_t get_did_intel(void) {
    flash_write_cmd(0, 0x50u);
    flash_write_cmd(0, 0xffu);
    flash_write_cmd(0, 0x90u);
    uint16_t data = flash_read_cmd(2u);
    flash_write_cmd(0, 0x50u);
    flash_write_cmd(0, 0xffu);
    return data;
}

static uint32_t get_flash_id_auto(void) {
    g_swap_d0d1 = 0;
    g_is_intel = 0;
    (void)enter_cfi(0);
    uint32_t ret = ((uint32_t)get_mid_amdnor() << 16u) | get_did_amdnor();
    uint32_t romdata = ((uint32_t)flash_read_cmd(0) << 16u) |
                       flash_read_cmd(2u);
    if (ret == romdata && (ret & 0xffffu) != 0x227eu) {
        g_swap_d0d1 = 0;
        (void)enter_cfi(1);
        romdata = ((uint32_t)flash_read_cmd(0) << 16u) |
                  flash_read_cmd(2u);
        ret = ((uint32_t)get_mid_intel() << 16u) | get_did_intel();
        if (ret == romdata) {
            flash_reset();
            return 0;
        }
        g_is_intel = 1;
    }
    flash_reset();
    return ret;
}

static int read_cfi(cl_cart_nor_probe_t *probe) {
    flash_reset();
    flash_write_cmd(0xaau, 0x98u);

    uint16_t q = flash_read_cmd(0x20u);
    uint16_t r = flash_read_cmd(0x22u);
    uint16_t y = flash_read_cmd(0x24u);
    if (q != 0x51u || r != 0x52u || y != 0x59u) {
        flash_reset();
        flash_write_cmd(0x00u, 0x98u);
        q = flash_read_cmd(0x20u);
        r = flash_read_cmd(0x22u);
        y = flash_read_cmd(0x24u);
    }
    if (q != 0x51u || r != 0x52u || y != 0x59u) {
        flash_reset();
        return -1;
    }

    uint16_t device_exp = flash_read_cmd(0x4eu);
    if (device_exp < 32u) {
        probe->flash_size = 1u << device_exp;
    }

    uint16_t buffer_low = flash_read_cmd(0x54u);
    uint16_t buffer_high = flash_read_cmd(0x56u);
    uint16_t buffer_exp = (uint16_t)((buffer_high << 8u) | buffer_low);
    if (buffer_exp > 1u &&
        flash_read_cmd(0x40u) > 0u &&
        flash_read_cmd(0x40u) < 0xffu &&
        buffer_exp < 32u) {
        probe->write_buffer_size = 1u << buffer_exp;
    }

    uint32_t sector_size = ((uint32_t)flash_read_cmd(0x60u) << 8u) |
                           flash_read_cmd(0x5eu);
    probe->sector_size = sector_size << 8u;

    uint32_t region_count = flash_read_cmd(0x58u);
    if (region_count > 4u) {
        region_count = 4u;
    }

    uint32_t region_blocks[4] = {0};
    uint32_t region_sizes[4] = {0};
    uint32_t total_size = 0;
    for (uint32_t i = 0; i < region_count; ++i) {
        uint32_t entry = 0x5au + i * 8u;
        uint32_t block_count = ((uint32_t)flash_read_cmd(entry + 2u) << 8u) |
                               flash_read_cmd(entry);
        uint32_t block_size = ((uint32_t)flash_read_cmd(entry + 6u) << 8u) |
                              flash_read_cmd(entry + 4u);
        block_count += 1u;
        block_size <<= 8u;
        region_blocks[i] = block_count;
        region_sizes[i] = block_size;
        total_size += block_count * block_size;
    }

    uint32_t pri_address = (uint32_t)flash_read_cmd(0x2au) |
                           ((uint32_t)flash_read_cmd(0x2cu) << 8u);
    pri_address *= 2u;
    if (pri_address + 0x3cu >= 0x400u) {
        pri_address = 0x80u;
    }
    if (flash_read_cmd(pri_address) == 'P' &&
        flash_read_cmd(pri_address + 2u) == 'R' &&
        flash_read_cmd(pri_address + 4u) == 'I' &&
        flash_read_cmd(pri_address + 0x1eu) == 0x03u) {
        probe->sector_region_reversed = 1u;
    }

    probe->sector_region_count = (uint8_t)region_count;
    if (region_count) {
        if (probe->sector_region_reversed) {
            uint32_t current = total_size;
            for (uint32_t i = 0; i < region_count; ++i) {
                uint32_t size = region_blocks[i] * region_sizes[i];
                current -= size;
                uint32_t dst = region_count - 1u - i;
                probe->sector_region_start[dst] = current;
                probe->sector_region_end[dst] = current + size;
                probe->sector_region_size[dst] = region_sizes[i];
            }
        } else {
            uint32_t current = 0;
            for (uint32_t i = 0; i < region_count; ++i) {
                uint32_t size = region_blocks[i] * region_sizes[i];
                probe->sector_region_start[i] = current;
                probe->sector_region_end[i] = current + size;
                probe->sector_region_size[i] = region_sizes[i];
                current += size;
            }
        }
    }

    probe->cfi_valid = 1u;
    flash_reset();
    return 0;
}

int cl_cart_nor_probe(cl_cart_nor_probe_t *out_probe) {
    if (!out_probe) {
        return -1;
    }
    if (g_probe_valid) {
        *out_probe = g_probe_cache;
        return g_probe_status;
    }
    cl_cart_nor_probe_t probe;
    clear_probe(&probe);
    probe.flash_id = get_flash_id_auto();
    probe.is_intel = g_is_intel;
    probe.swap_d0d1 = g_swap_d0d1;
    if (probe.flash_id) {
        (void)read_cfi(&probe);
    }
    g_probe_cache = probe;
    g_probe_status = probe.flash_id ? 0 : -2;
    g_probe_valid = 1u;
    *out_probe = probe;
    return g_probe_status;
}

void cl_cart_nor_invalidate(void) {
    g_probe_valid = 0;
    g_probe_status = 0;
    g_write_active = 0;
    g_write_offset = 0;
    g_write_expected_size = 0;
    g_pre_erase_active = 0;
    g_pre_erased_addr = 0;
    clear_probe(&g_probe_cache);
}

int cl_cart_nor_set_write_size(uint64_t size) {
    if (size > UINT32_MAX) {
        return -1;
    }
    g_write_expected_size = (uint32_t)size;
    g_write_active = 0;
    g_write_offset = 0;
    g_pre_erase_active = 0;
    g_pre_erased_addr = 0;
    if (g_write_expected_size == 0) {
        return 0;
    }

    cl_cart_nor_probe_t probe;
    int ret = cl_cart_nor_probe(&probe);
    if (ret < 0) {
        return ret;
    }
    if (!probe.flash_id || !probe.flash_size ||
        (!probe.sector_size && probe.sector_region_count == 0)) {
        return -2;
    }
    if (is_sector_boundary(&probe, 0)) {
        g_pre_erase_active = 1u;
        g_pre_erased_addr = 0;
        pre_erase_sector(&probe, 0);
    }
    return 0;
}

int cl_cart_nor_write_stream(uint32_t offset,
                             const void *src,
                             uint32_t length,
                             uint32_t *out_length,
                             cl_cart_nor_progress_fn progress,
                             void *user) {
    if (!src || !out_length) {
        return -1;
    }
    *out_length = 0;
    if (length == 0) {
        return 0;
    }

    cl_cart_nor_probe_t probe;
    int ret = cl_cart_nor_probe(&probe);
    if (ret < 0) {
        return ret;
    }
    if (!probe.flash_id || !probe.flash_size || offset >= probe.flash_size) {
        return -2;
    }
    if (!probe.sector_size && probe.sector_region_count == 0) {
        return -3;
    }
    if (!g_write_active) {
        if (offset != 0) {
            return -4;
        }
        g_write_active = 1u;
        g_write_offset = 0;
    }
    if ((offset & 1u) || offset != g_write_offset) {
        return -5;
    }

    uint32_t remaining_flash = probe.flash_size - offset;
    uint32_t n = remaining_flash < length ? remaining_flash : length;
    if (g_write_expected_size != 0) {
        if (offset >= g_write_expected_size) {
            *out_length = 0;
            return 0;
        }
        if (offset + n > g_write_expected_size) {
            n = g_write_expected_size - offset;
        }
    }
    const uint8_t *data = (const uint8_t *)src;
    uint32_t done = 0;
    while (done < n) {
        uint32_t addr = offset + done;
        if (is_sector_boundary(&probe, addr)) {
            if (g_pre_erase_active && g_pre_erased_addr == addr) {
                ret = wait_pre_erased(addr);
            } else {
                ret = erase_sector(&probe, addr);
            }
            if (ret < 0) {
                return ret;
            }
        }

        uint32_t unit = choose_write_unit(&probe, addr, n - done);
        uint32_t logical_unit = unit;
        if (logical_unit > n - done) {
            logical_unit = n - done;
        }
        if (unit == 2u) {
            uint16_t word = load_le16_or_ff(data + done, n - done, 0);
            ret = program_word(addr, word);
        } else {
            ret = program_buffer(addr, data + done, n - done, unit);
        }
        if (ret < 0) {
            return ret;
        }
        done += logical_unit;
        if (done >= n) {
            maybe_pre_erase_next_sector(&probe, offset + done);
        }
        if (progress) {
            progress(logical_unit, user);
        }
    }

    g_write_offset += done;
    if (done != 0 && g_write_expected_size != 0) {
        maybe_pre_erase_next_sector(&probe, g_write_offset);
    }
    *out_length = done;
    return 0;
}

int cl_cart_nor_write(uint32_t offset,
                      const void *src,
                      uint32_t length,
                      uint32_t *out_length) {
    return cl_cart_nor_write_stream(offset, src, length, out_length, 0, 0);
}

int cl_cart_nor_flush(void) {
    int ret = 0;
    if (g_pre_erase_active) {
        ret = wait_pre_erased(g_pre_erased_addr);
    }
    flash_reset();
    g_write_active = 0;
    g_write_offset = 0;
    g_write_expected_size = 0;
    g_pre_erase_active = 0;
    g_pre_erased_addr = 0;
    return ret;
}
#endif
