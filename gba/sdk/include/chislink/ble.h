#ifndef CHISLINK_BLE_H
#define CHISLINK_BLE_H

#include <stddef.h>
#include <stdint.h>

#include "chislink/client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ble.h
 * @brief Bluetooth Low Energy (BLE) central/peripheral API.
 *
 * Provides scan, connect, GATT discovery, read/write, notifications, and
 * event polling.  All operations are synchronous command-response round-trips
 * to the ESP32 MCU running NimBLE.
 *
 * ## Limitations
 * - 16-bit and selected 128-bit UUID discovery are supported.
 * - Central operations are exposed directly; peripheral mode exposes a compact
 *   static GATT server configured before advertising starts.
 * - **Poll-based** — no push/callback model for notifications.  Use
 *   cl_ble_event_next() or cl_ble_collect_notify_chr16().
 * - **Max 2 concurrent connections**, **max 2 concurrent GATT ops**
 *   (MCU-enforced).
 * - **Event queue depth 6** on the MCU; oldest events are dropped on overflow.
 *
 * ## Scratch buffer
 * The caller must provide a scratch buffer via cl_ble_init().  Sizes:
 * - CL_BLE_SCRATCH_MIN_BYTES   (4)  — scan start/stop only
 * - CL_BLE_SCRATCH_GATT_BYTES  — GATT discovery
 * - CL_BLE_SCRATCH_EVENT_BYTES — event polling
 * - CL_BLE_SCRATCH_FULL_BYTES  — all features (recommended, = EVENT_BYTES)
 *
 * @warning The scratch buffer is NOT reentrant.  Do not interleave BLE calls.
 *
 * ## Return values
 * All functions return 0 on success, negative on error.
 * -1: invalid argument, -2: protocol/wire error, -3: format/data error.
 *
 * ## Event struct
 * cl_ble_event_t is polymorphic: for scan results, addr[] and rssi are valid;
 * for GATT notifications, attr_handle and data[] are valid.  Check event.type
 * to determine which fields to read.
 *
 * ## Memory ownership
 * - cl_ble_t stores pointers to shared client and scratch buffer.
 * - Event arrays in collect functions are caller-owned.
 * - All structs are caller-allocated (stack or static).
 */

typedef uint16_t cl_ble_handle_t;   /**< Connection handle (0 = invalid). */

/** BLE subsystem state. */
typedef struct cl_ble {
    cl_client_t *client;            /**< Shared protocol client. */
    uint8_t *scratch;               /**< Caller-owned scratch buffer. */
    size_t scratch_size;            /**< Size of scratch buffer in bytes. */
    uint16_t link_notify_attr;      /**< Link TX notify attr for JOIN role. */
    uint16_t link_rx_attr;          /**< Link RX remote attr for JOIN role. */
    uint16_t link_tx_chr;           /**< Link TX local chr id for HOST role. */
    uint8_t link_role;              /**< Link role used by cl_ble_link_*(). */
} cl_ble_t;

/** Minimal scratch — sufficient for scan start/stop and disconnect. */
#define CL_BLE_SCRATCH_MIN_BYTES  4u
/** Scratch for GATT find/discovery operations. */
#define CL_BLE_SCRATCH_GATT_BYTES CLP_BLE_GATT_FIND_CHR_RESPONSE_BYTES
/** Scratch for event polling (header + max scan data). */
#define CL_BLE_SCRATCH_EVENT_BYTES \
    (CLP_BLE_EVENT_HEADER_BYTES + CLP_BLE_SCAN_DATA_MAX_BYTES)
/** Recommended scratch size for full functionality. */
#define CL_BLE_SCRATCH_FULL_BYTES CL_BLE_SCRATCH_EVENT_BYTES

/** BLE device address. */
typedef struct cl_ble_addr {
    uint8_t bytes[6];    /**< MAC address in little-endian order. */
    uint8_t type;        /**< Address type (0=public, 1=random, ...). */
} cl_ble_addr_t;

/** BLE event (scan result, GATT notification, connection event).
 *
 *  Fields valid per event type:
 *  - SCAN_RESULT:  addr[], addr_type, rssi, data[], data_length
 *  - SCAN_COMPLETE: (none)
 *  - CONNECT:      addr[]
 *  - DISCONNECT:   addr[]
 *  - GATT_DATA:    attr_handle, data[], data_length, flags */
typedef struct cl_ble_event {
    uint8_t type;                /**< Event type (CLP_BLE_EVENT_*). */
    int8_t rssi;                 /**< RSSI in dBm (scan results). */
    uint8_t addr_type;           /**< BLE address type. */
    uint8_t data_length;         /**< Bytes valid in data[]. */
    uint8_t addr[6];             /**< Device BD_ADDR (scan results). */
    uint16_t flags;              /**< Event flags. */
    uint16_t attr_handle;        /**< GATT attribute handle (GATT_DATA events). */
    uint8_t data[CLP_BLE_SCAN_DATA_MAX_BYTES]; /**< Scan data or notification payload. */
} cl_ble_event_t;

/** Discovered GATT characteristic. */
typedef struct cl_ble_chr {
    uint16_t def_handle;         /**< Characteristic definition handle. */
    uint16_t value_handle;       /**< Value handle (used for read/write/notify). */
    uint8_t properties;          /**< Characteristic properties bitmask. */
    uint8_t reserved;            /**< Reserved (padding). */
    uint16_t uuid16;             /**< 16-bit UUID (only set by find_chr, not find_chr16). */
} cl_ble_chr_t;

/** Discovered GATT service. */
typedef struct cl_ble_service {
    uint16_t start_handle;       /**< First handle in this service. */
    uint16_t end_handle;         /**< Last handle in this service. */
    uint16_t uuid16;             /**< 16-bit service UUID. */
    uint16_t reserved;           /**< Reserved (padding). */
} cl_ble_service_t;

/** Discovered GATT descriptor. */
typedef struct cl_ble_desc {
    uint16_t handle;             /**< Descriptor handle. */
    uint16_t uuid16;             /**< 16-bit descriptor UUID. */
    uint32_t reserved;           /**< Reserved (padding). */
} cl_ble_desc_t;

/** Union type for 16-bit or 128-bit UUIDs. */
typedef struct cl_ble_uuid {
    uint8_t is_128;              /**< 1 = 128-bit, 0 = 16-bit. */
    union {
        uint16_t u16;            /**< 16-bit UUID value. */
        uint8_t u128[16];        /**< 128-bit UUID value (big-endian). */
    } u;
} cl_ble_uuid_t;

typedef struct cl_ble_adv_config {
    uint16_t duration_ms;           /**< 0 = advertise forever. */
    uint16_t appearance;            /**< GAP appearance when flags request it. */
    uint32_t flags;                 /**< CLP_BLE_ADV_FLAG_* */
    const uint16_t *uuid16;         /**< Optional advertised 16-bit UUID list. */
    uint8_t uuid16_count;           /**< Entries in uuid16[]. */
    const uint8_t (*uuid128)[16];   /**< Optional advertised 128-bit UUID list. */
    uint8_t uuid128_count;          /**< Entries in uuid128[]. */
    const char *name;               /**< Optional complete local name. */
} cl_ble_adv_config_t;

typedef uint16_t cl_ble_gatts_id_t;

/** Connection information returned by cl_ble_conn_info(). */
typedef struct cl_ble_conn_info {
    uint32_t flags;              /**< CLP_BLE_CONN_FLAG_* bitmask. */
    uint8_t  connected;          /**< 1 if connection is active. */
    uint8_t  encrypted;          /**< 1 if link is encrypted. */
    uint8_t  authenticated;      /**< 1 if authenticated pairing is active. */
    uint8_t  bonded;             /**< 1 if peer bond is stored. */
    int8_t   rssi;               /**< Current RSSI in dBm. */
    uint16_t mtu;                /**< Negotiated ATT MTU. */
    uint32_t interval_us;        /**< Connection interval in microseconds. */
    uint32_t latency;            /**< Slave latency (events). */
    uint32_t timeout_us;         /**< Supervision timeout in microseconds. */
    uint8_t  addr_type;          /**< Peer address type. */
    uint8_t  addr[6];            /**< Peer BD_ADDR. */
} cl_ble_conn_info_t;

/* --- Scan flags --- */
#define CL_BLE_SCAN_DURATION_FOREVER 0u  /**< Scan indefinitely. */

/* --- Pairing IO capabilities --- */
#define CL_BLE_IO_CAPS_DISPLAY_ONLY  0u
#define CL_BLE_IO_CAPS_DISPLAY_YESNO 1u
#define CL_BLE_IO_CAPS_KEYBOARD_ONLY 2u
#define CL_BLE_IO_CAPS_NO_IO         3u

/* --- Security manager flags --- */
#define CL_BLE_SECURITY_BOND         CLP_BLE_SECURITY_BOND
#define CL_BLE_SECURITY_MITM         CLP_BLE_SECURITY_MITM
#define CL_BLE_SECURITY_SC           CLP_BLE_SECURITY_SC
#define CL_BLE_SECURITY_SC_ONLY      CLP_BLE_SECURITY_SC_ONLY
#define CL_BLE_SECURITY_KEY_DIST_ENC CLP_BLE_SECURITY_KEY_DIST_ENC
#define CL_BLE_SECURITY_KEY_DIST_ID  CLP_BLE_SECURITY_KEY_DIST_ID

/* --- Pairing passkey actions --- */
#define CL_BLE_PASSKEY_ACTION_NONE    CLP_BLE_PASSKEY_ACTION_NONE
#define CL_BLE_PASSKEY_ACTION_OOB     CLP_BLE_PASSKEY_ACTION_OOB
#define CL_BLE_PASSKEY_ACTION_INPUT   CLP_BLE_PASSKEY_ACTION_INPUT
#define CL_BLE_PASSKEY_ACTION_DISPLAY CLP_BLE_PASSKEY_ACTION_DISPLAY
#define CL_BLE_PASSKEY_ACTION_NUMCMP  CLP_BLE_PASSKEY_ACTION_NUMCMP
#define CL_BLE_PASSKEY_ACTION_OOB_SC  CLP_BLE_PASSKEY_ACTION_OOB_SC
#define CL_BLE_PASSKEY_ACTION_STATIC  CLP_BLE_PASSKEY_ACTION_STATIC

/** BLE security manager configuration.
 *
 *  This controls NimBLE pairing/encryption policy.  The MCU does not infer
 *  policy from a profile such as HID; GBA code should configure it before
 *  advertising or initiating pairing.
 */
typedef struct cl_ble_security_config {
    uint8_t io_caps;        /**< CL_BLE_IO_CAPS_* */
    uint8_t flags;          /**< CL_BLE_SECURITY_* */
    uint8_t our_key_dist;   /**< CL_BLE_SECURITY_KEY_DIST_* */
    uint8_t their_key_dist; /**< CL_BLE_SECURITY_KEY_DIST_* */
    uint8_t security_level; /**< NimBLE LE Security Mode 1 level, 0..4. */
} cl_ble_security_config_t;

/* --- Lifecycle --- */

/** Initialise the BLE subsystem.
 *  @param scratch       Caller-owned buffer.
 *  @param scratch_size  Must be >= required for functions you will use.
 *  @return 0 on success, -1 on NULL argument. */
int cl_ble_init(cl_ble_t *ble,
                cl_client_t *client,
                void *scratch,
                size_t scratch_size);

/* --- Scanning --- */

/** Start a BLE scan for the given duration.
 *  @param duration_ms  Scan duration in milliseconds.
 *  @return 0 on success, negative on error. */
int cl_ble_scan_start(cl_ble_t *ble, uint16_t duration_ms);

/** Stop an active scan. */
int cl_ble_scan_stop(cl_ble_t *ble);

/** Start a scan, collect results into a caller-owned array, stop.
 *  Busy-loops up to poll_attempts times.  Events beyond capacity are dropped.
 *  @param events         Caller-owned array to store scan results.
 *  @param capacity       Max number of events to store.
 *  @param poll_attempts  Max poll iterations.
 *  @return Number of scan results collected, or negative on error. */
int cl_ble_scan_collect(cl_ble_t *ble,
                        uint16_t duration_ms,
                        cl_ble_event_t *events,
                        size_t capacity,
                        uint32_t poll_attempts);

/** Start a BLE scan with full parameter control.
 *  @param duration_ms  Scan duration (0 = forever until stop).
 *  @param interval_us  Scan interval in 0.625ms units (0 = default).
 *  @param window_us    Scan window in 0.625ms units (0 = default).
 *  @param flags        CLP_BLE_SCAN_FLAG_ACTIVE | CLP_BLE_SCAN_FLAG_FILTER_DUP.
 *  @return 0 on success, negative on error. */
int cl_ble_scan_start_ex(cl_ble_t *ble,
                         uint16_t duration_ms,
                         uint16_t interval_us,
                         uint16_t window_us,
                         uint32_t flags);

/* --- Connection --- */

/** Connect to a BLE peripheral.
 *  Blocks until the connection is established or the MCU times out (~8s).
 *  @param addr        Device address (from scan result).
 *  @param out_handle  Set to connection handle on success.
 *  @return 0 on success, negative on error. */
int cl_ble_connect(cl_ble_t *ble,
                   const cl_ble_addr_t *addr,
                   cl_ble_handle_t *out_handle);

/** Disconnect from a peripheral.
 *  @param handle  Connection handle from cl_ble_connect().
 *  @return 0 on success, negative on error. */
int cl_ble_disconnect(cl_ble_t *ble, cl_ble_handle_t handle);

/* --- High-level convenience functions --- */

/** Connect to a device, read its Device Name characteristic (0x2A00),
 *  and disconnect.  Reads up to 32 bytes into out_name.
 *  @param out_name  Output buffer for device name (NUL-terminated if shorter
 *                   than out_size; truncated if the name is longer).
 *  @param out_size  Size of out_name buffer.
 *  @return Number of name bytes read, or negative on error. */
int cl_ble_probe_device_name(cl_ble_t *ble,
                             const cl_ble_addr_t *addr,
                             char *out_name,
                             size_t out_size);

/** Connect, find a characteristic by 16-bit UUID (across all services),
 *  read its value, disconnect.
 *  @param uuid16   Characteristic UUID to read.
 *  @param data     Output buffer.
 *  @param capacity Max bytes to read.
 *  @return Number of bytes read, or negative on error. */
int cl_ble_read_chr16(cl_ble_t *ble,
                      const cl_ble_addr_t *addr,
                      uint16_t uuid16,
                      void *data, size_t capacity);

/** Connect, find a characteristic by 16-bit UUID, write to it, disconnect.
 *  @return Number of bytes written, or negative on error. */
int cl_ble_write_chr16(cl_ble_t *ble,
                       const cl_ble_addr_t *addr,
                       uint16_t uuid16,
                       const void *data, size_t length);

/** Connect, subscribe to notifications, collect up to capacity events
 *  into the caller-owned array, unsubscribe, disconnect.
 *
 *  @param uuid16        Characteristic UUID to subscribe to.
 *  @param events        Caller-owned event array.
 *  @param capacity      Max events to store (excess dropped).
 *  @param poll_attempts Max poll iterations.
 *  @return Number of notification events collected, or negative on error.
 *
 *  @note If CCCD discovery returns NOT_FOUND, falls back to the direct
 *        cl_ble_gatt_notify() CCCD write helper.  Indications are not
 *        requested in this high-level path. */
int cl_ble_collect_notify_chr16(cl_ble_t *ble,
                                const cl_ble_addr_t *addr,
                                uint16_t uuid16,
                                cl_ble_event_t *events,
                                size_t capacity,
                                uint32_t poll_attempts);

/* --- GATT operations (require an open connection handle) --- */

/** Find a characteristic by 16-bit UUID using the compact opcode (0x08).
 *  Returns def_handle, value_handle, and properties.  Does NOT fill uuid16.
 *  @param start_handle  Search start (inclusive).
 *  @param end_handle    Search end (inclusive, 0xFFFF for all).
 *  @return 0 on success, negative on error. */
int cl_ble_gatt_find_chr16(cl_ble_t *ble,
                           cl_ble_handle_t handle,
                           uint16_t start_handle,
                           uint16_t end_handle, uint16_t uuid16,
                           cl_ble_chr_t *out_chr);

/** Find a GATT service by 16-bit UUID.
 *  @return 0 on success, negative on error. */
int cl_ble_gatt_find_service(cl_ble_t *ble,
                             cl_ble_handle_t handle,
                             uint16_t start_handle,
                             uint16_t end_handle,
                             uint16_t uuid16,
                             cl_ble_service_t *out_service);

/** Find a characteristic by 16-bit UUID using the full opcode (0x0B).
 *  Same as find_chr16 but additionally returns the resolved uuid16 in
 *  out_chr->uuid16 (useful for UUID-0 wildcard searches).
 *  @return 0 on success, negative on error. */
int cl_ble_gatt_find_chr(cl_ble_t *ble,
                         cl_ble_handle_t handle,
                         uint16_t start_handle,
                         uint16_t end_handle,
                         uint16_t uuid16,
                         cl_ble_chr_t *out_chr);

/** Find a GATT descriptor by 16-bit UUID under a characteristic.
 *  @param chr_value_handle  Characteristic value handle.
 *  @param end_handle        End handle of the characteristic's descriptor range.
 *  @return 0 on success, negative on error. */
int cl_ble_gatt_find_desc(cl_ble_t *ble,
                          cl_ble_handle_t handle,
                          uint16_t chr_value_handle,
                          uint16_t end_handle,
                          uint16_t uuid16,
                          cl_ble_desc_t *out_desc);

/** Read a GATT attribute value.
 *  @param attr      Attribute handle.
 *  @param data      Output buffer.
 *  @param capacity  Max bytes to read.
 *  @return Number of bytes read, or negative on error. */
int cl_ble_gatt_read(cl_ble_t *ble,
                     cl_ble_handle_t handle,
                     uint16_t attr,
                     void *data, size_t capacity);

/** Write a GATT attribute value (write request, not command).
 *  @param attr   Attribute handle.
 *  @return Number of bytes written, or negative on error. */
int cl_ble_gatt_write(cl_ble_t *ble,
                      cl_ble_handle_t handle,
                      uint16_t attr,
                      const void *data, size_t length);

/** Enable or disable notifications/indications on a characteristic using a
 *  direct CCCD write.
 *  @param enable  1 to enable, 0 to disable.
 *  @return 0 on success, negative on error.
 *  @note Prefer cl_ble_gatt_subscribe() for proper CCCD discovery. */
int cl_ble_gatt_notify(cl_ble_t *ble,
                       cl_ble_handle_t handle,
                       uint16_t attr,
                       uint8_t enable);

/** Subscribe to notifications/indications by discovering and writing the
 *  CCCD descriptor.
 *  @param value_handle  Handle of the characteristic value.
 *  @param end_handle    Search end for CCCD (0xFFFF = search to end).
 *  @param enable        1 to subscribe, 0 to unsubscribe.
 *  @param indicate      1 for indications, 0 for notifications.
 *  @return 0 on success, negative on error. */
int cl_ble_gatt_subscribe(cl_ble_t *ble,
                          cl_ble_handle_t handle,
                          uint16_t value_handle,
                          uint16_t end_handle,
                          uint8_t enable,
                          uint8_t indicate);

/** Convenience: find a characteristic by service+characteristic UUID, then
 *  subscribe to it.
 *  @param service_uuid16  Pass 0 to search all services.
 *  @param out_chr         Set to the found characteristic (may be NULL). */
int cl_ble_gatt_subscribe_chr16(cl_ble_t *ble,
                                cl_ble_handle_t handle,
                                uint16_t service_uuid16,
                                uint16_t chr_uuid16,
                                uint8_t enable,
                                uint8_t indicate,
                                cl_ble_chr_t *out_chr);

/** Poll for the next BLE event from the MCU.
 *  Non-blocking: returns 0 when no event is available.
 *  @param out_event  Filled on success.
 *  @param max_data   Max bytes to read into out_event->data.
 *  @return Event type (>0), 0 (no event), or negative on error. */
int cl_ble_event_next(cl_ble_t *ble,
                      cl_ble_event_t *out_event,
                      size_t max_data);

/** Extract the advertised device name from a scan event.
 *  Non-printable characters are replaced with '.'.  UTF-8 names may be
 *  mangled as a result.
 *  @param out       Output buffer.
 *  @param out_size  Size of output buffer.
 *  @return Length of the name written (may be 0), or negative on error. */
int cl_ble_adv_name(const cl_ble_event_t *event, char *out, size_t out_size);

/* --- 128-bit UUID GATT discovery --- */

int cl_ble_gatt_find_service128(cl_ble_t *ble,
                                cl_ble_handle_t handle,
                                uint16_t start_handle,
                                uint16_t end_handle,
                                const uint8_t uuid128[16],
                                cl_ble_service_t *out_service);
int cl_ble_gatt_find_chr128(cl_ble_t *ble,
                            cl_ble_handle_t handle,
                            uint16_t start_handle,
                            uint16_t end_handle,
                            const uint8_t uuid128[16],
                            cl_ble_chr_t *out_chr);
int cl_ble_gatt_find_desc128(cl_ble_t *ble,
                             cl_ble_handle_t handle,
                             uint16_t chr_value_handle,
                             uint16_t end_handle,
                             const uint8_t uuid128[16],
                             cl_ble_desc_t *out_desc);

/* --- Connection management --- */

int cl_ble_conn_update(cl_ble_t *ble,
                       cl_ble_handle_t handle,
                       uint16_t interval_min,
                       uint16_t interval_max,
                       uint16_t latency,
                       uint16_t timeout);
int cl_ble_exchange_mtu(cl_ble_t *ble,
                        cl_ble_handle_t handle,
                        uint16_t *out_mtu);
int cl_ble_conn_info(cl_ble_t *ble,
                     cl_ble_handle_t handle,
                     cl_ble_conn_info_t *out_info);
int cl_ble_is_connected(cl_ble_t *ble, cl_ble_handle_t handle);

/* --- GATT long read / write --- */

int cl_ble_gatt_read_long(cl_ble_t *ble,
                          cl_ble_handle_t handle,
                          uint16_t attr,
                          uint16_t offset,
                          void *data, size_t capacity);
int cl_ble_gatt_write_long(cl_ble_t *ble,
                           cl_ble_handle_t handle,
                           uint16_t attr,
                           uint16_t offset,
                           const void *data, size_t length);

/* --- Pairing --- */

int cl_ble_security_config(cl_ble_t *ble,
                           const cl_ble_security_config_t *config);
int cl_ble_pair(cl_ble_t *ble,
                cl_ble_handle_t handle,
                uint8_t io_caps,
                uint8_t bond);
int cl_ble_passkey_reply(cl_ble_t *ble,
                         cl_ble_handle_t handle,
                         uint32_t passkey);

/* --- GATT server / peripheral mode --- */

int cl_ble_gatts_reset(cl_ble_t *ble);
int cl_ble_gatts_add_service16(cl_ble_t *ble,
                               uint16_t uuid16,
                               uint32_t flags,
                               cl_ble_gatts_id_t *out_service);
int cl_ble_gatts_add_service128(cl_ble_t *ble,
                                const uint8_t uuid128[16],
                                uint32_t flags,
                                cl_ble_gatts_id_t *out_service);
int cl_ble_gatts_add_include(cl_ble_t *ble,
                             cl_ble_gatts_id_t service,
                             cl_ble_gatts_id_t included_service);
int cl_ble_gatts_add_chr16(cl_ble_t *ble,
                           cl_ble_gatts_id_t service,
                           uint16_t uuid16,
                           uint32_t flags,
                           const void *value,
                           size_t value_length,
                           cl_ble_gatts_id_t *out_chr);
int cl_ble_gatts_add_chr128(cl_ble_t *ble,
                            cl_ble_gatts_id_t service,
                            const uint8_t uuid128[16],
                            uint32_t flags,
                            const void *value,
                            size_t value_length,
                            cl_ble_gatts_id_t *out_chr);
int cl_ble_gatts_add_desc16(cl_ble_t *ble,
                            cl_ble_gatts_id_t chr,
                            uint16_t uuid16,
                            uint32_t flags,
                            const void *value,
                            size_t value_length,
                            cl_ble_gatts_id_t *out_desc);
int cl_ble_gatts_add_desc128(cl_ble_t *ble,
                             cl_ble_gatts_id_t chr,
                             const uint8_t uuid128[16],
                             uint32_t flags,
                             const void *value,
                             size_t value_length,
                             cl_ble_gatts_id_t *out_desc);
int cl_ble_gatts_start(cl_ble_t *ble);
int cl_ble_gatts_stop(cl_ble_t *ble);
int cl_ble_adv_start(cl_ble_t *ble, const cl_ble_adv_config_t *config);
int cl_ble_adv_stop(cl_ble_t *ble);
int cl_ble_gatts_notify(cl_ble_t *ble,
                        cl_ble_handle_t handle,
                        cl_ble_gatts_id_t chr,
                        const void *data,
                        size_t length);
int cl_ble_gatts_set_value(cl_ble_t *ble,
                           cl_ble_gatts_id_t attr,
                           const void *data,
                           size_t length);
int cl_ble_gatts_get_value(cl_ble_t *ble,
                           cl_ble_gatts_id_t attr,
                           uint16_t offset,
                           void *data,
                           size_t capacity);
int cl_ble_gatts_subscribed(cl_ble_t *ble,
                            cl_ble_handle_t handle,
                            cl_ble_gatts_id_t chr);

/* --- ChisLink-to-ChisLink Game Link --- */

/** Link roles. */
#define CL_BLE_LINK_ROLE_HOST  0u  /**< Peripheral: advertise and wait. */
#define CL_BLE_LINK_ROLE_JOIN  1u  /**< Central: scan and connect. */

/** Maximum length for a link device name. */
#define CL_BLE_LINK_NAME_MAX 20u

/** Start the ChisLink-to-ChisLink game link.
 *
 *  HOST: registers the ChisLink Link GATT service on the MCU and starts
 *  advertising with the provided name.  The GBA should then poll
 *  cl_ble_event_next() for
 *  CLP_BLE_EVENT_SUBSCRIBE (peer connected and ready) and
 *  CLP_BLE_EVENT_GATTS_WRITE (incoming game data).
 *
 *  JOIN: scans for a device advertising the link service UUID, connects,
 *  discovers the Link TX/RX characteristics, and subscribes to TX.
 *  @param role  CL_BLE_LINK_ROLE_HOST or CL_BLE_LINK_ROLE_JOIN.
 *  @param name  Human-readable name (max CL_BLE_LINK_NAME_MAX chars).
 *  @return Connection handle (>0) on success (JOIN) or 0 (HOST started),
 *          negative on error.  HOST callers should poll
 *          cl_ble_link_peer_ready() to receive the peer handle. */
int cl_ble_link_start(cl_ble_t *ble, uint8_t role, const char *name);

/** Stop the game link.  Disconnects and stops advertising.
 *  The MCU-side static GATT definition remains registered until reboot.
 *  @return 0 on success, negative on error. */
int cl_ble_link_stop(cl_ble_t *ble, cl_ble_handle_t handle);

/** Send game data to the connected peer.
 *  @param handle  Connection handle from link_start() or connect().
 *  @param data    Raw game link bytes (max CLP_DEFAULT_BLOCK_SIZE - 4).
 *  @param length  Number of bytes to send.
 *  @return Bytes sent on success, negative on error. */
int cl_ble_link_send(cl_ble_t *ble, cl_ble_handle_t handle,
                     const void *data, uint32_t length);

/** Check whether the peer is ready for game data (i.e. subscribed/connected).
 *  For HOST: returns 1 after central subscribes to Link TX.
 *  For JOIN: returns 1 after successful connect + subscribe.
 *  @return Connection handle (>0) if ready, 0 if not, negative on error. */
int cl_ble_link_peer_ready(cl_ble_t *ble, cl_ble_handle_t handle);

/* --- Low-level payload builders/parsers --- */

void cl_ble_build_scan_request(uint8_t payload[4], uint16_t duration_ms);
void cl_ble_build_scan_ext_request(uint8_t payload[CLP_BLE_SCAN_START_EXT_BYTES],
                                   uint16_t duration_ms,
                                   uint16_t interval_us,
                                   uint16_t window_us,
                                   uint32_t flags);
int cl_ble_build_connect_request(uint8_t payload[CLP_BLE_CONNECT_REQUEST_BYTES],
                                 const cl_ble_addr_t *addr);
void cl_ble_build_handle_request(uint8_t payload[4], cl_ble_handle_t handle);
void cl_ble_build_gatt_find_chr16_request(
    uint8_t payload[CLP_BLE_GATT_FIND_CHR16_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t start_handle,
    uint16_t end_handle,
    uint16_t uuid16);
void cl_ble_build_gatt_find_request(
    uint8_t payload[CLP_BLE_GATT_FIND_SERVICE_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t start_handle,
    uint16_t end_handle,
    uint16_t uuid16);
void cl_ble_build_gatt_read_request(
    uint8_t payload[CLP_BLE_GATT_READ_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t attr,
    uint32_t capacity);
void cl_ble_build_gatt_write_header(
    uint8_t payload[CLP_BLE_GATT_WRITE_DATA_OFFSET],
    cl_ble_handle_t handle,
    uint16_t attr);
void cl_ble_build_gatt_notify_request(
    uint8_t payload[CLP_BLE_GATT_NOTIFY_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t attr,
    uint8_t enable);
int cl_ble_parse_handle_response(const uint8_t payload[4],
                                 cl_ble_handle_t *out_handle);
int cl_ble_parse_gatt_find_chr16_response(
    const uint8_t payload[CLP_BLE_GATT_FIND_CHR16_RESPONSE_BYTES],
    cl_ble_chr_t *out_chr);
int cl_ble_parse_gatt_service_response(
    const uint8_t payload[CLP_BLE_GATT_FIND_SERVICE_RESPONSE_BYTES],
    cl_ble_service_t *out_service);
int cl_ble_parse_gatt_chr_response(
    const uint8_t payload[CLP_BLE_GATT_FIND_CHR_RESPONSE_BYTES],
    cl_ble_chr_t *out_chr);
int cl_ble_parse_gatt_desc_response(
    const uint8_t payload[CLP_BLE_GATT_FIND_DESC_RESPONSE_BYTES],
    cl_ble_desc_t *out_desc);
int cl_ble_parse_event_response(const uint8_t *payload,
                                uint32_t payload_length,
                                size_t max_data,
                                cl_ble_event_t *out_event);
void cl_ble_build_gatt_find128_request(
    uint8_t payload[CLP_BLE_GATT_FIND_SERVICE128_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t start_handle,
    uint16_t end_handle,
    const uint8_t uuid128[16]);
void cl_ble_build_conn_update_request(
    uint8_t payload[CLP_BLE_CONN_UPDATE_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t interval_min,
    uint16_t interval_max,
    uint16_t latency,
    uint16_t timeout);
void cl_ble_build_exchange_mtu_request(
    uint8_t payload[CLP_BLE_EXCHANGE_MTU_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t mtu);
void cl_ble_build_gatt_read_long_request(
    uint8_t payload[CLP_BLE_GATT_READ_LONG_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint16_t attr,
    uint16_t offset,
    uint16_t max_len);
void cl_ble_build_gatt_write_long_header(
    uint8_t payload[CLP_BLE_GATT_WRITE_LONG_DATA_OFFSET],
    cl_ble_handle_t handle,
    uint16_t attr,
    uint16_t offset);
void cl_ble_build_pair_request(
    uint8_t payload[CLP_BLE_PAIR_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint8_t io_caps,
    uint8_t bond);
void cl_ble_build_security_config_request(
    uint8_t payload[CLP_BLE_SECURITY_CONFIG_REQUEST_BYTES],
    const cl_ble_security_config_t *config);
void cl_ble_build_passkey_reply_request(
    uint8_t payload[CLP_BLE_PASSKEY_REPLY_REQUEST_BYTES],
    cl_ble_handle_t handle,
    uint32_t passkey);
void cl_ble_build_conn_info_request(
    uint8_t payload[CLP_BLE_CONN_INFO_REQUEST_BYTES],
    cl_ble_handle_t handle);
void cl_ble_build_gatts_get_value_request(
    uint8_t payload[CLP_BLE_GATTS_GET_VALUE_REQUEST_BYTES],
    cl_ble_gatts_id_t attr,
    uint16_t offset,
    uint32_t capacity);
void cl_ble_build_gatts_subscribed_request(
    uint8_t payload[CLP_BLE_GATTS_SUBSCRIBED_REQUEST_BYTES],
    cl_ble_handle_t handle,
    cl_ble_gatts_id_t chr);
int cl_ble_parse_conn_info_response(
    const uint8_t payload[CLP_BLE_CONN_INFO_RESPONSE_BYTES],
    cl_ble_conn_info_t *out_info);
int cl_ble_parse_exchange_mtu_response(
    const uint8_t payload[4],
    uint16_t *out_mtu);
int cl_ble_parse_conn_update_response(const uint8_t payload[4]);

#ifdef __cplusplus
}
#endif

#endif
