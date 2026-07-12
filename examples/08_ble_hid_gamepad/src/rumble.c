#include "rumble.h"

#include "xbox_hid.h"

#include <stdint.h>
#include <string.h>

#define CART_GPIO_RUMBLE_BIT  0x08u
#define RUMBLE_DEFAULT_FRAMES 18u
#define RUMBLE_MIN_STRENGTH   96u
#define RUMBLE_MIN_FRAMES     4u
#define RUMBLE_CONTINUOUS     0xffu

static void gpio_set(uint8_t on) {
    *(volatile uint8_t *)(uintptr_t)0x080000c4u =
        on ? CART_GPIO_RUMBLE_BIT : 0u;
}

void ex08_rumble_init(ex08_rumble_t *rumble) {
    if (!rumble) {
        return;
    }
    memset(rumble, 0, sizeof(*rumble));
    *(volatile uint8_t *)(uintptr_t)0x080000c8u = 1u;
    *(volatile uint8_t *)(uintptr_t)0x080000c6u = CART_GPIO_RUMBLE_BIT;
    gpio_set(0);
}

void ex08_rumble_stop(ex08_rumble_t *rumble) {
    if (!rumble) {
        return;
    }
    rumble->target = 0;
    rumble->current = 0;
    rumble->accum = 0;
    rumble->kick_frames = 0;
    rumble->last_strength = 0;
    rumble->frames_left = 0;
    if (rumble->gpio_on) {
        rumble->gpio_on = 0;
        gpio_set(0);
    }
}

static uint8_t scale_magnitude(uint8_t value) {
    uint8_t scaled;
    if (value <= 100u) {
        scaled = (uint8_t)(((uint16_t)value * 255u + 50u) / 100u);
    } else {
        scaled = value;
    }
    return scaled && scaled < RUMBLE_MIN_STRENGTH ?
        RUMBLE_MIN_STRENGTH : scaled;
}

static void start(ex08_rumble_t *rumble, uint8_t strength, uint16_t frames) {
    if (!strength) {
        ex08_rumble_stop(rumble);
        return;
    }
    rumble->target = strength;
    rumble->last_strength = strength;
    rumble->frames_left = frames;
    if (strength > rumble->current && strength >= 96u) {
        rumble->kick_frames = 2u;
    }
}

void ex08_rumble_update(ex08_rumble_t *rumble) {
    if (!rumble) {
        return;
    }

    if (rumble->frames_left && rumble->frames_left != 0xffffu) {
        rumble->frames_left--;
        if (!rumble->frames_left) {
            rumble->target = 0;
        }
    }

    if (rumble->current < rumble->target) {
        uint8_t delta = (uint8_t)(rumble->target - rumble->current);
        rumble->current += delta > 32u ? 32u : delta;
    } else if (rumble->current > rumble->target) {
        uint8_t delta = (uint8_t)(rumble->current - rumble->target);
        rumble->current -= delta > 48u ? 48u : delta;
    }

    uint8_t on = 0;
    if (rumble->current >= 248u) {
        on = 1u;
    } else if (rumble->current) {
        if (rumble->kick_frames) {
            rumble->kick_frames--;
            on = 1u;
        } else {
            uint16_t acc = (uint16_t)rumble->accum + rumble->current;
            rumble->accum = (uint8_t)acc;
            on = acc >= 256u ? 1u : 0u;
        }
    }

    if (on != rumble->gpio_on) {
        rumble->gpio_on = on;
        gpio_set(on);
    }
}

bool ex08_rumble_handle_xbox_report(ex08_rumble_t *rumble,
                                    const uint8_t *data,
                                    uint16_t length) {
    if (!rumble || !data) {
        return false;
    }
    if (length == XBOX_HID_RUMBLE_REPORT_BYTES + 1u &&
        data[0] == XBOX_HID_RUMBLE_REPORT_ID) {
        data++;
        length--;
    }
    if (length < XBOX_HID_RUMBLE_REPORT_BYTES) {
        return false;
    }

    uint8_t enable = data[0] & 0x0fu;
    uint8_t magnitude = data[1];
    if (data[2] > magnitude) magnitude = data[2];
    if (data[3] > magnitude) magnitude = data[3];
    if (data[4] > magnitude) magnitude = data[4];
    if (!enable || !magnitude) {
        ex08_rumble_stop(rumble);
        return true;
    }

    uint8_t duration = data[5];
    uint8_t loop_count = data[7];
    uint16_t frames = RUMBLE_DEFAULT_FRAMES;
    if (duration == RUMBLE_CONTINUOUS || loop_count == RUMBLE_CONTINUOUS) {
        frames = 0xffffu;
    } else if (duration) {
        uint16_t loops = loop_count ? loop_count : 1u;
        uint32_t total_10ms = (uint32_t)duration * loops;
        uint32_t total_frames = (total_10ms * 60u + 99u) / 100u;
        if (total_frames < RUMBLE_MIN_FRAMES) {
            total_frames = RUMBLE_MIN_FRAMES;
        } else if (total_frames > 0xfffeu) {
            total_frames = 0xfffeu;
        }
        frames = (uint16_t)total_frames;
    }

    start(rumble, scale_magnitude(magnitude), frames);
    return true;
}
