#include "example_common.h"

#include "chislink/ble.h"
#include "chislink/gba/hw.h"
#include "chislink/gba/text.h"
#include "chislink/gba/video.h"

#include <string.h>

#define BLE_EVENT_COUNT 4u

static example_link_t g_link;
static cl_ble_t g_ble;
static uint8_t g_ble_scratch[CL_BLE_SCRATCH_FULL_BYTES];
static cl_ble_event_t g_events[BLE_EVENT_COUNT];
static uint32_t g_count;
static int g_last_error;
static char g_names[BLE_EVENT_COUNT][18];

static void scan_ble(void) {
    memset(g_events, 0, sizeof(g_events));
    memset(g_names, 0, sizeof(g_names));
    g_count = 0;
    int ret = cl_ble_scan_collect(&g_ble, 1200u, g_events,
                                  BLE_EVENT_COUNT, 120u);
    if (ret < 0) {
        g_last_error = ret;
        return;
    }
    g_count = (uint32_t)ret;
    for (uint32_t i = 0; i < g_count && i < BLE_EVENT_COUNT; ++i) {
        (void)cl_ble_adv_name(&g_events[i], g_names[i], sizeof(g_names[i]));
        if (!g_names[i][0]) {
            g_names[i][0] = '-';
            g_names[i][1] = '\0';
        }
    }
    g_last_error = 0;
}

static char hex_digit(uint8_t value) {
    if (value < 10u) {
        return (char)('0' + (int)value);
    }
    return (char)('A' + (int)value - 10);
}

static void draw_addr(int x, int y, const uint8_t addr[6], uint8_t color) {
    for (uint8_t i = 0; i < 3u; ++i) {
        uint8_t b = addr[5u - i];
        uint8_t hi = (uint8_t)(b >> 4u);
        uint8_t lo = (uint8_t)(b & 0x0fu);
        char out[3];
        out[0] = hex_digit(hi);
        out[1] = hex_digit(lo);
        out[2] = '\0';
        cl_gba_text_draw(x + i * 20, y, out, color);
    }
}

static void draw(void) {
    ex_clear_body();
    cl_gba_text_draw(16, 34, "BLE scan through MCU stack", EX_COLOR_TEXT);
    cl_gba_text_draw(16, 50, "EVENTS", EX_COLOR_DIM);
    ex_draw_u32_dec(80, 50, g_count, EX_COLOR_OK);

    for (uint32_t i = 0; i < BLE_EVENT_COUNT; ++i) {
        int y = 70 + (int)i * 18;
        if (i >= g_count) {
            cl_gba_text_draw(16, y, "-", EX_COLOR_DIM);
            continue;
        }
        ex_draw_i32(16, y, g_events[i].rssi, EX_COLOR_TEXT);
        cl_gba_text_draw(48, y,
                         (g_events[i].flags & CLP_BLE_EVENT_FLAG_CONNECTABLE) ?
                         "CONN" : "ADV",
                         (g_events[i].flags & CLP_BLE_EVENT_FLAG_CONNECTABLE) ?
                         EX_COLOR_OK : EX_COLOR_DIM);
        draw_addr(88, y, g_events[i].addr, EX_COLOR_DIM);
        cl_gba_text_draw(150, y, g_names[i], EX_COLOR_TEXT);
    }

    ex_draw_error(168, 34, g_last_error);
    ex_draw_footer("A SCAN");
    ex_present();
}

int main(void) {
    ex_video_init("SDK BLE SCAN");
    if (ex_link_init(&g_link) && ex_link_hello(&g_link) &&
        cl_ble_init(&g_ble, &g_link.client,
                    g_ble_scratch, sizeof(g_ble_scratch)) == 0) {
        scan_ble();
    } else {
        g_last_error = g_link.last_error ? g_link.last_error : -1;
    }

    while (1) {
        uint16_t pressed = ex_keys_pressed();
        if (pressed & CL_GBA_KEY_A) {
            scan_ble();
        }
        draw();
        cl_gba_wait_vblank();
    }
}
