#include "chislink/crc32_chunks.h"

int cl_crc32_chunks_init(cl_crc32_chunks_t *chunks,
                         uint32_t chunk_size,
                         uint32_t *values,
                         uint32_t capacity) {
    if (!chunks || !chunk_size || !values || !capacity) {
        return -1;
    }
    chunks->values = values;
    chunks->capacity = capacity;
    chunks->count = 0;
    chunks->chunk_size = chunk_size;
    chunks->chunk_bytes = 0;
    chunks->total_bytes = 0;
    cl_crc32_init(&chunks->current);
    return 0;
}

static int chunks_finish_current(cl_crc32_chunks_t *chunks) {
    if (chunks->count >= chunks->capacity) {
        return -2;
    }
    chunks->values[chunks->count++] = cl_crc32_finalize(&chunks->current);
    chunks->chunk_bytes = 0;
    cl_crc32_init(&chunks->current);
    return 0;
}

int cl_crc32_chunks_update(cl_crc32_chunks_t *chunks,
                           const void *data,
                           uint32_t length) {
    if (!chunks || (!data && length) || !chunks->chunk_size ||
        !chunks->values) {
        return -1;
    }
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t consumed = 0;
    while (consumed < length) {
        uint32_t room = chunks->chunk_size - chunks->chunk_bytes;
        uint32_t n = length - consumed < room ? length - consumed : room;
        cl_crc32_update(&chunks->current, bytes + consumed, n);
        chunks->chunk_bytes += n;
        chunks->total_bytes += n;
        consumed += n;
        if (chunks->chunk_bytes == chunks->chunk_size) {
            int ret = chunks_finish_current(chunks);
            if (ret < 0) {
                return ret;
            }
        }
    }
    return 0;
}

int cl_crc32_chunks_finish(cl_crc32_chunks_t *chunks) {
    if (!chunks || !chunks->chunk_size || !chunks->values) {
        return -1;
    }
    return chunks->chunk_bytes ? chunks_finish_current(chunks) : 0;
}
