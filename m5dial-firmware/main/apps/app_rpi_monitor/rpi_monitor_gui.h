#pragma once

#include "../../hal/hal.h"
#include "../app.h"
#include "rpi_ble_client.h"

class RpiMonitorGui {
public:
    void init(HAL::HAL* hal);

    // Screen drawing methods (all draw to canvas sprite)
    void drawDashboard(const RpiBleClient& ble);
    void drawCpuDetail(const RpiBleClient& ble);
    void drawMemoryDetail(const RpiBleClient& ble);
    void drawStorageDetail(const RpiBleClient& ble);
    void drawNetwork(const RpiBleClient& ble);
    void drawSystemInfo(const RpiBleClient& ble);
    void drawServices(const RpiBleClient& ble, int selectedIndex, bool confirmMode);
    void drawPowerMenu(int selectedIndex, bool confirmMode);
    void drawCommands(const RpiBleClient& ble, int selectedIndex, bool confirmMode);
    void drawQrCode(const RpiBleClient& ble);
    void drawSettings(bool soundEnabled);

    // Overlay
    void drawDisconnectedOverlay(bool hasSavedServer, const char* serverName);

    // Page indicator dots at bottom
    void drawPageIndicator(int currentPage, int totalPages);

private:
    HAL::HAL* _hal = nullptr;

    // Display constants for 240x240 round display
    static constexpr int DISP_W = 240;
    static constexpr int DISP_H = 240;
    static constexpr int CENTER_X = 120;
    static constexpr int CENTER_Y = 120;
    static constexpr int SAFE_RADIUS = 110;

    // Layout constants
    static constexpr int TITLE_Y = 22;
    static constexpr int CONTENT_START_Y = 48;
    static constexpr int DOT_Y = 226;
    static constexpr int TEXT_LEFT = 30;
    static constexpr int TEXT_RIGHT = 210;

    // Colors (RGB565)
    static constexpr uint16_t COL_BG       = 0x0000;  // Black
    static constexpr uint16_t COL_TEXT      = 0xFFFF;  // White
    static constexpr uint16_t COL_TEXT_DIM  = 0x7BEF;  // Gray
    static constexpr uint16_t COL_ACCENT    = 0x07FF;  // Cyan
    static constexpr uint16_t COL_GOOD      = 0x07E0;  // Green
    static constexpr uint16_t COL_WARN      = 0xFFE0;  // Yellow
    static constexpr uint16_t COL_BAD       = 0xF800;  // Red
    static constexpr uint16_t COL_BAR_BG    = 0x2104;  // Dark gray
    static constexpr uint16_t COL_HIGHLIGHT = 0x001F;  // Blue highlight

    // Drawing primitives
    void _drawTitle(const char* title);
    void _drawArcGauge(int cx, int cy, int rOuter, int rInner,
                       float percent, uint16_t color, const char* label,
                       const char* valueStr);
    void _drawProgressBar(int x, int y, int w, int h, float percent, uint16_t color);
    void _drawKeyValue(int y, const char* key, const char* value,
                       uint16_t valueColor = 0xFFFF);
    void _drawCenteredText(int y, const char* text, uint16_t color, const lgfx::IFont* font);

    uint16_t _getUsageColor(float percent);
};
