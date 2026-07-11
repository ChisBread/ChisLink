#include "example_common.h"

#include "chislink/file.h"
#include "chislink/gba/hw.h"
#include "chislink/gba/text.h"
#include "chislink/gba/video.h"
#include "chislink/proto.h"

#include <string.h>

static uint16_t s_last_keys;
static const char *s_title = "CHISLINK SDK";

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((r & 31u) | ((uint16_t)(g & 31u) << 5u) |
                      ((uint16_t)(b & 31u) << 10u));
}

static void draw_header(void) {
    cl_gba_video_fill_rect(0, 0, 240, 24, EX_COLOR_PANEL);
    cl_gba_text_draw(12, 8, s_title, EX_COLOR_TEXT);
}

void ex_video_init(const char *title) {
    s_title = title ? title : "CHISLINK SDK";
    cl_gba_video_mode0_init(EX_COLOR_BG);
    cl_gba_video_set_palette(EX_COLOR_BG, rgb(30, 30, 24));
    cl_gba_video_set_palette(EX_COLOR_PANEL, rgb(21, 23, 18));
    cl_gba_video_set_palette(EX_COLOR_TEXT, rgb(4, 5, 4));
    cl_gba_video_set_palette(EX_COLOR_DIM, rgb(12, 13, 10));
    cl_gba_video_set_palette(EX_COLOR_OK, rgb(3, 18, 8));
    cl_gba_video_set_palette(EX_COLOR_WARN, rgb(31, 7, 3));
    cl_gba_time_init();

    ex_clear_body();
    cl_gba_video_present();
    ex_clear_body();
    cl_gba_video_present();
}

void ex_clear_body(void) {
    cl_gba_video_reset_text_tiles();
    cl_gba_video_clear_text_rect(0, 0, 240, 160);
    cl_gba_video_obj_hide_all();
    draw_header();
    cl_gba_video_fill_rect(0, 24, 240, 136, EX_COLOR_BG);
}

void ex_present(void) {
    cl_gba_video_present();
}

void ex_draw_i32(int x, int y, int32_t value, uint8_t color) {
    char out[12];
    char tmp[10];
    uint8_t pos = 0;
    uint8_t count = 0;
    uint32_t v;

    if (value < 0) {
        out[pos++] = '-';
        v = (uint32_t)(-value);
    } else {
        v = (uint32_t)value;
    }

    do {
        tmp[count++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v && count < sizeof(tmp));
    while (count) {
        out[pos++] = tmp[--count];
    }
    out[pos] = '\0';
    cl_gba_text_draw(x, y, out, color);
}

void ex_draw_u32_dec(int x, int y, uint32_t value, uint8_t color) {
    ex_draw_i32(x, y, (int32_t)value, color);
}

void ex_draw_size(int x, int y, uint64_t value, uint8_t color) {
    static const char *const suffix[] = {"B", "KiB", "MiB", "GiB"};
    uint32_t whole = (uint32_t)value;
    uint32_t frac = 0;
    uint8_t unit = 0;
    while (value >= 1024u && unit < 3u) {
        frac = (uint32_t)(((value % 1024u) * 10u) / 1024u);
        value /= 1024u;
        whole = (uint32_t)value;
        unit++;
    }
    ex_draw_u32_dec(x, y, whole, color);
    int px = x + 8;
    uint32_t n = whole;
    while (n >= 10u) {
        px += 8;
        n /= 10u;
    }
    if (unit && frac) {
        cl_gba_text_draw(px, y, ".", color);
        ex_draw_u32_dec(px + 8, y, frac, color);
        px += 16;
    }
    cl_gba_text_draw(px + 4, y, suffix[unit], color);
}

void ex_draw_ipv4(int x, int y, uint32_t ip, uint8_t color) {
    ex_draw_u32_dec(x, y, (ip >> 24u) & 0xffu, color);
    cl_gba_text_draw(x + 24, y, ".", color);
    ex_draw_u32_dec(x + 32, y, (ip >> 16u) & 0xffu, color);
    cl_gba_text_draw(x + 56, y, ".", color);
    ex_draw_u32_dec(x + 64, y, (ip >> 8u) & 0xffu, color);
    cl_gba_text_draw(x + 88, y, ".", color);
    ex_draw_u32_dec(x + 96, y, ip & 0xffu, color);
}

void ex_draw_error(int x, int y, int error) {
    cl_gba_text_draw(x, y, "ERR", error ? EX_COLOR_WARN : EX_COLOR_DIM);
    ex_draw_i32(x + 32, y, error, error ? EX_COLOR_WARN : EX_COLOR_OK);
}

void ex_draw_footer(const char *text) {
    cl_gba_video_fill_rect(0, 148, 240, 12, EX_COLOR_PANEL);
    cl_gba_text_draw(8, 152, text, EX_COLOR_TEXT);
}

void ex_wait_key_release(void) {
    while (cl_gba_keys_held()) {
        cl_gba_wait_vblank();
    }
    s_last_keys = 0;
}

uint16_t ex_keys_pressed(void) {
    uint16_t keys = cl_gba_keys_held();
    uint16_t pressed = (uint16_t)(keys & (uint16_t)~s_last_keys);
    s_last_keys = keys;
    return pressed;
}

bool ex_link_init(example_link_t *link) {
    if (!link) {
        return false;
    }
    memset(link, 0, sizeof(*link));
    bool ok = cl_gba_sio_transport_init_client(
        &link->transport,
        &link->client,
        clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
        CL_GBA_SIO_TRANSPORT_DEFAULT_TIMEOUT_TICKS);
    if (!ok) {
        link->last_error = -1;
        return false;
    }
    cl_gba_sio_transport_install(&link->transport);
    return true;
}

bool ex_link_hello(example_link_t *link) {
    if (!link) {
        return false;
    }
    if (!cl_client_hello(&link->client)) {
        link->last_error = -2;
        return false;
    }
    if (!cl_client_request_caps(&link->client)) {
        link->last_error = -3;
        return false;
    }
    link->caps = cl_client_caps(&link->client);
    link->last_error = 0;
    return true;
}

bool ex_link_register_storage(example_link_t *link) {
    if (!link) {
        return false;
    }
    if (link->storage_registered) {
        return true;
    }
    int ret = cl_storage_client_init(&link->storage, &link->client,
                                      link->storage_scratch,
                                      sizeof(link->storage_scratch));
    if (ret < 0) {
        link->last_error = ret;
        return false;
    }
    cl_file_reset_backends();
    ret = cl_file_register_remote_storage(&link->storage);
    if (ret < 0) {
        link->last_error = ret;
        return false;
    }
    link->storage_registered = true;
    link->last_error = 0;
    return true;
}
