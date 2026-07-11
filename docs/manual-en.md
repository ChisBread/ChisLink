# ChisLink User Manual

This manual describes the current ChisLink manager and public SDK release.

## 1. Safety

- Back up `/sd/.chislink/signature.bin`. It identifies your device.
- Legacy `/sd/config/signature.bin` is accepted, but new installations should
  use `/sd/.chislink/signature.bin`.
- Keep the GBA and ChisLink powered while flashing a cartridge or writing a
  save.
- Check the selected file and target before using `Flash`, `Restore`, or
  `Delete`.

## 2. SD Card Layout

Use an MBR partition table with one FAT32 partition. ChisLink automatically
creates:

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

`chislink.conf` stores MCU settings:

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

`manager.conf` stores manager settings:

```ini
lang=en
boot_mb_enabled=0
boot_mb_path=
```

If `manager.conf` does not exist, the default language is English.

## 3. Boot Modes

- Default boot starts the built-in ChisLink Manager through GBA multiboot.
- `.mb.gba` files can be run from the file browser.
- A custom `.mb.gba` file can be selected as the default boot firmware.
- Hold `L+R` during startup to force the built-in manager when custom boot is
  enabled.
- Games and homebrew can use the SDK directly after booting normally.

## 4. Controls

| Button | Action |
| --- | --- |
| D-pad | Move cursor or edit values |
| A | Open, confirm, change, or run selected action |
| B | Back or cancel |
| L/R | Page left/right where available |
| START | Save settings or run the current page action |
| SELECT | Open file operations where available |

Exact hints are shown at the bottom of each page.

## 5. Cartridge Page

The cartridge page probes the cartridge on the GBA side. It shows ROM, save,
NOR flash, and hardware save information when available.

Actions:

| Action | Use |
| --- | --- |
| Backup DB | Back up save using the game database result |
| Backup HW | Back up save using hardware probing result |
| Restore DB | Restore a `.sav` using the game database save size/type |
| Restore HW | Restore a `.sav` using hardware probing save size/type |
| Dump ROM | Dump ROM to `/sd/.chislink/dumps` |
| Flash ROM | Choose a `.gba` file and program a supported NOR flashcart |
| Browse SD | Open the SD file browser |

Use DB mode for known retail games. Use HW mode for flashcarts or unknown
cartridges. Hardware probing is designed not to destroy existing SRAM/Flash save
contents, but restore and flash operations overwrite data by definition.

## 6. File Page

The file browser can browse SD folders and UTF-8 filenames. Long names are
truncated on screen; the selected title can scroll where the UI supports it.

Operations for regular files:

| Operation | Notes |
| --- | --- |
| RUN | Run `.mb.gba`, `_mb.gba`, or `mb.gba` multiboot files |
| SET BOOT | Make a multiboot file the custom boot firmware |
| FLASH | Flash a `.gba` ROM to the inserted flashcart |
| RESTORE | Restore a `.sav` file to the inserted cartridge |
| FAVORITE | Move the file to `/sd/.chislink/favorites` |
| RENAME | Rename with the built-in input page |
| MOVE | Select a destination folder through the browser |
| DELETE | Delete the file |

Backup saves are written to `/sd/.chislink/saves`. ROM dumps are written to
`/sd/.chislink/dumps`.

## 7. Wireless Page

The wireless page has three pages.

WiFi:

- `WiFi Radio`: enable or disable WiFi.
- `Lazy Connect`: when enabled, WiFi connects only after the GBA requests it.
- `Power Save`: modem power save.
- `TX Power Trim`: small transmit power adjustment for power-sensitive setups.

Access:

- `Scan AP`: scan nearby access points.
- `SSID Source`: choose or edit the SSID.
- `Password`: edit the password.
- `Join WiFi`: save settings and connect.

Tools:

- `Web Access`: allow or block the web server feature.
- `Web Server`: start the HTTP file manager explicitly. The server listens on
  port 80 after WiFi is connected.
- `Bluetooth LE`: BLE scan/test entry.

The web server is not started just because WiFi connects. This avoids wasting
power for games and SDK programs that only need networking.

## 8. About Page

The About page contains:

- Language.
- Boot Mode: Default or Custom.
- Boot Firmware: selected custom `.mb.gba` path.
- Version and commit label.

Use `A` to change a field, `B` to go back, and `START` to save.

Supported UI languages include English, Simplified Chinese, Spanish, French,
Italian, German, Catalan, Portuguese, Czech, Japanese, Korean, Ukrainian,
Russian, Indonesian, and Malay.

## 9. SDK and Examples

The public SDK exposes:

- ChisLink protocol client.
- POSIX-style SD/file API.
- Stream file API.
- Socket-style TCP/UDP API, including DNS helpers.
- BLE central, BLE GATT server, pairing, passkey, advertising, notifications,
  and a ChisLink BLE link helper.
- Cartridge file backend and save backup helpers.

Examples are in `examples/`. Each example builds to a `.mb.gba` file.

```sh
make -C examples
```

The SDK requires applications to provide buffers and fd tables explicitly.
Including the SDK should not silently reserve large global memory blocks.

## 10. Firmware Update

Download `chislink-fw.zip` from a GitHub release. The package contains:

```text
flasher_args.json
bootloader/bootloader.bin
chislink-mcu.bin
partition_table/partition-table.bin
storage.bin
identify.bin
```

Flash with `flasher_args.json` using an ESP flashing tool. Keep your
`signature.bin` backed up before changing firmware.
