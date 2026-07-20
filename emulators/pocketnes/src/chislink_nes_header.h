#ifndef POCKETNES_CHISLINK_NES_HEADER_H
#define POCKETNES_CHISLINK_NES_HEADER_H

#include <stdint.h>

typedef struct chislink_nes_header {
    uint16_t mapper;
    uint8_t submapper;
    uint8_t nes20;
    uint8_t flags6;
    uint8_t flags7;
    uint32_t prg_size;
    uint32_t chr_size;
    uint32_t prg_offset;
    uint32_t chr_offset;
} chislink_nes_header_t;

enum {
    CHISLINK_NES_OK = 0,
    CHISLINK_NES_BAD_HEADER = -1,
    CHISLINK_NES_BAD_SIZE = -2,
    CHISLINK_NES_UNSUPPORTED_MAPPER = -3,
    CHISLINK_NES_UNSUPPORTED_NES20 = -4,
    CHISLINK_NES_UNSUPPORTED_SIZE = -5,
};

int chislink_nes_parse_header(const uint8_t header[16], uint32_t file_size,
                              chislink_nes_header_t *out);
int chislink_nes_mapper_supported(uint16_t mapper);

#endif
