#include "chislink/gba/sio.h"

#include "chislink/gba/hw.h"

#include <stddef.h>

/* --- Handler stack --- */
static cl_sio_handler_t g_handler_stack[CL_SIO_HANDLER_MAX_DEPTH];
static uint8_t g_handler_depth;
static uint32_t g_sio_idle_word;

/* --- IRQ rearm (shared across all handlers) --- */
static uint8_t g_sio_irq_rearm = 1u;
static void (*g_sio_direct_handler)(void);

static uint16_t sio_lock_irq(void) {
    uint16_t ime = CL_GBA_REG_IME;
    CL_GBA_REG_IME = 0;
    return ime;
}

static void sio_unlock_irq(uint16_t ime) {
    CL_GBA_REG_IME = ime;
}

int cl_sio_handler_push(const cl_sio_handler_t *handler) {
    if (!handler)
        return -1;

    cl_sio_handler_t new_handler = *handler;
    uint16_t ime = sio_lock_irq();
    if (g_handler_depth >= CL_SIO_HANDLER_MAX_DEPTH) {
        sio_unlock_irq(ime);
        return -1;
    }
    g_handler_stack[g_handler_depth++] = new_handler;
    int depth = (int)g_handler_depth;
    sio_unlock_irq(ime);

    if (new_handler.on_enter)
        new_handler.on_enter(new_handler.ctx);
    return depth;
}

int cl_sio_handler_pop(void) {
    uint16_t ime = sio_lock_irq();
    if (g_handler_depth == 0) {
        sio_unlock_irq(ime);
        return -1;
    }
    cl_sio_handler_t old_handler = g_handler_stack[g_handler_depth - 1u];
    g_handler_depth--;
    int depth = (int)g_handler_depth;
    sio_unlock_irq(ime);

    if (old_handler.on_leave)
        old_handler.on_leave(old_handler.ctx);
    return depth;
}

void cl_sio_handler_clear(void) {
    while (g_handler_depth != 0) {
        (void)cl_sio_handler_pop();
    }
}

void cl_sio_handler_replace(const cl_sio_handler_t *handler) {
    if (!handler) return;
    cl_sio_handler_t new_handler = *handler;
    cl_sio_handler_t old_handler = { 0 };
    uint8_t had_old = 0;

    uint16_t ime = sio_lock_irq();
    if (g_handler_depth == 0) {
        g_handler_stack[g_handler_depth++] = new_handler;
    } else {
        old_handler = g_handler_stack[g_handler_depth - 1u];
        had_old = 1u;
        g_handler_stack[g_handler_depth - 1u] = new_handler;
    }
    sio_unlock_irq(ime);

    if (had_old && old_handler.on_leave)
        old_handler.on_leave(old_handler.ctx);
    if (new_handler.on_enter)
        new_handler.on_enter(new_handler.ctx);
}

const cl_sio_handler_t *cl_sio_handler_current(void) {
    if (g_handler_depth == 0) return NULL;
    return &g_handler_stack[g_handler_depth - 1u];
}

int cl_sio_handler_depth(void) {
    return (int)g_handler_depth;
}

void cl_sio_handler_set_direct(void (*hw_handler)(void)) {
    uint16_t ime = sio_lock_irq();
    g_sio_direct_handler = hw_handler;
    cl_gba_irq_set_handler(CL_GBA_IRQ_SERIAL,
        hw_handler ? hw_handler : cl_gba_sio_irq_dispatch);
    sio_unlock_irq(ime);
}

void cl_sio_handler_restore_dispatch(void) {
    uint16_t ime = sio_lock_irq();
    g_sio_direct_handler = NULL;
    cl_gba_irq_set_handler(CL_GBA_IRQ_SERIAL, cl_gba_sio_irq_dispatch);
    sio_unlock_irq(ime);
}

void cl_gba_sio_set_idle_word(uint32_t idle_word) {
    g_sio_idle_word = idle_word;
}

/* --- Hardware control (IWRAM) --- */

void CL_GBA_IWRAM_CODE cl_gba_sio_send32(uint32_t out_word) {
    CL_GBA_REG_SIODATA32 = out_word;
    CL_GBA_REG_SIOCNT &= (uint16_t)~CL_GBA_SIO_SO_HIGH;
    CL_GBA_REG_SIOCNT |= CL_GBA_SIO_START;
    CL_GBA_REG_SIOCNT |= CL_GBA_SIO_SO_HIGH;
}

void CL_GBA_IWRAM_CODE cl_gba_sio_disarm(void) {
    CL_GBA_REG_SIOCNT &= (uint16_t)~CL_GBA_SIO_START;
    CL_GBA_REG_SIOCNT |= CL_GBA_SIO_SO_HIGH;
}

void CL_GBA_IWRAM_CODE cl_gba_sio_set_irq_rearm(uint8_t rearm) {
    g_sio_irq_rearm = rearm ? 1u : 0u;
}

void cl_gba_sio_init_slave32(void) {
    cl_gba_irq_init();
    CL_GBA_REG_RCNT = CL_GBA_RCNT_NORMAL;
    CL_GBA_REG_SIOCNT = CL_GBA_SIO_32BIT | CL_GBA_SIO_IRQ;
    cl_gba_irq_set_handler(CL_GBA_IRQ_SERIAL, cl_gba_sio_irq_dispatch);
    cl_gba_irq_enable(CL_GBA_IRQ_SERIAL);
    cl_gba_sio_disarm();
}

uint32_t CL_GBA_IWRAM_CODE cl_gba_sio_recv32(void) {
    return CL_GBA_REG_SIODATA32;
}

/* --- Top-level IRQ dispatch (IWRAM) --- */

void CL_GBA_IWRAM_CODE cl_gba_sio_irq_dispatch(void) {
    uint32_t in_word = CL_GBA_REG_SIODATA32;
    uint32_t out_word = g_sio_idle_word;
    g_sio_irq_rearm = 1u;

    /* Dispatch only to the active top handler. Keep this path predictable; code
     * that needs a faster path should replace the serial IRQ vector directly. */
    int d = (int)g_handler_depth;
    while (d > 0) {
        d--;
        const cl_sio_handler_t *h = &g_handler_stack[d];
        if (h->on_word) {
            out_word = h->on_word(in_word, h->ctx);
            break;  /* top handler processed it */
        }
    }

    if (g_sio_irq_rearm) {
        cl_gba_sio_send32(out_word);
    } else {
        CL_GBA_REG_SIODATA32 = out_word;
        cl_gba_sio_disarm();
    }
}

/* --- Built-in idle handler --- */

static uint32_t idle_on_word(uint32_t in_word, void *ctx) {
    (void)in_word;
    return *(uint32_t *)ctx;
}

int cl_sio_handler_push_idle(uint32_t idle_word) {
    g_sio_idle_word = idle_word;
    static uint32_t idle_copy;
    idle_copy = idle_word;
    cl_sio_handler_t h = { .on_word = idle_on_word, .ctx = &idle_copy };
    return cl_sio_handler_push(&h);
}

void cl_gba_sio_set_callback(uint32_t (*on_word)(uint32_t, void *), void *ctx) {
    cl_sio_handler_t h = { .on_word = on_word, .ctx = ctx };
    cl_sio_handler_replace(&h);
}
