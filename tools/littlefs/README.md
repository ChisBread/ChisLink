# LittleFS Image Tool

This tool packs, extracts, and lists ChisLink `storage.bin` LittleFS images
without ESP-IDF or CMake. It only manipulates files that are already present; it
does not rebuild the manager, fonts, game database, skins, or emulator
multiboot images.

## Pack

```sh
tools/littlefs/build_storage_image.py pack
```

Defaults:

- Source: `chislink-mcu/flash_storage`
- Output: `chislink-mcu/build/storage.bin`
- Partition: `storage` from `chislink-mcu/partitions_littlefs.csv`
- Block size: `4096`
- Name max: `CONFIG_LITTLEFS_OBJ_NAME_LEN` from `chislink-mcu/sdkconfig`, or `64`

The command without a subcommand is kept as a shortcut for `pack`:

```sh
tools/littlefs/build_storage_image.py --output /tmp/storage.bin
```

For standalone use outside this repository, pass the image size explicitly:

```sh
tools/littlefs/build_storage_image.py pack \
  --source ./storage_files \
  --output ./storage.bin \
  --fs-size 1856K
```

## Extract

```sh
tools/littlefs/build_storage_image.py extract \
  --image ./storage.bin \
  --output ./storage_files
```

## List

```sh
tools/littlefs/build_storage_image.py list --image ./storage.bin
```

## Files

Common files in ChisLink `storage.bin`:

| Path | Purpose |
| --- | --- |
| `/chislink-boot_mb.gba` | GBA manager multiboot program. |
| `/compact_font.bin` | Compact UI glyph bitmap store. |
| `/compact_font.idx` | Compact font lookup index. |
| `/db_AGB.gamedb` | GBA game database and save metadata. |
| `/pocketnes_mb.gba` | PocketNES multiboot launcher used for `.nes`/`.fc`. |
| `/goombacolor_mb.gba` | Goomba Color multiboot launcher used for `.gb`/`.gbc`. |
| `/chislink_key_pub.pem` | Public key used by MCU-side key verification. |
| `/wifi.conf` | Optional LittleFS Wi-Fi credential fallback. |
| `/static/index.html` | Web file browser and skin manager page. |
| `/static/skin.js` | Web skin builder/manager logic. |
| `/static/default_skin.bin` | Default skin package for web-side skin reset/build flows. |

Custom files can be added, but keep firmware-owned paths above intact unless you
know the matching MCU/manager code has been updated.

## Dependency

If `littlefs-python` is not available, the tool creates a private venv under
`~/.cache/chislink/` and installs the pinned version from `requirements.txt`.
Any existing `littlefs-python` executable is version-checked by default.
To use an existing executable instead:

```sh
tools/littlefs/build_storage_image.py pack \
  --littlefs-python /path/to/littlefs-python \
  --output /tmp/storage.bin
```
