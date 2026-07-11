#define CHISLINK_FILE_POSIX_NAMES
#include "example_common.h"

#include "chislink/cart_file.h"
#include "chislink/cart_gba.h"
#include "chislink/file.h"
#include "chislink/gba/hw.h"
#include "chislink/gba/text.h"
#include "chislink/gba/video.h"
#include "chislink/proto.h"

#include <string.h>

#define BACKUP_DIR "/sd/.chislink/examples"
#define BACKUP_PATH BACKUP_DIR "/cart-save.sav"
#define WORKSPACE_BYTES 4096u

typedef struct progress_state {
    uint64_t done;
    uint64_t total;
} progress_state_t;

static example_link_t g_link;
static cl_cart_info_t g_info;
static cl_cart_gba_save_probe_t g_probe;
static uint8_t g_workspace[WORKSPACE_BYTES];
static progress_state_t g_progress;
static int g_last_error;
static int g_db_error;
static int g_probe_error;

static int copy_progress(void *ctx, uint64_t done, uint64_t total) {
    progress_state_t *progress = (progress_state_t *)ctx;
    progress->done = done;
    progress->total = total;
    return 0;
}

static const char *save_type_name(uint8_t type) {
    switch (type) {
    case CL_CART_SAVE_SRAM:
        return "SRAM";
    case CL_CART_SAVE_EEPROM:
        return "EEPROM";
    case CL_CART_SAVE_FLASH:
        return "FLASH";
    case CL_CART_SAVE_BATTERYLESS:
        return "BATLESS";
    case CL_CART_SAVE_NONE:
        return "NONE";
    default:
        return "UNKNOWN";
    }
}

static void refresh_info(void) {
    memset(&g_info, 0, sizeof(g_info));
    g_last_error = cl_cart_info(&g_info);
}

static void configure_from_db(void) {
    g_db_error = cl_cart_gba_configure_save_from_gamedb(0);
    refresh_info();
}

static void probe_hardware(void) {
    memset(&g_probe, 0, sizeof(g_probe));
    g_probe_error = cl_cart_gba_probe_save_hardware(&g_probe);
    refresh_info();
}

static void backup_save(void) {
    g_progress.done = 0;
    g_progress.total = g_info.save_size;
    (void)mkdir("/sd/.chislink", 0755);
    (void)mkdir(BACKUP_DIR, 0755);
    g_last_error = cl_file_copy_buffered_progress(
        CL_CART_SAVE_PATH, BACKUP_PATH, CLP_COPY_OVERWRITE,
        g_workspace, sizeof(g_workspace), copy_progress, &g_progress);
    refresh_info();
}

static void draw(void) {
    ex_clear_body();
    cl_gba_text_draw(16, 34, "Cart save backup via cl_file", EX_COLOR_TEXT);

    cl_gba_text_draw(16, 52, "TITLE", EX_COLOR_DIM);
    cl_gba_text_draw(72, 52, g_info.title[0] ? g_info.title : "-",
                     EX_COLOR_TEXT);
    cl_gba_text_draw(16, 68, "CODE", EX_COLOR_DIM);
    cl_gba_text_draw(72, 68, g_info.game_code[0] ? g_info.game_code : "-",
                     EX_COLOR_TEXT);
    cl_gba_text_draw(16, 84, "DB SAVE", EX_COLOR_DIM);
    cl_gba_text_draw(88, 84, save_type_name(g_info.save_type),
                     EX_COLOR_TEXT);
    ex_draw_size(152, 84, g_info.save_size, EX_COLOR_TEXT);

    cl_gba_text_draw(16, 100, "HW SAVE", EX_COLOR_DIM);
    cl_gba_text_draw(88, 100, save_type_name(g_info.detected_save_type),
                     g_info.detected_save_type ? EX_COLOR_OK : EX_COLOR_DIM);
    ex_draw_size(152, 100, g_info.save_hw_size, EX_COLOR_TEXT);

    cl_gba_text_draw(16, 116, "COPY", EX_COLOR_DIM);
    ex_draw_size(72, 116, g_progress.done, EX_COLOR_OK);
    cl_gba_text_draw(128, 116, "/", EX_COLOR_DIM);
    ex_draw_size(144, 116, g_progress.total, EX_COLOR_TEXT);

    cl_gba_text_draw(16, 132, BACKUP_PATH, EX_COLOR_DIM);
    ex_draw_error(168, 34, g_last_error);
    cl_gba_text_draw(168, 52, "DB", EX_COLOR_DIM);
    ex_draw_i32(196, 52, g_db_error, g_db_error ? EX_COLOR_WARN : EX_COLOR_OK);
    cl_gba_text_draw(168, 68, "HW", EX_COLOR_DIM);
    ex_draw_i32(196, 68, g_probe_error,
                g_probe_error ? EX_COLOR_WARN : EX_COLOR_OK);
    ex_draw_footer("A INFO  B BACKUP  L DB  R HW PROBE");
    ex_present();
}

int main(void) {
    ex_video_init("SDK CART BACKUP");
    if (ex_link_init(&g_link) &&
        ex_link_hello(&g_link) &&
        ex_link_register_storage(&g_link) &&
        cl_cart_gba_install_driver(0) == 0 &&
        cl_file_register_cart() == 0) {
        configure_from_db();
    } else {
        g_last_error = g_link.last_error ? g_link.last_error : -1;
    }

    while (1) {
        uint16_t pressed = ex_keys_pressed();
        if (pressed & CL_GBA_KEY_A) {
            refresh_info();
        }
        if (pressed & CL_GBA_KEY_B) {
            backup_save();
        }
        if (pressed & CL_GBA_KEY_L) {
            configure_from_db();
        }
        if (pressed & CL_GBA_KEY_R) {
            probe_hardware();
        }
        draw();
        cl_gba_wait_vblank();
    }
}
