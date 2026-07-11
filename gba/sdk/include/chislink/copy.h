#ifndef CHISLINK_COPY_H
#define CHISLINK_COPY_H

#include <stdbool.h>
#include <stdint.h>

#include "chislink/proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file copy.h
 * @brief Streaming copy engine — unified data transfer between any source
 *        and sink backed by the callback-based cl_copy_source_t /
 *        cl_copy_sink_t interfaces.
 *
 * The copy engine supports three paths (tried in order):
 * 1. **Zero-copy** — direct_read → direct_write, bypassing the buffer.
 * 2. **Buffered** — read into buffer, write from buffer.
 * 3. **Fallback** — single-byte reads/writes.
 *
 * ## Memory ownership
 * - cl_copy_source_t, cl_copy_sink_t, and cl_copy_options_t are caller-owned.
 * - The buffer in cl_copy_options_t must remain valid for the duration of
 *   the copy call.
 * - cl_direct_window_t.data is owned by the source backend until
 *   release_direct() is called.
 */

/** Default block size for copy operations (matches protocol default). */
#define CL_COPY_DEFAULT_BLOCK_SIZE CLP_DEFAULT_BLOCK_SIZE

/** Describes the memory layout of a direct-window buffer. */
typedef enum cl_direct_window_access {
    CL_DIRECT_WINDOW_BYTES = 0,      /**< Byte-addressable (RAM, SRAM). */
    CL_DIRECT_WINDOW_ROM16_LE = 1,   /**< 16-bit little-endian (GBA ROM). */
} cl_direct_window_access_t;

/** A direct-memory window returned by a source's direct_read().
 *  The data pointer belongs to the source until release_direct() is called.
 *  @note The window may be narrower than max_length — the caller must
 *        honour the returned length. */
typedef struct cl_direct_window {
    const volatile uint8_t *data;    /**< Start of readable data. */
    uint32_t length;                 /**< Bytes available in this window. */
    uint8_t access;                  /**< cl_direct_window_access_t value. */
} cl_direct_window_t;

/** Read a single byte from a direct window.
 *  Handles ROM16_LE word-aligned access transparently.
 *  @warning Returns 0 for out-of-range offsets — indistinguishable from a
 *           valid 0x00 byte when the window is invalid.  Always validate
 *           the window pointer and offset before calling. */
static inline uint8_t cl_direct_window_read_byte(const cl_direct_window_t *window,
                                                 uint32_t offset) {
    if (!window || !window->data || offset >= window->length) {
        return 0;
    }
    if (window->access == CL_DIRECT_WINDOW_ROM16_LE) {
        uintptr_t addr = (uintptr_t)(window->data + offset);
        const volatile uint16_t *half =
            (const volatile uint16_t *)(addr & ~(uintptr_t)1u);
        uint16_t value = *half;
        return (uint8_t)((addr & 1u) ? (value >> 8u) : value);
    }
    return window->data[offset];
}

/* --- Callback signatures --- */

/** Read up to *length* bytes.  Sets *out_length* to actual bytes read.
 *  @return 0 on success, negative on error. */
typedef int (*cl_copy_read_fn)(void *ctx, void *dst, uint32_t length,
                               uint32_t *out_length);

/** Acquire a direct-memory window for zero-copy reading.
 *  @return 0 and fills out_window on success; 0 with out_window->length==0
 *          means "direct reads not available for this range". */
typedef int (*cl_copy_direct_read_fn)(void *ctx,
                                      uint32_t max_length,
                                      cl_direct_window_t *out_window);

/** Release a previously-acquired direct window. */
typedef int (*cl_copy_direct_release_fn)(void *ctx);

/** Write data.  Sets *out_length* to bytes actually written.
 *  @return 0 on success, negative on error. */
typedef int (*cl_copy_write_fn)(void *ctx, const void *src, uint32_t length,
                                uint32_t *out_length);

/** Write data from a direct window (zero-copy write path).
 *  @param window  Source window (may be a source's direct window or a
 *                 caller-constructed byte window).
 *  @param offset  Byte offset within window to start from.
 *  @param length  Bytes to write from window.
 *  @param out_length  Set to actual bytes written. */
typedef int (*cl_copy_direct_write_fn)(void *ctx,
                                       const cl_direct_window_t *window,
                                       uint32_t offset,
                                       uint32_t length,
                                       uint32_t *out_length);

/** Flush any buffered writes to the underlying storage. */
typedef int (*cl_copy_flush_fn)(void *ctx);

/** Progress callback.  Called periodically during copy.
 *  @param done   Bytes copied so far.
 *  @param total  Total bytes (0 if unknown).
 *  @return 0 to continue, negative to abort the copy. */
typedef int (*cl_copy_progress_fn)(void *ctx, uint64_t done, uint64_t total);

/** Data source descriptor.  At minimum, read() OR direct_read() is required. */
typedef struct cl_copy_source {
    cl_copy_read_fn read;                           /**< Buffered read (required if no direct_read). */
    cl_copy_direct_read_fn direct_read;             /**< Optional zero-copy read. */
    cl_copy_direct_release_fn release_direct;       /**< Required if direct_read is set. */
    void *ctx;                                      /**< Passed to all callbacks. */
    uint64_t size;                                  /**< Known size, or 0 if unknown. */
    uint32_t preferred_block_size;                  /**< Hint for optimal block size. */
    uint32_t max_block_size;                        /**< Maximum safe block size (0 = no extra cap). */
} cl_copy_source_t;

/** Data sink descriptor.  write() is required. */
typedef struct cl_copy_sink {
    cl_copy_write_fn write;                         /**< Buffered write (required). */
    cl_copy_direct_write_fn direct_write;           /**< Optional zero-copy write. */
    cl_copy_flush_fn flush;                         /**< Optional flush after copy. */
    void *ctx;                                      /**< Passed to all callbacks. */
    uint64_t size;                                  /**< Expected final size, or 0. */
    uint32_t preferred_block_size;                  /**< Hint for optimal block size. */
    uint32_t max_block_size;                        /**< Maximum safe block size (0 = no extra cap). */
} cl_copy_sink_t;

/** Options controlling copy behaviour. */
typedef struct cl_copy_options {
    void *buffer;                /**< Scratch buffer for buffered copy (required). */
    uint32_t buffer_size;        /**< Size of buffer in bytes (min 4). */
    uint32_t block_size;         /**< Preferred transfer block (0 = use source hint). */
    cl_copy_progress_fn progress;/**< Optional progress callback. */
    void *progress_ctx;          /**< Passed to progress callback. */
    bool flush_on_finish;        /**< If true, call sink->flush() on success. */
} cl_copy_options_t;

/** Run a streaming copy from source to sink.
 *
 *  Tries zero-copy (direct_read → direct_write) when both sides support it,
 *  falls back to buffered copy through options.buffer, then to byte-at-a-time.
 *
 *  @param source   Data source (must have read OR direct_read).
 *  @param sink     Data sink (must have write).
 *  @param options  Buffer + tuning parameters.
 *  @return 0 on success, negative on error.  A negative return from the
 *          progress callback aborts the copy. */
int cl_copy_stream(const cl_copy_source_t *source, const cl_copy_sink_t *sink,
                   const cl_copy_options_t *options);

#ifdef __cplusplus
}
#endif

#endif
