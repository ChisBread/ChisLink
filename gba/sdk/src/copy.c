#include "chislink/copy.h"

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static uint32_t choose_block_size(const cl_copy_source_t *source,
                                  const cl_copy_sink_t *sink,
                                  const cl_copy_options_t *options) {
    uint32_t block = CL_COPY_DEFAULT_BLOCK_SIZE;

    if (source && source->preferred_block_size) {
        block = min_u32(block, source->preferred_block_size);
    }
    if (source && source->max_block_size) {
        block = min_u32(block, source->max_block_size);
    }
    if (sink && sink->preferred_block_size) {
        block = min_u32(block, sink->preferred_block_size);
    }
    if (sink && sink->max_block_size) {
        block = min_u32(block, sink->max_block_size);
    }
    if (options && options->block_size) {
        block = min_u32(block, options->block_size);
    }
    if (options && options->buffer_size) {
        block = min_u32(block, options->buffer_size);
    }

    return block & ~3u;
}

int cl_copy_stream(const cl_copy_source_t *source, const cl_copy_sink_t *sink,
                   const cl_copy_options_t *options) {
    if (!source || (!source->read && !source->direct_read) || !sink ||
        !sink->write ||
        !options || !options->buffer || options->buffer_size < 4u) {
        return -1;
    }

    uint32_t block_size = choose_block_size(source, sink, options);
    if (block_size < 4u) {
        return -2;
    }

    uint64_t done = 0;
    while (source->size == 0 || done < source->size) {
        uint32_t want = block_size;
        if (source->size != 0) {
            uint64_t remaining = source->size - done;
            if (remaining < want) {
                want = (uint32_t)remaining;
            }
        }

        cl_direct_window_t direct = {0};
        uint32_t got = 0;
        int ret = 0;
        if (source->direct_read) {
            ret = source->direct_read(source->ctx, want, &direct);
            if (ret < 0) {
                return ret;
            }
            got = direct.length;
            if (got != 0 && !direct.data) {
                return -4;
            }
            if (got > want) {
                if (direct.data && source->release_direct) {
                    (void)source->release_direct(source->ctx);
                }
                return -5;
            }
        }
        if (got == 0) {
            if (!source->read) {
                break;
            }
            ret = source->read(source->ctx, options->buffer, want, &got);
            if (ret < 0) {
                return ret;
            }
            direct.data = 0;
            direct.length = 0;
            direct.access = CL_DIRECT_WINDOW_BYTES;
        }
        if (got == 0) {
            break;
        }

        uint32_t written_total = 0;
        while (written_total < got) {
            uint32_t written = 0;
            if (direct.data && sink->direct_write) {
                ret = sink->direct_write(sink->ctx,
                                         &direct,
                                         written_total,
                                         got - written_total, &written);
            } else {
                if (direct.data) {
                    uint32_t to_copy = got - written_total;
                    if (to_copy > block_size) {
                        to_copy = block_size;
                    }
                    for (uint32_t i = 0; i < to_copy; ++i) {
                        ((uint8_t *)options->buffer)[i] =
                            cl_direct_window_read_byte(&direct,
                                                       written_total + i);
                    }
                    ret = sink->write(sink->ctx, options->buffer, to_copy,
                                      &written);
                    if (ret >= 0 && written != to_copy) {
                        ret = -3;
                    }
                } else {
                    ret = sink->write(sink->ctx,
                                      (const uint8_t *)options->buffer +
                                      written_total,
                                      got - written_total, &written);
                }
            }
            if (ret < 0) {
                if (direct.data && source->release_direct) {
                    (void)source->release_direct(source->ctx);
                }
                return ret;
            }
            if (written == 0) {
                if (direct.data && source->release_direct) {
                    (void)source->release_direct(source->ctx);
                }
                return -3;
            }
            written_total += written;
        }
        if (direct.data && source->release_direct) {
            ret = source->release_direct(source->ctx);
            if (ret < 0) {
                return ret;
            }
        }

        done += got;
        if (options->progress) {
            ret = options->progress(options->progress_ctx, done, source->size);
            if (ret < 0) {
                return ret;
            }
        }
    }

    if (options->flush_on_finish && sink->flush) {
        return sink->flush(sink->ctx);
    }

    return 0;
}
