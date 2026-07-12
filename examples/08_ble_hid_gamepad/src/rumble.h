#ifndef EX08_RUMBLE_H
#define EX08_RUMBLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct ex08_rumble {
    uint8_t target;
    uint8_t current;
    uint8_t accum;
    uint8_t gpio_on;
    uint8_t kick_frames;
    uint8_t last_strength;
    uint16_t frames_left;
} ex08_rumble_t;

void ex08_rumble_init(ex08_rumble_t *rumble);
void ex08_rumble_stop(ex08_rumble_t *rumble);
void ex08_rumble_update(ex08_rumble_t *rumble);
bool ex08_rumble_handle_xbox_report(ex08_rumble_t *rumble,
                                    const uint8_t *data,
                                    uint16_t length);

#endif
