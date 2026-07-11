#ifndef CHISLINK_FILE_H
#define CHISLINK_FILE_H

#include <stdbool.h>
#include <stdint.h>

#include "chislink/copy.h"
#include "chislink/proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file file.h
 * @brief Unified file I/O interface with pluggable backends.
 *
 * The file layer provides a POSIX-like API (open, read, write, seek, tell,
 * stat, etc.) that dispatches to registered backends by path prefix.
 * Backends implement cl_file_backend_ops_t and are registered with
 * cl_file_register_backend().
 *
 * ## Handle type
 * cl_file_t is a 1-based uint16_t handle.  0 (zero) is the invalid sentinel.
 * Handles are allocated from a fixed-size internal pool (16 slots).
 *
 * ## Return values
 * All functions return 0 on success, negative on error.  Specific error codes
 * are backend-dependent; the common ones are:
 * - -1: invalid argument (NULL path, NULL out parameter, invalid handle)
 * - -2: no backend found for path / unsupported operation
 * - -3: out of handles / protocol-level error
 *
 * ## Directory listing
 * cl_file_dir_entry_t.name is valid only within the callback invocation.
 * Do not save the pointer — it points into backend-owned memory that may be
 * reused or freed after the callback returns.
 *
 * ## Memory ownership
 * - All structs are caller-allocated (stack or static).
 * - Backend structs must remain valid for the lifetime of the registration.
 * - No heap allocations are performed by this layer.
 */

/** Opaque file handle (0 = invalid). */
typedef uint16_t cl_file_t;

/** File metadata. */
typedef struct cl_file_stat {
    uint64_t size;                 /**< File size in bytes. */
    uint32_t preferred_block_size; /**< Optimal I/O block size. */
    uint32_t max_block_size;       /**< Maximum single-operation size. */
    uint16_t alignment;            /**< Required buffer alignment. */
    uint8_t type;                  /**< clp_file_type_t (regular, dir, ...). */
    uint8_t flags;                 /**< Permission / capability flags. */
} cl_file_stat_t;

/** Directory entry passed to list callbacks.
 *  @warning name is valid only during the callback. */
typedef struct cl_file_dir_entry {
    const char *name;    /**< Entry name (not a full path). */
    uint64_t size;       /**< File size (0 for directories). */
    uint8_t type;        /**< clp_file_type_t. */
    uint8_t flags;       /**< clp_file_meta_flags. */
} cl_file_dir_entry_t;

/** Callback for cl_file_list_dir and cl_file_list_dir_page.
 *  @param entry  Directory entry (name is transient!).
 *  @param user   Opaque user pointer.
 *  @return 0 to continue, negative to abort iteration. */
typedef int (*cl_file_list_cb)(const cl_file_dir_entry_t *entry, void *user);

/** Paginated directory listing function signature (used by backends). */
typedef int (*cl_file_list_page_fn)(void *ctx,
                                    const char *path,
                                    uint32_t start_index,
                                    uint32_t max_entries,
                                    cl_file_list_cb callback,
                                    void *user,
                                    uint32_t *out_next_index,
                                    uint32_t *out_entry_count);

typedef struct cl_file_backend cl_file_backend_t;

/** Backend operations vtable.
 *
 *  Only open() is mandatory; all others are optional depending on the
 *  backend's capabilities.  Leave unsupported ops as NULL. */
typedef struct cl_file_backend_ops {
    /** Open a file.  Returns a backend-local handle in *out_local. */
    int (*open)(void *ctx, const char *path, uint32_t flags, uint16_t *out_local);
    /** Close a file.  May be NULL if the backend has no per-handle state. */
    int (*close)(void *ctx, uint16_t local);
    /** Read from an open file.  Sets *out_length to bytes read. */
    int (*read)(void *ctx, uint16_t local, void *dst, uint32_t length,
                uint32_t *out_length);
    /** Write to an open file.  Sets *out_length to bytes written. */
    int (*write)(void *ctx, uint16_t local, const void *src, uint32_t length,
                 uint32_t *out_length);
    /** Acquire a zero-copy read window. */
    int (*direct_read)(void *ctx, uint16_t local, uint32_t max_length,
                       cl_direct_window_t *out_window);
    /** Release a previously-acquired direct read window. */
    int (*release_direct)(void *ctx, uint16_t local);
    /** Zero-copy write from a direct window. */
    int (*direct_write)(void *ctx, uint16_t local,
                        const cl_direct_window_t *window,
                        uint32_t offset,
                        uint32_t length,
                        uint32_t *out_length);
    /** Seek to an absolute byte offset. */
    int (*seek)(void *ctx, uint16_t local, uint64_t offset);
    /** Truncate/set file size. */
    int (*truncate)(void *ctx, uint16_t local, uint64_t size);
    /** Get file metadata by path. */
    int (*stat)(void *ctx, const char *path, cl_file_stat_t *out_stat);
    /** Get file metadata by open handle.  Optional — if NULL, callers may
     *  fall back to stat-by-path if the backend tracks paths. */
    int (*fstat)(void *ctx, uint16_t local, cl_file_stat_t *out_stat);
    /** Flush buffered writes. */
    int (*flush)(void *ctx, uint16_t local);
    /** List directory contents (non-paginated). */
    int (*list_dir)(void *ctx, const char *path, cl_file_list_cb callback,
                    void *user);
    /** List directory contents with pagination. */
    cl_file_list_page_fn list_page;
    /** Optimised server-side copy between paths on the same backend. */
    int (*copy)(void *ctx, const char *src_path, const char *dst_path,
                uint32_t flags);
    /** Remove a file. */
    int (*remove)(void *ctx, const char *path);
    /** Create a directory. */
    int (*mkdir)(void *ctx, const char *path);
    /** Rename a file or directory within the same backend. */
    int (*rename)(void *ctx, const char *old_path, const char *new_path);
} cl_file_backend_ops_t;

/** Describes a registered backend. */
struct cl_file_backend {
    const char *prefix;             /**< Path prefix (e.g. "/sd", "/dev/cart/"). */
    uint8_t prefix_length;          /**< Length of prefix (0 = auto-detect). */
    const cl_file_backend_ops_t *ops; /**< Operations vtable. */
    void *ctx;                      /**< Opaque pointer passed to all ops. */
};

/** Register a file backend.  Up to 8 backends may be registered.
 *  Backends are matched by longest prefix.
 *  @return 0 on success, -1 on invalid args, -2 if no slots available. */
int cl_file_register_backend(const cl_file_backend_t *backend);

/** Remove all registered backends and close all open handles.
 *  @warning Does not call close() on any open handle — remote resources
 *           may leak.  Use only at system reset. */
void cl_file_reset_backends(void);

/** Open a file.
 *  @param path     Absolute path (e.g. "/sd/roms/game.gba").
 *  @param flags    CLP_OPEN_READ | CLP_OPEN_WRITE | CLP_OPEN_CREATE |
 *                  CLP_OPEN_TRUNCATE | CLP_OPEN_APPEND (see proto.h).
 *  @param out_file Set to the open handle on success.
 *  @return 0 on success, negative on error. */
int cl_file_open(const char *path, uint32_t flags, cl_file_t *out_file);

/** Close a file handle.  Returns 0 on success; the handle is freed even on
 *  error (though the error is reported). */
int cl_file_close(cl_file_t file);

/** Read from a file.  Fewer bytes than requested may be returned (short read
 *  is not an error — check *out_length).
 *  @param out_length  Set to actual bytes read.
 *  @return 0 on success, negative on error. */
int cl_file_read(cl_file_t file, void *dst, uint32_t length, uint32_t *out_length);

/** Write to a file.  Fewer bytes than requested may be written.
 *  @param out_length  Set to actual bytes written.
 *  @return 0 on success, negative on error. */
int cl_file_write(cl_file_t file, const void *src, uint32_t length,
                  uint32_t *out_length);

/** Seek to an absolute byte offset.
 *  @return 0 on success, negative on error. */
int cl_file_seek(cl_file_t file, uint64_t offset);

/** Query the current file position.
 *  Tracked client-side: incremented by each read/write, set by seek.
 *  @param out_offset  Set to the current byte offset.
 *  @return 0 on success, -1 if handle or out_offset is invalid. */
int cl_file_tell(cl_file_t file, uint64_t *out_offset);

/** Set the file size (truncate or extend).  Only meaningful for writable
 *  regular files.  @return 0 on success, negative on error. */
int cl_file_truncate(cl_file_t file, uint64_t size);

/** Flush buffered writes to the underlying storage.
 *  @return 0 on success, negative on error.  May return 0 silently if the
 *          backend does not support flush — this is not an error. */
int cl_file_flush(cl_file_t file);

/** Get file metadata by path.
 *  @return 0 on success, negative on error (e.g. file not found = -3). */
int cl_file_stat(const char *path, cl_file_stat_t *out_stat);

/** Get file metadata by open handle.
 *  Dispatches to the backend's fstat op; falls back to stat-by-path for
 *  backends that do not implement it (the cl_file layer tracks the path).
 *  @return 0 on success, negative on error. */
int cl_file_fstat(cl_file_t file, cl_file_stat_t *out_stat);

/** List directory contents.  Calls callback for each entry.
 *  @return 0 on success, negative on error.  A negative callback return
 *          aborts the listing. */
int cl_file_list_dir(const char *path, cl_file_list_cb callback, void *user);

/** List directory contents with pagination.
 *  @param start_index   0-based index of first entry to return.
 *  @param max_entries   Max entries per page (0 = unlimited).
 *  @param out_next_index  Set to CLP_STORAGE_LIST_DONE on last page.
 *  @param out_entry_count Set to number of entries emitted in this page.
 *  @return 0 on success, negative on error.
 *  @note When the backend lacks native pagination, all entries are iterated
 *        and skipped up to start_index — this is O(n) per call for large
 *        directories. */
int cl_file_list_dir_page(const char *path,
                          uint32_t start_index,
                          uint32_t max_entries,
                          cl_file_list_cb callback,
                          void *user,
                          uint32_t *out_next_index,
                          uint32_t *out_entry_count);

/** Copy a file.  Uses server-side copy if both paths are on the same backend
 *  and the backend supports it. If CL_FILE_COPY_BUFFER_SIZE is non-zero at
 *  compile time, cross-backend copies use a static internal buffer; otherwise
 *  use cl_file_copy_buffered() with caller-provided memory.
 *  @param flags  CLP_COPY_VERIFY_CRC | CLP_COPY_OVERWRITE |
 *                CLP_COPY_STREAMING | CLP_COPY_WINDOWED_ACK (see proto.h).
 *  @return 0 on success, negative on error. */
int cl_file_copy(const char *src_path, const char *dst_path, uint32_t flags);

/** Copy with a caller-provided buffer (reentrant, preferred over
 *  cl_file_copy for production code).
 *  @param buffer       Scratch buffer (min 4 bytes).
 *  @param buffer_size  Size of buffer in bytes. */
int cl_file_copy_buffered(const char *src_path, const char *dst_path,
                          uint32_t flags, void *buffer, uint32_t buffer_size);

/** Copy with buffer + progress callback.
 *  @param progress      Called periodically; return negative to abort. */
int cl_file_copy_buffered_progress(const char *src_path,
                                   const char *dst_path,
                                   uint32_t flags,
                                   void *buffer,
                                   uint32_t buffer_size,
                                   cl_copy_progress_fn progress,
                                   void *progress_ctx);

/** Remove a file.  @return 0 on success, negative on error. */
int cl_file_remove(const char *path);

/** Create a directory.  @return 0 on success, negative on error. */
int cl_file_mkdir(const char *path);

/** Rename a file or directory.  Both paths must be on the same backend.
 *  @return 0 on success, negative on error. */
int cl_file_rename(const char *old_path, const char *new_path);

/** Return the preferred block size for a file, falling back to
 *  CLP_DEFAULT_BLOCK_SIZE. */
static inline uint32_t cl_file_default_block_size(const cl_file_stat_t *stat) {
    return stat && stat->preferred_block_size ? stat->preferred_block_size :
                                                CLP_DEFAULT_BLOCK_SIZE;
}

/* ==================================================================
 * POSIX compatibility layer
 *
 * Provides standard POSIX file I/O wrappers (open, read, write, close,
 * lseek, fsync, ftruncate, stat, unlink, mkdir, rename) that map to
 * the cl_file_* functions above.
 *
 * ## Return convention
 * POSIX wrappers return -1 on error and set `cl_file_posix_errno` to a
 * POSIX errno value (CL_FILE_E*).  The underlying cl_file_* functions
 * continue to use their native 0/-err convention — both may be freely
 * mixed in the same translation unit.
 *
 * ## POSIX name mapping
 * Define CHISLINK_FILE_POSIX_NAMES before including this header to use
 * un-prefixed POSIX names (open, read, write, close, lseek, fsync,
 * ftruncate, stat, unlink, mkdir, rename, errno, O_RDONLY, etc.).
 * ================================================================== */

/* --- POSIX open flags (matching standard values on 32-bit ARM) --- */
#define CL_FILE_O_RDONLY  0x00000000u
#define CL_FILE_O_WRONLY  0x00000001u
#define CL_FILE_O_RDWR    0x00000002u
#define CL_FILE_O_CREAT   0x00000200u
#define CL_FILE_O_TRUNC   0x00000400u
#define CL_FILE_O_APPEND  0x00000800u

/* --- lseek whence values --- */
#define CL_FILE_SEEK_SET  0
#define CL_FILE_SEEK_CUR  1
#define CL_FILE_SEEK_END  2

/* --- POSIX errno codes (matching Linux values) --- */
#define CL_FILE_EPERM     1
#define CL_FILE_ENOENT    2
#define CL_FILE_EIO       5
#define CL_FILE_EBADF     9
#define CL_FILE_EACCES    13
#define CL_FILE_EFAULT    14
#define CL_FILE_EEXIST    17
#define CL_FILE_ENOTDIR   20
#define CL_FILE_EISDIR    21
#define CL_FILE_EINVAL    22
#define CL_FILE_ENFILE    23
#define CL_FILE_ENOSPC    28
#define CL_FILE_EROFS     30
#define CL_FILE_ENAMETOOLONG 36
#define CL_FILE_ENOSYS    38

/* --- Mode bits for mkdir (backend may ignore) --- */
#define CL_FILE_S_IRWXU  0700u
#define CL_FILE_S_IRUSR   0400u
#define CL_FILE_S_IWUSR   0200u
#define CL_FILE_S_IXUSR   0100u

/* --- POSIX type aliases --- */

/** POSIX file descriptor alias.  Cast-compatible with cl_file_t; the
 *  invalid sentinel is < 0 (POSIX convention) vs 0 (cl_file_t). */
typedef int cl_posix_fd_t;

/** POSIX-compatible stat buffer.  Maps to cl_file_stat_t fields. */
typedef struct cl_posix_stat {
    uint64_t st_size;        /**< File size in bytes. */
    uint32_t st_blksize;     /**< Preferred I/O block size. */
    uint8_t  st_type;        /**< CLP_FILE_REGULAR, CLP_FILE_DIRECTORY, etc. */
    uint8_t  st_flags;       /**< Permission / capability flags. */
    uint8_t  _reserved[6];   /**< Reserved (padding to 24 bytes on ARM). */
} cl_posix_stat_t;

/** Test st_type against CLP_FILE_REGULAR. */
#define CL_FILE_S_ISREG(m)  ((m) == CLP_FILE_REGULAR)
/** Test st_type against CLP_FILE_DIRECTORY. */
#define CL_FILE_S_ISDIR(m)  ((m) == CLP_FILE_DIRECTORY)

/* --- Global errno --- */

/** Global errno for POSIX file wrappers.  Set whenever a wrapper returns
 *  -1.  Use CL_FILE_E* constants to interpret. */
extern int cl_file_posix_errno;

/* --- POSIX wrapper functions --- */

/** Map a cl_file_* negative return to a POSIX errno value. */
int cl_posix_errno_from_result(int result);

/** POSIX open().  flags is CL_FILE_O_*; internally mapped to CLP_OPEN_*.
 *  @return fd (>= 0) on success, -1 on error (check cl_file_posix_errno). */
cl_posix_fd_t cl_posix_open(const char *path, uint32_t flags);

/** POSIX read().  @return bytes read (>= 0), 0 = EOF, -1 on error. */
int cl_posix_read(cl_posix_fd_t fd, void *buf, uint32_t count);

/** POSIX write().  @return bytes written (>= 0), -1 on error. */
int cl_posix_write(cl_posix_fd_t fd, const void *buf, uint32_t count);

/** POSIX close().  Fd is invalidated even on error.
 *  @return 0 on success, -1 on error. */
int cl_posix_close(cl_posix_fd_t fd);

/** POSIX lseek().  Supports SEEK_SET, SEEK_CUR, SEEK_END.
 *  SEEK_END probes file size by seeking past end then telling — adds one
 *  extra protocol round-trip but is transparent to the caller.
 *  @return resulting absolute offset on success, (int64_t)-1 on error. */
int64_t cl_posix_lseek(cl_posix_fd_t fd, int64_t offset, int whence);

/** POSIX pread().  Read at a specific offset without changing the file
 *  position.  Implemented as lseek+read+lseek (restore).
 *  @return bytes read (>= 0), 0 = EOF, -1 on error. */
int cl_posix_pread(cl_posix_fd_t fd, void *buf, uint32_t count,
                   uint64_t offset);

/** POSIX pwrite().  Write at a specific offset without changing the file
 *  position.  Implemented as lseek+write+lseek (restore).
 *  @return bytes written (>= 0), -1 on error. */
int cl_posix_pwrite(cl_posix_fd_t fd, const void *buf, uint32_t count,
                    uint64_t offset);

/** POSIX fsync().  Flushes buffered writes to storage.
 *  Wraps cl_file_flush().  @return 0 on success, -1 on error. */
int cl_posix_fsync(cl_posix_fd_t fd);

/** POSIX ftruncate().  Sets file size; extends or truncates.
 *  Wraps cl_file_truncate().  @return 0 on success, -1 on error. */
int cl_posix_ftruncate(cl_posix_fd_t fd, uint64_t length);

/** POSIX stat().  Get file metadata by path.
 *  @return 0 on success, -1 on error. */
int cl_posix_stat(const char *path, cl_posix_stat_t *buf);

/** POSIX fstat().  Get file metadata by open descriptor.
 *  Wraps cl_file_fstat().  @return 0 on success, -1 on error. */
int cl_posix_fstat(cl_posix_fd_t fd, cl_posix_stat_t *buf);

/** POSIX access().  Check file accessibility via stat.
 *  @param path  Absolute path.
 *  @param mode  CL_FILE_F_OK (existence) | CL_FILE_R_OK | CL_FILE_W_OK.
 *  @return 0 if accessible, -1 on error (errno = ENOENT or EACCES). */
int cl_posix_access(const char *path, int mode);

/* --- Directory stream types --- */

/** Maximum name length for a directory entry. */
#define CL_POSIX_DIR_NAME_MAX 255

/** POSIX dirent: one directory entry. */
typedef struct cl_posix_dirent {
    char     d_name[CL_POSIX_DIR_NAME_MAX + 1]; /**< Entry name (NUL-terminated). */
    uint8_t  d_type;  /**< CLP_FILE_REGULAR or CLP_FILE_DIRECTORY. */
    uint64_t d_size;  /**< File size in bytes (0 for directories). */
} cl_posix_dirent_t;

/** POSIX DIR: directory stream handle.  Caller-allocated (stack or static);
 *  no heap, no implicit memory.  Pass to cl_posix_opendir() to initialise. */
typedef struct cl_posix_dir {
    char path[256];               /**< Directory path. */
    uint32_t next_index;          /**< Next page start index. */
    uint8_t entries[512];         /**< Raw list_page response buffer. */
    const uint8_t *cursor;        /**< Read cursor within entries[]. */
    const uint8_t *end;           /**< End of valid data in entries[]. */
    uint32_t entry_count;         /**< Entries remaining in current page. */
    cl_posix_dirent_t current;    /**< Current entry (filled by readdir). */
    uint8_t open;                 /**< 1 while opendir succeeded. */
} cl_posix_dir_t;

/** POSIX opendir().  Takes a caller-allocated cl_posix_dir_t buffer.
 *  Uses cl_file_list_dir_page internally.
 *  @param dir   Caller-allocated directory stream buffer.
 *  @param path  Directory path to open.
 *  @return dir on success, NULL on error (check errno). */
cl_posix_dir_t *cl_posix_opendir(cl_posix_dir_t *dir, const char *path);

/** Convenience wrapper using an internal static buffer.  Preferred for
 *  most users — call via the opendir() POSIX alias.  The internal buffer
 *  (~800B BSS) is only linked in if this function is actually called. */
cl_posix_dir_t *cl_posix_opendir_auto(const char *path);

/** POSIX readdir().  Read the next directory entry.
 *  @return pointer to internal dirent on success, NULL at end-of-stream
 *          or on error (check errno for error vs EOF). */
cl_posix_dirent_t *cl_posix_readdir(cl_posix_dir_t *dir);

/** POSIX closedir().  Release a directory stream.
 *  @return 0 on success, -1 on error. */
int cl_posix_closedir(cl_posix_dir_t *dir);

/* --- access() mode flags --- */
#define CL_FILE_F_OK  0  /**< Test for existence. */
#define CL_FILE_R_OK  4  /**< Test for read permission. */
#define CL_FILE_W_OK  2  /**< Test for write permission. */

/* --- POSIX unlink().  Remove a file.
 *  @return 0 on success, -1 on error. */
int cl_posix_unlink(const char *path);

/** POSIX mkdir().  mode is accepted for compatibility; backend may ignore.
 *  @return 0 on success, -1 on error. */
int cl_posix_mkdir(const char *path, uint32_t mode);

/** POSIX rename().  Both paths must reside on the same backend.
 *  @return 0 on success, -1 on error. */
int cl_posix_rename(const char *old_path, const char *new_path);

/* --- Optional POSIX name aliases --- */
#ifdef CHISLINK_FILE_POSIX_NAMES

#define O_RDONLY   CL_FILE_O_RDONLY
#define O_WRONLY   CL_FILE_O_WRONLY
#define O_RDWR     CL_FILE_O_RDWR
#define O_CREAT    CL_FILE_O_CREAT
#define O_TRUNC    CL_FILE_O_TRUNC
#define O_APPEND   CL_FILE_O_APPEND

#define SEEK_SET   CL_FILE_SEEK_SET
#define SEEK_CUR   CL_FILE_SEEK_CUR
#define SEEK_END   CL_FILE_SEEK_END

#undef errno
#define errno cl_file_posix_errno

#define open(path, flags)              cl_posix_open((path), (flags))
#define read(fd, buf, count)           cl_posix_read((fd), (buf), (count))
#define write(fd, buf, count)          cl_posix_write((fd), (buf), (count))
#define close(fd)                      cl_posix_close((fd))
#define lseek(fd, offset, whence)      cl_posix_lseek((fd), (offset), (whence))
#define pread(fd, buf, count, off)     cl_posix_pread((fd), (buf), (count), (off))
#define pwrite(fd, buf, count, off)    cl_posix_pwrite((fd), (buf), (count), (off))
#define fsync(fd)                      cl_posix_fsync((fd))
#define ftruncate(fd, length)          cl_posix_ftruncate((fd), (length))
#define stat(path, buf)                cl_posix_stat((path), (cl_posix_stat_t *)(buf))
#define fstat(fd, buf)                 cl_posix_fstat((fd), (cl_posix_stat_t *)(buf))
#define access(path, mode)             cl_posix_access((path), (mode))
#define unlink(path)                   cl_posix_unlink((path))
#define mkdir(path, mode)              cl_posix_mkdir((path), (mode))
#define rename(old_path, new_path)     cl_posix_rename((old_path), (new_path))

#define S_ISREG(m)  CL_FILE_S_ISREG(m)
#define S_ISDIR(m)  CL_FILE_S_ISDIR(m)

#define F_OK  CL_FILE_F_OK
#define R_OK  CL_FILE_R_OK
#define W_OK  CL_FILE_W_OK

/* Directory types */
#define DIR              cl_posix_dir_t
#define dirent           cl_posix_dirent_t
#define opendir(path)    cl_posix_opendir_auto((path))
#define readdir(d)       cl_posix_readdir((d))
#define closedir(d)      cl_posix_closedir((d))

#endif /* CHISLINK_FILE_POSIX_NAMES */

#ifdef __cplusplus
}
#endif

#endif
