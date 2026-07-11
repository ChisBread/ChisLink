#include "chislink/cart_file.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CL_CART_MAX_HANDLES 4u
#define CL_CART_INFO_BYTES ((uint32_t)sizeof(cl_cart_info_t))

typedef struct cl_cart_file_slot {
    cl_cart_file_kind_t kind;
    uint32_t flags;
    uint64_t offset;
    uint64_t target_size;
    uint8_t flushed;
    uint8_t direct_active;
    uint8_t open;
} cl_cart_file_slot_t;

static cl_cart_driver_ops_t g_driver;
static cl_cart_file_slot_t g_slots[CL_CART_MAX_HANDLES];

static int path_eq(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static cl_cart_file_kind_t kind_from_path(const char *path) {
    if (path_eq(path, CL_CART_ROM_PATH)) {
        return CL_CART_FILE_ROM;
    }
    if (path_eq(path, CL_CART_SAVE_PATH)) {
        return CL_CART_FILE_SAVE;
    }
    if (path_eq(path, CL_CART_INFO_PATH)) {
        return CL_CART_FILE_INFO;
    }
    return 0;
}

static cl_cart_file_slot_t *slot_from_local(uint16_t local) {
    if (local == 0 || local > CL_CART_MAX_HANDLES) {
        return 0;
    }
    cl_cart_file_slot_t *slot = &g_slots[local - 1u];
    return slot->open ? slot : 0;
}

static void fill_stat_for_kind(cl_cart_file_kind_t kind,
                               const cl_cart_info_t *info,
                               cl_file_stat_t *out_stat) {
    memset(out_stat, 0, sizeof(*out_stat));
    out_stat->preferred_block_size = CLP_DEFAULT_BLOCK_SIZE;
    out_stat->max_block_size = CLP_DEFAULT_BLOCK_SIZE;
    out_stat->alignment = CLP_DEFAULT_ALIGNMENT;
    out_stat->type = CLP_FILE_REGULAR;

    if (kind == CL_CART_FILE_ROM) {
        out_stat->size = info->rom_size;
        /* program_block_size describes NOR write-buffer granularity. Reads
         * must stay large so ROM dump avoids excessive link transactions. */
        if (info->can_program_rom) {
            out_stat->flags |= CLP_OPEN_WRITE;
        }
        out_stat->flags |= CLP_OPEN_READ;
        return;
    }

    if (kind == CL_CART_FILE_SAVE) {
        out_stat->size = info->save_size;
        if (info->can_read_save) {
            out_stat->flags |= CLP_OPEN_READ;
        }
        if (info->can_write_save) {
            out_stat->flags |= CLP_OPEN_WRITE;
        }
        return;
    }

    out_stat->size = CL_CART_INFO_BYTES;
    out_stat->flags = CLP_OPEN_READ;
}

static int ensure_info(cl_cart_info_t *out_info) {
    if (!out_info) {
        return -1;
    }
    memset(out_info, 0, sizeof(*out_info));
    if (!g_driver.info) {
        return -2;
    }
    return g_driver.info(g_driver.ctx, out_info);
}

int cl_cart_set_driver(const cl_cart_driver_ops_t *driver) {
    for (uint8_t i = 0; i < CL_CART_MAX_HANDLES; ++i) {
        g_slots[i].open = 0;
    }
    if (!driver) {
        memset(&g_driver, 0, sizeof(g_driver));
        return 0;
    }
    g_driver = *driver;
    return 0;
}

static int cart_open(void *ctx, const char *path, uint32_t flags,
                     uint16_t *out_local) {
    (void)ctx;
    if (!path || !out_local) {
        return -1;
    }
    cl_cart_file_kind_t kind = kind_from_path(path);
    if (!kind) {
        return -2;
    }
    if ((flags & CLP_OPEN_WRITE) && kind == CL_CART_FILE_INFO) {
        return -3;
    }

    cl_cart_info_t info;
    int ret = ensure_info(&info);
    if (ret < 0) {
        return ret;
    }
    if (kind == CL_CART_FILE_ROM && (flags & CLP_OPEN_WRITE) &&
        !info.can_program_rom) {
        return -4;
    }
    if (kind == CL_CART_FILE_SAVE) {
        if ((flags & CLP_OPEN_READ) && !info.can_read_save) {
            return -5;
        }
        if ((flags & CLP_OPEN_WRITE) && !info.can_write_save) {
            return -6;
        }
    }

    for (uint16_t i = 0; i < CL_CART_MAX_HANDLES; ++i) {
        if (!g_slots[i].open) {
            g_slots[i].kind = kind;
            g_slots[i].flags = flags;
            g_slots[i].offset = 0;
            g_slots[i].target_size = 0;
            g_slots[i].flushed = 0;
            g_slots[i].direct_active = 0;
            g_slots[i].open = 1;
            *out_local = (uint16_t)(i + 1u);
            return 0;
        }
    }
    return -7;
}

static int cart_close(void *ctx, uint16_t local) {
    (void)ctx;
    cl_cart_file_slot_t *slot = slot_from_local(local);
    if (!slot) {
        return -1;
    }
    if (slot->direct_active && g_driver.release_direct) {
        (void)g_driver.release_direct(g_driver.ctx, slot->kind);
        slot->direct_active = 0;
    }
    int ret = 0;
    if ((slot->flags & CLP_OPEN_WRITE) && !slot->flushed &&
        g_driver.flush && slot->kind != CL_CART_FILE_INFO) {
        ret = g_driver.flush(g_driver.ctx, slot->kind);
        slot->flushed = ret == 0 ? 1u : 0u;
    }
    slot->open = 0;
    return ret;
}

static int cart_read_info(uint64_t offset, void *dst, uint32_t length,
                          uint32_t *out_length) {
    cl_cart_info_t info;
    int ret = ensure_info(&info);
    if (ret < 0) {
        return ret;
    }
    if (offset >= CL_CART_INFO_BYTES) {
        *out_length = 0;
        return 0;
    }
    uint32_t remaining = CL_CART_INFO_BYTES - (uint32_t)offset;
    uint32_t n = remaining < length ? remaining : length;
    memcpy(dst, ((const uint8_t *)&info) + offset, n);
    *out_length = n;
    return 0;
}

static int cart_read(void *ctx, uint16_t local, void *dst, uint32_t length,
                     uint32_t *out_length) {
    (void)ctx;
    if ((!dst && length) || !out_length) {
        return -1;
    }
    cl_cart_file_slot_t *slot = slot_from_local(local);
    if (!slot || !(slot->flags & CLP_OPEN_READ)) {
        return -2;
    }
    if (slot->direct_active) {
        return -4;
    }
    if (slot->kind == CL_CART_FILE_INFO) {
        int ret = cart_read_info(slot->offset, dst, length, out_length);
        if (ret == 0) {
            slot->offset += *out_length;
        }
        return ret;
    }
    if (!g_driver.read) {
        return -3;
    }
    int ret = g_driver.read(g_driver.ctx, slot->kind, slot->offset,
                            dst, length, out_length);
    if (ret == 0) {
        slot->offset += *out_length;
    }
    return ret;
}

static int cart_direct_read(void *ctx,
                            uint16_t local,
                            uint32_t max_length,
                            cl_direct_window_t *out_window) {
    (void)ctx;
    if (!out_window) {
        return -1;
    }
    memset(out_window, 0, sizeof(*out_window));
    cl_cart_file_slot_t *slot = slot_from_local(local);
    if (!slot || !(slot->flags & CLP_OPEN_READ) ||
        slot->kind == CL_CART_FILE_INFO) {
        return -2;
    }
    if (slot->direct_active) {
        return -3;
    }
    if (!g_driver.direct_read) {
        return 0;
    }
    int ret = g_driver.direct_read(g_driver.ctx, slot->kind, slot->offset,
                                   max_length, out_window);
    if (ret < 0) {
        return ret;
    }
    if (out_window->length != 0 && !out_window->data) {
        return -4;
    }
    if (out_window->length > max_length) {
        if (out_window->length != 0 && g_driver.release_direct) {
            (void)g_driver.release_direct(g_driver.ctx, slot->kind);
        }
        memset(out_window, 0, sizeof(*out_window));
        return -5;
    }
    slot->offset += out_window->length;
    slot->direct_active = out_window->length != 0 ? 1u : 0u;
    return 0;
}

static int cart_release_direct(void *ctx, uint16_t local) {
    (void)ctx;
    cl_cart_file_slot_t *slot = slot_from_local(local);
    if (!slot) {
        return -1;
    }
    if (!slot->direct_active) {
        return 0;
    }
    int ret = 0;
    if (g_driver.release_direct) {
        ret = g_driver.release_direct(g_driver.ctx, slot->kind);
    }
    if (ret == 0) {
        slot->direct_active = 0;
    }
    return ret;
}

static int cart_write(void *ctx, uint16_t local, const void *src,
                      uint32_t length, uint32_t *out_length) {
    (void)ctx;
    if ((!src && length) || !out_length) {
        return -1;
    }
    cl_cart_file_slot_t *slot = slot_from_local(local);
    if (!slot || !(slot->flags & CLP_OPEN_WRITE) ||
        slot->kind == CL_CART_FILE_INFO) {
        return -2;
    }
    if (slot->direct_active) {
        return -4;
    }
    if (!g_driver.write) {
        return -3;
    }
    int ret = g_driver.write(g_driver.ctx, slot->kind, slot->offset,
                             src, length, out_length);
    if (ret == 0) {
        slot->offset += *out_length;
        if (*out_length != 0) {
            slot->flushed = 0;
        }
    }
    return ret;
}

static int cart_seek(void *ctx, uint16_t local, uint64_t offset) {
    (void)ctx;
    cl_cart_file_slot_t *slot = slot_from_local(local);
    if (!slot) {
        return -1;
    }
    if (slot->direct_active) {
        return -2;
    }
    slot->offset = offset;
    return 0;
}

static int cart_truncate(void *ctx, uint16_t local, uint64_t size) {
    (void)ctx;
    cl_cart_file_slot_t *slot = slot_from_local(local);
    if (!slot || !(slot->flags & CLP_OPEN_WRITE) ||
        slot->kind == CL_CART_FILE_INFO) {
        return -1;
    }
    if (slot->direct_active) {
        return -2;
    }
    slot->target_size = size;
    slot->flushed = 0;
    if (g_driver.set_size) {
        return g_driver.set_size(g_driver.ctx, slot->kind, size);
    }
    return 0;
}

static int cart_stat(void *ctx, const char *path, cl_file_stat_t *out_stat) {
    (void)ctx;
    if (!out_stat) {
        return -1;
    }
    cl_cart_file_kind_t kind = kind_from_path(path);
    if (!kind) {
        return -2;
    }
    cl_cart_info_t info;
    int ret = ensure_info(&info);
    if (ret < 0) {
        return ret;
    }
    fill_stat_for_kind(kind, &info, out_stat);
    return 0;
}

static int cart_fstat(void *ctx, uint16_t local, cl_file_stat_t *out_stat) {
    (void)ctx;
    if (!out_stat) {
        return -1;
    }
    cl_cart_file_slot_t *slot = slot_from_local(local);
    if (!slot) {
        return -1;
    }
    cl_cart_info_t info;
    int ret = ensure_info(&info);
    if (ret < 0) {
        return ret;
    }
    fill_stat_for_kind(slot->kind, &info, out_stat);
    return 0;
}

static int cart_flush(void *ctx, uint16_t local) {
    (void)ctx;
    cl_cart_file_slot_t *slot = slot_from_local(local);
    if (!slot) {
        return -1;
    }
    if (slot->direct_active) {
        return -2;
    }
    if (!g_driver.flush || slot->kind == CL_CART_FILE_INFO) {
        return 0;
    }
    int ret = g_driver.flush(g_driver.ctx, slot->kind);
    if (ret == 0) {
        slot->flushed = 1u;
    }
    return ret;
}

static const cl_file_backend_ops_t cart_ops = {
    .open = cart_open,
    .close = cart_close,
    .read = cart_read,
    .write = cart_write,
    .direct_read = cart_direct_read,
    .release_direct = cart_release_direct,
    .seek = cart_seek,
    .truncate = cart_truncate,
    .stat = cart_stat,
    .fstat = cart_fstat,
    .flush = cart_flush,
};

int cl_file_register_cart(void) {
    static const cl_file_backend_t cart_backend = {
        .prefix = "/dev/cart/",
        .prefix_length = 10,
        .ops = &cart_ops,
        .ctx = 0,
    };
    return cl_file_register_backend(&cart_backend);
}

int cl_cart_stat_rom(cl_file_stat_t *out_stat) {
    return cart_stat(0, CL_CART_ROM_PATH, out_stat);
}

int cl_cart_stat_save(cl_file_stat_t *out_stat) {
    return cart_stat(0, CL_CART_SAVE_PATH, out_stat);
}

int cl_cart_info(cl_cart_info_t *out_info) {
    return ensure_info(out_info);
}
