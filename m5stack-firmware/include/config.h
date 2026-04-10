#pragma once

// BLE Service and Characteristic UUIDs
#define SERVICE_UUID           "12345678-1234-5678-1234-56789abcdef0"
#define CHAR_CPU_UUID          "12345678-1234-5678-1234-56789abcdef1"
#define CHAR_MEMORY_UUID       "12345678-1234-5678-1234-56789abcdef2"
#define CHAR_STORAGE_UUID      "12345678-1234-5678-1234-56789abcdef3"
#define CHAR_NETWORK_UUID      "12345678-1234-5678-1234-56789abcdef4"
#define CHAR_SYSTEM_UUID       "12345678-1234-5678-1234-56789abcdef5"
#define CHAR_REGISTRATION_UUID "12345678-1234-5678-1234-56789abcdef6"
#define CHAR_SERVICES_UUID     "12345678-1234-5678-1234-56789abcdef7"
#define CHAR_SYSTEM_CTRL_UUID  "12345678-1234-5678-1234-56789abcdef8"
#define CHAR_COMMANDS_UUID     "12345678-1234-5678-1234-56789abcdef9"
#define CHAR_ROS2_UUID         "12345678-1234-5678-1234-56789abcdefa"

// Services screen
#define MAX_SERVICES           8
#define MAX_COMMANDS           8
#define MAX_ROS2_NODES         10
#define MAX_ROS2_TOPICS        10

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

// Speaker Settings
#define SPEAKER_PIN            25    // M5Stack internal speaker GPIO
#define TONE_BLE_CONNECT       1000  // Hz
#define TONE_BLE_DISCONNECT    400   // Hz
#define TONE_ALERT             2000  // Hz
#define TONE_DURATION          150   // ms

// Alert Thresholds
#define ALERT_CPU_USAGE        90.0f  // %
#define ALERT_CPU_TEMP         80.0f  // Celsius
#define ALERT_RAM_USAGE        90.0f  // %
#define ALERT_STORAGE_USAGE    95.0f  // %
#define ALERT_COOLDOWN         30000  // ms (同じアラートの再発火間隔)

// LCD Brightness
#define LCD_BRIGHTNESS         80
