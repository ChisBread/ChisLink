#ifndef CHISLINK_NET_H
#define CHISLINK_NET_H

#include <stddef.h>
#include <stdint.h>

#include "chislink/client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file net.h
 * @brief WiFi networking API — sockets, DNS, WiFi management.
 *
 * The net layer talks to the ESP32 MCU over the ChisLink protocol.  All
 * operations are synchronous: each function call performs one or more
 * command-response round-trips over the transport.
 *
 * ## Handle type
 * cl_net_handle_t is an opaque uint16_t assigned by the MCU.  Handle 0 is
 * the invalid/closed sentinel.  Up to 8 handles may be open simultaneously
 * (enforced by the MCU).
 *
 * ## Scratch buffer
 * The caller must provide a scratch buffer via cl_net_init().  It is used
 * for request construction and response parsing.  Choose the size based on
 * which functions you need:
 * - CL_NET_SCRATCH_MIN_BYTES    — minimal (sendto header only)
 * - CL_NET_SCRATCH_CONFIG_BYTES — WiFi config
 * - CL_NET_SCRATCH_FULL_BYTES   — WiFi scan results (largest)
 *
 * @warning The scratch buffer is NOT reentrant.  Do not call net functions
 *          from interrupt context while another call is in progress.
 *
 * ## Return values
 * 0 = success, -1 = invalid argument, -2 = protocol error,
 * -3 = timeout / handle zero / refused.  The underlying protocol status is
 * also stored in client->last_status.
 *
 * ## Non-blocking I/O
 * All send/recv functions may return 0 (zero bytes transferred) even when
 * no error occurred — this means "try again later" (equivalent to EAGAIN).
 * Use cl_net_wait_flags() or cl_net_poll() to check readiness.
 */

typedef uint16_t cl_net_handle_t;

/** Transport protocol. */
typedef enum cl_net_proto {
    CL_NET_PROTO_UDP = 1,
    CL_NET_PROTO_TCP = 2,
} cl_net_proto_t;

/** Net subsystem state.  The caller owns this struct. */
typedef struct cl_net {
    cl_client_t *client;         /**< Shared protocol client. */
    uint8_t *scratch;            /**< Caller-owned scratch buffer. */
    size_t scratch_size;         /**< Size of scratch buffer in bytes. */
} cl_net_t;

/** Minimum scratch size — sufficient for sendto requests. */
#define CL_NET_SCRATCH_MIN_BYTES    CLP_NET_SENDTO_DATA_OFFSET
/** Scratch size needed for cl_net_config_get / cl_net_config_set. */
#define CL_NET_SCRATCH_CONFIG_BYTES CLP_NET_CONFIG_BYTES
/** Scratch size needed for cl_net_scan (largest requirement). */
#define CL_NET_SCRATCH_FULL_BYTES   CLP_NET_SCAN_MAX_RESPONSE_BYTES

/** Build a host-order IPv4 address from four octets. */
#define CL_NET_IPV4(a, b, c, d) \
    ((((uint32_t)(a) & 0xffu) << 24u) | (((uint32_t)(b) & 0xffu) << 16u) | \
     (((uint32_t)(c) & 0xffu) << 8u) | ((uint32_t)(d) & 0xffu))

/** IPv4 address + port in host byte order. */
typedef struct cl_net_addr {
    uint32_t ipv4;   /**< Host-order IPv4: CL_NET_IPV4(192, 168, 1, 1). */
    uint16_t port;   /**< Host-order port. */
} cl_net_addr_t;

/** WiFi connection status. */
typedef struct cl_net_status {
    uint32_t flags;              /**< CLP_NET_STATUS_FLAG_* bits. */
    uint32_t source;             /**< IP assignment source. */
    uint32_t ip;                 /**< Local IP (host order). */
    uint32_t gateway;            /**< Gateway IP (host order). */
    int32_t rssi;                /**< Signal strength in dBm. */
    int32_t tx_power_qdbm;       /**< TX power in quarter-dBm. */
    int32_t power_adjust_qdbm;   /**< Power adjustment in quarter-dBm. */
    uint32_t connect_attempts;   /**< Number of connection attempts. */
    uint32_t open_sockets;       /**< Currently open sockets. */
    uint32_t max_sockets;        /**< Max socket limit. */
    char ssid[16];               /**< Connected SSID (NUL-terminated, max 15 chars). */
    uint32_t last_error;         /**< Last WiFi error code. */
} cl_net_status_t;

/** WiFi configuration. */
typedef struct cl_net_config {
    uint32_t flags;              /**< CLP_NET_CONFIG_FLAG_* bits. */
    uint32_t source;             /**< IP source preference. */
    int32_t power_adjust_qdbm;   /**< TX power adjustment. */
    char ssid[CLP_NET_CONFIG_SSID_BYTES]; /**< SSID (NUL-terminated, max 31 chars). */
} cl_net_config_t;

/** One scanned access point. */
typedef struct cl_net_scan_ap {
    int32_t rssi;                /**< Signal strength in dBm. */
    uint32_t authmode;           /**< Authentication mode. */
    uint32_t channel;            /**< Channel number. */
    char ssid[CLP_NET_CONFIG_SSID_BYTES]; /**< AP SSID (NUL-terminated). */
} cl_net_scan_ap_t;

/** Scan result container (caller-allocated). */
typedef struct cl_net_scan_result {
    uint32_t total;              /**< Total APs found. */
    uint32_t count;              /**< APs in this page. */
    cl_net_scan_ap_t aps[CLP_NET_SCAN_MAX_RESPONSE_APS]; /**< AP entries. */
} cl_net_scan_result_t;

/* --- Lifecycle --- */

/** Initialise the net subsystem.
 *  @param scratch       Caller-owned buffer (size depends on functions used).
 *  @param scratch_size  Must be >= the largest request/response needed.
 *  @return 0 on success, -1 on NULL argument. */
int cl_net_init(cl_net_t *net, cl_client_t *client,
                void *scratch, size_t scratch_size);

/* --- WiFi management --- */

/** Request a WiFi connection.  Returns immediately; poll with
 *  cl_net_status() to check when connected.
 *  @param out_status  Optional status buffer (may be NULL). */
int cl_net_connect(cl_net_t *net, cl_net_status_t *out_status);

/** Get current WiFi status.
 *  @param flags  CLP_NET_STATUS_FLAG_* filter bits. */
int cl_net_status(cl_net_t *net, uint32_t flags, cl_net_status_t *out_status);

/** Get WiFi configuration.
 *  Requires CL_NET_SCRATCH_CONFIG_BYTES scratch. */
int cl_net_config_get(cl_net_t *net, cl_net_config_t *out_config);

/** Set WiFi configuration and optionally the password.
 *  @param password  WPA passphrase (may be NULL for open networks).
 *  Requires CL_NET_SCRATCH_CONFIG_BYTES scratch. */
int cl_net_config_set(cl_net_t *net, const cl_net_config_t *config,
                      const char *password);

/** Explicitly start or stop the MCU Web file server.
 *  Starting may trigger WiFi connection in lazy mode. Third-party programs
 *  should only call this when they actually need the HTTP server. */
int cl_net_web_control(cl_net_t *net, uint32_t action);

/** Scan for WiFi access points (paginated).
 *  Requires CL_NET_SCRATCH_FULL_BYTES scratch.
 *  @param start_index  0-based AP index to start from.
 *  @param max_entries  Max APs to return in this page. */
int cl_net_scan(cl_net_t *net, uint32_t start_index, uint32_t max_entries,
                cl_net_scan_result_t *out_result);

/* --- Address helpers --- */

/** Parse a dotted IPv4 string ("192.168.1.1") to a host-order uint32_t.
 *  @return 0 on success, -1 on parse error. */
int cl_net_parse_ipv4(const char *text, uint32_t *out_ipv4);

/** Resolve a hostname to an IPv4 address via the MCU's DNS.
 *  @return 0 on success, negative on error. */
int cl_net_resolve_ipv4(cl_net_t *net, const char *host, uint32_t *out_ipv4);

/* --- Socket operations --- */

/** Open a TCP or UDP socket.
 *  @param remote  Destination address (ignored for UDP).
 *  @param out_handle  Set to the MCU-assigned handle on success.
 *  @return 0 on success.  For TCP, call cl_net_wait_connected() afterwards. */
int cl_net_open(cl_net_t *net, cl_net_proto_t proto, const cl_net_addr_t *remote,
                cl_net_handle_t *out_handle);

/** Open a UDP socket (no remote address needed). */
int cl_net_open_udp(cl_net_t *net, cl_net_handle_t *out_handle);

/** Bind a socket to a local address and port.
 *  @param local  Local address (zero IP = any, zero port = any). */
int cl_net_bind(cl_net_t *net, cl_net_proto_t proto, const cl_net_addr_t *local,
                cl_net_handle_t *out_handle);

/** Start listening on a TCP socket.
 *  @param backlog  Max pending connections. */
int cl_net_listen(cl_net_t *net, cl_net_handle_t handle, uint32_t backlog);

/** Accept a pending TCP connection.
 *  @param listen_handle  Handle from cl_net_bind + cl_net_listen.
 *  @param out_remote     Set to the remote peer's address.
 *  @param out_handle     Set to the new connection handle.
 *  @return 0 on success, -3 on timeout/no connection.  Check *out_handle:
 *          if it is 0, there was no pending connection (not an error). */
int cl_net_accept(cl_net_t *net, cl_net_handle_t listen_handle,
                  cl_net_addr_t *out_remote,
                  cl_net_handle_t *out_handle);

/** Open a socket to a host:port (resolves hostname via DNS).
 *  Port 0 is rejected — use cl_net_open_udp for unbound UDP. */
int cl_net_open_host(cl_net_t *net, cl_net_proto_t proto, const char *host,
                     uint16_t port,
                     cl_net_handle_t *out_handle);

/** Open a TCP connection to host:port and wait for it to be established.
 *  @param attempts  Max poll iterations (0 = default). */
int cl_net_open_tcp_host_wait(cl_net_t *net, const char *host, uint16_t port,
                              uint32_t attempts,
                              cl_net_handle_t *out_handle);

/** Open TCP to host:port, send tx_data, receive up to rx_capacity bytes,
 *  then close.  An all-in-one HTTP-style request helper.
 *  @param connect_attempts  Poll attempts for connect phase.
 *  @param io_attempts       Poll attempts for I/O phase.
 *  @return Number of bytes received, or negative on error. */
int cl_net_tcp_request_host(cl_net_t *net,
                            const char *host,
                            uint16_t port,
                            const void *tx_data,
                            size_t tx_length,
                            void *rx_data,
                            size_t rx_capacity,
                            uint32_t connect_attempts,
                            uint32_t io_attempts);

/** Close a socket handle. */
int cl_net_close(cl_net_t *net, cl_net_handle_t handle);

/** Poll socket flags (CLP_NET_SOCKET_FLAG_*).
 *  @param out_flags  Set to current flags (always set, even on error). */
int cl_net_poll(cl_net_t *net, cl_net_handle_t handle, uint32_t *out_flags);

/** Poll until required flags are set or attempts exhausted.
 *  @param required_flags  Bitmask of CLP_NET_SOCKET_FLAG_* to wait for.
 *  @param attempts        Max poll iterations.
 *  @param out_flags       Final flags after polling. */
int cl_net_wait_flags(cl_net_t *net, cl_net_handle_t handle,
                      uint32_t required_flags,
                      uint32_t attempts, uint32_t *out_flags);

/** Wait for a TCP socket to become connected.
 *  Convenience wrapper around cl_net_wait_flags with CLP_NET_SOCKET_FLAG_CONNECTED. */
int cl_net_wait_connected(cl_net_t *net, cl_net_handle_t handle,
                          uint32_t attempts);

/** Send data on a connected socket.  May send fewer bytes than requested
 *  (short send is not an error — the return value is bytes sent).
 *  @return Bytes sent (>0), 0 (try again), or negative on error. */
int cl_net_send(cl_net_t *net, cl_net_handle_t handle, const void *data,
                size_t length);

/** Send a UDP datagram to a specific address.
 *  @return Bytes sent, or negative on error. */
int cl_net_sendto(cl_net_t *net, cl_net_handle_t handle,
                  const cl_net_addr_t *remote,
                  const void *data,
                  size_t length);

/** Send all data, retrying on short sends.
 *  @param wait_attempts  Max poll iterations between retries.
 *  @return Total bytes sent, or negative on error. */
int cl_net_send_all(cl_net_t *net, cl_net_handle_t handle, const void *data,
                    size_t length,
                    uint32_t wait_attempts);

/** Convenience: open a UDP socket, send one datagram, close.
 *  @return Bytes sent, or negative on error. */
int cl_net_udp_send(cl_net_t *net,
                    const cl_net_addr_t *remote,
                    const void *data,
                    size_t length);

/** Convenience: resolve host, open UDP socket, send datagram, close.
 *  @return Bytes sent, or negative on error. */
int cl_net_udp_send_host(cl_net_t *net,
                         const char *host,
                         uint16_t port,
                         const void *data,
                         size_t length);

/** Receive data from a connected socket.
 *  @return Bytes received (>0), 0 (no data available), or negative on error. */
int cl_net_recv(cl_net_t *net, cl_net_handle_t handle, void *data,
                size_t capacity);

/** Receive a UDP datagram and the sender's address.
 *  @return Bytes received, or negative on error. */
int cl_net_recvfrom(cl_net_t *net, cl_net_handle_t handle,
                    cl_net_addr_t *out_remote,
                    void *data,
                    size_t capacity);

/** Receive data, waiting up to wait_attempts polls.
 *  @return Bytes received (>0), 0 (timeout), or negative on error. */
int cl_net_recv_wait(cl_net_t *net, cl_net_handle_t handle, void *data,
                     size_t capacity,
                     uint32_t wait_attempts);

/* --- Low-level payload builders/parsers (exposed for custom protocol use) --- */

void cl_net_build_status_request(uint8_t payload[CLP_NET_STATUS_REQUEST_BYTES],
                                 uint32_t flags);
int cl_net_parse_status_response(const uint8_t payload[CLP_NET_STATUS_RESPONSE_BYTES],
                                 cl_net_status_t *out_status);
int cl_net_parse_config_response(const uint8_t payload[CLP_NET_CONFIG_BYTES],
                                 cl_net_config_t *out_config);
int cl_net_build_config_request(uint8_t payload[CLP_NET_CONFIG_BYTES],
                                const cl_net_config_t *config,
                                const char *password);
void cl_net_build_scan_request(uint8_t payload[CLP_NET_SCAN_REQUEST_BYTES],
                               uint32_t start_index,
                               uint32_t max_entries);
int cl_net_parse_scan_response(const uint8_t *payload,
                               uint32_t payload_length,
                               cl_net_scan_result_t *out_result);
int cl_net_build_open_request(uint8_t payload[CLP_NET_OPEN_REQUEST_BYTES],
                              cl_net_proto_t proto,
                              const cl_net_addr_t *remote);
void cl_net_build_handle_request(uint8_t payload[4], cl_net_handle_t handle);
void cl_net_build_recv_request(uint8_t payload[CLP_NET_RECV_REQUEST_BYTES],
                               cl_net_handle_t handle,
                               uint32_t capacity);
int cl_net_parse_u32_response(const uint8_t payload[4],
                              uint32_t *out_value);

#ifdef __cplusplus
}
#endif

#endif
