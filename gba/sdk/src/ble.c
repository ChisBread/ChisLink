#include "chislink/ble.h"

#include "chislink/wire.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define CL_BLE_UUID_DEVICE_NAME 0x2a00u
#define CL_BLE_UUID_CCCD        0x2902u
#define CL_BLE_CCCD_NOTIFY      0x0001u
#define CL_BLE_CCCD_INDICATE    0x0002u

static const uint8_t CL_BLE_LINK_SERVICE_UUID128[16] = {
    0x34,0x9b,0x5f,0x80,0x00,0x00,0x80,0x00,
    0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00
};

static const uint8_t CL_BLE_LINK_RX_UUID128[16] = {
    0x34,0x9b,0x5f,0x80,0x00,0x00,0x80,0x00,
    0x00,0x01,0x00,0x00,0x10,0x00,0x00,0x00
};

static const uint8_t CL_BLE_LINK_TX_UUID128[16] = {
    0x34,0x9b,0x5f,0x80,0x00,0x00,0x80,0x00,
    0x00,0x01,0x00,0x00,0x20,0x00,0x00,0x00
};

static int ble_expect_ack(cl_ble_t *ble, uint8_t opcode,
                          const cl_wire_chunk_t *chunks, size_t chunk_count);

static int response_ok(const clp_header_t *response, uint32_t length) {
    return response &&
           response->imm == CLP_STATUS_OK &&
           response->length == length;
}

static int discard_response_error(cl_client_t *client,
                                  const clp_header_t *response) {
    if (!response) {
        return -2;
    }
    cl_wire_discard_payload(client, response->length);
    return response->imm == CLP_STATUS_OK ? -2 :
                                            cl_client_status_error(response->imm);
}

static cl_client_t *ble_client(cl_ble_t *ble) {
    return ble ? ble->client : NULL;
}

static uint8_t *ble_scratch(cl_ble_t *ble, size_t required) {
    if (!ble || !ble->scratch || ble->scratch_size < required) {
        return NULL;
    }
    return ble->scratch;
}

static int ble_adv_has_uuid128(const cl_ble_event_t *event,
                               const uint8_t uuid[16]) {
    if (!event || !uuid) return 0;
    uint8_t offset = 0;
    while (offset < event->data_length) {
        uint8_t field_len = event->data[offset];
        if (field_len == 0) break;
        uint16_t field_end = (uint16_t)offset + 1u + field_len;
        if (field_end > event->data_length || field_len < 1u) break;
        uint8_t type = event->data[offset + 1u];
        if (type == 0x06u || type == 0x07u) {
            uint16_t pos = (uint16_t)offset + 2u;
            while (pos + 16u <= field_end) {
                if (memcmp(event->data + pos, uuid, 16u) == 0) return 1;
                pos = (uint16_t)(pos + 16u);
            }
        }
        offset = (uint8_t)field_end;
    }
    return 0;
}

void cl_ble_build_scan_request(uint8_t payload[4], uint16_t duration_ms) {
    clp_store_le32(payload, duration_ms, 4u);
}

int cl_ble_build_connect_request(uint8_t payload[CLP_BLE_CONNECT_REQUEST_BYTES],
                                 const cl_ble_addr_t *addr) {
    if (!payload || !addr) {
        return -1;
    }
    memset(payload, 0, CLP_BLE_CONNECT_REQUEST_BYTES);
    memcpy(payload, addr->bytes, sizeof(addr->bytes));
    payload[6] = addr->type;
    return 0;
}

void cl_ble_build_handle_request(uint8_t payload[4], cl_ble_handle_t handle) {
    clp_store_le32(payload, handle, 4u);
}

void cl_ble_build_gatt_find_chr16_request(
    uint8_t payload[CLP_BLE_GATT_FIND_CHR16_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t start_handle,
    uint16_t end_handle,
    uint16_t uuid16) {
    if (!start_handle) {
        start_handle = 1u;
    }
    if (!end_handle) {
        end_handle = 0xffffu;
    }
    clp_store_le32(payload, handle, 4u);
    clp_store_le32(payload + 4u,
                   (uint32_t)start_handle |
                       ((uint32_t)end_handle << 16u),
                   4u);
    clp_store_le32(payload + 8u, uuid16, 4u);
}

void cl_ble_build_gatt_find_request(
    uint8_t payload[CLP_BLE_GATT_FIND_SERVICE_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t start_handle,
    uint16_t end_handle,
    uint16_t uuid16) {
    cl_ble_build_gatt_find_chr16_request(payload, handle, start_handle,
                                         end_handle, uuid16);
}

void cl_ble_build_gatt_read_request(
    uint8_t payload[CLP_BLE_GATT_READ_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t attr,
    uint32_t capacity) {
    clp_store_le32(payload, handle, 4u);
    clp_store_le32(payload + 4u, attr, 4u);
    clp_store_le32(payload + 8u, capacity, 4u);
}

void cl_ble_build_gatt_write_header(
    uint8_t payload[CLP_BLE_GATT_WRITE_DATA_OFFSET],
    cl_ble_handle_t handle,
    uint16_t attr) {
    clp_store_le32(payload, handle, 4u);
    clp_store_le32(payload + 4u, attr, 4u);
}

void cl_ble_build_gatt_notify_request(
    uint8_t payload[CLP_BLE_GATT_NOTIFY_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t attr,
    uint8_t enable) {
    clp_store_le32(payload, handle, 4u);
    clp_store_le32(payload + 4u, attr, 4u);
    clp_store_le32(payload + 8u,
                   enable ? CLP_BLE_NOTIFY_ENABLE :
                            CLP_BLE_NOTIFY_DISABLE,
                   4u);
}

int cl_ble_parse_handle_response(const uint8_t payload[4],
                                 cl_ble_handle_t *out_handle) {
    if (!payload || !out_handle) {
        return -1;
    }
    *out_handle = (cl_ble_handle_t)clp_load_le32(payload, 4u);
    return *out_handle ? 0 : -3;
}

int cl_ble_parse_gatt_find_chr16_response(
    const uint8_t payload[CLP_BLE_GATT_FIND_CHR16_RESPONSE_BYTES],
    cl_ble_chr_t *out_chr) {
    if (!payload || !out_chr) {
        return -1;
    }
    uint32_t handles = clp_load_le32(payload, 4u);
    memset(out_chr, 0, sizeof(*out_chr));
    out_chr->def_handle = (uint16_t)(handles & 0xffffu);
    out_chr->value_handle = (uint16_t)(handles >> 16u);
    out_chr->properties = (uint8_t)clp_load_le32(payload + 4u, 4u);
    return out_chr->value_handle ? 0 : -3;
}

int cl_ble_parse_gatt_service_response(
    const uint8_t payload[CLP_BLE_GATT_FIND_SERVICE_RESPONSE_BYTES],
    cl_ble_service_t *out_service) {
    if (!payload || !out_service) {
        return -1;
    }
    uint32_t handles = clp_load_le32(payload, 4u);
    memset(out_service, 0, sizeof(*out_service));
    out_service->start_handle = (uint16_t)(handles & 0xffffu);
    out_service->end_handle = (uint16_t)(handles >> 16u);
    out_service->uuid16 = (uint16_t)clp_load_le32(payload + 4u, 4u);
    return out_service->start_handle ? 0 : -3;
}

int cl_ble_parse_gatt_chr_response(
    const uint8_t payload[CLP_BLE_GATT_FIND_CHR_RESPONSE_BYTES],
    cl_ble_chr_t *out_chr) {
    if (!payload || !out_chr) {
        return -1;
    }
    int ret = cl_ble_parse_gatt_find_chr16_response(payload, out_chr);
    if (ret < 0) {
        return ret;
    }
    out_chr->uuid16 = (uint16_t)clp_load_le32(payload + 8u, 4u);
    return out_chr->value_handle ? 0 : -3;
}

int cl_ble_parse_gatt_desc_response(
    const uint8_t payload[CLP_BLE_GATT_FIND_DESC_RESPONSE_BYTES],
    cl_ble_desc_t *out_desc) {
    if (!payload || !out_desc) {
        return -1;
    }
    memset(out_desc, 0, sizeof(*out_desc));
    out_desc->handle = (uint16_t)clp_load_le32(payload, 4u);
    out_desc->uuid16 = (uint16_t)clp_load_le32(payload + 4u, 4u);
    return out_desc->handle ? 0 : -3;
}

int cl_ble_parse_event_response(const uint8_t *payload,
                                uint32_t payload_length,
                                size_t max_data,
                                cl_ble_event_t *out_event) {
    if (!payload || !out_event ||
        max_data > CLP_BLE_SCAN_DATA_MAX_BYTES ||
        payload_length < CLP_BLE_EVENT_HEADER_BYTES) {
        return -1;
    }
    uint8_t data_length = payload[3];
    if (data_length > max_data ||
        payload_length != CLP_BLE_EVENT_HEADER_BYTES + data_length) {
        return -3;
    }
    memset(out_event, 0, sizeof(*out_event));
    out_event->type = payload[0];
    out_event->rssi = (int8_t)payload[1];
    out_event->addr_type = payload[2];
    out_event->data_length = data_length;
    out_event->flags = (uint16_t)clp_load_le32(payload + 10u, 2u);
    if (out_event->type == CLP_BLE_EVENT_GATT_DATA ||
        out_event->type == CLP_BLE_EVENT_GATTS_READ ||
        out_event->type == CLP_BLE_EVENT_GATTS_WRITE ||
        out_event->type == CLP_BLE_EVENT_SUBSCRIBE) {
        out_event->attr_handle = (uint16_t)clp_load_le32(payload + 4u, 2u);
    }
    memcpy(out_event->addr, payload + 4u, sizeof(out_event->addr));
    memcpy(out_event->data, payload + CLP_BLE_EVENT_HEADER_BYTES,
           out_event->data_length);
    return out_event->type;
}

int cl_ble_adv_name(const cl_ble_event_t *event, char *out, size_t out_size) {
    if (!event || !out || out_size == 0 ||
        event->data_length > CLP_BLE_SCAN_DATA_MAX_BYTES) {
        return -1;
    }
    out[0] = '\0';
    uint8_t offset = 0;
    while (offset < event->data_length) {
        uint8_t field_len = event->data[offset++];
        if (field_len == 0u) {
            break;
        }
        if (offset + field_len > event->data_length) {
            return -3;
        }
        uint8_t type = event->data[offset];
        if (type == 0x08u || type == 0x09u) {
            uint8_t name_len = (uint8_t)(field_len - 1u);
            size_t copy_len = name_len;
            if (copy_len >= out_size) {
                copy_len = out_size - 1u;
            }
            for (size_t i = 0; i < copy_len; ++i) {
                uint8_t ch = event->data[offset + 1u + i];
                out[i] = ch >= 0x20u && ch < 0x7fu ? (char)ch : '.';
            }
            out[copy_len] = '\0';
            return (int)name_len;
        }
        offset = (uint8_t)(offset + field_len);
    }
    return 0;
}

int cl_ble_init(cl_ble_t *ble,
                cl_client_t *client,
                void *scratch,
                size_t scratch_size) {
    if (!ble || !client || !scratch || scratch_size == 0) {
        return -1;
    }
    ble->client = client;
    ble->scratch = (uint8_t *)scratch;
    ble->scratch_size = scratch_size;
    ble->link_notify_attr = 0;
    return 0;
}

int cl_ble_scan_start(cl_ble_t *ble, uint16_t duration_ms) {
    cl_client_t *client = ble_client(ble);
    if (!client) {
        return -1;
    }
    uint8_t *payload = ble_scratch(ble, 4u);
    if (!payload) {
        return -1;
    }
    cl_ble_build_scan_request(payload, duration_ms);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = 4u,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_BLE, CLP_BLE_SCAN_START,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    return cl_client_status_error(response.imm);
}

int cl_ble_scan_stop(cl_ble_t *ble) {
    cl_client_t *client = ble_client(ble);
    if (!client) {
        return -1;
    }
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_BLE, CLP_BLE_SCAN_STOP,
                                     0, 0, &response);
    if (ret < 0) {
        return ret;
    }
    return cl_client_status_error(response.imm);
}

int cl_ble_connect(cl_ble_t *ble,
                   const cl_ble_addr_t *addr,
                   cl_ble_handle_t *out_handle) {
    cl_client_t *client = ble_client(ble);
    if (!client || !addr || !out_handle) {
        return -1;
    }
    uint8_t *payload = ble_scratch(ble, CLP_BLE_CONNECT_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    if (cl_ble_build_connect_request(payload, addr) < 0) {
        return -1;
    }
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_BLE_CONNECT_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_BLE, CLP_BLE_CONNECT,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_BLE_CONNECT_RESPONSE_WORDS * 4u)) {
        return discard_response_error(client, &response);
    }
    cl_wire_read_payload(client, payload, 4u);
    return cl_ble_parse_handle_response(payload, out_handle);
}

int cl_ble_disconnect(cl_ble_t *ble, cl_ble_handle_t handle) {
    cl_client_t *client = ble_client(ble);
    if (!client || !handle) {
        return -1;
    }
    uint8_t *payload = ble_scratch(ble, 4u);
    if (!payload) {
        return -1;
    }
    cl_ble_build_handle_request(payload, handle);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = 4u,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_BLE, CLP_BLE_DISCONNECT,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    return cl_client_status_error(response.imm);
}

int cl_ble_probe_device_name(cl_ble_t *ble,
                             const cl_ble_addr_t *addr,
                             char *out_name,
                             size_t out_size) {
    if (!ble || !addr || !out_name || out_size == 0) {
        return -1;
    }
    out_name[0] = '\0';

    cl_ble_handle_t handle = 0;
    int ret = cl_ble_connect(ble, addr, &handle);
    if (ret < 0) {
        return ret;
    }

    cl_ble_chr_t chr;
    ret = cl_ble_gatt_find_chr16(ble, handle, 1u, 0xffffu,
                                 CL_BLE_UUID_DEVICE_NAME, &chr);
    if (ret == 0) {
        uint8_t *raw = ble_scratch(ble, 32u);
        if (!raw) {
            ret = -1;
        } else {
            int read = cl_ble_gatt_read(ble, handle, chr.value_handle, raw,
                                        32u);
            if (read >= 0) {
                size_t copy_len = (size_t)read;
                if (copy_len >= out_size) {
                    copy_len = out_size - 1u;
                }
                for (size_t i = 0; i < copy_len; ++i) {
                    uint8_t ch = raw[i];
                    out_name[i] = ch >= 0x20u && ch < 0x7fu ? (char)ch : '.';
                }
                out_name[copy_len] = '\0';
                ret = read;
            } else {
                ret = read;
            }
        }
    }

    int close_ret = cl_ble_disconnect(ble, handle);
    if (ret < 0) {
        return ret;
    }
    if (close_ret < 0) {
        return close_ret;
    }
    return ret;
}

int cl_ble_read_chr16(cl_ble_t *ble,
                      const cl_ble_addr_t *addr,
                      uint16_t uuid16,
                      void *data, size_t capacity) {
    if (!ble || !addr || !uuid16 || (capacity && !data)) {
        return -1;
    }

    cl_ble_handle_t handle = 0;
    int ret = cl_ble_connect(ble, addr, &handle);
    if (ret < 0) {
        return ret;
    }

    cl_ble_chr_t chr;
    ret = cl_ble_gatt_find_chr16(ble, handle, 1u, 0xffffu, uuid16, &chr);
    if (ret == 0) {
        ret = cl_ble_gatt_read(ble, handle, chr.value_handle, data, capacity);
    }

    int close_ret = cl_ble_disconnect(ble, handle);
    if (ret < 0) {
        return ret;
    }
    if (close_ret < 0) {
        return close_ret;
    }
    return ret;
}

int cl_ble_write_chr16(cl_ble_t *ble,
                       const cl_ble_addr_t *addr,
                       uint16_t uuid16,
                       const void *data, size_t length) {
    if (!ble || !addr || !uuid16 || (length && !data)) {
        return -1;
    }

    cl_ble_handle_t handle = 0;
    int ret = cl_ble_connect(ble, addr, &handle);
    if (ret < 0) {
        return ret;
    }

    cl_ble_chr_t chr;
    ret = cl_ble_gatt_find_chr16(ble, handle, 1u, 0xffffu, uuid16, &chr);
    if (ret == 0) {
        ret = cl_ble_gatt_write(ble, handle, chr.value_handle, data, length);
    }

    int close_ret = cl_ble_disconnect(ble, handle);
    if (ret < 0) {
        return ret;
    }
    if (close_ret < 0) {
        return close_ret;
    }
    return ret;
}

int cl_ble_collect_notify_chr16(cl_ble_t *ble,
                                const cl_ble_addr_t *addr,
                                uint16_t uuid16,
                                cl_ble_event_t *events,
                                size_t capacity,
                                uint32_t poll_attempts) {
    if (!ble || !addr || !uuid16 || (capacity && !events)) {
        return -1;
    }

    cl_ble_handle_t handle = 0;
    int ret = cl_ble_connect(ble, addr, &handle);
    if (ret < 0) {
        return ret;
    }

    cl_ble_chr_t chr;
    ret = cl_ble_gatt_find_chr16(ble, handle, 1u, 0xffffu, uuid16, &chr);
    if (ret == 0) {
        ret = cl_ble_gatt_subscribe(ble, handle, chr.value_handle, 0xffffu,
                                    1u, 0u);
        if (ret == CL_CLIENT_STATUS_ERROR(CLP_STATUS_NOT_FOUND)) {
            ret = cl_ble_gatt_notify(ble, handle, chr.value_handle, 1u);
        }
    }

    size_t count = 0;
    if (ret == 0) {
        for (uint32_t i = 0; i < poll_attempts; ++i) {
            cl_ble_event_t event;
            int event_ret = cl_ble_event_next(ble, &event,
                                              CLP_BLE_SCAN_DATA_MAX_BYTES);
            if (event_ret == CLP_BLE_EVENT_GATT_DATA) {
                if (event.attr_handle == chr.value_handle) {
                    if (count < capacity) {
                        events[count] = event;
                        count++;
                    }
                }
                continue;
            }
            if (event_ret == CLP_BLE_EVENT_NONE ||
                event_ret == CLP_BLE_EVENT_SCAN_RESULT ||
                event_ret == CLP_BLE_EVENT_SCAN_COMPLETE ||
                event_ret == CL_CLIENT_STATUS_ERROR(CLP_STATUS_NOT_FOUND)) {
                continue;
            }
            if (event_ret < 0) {
                ret = event_ret;
                break;
            }
        }
        int disable_ret = cl_ble_gatt_subscribe(
            ble, handle, chr.value_handle, 0xffffu, 0u, 0u);
        if (disable_ret == CL_CLIENT_STATUS_ERROR(CLP_STATUS_NOT_FOUND)) {
            disable_ret = cl_ble_gatt_notify(ble, handle, chr.value_handle, 0u);
        }
        if (ret == 0 && disable_ret < 0) {
            ret = disable_ret;
        }
    }

    int close_ret = cl_ble_disconnect(ble, handle);
    if (ret < 0) {
        return ret;
    }
    if (close_ret < 0) {
        return close_ret;
    }
    return count <= (size_t)INT_MAX ? (int)count : -3;
}

int cl_ble_gatt_find_chr16(cl_ble_t *ble,
                           cl_ble_handle_t handle,
                           uint16_t start_handle,
                           uint16_t end_handle, uint16_t uuid16,
                           cl_ble_chr_t *out_chr) {
    cl_client_t *client = ble_client(ble);
    if (!client || !handle || !out_chr || !uuid16) {
        return -1;
    }
    if (!start_handle) {
        start_handle = 1u;
    }
    if (!end_handle) {
        end_handle = 0xffffu;
    }
    if (start_handle > end_handle) {
        return -1;
    }

    uint8_t *payload = ble_scratch(ble, CLP_BLE_GATT_FIND_CHR16_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    cl_ble_build_gatt_find_chr16_request(payload, handle, start_handle,
                                         end_handle, uuid16);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_BLE_GATT_FIND_CHR16_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_BLE,
                                     CLP_BLE_GATT_FIND_CHR16, &chunk, 1,
                                     &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_BLE_GATT_FIND_CHR16_RESPONSE_BYTES)) {
        return discard_response_error(client, &response);
    }

    cl_wire_read_payload(client, payload,
                         CLP_BLE_GATT_FIND_CHR16_RESPONSE_BYTES);
    return cl_ble_parse_gatt_find_chr16_response(payload, out_chr);
}

int cl_ble_gatt_find_service(cl_ble_t *ble,
                             cl_ble_handle_t handle,
                             uint16_t start_handle,
                             uint16_t end_handle,
                             uint16_t uuid16,
                             cl_ble_service_t *out_service) {
    cl_client_t *client = ble_client(ble);
    if (!client || !handle || !out_service) {
        return -1;
    }
    uint8_t *payload = ble_scratch(ble, CLP_BLE_GATT_FIND_SERVICE_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    cl_ble_build_gatt_find_request(payload, handle, start_handle, end_handle,
                                   uuid16);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_BLE_GATT_FIND_SERVICE_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_BLE,
                                     CLP_BLE_GATT_FIND_SERVICE, &chunk, 1,
                                     &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_BLE_GATT_FIND_SERVICE_RESPONSE_BYTES)) {
        return discard_response_error(client, &response);
    }
    cl_wire_read_payload(client, payload,
                         CLP_BLE_GATT_FIND_SERVICE_RESPONSE_BYTES);
    return cl_ble_parse_gatt_service_response(payload, out_service);
}

int cl_ble_gatt_find_chr(cl_ble_t *ble,
                         cl_ble_handle_t handle,
                         uint16_t start_handle,
                         uint16_t end_handle,
                         uint16_t uuid16,
                         cl_ble_chr_t *out_chr) {
    cl_client_t *client = ble_client(ble);
    if (!client || !handle || !out_chr) {
        return -1;
    }
    uint8_t *payload = ble_scratch(ble, CLP_BLE_GATT_FIND_CHR_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    cl_ble_build_gatt_find_request(payload, handle, start_handle, end_handle,
                                   uuid16);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_BLE_GATT_FIND_CHR_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_BLE,
                                     CLP_BLE_GATT_FIND_CHR, &chunk, 1,
                                     &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_BLE_GATT_FIND_CHR_RESPONSE_BYTES)) {
        return discard_response_error(client, &response);
    }
    cl_wire_read_payload(client, payload, CLP_BLE_GATT_FIND_CHR_RESPONSE_BYTES);
    return cl_ble_parse_gatt_chr_response(payload, out_chr);
}

int cl_ble_gatt_find_desc(cl_ble_t *ble,
                          cl_ble_handle_t handle,
                          uint16_t chr_value_handle,
                          uint16_t end_handle,
                          uint16_t uuid16,
                          cl_ble_desc_t *out_desc) {
    cl_client_t *client = ble_client(ble);
    if (!client || !handle || !out_desc) {
        return -1;
    }
    uint8_t *payload = ble_scratch(ble, CLP_BLE_GATT_FIND_DESC_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    cl_ble_build_gatt_find_request(payload, handle, chr_value_handle,
                                   end_handle, uuid16);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_BLE_GATT_FIND_DESC_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_BLE,
                                     CLP_BLE_GATT_FIND_DESC, &chunk, 1,
                                     &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_BLE_GATT_FIND_DESC_RESPONSE_BYTES)) {
        return discard_response_error(client, &response);
    }
    cl_wire_read_payload(client, payload,
                         CLP_BLE_GATT_FIND_DESC_RESPONSE_BYTES);
    return cl_ble_parse_gatt_desc_response(payload, out_desc);
}

int cl_ble_gatt_read(cl_ble_t *ble,
                     cl_ble_handle_t handle,
                     uint16_t attr,
                     void *data, size_t capacity) {
    cl_client_t *client = ble_client(ble);
    if (!client || !handle || (capacity && !data) || capacity > UINT32_MAX) {
        return -1;
    }
    uint32_t request_capacity = capacity > CLP_BLE_GATT_READ_FRAME_MAX_DATA ?
        CLP_BLE_GATT_READ_FRAME_MAX_DATA : (uint32_t)capacity;
    uint8_t *payload = ble_scratch(ble, CLP_BLE_GATT_READ_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    cl_ble_build_gatt_read_request(payload, handle, attr, request_capacity);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_BLE_GATT_READ_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_BLE, CLP_BLE_GATT_READ,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.imm != CLP_STATUS_OK) {
        cl_wire_discard_payload(client, response.length);
        return cl_client_status_error(response.imm);
    }
    if (response.length > request_capacity) {
        cl_wire_discard_payload(client, response.length);
        return -2;
    }
    cl_wire_read_payload(client, (uint8_t *)data, response.length);
    return response.length <= (uint32_t)INT_MAX ? (int)response.length : -3;
}

int cl_ble_gatt_write(cl_ble_t *ble,
                      cl_ble_handle_t handle,
                      uint16_t attr,
                      const void *data, size_t length) {
    cl_client_t *client = ble_client(ble);
    if (!client || !handle || (length && !data) ||
        length > CLP_BLE_GATT_WRITE_FRAME_MAX_DATA) {
        return -1;
    }
    uint8_t *header_payload = ble_scratch(ble, CLP_BLE_GATT_WRITE_DATA_OFFSET);
    if (!header_payload) {
        return -1;
    }
    cl_ble_build_gatt_write_header(header_payload, handle, attr);
    cl_wire_chunk_t chunks[2] = {
        { .data = header_payload, .length = CLP_BLE_GATT_WRITE_DATA_OFFSET },
        { .data = (const uint8_t *)data, .length = (uint32_t)length },
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_BLE, CLP_BLE_GATT_WRITE,
                                     chunks, 2, &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_BLE_RW_RESPONSE_WORDS * 4u)) {
        return discard_response_error(client, &response);
    }
    cl_wire_read_payload(client, header_payload, 4u);
    uint32_t written = clp_load_le32(header_payload, 4u);
    return written <= (uint32_t)INT_MAX ? (int)written : -3;
}

int cl_ble_gatt_notify(cl_ble_t *ble,
                       cl_ble_handle_t handle,
                       uint16_t attr,
                       uint8_t enable) {
    cl_client_t *client = ble_client(ble);
    if (!client || !handle || !attr) {
        return -1;
    }
    uint8_t *payload = ble_scratch(ble, CLP_BLE_GATT_NOTIFY_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    cl_ble_build_gatt_notify_request(payload, handle, attr, enable);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_BLE_GATT_NOTIFY_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_BLE,
                                     CLP_BLE_GATT_NOTIFY, &chunk, 1,
                                     &response);
    if (ret < 0) {
        return ret;
    }
    return cl_client_status_error(response.imm);
}

int cl_ble_gatt_subscribe(cl_ble_t *ble,
                          cl_ble_handle_t handle,
                          uint16_t value_handle,
                          uint16_t end_handle,
                          uint8_t enable,
                          uint8_t indicate) {
    if (!ble || !handle || !value_handle) {
        return -1;
    }
    if (!end_handle) {
        end_handle = 0xffffu;
    }
    if (value_handle >= end_handle) {
        return -1;
    }

    cl_ble_desc_t desc;
    int ret = cl_ble_gatt_find_desc(ble, handle, value_handle,
                                    end_handle, CL_BLE_UUID_CCCD, &desc);
    if (ret < 0) {
        return ret;
    }

    uint16_t cccd = 0u;
    if (enable) {
        cccd = indicate ? CL_BLE_CCCD_INDICATE : CL_BLE_CCCD_NOTIFY;
    }
    uint8_t payload[2] = {
        (uint8_t)(cccd & 0xffu),
        (uint8_t)(cccd >> 8u),
    };
    ret = cl_ble_gatt_write(ble, handle, desc.handle, payload,
                            sizeof(payload));
    if (ret < 0) {
        return ret;
    }
    if (ret != (int)sizeof(payload)) {
        return -3;
    }
    return cl_ble_gatt_notify(ble, handle, value_handle, enable);
}

int cl_ble_gatt_subscribe_chr16(cl_ble_t *ble,
                                cl_ble_handle_t handle,
                                uint16_t service_uuid16,
                                uint16_t chr_uuid16,
                                uint8_t enable,
                                uint8_t indicate,
                                cl_ble_chr_t *out_chr) {
    if (!ble || !handle || !chr_uuid16) {
        return -1;
    }

    uint16_t start = 1u;
    uint16_t end = 0xffffu;
    if (service_uuid16) {
        cl_ble_service_t service;
        int ret = cl_ble_gatt_find_service(ble, handle, 1u, 0xffffu,
                                           service_uuid16, &service);
        if (ret < 0) {
            return ret;
        }
        start = service.start_handle;
        end = service.end_handle;
    }

    cl_ble_chr_t chr;
    int ret = cl_ble_gatt_find_chr16(ble, handle, start, end, chr_uuid16,
                                     &chr);
    if (ret < 0) {
        return ret;
    }
    ret = cl_ble_gatt_subscribe(ble, handle, chr.value_handle, end, enable,
                                indicate);
    if (ret < 0) {
        return ret;
    }
    if (out_chr) {
        *out_chr = chr;
    }
    return 0;
}

int cl_ble_event_next(cl_ble_t *ble,
                      cl_ble_event_t *out_event,
                      size_t max_data) {
    cl_client_t *client = ble_client(ble);
    if (!client || !out_event || max_data > CLP_BLE_SCAN_DATA_MAX_BYTES) {
        return -1;
    }

    size_t scratch_required = CLP_BLE_EVENT_HEADER_BYTES + max_data;
    if (scratch_required < CLP_BLE_EVENT_NEXT_REQUEST_BYTES) {
        scratch_required = CLP_BLE_EVENT_NEXT_REQUEST_BYTES;
    }
    uint8_t *payload = ble_scratch(ble, scratch_required);
    if (!payload) {
        return -1;
    }
    clp_store_le32(payload, (uint32_t)max_data,
                   CLP_BLE_EVENT_NEXT_REQUEST_BYTES);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_BLE_EVENT_NEXT_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_BLE, CLP_BLE_EVENT_NEXT,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.imm != CLP_STATUS_OK) {
        cl_wire_discard_payload(client, response.length);
        return cl_client_status_error(response.imm);
    }
    if (response.length < CLP_BLE_EVENT_HEADER_BYTES ||
        response.length > CLP_BLE_EVENT_HEADER_BYTES + max_data) {
        cl_wire_discard_payload(client, response.length);
        return -2;
    }

    cl_wire_read_payload(client, payload, response.length);
    return cl_ble_parse_event_response(payload, response.length, max_data,
                                       out_event);
}

int cl_ble_scan_collect(cl_ble_t *ble,
                        uint16_t duration_ms,
                        cl_ble_event_t *events,
                        size_t capacity,
                        uint32_t poll_attempts) {
    if (!ble || (capacity && !events)) {
        return -1;
    }
    int ret = cl_ble_scan_start(ble, duration_ms);
    if (ret < 0) {
        return ret;
    }
    size_t count = 0;
    for (uint32_t i = 0; i < poll_attempts; ++i) {
        cl_ble_event_t event;
        ret = cl_ble_event_next(ble, &event, CLP_BLE_SCAN_DATA_MAX_BYTES);
        if (ret == CLP_BLE_EVENT_SCAN_RESULT) {
            if (count < capacity) {
                events[count] = event;
                count++;
            }
        } else if (ret == CLP_BLE_EVENT_SCAN_COMPLETE) {
            break;
        } else if (ret < 0 &&
                   ret != CL_CLIENT_STATUS_ERROR(CLP_STATUS_NOT_FOUND)) {
            (void)cl_ble_scan_stop(ble);
            return ret;
        }
    }
    ret = cl_ble_scan_stop(ble);
    if (ret < 0) {
        return ret;
    }
    return count <= (size_t)INT_MAX ? (int)count : -3;
}

/* === 128-bit UUID GATT discovery === */

int cl_ble_gatt_find_service128(cl_ble_t *ble,
                                cl_ble_handle_t handle,
                                uint16_t start_handle,
                                uint16_t end_handle,
                                const uint8_t uuid128[16],
                                cl_ble_service_t *out_service) {
    if (!ble || !uuid128 || !out_service) return -1;
    uint8_t *scratch = ble_scratch(ble, CLP_BLE_GATT_FIND_SERVICE128_REQUEST_BYTES);
    if (!scratch) return -1;
    cl_ble_build_gatt_find128_request(scratch, handle, start_handle, end_handle,
                                      uuid128);
    cl_wire_chunk_t chunk = { scratch, CLP_BLE_GATT_FIND_SERVICE128_REQUEST_BYTES };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                              CLP_BLE_GATT_FIND_SERVICE128, &chunk, 1u, &response);
    if (ret < 0) return ret;
    if (!response_ok(&response, CLP_BLE_GATT_FIND_SERVICE128_RESPONSE_BYTES))
        return discard_response_error(ble_client(ble), &response);
    cl_wire_read_payload(ble_client(ble), scratch,
                         CLP_BLE_GATT_FIND_SERVICE128_RESPONSE_BYTES);
    return cl_ble_parse_gatt_service_response(scratch, out_service);
}

int cl_ble_gatt_find_chr128(cl_ble_t *ble,
                            cl_ble_handle_t handle,
                            uint16_t start_handle,
                            uint16_t end_handle,
                            const uint8_t uuid128[16],
                            cl_ble_chr_t *out_chr) {
    if (!ble || !uuid128 || !out_chr) return -1;
    uint8_t *scratch = ble_scratch(ble, CLP_BLE_GATT_FIND_CHR128_REQUEST_BYTES);
    if (!scratch) return -1;
    cl_ble_build_gatt_find128_request(scratch, handle, start_handle, end_handle,
                                      uuid128);
    cl_wire_chunk_t chunk = { scratch, CLP_BLE_GATT_FIND_CHR128_REQUEST_BYTES };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                              CLP_BLE_GATT_FIND_CHR128, &chunk, 1u, &response);
    if (ret < 0) return ret;
    if (!response_ok(&response, CLP_BLE_GATT_FIND_CHR128_RESPONSE_BYTES))
        return discard_response_error(ble_client(ble), &response);
    cl_wire_read_payload(ble_client(ble), scratch,
                         CLP_BLE_GATT_FIND_CHR128_RESPONSE_BYTES);
    return cl_ble_parse_gatt_chr_response(scratch, out_chr);
}

int cl_ble_gatt_find_desc128(cl_ble_t *ble,
                             cl_ble_handle_t handle,
                             uint16_t chr_value_handle,
                             uint16_t end_handle,
                             const uint8_t uuid128[16],
                             cl_ble_desc_t *out_desc) {
    if (!ble || !uuid128 || !out_desc) return -1;
    uint8_t *scratch = ble_scratch(ble, CLP_BLE_GATT_FIND_DESC128_REQUEST_BYTES);
    if (!scratch) return -1;
    cl_ble_build_gatt_find128_request(scratch, handle, chr_value_handle,
                                      end_handle, uuid128);
    cl_wire_chunk_t chunk = { scratch, CLP_BLE_GATT_FIND_DESC128_REQUEST_BYTES };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                              CLP_BLE_GATT_FIND_DESC128, &chunk, 1u, &response);
    if (ret < 0) return ret;
    if (!response_ok(&response, CLP_BLE_GATT_FIND_DESC128_RESPONSE_BYTES))
        return discard_response_error(ble_client(ble), &response);
    cl_wire_read_payload(ble_client(ble), scratch,
                         CLP_BLE_GATT_FIND_DESC128_RESPONSE_BYTES);
    return cl_ble_parse_gatt_desc_response(scratch, out_desc);
}

/* === Scan with extended parameters === */

int cl_ble_scan_start_ex(cl_ble_t *ble,
                         uint16_t duration_ms,
                         uint16_t interval_us,
                         uint16_t window_us,
                         uint32_t flags) {
    if (!ble) return -1;
    uint8_t *scratch = ble_scratch(ble, CLP_BLE_SCAN_START_EXT_BYTES);
    if (!scratch) return -1;
    cl_ble_build_scan_ext_request(scratch, duration_ms, interval_us, window_us,
                                  flags);
    cl_wire_chunk_t chunk = { scratch, CLP_BLE_SCAN_START_EXT_BYTES };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                              CLP_BLE_SCAN_START, &chunk, 1u, &response);
    if (ret < 0) return ret;
    return response.imm == CLP_STATUS_OK ? 0 :
           discard_response_error(ble_client(ble), &response);
}

/* === Connection management === */

int cl_ble_conn_update(cl_ble_t *ble,
                       cl_ble_handle_t handle,
                       uint16_t interval_min,
                       uint16_t interval_max,
                       uint16_t latency,
                       uint16_t timeout) {
    if (!ble || !handle) return -1;
    uint8_t *scratch = ble_scratch(ble, CLP_BLE_CONN_UPDATE_REQUEST_BYTES);
    if (!scratch) return -1;
    cl_ble_build_conn_update_request(scratch, handle, interval_min, interval_max,
                                     latency, timeout);
    cl_wire_chunk_t chunk = { scratch, CLP_BLE_CONN_UPDATE_REQUEST_BYTES };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                              CLP_BLE_CONN_UPDATE, &chunk, 1u, &response);
    if (ret < 0) return ret;
    return response.imm == CLP_STATUS_OK ? 0 :
           discard_response_error(ble_client(ble), &response);
}

int cl_ble_exchange_mtu(cl_ble_t *ble,
                        cl_ble_handle_t handle,
                        uint16_t *out_mtu) {
    if (!ble || !handle) return -1;
    uint8_t *scratch = ble_scratch(ble, CLP_BLE_EXCHANGE_MTU_REQUEST_BYTES);
    if (!scratch) return -1;
    cl_ble_build_exchange_mtu_request(scratch, handle, 256u);
    cl_wire_chunk_t chunk = { scratch, CLP_BLE_EXCHANGE_MTU_REQUEST_BYTES };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                              CLP_BLE_EXCHANGE_MTU, &chunk, 1u, &response);
    if (ret < 0) return ret;
    if (response.imm != CLP_STATUS_OK ||
        response.length != CLP_BLE_EXCHANGE_MTU_RESPONSE_WORDS * 4u)
        return discard_response_error(ble_client(ble), &response);
    cl_wire_read_payload(ble_client(ble), scratch, 4u);
    if (out_mtu) *out_mtu = (uint16_t)cl_wire_load_le32(scratch, 4u);
    return 0;
}

int cl_ble_conn_info(cl_ble_t *ble,
                     cl_ble_handle_t handle,
                     cl_ble_conn_info_t *out_info) {
    if (!ble || !handle || !out_info) return -1;
    uint8_t *scratch = ble_scratch(ble, CLP_BLE_CONN_INFO_REQUEST_BYTES);
    if (!scratch) return -1;
    cl_ble_build_conn_info_request(scratch, handle);
    cl_wire_chunk_t chunk = { scratch, CLP_BLE_CONN_INFO_REQUEST_BYTES };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                              CLP_BLE_CONN_INFO, &chunk, 1u, &response);
    if (ret < 0) return ret;
    if (!response_ok(&response, CLP_BLE_CONN_INFO_RESPONSE_BYTES))
        return discard_response_error(ble_client(ble), &response);
    cl_wire_read_payload(ble_client(ble), scratch, CLP_BLE_CONN_INFO_RESPONSE_BYTES);
    return cl_ble_parse_conn_info_response(scratch, out_info);
}

int cl_ble_is_connected(cl_ble_t *ble, cl_ble_handle_t handle) {
    cl_ble_conn_info_t info;
    if (cl_ble_conn_info(ble, handle, &info) < 0) return 0;
    return info.connected ? 1 : 0;
}

/* === GATT long read / write === */

int cl_ble_gatt_read_long(cl_ble_t *ble,
                          cl_ble_handle_t handle,
                          uint16_t attr,
                          uint16_t offset,
                          void *data, size_t capacity) {
    if (!ble || !handle || (!data && capacity)) return -1;
    uint32_t request_capacity = capacity > CLP_BLE_GATT_READ_LONG_FRAME_MAX_DATA ?
        CLP_BLE_GATT_READ_LONG_FRAME_MAX_DATA : (uint32_t)capacity;
    if (request_capacity > 0xffffu) request_capacity = 0xffffu;
    uint8_t *scratch = ble_scratch(ble, CLP_BLE_GATT_READ_LONG_REQUEST_BYTES);
    if (!scratch) return -1;
    cl_ble_build_gatt_read_long_request(scratch, handle, attr, offset,
                                        (uint16_t)request_capacity);
    cl_wire_chunk_t chunk = { scratch, CLP_BLE_GATT_READ_LONG_REQUEST_BYTES };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                              CLP_BLE_GATT_READ_LONG, &chunk, 1u, &response);
    if (ret < 0) return ret;
    if (response.imm != CLP_STATUS_OK)
        return discard_response_error(ble_client(ble), &response);
    if (response.length > request_capacity) {
        cl_wire_discard_payload(ble_client(ble), response.length);
        return -2;
    }
    if (response.length && data)
        cl_wire_read_payload(ble_client(ble), data, response.length);
    return (int)response.length;
}

int cl_ble_gatt_write_long(cl_ble_t *ble,
                           cl_ble_handle_t handle,
                           uint16_t attr,
                           uint16_t offset,
                           const void *data, size_t length) {
    if (!ble || !handle || (!data && length)) return -1;
    if (length > CLP_BLE_GATT_WRITE_LONG_FRAME_MAX_DATA)
        return -1;
    uint8_t *scratch = ble_scratch(ble, CLP_BLE_GATT_WRITE_LONG_DATA_OFFSET);
    if (!scratch) return -1;
    cl_ble_build_gatt_write_long_header(scratch, handle, attr, offset);
    cl_wire_chunk_t chunks[2] = {
        { scratch, CLP_BLE_GATT_WRITE_LONG_DATA_OFFSET },
        { (const uint8_t *)data, (uint32_t)length },
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                              CLP_BLE_GATT_WRITE_LONG, chunks, 2u, &response);
    if (ret < 0) return ret;
    if (response.imm != CLP_STATUS_OK ||
        response.length != CLP_BLE_RW_RESPONSE_WORDS * 4u)
        return discard_response_error(ble_client(ble), &response);
    cl_wire_read_payload(ble_client(ble), scratch, 4u);
    return (int)cl_wire_load_le32(scratch, 4u);
}

/* === Pairing === */

int cl_ble_security_config(cl_ble_t *ble,
                           const cl_ble_security_config_t *config) {
    if (!ble || !config) return -1;
    if (config->io_caps > CL_BLE_IO_CAPS_NO_IO ||
        config->security_level > 4u ||
        (config->flags & ~(CL_BLE_SECURITY_BOND |
                           CL_BLE_SECURITY_MITM |
                           CL_BLE_SECURITY_SC |
                           CL_BLE_SECURITY_SC_ONLY)) ||
        (config->our_key_dist & ~(CL_BLE_SECURITY_KEY_DIST_ENC |
                                  CL_BLE_SECURITY_KEY_DIST_ID)) ||
        (config->their_key_dist & ~(CL_BLE_SECURITY_KEY_DIST_ENC |
                                    CL_BLE_SECURITY_KEY_DIST_ID))) {
        return -1;
    }
    uint8_t *scratch = ble_scratch(ble, CLP_BLE_SECURITY_CONFIG_REQUEST_BYTES);
    if (!scratch) return -1;
    cl_ble_build_security_config_request(scratch, config);
    cl_wire_chunk_t chunk = { scratch, CLP_BLE_SECURITY_CONFIG_REQUEST_BYTES };
    return ble_expect_ack(ble, CLP_BLE_SECURITY_CONFIG, &chunk, 1u);
}

int cl_ble_pair(cl_ble_t *ble,
                cl_ble_handle_t handle,
                uint8_t io_caps,
                uint8_t bond) {
    if (!ble || !handle) return -1;
    if (io_caps > CL_BLE_IO_CAPS_NO_IO) return -1;
    uint8_t *scratch = ble_scratch(ble, CLP_BLE_PAIR_REQUEST_BYTES);
    if (!scratch) return -1;
    cl_ble_build_pair_request(scratch, handle, io_caps, bond);
    cl_wire_chunk_t chunk = { scratch, CLP_BLE_PAIR_REQUEST_BYTES };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                              CLP_BLE_PAIR, &chunk, 1u, &response);
    if (ret < 0) return ret;
    return response.imm == CLP_STATUS_OK ? 0 :
           discard_response_error(ble_client(ble), &response);
}

int cl_ble_passkey_reply(cl_ble_t *ble,
                         cl_ble_handle_t handle,
                         uint32_t passkey) {
    if (!ble || !handle) return -1;
    uint8_t *scratch = ble_scratch(ble, CLP_BLE_PASSKEY_REPLY_REQUEST_BYTES);
    if (!scratch) return -1;
    cl_ble_build_passkey_reply_request(scratch, handle, passkey);
    cl_wire_chunk_t chunk = { scratch, CLP_BLE_PASSKEY_REPLY_REQUEST_BYTES };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                              CLP_BLE_PASSKEY_REPLY, &chunk, 1u, &response);
    if (ret < 0) return ret;
    return response.imm == CLP_STATUS_OK ? 0 :
           discard_response_error(ble_client(ble), &response);
}

/* === GATT Server / Peripheral === */

static int ble_expect_ack(cl_ble_t *ble, uint8_t opcode,
                          const cl_wire_chunk_t *chunks, size_t chunk_count) {
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE, opcode,
                                     chunks, chunk_count, &response);
    if (ret < 0) return ret;
    return response.imm == CLP_STATUS_OK ? 0 :
           discard_response_error(ble_client(ble), &response);
}

static int ble_read_id_response(cl_ble_t *ble,
                                const clp_header_t *response,
                                cl_ble_gatts_id_t *out_id) {
    if (!response_ok(response, CLP_BLE_GATTS_RESPONSE_WORDS * 4u)) {
        return discard_response_error(ble_client(ble), response);
    }
    uint8_t scratch[4];
    cl_wire_read_payload(ble_client(ble), scratch, 4u);
    if (out_id) {
        *out_id = (cl_ble_gatts_id_t)clp_load_le32(scratch, 4u);
    }
    return 0;
}

static int ble_gatts_add_service(cl_ble_t *ble,
                                 uint32_t flags,
                                 uint16_t uuid16,
                                 const uint8_t uuid128[16],
                                 cl_ble_gatts_id_t *out_service) {
    if (!ble || !out_service) return -1;
    if ((flags & CLP_BLE_GATTS_UUID128) && !uuid128) return -1;
    uint32_t length = CLP_BLE_GATTS_ADD_SERVICE_HEADER_BYTES +
        ((flags & CLP_BLE_GATTS_UUID128) ? 16u : 0u);
    uint8_t *payload = ble_scratch(ble, length);
    if (!payload) return -1;
    clp_store_le32(payload, flags, 4u);
    clp_store_le32(payload + 4u, uuid16, 2u);
    if (flags & CLP_BLE_GATTS_UUID128) {
        memcpy(payload + 8u, uuid128, 16u);
    }
    cl_wire_chunk_t chunk = { payload, length };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                                     CLP_BLE_GATTS_ADD_SERVICE,
                                     &chunk, 1u, &response);
    if (ret < 0) return ret;
    return ble_read_id_response(ble, &response, out_service);
}

int cl_ble_gatts_reset(cl_ble_t *ble) {
    if (!ble_client(ble)) return -1;
    return ble_expect_ack(ble, CLP_BLE_GATTS_RESET, NULL, 0u);
}

int cl_ble_gatts_add_service16(cl_ble_t *ble,
                               uint16_t uuid16,
                               uint32_t flags,
                               cl_ble_gatts_id_t *out_service) {
    return ble_gatts_add_service(ble,
                                 (flags & ~CLP_BLE_GATTS_UUID128) |
                                     CLP_BLE_GATTS_PRIMARY,
                                 uuid16, NULL, out_service);
}

int cl_ble_gatts_add_service128(cl_ble_t *ble,
                                const uint8_t uuid128[16],
                                uint32_t flags,
                                cl_ble_gatts_id_t *out_service) {
    return ble_gatts_add_service(ble,
                                 flags | CLP_BLE_GATTS_UUID128 |
                                     CLP_BLE_GATTS_PRIMARY,
                                 0u, uuid128, out_service);
}

int cl_ble_gatts_add_include(cl_ble_t *ble,
                             cl_ble_gatts_id_t service,
                             cl_ble_gatts_id_t included_service) {
    if (!ble_client(ble) || !service || !included_service) {
        return -1;
    }
    uint8_t *payload = ble_scratch(ble, CLP_BLE_GATTS_ADD_INCLUDE_BYTES);
    if (!payload) return -1;
    clp_store_le32(payload, service, 2u);
    clp_store_le32(payload + 2u, included_service, 2u);
    cl_wire_chunk_t chunk = { payload, CLP_BLE_GATTS_ADD_INCLUDE_BYTES };
    return ble_expect_ack(ble, CLP_BLE_GATTS_ADD_INCLUDE, &chunk, 1u);
}

static int ble_gatts_add_attr(cl_ble_t *ble,
                              uint8_t opcode,
                              cl_ble_gatts_id_t parent,
                              uint32_t flags,
                              uint16_t uuid16,
                              const uint8_t uuid128[16],
                              const void *value,
                              size_t value_length,
                              cl_ble_gatts_id_t *out_id) {
    if (!ble || !out_id || value_length > CLP_BLE_GATTS_VALUE_MAX_BYTES ||
        (value_length && !value)) {
        return -1;
    }
    if ((flags & CLP_BLE_GATTS_UUID128) && !uuid128) return -1;
    uint8_t *header = ble_scratch(ble, CLP_BLE_GATTS_ADD_ATTR_HEADER_BYTES);
    if (!header) return -1;
    clp_store_le32(header, parent, 2u);
    clp_store_le32(header + 4u, flags, 4u);
    clp_store_le32(header + 8u, uuid16, 2u);
    clp_store_le32(header + 12u, (uint32_t)value_length, 4u);
    cl_wire_chunk_t chunks[3];
    size_t count = 0;
    chunks[count++] = (cl_wire_chunk_t){ header,
                                         CLP_BLE_GATTS_ADD_ATTR_HEADER_BYTES };
    if (flags & CLP_BLE_GATTS_UUID128) {
        chunks[count++] = (cl_wire_chunk_t){ uuid128, 16u };
    }
    if (value_length) {
        chunks[count++] = (cl_wire_chunk_t){ value, (uint32_t)value_length };
    }
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE, opcode,
                                     chunks, count, &response);
    if (ret < 0) return ret;
    return ble_read_id_response(ble, &response, out_id);
}

int cl_ble_gatts_add_chr16(cl_ble_t *ble,
                           cl_ble_gatts_id_t service,
                           uint16_t uuid16,
                           uint32_t flags,
                           const void *value,
                           size_t value_length,
                           cl_ble_gatts_id_t *out_chr) {
    return ble_gatts_add_attr(ble, CLP_BLE_GATTS_ADD_CHR, service,
                              flags & ~CLP_BLE_GATTS_UUID128,
                              uuid16, NULL, value, value_length, out_chr);
}

int cl_ble_gatts_add_chr128(cl_ble_t *ble,
                            cl_ble_gatts_id_t service,
                            const uint8_t uuid128[16],
                            uint32_t flags,
                            const void *value,
                            size_t value_length,
                            cl_ble_gatts_id_t *out_chr) {
    return ble_gatts_add_attr(ble, CLP_BLE_GATTS_ADD_CHR, service,
                              flags | CLP_BLE_GATTS_UUID128,
                              0u, uuid128, value, value_length, out_chr);
}

int cl_ble_gatts_add_desc16(cl_ble_t *ble,
                            cl_ble_gatts_id_t chr,
                            uint16_t uuid16,
                            uint32_t flags,
                            const void *value,
                            size_t value_length,
                            cl_ble_gatts_id_t *out_desc) {
    return ble_gatts_add_attr(ble, CLP_BLE_GATTS_ADD_DESC, chr,
                              flags & ~CLP_BLE_GATTS_UUID128,
                              uuid16, NULL, value, value_length, out_desc);
}

int cl_ble_gatts_add_desc128(cl_ble_t *ble,
                             cl_ble_gatts_id_t chr,
                             const uint8_t uuid128[16],
                             uint32_t flags,
                             const void *value,
                             size_t value_length,
                             cl_ble_gatts_id_t *out_desc) {
    return ble_gatts_add_attr(ble, CLP_BLE_GATTS_ADD_DESC, chr,
                              flags | CLP_BLE_GATTS_UUID128,
                              0u, uuid128, value, value_length, out_desc);
}

int cl_ble_gatts_start(cl_ble_t *ble) {
    if (!ble_client(ble)) return -1;
    return ble_expect_ack(ble, CLP_BLE_GATTS_START, NULL, 0u);
}

int cl_ble_gatts_stop(cl_ble_t *ble) {
    if (!ble_client(ble)) return -1;
    return ble_expect_ack(ble, CLP_BLE_GATTS_STOP, NULL, 0u);
}

int cl_ble_adv_start(cl_ble_t *ble, const cl_ble_adv_config_t *config) {
    if (!ble_client(ble) || !config ||
        config->uuid16_count > 4u || config->uuid128_count > 2u) {
        return -1;
    }
    size_t name_len = 0;
    if (config->name) {
        while (config->name[name_len] &&
               name_len <= CLP_BLE_ADV_NAME_MAX_BYTES) {
            name_len++;
        }
        if (name_len > CLP_BLE_ADV_NAME_MAX_BYTES) return -1;
    }
    size_t length = CLP_BLE_ADV_START_HEADER_BYTES +
        (size_t)config->uuid16_count * 2u +
        (size_t)config->uuid128_count * 16u + name_len;
    uint8_t *payload = ble_scratch(ble, length);
    if (!payload) return -1;
    clp_store_le32(payload, config->duration_ms, 4u);
    clp_store_le32(payload + 4u, config->flags, 4u);
    clp_store_le32(payload + 8u, config->appearance, 2u);
    payload[10] = config->uuid16_count;
    payload[11] = config->uuid128_count;
    size_t offset = CLP_BLE_ADV_START_HEADER_BYTES;
    for (uint8_t i = 0; i < config->uuid16_count; ++i) {
        clp_store_le32(payload + offset, config->uuid16[i], 2u);
        offset += 2u;
    }
    for (uint8_t i = 0; i < config->uuid128_count; ++i) {
        memcpy(payload + offset, config->uuid128[i], 16u);
        offset += 16u;
    }
    if (name_len) memcpy(payload + offset, config->name, name_len);
    cl_wire_chunk_t chunk = { payload, (uint32_t)length };
    return ble_expect_ack(ble, CLP_BLE_ADV_START, &chunk, 1u);
}

int cl_ble_adv_stop(cl_ble_t *ble) {
    if (!ble_client(ble)) return -1;
    return ble_expect_ack(ble, CLP_BLE_ADV_STOP, NULL, 0u);
}

int cl_ble_gatts_notify(cl_ble_t *ble,
                        cl_ble_handle_t handle,
                        cl_ble_gatts_id_t chr,
                        const void *data,
                        size_t length) {
    if (!ble_client(ble) || length > CLP_BLE_GATTS_FRAME_MAX_DATA ||
        (length && !data)) {
        return -1;
    }
    uint8_t *header = ble_scratch(ble, CLP_BLE_GATTS_NOTIFY_DATA_OFFSET);
    if (!header) return -1;
    clp_store_le32(header, handle, 2u);
    clp_store_le32(header + 4u, chr, 2u);
    cl_wire_chunk_t chunks[2] = {
        { header, CLP_BLE_GATTS_NOTIFY_DATA_OFFSET },
        { data, (uint32_t)length },
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                                     CLP_BLE_GATTS_NOTIFY,
                                     chunks, length ? 2u : 1u, &response);
    if (ret < 0) return ret;
    if (!response_ok(&response, CLP_BLE_RW_RESPONSE_WORDS * 4u)) {
        return discard_response_error(ble_client(ble), &response);
    }
    uint8_t scratch[4];
    cl_wire_read_payload(ble_client(ble), scratch, 4u);
    return (int)clp_load_le32(scratch, 4u);
}

int cl_ble_gatts_set_value(cl_ble_t *ble,
                           cl_ble_gatts_id_t attr,
                           const void *data,
                           size_t length) {
    if (!ble_client(ble) || length > CLP_BLE_GATTS_VALUE_MAX_BYTES ||
        (length && !data)) {
        return -1;
    }
    uint8_t *header = ble_scratch(ble, CLP_BLE_GATTS_SET_VALUE_DATA_OFFSET);
    if (!header) return -1;
    clp_store_le32(header, attr, 2u);
    clp_store_le32(header + 4u, (uint32_t)length, 4u);
    cl_wire_chunk_t chunks[2] = {
        { header, CLP_BLE_GATTS_SET_VALUE_DATA_OFFSET },
        { data, (uint32_t)length },
    };
    return ble_expect_ack(ble, CLP_BLE_GATTS_SET_VALUE,
                          chunks, length ? 2u : 1u);
}

int cl_ble_gatts_get_value(cl_ble_t *ble,
                           cl_ble_gatts_id_t attr,
                           uint16_t offset,
                           void *data,
                           size_t capacity) {
    if (!ble_client(ble) || (!data && capacity)) {
        return -1;
    }
    if (capacity > CLP_BLE_GATTS_VALUE_MAX_BYTES) {
        capacity = CLP_BLE_GATTS_VALUE_MAX_BYTES;
    }
    uint8_t *payload = ble_scratch(ble, CLP_BLE_GATTS_GET_VALUE_REQUEST_BYTES);
    if (!payload) return -1;
    cl_ble_build_gatts_get_value_request(payload, attr, offset,
                                         (uint32_t)capacity);
    cl_wire_chunk_t chunk = { payload, CLP_BLE_GATTS_GET_VALUE_REQUEST_BYTES };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                                     CLP_BLE_GATTS_GET_VALUE,
                                     &chunk, 1u, &response);
    if (ret < 0) return ret;
    if (response.imm != CLP_STATUS_OK) {
        return discard_response_error(ble_client(ble), &response);
    }
    if (response.length > capacity) {
        cl_wire_discard_payload(ble_client(ble), response.length);
        return -2;
    }
    if (response.length && data) {
        cl_wire_read_payload(ble_client(ble), data, response.length);
    }
    return (int)response.length;
}

int cl_ble_gatts_subscribed(cl_ble_t *ble,
                            cl_ble_handle_t handle,
                            cl_ble_gatts_id_t chr) {
    if (!ble_client(ble) || !handle || !chr) {
        return -1;
    }
    uint8_t *payload =
        ble_scratch(ble, CLP_BLE_GATTS_SUBSCRIBED_REQUEST_BYTES);
    if (!payload) return -1;
    cl_ble_build_gatts_subscribed_request(payload, handle, chr);
    cl_wire_chunk_t chunk = {
        payload,
        CLP_BLE_GATTS_SUBSCRIBED_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(ble_client(ble), CLP_CH_BLE,
                                     CLP_BLE_GATTS_SUBSCRIBED,
                                     &chunk, 1u, &response);
    if (ret < 0) return ret;
    if (!response_ok(&response, CLP_BLE_GATTS_RESPONSE_WORDS * 4u)) {
        return discard_response_error(ble_client(ble), &response);
    }
    uint8_t scratch[4];
    cl_wire_read_payload(ble_client(ble), scratch, 4u);
    return (clp_load_le32(scratch, 4u) &
            (CLP_BLE_GATTS_SUBSCRIBED_NOTIFY |
             CLP_BLE_GATTS_SUBSCRIBED_INDICATE)) ? 1 : 0;
}

/* === ChisLink-to-ChisLink Game Link === */

int cl_ble_link_start(cl_ble_t *ble, uint8_t role, const char *name) {
    if (!ble || !name || !name[0]) return -1;
    if (role > CL_BLE_LINK_ROLE_JOIN) return -1;
    uint32_t name_len = 0;
    while (name[name_len] && name_len < CL_BLE_LINK_NAME_MAX) ++name_len;
    if (name_len == 0 || name_len > CL_BLE_LINK_NAME_MAX) return -1;

    ble->link_role = role;
    ble->link_notify_attr = 0;
    ble->link_rx_attr = 0;
    ble->link_tx_chr = 0;

    if (role == CL_BLE_LINK_ROLE_HOST) {
        cl_ble_gatts_id_t svc = 0;
        cl_ble_gatts_id_t rx = 0;
        cl_ble_gatts_id_t tx = 0;
        int ret = cl_ble_gatts_reset(ble);
        if (ret < 0) return ret;
        ret = cl_ble_gatts_add_service128(ble, CL_BLE_LINK_SERVICE_UUID128,
                                          CLP_BLE_GATTS_PRIMARY, &svc);
        if (ret < 0) return ret;
        ret = cl_ble_gatts_add_chr128(ble, svc, CL_BLE_LINK_RX_UUID128,
                                      CLP_BLE_GATTS_CHR_WRITE |
                                      CLP_BLE_GATTS_CHR_WRITE_NO_RSP,
                                      NULL, 0u, &rx);
        if (ret < 0) return ret;
        ret = cl_ble_gatts_add_chr128(ble, svc, CL_BLE_LINK_TX_UUID128,
                                      CLP_BLE_GATTS_CHR_NOTIFY |
                                      CLP_BLE_GATTS_CHR_READ,
                                      NULL, 0u, &tx);
        if (ret < 0) return ret;
        ret = cl_ble_gatts_start(ble);
        if (ret < 0) return ret;
        ble->link_tx_chr = tx;
        cl_ble_adv_config_t adv;
        memset(&adv, 0, sizeof(adv));
        adv.uuid128 = (const uint8_t (*)[16])CL_BLE_LINK_SERVICE_UUID128;
        adv.uuid128_count = 1u;
        adv.name = name;
        ret = cl_ble_adv_start(ble, &adv);
        if (ret < 0) return ret;
        return 0;  /* HOST returns 0 — GBA polls events for data */
    }

    /* JOIN role: scan for link service, connect, subscribe */
    /* Scan briefly for a device */
    int ret = cl_ble_scan_start(ble, 3000u);
    if (ret < 0) return ret;
    cl_ble_addr_t peer_addr;
    memset(&peer_addr, 0, sizeof(peer_addr));
    int found = 0;
    for (uint32_t i = 0; i < 600u; ++i) {
        cl_ble_event_t ev;
        int eret = cl_ble_event_next(ble, &ev, sizeof(ev.data));
        if (eret < 0) break;
        if (ev.type == CLP_BLE_EVENT_SCAN_RESULT) {
            if ((ev.flags & CLP_BLE_EVENT_FLAG_CONNECTABLE) &&
                ble_adv_has_uuid128(&ev, CL_BLE_LINK_SERVICE_UUID128)) {
                memcpy(peer_addr.bytes, ev.addr, 6);
                peer_addr.type = ev.addr_type;
                found = 1;
                break;
            }
        }
        if (ev.type == CLP_BLE_EVENT_SCAN_COMPLETE) break;
    }
    cl_ble_scan_stop(ble);
    if (!found) return -3;

    /* Connect */
    cl_ble_handle_t handle = 0;
    ret = cl_ble_connect(ble, &peer_addr, &handle);
    if (ret < 0) return ret;

    /* Discover Link RX/TX characteristics and subscribe to TX */
    {
        cl_ble_chr_t chr;
        ret = cl_ble_gatt_find_chr128(ble, handle, 1u, 0xffffu,
                                      CL_BLE_LINK_RX_UUID128, &chr);
        if (ret < 0) { cl_ble_disconnect(ble, handle); return ret; }
        ble->link_rx_attr = chr.value_handle;
        ret = cl_ble_gatt_find_chr128(ble, handle, 1u, 0xffffu,
                                      CL_BLE_LINK_TX_UUID128, &chr);
        if (ret < 0) { cl_ble_disconnect(ble, handle); return ret; }
        ret = cl_ble_gatt_subscribe(ble, handle, chr.value_handle,
                                    0xffffu, 1u, 0u);
        if (ret < 0) { cl_ble_disconnect(ble, handle); return ret; }
        ble->link_notify_attr = chr.value_handle;
    }
    return (int)handle;
}

int cl_ble_link_stop(cl_ble_t *ble, cl_ble_handle_t handle) {
    if (!ble) return -1;
    ble->link_notify_attr = 0;
    ble->link_rx_attr = 0;
    ble->link_tx_chr = 0;
    if (handle) cl_ble_disconnect(ble, handle);
    (void)cl_ble_adv_stop(ble);
    (void)cl_ble_gatts_stop(ble);
    return 0;
}

int cl_ble_link_send(cl_ble_t *ble, cl_ble_handle_t handle,
                     const void *data, uint32_t length) {
    if (!ble || !handle || (!data && length)) return -1;
    if (ble->link_role == CL_BLE_LINK_ROLE_HOST && ble->link_tx_chr) {
        return cl_ble_gatts_notify(ble, handle, ble->link_tx_chr,
                                   data, length);
    }
    if (ble->link_rx_attr) {
        return cl_ble_gatt_write(ble, handle, ble->link_rx_attr,
                                 data, length);
    }
    return -1;
}

int cl_ble_link_peer_ready(cl_ble_t *ble, cl_ble_handle_t handle) {
    /* Poll for SUBSCRIBE events indicating peer is ready */
    if (!ble) return -1;
    cl_ble_event_t ev;
    int ret = cl_ble_event_next(ble, &ev, sizeof(ev.data));
    if (ret < 0) return ret;
    if (ret == CLP_BLE_EVENT_SUBSCRIBE) {
        if (ble->link_tx_chr && ev.attr_handle != ble->link_tx_chr) {
            return 0;
        }
        if (!ev.data[0]) return 0;
        if (ev.data_length >= 3u) {
            uint16_t peer_handle =
                (uint16_t)ev.data[1] | ((uint16_t)ev.data[2] << 8u);
            if (peer_handle) return (int)peer_handle;
        }
        return handle ? (int)handle : 1;
    }
    if (ret == 0) return 0;  /* no event yet */
    /* Drain other events, not ready */
    return 0;
}

/* === Low-level builders / parsers for new features === */

void cl_ble_build_scan_ext_request(uint8_t payload[CLP_BLE_SCAN_START_EXT_BYTES],
                                   uint16_t duration_ms,
                                   uint16_t interval_us,
                                   uint16_t window_us,
                                   uint32_t flags) {
    cl_wire_store_le32(payload, duration_ms, 4u);
    cl_wire_store_le32(payload + 4u, interval_us, 4u);
    cl_wire_store_le32(payload + 8u, window_us, 4u);
    cl_wire_store_le32(payload + 12u, flags, 4u);
}

void cl_ble_build_gatt_find128_request(
    uint8_t payload[CLP_BLE_GATT_FIND_SERVICE128_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t start_handle,
    uint16_t end_handle,
    const uint8_t uuid128[16]) {
    cl_wire_store_le32(payload, handle, 4u);
    uint16_t sh = start_handle ? start_handle : 1u;
    uint16_t eh = end_handle ? end_handle : 0xffffu;
    cl_wire_store_le32(payload + 4u, (uint32_t)sh | ((uint32_t)eh << 16u), 4u);
    memcpy(payload + 8u, uuid128, 16u);
}

void cl_ble_build_conn_update_request(
    uint8_t payload[CLP_BLE_CONN_UPDATE_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t interval_min,
    uint16_t interval_max,
    uint16_t latency,
    uint16_t timeout) {
    cl_wire_store_le32(payload, handle, 4u);
    cl_wire_store_le32(payload + 4u,
                       (uint32_t)interval_min | ((uint32_t)interval_max << 16u), 4u);
    cl_wire_store_le32(payload + 8u,
                       (uint32_t)latency | ((uint32_t)timeout << 16u), 4u);
}

void cl_ble_build_exchange_mtu_request(
    uint8_t payload[CLP_BLE_EXCHANGE_MTU_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t mtu) {
    cl_wire_store_le32(payload, handle, 4u);
    cl_wire_store_le32(payload + 4u, mtu, 2u);
}

void cl_ble_build_gatt_read_long_request(
    uint8_t payload[CLP_BLE_GATT_READ_LONG_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t attr,
    uint16_t offset,
    uint16_t max_len) {
    cl_wire_store_le32(payload, handle, 4u);
    cl_wire_store_le32(payload + 4u,
                       (uint32_t)attr | ((uint32_t)offset << 16u), 4u);
    cl_wire_store_le32(payload + 8u, max_len, 4u);
}

void cl_ble_build_gatt_write_long_header(
    uint8_t payload[CLP_BLE_GATT_WRITE_LONG_DATA_OFFSET],
    cl_ble_handle_t handle,
    uint16_t attr,
    uint16_t offset) {
    cl_wire_store_le32(payload, handle, 4u);
    cl_wire_store_le32(payload + 4u,
                       (uint32_t)attr | ((uint32_t)offset << 16u), 4u);
}

void cl_ble_build_pair_request(
    uint8_t payload[CLP_BLE_PAIR_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint8_t io_caps,
    uint8_t bond) {
    cl_wire_store_le32(payload, handle, 4u);
    payload[4] = io_caps;
    payload[5] = bond;
}

void cl_ble_build_security_config_request(
    uint8_t payload[CLP_BLE_SECURITY_CONFIG_REQUEST_BYTES],
    const cl_ble_security_config_t *config) {
    memset(payload, 0, CLP_BLE_SECURITY_CONFIG_REQUEST_BYTES);
    if (!config) {
        return;
    }
    payload[0] = config->io_caps;
    payload[1] = config->flags;
    payload[2] = config->our_key_dist;
    payload[3] = config->their_key_dist;
    payload[4] = config->security_level;
}

void cl_ble_build_passkey_reply_request(
    uint8_t payload[CLP_BLE_PASSKEY_REPLY_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint32_t passkey) {
    cl_wire_store_le32(payload, handle, 4u);
    cl_wire_store_le32(payload + 4u, passkey, 4u);
}

void cl_ble_build_conn_info_request(
    uint8_t payload[CLP_BLE_CONN_INFO_REQUEST_BYTES],
    cl_ble_handle_t handle) {
    cl_wire_store_le32(payload, handle, 4u);
}

void cl_ble_build_gatts_get_value_request(
    uint8_t payload[CLP_BLE_GATTS_GET_VALUE_REQUEST_BYTES],
    cl_ble_gatts_id_t attr,
    uint16_t offset,
    uint32_t capacity) {
    cl_wire_store_le32(payload, attr, 2u);
    cl_wire_store_le32(payload + 2u, offset, 2u);
    cl_wire_store_le32(payload + 4u, capacity, 4u);
}

void cl_ble_build_gatts_subscribed_request(
    uint8_t payload[CLP_BLE_GATTS_SUBSCRIBED_REQUEST_BYTES],
    cl_ble_handle_t handle,
    cl_ble_gatts_id_t chr) {
    cl_wire_store_le32(payload, handle, 2u);
    cl_wire_store_le32(payload + 4u, chr, 2u);
}

int cl_ble_parse_conn_info_response(
    const uint8_t payload[CLP_BLE_CONN_INFO_RESPONSE_BYTES],
    cl_ble_conn_info_t *out_info) {
    if (!payload || !out_info) return -1;
    out_info->flags = cl_wire_load_le32(payload, 4u);
    out_info->connected =
        (out_info->flags & CLP_BLE_CONN_FLAG_CONNECTED) ? 1u : 0u;
    out_info->encrypted =
        (out_info->flags & CLP_BLE_CONN_FLAG_ENCRYPTED) ? 1u : 0u;
    out_info->authenticated =
        (out_info->flags & CLP_BLE_CONN_FLAG_AUTHENTICATED) ? 1u : 0u;
    out_info->bonded =
        (out_info->flags & CLP_BLE_CONN_FLAG_BONDED) ? 1u : 0u;
    out_info->mtu = (uint16_t)cl_wire_load_le32(payload + 4u, 4u);
    out_info->interval_us = cl_wire_load_le32(payload + 8u, 4u);
    out_info->latency = cl_wire_load_le32(payload + 12u, 4u);
    out_info->timeout_us = cl_wire_load_le32(payload + 16u, 4u);
    out_info->rssi = (int8_t)(int32_t)cl_wire_load_le32(payload + 20u, 4u);
    out_info->addr_type = payload[24];
    memcpy(out_info->addr, payload + 25u, 6u);
    return 0;
}

int cl_ble_parse_exchange_mtu_response(
    const uint8_t payload[4],
    uint16_t *out_mtu) {
    if (!payload) return -1;
    if (out_mtu) *out_mtu = (uint16_t)cl_wire_load_le32(payload, 4u);
    return 0;
}

int cl_ble_parse_conn_update_response(const uint8_t payload[4]) {
    (void)payload;
    return 0;
}
