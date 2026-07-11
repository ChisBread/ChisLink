#ifndef CHISLINK_CART_FILE_H
#define CHISLINK_CART_FILE_H

#include <stdbool.h>
#include <stdint.h>

#include "chislink/file.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file cart_file.h
 * @brief GBA cartridge as a file backend — exposes ROM, save, and info
 *        through the unified cl_file_* interface under the /dev/cart/
 *        namespace.
 *
 * ## Virtual files
 * | Path              | Kind              | Read | Write |
 * |-------------------|-------------------|------|-------|
 * | `/dev/cart/rom`   | CL_CART_FILE_ROM  | yes  | if programmable |
 * | `/dev/cart/save`  | CL_CART_FILE_SAVE | yes  | if writable |
 * | `/dev/cart/info`  | CL_CART_FILE_INFO | yes  | no |
 *
 * ## Setup
 * 1. Populate a cl_cart_driver_ops_t with your hardware access functions.
 * 2. Call cl_cart_set_driver(&my_driver).
 * 3. Call cl_file_register_cart() to add /dev/cart/ to the file namespace.
 *
 * ## Memory ownership
 * - The driver ops struct is copied (cl_cart_set_driver stores by value).
 * - The driver's ctx pointer is stored as-is; the caller must keep the
 *   pointed-to context alive.
 * - cl_cart_info_t fields are zero-initialised before filling.
 */

#define CL_CART_ROM_PATH  CLP_CART_ROM_PATH    /**< "/dev/cart/rom" */
#define CL_CART_SAVE_PATH CLP_CART_SAVE_PATH   /**< "/dev/cart/save" */
#define CL_CART_INFO_PATH CLP_CART_INFO_PATH   /**< "/dev/cart/info" */
#define CL_CART_INFO_TITLE_BYTES 13u
#define CL_CART_INFO_GAME_CODE_BYTES 5u

/** Detected save hardware type. */
typedef enum cl_cart_save_type {
    CL_CART_SAVE_NONE = 0,           /**< No save hardware. */
    CL_CART_SAVE_SRAM = 1,           /**< SRAM (battery-backed). */
    CL_CART_SAVE_EEPROM = 2,         /**< EEPROM (4/64 kbit). */
    CL_CART_SAVE_FLASH = 3,          /**< Flash (512 kbit / 1 Mbit). */
    CL_CART_SAVE_BATTERYLESS = 4,    /**< Batteryless save (FRAM, etc.). */
    CL_CART_SAVE_UNKNOWN = 255,      /**< Could not determine save type. */
} cl_cart_save_type_t;

/** Cartridge metadata returned by the driver's info() callback. */
typedef struct cl_cart_info {
    uint32_t rom_size;               /**< ROM size in bytes. */
    uint32_t flash_id;               /**< Flash chip ID. */
    uint32_t flash_size;             /**< Flash chip size. */
    uint32_t program_block_size;     /**< Minimum program block (e.g. 4096). */
    uint32_t save_size;              /**< Save data size in bytes. */
    uint32_t save_flash_id;          /**< Save flash chip ID. */
    uint32_t save_hw_offset;         /**< Save hardware base offset. */
    uint32_t save_hw_size;           /**< Save hardware region size. */
    uint32_t save_hw_write_buffer_size; /**< Write buffer size for save chip. */
    uint32_t save_hw_rts_size;       /**< RTS save data size. */
    uint8_t save_type;               /**< cl_cart_save_type_t from game DB. */
    uint8_t detected_save_type;      /**< Hardware-detected save type. */
    uint8_t save_has_bank_switch;    /**< 1 if save uses bank switching. */
    uint8_t is_dmg_cart;             /**< 1 for DMG (Game Boy) cartridges. */
    uint8_t is_batteryless_rts;      /**< 1 if batteryless with RTS. */
    uint8_t can_program_rom;         /**< 1 if ROM is writable (flash cart). */
    uint8_t can_read_save;           /**< 1 if save can be read. */
    uint8_t can_write_save;          /**< 1 if save can be written. */
    char title[CL_CART_INFO_TITLE_BYTES];      /**< Game title (not NUL-terminated if full). */
    char game_code[CL_CART_INFO_GAME_CODE_BYTES]; /**< Game code (e.g. "AGBJ"). */
} cl_cart_info_t;

/** Identifies which virtual file is being accessed. */
typedef enum cl_cart_file_kind {
    CL_CART_FILE_ROM = 1,   /**< /dev/cart/rom */
    CL_CART_FILE_SAVE = 2,  /**< /dev/cart/save */
    CL_CART_FILE_INFO = 3,  /**< /dev/cart/info */
} cl_cart_file_kind_t;

/** Driver callbacks for cartridge hardware access.
 *
 *  At minimum, info() and read() are required for backing up saves/ROMs.
 *  Add write() and flush() for restoring saves or programming flash carts.
 *  Add direct_read() for zero-copy ROM streaming.
 *  Add set_size() for save trimming. */
typedef struct cl_cart_driver_ops {
    /** Read cartridge metadata.  Mandatory. */
    int (*info)(void *ctx, cl_cart_info_t *out_info);
    /** Read from ROM or save at the given byte offset. */
    int (*read)(void *ctx, cl_cart_file_kind_t kind, uint64_t offset,
                void *dst, uint32_t length, uint32_t *out_length);
    /** Zero-copy direct-read window (for ROM streaming).  Optional. */
    int (*direct_read)(void *ctx, cl_cart_file_kind_t kind, uint64_t offset,
                       uint32_t max_length,
                       cl_direct_window_t *out_window);
    /** Release a previously acquired direct window. */
    int (*release_direct)(void *ctx, cl_cart_file_kind_t kind);
    /** Write to ROM or save at the given byte offset.  Optional. */
    int (*write)(void *ctx, cl_cart_file_kind_t kind, uint64_t offset,
                 const void *src, uint32_t length, uint32_t *out_length);
    /** Flush buffered writes to hardware.  Optional. */
    int (*flush)(void *ctx, cl_cart_file_kind_t kind);
    /** Set the target size for save trimming.  Optional. */
    int (*set_size)(void *ctx, cl_cart_file_kind_t kind, uint64_t size);
    void *ctx;  /**< Opaque pointer passed to all callbacks. */
} cl_cart_driver_ops_t;

/** Install a cartridge driver.  Pass NULL to clear.
 *  Closes all open cart handles.  The driver struct is copied.
 *  @return 0 on success. */
int cl_cart_set_driver(const cl_cart_driver_ops_t *driver);

/** Register the cartridge file backend (/dev/cart/).
 *  Must call cl_cart_set_driver() first.
 *  @return 0 on success, negative on error. */
int cl_file_register_cart(void);

/** Convenience: stat /dev/cart/rom. */
int cl_cart_stat_rom(cl_file_stat_t *out_stat);

/** Convenience: stat /dev/cart/save. */
int cl_cart_stat_save(cl_file_stat_t *out_stat);

/** Read cartridge metadata directly (bypasses the file interface).
 *  @return 0 on success, negative on error. */
int cl_cart_info(cl_cart_info_t *out_info);

#ifdef __cplusplus
}
#endif

#endif
