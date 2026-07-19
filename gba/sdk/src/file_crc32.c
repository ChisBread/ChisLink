#include "chislink/file_crc32.h"

#include "chislink/crc32.h"
#include "chislink/crc32_chunks.h"
#include "chislink/file.h"

#include <stddef.h>
#include <string.h>

static void clear_result(cl_file_crc32_result_t *result) {
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    result->mismatch_offset = UINT64_MAX;
}

static int read_chunk_crc(cl_file_t file,
                          uint64_t offset,
                          uint32_t length,
                          void *buffer,
                          uint32_t buffer_size,
                          cl_file_crc32_transform_fn transform,
                          void *transform_ctx,
                          uint32_t *out_crc) {
    cl_crc32_t crc;
    cl_crc32_init(&crc);
    uint32_t done = 0;
    while (done < length) {
        uint32_t want = length - done < buffer_size ?
            length - done : buffer_size;
        uint32_t got = 0;
        int ret = cl_file_read(file, buffer, want, &got);
        if (ret < 0) {
            return ret;
        }
        if (got == 0 || got > want) {
            return -4;
        }
        if (transform) {
            ret = transform(transform_ctx, offset + done, buffer, got);
            if (ret < 0) {
                return ret;
            }
        }
        cl_crc32_update(&crc, buffer, got);
        done += got;
    }
    *out_crc = cl_crc32_finalize(&crc);
    return 0;
}

int cl_file_calculate_crc32_chunks(const char *path,
                                   uint64_t length,
                                   cl_crc32_chunks_t *chunks,
                                   void *buffer,
                                   uint32_t buffer_size,
                                   cl_copy_progress_fn progress,
                                   void *progress_ctx) {
    if (!path || !chunks || !chunks->chunk_size || !chunks->values ||
        !chunks->capacity || chunks->count || chunks->chunk_bytes ||
        chunks->total_bytes || !buffer || !buffer_size || !length) {
        return -1;
    }
    uint64_t required = length / chunks->chunk_size;
    if (length % chunks->chunk_size) {
        required++;
    }
    if (required > chunks->capacity) {
        return -2;
    }

    cl_file_stat_t stat;
    int ret = cl_file_stat(path, &stat);
    if (ret < 0) {
        return ret;
    }
    if (stat.size < length) {
        return CL_FILE_CRC32_SIZE_MISMATCH;
    }

    cl_file_t file = 0;
    ret = cl_file_open(path, CLP_OPEN_READ, &file);
    if (ret < 0) {
        return ret;
    }
    uint64_t offset = 0;
    while (offset < length) {
        uint64_t remaining = length - offset;
        uint32_t chunk = remaining < chunks->chunk_size ?
            (uint32_t)remaining : chunks->chunk_size;
        ret = read_chunk_crc(file, offset, chunk, buffer, buffer_size,
                             0, 0, &chunks->values[chunks->count]);
        if (ret < 0) {
            break;
        }
        chunks->count++;
        chunks->total_bytes += chunk;
        offset += chunk;
        if (progress) {
            ret = progress(progress_ctx, offset, length);
            if (ret < 0) {
                break;
            }
        }
    }
    int close_ret = cl_file_close(file);
    if (ret < 0) {
        return ret;
    }
    return close_ret;
}

static int verify_range(const char *source_path,
                        const char *target_path,
                        uint64_t length,
                        const cl_file_crc32_options_t *options,
                        cl_file_crc32_result_t *result) {
    clear_result(result);
    if (!source_path || !target_path || !options || !options->chunk_size ||
        !options->buffer || !options->buffer_size || !result) {
        return -1;
    }

    cl_file_stat_t source_stat;
    cl_file_stat_t target_stat;
    int ret = cl_file_stat(source_path, &source_stat);
    if (ret < 0) {
        return ret;
    }
    ret = cl_file_stat(target_path, &target_stat);
    if (ret < 0) {
        return ret;
    }
    if (source_stat.size < length || target_stat.size < length) {
        result->mismatch_offset = source_stat.size < target_stat.size ?
            source_stat.size : target_stat.size;
        return CL_FILE_CRC32_SIZE_MISMATCH;
    }

    cl_file_t source = 0;
    cl_file_t target = 0;
    ret = cl_file_open(source_path, CLP_OPEN_READ, &source);
    if (ret < 0) {
        return ret;
    }
    ret = cl_file_open(target_path, CLP_OPEN_READ, &target);
    if (ret < 0) {
        (void)cl_file_close(source);
        return ret;
    }

    uint64_t offset = 0;
    while (offset < length) {
        uint64_t remaining = length - offset;
        uint32_t chunk = remaining < options->chunk_size ?
            (uint32_t)remaining : options->chunk_size;
        ret = read_chunk_crc(source, offset, chunk,
                             options->buffer, options->buffer_size,
                             options->source_transform,
                             options->source_transform_ctx,
                             &result->source_crc32);
        if (ret < 0) {
            break;
        }
        ret = read_chunk_crc(target, offset, chunk,
                             options->buffer, options->buffer_size,
                             0, 0, &result->target_crc32);
        if (ret < 0) {
            break;
        }
        if (result->source_crc32 != result->target_crc32) {
            result->mismatch_offset = offset;
            ret = CL_FILE_CRC32_MISMATCH;
            break;
        }
        offset += chunk;
        result->verified_bytes = offset;
        result->chunks_verified++;
        if (options->progress) {
            ret = options->progress(options->progress_ctx, offset, length);
            if (ret < 0) {
                break;
            }
        }
    }

    int close_target = cl_file_close(target);
    int close_source = cl_file_close(source);
    if (ret < 0) {
        return ret;
    }
    if (close_target < 0) {
        return close_target;
    }
    return close_source;
}

int cl_file_verify_crc32_range(const char *source_path,
                               const char *target_path,
                               uint64_t length,
                               const cl_file_crc32_options_t *options,
                               cl_file_crc32_result_t *result) {
    return verify_range(source_path, target_path, length, options, result);
}

int cl_file_verify_crc32(const char *source_path,
                         const char *target_path,
                         const cl_file_crc32_options_t *options,
                         cl_file_crc32_result_t *result) {
    if (!source_path || !target_path || !result) {
        return -1;
    }
    cl_file_stat_t source_stat;
    cl_file_stat_t target_stat;
    int ret = cl_file_stat(source_path, &source_stat);
    if (ret < 0) {
        return ret;
    }
    ret = cl_file_stat(target_path, &target_stat);
    if (ret < 0) {
        return ret;
    }
    if (source_stat.size != target_stat.size) {
        clear_result(result);
        result->mismatch_offset = source_stat.size < target_stat.size ?
            source_stat.size : target_stat.size;
        return CL_FILE_CRC32_SIZE_MISMATCH;
    }
    return verify_range(source_path, target_path, source_stat.size,
                        options, result);
}

int cl_file_verify_crc32_chunks(const char *target_path,
                                const cl_crc32_chunks_t *chunks,
                                void *buffer,
                                uint32_t buffer_size,
                                cl_copy_progress_fn progress,
                                void *progress_ctx,
                                cl_file_crc32_result_t *result) {
    clear_result(result);
    if (!target_path || !chunks || !chunks->chunk_size || !chunks->values ||
        !buffer || !buffer_size || !result) {
        return -1;
    }
    uint64_t expected_chunks = chunks->total_bytes / chunks->chunk_size;
    if (chunks->total_bytes % chunks->chunk_size) {
        expected_chunks++;
    }
    if (expected_chunks != chunks->count) {
        return -2;
    }

    cl_file_stat_t target_stat;
    int ret = cl_file_stat(target_path, &target_stat);
    if (ret < 0) {
        return ret;
    }
    if (target_stat.size < chunks->total_bytes) {
        result->mismatch_offset = target_stat.size;
        return CL_FILE_CRC32_SIZE_MISMATCH;
    }

    cl_file_t target = 0;
    ret = cl_file_open(target_path, CLP_OPEN_READ, &target);
    if (ret < 0) {
        return ret;
    }
    uint64_t offset = 0;
    for (uint32_t i = 0; i < chunks->count; ++i) {
        uint64_t remaining = chunks->total_bytes - offset;
        uint32_t chunk = remaining < chunks->chunk_size ?
            (uint32_t)remaining : chunks->chunk_size;
        result->source_crc32 = chunks->values[i];
        ret = read_chunk_crc(target, offset, chunk, buffer, buffer_size,
                             0, 0, &result->target_crc32);
        if (ret < 0) {
            break;
        }
        if (result->source_crc32 != result->target_crc32) {
            ret = cl_file_seek(target, offset);
            if (ret < 0) {
                break;
            }
            ret = read_chunk_crc(target, offset, chunk, buffer, buffer_size,
                                 0, 0, &result->target_crc32);
            if (ret < 0) {
                break;
            }
            if (result->source_crc32 != result->target_crc32) {
                result->mismatch_offset = offset;
                ret = CL_FILE_CRC32_MISMATCH;
                break;
            }
        }
        offset += chunk;
        result->verified_bytes = offset;
        result->chunks_verified++;
        if (progress) {
            ret = progress(progress_ctx, offset, chunks->total_bytes);
            if (ret < 0) {
                break;
            }
        }
    }
    int close_ret = cl_file_close(target);
    if (ret < 0) {
        return ret;
    }
    return close_ret;
}
