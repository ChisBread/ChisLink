#ifndef CHISLINK_BLE_LINK_IO_H
#define CHISLINK_BLE_LINK_IO_H

#include <stdint.h>

#include "chislink/ble.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ble_link_io.h
 * @brief ChisLink BLE Link I/O helper over the ChisLink-to-ChisLink game
 *        link GATT service.
 *
 * This is a minimal abstraction that hides scanning, connecting, GATT
 * discovery, and the link service behind a small send/recv interface.  It is
 * ChisLink-specific; use cl_ble_gatts_* / cl_ble_gatt_* for generic BLE GATT.
 *
 * ## Usage sketch (host)
 * ```
 * cl_ble_t ble;
 * cl_ble_init(&ble, &client, scratch, sizeof(scratch));
 * int link = cl_ble_link_io_open(&ble, CL_BLE_LINK_IO_HOST, "Alice");
 * cl_ble_link_io_send(&ble, link, data, len);
 * int n = cl_ble_link_io_recv(&ble, link, buf, sizeof(buf));
 * cl_ble_link_io_close(&ble, link);
 * ```
 *
 * ## Usage sketch (join)
 * ```
 * int link = cl_ble_link_io_open(&ble, CL_BLE_LINK_IO_JOIN, "Bob");
 * // link > 0: connected and ready to send/recv
 * cl_ble_link_io_close(&ble, link);
 * ```
 *
 * ## Limitations
 * - One link connection at a time (single static state).
 * - No non-blocking mode — send/recv are synchronous round-trips.
 * - No select/poll — use cl_ble_event_next() directly for multiplexing.
 */

/** Link roles. */
#define CL_BLE_LINK_IO_HOST  0u  /**< Peripheral: advertise, wait for peer. */
#define CL_BLE_LINK_IO_JOIN  1u  /**< Central: scan, connect to host. */

/** Open a BLE Link I/O session.
 *
 *  HOST: registers the link GATT service, starts advertising, and returns
 *  immediately with handle=0.  The caller polls for SUBSCRIBE / GATTS_WRITE
 *  events (via cl_ble_event_next) to detect peer readiness and receive data.
 *
 *  JOIN: scans for a device advertising the link service, connects,
 *  discovers the link characteristics, and subscribes.  Blocks until
 *  the connection is established or times out.
 *
 *  @param role  CL_BLE_LINK_IO_HOST or CL_BLE_LINK_IO_JOIN.
 *  @param name  Human-readable name (max 20 chars, e.g. "Alice").
 *  @return link handle (>0) for JOIN (connected), 0 for HOST (advertising
 *          started), or -1 on error. */
int cl_ble_link_io_open(cl_ble_t *ble, uint8_t role, const char *name);

/** Send data to the connected peer.
 *  @param handle Link handle from cl_ble_link_io_open() or peer-ready event.
 *  @param data   Raw game link bytes.
 *  @param length Number of bytes to send.
 *  @return Bytes sent on success, -1 on error. */
int cl_ble_link_io_send(cl_ble_t *ble,
                        int handle,
                        const void *data,
                        uint32_t length);

/** Receive data from the connected peer.
 *  Polls the BLE event queue for link data.  HOST receives
 *  CLP_BLE_EVENT_GATTS_WRITE from peer writes; JOIN receives matching
 *  CLP_BLE_EVENT_GATT_DATA from the subscribed Link TX characteristic.
 *  Non-blocking — returns 0 if no data is available.
 *  @param handle   Link handle from cl_ble_link_io_open() or peer-ready event.
 *  @param buf      Buffer to receive data into.
 *  @param capacity Max bytes to receive.
 *  @return Bytes received (>0), 0 (no data), or -1 on error. */
int cl_ble_link_io_recv(cl_ble_t *ble,
                        int handle,
                        void *buf,
                        uint32_t capacity);

/** Close a BLE Link I/O session.
 *  Disconnects and stops advertising. The static GATT definition remains
 *  registered on the MCU until reboot.
 *  @return 0 on success, -1 on error. */
int cl_ble_link_io_close(cl_ble_t *ble, int handle);

#ifdef __cplusplus
}
#endif

#endif /* CHISLINK_BLE_LINK_IO_H */
