#ifndef POCKETNES_CHISLINK_CACHE_H
#define POCKETNES_CHISLINK_CACHE_H

#include "gba.h"

typedef struct chislink_pocketnes_io_pause_state {
	u16 ime;
	u16 ie;
	u8 active;
} chislink_pocketnes_io_pause_state_t;

int chislink_pocketnes_boot(void);
u8 *chislink_pocketnes_rom(void);
int chislink_pocketnes_cache_reserve(u8 **cursor, u8 *limit);
int chislink_pocketnes_cache_init(void);
u8 *chislink_pocketnes_ensure_prg(u32 page);
u8 *chislink_pocketnes_prg67(u32 page);
void chislink_pocketnes_ensure_chr(u32 page);
void chislink_pocketnes_ensure_chr_range(u32 page, u32 count);
int chislink_pocketnes_restore_cache_map(void);
chislink_pocketnes_io_pause_state_t chislink_pocketnes_io_pause(void);
void chislink_pocketnes_io_resume(chislink_pocketnes_io_pause_state_t state);

#endif
