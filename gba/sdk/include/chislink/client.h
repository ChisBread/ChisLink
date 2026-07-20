#ifndef CHISLINK_CLIENT_H
#define CHISLINK_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chislink/copy.h"
#include "chislink/proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file client.h
 * @brief Protocol transport client — initialisation, handshake, and capability
 *        negotiation over the ChisLink wire protocol.
 *
 * The client is the lowest-level public API.  It owns the protocol state
 * machine (offline → idle → busy → error) and sequence-number generation.
 * All higher-level subsystems (file, net, ble, stream) receive a pointer to
 * the same cl_client_t instance.
 *
 * ## Memory ownership
 * - The caller allocates cl_client_t (stack or static is fine).
 * - The caller provides cl_client_config_t with a transport callback
 *   (cl_transport_xfer32_t) that stays valid for the client's lifetime.
 * - No heap allocations are performed.
 *
 * ## Usage sketch
 * ```
 * cl_client_t client;
 * cl_client_config_t cfg = { .xfer32 = my_xfer32, .transport_user = NULL };
 * cl_client_init(&client, &cfg);
 * cl_client_hello(&client);    // handshake + capability exchange
 * // … use higher-level APIs that take &client …
 * ```
 */

/** Opaque word-exchange callback.  The transport sends *out_word* to the MCU
 *  and returns the word received from the MCU in the same exchange slot.
 *  Must return 0 on timeout (caller should then check the transport's
 *  timed_out flag). */
typedef uint32_t (*cl_transport_xfer32_t)(uint32_t out_word, void *user);

/** Payload size threshold where transports may switch from per-word xfer32 to
 *  a fixed-length data phase.  Small payloads stay on xfer32 to avoid the
 *  fixed IRQ-vector switching cost. */
#define CL_CLIENT_FAST_PAYLOAD_THRESHOLD 256u

/** Optional transport fast path for fixed-length MCU->GBA payloads.
 *  Returns 0 on success, negative on timeout/transport error. */
typedef int (*cl_transport_read_payload_t)(void *dst, uint32_t length,
                                           void *user);

/** Receives one little-endian payload word. valid_bytes is 1..4 for the final
 *  word and 4 otherwise. This supports destinations such as GBA VRAM that do
 *  not permit byte writes. */
typedef void (*cl_payload_store_word_t)(void *ctx,
                                        uint32_t offset,
                                        uint32_t word,
                                        uint32_t valid_bytes);

#ifndef CL_PAYLOAD_WRITER_T_DEFINED
#define CL_PAYLOAD_WRITER_T_DEFINED
typedef struct cl_payload_writer cl_payload_writer_t;
#endif

struct cl_payload_writer {
    cl_payload_store_word_t store_word;
    void *ctx;
};

/** Optional fixed-length MCU->GBA payload fast path using a caller-provided
 *  word writer instead of a byte-addressable destination. */
typedef int (*cl_transport_read_payload_writer_t)(
    const cl_payload_writer_t *writer,
    uint32_t length,
    void *user);

/** Optional transport fast path for fixed-length GBA->MCU payloads.
 *  Returns 0 on success, negative on timeout/transport error. */
typedef int (*cl_transport_write_payload_t)(const void *src, uint32_t length,
                                            void *user);

/** Optional transport fast path for fixed-length GBA->MCU payloads sourced
 *  from a direct window.  This lets ROM/SRAM mapped data stream without first
 *  copying into a byte buffer. */
typedef int (*cl_transport_write_window_payload_t)(
    const cl_direct_window_t *window,
    uint32_t offset,
    uint32_t length,
    void *user);

/** Client state machine. */
typedef enum cl_client_state {
    CL_CLIENT_OFFLINE = 0,  /**< Not yet connected / hello not performed. */
    CL_CLIENT_IDLE,         /**< Ready to accept commands. */
    CL_CLIENT_BUSY,         /**< Command in flight. */
    CL_CLIENT_ERROR,        /**< Last operation failed; check last_status. */
} cl_client_state_t;

/** Initialisation parameters for the client. */
typedef struct cl_client_config {
    /** Transport word-exchange function (mandatory). */
    cl_transport_xfer32_t xfer32;
    /** Optional fixed-length MCU->GBA payload fast path. */
    cl_transport_read_payload_t read_payload;
    /** Optional fixed-length MCU->GBA payload writer fast path. */
    cl_transport_read_payload_writer_t read_payload_writer;
    /** Optional fixed-length GBA->MCU payload fast path. */
    cl_transport_write_payload_t write_payload;
    /** Optional fixed-length direct-window GBA->MCU payload fast path. */
    cl_transport_write_window_payload_t write_window_payload;
    /** Opaque pointer passed through to xfer32. */
    void *transport_user;
} cl_client_config_t;

#ifndef CL_CLIENT_T_DEFINED
#define CL_CLIENT_T_DEFINED
typedef struct cl_client cl_client_t;
#endif

struct cl_client {
    cl_client_config_t config;
    cl_client_state_t state;     /**< Current protocol state. */
    uint16_t next_seq;           /**< Next outgoing sequence number. */
    uint16_t last_status;        /**< Protocol status from last response. */
    uint32_t caps;               /**< Server capability bitmask (CLP_CAP_*). */
};

/** Map a protocol status word to a negative errno-style value.
 *  CLP_STATUS_OK(0) → 0, CLP_STATUS_BUSY(1) → -11, etc. */
#define CL_CLIENT_STATUS_ERROR(status) (-(int)(status) - 10)

/** Initialise a client from a config.  Copies the config struct.  Returns
 *  true on success.  Client starts in CL_CLIENT_OFFLINE state. */
bool cl_client_init(cl_client_t *client, const cl_client_config_t *config);

/** Return the current client state. */
cl_client_state_t cl_client_state(const cl_client_t *client);

/** Poll the client: if offline, attempt a hello handshake.  Returns true
 *  when the client is idle (ready). */
bool cl_client_poll(cl_client_t *client);

/** Perform the HELLO / capability handshake with the MCU.
 *  Sends a 4-word header followed by 4 NOP words to read back the server's
 *  capability bits.  On success the client state becomes CL_CLIENT_IDLE and
 *  caps are populated.  Returns true on success. */
bool cl_client_hello(cl_client_t *client);

/** Explicitly re-request server capabilities (CLP_CTRL_CAPS command).
 *  Returns true on success.  Updates client->caps. */
bool cl_client_request_caps(cl_client_t *client);

/** Allocate and return the next outgoing sequence number.
 *  Advances internal counter; wraps from UINT16_MAX to 1 (0 is never used). */
uint16_t cl_client_next_seq(cl_client_t *client);

/** Return the most-recently-used sequence number (does not advance). */
uint16_t cl_client_current_seq(const cl_client_t *client);

/** Force the sequence counter.  Rarely needed; useful for re-sync. */
void cl_client_set_current_seq(cl_client_t *client, uint16_t seq);

/** Return the protocol status word from the last command response. */
uint16_t cl_client_last_status(const cl_client_t *client);

/** Convert a protocol status word to a negative error code.
 *  Uses CL_CLIENT_STATUS_ERROR internally. */
int cl_client_status_error(uint16_t status);

/** Return a human-readable name for a protocol status code (or "?"). */
const char *cl_client_status_name(uint16_t status);

/** Return the server capability bitmask (populated after a successful hello).
 *  See CLP_CAP_* in proto.h. */
uint32_t cl_client_caps(const cl_client_t *client);

/** Set the MCU's SPI poll interval at runtime.
 *  @param client  Initialised client (must have completed hello).
 *  @param ticks   FreeRTOS ticks between polls (1 = 1ms at 1000Hz).
 *                 Minimum 1, maximum 255.  Smaller = lower latency,
 *                 larger = lower CPU usage on the MCU.
 *  @return 0 on success, negative on error. */
int cl_client_set_poll_ticks(cl_client_t *client, uint8_t ticks);

#ifdef __cplusplus
}
#endif

#endif
