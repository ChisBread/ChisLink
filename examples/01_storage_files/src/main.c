#define CHISLINK_FILE_POSIX_NAMES
#include "example_common.h"

#include "chislink/file.h"
#include "chislink/gba/hw.h"
#include "chislink/gba/text.h"
#include "chislink/gba/video.h"
#include "chislink/proto.h"

#include <string.h>

#define LIST_ROWS 5u
#define EXAMPLE_FILE "/sd/.chislink/examples/example.txt"

typedef struct list_state {
    char names[LIST_ROWS][28];
    uint64_t sizes[LIST_ROWS];
    uint8_t types[LIST_ROWS];
    uint32_t count;
    uint32_t next;
} list_state_t;

static example_link_t g_link;
static list_state_t g_list;
static uint8_t g_read_buffer[512];
static uint32_t g_read_length;
static int g_last_error;

static void copy_clip(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;
    if (!dst || !dst_size) {
        return;
    }
    while (src && src[i] && i + 1u < dst_size) {
        char ch = src[i];
        dst[i] = (ch >= 0x20 && ch < 0x7f) ? ch : '?';
        i++;
    }
    dst[i] = '\0';
}

static void refresh_list(void) {
    memset(&g_list, 0, sizeof(g_list));
    DIR *d = opendir("/sd");
    if (!d) { g_last_error = -errno; return; }
    cl_posix_dirent_t *de;
    while ((de = readdir(d)) != NULL && g_list.count < LIST_ROWS) {
        copy_clip(g_list.names[g_list.count],
                  sizeof(g_list.names[g_list.count]), de->d_name);
        g_list.sizes[g_list.count] = de->d_size;
        g_list.types[g_list.count] = de->d_type;
        g_list.count++;
    }
    closedir(d);
    g_last_error = 0;
}

static void read_example_file(void) {
    g_read_length = 0;
    memset(g_read_buffer, 0, sizeof(g_read_buffer));
    int fd = open(EXAMPLE_FILE, O_RDONLY);
    if (fd < 0) {
        g_last_error = -errno;
        return;
    }
    int n = read(fd, g_read_buffer, sizeof(g_read_buffer) - 1u);
    if (n < 0) {
        g_last_error = -errno;
        close(fd);
        return;
    }
    close(fd);
    g_read_length = (uint32_t)n;
    g_read_buffer[g_read_length] = '\0';
    g_last_error = 0;
}

static void draw(void) {
    ex_clear_body();
    cl_gba_text_draw(16, 34, "Remote file backends", EX_COLOR_TEXT);
    cl_gba_text_draw(16, 48, "Registered: /sd /littlefs /dev", EX_COLOR_DIM);

    for (uint32_t i = 0; i < LIST_ROWS; ++i) {
        int y = 66 + (int)i * 14;
        if (i >= g_list.count) {
            cl_gba_text_draw(16, y, "-", EX_COLOR_DIM);
            continue;
        }
        cl_gba_text_draw(16, y,
                         g_list.types[i] == CLP_FILE_DIRECTORY ? "D" : "F",
                         g_list.types[i] == CLP_FILE_DIRECTORY ?
                         EX_COLOR_OK : EX_COLOR_DIM);
        cl_gba_text_draw(32, y, g_list.names[i], EX_COLOR_TEXT);
        if (g_list.types[i] != CLP_FILE_DIRECTORY) {
            ex_draw_size(168, y, g_list.sizes[i], EX_COLOR_DIM);
        }
    }

    cl_gba_text_draw(16, 138, "Read .chislink example:", EX_COLOR_DIM);
    if (g_read_length) {
        char line[28];
        copy_clip(line, sizeof(line), (const char *)g_read_buffer);
        cl_gba_text_draw(16, 150, line, EX_COLOR_TEXT);
    }
    ex_draw_error(168, 34, g_last_error);
    ex_draw_footer("A REFRESH  B READ FILE");
    ex_present();
}

int main(void) {
    ex_video_init("SDK STORAGE FILES");
    if (ex_link_init(&g_link) &&
        ex_link_hello(&g_link) &&
        ex_link_register_storage(&g_link)) {
        refresh_list();
    } else {
        g_last_error = g_link.last_error;
    }

    while (1) {
        uint16_t pressed = ex_keys_pressed();
        if (pressed & CL_GBA_KEY_A) {
            refresh_list();
        }
        if (pressed & CL_GBA_KEY_B) {
            read_example_file();
        }
        draw();
        cl_gba_wait_vblank();
    }
}
