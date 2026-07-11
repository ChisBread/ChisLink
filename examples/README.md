# ChisLink SDK Examples

These examples are small GBA multiboot programs that exercise the public
ChisLink SDK APIs. They are intentionally plain C and keep all SDK working
memory explicit in the application.

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
by git.

## Examples

`00_basic_link`

Minimal SIO transport and client setup. It sends `HELLO` and `CAPS`, then
shows the client state, capability bits, status, and last error.

Controls: `A` sends `HELLO/CAPS` again.

`01_storage_files`

Registers the remote storage file backends and lists `/sd` through the unified
`cl_file` API. It also tries to read `/sd/.chislink/examples/example.txt` into
an application-owned 512-byte buffer.

Controls: `A` refreshes, `R` lists the next page, `L` returns to the first
page, `B` reads the sample file.

`02_stream_file`

Subscribes to `/sd/.chislink/examples/example.txt` as a stream. The app provides
two 4 KiB stream slots and consumes data with `cl_stream_recv_slot()`,
`cl_stream_consumer_peek()`, and `cl_stream_consumer_release()`.

Requires `/sd/.chislink/examples/example.txt` on the MCU SD card.

Controls: `A` opens the stream, `B` receives one slot, `START` closes it.

`03_socket_airlink`

Uses the socket compatibility layer with an application-owned fd table. It
performs the same short Air Link style test as the manager: UDP discovery,
TCP connect or accept, then a small hello/ack exchange.

Controls: `A` runs one Air Link attempt.

For PC-side testing, run:

```sh
python3 tools/airlink_peer.py
```

`04_ble_scan`

Uses the BLE API with an application-owned scratch buffer. It scans briefly
and displays up to four advertisements, including RSSI, connectable state,
partial address, and advertisement name.

Controls: `A` scans again.

`05_cart_backup`

Registers both remote storage and the local cart backend, configures save
layout from the MCU game database, and copies `/dev/cart/save` to
`/sd/.chislink/examples/cart-save.sav` through the unified `cl_file` API.

Controls: `A` refreshes cart info, `B` backs up save, `L` configures from DB,
`R` probes save hardware.

The example only backs up saves. It does not restore data to the cart.

`06_file_test`

Automated regression test for the unified file API. It creates
`/sd/.chislink/examples/file_test`, then checks mkdir, create/write, stat/fstat, read,
seek/tell, access, pread, buffered copy, rename, directory listing,
truncate, stream read, and cleanup.

Controls: none. The test runs once at boot and shows the first failing case.

`07_bomberman`

Small two-player game that demonstrates the SDK as an application dependency
rather than a fixed manager workflow. It includes both BLE and WiFi transports
in one ROM. At startup, choose ChisLink BLE Link I/O or WiFi UDP discovery
plus a TCP session, then choose host or join.

Build it:

```sh
make -C examples/07_bomberman
```

Controls: `A/B` choose link mode and role during setup. In game, D-pad moves,
`A` drops a bomb, and `START` exits.

`08_ble_hid_gamepad`

Uses the generic BLE GATT server API to expose a small HID-shaped gamepad
service. The SDK does not hard-code HID; this example shows how an application
can define services, characteristics, descriptors, advertise, receive subscribe
events, handle numeric-comparison pairing, and send notifications.

Controls: during pairing, compare the host code with the GBA code and press
`A` to confirm or `B` to reject. After pairing, D-pad maps to X/Y axes and
`A/B/L/R/START/SELECT` map to buttons.

## Coverage Notes

Self-contained smoke/regression examples:

- Basic SIO/client handshake: `00_basic_link`.
- POSIX-style file operations and stream regression: `06_file_test`.

Environment-dependent examples:

- SD browsing and sample file read: `01_storage_files` needs SD content.
- Remote file stream receive path: `02_stream_file` needs
  `/sd/.chislink/examples/example.txt`.
- UDP discovery, TCP connect, listen, accept, send, and receive:
  `03_socket_airlink` needs `tools/airlink_peer.py` or another ChisLink peer.
- Save backup: `05_cart_backup` needs a readable cart/save device.
- BLE Link I/O / WiFi game-link send and receive: `07_bomberman` needs another
  peer.

Partial examples:

- BLE GATT central scan is covered by `04_ble_scan`; the generic GATT server
  path and HID-shaped services are covered by `08_ble_hid_gamepad`. Pairing
  and long GATT operations still need focused examples.
- Cart support is only partially covered by `05_cart_backup`; cart info,
  database save configuration, hardware save probing, and save backup are
  demonstrated, but restore, ROM dump, NOR probe, and NOR programming are not.
- Stream send/pump and seek are not shown in a standalone example; current
  stream examples focus on subscribing to a file and receiving slots.
- Direct low-level `cl_storage_*` calls are not shown directly because the
  intended public path is `cl_file_*`, but a storage-only diagnostic example
  would be useful when debugging MCU file protocol issues.
- WiFi configuration and AP scan APIs are exercised by the manager, not by a
  small standalone SDK example.

## Memory Ownership Pattern

The SDK does not allocate hidden global workspaces for these examples:

- `cl_net_init()` receives an application scratch buffer.
- `cl_ble_init()` receives an application scratch buffer.
- `cl_socket_init()` receives an application fd table.
- `cl_stream_init()` receives application stream slots and payload buffer.
- File copying receives an application workspace buffer.

This is the intended pattern for third-party GBA programs that need tight
control over EWRAM/IWRAM layout.
