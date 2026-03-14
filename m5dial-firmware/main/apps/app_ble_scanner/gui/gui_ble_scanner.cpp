#include "gui_ble_scanner.h"
#include <cstdio>

using MOONCAKE::USER_APP::BLE_SCANNER::FoundDevice;

void GUI_BLE_Scanner::init()
{
    _canvas->fillScreen(TFT_DARKGRAY);
    _canvas->fillSmoothCircle(120, 120, 120, _theme_color);
    _draw_top_icon();

    BasicObeject_t bubble;
    bubble.x = 120;
    bubble.y = 120;
    bubble.width = 240;
    bubble.height = 140;
    _canvas->fillSmoothRoundRect(
        bubble.x - bubble.width / 2, bubble.y - bubble.height / 2,
        bubble.width, bubble.height, 36, TFT_WHITE);

    _canvas->setTextColor(_theme_color);
    _canvas->drawCenterString("BLE SCAN", bubble.x, bubble.y - 65);
    _canvas->drawCenterString("Press to scan", bubble.x, bubble.y - 12);

    _draw_quit_button();
    _canvas->drawCircle(120, 120, 120, TFT_DARKGRAY);
    _canvas->pushSprite(0, 0);
}

void GUI_BLE_Scanner::renderPage(
    bool scanning,
    const std::vector<FoundDevice>& devices,
    int selected_index,
    const std::string& saved_name)
{
    _canvas->fillScreen(TFT_DARKGRAY);
    _canvas->fillSmoothCircle(120, 120, 120, _theme_color);

    BasicObeject_t bubble;
    bubble.x = 120;
    bubble.y = 120;
    bubble.width = 240;
    bubble.height = 160;
    _canvas->fillSmoothRoundRect(
        bubble.x - bubble.width / 2, bubble.y - bubble.height / 2,
        bubble.width, bubble.height, 36, TFT_WHITE);

    _canvas->setTextColor(_theme_color);

    if (scanning) {
        // Scanning animation
        _canvas->drawCenterString("BLE SCAN", bubble.x, bubble.y - 65);

        _canvas->setFont(GUI_FONT_CN_SMALL);
        _canvas->setTextColor(TFT_ORANGE, TFT_WHITE);
        _canvas->drawCenterString("Scanning...", bubble.x, bubble.y - 10);

        // Animated dots
        unsigned long ms = millis();
        int dot_count = (ms / 400) % 4;
        int dot_y = bubble.y + 16;
        for (int i = 0; i < 4; i++) {
            uint32_t col = (i < dot_count) ? _theme_color : 0xCCCCCC;
            _canvas->fillCircle(bubble.x - 24 + i * 16, dot_y, 3, col);
        }

        _canvas->setFont(GUI_FONT_CN_BIG);

    } else if (devices.empty()) {
        // No devices found
        _canvas->drawCenterString("BLE SCAN", bubble.x, bubble.y - 65);

        _canvas->setFont(GUI_FONT_CN_SMALL);
        _canvas->setTextColor(TFT_DARKGRAY, TFT_WHITE);
        _canvas->drawCenterString("No RPi found", bubble.x, bubble.y - 16);
        _canvas->drawCenterString("Press to rescan", bubble.x, bubble.y + 4);

        // Show saved device if any
        if (!saved_name.empty()) {
            _canvas->setTextColor(TFT_DARKGREEN, TFT_WHITE);
            char buf[48];
            snprintf(buf, sizeof(buf), "Saved: %s", saved_name.c_str());
            _canvas->drawCenterString(buf, bubble.x, bubble.y + 30);
        }
        _canvas->setFont(GUI_FONT_CN_BIG);

    } else {
        // Device list
        _canvas->drawCenterString("BLE SCAN", bubble.x, bubble.y - 65);

        int line_width = 90;
        _canvas->drawFastHLine(
            bubble.x - line_width / 2, bubble.y - 38, line_width, _theme_color);

        _canvas->setFont(GUI_FONT_CN_SMALL);

        int max_visible = 3;
        int start = 0;
        if (selected_index >= max_visible) {
            start = selected_index - max_visible + 1;
        }

        for (int i = 0; i < max_visible && (start + i) < (int)devices.size(); i++) {
            int idx = start + i;
            const auto& dev = devices[idx];
            int row_y = bubble.y - 28 + i * 24;

            bool is_selected = (idx == selected_index);
            bool is_saved = (!saved_name.empty() && dev.name == saved_name);

            if (is_selected) {
                // Highlight selected row
                _canvas->fillRoundRect(20, row_y - 2, 200, 20, 4, _theme_color);
                _canvas->setTextColor(TFT_WHITE, _theme_color);
            } else if (is_saved) {
                _canvas->setTextColor(TFT_DARKGREEN, TFT_WHITE);
            } else {
                _canvas->setTextColor(TFT_DARKGRAY, TFT_WHITE);
            }

            // Device name + RSSI
            char label[48];
            snprintf(label, sizeof(label), "%s%s (%d)",
                     is_saved ? "* " : "",
                     dev.name.c_str(), dev.rssi);

            // Truncate if needed
            if (strlen(label) > 24) {
                label[21] = '.';
                label[22] = '.';
                label[23] = '\0';
            }

            _canvas->setCursor(28, row_y);
            _canvas->print(label);
        }

        // Instructions at bottom
        int hint_y = bubble.y + 48;
        _canvas->setTextColor(TFT_DARKGRAY, TFT_WHITE);
        _canvas->drawCenterString("Rotate:Scroll Press:Save", bubble.x, hint_y);

        _canvas->setFont(GUI_FONT_CN_BIG);
    }

    _draw_top_icon();
    _draw_quit_button();
    _canvas->drawCircle(120, 120, 120, TFT_DARKGRAY);
    _canvas->pushSprite(0, 0);
}

void GUI_BLE_Scanner::renderSaved(const std::string& saved_name)
{
    _canvas->fillScreen(TFT_DARKGRAY);
    _canvas->fillSmoothCircle(120, 120, 120, _theme_color);

    BasicObeject_t bubble;
    bubble.x = 120;
    bubble.y = 120;
    bubble.width = 220;
    bubble.height = 100;
    _canvas->fillSmoothRoundRect(
        bubble.x - bubble.width / 2, bubble.y - bubble.height / 2,
        bubble.width, bubble.height, 24, TFT_WHITE);

    _canvas->setFont(GUI_FONT_CN_SMALL);

    if (saved_name.empty()) {
        _canvas->setTextColor(TFT_ORANGE, TFT_WHITE);
        _canvas->drawCenterString("Cleared", bubble.x, bubble.y - 16);
        _canvas->setTextColor(TFT_DARKGRAY, TFT_WHITE);
        _canvas->drawCenterString("No auto-connect", bubble.x, bubble.y + 8);
    } else {
        _canvas->setTextColor(TFT_DARKGREEN, TFT_WHITE);
        _canvas->drawCenterString("Saved!", bubble.x, bubble.y - 24);
        _canvas->setTextColor(_theme_color, TFT_WHITE);
        _canvas->drawCenterString(saved_name.c_str(), bubble.x, bubble.y);
        _canvas->setTextColor(TFT_DARKGRAY, TFT_WHITE);
        _canvas->drawCenterString("RPi Monitor will", bubble.x, bubble.y + 20);
    }

    _canvas->setFont(GUI_FONT_CN_BIG);

    _draw_top_icon();
    _canvas->drawCircle(120, 120, 120, TFT_DARKGRAY);
    _canvas->pushSprite(0, 0);
}
