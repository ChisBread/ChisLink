#ifndef CHISLINK_GBA_TEXT_H
#define CHISLINK_GBA_TEXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void cl_gba_text_draw_char(int x, int y, char ch, uint8_t color);
void cl_gba_text_draw(int x, int y, const char *text, uint8_t color);
void cl_gba_text_draw_u32_hex(int x, int y, uint32_t value, uint8_t color);

#ifdef __cplusplus
}
#endif

#endif
