#include "chislink/stream.h"

#include "chislink/wire.h"

#include <string.h>

static uint32_t stream_cstr_len(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) {
        ++n;
    }
    return n;
}

static int stream_status_error(cl_client_t *client,
                               const clp_header_t *response) {
    uint16_t status = response ? response->imm : CLP_STATUS_BAD_PACKET;
    if (client) {
        client->last_status = status;
        client->state = status == CLP_STATUS_OK ? CL_CLIENT_IDLE :
                                                  CL_CLIENT_ERROR;
    }
    return cl_client_status_error(status);
}

static int stream_command(cl_client_t *client,
                          uint8_t opcode,
                          const cl_wire_chunk_t *chunks,
                          uint8_t chunk_count,
                          clp_header_t *response) {
    int ret = cl_wire_command_chunks(client, CLP_CH_STREAM, opcode, chunks,
                                     chunk_count, response);
    if (ret < 0) {
        return ret;
    }
    if (response->imm != CLP_STATUS_OK) {
        return stream_status_error(client, response);
    }
    return 0;
}

int cl_stream_open(cl_client_t *client,
                   cl_stream_t *stream,
                   uint8_t kind,
                   const char *target) {
    if (!client || !stream || !target || !target[0] ||
        !stream->buffer || !stream->slots || stream->slot_count == 0 ||
        stream->slot_size == 0) {
        return -1;
    }
    if (stream->stream_id) {
        return -5;
    }
    uint32_t target_len = stream_cstr_len(target) + 1u;
    if (target_len == 1u ||
        CLP_STREAM_OPEN_PATH_OFFSET + target_len > CLP_DEFAULT_BLOCK_SIZE) {
        return -2;
    }
    cl_stream_reset(stream);

    uint8_t request[CLP_STREAM_OPEN_FIXED_BYTES];
    cl_wire_store_le32(request, kind, 4u);
    cl_wire_store_le32(request + 4u, stream->flags, 4u);
    cl_wire_store_le32(request + 8u, stream->slot_size, 4u);
    cl_wire_store_le32(request + 12u, stream->slot_count, 4u);
    cl_wire_store_le32(request + 16u, cl_stream_capacity(stream), 4u);
    cl_wire_chunk_t chunks[2] = {
        { request, sizeof(request) },
        { (const uint8_t *)target, target_len },
    };
    clp_header_t response;
    int ret = stream_command(client, CLP_STREAM_OPEN, chunks, 2u, &response);
    if (ret < 0) {
        stream->state = CL_STREAM_ERROR;
        stream->last_error = ret;
        return ret;
    }
    if (response.length != CLP_STREAM_OPEN_RESPONSE_WORDS * 4u) {
        stream->state = CL_STREAM_ERROR;
        stream->last_error = CL_CLIENT_STATUS_ERROR(CLP_STATUS_BAD_PACKET);
        cl_wire_discard_payload(client, response.length);
        return -3;
    }

    uint8_t payload[CLP_STREAM_OPEN_RESPONSE_WORDS * 4u];
    cl_wire_read_payload(client, payload, sizeof(payload));
    stream->stream_id = (uint8_t)cl_wire_load_le32(payload, 4u);
    stream->remote_size_low = cl_wire_load_le32(payload + 4u, 4u);
    stream->remote_size_high = cl_wire_load_le32(payload + 8u, 4u);
    stream->rx_offset = 0;
    stream->tx_offset = 0;
    stream->last_error = 0;
    stream->state = stream->stream_id ? CL_STREAM_OPEN : CL_STREAM_ERROR;
    return stream->stream_id ? 0 : -4;
}

int cl_stream_subscribe_file(cl_client_t *client,
                             cl_stream_t *stream,
                             const char *path) {
    return cl_stream_open(client, stream, CLP_STREAM_KIND_FILE, path);
}

int cl_stream_close(cl_client_t *client, cl_stream_t *stream) {
    if (!client || !stream || !stream->stream_id) {
        return -1;
    }
    uint8_t request[CLP_STREAM_CLOSE_REQUEST_BYTES];
    cl_wire_store_le32(request, stream->stream_id, 4u);
    cl_wire_chunk_t chunk = { request, sizeof(request) };
    clp_header_t response;
    int ret = stream_command(client, CLP_STREAM_CLOSE, &chunk, 1u, &response);
    if (response.length) {
        cl_wire_discard_payload(client, response.length);
    }
    if (ret < 0) {
        stream->state = CL_STREAM_ERROR;
        stream->last_error = ret;
        return ret;
    }
    stream->stream_id = 0;
    stream->state = CL_STREAM_CLOSED;
    return 0;
}

int cl_stream_poll(cl_client_t *client,
                   cl_stream_t *stream,
                   cl_stream_remote_status_t *out_status) {
    if (!client || !stream || !stream->stream_id || !out_status) {
        return -1;
    }
    uint8_t request[CLP_STREAM_POLL_REQUEST_BYTES];
    cl_wire_store_le32(request, stream->stream_id, 4u);
    cl_wire_chunk_t chunk = { request, sizeof(request) };
    clp_header_t response;
    int ret = stream_command(client, CLP_STREAM_POLL, &chunk, 1u, &response);
    if (ret < 0) {
        stream->state = CL_STREAM_ERROR;
        stream->last_error = ret;
        return ret;
    }
    if (response.length != CLP_STREAM_POLL_RESPONSE_BYTES) {
        cl_wire_discard_payload(client, response.length);
        stream->last_error = CL_CLIENT_STATUS_ERROR(CLP_STATUS_BAD_PACKET);
        return -2;
    }
    uint8_t payload[CLP_STREAM_POLL_RESPONSE_BYTES];
    cl_wire_read_payload(client, payload, sizeof(payload));
    out_status->available = cl_wire_load_le32(payload, 4u);
    out_status->offset = cl_wire_load_le32(payload + 4u, 4u);
    out_status->flags = cl_wire_load_le32(payload + 8u, 4u);
    out_status->last_error = cl_wire_load_le32(payload + 12u, 4u);
    stream->last_error = (int)out_status->last_error;
    return 0;
}

int cl_stream_credit(cl_client_t *client,
                     cl_stream_t *stream,
                     uint32_t consumed_bytes) {
    if (!client || !stream || !stream->stream_id) {
        return -1;
    }
    uint8_t request[CLP_STREAM_CREDIT_REQUEST_BYTES];
    cl_wire_store_le32(request, stream->stream_id, 4u);
    cl_wire_store_le32(request + 4u, consumed_bytes, 4u);
    cl_wire_store_le32(request + 8u, cl_stream_free_count(stream), 4u);
    cl_wire_chunk_t chunk = { request, sizeof(request) };
    clp_header_t response;
    int ret = stream_command(client, CLP_STREAM_CREDIT, &chunk, 1u, &response);
    if (response.length) {
        cl_wire_discard_payload(client, response.length);
    }
    if (ret < 0) {
        stream->state = CL_STREAM_ERROR;
        stream->last_error = ret;
        return ret;
    }
    (void)consumed_bytes;
    return 0;
}

int cl_stream_seek(cl_client_t *client, cl_stream_t *stream, uint64_t offset) {
    if (!client || !stream || !stream->stream_id) {
        return -1;
    }
    uint8_t stream_id = stream->stream_id;
    uint8_t request[CLP_STREAM_SEEK_REQUEST_BYTES];
    cl_wire_store_le32(request, stream_id, 4u);
    cl_wire_store_le32(request + 4u, (uint32_t)offset, 4u);
    cl_wire_store_le32(request + 8u, (uint32_t)(offset >> 32u), 4u);
    cl_wire_chunk_t chunk = { request, sizeof(request) };
    clp_header_t response;
    int ret = stream_command(client, CLP_STREAM_SEEK, &chunk, 1u, &response);
    if (response.length) {
        cl_wire_discard_payload(client, response.length);
    }
    if (ret < 0) {
        stream->state = CL_STREAM_ERROR;
        stream->last_error = ret;
        return ret;
    }
    cl_stream_reset(stream);
    stream->stream_id = stream_id;
    stream->rx_offset = (uint32_t)offset;
    stream->state = CL_STREAM_OPEN;
    return 0;
}

int cl_stream_recv_slot(cl_client_t *client, cl_stream_t *stream) {
    if (!client || !stream || !stream->stream_id ||
        !cl_stream_can_receive(stream)) {
        return -1;
    }
    cl_stream_span_t span;
    int ret = cl_stream_producer_acquire(stream, &span);
    if (ret < 0) {
        stream->last_error = ret;
        return ret;
    }
    if (!span.data || span.capacity == 0) {
        return -2;
    }

    uint8_t request[CLP_STREAM_RECV_REQUEST_BYTES];
    cl_wire_store_le32(request, stream->stream_id, 4u);
    cl_wire_store_le32(request + 4u, span.capacity, 4u);
    cl_wire_chunk_t chunk = { request, sizeof(request) };
    clp_header_t response;
    ret = stream_command(client, CLP_STREAM_RECV, &chunk, 1u, &response);
    if (ret < 0) {
        stream->state = CL_STREAM_ERROR;
        stream->last_error = ret;
        return ret;
    }
    if (response.length > span.capacity) {
        cl_wire_discard_payload(client, response.length);
        stream->last_error = CL_CLIENT_STATUS_ERROR(CLP_STATUS_BAD_PACKET);
        return -3;
    }
    if (response.length == 0) {
        return 0;
    }

    cl_wire_read_payload(client, span.data, response.length);
    ret = cl_stream_producer_commit(stream, (uint16_t)response.length);
    if (ret < 0) {
        stream->state = CL_STREAM_ERROR;
        stream->last_error = ret;
    }
    return ret < 0 ? ret : (int)response.length;
}

static int stream_read_or_discard(cl_client_t *client,
                                  cl_stream_t *stream,
                                  uint8_t *dst,
                                  uint32_t length,
                                  uint32_t *out_length,
                                  bool discard) {
    if (!client || !stream || !stream->stream_id || !out_length ||
        (!discard && !dst && length != 0)) {
        return -1;
    }
    *out_length = 0;
    while (*out_length < length) {
        cl_stream_view_t view;
        int ret = cl_stream_consumer_peek(stream, &view);
        if (ret == CL_STREAM_ERR_EMPTY) {
            ret = cl_stream_recv_slot(client, stream);
            if (ret < 0) {
                stream->last_error = ret;
                return ret;
            }
            if (ret == 0) {
                break;
            }
            ret = cl_stream_consumer_peek(stream, &view);
        }
        if (ret < 0) {
            stream->last_error = ret;
            return ret;
        }
        uint32_t take = length - *out_length;
        if (take > view.length) {
            take = view.length;
        }
        if (!discard && dst && take) {
            memcpy(dst + *out_length, view.data, take);
        }
        ret = cl_stream_consumer_release_bytes(stream, (uint16_t)take);
        if (ret < 0) {
            stream->last_error = ret;
            return ret;
        }
        *out_length += take;
    }
    return 0;
}

int cl_stream_read(cl_client_t *client,
                   cl_stream_t *stream,
                   void *dst,
                   uint32_t length,
                   uint32_t *out_length) {
    return stream_read_or_discard(client, stream, (uint8_t *)dst, length,
                                  out_length, false);
}

int cl_stream_read_exact(cl_client_t *client,
                         cl_stream_t *stream,
                         void *dst,
                         uint32_t length) {
    uint32_t read = 0;
    int ret = cl_stream_read(client, stream, dst, length, &read);
    if (ret < 0) {
        return ret;
    }
    return read == length ? 0 : -3;
}

int cl_stream_discard(cl_client_t *client,
                      cl_stream_t *stream,
                      uint32_t length,
                      uint32_t *out_length) {
    return stream_read_or_discard(client, stream, 0, length, out_length, true);
}

int cl_stream_send(cl_client_t *client,
                   cl_stream_t *stream,
                   const void *data,
                   uint32_t length) {
    if (!client || !stream || !stream->stream_id ||
        (!data && length != 0) ||
        length > CLP_DEFAULT_BLOCK_SIZE - CLP_STREAM_SEND_DATA_OFFSET) {
        return -1;
    }
    uint8_t request[CLP_STREAM_SEND_DATA_OFFSET];
    cl_wire_store_le32(request, stream->stream_id, 4u);
    cl_wire_chunk_t chunks[2] = {
        { request, sizeof(request) },
        { (const uint8_t *)data, length },
    };
    clp_header_t response;
    int ret = stream_command(client, CLP_STREAM_SEND, chunks, 2u, &response);
    if (ret < 0) {
        stream->state = CL_STREAM_ERROR;
        stream->last_error = ret;
        return ret;
    }
    if (response.length != CLP_STREAM_SEND_RESPONSE_WORDS * 4u) {
        cl_wire_discard_payload(client, response.length);
        stream->last_error = CL_CLIENT_STATUS_ERROR(CLP_STATUS_BAD_PACKET);
        return -2;
    }
    uint8_t payload[CLP_STREAM_SEND_RESPONSE_WORDS * 4u];
    cl_wire_read_payload(client, payload, sizeof(payload));
    uint32_t written = cl_wire_load_le32(payload, 4u);
    stream->tx_offset += written;
    return (int)written;
}

int cl_stream_pump(cl_client_t *client, cl_stream_t *stream) {
    if (!client || !stream || !stream->stream_id) {
        return -1;
    }
    uint8_t request[CLP_STREAM_PUMP_REQUEST_BYTES];
    cl_wire_store_le32(request, (uint32_t)stream->stream_id, sizeof(request));
    cl_wire_chunk_t chunk = { request, sizeof(request) };
    clp_header_t response;
    int ret = stream_command(client, CLP_STREAM_PUMP, &chunk, 1u, &response);
    if (response.length) {
        cl_wire_discard_payload(client, response.length);
    }
    if (ret < 0) {
        stream->state = CL_STREAM_ERROR;
        stream->last_error = ret;
        return ret;
    }
    stream->stream_id = 0;
    stream->state = CL_STREAM_CLOSED;
    return 0;
}
