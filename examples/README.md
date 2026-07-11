# ChisLink SDK Examples

These examples are small GBA multiboot programs that demonstrate the public
ChisLink SDK. They are written in plain C and keep SDK working memory explicit
in the application, so each project can be used as a starting point for
third-party GBA programs.

Build all examples:

```sh
make -C examples
```

Build one example:

```sh
make -C examples/03_socket_airlink
```

Each example produces a `.mb.gba` multiboot image in its own directory.
Generated `.mb.gba`, legacy `.gba`, `.elf`, and `build/` outputs are ignored
by git. Copy the built `.mb.gba` file to the SD card and run it from the
ChisLink Manager file browser.

## Example Index

| Project | Purpose | Main APIs |
| --- | --- | --- |
| `00_basic_link` | Minimal client smoke test | client, capabilities |
| `01_storage_files` | Browse and read SD files | file API |
| `02_stream_file` | Stream a file from SD | stream API |
| `03_socket_airlink` | UDP discovery plus TCP exchange | socket API |
| `04_ble_scan` | BLE central scan | BLE scan/events |
| `05_cart_backup` | Back up cart saves | cart file backend |
| `06_file_test` | File API regression test | file, copy, stream |
| `07_bomberman` | Two-player wireless game | BLE link, socket |
| `08_ble_hid_gamepad` | BLE HID-style gamepad | BLE GATT server, pairing |

## 00_basic_link

Purpose: verify that a GBA program can initialize the ChisLink client and talk
to the MCU.

What it demonstrates:

- Client initialization.
- `HELLO` and `CAPS` requests.
- Capability display and simple error reporting.

Controls:

- `A`: send `HELLO/CAPS` again.

## 01_storage_files

Purpose: show how to access the MCU SD card through the unified file API.

What it demonstrates:

- Registering remote storage as a `cl_file` backend.
- Listing `/sd`.
- Reading `/sd/.chislink/examples/example.txt` into an application-owned
  512-byte buffer.

Requirements:

- Optional: create `/sd/.chislink/examples/example.txt` to test file reading.

Controls:

- `A`: refresh directory listing.
- `R`: next page.
- `L`: first page.
- `B`: read the sample file.

## 02_stream_file

Purpose: receive a file as a stream without hiding large SDK-owned buffers.

What it demonstrates:

- Opening a remote file stream.
- Providing two 4 KiB stream slots from the application.
- Consuming data with `cl_stream_recv_slot()`,
  `cl_stream_consumer_peek()`, and `cl_stream_consumer_release()`.

Requirements:

- Create `/sd/.chislink/examples/example.txt` on the SD card.

Controls:

- `A`: open stream.
- `B`: receive one slot.
- `START`: close stream.

## 03_socket_airlink

Purpose: demonstrate a small LAN peer test using socket-style APIs.

What it demonstrates:

- Application-owned socket fd table.
- UDP broadcast discovery.
- TCP connect or accept.
- A short hello/ack exchange.

Requirements:

- Another ChisLink peer, or a compatible program that answers the same UDP/TCP
  test protocol.

Controls:

- `A`: run one Air Link attempt.

## 04_ble_scan

Purpose: scan nearby BLE devices from a GBA program.

What it demonstrates:

- BLE initialization with an application-owned scratch buffer.
- Starting a scan and reading BLE events.
- Displaying RSSI, connectable state, partial address, and advertisement name.

Controls:

- `A`: scan again.

## 05_cart_backup

Purpose: back up the inserted cartridge save through the same file abstraction
used by SD storage.

What it demonstrates:

- Registering remote storage and local cartridge file backends.
- Reading cart metadata and save layout.
- Configuring save layout from the MCU game database.
- Probing save hardware.
- Copying `/dev/cart/save` to `/sd/.chislink/examples/cart-save.sav`.

Requirements:

- A cartridge with readable save memory.

Controls:

- `A`: refresh cart info.
- `B`: back up save.
- `L`: configure from database.
- `R`: probe save hardware.

Notes:

- This example only backs up saves. It does not restore data to the cartridge.

## 06_file_test

Purpose: provide a self-contained regression test for the unified file API.

What it demonstrates:

- `mkdir`, create/write, stat/fstat, read, seek/tell, access, pread.
- Buffered copy.
- Rename.
- Directory listing.
- Truncate.
- Stream read.
- Cleanup.

Runtime behavior:

- The test creates `/sd/.chislink/examples/file_test`.
- It runs once at boot and shows the first failing case.

Controls:

- None.

## 07_bomberman

Purpose: show ChisLink SDK use inside an actual interactive program rather than
a fixed manager workflow.

What it demonstrates:

- A small two-player game.
- BLE Link I/O and WiFi socket modes in one ROM.
- Host/join flow.
- Repeated send/receive during gameplay.

Requirements:

- Another ChisLink peer running the same example.

Controls:

- Setup: `A/B` choose link mode and role.
- Game: D-pad moves, `A` drops a bomb, `START` exits.

## 08_ble_hid_gamepad

Purpose: expose the GBA as a BLE HID-style gamepad and demonstrate generic
GATT server features.

What it demonstrates:

- BLE security configuration.
- GATT service, characteristic, and descriptor definition.
- Advertising as a gamepad-like BLE peripheral.
- Subscription events.
- Numeric-comparison pairing.
- Notifications for input reports.

Controls:

- Pairing: compare the host code with the GBA code, press `A` to confirm or
  `B` to reject.
- Connected: D-pad maps to X/Y axes, and `A/B/L/R/START/SELECT` map to
  buttons.

## Coverage Notes

Self-contained smoke/regression examples:

- Basic client handshake: `00_basic_link`.
- POSIX-style file operations and stream regression: `06_file_test`.

Environment-dependent examples:

- SD browsing and sample file read: `01_storage_files` needs SD content for
  the sample read.
- Remote file stream receive path: `02_stream_file` needs
  `/sd/.chislink/examples/example.txt`.
- UDP discovery, TCP connect, listen, accept, send, and receive:
  `03_socket_airlink` needs another compatible peer.
- Save backup: `05_cart_backup` needs a readable cartridge/save device.
- BLE Link I/O and WiFi game-link send/receive: `07_bomberman` needs another
  peer.
- BLE HID behavior: `08_ble_hid_gamepad` needs a BLE host such as a PC, phone,
  or tablet.

Partial areas:

- Cart restore, ROM dump, NOR probe, and NOR programming are covered by the
  manager, not by standalone examples yet.
- WiFi configuration and AP scan APIs are exercised by the manager, not by a
  small standalone SDK example yet.

## Memory Ownership Pattern

The SDK does not allocate hidden global workspaces for these examples:

- `cl_net_init()` receives an application scratch buffer.
- `cl_ble_init()` receives an application scratch buffer.
- `cl_socket_init()` receives an application fd table.
- `cl_stream_init()` receives application stream slots and payload buffer.
- File copying receives an application workspace buffer.

This is the intended pattern for third-party GBA programs that need tight
control over EWRAM/IWRAM layout.
