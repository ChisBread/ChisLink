# ChisLink

ChisLink is a portable GBA cartridge manager and MCU service platform. It can
start a GBA multiboot manager, manage GBA cartridges, browse SD files, provide
WiFi/BLE services to GBA software, and expose a C SDK for third-party programs.

- README: [English](./README.md) | [中文](./README-cn.md)
- User manuals: [English](./docs/manual-en.md) | [中文](./docs/manual-zh.md)
- ChisFlash cartridges: [ChisFlash](https://github.com/ChisBread/ChisFlash)
- Store: [ChisFamily Official Store](https://www.aliexpress.com/item/1005011958199331.html)

## What Is Included

- GBA multiboot manager for cartridge management.
- ESP32-C3 MCU firmware with SD, WiFi, BLE, web file manager, and signing support.
- Public GBA SDK for file, stream, socket, BLE, cart, and runtime services.
- SDK examples, including storage, streams, sockets, BLE scan, Bomberman link,
  and a BLE HID gamepad sample.

## Manager Features

- Cartridge page:
  - GBA-side cartridge probing.
  - Game database save detection and hardware save detection.
  - Backup/restore by DB result or by hardware result.
  - ROM dump and GBA NOR flash programming with progress, speed, and ETA.
- File page:
  - UTF-8 SD browser with multilingual font support.
  - Run `.mb.gba` multiboot apps through the chainloader.
  - Set a custom boot multiboot app.
  - Flash ROMs, restore saves, favorite, rename, move, and delete files.
- Wireless page:
  - WiFi radio, lazy connect, power save, and TX power trim.
  - AP scan, SSID/password input, and Join WiFi.
  - Explicit Web Server startup on port 80.
  - BLE scan/test entry.
- About page:
  - Language selection.
  - Default/custom boot firmware selection.
  - Build version and commit label.

## Quick Start

1. Prepare a microSD card with an MBR partition table and a FAT32 partition.
2. Put your device signature at `/sd/.chislink/signature.bin`.
   Legacy `/sd/config/signature.bin` is still accepted and migrated.
3. Copy ROMs, saves, and `.mb.gba` examples anywhere on the SD card.
4. Flash the firmware package from the latest GitHub release.
5. Start the GBA in multiboot mode to enter the manager.

The firmware creates this application directory automatically:

```text
/sd/.chislink/
├── chislink.conf      # MCU config: WiFi, web, signing server
├── manager.conf       # GBA manager config: language, custom boot
├── signature.bin      # Device signature backup
├── saves/             # Save backups
├── dumps/             # ROM dumps
├── favorites/         # Favorite files
└── examples/          # Sample files for SDK examples
```

## Firmware Package

Release assets contain `chislink-fw.zip` with:

```text
flasher_args.json
bootloader/bootloader.bin
chislink-mcu.bin
partition_table/partition-table.bin
storage.bin
identify.bin
```

You can flash with [esptool-js](https://chisbread.github.io/esptool-js/) or a
compatible ESP flashing tool using `flasher_args.json`.

## SDK

The SDK is plain C and does not allocate hidden global workspaces. Applications
provide the memory used by socket, BLE, stream, and protocol layers.

Build all examples:

```sh
make -C examples
```

Each example emits a `.mb.gba` multiboot image. Copy it to the SD card and run
it from the manager file browser.

## BLE Example Note

BLE hosts can automatically reconnect to a previously paired HID device. If a
PC or phone is still connected to the `08_ble_hid_gamepad` example
(`ChisLink Pad`), it can interfere with peer-to-peer BLE examples such as
`07_bomberman`.

Before testing BLE Link examples, disconnect or forget `ChisLink Pad` on the
host, or temporarily disable the host Bluetooth radio. Restart the involved
ChisLink devices or reload the target `.mb.gba` examples so the BLE profile
starts from a clean state.

## License

MIT. See [LICENSE](./LICENSE).
