#include "chislink/ram_map.h"

#include <stddef.h>

#include "chislink/wire.h"

static int validate_map(const cl_ram_map_t *map) {
    if (!map || !map->client || !map->client->config.xfer32) {
        return -1;
    }
    return (map->client->caps & CLP_CAP_RAM_MAP) ? 0 : -4;
}

static int validate_key(const void *key, uint32_t key_length) {
    return key && key_length && key_length <= CLP_RAM_MAP_KEY_MAX_BYTES ?
           0 : -1;
}

int cl_ram_map_init(cl_ram_map_t *map, cl_client_t *client) {
    if (!map || !client) {
        return -1;
    }
    map->client = client;
    return 0;
}

int cl_ram_map_put(cl_ram_map_t *map,
                   const void *key, uint32_t key_length,
                   const void *value, uint32_t value_length) {
    int ret = validate_map(map);
    if (ret < 0 || validate_key(key, key_length) < 0 ||
        (value_length && !value) ||
        value_length > CLP_RAM_MAP_VALUE_MAX_BYTES) {
        return ret < 0 ? ret : -1;
    }

    uint8_t fixed[CLP_RAM_MAP_PUT_FIXED_BYTES];
    cl_wire_store_le32(fixed, key_length, 4u);
    cl_wire_store_le32(fixed + 4u, value_length, 4u);
    cl_wire_chunk_t chunks[3] = {
        { fixed, sizeof(fixed) },
        { (const uint8_t *)key, key_length },
        { (const uint8_t *)value, value_length },
    };
    clp_header_t response;
    ret = cl_wire_command_chunks(map->client, CLP_CH_RAM_MAP,
                                 CLP_RAM_MAP_PUT, chunks, 3u, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.length) {
        cl_wire_discard_payload(map->client, response.length);
        return -2;
    }
    return cl_client_status_error(response.imm);
}

int cl_ram_map_get(cl_ram_map_t *map,
                   const void *key, uint32_t key_length,
                   void *value, uint32_t value_capacity,
                   uint32_t *out_value_length) {
    if (out_value_length) {
        *out_value_length = 0;
    }
    int ret = validate_map(map);
    if (ret < 0 || validate_key(key, key_length) < 0 || !out_value_length ||
        (value_capacity && !value)) {
        return ret < 0 ? ret : -1;
    }

    uint8_t fixed[CLP_RAM_MAP_KEY_FIXED_BYTES];
    cl_wire_store_le32(fixed, key_length, sizeof(fixed));
    cl_wire_chunk_t chunks[2] = {
        { fixed, sizeof(fixed) },
        { (const uint8_t *)key, key_length },
    };
    clp_header_t response;
    ret = cl_wire_command_chunks(map->client, CLP_CH_RAM_MAP,
                                 CLP_RAM_MAP_GET, chunks, 2u, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.imm != CLP_STATUS_OK) {
        if (response.length) {
            cl_wire_discard_payload(map->client, response.length);
        }
        return cl_client_status_error(response.imm);
    }
    if (response.length > value_capacity ||
        response.length > CLP_RAM_MAP_VALUE_MAX_BYTES) {
        cl_wire_discard_payload(map->client, response.length);
        return -2;
    }
    cl_wire_read_payload(map->client, (uint8_t *)value, response.length);
    *out_value_length = response.length;
    return 0;
}

int cl_ram_map_remove(cl_ram_map_t *map,
                      const void *key, uint32_t key_length) {
    int ret = validate_map(map);
    if (ret < 0 || validate_key(key, key_length) < 0) {
        return ret < 0 ? ret : -1;
    }
    uint8_t fixed[CLP_RAM_MAP_KEY_FIXED_BYTES];
    cl_wire_store_le32(fixed, key_length, sizeof(fixed));
    cl_wire_chunk_t chunks[2] = {
        { fixed, sizeof(fixed) },
        { (const uint8_t *)key, key_length },
    };
    clp_header_t response;
    ret = cl_wire_command_chunks(map->client, CLP_CH_RAM_MAP,
                                 CLP_RAM_MAP_REMOVE, chunks, 2u, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.length) {
        cl_wire_discard_payload(map->client, response.length);
        return -2;
    }
    return cl_client_status_error(response.imm);
}

int cl_ram_map_clear(cl_ram_map_t *map) {
    int ret = validate_map(map);
    if (ret < 0) {
        return ret;
    }
    clp_header_t response;
    ret = cl_wire_command_chunks(map->client, CLP_CH_RAM_MAP,
                                 CLP_RAM_MAP_CLEAR, NULL, 0, &response);
    if (ret < 0) {
        return ret;
    }
    if (response.length) {
        cl_wire_discard_payload(map->client, response.length);
        return -2;
    }
    return cl_client_status_error(response.imm);
}
