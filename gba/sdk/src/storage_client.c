#include "chislink/storage_client.h"
#include "chislink/wire.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t load_le32(const uint8_t *src, uint32_t available) {
    uint32_t word = 0;
    for (uint32_t i = 0; i < 4u && i < available; ++i) {
        word |= (uint32_t)src[i] << (i * 8u);
    }
    return word;
}

static void store_le32(uint8_t *dst, uint32_t word, uint32_t available) {
    for (uint32_t i = 0; i < 4u && i < available; ++i) {
        dst[i] = (uint8_t)(word >> (i * 8u));
    }
}

static uint32_t payload_word(const uint8_t *payload, uint32_t length,
                             uint32_t offset) {
    if (!payload || offset >= length) {
        return 0;
    }
    return load_le32(payload + offset, length - offset);
}

static uint32_t direct_window_word(const cl_direct_window_t *window,
                                   uint32_t offset) {
    uint32_t word = 0;
    if (!window || !window->data || offset >= window->length) {
        return 0;
    }

    uint32_t available = window->length - offset;
    if (window->access == CL_DIRECT_WINDOW_ROM16_LE) {
        uintptr_t addr = (uintptr_t)(window->data + offset);
        if ((addr & 1u) == 0 && available >= 4u) {
            const volatile uint16_t *half = (const volatile uint16_t *)addr;
            return (uint32_t)half[0] | ((uint32_t)half[1] << 16u);
        }
    }

    for (uint32_t i = 0; i < 4u && i < available; ++i) {
        word |= (uint32_t)cl_direct_window_read_byte(window, offset + i) <<
                (i * 8u);
    }
    return word;
}

static void read_payload_words(cl_client_t *client, uint8_t *payload,
                               uint32_t length) {
    uint32_t aligned = (uint32_t)clp_aligned_length(length);
    for (uint32_t offset = 0; offset < aligned; offset += 4u) {
        uint32_t word = client->config.xfer32(
            clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
            client->config.transport_user);
        if (payload && offset < length) {
            store_le32(payload + offset, word, length - offset);
        }
    }
}

static void read_payload_writer_words(cl_client_t *client,
                                      const cl_payload_writer_t *writer,
                                      uint32_t length) {
    uint32_t aligned = (uint32_t)clp_aligned_length(length);
    for (uint32_t offset = 0; offset < aligned; offset += 4u) {
        uint32_t word = client->config.xfer32(
            clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
            client->config.transport_user);
        uint32_t valid = length - offset;
        if (valid > 4u) valid = 4u;
        writer->store_word(writer->ctx, offset, word, valid);
    }
}

static int read_payload_fast_or_words(cl_client_t *client, uint8_t *payload,
                                      uint32_t length) {
    if (length >= CL_CLIENT_FAST_PAYLOAD_THRESHOLD &&
        client->config.read_payload) {
        int ret = client->config.read_payload(payload, length,
                                              client->config.transport_user);
        if (ret < 0) {
            client->state = CL_CLIENT_ERROR;
            client->last_status = CLP_STATUS_TIMEOUT;
            return ret;
        }
        return 0;
    }
    read_payload_words(client, payload, length);
    return 0;
}

static int read_payload_writer_fast_or_words(
    cl_client_t *client,
    const cl_payload_writer_t *writer,
    uint32_t length) {
    if (length >= CL_CLIENT_FAST_PAYLOAD_THRESHOLD &&
        client->config.read_payload_writer) {
        int ret = client->config.read_payload_writer(
            writer, length, client->config.transport_user);
        if (ret < 0) {
            client->state = CL_CLIENT_ERROR;
            client->last_status = CLP_STATUS_TIMEOUT;
            return ret;
        }
        return 0;
    }
    read_payload_writer_words(client, writer, length);
    return 0;
}

static int write_payload_fast_or_words(cl_client_t *client,
                                       const cl_direct_window_t *window,
                                       uint32_t offset,
                                       uint32_t length) {
    if (length >= CL_CLIENT_FAST_PAYLOAD_THRESHOLD &&
        client->config.write_payload &&
        window->access == CL_DIRECT_WINDOW_BYTES) {
        const void *src = (const void *)(uintptr_t)(window->data + offset);
        int ret = client->config.write_payload(src, length,
                                               client->config.transport_user);
        if (ret < 0) {
            client->state = CL_CLIENT_ERROR;
            client->last_status = CLP_STATUS_TIMEOUT;
            return ret;
        }
        return 0;
    }
    if (length >= CL_CLIENT_FAST_PAYLOAD_THRESHOLD &&
        client->config.write_window_payload) {
        int ret = client->config.write_window_payload(window, offset, length,
                                                      client->config.transport_user);
        if (ret < 0) {
            client->state = CL_CLIENT_ERROR;
            client->last_status = CLP_STATUS_TIMEOUT;
            return ret;
        }
        return 0;
    }

    uint32_t aligned = (uint32_t)clp_aligned_length(length);
    for (uint32_t sent = 0; sent < aligned; sent += 4u) {
        (void)client->config.xfer32(direct_window_word(window, offset + sent),
                                    client->config.transport_user);
    }
    return 0;
}

static int storage_write_chunk(cl_storage_client_t *storage,
                               uint16_t handle,
                               const cl_direct_window_t *window,
                               uint32_t offset,
                               uint32_t length,
                               uint32_t *out_length) {
    if (out_length) {
        *out_length = 0;
    }
    if (length > CLP_STORAGE_WRITE_FRAME_MAX_DATA) {
        return -1;
    }
    uint8_t header_payload[CLP_STORAGE_WRITE_DATA_OFFSET];
    store_le32(header_payload, handle, sizeof(header_payload));

    cl_client_t *client = storage->client;
    clp_header_t header = {
        .type = CLP_TYPE_COMMAND,
        .channel = CLP_CH_STORAGE,
        .opcode = CLP_STORAGE_WRITE,
        .imm = 0,
        .length = CLP_STORAGE_WRITE_DATA_OFFSET + length,
        .seq = cl_client_next_seq(client),
        .flags = CLP_FLAG_NEEDS_ACK,
        .crc32 = 0,
    };
    uint32_t words[4];
    clp_encode_header_words(&header, words);
    for (uint8_t i = 0; i < 4u; ++i) {
        (void)client->config.xfer32(words[i], client->config.transport_user);
    }
    (void)client->config.xfer32(payload_word(header_payload,
                                            sizeof(header_payload), 0),
                                client->config.transport_user);
    int payload_ret = write_payload_fast_or_words(client, window, offset,
                                                  length);
    if (payload_ret < 0) {
        return -2;
    }

    clp_header_t response;
    int response_ret = cl_wire_read_response(client, CLP_CH_STORAGE,
                                             CLP_STORAGE_WRITE, header.seq,
                                             &response);
    if (response_ret < 0 ||
        response.length != 4u ||
        response.imm != CLP_STATUS_OK) {
        client->last_status = CLP_STATUS_BAD_PACKET;
        client->state = CL_CLIENT_ERROR;
        return -2;
    }
    uint8_t written_payload[4];
    read_payload_words(client, written_payload, sizeof(written_payload));
    *out_length = load_le32(written_payload, sizeof(written_payload));
    if (*out_length > length) {
        *out_length = 0;
        client->last_status = CLP_STATUS_BAD_PACKET;
        client->state = CL_CLIENT_ERROR;
        return -2;
    }
    client->last_status = CLP_STATUS_OK;
    client->state = CL_CLIENT_IDLE;
    return 0;
}

static int storage_path_length(const char *path, uint32_t *out_length) {
    if (!path || !out_length) {
        return -1;
    }
    uint32_t length = 0;
    while (length < CLP_STORAGE_PATH_MAX_BYTES && path[length]) {
        ++length;
    }
    if (length >= CLP_STORAGE_PATH_MAX_BYTES) {
        return -2;
    }
    *out_length = length + 1u;
    return 0;
}

static int send_command(cl_storage_client_t *storage, uint8_t opcode,
                        const uint8_t *payload, uint32_t length,
                        clp_header_t *response) {
    if (!storage || !storage->client || !storage->client->config.xfer32 ||
        !response) {
        return -1;
    }

    cl_client_t *client = storage->client;
    clp_header_t header = {
        .type = CLP_TYPE_COMMAND,
        .channel = CLP_CH_STORAGE,
        .opcode = opcode,
        .imm = 0,
        .length = length,
        .seq = cl_client_next_seq(client),
        .flags = CLP_FLAG_NEEDS_ACK,
        .crc32 = 0,
    };

    uint32_t words[4];
    clp_encode_header_words(&header, words);
    for (uint8_t i = 0; i < 4u; ++i) {
        (void)client->config.xfer32(words[i], client->config.transport_user);
    }
    uint32_t aligned = (uint32_t)clp_aligned_length(length);
    for (uint32_t offset = 0; offset < aligned; offset += 4u) {
        (void)client->config.xfer32(payload_word(payload, length, offset),
                                    client->config.transport_user);
    }

    return cl_wire_read_response(client, CLP_CH_STORAGE, opcode,
                                 header.seq, response);
}

int cl_storage_client_init(cl_storage_client_t *storage, cl_client_t *client,
                           void *scratch, size_t scratch_size) {
    if (!storage || !client) {
        return -1;
    }
    storage->client = client;
    storage->scratch = (uint8_t *)scratch;
    storage->scratch_size = scratch_size;
    return 0;
}

/* Check scratch buffer is large enough; returns pointer or NULL. */
static uint8_t *storage_scratch(cl_storage_client_t *storage, size_t required) {
    if (!storage || !storage->scratch || storage->scratch_size < required)
        return NULL;
    return storage->scratch;
}

static uint8_t *storage_request_scratch(cl_storage_client_t *storage,
                                        uint32_t required) {
    if (required > CLP_FRAME_MAX_PAYLOAD_BYTES) {
        return NULL;
    }
    return storage_scratch(storage, required);
}

int cl_storage_open(cl_storage_client_t *storage, const char *path,
                    uint32_t flags, uint16_t *out_handle) {
    if (!path || !out_handle) {
        return -1;
    }
    uint32_t path_len = 0;
    if (storage_path_length(path, &path_len) < 0) {
        return -2;
    }
    uint32_t length = CLP_STORAGE_OPEN_PATH_OFFSET + path_len;
    uint8_t *payload = storage_request_scratch(storage, length);
    if (!payload) {
        return -2;
    }
    store_le32(payload, flags, 4u);
    memcpy(payload + CLP_STORAGE_OPEN_PATH_OFFSET, path, path_len);

    clp_header_t response;
    int ret = send_command(storage, CLP_STORAGE_OPEN, payload, length, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.imm != CLP_STATUS_OK || response.length != 4u) {
        return -3;
    }
    uint8_t handle_payload[4];
    read_payload_words(storage->client, handle_payload, sizeof(handle_payload));
    *out_handle = (uint16_t)load_le32(handle_payload, sizeof(handle_payload));
    return *out_handle ? 0 : -4;
}

int cl_storage_close(cl_storage_client_t *storage, uint16_t handle) {
    uint8_t payload[4];
    store_le32(payload, handle, sizeof(payload));
    clp_header_t response;
    int ret = send_command(storage, CLP_STORAGE_CLOSE, payload, sizeof(payload),
                           &response);
    if (ret < 0) {
        return ret;
    }
    return response.imm == CLP_STATUS_OK ? 0 : -3;
}

static int storage_read(cl_storage_client_t *storage,
                        uint16_t handle,
                        void *dst,
                        const cl_payload_writer_t *writer,
                        uint32_t length,
                        uint32_t *out_length) {
    if ((!dst && !writer) || (writer && !writer->store_word) || !out_length) {
        return -1;
    }
    uint8_t payload[CLP_STORAGE_READ_REQUEST_BYTES];
    store_le32(payload, handle, 4u);
    store_le32(payload + 4u, length, 4u);

    clp_header_t response;
    int ret = send_command(storage, CLP_STORAGE_READ, payload, sizeof(payload),
                           &response);
    if (ret < 0) {
        return ret;
    }
    if (response.imm != CLP_STATUS_OK || response.length > length) {
        return -3;
    }
    if (writer) {
        ret = read_payload_writer_fast_or_words(storage->client, writer,
                                                response.length);
    } else {
        ret = read_payload_fast_or_words(storage->client, (uint8_t *)dst,
                                         response.length);
    }
    if (ret < 0) {
        return -2;
    }
    *out_length = response.length;
    return 0;
}

int cl_storage_read(cl_storage_client_t *storage, uint16_t handle, void *dst,
                    uint32_t length, uint32_t *out_length) {
    return storage_read(storage, handle, dst, NULL, length, out_length);
}

int cl_storage_read_with_writer(cl_storage_client_t *storage,
                                uint16_t handle,
                                const cl_payload_writer_t *writer,
                                uint32_t length,
                                uint32_t *out_length) {
    return storage_read(storage, handle, NULL, writer, length, out_length);
}

int cl_storage_write_direct(cl_storage_client_t *storage,
                            uint16_t handle,
                            const cl_direct_window_t *window,
                            uint32_t offset,
                            uint32_t length,
                            uint32_t *out_length) {
    if (!window || (!window->data && length) || !out_length ||
        offset > window->length || length > window->length - offset ||
        window->access > CL_DIRECT_WINDOW_ROM16_LE) {
        return -1;
    }
    if (!storage || !storage->client || !storage->client->config.xfer32) {
        return -1;
    }

    *out_length = 0;
    uint32_t total = 0;
    do {
        uint32_t remaining = length - total;
        uint32_t chunk = remaining > CLP_STORAGE_WRITE_FRAME_MAX_DATA ?
            CLP_STORAGE_WRITE_FRAME_MAX_DATA : remaining;
        uint32_t written = 0;
        int ret = storage_write_chunk(storage, handle, window, offset + total,
                                      chunk, &written);
        *out_length += written;
        if (ret < 0) {
            return ret;
        }
        if (written != chunk) {
            return 0;
        }
        total += written;
    } while (total < length);
    return 0;
}

int cl_storage_write(cl_storage_client_t *storage, uint16_t handle,
                     const void *src, uint32_t length, uint32_t *out_length) {
    cl_direct_window_t window = {
        .data = (const volatile uint8_t *)src,
        .length = length,
        .access = CL_DIRECT_WINDOW_BYTES,
    };
    return cl_storage_write_direct(storage, handle, &window, 0, length,
                                   out_length);
}

int cl_storage_seek(cl_storage_client_t *storage, uint16_t handle,
                    uint64_t offset) {
    uint8_t payload[CLP_STORAGE_SEEK_REQUEST_BYTES];
    store_le32(payload, handle, 4u);
    store_le32(payload + 4u, (uint32_t)offset, 4u);
    store_le32(payload + 8u, (uint32_t)(offset >> 32u), 4u);
    clp_header_t response;
    int ret = send_command(storage, CLP_STORAGE_SEEK, payload, sizeof(payload),
                           &response);
    if (ret < 0) {
        return ret;
    }
    return response.imm == CLP_STATUS_OK ? 0 : -3;
}

int cl_storage_flush(cl_storage_client_t *storage, uint16_t handle) {
    uint8_t payload[4];
    store_le32(payload, handle, sizeof(payload));
    clp_header_t response;
    int ret = send_command(storage, CLP_STORAGE_FLUSH, payload, sizeof(payload),
                           &response);
    if (ret < 0) {
        return ret;
    }
    return response.imm == CLP_STATUS_OK ? 0 : -3;
}

int cl_storage_stat(cl_storage_client_t *storage, const char *path,
                    cl_file_stat_t *out_stat) {
    if (!path || !out_stat) {
        return -1;
    }
    uint32_t length = 0;
    if (storage_path_length(path, &length) < 0) {
        return -2;
    }
    uint8_t *payload = storage_request_scratch(storage, length);
    if (!payload) {
        return -2;
    }
    memcpy(payload, path, length);
    clp_header_t response;
    int ret = send_command(storage, CLP_STORAGE_STAT, payload, length, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.imm != CLP_STATUS_OK) {
        storage->client->last_status = response.imm;
        return response.imm == CLP_STATUS_NOT_FOUND ?
               -(int)CLP_STATUS_NOT_FOUND : -3;
    }
    if (response.length != CLP_STORAGE_STAT_RESPONSE_WORDS * 4u) {
        storage->client->last_status = CLP_STATUS_BAD_PACKET;
        return -3;
    }

    uint8_t stat_payload[CLP_STORAGE_STAT_RESPONSE_WORDS * 4u];
    read_payload_words(storage->client, stat_payload, sizeof(stat_payload));
    memset(out_stat, 0, sizeof(*out_stat));
    out_stat->size = (uint64_t)load_le32(stat_payload, 4u) |
                     ((uint64_t)load_le32(stat_payload + 4u, 4u) << 32u);
    out_stat->preferred_block_size = load_le32(stat_payload + 8u, 4u);
    out_stat->max_block_size = load_le32(stat_payload + 12u, 4u);
    uint32_t meta = load_le32(stat_payload + 16u, 4u);
    out_stat->alignment = clp_file_meta_alignment(meta);
    out_stat->type = clp_file_meta_type(meta);
    out_stat->flags = clp_file_meta_flags(meta);
    storage->client->last_status = CLP_STATUS_OK;
    return 0;
}

int cl_storage_fstat(cl_storage_client_t *storage, uint16_t handle,
                     cl_file_stat_t *out_stat) {
    if (!out_stat) {
        return -1;
    }
    uint8_t payload[4];
    store_le32(payload, handle, sizeof(payload));

    clp_header_t response;
    int ret = send_command(storage, CLP_STORAGE_FSTAT, payload, sizeof(payload),
                           &response);
    if (ret < 0) {
        return ret;
    }
    if (response.imm != CLP_STATUS_OK ||
        response.length != CLP_STORAGE_STAT_RESPONSE_WORDS * 4u) {
        return -3;
    }

    uint8_t stat_payload[CLP_STORAGE_STAT_RESPONSE_WORDS * 4u];
    read_payload_words(storage->client, stat_payload, sizeof(stat_payload));
    memset(out_stat, 0, sizeof(*out_stat));
    out_stat->size = (uint64_t)load_le32(stat_payload, 4u) |
                     ((uint64_t)load_le32(stat_payload + 4u, 4u) << 32u);
    out_stat->preferred_block_size = load_le32(stat_payload + 8u, 4u);
    out_stat->max_block_size = load_le32(stat_payload + 12u, 4u);
    uint32_t meta = load_le32(stat_payload + 16u, 4u);
    out_stat->alignment = clp_file_meta_alignment(meta);
    out_stat->type = clp_file_meta_type(meta);
    out_stat->flags = clp_file_meta_flags(meta);
    return 0;
}

int cl_storage_copy(cl_storage_client_t *storage,
                    const char *src_path,
                    const char *dst_path,
                    uint32_t flags) {
    if (!storage || !src_path || !dst_path) {
        return -1;
    }
    if (storage->client &&
        !(cl_client_caps(storage->client) & CLP_CAP_STREAM_COPY)) {
        return -4;
    }
    uint32_t src_len = 0;
    uint32_t dst_len = 0;
    if (storage_path_length(src_path, &src_len) < 0 ||
        storage_path_length(dst_path, &dst_len) < 0) {
        return -2;
    }
    uint32_t length = CLP_STORAGE_COPY_SRC_PATH_OFFSET + src_len + dst_len;
    uint8_t *payload = storage_request_scratch(storage, length);
    if (!payload) {
        return -2;
    }
    store_le32(payload, flags, 4u);
    memcpy(payload + CLP_STORAGE_COPY_SRC_PATH_OFFSET, src_path, src_len);
    memcpy(payload + CLP_STORAGE_COPY_SRC_PATH_OFFSET + src_len,
           dst_path, dst_len);

    clp_header_t response;
    int ret = send_command(storage, CLP_STORAGE_COPY, payload, length, &response);
    if (ret < 0) {
        return ret;
    }
    return response.imm == CLP_STATUS_OK ? 0 : -3;
}

static int cl_storage_single_path_ack(cl_storage_client_t *storage,
                                      uint8_t opcode,
                                      const char *path) {
    if (!storage || !path) {
        return -1;
    }
    uint32_t length = 0;
    if (storage_path_length(path, &length) < 0) {
        return -2;
    }
    uint8_t *payload = storage_request_scratch(storage, length);
    if (!payload) {
        return -2;
    }
    memcpy(payload, path, length);

    clp_header_t response;
    int ret = send_command(storage, opcode, payload, length, &response);
    if (ret < 0) {
        return ret;
    }
    return response.imm == CLP_STATUS_OK ? 0 : -3;
}

int cl_storage_remove(cl_storage_client_t *storage, const char *path) {
    return cl_storage_single_path_ack(storage, CLP_STORAGE_REMOVE, path);
}

int cl_storage_mkdir(cl_storage_client_t *storage, const char *path) {
    return cl_storage_single_path_ack(storage, CLP_STORAGE_MKDIR, path);
}

int cl_storage_rename(cl_storage_client_t *storage,
                      const char *old_path,
                      const char *new_path) {
    if (!storage || !old_path || !new_path) {
        return -1;
    }
    uint32_t old_len = 0;
    uint32_t new_len = 0;
    if (storage_path_length(old_path, &old_len) < 0 ||
        storage_path_length(new_path, &new_len) < 0) {
        return -2;
    }
    uint32_t length = old_len + new_len;
    uint8_t *payload = storage_request_scratch(storage, length);
    if (!payload) {
        return -2;
    }
    memcpy(payload, old_path, old_len);
    memcpy(payload + old_len, new_path, new_len);

    clp_header_t response;
    int ret = send_command(storage, CLP_STORAGE_RENAME, payload, length,
                           &response);
    if (ret < 0) {
        return ret;
    }
    return response.imm == CLP_STATUS_OK ? 0 : -3;
}

int cl_storage_crc32_ex(cl_storage_client_t *storage,
                        const char *path,
                        uint64_t offset,
                        uint64_t length,
                        uint32_t chunk_size,
                        uint32_t flags,
                        uint32_t *out_crc32,
                        uint32_t max_chunks,
                        uint64_t *out_length,
                        uint32_t *out_chunks) {
    if (!storage || !path || !chunk_size || !out_crc32 || !max_chunks ||
        !out_length || !out_chunks) {
        return -1;
    }
    if (storage->client &&
        !(cl_client_caps(storage->client) & CLP_CAP_CRC32)) {
        return -4;
    }
    uint32_t path_len = 0;
    if (storage_path_length(path, &path_len) < 0) {
        return -2;
    }
    uint32_t request_length = CLP_STORAGE_CRC32_PATH_OFFSET + path_len;
    uint8_t *payload = storage_request_scratch(storage, request_length);
    if (!payload) {
        return -2;
    }
    store_le32(payload, (uint32_t)offset, 4u);
    store_le32(payload + 4u, (uint32_t)(offset >> 32u), 4u);
    store_le32(payload + 8u, (uint32_t)length, 4u);
    store_le32(payload + 12u, (uint32_t)(length >> 32u), 4u);
    store_le32(payload + 16u, chunk_size, 4u);
    store_le32(payload + 20u, max_chunks, 4u);
    store_le32(payload + 24u, flags, 4u);
    memcpy(payload + CLP_STORAGE_CRC32_PATH_OFFSET, path, path_len);

    clp_header_t response;
    int ret = send_command(storage, CLP_STORAGE_CRC32, payload,
                           request_length, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.imm != CLP_STATUS_OK ||
        response.length < CLP_STORAGE_CRC32_RESPONSE_FIXED_BYTES ||
        (response.length - CLP_STORAGE_CRC32_RESPONSE_FIXED_BYTES) % 4u) {
        return -3;
    }
    uint32_t wire_chunks =
        (response.length - CLP_STORAGE_CRC32_RESPONSE_FIXED_BYTES) / 4u;
    if (wire_chunks > max_chunks) {
        return -3;
    }
    uint8_t response_header[CLP_STORAGE_CRC32_RESPONSE_FIXED_BYTES];
    read_payload_words(storage->client, response_header,
                       sizeof(response_header));
    uint32_t reported_chunks = load_le32(response_header + 8u, 4u);
    if (reported_chunks != wire_chunks) {
        return -3;
    }
    *out_length = (uint64_t)load_le32(response_header, 4u) |
        ((uint64_t)load_le32(response_header + 4u, 4u) << 32u);
    *out_chunks = wire_chunks;
    if (wire_chunks) {
        ret = read_payload_fast_or_words(storage->client,
                                         (uint8_t *)out_crc32,
                                         wire_chunks * 4u);
        if (ret < 0) {
            *out_chunks = 0;
            return ret;
        }
    }
    return 0;
}

int cl_storage_crc32(cl_storage_client_t *storage,
                     const char *path,
                     uint64_t offset,
                     uint64_t length,
                     uint32_t chunk_size,
                     uint32_t *out_crc32,
                     uint32_t max_chunks,
                     uint64_t *out_length,
                     uint32_t *out_chunks) {
    return cl_storage_crc32_ex(storage, path, offset, length, chunk_size, 0u,
                               out_crc32, max_chunks,
                               out_length, out_chunks);
}

int cl_storage_list(cl_storage_client_t *storage,
                    const char *path,
                    uint32_t start_index,
                    uint32_t max_entries,
                    void *dst,
                    uint32_t capacity,
                    uint32_t *out_length,
                    uint32_t *out_next_index,
                    uint32_t *out_entry_count) {
    if (!storage || !path || !dst || !out_length || !out_next_index ||
        !out_entry_count ||
        capacity < CLP_STORAGE_LIST_RESPONSE_HEADER_BYTES) {
        return -1;
    }
    if (storage->client &&
        !(cl_client_caps(storage->client) & CLP_CAP_DIR_LIST)) {
        return -4;
    }

    uint32_t path_len = 0;
    if (storage_path_length(path, &path_len) < 0) {
        return -2;
    }
    uint32_t length = CLP_STORAGE_LIST_PATH_OFFSET + path_len;
    uint8_t *payload = storage_request_scratch(storage, length);
    if (!payload) {
        return -2;
    }
    store_le32(payload, start_index, 4u);
    store_le32(payload + 4u, max_entries, 4u);
    store_le32(payload + 8u, capacity, 4u);
    memcpy(payload + CLP_STORAGE_LIST_PATH_OFFSET, path, path_len);

    clp_header_t response;
    int ret = send_command(storage, CLP_STORAGE_LIST, payload, length, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.imm != CLP_STATUS_OK ||
        response.length < CLP_STORAGE_LIST_RESPONSE_HEADER_BYTES ||
        response.length > capacity) {
        return -3;
    }

    read_payload_words(storage->client, (uint8_t *)dst, response.length);
    uint8_t *bytes = (uint8_t *)dst;
    *out_next_index = load_le32(bytes, 4u);
    *out_entry_count = load_le32(bytes + 4u, 4u);
    *out_length = response.length;
    return 0;
}

static int remote_open(void *ctx, const char *path, uint32_t flags,
                       uint16_t *out_local) {
    return cl_storage_open((cl_storage_client_t *)ctx, path, flags, out_local);
}

static int remote_close(void *ctx, uint16_t local) {
    return cl_storage_close((cl_storage_client_t *)ctx, local);
}

static int remote_read(void *ctx, uint16_t local, void *dst, uint32_t length,
                       uint32_t *out_length) {
    return cl_storage_read((cl_storage_client_t *)ctx, local, dst, length,
                           out_length);
}

static int remote_write(void *ctx, uint16_t local, const void *src,
                        uint32_t length, uint32_t *out_length) {
    return cl_storage_write((cl_storage_client_t *)ctx, local, src, length,
                            out_length);
}

static int remote_direct_write(void *ctx,
                               uint16_t local,
                               const cl_direct_window_t *window,
                               uint32_t offset,
                               uint32_t length,
                               uint32_t *out_length) {
    return cl_storage_write_direct((cl_storage_client_t *)ctx, local, window,
                                   offset, length, out_length);
}

static int remote_seek(void *ctx, uint16_t local, uint64_t offset) {
    return cl_storage_seek((cl_storage_client_t *)ctx, local, offset);
}

static int remote_stat(void *ctx, const char *path, cl_file_stat_t *out_stat) {
    return cl_storage_stat((cl_storage_client_t *)ctx, path, out_stat);
}

static int remote_fstat(void *ctx, uint16_t local, cl_file_stat_t *out_stat) {
    return cl_storage_fstat((cl_storage_client_t *)ctx, local, out_stat);
}

static int remote_flush(void *ctx, uint16_t local) {
    return cl_storage_flush((cl_storage_client_t *)ctx, local);
}

static int remote_list_dir(void *ctx, const char *path,
                           cl_file_list_cb callback, void *user) {
    if (!ctx || !path || !callback) {
        return -1;
    }
    uint8_t *buffer = storage_scratch((cl_storage_client_t *)ctx, 512u);
    if (!buffer) return -1;
    uint32_t start_index = 0;
    uint32_t guard = 0;
    do {
        uint32_t out_length = 0;
        uint32_t next_index = CLP_STORAGE_LIST_DONE;
        uint32_t entry_count = 0;
        int ret = cl_storage_list((cl_storage_client_t *)ctx, path,
                                  start_index, 8u, buffer, 512u,
                                  &out_length, &next_index, &entry_count);
        if (ret < 0) {
            return ret;
        }
        uint32_t offset = CLP_STORAGE_LIST_RESPONSE_HEADER_BYTES;
        for (uint32_t i = 0; i < entry_count; ++i) {
            if (offset + CLP_STORAGE_DIR_ENTRY_BYTES > out_length) {
                return -3;
            }
            uint8_t name_len = buffer[offset];
            if (name_len == 0 ||
                offset + CLP_STORAGE_DIR_ENTRY_BYTES +
                (uint32_t)name_len + 1u > out_length) {
                return -3;
            }
            const char *name = (const char *)buffer + offset +
                CLP_STORAGE_DIR_ENTRY_BYTES;
            if (name[name_len] != '\0') {
                return -3;
            }
            cl_file_dir_entry_t entry = {
                .name = name,
                .size = (uint64_t)load_le32(buffer + offset + 4u, 4u) |
                        ((uint64_t)load_le32(buffer + offset + 8u, 4u) << 32u),
                .type = buffer[offset + 1u],
                .flags = buffer[offset + 2u],
            };
            ret = callback(&entry, user);
            if (ret < 0) {
                return ret;
            }
            offset += CLP_STORAGE_DIR_ENTRY_BYTES + (uint32_t)name_len + 1u;
        }
        if (next_index == CLP_STORAGE_LIST_DONE) {
            return 0;
        }
        if (next_index <= start_index || ++guard > 4096u) {
            return -3;
        }
        start_index = next_index;
    } while (1);
}

static int remote_list_page(void *ctx,
                            const char *path,
                            uint32_t start_index,
                            uint32_t max_entries,
                            cl_file_list_cb callback,
                            void *user,
                            uint32_t *out_next_index,
                            uint32_t *out_entry_count) {
    if (!ctx || !path || !callback || !out_next_index || !out_entry_count) {
        return -1;
    }
    uint8_t *buffer = storage_scratch((cl_storage_client_t *)ctx, 512u);
    if (!buffer) {
        return -1;
    }
    uint32_t out_length = 0;
    uint32_t next_index = CLP_STORAGE_LIST_DONE;
    uint32_t entry_count = 0;
    int ret = cl_storage_list((cl_storage_client_t *)ctx, path, start_index,
                              max_entries, buffer, 512u,
                              &out_length, &next_index, &entry_count);
    if (ret < 0) {
        return ret;
    }

    uint32_t offset = CLP_STORAGE_LIST_RESPONSE_HEADER_BYTES;
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < entry_count; ++i) {
        if (offset + CLP_STORAGE_DIR_ENTRY_BYTES > out_length) {
            return -3;
        }
        uint8_t name_len = buffer[offset];
        if (name_len == 0 ||
            offset + CLP_STORAGE_DIR_ENTRY_BYTES +
            (uint32_t)name_len + 1u > out_length) {
            return -3;
        }
        const char *name = (const char *)buffer + offset +
            CLP_STORAGE_DIR_ENTRY_BYTES;
        if (name[name_len] != '\0') {
            return -3;
        }
        cl_file_dir_entry_t entry = {
            .name = name,
            .size = (uint64_t)load_le32(buffer + offset + 4u, 4u) |
                    ((uint64_t)load_le32(buffer + offset + 8u, 4u) << 32u),
            .type = buffer[offset + 1u],
            .flags = buffer[offset + 2u],
        };
        ret = callback(&entry, user);
        if (ret < 0) {
            return ret;
        }
        emitted++;
        offset += CLP_STORAGE_DIR_ENTRY_BYTES + (uint32_t)name_len + 1u;
    }

    *out_next_index = next_index;
    *out_entry_count = emitted;
    return 0;
}

static int remote_copy(void *ctx, const char *src_path, const char *dst_path,
                       uint32_t flags) {
    return cl_storage_copy((cl_storage_client_t *)ctx, src_path, dst_path,
                           flags);
}

static int remote_remove(void *ctx, const char *path) {
    return cl_storage_remove((cl_storage_client_t *)ctx, path);
}

static int remote_mkdir(void *ctx, const char *path) {
    return cl_storage_mkdir((cl_storage_client_t *)ctx, path);
}

static int remote_rename(void *ctx, const char *old_path,
                         const char *new_path) {
    return cl_storage_rename((cl_storage_client_t *)ctx, old_path, new_path);
}

static const cl_file_backend_ops_t remote_ops = {
    .open = remote_open,
    .close = remote_close,
    .read = remote_read,
    .write = remote_write,
    .direct_write = remote_direct_write,
    .seek = remote_seek,
    .stat = remote_stat,
    .fstat = remote_fstat,
    .flush = remote_flush,
    .list_dir = remote_list_dir,
    .list_page = remote_list_page,
    .copy = remote_copy,
    .remove = remote_remove,
    .mkdir = remote_mkdir,
    .rename = remote_rename,
};

int cl_file_register_remote_storage(cl_storage_client_t *storage) {
    static cl_file_backend_t sd_backend = {
        .prefix = "/sd",
        .prefix_length = 3,
        .ops = &remote_ops,
        .ctx = 0,
    };
    static cl_file_backend_t littlefs_backend = {
        .prefix = "/littlefs",
        .prefix_length = 9,
        .ops = &remote_ops,
        .ctx = 0,
    };
    static cl_file_backend_t dev_backend = {
        .prefix = "/dev",
        .prefix_length = 4,
        .ops = &remote_ops,
        .ctx = 0,
    };
    if (!storage) {
        return -1;
    }
    sd_backend.ctx = storage;
    littlefs_backend.ctx = storage;
    dev_backend.ctx = storage;
    int ret = cl_file_register_backend(&sd_backend);
    if (ret < 0) {
        return ret;
    }
    ret = cl_file_register_backend(&littlefs_backend);
    if (ret < 0) {
        return ret;
    }
    return cl_file_register_backend(&dev_backend);
}
