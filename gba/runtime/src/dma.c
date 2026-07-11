#include "chislink/gba/dma.h"

#include <stdint.h>

#define CL_GBA_DMA_SAD(n) (*(volatile uint32_t *)(uintptr_t)(0x040000b0u + (n) * 12u))
#define CL_GBA_DMA_DAD(n) (*(volatile uint32_t *)(uintptr_t)(0x040000b4u + (n) * 12u))
#define CL_GBA_DMA_CNT(n) (*(volatile uint32_t *)(uintptr_t)(0x040000b8u + (n) * 12u))

#define CL_GBA_DMA_ENABLE      0x80000000u
#define CL_GBA_DMA_TRANSFER32  0x04000000u
#define CL_GBA_DMA_SRC_FIXED   0x01000000u

static void dma_wait(uint8_t channel) {
    while (CL_GBA_DMA_CNT(channel) & CL_GBA_DMA_ENABLE) {
    }
}

void cl_gba_dma_copy16(volatile void *dst, const void *src, uint16_t count) {
    if (!count) {
        return;
    }
    CL_GBA_DMA_SAD(3) = (uint32_t)(uintptr_t)src;
    CL_GBA_DMA_DAD(3) = (uint32_t)(uintptr_t)dst;
    CL_GBA_DMA_CNT(3) = CL_GBA_DMA_ENABLE | count;
    dma_wait(3);
}

void cl_gba_dma_copy32(volatile void *dst, const void *src, uint16_t count) {
    if (!count) {
        return;
    }
    CL_GBA_DMA_SAD(3) = (uint32_t)(uintptr_t)src;
    CL_GBA_DMA_DAD(3) = (uint32_t)(uintptr_t)dst;
    CL_GBA_DMA_CNT(3) = CL_GBA_DMA_ENABLE | CL_GBA_DMA_TRANSFER32 | count;
    dma_wait(3);
}

void cl_gba_dma_fill16(volatile void *dst, uint16_t value, uint16_t count) {
    if (!count) {
        return;
    }
    volatile uint16_t fill = value;
    CL_GBA_DMA_SAD(3) = (uint32_t)(uintptr_t)&fill;
    CL_GBA_DMA_DAD(3) = (uint32_t)(uintptr_t)dst;
    CL_GBA_DMA_CNT(3) = CL_GBA_DMA_ENABLE | CL_GBA_DMA_SRC_FIXED | count;
    dma_wait(3);
}
