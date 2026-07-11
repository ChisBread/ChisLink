#include "chislink/gba/video.h"

#include "chislink/gba/dma.h"
#include "chislink/gba/hw.h"

static volatile uint8_t *g_framebuffer = CL_GBA_MEM_VRAM_PAGE1_U8;
static uint8_t g_visible_page;
static uint8_t g_draw_page;
static uint8_t g_tile_mode;
static uint16_t g_next_text_tile;
static uint16_t g_oam_shadow[128u * 4u];

#define CL_GBA_BG_TILE_BYTES 32u
#define CL_GBA_UI_BG_CHAR_BASE 0u
#define CL_GBA_UI_TEXT_CHAR_BASE0 1u
#define CL_GBA_UI_TEXT_CHAR_BASE1 2u
#define CL_GBA_UI_BG_MAP_BASE0 28u
#define CL_GBA_UI_BG_MAP_BASE1 29u
#define CL_GBA_UI_TEXT_MAP_BASE0 30u
#define CL_GBA_UI_TEXT_MAP_BASE1 31u
#define CL_GBA_UI_BG_TILE_BASE ((volatile uint8_t *)(uintptr_t)0x06000000u)
#define CL_GBA_UI_TEXT_TILE_BASE0 ((volatile uint8_t *)(uintptr_t)0x06004000u)
#define CL_GBA_UI_TEXT_TILE_BASE1 ((volatile uint8_t *)(uintptr_t)0x06008000u)
#define CL_GBA_UI_BG_MAP0 ((volatile uint16_t *)(uintptr_t)0x0600e000u)
#define CL_GBA_UI_BG_MAP1 ((volatile uint16_t *)(uintptr_t)0x0600e800u)
#define CL_GBA_UI_TEXT_MAP0 ((volatile uint16_t *)(uintptr_t)0x0600f000u)
#define CL_GBA_UI_TEXT_MAP1 ((volatile uint16_t *)(uintptr_t)0x0600f800u)
#define CL_GBA_UI_SOLID_TILE_COUNT 16u
#define CL_GBA_UI_TEXT_TILE_BASE_INDEX 1u
#define CL_GBA_UI_TEXT_TILE_CAPACITY 511u

static inline uint16_t gba_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((r & 31u) | ((g & 31u) << 5u) | ((b & 31u) << 10u));
}

void cl_gba_video_mode4_init(uint8_t bg_color) {
    g_visible_page = 0;
    g_framebuffer = CL_GBA_MEM_VRAM_PAGE0_U8;
    CL_GBA_REG_DISPCNT = CL_GBA_DISPCNT_MODE4 | CL_GBA_DISPCNT_BG2;
    CL_GBA_REG_BG2CNT = 0x0080u;
    CL_GBA_REG_BG2PA = 0x0100u;
    CL_GBA_REG_BG2PB = 0;
    CL_GBA_REG_BG2PC = 0;
    CL_GBA_REG_BG2PD = 0x0100u;
    CL_GBA_REG_BG2X = 0;
    CL_GBA_REG_BG2Y = 0;
    CL_GBA_MEM_PALETTE[0] = gba_rgb(0, 0, 0);
    cl_gba_video_clear(bg_color);
    g_framebuffer = CL_GBA_MEM_VRAM_PAGE1_U8;
    cl_gba_video_clear(bg_color);
}

static void load_solid_tiles(void) {
    uint8_t tile[CL_GBA_BG_TILE_BYTES];
    for (uint8_t color = 0; color < CL_GBA_UI_SOLID_TILE_COUNT; ++color) {
        uint8_t packed = (uint8_t)(color | (color << 4u));
        for (uint8_t i = 0; i < sizeof(tile); ++i) {
            tile[i] = packed;
        }
        cl_gba_dma_copy16(CL_GBA_UI_BG_TILE_BASE +
                              (uint32_t)color * CL_GBA_BG_TILE_BYTES,
                          tile, CL_GBA_BG_TILE_BYTES / 2u);
    }
}

static volatile uint16_t *bg_map_for_page(uint8_t page) {
    return page ? CL_GBA_UI_BG_MAP1 : CL_GBA_UI_BG_MAP0;
}

static volatile uint16_t *text_map_for_page(uint8_t page) {
    return page ? CL_GBA_UI_TEXT_MAP1 : CL_GBA_UI_TEXT_MAP0;
}

static volatile uint8_t *text_tiles_for_page(uint8_t page) {
    return page ? CL_GBA_UI_TEXT_TILE_BASE1 : CL_GBA_UI_TEXT_TILE_BASE0;
}

static uint16_t text_char_base_for_page(uint8_t page) {
    return page ? CL_GBA_UI_TEXT_CHAR_BASE1 : CL_GBA_UI_TEXT_CHAR_BASE0;
}

static uint16_t bg_map_base_for_page(uint8_t page) {
    return page ? CL_GBA_UI_BG_MAP_BASE1 : CL_GBA_UI_BG_MAP_BASE0;
}

static uint16_t text_map_base_for_page(uint8_t page) {
    return page ? CL_GBA_UI_TEXT_MAP_BASE1 : CL_GBA_UI_TEXT_MAP_BASE0;
}

static void select_visible_tile_page(uint8_t page) {
    CL_GBA_REG_BG0CNT = 0u |
                        CL_GBA_BG_CHAR_BASE(text_char_base_for_page(page)) |
                        CL_GBA_BG_SCREEN_BASE(text_map_base_for_page(page)) |
                        CL_GBA_BG_16_COLOR | CL_GBA_BG_SIZE_32x32;
    CL_GBA_REG_BG1CNT = 3u |
                        CL_GBA_BG_CHAR_BASE(CL_GBA_UI_BG_CHAR_BASE) |
                        CL_GBA_BG_SCREEN_BASE(bg_map_base_for_page(page)) |
                        CL_GBA_BG_16_COLOR | CL_GBA_BG_SIZE_32x32;
}

void cl_gba_video_mode0_init(uint8_t bg_color) {
    g_tile_mode = 1;
    CL_GBA_REG_DISPCNT = 0;
    g_visible_page = 0;
    g_draw_page = 1;
    select_visible_tile_page(g_visible_page);
    CL_GBA_REG_BG0HOFS = 0;
    CL_GBA_REG_BG0VOFS = 0;
    CL_GBA_REG_BG1HOFS = 0;
    CL_GBA_REG_BG1VOFS = 0;
    load_solid_tiles();
    cl_gba_dma_fill16(bg_map_for_page(0), bg_color & 0x0fu, 32u * 32u);
    cl_gba_dma_fill16(bg_map_for_page(1), bg_color & 0x0fu, 32u * 32u);
    cl_gba_dma_fill16(text_map_for_page(0), 0, 32u * 32u);
    cl_gba_dma_fill16(text_map_for_page(1), 0, 32u * 32u);
    cl_gba_video_obj_hide_all();
    cl_gba_dma_copy16(CL_GBA_MEM_OAM, g_oam_shadow, 128u * 4u);
    cl_gba_video_reset_text_tiles();
    CL_GBA_REG_DISPCNT = CL_GBA_DISPCNT_MODE0 | CL_GBA_DISPCNT_OBJ_1D |
                         CL_GBA_DISPCNT_BG1 | CL_GBA_DISPCNT_BG0 |
                         CL_GBA_DISPCNT_OBJ;
}

void cl_gba_video_present(void) {
    if (g_tile_mode) {
        g_visible_page = g_draw_page;
        select_visible_tile_page(g_visible_page);
        cl_gba_dma_copy16(CL_GBA_MEM_OAM, g_oam_shadow, 128u * 4u);
        g_draw_page ^= 1u;
        return;
    }
    g_visible_page ^= 1u;
    if (g_visible_page) {
        CL_GBA_REG_DISPCNT |= CL_GBA_DISPCNT_PAGE1;
        g_framebuffer = CL_GBA_MEM_VRAM_PAGE0_U8;
    } else {
        CL_GBA_REG_DISPCNT &= (uint16_t)~CL_GBA_DISPCNT_PAGE1;
        g_framebuffer = CL_GBA_MEM_VRAM_PAGE1_U8;
    }
}

void cl_gba_video_set_palette(uint8_t index, uint16_t color) {
    CL_GBA_MEM_PALETTE[index] = color;
}

void cl_gba_video_set_obj_palette(uint8_t index, uint16_t color) {
    CL_GBA_MEM_OBJ_PALETTE[index] = color;
}

void cl_gba_video_clear(uint8_t color) {
    if (g_tile_mode) {
        uint16_t bg_entry = color & 0x0fu;
        cl_gba_dma_fill16(bg_map_for_page(g_draw_page), bg_entry, 32u * 32u);
        cl_gba_dma_fill16(text_map_for_page(g_draw_page), 0, 32u * 32u);
        cl_gba_video_obj_hide_all();
        cl_gba_video_reset_text_tiles();
        return;
    }
    uint16_t packed = (uint16_t)(color | ((uint16_t)color << 8u));
    cl_gba_dma_fill16(g_framebuffer, packed,
                      (uint16_t)((CL_GBA_SCREEN_WIDTH * CL_GBA_SCREEN_HEIGHT) / 2u));
}

void cl_gba_video_put_pixel(uint16_t x, uint16_t y, uint8_t color) {
    if (g_tile_mode) {
        (void)x;
        (void)y;
        (void)color;
        return;
    }
    if (x >= CL_GBA_SCREEN_WIDTH || y >= CL_GBA_SCREEN_HEIGHT) {
        return;
    }

    uintptr_t offset = (uintptr_t)y * CL_GBA_SCREEN_WIDTH + x;
    volatile uint16_t *dst = (volatile uint16_t *)(uintptr_t)&g_framebuffer[offset & ~(uintptr_t)1u];
    uint16_t prev = *dst;
    if (offset & 1u) {
        *dst = (uint16_t)((prev & 0x00ffu) | ((uint16_t)color << 8u));
    } else {
        *dst = (uint16_t)((prev & 0xff00u) | color);
    }
}

void cl_gba_video_fill_rect(int x, int y, int width, int height, uint8_t color) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (g_tile_mode) {
        int x0 = x < 0 ? 0 : x / 8;
        int y0 = y < 0 ? 0 : y / 8;
        int x1 = (x + width + 7) / 8;
        int y1 = (y + height + 7) / 8;
        if (x1 > 30) {
            x1 = 30;
        }
        if (y1 > 20) {
            y1 = 20;
        }
        if (x0 >= x1 || y0 >= y1) {
            return;
        }
        uint16_t entry = color & 0x0fu;
        for (int row = y0; row < y1; ++row) {
            for (int col = x0; col < x1; ++col) {
                bg_map_for_page(g_draw_page)[row * 32 + col] = entry;
            }
        }
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    if (x1 > (int)CL_GBA_SCREEN_WIDTH) {
        x1 = (int)CL_GBA_SCREEN_WIDTH;
    }
    if (y1 > (int)CL_GBA_SCREEN_HEIGHT) {
        y1 = (int)CL_GBA_SCREEN_HEIGHT;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    uint16_t packed = (uint16_t)(color | ((uint16_t)color << 8u));
    for (int row = y0; row < y1; ++row) {
        int start = x0;
        int end = x1;
        if (start & 1) {
            cl_gba_video_put_pixel((uint16_t)start, (uint16_t)row, color);
            ++start;
        }
        if (end & 1) {
            --end;
            cl_gba_video_put_pixel((uint16_t)end, (uint16_t)row, color);
        }
        if (start < end) {
            uintptr_t offset = (uintptr_t)row * CL_GBA_SCREEN_WIDTH + (uint32_t)start;
            volatile uint16_t *dst =
                (volatile uint16_t *)(uintptr_t)&g_framebuffer[offset];
            cl_gba_dma_fill16(dst, packed, (uint16_t)((end - start) >> 1));
        }
    }
}

void cl_gba_video_clear_text_rect(int x, int y, int width, int height) {
    if (!g_tile_mode || width <= 0 || height <= 0) {
        return;
    }
    int x0 = x < 0 ? 0 : x / 8;
    int y0 = y < 0 ? 0 : y / 8;
    int x1 = (x + width + 7) / 8;
    int y1 = (y + height + 7) / 8;
    if (x1 > 30) {
        x1 = 30;
    }
    if (y1 > 20) {
        y1 = 20;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    volatile uint16_t *map = text_map_for_page(g_draw_page);
    for (int row = y0; row < y1; ++row) {
        for (int col = x0; col < x1; ++col) {
            map[row * 32 + col] = 0;
        }
    }
}

void cl_gba_video_rect(int x, int y, int width, int height, uint8_t color) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (g_tile_mode) {
        int edge_x = width < 8 ? width : 8;
        int edge_y = height < 8 ? height : 8;
        cl_gba_video_fill_rect(x, y, width, edge_y, color);
        cl_gba_video_fill_rect(x, y + height - edge_y, width, edge_y, color);
        cl_gba_video_fill_rect(x, y, edge_x, height, color);
        cl_gba_video_fill_rect(x + width - edge_x, y, edge_x, height, color);
        return;
    }
    cl_gba_video_fill_rect(x, y, width, 1, color);
    cl_gba_video_fill_rect(x, y + height - 1, width, 1, color);
    cl_gba_video_fill_rect(x, y, 1, height, color);
    cl_gba_video_fill_rect(x + width - 1, y, 1, height, color);
}

void cl_gba_video_blit_mono(int x,
                            int y,
                            uint16_t width,
                            uint16_t height,
                            const uint8_t *bits,
                            uint8_t color) {
    if (!bits || width == 0 || height == 0) {
        return;
    }
    if (g_tile_mode) {
        (void)x;
        (void)y;
        (void)color;
        return;
    }
    uint16_t stride = (uint16_t)((width + 7u) >> 3u);
    for (uint16_t row = 0; row < height; ++row) {
        for (uint16_t col = 0; col < width; ++col) {
            uint8_t byte = bits[(uint32_t)row * stride + (col >> 3u)];
            if (byte & (uint8_t)(0x80u >> (col & 7u))) {
                cl_gba_video_put_pixel((uint16_t)(x + (int)col),
                                       (uint16_t)(y + (int)row),
                                       color);
            }
        }
    }
}

void cl_gba_video_blit_indexed(int x,
                               int y,
                               uint16_t width,
                               uint16_t height,
                               const uint8_t *pixels,
                               uint8_t transparent_color) {
    if (!pixels || width == 0 || height == 0) {
        return;
    }
    if (g_tile_mode) {
        (void)x;
        (void)y;
        (void)transparent_color;
        return;
    }
    for (uint16_t row = 0; row < height; ++row) {
        int dst_y = y + (int)row;
        if (dst_y < 0 || dst_y >= (int)CL_GBA_SCREEN_HEIGHT) {
            continue;
        }
        for (uint16_t col = 0; col < width; ++col) {
            int dst_x = x + (int)col;
            if (dst_x < 0 || dst_x >= (int)CL_GBA_SCREEN_WIDTH) {
                continue;
            }
            uint8_t color = pixels[(uint32_t)row * width + col];
            if (color != transparent_color) {
                cl_gba_video_put_pixel((uint16_t)dst_x, (uint16_t)dst_y, color);
            }
        }
    }
}

volatile uint8_t *cl_gba_video_framebuffer(void) {
    return g_framebuffer;
}

void cl_gba_video_obj_hide_all(void) {
    for (uint16_t i = 0; i < 128u; ++i) {
        g_oam_shadow[i * 4u] = CL_GBA_OBJ_ATTR0_HIDE;
        g_oam_shadow[i * 4u + 1u] = 0;
        g_oam_shadow[i * 4u + 2u] = 0;
        g_oam_shadow[i * 4u + 3u] = 0;
    }
}

static void set_obj(uint8_t obj,
                    int x,
                    int y,
                    uint16_t tile,
                    uint16_t shape,
                    uint8_t size_code,
                    uint8_t palette) {
    if (obj >= 128u) {
        return;
    }
    g_oam_shadow[obj * 4u] =
        (uint16_t)((y & CL_GBA_OBJ_ATTR0_Y_MASK) | shape |
                   CL_GBA_OBJ_ATTR0_4BPP);
    g_oam_shadow[obj * 4u + 1u] =
        (uint16_t)((x & CL_GBA_OBJ_ATTR1_X_MASK) |
                   ((uint16_t)size_code << 14u));
    g_oam_shadow[obj * 4u + 2u] =
        (uint16_t)((tile & CL_GBA_OBJ_ATTR2_TILE_MASK) |
                   CL_GBA_OBJ_ATTR2_PRIORITY(1) |
                   CL_GBA_OBJ_ATTR2_PALETTE(palette & 0x0fu));
}

void cl_gba_video_obj_set_square(uint8_t obj,
                                 int x,
                                 int y,
                                 uint16_t tile,
                                 uint8_t size_code,
                                 uint8_t palette) {
    set_obj(obj, x, y, tile, CL_GBA_OBJ_ATTR0_SQUARE, size_code, palette);
}

void cl_gba_video_obj_set_wide(uint8_t obj,
                               int x,
                               int y,
                               uint16_t tile,
                               uint8_t size_code,
                               uint8_t palette) {
    set_obj(obj, x, y, tile, CL_GBA_OBJ_ATTR0_WIDE, size_code, palette);
}

volatile uint8_t *cl_gba_video_obj_tile_addr(uint16_t tile) {
    return CL_GBA_MEM_OBJ_VRAM_U8 + (uint32_t)tile * CL_GBA_BG_TILE_BYTES;
}

void cl_gba_video_set_bg_tile(uint8_t layer,
                              uint8_t x,
                              uint8_t y,
                              uint16_t tile,
                              uint8_t palette) {
    if (!g_tile_mode || x >= 32u || y >= 32u) {
        return;
    }
    volatile uint16_t *map = layer == 0 ?
        text_map_for_page(g_draw_page) : bg_map_for_page(g_draw_page);
    map[(uint32_t)y * 32u + x] = (uint16_t)(tile | ((uint16_t)palette << 12u));
}

void cl_gba_video_load_bg_tile(uint16_t tile, const uint8_t data[32]) {
    if (!g_tile_mode || !data) {
        return;
    }
    volatile uint8_t *dst = text_tiles_for_page(g_draw_page) +
        (uint32_t)tile * CL_GBA_BG_TILE_BYTES;
    cl_gba_dma_copy16(dst, data, CL_GBA_BG_TILE_BYTES / 2u);
}

uint16_t cl_gba_video_alloc_text_tile(void) {
    return cl_gba_video_alloc_text_tiles(1u);
}

uint16_t cl_gba_video_alloc_text_tiles(uint8_t count) {
    if (!g_tile_mode || count == 0 ||
        g_next_text_tile + (uint16_t)count > CL_GBA_UI_TEXT_TILE_CAPACITY) {
        return 0;
    }
    uint16_t start = g_next_text_tile;
    g_next_text_tile = (uint16_t)(g_next_text_tile + count);
    return start;
}

void cl_gba_video_reset_text_tiles(void) {
    uint8_t empty[CL_GBA_BG_TILE_BYTES] = {0};
    cl_gba_dma_copy16(text_tiles_for_page(g_draw_page), empty,
                      CL_GBA_BG_TILE_BYTES / 2u);
    cl_gba_dma_copy16(text_tiles_for_page(g_visible_page), empty,
                      CL_GBA_BG_TILE_BYTES / 2u);
    g_next_text_tile = CL_GBA_UI_TEXT_TILE_BASE_INDEX;
}
