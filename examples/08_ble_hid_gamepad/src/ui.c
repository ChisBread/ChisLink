#include "ui.h"

#include "example_common.h"
#include "chislink/ble.h"
#include "chislink/gba/hw.h"
#include "chislink/gba/text.h"
#include "chislink/gba/video.h"

#include <string.h>

#define UI_COLOR_A           6u
#define UI_COLOR_B           7u
#define UI_COLOR_ACTIVE      8u
#define UI_COLOR_DPAD        9u
#define UI_COLOR_DPAD_ACTIVE 10u
#define UI_COLOR_SHOULDER    11u
#define UI_COLOR_RUMBLE      12u

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((r & 31u) | ((uint16_t)(g & 31u) << 5u) |
                      ((uint16_t)(b & 31u) << 10u));
}

void ex08_ui_init(void) {
    cl_gba_video_set_palette(UI_COLOR_A, rgb(25, 4, 4));
    cl_gba_video_set_palette(UI_COLOR_B, rgb(4, 9, 25));
    cl_gba_video_set_palette(UI_COLOR_ACTIVE, rgb(29, 22, 4));
    cl_gba_video_set_palette(UI_COLOR_DPAD, rgb(9, 11, 12));
    cl_gba_video_set_palette(UI_COLOR_DPAD_ACTIVE, rgb(3, 18, 8));
    cl_gba_video_set_palette(UI_COLOR_SHOULDER, rgb(7, 16, 20));
    cl_gba_video_set_palette(UI_COLOR_RUMBLE, rgb(26, 10, 3));
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

static void draw_pairing_screen(const ex08_ui_state_t *state) {
    char code[7];
    format_passkey(code, state->pair_code);
    cl_gba_text_draw(16, 36, "Pair Xbox Controller", EX_COLOR_TEXT);
    cl_gba_video_fill_rect(40, 58, 160, 48, EX_COLOR_PANEL);
    cl_gba_video_rect(40, 58, 160, 48, EX_COLOR_DIM);
    cl_gba_text_draw(64, 66, "Compare this code", EX_COLOR_DIM);
    cl_gba_text_draw(96, 84, code, EX_COLOR_TEXT);
    cl_gba_text_draw(32, 116, "Confirm if host matches", EX_COLOR_TEXT);
    ex_draw_error(168, 36, state->last_error);
    ex_draw_footer("A CONFIRM  B REJECT");
}

static void draw_wait_screen(const ex08_ui_state_t *state) {
    cl_gba_text_draw(16, 36, "Xbox HID Gamepad", EX_COLOR_TEXT);
    cl_gba_text_draw(16, 54, "Advertise as Xbox Controller", EX_COLOR_DIM);
    draw_status_row(68, "Connected", state->connected);
    draw_status_row(84, "Encrypted", state->encrypted);
    draw_status_row(100, "Bonded", state->bonded);
    draw_status_row(116, "Subscribed", state->subscribed);
    if (state->pair_action != CL_BLE_PASSKEY_ACTION_NONE &&
        !state->pair_wait_confirm) {
        cl_gba_text_draw(16, 132, "Unsupported pair method", EX_COLOR_WARN);
    } else if (state->connected && !state->encrypted) {
        cl_gba_text_draw(16, 132, "Waiting for secure pair", EX_COLOR_DIM);
    } else if (state->connected && !state->subscribed) {
        cl_gba_text_draw(16, 132, "Waiting for input report", EX_COLOR_DIM);
    } else if (state->seen_disconnect) {
        cl_gba_text_draw(16, 132, "Host disconnected", EX_COLOR_DIM);
        ex_draw_i32(152, 132, state->disconnect_reason, EX_COLOR_DIM);
    } else {
        cl_gba_text_draw(16, 132, "Open Bluetooth settings", EX_COLOR_DIM);
    }
    ex_draw_error(168, 36, state->last_error);
    ex_draw_footer("PAIR FROM HOST");
}

static void draw_gamepad(const ex08_ui_state_t *state) {
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

    cl_gba_text_draw(16, 124, "Rumble", EX_COLOR_DIM);
    cl_gba_text_draw(72, 124, state->rumble_current ? "ON" : "OFF",
                     state->rumble_current ? UI_COLOR_RUMBLE : EX_COLOR_DIM);
    ex_draw_u32_dec(112, 124, state->rumble_strength,
                    state->rumble_strength ? UI_COLOR_RUMBLE : EX_COLOR_DIM);

    cl_gba_text_draw(16, 136, "Bond", EX_COLOR_DIM);
    cl_gba_text_draw(56, 136, state->bonded ? "YES" : "NO",
                     state->bonded ? EX_COLOR_OK : EX_COLOR_WARN);
    cl_gba_text_draw(96, 136, "Auth", EX_COLOR_DIM);
    cl_gba_text_draw(136, 136, state->authenticated ? "YES" : "NO",
                     state->authenticated ? EX_COLOR_OK : EX_COLOR_DIM);
    ex_draw_error(168, 136, state->last_error);
    ex_draw_footer("CONNECTED GAMEPAD");
}

void ex08_ui_draw(const ex08_ui_state_t *state) {
    ex_clear_body();
    if (state && state->pair_wait_confirm) {
        draw_pairing_screen(state);
    } else if (state && state->connected &&
               state->encrypted && state->subscribed) {
        draw_gamepad(state);
    } else if (state) {
        draw_wait_screen(state);
    }
    ex_present();
}
