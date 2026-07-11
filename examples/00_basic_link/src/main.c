#include "example_common.h"

#include "chislink/gba/hw.h"
#include "chislink/gba/text.h"
#include "chislink/gba/video.h"

static example_link_t g_link;

static const char *state_name(void) {
    switch (cl_client_state(&g_link.client)) {
    case CL_CLIENT_OFFLINE:
        return "OFFLINE";
    case CL_CLIENT_IDLE:
        return "IDLE";
    case CL_CLIENT_BUSY:
        return "BUSY";
    case CL_CLIENT_ERROR:
    default:
        return "ERROR";
    }
}

static void draw(void) {
    ex_clear_body();
    cl_gba_text_draw(16, 36, "Minimal SDK link example", EX_COLOR_TEXT);

    cl_gba_text_draw(16, 60, "STATE", EX_COLOR_DIM);
    cl_gba_text_draw(72, 60, state_name(), EX_COLOR_OK);

    cl_gba_text_draw(16, 76, "CAPS", EX_COLOR_DIM);
    cl_gba_text_draw_u32_hex(72, 76, g_link.caps, EX_COLOR_TEXT);

    cl_gba_text_draw(16, 92, "STATUS", EX_COLOR_DIM);
    ex_draw_u32_dec(72, 92, cl_client_last_status(&g_link.client),
                    EX_COLOR_TEXT);

    ex_draw_error(16, 108, g_link.last_error);
    cl_gba_text_draw(16, 124, "All SDK state is caller-owned.", EX_COLOR_DIM);
    ex_draw_footer("A HELLO/CAPS");
    ex_present();
}

int main(void) {
    ex_video_init("SDK BASIC LINK");
    if (ex_link_init(&g_link)) {
        (void)ex_link_hello(&g_link);
    }

    while (1) {
        uint16_t pressed = ex_keys_pressed();
        if (pressed & CL_GBA_KEY_A) {
            (void)ex_link_hello(&g_link);
        }
        draw();
        cl_gba_wait_vblank();
    }
}
