#include "chislink/net.h"

#include "chislink/wire.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static cl_client_t *net_client(cl_net_t *net) {
    return net ? net->client : NULL;
}

static uint8_t *net_scratch(cl_net_t *net, size_t required) {
    if (!net || !net->scratch || net->scratch_size < required) {
        return NULL;
    }
    return net->scratch;
}

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

void cl_net_build_status_request(uint8_t payload[CLP_NET_STATUS_REQUEST_BYTES],
                                 uint32_t flags) {
    clp_store_le32(payload, flags, CLP_NET_STATUS_REQUEST_BYTES);
}

int cl_net_parse_status_response(const uint8_t payload[CLP_NET_STATUS_RESPONSE_BYTES],
                                 cl_net_status_t *out_status) {
    if (!payload || !out_status) {
        return -1;
    }
    out_status->flags = clp_load_le32(payload, 4u);
    out_status->source = clp_load_le32(payload + 4u, 4u);
    out_status->ip = clp_load_le32(payload + 8u, 4u);
    out_status->gateway = clp_load_le32(payload + 12u, 4u);
    out_status->rssi = (int32_t)clp_load_le32(payload + 16u, 4u);
    out_status->tx_power_qdbm = (int32_t)clp_load_le32(payload + 20u, 4u);
    out_status->power_adjust_qdbm = (int32_t)clp_load_le32(payload + 24u, 4u);
    out_status->connect_attempts = clp_load_le32(payload + 28u, 4u);
    out_status->open_sockets = clp_load_le32(payload + 32u, 4u);
    out_status->max_sockets = clp_load_le32(payload + 36u, 4u);
    memcpy(out_status->ssid, payload + 40u, sizeof(out_status->ssid));
    out_status->ssid[sizeof(out_status->ssid) - 1u] = '\0';
    out_status->last_error = clp_load_le32(payload + 56u, 4u);
    return 0;
}

int cl_net_parse_config_response(const uint8_t payload[CLP_NET_CONFIG_BYTES],
                                 cl_net_config_t *out_config) {
    if (!payload || !out_config) {
        return -1;
    }
    memset(out_config, 0, sizeof(*out_config));
    out_config->flags = clp_load_le32(payload, 4u);
    out_config->power_adjust_qdbm =
        (int32_t)clp_load_le32(payload + 4u, 4u);
    out_config->source = clp_load_le32(payload + 8u, 4u);
    memcpy(out_config->ssid, payload + 12u, sizeof(out_config->ssid));
    out_config->ssid[sizeof(out_config->ssid) - 1u] = '\0';
    return 0;
}

int cl_net_build_config_request(uint8_t payload[CLP_NET_CONFIG_BYTES],
                                const cl_net_config_t *config,
                                const char *password) {
    if (!payload || !config) {
        return -1;
    }
    if (config->source != CLP_NET_SOURCE_SD &&
        config->source != CLP_NET_SOURCE_NONE) {
        return -1;
    }
    memset(payload, 0, CLP_NET_CONFIG_BYTES);
    uint32_t flags = config->flags &
        ~(CLP_NET_CONFIG_FLAG_PASSWORD_PRESENT |
          CLP_NET_CONFIG_FLAG_STORED_PASSWORD |
          CLP_NET_CONFIG_FLAG_EFFECTIVE_SSID);
    if (config->ssid[0]) {
        flags |= CLP_NET_CONFIG_FLAG_HAS_SSID;
        flags &= ~CLP_NET_CONFIG_FLAG_CLEAR_CREDENTIALS;
    }
    if (password) {
        size_t password_length = strlen(password);
        if (password_length >= CLP_NET_CONFIG_PASSWORD_BYTES) {
            return -1;
        }
        flags |= CLP_NET_CONFIG_FLAG_PASSWORD_PRESENT;
        memcpy(payload + 12u + CLP_NET_CONFIG_SSID_BYTES,
               password, password_length + 1u);
    }
    clp_store_le32(payload, flags, 4u);
    clp_store_le32(payload + 4u, (uint32_t)config->power_adjust_qdbm, 4u);
    clp_store_le32(payload + 8u, config->source, 4u);
    memcpy(payload + 12u, config->ssid, sizeof(config->ssid) - 1u);
    return 0;
}

void cl_net_build_scan_request(uint8_t payload[CLP_NET_SCAN_REQUEST_BYTES],
                               uint32_t start_index,
                               uint32_t max_entries) {
    clp_store_le32(payload, start_index, 4u);
    clp_store_le32(payload + 4u, max_entries, 4u);
}

int cl_net_parse_scan_response(const uint8_t *payload,
                               uint32_t payload_length,
                               cl_net_scan_result_t *out_result) {
    if (!payload || !out_result ||
        payload_length < CLP_NET_SCAN_RESPONSE_HEADER_BYTES) {
        return -1;
    }
    uint32_t count = clp_load_le32(payload + 4u, 4u);
    uint32_t expected_length =
        CLP_NET_SCAN_RESPONSE_HEADER_BYTES + count * CLP_NET_SCAN_AP_BYTES;
    if (count > CLP_NET_SCAN_MAX_RESPONSE_APS ||
        payload_length != expected_length) {
        return -2;
    }
    memset(out_result, 0, sizeof(*out_result));
    out_result->total = clp_load_le32(payload, 4u);
    out_result->count = count;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *entry = payload + CLP_NET_SCAN_RESPONSE_HEADER_BYTES +
                               i * CLP_NET_SCAN_AP_BYTES;
        out_result->aps[i].rssi = (int32_t)clp_load_le32(entry, 4u);
        out_result->aps[i].authmode = clp_load_le32(entry + 4u, 4u);
        out_result->aps[i].channel = clp_load_le32(entry + 8u, 4u);
        memcpy(out_result->aps[i].ssid, entry + 12u,
               sizeof(out_result->aps[i].ssid));
        out_result->aps[i].ssid[sizeof(out_result->aps[i].ssid) - 1u] = '\0';
    }
    return 0;
}

int cl_net_build_open_request(uint8_t payload[CLP_NET_OPEN_REQUEST_BYTES],
                              cl_net_proto_t proto,
                              const cl_net_addr_t *remote) {
    if (!payload || !remote ||
        (proto != CL_NET_PROTO_UDP && proto != CL_NET_PROTO_TCP) ||
        (proto == CL_NET_PROTO_TCP && remote->port == 0)) {
        return -1;
    }
    clp_store_le32(payload, (uint32_t)proto, 4u);
    clp_store_le32(payload + 4u, remote->ipv4, 4u);
    clp_store_le32(payload + 8u, remote->port, 4u);
    return 0;
}

void cl_net_build_handle_request(uint8_t payload[4], cl_net_handle_t handle) {
    clp_store_le32(payload, handle, 4u);
}

void cl_net_build_recv_request(uint8_t payload[CLP_NET_RECV_REQUEST_BYTES],
                               cl_net_handle_t handle,
                               uint32_t capacity) {
    clp_store_le32(payload, handle, 4u);
    clp_store_le32(payload + 4u, capacity, 4u);
}

int cl_net_parse_u32_response(const uint8_t payload[4],
                              uint32_t *out_value) {
    if (!payload || !out_value) {
        return -1;
    }
    *out_value = clp_load_le32(payload, 4u);
    return 0;
}

int cl_net_init(cl_net_t *net, cl_client_t *client,
                void *scratch, size_t scratch_size) {
    if (!net || !client || !scratch || scratch_size == 0) {
        return -1;
    }
    net->client = client;
    net->scratch = (uint8_t *)scratch;
    net->scratch_size = scratch_size;
    return 0;
}

int cl_net_status(cl_net_t *net, uint32_t flags, cl_net_status_t *out_status) {
    cl_client_t *client = net_client(net);
    if (!client || !out_status) {
        return -1;
    }

    uint8_t *payload = net_scratch(net, CLP_NET_STATUS_RESPONSE_BYTES);
    if (!payload) {
        return -1;
    }
    cl_net_build_status_request(payload, flags);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_NET_STATUS_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET, CLP_NET_STATUS,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_NET_STATUS_RESPONSE_BYTES)) {
        return discard_response_error(client, &response);
    }

    cl_wire_read_payload(client, payload, CLP_NET_STATUS_RESPONSE_BYTES);
    return cl_net_parse_status_response(payload, out_status);
}

int cl_net_connect(cl_net_t *net, cl_net_status_t *out_status) {
    cl_net_status_t status;
    cl_net_status_t *target = out_status ? out_status : &status;
    int ret = cl_net_status(net, CLP_NET_STATUS_CONNECT_IF_NEEDED, target);
    if (ret < 0) {
        return ret;
    }
    return (target->flags & CLP_NET_STATUS_FLAG_CONNECTED) ? 0 : -3;
}

int cl_net_config_get(cl_net_t *net, cl_net_config_t *out_config) {
    cl_client_t *client = net_client(net);
    if (!client || !out_config) {
        return -1;
    }
    uint8_t *payload = net_scratch(net, CLP_NET_CONFIG_BYTES);
    if (!payload) {
        return -1;
    }

    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET,
                                     CLP_NET_CONFIG_GET, 0, 0, &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_NET_CONFIG_BYTES)) {
        return discard_response_error(client, &response);
    }

    cl_wire_read_payload(client, payload, CLP_NET_CONFIG_BYTES);
    return cl_net_parse_config_response(payload, out_config);
}

int cl_net_config_set(cl_net_t *net, const cl_net_config_t *config,
                      const char *password) {
    cl_client_t *client = net_client(net);
    if (!client || !config) {
        return -1;
    }

    uint8_t *payload = net_scratch(net, CLP_NET_CONFIG_BYTES);
    if (!payload) {
        return -1;
    }
    if (cl_net_build_config_request(payload, config, password) < 0) {
        return -1;
    }

    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_NET_CONFIG_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET,
                                     CLP_NET_CONFIG_SET, &chunk, 1,
                                     &response);
    if (ret < 0) {
        return ret;
    }
    return cl_client_status_error(response.imm);
}

int cl_net_web_control(cl_net_t *net, uint32_t action) {
    cl_client_t *client = net_client(net);
    if (!client ||
        (action != CLP_NET_WEB_CONTROL_STOP &&
         action != CLP_NET_WEB_CONTROL_START)) {
        return -1;
    }

    uint8_t *payload = net_scratch(net, CLP_NET_WEB_CONTROL_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    clp_store_le32(payload, action, CLP_NET_WEB_CONTROL_REQUEST_BYTES);

    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_NET_WEB_CONTROL_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET,
                                     CLP_NET_WEB_CONTROL, &chunk, 1,
                                     &response);
    if (ret < 0) {
        return ret;
    }
    return cl_client_status_error(response.imm);
}

int cl_net_scan(cl_net_t *net, uint32_t start_index, uint32_t max_entries,
                cl_net_scan_result_t *out_result) {
    cl_client_t *client = net_client(net);
    if (!client || !out_result ||
        max_entries == 0 ||
        max_entries > CLP_NET_SCAN_MAX_RESPONSE_APS) {
        return -1;
    }

    uint8_t *payload = net_scratch(net, CLP_NET_SCAN_MAX_RESPONSE_BYTES);
    if (!payload) {
        return -1;
    }
    cl_net_build_scan_request(payload, start_index, max_entries);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_NET_SCAN_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET, CLP_NET_SCAN,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.imm != CLP_STATUS_OK ||
        response.length > CLP_NET_SCAN_MAX_RESPONSE_BYTES) {
        return discard_response_error(client, &response);
    }

    cl_wire_read_payload(client, payload, response.length);
    return cl_net_parse_scan_response(payload, response.length,
                                      out_result);
}

int cl_net_parse_ipv4(const char *text, uint32_t *out_ipv4) {
    if (!text || !out_ipv4) {
        return -1;
    }
    uint32_t parts[4] = {0, 0, 0, 0};
    uint8_t part = 0;
    uint8_t digit_count = 0;
    const char *p = text;
    while (*p) {
        char ch = *p++;
        if (ch >= '0' && ch <= '9') {
            if (digit_count >= 3u) {
                return -1;
            }
            parts[part] = parts[part] * 10u + (uint32_t)(ch - '0');
            if (parts[part] > 255u) {
                return -1;
            }
            digit_count++;
            continue;
        }
        if (ch == '.') {
            if (digit_count == 0 || part >= 3u) {
                return -1;
            }
            part++;
            digit_count = 0;
            continue;
        }
        return -1;
    }
    if (part != 3u || digit_count == 0) {
        return -1;
    }
    *out_ipv4 = CL_NET_IPV4(parts[0], parts[1], parts[2], parts[3]);
    return 0;
}

int cl_net_resolve_ipv4(cl_net_t *net, const char *host, uint32_t *out_ipv4) {
    cl_client_t *client = net_client(net);
    if (!client || !host || !out_ipv4) {
        return -1;
    }
    if (cl_net_parse_ipv4(host, out_ipv4) == 0) {
        return 0;
    }
    size_t host_length = strlen(host);
    if (host_length == 0 || host_length > UINT32_MAX ||
        host_length + 1u > UINT32_MAX - CLP_NET_RESOLVE_HOST_OFFSET) {
        return -1;
    }
    uint8_t *flags_payload = net_scratch(net, CLP_NET_RESOLVE_RESPONSE_WORDS * 4u);
    if (!flags_payload) {
        return -1;
    }
    clp_store_le32(flags_payload, 0, CLP_NET_RESOLVE_HOST_OFFSET);
    cl_wire_chunk_t chunks[2] = {
        { .data = flags_payload, .length = CLP_NET_RESOLVE_HOST_OFFSET },
        { .data = (const uint8_t *)host, .length = (uint32_t)(host_length + 1u) },
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET,
                                     CLP_NET_RESOLVE, chunks, 2,
                                     &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_NET_RESOLVE_RESPONSE_WORDS * 4u)) {
        return discard_response_error(client, &response);
    }
    cl_wire_read_payload(client, flags_payload, CLP_NET_RESOLVE_RESPONSE_WORDS * 4u);
    cl_net_parse_u32_response(flags_payload, out_ipv4);
    return *out_ipv4 ? 0 : -3;
}

int cl_net_open(cl_net_t *net, cl_net_proto_t proto, const cl_net_addr_t *remote,
                cl_net_handle_t *out_handle) {
    cl_client_t *client = net_client(net);
    if (!client || !remote || !out_handle) {
        return -1;
    }

    uint8_t *payload = net_scratch(net, CLP_NET_OPEN_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    if (cl_net_build_open_request(payload, proto, remote) < 0) {
        return -1;
    }
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_NET_OPEN_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET, CLP_NET_OPEN,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_NET_OPEN_RESPONSE_WORDS * 4u)) {
        return discard_response_error(client, &response);
    }

    cl_wire_read_payload(client, payload, CLP_NET_OPEN_RESPONSE_WORDS * 4u);
    uint32_t handle = 0;
    cl_net_parse_u32_response(payload, &handle);
    *out_handle = (cl_net_handle_t)handle;
    return *out_handle ? 0 : -3;
}

int cl_net_open_udp(cl_net_t *net, cl_net_handle_t *out_handle) {
    cl_net_addr_t any = {
        .ipv4 = 0,
        .port = 0,
    };
    return cl_net_open(net, CL_NET_PROTO_UDP, &any, out_handle);
}

int cl_net_bind(cl_net_t *net, cl_net_proto_t proto, const cl_net_addr_t *local,
                cl_net_handle_t *out_handle) {
    cl_client_t *client = net_client(net);
    if (!client || !local || !out_handle ||
        (proto != CL_NET_PROTO_UDP && proto != CL_NET_PROTO_TCP)) {
        return -1;
    }

    uint8_t *payload = net_scratch(net, CLP_NET_BIND_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    clp_store_le32(payload, (uint32_t)proto, 4u);
    clp_store_le32(payload + 4u, local->ipv4, 4u);
    clp_store_le32(payload + 8u, local->port, 4u);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_NET_BIND_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET, CLP_NET_BIND,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_NET_OPEN_RESPONSE_WORDS * 4u)) {
        return discard_response_error(client, &response);
    }

    cl_wire_read_payload(client, payload, CLP_NET_OPEN_RESPONSE_WORDS * 4u);
    uint32_t handle = 0;
    cl_net_parse_u32_response(payload, &handle);
    *out_handle = (cl_net_handle_t)handle;
    return *out_handle ? 0 : -3;
}

int cl_net_listen(cl_net_t *net, cl_net_handle_t handle, uint32_t backlog) {
    cl_client_t *client = net_client(net);
    if (!client || !handle) {
        return -1;
    }
    uint8_t *payload = net_scratch(net, CLP_NET_LISTEN_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    cl_net_build_handle_request(payload, handle);
    clp_store_le32(payload + 4u, backlog, 4u);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_NET_LISTEN_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET, CLP_NET_LISTEN,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    return cl_client_status_error(response.imm);
}

int cl_net_accept(cl_net_t *net, cl_net_handle_t listen_handle,
                  cl_net_addr_t *out_remote,
                  cl_net_handle_t *out_handle) {
    cl_client_t *client = net_client(net);
    if (!client || !listen_handle || !out_handle) {
        return -1;
    }
    *out_handle = 0;
    if (out_remote) {
        memset(out_remote, 0, sizeof(*out_remote));
    }
    uint8_t *payload = net_scratch(net, CLP_NET_ACCEPT_RESPONSE_BYTES);
    if (!payload) {
        return -1;
    }
    cl_net_build_handle_request(payload, listen_handle);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_NET_ACCEPT_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET, CLP_NET_ACCEPT,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.imm != CLP_STATUS_OK) {
        cl_wire_discard_payload(client, response.length);
        return cl_client_status_error(response.imm);
    }
    if (response.length == 0) {
        return 0;
    }
    if (!response_ok(&response, CLP_NET_ACCEPT_RESPONSE_BYTES)) {
        cl_wire_discard_payload(client, response.length);
        return -2;
    }
    cl_wire_read_payload(client, payload, CLP_NET_ACCEPT_RESPONSE_BYTES);
    uint32_t handle = clp_load_le32(payload, 4u);
    *out_handle = (cl_net_handle_t)handle;
    if (out_remote) {
        out_remote->ipv4 = clp_load_le32(payload + 4u, 4u);
        out_remote->port = (uint16_t)clp_load_le32(payload + 8u, 4u);
    }
    return *out_handle ? 0 : -3;
}

int cl_net_open_host(cl_net_t *net, cl_net_proto_t proto, const char *host,
                     uint16_t port,
                     cl_net_handle_t *out_handle) {
    if (!host || !out_handle || !port) {
        return -1;
    }
    uint32_t ipv4 = 0;
    int ret = cl_net_resolve_ipv4(net, host, &ipv4);
    if (ret < 0) {
        return ret;
    }
    cl_net_addr_t remote = {
        .ipv4 = ipv4,
        .port = port,
    };
    return cl_net_open(net, proto, &remote, out_handle);
}

int cl_net_open_tcp_host_wait(cl_net_t *net, const char *host, uint16_t port,
                              uint32_t attempts,
                              cl_net_handle_t *out_handle) {
    if (!out_handle) {
        return -1;
    }
    *out_handle = 0;
    cl_net_handle_t handle = 0;
    int ret = cl_net_open_host(net, CL_NET_PROTO_TCP, host, port, &handle);
    if (ret < 0) {
        return ret;
    }
    ret = cl_net_wait_connected(net, handle, attempts);
    if (ret < 0) {
        (void)cl_net_close(net, handle);
        return ret;
    }
    *out_handle = handle;
    return 0;
}

int cl_net_tcp_request_host(cl_net_t *net,
                            const char *host,
                            uint16_t port,
                            const void *tx_data,
                            size_t tx_length,
                            void *rx_data,
                            size_t rx_capacity,
                            uint32_t connect_attempts,
                            uint32_t io_attempts) {
    if (!host || (tx_length && !tx_data) || (rx_capacity && !rx_data)) {
        return -1;
    }

    cl_net_handle_t handle = 0;
    int ret = cl_net_open_tcp_host_wait(net, host, port, connect_attempts,
                                        &handle);
    if (ret < 0) {
        return ret;
    }

    int result = 0;
    if (tx_length) {
        result = cl_net_send_all(net, handle, tx_data, tx_length, io_attempts);
        if (result < 0) {
            (void)cl_net_close(net, handle);
            return result;
        }
    }
    if (rx_capacity) {
        result = cl_net_recv_wait(net, handle, rx_data, rx_capacity,
                                  io_attempts);
    }
    int close_ret = cl_net_close(net, handle);
    if (result < 0) {
        return result;
    }
    if (close_ret < 0) {
        return close_ret;
    }
    return rx_capacity ? result : (int)tx_length;
}

int cl_net_close(cl_net_t *net, cl_net_handle_t handle) {
    cl_client_t *client = net_client(net);
    if (!client || !handle) {
        return -1;
    }
    uint8_t *payload = net_scratch(net, 4u);
    if (!payload) {
        return -1;
    }
    cl_net_build_handle_request(payload, handle);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = 4u,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET, CLP_NET_CLOSE,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    return cl_client_status_error(response.imm);
}

int cl_net_poll(cl_net_t *net, cl_net_handle_t handle, uint32_t *out_flags) {
    cl_client_t *client = net_client(net);
    if (!client || !handle || !out_flags) {
        return -1;
    }
    uint8_t *payload = net_scratch(net, CLP_NET_POLL_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    cl_net_build_handle_request(payload, handle);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_NET_POLL_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET, CLP_NET_POLL,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_NET_POLL_RESPONSE_BYTES)) {
        return discard_response_error(client, &response);
    }
    cl_wire_read_payload(client, payload, CLP_NET_POLL_RESPONSE_BYTES);
    return cl_net_parse_u32_response(payload, out_flags);
}

static int net_wait_flags_budget(cl_net_t *net,
                                 cl_net_handle_t handle,
                                 uint32_t required_flags,
                                 uint32_t *remaining_attempts,
                                 uint32_t *out_flags) {
    if (!net_client(net) || !handle || !required_flags ||
        !remaining_attempts) {
        return -1;
    }
    uint32_t last_flags = 0;
    while (*remaining_attempts) {
        --(*remaining_attempts);
        int ret = cl_net_poll(net, handle, &last_flags);
        if (ret < 0) {
            return ret;
        }
        if ((last_flags & required_flags) == required_flags) {
            if (out_flags) {
                *out_flags = last_flags;
            }
            return 0;
        }
    }
    if (out_flags) {
        *out_flags = last_flags;
    }
    return -3;
}

int cl_net_wait_flags(cl_net_t *net, cl_net_handle_t handle,
                      uint32_t required_flags,
                      uint32_t attempts, uint32_t *out_flags) {
    if (!attempts) {
        attempts = 1u;
    }
    return net_wait_flags_budget(net, handle, required_flags, &attempts,
                                 out_flags);
}

int cl_net_wait_connected(cl_net_t *net, cl_net_handle_t handle,
                          uint32_t attempts) {
    return cl_net_wait_flags(net, handle, CLP_NET_SOCKET_FLAG_CONNECTED,
                             attempts,
                             NULL);
}

int cl_net_send(cl_net_t *net, cl_net_handle_t handle, const void *data,
                size_t length) {
    cl_client_t *client = net_client(net);
    if (!client || !handle || (length && !data)) {
        return -1;
    }
    uint32_t chunk_length = length > CLP_NET_SEND_FRAME_MAX_DATA ?
        CLP_NET_SEND_FRAME_MAX_DATA : (uint32_t)length;
    uint8_t *handle_payload = net_scratch(net, CLP_NET_SEND_DATA_OFFSET);
    if (!handle_payload) {
        return -1;
    }
    cl_net_build_handle_request(handle_payload, handle);
    cl_wire_chunk_t chunks[2] = {
        { .data = handle_payload, .length = CLP_NET_SEND_DATA_OFFSET },
        { .data = (const uint8_t *)data, .length = chunk_length },
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET, CLP_NET_SEND,
                                     chunks, 2, &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_NET_RW_RESPONSE_WORDS * 4u)) {
        return discard_response_error(client, &response);
    }
    cl_wire_read_payload(client, handle_payload, 4u);
    uint32_t written = 0;
    cl_net_parse_u32_response(handle_payload, &written);
    return written <= (uint32_t)INT_MAX ? (int)written : -3;
}

int cl_net_sendto(cl_net_t *net, cl_net_handle_t handle,
                  const cl_net_addr_t *remote,
                  const void *data,
                  size_t length) {
    cl_client_t *client = net_client(net);
    if (!client || !handle || !remote || remote->port == 0 ||
        (length && !data) || length > CLP_NET_SENDTO_FRAME_MAX_DATA) {
        return -1;
    }
    uint8_t *addr_payload = net_scratch(net, CLP_NET_SENDTO_DATA_OFFSET);
    if (!addr_payload) {
        return -1;
    }
    cl_net_build_handle_request(addr_payload, handle);
    clp_store_le32(addr_payload + 4u, remote->ipv4, 4u);
    clp_store_le32(addr_payload + 8u, remote->port, 4u);
    cl_wire_chunk_t chunks[2] = {
        { .data = addr_payload, .length = CLP_NET_SENDTO_DATA_OFFSET },
        { .data = (const uint8_t *)data, .length = (uint32_t)length },
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET, CLP_NET_SENDTO,
                                     chunks, 2, &response);
    if (ret < 0) {
        return ret;
    }
    if (!response_ok(&response, CLP_NET_RW_RESPONSE_WORDS * 4u)) {
        return discard_response_error(client, &response);
    }
    cl_wire_read_payload(client, addr_payload, 4u);
    uint32_t written = 0;
    cl_net_parse_u32_response(addr_payload, &written);
    return written <= (uint32_t)INT_MAX ? (int)written : -3;
}

int cl_net_send_all(cl_net_t *net, cl_net_handle_t handle, const void *data,
                    size_t length,
                    uint32_t wait_attempts) {
    if (!net_client(net) || !handle || (length && !data) ||
        length > (size_t)INT_MAX) {
        return -1;
    }
    if (!length) {
        return 0;
    }

    const uint8_t *cursor = (const uint8_t *)data;
    size_t sent = 0;
    uint32_t stall_budget = wait_attempts ? wait_attempts : 1u;
    uint32_t remaining = stall_budget;
    while (sent < length) {
        int ret = cl_net_send(net, handle, cursor + sent, length - sent);
        if (ret < 0) {
            return ret;
        }
        if (ret > 0) {
            if ((size_t)ret > length - sent) {
                return -2;
            }
            sent += (size_t)ret;
            remaining = stall_budget;
            continue;
        }
        ret = net_wait_flags_budget(net, handle,
                                    CLP_NET_SOCKET_FLAG_WRITABLE,
                                    &remaining, NULL);
        if (ret < 0) {
            return ret;
        }
    }
    return (int)sent;
}

int cl_net_udp_send(cl_net_t *net,
                    const cl_net_addr_t *remote,
                    const void *data,
                    size_t length) {
    if (!remote || (length && !data)) {
        return -1;
    }
    cl_net_handle_t handle = 0;
    int ret = cl_net_open(net, CL_NET_PROTO_UDP, remote, &handle);
    if (ret < 0) {
        return ret;
    }
    ret = cl_net_send(net, handle, data, length);
    int close_ret = cl_net_close(net, handle);
    if (ret < 0) {
        return ret;
    }
    if (close_ret < 0) {
        return close_ret;
    }
    return ret;
}

int cl_net_udp_send_host(cl_net_t *net,
                         const char *host,
                         uint16_t port,
                         const void *data,
                         size_t length) {
    if (!host || (length && !data)) {
        return -1;
    }
    cl_net_handle_t handle = 0;
    int ret = cl_net_open_host(net, CL_NET_PROTO_UDP, host, port, &handle);
    if (ret < 0) {
        return ret;
    }
    ret = cl_net_send(net, handle, data, length);
    int close_ret = cl_net_close(net, handle);
    if (ret < 0) {
        return ret;
    }
    if (close_ret < 0) {
        return close_ret;
    }
    return ret;
}

int cl_net_recv(cl_net_t *net, cl_net_handle_t handle, void *data,
                size_t capacity) {
    cl_client_t *client = net_client(net);
    if (!client || !handle || (capacity && !data) ||
        capacity > UINT32_MAX) {
        return -1;
    }
    uint32_t request_capacity = capacity > CLP_NET_RECV_FRAME_MAX_DATA ?
        CLP_NET_RECV_FRAME_MAX_DATA : (uint32_t)capacity;
    uint8_t *payload = net_scratch(net, CLP_NET_RECV_REQUEST_BYTES);
    if (!payload) {
        return -1;
    }
    cl_net_build_recv_request(payload, handle, request_capacity);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_NET_RECV_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET, CLP_NET_RECV,
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

int cl_net_recvfrom(cl_net_t *net, cl_net_handle_t handle,
                    cl_net_addr_t *out_remote,
                    void *data,
                    size_t capacity) {
    cl_client_t *client = net_client(net);
    if (!client || !handle || (capacity && !data) ||
        capacity > UINT32_MAX) {
        return -1;
    }
    uint32_t request_capacity = capacity > CLP_NET_RECVFROM_FRAME_MAX_DATA ?
        CLP_NET_RECVFROM_FRAME_MAX_DATA : (uint32_t)capacity;
    uint8_t *payload = net_scratch(net, CLP_NET_RECVFROM_ADDR_BYTES);
    if (!payload) {
        return -1;
    }
    cl_net_build_recv_request(payload, handle, request_capacity);
    cl_wire_chunk_t chunk = {
        .data = payload,
        .length = CLP_NET_RECVFROM_REQUEST_BYTES,
    };
    clp_header_t response;
    int ret = cl_wire_command_chunks(client, CLP_CH_NET, CLP_NET_RECVFROM,
                                     &chunk, 1, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.imm != CLP_STATUS_OK) {
        cl_wire_discard_payload(client, response.length);
        return cl_client_status_error(response.imm);
    }
    if (response.length == 0) {
        return 0;
    }
    if (response.length < CLP_NET_RECVFROM_ADDR_BYTES ||
        response.length > request_capacity + CLP_NET_RECVFROM_ADDR_BYTES) {
        cl_wire_discard_payload(client, response.length);
        return -2;
    }

    cl_wire_read_payload(client, payload, CLP_NET_RECVFROM_ADDR_BYTES);
    if (out_remote) {
        out_remote->ipv4 = clp_load_le32(payload, 4u);
        out_remote->port = (uint16_t)clp_load_le32(payload + 4u, 4u);
    }
    uint32_t data_length = response.length - CLP_NET_RECVFROM_ADDR_BYTES;
    if (data_length) {
        cl_wire_read_payload(client, (uint8_t *)data, data_length);
    }
    return data_length <= (uint32_t)INT_MAX ? (int)data_length : -3;
}

int cl_net_recv_wait(cl_net_t *net, cl_net_handle_t handle, void *data,
                     size_t capacity,
                     uint32_t wait_attempts) {
    if (!net_client(net) || !handle || !data || !capacity ||
        capacity > (size_t)INT_MAX) {
        return -1;
    }
    uint32_t remaining = wait_attempts ? wait_attempts : 1u;
    for (;;) {
        int ret = cl_net_recv(net, handle, data, capacity);
        if (ret < 0) {
            return ret;
        }
        if (ret > 0) {
            return ret;
        }
        ret = net_wait_flags_budget(net, handle,
                                    CLP_NET_SOCKET_FLAG_READABLE,
                                    &remaining, NULL);
        if (ret < 0) {
            return ret;
        }
    }
    return -3;
}
