#include "includes.h"

#define page_size (16)
#define page_size_2 (page_size*1024)

#define CRAP_AMOUNT 512
#define INVALID_PAGE 65535u
#define SERIAL_IRQ_MASK 0x0080u

u8 *const bank_1=(u8*)0x06010000-CRAP_AMOUNT;

#if MOVIEPLAYER
EWRAM_BSS u16 prgcache[MAX_CACHE_SIZE];
EWRAM_BSS u8* cache_location[MAX_CACHE_SIZE];
EWRAM_BSS u8* cachebase;
EWRAM_BSS u8 cachepages;
EWRAM_BSS u32* pageoffsets;
EWRAM_BSS static u32 cache_age[MAX_CACHE_SIZE];
EWRAM_BSS static u32 cache_clock;
#endif

u8 *make_instant_pages(u8* rom_base)
{
	//this is for cases where there is no caching!
	u32 *p=(u32*)rom_base;
	u8 *page0_rom;
//	u8 cartsizebyte;
	int i;
	
#if MOVIEPLAYER
	if (usingcache)
	{
		return rom_base;
	}
#endif

#if USETRIM
	if (*p==TRIM)
	{
		p+=2;
//		num_pages=p[0]/4-8;
//		page_mask=num_pages-1;
		for (i=0;i<MAX_ROM_PAGES;i++)
		{
			INSTANT_PAGES[i]=rom_base+p[i];//&page_mask];
		}
	}
	else
#endif
	{
//		num_pages=(2<<rom_base[148]);
//		page_mask=num_pages-1;
		for (i=0;i<MAX_ROM_PAGES;i++)
		{
			INSTANT_PAGES[i]=rom_base+16384*(i);//&page_mask);
		}
	}
	page0_rom=INSTANT_PAGES[0];
//	cartsizebyte=page0_rom[0x148];

//	if (cartsizebyte>0)
	{
		//copy bank 0 to VRAM
//		memcpy32(bank_1,page0_rom,16384);
		memcpy32(bank_1,page0_rom,16384+CRAP_AMOUNT);
		INSTANT_PAGES[0]=bank_1;
	}
	return page0_rom;
}

#if !MOVIEPLAYER
void init_cache() {}
#endif

#if MOVIEPLAYER

#ifdef CHISLINK
chislink_io_pause_state_t chislink_io_pause(void)
{
	chislink_io_pause_state_t state = {0};
	state.ime = REG_IME;
	REG_IME = 0;
	state.ie = REG_IE;
	REG_DM0CNT_H = 0;
	REG_DM1CNT_H = 0;
	REG_DM2CNT_H = 0;
	REG_IF = 0xffffu;
	REG_IE = SERIAL_IRQ_MASK;
	REG_IME = 1;
	state.active = 1;
	return state;
}

void chislink_io_resume(chislink_io_pause_state_t state)
{
	if (!state.active) return;

	REG_IME = 0;
	// A storage transfer spans multiple frames. Resume from the same safe
	// state as end_gba_hdma; the next VBlank rebuilds the scanline DMA chain.
	REG_DM0CNT_H = 0;
	REG_DM1CNT_H = 0;
	REG_DM2CNT_H = 0;
	vcountfptr = &vbldummy;
	REG_DISPSTAT = 0x0008u;
	REG_IF = 0xffffu;
	REG_IE = state.ie;
	REG_IME = state.ime;
}

#endif

static int cache_slot_for_page(int page)
{
	int i;
	for (i = 0; i < cachepages; ++i)
	{
		if (prgcache[i] == page) return i;
	}
	return -1;
}

void init_cache()
{
	int i;
	u8* dest=ewram_start;
	
#if RESIZABLE
	u8 *end_of_cache=END_of_exram;
#else
	u8 *end_of_cache=(u8*)(&END_OF_EXRAM);
#endif
	if (!usingcache) return;
	
//	g_banks[0]=0;
//	g_banks[1]=1;
	
	cachebase=dest;
	cachepages=(end_of_cache-cachebase)/page_size_2;
	if (cachepages >= MAX_CACHE_SIZE) cachepages = MAX_CACHE_SIZE - 1;
	//set up cache locations, first few are sequential
	for (i=0;i<cachepages;i++)
	{
		cache_location[i+1]=cachebase+page_size_2*i;
	}
	//extra page in VRAM to accelerate games
	cache_location[0]=bank_1;
	cachepages++;
	
	clear_instant_prg();
	flushcache();
	#ifdef CHISLINK
	const chislink_launch_info_t *launch = chislink_bridge_launch();
	if (launch)
	{
		chislink_io_pause_state_t pause = chislink_io_pause();
		int banks = (launch->size + page_size_2 - 1u) / page_size_2;
		for (i = 0; i < cachepages && i < banks; ++i)
		{
			if (i != 0) loadcachepage(i, i);
			prgcache[i] = (u16)i;
			cache_age[i] = ++cache_clock;
		}
		regenerate_instant_prg();
		chislink_io_resume(pause);
	}
	#endif
//	usingcompcache=0;
}

void flushcache()
{
	int i;

	for (i=0;i<cachepages;i++)
	{
		prgcache[i]=INVALID_PAGE;
		cache_age[i]=0;
	}
	cache_clock=0;
}

void clear_instant_prg()
{
	int i;
	int l = MAX_ROM_PAGES;
	u32 *instant_prg = (u32*)INSTANT_PAGES;
	for (i=0;i<l;i++)
	{
		instant_prg[i]=0xC0000000;
	}
}

void regenerate_instant_prg()
{
	int i;
	u8**instant_prg=INSTANT_PAGES;

	for (i=0;i<cachepages;i++)
	{
		int p=prgcache[i];
		if (p<65535)
		{
			instant_prg[p]=cache_location[i];
		}
	}
}

void loadcachepage(int i,int bank) //i=slot, bank=bank that goes into the slot
{
	u8 *dest;
	if (usinggbamp)
	{
		#ifdef CHISLINK
		u32 offset = (u32)bank * page_size_2;
		u32 take = 0;
		const chislink_launch_info_t *launch = chislink_bridge_launch();
		dest = cache_location[i];
		memset(dest, 0xff, page_size_2);
		if (launch && offset < launch->size)
		{
			take = launch->size - offset;
			if (take > page_size_2) take = page_size_2;
			(void)chislink_bridge_read_words(offset, dest, take);
		}
		#else
		FAT_fseek(rom_file, bank*page_size_2,SEEK_SET);
		dest =cache_location[i];
		FAT_fread(dest,1,page_size_2,rom_file);
		#endif
	}
	else
	{
		dest =cache_location[i];
		memcpy(dest,romstart+bank*page_size_2,page_size_2);
		waitframe();
		waitframe();
		waitframe();
	}
}

void getbank(int kilobyte)
{
	int bank;
	u32 i,j;
	int slot;
	int slotcontent;
	u32 oldest;
//	u8 *src, *dest;
	u32 *banks=g_banks;
	bank=kilobyte/page_size;
	
	//page is in cache?
	slot = cache_slot_for_page(bank);
	if (slot >= 0)
	{
		cache_age[slot] = ++cache_clock;
		return;
	}

	// Prefer an empty slot, otherwise evict the least recently used page
	// that is not currently mapped into either Game Boy ROM window.
	i = MAX_CACHE_SIZE;
	oldest = 0xffffffffu;
	for (j = 0; j < cachepages; ++j)
	{
		slotcontent = prgcache[j];
		if (slotcontent == INVALID_PAGE)
		{
			i = j;
			break;
		}
		if (slotcontent == (int)banks[0] ||
			slotcontent == (int)banks[1]) continue;
		if (cache_age[j] < oldest)
		{
			oldest = cache_age[j];
			i = j;
		}
	}
	if (i >= cachepages) return;
	slotcontent = prgcache[i];
	if (slotcontent != INVALID_PAGE &&
		INSTANT_PAGES[slotcontent] == cache_location[i])
	{
		INSTANT_PAGES[slotcontent] = (u8*)0xC0000000;
	}

#if 0
	if (usingcompcache)
	{
		int srcoffset;
		srcoffset = 16 + pageoffsets[bank];
//		dest = (u8*)06014000;
//		FAT_fseek(rom_file,srcoffset,SEEK_SET);
//		src=FAT_fread_16(dest,1,16384,rom_file);

		dest = cachebase+0x4000*cachepages;
		FAT_fseek(rom_file,srcoffset,SEEK_SET);
		FAT_fread(dest,1,16384,rom_file);
		src=dest;
		dest =cachebase+0x4000*i;
		depack(src,dest);
	}
	else
#endif
	loadcachepage(i,bank);
	prgcache[i]=bank;
	cache_age[i]=++cache_clock;
	INSTANT_PAGES[bank]=cache_location[i];
}

void get_rom_map()
{
	u32 *banks=g_banks;
	u8**memmap = g_memmap_tbl;
	u8**instant_prg = INSTANT_PAGES;
	int i;
	int j;

	for (i=0;i<2;i++)
	{
		u8* data=instant_prg[banks[i]]-(i*16384);
		for (j=0;j<4;j++)
		{
			memmap[i*4+j]=data;
		}
	}
}
void update_cache()
{
	//updates the cache's state, and all the lookup tables
	//also fixes the memory map and vram map
	u32 *banks=g_banks;
	int i;
	int missing = cache_slot_for_page((int)banks[0]) < 0 ||
		cache_slot_for_page((int)banks[1]) < 0;
	#ifdef CHISLINK
	chislink_io_pause_state_t pause = {0};
	if (missing) pause = chislink_io_pause();
	#endif

	for (i=0;i<2;i++)
	{
		getbank(banks[i]*16);
	}
	regenerate_instant_prg();
	get_rom_map();
	#ifdef CHISLINK
	chislink_io_resume(pause);
	#endif
}

#if RESIZABLE
void add_exram()
{
	GBC_exramsize=0x6000;
	GBC_exram=END_of_exram-GBC_exramsize;
	memset(GBC_exram,0,GBC_exramsize);
	END_of_exram=GBC_exram;
	init_cache();
	update_cache();
}
#endif

/*
void reload_vram_page1()
{
	int i=cachepages-2;
	int bank=prgcache[i];
	if (bank<65535)
	{
		loadcachepage(i,bank);
	}
}
*/

#endif
