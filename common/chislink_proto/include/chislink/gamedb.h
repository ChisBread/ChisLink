#ifndef CHISLINK_GAMEDB_H
#define CHISLINK_GAMEDB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cl_game_save_type {
    CL_GAME_SAVE_NONE = 0xffu,
    CL_GAME_SAVE_SRAM = 0u,
    CL_GAME_SAVE_EEPROM = 1u,
    CL_GAME_SAVE_FLASH = 2u,
} cl_game_save_type_t;

typedef struct cl_game_save_info {
    uint32_t type;
    uint32_t size;
} cl_game_save_info_t;

int cl_gamedb_record_size(uint32_t file_size, uint32_t *out_record_size);
int cl_gamedb_decode_record(const uint8_t *record,
                            uint32_t record_size,
                            cl_game_save_info_t *out_info);
int cl_gamedb_search_records(const uint8_t *records,
                             uint32_t file_size,
                             const char game_code[4],
                             cl_game_save_info_t *out_info);

#ifdef __cplusplus
}
#endif

#endif
