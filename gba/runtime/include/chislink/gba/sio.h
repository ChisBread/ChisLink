#ifndef CHISLINK_GBA_SIO_H
#define CHISLINK_GBA_SIO_H

#include <stdint.h>

#include "chislink/gba/hw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Handler stack ---
 *
 * Each handler is a self-contained IRQ state. Push a handler to activate
 * it; pop to restore the previous one. The top handler's on_word() is
 * called on every SPI word from IRQ context.
 *
 * Default stack:
 *   [cl_sio_idle_handler]  — returns NOP, keeps link alive
 *
 * Default manager/game SDK stack:
 *   [transport_handler]
 *
 * During flash programming:
 *   [flash_handler]
 *
 * Direct vector bypass (cl_sio_handler_set_direct) replaces the entire
 * IRQ vector with a naked function — the stack is not traversed.  Use
 * only for extreme performance (flash payload DMA).
 */

#define CL_SIO_HANDLER_MAX_DEPTH 4

typedef struct cl_sio_handler {
    uint32_t (*on_word)(uint32_t in_word, void *ctx);
    void     (*on_enter)(void *ctx);
    void     (*on_leave)(void *ctx);
    void     *ctx;
} cl_sio_handler_t;

/* Push a handler onto the stack.  Calls on_enter() if non-NULL.
 * Returns new depth (>=1) on success, -1 if stack full. */
int cl_sio_handler_push(const cl_sio_handler_t *handler);

/* Pop the top handler.  Calls on_leave() if non-NULL.
 * Returns new depth on success, -1 if stack is already at root. */
int cl_sio_handler_pop(void);

/* Clear the handler stack, calling on_leave() from top to bottom. */
void cl_sio_handler_clear(void);

/* Replace the top handler without pushing (no save). */
void cl_sio_handler_replace(const cl_sio_handler_t *handler);

/* Query the current handler and depth. */
const cl_sio_handler_t *cl_sio_handler_current(void);
int cl_sio_handler_depth(void);

/* Direct hardware-vector bypass.  Replaces the IRQ vector entirely —
 * the handler stack is NOT walked.  Use restore_dispatch() to return
 * to normal stack-based dispatch. */
void cl_sio_handler_set_direct(void (*hw_handler)(void));
void cl_sio_handler_restore_dispatch(void);

/* --- Hardware control (IWRAM for speed) --- */

void cl_gba_sio_init_slave32(void);
void cl_gba_sio_send32(uint32_t out_word) CL_GBA_IWRAM_CODE;
void cl_gba_sio_disarm(void) CL_GBA_IWRAM_CODE;
void cl_gba_sio_set_irq_rearm(uint8_t rearm) CL_GBA_IWRAM_CODE;
uint32_t cl_gba_sio_recv32(void) CL_GBA_IWRAM_CODE;
void cl_gba_sio_irq_dispatch(void) CL_GBA_IWRAM_CODE;

/* Set the idle word sent when the stack is empty. */
void cl_gba_sio_set_idle_word(uint32_t idle_word);

/* Convenience: push the built-in idle handler (returns idle_word, keeps
 * link alive).  Typically called once at boot. */
int cl_sio_handler_push_idle(uint32_t idle_word);

/* Backward-compatible shim: set a single on_word callback as the top
 * handler (replaces current, no on_enter/on_leave).  For migration from
 * the old g_sio_callback API. */
void cl_gba_sio_set_callback(uint32_t (*on_word)(uint32_t, void *), void *ctx);

#ifdef __cplusplus
}
#endif

#endif
