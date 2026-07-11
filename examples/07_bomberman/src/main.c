/* Bomberman — 2-player multiplayer over BLE or WiFi
 *
 * Controls: D-pad = move, A = place bomb, START = exit
 * Arena:   13x9 grid, text-rendered.  # = wall, * = soft block,
 *           P = player 1 (host), p = player 2 (join),
 *           B = bomb, X = explosion
 */

#define CHISLINK_FILE_POSIX_NAMES
#include "example_common.h"

#include "chislink/ble_link_io.h"
#include "chislink/file.h"
#include "chislink/gba/hw.h"
#include "chislink/gba/text.h"
#include "chislink/gba/video.h"
#include "chislink/net.h"
#include "chislink/socket.h"
#include "tiles.h"

#include <string.h>

/* === Game constants === */
#define ARENA_W 13u
#define ARENA_H  9u
#define CELL_EMPTY 0u
#define CELL_HARD  1u
#define CELL_SOFT  2u
#define BOMB_FUSE  90u
#define EXPLOSION_FRAMES 30u
#define BOMB_RANGE 3u

/* === Network protocol === */
#define MSG_MOVE  0x01u
#define MSG_BOMB  0x02u

/* === WiFi discovery === */
#define WIFI_UDP_PORT 13693u
#define WIFI_TCP_PORT 13694u
#define WIFI_SOCKET_COUNT 4u
#define WIFI_SCRATCH_BYTES 128u
#define WIFI_MAGIC 0x434d4242u  /* "BMBB" */
#define WIFI_DISCOVER_FRAMES 300u

/* === Game state === */
typedef struct {
    uint8_t x, y;
    uint8_t alive;
    uint8_t bomb_active;
    uint8_t bomb_x, bomb_y;
    uint32_t bomb_timer;
} player_t;

typedef enum {
    TRANSPORT_BLE = 0,
    TRANSPORT_WIFI = 1,
} transport_mode_t;

static example_link_t g_link;
static cl_ble_t g_ble;
static cl_net_t g_net;
static cl_socket_context_t g_sockets;
static cl_socket_fd_t g_socket_fds[WIFI_SOCKET_COUNT];
static uint32_t g_peer_ip;
static union {
    uint8_t ble[CL_BLE_SCRATCH_FULL_BYTES];
    uint8_t net[WIFI_SCRATCH_BYTES];
} g_radio_scratch;
static uint8_t g_arena[ARENA_H][ARENA_W];
static uint8_t g_explosion[ARENA_H][ARENA_W];
static player_t g_players[2];
static int g_fd;
static transport_mode_t g_transport;
static uint8_t g_is_host;
static uint8_t g_my_id;
static int g_last_error;
static uint32_t g_frame;

static const char *transport_name(void) {
    return g_transport == TRANSPORT_WIFI ? "WiFi" : "BLE";
}

/* === Arena setup === */
static void arena_init(void) {
    memset(g_arena, 0, sizeof(g_arena));
    memset(g_explosion, 0, sizeof(g_explosion));
    for (uint8_t y = 0; y < ARENA_H; ++y) {
        g_arena[y][0] = CELL_HARD; g_arena[y][ARENA_W - 1u] = CELL_HARD;
    }
    for (uint8_t x = 0; x < ARENA_W; ++x) {
        g_arena[0][x] = CELL_HARD; g_arena[ARENA_H - 1u][x] = CELL_HARD;
    }
    for (uint8_t y = 2u; y < ARENA_H - 2u; y += 2u)
        for (uint8_t x = 2u; x < ARENA_W - 2u; x += 2u)
            g_arena[y][x] = CELL_HARD;
    for (uint8_t y = 1u; y < ARENA_H - 1u; ++y)
        for (uint8_t x = 1u; x < ARENA_W - 1u; ++x)
            if (g_arena[y][x] == CELL_EMPTY && ((x * 7u + y * 13u) % 5u) < 2u)
                g_arena[y][x] = CELL_SOFT;
    g_arena[1][1] = CELL_EMPTY; g_arena[1][2] = CELL_EMPTY; g_arena[2][1] = CELL_EMPTY;
    g_arena[ARENA_H-2u][ARENA_W-2u] = CELL_EMPTY;
    g_arena[ARENA_H-2u][ARENA_W-3u] = CELL_EMPTY;
    g_arena[ARENA_H-3u][ARENA_W-2u] = CELL_EMPTY;
    g_players[0].x = 1; g_players[0].y = 1; g_players[0].alive = 1;
    g_players[1].x = ARENA_W - 2u; g_players[1].y = ARENA_H - 2u;
    g_players[1].alive = 1;
    g_players[0].bomb_active = 0; g_players[1].bomb_active = 0;
}

/* ============ WiFi transport ============ */

static void sockaddr4(cl_sockaddr_t *addr, uint32_t ip, uint16_t port) {
    cl_sockaddr_in_t *a4 = (cl_sockaddr_in_t *)addr;
    memset(a4, 0, sizeof(*a4));
    a4->sin_family = CL_AF_INET;
    a4->sin_port = cl_htons(port);
    a4->sin_addr.s_addr = cl_htonl(ip);
}

static int wifi_connect(void) {
    cl_net_status_t st;
    int ret = cl_net_status(&g_net, 1u, &st);  /* flag=1 = connect if needed */
    if (ret < 0) return ret;
    return st.flags & 0x01u ? 0 : -1;  /* bit 0 = connected */
}

static int wifi_init(void) {
    cl_net_init(&g_net, &g_link.client, g_radio_scratch.net,
                sizeof(g_radio_scratch.net));
    cl_socket_init(&g_sockets, &g_net, g_socket_fds, WIFI_SOCKET_COUNT);
    /* Wait for WiFi connection */
    for (int i = 0; i < 600; ++i) {
        if (wifi_connect() == 0) return 0;
        cl_gba_wait_vblank();
    }
    return -1;
}

static int wifi_host(void) {
    /* Host: listen on TCP, respond to UDP discovery */
    int listen_fd = cl_socket_socket(&g_sockets, CL_AF_INET, CL_SOCK_STREAM, 0);
    if (listen_fd < 0) return listen_fd;
    cl_sockaddr_t addr;
    sockaddr4(&addr, 0u, WIFI_TCP_PORT);
    if (cl_socket_bind(&g_sockets, listen_fd, &addr, sizeof(addr)) < 0) return -2;
    if (cl_socket_listen(&g_sockets, listen_fd, 1) < 0) return -3;

    int udp_fd = cl_socket_socket(&g_sockets, CL_AF_INET, CL_SOCK_DGRAM, 0);
    if (udp_fd < 0) return udp_fd;
    sockaddr4(&addr, 0u, WIFI_UDP_PORT);
    if (cl_socket_bind(&g_sockets, udp_fd, &addr, sizeof(addr)) < 0) return -4;

    cl_net_status_t st;
    cl_net_status(&g_net, 0, &st);
    uint32_t my_ip = st.ip;

    /* Discovery loop: reply to DISCOVER, wait for TCP connect */
    for (uint32_t frame = 0; frame < WIFI_DISCOVER_FRAMES; ++frame) {
        /* Accept TCP connection */
        cl_sockaddr_t remote;
        cl_socklen_t rlen = sizeof(remote);
        int client_fd = cl_socket_accept(&g_sockets, listen_fd, &remote, &rlen);
        if (client_fd > 0) {
            cl_socket_close(&g_sockets, udp_fd);
            cl_socket_close(&g_sockets, listen_fd);
            return client_fd;
        }
        /* Reply to UDP discovery */
        uint8_t pkt[16];
        cl_sockaddr_t sender;
        cl_socklen_t slen = sizeof(sender);
        int nr = cl_socket_recvfrom(&g_sockets, udp_fd, pkt, sizeof(pkt), 0,
                                     &sender, &slen);
        if (nr >= 8) {
            uint32_t magic = (uint32_t)pkt[0] | ((uint32_t)pkt[1] << 8u) |
                             ((uint32_t)pkt[2] << 16u) | ((uint32_t)pkt[3] << 24u);
            /* Ignore own broadcast */
            cl_sockaddr_in_t *s4 = (cl_sockaddr_in_t *)&sender;
            if (magic == WIFI_MAGIC && cl_ntohl(s4->sin_addr.s_addr) != my_ip) {
                uint8_t reply[8];
                reply[0] = (uint8_t)(WIFI_MAGIC & 0xffu); reply[1] = (uint8_t)(WIFI_MAGIC >> 8u);
                reply[2] = (uint8_t)(WIFI_MAGIC >> 16u); reply[3] = (uint8_t)(WIFI_MAGIC >> 24u);
                reply[4] = (uint8_t)(WIFI_TCP_PORT & 0xffu); reply[5] = (uint8_t)(WIFI_TCP_PORT >> 8u);
                reply[6] = 0; reply[7] = 0;
                cl_socket_sendto(&g_sockets, udp_fd, reply, 8u, 0, &sender, slen);
            }
        }
        cl_gba_wait_vblank();
    }
    cl_socket_close(&g_sockets, udp_fd);
    cl_socket_close(&g_sockets, listen_fd);
    return -5;
}

static int wifi_join(void) {
    int udp_fd = cl_socket_socket(&g_sockets, CL_AF_INET, CL_SOCK_DGRAM, 0);
    if (udp_fd < 0) return udp_fd;
    cl_sockaddr_t addr;
    sockaddr4(&addr, 0u, WIFI_UDP_PORT);
    cl_socket_bind(&g_sockets, udp_fd, &addr, sizeof(addr));

    cl_net_status_t st;
    cl_net_status(&g_net, 0, &st);
    uint32_t my_ip = st.ip;

    uint8_t discover[8];
    discover[0] = (uint8_t)(WIFI_MAGIC & 0xffu); discover[1] = (uint8_t)(WIFI_MAGIC >> 8u);
    discover[2] = (uint8_t)(WIFI_MAGIC >> 16u); discover[3] = (uint8_t)(WIFI_MAGIC >> 24u);
    discover[4] = 0; discover[5] = 0; discover[6] = 0; discover[7] = 0;

    for (uint32_t frame = 0; frame < WIFI_DISCOVER_FRAMES; ++frame) {
        if ((frame % 30u) == 0)
            cl_socket_sendto(&g_sockets, udp_fd, discover, 8u, 0,
                             (cl_sockaddr_t *)&(cl_sockaddr_in_t){
                                 .sin_family = CL_AF_INET,
                                 .sin_port = cl_htons(WIFI_UDP_PORT),
                                 .sin_addr.s_addr = cl_htonl(0xffffffffu)
                             }, sizeof(cl_sockaddr_in_t));
        uint8_t reply[16];
        cl_sockaddr_t sender;
        cl_socklen_t slen = sizeof(sender);
        int nr = cl_socket_recvfrom(&g_sockets, udp_fd, reply, sizeof(reply), 0,
                                     &sender, &slen);
        if (nr >= 8) {
            uint32_t magic = (uint32_t)reply[0] | ((uint32_t)reply[1] << 8u) |
                             ((uint32_t)reply[2] << 16u) | ((uint32_t)reply[3] << 24u);
            if (magic == WIFI_MAGIC) {
                uint16_t tcp_port = (uint16_t)reply[4] | ((uint16_t)reply[5] << 8u);
                uint32_t sender_ip = cl_ntohl(((cl_sockaddr_in_t *)&sender)->sin_addr.s_addr);
                if (sender_ip == my_ip || tcp_port == 0)
                    continue;
                g_peer_ip = sender_ip;
                cl_socket_close(&g_sockets, udp_fd);
                /* TCP connect */
                int tcp_fd = cl_socket_socket(&g_sockets, CL_AF_INET, CL_SOCK_STREAM, 0);
                cl_sockaddr_t tcp_addr;
                sockaddr4(&tcp_addr, g_peer_ip, tcp_port);
                if (cl_socket_connect(&g_sockets, tcp_fd, &tcp_addr, sizeof(tcp_addr)) < 0)
                    return -6;
                return tcp_fd;
            }
        }
        cl_gba_wait_vblank();
    }
    cl_socket_close(&g_sockets, udp_fd);
    return -5;
}

static void wifi_send(const uint8_t *data, uint32_t len) {
    if (g_fd <= 0) return;
    cl_socket_send(&g_sockets, g_fd, data, (size_t)len, 0);
}

static int wifi_recv(uint8_t *buf, uint32_t cap) {
    if (g_fd <= 0) return 0;
    int ret = cl_socket_recv(&g_sockets, g_fd, buf, (size_t)cap, 0);
    if (ret == -CL_SOCKET_EAGAIN) return 0;
    return ret;
}

/* ============ Common transport wrappers ============ */

static void send_msg(const uint8_t *data, uint32_t len) {
    if (g_fd <= 0) return;
    if (g_transport == TRANSPORT_WIFI)
        wifi_send(data, len);
    else
        cl_ble_link_io_send(&g_ble, g_fd, data, len);
}

static int recv_msg(uint8_t *buf, uint32_t cap) {
    if (g_fd <= 0) return 0;
    if (g_transport == TRANSPORT_WIFI)
        return wifi_recv(buf, cap);
    return cl_ble_link_io_recv(&g_ble, g_fd, buf, cap);
}

static int net_init(uint8_t is_host) {
    g_is_host = is_host;
    if (g_transport == TRANSPORT_WIFI) {
        int ret = wifi_init();
        if (ret < 0) return ret;
        ret = is_host ? wifi_host() : wifi_join();
        if (ret < 0) return ret;
        g_fd = ret;
        return 0;
    }

    const char *name = is_host ? "Bomber1" : "Bomber2";
    int ret = cl_ble_init(&g_ble, &g_link.client, g_radio_scratch.ble,
                          sizeof(g_radio_scratch.ble));
    if (ret < 0) return ret;
    ret = cl_ble_link_io_open(&g_ble, is_host ? CL_BLE_LINK_IO_HOST : CL_BLE_LINK_IO_JOIN, name);
    if (ret < 0) return ret;
    g_fd = ret;
    return 0;
}

static int net_wait_ready(void) {
    if (g_transport == TRANSPORT_WIFI)
        return g_fd > 0 ? 1 : 0;  /* WiFi: TCP connect is already done */

    if (g_is_host) {
        for (uint32_t i = 0; i < 600u; ++i) {
            int r = cl_ble_link_peer_ready(&g_ble, (cl_ble_handle_t)(uint16_t)g_fd);
            if (r > 0) {
                g_fd = r;
                return 1;
            }
            if (r < 0) return r;
            cl_gba_wait_vblank();
        }
        return 0;
    }
    return g_fd > 0 ? 1 : 0;
}

/* ============ Game logic ============ */

static void send_move(void) {
    uint8_t msg[4];
    msg[0] = MSG_MOVE; msg[1] = g_my_id;
    msg[2] = g_players[g_my_id].x; msg[3] = g_players[g_my_id].y;
    send_msg(msg, sizeof(msg));
}

static void send_bomb(uint8_t x, uint8_t y) {
    uint8_t msg[4];
    msg[0] = MSG_BOMB; msg[1] = g_my_id;
    msg[2] = x; msg[3] = y;
    send_msg(msg, sizeof(msg));
}

static void process_msg(const uint8_t *msg, uint32_t len) {
    if (len < 3) return;
    uint8_t pid = msg[1] & 1u;
    if (pid == g_my_id) return;
    switch (msg[0]) {
    case MSG_MOVE:
        if (len >= 4) { g_players[pid].x = msg[2]; g_players[pid].y = msg[3]; }
        break;
    case MSG_BOMB:
        if (len >= 4 && g_players[pid].alive) {
            g_players[pid].bomb_active = 1;
            g_players[pid].bomb_x = msg[2]; g_players[pid].bomb_y = msg[3];
            g_players[pid].bomb_timer = BOMB_FUSE;
        }
        break;
    }
}

static int try_move(uint8_t pid, int8_t dx, int8_t dy) {
    player_t *p = &g_players[pid];
    if (!p->alive) return 0;
    int nx = (int)p->x + dx, ny = (int)p->y + dy;
    if (nx < 0 || nx >= (int)ARENA_W || ny < 0 || ny >= (int)ARENA_H) return 0;
    if (g_arena[ny][nx] == CELL_HARD || g_arena[ny][nx] == CELL_SOFT) return 0;
    if (g_arena[ny][nx] == CELL_EMPTY && g_explosion[ny][nx]) return 0;
    uint8_t other = pid ^ 1u;
    if (g_players[other].alive && g_players[other].x == (uint8_t)nx &&
        g_players[other].y == (uint8_t)ny) return 0;
    p->x = (uint8_t)nx; p->y = (uint8_t)ny;
    return 1;
}

static void place_bomb(uint8_t pid) {
    player_t *p = &g_players[pid];
    if (!p->alive || p->bomb_active) return;
    for (uint8_t i = 0; i < 2u; ++i)
        if (g_players[i].bomb_active && g_players[i].bomb_x == p->x &&
            g_players[i].bomb_y == p->y) return;
    p->bomb_active = 1; p->bomb_x = p->x; p->bomb_y = p->y;
    p->bomb_timer = BOMB_FUSE;
    send_bomb(p->x, p->y);
}

static void explode(uint8_t bx, uint8_t by) {
    g_explosion[by][bx] = EXPLOSION_FRAMES;
    for (uint8_t d = 1; d <= BOMB_RANGE; ++d) {
        if (bx + d >= ARENA_W || g_arena[by][bx + d] == CELL_HARD) break;
        g_explosion[by][bx + d] = EXPLOSION_FRAMES;
        if (g_arena[by][bx + d] == CELL_SOFT) { g_arena[by][bx + d] = CELL_EMPTY; break; }
    }
    for (uint8_t d = 1; d <= BOMB_RANGE; ++d) {
        if (bx < d || g_arena[by][bx - d] == CELL_HARD) break;
        g_explosion[by][bx - d] = EXPLOSION_FRAMES;
        if (g_arena[by][bx - d] == CELL_SOFT) { g_arena[by][bx - d] = CELL_EMPTY; break; }
    }
    for (uint8_t d = 1; d <= BOMB_RANGE; ++d) {
        if (by + d >= ARENA_H || g_arena[by + d][bx] == CELL_HARD) break;
        g_explosion[by + d][bx] = EXPLOSION_FRAMES;
        if (g_arena[by + d][bx] == CELL_SOFT) { g_arena[by + d][bx] = CELL_EMPTY; break; }
    }
    for (uint8_t d = 1; d <= BOMB_RANGE; ++d) {
        if (by < d || g_arena[by - d][bx] == CELL_HARD) break;
        g_explosion[by - d][bx] = EXPLOSION_FRAMES;
        if (g_arena[by - d][bx] == CELL_SOFT) { g_arena[by - d][bx] = CELL_EMPTY; break; }
    }
}

static void tick_bombs(void) {
    for (uint8_t i = 0; i < 2u; ++i) {
        player_t *p = &g_players[i];
        if (!p->bomb_active) continue;
        if (p->bomb_timer > 0) { p->bomb_timer--; continue; }
        p->bomb_active = 0;
        explode(p->bomb_x, p->bomb_y);
        for (uint8_t j = 0; j < 2u; ++j)
            if (g_players[j].alive && g_explosion[g_players[j].y][g_players[j].x])
                g_players[j].alive = 0;
    }
}

static void tick_explosions(void) {
    for (uint8_t y = 0; y < ARENA_H; ++y)
        for (uint8_t x = 0; x < ARENA_W; ++x)
            if (g_explosion[y][x] > 0) g_explosion[y][x]--;
}

/* ============ Render ============ */

static void draw_arena(void) {
    ex_clear_body();
    tiles_begin_frame();

    /* Draw arena sprites centred below the header. */
    int mx = 8, my = 4;  /* tile-space offset */
    for (uint8_t y = 0; y < ARENA_H; ++y) {
        for (uint8_t x = 0; x < ARENA_W; ++x) {
            uint16_t tile = (uint16_t)TILE_FLOOR;

            if (g_explosion[y][x])
                tile = TILE_EXPLODE;
            else if (g_players[0].alive && g_players[0].x == x && g_players[0].y == y)
                tile = TILE_P1;
            else if (g_players[1].alive && g_players[1].x == x && g_players[1].y == y)
                tile = TILE_P2;
            else if (!g_players[0].alive && g_players[0].x == x && g_players[0].y == y)
                tile = TILE_DEAD;
            else if (!g_players[1].alive && g_players[1].x == x && g_players[1].y == y)
                tile = TILE_DEAD;
            else switch (g_arena[y][x]) {
                case CELL_HARD: tile = TILE_WALL; break;
                case CELL_SOFT: tile = TILE_SOFT; break;
                default: break;
            }

            /* Bomb overlay */
            for (uint8_t i = 0; i < 2u; ++i)
                if (g_players[i].bomb_active &&
                    g_players[i].bomb_x == x && g_players[i].bomb_y == y)
                    tile = TILE_BOMB;

            tile_set((uint8_t)(mx + x), (uint8_t)(my + y), tile);
        }
    }

    /* Text overlays using BG0 text layer */
    cl_gba_text_draw(16, 116, g_is_host ? "HOST" : "JOIN",
                     g_is_host ? EX_COLOR_OK : EX_COLOR_TEXT);
    cl_gba_text_draw(64, 116, g_players[g_my_id].alive ? "ALIVE" : "DEAD",
                     g_players[g_my_id].alive ? EX_COLOR_OK : EX_COLOR_WARN);
    cl_gba_text_draw(128, 116, transport_name(), EX_COLOR_DIM);
    ex_draw_error(176, 116, g_last_error);
    if (!g_players[0].alive || !g_players[1].alive)
        cl_gba_text_draw(80, 132,
                         g_players[g_my_id].alive ? "YOU WIN!" : "YOU LOSE",
                         g_players[g_my_id].alive ? EX_COLOR_OK : EX_COLOR_WARN);
    ex_draw_footer("DPAD MOVE  A BOMB  START EXIT");
    ex_present();
}

/* ============ Main ============ */

int main(void) {
    ex_video_init("BOMBERMAN");
    tiles_init();

    if (!ex_link_init(&g_link) || !ex_link_hello(&g_link)) {
        g_last_error = g_link.last_error; goto dead;
    }

    /* Select transport. */
    {
        ex_clear_body();
        cl_gba_text_draw(16, 50, "SELECT LINK", EX_COLOR_TEXT);
        cl_gba_text_draw(16, 66, "A = BLE", EX_COLOR_DIM);
        cl_gba_text_draw(16, 78, "B = WiFi", EX_COLOR_DIM);
        ex_draw_footer("A BLE  B WiFi");
        ex_present();
        uint8_t chosen = 0;
        while (!chosen) {
            uint16_t k = ex_keys_pressed();
            if (k & CL_GBA_KEY_A) {
                chosen = 1;
                g_transport = TRANSPORT_BLE;
            }
            if (k & CL_GBA_KEY_B) {
                chosen = 1;
                g_transport = TRANSPORT_WIFI;
            }
            cl_gba_wait_vblank();
        }
        ex_wait_key_release();
    }

    /* Wait for host/join selection. */
    {
        ex_clear_body();
        cl_gba_text_draw(16, 50, transport_name(), EX_COLOR_TEXT);
        cl_gba_text_draw(16, 66, "SELECT ROLE", EX_COLOR_TEXT);
        cl_gba_text_draw(16, 82, "A = HOST", EX_COLOR_DIM);
        cl_gba_text_draw(16, 94, "B = JOIN", EX_COLOR_DIM);
        ex_draw_footer("A HOST  B JOIN");
        ex_present();
        uint8_t chosen = 0;
        while (!chosen) {
            uint16_t k = ex_keys_pressed();
            if (k & CL_GBA_KEY_A) { chosen = 1; g_my_id = 0; }
            if (k & CL_GBA_KEY_B) { chosen = 1; g_my_id = 1; }
            cl_gba_wait_vblank();
        }
        ex_wait_key_release();
    }

    /* Init network */
    {
        int ret = net_init(g_my_id == 0);
        if (ret < 0) { g_last_error = ret; goto dead; }
    }

    /* Wait for peer */
    {
        ex_clear_body();
        cl_gba_text_draw(16, 50, transport_name(), EX_COLOR_DIM);
        cl_gba_text_draw(16, 66, "WAITING FOR PEER...", EX_COLOR_TEXT);
        ex_present();
        int retries = 0;
        while (!net_wait_ready() && retries < 1800) {
            cl_gba_wait_vblank(); retries++;
        }
        if (retries >= 1800) { g_last_error = -3; goto dead; }
    }

    arena_init();
    g_frame = 0;

    /* Game loop */
    while (1) {
        uint16_t keys = ex_keys_pressed();
        if (keys & CL_GBA_KEY_START) break;
        int moved = 0;
        if (keys & CL_GBA_KEY_UP)    moved = try_move(g_my_id, 0, -1);
        if (keys & CL_GBA_KEY_DOWN)  moved = try_move(g_my_id, 0, 1);
        if (keys & CL_GBA_KEY_LEFT)  moved = try_move(g_my_id, -1, 0);
        if (keys & CL_GBA_KEY_RIGHT) moved = try_move(g_my_id, 1, 0);
        if (keys & CL_GBA_KEY_A)     place_bomb(g_my_id);
        if (moved) send_move();

        /* Network recv */
        {   uint8_t rbuf[128];
            int nr = recv_msg(rbuf, sizeof(rbuf));
            while (nr > 0) {
                process_msg(rbuf, (uint32_t)nr);
                nr = recv_msg(rbuf, sizeof(rbuf));
            }
        }

        if ((g_frame & 1u) == 0) { tick_bombs(); tick_explosions(); }
        draw_arena();
        cl_gba_wait_vblank();
        g_frame++;
    }

    /* Cleanup */
    if (g_fd > 0) {
        if (g_transport == TRANSPORT_WIFI)
            cl_socket_close(&g_sockets, g_fd);
        else
            cl_ble_link_io_close(&g_ble, g_fd);
    }

dead:
    { ex_clear_body(); cl_gba_text_draw(16, 50, "GAME OVER", EX_COLOR_TEXT);
      ex_draw_error(168, 34, g_last_error); ex_present(); }
    while (1) cl_gba_wait_vblank();
}
