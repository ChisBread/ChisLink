#ifndef CHISLINK_CRC32_H
#define CHISLINK_CRC32_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Incremental CRC-32/ISO-HDLC state. */
typedef struct cl_crc32 {
    uint32_t state;
} cl_crc32_t;

void cl_crc32_init(cl_crc32_t *crc);
void cl_crc32_update(cl_crc32_t *crc, const void *data, uint32_t length);
uint32_t cl_crc32_finalize(const cl_crc32_t *crc);
uint32_t cl_crc32_calculate(const void *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif
