# ChisLink 使用手册

本文档对应当前 ChisLink Manager 和公开 SDK 版本。

## 1. 安全须知

- 请备份 `/sd/.chislink/signature.bin`，这是设备身份文件。
- 旧路径 `/sd/config/signature.bin` 仍会兼容读取并迁移，但新版本推荐使用
  `/sd/.chislink/signature.bin`。
- 烧录卡带或写入存档时，请保证 GBA 和 ChisLink 供电稳定。
- 使用 `Flash`、`Restore`、`Delete` 前确认文件和目标，避免误操作。

## 2. SD 卡目录

建议使用 MBR 分区表、单 FAT32 分区。ChisLink 会自动创建：

```text
/sd/.chislink/
├── chislink.conf
├── manager.conf
├── signature.bin
├── saves/
├── dumps/
├── favorites/
└── examples/
```

`chislink.conf` 保存 MCU 设置：

```ini
wifi_disable=false
wifi_lazy_connect=true
wifi_power_save=true
wifi_web_server=true
wifi_max_power_adjust=0
wifi_ssid=
wifi_pass=
chislink_sign_server=
chislink_sign_port=13692
```

`manager.conf` 保存 GBA Manager 设置：

```ini
lang=en
boot_mb_enabled=0
boot_mb_path=
```

如果没有 `manager.conf`，默认语言为英文。

## 3. 启动方式

- 默认启动内置 ChisLink Manager。
- 文件浏览器可以直接运行 `.mb.gba` 文件。
- 可以把某个 `.mb.gba` 设置为默认自定义启动固件。
- 启用了自定义启动后，开机按住 `L+R` 可以强制进入内置 Manager。
- 普通游戏和自制程序也可以直接使用 ChisLink SDK 访问 MCU 服务。

## 4. 基本按键

| 按键 | 作用 |
| --- | --- |
| 方向键 | 移动光标或调整数值 |
| A | 打开、确认、切换、执行当前操作 |
| B | 返回或取消 |
| L/R | 可用页面中左右翻页 |
| START | 保存设置或执行当前页面动作 |
| SELECT | 在支持的页面打开文件操作 |

每个页面底部会显示当前可用的快捷键提示。

## 5. 卡带管理

卡带页面由 GBA 侧执行硬件探测，显示 ROM、存档、NOR Flash、硬件存档等信息。

操作：

| 操作 | 用途 |
| --- | --- |
| Backup DB | 按游戏数据库结果备份存档 |
| Backup HW | 按硬件探测结果备份存档 |
| Restore DB | 按数据库存档类型和大小恢复 `.sav` |
| Restore HW | 按硬件探测存档类型和大小恢复 `.sav` |
| Dump ROM | 导出 ROM 到 `/sd/.chislink/dumps` |
| Flash ROM | 选择 `.gba` 文件并烧录到支持的 NOR 烧录卡 |
| Browse SD | 打开 SD 文件浏览器 |

已知正版游戏建议使用 DB 模式。烧录卡、未知卡、非官方卡可以使用 HW 模式。
硬件探测会尽量避免破坏已有 SRAM/Flash 存档，但 Restore 和 Flash 本身会写入目标。

## 6. 文件浏览器

文件浏览器支持 SD 目录遍历和 UTF-8 文件名显示。过长文件名会按屏幕空间截断，
部分标题会在选中时滚动显示。

普通文件支持的操作：

| 操作 | 说明 |
| --- | --- |
| RUN | 运行 `.mb.gba`、`_mb.gba`、`mb.gba` 多重启动文件 |
| SET BOOT | 设置为自定义启动固件 |
| FLASH | 将 `.gba` 烧录到插入的烧录卡 |
| RESTORE | 将 `.sav` 恢复到插入的卡带 |
| FAVORITE | 移动到 `/sd/.chislink/favorites` |
| RENAME | 使用内置输入页重命名 |
| MOVE | 通过浏览器选择目标目录移动 |
| DELETE | 删除文件 |

存档备份默认写入 `/sd/.chislink/saves`。ROM 导出默认写入
`/sd/.chislink/dumps`。

## 7. 无线页面

无线页面分为三页。

WiFi：

- `WiFi Radio`：启用或禁用 WiFi。
- `Lazy Connect`：启用后，只有 GBA 主动请求时才连接 WiFi。
- `Power Save`：WiFi 省电模式。
- `TX Power Trim`：针对供电敏感设备的小幅发射功率调整。

Access：

- `Scan AP`：扫描附近热点。
- `SSID Source`：选择或编辑 SSID。
- `Password`：编辑密码。
- `Join WiFi`：保存设置并连接 WiFi。

Tools：

- `Web Access`：允许或关闭 Web 功能。
- `Web Server`：显式启动 HTTP 文件管理器。WiFi 连接后监听 80 端口。
- `Bluetooth LE`：BLE 扫描/测试入口。

Web Server 不会因为 WiFi 连接就自动启动，避免第三方程序默认耗电。

## 8. 关于页面

关于页面包含：

- 语言。
- Boot Mode：默认启动或自定义启动。
- Boot Firmware：当前自定义 `.mb.gba` 路径。
- 版本号和 commit 标识。

使用 `A` 修改字段，`B` 返回，`START` 保存。

当前 UI 语言包括英文、简体中文、西班牙语、法语、意大利语、德语、
加泰罗尼亚语、葡萄牙语、捷克语、日语、韩语、乌克兰语、俄语、
印尼语、马来语。

## 9. SDK 和示例

公开 SDK 提供：

- ChisLink 协议客户端。
- 类 POSIX 的 SD/文件接口。
- 文件流接口。
- Socket 风格 TCP/UDP 接口，包括 DNS 辅助接口。
- BLE Central、BLE GATT Server、配对、Passkey、广播、通知，以及
  ChisLink BLE Link 辅助层。
- 卡带文件后端和存档备份辅助能力。

示例位于 `examples/`。每个示例会构建为 `.mb.gba` 文件。

```sh
make -C examples
```

SDK 要求应用显式提供 buffer 和 fd table。引入 SDK 不应隐式占用大块全局内存。

## 10. 固件更新

从 GitHub Release 下载 `chislink-fw.zip`，其中包含：

```text
flasher_args.json
bootloader/bootloader.bin
chislink-mcu.bin
partition_table/partition-table.bin
storage.bin
identify.bin
```

使用支持 `flasher_args.json` 的 ESP 烧录工具写入。更新固件前请确认
`signature.bin` 已备份。
