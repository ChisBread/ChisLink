#include "chislink/gba_sio_transport.h"

#include "chislink/gba/sio.h"
#include "chislink/proto.h"

#include <stddef.h>
#include <stdint.h>

static cl_gba_sio_transport_t *g_fast_transport;

/* Keep the optional direct-payload IRQ path independently collectable. Bare
 * word-only users should retain only cl_gba_sio_transport_irq_word in IWRAM. */
#define CL_GBA_SIO_FAST_CODE \
    __attribute__((section(".iwram.fast"), long_call))

static uint32_t CL_GBA_SIO_FAST_CODE
fast_load_le32(const uint8_t *src, uint32_t available) {
    uint32_t word = 0;
    for (uint32_t i = 0; i < 4u && i < available; ++i) {
        word |= (uint32_t)src[i] << (i * 8u);
    }
    return word;
}

static void CL_GBA_SIO_FAST_CODE
fast_store_le32(uint8_t *dst, uint32_t word, uint32_t available) {
    for (uint32_t i = 0; i < 4u && i < available; ++i) {
        dst[i] = (uint8_t)(word >> (i * 8u));
    }
}

static uint32_t CL_GBA_SIO_FAST_CODE
fast_window_load_le32(const cl_direct_window_t *window,
                      uint32_t offset,
                      uint32_t available) {
    if (!window || !window->data || offset >= window->length) {
        return 0;
    }
    if (window->access == CL_DIRECT_WINDOW_ROM16_LE) {
        uintptr_t addr = (uintptr_t)(window->data + offset);
        if ((addr & 1u) == 0 && available >= 4u) {
            const volatile uint16_t *half = (const volatile uint16_t *)addr;
            return (uint32_t)half[0] | ((uint32_t)half[1] << 16u);
        }
    }

    uint32_t word = 0;
    for (uint32_t i = 0; i < 4u && i < available; ++i) {
        uint32_t at = offset + i;
        uint8_t byte = 0;
        if (window->access == CL_DIRECT_WINDOW_ROM16_LE) {
            uintptr_t addr = (uintptr_t)(window->data + at);
            const volatile uint16_t *half =
                (const volatile uint16_t *)(addr & ~(uintptr_t)1u);
            uint16_t value = *half;
            byte = (uint8_t)((addr & 1u) ? (value >> 8u) : value);
        } else {
            byte = window->data[at];
        }
        word |= (uint32_t)byte << (i * 8u);
    }
    return word;
}

static uint16_t CL_GBA_SIO_FAST_CODE fast_irq_lock(void) {
    uint16_t ime = CL_GBA_REG_IME;
    CL_GBA_REG_IME = 0;
    return ime;
}

static void CL_GBA_SIO_FAST_CODE fast_irq_unlock(uint16_t ime) {
    CL_GBA_REG_IME = ime;
}

static void CL_GBA_SIO_FAST_CODE
fast_disarm_idle(cl_gba_sio_transport_t *transport) {
    CL_GBA_REG_SIODATA32 = transport ? transport->idle_word : 0;
    cl_gba_sio_disarm();
}

static void
fast_restore_dispatch(cl_gba_sio_transport_t *transport) {
    g_fast_transport = 0;
    cl_sio_handler_restore_dispatch();
    fast_disarm_idle(transport);
}

static void CL_GBA_SIO_FAST_CODE fast_finish_irq(void) {
    cl_gba_sio_transport_t *transport = g_fast_transport;
    if (transport) {
        transport->fast_done = 1u;
        fast_disarm_idle(transport);
    }
}

static void CL_GBA_SIO_FAST_CODE fast_rx_irq(void) {
    cl_gba_sio_transport_t *transport = g_fast_transport;
    if (!transport) {
        fast_disarm_idle(0);
        return;
    }

    uint32_t offset = transport->fast_offset;
    uint32_t word = CL_GBA_REG_SIODATA32;
    if (offset < transport->fast_length) {
        uint32_t remaining = transport->fast_length - offset;
        uint32_t valid = remaining > 4u ? 4u : remaining;
        if (transport->fast_rx_store_word) {
            transport->fast_rx_store_word(transport->fast_rx_store_ctx,
                                          offset, word, valid);
        } else {
            fast_store_le32(transport->fast_rx_dst + offset, word, valid);
        }
    }

    offset += 4u;
    transport->fast_offset = offset;
    if (offset >= transport->fast_aligned) {
        fast_finish_irq();
        return;
    }

    cl_gba_sio_send32(transport->idle_word);
}

static void CL_GBA_SIO_FAST_CODE fast_tx_send_next(
    cl_gba_sio_transport_t *transport) {
    uint32_t offset = transport->fast_offset;
    uint32_t remaining = transport->fast_length > offset ?
                         transport->fast_length - offset : 0u;
    uint32_t word = fast_load_le32(transport->fast_tx_src + offset, remaining);
    transport->fast_offset = offset + 4u;
    cl_gba_sio_send32(word);
}

static void CL_GBA_SIO_FAST_CODE fast_window_tx_send_next(
    cl_gba_sio_transport_t *transport) {
    uint32_t offset = transport->fast_offset;
    uint32_t remaining = transport->fast_length > offset ?
                         transport->fast_length - offset : 0u;
    uint32_t word = fast_window_load_le32(transport->fast_tx_window,
                                          transport->fast_tx_window_offset + offset,
                                          remaining);
    transport->fast_offset = offset + 4u;
    cl_gba_sio_send32(word);
}

static void CL_GBA_SIO_FAST_CODE fast_tx_irq(void) {
    cl_gba_sio_transport_t *transport = g_fast_transport;
    if (!transport) {
        fast_disarm_idle(0);
        return;
    }

    if (transport->fast_offset >= transport->fast_aligned) {
        fast_finish_irq();
        return;
    }
    fast_tx_send_next(transport);
}

static void CL_GBA_SIO_FAST_CODE fast_window_tx_irq(void) {
    cl_gba_sio_transport_t *transport = g_fast_transport;
    if (!transport) {
        fast_disarm_idle(0);
        return;
    }

    if (transport->fast_offset >= transport->fast_aligned) {
        fast_finish_irq();
        return;
    }
    fast_window_tx_send_next(transport);
}

static int fast_wait(cl_gba_sio_transport_t *transport) {
    uint32_t start_ticks = cl_gba_time_ticks();
    while (!transport->fast_done) {
        if (transport->timeout_ticks &&
            cl_gba_time_ticks() - start_ticks >= transport->timeout_ticks) {
            transport->fast_timed_out = 1u;
            transport->timed_out = 1u;
            uint16_t ime = fast_irq_lock();
            fast_restore_dispatch(transport);
            fast_irq_unlock(ime);
            return -2;
        }
    }
    uint16_t ime = fast_irq_lock();
    if (g_fast_transport == transport) {
        fast_restore_dispatch(transport);
    }
    fast_irq_unlock(ime);
    return transport->fast_timed_out ? -2 : 0;
}

static int cl_gba_sio_transport_read_payload(void *dst, uint32_t length,
                                             void *user) {
    cl_gba_sio_transport_t *transport = (cl_gba_sio_transport_t *)user;
    if (!transport || (length && !dst)) {
        return -1;
    }
    if (!length) {
        return 0;
    }

    uint16_t ime = fast_irq_lock();
    transport->fast_done = 0u;
    transport->fast_timed_out = 0u;
    transport->timed_out = 0u;
    transport->fast_offset = 0u;
    transport->fast_rx_dst = (uint8_t *)dst;
    transport->fast_rx_store_word = 0;
    transport->fast_rx_store_ctx = 0;
    transport->fast_tx_src = 0;
    transport->fast_tx_window = 0;
    transport->fast_tx_window_offset = 0;
    transport->fast_length = length;
    transport->fast_aligned = (uint32_t)clp_aligned_length(length);
    g_fast_transport = transport;
    cl_sio_handler_set_direct(fast_rx_irq);
    fast_irq_unlock(ime);

    cl_gba_sio_send32(transport->idle_word);
    return fast_wait(transport);
}

static int cl_gba_sio_transport_read_payload_writer(
    const cl_payload_writer_t *writer,
    uint32_t length,
    void *user) {
    cl_gba_sio_transport_t *transport = (cl_gba_sio_transport_t *)user;
    if (!transport || !writer || !writer->store_word) {
        return -1;
    }
    if (!length) {
        return 0;
    }

    uint16_t ime = fast_irq_lock();
    transport->fast_done = 0u;
    transport->fast_timed_out = 0u;
    transport->timed_out = 0u;
    transport->fast_offset = 0u;
    transport->fast_rx_dst = 0;
    transport->fast_rx_store_word = writer->store_word;
    transport->fast_rx_store_ctx = writer->ctx;
    transport->fast_tx_src = 0;
    transport->fast_tx_window = 0;
    transport->fast_tx_window_offset = 0;
    transport->fast_length = length;
    transport->fast_aligned = (uint32_t)clp_aligned_length(length);
    g_fast_transport = transport;
    cl_sio_handler_set_direct(fast_rx_irq);
    fast_irq_unlock(ime);

    cl_gba_sio_send32(transport->idle_word);
    return fast_wait(transport);
}

static int cl_gba_sio_transport_write_payload(const void *src, uint32_t length,
                                              void *user) {
    cl_gba_sio_transport_t *transport = (cl_gba_sio_transport_t *)user;
    if (!transport || (length && !src)) {
        return -1;
    }
    if (!length) {
        return 0;
    }

    uint16_t ime = fast_irq_lock();
    transport->fast_done = 0u;
    transport->fast_timed_out = 0u;
    transport->timed_out = 0u;
    transport->fast_offset = 0u;
    transport->fast_rx_dst = 0;
    transport->fast_rx_store_word = 0;
    transport->fast_rx_store_ctx = 0;
    transport->fast_tx_src = (const uint8_t *)src;
    transport->fast_tx_window = 0;
    transport->fast_tx_window_offset = 0;
    transport->fast_length = length;
    transport->fast_aligned = (uint32_t)clp_aligned_length(length);
    g_fast_transport = transport;
    cl_sio_handler_set_direct(fast_tx_irq);
    fast_tx_send_next(transport);
    fast_irq_unlock(ime);

    return fast_wait(transport);
}

static int cl_gba_sio_transport_write_window_payload(
    const cl_direct_window_t *window,
    uint32_t offset,
    uint32_t length,
    void *user) {
    cl_gba_sio_transport_t *transport = (cl_gba_sio_transport_t *)user;
    if (!transport || !window || (length && !window->data) ||
        offset > window->length || length > window->length - offset) {
        return -1;
    }
    if (!length) {
        return 0;
    }

    uint16_t ime = fast_irq_lock();
    transport->fast_done = 0u;
    transport->fast_timed_out = 0u;
    transport->timed_out = 0u;
    transport->fast_offset = 0u;
    transport->fast_rx_dst = 0;
    transport->fast_rx_store_word = 0;
    transport->fast_rx_store_ctx = 0;
    transport->fast_tx_src = 0;
    transport->fast_tx_window = window;
    transport->fast_tx_window_offset = offset;
    transport->fast_length = length;
    transport->fast_aligned = (uint32_t)clp_aligned_length(length);
    g_fast_transport = transport;
    cl_sio_handler_set_direct(fast_window_tx_irq);
    fast_window_tx_send_next(transport);
    fast_irq_unlock(ime);

    return fast_wait(transport);
}

/* --- Transport IRQ handler (IWRAM) --- */

uint32_t CL_GBA_IWRAM_CODE
cl_gba_sio_transport_irq_word(uint32_t in_word, void *user) {
    cl_gba_sio_transport_t *transport = (cl_gba_sio_transport_t *)user;
    if (!transport) {
        cl_gba_sio_set_irq_rearm(0);
        return 0;
    }
    if (!transport->waiting) {
        return transport->idle_word;
    }
    transport->in_word = in_word;
    transport->done = 1u;
    transport->waiting = 0u;
    cl_gba_sio_set_irq_rearm(0);
    return transport->idle_word;
}

/* --- Initialisation --- */

bool cl_gba_sio_transport_init(cl_gba_sio_transport_t *transport,
                               cl_client_t *client,
                               uint32_t idle_word,
                               uint32_t timeout_ticks) {
    if (!transport) return false;
    transport->waiting = 0u;
    transport->done = 0u;
    transport->timed_out = 0u;
    transport->fast_done = 0u;
    transport->fast_timed_out = 0u;
    transport->fast_offset = 0u;
    transport->fast_rx_dst = 0;
    transport->fast_rx_store_word = 0;
    transport->fast_rx_store_ctx = 0;
    transport->fast_tx_src = 0;
    transport->fast_tx_window = 0;
    transport->fast_tx_window_offset = 0;
    transport->fast_length = 0;
    transport->fast_aligned = 0;
    transport->in_word = 0;
    transport->idle_word = idle_word;
    transport->timeout_ticks = timeout_ticks;
    (void)client;
    return true;
}

bool cl_gba_sio_transport_init_client(cl_gba_sio_transport_t *transport,
                                      cl_client_t *client,
                                      uint32_t idle_word,
                                      uint32_t timeout_ticks) {
    if (!transport || !client) return false;
    if (!cl_gba_sio_transport_init(transport, client, idle_word, timeout_ticks))
        return false;
    cl_client_config_t cfg = {
        .xfer32 = cl_gba_sio_transport_xfer32,
        .read_payload = cl_gba_sio_transport_read_payload,
        .read_payload_writer = cl_gba_sio_transport_read_payload_writer,
        .write_payload = cl_gba_sio_transport_write_payload,
        .write_window_payload = cl_gba_sio_transport_write_window_payload,
        .transport_user = transport,
    };
    return cl_client_init(client, &cfg);
}

void cl_gba_sio_transport_install(cl_gba_sio_transport_t *transport) {
    if (!transport) return;
    cl_gba_sio_init_slave32();
    cl_sio_handler_clear();
    cl_gba_sio_set_idle_word(transport->idle_word);
    cl_sio_handler_t h = {
        .on_word = cl_gba_sio_transport_irq_word,
        .ctx = transport,
    };
    cl_sio_handler_push(&h);
}

/* --- Synchronous xfer32 (IWRAM-friendly busy-wait) --- */

uint32_t cl_gba_sio_transport_xfer32(uint32_t out_word, void *user) {
    cl_gba_sio_transport_t *transport = (cl_gba_sio_transport_t *)user;
    if (!transport) return 0;

    transport->done = 0u;
    transport->timed_out = 0u;
    transport->waiting = 1u;
    cl_gba_sio_set_irq_rearm(1u);
    cl_gba_sio_send32(out_word);

    uint32_t start_ticks = cl_gba_time_ticks();
    while (!transport->done) {
        if (transport->timeout_ticks &&
            cl_gba_time_ticks() - start_ticks >= transport->timeout_ticks) {
            transport->timed_out = 1u;
            transport->waiting = 0u;
            cl_gba_sio_set_irq_rearm(0u);
            cl_gba_sio_disarm();
            return 0;
        }
    }
    return transport->in_word;
}

void cl_gba_sio_transport_set_timeout(cl_gba_sio_transport_t *transport,
                                      uint32_t timeout_ticks) {
    if (transport) transport->timeout_ticks = timeout_ticks;
}

uint8_t cl_gba_sio_transport_timed_out(const cl_gba_sio_transport_t *transport) {
    return transport ? transport->timed_out : 0u;
}
