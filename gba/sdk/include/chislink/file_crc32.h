#ifndef CHISLINK_FILE_CRC32_H
#define CHISLINK_FILE_CRC32_H

#include <stdint.h>

#include "chislink/copy.h"
#include "chislink/crc32_chunks.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CL_FILE_CRC32_MISMATCH      (-100)
#define CL_FILE_CRC32_SIZE_MISMATCH (-101)

typedef int (*cl_file_crc32_transform_fn)(void *ctx,
                                          uint64_t offset,
                                          void *data,
                                          uint32_t length);

typedef struct cl_file_crc32_options {
    uint32_t chunk_size;
    void *buffer;
    uint32_t buffer_size;
    cl_file_crc32_transform_fn source_transform;
    void *source_transform_ctx;
    cl_copy_progress_fn progress;
    void *progress_ctx;
} cl_file_crc32_options_t;

typedef struct cl_file_crc32_result {
    uint64_t verified_bytes;
    uint64_t mismatch_offset;
    uint32_t chunks_verified;
    uint32_t source_crc32;
    uint32_t target_crc32;
} cl_file_crc32_result_t;

/** Calculate a segmented CRC manifest for exactly length bytes.
 *  The file must contain at least length bytes. The caller initializes chunks
 *  and owns both its value array and the I/O buffer. */
int cl_file_calculate_crc32_chunks(const char *path,
                                   uint64_t length,
                                   cl_crc32_chunks_t *chunks,
                                   void *buffer,
                                   uint32_t buffer_size,
                                   cl_copy_progress_fn progress,
                                   void *progress_ctx);

/** Verify two complete files and require equal sizes. */
int cl_file_verify_crc32(const char *source_path,
                         const char *target_path,
                         const cl_file_crc32_options_t *options,
                         cl_file_crc32_result_t *result);

/** Verify exactly length bytes from the start of each file.
 *  Both files must contain at least length bytes. Bytes beyond length are
 *  intentionally ignored, which is useful for fixed-capacity devices. */
int cl_file_verify_crc32_range(const char *source_path,
                               const char *target_path,
                               uint64_t length,
                               const cl_file_crc32_options_t *options,
                               cl_file_crc32_result_t *result);

/** Verify a file against CRC values captured while producing it. */
int cl_file_verify_crc32_chunks(const char *target_path,
                                const cl_crc32_chunks_t *chunks,
                                void *buffer,
                                uint32_t buffer_size,
                                cl_copy_progress_fn progress,
                                void *progress_ctx,
                                cl_file_crc32_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
