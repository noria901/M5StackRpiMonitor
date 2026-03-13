#include "rpi_monitor_gui.h"
#include <cstdio>
#include <cstring>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void RpiMonitorGui::init(HAL::HAL* hal)
{
    _hal = hal;
}

// ---- Drawing Primitives ----

void RpiMonitorGui::_drawTitle(const char* title)
{
    auto* c = _hal->canvas;
    c->setTextColor(COL_ACCENT);
    c->setFont(&fonts::Font2);
    int tw = c->textWidth(title);
    c->setCursor((DISP_W - tw) / 2, TITLE_Y);
    c->print(title);
}

void RpiMonitorGui::_drawArcGauge(int cx, int cy, int rOuter, int rInner,
                                   float percent, uint16_t color,
                                   const char* label, const char* valueStr)
{
    auto* c = _hal->canvas;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    // Background arc: full 270 degrees (from 135 to 405 = 135+270)
    float startAngle = 135.0f;
    float fullSweep = 270.0f;

    // Draw background arc
    c->drawArc(cx, cy, rOuter, rInner,
               startAngle, startAngle + fullSweep, COL_BAR_BG);

    // Draw filled arc
    if (percent > 0) {
        float endAngle = startAngle + (fullSweep * percent / 100.0f);
        c->drawArc(cx, cy, rOuter, rInner, startAngle, endAngle, color);
    }

    // Center label
    if (label) {
        c->setTextColor(COL_TEXT_DIM);
        c->setFont(&fonts::Font0);
        int lw = c->textWidth(label);
        c->setCursor(cx - lw / 2, cy - 14);
        c->print(label);
    }

    // Center value
    if (valueStr) {
        c->setTextColor(color);
        c->setFont(&fonts::Font2);
        int vw = c->textWidth(valueStr);
        c->setCursor(cx - vw / 2, cy + 2);
        c->print(valueStr);
    }
}

void RpiMonitorGui::_drawProgressBar(int x, int y, int w, int h,
                                      float percent, uint16_t color)
{
    auto* c = _hal->canvas;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    c->drawRect(x, y, w, h, COL_TEXT_DIM);
    int fillW = (int)((w - 2) * percent / 100.0f);
    if (fillW > 0) {
        c->fillRect(x + 1, y + 1, fillW, h - 2, color);
    }
    int emptyW = w - 2 - fillW;
    if (emptyW > 0) {
        c->fillRect(x + 1 + fillW, y + 1, emptyW, h - 2, COL_BAR_BG);
    }
}

void RpiMonitorGui::_drawKeyValue(int y, const char* key, const char* value,
                                   uint16_t valueColor)
{
    auto* c = _hal->canvas;
    c->setFont(&fonts::Font0);

    c->setTextColor(COL_TEXT_DIM);
    c->setCursor(TEXT_LEFT, y);
    c->print(key);

    c->setTextColor(valueColor);
    // Right-align value within safe area
    int vw = c->textWidth(value);
    c->setCursor(TEXT_RIGHT - vw, y);
    c->print(value);
}

void RpiMonitorGui::_drawCenteredText(int y, const char* text, uint16_t color,
                                       const lgfx::IFont* font)
{
    auto* c = _hal->canvas;
    c->setFont(font);
    c->setTextColor(color);
    int tw = c->textWidth(text);
    c->setCursor((DISP_W - tw) / 2, y);
    c->print(text);
}

uint16_t RpiMonitorGui::_getUsageColor(float percent)
{
    if (percent < 60) return COL_GOOD;
    if (percent < 85) return COL_WARN;
    return COL_BAD;
}

// ---- Page Indicator ----

void RpiMonitorGui::drawPageIndicator(int currentPage, int totalPages)
{
    auto* c = _hal->canvas;
    if (totalPages <= 0) return;

    int dotR = 3;
    int gap = 10;
    int totalW = totalPages * (dotR * 2) + (totalPages - 1) * (gap - dotR * 2);
    int startX = (DISP_W - totalW) / 2;

    for (int i = 0; i < totalPages; i++) {
        int x = startX + i * gap;
        if (i == currentPage) {
            c->fillCircle(x + dotR, DOT_Y, dotR, COL_ACCENT);
        } else {
            c->fillCircle(x + dotR, DOT_Y, dotR - 1, COL_TEXT_DIM);
        }
    }
}

// ---- Dashboard Screen ----
// Two arc gauges (CPU + RAM) side by side, temperature, IP, storage bar

void RpiMonitorGui::drawDashboard(const RpiBleClient& ble)
{
    _drawTitle("Dashboard");

    auto& cpu = ble.getCpuInfo();
    auto& mem = ble.getMemoryInfo();
    auto& st = ble.getStorageInfo();
    auto& net = ble.getNetworkInfo();

    float ramPct = mem.ramTotal > 0 ? (float)mem.ramUsed / mem.ramTotal * 100.0f : 0;
    float stPct = st.total > 0 ? (float)st.used / st.total * 100.0f : 0;

    char buf[32];

    // CPU arc gauge (left)
    snprintf(buf, sizeof(buf), "%.0f%%", cpu.usage);
    _drawArcGauge(75, 100, 42, 34, cpu.usage,
                  _getUsageColor(cpu.usage), "CPU", buf);

    // RAM arc gauge (right)
    snprintf(buf, sizeof(buf), "%.0f%%", ramPct);
    _drawArcGauge(165, 100, 42, 34, ramPct,
                  _getUsageColor(ramPct), "RAM", buf);

    // Temperature
    int y = 155;
    snprintf(buf, sizeof(buf), "%.1f C", cpu.temp);
    _drawKeyValue(y, "Temp:", buf, _getUsageColor(cpu.temp / 85.0f * 100.0f));

    // IP Address
    y += 16;
    _drawKeyValue(y, "IP:", net.ip.c_str(), COL_ACCENT);

    // Storage bar
    y += 18;
    auto* c = _hal->canvas;
    c->setFont(&fonts::Font0);
    c->setTextColor(COL_TEXT_DIM);
    c->setCursor(TEXT_LEFT, y);
    snprintf(buf, sizeof(buf), "Disk %.0f%%", stPct);
    c->print(buf);
    _drawProgressBar(TEXT_LEFT + 60, y - 1, TEXT_RIGHT - TEXT_LEFT - 60, 10,
                     stPct, _getUsageColor(stPct));
}

// ---- CPU Detail Screen ----

void RpiMonitorGui::drawCpuDetail(const RpiBleClient& ble)
{
    _drawTitle("CPU");

    auto& cpu = ble.getCpuInfo();
    char buf[32];

    // Large arc gauge
    snprintf(buf, sizeof(buf), "%.1f%%", cpu.usage);
    _drawArcGauge(CENTER_X, 100, 60, 48, cpu.usage,
                  _getUsageColor(cpu.usage), "Usage", buf);

    // Temperature
    int y = 170;
    snprintf(buf, sizeof(buf), "%.1f C", cpu.temp);
    _drawKeyValue(y, "Temp:", buf, _getUsageColor(cpu.temp / 85.0f * 100.0f));

    // Frequency
    y += 18;
    snprintf(buf, sizeof(buf), "%d MHz", cpu.freq);
    _drawKeyValue(y, "Freq:", buf, COL_TEXT);
}

// ---- Memory Detail Screen ----

void RpiMonitorGui::drawMemoryDetail(const RpiBleClient& ble)
{
    _drawTitle("Memory");

    auto& mem = ble.getMemoryInfo();
    char buf[48];

    float ramPct = mem.ramTotal > 0 ? (float)mem.ramUsed / mem.ramTotal * 100.0f : 0;
    float swapPct = mem.swapTotal > 0 ? (float)mem.swapUsed / mem.swapTotal * 100.0f : 0;

    int y = CONTENT_START_Y;

    // RAM section
    auto* c = _hal->canvas;
    c->setFont(&fonts::Font0);
    c->setTextColor(COL_TEXT);
    c->setCursor(TEXT_LEFT, y);
    c->print("RAM");
    y += 14;

    _drawProgressBar(TEXT_LEFT, y, TEXT_RIGHT - TEXT_LEFT, 12, ramPct,
                     _getUsageColor(ramPct));
    y += 16;

    snprintf(buf, sizeof(buf), "%d / %d MB", mem.ramUsed, mem.ramTotal);
    _drawKeyValue(y, "Used:", buf, _getUsageColor(ramPct));
    y += 16;

    snprintf(buf, sizeof(buf), "%.1f%%", ramPct);
    _drawKeyValue(y, "Pct:", buf, _getUsageColor(ramPct));

    // Swap section
    y += 26;
    c->setFont(&fonts::Font0);
    c->setTextColor(COL_TEXT);
    c->setCursor(TEXT_LEFT, y);
    c->print("Swap");
    y += 14;

    _drawProgressBar(TEXT_LEFT, y, TEXT_RIGHT - TEXT_LEFT, 12, swapPct,
                     _getUsageColor(swapPct));
    y += 16;

    snprintf(buf, sizeof(buf), "%d / %d MB", mem.swapUsed, mem.swapTotal);
    _drawKeyValue(y, "Used:", buf, _getUsageColor(swapPct));
    y += 16;

    snprintf(buf, sizeof(buf), "%.1f%%", swapPct);
    _drawKeyValue(y, "Pct:", buf, _getUsageColor(swapPct));
}

// ---- Storage Detail Screen ----

void RpiMonitorGui::drawStorageDetail(const RpiBleClient& ble)
{
    _drawTitle("Storage");

    auto& st = ble.getStorageInfo();
    char buf[32];
    float pct = st.total > 0 ? (float)st.used / st.total * 100.0f : 0;

    // Large arc gauge
    snprintf(buf, sizeof(buf), "%.1f%%", pct);
    _drawArcGauge(CENTER_X, 100, 60, 48, pct,
                  _getUsageColor(pct), "Disk", buf);

    int y = 170;
    snprintf(buf, sizeof(buf), "%.1f GB", st.total / 1000.0f);
    _drawKeyValue(y, "Total:", buf, COL_TEXT);

    y += 16;
    snprintf(buf, sizeof(buf), "%.1f / %.1f GB", st.used / 1000.0f, st.total / 1000.0f);
    _drawKeyValue(y, "Used:", buf, _getUsageColor(pct));

    y += 16;
    snprintf(buf, sizeof(buf), "%.1f GB", st.free / 1000.0f);
    _drawKeyValue(y, "Free:", buf, COL_GOOD);
}

// ---- Network Screen ----

void RpiMonitorGui::drawNetwork(const RpiBleClient& ble)
{
    _drawTitle("Network");

    auto& net = ble.getNetworkInfo();
    char buf[32];

    int y = CONTENT_START_Y;

    _drawKeyValue(y, "SSID:", net.wifiSsid.c_str(), COL_TEXT);
    y += 16;

    snprintf(buf, sizeof(buf), "%d dBm", net.wifiSignal);
    uint16_t sigColor = net.wifiSignal > -50 ? COL_GOOD :
                        (net.wifiSignal > -70 ? COL_WARN : COL_BAD);
    _drawKeyValue(y, "Signal:", buf, sigColor);

    // Signal strength bar
    y += 14;
    float sigPct = 0;
    if (net.wifiSignal > -30) sigPct = 100;
    else if (net.wifiSignal > -90) sigPct = (float)(net.wifiSignal + 90) / 60.0f * 100.0f;
    _drawProgressBar(TEXT_LEFT, y, TEXT_RIGHT - TEXT_LEFT, 8, sigPct, sigColor);

    y += 16;
    _drawKeyValue(y, "IP:", net.ip.c_str(), COL_ACCENT);

    y += 16;
    _drawKeyValue(y, "MAC:", net.mac.c_str(), COL_TEXT_DIM);

    y += 20;
    // Hotspot section
    auto* c = _hal->canvas;
    c->setFont(&fonts::Font0);
    c->setTextColor(COL_TEXT_DIM);
    int tw = c->textWidth("-- Hotspot --");
    c->setCursor((DISP_W - tw) / 2, y);
    c->print("-- Hotspot --");

    y += 14;
    _drawKeyValue(y, "Status:", net.hotspot ? "Active" : "Inactive",
                  net.hotspot ? COL_GOOD : COL_TEXT_DIM);

    if (net.hotspot) {
        y += 16;
        _drawKeyValue(y, "SSID:", net.hotspotSsid.c_str(), COL_TEXT);
    }
}

// ---- System Info Screen ----

void RpiMonitorGui::drawSystemInfo(const RpiBleClient& ble)
{
    auto& sys = ble.getSystemInfo();
    const char* title = (sys.platform == "jetson") ? "Jetson" : "System";
    _drawTitle(title);

    char buf[48];
    int y = CONTENT_START_Y;

    _drawKeyValue(y, "Host:", sys.hostname.c_str(), COL_ACCENT);
    y += 16;

    _drawKeyValue(y, "OS:", sys.os.c_str(), COL_TEXT);
    y += 16;

    _drawKeyValue(y, "Kernel:", sys.kernel.c_str(), COL_TEXT);
    y += 16;

    // Uptime formatting
    unsigned long sec = sys.uptime;
    int days = sec / 86400;
    int hours = (sec % 86400) / 3600;
    int mins = (sec % 3600) / 60;
    snprintf(buf, sizeof(buf), "%dd %dh %dm", days, hours, mins);
    _drawKeyValue(y, "Uptime:", buf, COL_GOOD);

    y += 16;
    if (!sys.time.empty()) {
        _drawKeyValue(y, "Time:", sys.time.c_str(), COL_TEXT);
    }

    y += 16;
    if (!sys.platform.empty()) {
        _drawKeyValue(y, "Arch:", sys.platform.c_str(), COL_TEXT_DIM);
    }
}

// ---- Registration Screen ----

void RpiMonitorGui::drawRegistration(const RpiBleClient& ble, int selectedDevice,
                                      bool confirmMode)
{
    _drawTitle("Register");

    auto* c = _hal->canvas;
    BleState state = ble.getState();

    if (state == BleState::CONNECTED) {
        int y = 80;
        _drawCenteredText(y, "Connected", COL_GOOD, &fonts::Font2);
        y += 24;
        _drawCenteredText(y, ble.getSavedServerName().c_str(), COL_ACCENT, &fonts::Font2);
        y += 36;
        _drawCenteredText(y, "Press to disconnect", COL_TEXT_DIM, &fonts::Font0);
        return;
    }

    if (state == BleState::SCANNING) {
        _drawCenteredText(100, "Scanning...", COL_ACCENT, &fonts::Font2);

        // Spinning animation dots
        unsigned long ms = (unsigned long)(esp_timer_get_time() / 1000);
        int dotIdx = (ms / 300) % 4;
        for (int i = 0; i < 4; i++) {
            uint16_t col = (i == dotIdx) ? COL_ACCENT : COL_TEXT_DIM;
            c->fillCircle(CENTER_X - 24 + i * 16, 130, 3, col);
        }
        return;
    }

    if (state == BleState::CONNECTING) {
        _drawCenteredText(100, "Connecting...", COL_WARN, &fonts::Font2);
        return;
    }

    int count = ble.getFoundDeviceCount();
    if (count == 0) {
        int y = 80;
        _drawCenteredText(y, "No devices", COL_TEXT_DIM, &fonts::Font2);
        y += 30;
        _drawCenteredText(y, "Press button to", COL_TEXT_DIM, &fonts::Font0);
        y += 14;
        _drawCenteredText(y, "scan for RPi", COL_TEXT_DIM, &fonts::Font0);

        // Draw scan icon (circle with waves)
        y += 24;
        c->drawCircle(CENTER_X, y + 8, 8, COL_ACCENT);
        c->drawArc(CENTER_X, y + 8, 16, 14, 300, 60, COL_ACCENT);
        c->drawArc(CENTER_X, y + 8, 24, 22, 310, 50, COL_ACCENT);
        return;
    }

    // Device list
    int y = CONTENT_START_Y;
    int maxVisible = 4;
    c->setFont(&fonts::Font0);

    for (int i = 0; i < count && i < maxVisible; i++) {
        bool selected = (i == selectedDevice);
        int rowY = y + i * 22;

        if (selected) {
            // Highlight selected row
            c->fillRoundRect(TEXT_LEFT - 4, rowY - 3, TEXT_RIGHT - TEXT_LEFT + 8, 20,
                             4, COL_HIGHLIGHT);
            c->setTextColor(COL_TEXT);
        } else {
            c->setTextColor(COL_TEXT_DIM);
        }

        char label[48];
        snprintf(label, sizeof(label), "%d. %s", i + 1,
                 ble.getFoundDeviceName(i).c_str());

        // Truncate if too wide
        if (c->textWidth(label) > (TEXT_RIGHT - TEXT_LEFT)) {
            label[20] = '.';
            label[21] = '.';
            label[22] = '\0';
        }

        c->setCursor(TEXT_LEFT, rowY);
        c->print(label);
    }

    // Confirm dialog
    y = y + maxVisible * 22 + 8;
    if (confirmMode) {
        c->setFont(&fonts::Font0);

        char msg[48];
        snprintf(msg, sizeof(msg), "Connect to #%d?", selectedDevice + 1);
        _drawCenteredText(y, msg, COL_WARN, &fonts::Font0);
        y += 16;
        _drawCenteredText(y, "Rotate=Cancel  Press=OK", COL_TEXT_DIM, &fonts::Font0);
    } else {
        _drawCenteredText(y, "Rotate=Scroll  Press=Select", COL_TEXT_DIM, &fonts::Font0);
    }
}

// ---- Disconnected Overlay ----

void RpiMonitorGui::drawDisconnectedOverlay(bool hasSavedServer, const char* serverName)
{
    auto* c = _hal->canvas;

    // Semi-transparent dark overlay (draw filled rect with dark color)
    // Note: LovyanGFX doesn't support alpha blending on sprites easily,
    // so we just draw on top with a dark background in the center area
    c->fillRoundRect(20, 60, 200, 120, 12, 0x0841);  // very dark gray
    c->drawRoundRect(20, 60, 200, 120, 12, COL_ACCENT);

    if (hasSavedServer) {
        _drawCenteredText(75, "Reconnecting", COL_ACCENT, &fonts::Font2);

        char buf[48];
        snprintf(buf, sizeof(buf), "to %s", serverName);
        _drawCenteredText(100, buf, COL_TEXT_DIM, &fonts::Font0);

        // Animated dots
        unsigned long ms = (unsigned long)(esp_timer_get_time() / 1000);
        int dotCount = (ms / 500) % 4;
        char dots[8] = "...";
        dots[dotCount] = '\0';
        _drawCenteredText(120, dots, COL_ACCENT, &fonts::Font2);

        _drawCenteredText(145, "Press to cancel", COL_TEXT_DIM, &fonts::Font0);
    } else {
        _drawCenteredText(80, "Disconnected", COL_TEXT_DIM, &fonts::Font2);
        _drawCenteredText(110, "Go to Register", COL_TEXT_DIM, &fonts::Font0);
        _drawCenteredText(124, "screen to connect", COL_TEXT_DIM, &fonts::Font0);
        _drawCenteredText(150, "Rotate encoder", COL_ACCENT, &fonts::Font0);
    }
}
