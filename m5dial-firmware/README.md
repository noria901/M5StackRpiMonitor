# M5Dial Firmware

Based on [M5Dial-UserDemo](https://github.com/m5stack/M5Dial-UserDemo) with custom apps added.

## Custom Apps

- **BLE Scanner** (`app_ble_scanner/`) - Scans for RPi Monitor BLE servers, saves selection to NVS
- **RPi Monitor** (`app_rpi_monitor/`) - Connects to RPi via BLE GATT, displays system info

## Build

Requires [ESP-IDF v5.1.3](https://docs.espressif.com/projects/esp-idf/en/v5.1.3/esp32s3/index.html).

```bash
cd m5dial-firmware
idf.py set-target esp32s3
idf.py build
idf.py flash
```

## Launcher Integration

Custom apps are registered as launcher items 8 (BLE SCAN) and 9 (RPI MONITOR) in the dial menu.

- `launcher.h` - includes for custom app headers
- `launcher.cpp` - case 8/9 in `_app_open_callback`
- `launcher_render_callback.hpp` - ICON_NUM=10, colors/tags/icons for new apps
