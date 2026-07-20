#ifndef POCKETNES_CHISLINK_CACHE_H
#define POCKETNES_CHISLINK_CACHE_H

#include "gba.h"

int chislink_pocketnes_boot(void);
u8 *chislink_pocketnes_rom(void);
int chislink_pocketnes_cache_reserve(u8 **cursor, u8 *limit);
int chislink_pocketnes_cache_init(void);
u8 *chislink_pocketnes_ensure_prg(u32 page);
u8 *chislink_pocketnes_prg67(u32 page);
void chislink_pocketnes_ensure_chr(u32 page);
void chislink_pocketnes_ensure_chr_range(u32 page, u32 count);

#endif
