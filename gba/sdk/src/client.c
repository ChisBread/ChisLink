#include "chislink/client.h"
#include "chislink/wire.h"

static uint16_t client_next_seq(cl_client_t *client) {
    client->next_seq++;
    if (!client->next_seq) {
        client->next_seq = 1;
    }
    return client->next_seq;
}

bool cl_client_init(cl_client_t *client, const cl_client_config_t *config) {
    if (!client || !config || !config->xfer32) {
        return false;
    }

    client->config = *config;
    client->state = CL_CLIENT_OFFLINE;
    client->next_seq = 0;
    client->last_status = CLP_STATUS_OK;
    client->caps = 0;
    return true;
}

cl_client_state_t cl_client_state(const cl_client_t *client) {
    return client ? client->state : CL_CLIENT_ERROR;
}

bool cl_client_poll(cl_client_t *client) {
    bool online = cl_client_hello(client);
    if (!online && client) {
        client->state = CL_CLIENT_OFFLINE;
    }
    return online;
}

bool cl_client_hello(cl_client_t *client) {
    if (!client || !client->config.xfer32) {
        return false;
    }

    uint16_t seq = client_next_seq(client);
    uint32_t words[4];
    clp_header_t header = {
        .type = CLP_TYPE_COMMAND,
        .channel = CLP_CH_CONTROL,
        .opcode = CLP_CTRL_HELLO,
        .imm = CLP_VERSION,
        .length = 0,
        .seq = seq,
        .flags = CLP_FLAG_NEEDS_ACK,
        .crc32 = 0,
    };
    clp_encode_header_words(&header, words);

    client->state = CL_CLIENT_BUSY;
    for (unsigned i = 0; i < 4; ++i) {
        (void)client->config.xfer32(words[i], client->config.transport_user);
    }

    clp_header_t decoded;
    if (cl_wire_read_response(client, CLP_CH_CONTROL, CLP_CTRL_HELLO,
                              seq, &decoded) < 0 ||
        decoded.length < 16) {
        client->state = CL_CLIENT_ERROR;
        client->last_status = CLP_STATUS_BAD_PACKET;
        return false;
    }

    uint32_t caps = client->config.xfer32(
        clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
        client->config.transport_user);
    (void)client->config.xfer32(
        clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
        client->config.transport_user);
    (void)client->config.xfer32(
        clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
        client->config.transport_user);
    (void)client->config.xfer32(
        clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
        client->config.transport_user);

    client->caps = caps;
    client->last_status = decoded.imm;
    client->state = decoded.imm == CLP_STATUS_OK ? CL_CLIENT_IDLE :
                                                   CL_CLIENT_ERROR;
    return decoded.imm == CLP_STATUS_OK;
}

uint16_t cl_client_last_status(const cl_client_t *client) {
    return client ? client->last_status : CLP_STATUS_BAD_PACKET;
}

int cl_client_status_error(uint16_t status) {
    return status == CLP_STATUS_OK ? 0 : CL_CLIENT_STATUS_ERROR(status);
}

const char *cl_client_status_name(uint16_t status) {
    switch (status) {
    case CLP_STATUS_OK:
        return "OK";
    case CLP_STATUS_BUSY:
        return "BUSY";
    case CLP_STATUS_UNSUPPORTED:
        return "UNSUP";
    case CLP_STATUS_BAD_PACKET:
        return "BADPKT";
    case CLP_STATUS_BAD_CRC:
        return "BADCRC";
    case CLP_STATUS_TIMEOUT:
        return "TIMEOUT";
    case CLP_STATUS_CANCELLED:
        return "CANCEL";
    case CLP_STATUS_IO_ERROR:
        return "IOERR";
    case CLP_STATUS_NOT_FOUND:
        return "NOTFOUND";
    default:
        return "STATUS";
    }
}

bool cl_client_request_caps(cl_client_t *client) {
    if (!client || !client->config.xfer32) {
        return false;
    }

    clp_header_t header = {
        .type = CLP_TYPE_COMMAND,
        .channel = CLP_CH_CONTROL,
        .opcode = CLP_CTRL_CAPS,
        .imm = 0,
        .length = 0,
        .seq = client_next_seq(client),
        .flags = CLP_FLAG_NEEDS_ACK,
        .crc32 = 0,
    };
    uint32_t words[4];
    clp_encode_header_words(&header, words);
    for (unsigned i = 0; i < 4; ++i) {
        (void)client->config.xfer32(words[i], client->config.transport_user);
    }

    clp_header_t decoded;
    if (cl_wire_read_response(client, CLP_CH_CONTROL, CLP_CTRL_CAPS,
                              header.seq, &decoded) < 0 ||
        decoded.length < 16) {
        client->state = CL_CLIENT_ERROR;
        client->last_status = CLP_STATUS_BAD_PACKET;
        return false;
    }

    uint32_t caps = client->config.xfer32(
        clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
        client->config.transport_user);
    (void)client->config.xfer32(
        clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
        client->config.transport_user);
    (void)client->config.xfer32(
        clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
        client->config.transport_user);
    (void)client->config.xfer32(
        clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
        client->config.transport_user);

    client->caps = caps;
    client->last_status = decoded.imm;
    client->state = decoded.imm == CLP_STATUS_OK ? CL_CLIENT_IDLE : CL_CLIENT_ERROR;
    return decoded.imm == CLP_STATUS_OK;
}

uint32_t cl_client_caps(const cl_client_t *client) {
    return client ? client->caps : 0;
}

uint16_t cl_client_next_seq(cl_client_t *client) {
    if (!client) {
        return 0;
    }
    return client_next_seq(client);
}

uint16_t cl_client_current_seq(const cl_client_t *client) {
    return client ? client->next_seq : 0;
}

void cl_client_set_current_seq(cl_client_t *client, uint16_t seq) {
    if (client) {
        client->next_seq = seq;
    }
}

int cl_client_set_poll_ticks(cl_client_t *client, uint8_t ticks) {
    if (!client || !client->config.xfer32) return -1;
    if (ticks == 0) ticks = 1;

    uint16_t seq = client_next_seq(client);
    uint32_t words[4];
    clp_header_t header = {
        .type = CLP_TYPE_COMMAND, .channel = CLP_CH_CONTROL,
        .opcode = CLP_CTRL_SET_POLL_TICKS, .imm = 0,
        .length = 1, .seq = seq,
        .flags = CLP_FLAG_NEEDS_ACK, .crc32 = 0,
    };
    clp_encode_header_words(&header, words);
    for (uint8_t i = 0; i < 4u; ++i)
        (void)client->config.xfer32(words[i], client->config.transport_user);
    (void)client->config.xfer32((uint32_t)ticks, client->config.transport_user);

    clp_header_t response;
    if (cl_wire_read_response(client, CLP_CH_CONTROL,
                              CLP_CTRL_SET_POLL_TICKS, seq,
                              &response) == 0 &&
        response.imm == CLP_STATUS_OK) {
        return 0;
    }
    client->last_status = CLP_STATUS_BAD_PACKET;
    client->state = CL_CLIENT_ERROR;
    return -2;
}
