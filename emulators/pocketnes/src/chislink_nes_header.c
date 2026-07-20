#include "chislink_nes_header.h"

#include <stddef.h>

static const uint8_t supported_mappers[] = {
    0, 1, 2, 3, 4, 5, 7, 9, 10, 11, 15, 16, 17, 18, 19, 21, 22, 23,
    24, 25, 26, 30, 32, 33, 34, 40, 42, 64, 65, 66, 67, 68, 69, 70,
    71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 85, 86, 87, 88, 92, 93,
    94, 97, 99, 105, 118, 119, 140, 151, 152, 158, 163, 178, 180,
    184, 187, 206, 218, 228, 232, 245, 249, 252, 254,
};

int chislink_nes_mapper_supported(uint16_t mapper) {
    for (size_t i = 0; i < sizeof(supported_mappers); ++i) {
        if (supported_mappers[i] == mapper) {
            return 1;
        }
    }
    return 0;
}

int chislink_nes_parse_header(const uint8_t header[16], uint32_t file_size,
                              chislink_nes_header_t *out) {
    if (!header || !out || header[0] != 'N' || header[1] != 'E' ||
        header[2] != 'S' || header[3] != 0x1a) {
        return CHISLINK_NES_BAD_HEADER;
    }

    uint8_t flags6 = header[6];
    uint8_t flags7 = header[7];
    uint8_t nes20 = (flags7 & 0x0cu) == 0x08u;
    if (!nes20 && (flags7 & 0x0eu) != 0) {
        flags7 = 0;
    }

    uint16_t mapper = (uint16_t)(flags6 >> 4u) | (flags7 & 0xf0u);
    uint8_t submapper = 0;
    if (nes20) {
        mapper |= (uint16_t)(header[8] & 0x0fu) << 8u;
        submapper = header[8] >> 4u;
        if (mapper > 255u || submapper != 0 ||
            (header[9] & 0x0fu) != 0 || (header[9] >> 4u) != 0) {
            return CHISLINK_NES_UNSUPPORTED_NES20;
        }
    }
    if (!chislink_nes_mapper_supported(mapper)) {
        return CHISLINK_NES_UNSUPPORTED_MAPPER;
    }
    if (header[5] > 128u) {
        return CHISLINK_NES_UNSUPPORTED_SIZE;
    }

    uint32_t prg_size = (uint32_t)header[4] * 16u * 1024u;
    uint32_t chr_size = (uint32_t)header[5] * 8u * 1024u;
    uint32_t prg_offset = 16u + ((flags6 & 0x04u) ? 512u : 0u);
    if (prg_size == 0 || prg_offset > file_size ||
        prg_size > file_size - prg_offset ||
        chr_size > file_size - prg_offset - prg_size) {
        return CHISLINK_NES_BAD_SIZE;
    }

    out->mapper = mapper;
    out->submapper = submapper;
    out->nes20 = nes20;
    out->flags6 = flags6;
    out->flags7 = flags7;
    out->prg_size = prg_size;
    out->chr_size = chr_size;
    out->prg_offset = prg_offset;
    out->chr_offset = prg_offset + prg_size;
    return CHISLINK_NES_OK;
}
