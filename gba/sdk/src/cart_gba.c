#include "chislink/cart_gba.h"

#include "chislink/cart_file.h"
#include "chislink/cart_nor.h"
#include "chislink/gamedb.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct cl_cart_gba_ctx {
    const volatile uint16_t *rom16;
    uintptr_t save_base;
    volatile uint8_t *save8;
    uint32_t rom_size;
    uint32_t save_size;
    uint8_t save_type;
    uint8_t save_linear;
    cl_cart_gba_sram_bank_fn switch_sram_bank;
    void *sram_bank_user;
    cl_cart_gba_flash_bank_fn switch_flash_bank;
    cl_cart_gba_flash_erase_fn erase_flash;
    cl_cart_gba_flash_write_fn write_flash_byte;
    void *flash_user;
    uint8_t flash_erased_for_write;
    cl_cart_gba_eeprom_read_fn read_eeprom_unit;
    cl_cart_gba_eeprom_write_fn write_eeprom_unit;
    void *eeprom_user;
    uint8_t save_probe_valid;
    int save_probe_status;
    cl_cart_gba_save_probe_t save_probe;
} cl_cart_gba_ctx_t;

static cl_cart_gba_ctx_t g_ctx;

#define CL_GBA_REG_WAITCNT (*(volatile uint16_t *)(uintptr_t)0x04000204u)
#define CL_GBA_DMA3SAD (*(volatile uint32_t *)(uintptr_t)0x040000d4u)
#define CL_GBA_DMA3DAD (*(volatile uint32_t *)(uintptr_t)0x040000d8u)
#define CL_GBA_DMA3CNT (*(volatile uint32_t *)(uintptr_t)0x040000dcu)
#define CL_GBA_DMA_ENABLE 0x80000000u
#define CL_CART_GBA_EEPROM ((volatile uint16_t *)(uintptr_t)0x0dffff00u)
#define CL_CART_GBA_EEPROM_UNIT_SIZE 8u
#define CL_GBA_WAITCNT_EEPROM_MASK 0x0f00u
#define CL_GBA_WAITCNT_EEPROM_X3XX 0x0300u
#define CL_CART_GBA_EEPROM_READ_ATTEMPTS 8u
#define CL_CART_GBA_EEPROM_STABLE_READS 3u

#define CL_CART_GBA_BOOT_VECTOR_SEARCH_BYTES 0x2000u
#define CL_CART_GBA_SAVE_FLASH_ID_SST_39VF512 0xbfd4u
#define CL_CART_GBA_SAVE_FLASH_ID_ATMEL_AT29LV512 0x1f3du
#define CL_CART_GBA_SAVE_FLASH_ID_MACRONIX_MX29L512 0xc21cu
#define CL_CART_GBA_SAVE_FLASH_ID_PANASONIC_MN63F805MNP 0x321bu
#define CL_CART_GBA_SAVE_FLASH_ID_MACRONIX_MX29L010 0xc209u
#define CL_CART_GBA_SAVE_FLASH_ID_SANYO_LE26FV10N1TS 0x6213u
#define CL_CART_GBA_SAVE_FLASH_ID_UNLICENSED_SST49LF080A 0xbf5bu

static uint8_t rom_read8(const cl_cart_gba_ctx_t *ctx, uint32_t offset) {
    uint16_t half = ctx->rom16[offset >> 1u];
    return (uint8_t)(offset & 1u ? half >> 8u : half);
}

static void rom_read_bytes(const cl_cart_gba_ctx_t *ctx,
                           uint32_t offset,
                           void *dst,
                           uint32_t length) {
    uint8_t *out = (uint8_t *)dst;
    uint32_t pos = 0;

    if ((offset & 1u) && length) {
        out[pos++] = rom_read8(ctx, offset++);
        --length;
    }

    while (length >= 2u) {
        uint16_t half = ctx->rom16[offset >> 1u];
        out[pos++] = (uint8_t)half;
        out[pos++] = (uint8_t)(half >> 8u);
        offset += 2u;
        length -= 2u;
    }

    if (length) {
        out[pos] = rom_read8(ctx, offset);
    }
}

static uint16_t rom_read16(const cl_cart_gba_ctx_t *ctx, uint32_t offset) {
    return ctx->rom16[offset >> 1u];
}

static void rom_write16(const cl_cart_gba_ctx_t *ctx,
                        uint32_t offset,
                        uint16_t value) {
    if (offset + 1u >= ctx->rom_size) {
        return;
    }
    volatile uint16_t *rom16 = (volatile uint16_t *)(uintptr_t)ctx->rom16;
    rom16[offset >> 1u] = value;
    __asm__ volatile("nop");
}

static uint8_t save_read8(const cl_cart_gba_ctx_t *ctx, uint32_t offset) {
#ifdef CL_CART_GBA_HOST_SAVE_BUS
    extern uint8_t cl_cart_gba_host_save_read8(uintptr_t base,
                                               uint32_t offset);
    return cl_cart_gba_host_save_read8(ctx->save_base, offset);
#else
    return ((volatile uint8_t *)ctx->save_base)[offset];
#endif
}

static void save_write8(const cl_cart_gba_ctx_t *ctx,
                        uint32_t offset,
                        uint8_t value) {
#ifdef CL_CART_GBA_HOST_SAVE_BUS
    extern void cl_cart_gba_host_save_write8(uintptr_t base,
                                             uint32_t offset,
                                             uint8_t value);
    cl_cart_gba_host_save_write8(ctx->save_base, offset, value);
#else
    ((volatile uint8_t *)ctx->save_base)[offset] = value;
#endif
}

static uint32_t clamp_rom_size(uint32_t rom_size) {
    if (rom_size == 0 || rom_size > CL_CART_GBA_MAX_ROM_SIZE) {
        return CL_CART_GBA_MAX_ROM_SIZE;
    }
    return rom_size;
}

static uint8_t normalize_save_type(uint8_t save_type) {
    switch (save_type) {
    case CL_CART_SAVE_SRAM:
    case CL_CART_SAVE_FLASH:
    case CL_CART_SAVE_BATTERYLESS:
    case CL_CART_SAVE_EEPROM:
        return save_type;
    default:
        return CL_CART_SAVE_NONE;
    }
}

static uint8_t save_type_is_linear(uint8_t save_type) {
    return save_type == CL_CART_SAVE_SRAM || save_type == CL_CART_SAVE_FLASH ||
        save_type == CL_CART_SAVE_EEPROM;
}

static uint8_t save_size_is_file_accessible(uint8_t save_type,
                                            uint32_t save_size) {
    if (save_type == CL_CART_SAVE_SRAM) {
        return save_size <= CL_CART_GBA_MAX_LINEAR_SAVE_SIZE;
    }
    if (save_type == CL_CART_SAVE_FLASH) {
        return save_size == CL_CART_GBA_SRAM_BANK_SIZE ||
            save_size == CL_CART_GBA_MAX_LINEAR_SAVE_SIZE;
    }
    if (save_type == CL_CART_SAVE_EEPROM) {
        return save_size == 512u || save_size == 8192u;
    }
    return 0;
}

static void default_switch_sram_bank(uint8_t bank, void *user) {
    (void)user;
    volatile uint16_t *superchis = (volatile uint16_t *)0x09fffffeu;
    volatile uint16_t *bootleg = (volatile uint16_t *)0x09000000u;
    uint16_t command = (uint16_t)(0x0070u | ((uint16_t)bank << 3u));
    *superchis = 0xa55au;
    *superchis = 0xa55au;
    *superchis = command;
    *superchis = command;
    *bootleg = bank;
}

static void switch_sram_bank(const cl_cart_gba_ctx_t *ctx, uint8_t bank) {
    if (ctx->save_size > CL_CART_GBA_SRAM_BANK_SIZE &&
        ctx->switch_sram_bank) {
        ctx->switch_sram_bank(bank, ctx->sram_bank_user);
    }
}

static void probe_switch_sram_bank(const cl_cart_gba_ctx_t *ctx,
                                   uint8_t bank) {
    if (ctx->switch_sram_bank) {
        ctx->switch_sram_bank(bank, ctx->sram_bank_user);
    }
}

static void default_switch_flash_bank(uint8_t bank, void *user) {
    (void)user;
    volatile uint8_t *sram = (volatile uint8_t *)CL_CART_GBA_DEFAULT_SAVE_BASE;
    sram[0x5555u] = 0xaau;
    sram[0x2aaau] = 0x55u;
    sram[0x5555u] = 0xb0u;
    sram[0x0000u] = bank;
}

static int default_flash_erase(volatile uint8_t *base, void *user) {
    (void)user;
    base[0x5555u] = 0xaau;
    base[0x2aaau] = 0x55u;
    base[0x5555u] = 0x80u;
    base[0x5555u] = 0xaau;
    base[0x2aaau] = 0x55u;
    base[0x5555u] = 0x10u;
    while (base[0x5555u] != 0xffu) {
    }
    return 0;
}

static int default_flash_write_byte(volatile uint8_t *base,
                                    uint32_t offset,
                                    uint8_t value,
                                    void *user) {
    (void)user;
    base[0x5555u] = 0xaau;
    base[0x2aaau] = 0x55u;
    base[0x5555u] = 0xa0u;
    base[offset] = value;
    while (base[offset] != value) {
    }
    return 0;
}

static void switch_flash_bank(const cl_cart_gba_ctx_t *ctx, uint8_t bank) {
    if (ctx->save_size > CL_CART_GBA_SRAM_BANK_SIZE &&
        ctx->switch_flash_bank) {
        ctx->switch_flash_bank(bank, ctx->flash_user);
    }
}

static void eeprom_dma_send(uint16_t *bits, uint32_t count) {
    CL_GBA_DMA3SAD = (uint32_t)(uintptr_t)bits;
    CL_GBA_DMA3DAD = (uint32_t)(uintptr_t)CL_CART_GBA_EEPROM;
    CL_GBA_DMA3CNT = CL_GBA_DMA_ENABLE | count;
}

static void eeprom_dma_recv(uint16_t *bits, uint32_t count) {
    CL_GBA_DMA3SAD = (uint32_t)(uintptr_t)CL_CART_GBA_EEPROM;
    CL_GBA_DMA3DAD = (uint32_t)(uintptr_t)bits;
    CL_GBA_DMA3CNT = CL_GBA_DMA_ENABLE | count;
}

static uint16_t eeprom_waitcnt_enter(void) {
    uint16_t old = CL_GBA_REG_WAITCNT;
    CL_GBA_REG_WAITCNT =
        (uint16_t)((old & ~CL_GBA_WAITCNT_EEPROM_MASK) |
                   CL_GBA_WAITCNT_EEPROM_X3XX);
    return old;
}

static void eeprom_waitcnt_restore(uint16_t old) {
    CL_GBA_REG_WAITCNT = old;
}

static void eeprom_pack_read_bits(const uint16_t bits[68], uint8_t out[8]) {
    const uint16_t *in_bit = bits + 4u;
    for (uint8_t byte_index = 0; byte_index < 8u; ++byte_index) {
        uint8_t byte = (uint8_t)(*in_bit++ & 1u);
        for (uint8_t bit = 1; bit < 8u; ++bit) {
            byte <<= 1u;
            byte |= (uint8_t)(*in_bit++ & 1u);
        }
        out[byte_index] = byte;
    }
}

static void eeprom_prepare_read_bits(uint16_t bits[68],
                                     uint32_t unit,
                                     uint8_t address_bits) {
    bits[0] = 1u;
    bits[1] = 1u;
    for (uint8_t i = 0; i < address_bits; ++i) {
        bits[2u + i] = (uint16_t)((unit >> (address_bits - 1u - i)) & 1u);
    }
    bits[2u + address_bits] = 0u;
}

/* Shared DMA bit-buffer for EEPROM I/O — single GBA, no reentrancy. */
static uint16_t g_eeprom_bits[81];

static int default_eeprom_read_unit_once(uint32_t unit,
                                         uint8_t address_bits,
                                         uint8_t out[8]) {
    uint16_t *bits = g_eeprom_bits;
    eeprom_prepare_read_bits(bits, unit, address_bits);
    eeprom_dma_send(bits, (uint32_t)address_bits + 3u);
    eeprom_dma_recv(bits, 68u);
    eeprom_pack_read_bits(bits, out);
    return 0;
}

static int default_eeprom_read_unit(uint32_t unit,
                                    uint8_t address_bits,
                                    uint8_t out[8],
                                    void *user) {
    (void)user;
    if (!out || (address_bits != 6u && address_bits != 14u)) {
        return -1;
    }

    uint16_t old_waitcnt = eeprom_waitcnt_enter();
    uint8_t last[8] = {0};
    uint8_t stable_count = 0;
    uint8_t have_last = 0;
    int ret = -2;

    for (uint8_t attempt = 0;
         attempt < CL_CART_GBA_EEPROM_READ_ATTEMPTS;
         ++attempt) {
        uint8_t current[8];
        ret = default_eeprom_read_unit_once(unit, address_bits, current);
        if (ret < 0) {
            continue;
        }
        if (have_last && memcmp(last, current, 8u) == 0) {
            ++stable_count;
        } else {
            memcpy(last, current, 8u);
            stable_count = 1u;
            have_last = 1u;
        }
        if (stable_count >= CL_CART_GBA_EEPROM_STABLE_READS) {
            memcpy(out, current, 8u);
            eeprom_waitcnt_restore(old_waitcnt);
            return 0;
        }
    }

    if (have_last) {
        memcpy(out, last, 8u);
    }
    eeprom_waitcnt_restore(old_waitcnt);
    return ret < 0 ? ret : -2;
}

static int default_eeprom_write_unit(uint32_t unit,
                                     uint8_t address_bits,
                                     const uint8_t data[8],
                                     void *user) {
    (void)user;
    if (!data || (address_bits != 6u && address_bits != 14u)) {
        return -1;
    }
    uint16_t old_waitcnt = eeprom_waitcnt_enter();
    int result = -2;
    for (uint8_t attempt = 0; attempt < 3u; ++attempt) {
        uint16_t *bits = g_eeprom_bits;
        bits[0] = 1u;
        bits[1] = 0u;
        for (uint8_t i = 0; i < address_bits; ++i) {
            bits[2u + i] = (uint16_t)((unit >> (address_bits - 1u - i)) & 1u);
        }

        uint16_t *out_bit = bits + 2u + address_bits;
        for (uint8_t byte_index = 0; byte_index < 8u; ++byte_index) {
            uint8_t byte = data[byte_index];
            for (int bit = 7; bit >= 0; --bit) {
                *out_bit++ = (uint16_t)((byte >> bit) & 1u);
            }
        }
        bits[2u + address_bits + 64u] = 0u;
        eeprom_dma_send(bits, (uint32_t)2u + address_bits + 64u + 1u);

        uint8_t ready = 0;
        for (uint32_t i = 0; i < 0x10000u; ++i) {
            if ((*CL_CART_GBA_EEPROM & 1u) == 1u) {
                ready = 1u;
                break;
            }
        }
        if (!ready) {
            continue;
        }

        uint8_t verify[8];
        int ret = default_eeprom_read_unit(unit, address_bits, verify, user);
        if (ret == 0 && memcmp(verify, data, 8u) == 0) {
            result = 0;
            break;
        }
    }
    eeprom_waitcnt_restore(old_waitcnt);
    return result;
}

static void apply_save_config(cl_cart_gba_ctx_t *ctx,
                              uint8_t save_type,
                              uint32_t save_size,
                              uintptr_t save_base) {
    save_type = normalize_save_type(save_type);
    if (save_type == CL_CART_SAVE_NONE || save_size == 0) {
        ctx->save_base = save_base ? save_base : ctx->save_base;
        ctx->save8 = 0;
        ctx->save_size = 0;
        ctx->save_type = CL_CART_SAVE_NONE;
        ctx->save_linear = 0;
        ctx->flash_erased_for_write = 0;
        return;
    }

    if (!save_base) {
        save_base = ctx->save_base ? ctx->save_base : CL_CART_GBA_DEFAULT_SAVE_BASE;
    }
    ctx->save_base = save_base;
    ctx->save8 = (volatile uint8_t *)save_base;
    ctx->save_size = save_size;
    ctx->save_type = save_type;
    ctx->save_linear = save_type_is_linear(save_type) &&
        save_size_is_file_accessible(save_type, save_size);
    ctx->flash_erased_for_write = 0;
}

static uint32_t linear_bank_offset(uint32_t offset) {
    return offset & (CL_CART_GBA_SRAM_BANK_SIZE - 1u);
}

static uint32_t linear_bank_remaining(uint32_t offset) {
    return CL_CART_GBA_SRAM_BANK_SIZE - linear_bank_offset(offset);
}

static int linear_save_direct_read(const cl_cart_gba_ctx_t *ctx,
                                   uint32_t offset,
                                   uint32_t max_length,
                                   cl_direct_window_t *out_window) {
    if (!ctx || !ctx->save8 || !out_window ||
        !ctx->save_linear || ctx->save_type == CL_CART_SAVE_EEPROM ||
        offset >= ctx->save_size) {
        return 0;
    }
    uint32_t remaining = ctx->save_size - offset;
    uint32_t n = remaining < max_length ? remaining : max_length;
    uint32_t bank_left = linear_bank_remaining(offset);
    if (n > bank_left) {
        n = bank_left;
    }
    uint8_t bank = (uint8_t)(offset / CL_CART_GBA_SRAM_BANK_SIZE);
    uint32_t bank_offset = linear_bank_offset(offset);
    if (ctx->save_type == CL_CART_SAVE_FLASH) {
        switch_flash_bank(ctx, bank);
    } else {
        switch_sram_bank(ctx, bank);
    }
    out_window->data = ctx->save8 + bank_offset;
    out_window->length = n;
    out_window->access = CL_DIRECT_WINDOW_BYTES;
    return 0;
}

static int linear_save_release_direct(const cl_cart_gba_ctx_t *ctx) {
    if (!ctx || !ctx->save_linear || ctx->save_type == CL_CART_SAVE_EEPROM) {
        return 0;
    }
    if (ctx->save_type == CL_CART_SAVE_FLASH) {
        switch_flash_bank(ctx, 0);
    } else {
        switch_sram_bank(ctx, 0);
    }
    return 0;
}

static void linear_save_read(const cl_cart_gba_ctx_t *ctx,
                             uint32_t offset,
                             uint8_t *dst,
                             uint32_t length) {
    uint32_t copied = 0;
    while (copied < length) {
        uint32_t absolute = offset + copied;
        uint8_t bank = (uint8_t)(absolute / CL_CART_GBA_SRAM_BANK_SIZE);
        uint32_t bank_offset = linear_bank_offset(absolute);
        uint32_t n = length - copied;
        uint32_t bank_left = linear_bank_remaining(absolute);
        if (n > bank_left) {
            n = bank_left;
        }
        if (ctx->save_type == CL_CART_SAVE_FLASH) {
            switch_flash_bank(ctx, bank);
        } else {
            switch_sram_bank(ctx, bank);
        }
        for (uint32_t i = 0; i < n; ++i) {
            dst[copied + i] = ctx->save8[bank_offset + i];
        }
        copied += n;
    }
    if (ctx->save_type == CL_CART_SAVE_FLASH) {
        switch_flash_bank(ctx, 0);
    } else {
        switch_sram_bank(ctx, 0);
    }
}

static void linear_save_write(const cl_cart_gba_ctx_t *ctx,
                              uint32_t offset,
                              const uint8_t *src,
                              uint32_t length) {
    uint32_t copied = 0;
    while (copied < length) {
        uint32_t absolute = offset + copied;
        uint8_t bank = (uint8_t)(absolute / CL_CART_GBA_SRAM_BANK_SIZE);
        uint32_t bank_offset = linear_bank_offset(absolute);
        uint32_t n = length - copied;
        uint32_t bank_left = linear_bank_remaining(absolute);
        if (n > bank_left) {
            n = bank_left;
        }
        switch_sram_bank(ctx, bank);
        for (uint32_t i = 0; i < n; ++i) {
            ctx->save8[bank_offset + i] = src[copied + i];
        }
        copied += n;
    }
    switch_sram_bank(ctx, 0);
}

static int linear_flash_write(cl_cart_gba_ctx_t *ctx,
                              uint32_t offset,
                              const uint8_t *src,
                              uint32_t length) {
    if (!ctx->write_flash_byte || !ctx->erase_flash) {
        return -5;
    }
    if (!ctx->flash_erased_for_write) {
        if (offset != 0) {
            return -6;
        }
        int ret = ctx->erase_flash(ctx->save8, ctx->flash_user);
        if (ret < 0) {
            return ret;
        }
        ctx->flash_erased_for_write = 1;
    }

    uint32_t copied = 0;
    while (copied < length) {
        uint32_t absolute = offset + copied;
        uint8_t bank = (uint8_t)(absolute / CL_CART_GBA_SRAM_BANK_SIZE);
        uint32_t bank_offset = linear_bank_offset(absolute);
        uint32_t n = length - copied;
        uint32_t bank_left = linear_bank_remaining(absolute);
        if (n > bank_left) {
            n = bank_left;
        }
        switch_flash_bank(ctx, bank);
        for (uint32_t i = 0; i < n; ++i) {
            int ret = ctx->write_flash_byte(ctx->save8,
                                            bank_offset + i,
                                            src[copied + i],
                                            ctx->flash_user);
            if (ret < 0) {
                switch_flash_bank(ctx, 0);
                return ret;
            }
        }
        copied += n;
    }
    switch_flash_bank(ctx, 0);
    return 0;
}

static uint8_t eeprom_address_bits(uint32_t save_size) {
    return save_size == 512u ? 6u : 14u;
}

static int eeprom_read_bytes(cl_cart_gba_ctx_t *ctx,
                             uint32_t offset,
                             uint8_t *dst,
                             uint32_t length) {
    if (!ctx->read_eeprom_unit) {
        return -5;
    }
    uint8_t address_bits = eeprom_address_bits(ctx->save_size);
    uint32_t copied = 0;
    while (copied < length) {
        uint32_t absolute = offset + copied;
        uint32_t unit = absolute / CL_CART_GBA_EEPROM_UNIT_SIZE;
        uint32_t unit_offset = absolute & (CL_CART_GBA_EEPROM_UNIT_SIZE - 1u);
        uint32_t n = length - copied;
        uint32_t unit_left = CL_CART_GBA_EEPROM_UNIT_SIZE - unit_offset;
        if (n > unit_left) {
            n = unit_left;
        }

        uint8_t unit_data[8];
        int ret = ctx->read_eeprom_unit(unit, address_bits, unit_data,
                                        ctx->eeprom_user);
        if (ret < 0) {
            return ret;
        }
        memcpy(dst + copied, unit_data + unit_offset, n);
        copied += n;
    }
    return 0;
}

static int eeprom_write_bytes(cl_cart_gba_ctx_t *ctx,
                              uint32_t offset,
                              const uint8_t *src,
                              uint32_t length) {
    if (!ctx->read_eeprom_unit || !ctx->write_eeprom_unit) {
        return -5;
    }
    uint8_t address_bits = eeprom_address_bits(ctx->save_size);
    uint32_t copied = 0;
    while (copied < length) {
        uint32_t absolute = offset + copied;
        uint32_t unit = absolute / CL_CART_GBA_EEPROM_UNIT_SIZE;
        uint32_t unit_offset = absolute & (CL_CART_GBA_EEPROM_UNIT_SIZE - 1u);
        uint32_t n = length - copied;
        uint32_t unit_left = CL_CART_GBA_EEPROM_UNIT_SIZE - unit_offset;
        if (n > unit_left) {
            n = unit_left;
        }

        uint8_t unit_data[8];
        int ret = 0;
        if (n != CL_CART_GBA_EEPROM_UNIT_SIZE) {
            ret = ctx->read_eeprom_unit(unit, address_bits, unit_data,
                                        ctx->eeprom_user);
            if (ret < 0) {
                return ret;
            }
        }
        memcpy(unit_data + unit_offset, src + copied, n);
        ret = ctx->write_eeprom_unit(unit, address_bits, unit_data,
                                     ctx->eeprom_user);
        if (ret < 0) {
            return ret;
        }
        copied += n;
    }
    return 0;
}

static int map_gamedb_save_type(uint32_t db_type, uint8_t *out_type) {
    if (!out_type) {
        return -1;
    }
    switch (db_type) {
    case CL_GAME_SAVE_NONE:
        *out_type = CL_CART_SAVE_NONE;
        return 0;
    case CL_GAME_SAVE_SRAM:
        *out_type = CL_CART_SAVE_SRAM;
        return 0;
    case CL_GAME_SAVE_EEPROM:
        *out_type = CL_CART_SAVE_EEPROM;
        return 0;
    case CL_GAME_SAVE_FLASH:
        *out_type = CL_CART_SAVE_FLASH;
        return 0;
    default:
        *out_type = CL_CART_SAVE_UNKNOWN;
        return -2;
    }
}

static uint8_t save_flash_id_is_known(uint32_t flash_id) {
    switch (flash_id) {
    case CL_CART_GBA_SAVE_FLASH_ID_SST_39VF512:
    case CL_CART_GBA_SAVE_FLASH_ID_ATMEL_AT29LV512:
    case CL_CART_GBA_SAVE_FLASH_ID_MACRONIX_MX29L512:
    case CL_CART_GBA_SAVE_FLASH_ID_PANASONIC_MN63F805MNP:
    case CL_CART_GBA_SAVE_FLASH_ID_MACRONIX_MX29L010:
    case CL_CART_GBA_SAVE_FLASH_ID_SANYO_LE26FV10N1TS:
    case CL_CART_GBA_SAVE_FLASH_ID_UNLICENSED_SST49LF080A:
        return 1;
    default:
        return 0;
    }
}

static uint8_t save_flash_id_has_bank_switch(uint32_t flash_id) {
    return flash_id == CL_CART_GBA_SAVE_FLASH_ID_MACRONIX_MX29L010 ||
           flash_id == CL_CART_GBA_SAVE_FLASH_ID_SANYO_LE26FV10N1TS ||
           flash_id == CL_CART_GBA_SAVE_FLASH_ID_UNLICENSED_SST49LF080A;
}

static uint8_t probe_pattern(uint8_t original, uint8_t avoid) {
    static const uint8_t masks[] = {0xffu, 0xa5u, 0x5au, 0x3cu, 0xc3u};
    for (uint8_t i = 0; i < (uint8_t)(sizeof(masks) / sizeof(masks[0])); ++i) {
        uint8_t value = (uint8_t)(original ^ masks[i]);
        if (value != original && value != avoid) {
            return value;
        }
    }
    return (uint8_t)(avoid ^ 0xffu);
}

static int probe_sram_writable(const cl_cart_gba_ctx_t *ctx,
                               uint8_t *out_writable) {
    static const uint32_t offset_a = 0x0004u;
    static const uint32_t offset_b = 0x1ffcu;
    if (!out_writable) {
        return -1;
    }

    uint8_t original_a = save_read8(ctx, offset_a);
    uint8_t original_b = save_read8(ctx, offset_b);
    uint8_t pattern_a = probe_pattern(original_a, original_b);
    uint8_t pattern_b = probe_pattern(original_b, pattern_a);

    save_write8(ctx, offset_a, pattern_a);
    uint8_t read_a_first = save_read8(ctx, offset_a);
    save_write8(ctx, offset_b, pattern_b);
    uint8_t read_b = save_read8(ctx, offset_b);
    uint8_t read_a_second = save_read8(ctx, offset_a);

    uint8_t writable =
        read_a_first == pattern_a &&
        read_b == pattern_b &&
        read_a_second == pattern_a;

    save_write8(ctx, offset_b, original_b);
    save_write8(ctx, offset_a, original_a);

    if (save_read8(ctx, offset_a) != original_a ||
        save_read8(ctx, offset_b) != original_b) {
        return -3;
    }

    *out_writable = writable;
    return 0;
}

static int probe_sram_bank_switch(const cl_cart_gba_ctx_t *ctx,
                                  cl_cart_gba_save_probe_t *probe) {
    probe_switch_sram_bank(ctx, 0);
    uint8_t bank0 = save_read8(ctx, 0x0004u);
    probe_switch_sram_bank(ctx, 1);
    uint8_t bank1 = save_read8(ctx, 0x0004u);
    probe_switch_sram_bank(ctx, 0);
    if (bank0 != bank1) {
        probe->has_bank_switch = 1u;
        return 0;
    }

    probe_switch_sram_bank(ctx, 0);
    save_write8(ctx, 0x0004u, (uint8_t)~bank0);
    if (save_read8(ctx, 0x0004u) == bank0) {
        probe_switch_sram_bank(ctx, 0);
        return 0;
    }
    probe_switch_sram_bank(ctx, 1);
    if (save_read8(ctx, 0x0004u) == bank1) {
        probe->has_bank_switch = 1u;
    }
    probe_switch_sram_bank(ctx, 0);
    save_write8(ctx, 0x0004u, bank0);
    if (save_read8(ctx, 0x0004u) != bank0) {
        return -3;
    }
    return 0;
}

static uint16_t read_dmg_mid_amdnor(const cl_cart_gba_ctx_t *ctx) {
    save_write8(ctx, 0, 0xf0u);
    save_write8(ctx, 0xaaau, 0xaau);
    save_write8(ctx, 0x555u, 0x55u);
    save_write8(ctx, 0xaaau, 0x90u);
    uint16_t data = (uint16_t)(((uint16_t)save_read8(ctx, 0) << 8u) |
                               save_read8(ctx, 1u));
    save_write8(ctx, 0, 0xf0u);
    return data;
}

static uint16_t read_dmg_did_amdnor(const cl_cart_gba_ctx_t *ctx) {
    save_write8(ctx, 0, 0xf0u);
    save_write8(ctx, 0xaaau, 0xaau);
    save_write8(ctx, 0x555u, 0x55u);
    save_write8(ctx, 0xaaau, 0x90u);
    uint16_t data = (uint16_t)(((uint16_t)save_read8(ctx, 2u) << 8u) |
                               save_read8(ctx, 3u));
    save_write8(ctx, 0, 0xf0u);
    return data;
}

static uint32_t read_dmg_flash_id(const cl_cart_gba_ctx_t *ctx) {
    uint32_t ret = ((uint32_t)read_dmg_mid_amdnor(ctx) << 16u) |
                   read_dmg_did_amdnor(ctx);
    uint32_t romdata = ((uint32_t)save_read8(ctx, 0) << 16u) |
                       save_read8(ctx, 2u);
    if (ret == romdata &&
        !((ret & 0xffffu) == 0x7e7eu || (ret >> 16u) == 0xc2c2u)) {
        return 0;
    }
    return ret;
}

static uint8_t gba_header_checksum_valid(const cl_cart_gba_ctx_t *ctx) {
    if (!ctx || ctx->rom_size <= 0xbdu) {
        return 0;
    }
    uint8_t checksum = 0;
    for (uint32_t i = 0xa0u; i <= 0xbcu; ++i) {
        checksum = (uint8_t)(checksum - rom_read8(ctx, i));
    }
    checksum = (uint8_t)(checksum - 0x19u);
    return rom_read8(ctx, 0xbdu) == checksum;
}

static uint8_t check_batteryless_sram(const cl_cart_gba_ctx_t *ctx,
                                      cl_cart_gba_save_probe_t *probe) {
    static const char maniac_sig[] = "<3 from Maniac";
    uint32_t boot_low = rom_read16(ctx, 0x0000u);
    uint32_t boot_mid = rom_read16(ctx, 0x0002u);
    uint32_t boot_vector = (boot_low | (boot_mid << 16u)) & 0x00ffffffu;
    boot_vector = (boot_vector + 2u) << 2u;
    if (boot_vector >= ctx->rom_size ||
        boot_vector + CL_CART_GBA_BOOT_VECTOR_SEARCH_BYTES > ctx->rom_size) {
        return 0;
    }

    uint32_t search_end = boot_vector + CL_CART_GBA_BOOT_VECTOR_SEARCH_BYTES;
    uint32_t sig_len = (uint32_t)sizeof(maniac_sig) - 1u;
    for (uint32_t addr = boot_vector; addr + sig_len < search_end; addr += 2u) {
        uint8_t found = 1u;
        for (uint32_t i = 0; i < sig_len; ++i) {
            if (rom_read8(ctx, addr + i) != (uint8_t)maniac_sig[i]) {
                found = 0;
                break;
            }
        }
        if (!found) {
            continue;
        }

        uint32_t maniac_index = addr - boot_vector;
        uint32_t payload_size = rom_read16(ctx, addr + 0x0eu);
        if (!payload_size) {
            payload_size = 0x414u;
        }
        probe->batteryless_offset = maniac_index + boot_vector + 0x10u;
        if (probe->batteryless_offset < payload_size ||
            probe->batteryless_offset > ctx->rom_size) {
            return 0;
        }
        uint32_t payload_start = probe->batteryless_offset - payload_size;
        if (payload_start + 0x14u > ctx->rom_size) {
            return 0;
        }

        uint32_t ctrl_flag = rom_read16(ctx, payload_start + 0x4u) |
                             ((uint32_t)rom_read16(ctx, payload_start + 0x6u)
                              << 16u);
        uint32_t size_word = rom_read16(ctx, payload_start + 0x8u) |
                             ((uint32_t)rom_read16(ctx, payload_start + 0xau)
                              << 16u);
        probe->batteryless_size = size_word & ~0xfffu;
        probe->batteryless_write_buffer_size = size_word & 0xfffu;
        if (probe->batteryless_size > 0x70000u) {
            if (ctrl_flag == 0xf0u) {
                probe->batteryless_rts_size =
                    rom_read16(ctx, payload_start + 0xcu) |
                    ((uint32_t)rom_read16(ctx, payload_start + 0xeu) << 16u);
                probe->batteryless_write_buffer_size =
                    rom_read16(ctx, payload_start + 0x10u) |
                    ((uint32_t)rom_read16(ctx, payload_start + 0x12u) << 16u);
            }
            probe->is_batteryless_rts = 1u;
            return 0;
        }
        return probe->batteryless_size != 0;
    }
    return 0;
}

static int probe_save_hardware_uncached(cl_cart_gba_ctx_t *ctx,
                                        cl_cart_gba_save_probe_t *probe) {
    if (!ctx || !probe || !ctx->rom16 || !ctx->save_base) {
        return -1;
    }
    memset(probe, 0, sizeof(*probe));

    rom_write16(ctx, 0x1000002u, 0xc2u);
    rom_write16(ctx, 0x1000004u, 0x09u);

    uint8_t writable = 0;
    int ret = probe_sram_writable(ctx, &writable);
    if (ret < 0) {
        return ret;
    }
    if (writable) {
        ret = probe_sram_bank_switch(ctx, probe);
        if (ret < 0) {
            return ret;
        }
        if (check_batteryless_sram(ctx, probe)) {
            probe->save_type = CL_CART_SAVE_BATTERYLESS;
            probe->save_size = probe->batteryless_size;
        } else {
            probe->save_type = CL_CART_SAVE_SRAM;
            probe->save_size = probe->has_bank_switch ?
                CL_CART_GBA_MAX_LINEAR_SAVE_SIZE : 32768u;
        }
        return 0;
    }

    uint8_t temp5555 = save_read8(ctx, 0x5555u);
    uint8_t temp2aaa = save_read8(ctx, 0x2aaau);
    uint8_t temp0000 = save_read8(ctx, 0x0000u);
    save_write8(ctx, 0x5555u, 0xaau);
    save_write8(ctx, 0x2aaau, 0x55u);
    save_write8(ctx, 0x5555u, 0x90u);
    uint32_t flash_id = ((uint32_t)save_read8(ctx, 0x0000u) << 8u) |
                        save_read8(ctx, 0x0001u);
    save_write8(ctx, 0x5555u, 0xaau);
    save_write8(ctx, 0x2aaau, 0x55u);
    save_write8(ctx, 0x5555u, 0xf0u);
    __asm__ volatile("nop; nop; nop; nop");
    save_write8(ctx, 0x0000u, 0xf0u);

    if (save_flash_id_is_known(flash_id)) {
        probe->flash_id = flash_id;
        probe->has_bank_switch = save_flash_id_has_bank_switch(flash_id);
        probe->save_type = CL_CART_SAVE_FLASH;
        probe->save_size = probe->has_bank_switch ?
            CL_CART_GBA_MAX_LINEAR_SAVE_SIZE : CL_CART_GBA_SRAM_BANK_SIZE;
        return 0;
    }

    if (!gba_header_checksum_valid(ctx)) {
        flash_id = read_dmg_flash_id(ctx);
        if (flash_id) {
            probe->is_dmg_cart = 1u;
            probe->flash_id = flash_id;
            probe->save_type = CL_CART_SAVE_SRAM;
            probe->save_size = 0;
            return 0;
        }
    }

    save_write8(ctx, 0x5555u, temp5555);
    save_write8(ctx, 0x2aaau, temp2aaa);
    save_write8(ctx, 0x0000u, temp0000);
    probe->save_type = CL_CART_SAVE_EEPROM;
    probe->save_size = 8192u;
    return 0;
}

static void copy_printable(char *dst,
                           uint32_t dst_size,
                           const uint8_t *src,
                           uint32_t src_size) {
    uint32_t n = dst_size - 1u;
    if (n > src_size) {
        n = src_size;
    }
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t ch = src[i];
        dst[i] = ch >= 32u && ch <= 126u ? (char)ch : '\0';
        if (dst[i] == '\0') {
            for (uint32_t j = i + 1u; j < dst_size; ++j) {
                dst[j] = '\0';
            }
            return;
        }
    }
    dst[n] = '\0';
    while (n > 0 && dst[n - 1u] == ' ') {
        dst[--n] = '\0';
    }
}

static int gba_info(void *ctx, cl_cart_info_t *out_info) {
    cl_cart_gba_ctx_t *gba = (cl_cart_gba_ctx_t *)ctx;
    if (!gba || !out_info || !gba->rom16 || gba->rom_size == 0) {
        return -1;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->rom_size = gba->rom_size;
    out_info->save_size = gba->save_size;
    out_info->save_type = gba->save_type;
    if (gba->save_probe_valid && gba->save_probe_status == 0) {
        out_info->save_flash_id = gba->save_probe.flash_id;
        out_info->save_hw_offset = gba->save_probe.batteryless_offset;
        out_info->save_hw_size = gba->save_probe.batteryless_size;
        out_info->save_hw_write_buffer_size =
            gba->save_probe.batteryless_write_buffer_size;
        out_info->save_hw_rts_size = gba->save_probe.batteryless_rts_size;
        out_info->detected_save_type = gba->save_probe.save_type;
        out_info->save_has_bank_switch = gba->save_probe.has_bank_switch;
        out_info->is_dmg_cart = gba->save_probe.is_dmg_cart;
        out_info->is_batteryless_rts = gba->save_probe.is_batteryless_rts;
    }
    cl_cart_nor_probe_t nor;
    if (cl_cart_nor_probe(&nor) == 0) {
        out_info->flash_id = nor.flash_id;
        out_info->flash_size = nor.flash_size;
        out_info->program_block_size = nor.write_buffer_size ?
            nor.write_buffer_size : nor.sector_size;
    }
    out_info->can_program_rom = out_info->flash_id != 0 &&
        out_info->flash_size != 0;
    out_info->can_read_save = gba->save_linear;
    out_info->can_write_save = gba->save_linear &&
        ((gba->save_type == CL_CART_SAVE_FLASH &&
          gba->erase_flash && gba->write_flash_byte) ||
         (gba->save_type == CL_CART_SAVE_EEPROM &&
          gba->read_eeprom_unit && gba->write_eeprom_unit) ||
         gba->save_type == CL_CART_SAVE_SRAM);

    uint8_t field[16];
    rom_read_bytes(gba, 0xa0u, field, 12u);
    copy_printable(out_info->title, sizeof(out_info->title), field, 12u);
    rom_read_bytes(gba, 0xacu, field, 4u);
    copy_printable(out_info->game_code, sizeof(out_info->game_code), field, 4u);
    return 0;
}

static int gba_read(void *ctx,
                    cl_cart_file_kind_t kind,
                    uint64_t offset64,
                    void *dst,
                    uint32_t length,
                    uint32_t *out_length) {
    cl_cart_gba_ctx_t *gba = (cl_cart_gba_ctx_t *)ctx;
    if (!gba || !dst || !out_length || offset64 > UINT32_MAX) {
        return -1;
    }
    uint32_t offset = (uint32_t)offset64;

    if (kind == CL_CART_FILE_ROM) {
        if (offset >= gba->rom_size) {
            *out_length = 0;
            return 0;
        }
        uint32_t remaining = gba->rom_size - offset;
        uint32_t n = remaining < length ? remaining : length;
        rom_read_bytes(gba, offset, dst, n);
        *out_length = n;
        return 0;
    }

    if (kind != CL_CART_FILE_SAVE || !gba->save8 || !gba->save_size ||
        !gba->save_linear) {
        return -2;
    }
    if (offset >= gba->save_size) {
        *out_length = 0;
        return 0;
    }
    uint32_t remaining = gba->save_size - offset;
    uint32_t n = remaining < length ? remaining : length;
    if (gba->save_type == CL_CART_SAVE_EEPROM) {
        int ret = eeprom_read_bytes(gba, offset, (uint8_t *)dst, n);
        if (ret < 0) {
            *out_length = 0;
            return ret;
        }
    } else {
        linear_save_read(gba, offset, (uint8_t *)dst, n);
    }
    *out_length = n;
    return 0;
}

static int gba_direct_read(void *ctx,
                           cl_cart_file_kind_t kind,
                           uint64_t offset64,
                           uint32_t max_length,
                           cl_direct_window_t *out_window) {
    cl_cart_gba_ctx_t *gba = (cl_cart_gba_ctx_t *)ctx;
    if (!gba || !out_window || offset64 > UINT32_MAX) {
        return -1;
    }
    memset(out_window, 0, sizeof(*out_window));
    uint32_t offset = (uint32_t)offset64;
    if (max_length == 0) {
        return 0;
    }

    if (kind == CL_CART_FILE_ROM) {
        if (offset >= gba->rom_size) {
            return 0;
        }
        uint32_t remaining = gba->rom_size - offset;
        uint32_t n = remaining < max_length ? remaining : max_length;
        out_window->data = (const volatile uint8_t *)gba->rom16 + offset;
        out_window->length = n;
        out_window->access = CL_DIRECT_WINDOW_ROM16_LE;
        return 0;
    }

    if (kind != CL_CART_FILE_SAVE) {
        return -2;
    }
    return linear_save_direct_read(gba, offset, max_length, out_window);
}

static int gba_release_direct(void *ctx, cl_cart_file_kind_t kind) {
    cl_cart_gba_ctx_t *gba = (cl_cart_gba_ctx_t *)ctx;
    if (!gba) {
        return -1;
    }
    if (kind == CL_CART_FILE_SAVE) {
        return linear_save_release_direct(gba);
    }
    return 0;
}

static int gba_write(void *ctx,
                     cl_cart_file_kind_t kind,
                     uint64_t offset,
                     const void *src,
                     uint32_t length,
                     uint32_t *out_length) {
    cl_cart_gba_ctx_t *gba = (cl_cart_gba_ctx_t *)ctx;
    if (!gba || !src || !out_length || offset > UINT32_MAX) {
        if (out_length) {
            *out_length = 0;
        }
        return -1;
    }
    uint32_t off = (uint32_t)offset;

    if (kind == CL_CART_FILE_ROM) {
        return cl_cart_nor_write(off, src, length, out_length);
    }

    if (kind != CL_CART_FILE_SAVE || !gba->save8 || !gba->save_linear) {
        *out_length = 0;
        return -1;
    }

    if (off >= gba->save_size) {
        *out_length = 0;
        return 0;
    }
    uint32_t remaining = gba->save_size - off;
    uint32_t n = remaining < length ? remaining : length;
    if (gba->save_type == CL_CART_SAVE_EEPROM) {
        int ret = eeprom_write_bytes(gba, off, (const uint8_t *)src, n);
        if (ret < 0) {
            *out_length = 0;
            return ret;
        }
    } else if (gba->save_type == CL_CART_SAVE_FLASH) {
        int ret = linear_flash_write(gba, off, (const uint8_t *)src, n);
        if (ret < 0) {
            *out_length = 0;
            return ret;
        }
    } else {
        linear_save_write(gba, off, (const uint8_t *)src, n);
    }
    *out_length = n;
    return 0;
}

static int gba_flush(void *ctx, cl_cart_file_kind_t kind) {
    (void)ctx;
    if (kind == CL_CART_FILE_ROM) {
        return cl_cart_nor_flush();
    }
    return 0;
}

static int gba_set_size(void *ctx, cl_cart_file_kind_t kind, uint64_t size) {
    (void)ctx;
    if (kind == CL_CART_FILE_ROM) {
        return cl_cart_nor_set_write_size(size);
    }
    return 0;
}

int cl_cart_gba_install_driver(const cl_cart_gba_config_t *config) {
    uintptr_t base = CL_CART_GBA_DEFAULT_ROM_BASE;
    uint32_t size = CL_CART_GBA_MAX_ROM_SIZE;
    uintptr_t save_base = CL_CART_GBA_DEFAULT_SAVE_BASE;
    uint32_t save_size = 0;
    uint8_t save_type = CL_CART_SAVE_NONE;
    if (config) {
        if (config->rom_base) {
            base = (uintptr_t)config->rom_base;
        }
        size = clamp_rom_size(config->rom_size);
        if (config->save_base) {
            save_base = (uintptr_t)config->save_base;
        }
        save_type = normalize_save_type(config->save_type);
        save_size = save_type == CL_CART_SAVE_NONE ? 0 : config->save_size;
        g_ctx.switch_sram_bank = config->switch_sram_bank;
        g_ctx.sram_bank_user = config->sram_bank_user;
        g_ctx.switch_flash_bank = config->switch_flash_bank;
        g_ctx.erase_flash = config->erase_flash;
        g_ctx.write_flash_byte = config->write_flash_byte;
        g_ctx.flash_user = config->flash_user;
        g_ctx.read_eeprom_unit = config->read_eeprom_unit;
        g_ctx.write_eeprom_unit = config->write_eeprom_unit;
        g_ctx.eeprom_user = config->eeprom_user;
    } else {
        g_ctx.switch_sram_bank = default_switch_sram_bank;
        g_ctx.sram_bank_user = 0;
        g_ctx.switch_flash_bank = default_switch_flash_bank;
        g_ctx.erase_flash = default_flash_erase;
        g_ctx.write_flash_byte = default_flash_write_byte;
        g_ctx.flash_user = 0;
        g_ctx.read_eeprom_unit = default_eeprom_read_unit;
        g_ctx.write_eeprom_unit = default_eeprom_write_unit;
        g_ctx.eeprom_user = 0;
    }
    if (!g_ctx.switch_sram_bank) {
        g_ctx.switch_sram_bank = default_switch_sram_bank;
    }
    if (!g_ctx.switch_flash_bank) {
        g_ctx.switch_flash_bank = default_switch_flash_bank;
    }
    if (!g_ctx.erase_flash) {
        g_ctx.erase_flash = default_flash_erase;
    }
    if (!g_ctx.write_flash_byte) {
        g_ctx.write_flash_byte = default_flash_write_byte;
    }
    if (!g_ctx.read_eeprom_unit) {
        g_ctx.read_eeprom_unit = default_eeprom_read_unit;
    }
    if (!g_ctx.write_eeprom_unit) {
        g_ctx.write_eeprom_unit = default_eeprom_write_unit;
    }

    g_ctx.rom16 = (const volatile uint16_t *)base;
    g_ctx.rom_size = size;
    g_ctx.save_base = save_base;
    cl_cart_gba_invalidate_save_probe();
    apply_save_config(&g_ctx, save_type, save_size, save_base);

    cl_cart_driver_ops_t ops = {
        .info = gba_info,
        .read = gba_read,
        .direct_read = gba_direct_read,
        .release_direct = gba_release_direct,
        .write = gba_write,
        .flush = gba_flush,
        .set_size = gba_set_size,
        .ctx = &g_ctx,
    };
    return cl_cart_set_driver(&ops);
}

int cl_cart_gba_configure_save(uint8_t save_type,
                               uint32_t save_size,
                               volatile void *save_base) {
    if (!g_ctx.rom16) {
        return -1;
    }
    cl_cart_gba_invalidate_save_probe();
    apply_save_config(&g_ctx, save_type, save_size, (uintptr_t)save_base);
    return 0;
}

int cl_cart_gba_probe_save_hardware(cl_cart_gba_save_probe_t *out_probe) {
    if (!out_probe) {
        return -1;
    }
    if (g_ctx.save_probe_valid) {
        *out_probe = g_ctx.save_probe;
        return g_ctx.save_probe_status;
    }
    int ret = probe_save_hardware_uncached(&g_ctx, &g_ctx.save_probe);
    g_ctx.save_probe_status = ret;
    g_ctx.save_probe_valid = 1u;
    *out_probe = g_ctx.save_probe;
    return ret;
}

void cl_cart_gba_invalidate_save_probe(void) {
    g_ctx.save_probe_valid = 0;
    g_ctx.save_probe_status = 0;
    memset(&g_ctx.save_probe, 0, sizeof(g_ctx.save_probe));
}

static int read_exact_record(cl_file_t file,
                             uint64_t index,
                             uint32_t record_size,
                             uint8_t record[9]) {
    int ret = cl_file_seek(file, index * record_size);
    if (ret < 0) {
        return ret;
    }
    uint32_t read = 0;
    ret = cl_file_read(file, record, record_size, &read);
    if (ret < 0) {
        return ret;
    }
    return read == record_size ? 0 : -3;
}

int cl_cart_gba_configure_save_from_gamedb(const char *path) {
    if (!path) {
        path = CL_CART_GBA_DEFAULT_GAME_DB_PATH;
    }

    cl_cart_info_t cart;
    int ret = cl_cart_info(&cart);
    if (ret < 0) {
        return ret;
    }

    cl_file_stat_t stat;
    ret = cl_file_stat(path, &stat);
    if (ret < 0) {
        return ret;
    }
    if (stat.size < 5u || stat.size > UINT32_MAX) {
        return -2;
    }

    uint32_t record_size = 0;
    ret = cl_gamedb_record_size((uint32_t)stat.size, &record_size);
    if (ret < 0) {
        return ret;
    }

    cl_file_t file = 0;
    ret = cl_file_open(path, CLP_OPEN_READ, &file);
    if (ret < 0) {
        return ret;
    }

    uint64_t left = 0;
    uint64_t right = stat.size / record_size;
    while (left < right) {
        uint64_t mid = left + ((right - left) >> 1u);
        uint8_t record[9];
        ret = read_exact_record(file, mid, record_size, record);
        if (ret < 0) {
            (void)cl_file_close(file);
            return ret;
        }

        int cmp = memcmp(record, cart.game_code, 4u);
        if (cmp == 0) {
            cl_game_save_info_t save_info;
            ret = cl_gamedb_decode_record(record, record_size, &save_info);
            (void)cl_file_close(file);
            if (ret < 0) {
                return ret;
            }

            uint8_t cart_save_type = CL_CART_SAVE_NONE;
            ret = map_gamedb_save_type(save_info.type, &cart_save_type);
            if (ret < 0) {
                return ret;
            }
            return cl_cart_gba_configure_save(cart_save_type,
                                              save_info.size,
                                              0);
        }
        if (cmp < 0) {
            left = mid + 1u;
        } else {
            right = mid;
        }
    }

    (void)cl_file_close(file);
    return -4;
}
