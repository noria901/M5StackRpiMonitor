#include "ui.h"

void UI::init() {
    M5.Lcd.fillScreen(COLOR_BG);
    needsFullRedraw = true;
}

void UI::nextScreen() {
    int next = ((int)currentScreen + 1) % (int)Screen::SCREEN_COUNT;
    currentScreen = (Screen)next;
    needsFullRedraw = true;
    regConfirmMode = false;
}

void UI::prevScreen() {
    int prev = ((int)currentScreen - 1 + (int)Screen::SCREEN_COUNT) % (int)Screen::SCREEN_COUNT;
    currentScreen = (Screen)prev;
    needsFullRedraw = true;
    regConfirmMode = false;
}

void UI::buttonAction(BLEMonitorClient& ble) {
    if (currentScreen == Screen::REGISTRATION) {
        if (!ble.isConnected()) {
            // スキャン or デバイス選択
            if (ble.getFoundDeviceCount() == 0) {
                ble.scan();
                needsFullRedraw = true;
            } else if (regConfirmMode) {
                // 接続実行
                BLEAdvertisedDevice* dev = ble.getFoundDevice(regSelectedDevice);
                if (dev) {
                    ble.connectToServer(dev);
                    if (ble.isConnected()) {
                        ble.sendRegistration(BLE_DEVICE_NAME);
                    }
                }
                regConfirmMode = false;
                needsFullRedraw = true;
            } else {
                regConfirmMode = true;
                needsFullRedraw = true;
            }
        } else {
            // 切断 & 保存デバイスを忘れる
            ble.forgetDevice();
            needsFullRedraw = true;
        }
        return;
    }
    // 他の画面ではBtnBでデータ更新
    if (ble.isConnected()) {
        ble.readAll();
        needsFullRedraw = true;
    }
}

void UI::setNeedsRedraw() {
    needsFullRedraw = true;
}

Screen UI::getCurrentScreen() {
    return currentScreen;
}

void UI::update(BLEMonitorClient& ble) {
    unsigned long now = millis();
    if (!needsFullRedraw && (now - lastUpdate) < DATA_UPDATE_INTERVAL) {
        return;
    }

    if (ble.isConnected()) {
        ble.readAll();
    }

    if (needsFullRedraw) {
        M5.Lcd.fillScreen(COLOR_BG);
    } else {
        // データ更新時はコンテンツエリアのみクリア（数値の重なりを防ぐ）
        M5.Lcd.fillRect(0, HEADER_HEIGHT, SCREEN_WIDTH,
                        SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT, COLOR_BG);
    }

    drawHeader(ble);
    drawFooter();

    if (!ble.isConnected() && currentScreen != Screen::REGISTRATION) {
        drawDisconnected(ble);
    } else {
        switch (currentScreen) {
            case Screen::DASHBOARD:     drawDashboard(ble); break;
            case Screen::CPU_DETAIL:    drawCpuDetail(ble); break;
            case Screen::MEMORY_DETAIL: drawMemoryDetail(ble); break;
            case Screen::STORAGE_DETAIL:drawStorageDetail(ble); break;
            case Screen::NETWORK:       drawNetwork(ble); break;
            case Screen::SYSTEM_INFO:   drawSystemInfo(ble); break;
            case Screen::REGISTRATION:  drawRegistration(ble); break;
            default: break;
        }
    }

    needsFullRedraw = false;
    lastUpdate = now;
}

void UI::drawHeader(BLEMonitorClient& ble) {
    M5.Lcd.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);
    M5.Lcd.setTextColor(COLOR_ACCENT);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(4, 6);
    M5.Lcd.print("RPi Monitor");

    // 接続状態
    M5.Lcd.setCursor(200, 6);
    if (ble.isConnected()) {
        M5.Lcd.setTextColor(COLOR_GOOD);
        M5.Lcd.printf("BLE: %s", ble.getServerName().c_str());
    } else {
        M5.Lcd.setTextColor(COLOR_BAD);
        M5.Lcd.print("BLE: Disconnected");
    }
}

void UI::drawFooter() {
    int y = SCREEN_HEIGHT - FOOTER_HEIGHT;
    M5.Lcd.fillRect(0, y, SCREEN_WIDTH, FOOTER_HEIGHT, COLOR_HEADER_BG);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_TEXT_DIM);

    const char* screenNames[] = {
        "Dashboard", "CPU", "Memory", "Storage", "Network", "System", "Register"
    };
    int idx = (int)currentScreen;

    // BtnA label
    M5.Lcd.setCursor(30, y + 5);
    M5.Lcd.setTextColor(COLOR_BUTTON_BG);
    M5.Lcd.print("[<Prev]");

    // BtnB label
    M5.Lcd.setCursor(130, y + 5);
    M5.Lcd.setTextColor(COLOR_ACCENT);
    M5.Lcd.printf("[%s]", screenNames[idx]);

    // BtnC label
    M5.Lcd.setCursor(240, y + 5);
    M5.Lcd.setTextColor(COLOR_BUTTON_BG);
    M5.Lcd.print("[Next>]");
}

void UI::drawScreenTitle(const char* title) {
    M5.Lcd.setTextColor(COLOR_ACCENT);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, HEADER_HEIGHT + 8);
    M5.Lcd.print(title);
}

void UI::drawProgressBar(int x, int y, int w, int h, float percent, uint16_t color) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    M5.Lcd.drawRect(x, y, w, h, COLOR_TEXT_DIM);
    int fillW = (int)((w - 2) * percent / 100.0f);
    M5.Lcd.fillRect(x + 1, y + 1, fillW, h - 2, color);
    M5.Lcd.fillRect(x + 1 + fillW, y + 1, w - 2 - fillW, h - 2, COLOR_BAR_BG);
}

void UI::drawKeyValue(int x, int y, const char* key, const char* value, uint16_t valueColor) {
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_TEXT_DIM);
    M5.Lcd.setCursor(x, y);
    M5.Lcd.print(key);
    M5.Lcd.setTextColor(valueColor);
    M5.Lcd.setCursor(x + 110, y);
    M5.Lcd.print(value);
}

uint16_t UI::getUsageColor(float percent) {
    if (percent < 60) return COLOR_GOOD;
    if (percent < 85) return COLOR_WARN;
    return COLOR_BAD;
}

// === Dashboard Screen ===
void UI::drawDashboard(BLEMonitorClient& ble) {
    drawScreenTitle("Dashboard");
    int y = HEADER_HEIGHT + 36;

    auto cpu = ble.getCpuInfo();
    auto mem = ble.getMemoryInfo();
    auto storage = ble.getStorageInfo();

    char buf[32];

    // CPU
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_TEXT);
    M5.Lcd.setCursor(10, y);
    snprintf(buf, sizeof(buf), "CPU  %.1f%%", cpu.usage);
    M5.Lcd.print(buf);
    drawProgressBar(110, y - 2, 190, 14, cpu.usage, getUsageColor(cpu.usage));
    y += 24;

    // RAM
    float ramPercent = mem.ramTotal > 0 ? (float)mem.ramUsed / mem.ramTotal * 100.0f : 0;
    M5.Lcd.setCursor(10, y);
    snprintf(buf, sizeof(buf), "RAM  %.1f%%", ramPercent);
    M5.Lcd.print(buf);
    drawProgressBar(110, y - 2, 190, 14, ramPercent, getUsageColor(ramPercent));
    y += 24;

    // Storage
    float stPercent = storage.total > 0 ? (float)storage.used / storage.total * 100.0f : 0;
    M5.Lcd.setCursor(10, y);
    snprintf(buf, sizeof(buf), "Disk %.1f%%", stPercent);
    M5.Lcd.print(buf);
    drawProgressBar(110, y - 2, 190, 14, stPercent, getUsageColor(stPercent));
    y += 30;

    // Temp
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(10, y);
    M5.Lcd.setTextColor(COLOR_TEXT_DIM);
    M5.Lcd.print("Temp:");
    M5.Lcd.setCursor(60, y);
    M5.Lcd.setTextColor(getUsageColor(cpu.temp / 85.0f * 100.0f));
    snprintf(buf, sizeof(buf), "%.1f C", cpu.temp);
    M5.Lcd.print(buf);

    // IP
    auto net = ble.getNetworkInfo();
    M5.Lcd.setCursor(150, y);
    M5.Lcd.setTextColor(COLOR_TEXT_DIM);
    M5.Lcd.print("IP:");
    M5.Lcd.setCursor(175, y);
    M5.Lcd.setTextColor(COLOR_TEXT);
    M5.Lcd.print(net.ip.c_str());
}

// === CPU Detail Screen ===
void UI::drawCpuDetail(BLEMonitorClient& ble) {
    drawScreenTitle("CPU Detail");
    int y = HEADER_HEIGHT + 40;
    auto cpu = ble.getCpuInfo();
    char buf[32];

    snprintf(buf, sizeof(buf), "%.1f %%", cpu.usage);
    drawKeyValue(10, y, "Usage:", buf, getUsageColor(cpu.usage));
    drawProgressBar(10, y + 14, 300, 12, cpu.usage, getUsageColor(cpu.usage));
    y += 38;

    snprintf(buf, sizeof(buf), "%.1f C", cpu.temp);
    drawKeyValue(10, y, "Temperature:", buf, getUsageColor(cpu.temp / 85.0f * 100.0f));
    y += 20;

    snprintf(buf, sizeof(buf), "%d MHz", cpu.freq);
    drawKeyValue(10, y, "Frequency:", buf);
}

// === Memory Detail Screen ===
void UI::drawMemoryDetail(BLEMonitorClient& ble) {
    drawScreenTitle("Memory");
    int y = HEADER_HEIGHT + 40;
    auto mem = ble.getMemoryInfo();
    char buf[64];

    // RAM
    float ramPercent = mem.ramTotal > 0 ? (float)mem.ramUsed / mem.ramTotal * 100.0f : 0;
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_TEXT);
    M5.Lcd.setCursor(10, y);
    M5.Lcd.print("RAM");
    y += 14;
    drawProgressBar(10, y, 300, 14, ramPercent, getUsageColor(ramPercent));
    y += 18;
    snprintf(buf, sizeof(buf), "%d / %d MB (%.1f%%)", mem.ramUsed, mem.ramTotal, ramPercent);
    drawKeyValue(10, y, "Used:", buf);
    y += 30;

    // Swap
    float swapPercent = mem.swapTotal > 0 ? (float)mem.swapUsed / mem.swapTotal * 100.0f : 0;
    M5.Lcd.setTextColor(COLOR_TEXT);
    M5.Lcd.setCursor(10, y);
    M5.Lcd.print("Swap");
    y += 14;
    drawProgressBar(10, y, 300, 14, swapPercent, getUsageColor(swapPercent));
    y += 18;
    snprintf(buf, sizeof(buf), "%d / %d MB (%.1f%%)", mem.swapUsed, mem.swapTotal, swapPercent);
    drawKeyValue(10, y, "Used:", buf);
}

// === Storage Detail Screen ===
void UI::drawStorageDetail(BLEMonitorClient& ble) {
    drawScreenTitle("Storage");
    int y = HEADER_HEIGHT + 40;
    auto st = ble.getStorageInfo();
    char buf[64];

    float percent = st.total > 0 ? (float)st.used / st.total * 100.0f : 0;
    drawProgressBar(10, y, 300, 18, percent, getUsageColor(percent));
    y += 28;

    snprintf(buf, sizeof(buf), "%.1f GB", st.total / 1000.0f);
    drawKeyValue(10, y, "Total:", buf);
    y += 18;

    snprintf(buf, sizeof(buf), "%.1f GB", st.used / 1000.0f);
    drawKeyValue(10, y, "Used:", buf, getUsageColor(percent));
    y += 18;

    snprintf(buf, sizeof(buf), "%.1f GB", st.free / 1000.0f);
    drawKeyValue(10, y, "Free:", buf, COLOR_GOOD);
    y += 18;

    snprintf(buf, sizeof(buf), "%.1f %%", percent);
    drawKeyValue(10, y, "Usage:", buf, getUsageColor(percent));
}

// === Network Screen ===
void UI::drawNetwork(BLEMonitorClient& ble) {
    drawScreenTitle("Network");
    int y = HEADER_HEIGHT + 40;
    auto net = ble.getNetworkInfo();
    char buf[64];

    drawKeyValue(10, y, "WiFi SSID:", net.wifiSsid.c_str());
    y += 18;

    snprintf(buf, sizeof(buf), "%d dBm", net.wifiSignal);
    uint16_t sigColor = net.wifiSignal > -50 ? COLOR_GOOD : (net.wifiSignal > -70 ? COLOR_WARN : COLOR_BAD);
    drawKeyValue(10, y, "Signal:", buf, sigColor);
    y += 18;

    drawKeyValue(10, y, "IP Address:", net.ip.c_str(), COLOR_ACCENT);
    y += 18;

    drawKeyValue(10, y, "MAC:", net.mac.c_str());
    y += 24;

    // Hotspot
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_TEXT);
    M5.Lcd.setCursor(10, y);
    M5.Lcd.print("--- Hotspot ---");
    y += 16;

    drawKeyValue(10, y, "Status:", net.hotspot ? "Active" : "Inactive",
                 net.hotspot ? COLOR_GOOD : COLOR_TEXT_DIM);
    y += 18;

    if (net.hotspot) {
        drawKeyValue(10, y, "SSID:", net.hotspotSsid.c_str());
    }
}

// === System Info Screen ===
void UI::drawSystemInfo(BLEMonitorClient& ble) {
    drawScreenTitle("System");
    int y = HEADER_HEIGHT + 40;
    auto sys = ble.getSystemInfo();
    char buf[64];

    drawKeyValue(10, y, "Hostname:", sys.hostname.c_str(), COLOR_ACCENT);
    y += 18;

    drawKeyValue(10, y, "OS:", sys.os.c_str());
    y += 18;

    drawKeyValue(10, y, "Kernel:", sys.kernel.c_str());
    y += 18;

    // Uptime formatting
    unsigned long sec = sys.uptime;
    int days = sec / 86400;
    int hours = (sec % 86400) / 3600;
    int mins = (sec % 3600) / 60;
    snprintf(buf, sizeof(buf), "%dd %dh %dm", days, hours, mins);
    drawKeyValue(10, y, "Uptime:", buf, COLOR_GOOD);
}

// === Registration Screen ===
void UI::drawRegistration(BLEMonitorClient& ble) {
    drawScreenTitle("Register");
    int y = HEADER_HEIGHT + 40;

    BLEState bleState = ble.getState();

    if (ble.isConnected()) {
        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(COLOR_GOOD);
        M5.Lcd.setCursor(10, y);
        M5.Lcd.printf("Connected: %s", ble.getServerName().c_str());
        y += 24;

        M5.Lcd.setTextColor(COLOR_TEXT_DIM);
        M5.Lcd.setCursor(10, y);
        M5.Lcd.print("Press [Select] to disconnect");
        return;
    }

    if (bleState == BLEState::SCANNING) {
        M5.Lcd.setTextSize(2);
        M5.Lcd.setTextColor(COLOR_ACCENT);
        M5.Lcd.setCursor(60, 100);
        M5.Lcd.print("Scanning...");
        return;
    }

    int count = ble.getFoundDeviceCount();
    if (count == 0) {
        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(COLOR_TEXT_DIM);
        M5.Lcd.setCursor(10, y);
        M5.Lcd.print("No devices found.");
        y += 20;
        M5.Lcd.setCursor(10, y);
        M5.Lcd.print("Press [Select] to scan for RPi");
        return;
    }

    // デバイスリスト
    M5.Lcd.setTextSize(1);
    for (int i = 0; i < count && i < 5; i++) {
        if (i == regSelectedDevice) {
            M5.Lcd.setTextColor(COLOR_BG);
            M5.Lcd.fillRect(8, y - 2, 304, 16, COLOR_ACCENT);
        } else {
            M5.Lcd.setTextColor(COLOR_TEXT);
        }
        M5.Lcd.setCursor(12, y);
        M5.Lcd.printf("%d: %s", i + 1, ble.getFoundDeviceName(i).c_str());
        y += 18;
    }

    y += 10;
    if (regConfirmMode) {
        M5.Lcd.setTextColor(COLOR_WARN);
        M5.Lcd.setCursor(10, y);
        M5.Lcd.printf("Connect to %s?", ble.getFoundDeviceName(regSelectedDevice).c_str());
        y += 16;
        M5.Lcd.setCursor(10, y);
        M5.Lcd.print("[<Prev] Cancel  [Select] OK  [Next>] ---");
    } else {
        M5.Lcd.setTextColor(COLOR_TEXT_DIM);
        M5.Lcd.setCursor(10, y);
        M5.Lcd.print("[<Prev] Up  [Select] Connect  [Next>] Down");
    }
}

// === Disconnected Screen ===
void UI::drawDisconnected(BLEMonitorClient& ble) {
    if (ble.hasSavedServer()) {
        M5.Lcd.setTextSize(2);
        M5.Lcd.setTextColor(COLOR_ACCENT);
        M5.Lcd.setCursor(30, 80);
        M5.Lcd.print("Auto-connecting");

        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(COLOR_TEXT_DIM);
        M5.Lcd.setCursor(40, 120);
        M5.Lcd.printf("Searching for %s...", ble.getServerName().c_str());
    } else {
        M5.Lcd.setTextSize(2);
        M5.Lcd.setTextColor(COLOR_TEXT_DIM);
        M5.Lcd.setCursor(50, 80);
        M5.Lcd.print("No Connection");

        M5.Lcd.setTextSize(1);
        M5.Lcd.setCursor(40, 120);
        M5.Lcd.print("Go to Register screen to");
        M5.Lcd.setCursor(40, 136);
        M5.Lcd.print("scan and connect to RPi");

        M5.Lcd.setTextColor(COLOR_ACCENT);
        M5.Lcd.setCursor(60, 170);
        M5.Lcd.print("Press [Next>] to navigate");
    }
}
