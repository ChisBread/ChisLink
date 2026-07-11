#ifndef CHISLINK_GBA_SIO_TRANSPORT_H
#define CHISLINK_GBA_SIO_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "chislink/client.h"
#include "chislink/gba/hw.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file gba_sio_transport.h
 * @brief GBA Serial I/O transport implementing cl_transport_xfer32_t over
 *        the GBA link port in 32-bit SPI slave mode.
 *
 * The transport is IRQ-driven: the GBA SIO interrupt fires on each completed
 * 32-bit word exchange.  The transport's IRQ handler stores the received word
 * and signals completion.  The synchronous xfer32 callback busy-waits (with a
 * tick-count timeout) for the IRQ to signal done.
 *
 * ## Initialisation
 * Use either cl_gba_sio_transport_init() (bare) or the convenience wrapper
 * cl_gba_sio_transport_init_client() which also initialises the client.
 *
 * ## IRQ integration
 * Call cl_gba_sio_transport_install() after init to register the transport's
 * IRQ handler with the SIO IRQ dispatch subsystem.
 */

/** Default timeout: 10 seconds in GBA timer ticks (~167.8 MHz / 2^14). */
#define CL_GBA_SIO_TRANSPORT_DEFAULT_TIMEOUT_TICKS \
    (CL_GBA_TIME_TICKS_PER_SECOND * 10u)

/** Transport state.  All fields marked volatile may be accessed from IRQ
 *  context. */
typedef struct cl_gba_sio_transport {
    volatile uint8_t waiting;     /**< 1 when xfer32 is waiting for IRQ. */
    volatile uint8_t done;        /**< 1 when IRQ has delivered a word. */
    volatile uint8_t timed_out;   /**< 1 when the tick timeout expired. */
    volatile uint32_t in_word;    /**< Word received from MCU. */
    volatile uint8_t fast_done;   /**< 1 when a fast payload phase completed. */
    volatile uint8_t fast_timed_out; /**< 1 when a fast payload phase timed out. */
    volatile uint32_t fast_offset; /**< Byte offset used by fast payload IRQ. */
    uint8_t *fast_rx_dst;         /**< Destination for fast MCU->GBA payload. */
    const uint8_t *fast_tx_src;   /**< Source for fast GBA->MCU payload. */
    const cl_direct_window_t *fast_tx_window; /**< Direct-window TX source. */
    uint32_t fast_tx_window_offset; /**< Base offset in fast_tx_window. */
    uint32_t fast_length;         /**< Exact payload length in bytes. */
    uint32_t fast_aligned;        /**< Payload length rounded up to 4 bytes. */
    uint32_t idle_word;           /**< Word sent while idle (typically NOP). */
    uint32_t timeout_ticks;       /**< Tick count before timeout. */
} cl_gba_sio_transport_t;

/** Initialise the transport struct.  Does not touch hardware or IRQs.
 *  @param transport  Pointer to caller-owned transport struct.
 *  @param client     Client (used for idle-word auto-config; may be NULL).
 *  @param idle_word  Word sent on the wire when no command is active.
 *  @param timeout_ticks  Timeout in GBA timer ticks.
 *  @return true on success. */
bool cl_gba_sio_transport_init(cl_gba_sio_transport_t *transport,
                               cl_client_t *client,
                               uint32_t idle_word,
                               uint32_t timeout_ticks);

/** Convenience: init transport AND client in one call.
 *  Configures the transport as the client's xfer32 callback.
 *  @return true if both initialisations succeeded. */
bool cl_gba_sio_transport_init_client(cl_gba_sio_transport_t *transport,
                                      cl_client_t *client,
                                      uint32_t idle_word,
                                      uint32_t timeout_ticks);

/** IRQ handler — call from the GBA SIO IRQ dispatch.  Stores in_word and
 *  sets done=1.  Disarms further SIO IRQs until the next xfer32 call.
 *  Placed in IWRAM for speed. */
uint32_t cl_gba_sio_transport_irq_word(uint32_t in_word, void *user)
    CL_GBA_IWRAM_CODE;

/** Register this transport as the active SIO IRQ handler.
 *  Must be called after cl_gba_sio_transport_init(). */
void cl_gba_sio_transport_install(cl_gba_sio_transport_t *transport);

/** Change the timeout after initialisation. */
void cl_gba_sio_transport_set_timeout(cl_gba_sio_transport_t *transport,
                                      uint32_t timeout_ticks);

/** Return 1 if the last xfer32 call timed out, 0 otherwise. */
uint8_t cl_gba_sio_transport_timed_out(
    const cl_gba_sio_transport_t *transport);

/** The actual cl_transport_xfer32_t implementation.
 *  Sends out_word, busy-waits for the IRQ response (with timeout), returns
 *  the received word.  Returns 0 on timeout — check timed_out() to
 *  distinguish a real zero word from a timeout. */
uint32_t cl_gba_sio_transport_xfer32(uint32_t out_word, void *user);

#ifdef __cplusplus
}
#endif

#endif
