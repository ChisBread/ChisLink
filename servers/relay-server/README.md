# ChisLink Relay Server

ChisLink Relay Server 用于让两台 **ChisLink Pro（ESP32-S3 版本）** 通过互联网进行
GBA 无线适配器（AGB-015/AgbRFU）联机。它只负责转发 112 字节的 `WaNetPacket`，
不解析 GBA 游戏协议。

> 适用范围：**ChisLink Pro（ESP32-S3 版本）**。
> ESP32-C3 版本不具备无线适配器模式，无法使用本服务。

## 工作原理

```text
GBA + ChisLink Pro (S3) ── TCP :57318 ──> Relay Server <── TCP :57318 ── GBA + ChisLink Pro (S3)
```

- MCU 与服务器先完成 `RLY1` 握手：
  - `auth = HMAC-SHA256(server_key, nonce || device_id || room)`
  - `room_auth = HMAC-SHA256(room_password, room)`
- 房间名就是 6 位房间密码本身（大写字母 + 数字）。
- 首个客户端创建房间并定义 `room_auth`，后续客户端必须使用相同密码。
- 服务器按 `target_id` 在房间内转发 `WaNetPacket`。
- 背压策略：`DATA` 可丢弃；`CHILD_DATA` 和控制类包不丢。

## 构建

```bash
go build -o relay-server .
```

或从仓库根目录：

```bash
go build -o relay-server ./servers/relay-server
```

## 启动

明文模式（测试/内网）：

```bash
./relay-server \
  -server-key "你的服务器密钥" \
  -listen ":57318" \
  -admin-listen "0.0.0.0:57321" \
  -admin-token "你的管理token"
```

启用 TLS（服务器端，MCU 暂为明文 TCP）：

```bash
./relay-server \
  -server-key "你的服务器密钥" \
  -listen ":57318" \
  -admin-listen "127.0.0.1:57321" \
  -admin-token "你的管理token" \
  -tls-cert "/path/server.crt" \
  -tls-key "/path/server.key"
```

调试日志（排查联机问题时开启）：

```bash
./relay-server -server-key "你的服务器密钥" -debug
```

### 启动参数

| 参数 | 默认值 | 说明 |
|---|---|---|
| `-server-key` | 无（必填） | 服务器密钥。MCU 的 `Server Key` 必须与此一致 |
| `-listen` | `:57318` | MCU 连接的 TCP 地址 |
| `-admin-listen` | `127.0.0.1:57321` | 管理 HTTP 地址；置空禁用 |
| `-admin-token` | 空 | 管理接口 Bearer token；为空则不鉴权 |
| `-tls-cert` / `-tls-key` | 空 | 同时提供则启用 TLS |
| `-max-rooms` | `256` | 最大活跃房间数 |
| `-max-clients` | `64` | 最大认证客户端数 |
| `-debug` | `false` | 打印每个被路由的包 |

## 管理 API

```text
GET    /healthz
GET    /metrics                      # Prometheus 文本格式
GET    /admin/overview
GET    /admin/rooms
GET    /admin/rooms/{room}
GET    /admin/rooms/{room}/clients
DELETE /admin/rooms/{room}
DELETE /admin/rooms/{room}/clients/{device_id}
POST   /admin/server-key/rotate      # JSON: {"server_key":"..."}
```

带 `-admin-token` 时需附加请求头：

```text
Authorization: Bearer 你的管理token
```

示例：

```bash
curl -H 'Authorization: Bearer 你的管理token' http://127.0.0.1:57321/admin/overview
```

## MCU / Manager 配置

在 GBA Manager 的 `Link → Relay` 页配置：

| 字段 | 主机 | 从机 |
|---|---|---|
| `Enabled` | On | On |
| `Host` | 服务器 IP 或域名 | 服务器 IP 或域名 |
| `Port` | 57318 | 57318 |
| `Server Key` | 与 `-server-key` 一致 | 与 `-server-key` 一致 |
| `Host Mode` | On | Off |
| `Room Password` | 主机模式下由 MCU 根据 MAC 生成 6 位密码 | 手动输入主机生成的 6 位密码 |

## 文件

```text
relay-server/
├── main.go
└── README.md
```

## License

MIT（与 ChisLink 主项目一致）。
