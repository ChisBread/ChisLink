#include "example_common.h"

#include "chislink/gba/hw.h"
#include "chislink/gba/text.h"
#include "chislink/gba/video.h"
#include "chislink/stream.h"

#include <string.h>

#define STREAM_SLOT_BYTES 4096u
#define STREAM_SLOT_COUNT 2u
#define STREAM_FILE_PATH "/sd/.chislink/examples/example.txt"

static example_link_t g_link;
static cl_stream_t g_stream;
static CL_STREAM_BUFFER(g_stream_buffer, STREAM_SLOT_BYTES * STREAM_SLOT_COUNT);
static CL_STREAM_SLOTS(g_stream_slots, STREAM_SLOT_COUNT);
static uint32_t g_total_read;
static uint32_t g_last_chunk;
static uint32_t g_remote_size;
static uint8_t g_opened;
static int g_last_error;
static char g_preview[29];

static void preview_bytes(const uint8_t *data, uint32_t length) {
    uint32_t n = length < sizeof(g_preview) - 1u ?
        length : sizeof(g_preview) - 1u;
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t ch = data[i];
        g_preview[i] = (ch >= 0x20u && ch < 0x7fu) ? (char)ch : '.';
    }
    g_preview[n] = '\0';
}

static void stream_open_file(void) {
    if (g_opened) {
        (void)cl_stream_close(&g_link.client, &g_stream);
        g_opened = 0;
    }
    cl_stream_config_t config = CL_STREAM_CONFIG(
        g_stream_buffer,
        g_stream_slots,
        STREAM_SLOT_BYTES,
        CLP_STREAM_FLAG_RX | CLP_STREAM_FLAG_RELIABLE,
        CL_STREAM_PROFILE_HIGH_THROUGHPUT);
    if (!cl_stream_init(&g_stream, &config)) {
        g_last_error = -1;
        return;
    }
    g_total_read = 0;
    g_last_chunk = 0;
    g_preview[0] = '\0';
    g_last_error = cl_stream_subscribe_file(&g_link.client, &g_stream,
                                            STREAM_FILE_PATH);
    if (g_last_error == 0) {
        g_remote_size = g_stream.remote_size_low;
        g_opened = 1u;
    }
}

static void stream_recv_one_slot(void) {
    if (!g_opened) {
        stream_open_file();
        if (!g_opened) {
            return;
        }
    }
    g_last_error = cl_stream_recv_slot(&g_link.client, &g_stream);
    if (g_last_error <= 0) {
        return;
    }

    cl_stream_view_t view;
    int ret = cl_stream_consumer_peek(&g_stream, &view);
    if (ret < 0) {
        g_last_error = ret;
        return;
    }
    g_last_chunk = view.length;
    g_total_read += view.length;
    preview_bytes(view.data, view.length);
    g_last_error = cl_stream_consumer_release(&g_stream);
}

static void stream_close_file(void) {
    if (g_opened) {
        g_last_error = cl_stream_close(&g_link.client, &g_stream);
        g_opened = 0;
    }
}

static void draw(void) {
    ex_clear_body();
    cl_gba_text_draw(16, 36, "File stream subscription", EX_COLOR_TEXT);
    cl_gba_text_draw(16, 52, "Path", EX_COLOR_DIM);
    cl_gba_text_draw(64, 52, ".chislink/examples", EX_COLOR_TEXT);

    cl_gba_text_draw(16, 70, "SLOTS", EX_COLOR_DIM);
    ex_draw_u32_dec(72, 70, STREAM_SLOT_COUNT, EX_COLOR_TEXT);
    cl_gba_text_draw(104, 70, "x", EX_COLOR_DIM);
    ex_draw_size(120, 70, STREAM_SLOT_BYTES, EX_COLOR_TEXT);

    cl_gba_text_draw(16, 86, "SIZE", EX_COLOR_DIM);
    ex_draw_size(72, 86, g_remote_size, EX_COLOR_TEXT);
    cl_gba_text_draw(16, 102, "READ", EX_COLOR_DIM);
    ex_draw_size(72, 102, g_total_read, EX_COLOR_OK);
    cl_gba_text_draw(16, 118, "CHUNK", EX_COLOR_DIM);
    ex_draw_size(72, 118, g_last_chunk, EX_COLOR_TEXT);

    cl_gba_text_draw(16, 134, g_preview[0] ? g_preview : "-", EX_COLOR_DIM);
    ex_draw_error(168, 36, g_last_error);
    ex_draw_footer("A OPEN  B RECV SLOT  START CLOSE");
    ex_present();
}

int main(void) {
    ex_video_init("SDK STREAM FILE");
    if (!(ex_link_init(&g_link) && ex_link_hello(&g_link))) {
        g_last_error = g_link.last_error;
    }

    while (1) {
        uint16_t pressed = ex_keys_pressed();
        if (pressed & CL_GBA_KEY_A) {
            stream_open_file();
        }
        if (pressed & CL_GBA_KEY_B) {
            stream_recv_one_slot();
        }
        if (pressed & CL_GBA_KEY_START) {
            stream_close_file();
        }
        draw();
        cl_gba_wait_vblank();
    }
}
