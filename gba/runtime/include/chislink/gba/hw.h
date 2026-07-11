#ifndef CHISLINK_GBA_HW_H
#define CHISLINK_GBA_HW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CL_GBA_IWRAM_CODE __attribute__((section(".iwram"), long_call))

#define CL_GBA_SCREEN_WIDTH  240u
#define CL_GBA_SCREEN_HEIGHT 160u

#define CL_GBA_REG16(addr) (*(volatile uint16_t *)(uintptr_t)(addr))
#define CL_GBA_REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define CL_GBA_REG_DISPCNT  CL_GBA_REG16(0x04000000u)
#define CL_GBA_REG_DISPSTAT CL_GBA_REG16(0x04000004u)
#define CL_GBA_REG_VCOUNT   CL_GBA_REG16(0x04000006u)
#define CL_GBA_REG_BG0CNT   CL_GBA_REG16(0x04000008u)
#define CL_GBA_REG_BG1CNT   CL_GBA_REG16(0x0400000au)
#define CL_GBA_REG_BG2CNT   CL_GBA_REG16(0x0400000cu)
#define CL_GBA_REG_BG0HOFS  CL_GBA_REG16(0x04000010u)
#define CL_GBA_REG_BG0VOFS  CL_GBA_REG16(0x04000012u)
#define CL_GBA_REG_BG1HOFS  CL_GBA_REG16(0x04000014u)
#define CL_GBA_REG_BG1VOFS  CL_GBA_REG16(0x04000016u)
#define CL_GBA_REG_BG2PA    CL_GBA_REG16(0x04000020u)
#define CL_GBA_REG_BG2PB    CL_GBA_REG16(0x04000022u)
#define CL_GBA_REG_BG2PC    CL_GBA_REG16(0x04000024u)
#define CL_GBA_REG_BG2PD    CL_GBA_REG16(0x04000026u)
#define CL_GBA_REG_BG2X     CL_GBA_REG32(0x04000028u)
#define CL_GBA_REG_BG2Y     CL_GBA_REG32(0x0400002cu)
#define CL_GBA_REG_TM2CNT_L CL_GBA_REG16(0x04000108u)
#define CL_GBA_REG_TM2CNT_H CL_GBA_REG16(0x0400010au)
#define CL_GBA_REG_TM3CNT_L CL_GBA_REG16(0x0400010cu)
#define CL_GBA_REG_TM3CNT_H CL_GBA_REG16(0x0400010eu)
#define CL_GBA_REG_KEYINPUT CL_GBA_REG16(0x04000130u)
#define CL_GBA_REG_RCNT     CL_GBA_REG16(0x04000134u)
#define CL_GBA_REG_SIOCNT   CL_GBA_REG16(0x04000128u)
#define CL_GBA_REG_SIODATA32 CL_GBA_REG32(0x04000120u)
#define CL_GBA_REG_IE       CL_GBA_REG16(0x04000200u)
#define CL_GBA_REG_IF       CL_GBA_REG16(0x04000202u)
#define CL_GBA_REG_IME      CL_GBA_REG16(0x04000208u)

#define CL_GBA_MEM_PALETTE  ((volatile uint16_t *)(uintptr_t)0x05000000u)
#define CL_GBA_MEM_OBJ_PALETTE ((volatile uint16_t *)(uintptr_t)0x05000200u)
#define CL_GBA_MEM_VRAM_U8  ((volatile uint8_t *)(uintptr_t)0x06000000u)
#define CL_GBA_MEM_VRAM_U16 ((volatile uint16_t *)(uintptr_t)0x06000000u)
#define CL_GBA_MEM_OBJ_VRAM_U8 ((volatile uint8_t *)(uintptr_t)0x06010000u)
#define CL_GBA_MEM_OAM      ((volatile uint16_t *)(uintptr_t)0x07000000u)
#define CL_GBA_MEM_VRAM_PAGE0_U8 ((volatile uint8_t *)(uintptr_t)0x06000000u)
#define CL_GBA_MEM_VRAM_PAGE1_U8 ((volatile uint8_t *)(uintptr_t)0x0600a000u)

#define CL_GBA_DISPCNT_MODE0 0x0000u
#define CL_GBA_DISPCNT_MODE4 0x0004u
#define CL_GBA_DISPCNT_OBJ_1D 0x0040u
#define CL_GBA_DISPCNT_BG0   0x0100u
#define CL_GBA_DISPCNT_BG1   0x0200u
#define CL_GBA_DISPCNT_BG2   0x0400u
#define CL_GBA_DISPCNT_OBJ   0x1000u
#define CL_GBA_DISPCNT_PAGE1 0x0010u

#define CL_GBA_BG_16_COLOR 0x0000u
#define CL_GBA_BG_256_COLOR 0x0080u
#define CL_GBA_BG_CHAR_BASE(n) ((uint16_t)((n) << 2u))
#define CL_GBA_BG_SCREEN_BASE(n) ((uint16_t)((n) << 8u))
#define CL_GBA_BG_SIZE_32x32 0x0000u

#define CL_GBA_OBJ_ATTR0_Y_MASK 0x00ffu
#define CL_GBA_OBJ_ATTR0_HIDE 0x0200u
#define CL_GBA_OBJ_ATTR0_4BPP 0x0000u
#define CL_GBA_OBJ_ATTR0_SQUARE 0x0000u
#define CL_GBA_OBJ_ATTR0_WIDE 0x4000u
#define CL_GBA_OBJ_ATTR1_X_MASK 0x01ffu
#define CL_GBA_OBJ_ATTR1_SIZE_8 0x0000u
#define CL_GBA_OBJ_ATTR1_SIZE_16 0x4000u
#define CL_GBA_OBJ_ATTR1_SIZE_32 0x8000u
#define CL_GBA_OBJ_ATTR1_SIZE_64 0xc000u
#define CL_GBA_OBJ_ATTR2_TILE_MASK 0x03ffu
#define CL_GBA_OBJ_ATTR2_PRIORITY(n) ((uint16_t)((n) << 10u))
#define CL_GBA_OBJ_ATTR2_PALETTE(n) ((uint16_t)((n) << 12u))

#define CL_GBA_KEY_A      0x0001u
#define CL_GBA_KEY_B      0x0002u
#define CL_GBA_KEY_SELECT 0x0004u
#define CL_GBA_KEY_START  0x0008u
#define CL_GBA_KEY_RIGHT  0x0010u
#define CL_GBA_KEY_LEFT   0x0020u
#define CL_GBA_KEY_UP     0x0040u
#define CL_GBA_KEY_DOWN   0x0080u
#define CL_GBA_KEY_R      0x0100u
#define CL_GBA_KEY_L      0x0200u

#define CL_GBA_IRQ_VBLANK 0x0001u
#define CL_GBA_IRQ_SERIAL 0x0080u
#define CL_GBA_IRQ_KEYPAD 0x1000u

#define CL_GBA_SIO_32BIT 0x1000u
#define CL_GBA_SIO_IRQ   0x4000u
#define CL_GBA_SIO_START 0x0080u
#define CL_GBA_SIO_SO_HIGH 0x0008u
#define CL_GBA_RCNT_NORMAL 0x0000u
#define CL_GBA_TIMER_ENABLE 0x0080u
#define CL_GBA_TIMER_CASCADE 0x0004u
#define CL_GBA_TIMER_PRESCALE_1024 0x0003u
#define CL_GBA_TIME_TICKS_PER_SECOND 16384u

uint16_t cl_gba_keys_held(void);
void cl_gba_wait_vblank(void);
void cl_gba_time_init(void);
uint32_t cl_gba_time_ticks(void);
void cl_gba_irq_init(void);
void cl_gba_irq_set_handler(uint16_t irq_mask, void (*handler)(void));
void cl_gba_irq_enable(uint16_t irq_mask);
void cl_gba_irq_disable(uint16_t irq_mask);

#ifdef __cplusplus
}
#endif

#endif
