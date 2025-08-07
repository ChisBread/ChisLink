# ChisLink

A full-function portable cartridge programmer designed for GBA

- [Discord(Preparing)](https://discord.com/channels/1400338637580795904/1400338638025527339)

- Open Source Flashcarts
    - [ChisFlash 0.1(SRAM1M), 1.0(FRAM1M), 1.1(Flash1M)](https://github.com/ChisBread/ChisFlash)
    - [ChisFlash MBC5 Max (16in1)](https://oshwhub.com/morinaka/chisflash-mbc5-max-32m-gbc-shao-lu-ka)
    - [ChisFlash MBC5 Plus (4in1)](https://oshwhub.com/morinaka/chisflash-mbc5-gbc-shao-lu-ka)

- [ChisLink中文说明](./docs/manual-zh.md) | [ChisLink English Manual](./docs/manual-en.md)

## Overview

ChisLink is a powerful GBA (Game Boy Advance) portable cartridge programmer that supports reading/writing various cartridge types, save management, ROM backup, and more. The device is compact and portable, featuring WiFi connectivity.

## Key Features

- 🎮 **Multi-cartridge Support** - Compatible with ChisFlash, ChisMBC5(adapter required), genuine cartridges, and some bootleg cartridges
- 💾 **Smart Save Management** - Auto-detect save types with backup and restore functionality
- ⚡ **Flashcart Programming** - Change the game on the flashcart anytime, anywhere!
- 🗂️ **File System** - File management capabilities.
- 📡 **WiFi Connectivity** - Wireless file transfer and remote management.

## Supported Cartridge Types

| Type | Description | Save Support | Programming |
|------|-------------|--------------|-------------|
| ChisFlash | Compatible cartridges | SRAM/Flash/EEPROM | ✅ |
| ChisMBC5 | MBC5 cartridges (adapter required) | SRAM/Flash/EEPROM | ✅ |
| Cartridge | Genuine cartridges | Database-based identification | ❌ |
| Bootleg | Bootleg cartridges | Bootleg's Batteryless-SRAM is not fully supported, and it requires constant power restart (insert ChisLink in the game) | ⚠️ |

## Quick Start

### Preparation

1. Prepare a TF card (MBR partition table, single partition format)
2. Ensure `signature.bin` file exists in `/config` directory
3. **Important**: Backup `signature.bin` file to a secure location

### Device Startup

- **No cartridge**: Auto-launch Link or custom App
- **With cartridge**: Hold `START+SELECT` combo to boot

### Basic Operations

- `SELECT` - Switch menu view (multi-cart mode)
- `A` - Select game for operations
- `START` - Toggle file extension display / Enable overclock
- `L+R` - View hardware details
- D-pad - Navigate pages

## File System Structure

```
Root/
├── saves/        # Save files directory
├── favorites/    # Favorites folder
├── config/       # System configuration
│   ├── signature.bin  # License file (required)
│   └── web.conf      # WiFi configuration
└── dumps/        # ROM dump directory
```

## WiFi Configuration

### Default Settings
- SSID: `Bach`
- Password: `chisbread`

### Advanced Configuration (`/config/web.conf`)
```ini
wifi_disable=0          # Disable WiFi
wifi_lazy_connect=1     # Recommended (reduces power consumption)
wifi_max_power_adjust=0 # Power adjustment range (-16~16)
```

## Firmware Updates

1. Connect to computer via USB
2. Visit [esptool-js page](https://chisbread.github.io/esptool-js/)
3. Select `USB JTAG/serial` to complete flashing

## Safety Precautions

⚠️ **Important Reminders**:
- Verify TF card contains correct `signature.bin` before firmware flashing
- Missing this file will brick the device
- Regularly backup configuration files

## Compatibility

- ✅ Supports all GBA console models
- ✅ Compatible with ChisFlash MBC5 cartridges
- ❌ GB Micro does not support overclocking
- ⚠️ ChisMBC5 requires adapter

## Technical Support

For compatibility issues, please provide:
- Hardware details (press `L+R` to view)
- Cartridge model and PCB photo
- Specific error symptoms

## Credits

- [Butano](https://github.com/GValiente/butano) - GBA development framework
- [devkitPro](https://devkitpro.org/) - Development toolchain
- [gba-link-connection](https://github.com/afska/gba-link-connection) - Connection library

---

📖 **Detailed Manuals**: [中文版](./docs/manual-zh.md) | [English](./docs/manual-en.md)

### Agents
- [EPICJOY Official Store](https://www.aliexpress.us/item/3256809487843308.html)

