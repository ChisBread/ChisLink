#ifndef CHISLINK_GBA_DMA_H
#define CHISLINK_GBA_DMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void cl_gba_dma_copy16(volatile void *dst, const void *src, uint16_t count);
void cl_gba_dma_copy32(volatile void *dst, const void *src, uint16_t count);
void cl_gba_dma_fill16(volatile void *dst, uint16_t value, uint16_t count);

#ifdef __cplusplus
}
#endif

#endif
