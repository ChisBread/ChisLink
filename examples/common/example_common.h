#ifndef CHISLINK_EXAMPLES_COMMON_H
#define CHISLINK_EXAMPLES_COMMON_H

#include "chislink/client.h"
#include "chislink/gba_sio_transport.h"
#include "chislink/storage_client.h"

#include <stdbool.h>
#include <stdint.h>

enum example_palette {
    EX_COLOR_BG = 0,
    EX_COLOR_PANEL = 1,
    EX_COLOR_TEXT = 2,
    EX_COLOR_DIM = 3,
    EX_COLOR_OK = 4,
    EX_COLOR_WARN = 5,
};

#define EX_STORAGE_SCRATCH_BYTES 520u

typedef struct example_link {
    cl_client_t client;
    cl_gba_sio_transport_t transport;
    cl_storage_client_t storage;
    uint8_t storage_scratch[EX_STORAGE_SCRATCH_BYTES];
    uint32_t caps;
    int last_error;
    bool storage_registered;
} example_link_t;

void ex_video_init(const char *title);
void ex_clear_body(void);
void ex_present(void);
void ex_draw_i32(int x, int y, int32_t value, uint8_t color);
void ex_draw_u32_dec(int x, int y, uint32_t value, uint8_t color);
void ex_draw_size(int x, int y, uint64_t value, uint8_t color);
void ex_draw_ipv4(int x, int y, uint32_t ip, uint8_t color);
void ex_draw_error(int x, int y, int error);
void ex_draw_footer(const char *text);
void ex_wait_key_release(void);
uint16_t ex_keys_pressed(void);

bool ex_link_init(example_link_t *link);
bool ex_link_hello(example_link_t *link);
bool ex_link_register_storage(example_link_t *link);

#endif
