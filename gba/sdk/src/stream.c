#include "chislink/stream.h"

#include <string.h>

static bool stream_valid_config(const cl_stream_config_t *config) {
    if (!config || !config->buffer || !config->slots ||
        config->buffer_size == 0 || config->slot_count == 0 ||
        config->slot_size == 0) {
        return false;
    }
    if (((uintptr_t)config->buffer & (CLP_DEFAULT_ALIGNMENT - 1u)) != 0u) {
        return false;
    }
    return (uint32_t)config->slot_count * config->slot_size <=
           config->buffer_size;
}

bool cl_stream_init(cl_stream_t *stream, const cl_stream_config_t *config) {
    if (!stream || !stream_valid_config(config)) {
        return false;
    }
    memset(stream, 0, sizeof(*stream));
    stream->buffer = config->buffer;
    stream->buffer_size = config->buffer_size;
    stream->slots = config->slots;
    stream->slot_count = config->slot_count;
    stream->slot_size = config->slot_size;
    stream->flags = config->flags | cl_stream_flags_for_profile(config->profile);
    stream->profile = config->profile;
    stream->state = CL_STREAM_IDLE;
    for (uint8_t i = 0; i < stream->slot_count; ++i) {
        stream->slots[i].offset = 0;
        stream->slots[i].length = 0;
        stream->slots[i].flags = 0;
    }
    return true;
}

void cl_stream_reset(cl_stream_t *stream) {
    if (!stream) {
        return;
    }
    stream->read_slot = 0;
    stream->write_slot = 0;
    stream->ready_slots = 0;
    stream->state = CL_STREAM_IDLE;
    stream->read_offset = 0;
    stream->stream_id = 0;
    stream->rx_offset = 0;
    stream->tx_offset = 0;
    stream->remote_size_low = 0;
    stream->remote_size_high = 0;
    stream->last_error = 0;
    if (stream->slots) {
        for (uint8_t i = 0; i < stream->slot_count; ++i) {
            stream->slots[i].offset = 0;
            stream->slots[i].length = 0;
            stream->slots[i].flags = 0;
        }
    }
}

uint32_t cl_stream_capacity(const cl_stream_t *stream) {
    if (!stream) {
        return 0;
    }
    return (uint32_t)stream->slot_count * stream->slot_size;
}

uint32_t cl_stream_slot_capacity(const cl_stream_t *stream) {
    return stream ? stream->slot_size : 0;
}

uint8_t cl_stream_ready_count(const cl_stream_t *stream) {
    return stream ? stream->ready_slots : 0;
}

uint8_t cl_stream_free_count(const cl_stream_t *stream) {
    if (!stream || stream->ready_slots > stream->slot_count) {
        return 0;
    }
    return (uint8_t)(stream->slot_count - stream->ready_slots);
}

bool cl_stream_can_receive(const cl_stream_t *stream) {
    return cl_stream_free_count(stream) != 0;
}

int cl_stream_producer_acquire(cl_stream_t *stream,
                               cl_stream_span_t *out_span) {
    if (!out_span) {
        return CL_STREAM_ERR_INVALID;
    }
    if (!stream || !stream->buffer || !stream->slots || !stream->slot_size ||
        !stream->slot_count || stream->ready_slots > stream->slot_count) {
        out_span->data = 0;
        out_span->capacity = 0;
        out_span->slot_index = 0;
        out_span->reserved = 0;
        return CL_STREAM_ERR_INVALID;
    }
    if (!cl_stream_can_receive(stream)) {
        out_span->data = 0;
        out_span->capacity = 0;
        out_span->slot_index = stream->write_slot;
        out_span->reserved = 0;
        return CL_STREAM_ERR_FULL;
    }
    out_span->data = stream->buffer +
                     (uint32_t)stream->write_slot * stream->slot_size;
    out_span->capacity = stream->slot_size;
    out_span->slot_index = stream->write_slot;
    out_span->reserved = 0;
    return CL_STREAM_OK;
}

int cl_stream_producer_commit(cl_stream_t *stream, uint16_t length) {
    if (!stream) {
        return CL_STREAM_ERR_INVALID;
    }
    return cl_stream_producer_commit_at(stream, stream->rx_offset, length);
}

int cl_stream_producer_commit_at(cl_stream_t *stream,
                                 uint32_t offset,
                                 uint16_t length) {
    if (!stream || !stream->slots || !stream->buffer ||
        !stream->slot_count || !stream->slot_size ||
        stream->ready_slots > stream->slot_count ||
        stream->write_slot >= stream->slot_count) {
        return CL_STREAM_ERR_INVALID;
    }
    if (stream->ready_slots >= stream->slot_count) {
        return CL_STREAM_ERR_FULL;
    }
    if (length == 0 || length > stream->slot_size) {
        return CL_STREAM_ERR_TOO_LARGE;
    }
    cl_stream_slot_t *slot = &stream->slots[stream->write_slot];
    slot->offset = offset;
    slot->length = length;
    slot->flags = CLP_STREAM_SLOT_READY;
    stream->rx_offset = offset + length;
    stream->write_slot++;
    if (stream->write_slot >= stream->slot_count) {
        stream->write_slot = 0;
    }
    stream->ready_slots++;
    return 0;
}

int cl_stream_consumer_peek(const cl_stream_t *stream,
                            cl_stream_view_t *out_view) {
    if (!out_view) {
        return CL_STREAM_ERR_INVALID;
    }
    out_view->data = 0;
    out_view->offset = 0;
    out_view->length = 0;
    out_view->flags = 0;
    out_view->slot_index = 0;
    out_view->reserved[0] = 0;
    out_view->reserved[1] = 0;
    out_view->reserved[2] = 0;
    if (!stream || !stream->slots || !stream->buffer ||
        !stream->slot_count || stream->ready_slots > stream->slot_count ||
        stream->read_slot >= stream->slot_count) {
        return CL_STREAM_ERR_INVALID;
    }
    if (!stream->ready_slots) {
        return CL_STREAM_ERR_EMPTY;
    }
    const cl_stream_slot_t *slot = &stream->slots[stream->read_slot];
    if ((slot->flags & CLP_STREAM_SLOT_READY) == 0 || slot->length == 0) {
        return CL_STREAM_ERR_EMPTY;
    }
    if (slot->length > stream->slot_size ||
        stream->read_offset >= slot->length) {
        return CL_STREAM_ERR_INVALID;
    }
    out_view->data = stream->buffer +
                     (uint32_t)stream->read_slot * stream->slot_size +
                     stream->read_offset;
    out_view->offset = slot->offset + stream->read_offset;
    out_view->length = slot->length - stream->read_offset;
    out_view->flags = slot->flags;
    out_view->slot_index = stream->read_slot;
    return CL_STREAM_OK;
}

int cl_stream_consumer_release(cl_stream_t *stream) {
    if (!stream || !stream->slots || !stream->slot_count ||
        stream->ready_slots > stream->slot_count ||
        stream->read_slot >= stream->slot_count) {
        return CL_STREAM_ERR_INVALID;
    }
    if (!stream->ready_slots) {
        return CL_STREAM_ERR_EMPTY;
    }
    cl_stream_slot_t *slot = &stream->slots[stream->read_slot];
    if ((slot->flags & CLP_STREAM_SLOT_READY) == 0 || slot->length == 0 ||
        slot->length > stream->slot_size ||
        stream->read_offset >= slot->length) {
        return CL_STREAM_ERR_INVALID;
    }
    slot->flags = CLP_STREAM_SLOT_CONSUMED;
    slot->length = 0;
    stream->read_offset = 0;
    stream->read_slot++;
    if (stream->read_slot >= stream->slot_count) {
        stream->read_slot = 0;
    }
    stream->ready_slots--;
    return 0;
}

int cl_stream_consumer_release_bytes(cl_stream_t *stream, uint16_t length) {
    if (!stream) {
        return CL_STREAM_ERR_INVALID;
    }
    if (length == 0) {
        return CL_STREAM_OK;
    }
    if (!stream->slots || !stream->slot_count ||
        stream->ready_slots > stream->slot_count ||
        stream->read_slot >= stream->slot_count) {
        return CL_STREAM_ERR_INVALID;
    }
    if (!stream->ready_slots) {
        return CL_STREAM_ERR_EMPTY;
    }
    cl_stream_slot_t *slot = &stream->slots[stream->read_slot];
    if ((slot->flags & CLP_STREAM_SLOT_READY) == 0 || slot->length == 0 ||
        slot->length > stream->slot_size ||
        stream->read_offset >= slot->length) {
        return CL_STREAM_ERR_INVALID;
    }
    uint16_t remaining = slot->length - stream->read_offset;
    if (length > remaining) {
        return CL_STREAM_ERR_TOO_LARGE;
    }
    if (length < remaining) {
        stream->read_offset += length;
        return CL_STREAM_OK;
    }
    return cl_stream_consumer_release(stream);
}

uint8_t *cl_stream_receive_ptr(cl_stream_t *stream, uint16_t *out_capacity) {
    if (out_capacity) {
        *out_capacity = 0;
    }
    cl_stream_span_t span;
    if (cl_stream_producer_acquire(stream, &span) != CL_STREAM_OK) {
        return 0;
    }
    if (out_capacity) {
        *out_capacity = span.capacity;
    }
    return span.data;
}

int cl_stream_commit_receive(cl_stream_t *stream, uint16_t length) {
    return cl_stream_producer_commit(stream, length);
}

int cl_stream_commit_receive_at(cl_stream_t *stream,
                                uint32_t offset,
                                uint16_t length) {
    return cl_stream_producer_commit_at(stream, offset, length);
}

const uint8_t *cl_stream_peek(const cl_stream_t *stream,
                              uint16_t *out_length,
                              uint32_t *out_offset) {
    if (out_length) {
        *out_length = 0;
    }
    if (out_offset) {
        *out_offset = 0;
    }
    cl_stream_view_t view;
    if (cl_stream_consumer_peek(stream, &view) != CL_STREAM_OK) {
        return 0;
    }
    if (out_length) {
        *out_length = view.length;
    }
    if (out_offset) {
        *out_offset = view.offset;
    }
    return view.data;
}

int cl_stream_consume(cl_stream_t *stream) {
    return cl_stream_consumer_release(stream);
}

uint32_t cl_stream_flags_for_profile(uint8_t profile) {
    switch ((cl_stream_profile_t)profile) {
    case CL_STREAM_PROFILE_LOW_LATENCY:
        return CLP_STREAM_FLAG_LOW_LATENCY;
    case CL_STREAM_PROFILE_HIGH_THROUGHPUT:
        return CLP_STREAM_FLAG_HIGH_THROUGHPUT;
    case CL_STREAM_PROFILE_BALANCED:
    default:
        return 0;
    }
}
