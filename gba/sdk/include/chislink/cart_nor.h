#ifndef CHISLINK_CART_NOR_H
#define CHISLINK_CART_NOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cl_cart_nor_probe {
    uint32_t flash_id;
    uint32_t flash_size;
    uint32_t write_buffer_size;
    uint32_t sector_size;
    uint32_t sector_region_start[4];
    uint32_t sector_region_end[4];
    uint32_t sector_region_size[4];
    uint8_t cfi_valid;
    uint8_t is_intel;
    uint8_t swap_d0d1;
    uint8_t sector_region_count;
    uint8_t sector_region_reversed;
    uint8_t reserved[3];
} cl_cart_nor_probe_t;

typedef void (*cl_cart_nor_progress_fn)(uint32_t consumed, void *user);

int cl_cart_nor_probe(cl_cart_nor_probe_t *out_probe);
int cl_cart_nor_write(uint32_t offset,
                      const void *src,
                      uint32_t length,
                      uint32_t *out_length);
int cl_cart_nor_write_stream(uint32_t offset,
                             const void *src,
                             uint32_t length,
                             uint32_t *out_length,
                             cl_cart_nor_progress_fn progress,
                             void *user);
int cl_cart_nor_set_write_size(uint64_t size);
int cl_cart_nor_flush(void);
void cl_cart_nor_invalidate(void);

#ifdef __cplusplus
}
#endif

#endif
