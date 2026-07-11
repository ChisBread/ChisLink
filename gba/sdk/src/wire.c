#include "chislink/wire.h"

#include <stddef.h>

uint32_t cl_wire_load_le32(const uint8_t *src, uint32_t available) {
    uint32_t word = 0;
    for (uint32_t i = 0; i < 4u && i < available; ++i) {
        word |= (uint32_t)src[i] << (i * 8u);
    }
    return word;
}

void cl_wire_store_le32(uint8_t *dst, uint32_t word, uint32_t available) {
    for (uint32_t i = 0; i < 4u && i < available; ++i) {
        dst[i] = (uint8_t)(word >> (i * 8u));
    }
}

static void wire_xfer_payload_chunks(cl_client_t *client,
                                     const cl_wire_chunk_t *chunks,
                                     uint8_t chunk_count) {
    uint8_t pending[4] = {0, 0, 0, 0};
    uint8_t pending_len = 0;

    for (uint8_t i = 0; i < chunk_count; ++i) {
        const uint8_t *data = chunks[i].data;
        uint32_t length = chunks[i].length;
        for (uint32_t offset = 0; offset < length; ++offset) {
            pending[pending_len++] = data ? data[offset] : 0;
            if (pending_len == 4u) {
                (void)client->config.xfer32(cl_wire_load_le32(pending, 4u),
                                            client->config.transport_user);
                pending[0] = 0;
                pending[1] = 0;
                pending[2] = 0;
                pending[3] = 0;
                pending_len = 0;
            }
        }
    }

    if (pending_len) {
        (void)client->config.xfer32(cl_wire_load_le32(pending, pending_len),
                                    client->config.transport_user);
    }
}

static int wire_payload_length(const cl_wire_chunk_t *chunks,
                               uint8_t chunk_count,
                               uint32_t *out_length) {
    if (!out_length) {
        return -1;
    }
    uint32_t total = 0;
    for (uint8_t i = 0; i < chunk_count; ++i) {
        if (chunks[i].length > CLP_FRAME_MAX_PAYLOAD_BYTES ||
            total > CLP_FRAME_MAX_PAYLOAD_BYTES - chunks[i].length) {
            return -1;
        }
        total += chunks[i].length;
    }
    *out_length = total;
    return 0;
}

int cl_wire_read_response(cl_client_t *client, uint8_t channel, uint8_t opcode,
                          uint16_t seq, clp_header_t *response) {
    if (!client || !client->config.xfer32 || !response) {
        return -1;
    }

    for (uint8_t skipped = 0; skipped <= 8u; ++skipped) {
        uint32_t first = client->config.xfer32(
            clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
            client->config.transport_user);
        if (!clp_is_protocol_word(first) ||
            clp_word_type(first) == CLP_TYPE_NOP) {
            continue;
        }
        if (clp_word_type(first) == CLP_TYPE_ACK) {
            if (clp_word_channel(first) != channel ||
                clp_word_opcode(first) != opcode) {
                client->last_status = CLP_STATUS_BAD_PACKET;
                client->state = CL_CLIENT_ERROR;
                return -2;
            }
            response->type = clp_word_type(first);
            response->channel = clp_word_channel(first);
            response->opcode = clp_word_opcode(first);
            response->imm = clp_word_imm(first);
            response->length = 0;
            response->seq = seq;
            response->flags = 0;
            response->crc32 = 0;
            client->last_status = response->imm;
            client->state = response->imm == CLP_STATUS_OK ?
                            CL_CLIENT_IDLE : CL_CLIENT_ERROR;
            return 0;
        }
        if (clp_word_type(first) != CLP_TYPE_RESPONSE) {
            client->last_status = CLP_STATUS_BAD_PACKET;
            client->state = CL_CLIENT_ERROR;
            return -2;
        }

        uint32_t response_words[4];
        response_words[0] = first;
        for (uint8_t i = 1; i < 4u; ++i) {
            response_words[i] = client->config.xfer32(
                clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
                client->config.transport_user);
        }
        if (!clp_decode_header_words(response_words, response) ||
            response->type != CLP_TYPE_RESPONSE ||
            response->channel != channel ||
            response->opcode != opcode ||
            response->seq != seq) {
            client->last_status = CLP_STATUS_BAD_PACKET;
            client->state = CL_CLIENT_ERROR;
            return -2;
        }

        client->last_status = response->imm;
        client->state = response->imm == CLP_STATUS_OK ?
                        CL_CLIENT_IDLE : CL_CLIENT_ERROR;
        return 0;
    }

    client->last_status = CLP_STATUS_BAD_PACKET;
    client->state = CL_CLIENT_ERROR;
    return -2;
}

int cl_wire_command_chunks(cl_client_t *client, uint8_t channel, uint8_t opcode,
                           const cl_wire_chunk_t *chunks, uint8_t chunk_count,
                           clp_header_t *response) {
    if (!client || !client->config.xfer32 || !response ||
        (chunk_count && !chunks)) {
        return -1;
    }

    uint32_t length = 0;
    if (wire_payload_length(chunks, chunk_count, &length) < 0) {
        return -1;
    }
    clp_header_t header = {
        .type = CLP_TYPE_COMMAND,
        .channel = channel,
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
    wire_xfer_payload_chunks(client, chunks, chunk_count);

    return cl_wire_read_response(client, channel, opcode, header.seq, response);
}

void cl_wire_read_payload(cl_client_t *client, uint8_t *payload, uint32_t length) {
    uint32_t aligned = (uint32_t)clp_aligned_length(length);
    for (uint32_t offset = 0; offset < aligned; offset += 4u) {
        uint32_t word = client->config.xfer32(
            clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
            client->config.transport_user);
        if (payload && offset < length) {
            cl_wire_store_le32(payload + offset, word, length - offset);
        }
    }
}

void cl_wire_discard_payload(cl_client_t *client, uint32_t length) {
    cl_wire_read_payload(client, 0, length);
}
