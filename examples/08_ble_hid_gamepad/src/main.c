#include "example_common.h"

#include "chislink/ble.h"
#include "chislink/gba/hw.h"
#include "chislink/gba/text.h"
#include "chislink/gba/video.h"

#include <string.h>

#define HID_UUID_SERVICE        0x1812u
#define HID_UUID_INFORMATION    0x2a4au
#define HID_UUID_REPORT_MAP     0x2a4bu
#define HID_UUID_CONTROL_POINT  0x2a4cu
#define HID_UUID_REPORT         0x2a4du
#define HID_UUID_PROTOCOL_MODE  0x2a4eu
#define HID_UUID_EXT_REPORT_REF 0x2907u
#define HID_UUID_REPORT_REF     0x2908u

#define DIS_UUID_SERVICE        0x180au
#define DIS_UUID_MANUFACTURER   0x2a29u
#define DIS_UUID_PNP_ID         0x2a50u

#define BAS_UUID_SERVICE        0x180fu
#define BAS_UUID_BATTERY_LEVEL  0x2a19u

#define HID_APPEARANCE_GAMEPAD  0x03c4u
#define HID_REPORT_ID_GAMEPAD   1u
#define HID_REPORT_TYPE_INPUT   1u
#define HID_REPORT_BYTES        8u
#define HID_HAT_NEUTRAL         8u
#define SECURITY_PAIR_DELAY_FRAMES 30u
#define SUBSCRIBE_QUERY_FRAMES  15u
#define BLE_EVENTS_PER_FRAME    16u
#define UI_COLOR_A              6u
#define UI_COLOR_B              7u
#define UI_COLOR_ACTIVE         8u
#define UI_COLOR_DPAD           9u
#define UI_COLOR_DPAD_ACTIVE    10u
#define UI_COLOR_SHOULDER       11u

static const uint8_t HID_REPORT_MAP[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop Controls) */
    0x09, 0x05,        /* Usage (Game Pad) */
    0xa1, 0x01,        /* Collection (Application) */
    0x85, HID_REPORT_ID_GAMEPAD,

    0x05, 0x09,        /* Usage Page (Button) */
    0x19, 0x01,        /* Usage Minimum (Button 1) */
    0x29, 0x0c,        /* Usage Maximum (Button 12) */
    0x15, 0x00,        /* Logical Minimum (0) */
    0x25, 0x01,        /* Logical Maximum (1) */
    0x75, 0x01,        /* Report Size (1) */
    0x95, 0x0c,        /* Report Count (12) */
    0x81, 0x02,        /* Input (Data, Variable, Absolute) */

    0x05, 0x01,        /* Usage Page (Generic Desktop Controls) */
    0x09, 0x39,        /* Usage (Hat switch) */
    0x15, 0x00,        /* Logical Minimum (0) */
    0x25, 0x08,        /* Logical Maximum (8, neutral) */
    0x35, 0x00,        /* Physical Minimum (0) */
    0x46, 0x3b, 0x01,  /* Physical Maximum (315) */
    0x65, 0x14,        /* Unit (English Rotation, degrees) */
    0x75, 0x04,        /* Report Size (4) */
    0x95, 0x01,        /* Report Count (1) */
    0x81, 0x42,        /* Input (Data, Variable, Absolute, Null State) */
    0x65, 0x00,        /* Unit (None) */

    0x05, 0x01,        /* Usage Page (Generic Desktop Controls) */
    0x09, 0x30,        /* Usage (X) */
    0x09, 0x31,        /* Usage (Y) */
    0x09, 0x33,        /* Usage (Rx) */
    0x09, 0x34,        /* Usage (Ry) */
    0x09, 0x32,        /* Usage (Z) */
    0x09, 0x35,        /* Usage (Rz) */
    0x15, 0x81,        /* Logical Minimum (-127) */
    0x25, 0x7f,        /* Logical Maximum (127) */
    0x75, 0x08,        /* Report Size (8) */
    0x95, 0x06,        /* Report Count (6) */
    0x81, 0x02,        /* Input (Data, Variable, Absolute) */
    0xc0,              /* End Collection */
};

static const uint8_t HID_INFORMATION[] = {
    0x11, 0x01,        /* HID 1.11 */
    0x00,              /* Country code */
    0x03,              /* Remote wake and normally connectable */
};

static const uint8_t HID_EXTERNAL_REPORT_REF_BATTERY[] = {
    0x19, 0x2a,        /* Battery Level characteristic UUID */
};

static const uint8_t HID_REPORT_REF[] = {
    HID_REPORT_ID_GAMEPAD,
    HID_REPORT_TYPE_INPUT,
};

static const uint8_t HID_PROTOCOL_MODE_REPORT[] = { 1u };
static const uint8_t HID_EMPTY_REPORT[HID_REPORT_BYTES] = {
    0u,
    (uint8_t)(HID_HAT_NEUTRAL << 4u),
    0u, 0u, 0u, 0u, 0u, 0u,
};
static const uint16_t HID_ADV_UUIDS[] = { HID_UUID_SERVICE };
static const char DIS_MANUFACTURER[] = "ChisLink";
static const uint8_t DIS_PNP_ID[] = {
    0x02,              /* Vendor ID source: USB Implementer's Forum */
    0x09, 0x12,        /* Vendor ID: pid.codes test/example range */
    0x13, 0x00,        /* Product ID */
    0x02, 0x00,        /* Product version */
};
static const uint8_t BAS_LEVEL[] = { 100u };

static example_link_t g_link;
static cl_ble_t g_ble;
static uint8_t g_ble_scratch[CL_BLE_SCRATCH_FULL_BYTES];
static cl_ble_gatts_id_t g_report_chr;
static cl_ble_handle_t g_peer;
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
static uint8_t g_last_report[HID_REPORT_BYTES];

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((r & 31u) | ((uint16_t)(g & 31u) << 5u) |
                      ((uint16_t)(b & 31u) << 10u));
}

static uint16_t load_peer(const cl_ble_event_t *ev) {
    if (!ev || ev->data_length < 3u) {
        return 0;
    }
    return (uint16_t)ev->data[1] | ((uint16_t)ev->data[2] << 8u);
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

static uint16_t load_event_u16(const cl_ble_event_t *ev, uint8_t offset) {
    if (!ev || ev->data_length < (uint8_t)(offset + 2u)) {
        return 0;
    }
    return (uint16_t)ev->data[offset] |
           ((uint16_t)ev->data[offset + 1u] << 8u);
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
        return;
    }

    if (g_pair_action == CL_BLE_PASSKEY_ACTION_NONE) {
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

static int setup_hid(void) {
    cl_ble_gatts_id_t bas_svc;
    cl_ble_gatts_id_t dis_svc;
    cl_ble_gatts_id_t hid_svc;
    cl_ble_gatts_id_t chr;
    cl_ble_gatts_id_t report_map_chr;
    const cl_ble_security_config_t security = {
        .io_caps = CL_BLE_IO_CAPS_DISPLAY_YESNO,
        .flags = CL_BLE_SECURITY_BOND |
                 CL_BLE_SECURITY_MITM |
                 CL_BLE_SECURITY_SC,
        .our_key_dist = CL_BLE_SECURITY_KEY_DIST_ENC,
        .their_key_dist = CL_BLE_SECURITY_KEY_DIST_ENC,
        .security_level = 2u,
    };
    int ret = cl_ble_security_config(&g_ble, &security);
    if (ret < 0) return ret;

    ret = cl_ble_gatts_reset(&g_ble);
    if (ret < 0) return ret;

    ret = cl_ble_gatts_add_service16(&g_ble, BAS_UUID_SERVICE,
                                     CLP_BLE_GATTS_PRIMARY, &bas_svc);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(&g_ble, bas_svc, BAS_UUID_BATTERY_LEVEL,
                                 CLP_BLE_GATTS_CHR_READ,
                                 BAS_LEVEL, sizeof(BAS_LEVEL), &chr);
    if (ret < 0) return ret;

    ret = cl_ble_gatts_add_service16(&g_ble, DIS_UUID_SERVICE,
                                     CLP_BLE_GATTS_PRIMARY, &dis_svc);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(&g_ble, dis_svc, DIS_UUID_MANUFACTURER,
                                 CLP_BLE_GATTS_CHR_READ,
                                 DIS_MANUFACTURER, sizeof(DIS_MANUFACTURER) - 1u,
                                 &chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(&g_ble, dis_svc, DIS_UUID_PNP_ID,
                                 CLP_BLE_GATTS_CHR_READ,
                                 DIS_PNP_ID, sizeof(DIS_PNP_ID),
                                 &chr);
    if (ret < 0) return ret;

    ret = cl_ble_gatts_add_service16(&g_ble, HID_UUID_SERVICE,
                                     CLP_BLE_GATTS_PRIMARY, &hid_svc);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_include(&g_ble, hid_svc, bas_svc);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(&g_ble, hid_svc, HID_UUID_INFORMATION,
                                 CLP_BLE_GATTS_CHR_READ |
                                 CLP_BLE_GATTS_READ_ENC,
                                 HID_INFORMATION, sizeof(HID_INFORMATION),
                                 &chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(&g_ble, hid_svc, HID_UUID_CONTROL_POINT,
                                 CLP_BLE_GATTS_CHR_WRITE_NO_RSP |
                                 CLP_BLE_GATTS_WRITE_ENC,
                                 NULL, 0u, &chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(&g_ble, hid_svc, HID_UUID_REPORT_MAP,
                                 CLP_BLE_GATTS_CHR_READ |
                                 CLP_BLE_GATTS_READ_ENC,
                                 HID_REPORT_MAP, sizeof(HID_REPORT_MAP),
                                 &report_map_chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_desc16(&g_ble, report_map_chr,
                                  HID_UUID_EXT_REPORT_REF,
                                  CLP_BLE_GATTS_ATTR_READ,
                                  HID_EXTERNAL_REPORT_REF_BATTERY,
                                  sizeof(HID_EXTERNAL_REPORT_REF_BATTERY),
                                  &chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(&g_ble, hid_svc, HID_UUID_PROTOCOL_MODE,
                                 CLP_BLE_GATTS_CHR_READ |
                                 CLP_BLE_GATTS_CHR_WRITE_NO_RSP |
                                 CLP_BLE_GATTS_READ_ENC |
                                 CLP_BLE_GATTS_WRITE_ENC,
                                 HID_PROTOCOL_MODE_REPORT,
                                 sizeof(HID_PROTOCOL_MODE_REPORT), &chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_chr16(&g_ble, hid_svc, HID_UUID_REPORT,
                                 CLP_BLE_GATTS_CHR_READ |
                                 CLP_BLE_GATTS_CHR_NOTIFY |
                                 CLP_BLE_GATTS_READ_ENC |
                                 CLP_BLE_GATTS_NOTIFY_ENC,
                                 HID_EMPTY_REPORT, sizeof(HID_EMPTY_REPORT),
                                 &g_report_chr);
    if (ret < 0) return ret;
    ret = cl_ble_gatts_add_desc16(&g_ble, g_report_chr, HID_UUID_REPORT_REF,
                                  CLP_BLE_GATTS_ATTR_READ,
                                  HID_REPORT_REF, sizeof(HID_REPORT_REF),
                                  &chr);
    if (ret < 0) return ret;

    ret = cl_ble_gatts_start(&g_ble);
    if (ret < 0) return ret;

    cl_ble_adv_config_t adv;
    memset(&adv, 0, sizeof(adv));
    adv.flags = CLP_BLE_ADV_FLAG_APPEARANCE;
    adv.appearance = HID_APPEARANCE_GAMEPAD;
    adv.uuid16 = HID_ADV_UUIDS;
    adv.uuid16_count = 1u;
    adv.name = "ChisLink Pad";
    return cl_ble_adv_start(&g_ble, &adv);
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
        } else if (ret == CLP_BLE_EVENT_DISCONNECT) {
            g_disconnect_reason = load_event_u16(&ev, 2u);
            g_seen_disconnect = 1u;
            clear_connection_state();
            g_last_error = 0;
        }
    }
}

static uint8_t hat_from_keys(uint16_t keys) {
    uint8_t up = (keys & CL_GBA_KEY_UP) ? 1u : 0u;
    uint8_t down = (keys & CL_GBA_KEY_DOWN) ? 1u : 0u;
    uint8_t left = (keys & CL_GBA_KEY_LEFT) ? 1u : 0u;
    uint8_t right = (keys & CL_GBA_KEY_RIGHT) ? 1u : 0u;
    if (up && right) return 1u;
    if (down && right) return 3u;
    if (down && left) return 5u;
    if (up && left) return 7u;
    if (up) return 0u;
    if (right) return 2u;
    if (down) return 4u;
    if (left) return 6u;
    return HID_HAT_NEUTRAL;
}

static void build_report(uint16_t keys, uint8_t report[HID_REPORT_BYTES]) {
    memset(report, 0, HID_REPORT_BYTES);
    if (keys & CL_GBA_KEY_A) report[0] |= 0x01u;
    if (keys & CL_GBA_KEY_B) report[0] |= 0x02u;
    if (keys & CL_GBA_KEY_L) report[0] |= 0x04u;
    if (keys & CL_GBA_KEY_R) report[0] |= 0x08u;
    if (keys & CL_GBA_KEY_START) report[0] |= 0x10u;
    if (keys & CL_GBA_KEY_SELECT) report[0] |= 0x20u;
    report[1] = (uint8_t)(hat_from_keys(keys) << 4u);
    report[2] = (keys & CL_GBA_KEY_LEFT) ? 0x81u :
                (keys & CL_GBA_KEY_RIGHT) ? 0x7fu : 0u;
    report[3] = (keys & CL_GBA_KEY_UP) ? 0x81u :
                (keys & CL_GBA_KEY_DOWN) ? 0x7fu : 0u;
}

static void send_report(void) {
    if (!g_peer || !g_subscribed || !g_encrypted) {
        return;
    }
    uint8_t report[HID_REPORT_BYTES];
    build_report(cl_gba_keys_held(), report);
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

static void format_passkey(char out[7], uint32_t value) {
    value %= 1000000u;
    for (int i = 5; i >= 0; --i) {
        out[i] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    out[6] = '\0';
}

static void draw_status_row(int y, const char *label, uint8_t on) {
    cl_gba_text_draw(16, y, label, EX_COLOR_DIM);
    cl_gba_text_draw(104, y, on ? "YES" : "NO",
                     on ? EX_COLOR_OK : EX_COLOR_WARN);
}

static int iabs(int v) {
    return v < 0 ? -v : v;
}

static void tile_set_pixel(uint8_t tile[32], uint8_t x, uint8_t y,
                           uint8_t color) {
    uint8_t *dst = &tile[(uint8_t)(y * 4u + (x >> 1u))];
    if (x & 1u) {
        *dst = (uint8_t)((*dst & 0x0fu) | (uint8_t)(color << 4u));
    } else {
        *dst = (uint8_t)((*dst & 0xf0u) | (color & 0x0fu));
    }
}

static uint8_t letter_pixel(char ch, uint8_t x, uint8_t y) {
    static const uint8_t a[7] = {
        0x0eu, 0x11u, 0x11u, 0x1fu, 0x11u, 0x11u, 0x11u,
    };
    static const uint8_t b[7] = {
        0x1eu, 0x11u, 0x11u, 0x1eu, 0x11u, 0x11u, 0x1eu,
    };
    const uint8_t *rows = ch == 'A' ? a : b;
    if (x >= 5u || y >= 7u) {
        return 0;
    }
    return (rows[y] & (uint8_t)(0x10u >> x)) ? 1u : 0u;
}

static void put_custom_tile(uint8_t tx, uint8_t ty, const uint8_t tile[32]) {
    uint16_t tile_id = cl_gba_video_alloc_text_tile();
    if (!tile_id) {
        return;
    }
    cl_gba_video_load_bg_tile(tile_id, tile);
    cl_gba_video_set_bg_tile(0, tx, ty, tile_id, 0);
}

static void draw_shoulder_button(int x,
                                 int y,
                                 const char *label,
                                 uint8_t active) {
    uint8_t fill = active ? UI_COLOR_ACTIVE : UI_COLOR_SHOULDER;
    cl_gba_video_fill_rect(x, y, 48, 16, fill);
    cl_gba_video_rect(x, y, 48, 16, active ? EX_COLOR_TEXT : EX_COLOR_DIM);
    cl_gba_video_fill_rect(x + 8, y + 8, 32, 8,
                           active ? EX_COLOR_TEXT : EX_COLOR_DIM);
    cl_gba_text_draw(x + 20, y + 4, label, EX_COLOR_BG);
}

static void draw_disc_button(int x,
                             int y,
                             uint8_t diameter,
                             char label,
                             uint8_t base_color,
                             uint8_t active_color,
                             uint8_t active) {
    int radius = diameter / 2;
    int cx = x + radius;
    int cy = y + radius;
    int inner = radius - 3;
    int r2 = radius * radius;
    int inner2 = inner * inner;
    uint8_t fill = active ? active_color : base_color;
    uint8_t edge = active ? EX_COLOR_TEXT : EX_COLOR_DIM;
    uint8_t tiles = (uint8_t)((diameter + 7u) / 8u);
    int letter_x = cx - 2;
    int letter_y = cy - 3;

    for (uint8_t ty = 0; ty < tiles; ++ty) {
        for (uint8_t tx = 0; tx < tiles; ++tx) {
            uint8_t tile[32];
            memset(tile, 0, sizeof(tile));
            for (uint8_t py = 0; py < 8u; ++py) {
                for (uint8_t px = 0; px < 8u; ++px) {
                    int sx = x + (int)tx * 8 + px;
                    int sy = y + (int)ty * 8 + py;
                    int dx = sx - cx;
                    int dy = sy - cy;
                    int dist2 = dx * dx + dy * dy;
                    uint8_t color = 0;
                    if (dist2 <= r2) {
                        color = dist2 >= inner2 ? edge : fill;
                        int lx = sx - letter_x;
                        int ly = sy - letter_y;
                        if (label &&
                            letter_pixel(label, (uint8_t)lx, (uint8_t)ly)) {
                            color = EX_COLOR_BG;
                        }
                    }
                    tile_set_pixel(tile, px, py, color);
                }
            }
            put_custom_tile((uint8_t)(x / 8 + tx), (uint8_t)(y / 8 + ty),
                            tile);
        }
    }
}

static void draw_mini_round_button(int x,
                                   int y,
                                   const char *label,
                                   uint8_t active) {
    draw_disc_button(x, y, 16, label ? label[0] : 0,
                     EX_COLOR_PANEL, UI_COLOR_ACTIVE,
                     active);
}

static void draw_dpad_cross(int x, int y, uint16_t keys) {
    const int size = 48;
    const int cx = x + size / 2;
    const int cy = y + size / 2;
    const int outer = 22;
    const int half = 10;

    for (uint8_t ty = 0; ty < 6u; ++ty) {
        for (uint8_t tx = 0; tx < 6u; ++tx) {
            uint8_t tile[32];
            memset(tile, 0, sizeof(tile));
            for (uint8_t py = 0; py < 8u; ++py) {
                for (uint8_t px = 0; px < 8u; ++px) {
                    int sx = x + (int)tx * 8 + px;
                    int sy = y + (int)ty * 8 + py;
                    int dx = sx - cx;
                    int dy = sy - cy;
                    int ax = iabs(dx);
                    int ay = iabs(dy);
                    uint8_t inside = ((ax <= half && ay <= outer) ||
                                      (ay <= half && ax <= outer)) ? 1u : 0u;
                    uint8_t color = 0;
                    if (inside) {
                        uint8_t active = 0;
                        if (dy < -half && (keys & CL_GBA_KEY_UP)) {
                            active = 1u;
                        } else if (dy > half && (keys & CL_GBA_KEY_DOWN)) {
                            active = 1u;
                        } else if (dx < -half && (keys & CL_GBA_KEY_LEFT)) {
                            active = 1u;
                        } else if (dx > half && (keys & CL_GBA_KEY_RIGHT)) {
                            active = 1u;
                        }
                        uint8_t border = (ax >= outer - 2 ||
                                          ay >= outer - 2 ||
                                          (ax >= half - 1 && ay >= half - 1)) ?
                            1u : 0u;
                        color = border ? EX_COLOR_TEXT :
                            (active ? UI_COLOR_DPAD_ACTIVE : UI_COLOR_DPAD);
                    }
                    tile_set_pixel(tile, px, py, color);
                }
            }
            put_custom_tile((uint8_t)(x / 8 + tx), (uint8_t)(y / 8 + ty),
                            tile);
        }
    }
}

static void draw_pairing_screen(void) {
    char code[7];
    format_passkey(code, g_pair_code);
    cl_gba_text_draw(16, 36, "Pair ChisLink Pad", EX_COLOR_TEXT);
    cl_gba_video_fill_rect(40, 58, 160, 48, EX_COLOR_PANEL);
    cl_gba_video_rect(40, 58, 160, 48, EX_COLOR_DIM);
    cl_gba_text_draw(64, 66, "Compare this code", EX_COLOR_DIM);
    cl_gba_text_draw(96, 84, code, EX_COLOR_TEXT);
    cl_gba_text_draw(32, 116, "Confirm if host matches", EX_COLOR_TEXT);
    ex_draw_error(168, 36, g_last_error);
    ex_draw_footer("A CONFIRM  B REJECT");
}

static void draw_wait_screen(void) {
    cl_gba_text_draw(16, 36, "BLE HID Gamepad", EX_COLOR_TEXT);
    cl_gba_text_draw(16, 54, "Advertise as ChisLink Pad", EX_COLOR_DIM);
    draw_status_row(68, "Connected", g_peer != 0);
    draw_status_row(84, "Encrypted", g_encrypted != 0);
    draw_status_row(100, "Bonded", g_bonded != 0);
    draw_status_row(116, "Subscribed", g_subscribed != 0);
    if (g_pair_action != CL_BLE_PASSKEY_ACTION_NONE &&
        !g_pair_wait_confirm) {
        cl_gba_text_draw(16, 132, "Unsupported pair method", EX_COLOR_WARN);
    } else if (g_peer && !g_encrypted) {
        cl_gba_text_draw(16, 132, "Waiting for secure pair", EX_COLOR_DIM);
    } else if (g_peer && !g_subscribed) {
        cl_gba_text_draw(16, 132, "Waiting for input report", EX_COLOR_DIM);
    } else if (g_seen_disconnect) {
        cl_gba_text_draw(16, 132, "Host disconnected", EX_COLOR_DIM);
        ex_draw_i32(152, 132, g_disconnect_reason, EX_COLOR_DIM);
    } else {
        cl_gba_text_draw(16, 132, "Open Bluetooth settings", EX_COLOR_DIM);
    }
    ex_draw_error(168, 36, g_last_error);
    ex_draw_footer("PAIR FROM HOST");
}

static void draw_gamepad(void) {
    uint16_t keys = cl_gba_keys_held();
    cl_gba_text_draw(16, 32, "ChisLink Pad Ready", EX_COLOR_TEXT);
    cl_gba_text_draw(168, 32, "HID", EX_COLOR_OK);

    draw_shoulder_button(24, 46, "L", (keys & CL_GBA_KEY_L) != 0);
    draw_shoulder_button(168, 46, "R", (keys & CL_GBA_KEY_R) != 0);

    draw_dpad_cross(24, 72, keys);

    draw_mini_round_button(106, 104, NULL,
                           (keys & CL_GBA_KEY_SELECT) != 0);
    draw_mini_round_button(106, 84, NULL,
                           (keys & CL_GBA_KEY_START) != 0);

    draw_disc_button(160, 88, 32, 'B', UI_COLOR_B, UI_COLOR_ACTIVE,
                     (keys & CL_GBA_KEY_B) != 0);
    draw_disc_button(192, 68, 32, 'A', UI_COLOR_A, UI_COLOR_ACTIVE,
                     (keys & CL_GBA_KEY_A) != 0);

    cl_gba_text_draw(16, 136, "Bond", EX_COLOR_DIM);
    cl_gba_text_draw(56, 136, g_bonded ? "YES" : "NO",
                     g_bonded ? EX_COLOR_OK : EX_COLOR_WARN);
    cl_gba_text_draw(96, 136, "Auth", EX_COLOR_DIM);
    cl_gba_text_draw(136, 136, g_authenticated ? "YES" : "NO",
                     g_authenticated ? EX_COLOR_OK : EX_COLOR_DIM);
    ex_draw_error(168, 136, g_last_error);
    ex_draw_footer("CONNECTED GAMEPAD");
}

static void draw(void) {
    ex_clear_body();
    if (g_pair_wait_confirm) {
        draw_pairing_screen();
    } else if (g_peer && g_encrypted && g_subscribed) {
        draw_gamepad();
    } else {
        draw_wait_screen();
    }
    ex_present();
}

int main(void) {
    ex_video_init("BLE HID PAD");
    cl_gba_video_set_palette(UI_COLOR_A, rgb(25, 4, 4));
    cl_gba_video_set_palette(UI_COLOR_B, rgb(4, 9, 25));
    cl_gba_video_set_palette(UI_COLOR_ACTIVE, rgb(29, 22, 4));
    cl_gba_video_set_palette(UI_COLOR_DPAD, rgb(9, 11, 12));
    cl_gba_video_set_palette(UI_COLOR_DPAD_ACTIVE, rgb(3, 18, 8));
    cl_gba_video_set_palette(UI_COLOR_SHOULDER, rgb(7, 16, 20));
    if (!ex_link_init(&g_link) || !ex_link_hello(&g_link)) {
        g_last_error = g_link.last_error ? g_link.last_error : -1;
    } else if (cl_ble_init(&g_ble, &g_link.client,
                          g_ble_scratch, sizeof(g_ble_scratch)) < 0) {
        g_last_error = -2;
    } else {
        g_last_error = setup_hid();
    }

    while (1) {
        uint16_t pressed = ex_keys_pressed();
        poll_ble();
        handle_pair_input(pressed);
        handle_security_tick();
        handle_subscription_tick();
        send_report();
        draw();
        cl_gba_wait_vblank();
    }
}
