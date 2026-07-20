#include "chislink_bridge.h"

#include <stddef.h>
#include <string.h>

#include "chislink/client.h"
#include "chislink/gba/hw.h"
#include "chislink/gba_sio_transport.h"
#include "chislink/ram_map.h"
#include "chislink/storage_client.h"
#include "chislink/stream.h"

#define REG_DISPCNT (*(volatile uint16_t *)0x04000000u)
#define BG_PALETTE0 (*(volatile uint16_t *)0x05000000u)

#define BRIDGE_EWRAM __attribute__((section(".ewram")))

static BRIDGE_EWRAM cl_client_t s_client;
static BRIDGE_EWRAM cl_gba_sio_transport_t s_transport;
static BRIDGE_EWRAM cl_ram_map_t s_ram_map;
static BRIDGE_EWRAM cl_storage_client_t s_storage;
static BRIDGE_EWRAM chislink_launch_info_t s_launch;
static BRIDGE_EWRAM uint8_t s_storage_scratch[CLP_LAUNCH_VALUE_MAX_BYTES];
static BRIDGE_EWRAM uint64_t s_rom_position;
static BRIDGE_EWRAM uint16_t s_rom_handle;
static BRIDGE_EWRAM uint8_t s_open;
static BRIDGE_EWRAM cl_stream_t s_rom_stream;
static BRIDGE_EWRAM cl_stream_slot_t s_rom_stream_slot;
static BRIDGE_EWRAM uint16_t s_rom_stream_max;
static BRIDGE_EWRAM uint16_t s_state_handle;
static BRIDGE_EWRAM uint32_t s_state_position;
static BRIDGE_EWRAM uint8_t s_state_write;
static BRIDGE_EWRAM char s_state_path[320];
#ifdef CHISLINK_GOOMBACOLOR
static BRIDGE_EWRAM char s_save_path[72];
static BRIDGE_EWRAM uint16_t s_save_handle;
#endif

static void bridge_show_status(uint16_t color) {
    REG_DISPCNT = 0;
    BG_PALETTE0 = color;
}

static void bridge_abort(void) {
    bridge_show_status(0x001fu);
    for (;;) {}
}

static int bridge_parse_launch(uint32_t expected_type, uint32_t length) {
    if (length <= CLP_LAUNCH_VALUE_FIXED_BYTES ||
        length > CLP_LAUNCH_VALUE_MAX_BYTES ||
        clp_load_le32(s_storage_scratch, 4u) != CLP_LAUNCH_VALUE_VERSION) {
        return -1;
    }

    s_launch.type = clp_load_le32(s_storage_scratch + 4u, 4u);
    s_launch.flags = clp_load_le32(s_storage_scratch + 8u, 4u);
    s_launch.size = clp_load_le32(s_storage_scratch + 12u, 4u);
    uint32_t path_length = length - CLP_LAUNCH_VALUE_FIXED_BYTES;
    const uint8_t *path = s_storage_scratch + CLP_LAUNCH_VALUE_FIXED_BYTES;
    if (s_launch.type != expected_type || s_launch.size == 0 ||
        path_length == 0 || path_length > sizeof(s_launch.path) ||
        path[path_length - 1u] != '\0' || path[0] != '/') {
        return -2;
    }
    memcpy(s_launch.path, path, path_length);
    return 0;
}

void chislink_bridge_reinstall_transport(void) {
    cl_gba_sio_transport_install(&s_transport);
}

#ifdef CHISLINK_GOOMBACOLOR
void chislink_bridge_install_graphics_irqs(void (*vblank_handler)(void),
                                           void (*vcount_handler)(void)) {
    uint16_t ime = CL_GBA_REG_IME;
    CL_GBA_REG_IME = 0;
    CL_GBA_REG_IF = 0xffffu;
    cl_gba_irq_set_handler(CL_GBA_IRQ_VBLANK, vblank_handler);
    cl_gba_irq_set_handler(CL_GBA_IRQ_VCOUNT, vcount_handler);
    CL_GBA_REG_DISPSTAT = 0x0008u;
    cl_gba_irq_enable(CL_GBA_IRQ_VBLANK | CL_GBA_IRQ_VCOUNT |
                      CL_GBA_IRQ_SERIAL);
    CL_GBA_REG_IME = ime;
}
#endif

int chislink_bridge_open(uint32_t expected_type) {
    bridge_show_status(0x4000u);
    memset(&s_launch, 0, sizeof(s_launch));
    s_rom_handle = 0;
    s_rom_position = 0;
    s_open = 0;
    memset(&s_rom_stream, 0, sizeof(s_rom_stream));
    memset(&s_rom_stream_slot, 0, sizeof(s_rom_stream_slot));
    s_rom_stream_max = 0;
    s_state_handle = 0;
    s_state_position = 0;
    s_state_write = 0;

    uint32_t idle_word = clp_make_word(
        CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0);
    if (!cl_gba_sio_transport_init_client(&s_transport, &s_client,
                                           idle_word, 0)) {
        return -2;
    }
    chislink_bridge_reinstall_transport();

    int online = 0;
    for (uint32_t attempt = 0; attempt < 8u; ++attempt) {
        if (cl_client_hello(&s_client)) {
            online = 1;
            break;
        }
        cl_gba_wait_vblank();
    }
    if (!online) {
        return -3;
    }
    if (cl_ram_map_init(&s_ram_map, &s_client) < 0 ||
        cl_storage_client_init(&s_storage, &s_client, s_storage_scratch,
                               sizeof(s_storage_scratch)) < 0) {
        return -4;
    }

    uint32_t launch_length = 0;
    int ret = cl_ram_map_get(&s_ram_map,
                             CLP_LAUNCH_MAP_KEY,
                             sizeof(CLP_LAUNCH_MAP_KEY) - 1u,
                             s_storage_scratch,
                             sizeof(s_storage_scratch),
                             &launch_length);
    if (ret < 0 || bridge_parse_launch(expected_type, launch_length) < 0) {
        return ret < 0 ? ret : -5;
    }
    ret = cl_storage_open(&s_storage, s_launch.path, CLP_OPEN_READ,
                          &s_rom_handle);
    if (ret < 0) {
        return -6;
    }
    s_open = 1u;
    return 0;
}

const chislink_launch_info_t *chislink_bridge_launch(void) {
    return s_open ? &s_launch : NULL;
}

#ifdef CHISLINK_GOOMBACOLOR
static void bridge_store_payload_word(void *ctx,
                                      uint32_t offset,
                                      uint32_t word,
                                      uint32_t valid_bytes) {
    uintptr_t address = (uintptr_t)ctx + offset;
    if ((address & 3u) == 0 && valid_bytes == 4u) {
        *(volatile uint32_t *)address = word;
        return;
    }

    for (uint32_t i = 0; i < valid_bytes;) {
        uintptr_t at = address + i;
        volatile uint16_t *half =
            (volatile uint16_t *)(at & ~(uintptr_t)1u);
        uint16_t value = *half;
        if (at & 1u) {
            value = (uint16_t)((value & 0x00ffu) |
                               ((word & 0xffu) << 8u));
            word >>= 8u;
            ++i;
        } else {
            value = (uint16_t)((value & 0xff00u) | (word & 0xffu));
            word >>= 8u;
            ++i;
            if (i < valid_bytes) {
                value = (uint16_t)((value & 0x00ffu) |
                                   ((word & 0xffu) << 8u));
                word >>= 8u;
                ++i;
            }
        }
        *half = value;
    }
}
#endif

static int bridge_read(uint32_t offset, void *dst, uint32_t length,
                       int use_word_writer) {
    if (!s_open || !dst || offset > s_launch.size ||
        length > s_launch.size - offset) {
        return -1;
    }
    if (s_rom_position != offset) {
        int ret = cl_storage_seek(&s_storage, s_rom_handle, offset);
        if (ret < 0) {
            bridge_abort();
        }
        s_rom_position = offset;
    }

    uint8_t *out = (uint8_t *)dst;
    uint32_t done = 0;
    while (done < length) {
        uint32_t chunk = length - done;
        if (chunk > CLP_FRAME_MAX_PAYLOAD_BYTES) {
            chunk = CLP_FRAME_MAX_PAYLOAD_BYTES;
        }
        uint32_t got = 0;
        int ret;
#ifdef CHISLINK_GOOMBACOLOR
        if (use_word_writer) {
            cl_payload_writer_t writer = {
                .store_word = bridge_store_payload_word,
                .ctx = out + done,
            };
            ret = cl_storage_read_with_writer(&s_storage, s_rom_handle,
                                              &writer, chunk, &got);
        } else
#else
        (void)use_word_writer;
#endif
        {
            ret = cl_storage_read(&s_storage, s_rom_handle, out + done,
                                  chunk, &got);
        }
        if (ret < 0 || got != chunk) {
            bridge_abort();
        }
        done += got;
        s_rom_position += got;
    }
    return 0;
}

int chislink_bridge_read(uint32_t offset, void *dst, uint32_t length) {
    return bridge_read(offset, dst, length, 0);
}

static int bridge_stream_open(void *dst, uint32_t length) {
    if (!s_open || !dst || length == 0u || length > UINT16_MAX) return -1;
    cl_stream_config_t config = {
        .buffer = (uint8_t *)dst,
        .buffer_size = length,
        .slots = &s_rom_stream_slot,
        .slot_count = 1u,
        .slot_size = (uint16_t)length,
        .flags = CLP_STREAM_FLAG_RX,
        .profile = CL_STREAM_PROFILE_HIGH_THROUGHPUT,
    };
    if (!cl_stream_init(&s_rom_stream, &config)) return -2;
    int ret = cl_stream_subscribe_file(&s_client, &s_rom_stream,
                                       s_launch.path);
    if (ret < 0) return ret;
    s_rom_stream_max = (uint16_t)length;
    return 0;
}

static int bridge_stream_prepare(uint32_t offset, void *dst,
                                 uint32_t length) {
    if (!s_rom_stream.stream_id) {
        int ret = bridge_stream_open(dst, length);
        if (ret < 0) return ret;
    }
    if (!dst || length == 0u || length > s_rom_stream_max ||
        offset > s_launch.size || length > s_launch.size - offset) return -1;
    if (s_rom_stream.rx_offset != offset) {
        int ret = cl_stream_seek(&s_client, &s_rom_stream, offset);
        if (ret < 0) return ret;
    }
    return 0;
}

int chislink_bridge_read_stream(uint32_t offset, void *dst, uint32_t length) {
    int ret = bridge_stream_prepare(offset, dst, length);
    if (ret < 0) return ret;
    uint32_t got = 0;
    ret = cl_stream_recv_into(&s_client, &s_rom_stream, dst, length, &got);
    return ret < 0 ? ret : got == length ? 0 : -3;
}

#ifdef CHISLINK_GOOMBACOLOR
int chislink_bridge_read_words(uint32_t offset, void *dst, uint32_t length) {
    return bridge_read(offset, dst, length, 1);
}

int chislink_bridge_read_stream_words(uint32_t offset, void *dst,
                                      uint32_t length) {
    int ret = bridge_stream_prepare(offset, dst, length);
    if (ret < 0) return ret;
    cl_payload_writer_t writer = {
        .store_word = bridge_store_payload_word,
        .ctx = dst,
    };
    uint32_t got = 0;
    ret = cl_stream_recv_with_writer(&s_client, &s_rom_stream, &writer,
                                     length, &got);
    return ret < 0 ? ret : got == length ? 0 : -3;
}
#endif

static int bridge_path_append(const char *text, size_t *at) {
    while (text && *text) {
        if (*at + 1u >= sizeof(s_state_path)) return -1;
        s_state_path[(*at)++] = *text++;
    }
    s_state_path[*at] = '\0';
    return 0;
}

static size_t bridge_utf8_width(const char *text, const char *end) {
    uint8_t lead = (uint8_t)*text;
    size_t width = lead < 0x80u ? 1u :
                   (lead & 0xe0u) == 0xc0u ? 2u :
                   (lead & 0xf0u) == 0xe0u ? 3u :
                   (lead & 0xf8u) == 0xf0u ? 4u : 0u;
    if (width == 0u || (size_t)(end - text) < width) return 0u;
    for (size_t i = 1u; i < width; ++i) {
        if (((uint8_t)text[i] & 0xc0u) != 0x80u) return 0u;
    }
    return width;
}

static int bridge_state_build_path(uint8_t slot, int make_dirs) {
    if (!s_open || slot == 0u || slot > CHISLINK_STATE_SLOT_COUNT) return -1;
#ifdef CHISLINK_GOOMBACOLOR
    static const char emulator[] = "goombacolor";
#else
    static const char emulator[] = "pocketnes";
#endif
    size_t at = 0;
    s_state_path[0] = '\0';
    if (bridge_path_append("/sd/.chislink", &at) < 0) return -2;
    if (make_dirs) (void)cl_storage_mkdir(&s_storage, s_state_path);
    if (bridge_path_append("/", &at) < 0 ||
        bridge_path_append(emulator, &at) < 0) return -2;
    if (make_dirs) (void)cl_storage_mkdir(&s_storage, s_state_path);
    if (bridge_path_append("/savestate", &at) < 0) return -2;
    if (make_dirs) (void)cl_storage_mkdir(&s_storage, s_state_path);
    if (bridge_path_append("/", &at) < 0) return -2;

    const char *name = s_launch.path;
    for (const char *p = s_launch.path; *p; ++p) {
        if (*p == '/') name = p + 1;
    }
    const char *end = name;
    const char *dot = NULL;
    while (*end) {
        if (*end == '.') dot = end;
        ++end;
    }
    if (dot && dot != name) end = dot;
    size_t component_start = at;
    while (name < end) {
        uint8_t ch = (uint8_t)*name;
        if (ch < 0x20u || ch == '<' || ch == '>' || ch == ':' ||
            ch == '"' || ch == '/' || ch == '\\' || ch == '|' ||
            ch == '?' || ch == '*') {
            if (at - component_start + 1u > 200u ||
                at + 1u >= sizeof(s_state_path)) break;
            s_state_path[at++] = '_';
            ++name;
            continue;
        }
        size_t width = bridge_utf8_width(name, end);
        if (width == 0u) {
            width = 1u;
            ch = '_';
        }
        if (at - component_start + width > 200u ||
            at + width >= sizeof(s_state_path)) break;
        if (ch == '_') {
            s_state_path[at++] = '_';
        } else {
            memcpy(s_state_path + at, name, width);
            at += width;
        }
        name += width;
    }
    while (at > component_start &&
           (s_state_path[at - 1u] == ' ' || s_state_path[at - 1u] == '.')) {
        --at;
    }
    if (at == component_start && bridge_path_append("game", &at) < 0) {
        return -2;
    }
    s_state_path[at] = '\0';
    if (make_dirs) (void)cl_storage_mkdir(&s_storage, s_state_path);
    if (bridge_path_append("/state-", &at) < 0) return -2;
    if (slot == 10u) s_state_path[at++] = '1';
    s_state_path[at++] = slot == 10u ? '0' : (char)('0' + slot);
    s_state_path[at] = '\0';
    return bridge_path_append(".sav", &at);
}

int chislink_bridge_state_open(uint8_t slot, int write,
                               uint32_t *out_size) {
    if (s_state_handle) (void)chislink_bridge_state_close();
    int ret = bridge_state_build_path(slot, write != 0);
    if (ret < 0) return ret;
    if (out_size) {
        cl_file_stat_t stat;
        ret = cl_storage_stat(&s_storage, s_state_path, &stat);
        if (ret < 0 && !write) return ret;
        *out_size = ret < 0 || stat.size > UINT32_MAX ? 0u : (uint32_t)stat.size;
    }
    uint32_t flags = write ?
        CLP_OPEN_WRITE | CLP_OPEN_CREATE | CLP_OPEN_TRUNCATE : CLP_OPEN_READ;
    ret = cl_storage_open(&s_storage, s_state_path, flags, &s_state_handle);
    if (ret < 0) return ret;
    s_state_position = 0;
    s_state_write = write != 0;
    return 0;
}

int chislink_bridge_state_read(void *dst, uint32_t length,
                               uint32_t *out_length) {
    if (!s_state_handle || s_state_write || !dst || !out_length) return -1;
    int ret = cl_storage_read(&s_storage, s_state_handle, dst, length,
                              out_length);
    if (ret == 0) s_state_position += *out_length;
    return ret;
}

int chislink_bridge_state_write(const void *src, uint32_t length) {
    if (!s_state_handle || !s_state_write || (!src && length)) return -1;
    uint32_t written = 0;
    int ret = cl_storage_write(&s_storage, s_state_handle, src, length,
                               &written);
    if (ret == 0) s_state_position += written;
    return ret < 0 ? ret : written == length ? 0 : -2;
}

int chislink_bridge_state_seek(int32_t relative_offset) {
    if (!s_state_handle) return -1;
    int64_t target = (int64_t)s_state_position + relative_offset;
    if (target < 0 || target > UINT32_MAX) return -2;
    int ret = cl_storage_seek(&s_storage, s_state_handle, (uint32_t)target);
    if (ret == 0) s_state_position = (uint32_t)target;
    return ret;
}

int chislink_bridge_state_close(void) {
    if (!s_state_handle) return 0;
    int ret = 0;
    if (s_state_write) ret = cl_storage_flush(&s_storage, s_state_handle);
    int close_ret = cl_storage_close(&s_storage, s_state_handle);
    s_state_handle = 0;
    s_state_position = 0;
    s_state_write = 0;
    return ret < 0 ? ret : close_ret;
}

int chislink_bridge_state_stat(uint8_t slot, uint32_t *out_size) {
    if (!out_size || bridge_state_build_path(slot, 0) < 0) return -1;
    cl_file_stat_t stat;
    int ret = cl_storage_stat(&s_storage, s_state_path, &stat);
    if (ret < 0 || stat.type != CLP_FILE_REGULAR || stat.size > UINT32_MAX) {
        *out_size = 0;
        return ret < 0 ? ret : -2;
    }
    *out_size = (uint32_t)stat.size;
    return 0;
}

int chislink_bridge_state_remove(uint8_t slot) {
    if (bridge_state_build_path(slot, 0) < 0) return -1;
    return cl_storage_remove(&s_storage, s_state_path);
}

#ifdef CHISLINK_GOOMBACOLOR
static uint32_t bridge_path_hash(const char *path) {
    uint32_t hash = 2166136261u;
    while (path && *path) {
        hash ^= (uint8_t)*path++;
        hash *= 16777619u;
    }
    return hash;
}

static void bridge_build_save_path(void) {
    static const char prefix[] =
        "/sd/.chislink/emulators/goombacolor/";
    static const char hex[] = "0123456789abcdef";
    size_t at = 0;
    while (prefix[at] != '\0') {
        s_save_path[at] = prefix[at];
        ++at;
    }
    uint32_t hash = bridge_path_hash(s_launch.path);
    for (uint32_t shift = 28u;; shift -= 4u) {
        s_save_path[at++] = hex[(hash >> shift) & 0x0fu];
        if (shift == 0u) break;
    }
    memcpy(s_save_path + at, ".sav", 5u);
}

int chislink_bridge_save_open(void) {
    if (!s_open) return -1;
    if (s_save_handle) {
        (void)cl_storage_close(&s_storage, s_save_handle);
        s_save_handle = 0;
    }
    (void)cl_storage_mkdir(&s_storage, "/sd/.chislink");
    (void)cl_storage_mkdir(&s_storage, "/sd/.chislink/emulators");
    (void)cl_storage_mkdir(&s_storage,
                           "/sd/.chislink/emulators/goombacolor");
    bridge_build_save_path();
    return cl_storage_open(&s_storage, s_save_path,
                           CLP_OPEN_READ | CLP_OPEN_WRITE | CLP_OPEN_CREATE,
                           &s_save_handle);
}

int chislink_bridge_save_read(void *dst, uint32_t length,
                              uint32_t *out_length) {
    if (!s_save_handle || !dst || !out_length) return -1;
    if (cl_storage_seek(&s_storage, s_save_handle, 0) < 0) return -2;
    return cl_storage_read(&s_storage, s_save_handle, dst, length, out_length);
}

int chislink_bridge_save_write(uint32_t offset, const void *src,
                               uint32_t length) {
    if (!s_save_handle || (!src && length)) return -1;
    if (cl_storage_seek(&s_storage, s_save_handle, offset) < 0) return -2;
    uint32_t written = 0;
    int ret = cl_storage_write(&s_storage, s_save_handle, src, length,
                               &written);
    if (ret < 0 || written != length) return ret < 0 ? ret : -3;
    return cl_storage_flush(&s_storage, s_save_handle);
}

#endif

void chislink_bridge_close(void) {
    (void)chislink_bridge_state_close();
    if (s_rom_stream.stream_id) {
        (void)cl_stream_close(&s_client, &s_rom_stream);
    }
    s_rom_stream_max = 0;
#ifdef CHISLINK_GOOMBACOLOR
    if (s_save_handle) {
        (void)cl_storage_close(&s_storage, s_save_handle);
    }
    s_save_handle = 0;
#endif
    if (s_open) {
        (void)cl_storage_close(&s_storage, s_rom_handle);
    }
    s_open = 0;
    s_rom_handle = 0;
    s_rom_position = 0;
}
