# Emulator dependencies

This directory vendors and adapts two upstream GBA emulators for ChisLink's
SD-backed multiboot launcher:

- `goombacolor/`: <https://github.com/Dwedit/goombacolor>, imported from
  commit `35c3ffd074062c2c82190af1b883a6768d777ae1`.
- `pocketnes/`: <https://github.com/Dwedit/PocketNES>, imported from commit
  `9c07f3d1d2a26b957769c5cde942060cc9ea6e64`.

The upstream repositories did not contain a license file at those revisions.
Their source and the local adaptations are retained here with provenance so a
release contains the exact source used to build the bundled binaries.

Run `make -C emulators sync-flash-storage` to build both multiboot images and
copy them into `chislink-mcu/flash_storage/`.

Both ports obtain the selected SD path from the MCU after multiboot and keep
ROM reads out of their instruction hot paths. Goomba Color uses its existing
16 KiB bank cache with a 512-entry table for ROMs up to 8 MiB. PocketNES uses
four 32 KiB PRG groups, a dedicated 8 KiB
PRG window for `$6000-$7FFF`, and eight independent 1 KiB CHR pages so mapper
bank combinations do not depend on adjacent cache slots. Startup fills every
available cache slot sequentially; later misses use LRU replacement while
protecting the banks currently mapped by the emulator.

PocketNES reserves these cache buffers explicitly after `__rom_end__`. They
must not be linked into its `.sbss`, because the upstream multiboot linker uses
the same EWRAM addresses as load images for IWRAM overlays and `.append` code.

PocketNES accepts iNES files whose mapper exists in its mapper table. Basic
NES 2.0 headers are accepted when the mapper is 8-bit, the submapper is zero,
and the legacy PRG/CHR size fields are sufficient. Unsupported NES 2.0 mapper
extensions are rejected by the emulator before ROM paging starts.
CHR-ROM images above 1 MiB are also rejected because PocketNES stores its
rounded CHR page count in an 8-bit field.
