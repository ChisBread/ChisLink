#ifndef CHISLINK_SDK_WIRE_H
#define CHISLINK_SDK_WIRE_H

#include <stdint.h>

#include "chislink/client.h"

typedef struct cl_wire_chunk {
    const uint8_t *data;
    uint32_t length;
} cl_wire_chunk_t;

uint32_t cl_wire_load_le32(const uint8_t *src, uint32_t available);
void cl_wire_store_le32(uint8_t *dst, uint32_t word, uint32_t available);
int cl_wire_read_response(cl_client_t *client, uint8_t channel, uint8_t opcode,
                          uint16_t seq, clp_header_t *response);
int cl_wire_command_chunks(cl_client_t *client, uint8_t channel, uint8_t opcode,
                           const cl_wire_chunk_t *chunks, uint8_t chunk_count,
                           clp_header_t *response);
void cl_wire_read_payload(cl_client_t *client, uint8_t *payload, uint32_t length);
void cl_wire_discard_payload(cl_client_t *client, uint32_t length);

#endif
