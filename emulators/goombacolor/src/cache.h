#ifndef __CACHE_H__
#define __CACHE_H__

#define MAX_CACHE_SIZE 16
#define MAX_ROM_PAGES 512

extern u8 *const bank_1;

extern u8* cache_location[MAX_CACHE_SIZE];
extern u16 prgcache[MAX_CACHE_SIZE];
extern u8* cachebase;
extern u8 cachepages;
extern u32* pageoffsets;

u8 *make_instant_pages(u8* rom_base);
void init_cache(void);
void flushcache(void);
void clear_instant_prg(void);
void regenerate_instant_prg(void);
void loadcachepage(int i,int bank);
void getbank(int kilobyte);
void get_rom_map(void);
void update_cache(void);

#ifdef CHISLINK
typedef struct chislink_io_pause_state
{
	u16 ime;
	u16 ie;
	u8 active;
} chislink_io_pause_state_t;

chislink_io_pause_state_t chislink_io_pause(void);
void chislink_io_resume(chislink_io_pause_state_t state);
#endif

void reload_vram_page1(void);

#endif
