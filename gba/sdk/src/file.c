#include "chislink/file.h"

#include "chislink/copy.h"

#include <stddef.h>
#include <string.h>

#define CL_FILE_MAX_BACKENDS 8u
#define CL_FILE_MAX_HANDLES 16u

#ifndef CL_FILE_COPY_BUFFER_SIZE
#define CL_FILE_COPY_BUFFER_SIZE 0  /* no implicit buffer — use cl_file_copy_buffered() */
#endif

#define CL_FILE_LIST_PAGE_DONE (-1000)

typedef struct cl_file_slot {
    const cl_file_backend_t *backend;
    uint16_t local;
    uint64_t offset;
} cl_file_slot_t;

static const cl_file_backend_t *g_backends[CL_FILE_MAX_BACKENDS];
static cl_file_slot_t g_slots[CL_FILE_MAX_HANDLES];

static uint8_t cstr_len_u8(const char *s) {
    uint8_t n = 0;
    while (s && *s && n < 255u) {
        ++s;
        ++n;
    }
    return n;
}

static int cstr_starts_with(const char *s, const char *prefix, uint8_t prefix_len) {
    if (!s || !prefix) {
        return 0;
    }
    for (uint8_t i = 0; i < prefix_len; ++i) {
        if (s[i] == '\0') {
            return 0;
        }
        if (s[i] != prefix[i]) {
            return 0;
        }
    }
    return 1;
}

static const cl_file_backend_t *find_backend(const char *path) {
    const cl_file_backend_t *best = 0;
    uint8_t best_len = 0;
    for (uint8_t i = 0; i < CL_FILE_MAX_BACKENDS; ++i) {
        const cl_file_backend_t *backend = g_backends[i];
        if (!backend || !backend->ops || !backend->prefix) {
            continue;
        }
        uint8_t prefix_len = backend->prefix_length ?
            backend->prefix_length : cstr_len_u8(backend->prefix);
        if (prefix_len >= best_len && cstr_starts_with(path, backend->prefix, prefix_len)) {
            best = backend;
            best_len = prefix_len;
        }
    }
    return best;
}

static int same_copy_domain(const cl_file_backend_t *src,
                            const cl_file_backend_t *dst) {
    return src && dst && src->ops && src->ops->copy &&
           (src == dst || (src->ops == dst->ops && src->ctx == dst->ctx));
}

static int same_backend_domain(const cl_file_backend_t *a,
                               const cl_file_backend_t *b) {
    return a && b && (a == b || (a->ops == b->ops && a->ctx == b->ctx));
}

static cl_file_slot_t *slot_from_handle(cl_file_t file) {
    if (file == 0 || file > CL_FILE_MAX_HANDLES) {
        return 0;
    }
    cl_file_slot_t *slot = &g_slots[file - 1u];
    return slot->backend ? slot : 0;
}

typedef struct list_page_fallback_ctx {
    uint32_t start_index;
    uint32_t max_entries;
    uint32_t seen;
    uint32_t emitted;
    uint32_t next_index;
    cl_file_list_cb callback;
    void *user;
} list_page_fallback_ctx_t;

static int list_page_fallback_cb(const cl_file_dir_entry_t *entry, void *user) {
    list_page_fallback_ctx_t *ctx = (list_page_fallback_ctx_t *)user;
    if (!entry || !ctx || !ctx->callback) {
        return -1;
    }
    uint32_t current = ctx->seen++;
    if (current < ctx->start_index) {
        return 0;
    }
    if (ctx->max_entries && ctx->emitted >= ctx->max_entries) {
        ctx->next_index = current;
        return CL_FILE_LIST_PAGE_DONE;
    }
    int ret = ctx->callback(entry, ctx->user);
    if (ret < 0) {
        return ret;
    }
    ctx->emitted++;
    return 0;
}

int cl_file_register_backend(const cl_file_backend_t *backend) {
    if (!backend || !backend->prefix || !backend->ops || !backend->ops->open) {
        return -1;
    }
    for (uint8_t i = 0; i < CL_FILE_MAX_BACKENDS; ++i) {
        if (!g_backends[i]) {
            g_backends[i] = backend;
            return 0;
        }
    }
    return -2;
}

void cl_file_reset_backends(void) {
    for (uint8_t i = 0; i < CL_FILE_MAX_BACKENDS; ++i) {
        g_backends[i] = 0;
    }
    for (uint8_t i = 0; i < CL_FILE_MAX_HANDLES; ++i) {
        g_slots[i].backend = 0;
        g_slots[i].local = 0;
    }
}

int cl_file_open(const char *path, uint32_t flags, cl_file_t *out_file) {
    if (!path || !out_file) {
        return -1;
    }

    const cl_file_backend_t *backend = find_backend(path);
    if (!backend || !backend->ops || !backend->ops->open) {
        return -2;
    }

    for (uint16_t i = 0; i < CL_FILE_MAX_HANDLES; ++i) {
        if (!g_slots[i].backend) {
            uint16_t local = 0;
            int ret = backend->ops->open(backend->ctx, path, flags, &local);
            if (ret < 0) {
                return ret;
            }
            g_slots[i].backend = backend;
            g_slots[i].local = local;
            g_slots[i].offset = 0;
            *out_file = (cl_file_t)(i + 1u);
            return 0;
        }
    }

    return -3;
}

int cl_file_close(cl_file_t file) {
    cl_file_slot_t *slot = slot_from_handle(file);
    if (!slot) {
        return -1;
    }

    int ret = 0;
    if (slot->backend->ops->close) {
        ret = slot->backend->ops->close(slot->backend->ctx, slot->local);
    }
    slot->backend = 0;
    slot->local = 0;
    return ret;
}

int cl_file_read(cl_file_t file, void *dst, uint32_t length, uint32_t *out_length) {
    cl_file_slot_t *slot = slot_from_handle(file);
    if (!slot || !slot->backend->ops->read) {
        return -1;
    }
    int ret = slot->backend->ops->read(slot->backend->ctx, slot->local,
                                       dst, length, out_length);
    if (ret == 0 && out_length) {
        slot->offset += *out_length;
    }
    return ret;
}

int cl_file_write(cl_file_t file, const void *src, uint32_t length,
                  uint32_t *out_length) {
    cl_file_slot_t *slot = slot_from_handle(file);
    if (!slot || !slot->backend->ops->write) {
        return -1;
    }
    int ret = slot->backend->ops->write(slot->backend->ctx, slot->local,
                                        src, length, out_length);
    if (ret == 0 && out_length) {
        slot->offset += *out_length;
    }
    return ret;
}

int cl_file_seek(cl_file_t file, uint64_t offset) {
    cl_file_slot_t *slot = slot_from_handle(file);
    if (!slot || !slot->backend->ops->seek) {
        return -1;
    }
    int ret = slot->backend->ops->seek(slot->backend->ctx, slot->local, offset);
    if (ret == 0) {
        slot->offset = offset;
    }
    return ret;
}

int cl_file_tell(cl_file_t file, uint64_t *out_offset) {
    if (!out_offset) {
        return -1;
    }
    cl_file_slot_t *slot = slot_from_handle(file);
    if (!slot) {
        return -1;
    }
    *out_offset = slot->offset;
    return 0;
}

static int cl_file_truncate_open(cl_file_t file, uint64_t size) {
    cl_file_slot_t *slot = slot_from_handle(file);
    if (!slot || !slot->backend->ops->truncate) {
        return 0;
    }
    return slot->backend->ops->truncate(slot->backend->ctx, slot->local, size);
}

int cl_file_truncate(cl_file_t file, uint64_t size) {
    return cl_file_truncate_open(file, size);
}

int cl_file_flush(cl_file_t file) {
    cl_file_slot_t *slot = slot_from_handle(file);
    if (!slot || !slot->backend->ops->flush) {
        return 0;
    }
    return slot->backend->ops->flush(slot->backend->ctx, slot->local);
}

int cl_file_stat(const char *path, cl_file_stat_t *out_stat) {
    if (!path || !out_stat) {
        return -1;
    }
    const cl_file_backend_t *backend = find_backend(path);
    if (!backend || !backend->ops || !backend->ops->stat) {
        return -2;
    }
    return backend->ops->stat(backend->ctx, path, out_stat);
}

int cl_file_fstat(cl_file_t file, cl_file_stat_t *out_stat) {
    if (!out_stat) {
        return -1;
    }
    cl_file_slot_t *slot = slot_from_handle(file);
    if (!slot) {
        return -1;
    }
    if (slot->backend->ops->fstat) {
        return slot->backend->ops->fstat(slot->backend->ctx,
                                         slot->local, out_stat);
    }
    /* Backend does not support fstat. */
    return -2;
}

int cl_file_list_dir(const char *path, cl_file_list_cb callback, void *user) {
    if (!path || !callback) {
        return -1;
    }
    const cl_file_backend_t *backend = find_backend(path);
    if (!backend || !backend->ops || !backend->ops->list_dir) {
        return -2;
    }
    return backend->ops->list_dir(backend->ctx, path, callback, user);
}

int cl_file_list_dir_page(const char *path,
                          uint32_t start_index,
                          uint32_t max_entries,
                          cl_file_list_cb callback,
                          void *user,
                          uint32_t *out_next_index,
                          uint32_t *out_entry_count) {
    if (!path || !callback || !out_next_index || !out_entry_count) {
        return -1;
    }
    const cl_file_backend_t *backend = find_backend(path);
    if (!backend || !backend->ops) {
        return -2;
    }
    if (backend->ops->list_page) {
        return backend->ops->list_page(backend->ctx, path, start_index,
                                       max_entries, callback, user,
                                       out_next_index, out_entry_count);
    }
    if (!backend->ops->list_dir) {
        return -2;
    }

    list_page_fallback_ctx_t ctx = {
        .start_index = start_index,
        .max_entries = max_entries,
        .seen = 0,
        .emitted = 0,
        .next_index = CLP_STORAGE_LIST_DONE,
        .callback = callback,
        .user = user,
    };
    int ret = backend->ops->list_dir(backend->ctx, path,
                                     list_page_fallback_cb, &ctx);
    if (ret < 0 && ret != CL_FILE_LIST_PAGE_DONE) {
        return ret;
    }
    if (ret != CL_FILE_LIST_PAGE_DONE) {
        ctx.next_index = CLP_STORAGE_LIST_DONE;
    }
    *out_next_index = ctx.next_index;
    *out_entry_count = ctx.emitted;
    return 0;
}

typedef struct copy_file_ctx {
    cl_file_t file;
} copy_file_ctx_t;

static int copy_read_file(void *ctx, void *dst, uint32_t length,
                          uint32_t *out_length) {
    copy_file_ctx_t *file_ctx = (copy_file_ctx_t *)ctx;
    return cl_file_read(file_ctx->file, dst, length, out_length);
}

static int copy_write_file(void *ctx, const void *src, uint32_t length,
                           uint32_t *out_length) {
    copy_file_ctx_t *file_ctx = (copy_file_ctx_t *)ctx;
    return cl_file_write(file_ctx->file, src, length, out_length);
}

static int copy_direct_read_file(void *ctx,
                                 uint32_t max_length,
                                 cl_direct_window_t *out_window) {
    copy_file_ctx_t *file_ctx = (copy_file_ctx_t *)ctx;
    cl_file_slot_t *slot = slot_from_handle(file_ctx->file);
    if (!slot || !slot->backend->ops->direct_read ||
        !out_window) {
        return -1;
    }
    return slot->backend->ops->direct_read(slot->backend->ctx,
                                           slot->local,
                                           max_length,
                                           out_window);
}

static int copy_direct_release_file(void *ctx) {
    copy_file_ctx_t *file_ctx = (copy_file_ctx_t *)ctx;
    cl_file_slot_t *slot = slot_from_handle(file_ctx->file);
    if (!slot || !slot->backend->ops->release_direct) {
        return 0;
    }
    return slot->backend->ops->release_direct(slot->backend->ctx,
                                              slot->local);
}

static int copy_direct_write_file(void *ctx,
                                  const cl_direct_window_t *window,
                                  uint32_t offset,
                                  uint32_t length,
                                  uint32_t *out_length) {
    copy_file_ctx_t *file_ctx = (copy_file_ctx_t *)ctx;
    cl_file_slot_t *slot = slot_from_handle(file_ctx->file);
    if (!slot || !slot->backend->ops->direct_write) {
        return -1;
    }
    return slot->backend->ops->direct_write(slot->backend->ctx,
                                            slot->local,
                                            window,
                                            offset,
                                            length,
                                            out_length);
}

static int copy_flush_file(void *ctx) {
    copy_file_ctx_t *file_ctx = (copy_file_ctx_t *)ctx;
    cl_file_slot_t *slot = slot_from_handle(file_ctx->file);
    if (!slot || !slot->backend->ops->flush) {
        return 0;
    }
    return slot->backend->ops->flush(slot->backend->ctx, slot->local);
}

int cl_file_copy(const char *src_path, const char *dst_path, uint32_t flags) {
    const cl_file_backend_t *src_backend = find_backend(src_path);
    const cl_file_backend_t *dst_backend = find_backend(dst_path);
    if (!src_backend || !dst_backend) {
        return -2;
    }
    if (same_copy_domain(src_backend, dst_backend)) {
        return src_backend->ops->copy(src_backend->ctx, src_path, dst_path,
                                      flags);
    }

#if CL_FILE_COPY_BUFFER_SIZE > 0
    static uint32_t buffer_words[(CL_FILE_COPY_BUFFER_SIZE + 3u) / 4u];
    return cl_file_copy_buffered(src_path, dst_path, flags, buffer_words,
                                 sizeof(buffer_words));
#else
    return -3;
#endif
}

int cl_file_copy_buffered_progress(const char *src_path,
                                   const char *dst_path,
                                   uint32_t flags,
                                   void *buffer,
                                   uint32_t buffer_size,
                                   cl_copy_progress_fn progress,
                                   void *progress_ctx) {
    if (!src_path || !dst_path || !buffer || buffer_size < 4u) {
        return -1;
    }

    cl_file_stat_t src_stat;
    int ret = cl_file_stat(src_path, &src_stat);
    if (ret < 0) {
        return ret;
    }
    cl_file_stat_t dst_stat;
    int has_dst_stat = cl_file_stat(dst_path, &dst_stat) == 0;

    cl_file_t src_file = 0;
    cl_file_t dst_file = 0;
    ret = cl_file_open(src_path, CLP_OPEN_READ, &src_file);
    if (ret < 0) {
        return ret;
    }

    uint32_t dst_flags = CLP_OPEN_WRITE | CLP_OPEN_CREATE;
    if (flags & CLP_COPY_OVERWRITE) {
        dst_flags |= CLP_OPEN_TRUNCATE;
    }
    ret = cl_file_open(dst_path, dst_flags, &dst_file);
    if (ret < 0) {
        (void)cl_file_close(src_file);
        return ret;
    }
    ret = cl_file_truncate_open(dst_file, src_stat.size);
    if (ret < 0) {
        (void)cl_file_close(dst_file);
        (void)cl_file_close(src_file);
        return ret;
    }
    if (!has_dst_stat && cl_file_fstat(dst_file, &dst_stat) == 0) {
        has_dst_stat = 1;
    }

    copy_file_ctx_t src_ctx = { .file = src_file };
    copy_file_ctx_t dst_ctx = { .file = dst_file };
    cl_copy_source_t source = {
        .read = copy_read_file,
        .direct_read = src_file ?
            (slot_from_handle(src_file)->backend->ops->direct_read ?
             copy_direct_read_file : 0) : 0,
        .release_direct = src_file ?
            (slot_from_handle(src_file)->backend->ops->release_direct ?
             copy_direct_release_file : 0) : 0,
        .ctx = &src_ctx,
        .size = src_stat.size,
        .preferred_block_size = src_stat.preferred_block_size,
        .max_block_size = src_stat.max_block_size,
    };
    cl_copy_sink_t sink = {
        .write = copy_write_file,
        .direct_write = dst_file ?
            (slot_from_handle(dst_file)->backend->ops->direct_write ?
             copy_direct_write_file : 0) : 0,
        .flush = copy_flush_file,
        .ctx = &dst_ctx,
        .size = src_stat.size,
        .preferred_block_size = has_dst_stat ?
            dst_stat.preferred_block_size : src_stat.preferred_block_size,
        .max_block_size = has_dst_stat ?
            dst_stat.max_block_size : src_stat.max_block_size,
    };
    cl_copy_options_t options = {
        .buffer = buffer,
        .buffer_size = buffer_size,
        .block_size = cl_file_default_block_size(&src_stat),
        .progress = progress,
        .progress_ctx = progress_ctx,
        .flush_on_finish = true,
    };

    ret = cl_copy_stream(&source, &sink, &options);
    int close_dst = cl_file_close(dst_file);
    int close_src = cl_file_close(src_file);
    if (ret < 0) {
        return ret;
    }
    if (close_dst < 0) {
        return close_dst;
    }
    return close_src;
}

int cl_file_copy_buffered(const char *src_path, const char *dst_path,
                          uint32_t flags, void *buffer, uint32_t buffer_size) {
    return cl_file_copy_buffered_progress(src_path, dst_path, flags,
                                          buffer, buffer_size, 0, 0);
}

int cl_file_remove(const char *path) {
    if (!path) {
        return -1;
    }
    const cl_file_backend_t *backend = find_backend(path);
    if (!backend || !backend->ops || !backend->ops->remove) {
        return -2;
    }
    return backend->ops->remove(backend->ctx, path);
}

int cl_file_mkdir(const char *path) {
    if (!path) {
        return -1;
    }
    const cl_file_backend_t *backend = find_backend(path);
    if (!backend || !backend->ops || !backend->ops->mkdir) {
        return -2;
    }
    return backend->ops->mkdir(backend->ctx, path);
}

int cl_file_rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) {
        return -1;
    }
    const cl_file_backend_t *old_backend = find_backend(old_path);
    const cl_file_backend_t *new_backend = find_backend(new_path);
    if (!old_backend || !new_backend ||
        !same_backend_domain(old_backend, new_backend) ||
        !old_backend->ops || !old_backend->ops->rename) {
        return -2;
    }
    return old_backend->ops->rename(old_backend->ctx, old_path, new_path);
}

/* ==================================================================
 * POSIX wrapper implementations
 * ================================================================== */

/* --- Global errno --- */
int cl_file_posix_errno = 0;

/* --- Internal helpers --- */

static int posix_set_errno(int err) {
    cl_file_posix_errno = err;
    return -1;
}

static uint32_t map_posix_flags_to_clp(uint32_t flags) {
    uint32_t clp = 0;
    uint32_t acc = flags & 3u;

    if (acc == 0u) {
        clp |= CLP_OPEN_READ;
    } else if (acc == CL_FILE_O_WRONLY) {
        clp |= CLP_OPEN_WRITE;
    } else if (acc == CL_FILE_O_RDWR) {
        clp |= CLP_OPEN_READ | CLP_OPEN_WRITE;
    } else {
        clp |= CLP_OPEN_READ;
    }

    if (flags & CL_FILE_O_CREAT)  clp |= CLP_OPEN_CREATE;
    if (flags & CL_FILE_O_TRUNC)  clp |= CLP_OPEN_TRUNCATE;
    if (flags & CL_FILE_O_APPEND) clp |= CLP_OPEN_APPEND;

    return clp;
}

/* --- Public POSIX API --- */

int cl_posix_errno_from_result(int result) {
    if (result >= 0) return 0;
    switch (result) {
    case -1:  return CL_FILE_EINVAL;
    case -2:  return CL_FILE_ENOENT;
    case -3:  return CL_FILE_EIO;
    case -4:  return CL_FILE_EACCES;
    case -5:  return CL_FILE_EACCES;
    case -6:  return CL_FILE_EACCES;
    case -7:  return CL_FILE_ENFILE;
    default:  return CL_FILE_EIO;
    }
}

cl_posix_fd_t cl_posix_open(const char *path, uint32_t flags) {
    if (!path) {
        return (cl_posix_fd_t)posix_set_errno(CL_FILE_EINVAL);
    }

    cl_file_t file = 0;
    uint32_t clp_flags = map_posix_flags_to_clp(flags);
    int ret = cl_file_open(path, clp_flags, &file);
    if (ret < 0) {
        return (cl_posix_fd_t)posix_set_errno(cl_posix_errno_from_result(ret));
    }

    if ((flags & CL_FILE_O_APPEND) && file) {
        int seek_ret = cl_file_seek(file, UINT64_MAX);
        if (seek_ret < 0) {
            (void)cl_file_close(file);
            return (cl_posix_fd_t)posix_set_errno(CL_FILE_EIO);
        }
    }

    return (cl_posix_fd_t)file;
}

int cl_posix_read(cl_posix_fd_t fd, void *buf, uint32_t count) {
    if (!buf || count == 0) {
        return posix_set_errno(CL_FILE_EINVAL);
    }
    if (fd <= 0) {
        return posix_set_errno(CL_FILE_EBADF);
    }

    uint32_t out_len = 0;
    int ret = cl_file_read((cl_file_t)fd, buf, count, &out_len);
    if (ret < 0) {
        return posix_set_errno(cl_posix_errno_from_result(ret));
    }
    return (int)out_len;
}

int cl_posix_write(cl_posix_fd_t fd, const void *buf, uint32_t count) {
    if (!buf || count == 0) {
        return posix_set_errno(CL_FILE_EINVAL);
    }
    if (fd <= 0) {
        return posix_set_errno(CL_FILE_EBADF);
    }

    uint32_t out_len = 0;
    int ret = cl_file_write((cl_file_t)fd, buf, count, &out_len);
    if (ret < 0) {
        return posix_set_errno(cl_posix_errno_from_result(ret));
    }
    return (int)out_len;
}

int cl_posix_close(cl_posix_fd_t fd) {
    if (fd <= 0) {
        return posix_set_errno(CL_FILE_EBADF);
    }

    int ret = cl_file_close((cl_file_t)fd);
    if (ret < 0) {
        return posix_set_errno(cl_posix_errno_from_result(ret));
    }
    return 0;
}

int64_t cl_posix_lseek(cl_posix_fd_t fd, int64_t offset, int whence) {
    if (fd <= 0) {
        posix_set_errno(CL_FILE_EBADF);
        return (int64_t)-1;
    }

    uint64_t abs_offset;

    switch (whence) {
    case CL_FILE_SEEK_SET:
        if (offset < 0) {
            posix_set_errno(CL_FILE_EINVAL);
            return (int64_t)-1;
        }
        abs_offset = (uint64_t)offset;
        break;

    case CL_FILE_SEEK_CUR: {
        uint64_t current = 0;
        if (cl_file_tell((cl_file_t)fd, &current) < 0) {
            posix_set_errno(cl_posix_errno_from_result(-1));
            return (int64_t)-1;
        }
        if (offset < 0 && (uint64_t)(-offset) > current) {
            abs_offset = 0;
        } else if (offset < 0) {
            abs_offset = current - (uint64_t)(-offset);
        } else {
            abs_offset = current + (uint64_t)offset;
        }
        break;
    }

    case CL_FILE_SEEK_END: {
        if (cl_file_seek((cl_file_t)fd, UINT64_MAX) < 0) {
            posix_set_errno(CL_FILE_EIO);
            return (int64_t)-1;
        }
        uint64_t file_size = 0;
        if (cl_file_tell((cl_file_t)fd, &file_size) < 0) {
            posix_set_errno(CL_FILE_EIO);
            return (int64_t)-1;
        }
        if (offset < 0 && (uint64_t)(-offset) > file_size) {
            abs_offset = 0;
        } else if (offset < 0) {
            abs_offset = file_size - (uint64_t)(-offset);
        } else {
            abs_offset = file_size + (uint64_t)offset;
        }
        break;
    }

    default:
        posix_set_errno(CL_FILE_EINVAL);
        return (int64_t)-1;
    }

    int ret = cl_file_seek((cl_file_t)fd, abs_offset);
    if (ret < 0) {
        posix_set_errno(cl_posix_errno_from_result(ret));
        return (int64_t)-1;
    }

    uint64_t actual = 0;
    (void)cl_file_tell((cl_file_t)fd, &actual);
    return (int64_t)actual;
}

int cl_posix_pread(cl_posix_fd_t fd, void *buf, uint32_t count,
                   uint64_t offset) {
    if (!buf || count == 0) {
        return posix_set_errno(CL_FILE_EINVAL);
    }
    if (fd <= 0) {
        return posix_set_errno(CL_FILE_EBADF);
    }

    /* Save current position, seek to target, read, restore. */
    uint64_t saved = 0;
    if (cl_file_tell((cl_file_t)fd, &saved) < 0) {
        return posix_set_errno(CL_FILE_EIO);
    }
    if (cl_file_seek((cl_file_t)fd, offset) < 0) {
        return posix_set_errno(CL_FILE_EIO);
    }

    uint32_t out_len = 0;
    int ret = cl_file_read((cl_file_t)fd, buf, count, &out_len);

    /* Best-effort position restore — if it fails we still return the read
     * result since the data was already delivered. */
    (void)cl_file_seek((cl_file_t)fd, saved);

    if (ret < 0) {
        return posix_set_errno(cl_posix_errno_from_result(ret));
    }
    return (int)out_len;
}

int cl_posix_pwrite(cl_posix_fd_t fd, const void *buf, uint32_t count,
                    uint64_t offset) {
    if (!buf || count == 0) {
        return posix_set_errno(CL_FILE_EINVAL);
    }
    if (fd <= 0) {
        return posix_set_errno(CL_FILE_EBADF);
    }

    /* Save current position, seek to target, write, restore. */
    uint64_t saved = 0;
    if (cl_file_tell((cl_file_t)fd, &saved) < 0) {
        return posix_set_errno(CL_FILE_EIO);
    }
    if (cl_file_seek((cl_file_t)fd, offset) < 0) {
        return posix_set_errno(CL_FILE_EIO);
    }

    uint32_t out_len = 0;
    int ret = cl_file_write((cl_file_t)fd, buf, count, &out_len);
    (void)cl_file_seek((cl_file_t)fd, saved);

    if (ret < 0) {
        return posix_set_errno(cl_posix_errno_from_result(ret));
    }
    return (int)out_len;
}

int cl_posix_fsync(cl_posix_fd_t fd) {
    if (fd <= 0) {
        return posix_set_errno(CL_FILE_EBADF);
    }

    int ret = cl_file_flush((cl_file_t)fd);
    if (ret < 0) {
        return posix_set_errno(cl_posix_errno_from_result(ret));
    }
    return 0;
}

int cl_posix_ftruncate(cl_posix_fd_t fd, uint64_t length) {
    if (fd <= 0) {
        return posix_set_errno(CL_FILE_EBADF);
    }

    int ret = cl_file_truncate((cl_file_t)fd, length);
    if (ret < 0) {
        return posix_set_errno(cl_posix_errno_from_result(ret));
    }
    return 0;
}

int cl_posix_stat(const char *path, cl_posix_stat_t *buf) {
    if (!path || !buf) {
        return posix_set_errno(CL_FILE_EINVAL);
    }

    cl_file_stat_t fst;
    int ret = cl_file_stat(path, &fst);
    if (ret < 0) {
        return posix_set_errno(cl_posix_errno_from_result(ret));
    }

    buf->st_size = fst.size;
    buf->st_blksize = cl_file_default_block_size(&fst);
    buf->st_type = fst.type;
    buf->st_flags = fst.flags;
    return 0;
}

int cl_posix_unlink(const char *path) {
    if (!path) {
        return posix_set_errno(CL_FILE_EINVAL);
    }

    int ret = cl_file_remove(path);
    if (ret < 0) {
        return posix_set_errno(cl_posix_errno_from_result(ret));
    }
    return 0;
}

int cl_posix_mkdir(const char *path, uint32_t mode) {
    (void)mode;

    if (!path) {
        return posix_set_errno(CL_FILE_EINVAL);
    }

    int ret = cl_file_mkdir(path);
    if (ret < 0) {
        return posix_set_errno(cl_posix_errno_from_result(ret));
    }
    return 0;
}

int cl_posix_rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) {
        return posix_set_errno(CL_FILE_EINVAL);
    }

    int ret = cl_file_rename(old_path, new_path);
    if (ret < 0) {
        return posix_set_errno(cl_posix_errno_from_result(ret));
    }
    return 0;
}

int cl_posix_fstat(cl_posix_fd_t fd, cl_posix_stat_t *buf) {
    if (!buf) {
        return posix_set_errno(CL_FILE_EINVAL);
    }
    if (fd <= 0) {
        return posix_set_errno(CL_FILE_EBADF);
    }

    cl_file_stat_t fst;
    int ret = cl_file_fstat((cl_file_t)fd, &fst);
    if (ret < 0) {
        return posix_set_errno(cl_posix_errno_from_result(ret));
    }

    buf->st_size = fst.size;
    buf->st_blksize = cl_file_default_block_size(&fst);
    buf->st_type = fst.type;
    buf->st_flags = fst.flags;
    return 0;
}

int cl_posix_access(const char *path, int mode) {
    if (!path) {
        return posix_set_errno(CL_FILE_EINVAL);
    }

    cl_file_stat_t fst;
    int ret = cl_file_stat(path, &fst);
    if (ret < 0) {
        return posix_set_errno(CL_FILE_ENOENT);
    }

    /* Check readability: file must have CLP_OPEN_READ in flags */
    if (mode & CL_FILE_R_OK) {
        if (!(fst.flags & CLP_OPEN_READ)) {
            return posix_set_errno(CL_FILE_EACCES);
        }
    }
    /* Check writability: file must have CLP_OPEN_WRITE in flags */
    if (mode & CL_FILE_W_OK) {
        if (!(fst.flags & CLP_OPEN_WRITE)) {
            return posix_set_errno(CL_FILE_EACCES);
        }
    }

    return 0;
}

/* --- Directory stream (opendir / readdir / closedir) --- */

/* Internal callback: buffer one entry into the DIR's raw entries buffer */
typedef struct fill_dir_ctx {
    uint8_t *dst;
    uint32_t capacity;
    uint32_t offset;
} fill_dir_ctx_t;

static int fill_dir_cb(const cl_file_dir_entry_t *entry, void *user) {
    fill_dir_ctx_t *ctx = (fill_dir_ctx_t *)user;
    if (!entry || !entry->name) return 0;
    uint32_t name_len = 0;
    while (entry->name[name_len] && name_len < CL_POSIX_DIR_NAME_MAX) ++name_len;
    uint32_t need = CLP_STORAGE_DIR_ENTRY_BYTES + name_len + 1u;
    if (ctx->offset + need > ctx->capacity) return -1;
    uint8_t *dst = ctx->dst + ctx->offset;
    dst[0] = (uint8_t)name_len;
    dst[1] = entry->type;
    dst[2] = entry->flags;
    dst[3] = 0;
    /* size: 8 bytes LE */
    for (int i = 0; i < 8; i++) dst[4 + i] = (uint8_t)(entry->size >> (i * 8));
    memcpy(dst + CLP_STORAGE_DIR_ENTRY_BYTES, entry->name, name_len);
    dst[CLP_STORAGE_DIR_ENTRY_BYTES + name_len] = '\0';
    ctx->offset += need;
    return 0;
}

static int fetch_dir_page(cl_posix_dir_t *dir) {
    fill_dir_ctx_t ctx = { dir->entries, sizeof(dir->entries), 0 };
    uint32_t next_index = CLP_STORAGE_LIST_DONE;
    uint32_t entry_count = 0;
    int ret = cl_file_list_dir_page(dir->path, dir->next_index, 8u,
                                    fill_dir_cb, &ctx,
                                    &next_index, &entry_count);
    if (ret < 0) return ret;
    dir->next_index = next_index;
    dir->entry_count = entry_count;
    dir->cursor = dir->entries;
    dir->end = dir->entries + ctx.offset;
    return 0;
}

cl_posix_dir_t *cl_posix_opendir(cl_posix_dir_t *dir, const char *path) {
    if (!dir || !path || !path[0]) {
        posix_set_errno(CL_FILE_EINVAL);
        return NULL;
    }

    /* Verify path is a directory via stat */
    cl_file_stat_t fst;
    if (cl_file_stat(path, &fst) < 0 ||
        fst.type != CLP_FILE_DIRECTORY) {
        posix_set_errno(CL_FILE_ENOTDIR);
        return NULL;
    }

    memset(dir, 0, sizeof(*dir));
    {
        uint32_t i = 0;
        while (path[i] && i < sizeof(dir->path) - 1u) {
            dir->path[i] = path[i];
            ++i;
        }
        dir->path[i] = '\0';
    }
    dir->next_index = 0;
    dir->open = 1;

    /* Pre-fetch the first page */
    int ret = fetch_dir_page(dir);
    if (ret < 0) {
        posix_set_errno(cl_posix_errno_from_result(ret));
        dir->open = 0;
        return NULL;
    }

    return dir;
}

cl_posix_dir_t *cl_posix_opendir_auto(const char *path) {
    static cl_posix_dir_t auto_dir;
    return cl_posix_opendir(&auto_dir, path);
}

cl_posix_dirent_t *cl_posix_readdir(cl_posix_dir_t *dir) {
    if (!dir || !dir->open) {
        return NULL;
    }

    /* If current page exhausted, fetch next */
    if (dir->cursor >= dir->end) {
        if (dir->next_index == CLP_STORAGE_LIST_DONE ||
            dir->entry_count == 0) {
            /* No more entries — end of stream, not an error */
            return NULL;
        }
        int ret = fetch_dir_page(dir);
        if (ret < 0) {
            posix_set_errno(cl_posix_errno_from_result(ret));
            return NULL;
        }
        if (dir->entry_count == 0) {
            return NULL;
        }
    }

    /* Parse one entry from raw buffer */
    const uint8_t *raw = dir->cursor;
    uint8_t name_len = raw[0];
    if (name_len == 0) {
        dir->open = 0;
        posix_set_errno(CL_FILE_EIO);
        return NULL;
    }

    dir->current.d_type = raw[1];
    dir->current.d_size = 0;
    for (int i = 0; i < 8; i++) dir->current.d_size |= (uint64_t)raw[4 + i] << (i * 8);
    {
        const uint8_t *name_src = raw + CLP_STORAGE_DIR_ENTRY_BYTES;
        uint32_t i = 0;
        while (i < name_len && i < sizeof(dir->current.d_name) - 1u) {
            dir->current.d_name[i] = (char)name_src[i];
            ++i;
        }
        dir->current.d_name[i] = '\0';
    }

    dir->cursor += CLP_STORAGE_DIR_ENTRY_BYTES + name_len + 1u;

    /* When cursor ≥ end but no more pages available, clear entry_count
     * to signal EOS on next call. */
    return &dir->current;
}

int cl_posix_closedir(cl_posix_dir_t *dir) {
    if (!dir || !dir->open) {
        return posix_set_errno(CL_FILE_EBADF);
    }
    memset(dir, 0, sizeof(*dir));
    return 0;
}
