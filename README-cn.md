# ChisLink

ChisLink 是面向 GBA 的便携式卡带管理器和 MCU 服务平台。它可以启动
GBA 多重启动管理器，管理 GBA 卡带，浏览 SD 文件，并为 GBA 程序提供
WiFi、BLE、文件、流式传输和卡带访问能力。

- README: [English](./README.md) | [中文](./README-cn.md)
- 使用手册：[中文](./docs/manual-zh.md) | [English](./docs/manual-en.md)
- ChisFlash 卡带：[ChisFlash](https://github.com/ChisBread/ChisFlash)
- 商店：[ChisFamily Official Store](https://www.aliexpress.com/item/1005011958199331.html)

## 项目内容

- 用于卡带管理的 GBA 多重启动 Manager。
- ESP32-C3 MCU 固件，提供 SD、WiFi、BLE、Web 文件管理和签名支持。
- 面向第三方 GBA 程序的公开 C SDK。
- SDK 示例，覆盖文件、流、socket、BLE、卡带备份、Bomberman 联机和
  BLE HID 手柄。

## Manager 功能

- 卡带页面：
  - GBA 侧卡带探测。
  - 游戏数据库存档检测和硬件存档检测。
  - 基于 DB 或 HW 结果的存档备份、恢复。
  - ROM dump 和 GBA NOR 烧录，显示进度、速度和剩余时间。
- 文件页面：
  - 支持 UTF-8 文件名和多语言字库的 SD 浏览器。
  - 通过 chainloader 运行 `.mb.gba` 多重启动程序。
  - 设置自定义默认启动固件。
  - 支持烧录 ROM、恢复存档、收藏、重命名、移动、删除文件。
- 无线页面：
  - WiFi 开关、懒连接、省电模式、TX 功率微调。
  - AP 扫描、SSID/密码输入、连接 WiFi。
  - 显式启动 80 端口 Web Server。
  - BLE 扫描/测试入口。
- 关于页面：
  - 语言设置。
  - 默认/自定义启动固件设置。
  - 版本号和 commit 标识。

## 快速开始

1. 准备一张 MBR 分区表、FAT32 分区的 microSD 卡。
2. 将设备签名放到 `/sd/.chislink/signature.bin`。
   旧路径 `/sd/config/signature.bin` 仍会兼容读取并迁移。
3. 将 ROM、存档、`.mb.gba` 示例复制到 SD 卡任意目录。
4. 从 GitHub Release 下载并刷入固件包。
5. 让 GBA 进入 multiboot 模式，即可启动 Manager。

固件会自动创建应用目录：

```text
/sd/.chislink/
├── chislink.conf      # MCU 配置：WiFi、Web、签名服务器
├── manager.conf       # GBA Manager 配置：语言、自定义启动
├── signature.bin      # 设备签名备份
├── saves/             # 存档备份
├── dumps/             # ROM dump
├── favorites/         # 收藏文件
└── examples/          # SDK 示例使用的测试文件
```

## 固件包

Release 中的 `chislink-fw.zip` 包含：

```text
flasher_args.json
bootloader/bootloader.bin
chislink-mcu.bin
partition_table/partition-table.bin
storage.bin
identify.bin
```

可以使用 [esptool-js](https://chisbread.github.io/esptool-js/) 或兼容
`flasher_args.json` 的 ESP 烧录工具刷入。

## SDK

SDK 使用纯 C 编写，不隐式分配全局大 buffer。socket、BLE、stream、
协议层所需内存由应用显式提供，便于第三方 GBA 程序管理 EWRAM/IWRAM。

构建所有示例：

```sh
make -C examples
```

每个示例都会生成一个 `.mb.gba` 多重启动镜像。复制到 SD 卡后，可以在
Manager 文件浏览器中运行。

## BLE 示例注意事项

PC、手机等 BLE 主机可能会自动重连之前配对过的 HID 设备。如果主机仍然
连接着 `08_ble_hid_gamepad` 示例（`ChisLink Pad`），它可能会干扰
`07_bomberman` 这类点对点 BLE Link 示例，导致发现或 GATT 连接错误。

测试 BLE Link 示例前，请先在主机侧断开或忽略 `ChisLink Pad`，或者临时
关闭主机蓝牙。切换示例时，建议重启参与测试的 ChisLink 设备，或重新加载
目标 `.mb.gba`，让 BLE profile 从干净状态启动。

## 许可证

MIT。详见 [LICENSE](./LICENSE)。
