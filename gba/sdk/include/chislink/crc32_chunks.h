#ifndef CHISLINK_CRC32_CHUNKS_H
#define CHISLINK_CRC32_CHUNKS_H

#include <stdint.h>

#include "chislink/copy.h"
#include "chislink/crc32.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Caller-owned segmented CRC manifest. Each completed CRC covers at most
 *  chunk_size bytes; the final value covers the exact short tail. */
typedef struct cl_crc32_chunks {
    uint32_t *values;
    uint32_t capacity;
    uint32_t count;
    uint32_t chunk_size;
    uint32_t chunk_bytes;
    uint64_t total_bytes;
    cl_crc32_t current;
} cl_crc32_chunks_t;

int cl_crc32_chunks_init(cl_crc32_chunks_t *chunks,
                         uint32_t chunk_size,
                         uint32_t *values,
                         uint32_t capacity);
int cl_crc32_chunks_update(cl_crc32_chunks_t *chunks,
                           const void *data,
                           uint32_t length);
int cl_crc32_chunks_finish(cl_crc32_chunks_t *chunks);

#ifdef __cplusplus
}
#endif

#endif
