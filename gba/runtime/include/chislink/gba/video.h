#ifndef CHISLINK_GBA_VIDEO_H
#define CHISLINK_GBA_VIDEO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void cl_gba_video_mode4_init(uint8_t bg_color);
void cl_gba_video_mode0_init(uint8_t bg_color);
void cl_gba_video_present(void);
void cl_gba_video_set_palette(uint8_t index, uint16_t color);
void cl_gba_video_set_obj_palette(uint8_t index, uint16_t color);
void cl_gba_video_clear(uint8_t color);
void cl_gba_video_put_pixel(uint16_t x, uint16_t y, uint8_t color);
void cl_gba_video_fill_rect(int x, int y, int width, int height, uint8_t color);
void cl_gba_video_clear_text_rect(int x, int y, int width, int height);
void cl_gba_video_rect(int x, int y, int width, int height, uint8_t color);
void cl_gba_video_blit_mono(int x,
                            int y,
                            uint16_t width,
                            uint16_t height,
                            const uint8_t *bits,
                            uint8_t color);
void cl_gba_video_blit_indexed(int x,
                               int y,
                               uint16_t width,
                               uint16_t height,
                               const uint8_t *pixels,
                               uint8_t transparent_color);
volatile uint8_t *cl_gba_video_framebuffer(void);

void cl_gba_video_obj_hide_all(void);
void cl_gba_video_obj_set_square(uint8_t obj,
                                 int x,
                                 int y,
                                 uint16_t tile,
                                 uint8_t size_code,
                                 uint8_t palette);
void cl_gba_video_obj_set_wide(uint8_t obj,
                               int x,
                               int y,
                               uint16_t tile,
                               uint8_t size_code,
                               uint8_t palette);
volatile uint8_t *cl_gba_video_obj_tile_addr(uint16_t tile);
void cl_gba_video_set_bg_tile(uint8_t layer,
                              uint8_t x,
                              uint8_t y,
                              uint16_t tile,
                              uint8_t palette);
void cl_gba_video_load_bg_tile(uint16_t tile, const uint8_t data[32]);
uint16_t cl_gba_video_alloc_text_tile(void);
uint16_t cl_gba_video_alloc_text_tiles(uint8_t count);
void cl_gba_video_reset_text_tiles(void);

#ifdef __cplusplus
}
#endif

#endif
