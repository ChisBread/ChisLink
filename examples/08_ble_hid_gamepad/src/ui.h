#ifndef EX08_UI_H
#define EX08_UI_H

#include <stdint.h>

typedef struct ex08_ui_state {
    uint8_t connected;
    uint8_t encrypted;
    uint8_t bonded;
    uint8_t authenticated;
    uint8_t subscribed;
    uint8_t pair_action;
    uint8_t pair_wait_confirm;
    uint8_t seen_disconnect;
    uint8_t rumble_current;
    uint8_t rumble_strength;
    uint32_t pair_code;
    uint16_t disconnect_reason;
    int last_error;
} ex08_ui_state_t;

void ex08_ui_init(void);
void ex08_ui_draw(const ex08_ui_state_t *state);

#endif
