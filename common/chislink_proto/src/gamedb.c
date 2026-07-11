#include "chislink/gamedb.h"

#include <string.h>

static uint32_t load_le32(const uint8_t *src) {
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8u) |
           ((uint32_t)src[2] << 16u) |
           ((uint32_t)src[3] << 24u);
}

static void save_type_from_short_code(uint8_t code,
                                      cl_game_save_info_t *out_info) {
    switch (code) {
    case 0:
        out_info->type = CL_GAME_SAVE_NONE;
        out_info->size = 0;
        break;
    case 1:
        out_info->type = CL_GAME_SAVE_EEPROM;
        out_info->size = 512;
        break;
    case 2:
        out_info->type = CL_GAME_SAVE_EEPROM;
        out_info->size = 8192;
        break;
    case 3:
        out_info->type = CL_GAME_SAVE_SRAM;
        out_info->size = 32768;
        break;
    case 4:
        out_info->type = CL_GAME_SAVE_FLASH;
        out_info->size = 65536;
        break;
    case 5:
        out_info->type = CL_GAME_SAVE_FLASH;
        out_info->size = 131072;
        break;
    case 6:
        out_info->type = CL_GAME_SAVE_SRAM;
        out_info->size = 1048576;
        break;
    case 7:
        out_info->type = CL_GAME_SAVE_SRAM;
        out_info->size = 65536;
        break;
    case 8:
        out_info->type = CL_GAME_SAVE_SRAM;
        out_info->size = 131072;
        break;
    default:
        out_info->type = CL_GAME_SAVE_NONE;
        out_info->size = 0;
        break;
    }
}

int cl_gamedb_record_size(uint32_t file_size, uint32_t *out_record_size) {
    if (!out_record_size || file_size < 5u) {
        return -1;
    }
    if ((file_size % 5u) == 0) {
        *out_record_size = 5u;
        return 0;
    }
    if ((file_size % 9u) == 0) {
        *out_record_size = 9u;
        return 0;
    }
    return -2;
}

int cl_gamedb_decode_record(const uint8_t *record,
                            uint32_t record_size,
                            cl_game_save_info_t *out_info) {
    if (!record || !out_info || (record_size != 5u && record_size != 9u)) {
        return -1;
    }
    save_type_from_short_code(record[4], out_info);
    if (record_size == 9u) {
        out_info->size = load_le32(record + 5u);
    }
    return 0;
}

int cl_gamedb_search_records(const uint8_t *records,
                             uint32_t file_size,
                             const char game_code[4],
                             cl_game_save_info_t *out_info) {
    if (!records || !game_code || !out_info) {
        return -1;
    }
    out_info->type = CL_GAME_SAVE_NONE;
    out_info->size = 0;

    uint32_t record_size = 0;
    int ret = cl_gamedb_record_size(file_size, &record_size);
    if (ret < 0) {
        return ret;
    }

    uint32_t count = file_size / record_size;
    uint32_t left = 0;
    uint32_t right = count;
    while (left < right) {
        uint32_t mid = left + ((right - left) >> 1u);
        const uint8_t *record = records + mid * record_size;
        int cmp = memcmp(record, game_code, 4u);
        if (cmp == 0) {
            return cl_gamedb_decode_record(record, record_size, out_info);
        }
        if (cmp < 0) {
            left = mid + 1u;
        } else {
            right = mid;
        }
    }
    return -3;
}
