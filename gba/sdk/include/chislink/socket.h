#ifndef CHISLINK_SOCKET_H
#define CHISLINK_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#include "chislink/net.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file socket.h
 * @brief POSIX-style socket compatibility layer over cl_net_*.
 *
 * Provides familiar socket(), connect(), bind(), listen(), accept(),
 * send(), recv(), sendto(), recvfrom(), select(), getaddrinfo(),
 * freeaddrinfo(), close(), shutdown() with a cl_socket_ prefix (and optional
 * CHISLINK_SOCKET_POSIX_NAMES macros for drop-in POSIX names).
 *
 * ## Key divergences from POSIX
 * - Return values use the **kernel convention**: -errno directly (e.g.
 *   -CL_SOCKET_EAGAIN = -11), NOT the POSIX convention of -1 + errno.
 * - select() uses poll_attempts instead of a wall-clock timeout — use the
 *   cl_select_timeout_to_attempts() helper to convert a struct timeval *.
 * - shutdown() is a stub that always returns -ENOSYS.
 * - getaddrinfo() returns one IPv4 result stored inside cl_socket_context_t;
 *   the result is overwritten by the next cl_getaddrinfo() call on that
 *   context.  freeaddrinfo() is a no-op.
 * - socket options (setsockopt/getsockopt) are not available.
 * - Only IPv4 (CL_AF_INET), TCP (CL_SOCK_STREAM), and UDP (CL_SOCK_DGRAM).
 * - FD_SETSIZE is a compile-time constant (default 32, max 32).
 *
 * ## Memory ownership
 * - cl_socket_context_t holds pointers to a shared cl_net_t and a
 *   caller-owned cl_socket_fd_t array.
 * - The FD array size is fd_count — allocate enough slots for your max
 *   simultaneous sockets.
 * - No heap allocations.
 *
 * ## POSIX name mapping
 * Define CHISLINK_SOCKET_POSIX_NAMES and CHISLINK_SOCKET_CONTEXT before
 * including this header to use un-prefixed POSIX names:
 * ```
 * #define CHISLINK_SOCKET_POSIX_NAMES
 * #define CHISLINK_SOCKET_CONTEXT (&g_socket_ctx)
 * #include "chislink/socket.h"
 * ```
 */

/* --- Address family and socket type constants --- */
#define CL_AF_UNSPEC 0        /**< Unspecified family (accepted by getaddrinfo). */
#define CL_AF_INET 2          /**< IPv4 (matches POSIX AF_INET). */
#define CL_SOCK_STREAM 1      /**< TCP. */
#define CL_SOCK_DGRAM  2      /**< UDP. */
#define CL_SOL_SOCKET 0xffff  /**< Socket-level option (unused — no sockopts). */
#define CL_IPPROTO_TCP 6      /**< TCP protocol number. */
#define CL_IPPROTO_UDP 17     /**< UDP protocol number. */

/** Max FDs for fd_set.  Hard limit of 32. */
#ifndef CL_SOCKET_FD_SETSIZE
#define CL_SOCKET_FD_SETSIZE 32
#endif
#if CL_SOCKET_FD_SETSIZE > 32
#error "CL_SOCKET_FD_SETSIZE currently supports at most 32 descriptors"
#endif

/** shutdown() how values. */
#define CL_SOCKET_SHUT_RD   0   /**< Disable receives. */
#define CL_SOCKET_SHUT_WR   1   /**< Disable sends. */
#define CL_SOCKET_SHUT_RDWR 2   /**< Disable both. */

#define CL_INET_ADDRSTRLEN 16   /**< Max length of dotted IPv4 string. */
#define CL_INADDR_NONE 0xffffffffu /**< Invalid address sentinel. */

/* --- getaddrinfo flags and errors --- */
#define CL_AI_PASSIVE      0x01
#define CL_AI_CANONNAME    0x02
#define CL_AI_NUMERICHOST  0x04
#define CL_AI_NUMERICSERV  0x08

#define CL_EAI_BADFLAGS   -1
#define CL_EAI_NONAME     -2
#define CL_EAI_AGAIN      -3
#define CL_EAI_FAIL       -4
#define CL_EAI_FAMILY     -6
#define CL_EAI_SOCKTYPE   -7
#define CL_EAI_SERVICE    -8
#define CL_EAI_MEMORY     -10
#define CL_EAI_SYSTEM     -11

/* --- errno-style error codes (match Linux values where possible) --- */
#define CL_SOCKET_EPERM           1
#define CL_SOCKET_EBADF           9
#define CL_SOCKET_EAGAIN          11
#define CL_SOCKET_ENOMEM          12
#define CL_SOCKET_EINVAL          22
#define CL_SOCKET_ENOSYS          38
#define CL_SOCKET_EPROTONOSUPPORT 93
#define CL_SOCKET_EAFNOSUPPORT    97
#define CL_SOCKET_EISCONN         106
#define CL_SOCKET_ENOTCONN        107

/* --- POSIX-compatible types --- */
typedef uint16_t cl_sa_family_t;
typedef uint16_t cl_in_port_t;
typedef uint32_t cl_in_addr_t;
typedef uint32_t cl_socklen_t;

/** IPv4 address in network byte order. */
typedef struct cl_in_addr {
    cl_in_addr_t s_addr;   /**< Network byte order. Use cl_inet_addr4() or cl_htonl(). */
} cl_in_addr_struct_t;

/** Generic socket address. */
typedef struct cl_sockaddr {
    cl_sa_family_t sa_family;
    uint8_t sa_data[14];
} cl_sockaddr_t;

/** IPv4 socket address (binary-compatible with POSIX struct sockaddr_in). */
typedef struct cl_sockaddr_in {
    cl_sa_family_t sin_family;          /**< CL_AF_INET. */
    cl_in_port_t sin_port;              /**< Network byte order port. */
    cl_in_addr_struct_t sin_addr;       /**< Network byte order IP. */
    uint8_t sin_zero[8];                /**< Padding (must be zero). */
} cl_sockaddr_in_t;

/** Lightweight addrinfo.  cl_getaddrinfo() returns a single IPv4 result
 *  owned by cl_socket_context_t, not a heap-allocated linked list. */
typedef struct cl_addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    cl_socklen_t ai_addrlen;
    cl_sockaddr_t *ai_addr;
    char *ai_canonname;
    struct cl_addrinfo *ai_next;
} cl_addrinfo_t;

/** Per-socket descriptor managed by the socket context. */
typedef struct cl_socket_fd {
    cl_net_addr_t remote;      /**< Peer address (when connected). */
    cl_net_handle_t handle;    /**< Underlying cl_net handle. */
    uint8_t used;              /**< 1 if this FD slot is in use. */
    uint8_t proto;             /**< CL_NET_PROTO_UDP or CL_NET_PROTO_TCP. */
    uint8_t nonblocking;       /**< Non-blocking flag (currently stored only). */
    uint8_t has_remote;        /**< 1 if remote address is valid. */
    uint8_t listening;         /**< 1 if this is a listening TCP socket. */
    uint8_t reserved;
    uint32_t flags;            /**< Last polled CLP_NET_SOCKET_FLAG_*. */
    int last_error;            /**< Per-socket error code. */
} cl_socket_fd_t;

/** Socket context — owns the FD table and references the net layer. */
typedef struct cl_socket_context {
    cl_net_t *net;                  /**< Shared net instance (not owned). */
    cl_socket_fd_t *fds;            /**< Caller-owned FD array. */
    size_t fd_count;                /**< Number of slots in fds[]. */
    int last_error;                 /**< Last context-wide error. */
    cl_addrinfo_t addrinfo_result;  /**< Single reusable getaddrinfo result. */
    cl_sockaddr_in_t addrinfo_addr; /**< Backing sockaddr for addrinfo_result. */
} cl_socket_context_t;

/** File descriptor set (bitmask, up to 32 FDs). */
typedef struct cl_fd_set {
    uint32_t bits;   /**< Bitmask: bit 0 = fd 1, bit 1 = fd 2, ... */
} cl_fd_set_t;

/* --- Network byte order conversion --- */
uint16_t cl_htons(uint16_t value);
uint16_t cl_ntohs(uint16_t value);
uint32_t cl_htonl(uint32_t value);
uint32_t cl_ntohl(uint32_t value);

/* --- Address conversion --- */

/** Build a network-order IPv4 address from four octets. */
uint32_t cl_inet_addr4(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

/** Convert a dotted string to network byte order.  Returns CL_INADDR_NONE on error. */
uint32_t cl_inet_addr(const char *text);

/** Convert a dotted string to a cl_in_addr_struct_t.  Returns 1 on success, 0 on error. */
int cl_inet_aton(const char *text, cl_in_addr_struct_t *out_addr);

/** Generic address conversion (AF_INET only). */
int cl_inet_pton(int af, const char *src, void *dst);

/** Convert network-order address to dotted string.  Returns dst on success, NULL on error.
 *  @param src   Pointer to raw network-order bytes (4 bytes for IPv4). */
const char *cl_inet_ntop(int af, const void *src, char *dst, cl_socklen_t size);

/** Resolve node/service to one IPv4 addrinfo result.
 *
 *  Diverges from POSIX to avoid heap allocation: the result is stored inside
 *  ctx and overwritten by the next cl_getaddrinfo() call on the same context.
 *  cl_freeaddrinfo() is therefore a no-op.
 *
 *  Supported inputs: AF_UNSPEC/AF_INET, SOCK_STREAM/SOCK_DGRAM, numeric
 *  service strings, dotted IPv4 addresses, and hostnames resolved by MCU DNS.
 */
int cl_getaddrinfo(cl_socket_context_t *ctx,
                   const char *node,
                   const char *service,
                   const cl_addrinfo_t *hints,
                   cl_addrinfo_t **res);

/** Compatibility no-op; included so POSIX-style code can still call it. */
void cl_freeaddrinfo(cl_addrinfo_t *res);

/* --- fd_set operations --- */
void cl_fd_zero(cl_fd_set_t *set);
void cl_fd_set(int fd, cl_fd_set_t *set);
void cl_fd_clr(int fd, cl_fd_set_t *set);
int cl_fd_isset(int fd, const cl_fd_set_t *set);

/* --- Socket API --- */

/** Initialise the socket context.
 *  @param fds       Caller-owned array of cl_socket_fd_t.
 *  @param fd_count  Number of slots in fds (max simultaneous sockets).
 *  @return 0 on success, -CL_SOCKET_EINVAL on bad arguments. */
int cl_socket_init(cl_socket_context_t *ctx,
                   cl_net_t *net,
                   cl_socket_fd_t *fds,
                   size_t fd_count);

/** Return the context's last error code. */
int cl_socket_errno(const cl_socket_context_t *ctx);

/** Create an unbound socket.
 *  @param domain    CL_AF_INET only.
 *  @param type      CL_SOCK_STREAM or CL_SOCK_DGRAM.
 *  @param protocol  0 (ignored).
 *  @return FD number (>0) on success, -errno on error. */
int cl_socket_socket(cl_socket_context_t *ctx,
                     int domain,
                     int type,
                     int protocol);

/** Connect a TCP socket to a remote address.
 *  @return 0 on success, -errno on error. */
int cl_socket_connect(cl_socket_context_t *ctx,
                      int fd,
                      const cl_sockaddr_t *addr,
                      cl_socklen_t addr_len);

/** Bind a socket to a local address.
 *  @return 0 on success, -errno on error. */
int cl_socket_bind(cl_socket_context_t *ctx,
                   int fd,
                   const cl_sockaddr_t *addr,
                   cl_socklen_t addr_len);

/** Mark a TCP socket as listening.
 *  @return 0 on success, -errno on error. */
int cl_socket_listen(cl_socket_context_t *ctx, int fd, int backlog);

/** Accept a pending TCP connection.
 *  @return New FD (>0) on success, -CL_SOCKET_EAGAIN if no pending
 *          connections, or -errno on error. */
int cl_socket_accept(cl_socket_context_t *ctx,
                     int fd,
                     cl_sockaddr_t *addr,
                     cl_socklen_t *addr_len);

/** Send data on a connected socket.
 *  @param flags  Ignored (no MSG_* support).
 *  @return Bytes sent (>0), -CL_SOCKET_EAGAIN (0 bytes sent), or -errno. */
int cl_socket_send(cl_socket_context_t *ctx,
                   int fd,
                   const void *data,
                   size_t length,
                   int flags);

/** Send a UDP datagram.
 *  @param flags  Ignored.
 *  Automatically creates a UDP socket if the FD is unbound.
 *  @return Bytes sent, -CL_SOCKET_EAGAIN, or -errno. */
int cl_socket_sendto(cl_socket_context_t *ctx,
                     int fd,
                     const void *data,
                     size_t length,
                     int flags,
                     const cl_sockaddr_t *addr,
                     cl_socklen_t addr_len);

/** Receive data from a connected socket.
 *  @param flags  Ignored.
 *  @return Bytes received (>0), -CL_SOCKET_EAGAIN (0 bytes), or -errno. */
int cl_socket_recv(cl_socket_context_t *ctx,
                   int fd,
                   void *data,
                   size_t capacity,
                   int flags);

/** Receive a UDP datagram with sender address.
 *  @param flags  Ignored.
 *  Automatically creates a UDP socket if the FD is unbound.
 *  @return Bytes received, -CL_SOCKET_EAGAIN, or -errno. */
int cl_socket_recvfrom(cl_socket_context_t *ctx,
                       int fd,
                       void *data,
                       size_t capacity,
                       int flags,
                       cl_sockaddr_t *addr,
                       cl_socklen_t *addr_len);

/** Poll a single socket's flags.
 *  @param out_flags  Set to CLP_NET_SOCKET_FLAG_* bits. */
int cl_socket_poll(cl_socket_context_t *ctx, int fd, uint32_t *out_flags);

/** Multiplexed poll across multiple sockets (similar to POSIX select).
 *
 *  Polls each requested FD once per attempt.  The function loops up to
 *  poll_attempts times, returning early as soon as any FD is ready.
 *
 *  @param nfds          Highest-numbered FD to check + 1.
 *  @param readfds       FDs to check for readability (may be NULL).
 *  @param writefds      FDs to check for writability (may be NULL).
 *  @param exceptfds     Always zeroed (OOB not supported, may be NULL).
 *  @param poll_attempts Max poll iterations (0 = 1).
 *  @return Number of ready FDs (>0), 0 on timeout, or -errno on error.
 *
 *  @note This is not a true blocking select().  Each attempt is one full
 *        polling round; there is no wall-clock sleep.  For approximate
 *        time-based behaviour, use cl_select_timeout_to_attempts(). */
int cl_socket_select(cl_socket_context_t *ctx,
                     int nfds,
                     cl_fd_set_t *readfds,
                     cl_fd_set_t *writefds,
                     cl_fd_set_t *exceptfds,
                     uint32_t poll_attempts);

/** Set or clear the non-blocking flag on a socket FD.
 *  @note Currently the flag is stored but the underlying I/O always
 *        behaves as-if-nonblocking (returns EAGAIN on zero bytes). */
int cl_socket_set_nonblocking(cl_socket_context_t *ctx,
                              int fd,
                              int enabled);

/** Close a socket FD.  The FD slot is freed even on error.
 *  @return 0 on success, -errno on error. */
int cl_socket_close(cl_socket_context_t *ctx, int fd);

/** Shutdown a socket direction.
 *  @return Always -CL_SOCKET_ENOSYS (not implemented). */
int cl_socket_shutdown(cl_socket_context_t *ctx, int fd, int how);

/* --- Timeout conversion helper for select() --- */

/** Convert a POSIX struct timeval * to poll_attempts for cl_socket_select().
 *
 *  Pass NULL to poll indefinitely, a zero-valued timeval for a single
 *  non-blocking poll, or a non-zero timeval for timed polling.
 *
 *  The timeval struct is expected as two consecutive uint32_t fields:
 *  { uint32_t tv_sec; uint32_t tv_usec; }.  Each poll attempt is estimated
 *  at ~10 ms on GBA SIO, so e.g. {1, 0} yields ~100 attempts.
 *
 *  Usage:
 *  ```
 *  struct timeval tv = {5, 0};  // 5-second timeout
 *  int n = cl_socket_select(&ctx, nfds, &rfds, NULL, NULL,
 *                           cl_select_timeout_to_attempts(&tv));
 *  ``` */
static inline uint32_t cl_select_timeout_to_attempts(const void *timeout) {
    if (!timeout) {
        return 0xFFFFFFFFu;
    }
    uint32_t tv[2];
    {
        const uint8_t *src = (const uint8_t *)timeout;
        uint8_t *dst = (uint8_t *)tv;
        for (uint8_t i = 0; i < 8u; ++i) {
            dst[i] = src[i];
        }
    }
    uint32_t sec = tv[0];
    uint32_t usec = tv[1];
    if (sec == 0 && usec == 0) {
        return 1u;
    }
    uint32_t ms = sec * 1000u + usec / 1000u;
    uint32_t attempts = ms / 10u;
    return attempts ? attempts : 1u;
}

/* --- Optional POSIX name aliases --- */
#ifdef CHISLINK_SOCKET_POSIX_NAMES
#define AF_UNSPEC CL_AF_UNSPEC
#define AF_INET CL_AF_INET
#define SOCK_STREAM CL_SOCK_STREAM
#define SOCK_DGRAM CL_SOCK_DGRAM
#define SOL_SOCKET CL_SOL_SOCKET
#define IPPROTO_TCP CL_IPPROTO_TCP
#define IPPROTO_UDP CL_IPPROTO_UDP
#define SHUT_RD CL_SOCKET_SHUT_RD
#define SHUT_WR CL_SOCKET_SHUT_WR
#define SHUT_RDWR CL_SOCKET_SHUT_RDWR
#define sockaddr cl_sockaddr
#define sockaddr_in cl_sockaddr_in
#define addrinfo cl_addrinfo
#define in_addr cl_in_addr
#define sa_family_t cl_sa_family_t
#define in_port_t cl_in_port_t
#define in_addr_t cl_in_addr_t
#define socklen_t cl_socklen_t
#define fd_set cl_fd_set_t
#ifndef AI_PASSIVE
#define AI_PASSIVE CL_AI_PASSIVE
#endif
#ifndef AI_CANONNAME
#define AI_CANONNAME CL_AI_CANONNAME
#endif
#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST CL_AI_NUMERICHOST
#endif
#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV CL_AI_NUMERICSERV
#endif
#ifndef EAI_BADFLAGS
#define EAI_BADFLAGS CL_EAI_BADFLAGS
#endif
#ifndef EAI_NONAME
#define EAI_NONAME CL_EAI_NONAME
#endif
#ifndef EAI_AGAIN
#define EAI_AGAIN CL_EAI_AGAIN
#endif
#ifndef EAI_FAIL
#define EAI_FAIL CL_EAI_FAIL
#endif
#ifndef EAI_FAMILY
#define EAI_FAMILY CL_EAI_FAMILY
#endif
#ifndef EAI_SOCKTYPE
#define EAI_SOCKTYPE CL_EAI_SOCKTYPE
#endif
#ifndef EAI_SERVICE
#define EAI_SERVICE CL_EAI_SERVICE
#endif
#ifndef EAI_MEMORY
#define EAI_MEMORY CL_EAI_MEMORY
#endif
#ifndef EAI_SYSTEM
#define EAI_SYSTEM CL_EAI_SYSTEM
#endif
#define htons cl_htons
#define ntohs cl_ntohs
#define htonl cl_htonl
#define ntohl cl_ntohl
#define INET_ADDRSTRLEN CL_INET_ADDRSTRLEN
#define INADDR_NONE CL_INADDR_NONE
#define inet_addr cl_inet_addr
#define inet_aton cl_inet_aton
#define inet_pton cl_inet_pton
#define inet_ntop cl_inet_ntop
#define getaddrinfo(node, service, hints, res) \
    cl_getaddrinfo((CHISLINK_SOCKET_CONTEXT), (node), (service), \
                   (const cl_addrinfo_t *)(hints), (cl_addrinfo_t **)(res))
#define freeaddrinfo(res) cl_freeaddrinfo((cl_addrinfo_t *)(res))
#define FD_ZERO(set) cl_fd_zero((set))
#define FD_SET(fd, set) cl_fd_set((fd), (set))
#define FD_CLR(fd, set) cl_fd_clr((fd), (set))
#define FD_ISSET(fd, set) cl_fd_isset((fd), (set))

/** All POSIX-name macros require this to be defined as a pointer to the
 *  active cl_socket_context_t.  Example:
 *  #define CHISLINK_SOCKET_CONTEXT (&g_socket_ctx) */
#define socket(domain, type, protocol) \
    cl_socket_socket((CHISLINK_SOCKET_CONTEXT), (domain), (type), (protocol))
#define connect(fd, addr, addr_len) \
    cl_socket_connect((CHISLINK_SOCKET_CONTEXT), (fd), \
                      (const cl_sockaddr_t *)(addr), (addr_len))
#define bind(fd, addr, addr_len) \
    cl_socket_bind((CHISLINK_SOCKET_CONTEXT), (fd), \
                   (const cl_sockaddr_t *)(addr), (addr_len))
#define listen(fd, backlog) \
    cl_socket_listen((CHISLINK_SOCKET_CONTEXT), (fd), (backlog))
#define accept(fd, addr, addr_len) \
    cl_socket_accept((CHISLINK_SOCKET_CONTEXT), (fd), \
                     (cl_sockaddr_t *)(addr), (addr_len))
#define send(fd, data, length, flags) \
    cl_socket_send((CHISLINK_SOCKET_CONTEXT), (fd), (data), (length), (flags))
#define sendto(fd, data, length, flags, addr, addr_len) \
    cl_socket_sendto((CHISLINK_SOCKET_CONTEXT), (fd), (data), (length), \
                     (flags), (const cl_sockaddr_t *)(addr), (addr_len))
#define recv(fd, data, capacity, flags) \
    cl_socket_recv((CHISLINK_SOCKET_CONTEXT), (fd), (data), (capacity), (flags))
#define recvfrom(fd, data, capacity, flags, addr, addr_len) \
    cl_socket_recvfrom((CHISLINK_SOCKET_CONTEXT), (fd), (data), (capacity), \
                       (flags), (cl_sockaddr_t *)(addr), (addr_len))
/** POSIX select() — timeout is converted to poll_attempts.
 *  NULL timeout → poll indefinitely; {0,0} → single non-blocking poll;
 *  {sec, usec} → timed poll (~10ms per attempt). */
#define select(nfds, readfds, writefds, exceptfds, timeout) \
    cl_socket_select((CHISLINK_SOCKET_CONTEXT), (nfds), (readfds), \
                     (writefds), (exceptfds), \
                     cl_select_timeout_to_attempts(timeout))
#define close(fd) cl_socket_close((CHISLINK_SOCKET_CONTEXT), (fd))
#define shutdown(fd, how) \
    cl_socket_shutdown((CHISLINK_SOCKET_CONTEXT), (fd), (how))
#endif

#ifdef __cplusplus
}
#endif

#endif
