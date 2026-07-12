#include "example_common.h"
#include "rumble.h"
#include "ui.h"
#include "xbox_hid.h"

#include "chislink/ble.h"
#include "chislink/gba/hw.h"
#include "chislink/proto.h"

#include <string.h>

#define SECURITY_PAIR_DELAY_FRAMES 30u
#define SUBSCRIBE_QUERY_FRAMES    15u
#define BLE_EVENTS_PER_FRAME      16u

static example_link_t g_link;
static cl_ble_t g_ble;
static uint8_t g_ble_scratch[CL_BLE_SCRATCH_FULL_BYTES];
static cl_ble_gatts_id_t g_report_chr;
static cl_ble_gatts_id_t g_rumble_chr;
static cl_ble_handle_t g_peer;
static ex08_rumble_t g_rumble;
static uint8_t g_subscribed;
static uint8_t g_encrypted;
static uint8_t g_authenticated;
static uint8_t g_bonded;
static uint8_t g_pairing_started;
static uint8_t g_pair_action;
static uint8_t g_pair_wait_confirm;
static uint8_t g_security_delay;
static uint8_t g_subscribe_delay;
static uint32_t g_pair_code;
static uint16_t g_disconnect_reason;
static uint8_t g_seen_disconnect;
static int g_last_error;
static uint8_t g_last_report[XBOX_HID_REPORT_BYTES];

static uint16_t load_peer(const cl_ble_event_t *ev) {
    if (!ev || ev->data_length < 3u) {
        return 0;
    }
    return (uint16_t)ev->data[1] | ((uint16_t)ev->data[2] << 8u);
}

static uint16_t load_event_u16(const cl_ble_event_t *ev, uint8_t offset) {
    if (!ev || ev->data_length < (uint8_t)(offset + 2u)) {
        return 0;
    }
    return (uint16_t)ev->data[offset] |
           ((uint16_t)ev->data[offset + 1u] << 8u);
}

static uint32_t load_event_u32(const cl_ble_event_t *ev, uint8_t offset) {
    if (!ev || ev->data_length < (uint8_t)(offset + 4u)) {
        return 0;
    }
    return (uint32_t)ev->data[offset] |
           ((uint32_t)ev->data[offset + 1u] << 8u) |
           ((uint32_t)ev->data[offset + 2u] << 16u) |
           ((uint32_t)ev->data[offset + 3u] << 24u);
}

static void clear_pair_prompt(void) {
    g_pair_action = CL_BLE_PASSKEY_ACTION_NONE;
    g_pair_wait_confirm = 0;
    g_pair_code = 0;
}

static void apply_conn_flags(uint32_t flags) {
    g_encrypted = (flags & CLP_BLE_CONN_FLAG_ENCRYPTED) ? 1u : 0u;
    g_authenticated = (flags & CLP_BLE_CONN_FLAG_AUTHENTICATED) ? 1u : 0u;
    g_bonded = (flags & CLP_BLE_CONN_FLAG_BONDED) ? 1u : 0u;
}

static void clear_connection_state(void) {
    g_peer = 0;
    g_subscribed = 0;
    g_encrypted = 0;
    g_authenticated = 0;
    g_bonded = 0;
    g_pairing_started = 0;
    g_security_delay = 0;
    g_subscribe_delay = 0;
    clear_pair_prompt();
    memset(g_last_report, 0, sizeof(g_last_report));
    ex08_rumble_stop(&g_rumble);
}

static void refresh_conn_info(void) {
    if (!g_peer) {
        return;
    }
    cl_ble_conn_info_t info;
    int ret = cl_ble_conn_info(&g_ble, g_peer, &info);
    if (ret < 0) {
        g_last_error = ret;
        return;
    }
    if (!info.connected) {
        clear_connection_state();
        g_seen_disconnect = 1u;
        g_disconnect_reason = 0u;
        return;
    }
    apply_conn_flags(info.flags);
}

static void refresh_subscription(void) {
    if (!g_peer || !g_report_chr) {
        return;
    }
    int ret = cl_ble_gatts_subscribed(&g_ble, g_peer, g_report_chr);
    if (ret > 0) {
        g_subscribed = 1u;
        g_last_error = 0;
    } else if (ret == 0) {
        g_subscribed = 0;
    } else {
        g_last_error = ret;
    }
}

static void handle_passkey_event(const cl_ble_event_t *ev) {
    if (!ev || ev->data_length < 3u) {
        return;
    }
    cl_ble_handle_t peer = (cl_ble_handle_t)((uint16_t)ev->data[0] |
                                            ((uint16_t)ev->data[1] << 8u));
    if (peer) {
        g_peer = peer;
    }
    g_pairing_started = 1u;
    g_pair_action = ev->data[2];
    g_pair_code = load_event_u32(ev, 4u);
    g_pair_wait_confirm = 0;

    if (g_pair_action == CL_BLE_PASSKEY_ACTION_NUMCMP) {
        g_pair_wait_confirm = 1u;
    } else if (g_pair_action == CL_BLE_PASSKEY_ACTION_NONE) {
        (void)cl_ble_passkey_reply(&g_ble, g_peer, 0u);
    } else {
        g_last_error = -11;
    }
}

static void handle_pair_input(uint16_t pressed) {
    if (!g_pair_wait_confirm || !g_peer) {
        return;
    }
    if (pressed & CL_GBA_KEY_A) {
        int ret = cl_ble_passkey_reply(&g_ble, g_peer, 1u);
        if (ret < 0) {
            g_last_error = ret;
        }
        g_pair_wait_confirm = 0;
    } else if (pressed & CL_GBA_KEY_B) {
        int ret = cl_ble_passkey_reply(&g_ble, g_peer, 0u);
        if (ret < 0) {
            g_last_error = ret;
        }
        g_pair_wait_confirm = 0;
        g_pairing_started = 0;
    }
}

static void start_pairing(void) {
    if (!g_peer || g_pairing_started) {
        return;
    }
    g_pairing_started = 1u;
    int ret = cl_ble_pair(&g_ble, g_peer, CL_BLE_IO_CAPS_DISPLAY_YESNO, 1u);
    if (ret < 0) {
        g_pairing_started = 0;
        g_last_error = ret;
    }
}

static void handle_security_tick(void) {
    if (!g_peer || g_encrypted || g_pairing_started || g_pair_wait_confirm) {
        return;
    }
    if (g_security_delay < SECURITY_PAIR_DELAY_FRAMES) {
        g_security_delay++;
        return;
    }
    g_security_delay = 0;
    refresh_conn_info();
    if (!g_encrypted) {
        start_pairing();
    }
}

static void handle_subscription_tick(void) {
    if (!g_peer || !g_encrypted || g_subscribed || g_pair_wait_confirm) {
        return;
    }
    if (g_subscribe_delay < SUBSCRIBE_QUERY_FRAMES) {
        g_subscribe_delay++;
        return;
    }
    g_subscribe_delay = 0;
    refresh_subscription();
}

static void handle_rumble_event(const cl_ble_event_t *ev) {
    if (!ev || ev->attr_handle != g_rumble_chr ||
        ev->data_length < CLP_BLE_GATTS_WRITE_EVENT_HEADER_BYTES) {
        return;
    }
    uint16_t value_length = load_event_u16(ev, 2u);
    uint8_t available =
        (uint8_t)(ev->data_length - CLP_BLE_GATTS_WRITE_EVENT_HEADER_BYTES);
    if (value_length > available) {
        value_length = available;
    }
    if (ex08_rumble_handle_xbox_report(
            &g_rumble,
            ev->data + CLP_BLE_GATTS_WRITE_EVENT_HEADER_BYTES,
            value_length)) {
        g_last_error = 0;
    }
}

static void poll_ble(void) {
    for (uint8_t i = 0; i < BLE_EVENTS_PER_FRAME; ++i) {
        cl_ble_event_t ev;
        int ret = cl_ble_event_next(&g_ble, &ev, sizeof(ev.data));
        if (ret < 0) {
            g_last_error = ret;
            return;
        }
        if (ret == 0) {
            return;
        }
        if (ret == CLP_BLE_EVENT_CONNECT && ev.data_length >= 2u) {
            g_peer = (cl_ble_handle_t)((uint16_t)ev.data[0] |
                                       ((uint16_t)ev.data[1] << 8u));
            g_subscribed = 0;
            g_encrypted = 0;
            g_authenticated = 0;
            g_bonded = 0;
            g_pairing_started = 0;
            g_security_delay = 0;
            g_subscribe_delay = 0;
            g_seen_disconnect = 0;
            g_disconnect_reason = 0;
            clear_pair_prompt();
            memset(g_last_report, 0, sizeof(g_last_report));
            ex08_rumble_stop(&g_rumble);
        } else if (ret == CLP_BLE_EVENT_PAIR_COMPLETE &&
                   ev.data_length >= 4u) {
            g_pairing_started = 0;
            clear_pair_prompt();
            if (ev.data[2] == 0u) {
                apply_conn_flags(ev.data[3]);
                g_subscribe_delay = SUBSCRIBE_QUERY_FRAMES;
                g_last_error = 0;
            } else {
                g_encrypted = 0;
                g_authenticated = 0;
                g_bonded = 0;
                g_last_error = -10;
            }
        } else if (ret == CLP_BLE_EVENT_PASSKEY_REQ) {
            handle_passkey_event(&ev);
        } else if (ret == CLP_BLE_EVENT_SUBSCRIBE) {
            cl_ble_handle_t peer = load_peer(&ev);
            if (peer) {
                g_peer = peer;
            }
            if (ev.attr_handle == g_report_chr) {
                g_subscribed = ev.data[0] ? 1u : 0u;
                g_subscribe_delay = 0;
            }
            refresh_conn_info();
        } else if (ret == CLP_BLE_EVENT_GATTS_WRITE) {
            handle_rumble_event(&ev);
        } else if (ret == CLP_BLE_EVENT_DISCONNECT) {
            g_disconnect_reason = load_event_u16(&ev, 2u);
            g_seen_disconnect = 1u;
            clear_connection_state();
            g_last_error = 0;
        }
    }
}

static void send_report(void) {
    if (!g_peer || !g_subscribed || !g_encrypted) {
        return;
    }
    uint8_t report[XBOX_HID_REPORT_BYTES];
    xbox_hid_build_report(cl_gba_keys_held(), report);
    if (memcmp(report, g_last_report, sizeof(report)) == 0) {
        return;
    }
    int ret = cl_ble_gatts_notify(&g_ble, g_peer, g_report_chr,
                                  report, sizeof(report));
    if (ret >= 0) {
        memcpy(g_last_report, report, sizeof(report));
        g_last_error = 0;
    } else {
        g_last_error = ret;
        g_subscribed = 0;
    }
}

static void build_ui_state(ex08_ui_state_t *state) {
    memset(state, 0, sizeof(*state));
    state->connected = g_peer != 0;
    state->encrypted = g_encrypted;
    state->bonded = g_bonded;
    state->authenticated = g_authenticated;
    state->subscribed = g_subscribed;
    state->pair_action = g_pair_action;
    state->pair_wait_confirm = g_pair_wait_confirm;
    state->seen_disconnect = g_seen_disconnect;
    state->rumble_current = g_rumble.current;
    state->rumble_strength = g_rumble.last_strength;
    state->pair_code = g_pair_code;
    state->disconnect_reason = g_disconnect_reason;
    state->last_error = g_last_error;
}

int main(void) {
    ex_video_init("XBOX HID PAD");
    ex08_ui_init();
    ex08_rumble_init(&g_rumble);

    if (!ex_link_init(&g_link) || !ex_link_hello(&g_link)) {
        g_last_error = g_link.last_error ? g_link.last_error : -1;
    } else if (cl_ble_init(&g_ble, &g_link.client,
                          g_ble_scratch, sizeof(g_ble_scratch)) < 0) {
        g_last_error = -2;
    } else {
        g_last_error = xbox_hid_setup(&g_ble, &g_report_chr, &g_rumble_chr);
    }

    while (1) {
        uint16_t pressed = ex_keys_pressed();
        poll_ble();
        handle_pair_input(pressed);
        handle_security_tick();
        handle_subscription_tick();
        send_report();
        ex08_rumble_update(&g_rumble);

        ex08_ui_state_t state;
        build_ui_state(&state);
        ex08_ui_draw(&state);
        cl_gba_wait_vblank();
    }
}
