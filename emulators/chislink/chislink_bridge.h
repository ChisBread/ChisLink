#ifndef CHISLINK_EMULATOR_BRIDGE_H
#define CHISLINK_EMULATOR_BRIDGE_H

#include <stdint.h>

#include "chislink/proto.h"

typedef struct chislink_launch_info {
    uint32_t type;
    uint32_t flags;
    uint32_t size;
    char path[CLP_STORAGE_PATH_MAX_BYTES];
} chislink_launch_info_t;

int chislink_bridge_open(uint32_t expected_type);
void chislink_bridge_reinstall_transport(void);
const chislink_launch_info_t *chislink_bridge_launch(void);
int chislink_bridge_read(uint32_t offset, void *dst, uint32_t length);
#ifdef CHISLINK_GOOMBACOLOR
int chislink_bridge_read_words(uint32_t offset, void *dst, uint32_t length);
void chislink_bridge_install_graphics_irqs(void (*vblank_handler)(void),
                                           void (*vcount_handler)(void));
int chislink_bridge_save_open(void);
int chislink_bridge_save_read(void *dst, uint32_t length,
                              uint32_t *out_length);
int chislink_bridge_save_write(uint32_t offset, const void *src,
                               uint32_t length);
#endif
void chislink_bridge_close(void);

#endif
