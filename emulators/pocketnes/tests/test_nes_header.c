#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "chislink_nes_header.h"

static void base_header(uint8_t h[16]) {
    memset(h, 0, 16);
    h[0] = 'N';
    h[1] = 'E';
    h[2] = 'S';
    h[3] = 0x1a;
    h[4] = 2;
    h[5] = 1;
}

int main(void) {
    uint8_t h[16];
    chislink_nes_header_t parsed;

    base_header(h);
    h[6] = 0x40;
    assert(chislink_nes_parse_header(h, 16 + 32768 + 8192, &parsed) == 0);
    assert(parsed.mapper == 4);
    assert(parsed.prg_offset == 16);
    assert(parsed.chr_offset == 16 + 32768);

    base_header(h);
    h[4] = 8;
    h[5] = 16;
    h[6] = 0x41;
    assert(chislink_nes_parse_header(h, 16 + 128 * 1024 + 128 * 1024,
                                    &parsed) == 0);
    assert(parsed.mapper == 4);
    assert(parsed.prg_size == 128 * 1024);
    assert(parsed.chr_size == 128 * 1024);
    assert(parsed.chr_offset == 16 + 128 * 1024);

    base_header(h);
    h[6] = 0x44;
    assert(chislink_nes_parse_header(h, 16 + 512 + 32768 + 8192,
                                    &parsed) == 0);
    assert(parsed.prg_offset == 528);

    base_header(h);
    h[7] = 0x08;
    assert(chislink_nes_parse_header(h, 16 + 32768 + 8192, &parsed) == 0);
    assert(parsed.nes20 == 1);

    h[8] = 0x10;
    assert(chislink_nes_parse_header(h, 16 + 32768 + 8192, &parsed) ==
           CHISLINK_NES_UNSUPPORTED_NES20);
    h[8] = 0x01;
    assert(chislink_nes_parse_header(h, 16 + 32768 + 8192, &parsed) ==
           CHISLINK_NES_UNSUPPORTED_NES20);

    base_header(h);
    h[6] = 0x60;
    h[7] = 0xf0;
    assert(chislink_nes_parse_header(h, 16 + 32768 + 8192, &parsed) ==
           CHISLINK_NES_UNSUPPORTED_MAPPER);

    base_header(h);
    assert(chislink_nes_parse_header(h, 16 + 100, &parsed) ==
           CHISLINK_NES_BAD_SIZE);

    base_header(h);
    h[5] = 129;
    assert(chislink_nes_parse_header(h, 16 + 32768 + 129 * 8192,
                                    &parsed) ==
           CHISLINK_NES_UNSUPPORTED_SIZE);
    return 0;
}
