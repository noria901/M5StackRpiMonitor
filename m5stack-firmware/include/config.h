#pragma once

// BLE Service and Characteristic UUIDs
#define SERVICE_UUID           "12345678-1234-5678-1234-56789abcdef0"
#define CHAR_CPU_UUID          "12345678-1234-5678-1234-56789abcdef1"
#define CHAR_MEMORY_UUID       "12345678-1234-5678-1234-56789abcdef2"
#define CHAR_STORAGE_UUID      "12345678-1234-5678-1234-56789abcdef3"
#define CHAR_NETWORK_UUID      "12345678-1234-5678-1234-56789abcdef4"
#define CHAR_SYSTEM_UUID       "12345678-1234-5678-1234-56789abcdef5"
#define CHAR_REGISTRATION_UUID "12345678-1234-5678-1234-56789abcdef6"

// BLE Settings
#define BLE_DEVICE_NAME        "M5Stack-RpiMon"
#define BLE_SCAN_DURATION      5  // seconds
#define BLE_RECONNECT_INTERVAL 3000  // ms

// UI Settings
#define SCREEN_WIDTH           320
#define SCREEN_HEIGHT          240
#define HEADER_HEIGHT          24
#define FOOTER_HEIGHT          20
#define STATUS_BAR_HEIGHT      16

// Colors
#define COLOR_BG               TFT_BLACK
#define COLOR_HEADER_BG        0x1A1A
#define COLOR_TEXT             TFT_WHITE
#define COLOR_TEXT_DIM          TFT_DARKGREY
#define COLOR_ACCENT           0x07FF  // Cyan
#define COLOR_GOOD             TFT_GREEN
#define COLOR_WARN             TFT_YELLOW
#define COLOR_BAD              TFT_RED
#define COLOR_BAR_BG           0x2104
#define COLOR_BUTTON_BG        0x3186

// Update interval
#define DATA_UPDATE_INTERVAL   2000  // ms
