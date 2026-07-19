#ifndef CHISLINK_STORAGE_CLIENT_H
#define CHISLINK_STORAGE_CLIENT_H

#include <stdint.h>

#include "chislink/client.h"
#include "chislink/file.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file storage_client.h
 * @brief Low-level remote-storage protocol client and the convenience
 *        registration function that wires it into the unified file layer.
 *
 * Most applications use the cl_file_* API; the cl_storage_* functions are
 * exposed for code that needs direct control over storage handles (e.g. to
 * use write_direct or avoid the file handle pool).
 *
 * ## Handle type
 * Storage handles (uint16_t) are opaque values assigned by the MCU.
 * Handles are distinct from cl_file_t — do not mix them.
 *
 * ## Return values
 * All functions return 0 on success, negative on error.
 * -1: invalid argument, -2: protocol/wire error, -3: MCU error response,
 * -4: capability missing.
 *
 * ## Memory ownership
 * - cl_storage_client_t stores a pointer to the shared cl_client_t.
 * - All data buffers are caller-owned.
 * - cl_storage_write_direct reads from a cl_direct_window_t owned by the
 *   caller (the window must stay valid for the duration of the call).
 */

/** Thin wrapper holding a pointer to the shared transport client. */
typedef struct cl_storage_client {
    cl_client_t *client;      /**< Shared protocol client (not owned). */
    uint8_t *scratch;         /**< Caller-owned scratch buffer (min 520 bytes). */
    size_t scratch_size;      /**< Size of scratch buffer. */
} cl_storage_client_t;

/** Initialise the storage client.
 *  @param scratch       Caller-owned buffer for protocol payloads (min 520B).
 *  @param scratch_size  Size of scratch buffer.
 *  @return 0 on success, -1 on NULL argument. */
int cl_storage_client_init(cl_storage_client_t *storage, cl_client_t *client,
                           void *scratch, size_t scratch_size);

/** Open a remote file.
 *  @param flags  CLP_OPEN_READ | CLP_OPEN_WRITE | CLP_OPEN_CREATE | ...
 *  @param out_handle  Set to the MCU-assigned handle on success.
 *  @return 0 on success, negative on error. */
int cl_storage_open(cl_storage_client_t *storage, const char *path,
                    uint32_t flags, uint16_t *out_handle);

/** Close a remote handle. */
int cl_storage_close(cl_storage_client_t *storage, uint16_t handle);

/** Read from a remote handle.  Short reads possible; check *out_length.
 *  @param out_length  Set to bytes actually read. */
int cl_storage_read(cl_storage_client_t *storage, uint16_t handle, void *dst,
                    uint32_t length, uint32_t *out_length);

/** Write to a remote handle.
 *  The SDK automatically splits requests larger than the storage write frame
 *  ceiling; no extra caller buffer is allocated.  Higher-level copy code also
 *  caps the request by the caller-provided workspace and file backend limits. */
int cl_storage_write(cl_storage_client_t *storage, uint16_t handle,
                     const void *src, uint32_t length, uint32_t *out_length);

/** Write from a direct window.
 *  The SDK automatically splits requests larger than the storage write frame
 *  ceiling.  When the transport supports direct-window payloads, ROM/SRAM
 *  mapped data can stream without copying into workspace.
 *  @param window  Source window (byte or ROM16_LE access).
 *  @param offset  Byte offset within window.
 *  @param length  Bytes to write from window.
 *  @param out_length  Set to bytes actually written. */
int cl_storage_write_direct(cl_storage_client_t *storage,
                            uint16_t handle,
                            const cl_direct_window_t *window,
                            uint32_t offset,
                            uint32_t length,
                            uint32_t *out_length);

/** Seek to an absolute byte offset on the remote handle. */
int cl_storage_seek(cl_storage_client_t *storage, uint16_t handle,
                    uint64_t offset);

/** Flush buffered writes on the remote handle. */
int cl_storage_flush(cl_storage_client_t *storage, uint16_t handle);

/** Get file metadata from the MCU.
 *  Zero-initialises *out_stat before filling. */
int cl_storage_stat(cl_storage_client_t *storage, const char *path,
                    cl_file_stat_t *out_stat);

/** Server-side copy between two paths on the same MCU filesystem.
 *  @param flags  CLP_COPY_VERIFY_CRC | CLP_COPY_OVERWRITE | ...
 *  @return 0 on success, -4 if the MCU lacks CLP_CAP_STREAM_COPY. */
int cl_storage_copy(cl_storage_client_t *storage,
                    const char *src_path,
                    const char *dst_path,
                    uint32_t flags);

/** Remove a file on the MCU. */
int cl_storage_remove(cl_storage_client_t *storage, const char *path);

/** Create a directory on the MCU. */
int cl_storage_mkdir(cl_storage_client_t *storage, const char *path);

/** Rename a file or directory on the MCU. */
int cl_storage_rename(cl_storage_client_t *storage,
                      const char *old_path,
                      const char *new_path);

/** Ask the MCU to calculate segmented CRC32 values for a remote file.
 *  The final CRC covers the exact short tail. */
int cl_storage_crc32(cl_storage_client_t *storage,
                     const char *path,
                     uint64_t offset,
                     uint64_t length,
                     uint32_t chunk_size,
                     uint32_t *out_crc32,
                     uint32_t max_chunks,
                     uint64_t *out_length,
                     uint32_t *out_chunks);

/** Extended remote CRC request. flags may request source-side transforms. */
int cl_storage_crc32_ex(cl_storage_client_t *storage,
                        const char *path,
                        uint64_t offset,
                        uint64_t length,
                        uint32_t chunk_size,
                        uint32_t flags,
                        uint32_t *out_crc32,
                        uint32_t max_chunks,
                        uint64_t *out_length,
                        uint32_t *out_chunks);

/** Paginated directory listing — raw protocol access.
 *
 *  @param dst              Caller-owned buffer for response data.
 *  @param capacity         Size of dst in bytes (min CLP_STORAGE_LIST_RESPONSE_HEADER_BYTES).
 *  @param out_length       Set to total bytes written to dst.
 *  @param out_next_index   Set to CLP_STORAGE_LIST_DONE on last page.
 *  @param out_entry_count  Number of entries in this response.
 *  @return 0 on success, -4 if MCU lacks CLP_CAP_DIR_LIST. */
int cl_storage_list(cl_storage_client_t *storage,
                    const char *path,
                    uint32_t start_index,
                    uint32_t max_entries,
                    void *dst,
                    uint32_t capacity,
                    uint32_t *out_length,
                    uint32_t *out_next_index,
                    uint32_t *out_entry_count);

/** Convenience: register the remote storage backends on the unified file layer.
 *
 *  Registers three backends:
 *  - /sd       → SD card
 *  - /littlefs → internal flash LittleFS
 *  - /dev      → device pseudo-files
 *
 *  All three share the same cl_storage_client_t and ops table.
 *  @return 0 on success, negative on error. */
int cl_file_register_remote_storage(cl_storage_client_t *storage);

#ifdef __cplusplus
}
#endif

#endif
