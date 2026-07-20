#ifndef CHISLINK_STREAM_H
#define CHISLINK_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "chislink/proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file stream.h
 * @brief Stream subscriptions — producer-consumer ring buffer for
 *        receiving (and optionally sending) streaming data over the
 *        ChisLink protocol.
 *
 * A stream is an open channel to the MCU for transferring a large
 * sequential data flow (file download, socket data, BLE notification
 * stream) with flow control and credit management.
 *
 * ## Architecture
 *
 * The stream object owns a ring buffer divided into fixed-size **slots**.
 * Two roles share the same buffer:
 * - **Producer** (network → buffer): The MCU pushes data into slots via
 *   cl_stream_recv_slot().  The caller acquires a free slot with
 *   cl_stream_producer_acquire(), fills it from the network, then commits
 *   with cl_stream_producer_commit().
 * - **Consumer** (buffer → application): The application pulls data out
 *   with cl_stream_consumer_peek() and releases consumed bytes with
 *   cl_stream_consumer_release_bytes().
 *
 * Credit management (cl_stream_credit()) tells the MCU how many bytes have
 * been consumed so it can send more.  This prevents buffer overrun.
 *
 * ## Lifecycle
 * 1. cl_stream_init() — initialise the struct.
 * 2. cl_stream_open() or cl_stream_subscribe_file() — open a stream.
 * 3. Loop: cl_stream_recv_slot() → cl_stream_consumer_peek() →
 *    cl_stream_consumer_release_bytes() → cl_stream_credit().
 * 4. cl_stream_close() or cl_stream_pump() — finish.
 *
 * ## Memory ownership
 * - **All memory is caller-owned.**  The buffer, slots array, and stream
 *   struct are allocated by the caller (stack or static is fine).
 * - Use CL_STREAM_BUFFER() and CL_STREAM_SLOTS() macros for correctly
 *   aligned static allocations.
 * - Use CL_STREAM_CONFIG() to build the initialisation struct.
 * - The buffer and slots must stay valid for the stream's lifetime.
 * - No heap allocations.
 *
 * ## Buffer sizing
 * - Total buffer = slot_count × slot_size.
 * - slot_size should match your expected network chunk size.
 * - slot_count ≥ 2 for pipelining (one being received, one being consumed).
 * - For high-throughput file downloads, use larger buffers (e.g. 4 slots ×
 *   8 KiB = 32 KiB).
 *
 * ## Synchronous vs async
 * All cl_stream_* functions that talk to the MCU (open, close, recv_slot,
 * credit, seek, send, pump) are synchronous blocking calls.  The ring-buffer
 * operations (producer_acquire, consumer_peek, etc.) are local-only and do
 * not touch the transport.
 */

#ifndef CL_CLIENT_T_DEFINED
#define CL_CLIENT_T_DEFINED
typedef struct cl_client cl_client_t;
#endif

#ifndef CL_PAYLOAD_WRITER_T_DEFINED
#define CL_PAYLOAD_WRITER_T_DEFINED
typedef struct cl_payload_writer cl_payload_writer_t;
#endif

/** Alignment attribute for stream buffers (matches protocol alignment). */
#define CL_STREAM_ALIGN __attribute__((aligned(CLP_DEFAULT_ALIGNMENT)))

/** Declare a correctly-aligned stream buffer. */
#define CL_STREAM_BUFFER(name, size) \
    uint8_t name[(size)] CL_STREAM_ALIGN

/** Declare a stream slot array. */
#define CL_STREAM_SLOTS(name, count) \
    cl_stream_slot_t name[(count)]

/** Count elements in an array. */
#define CL_STREAM_ARRAY_COUNT(array) \
    (sizeof(array) / sizeof((array)[0]))

/** Build a cl_stream_config_t initialiser from statically-allocated arrays.
 *  Usage:
 *  ```
 *  CL_STREAM_BUFFER(buf, 32768);
 *  CL_STREAM_SLOTS(slots, 4);
 *  cl_stream_config_t cfg = CL_STREAM_CONFIG(buf, slots, 8192, 0, CL_STREAM_PROFILE_BALANCED);
 *  cl_stream_init(&stream, &cfg);
 *  ``` */
#define CL_STREAM_CONFIG(buffer_array, slot_array, slot_bytes, stream_flags, \
                         stream_profile) \
    { \
        .buffer = (buffer_array), \
        .buffer_size = sizeof(buffer_array), \
        .slots = (slot_array), \
        .slot_count = (uint8_t)CL_STREAM_ARRAY_COUNT(slot_array), \
        .slot_size = (slot_bytes), \
        .flags = (stream_flags), \
        .profile = (stream_profile), \
    }

/** Stream state machine. */
typedef enum cl_stream_state {
    CL_STREAM_CLOSED = 0,   /**< Not open. */
    CL_STREAM_IDLE,         /**< Initialised but not opened. */
    CL_STREAM_OPEN,         /**< Open and active. */
    CL_STREAM_PAUSED,       /**< Paused (reserved for future use). */
    CL_STREAM_ERROR,        /**< Fatal error — close and re-open needed. */
} cl_stream_state_t;

/** Stream performance profile (hint passed to MCU). */
typedef enum cl_stream_profile {
    CL_STREAM_PROFILE_BALANCED = 0,       /**< Default. */
    CL_STREAM_PROFILE_LOW_LATENCY,        /**< Prefer smaller, faster chunks. */
    CL_STREAM_PROFILE_HIGH_THROUGHPUT,    /**< Prefer larger chunks. */
} cl_stream_profile_t;

/** Error codes returned by ring-buffer operations. */
typedef enum cl_stream_result {
    CL_STREAM_OK = 0,              /**< Success. */
    CL_STREAM_ERR_INVALID = -1,    /**< Bad argument or state. */
    CL_STREAM_ERR_FULL = -2,       /**< No free slots for producer. */
    CL_STREAM_ERR_EMPTY = -3,      /**< No ready data for consumer. */
    CL_STREAM_ERR_TOO_LARGE = -4,  /**< Commit length exceeds slot size. */
} cl_stream_result_t;

/** One slot in the ring buffer.  All fields are volatile (may be updated
 *  by producer/consumer in any order). */
typedef struct cl_stream_slot {
    volatile uint32_t offset;   /**< Absolute byte offset in the logical stream. */
    volatile uint16_t length;   /**< Valid bytes in this slot. */
    volatile uint16_t flags;    /**< Slot flags (READY, CONSUMED). */
} cl_stream_slot_t;

/** Initialisation parameters for a stream. */
typedef struct cl_stream_config {
    uint8_t *buffer;            /**< Caller-owned data buffer. */
    uint32_t buffer_size;       /**< Total size of buffer in bytes. */
    cl_stream_slot_t *slots;    /**< Caller-owned slot array. */
    uint8_t slot_count;         /**< Number of slots. */
    uint16_t slot_size;         /**< Bytes per slot. */
    uint32_t flags;             /**< Stream flags. */
    uint8_t profile;            /**< cl_stream_profile_t hint. */
} cl_stream_config_t;

/** Remote stream status (returned by cl_stream_poll). */
typedef struct cl_stream_remote_status {
    uint32_t available;    /**< Bytes available at the source. */
    uint32_t offset;       /**< Current remote offset. */
    uint32_t flags;        /**< Stream status flags. */
    uint32_t last_error;   /**< Last remote error code. */
} cl_stream_remote_status_t;

/** Descriptor for a free slot available to the producer. */
typedef struct cl_stream_span {
    uint8_t *data;         /**< Start of writable buffer space. */
    uint16_t capacity;     /**< Bytes available in this span. */
    uint8_t slot_index;    /**< Slot index (for commit_at). */
    uint8_t reserved;      /**< Reserved (padding). */
} cl_stream_span_t;

/** Descriptor for a readable data region available to the consumer. */
typedef struct cl_stream_view {
    uint8_t *data;         /**< Start of readable data. */
    uint32_t offset;       /**< Absolute byte offset in the logical stream. */
    uint16_t length;       /**< Bytes available in this view. */
    uint16_t flags;        /**< Slot flags. */
    uint8_t slot_index;    /**< Slot index. */
    uint8_t reserved[3];   /**< Reserved (padding). */
} cl_stream_view_t;

/** Stream instance — the caller owns this entire struct. */
typedef struct cl_stream {
    uint8_t *buffer;              /**< Caller-owned data buffer. */
    uint32_t buffer_size;         /**< Total buffer size. */
    cl_stream_slot_t *slots;      /**< Caller-owned slot array. */
    uint32_t flags;               /**< Stream flags. */
    uint16_t slot_size;           /**< Bytes per slot. */
    uint8_t slot_count;           /**< Number of slots. */
    volatile uint8_t read_slot;   /**< Next slot index for consumer. */
    volatile uint8_t write_slot;  /**< Next slot index for producer. */
    volatile uint8_t ready_slots; /**< Number of slots with data ready. */
    volatile uint8_t state;       /**< cl_stream_state_t. */
    volatile uint16_t read_offset;/**< Byte offset within current read slot. */
    uint8_t profile;              /**< cl_stream_profile_t. */
    uint8_t stream_id;            /**< MCU-assigned stream ID (0 = closed). */
    uint8_t reserved;             /**< Reserved (padding). */
    volatile uint32_t rx_offset;  /**< Total bytes received (consumer position). */
    volatile uint32_t tx_offset;  /**< Total bytes sent (producer position). */
    uint32_t remote_size_low;     /**< Remote file size, low 32 bits. */
    uint32_t remote_size_high;    /**< Remote file size, high 32 bits. */
    volatile int last_error;      /**< Last error code. */
} cl_stream_t;

/* --- Lifecycle --- */

/** Initialise a stream struct from a config.
 *  Config is not copied — the referenced buffers must stay alive.
 *  @return true on success, false on invalid config. */
bool cl_stream_init(cl_stream_t *stream, const cl_stream_config_t *config);

/** Reset a stream to its initial state.  Discards all buffered data.
 *  Does not close the stream on the MCU side. */
void cl_stream_reset(cl_stream_t *stream);

/** Open a stream to the MCU.
 *  @param kind    CLP_STREAM_KIND_FILE, SOCKET, BLE_NOTIFY, or DEVICE
 *                 (note: only FILE is currently implemented on the MCU).
 *  @param target  Path or identifier string for the stream source.
 *  @return 0 on success, negative on error.  On success, stream->stream_id
 *          is set and state becomes CL_STREAM_OPEN. */
int cl_stream_open(cl_client_t *client,
                   cl_stream_t *stream,
                   uint8_t kind,
                   const char *target);

/** Convenience: open a file subscription stream.
 *  Equivalent to cl_stream_open() with kind=CLP_STREAM_KIND_FILE. */
int cl_stream_subscribe_file(cl_client_t *client,
                             cl_stream_t *stream,
                             const char *path);

/** Close a stream.  Frees the MCU-side stream ID.
 *  @return 0 on success, negative on error.  State becomes CL_STREAM_CLOSED
 *          or CL_STREAM_ERROR. */
int cl_stream_close(cl_client_t *client, cl_stream_t *stream);

/** Poll remote stream status (bytes available, offset, flags, errors). */
int cl_stream_poll(cl_client_t *client,
                   cl_stream_t *stream,
                   cl_stream_remote_status_t *out_status);

/** Send credit to the MCU — tells it how many bytes have been consumed
 *  so it can send more.  Use after releasing consumed data.
 *  @param consumed_bytes  Total bytes consumed since stream open. */
int cl_stream_credit(cl_client_t *client,
                     cl_stream_t *stream,
                     uint32_t consumed_bytes);

/** Seek to an absolute byte offset.  Resets the local buffer (discards
 *  all unread data).
 *  @param offset  Target byte offset (0-based).
 *  @return 0 on success, negative on error. */
int cl_stream_seek(cl_client_t *client, cl_stream_t *stream, uint64_t offset);

/* --- Producer side (MCU → buffer) --- */

/** Fetch one slot of data from the MCU into the ring buffer.
 *  Acquires a free producer slot, sends a RECV command, reads the response
 *  payload directly into the slot buffer, then commits.
 *  @return Bytes received (>0), 0 (no data available), or negative on error. */
int cl_stream_recv_slot(cl_client_t *client, cl_stream_t *stream);

/** Receive directly into a caller-owned destination without using the ring.
 *  The requested length must not exceed the slot size negotiated at open.
 *  This is useful when the destination is already an application cache slot.
 *  @param out_length  Set to bytes received.
 *  @return 0 on success, negative on error. */
int cl_stream_recv_into(cl_client_t *client,
                        cl_stream_t *stream,
                        void *dst,
                        uint32_t length,
                        uint32_t *out_length);

/** Direct receive variant for destinations that require word-width stores,
 *  such as GBA VRAM. */
int cl_stream_recv_with_writer(cl_client_t *client,
                               cl_stream_t *stream,
                               const cl_payload_writer_t *writer,
                               uint32_t length,
                               uint32_t *out_length);

/* --- Consumer side (buffer → application) --- */

/** Read data from the stream into a caller buffer.
 *  Pulls data from ready consumer slots, automatically calling
 *  cl_stream_recv_slot() when more data is needed.
 *  @param out_length  Set to total bytes read (≤ length).
 *  @return 0 on success, negative on error.  Fewer bytes than requested
 *          does not indicate an error — check *out_length. */
int cl_stream_read(cl_client_t *client,
                   cl_stream_t *stream,
                   void *dst,
                   uint32_t length,
                   uint32_t *out_length);

/** Read exactly length bytes, blocking until all are received or an error
 *  occurs.
 *  @return 0 on success, -3 on short read (stream ended early),
 *          or negative on error. */
int cl_stream_read_exact(cl_client_t *client,
                         cl_stream_t *stream,
                         void *dst,
                         uint32_t length);

/** Discard up to length bytes from the stream.
 *  @param out_length  Set to bytes actually discarded.
 *  @return 0 on success, negative on error. */
int cl_stream_discard(cl_client_t *client,
                      cl_stream_t *stream,
                      uint32_t length,
                      uint32_t *out_length);

/* --- Producer side (application → MCU) --- */

/** Send data from the application to the MCU.
 *  @param data    Data to send.
 *  @param length  Bytes to send (max CLP_DEFAULT_BLOCK_SIZE - header).
 *  @return Bytes actually written (may be less than length), or negative
 *          on error.
 *  @note The MCU currently returns UNSUPPORTED for stream SEND. */
int cl_stream_send(cl_client_t *client,
                   cl_stream_t *stream,
                   const void *data,
                   uint32_t length);

/** Request the MCU to pump (flush) all buffered stream data to its final
 *  destination.  Always marks the stream as closed on the client side,
 *  regardless of the MCU response.
 *  @return 0 on success, negative on error. */
int cl_stream_pump(cl_client_t *client, cl_stream_t *stream);

/* --- Ring buffer introspection --- */

/** Total ring buffer capacity in bytes. */
uint32_t cl_stream_capacity(const cl_stream_t *stream);

/** Capacity of a single slot in bytes (same as slot_size). */
uint32_t cl_stream_slot_capacity(const cl_stream_t *stream);

/** Number of slots currently holding ready data. */
uint8_t cl_stream_ready_count(const cl_stream_t *stream);

/** Number of free slots available for the producer. */
uint8_t cl_stream_free_count(const cl_stream_t *stream);

/** True if the stream has space for at least one more recv_slot. */
bool cl_stream_can_receive(const cl_stream_t *stream);

/* --- Low-level ring-buffer operations --- */

/** Acquire a free slot for the producer to write into.
 *  @param out_span  Set to the writable span on success.
 *  @return CL_STREAM_OK, CL_STREAM_ERR_FULL, or CL_STREAM_ERR_INVALID. */
int cl_stream_producer_acquire(cl_stream_t *stream, cl_stream_span_t *out_span);

/** Commit a filled slot to make it available to the consumer.
 *  @param length  Bytes written to the slot (must be ≤ slot_size, > 0).
 *  @note length == 0 returns CL_STREAM_ERR_TOO_LARGE (not OK). */
int cl_stream_producer_commit(cl_stream_t *stream, uint16_t length);

/** Commit a slot at a specific absolute stream offset (for out-of-order
 *  or sparse writes).
 *  @param offset  Absolute byte offset in the logical stream. */
int cl_stream_producer_commit_at(cl_stream_t *stream,
                                 uint32_t offset,
                                 uint16_t length);

/** Peek at the next readable data from the consumer side.
 *  @param out_view  Set to the readable view on success.
 *  @return CL_STREAM_OK, CL_STREAM_ERR_EMPTY, or CL_STREAM_ERR_INVALID. */
int cl_stream_consumer_peek(const cl_stream_t *stream,
                            cl_stream_view_t *out_view);

/** Release the entire current read slot (marks it as consumed). */
int cl_stream_consumer_release(cl_stream_t *stream);

/** Release a partial amount of data from the current read slot.
 *  @param length  Bytes to mark consumed (must be ≤ view.length). */
int cl_stream_consumer_release_bytes(cl_stream_t *stream, uint16_t length);

/* --- Convenience wrappers --- */

/** Get a pointer to the producer's next free write location.
 *  Equivalent to producer_acquire but returns the raw pointer. */
uint8_t *cl_stream_receive_ptr(cl_stream_t *stream, uint16_t *out_capacity);

/** Commit after using cl_stream_receive_ptr(). */
int cl_stream_commit_receive(cl_stream_t *stream, uint16_t length);

/** Commit at a specific offset after using receive_ptr. */
int cl_stream_commit_receive_at(cl_stream_t *stream,
                                uint32_t offset,
                                uint16_t length);

/** Peek at the consumer's next readable data (raw pointer variant). */
const uint8_t *cl_stream_peek(const cl_stream_t *stream,
                              uint16_t *out_length,
                              uint32_t *out_offset);

/** Consume the current peeked slot (equivalent to consumer_release). */
int cl_stream_consume(cl_stream_t *stream);

/** Map a stream profile enum to protocol flag bits. */
uint32_t cl_stream_flags_for_profile(uint8_t profile);

#ifdef __cplusplus
}
#endif

#endif
