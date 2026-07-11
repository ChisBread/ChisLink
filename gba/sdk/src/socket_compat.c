#include "chislink/socket.h"

#include <string.h>

static uint16_t swap16(uint16_t value) {
    return (uint16_t)((value << 8u) | (value >> 8u));
}

static uint32_t swap32(uint32_t value) {
    return ((value & 0x000000ffu) << 24u) |
           ((value & 0x0000ff00u) << 8u) |
           ((value & 0x00ff0000u) >> 8u) |
           ((value & 0xff000000u) >> 24u);
}

static int socket_fail(cl_socket_context_t *ctx, int error) {
    if (ctx) {
        ctx->last_error = error;
    }
    return -error;
}

static int socket_fail_fd(cl_socket_context_t *ctx,
                          cl_socket_fd_t *slot,
                          int error) {
    if (slot) {
        slot->last_error = error;
    }
    return socket_fail(ctx, error);
}

static cl_socket_fd_t *socket_slot(cl_socket_context_t *ctx, int fd) {
    if (!ctx || !ctx->fds || fd <= 0 || (size_t)fd > ctx->fd_count) {
        return NULL;
    }
    cl_socket_fd_t *slot = &ctx->fds[(size_t)fd - 1u];
    return slot->used ? slot : NULL;
}

static int socket_alloc_fd(cl_socket_context_t *ctx) {
    if (!ctx || !ctx->fds) {
        return 0;
    }
    for (size_t i = 0; i < ctx->fd_count; ++i) {
        if (!ctx->fds[i].used) {
            return (int)i + 1;
        }
    }
    return 0;
}

static uint32_t fd_bit(int fd) {
    if (fd <= 0 || fd > CL_SOCKET_FD_SETSIZE) {
        return 0;
    }
    return 1u << ((uint32_t)fd - 1u);
}

static int sockaddr_to_net_addr_checked(const cl_sockaddr_t *addr,
                                        cl_socklen_t addr_len,
                                        cl_net_addr_t *out_remote,
                                        uint8_t allow_zero_ip,
                                        uint8_t allow_zero_port) {
    if (!addr || !out_remote || addr_len < sizeof(cl_sockaddr_in_t)) {
        return -CL_SOCKET_EINVAL;
    }
    if (addr->sa_family != CL_AF_INET) {
        return -CL_SOCKET_EAFNOSUPPORT;
    }
    const cl_sockaddr_in_t *addr4 = (const cl_sockaddr_in_t *)addr;
    out_remote->ipv4 = cl_ntohl(addr4->sin_addr.s_addr);
    out_remote->port = cl_ntohs(addr4->sin_port);
    if ((!allow_zero_ip && !out_remote->ipv4) ||
        (!allow_zero_port && !out_remote->port)) {
        return -CL_SOCKET_EINVAL;
    }
    return 0;
}

static int net_addr_to_sockaddr(const cl_net_addr_t *remote,
                                cl_sockaddr_t *addr,
                                cl_socklen_t *addr_len) {
    if (!remote || !addr || !addr_len) {
        return 0;
    }
    if (*addr_len < sizeof(cl_sockaddr_in_t)) {
        *addr_len = sizeof(cl_sockaddr_in_t);
        return -CL_SOCKET_EINVAL;
    }
    cl_sockaddr_in_t *addr4 = (cl_sockaddr_in_t *)addr;
    memset(addr4, 0, sizeof(*addr4));
    addr4->sin_family = CL_AF_INET;
    addr4->sin_port = cl_htons(remote->port);
    addr4->sin_addr.s_addr = cl_htonl(remote->ipv4);
    *addr_len = sizeof(*addr4);
    return 0;
}

uint16_t cl_htons(uint16_t value) {
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return value;
#else
    return swap16(value);
#endif
}

uint16_t cl_ntohs(uint16_t value) {
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return value;
#else
    return swap16(value);
#endif
}

uint32_t cl_htonl(uint32_t value) {
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return value;
#else
    return swap32(value);
#endif
}

uint32_t cl_ntohl(uint32_t value) {
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return value;
#else
    return swap32(value);
#endif
}

uint32_t cl_inet_addr4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return cl_htonl(CL_NET_IPV4(a, b, c, d));
}

uint32_t cl_inet_addr(const char *text) {
    uint32_t ipv4 = 0;
    if (cl_net_parse_ipv4(text, &ipv4) != 0) {
        return CL_INADDR_NONE;
    }
    return cl_htonl(ipv4);
}

int cl_inet_aton(const char *text, cl_in_addr_struct_t *out_addr) {
    if (!out_addr) {
        return 0;
    }
    uint32_t ipv4 = 0;
    if (cl_net_parse_ipv4(text, &ipv4) != 0) {
        return 0;
    }
    out_addr->s_addr = cl_htonl(ipv4);
    return 1;
}

int cl_inet_pton(int af, const char *src, void *dst) {
    if (af != CL_AF_INET) {
        return -1;
    }
    if (!dst) {
        return 0;
    }
    return cl_inet_aton(src, (cl_in_addr_struct_t *)dst);
}

static char *append_u8_decimal(char *dst, uint8_t value) {
    if (value >= 100u) {
        *dst++ = (char)('0' + value / 100u);
        value = (uint8_t)(value % 100u);
        *dst++ = (char)('0' + value / 10u);
        *dst++ = (char)('0' + value % 10u);
    } else if (value >= 10u) {
        *dst++ = (char)('0' + value / 10u);
        *dst++ = (char)('0' + value % 10u);
    } else {
        *dst++ = (char)('0' + value);
    }
    return dst;
}

const char *cl_inet_ntop(int af, const void *src, char *dst, cl_socklen_t size) {
    if (af != CL_AF_INET || !src || !dst || size < CL_INET_ADDRSTRLEN) {
        return NULL;
    }

    const uint8_t *bytes = (const uint8_t *)src;
    char *out = dst;
    for (uint32_t i = 0; i < 4u; ++i) {
        if (i) {
            *out++ = '.';
        }
        out = append_u8_decimal(out, bytes[i]);
    }
    *out = '\0';
    return dst;
}

static int parse_service_port(const char *service, uint16_t *out_port) {
    if (!out_port) {
        return CL_EAI_FAIL;
    }
    *out_port = 0;
    if (!service || !service[0]) {
        return 0;
    }

    uint32_t port = 0;
    for (const char *p = service; *p; ++p) {
        char ch = *p;
        if (ch < '0' || ch > '9') {
            return CL_EAI_SERVICE;
        }
        port = port * 10u + (uint32_t)(ch - '0');
        if (port > 65535u) {
            return CL_EAI_SERVICE;
        }
    }
    *out_port = (uint16_t)port;
    return 0;
}

int cl_getaddrinfo(cl_socket_context_t *ctx,
                   const char *node,
                   const char *service,
                   const cl_addrinfo_t *hints,
                   cl_addrinfo_t **res) {
    if (!res) {
        return CL_EAI_FAIL;
    }
    *res = NULL;
    if (!ctx || !ctx->net) {
        return CL_EAI_FAIL;
    }
    if ((!node || !node[0]) && (!service || !service[0])) {
        return CL_EAI_NONAME;
    }

    int flags = hints ? hints->ai_flags : 0;
    if (flags & ~(CL_AI_PASSIVE | CL_AI_CANONNAME |
                  CL_AI_NUMERICHOST | CL_AI_NUMERICSERV)) {
        return CL_EAI_BADFLAGS;
    }

    int family = hints ? hints->ai_family : CL_AF_UNSPEC;
    if (family != CL_AF_UNSPEC && family != CL_AF_INET) {
        return CL_EAI_FAMILY;
    }

    int socktype = hints ? hints->ai_socktype : 0;
    int protocol = hints ? hints->ai_protocol : 0;
    if (socktype != 0 &&
        socktype != CL_SOCK_STREAM &&
        socktype != CL_SOCK_DGRAM) {
        return CL_EAI_SOCKTYPE;
    }
    if (protocol != 0 &&
        protocol != CL_IPPROTO_TCP &&
        protocol != CL_IPPROTO_UDP) {
        return CL_EAI_SOCKTYPE;
    }
    if (!socktype) {
        socktype = (protocol == CL_IPPROTO_UDP) ?
            CL_SOCK_DGRAM : CL_SOCK_STREAM;
    }
    int expected_protocol = (socktype == CL_SOCK_DGRAM) ?
        CL_IPPROTO_UDP : CL_IPPROTO_TCP;
    if (protocol && protocol != expected_protocol) {
        return CL_EAI_SOCKTYPE;
    }
    protocol = expected_protocol;

    uint16_t port = 0;
    int service_ret = parse_service_port(service, &port);
    if (service_ret != 0) {
        return service_ret;
    }

    uint32_t ipv4 = 0;
    if (!node || !node[0]) {
        ipv4 = (flags & CL_AI_PASSIVE) ? 0u : CL_NET_IPV4(127, 0, 0, 1);
    } else if (cl_net_parse_ipv4(node, &ipv4) != 0) {
        if (flags & CL_AI_NUMERICHOST) {
            return CL_EAI_NONAME;
        }
        int ret = cl_net_resolve_ipv4(ctx->net, node, &ipv4);
        if (ret < 0 || !ipv4) {
            return CL_EAI_NONAME;
        }
    }

    memset(&ctx->addrinfo_addr, 0, sizeof(ctx->addrinfo_addr));
    ctx->addrinfo_addr.sin_family = CL_AF_INET;
    ctx->addrinfo_addr.sin_port = cl_htons(port);
    ctx->addrinfo_addr.sin_addr.s_addr = cl_htonl(ipv4);

    memset(&ctx->addrinfo_result, 0, sizeof(ctx->addrinfo_result));
    ctx->addrinfo_result.ai_flags = flags;
    ctx->addrinfo_result.ai_family = CL_AF_INET;
    ctx->addrinfo_result.ai_socktype = socktype;
    ctx->addrinfo_result.ai_protocol = protocol;
    ctx->addrinfo_result.ai_addrlen = sizeof(ctx->addrinfo_addr);
    ctx->addrinfo_result.ai_addr = (cl_sockaddr_t *)&ctx->addrinfo_addr;
    ctx->addrinfo_result.ai_canonname =
        (flags & CL_AI_CANONNAME) ? (char *)node : NULL;
    ctx->addrinfo_result.ai_next = NULL;

    *res = &ctx->addrinfo_result;
    ctx->last_error = 0;
    return 0;
}

void cl_freeaddrinfo(cl_addrinfo_t *res) {
    (void)res;
}

void cl_fd_zero(cl_fd_set_t *set) {
    if (set) {
        set->bits = 0;
    }
}

void cl_fd_set(int fd, cl_fd_set_t *set) {
    uint32_t bit = fd_bit(fd);
    if (set && bit) {
        set->bits |= bit;
    }
}

void cl_fd_clr(int fd, cl_fd_set_t *set) {
    uint32_t bit = fd_bit(fd);
    if (set && bit) {
        set->bits &= ~bit;
    }
}

int cl_fd_isset(int fd, const cl_fd_set_t *set) {
    uint32_t bit = fd_bit(fd);
    return set && bit && (set->bits & bit) ? 1 : 0;
}

int cl_socket_init(cl_socket_context_t *ctx,
                   cl_net_t *net,
                   cl_socket_fd_t *fds,
                   size_t fd_count) {
    if (!ctx || !net || !fds || !fd_count) {
        return socket_fail(ctx, CL_SOCKET_EINVAL);
    }
    ctx->net = net;
    ctx->fds = fds;
    ctx->fd_count = fd_count;
    ctx->last_error = 0;
    memset(&ctx->addrinfo_result, 0, sizeof(ctx->addrinfo_result));
    memset(&ctx->addrinfo_addr, 0, sizeof(ctx->addrinfo_addr));
    memset(fds, 0, sizeof(*fds) * fd_count);
    return 0;
}

int cl_socket_errno(const cl_socket_context_t *ctx) {
    return ctx ? ctx->last_error : CL_SOCKET_EINVAL;
}

int cl_socket_socket(cl_socket_context_t *ctx,
                     int domain,
                     int type,
                     int protocol) {
    if (!ctx || !ctx->net || !ctx->fds) {
        return socket_fail(ctx, CL_SOCKET_EINVAL);
    }
    if (domain != CL_AF_INET) {
        return socket_fail(ctx, CL_SOCKET_EAFNOSUPPORT);
    }
    if (protocol != 0) {
        return socket_fail(ctx, CL_SOCKET_EPROTONOSUPPORT);
    }
    uint8_t proto = 0;
    if (type == CL_SOCK_STREAM) {
        proto = (uint8_t)CL_NET_PROTO_TCP;
    } else if (type == CL_SOCK_DGRAM) {
        proto = (uint8_t)CL_NET_PROTO_UDP;
    } else {
        return socket_fail(ctx, CL_SOCKET_EPROTONOSUPPORT);
    }

    for (size_t i = 0; i < ctx->fd_count; ++i) {
        cl_socket_fd_t *slot = &ctx->fds[i];
        if (slot->used) {
            continue;
        }
        memset(slot, 0, sizeof(*slot));
        slot->used = 1;
        slot->proto = proto;
        ctx->last_error = 0;
        return (int)i + 1;
    }
    return socket_fail(ctx, CL_SOCKET_ENOMEM);
}

int cl_socket_connect(cl_socket_context_t *ctx,
                      int fd,
                      const cl_sockaddr_t *addr,
                      cl_socklen_t addr_len) {
    cl_socket_fd_t *slot = socket_slot(ctx, fd);
    if (!slot) {
        return socket_fail(ctx, CL_SOCKET_EBADF);
    }
    if (!addr || addr_len < sizeof(cl_sockaddr_in_t)) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_EINVAL);
    }
    if (addr->sa_family != CL_AF_INET) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_EAFNOSUPPORT);
    }
    if (slot->handle) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_EISCONN);
    }

    cl_net_addr_t remote;
    int addr_ret = sockaddr_to_net_addr_checked(addr, addr_len, &remote,
                                                0, 0);
    if (addr_ret < 0) {
        return socket_fail_fd(ctx, slot, -addr_ret);
    }
    cl_net_handle_t handle = 0;
    int ret = cl_net_open(ctx->net, (cl_net_proto_t)slot->proto, &remote,
                          &handle);
    if (ret < 0) {
        slot->last_error = -ret;
        ctx->last_error = -ret;
        return ret;
    }
    slot->remote = remote;
    slot->handle = handle;
    slot->has_remote = 1;
    slot->flags = 0;
    slot->last_error = 0;
    ctx->last_error = 0;
    return 0;
}

int cl_socket_bind(cl_socket_context_t *ctx,
                   int fd,
                   const cl_sockaddr_t *addr,
                   cl_socklen_t addr_len) {
    cl_socket_fd_t *slot = socket_slot(ctx, fd);
    if (!slot) {
        return socket_fail(ctx, CL_SOCKET_EBADF);
    }
    if (slot->handle) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_EINVAL);
    }
    cl_net_addr_t local;
    int addr_ret = sockaddr_to_net_addr_checked(addr, addr_len, &local,
                                                1, 1);
    if (addr_ret < 0) {
        return socket_fail_fd(ctx, slot, -addr_ret);
    }

    cl_net_handle_t handle = 0;
    int ret = cl_net_bind(ctx->net, (cl_net_proto_t)slot->proto, &local,
                          &handle);
    if (ret < 0) {
        slot->last_error = -ret;
        ctx->last_error = -ret;
        return ret;
    }
    slot->remote = local;
    slot->handle = handle;
    slot->has_remote = 0;
    slot->listening = 0;
    slot->flags = 0;
    slot->last_error = 0;
    ctx->last_error = 0;
    return 0;
}

int cl_socket_listen(cl_socket_context_t *ctx, int fd, int backlog) {
    cl_socket_fd_t *slot = socket_slot(ctx, fd);
    if (!slot) {
        return socket_fail(ctx, CL_SOCKET_EBADF);
    }
    if (slot->proto != (uint8_t)CL_NET_PROTO_TCP || !slot->handle ||
        backlog < 0) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_EINVAL);
    }
    int ret = cl_net_listen(ctx->net, slot->handle, (uint32_t)backlog);
    if (ret < 0) {
        slot->last_error = -ret;
        ctx->last_error = -ret;
        return ret;
    }
    slot->listening = 1;
    slot->flags = CLP_NET_SOCKET_FLAG_CONNECTED;
    slot->last_error = 0;
    ctx->last_error = 0;
    return 0;
}

int cl_socket_accept(cl_socket_context_t *ctx,
                     int fd,
                     cl_sockaddr_t *addr,
                     cl_socklen_t *addr_len) {
    cl_socket_fd_t *slot = socket_slot(ctx, fd);
    if (!slot) {
        return socket_fail(ctx, CL_SOCKET_EBADF);
    }
    if (slot->proto != (uint8_t)CL_NET_PROTO_TCP || !slot->handle ||
        !slot->listening) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_EINVAL);
    }

    int accepted_fd = socket_alloc_fd(ctx);
    if (!accepted_fd) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_ENOMEM);
    }

    cl_net_addr_t remote;
    cl_net_handle_t handle = 0;
    int ret = cl_net_accept(ctx->net, slot->handle, &remote, &handle);
    if (ret < 0) {
        slot->last_error = -ret;
        ctx->last_error = -ret;
        return ret;
    }
    if (!handle) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_EAGAIN);
    }

    int addr_ret = net_addr_to_sockaddr(&remote, addr, addr_len);
    if (addr_ret < 0) {
        (void)cl_net_close(ctx->net, handle);
        return socket_fail_fd(ctx, slot, -addr_ret);
    }

    cl_socket_fd_t *accepted = &ctx->fds[(size_t)accepted_fd - 1u];
    memset(accepted, 0, sizeof(*accepted));
    accepted->used = 1;
    accepted->proto = (uint8_t)CL_NET_PROTO_TCP;
    accepted->remote = remote;
    accepted->handle = handle;
    accepted->has_remote = 1;
    accepted->flags = CLP_NET_SOCKET_FLAG_CONNECTED;
    ctx->last_error = 0;
    slot->last_error = 0;
    return accepted_fd;
}

int cl_socket_send(cl_socket_context_t *ctx,
                   int fd,
                   const void *data,
                   size_t length,
                   int flags) {
    (void)flags;
    cl_socket_fd_t *slot = socket_slot(ctx, fd);
    if (!slot) {
        return socket_fail(ctx, CL_SOCKET_EBADF);
    }
    if (!slot->handle) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_ENOTCONN);
    }
    int ret = cl_net_send(ctx->net, slot->handle, data, length);
    if (ret < 0) {
        slot->last_error = -ret;
        ctx->last_error = -ret;
        return ret;
    }
    if (length && ret == 0) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_EAGAIN);
    }
    slot->last_error = 0;
    ctx->last_error = 0;
    return ret;
}

int cl_socket_sendto(cl_socket_context_t *ctx,
                     int fd,
                     const void *data,
                     size_t length,
                     int flags,
                     const cl_sockaddr_t *addr,
                     cl_socklen_t addr_len) {
    (void)flags;
    cl_socket_fd_t *slot = socket_slot(ctx, fd);
    if (!slot) {
        return socket_fail(ctx, CL_SOCKET_EBADF);
    }
    if (!addr) {
        return cl_socket_send(ctx, fd, data, length, flags);
    }
    if (slot->proto != (uint8_t)CL_NET_PROTO_UDP) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_EISCONN);
    }
    cl_net_addr_t remote;
    int addr_ret = sockaddr_to_net_addr_checked(addr, addr_len, &remote,
                                                0, 0);
    if (addr_ret < 0) {
        return socket_fail_fd(ctx, slot, -addr_ret);
    }
    if (!slot->handle) {
        cl_net_handle_t handle = 0;
        int open_ret = cl_net_open_udp(ctx->net, &handle);
        if (open_ret < 0) {
            slot->last_error = -open_ret;
            ctx->last_error = -open_ret;
            return open_ret;
        }
        slot->handle = handle;
    }
    int ret = cl_net_sendto(ctx->net, slot->handle, &remote, data, length);
    if (ret < 0) {
        slot->last_error = -ret;
        ctx->last_error = -ret;
        return ret;
    }
    if (length && ret == 0) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_EAGAIN);
    }
    slot->last_error = 0;
    ctx->last_error = 0;
    return ret;
}

int cl_socket_recv(cl_socket_context_t *ctx,
                   int fd,
                   void *data,
                   size_t capacity,
                   int flags) {
    (void)flags;
    cl_socket_fd_t *slot = socket_slot(ctx, fd);
    if (!slot) {
        return socket_fail(ctx, CL_SOCKET_EBADF);
    }
    if (!slot->handle) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_ENOTCONN);
    }
    int ret = cl_net_recv(ctx->net, slot->handle, data, capacity);
    if (ret < 0) {
        slot->last_error = -ret;
        ctx->last_error = -ret;
        return ret;
    }
    if (capacity && ret == 0) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_EAGAIN);
    }
    slot->last_error = 0;
    ctx->last_error = 0;
    return ret;
}

int cl_socket_recvfrom(cl_socket_context_t *ctx,
                       int fd,
                       void *data,
                       size_t capacity,
                       int flags,
                       cl_sockaddr_t *addr,
                       cl_socklen_t *addr_len) {
    cl_socket_fd_t *slot = socket_slot(ctx, fd);
    if (!slot) {
        return socket_fail(ctx, CL_SOCKET_EBADF);
    }
    if (slot->proto == (uint8_t)CL_NET_PROTO_UDP) {
        if (!slot->handle) {
            cl_net_handle_t handle = 0;
            int open_ret = cl_net_open_udp(ctx->net, &handle);
            if (open_ret < 0) {
                slot->last_error = -open_ret;
                ctx->last_error = -open_ret;
                return open_ret;
            }
            slot->handle = handle;
        }
        cl_net_addr_t remote;
        int ret = cl_net_recvfrom(ctx->net, slot->handle, &remote, data,
                                  capacity);
        if (ret < 0) {
            slot->last_error = -ret;
            ctx->last_error = -ret;
            return ret;
        }
        if (capacity && ret == 0) {
            return socket_fail_fd(ctx, slot, CL_SOCKET_EAGAIN);
        }
        if (ret > 0) {
            slot->remote = remote;
            slot->has_remote = 1;
        }
        int addr_ret = net_addr_to_sockaddr(slot->has_remote ? &slot->remote : NULL,
                                            addr, addr_len);
        if (addr_ret < 0) {
            return socket_fail_fd(ctx, slot, -addr_ret);
        }
        slot->last_error = 0;
        ctx->last_error = 0;
        return ret;
    }
    int ret = cl_socket_recv(ctx, fd, data, capacity, flags);
    if (ret < 0) {
        return ret;
    }
    int addr_ret = net_addr_to_sockaddr(&slot->remote, addr, addr_len);
    if (addr_ret < 0) {
        return socket_fail_fd(ctx, slot, -addr_ret);
    }
    return ret;
}

int cl_socket_poll(cl_socket_context_t *ctx, int fd, uint32_t *out_flags) {
    cl_socket_fd_t *slot = socket_slot(ctx, fd);
    if (!slot) {
        return socket_fail(ctx, CL_SOCKET_EBADF);
    }
    if (!slot->handle) {
        return socket_fail_fd(ctx, slot, CL_SOCKET_ENOTCONN);
    }
    int ret = cl_net_poll(ctx->net, slot->handle, &slot->flags);
    if (ret < 0) {
        slot->last_error = -ret;
        ctx->last_error = -ret;
        return ret;
    }
    if (out_flags) {
        *out_flags = slot->flags;
    }
    slot->last_error = 0;
    ctx->last_error = 0;
    return 0;
}

int cl_socket_select(cl_socket_context_t *ctx,
                     int nfds,
                     cl_fd_set_t *readfds,
                     cl_fd_set_t *writefds,
                     cl_fd_set_t *exceptfds,
                     uint32_t poll_attempts) {
    if (!ctx || !ctx->net || !ctx->fds || nfds < 0) {
        return socket_fail(ctx, CL_SOCKET_EINVAL);
    }
    uint32_t requested_read = readfds ? readfds->bits : 0;
    uint32_t requested_write = writefds ? writefds->bits : 0;
    uint32_t requested_except = exceptfds ? exceptfds->bits : 0;
    if (readfds) {
        readfds->bits = 0;
    }
    if (writefds) {
        writefds->bits = 0;
    }
    if (exceptfds) {
        exceptfds->bits = 0;
    }

    uint32_t requested = requested_read | requested_write | requested_except;
    if (!requested || nfds == 0) {
        return 0;
    }

    uint32_t max_fd = (uint32_t)nfds - 1u;
    if (max_fd > CL_SOCKET_FD_SETSIZE) {
        max_fd = CL_SOCKET_FD_SETSIZE;
    }
    uint32_t attempts = poll_attempts ? poll_attempts : 1u;
    while (attempts--) {
        cl_fd_set_t ready_read = {0};
        cl_fd_set_t ready_write = {0};
        int ready_count = 0;
        for (uint32_t fd = 1; fd <= max_fd; ++fd) {
            uint32_t bit = fd_bit((int)fd);
            if (!(requested & bit)) {
                continue;
            }
            cl_socket_fd_t *slot = socket_slot(ctx, (int)fd);
            if (!slot) {
                return socket_fail(ctx, CL_SOCKET_EBADF);
            }
            if (!slot->handle) {
                return socket_fail_fd(ctx, slot, CL_SOCKET_ENOTCONN);
            }
            int ret = cl_net_poll(ctx->net, slot->handle, &slot->flags);
            if (ret < 0) {
                slot->last_error = -ret;
                ctx->last_error = -ret;
                return ret;
            }
            uint8_t fd_ready = 0;
            if ((requested_read & bit) &&
                (slot->flags & CLP_NET_SOCKET_FLAG_READABLE)) {
                ready_read.bits |= bit;
                fd_ready = 1;
            }
            if ((requested_write & bit) &&
                (slot->flags & CLP_NET_SOCKET_FLAG_WRITABLE)) {
                ready_write.bits |= bit;
                fd_ready = 1;
            }
            if (fd_ready) {
                ++ready_count;
            }
        }
        if (ready_count) {
            if (readfds) {
                readfds->bits = ready_read.bits;
            }
            if (writefds) {
                writefds->bits = ready_write.bits;
            }
            ctx->last_error = 0;
            return ready_count;
        }
    }
    ctx->last_error = 0;
    return 0;
}

int cl_socket_set_nonblocking(cl_socket_context_t *ctx,
                              int fd,
                              int enabled) {
    cl_socket_fd_t *slot = socket_slot(ctx, fd);
    if (!slot) {
        return socket_fail(ctx, CL_SOCKET_EBADF);
    }
    slot->nonblocking = enabled ? 1u : 0u;
    slot->last_error = 0;
    ctx->last_error = 0;
    return 0;
}

int cl_socket_close(cl_socket_context_t *ctx, int fd) {
    cl_socket_fd_t *slot = socket_slot(ctx, fd);
    if (!slot) {
        return socket_fail(ctx, CL_SOCKET_EBADF);
    }
    int ret = 0;
    if (slot->handle) {
        ret = cl_net_close(ctx->net, slot->handle);
    }
    memset(slot, 0, sizeof(*slot));
    if (ret < 0) {
        ctx->last_error = -ret;
        return ret;
    }
    ctx->last_error = 0;
    return 0;
}

int cl_socket_shutdown(cl_socket_context_t *ctx, int fd, int how) {
    if (!socket_slot(ctx, fd)) {
        return socket_fail(ctx, CL_SOCKET_EBADF);
    }
    if (how != CL_SOCKET_SHUT_RD &&
        how != CL_SOCKET_SHUT_WR &&
        how != CL_SOCKET_SHUT_RDWR) {
        return socket_fail(ctx, CL_SOCKET_EINVAL);
    }
    return socket_fail(ctx, CL_SOCKET_ENOSYS);
}
