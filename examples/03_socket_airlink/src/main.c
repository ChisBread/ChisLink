#include "example_common.h"

#include "chislink/gba/hw.h"
#include "chislink/gba/text.h"
#include "chislink/gba/video.h"
#include "chislink/net.h"
#include "chislink/proto.h"
#include "chislink/socket.h"

#include <string.h>

#define AIR_UDP_PORT 13693u
#define AIR_TCP_PORT 13694u
#define AIR_PACKET_BYTES 24u
#define AIR_MAGIC 0x52414c43u
#define AIR_VERSION 1u
#define AIR_DISCOVER 1u
#define AIR_OFFER 2u
#define AIR_HELLO 3u
#define AIR_ACK 4u
#define AIR_FRAMES 300u
#define AIR_RETRY_FRAMES 180u
#define AIR_BROADCAST_INTERVAL 30u
#define SOCKET_COUNT 4u

typedef struct air_packet {
    uint8_t type;
    uint32_t nonce;
    uint32_t peer_nonce;
    uint16_t tcp_port;
} air_packet_t;

static example_link_t g_link;
static cl_net_t g_net;
static uint8_t g_net_scratch[96];
static cl_socket_context_t g_sockets;
static cl_socket_fd_t g_socket_fds[SOCKET_COUNT];
static cl_net_status_t g_status;
static uint32_t g_nonce;
static uint32_t g_peer_ip;
static uint32_t g_peer_nonce;
static uint16_t g_udp_sent;
static uint16_t g_udp_recv;
static uint16_t g_tcp_sent;
static uint16_t g_tcp_recv;
static uint8_t g_peer_count;
static uint8_t g_done;
static int g_last_error;

static uint32_t load_le32(const uint8_t *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8u) |
           ((uint32_t)src[2] << 16u) | ((uint32_t)src[3] << 24u);
}

static void store_le32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8u);
    dst[2] = (uint8_t)(value >> 16u);
    dst[3] = (uint8_t)(value >> 24u);
}

static uint32_t air_checksum(uint8_t type,
                             uint32_t nonce,
                             uint32_t peer_nonce,
                             uint16_t tcp_port) {
    uint32_t value = AIR_MAGIC ^ ((uint32_t)AIR_VERSION << 24u) ^
        ((uint32_t)type << 16u) ^ nonce ^
        ((peer_nonce << 7u) | (peer_nonce >> 25u)) ^
        ((uint32_t)tcp_port << 1u);
    return value ^ 0xa17c1369u;
}

static void air_build(uint8_t out[AIR_PACKET_BYTES],
                      uint8_t type,
                      uint32_t nonce,
                      uint32_t peer_nonce,
                      uint16_t tcp_port) {
    store_le32(out, AIR_MAGIC);
    store_le32(out + 4u, AIR_VERSION | ((uint32_t)type << 8u));
    store_le32(out + 8u, nonce);
    store_le32(out + 12u, peer_nonce);
    store_le32(out + 16u, tcp_port);
    store_le32(out + 20u,
               air_checksum(type, nonce, peer_nonce, tcp_port));
}

static int air_parse(const uint8_t data[AIR_PACKET_BYTES], air_packet_t *out) {
    if (!data || !out || load_le32(data) != AIR_MAGIC) {
        return -1;
    }
    uint32_t version_type = load_le32(data + 4u);
    uint8_t version = (uint8_t)(version_type & 0xffu);
    uint8_t type = (uint8_t)((version_type >> 8u) & 0xffu);
    uint32_t nonce = load_le32(data + 8u);
    uint32_t peer_nonce = load_le32(data + 12u);
    uint16_t tcp_port = (uint16_t)load_le32(data + 16u);
    if (version != AIR_VERSION || type < AIR_DISCOVER || type > AIR_ACK ||
        !nonce || !tcp_port ||
        load_le32(data + 20u) !=
            air_checksum(type, nonce, peer_nonce, tcp_port)) {
        return -2;
    }
    out->type = type;
    out->nonce = nonce;
    out->peer_nonce = peer_nonce;
    out->tcp_port = tcp_port;
    return 0;
}

static void sockaddr4(cl_sockaddr_in_t *addr, uint32_t ip, uint16_t port) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = CL_AF_INET;
    addr->sin_port = cl_htons(port);
    addr->sin_addr.s_addr = cl_htonl(ip);
}

static uint32_t sockaddr4_ip(const cl_sockaddr_in_t *addr) {
    return addr ? cl_ntohl(addr->sin_addr.s_addr) : 0u;
}

static int send_packet(int fd,
                       uint32_t ip,
                       uint16_t port,
                       uint8_t type,
                       uint32_t nonce,
                       uint32_t peer_nonce,
                       uint16_t tcp_port) {
    uint8_t packet[AIR_PACKET_BYTES];
    cl_sockaddr_in_t addr;
    air_build(packet, type, nonce, peer_nonce, tcp_port);
    sockaddr4(&addr, ip, port);
    int ret = cl_socket_sendto(&g_sockets, fd, packet, sizeof(packet), 0,
                               (const cl_sockaddr_t *)&addr, sizeof(addr));
    if (ret == (int)sizeof(packet)) {
        g_udp_sent += (uint16_t)ret;
        return 0;
    }
    return ret < 0 ? ret : -5;
}

static int send_all(int fd, const uint8_t *data, uint32_t length) {
    uint32_t done = 0;
    uint32_t waited = 0;
    while (done < length && waited < AIR_RETRY_FRAMES) {
        int ret = cl_socket_send(&g_sockets, fd, data + done,
                                 length - done, 0);
        if (ret < 0 && cl_socket_errno(&g_sockets) != CL_SOCKET_EAGAIN) {
            return ret;
        }
        if (ret > 0) {
            done += (uint32_t)ret;
            continue;
        }
        cl_gba_wait_vblank();
        waited++;
    }
    g_tcp_sent += (uint16_t)done;
    return done == length ? 0 : CL_CLIENT_STATUS_ERROR(CLP_STATUS_TIMEOUT);
}

static int recv_exact(int fd, uint8_t *data, uint32_t length) {
    uint32_t done = 0;
    uint32_t waited = 0;
    while (done < length && waited < AIR_RETRY_FRAMES) {
        int ret = cl_socket_recv(&g_sockets, fd, data + done,
                                 length - done, 0);
        if (ret < 0 && cl_socket_errno(&g_sockets) != CL_SOCKET_EAGAIN) {
            return ret;
        }
        if (ret > 0) {
            done += (uint32_t)ret;
            continue;
        }
        cl_gba_wait_vblank();
        waited++;
    }
    g_tcp_recv += (uint16_t)done;
    return done == length ? 0 : CL_CLIENT_STATUS_ERROR(CLP_STATUS_TIMEOUT);
}

static int dial_peer(uint16_t peer_tcp_port) {
    cl_sockaddr_in_t addr;
    int fd = cl_socket_socket(&g_sockets, CL_AF_INET, CL_SOCK_STREAM, 0);
    if (fd < 0) {
        return fd;
    }
    sockaddr4(&addr, g_peer_ip, peer_tcp_port);
    int ret = cl_socket_connect(&g_sockets, fd,
                                (const cl_sockaddr_t *)&addr, sizeof(addr));
    if (ret < 0) {
        (void)cl_socket_close(&g_sockets, fd);
        return ret;
    }

    uint8_t packet[AIR_PACKET_BYTES];
    air_build(packet, AIR_HELLO, g_nonce, g_peer_nonce, AIR_TCP_PORT);
    ret = send_all(fd, packet, sizeof(packet));
    if (ret == 0) {
        ret = recv_exact(fd, packet, sizeof(packet));
    }
    if (ret == 0) {
        air_packet_t ack;
        ret = air_parse(packet, &ack);
        if (ret == 0 && (ack.type != AIR_ACK ||
                         ack.nonce != g_peer_nonce ||
                         ack.peer_nonce != g_nonce)) {
            ret = -2;
        }
    }
    (void)cl_socket_close(&g_sockets, fd);
    return ret;
}

static int accept_peer(int listen_fd) {
    int fd = -1;
    uint32_t waited = 0;
    while (waited < AIR_RETRY_FRAMES) {
        cl_sockaddr_in_t remote;
        cl_socklen_t len = sizeof(remote);
        fd = cl_socket_accept(&g_sockets, listen_fd,
                              (cl_sockaddr_t *)&remote, &len);
        if (fd < 0 && cl_socket_errno(&g_sockets) != CL_SOCKET_EAGAIN) {
            return fd;
        }
        if (fd > 0) {
            break;
        }
        cl_gba_wait_vblank();
        waited++;
    }
    if (fd < 0) {
        return CL_CLIENT_STATUS_ERROR(CLP_STATUS_TIMEOUT);
    }

    uint8_t packet[AIR_PACKET_BYTES];
    int ret = recv_exact(fd, packet, sizeof(packet));
    if (ret == 0) {
        air_packet_t hello;
        ret = air_parse(packet, &hello);
        if (ret == 0 && (hello.type != AIR_HELLO ||
                         hello.nonce != g_peer_nonce ||
                         hello.peer_nonce != g_nonce)) {
            ret = -2;
        }
    }
    if (ret == 0) {
        air_build(packet, AIR_ACK, g_nonce, g_peer_nonce, AIR_TCP_PORT);
        ret = send_all(fd, packet, sizeof(packet));
    }
    (void)cl_socket_close(&g_sockets, fd);
    return ret;
}

static int run_air_link(void) {
    g_udp_sent = 0;
    g_udp_recv = 0;
    g_tcp_sent = 0;
    g_tcp_recv = 0;
    g_peer_count = 0;
    g_done = 0;
    g_peer_ip = 0;
    g_peer_nonce = 0;

    int ret = cl_net_status(&g_net, CLP_NET_STATUS_CONNECT_IF_NEEDED,
                            &g_status);
    if (ret < 0) {
        return ret;
    }
    g_nonce = cl_gba_time_ticks() ^ g_status.ip ^ 0x584c4143u;
    if (!g_nonce) {
        g_nonce = 0x13579bdfu;
    }

    int udp_fd = cl_socket_socket(&g_sockets, CL_AF_INET, CL_SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        return udp_fd;
    }
    int listen_fd = cl_socket_socket(&g_sockets, CL_AF_INET,
                                     CL_SOCK_STREAM, 0);
    if (listen_fd < 0) {
        (void)cl_socket_close(&g_sockets, udp_fd);
        return listen_fd;
    }
    cl_sockaddr_in_t addr;
    sockaddr4(&addr, 0u, AIR_UDP_PORT);
    ret = cl_socket_bind(&g_sockets, udp_fd,
                         (const cl_sockaddr_t *)&addr, sizeof(addr));
    if (ret == 0) {
        sockaddr4(&addr, 0u, AIR_TCP_PORT);
        ret = cl_socket_bind(&g_sockets, listen_fd,
                             (const cl_sockaddr_t *)&addr, sizeof(addr));
    }
    if (ret == 0) {
        ret = cl_socket_listen(&g_sockets, listen_fd, 1);
    }
    if (ret < 0) {
        (void)cl_socket_close(&g_sockets, listen_fd);
        (void)cl_socket_close(&g_sockets, udp_fd);
        return ret;
    }

    for (uint32_t frame = 0; frame < AIR_FRAMES && !g_peer_nonce; ++frame) {
        if ((frame % AIR_BROADCAST_INTERVAL) == 0) {
            int send_ret = send_packet(udp_fd, 0xffffffffu, AIR_UDP_PORT,
                                       AIR_DISCOVER, g_nonce, 0u,
                                       AIR_TCP_PORT);
            if (send_ret < 0 &&
                cl_socket_errno(&g_sockets) != CL_SOCKET_EAGAIN) {
                ret = send_ret;
                break;
            }
        }

        for (uint8_t i = 0; i < 4u; ++i) {
            uint8_t packet[AIR_PACKET_BYTES];
            cl_sockaddr_in_t remote;
            cl_socklen_t len = sizeof(remote);
            int n = cl_socket_recvfrom(&g_sockets, udp_fd, packet,
                                       sizeof(packet), 0,
                                       (cl_sockaddr_t *)&remote, &len);
            if (n < 0 && cl_socket_errno(&g_sockets) != CL_SOCKET_EAGAIN) {
                ret = n;
                break;
            }
            if (n <= 0) {
                break;
            }
            g_udp_recv += (uint16_t)n;
            uint32_t remote_ip = sockaddr4_ip(&remote);
            if (remote_ip == g_status.ip) {
                continue;
            }
            air_packet_t air;
            if (n != AIR_PACKET_BYTES || air_parse(packet, &air) < 0 ||
                air.nonce == g_nonce) {
                continue;
            }
            if (g_peer_nonce != air.nonce && g_peer_count < 255u) {
                g_peer_count++;
            }
            if (air.type == AIR_DISCOVER) {
                (void)send_packet(udp_fd, remote_ip, AIR_UDP_PORT,
                                  AIR_OFFER, g_nonce, air.nonce,
                                  AIR_TCP_PORT);
                g_peer_ip = remote_ip;
                g_peer_nonce = air.nonce;
            } else if (air.type == AIR_OFFER && air.peer_nonce == g_nonce) {
                g_peer_ip = remote_ip;
                g_peer_nonce = air.nonce;
            }
        }
        cl_gba_wait_vblank();
    }

    if (ret == 0 && g_peer_nonce) {
        ret = g_nonce < g_peer_nonce ? dial_peer(AIR_TCP_PORT) :
                                       accept_peer(listen_fd);
        if (ret == 0) {
            g_done = 1u;
        }
    }

    (void)cl_socket_close(&g_sockets, listen_fd);
    (void)cl_socket_close(&g_sockets, udp_fd);
    return ret;
}

static void draw(void) {
    ex_clear_body();
    cl_gba_text_draw(16, 34, "Air Link over socket API", EX_COLOR_TEXT);
    cl_gba_text_draw(16, 50, "IP", EX_COLOR_DIM);
    ex_draw_ipv4(56, 50, g_status.ip, EX_COLOR_TEXT);
    cl_gba_text_draw(16, 66, "PEER", EX_COLOR_DIM);
    if (g_peer_ip) {
        ex_draw_ipv4(56, 66, g_peer_ip, EX_COLOR_OK);
    } else {
        cl_gba_text_draw(56, 66, "-", EX_COLOR_DIM);
    }
    cl_gba_text_draw(16, 84, "UDP TX/RX", EX_COLOR_DIM);
    ex_draw_u32_dec(96, 84, g_udp_sent, EX_COLOR_TEXT);
    cl_gba_text_draw(128, 84, "/", EX_COLOR_DIM);
    ex_draw_u32_dec(144, 84, g_udp_recv, EX_COLOR_TEXT);
    cl_gba_text_draw(16, 100, "TCP TX/RX", EX_COLOR_DIM);
    ex_draw_u32_dec(96, 100, g_tcp_sent, EX_COLOR_TEXT);
    cl_gba_text_draw(128, 100, "/", EX_COLOR_DIM);
    ex_draw_u32_dec(144, 100, g_tcp_recv, EX_COLOR_TEXT);
    cl_gba_text_draw(16, 116, "RESULT", EX_COLOR_DIM);
    cl_gba_text_draw(80, 116, g_done ? "OK" :
                     (g_peer_count ? "PEER" : "IDLE"),
                     g_done ? EX_COLOR_OK : EX_COLOR_TEXT);
    ex_draw_error(168, 34, g_last_error);
    ex_draw_footer("A RUN AIR LINK");
    ex_present();
}

int main(void) {
    ex_video_init("SDK SOCKET AIR LINK");
    if (ex_link_init(&g_link) && ex_link_hello(&g_link) &&
        cl_net_init(&g_net, &g_link.client,
                    g_net_scratch, sizeof(g_net_scratch)) == 0 &&
        cl_socket_init(&g_sockets, &g_net,
                       g_socket_fds, SOCKET_COUNT) == 0) {
        (void)cl_net_status(&g_net, 0, &g_status);
    } else {
        g_last_error = g_link.last_error ? g_link.last_error : -1;
    }

    while (1) {
        uint16_t pressed = ex_keys_pressed();
        if (pressed & CL_GBA_KEY_A) {
            g_last_error = run_air_link();
        }
        draw();
        cl_gba_wait_vblank();
    }
}
