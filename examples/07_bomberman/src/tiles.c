/* Bomberman tile graphics — 8×8 px, 4 bpp (32 bytes each).
 * To replace with real art: regenerate g_tiles[][] from PNG via
 * tools/quant_bmp_16c.py or a similar asset pipeline.
 */
#include "tiles.h"

#include "chislink/gba/video.h"

/* Each uint32_t encodes 8 pixels (4 bpp = 1 nibble each, low nibble first).
 * Indices 0-7 map to palette entries 8-14 (set in tiles_init). */
/* clang-format off */
static const uint32_t g_tiles[TILE_COUNT][8] = {
    /* 0: Floor — subtle grid */
    {
        0x11111111, 0x11111111, 0x11111111, 0x11111111,
        0x11111111, 0x11111111, 0x11111111, 0x11111111,
    },
    /* 1: Wall — solid block, highlighted border top-left */
    {
        0x22222222, 0x21111112, 0x21222212, 0x21222212,
        0x21222212, 0x21222212, 0x21111112, 0x22222222,
    },
    /* 2: Soft block — checker / noise */
    {
        0x03030303, 0x30303030, 0x03030303, 0x30030030,
        0x03030303, 0x30303030, 0x03030303, 0x30303030,
    },
    /* 3: Player 1 — green stick figure */
    {
        0x00044000, 0x00044000, 0x00444400, 0x00044000,
        0x00433400, 0x00044000, 0x00400400, 0x04400440,
    },
    /* 4: Player 2 — blue stick figure */
    {
        0x00055000, 0x00055000, 0x00555500, 0x00055000,
        0x00533500, 0x00055000, 0x00500500, 0x05500550,
    },
    /* 5: Bomb — circle, fuse on top */
    {
        0x00000000, 0x00066000, 0x00600600, 0x06066060,
        0x06066060, 0x00600600, 0x00066000, 0x00000000,
    },
    /* 6: Explosion — starburst */
    {
        0x00077000, 0x00777700, 0x07700770, 0x77000077,
        0x77000077, 0x07700770, 0x00777700, 0x00077000,
    },
    /* 7: Dead — cross */
    {
        0x70000007, 0x07000070, 0x00700700, 0x00070000,
        0x00070000, 0x00700700, 0x07000070, 0x70000007,
    },
};
/* clang-format on */

static uint8_t g_next_obj;

/* RGB555 helper (defined in video.h but inline here for clarity) */
static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0x1fu) | ((g & 0x1fu) << 5u) | ((b & 0x1fu) << 10u)));
}

void tiles_init(void) {
    /* Copy tile pixel data to OBJ VRAM. BG0/BG1 are owned by example_common. */
    volatile uint32_t *tile_vram =
        (volatile uint32_t *)cl_gba_video_obj_tile_addr(0);
    for (int i = 0; i < (int)TILE_COUNT; ++i)
        for (int j = 0; j < 8; ++j)
            tile_vram[i * 8 + j] = g_tiles[i][j];

    /* OBJ palette bank 0. Color 0 remains transparent. */
    cl_gba_video_set_obj_palette(1, rgb(10, 10, 8));   /* floor  */
    cl_gba_video_set_obj_palette(2, rgb(22, 21, 18));  /* wall   */
    cl_gba_video_set_obj_palette(3, rgb(18, 12, 6));   /* soft   */
    cl_gba_video_set_obj_palette(4, rgb(6, 24, 10));   /* p1 grn */
    cl_gba_video_set_obj_palette(5, rgb(8, 12, 26));   /* p2 blu */
    cl_gba_video_set_obj_palette(6, rgb(8, 6, 4));     /* bomb   */
    cl_gba_video_set_obj_palette(7, rgb(31, 24, 4));   /* explod */
}

void tiles_begin_frame(void) {
    g_next_obj = 0;
}

void tile_set(uint8_t tx, uint8_t ty, uint16_t tile_idx) {
    if (g_next_obj >= 128u) {
        return;
    }
    cl_gba_video_obj_set_square(g_next_obj++, (int)tx * 8, (int)ty * 8,
                                tile_idx, 0, 0);
}
