#include "chislink/gba/hw.h"

typedef void (*cl_gba_irq_fn_t)(void);

void irqInit(void);
cl_gba_irq_fn_t *irqSet(unsigned int mask, cl_gba_irq_fn_t function);
void irqEnable(unsigned int mask);
void irqDisable(unsigned int mask);

uint16_t cl_gba_keys_held(void) {
    return (uint16_t)(~CL_GBA_REG_KEYINPUT & 0x03ffu);
}

void cl_gba_wait_vblank(void) {
    while (CL_GBA_REG_VCOUNT >= 160u) {
    }
    while (CL_GBA_REG_VCOUNT < 160u) {
    }
}

void cl_gba_time_init(void) {
    CL_GBA_REG_TM2CNT_H = 0;
    CL_GBA_REG_TM3CNT_H = 0;
    CL_GBA_REG_TM2CNT_L = 0;
    CL_GBA_REG_TM3CNT_L = 0;
    CL_GBA_REG_TM2CNT_H = CL_GBA_TIMER_ENABLE | CL_GBA_TIMER_PRESCALE_1024;
    CL_GBA_REG_TM3CNT_H = CL_GBA_TIMER_ENABLE | CL_GBA_TIMER_CASCADE;
}

uint32_t cl_gba_time_ticks(void) {
    uint16_t high = CL_GBA_REG_TM3CNT_L;
    uint16_t low = CL_GBA_REG_TM2CNT_L;
    uint16_t high2 = CL_GBA_REG_TM3CNT_L;
    if (high != high2) {
        high = high2;
        low = CL_GBA_REG_TM2CNT_L;
    }
    return ((uint32_t)high << 16u) | low;
}

void cl_gba_irq_enable(uint16_t irq_mask) {
    irqEnable(irq_mask);
}

void cl_gba_irq_disable(uint16_t irq_mask) {
    irqDisable(irq_mask);
}

void cl_gba_irq_init(void) {
    CL_GBA_REG_IME = 0;
    CL_GBA_REG_IF = 0xffffu;
    irqInit();
}

void cl_gba_irq_set_handler(uint16_t irq_mask, void (*handler)(void)) {
    (void)irqSet(irq_mask, handler);
}
