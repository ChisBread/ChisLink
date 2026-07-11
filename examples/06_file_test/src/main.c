#define CHISLINK_FILE_POSIX_NAMES
#include "example_common.h"

#include "chislink/file.h"
#include "chislink/gba/hw.h"
#include "chislink/gba/text.h"
#include "chislink/gba/video.h"
#include "chislink/proto.h"
#include "chislink/stream.h"

#include <string.h>

#define TEST_BASE "/sd/.chislink/examples"
#define TEST_DIR  TEST_BASE "/file_test"
#define TEST_FILE TEST_DIR "/data.txt"
#define TEST_COPY TEST_DIR "/copy.txt"
#define TEST_REN  TEST_DIR "/renamed.txt"
#define TEST_BUF_SIZE 512u

static example_link_t g_link;
static const char *g_test_name;
static int  g_last_error;
static int  g_tests_run;
static int  g_tests_pass;

static int check(int ok, int err) {
    if (!ok) g_last_error = err;
    return ok ? 0 : (err ? err : -1);
}

#define CHK(expr)    do { int _r = (expr); if (_r < 0) return _r; } while(0)
#define CHK0(expr)   do { if (!(expr)) return -1; } while(0)
#define CHK_EQ(a,b)  do { if ((a) != (b)) return -2; } while(0)

/* --- Individual test functions --- */

static int t_mkdir(void) {
    g_test_name = "mkdir";
    (void)mkdir("/sd/.chislink", 0755);
    (void)mkdir(TEST_BASE, 0755);
    return mkdir(TEST_DIR, 0755);
}

static int t_create_write(void) {
    g_test_name = "create+write";
    int fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    CHK0(fd >= 0);
    const char *msg = "Hello ChisLink!\nLine 2\nLine 3\n";
    int n = write(fd, msg, (uint32_t)strlen(msg));
    close(fd);
    return check(n == (int)strlen(msg), -3);
}

static int t_stat(void) {
    g_test_name = "stat";
    cl_posix_stat_t st;
    CHK(stat(TEST_FILE, &st));
    return check(S_ISREG(st.st_type) && st.st_size > 0, -4);
}

static int t_open_read(void) {
    g_test_name = "open+read";
    int fd = open(TEST_FILE, O_RDONLY);
    CHK0(fd >= 0);
    char buf[64];
    int n = read(fd, buf, sizeof(buf) - 1u);
    close(fd);
    CHK0(n > 0);
    buf[n] = '\0';
    return check(strncmp(buf, "Hello ChisLink!", 15) == 0, -5);
}

static int t_seek_tell(void) {
    g_test_name = "seek+tell";
    int fd = open(TEST_FILE, O_RDONLY);
    CHK0(fd >= 0);
    CHK0((int)lseek(fd, 6, SEEK_SET) >= 0);
    uint64_t pos;
    CHK(cl_file_tell((cl_file_t)fd, &pos));
    CHK_EQ(pos, 6);
    char buf[16];
    int n = read(fd, buf, 10u);
    close(fd);
    CHK_EQ(n, 10);
    return check(strncmp(buf, "ChisLink!\n", 10) == 0, -6);
}

static int t_fstat(void) {
    g_test_name = "fstat";
    int fd = open(TEST_FILE, O_RDONLY);
    CHK0(fd >= 0);
    cl_posix_stat_t st;
    int r = fstat(fd, &st);
    close(fd);
    CHK(r);
    return check(S_ISREG(st.st_type) && st.st_size > 0, -7);
}

static int t_access(void) {
    g_test_name = "access";
    CHK(access(TEST_FILE, F_OK));
    CHK(access(TEST_FILE, R_OK));
    return 0;
}

static int t_pread(void) {
    g_test_name = "pread";
    int fd = open(TEST_FILE, O_RDONLY);
    CHK0(fd >= 0);
    char buf[8];
    int n = pread(fd, buf, 6u, 6u);
    CHK_EQ(n, 6);
    buf[6] = '\0';
    int ok = (strncmp(buf, "ChisLi", 6) == 0);
    uint64_t pos = 0;
    cl_file_tell((cl_file_t)fd, &pos);
    close(fd);
    return check(ok && pos == 0, -8);
}

static int t_copy(void) {
    g_test_name = "copy";
    uint8_t buf[TEST_BUF_SIZE];
    CHK(cl_file_copy_buffered_progress(TEST_FILE, TEST_COPY,
                                        CLP_COPY_OVERWRITE,
                                        buf, sizeof(buf), NULL, NULL));
    return access(TEST_COPY, F_OK);
}

static int t_rename(void) {
    g_test_name = "rename";
    CHK(rename(TEST_COPY, TEST_REN));
    CHK0(access(TEST_COPY, F_OK) < 0);
    return access(TEST_REN, F_OK);
}

static int t_opendir(void) {
    g_test_name = "opendir/readdir";
    DIR *d = opendir(TEST_DIR);
    CHK0(d != NULL);
    int found = 0;
    cl_posix_dirent_t *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, "data.txt") == 0 ||
            strcmp(de->d_name, "renamed.txt") == 0) found++;
    }
    closedir(d);
    return check(found >= 2, -11);
}

static int t_ftruncate(void) {
    g_test_name = "ftruncate";
    int fd = open(TEST_FILE, O_WRONLY);
    CHK0(fd >= 0);
    CHK(ftruncate(fd, 5u));
    close(fd);
    cl_posix_stat_t st;
    CHK(stat(TEST_FILE, &st));
    return check(st.st_size == 5, -12);
}

static int t_stream(void) {
    g_test_name = "stream read";
    /* Re-write test file */
    int fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    CHK0(fd >= 0);
    const char *txt = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int wn = write(fd, txt, 40u);
    close(fd);
    CHK0(wn == 40);

    CL_STREAM_BUFFER(sbuf, 1024);
    CL_STREAM_SLOTS(sslots, 4);
    cl_stream_t stream;
    cl_stream_config_t cfg = CL_STREAM_CONFIG(sbuf, sslots, 256, 0,
                                               CL_STREAM_PROFILE_BALANCED);
    CHK0(cl_stream_init(&stream, &cfg));
    CHK(cl_stream_subscribe_file(&g_link.client, &stream, TEST_FILE));

    char rbuf[64];
    uint32_t total = 0;
    int attempts = 0;
    while (total < 40 && attempts < 60) {
        int r = cl_stream_recv_slot(&g_link.client, &stream);
        if (r < 0) { cl_stream_close(&g_link.client, &stream); return r; }
        cl_stream_view_t view;
        int p = cl_stream_consumer_peek(&stream, &view);
        if (p == CL_STREAM_ERR_EMPTY) { attempts++; continue; }
        CHK(p);
        uint32_t take = view.length;
        if (take > sizeof(rbuf) - total) take = (uint32_t)(sizeof(rbuf) - total);
        if (take && view.data) memcpy(rbuf + total, view.data, take);
        total += take;
        cl_stream_consumer_release_bytes(&stream, (uint16_t)take);
        attempts++;
    }
    cl_stream_close(&g_link.client, &stream);
    CHK0(total >= 40);
    return check(strncmp(rbuf, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 40) == 0, -13);
}

static int t_cleanup(void) {
    g_test_name = "cleanup";
    CHK(unlink(TEST_REN));
    CHK(unlink(TEST_FILE));
    (void)unlink(TEST_DIR);
    return 0;
}

/* Test table */
typedef int (*test_fn)(void);
static const test_fn g_tests[] = {
    t_mkdir, t_create_write, t_stat, t_open_read,
    t_seek_tell, t_fstat, t_access, t_pread,
    t_copy, t_rename, t_opendir, t_ftruncate,
    t_stream, t_cleanup,
};
#define NUM_TESTS (sizeof(g_tests) / sizeof(g_tests[0]))

static void draw(void) {
    ex_clear_body();
    cl_gba_text_draw(16, 34, "File API Test", EX_COLOR_TEXT);
    cl_gba_text_draw(16, 50, g_test_name ? g_test_name : "init", EX_COLOR_DIM);
    ex_draw_error(168, 34, g_last_error);

    char buf[16];
    {
        uint32_t n = (uint32_t)g_tests_run;
        uint8_t i = 0;
        if (n >= 100) { buf[i++] = (uint8_t)('0' + n / 100); n %= 100; }
        if (n >= 10 || i > 0) { buf[i++] = (uint8_t)('0' + n / 10); n %= 10; }
        buf[i++] = (uint8_t)('0' + n); buf[i] = '\0';
    }
    cl_gba_text_draw(16, 66, "Tests:", EX_COLOR_DIM);
    cl_gba_text_draw(80, 66, buf, EX_COLOR_TEXT);
    cl_gba_text_draw(16, 78, "Passed:", EX_COLOR_DIM);
    char buf2[8];
    {
        uint32_t n = (uint32_t)g_tests_pass;
        uint8_t i = 0;
        if (n >= 10) { buf2[i++] = (uint8_t)('0' + n / 10); n %= 10; }
        buf2[i++] = (uint8_t)('0' + n); buf2[i] = '\0';
    }
    cl_gba_text_draw(80, 78, buf2,
        g_tests_pass == (int)NUM_TESTS ? EX_COLOR_OK : EX_COLOR_WARN);

    int y = 100;
    const char *result = g_tests_pass == (int)NUM_TESTS ?
        "ALL TESTS PASSED" : "TEST FAILED";
    int color = g_tests_pass == (int)NUM_TESTS ? EX_COLOR_OK : EX_COLOR_WARN;
    cl_gba_text_draw(16, y, result, color);
    ex_draw_footer("RESTART TO RE-RUN");
    ex_present();
}

int main(void) {
    ex_video_init("SDK FILE TEST");
    g_tests_run = 0;
    g_tests_pass = 0;
    g_test_name = "init";
    g_last_error = 0;

    if (!ex_link_init(&g_link) || !ex_link_hello(&g_link) ||
        !ex_link_register_storage(&g_link)) {
        g_last_error = g_link.last_error;
        draw();
        while (1) cl_gba_wait_vblank();
    }

    draw();

    for (uint32_t i = 0; i < NUM_TESTS; ++i) {
        g_tests_run = (int)(i + 1);
        int ret = g_tests[i]();
        if (ret == 0) {
            g_tests_pass = (int)(i + 1);
        } else {
            g_last_error = ret;
            break;  /* stop on first failure */
        }
        draw();
    }

    /* Final draw */
    g_test_name = "done";
    draw();

    while (1) cl_gba_wait_vblank();
}
