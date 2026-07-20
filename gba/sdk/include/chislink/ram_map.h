#ifndef CHISLINK_RAM_MAP_H
#define CHISLINK_RAM_MAP_H

#include <stdint.h>

#include "chislink/client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_ram_map {
    cl_client_t *client;
} cl_ram_map_t;

int cl_ram_map_init(cl_ram_map_t *map, cl_client_t *client);
int cl_ram_map_put(cl_ram_map_t *map,
                   const void *key, uint32_t key_length,
                   const void *value, uint32_t value_length);
int cl_ram_map_get(cl_ram_map_t *map,
                   const void *key, uint32_t key_length,
                   void *value, uint32_t value_capacity,
                   uint32_t *out_value_length);
int cl_ram_map_remove(cl_ram_map_t *map,
                      const void *key, uint32_t key_length);
int cl_ram_map_clear(cl_ram_map_t *map);

#ifdef __cplusplus
}
#endif

#endif
