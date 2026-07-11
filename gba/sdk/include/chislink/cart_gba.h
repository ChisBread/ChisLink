#ifndef CHISLINK_CART_GBA_H
#define CHISLINK_CART_GBA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CL_CART_GBA_DEFAULT_ROM_BASE ((uintptr_t)0x08000000u)
#define CL_CART_GBA_MAX_ROM_SIZE     0x02000000u
#define CL_CART_GBA_DEFAULT_SAVE_BASE ((uintptr_t)0x0e000000u)
#define CL_CART_GBA_SRAM_BANK_SIZE   0x00010000u
#define CL_CART_GBA_MAX_LINEAR_SAVE_SIZE 0x00020000u
#define CL_CART_GBA_DEFAULT_GAME_DB_PATH "/littlefs/db_AGB.gamedb"

typedef void (*cl_cart_gba_sram_bank_fn)(uint8_t bank, void *user);
typedef void (*cl_cart_gba_flash_bank_fn)(uint8_t bank, void *user);
typedef int (*cl_cart_gba_flash_erase_fn)(volatile uint8_t *base, void *user);
typedef int (*cl_cart_gba_flash_write_fn)(volatile uint8_t *base,
                                          uint32_t offset,
                                          uint8_t value,
                                          void *user);
typedef int (*cl_cart_gba_eeprom_read_fn)(uint32_t unit,
                                          uint8_t address_bits,
                                          uint8_t out[8],
                                          void *user);
typedef int (*cl_cart_gba_eeprom_write_fn)(uint32_t unit,
                                           uint8_t address_bits,
                                           const uint8_t data[8],
                                           void *user);

typedef struct cl_cart_gba_save_probe {
    uint32_t flash_id;
    uint32_t save_size;
    uint32_t batteryless_offset;
    uint32_t batteryless_size;
    uint32_t batteryless_write_buffer_size;
    uint32_t batteryless_rts_size;
    uint8_t save_type;
    uint8_t has_bank_switch;
    uint8_t is_dmg_cart;
    uint8_t is_batteryless_rts;
} cl_cart_gba_save_probe_t;

typedef struct cl_cart_gba_config {
    const void *rom_base;
    uint32_t rom_size;
    volatile void *save_base;
    uint32_t save_size;
    uint8_t save_type;
    cl_cart_gba_sram_bank_fn switch_sram_bank;
    void *sram_bank_user;
    cl_cart_gba_flash_bank_fn switch_flash_bank;
    cl_cart_gba_flash_erase_fn erase_flash;
    cl_cart_gba_flash_write_fn write_flash_byte;
    void *flash_user;
    cl_cart_gba_eeprom_read_fn read_eeprom_unit;
    cl_cart_gba_eeprom_write_fn write_eeprom_unit;
    void *eeprom_user;
} cl_cart_gba_config_t;

int cl_cart_gba_install_driver(const cl_cart_gba_config_t *config);
int cl_cart_gba_configure_save(uint8_t save_type,
                               uint32_t save_size,
                               volatile void *save_base);
int cl_cart_gba_configure_save_from_gamedb(const char *path);
int cl_cart_gba_probe_save_hardware(cl_cart_gba_save_probe_t *out_probe);
void cl_cart_gba_invalidate_save_probe(void);

#ifdef __cplusplus
}
#endif

#endif
