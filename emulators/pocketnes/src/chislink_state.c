#include "includes.h"

#ifdef CHISLINK

#include "chislink_cache.h"

static const char *const state_slots[CHISLINK_STATE_SLOT_COUNT] = {
	"State 1", "State 2", "State 3", "State 4", "State 5",
	"State 6", "State 7", "State 8", "State 9", "State 10",
};

static void draw_state_slots(const char *title) {
	cls(2);
	drawtext(32, title, 0);
	for (u32 i = 0; i < CHISLINK_STATE_SLOT_COUNT; ++i) {
		drawtext(32 + 2 + i, state_slots[i], selected == (int)i);
	}
	drawtext(32 + 18, "A=OK SELECT=Delete", 0);
}

static void state_result(const char *message) {
	cls(2);
	drawtext(32 + 9, message, 0);
	for (int i = 0; i < 45; ++i) waitframe();
}

bool can_quickload(void) {
	uint32_t size = 0;
	chislink_pocketnes_io_pause_state_t pause = chislink_pocketnes_io_pause();
	int ok = chislink_bridge_state_stat(1u, &size) == 0 && size != 0u;
	chislink_pocketnes_io_resume(pause);
	return ok;
}

static bool save_state_slot(uint8_t slot) {
	chislink_pocketnes_io_pause_state_t pause = chislink_pocketnes_io_pause();
	int ok = chislink_bridge_state_open(slot, 1, NULL) == 0 &&
		chislink_savestate();
	if (chislink_bridge_state_close() < 0) ok = 0;
	chislink_pocketnes_io_resume(pause);
	return ok;
}

static bool load_state_slot(uint8_t slot) {
	uint32_t size = 0;
	chislink_pocketnes_io_pause_state_t pause = chislink_pocketnes_io_pause();
	int ok = chislink_bridge_state_open(slot, 0, &size) == 0 &&
		size != 0u && chislink_loadstate(size);
	if (chislink_bridge_state_close() < 0) ok = 0;
	chislink_pocketnes_io_resume(pause);
	return ok;
}

static void remove_state_slot(uint8_t slot) {
	chislink_pocketnes_io_pause_state_t pause = chislink_pocketnes_io_pause();
	(void)chislink_bridge_state_remove(slot);
	chislink_pocketnes_io_resume(pause);
}

bool quicksave(void) {
	return save_state_slot(1u);
}

bool quickload(void) {
	return load_state_slot(1u);
}

void savestatemenu(void) {
	selected = 0;
	draw_state_slots("Save state:");
	scrolll(0);
	u32 key;
	do {
		key = getmenuinput(CHISLINK_STATE_SLOT_COUNT);
		if (key & A_BTN) {
			uint8_t slot = (uint8_t)selected + 1u;
			int ok = save_state_slot(slot);
			state_result(ok ? "          State saved." :
			                  "        Save state failed.");
			draw_state_slots("Save state:");
		} else if (key & SELECT) {
			remove_state_slot((uint8_t)selected + 1u);
			draw_state_slots("Save state:");
		}
	} while (!(key & (L_BTN + R_BTN + B_BTN)));
	scrollr();
}

void loadstatemenu(void) {
	selected = 0;
	draw_state_slots("Load state:");
	scrolll(0);
	u32 key;
	do {
		key = getmenuinput(CHISLINK_STATE_SLOT_COUNT);
		if (key & A_BTN) {
			uint8_t slot = (uint8_t)selected + 1u;
			int ok = load_state_slot(slot);
			if (!ok) state_result("        Load state failed.");
			draw_state_slots("Load state:");
		} else if (key & SELECT) {
			remove_state_slot((uint8_t)selected + 1u);
			draw_state_slots("Load state:");
		}
	} while (!(key & (L_BTN + R_BTN + B_BTN + A_BTN)));
	scrollr();
}

#endif
