#include "chislink/crc32.h"

#define CL_CRC32_POLYNOMIAL 0xedb88320u

static uint32_t g_crc32_table[256];
static uint8_t g_crc32_table_ready;

static void crc32_prepare_table(void) {
    if (g_crc32_table_ready) {
        return;
    }
    for (uint32_t i = 0; i < 256u; ++i) {
        uint32_t value = i;
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            value = (value >> 1u) ^
                ((value & 1u) ? CL_CRC32_POLYNOMIAL : 0u);
        }
        g_crc32_table[i] = value;
    }
    g_crc32_table_ready = 1u;
}

void cl_crc32_init(cl_crc32_t *crc) {
    if (!crc) {
        return;
    }
    crc32_prepare_table();
    crc->state = 0xffffffffu;
}

void cl_crc32_update(cl_crc32_t *crc, const void *data, uint32_t length) {
    if (!crc || (!data && length)) {
        return;
    }
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t state = crc->state;
    for (uint32_t i = 0; i < length; ++i) {
        state = g_crc32_table[(state ^ bytes[i]) & 0xffu] ^ (state >> 8u);
    }
    crc->state = state;
}

uint32_t cl_crc32_finalize(const cl_crc32_t *crc) {
    return crc ? ~crc->state : 0u;
}

uint32_t cl_crc32_calculate(const void *data, uint32_t length) {
    cl_crc32_t crc;
    cl_crc32_init(&crc);
    cl_crc32_update(&crc, data, length);
    return cl_crc32_finalize(&crc);
}
