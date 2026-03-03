#include "ui.h"
#include <qrcode.h>

void UI::init() {
    M5.Lcd.fillScreen(COLOR_BG);
    loadSettings();
    needsFullRedraw = true;
}

void UI::loadSettings() {
    uiPrefs.begin("ui", true);  // read-only
    soundEnabled = uiPrefs.getBool("sound", true);
    uiPrefs.end();
}

void UI::saveSettings() {
    uiPrefs.begin("ui", false);
    uiPrefs.putBool("sound", soundEnabled);
    uiPrefs.end();
}

void UI::playTone(int freq, int duration) {
    if (!soundEnabled) return;
    M5.Speaker.tone(freq, duration);
}

void UI::onBleConnected() {
    playTone(TONE_BLE_CONNECT, TONE_DURATION);
}

void UI::onBleDisconnected() {
    playTone(TONE_BLE_DISCONNECT, TONE_DURATION);
}

void UI::checkAlerts(BLEMonitorClient& ble) {
    if (!soundEnabled || !ble.isConnected()) return;
    unsigned long now = millis();
    auto cpu = ble.getCpuInfo();
    auto mem = ble.getMemoryInfo();
    auto st = ble.getStorageInfo();

    if (cpu.usage >= ALERT_CPU_USAGE && (now - lastAlertCpu > ALERT_COOLDOWN)) {
        playTone(TONE_ALERT, TONE_DURATION);
        lastAlertCpu = now;
    }
    if (cpu.temp >= ALERT_CPU_TEMP && (now - lastAlertTemp > ALERT_COOLDOWN)) {
        playTone(TONE_ALERT, TONE_DURATION);
        lastAlertTemp = now;
    }
    float ramPct = mem.ramTotal > 0 ? (float)mem.ramUsed / mem.ramTotal * 100.0f : 0;
    if (ramPct >= ALERT_RAM_USAGE && (now - lastAlertRam > ALERT_COOLDOWN)) {
        playTone(TONE_ALERT, TONE_DURATION);
        lastAlertRam = now;
    }
    float stPct = st.total > 0 ? (float)st.used / st.total * 100.0f : 0;
    if (stPct >= ALERT_STORAGE_USAGE && (now - lastAlertStorage > ALERT_COOLDOWN)) {
        playTone(TONE_ALERT, TONE_DURATION);
        lastAlertStorage = now;
    }
}

void UI::registrationBtnA() {
    // confirm中 → キャンセル、それ以外 → リスト上移動
    if (regConfirmMode) {
        regConfirmMode = false;
    } else if (regSelectedDevice > 0) {
        regSelectedDevice--;
    }
    needsFullRedraw = true;
}

void UI::registrationBtnC(int deviceCount) {
    // リスト下移動（confirm中は無効）
    if (!regConfirmMode && regSelectedDevice < deviceCount - 1) {
        regSelectedDevice++;
    }
    needsFullRedraw = true;
}

void UI::nextScreen() {
    int next = ((int)currentScreen + 1) % (int)Screen::SCREEN_COUNT;
    currentScreen = (Screen)next;
    needsFullRedraw = true;
    regConfirmMode = false;
    svcConfirmMode = false;
    pwrConfirmMode = false;
}

void UI::prevScreen() {
    int prev = ((int)currentScreen - 1 + (int)Screen::SCREEN_COUNT) % (int)Screen::SCREEN_COUNT;
    currentScreen = (Screen)prev;
    needsFullRedraw = true;
    regConfirmMode = false;
    svcConfirmMode = false;
    pwrConfirmMode = false;
}

void UI::servicesBtnA() {
    if (svcConfirmMode) {
        svcConfirmMode = false;
        needsFullRedraw = true;
    } else if (svcSelectedIndex > 0) {
        svcSelectedIndex--;
        needsFullRedraw = true;
    } else {
        prevScreen();
    }
}

void UI::servicesBtnC(int serviceCount) {
    if (!svcConfirmMode && svcSelectedIndex < serviceCount - 1) {
        svcSelectedIndex++;
        needsFullRedraw = true;
    } else if (!svcConfirmMode) {
        nextScreen();
    }
}

void UI::powerBtnA() {
    if (pwrConfirmMode) {
        pwrConfirmMode = false;
        needsFullRedraw = true;
    } else if (pwrSelectedIndex > 0) {
        pwrSelectedIndex--;
        needsFullRedraw = true;
    } else {
        prevScreen();
    }
}

void UI::powerBtnC() {
    if (!pwrConfirmMode && pwrSelectedIndex < 1) {
        pwrSelectedIndex++;
        needsFullRedraw = true;
    } else if (!pwrConfirmMode) {
        nextScreen();
    }
}

void UI::buttonAction(BLEMonitorClient& ble) {
    if (currentScreen == Screen::SERVICES) {
        if (!ble.isConnected()) return;
        int count = ble.getServiceCount();
        if (count == 0) return;
        if (svcConfirmMode) {
            // 実行: active なら stop、inactive なら start
            auto svc = ble.getServiceInfo(svcSelectedIndex);
            String action = svc.active ? "stop" : "start";
            ble.sendServiceControl(svc.name, action);
            // 状態更新のため少し待ってから再読み込み
            delay(500);
            ble.readAll();
            svcConfirmMode = false;
            needsFullRedraw = true;
        } else {
            svcConfirmMode = true;
            needsFullRedraw = true;
        }
        return;
    }
    if (currentScreen == Screen::POWER_MENU) {
        if (!ble.isConnected()) return;
        if (pwrConfirmMode) {
            // 実行: reboot or shutdown
            String action = (pwrSelectedIndex == 0) ? "reboot" : "shutdown";
            ble.sendPowerCommand(action);
            pwrConfirmMode = false;
            needsFullRedraw = true;
        } else {
            pwrConfirmMode = true;
            needsFullRedraw = true;
        }
        return;
    }
    if (currentScreen == Screen::SETTINGS) {
        // Settings画面ではトグル操作
        soundEnabled = !soundEnabled;
        saveSettings();
        needsFullRedraw = true;
        // 変更確認用にトーンを鳴らす（ONにしたときだけ）
        if (soundEnabled) {
            playTone(TONE_BLE_CONNECT, TONE_DURATION);
        }
        return;
    }
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

    // 接続状態が変わったら全体再描画（画面内容が根本的に変わるため）
    bool connected = ble.isConnected();
    if (connected != lastConnected) {
        needsFullRedraw = true;
        lastConnected = connected;
    }

    if (needsFullRedraw) {
        M5.Lcd.fillScreen(COLOR_BG);
    }

    drawHeader(ble);
    drawFooter();

    if (!ble.isConnected() &&
        currentScreen != Screen::REGISTRATION &&
        currentScreen != Screen::SETTINGS) {
        drawDisconnected(ble);
    } else {
        switch (currentScreen) {
            case Screen::DASHBOARD:     drawDashboard(ble); break;
            case Screen::CPU_DETAIL:    drawCpuDetail(ble); break;
            case Screen::MEMORY_DETAIL: drawMemoryDetail(ble); break;
            case Screen::STORAGE_DETAIL:drawStorageDetail(ble); break;
            case Screen::NETWORK:       drawNetwork(ble); break;
            case Screen::SYSTEM_INFO:   drawSystemInfo(ble); break;
            case Screen::SERVICES:      drawServices(ble); break;
            case Screen::POWER_MENU:    drawPowerMenu(ble); break;
            case Screen::QR_CODE:       drawQrCodeScreen(ble); break;
            case Screen::SETTINGS:      drawSettings(); break;
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
    M5.Lcd.setCursor(4, 2);
    auto sys = ble.getSystemInfo();
    if (sys.platform == "jetson") {
        M5.Lcd.print("Jetson Monitor");
    } else {
        M5.Lcd.print("RPi Monitor");
    }

    // 接続状態 (1行目右側)
    if (ble.isConnected()) {
        M5.Lcd.setTextColor(COLOR_GOOD);
        M5.Lcd.setCursor(200, 2);
        M5.Lcd.printf("BLE: %s", ble.getServerName().c_str());
    } else {
        M5.Lcd.setTextColor(COLOR_BAD);
        M5.Lcd.setCursor(200, 2);
        M5.Lcd.print("BLE: Disconnected");
    }

    // 最終更新時刻 (2行目)
    if (ble.isConnected() && ble.getLastDataMillis() > 0) {
        M5.Lcd.setCursor(4, 14);
        M5.Lcd.setTextColor(COLOR_TEXT_DIM);

        unsigned long elapsed = (millis() - ble.getLastDataMillis()) / 1000;

        if (sys.time.length() > 0) {
            // RPi時刻あり: "Updated: 14:32:05 (3s ago)"
            M5.Lcd.printf("Updated: %s (%lus ago)", sys.time.c_str(), elapsed);
        } else {
            // RPi時刻なし: フォールバック相対表示
            M5.Lcd.printf("Updated: %lus ago", elapsed);
        }
    }
}

void UI::drawFooter() {
    int y = SCREEN_HEIGHT - FOOTER_HEIGHT;
    M5.Lcd.fillRect(0, y, SCREEN_WIDTH, FOOTER_HEIGHT, COLOR_HEADER_BG);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_TEXT_DIM);

    const char* screenNames[] = {
        "Dashboard", "CPU", "Memory", "Storage", "Network", "System", "Services", "Power", "QR", "Settings", "Register"
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
    // value領域だけクリアして上書き（文字重なり防止）
    int valueX = x + 110;
    M5.Lcd.fillRect(valueX, y, SCREEN_WIDTH - valueX - 10, 8, COLOR_BG);
    M5.Lcd.setTextColor(valueColor);
    M5.Lcd.setCursor(valueX, y);
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
    M5.Lcd.fillRect(10, y, 96, 8, COLOR_BG);
    M5.Lcd.setCursor(10, y);
    snprintf(buf, sizeof(buf), "CPU  %.1f%%", cpu.usage);
    M5.Lcd.print(buf);
    drawProgressBar(110, y - 2, 190, 14, cpu.usage, getUsageColor(cpu.usage));
    y += 24;

    // RAM
    float ramPercent = mem.ramTotal > 0 ? (float)mem.ramUsed / mem.ramTotal * 100.0f : 0;
    M5.Lcd.fillRect(10, y, 96, 8, COLOR_BG);
    M5.Lcd.setCursor(10, y);
    snprintf(buf, sizeof(buf), "RAM  %.1f%%", ramPercent);
    M5.Lcd.print(buf);
    drawProgressBar(110, y - 2, 190, 14, ramPercent, getUsageColor(ramPercent));
    y += 24;

    // Storage
    float stPercent = storage.total > 0 ? (float)storage.used / storage.total * 100.0f : 0;
    M5.Lcd.fillRect(10, y, 96, 8, COLOR_BG);
    M5.Lcd.setCursor(10, y);
    snprintf(buf, sizeof(buf), "Disk %.1f%%", stPercent);
    M5.Lcd.print(buf);
    drawProgressBar(110, y - 2, 190, 14, stPercent, getUsageColor(stPercent));
    y += 30;

    // Temp（ラベルは固定、値だけクリア）
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(10, y);
    M5.Lcd.setTextColor(COLOR_TEXT_DIM);
    M5.Lcd.print("Temp:");
    M5.Lcd.fillRect(60, y, 80, 8, COLOR_BG);
    M5.Lcd.setCursor(60, y);
    M5.Lcd.setTextColor(getUsageColor(cpu.temp / 85.0f * 100.0f));
    snprintf(buf, sizeof(buf), "%.1f C", cpu.temp);
    M5.Lcd.print(buf);

    // IP（ラベルは固定、値だけクリア）
    auto net = ble.getNetworkInfo();
    M5.Lcd.setCursor(150, y);
    M5.Lcd.setTextColor(COLOR_TEXT_DIM);
    M5.Lcd.print("IP:");
    M5.Lcd.fillRect(175, y, SCREEN_WIDTH - 185, 8, COLOR_BG);
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

// === QR Code Helper ===
void UI::drawSingleQr(int x, int y, const char* text, const char* label) {
    QRCode qrcode;
    uint8_t qrcodeData[256];
    qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, text);

    int moduleSize = 3;
    int qrPixels = qrcode.size * moduleSize;
    int padding = 4;
    int totalSize = qrPixels + padding * 2;

    // 白背景（QRコードの quiet zone）
    M5.Lcd.fillRect(x, y, totalSize, totalSize, TFT_WHITE);

    // 黒モジュールを描画
    for (uint8_t my = 0; my < qrcode.size; my++) {
        for (uint8_t mx = 0; mx < qrcode.size; mx++) {
            if (qrcode_getModule(&qrcode, mx, my)) {
                M5.Lcd.fillRect(x + padding + mx * moduleSize,
                                y + padding + my * moduleSize,
                                moduleSize, moduleSize, TFT_BLACK);
            }
        }
    }

    // ラベル（中央揃え）
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_ACCENT);
    int labelW = strlen(label) * 6;  // textSize=1 は1文字6px幅
    M5.Lcd.setCursor(x + (totalSize - labelW) / 2, y + totalSize + 4);
    M5.Lcd.print(label);
}

// === QR Code Screen ===
void UI::drawQrCodeScreen(BLEMonitorClient& ble) {
    drawScreenTitle("QR Code");

    auto net = ble.getNetworkInfo();
    if (net.ip.length() == 0 || net.ip == "0.0.0.0") {
        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(COLOR_TEXT_DIM);
        M5.Lcd.setCursor(60, 100);
        M5.Lcd.print("IP address not available");
        return;
    }

    char url5000[64], url8080[64];
    snprintf(url5000, sizeof(url5000), "http://%s:5000", net.ip.c_str());
    snprintf(url8080, sizeof(url8080), "http://%s:8080", net.ip.c_str());

    // QRコード version 3 (29modules) * 3px + padding 4*2 = 95px
    int qrTotalSize = 95;
    int gap = 30;
    int totalW = qrTotalSize * 2 + gap;
    int startX = (SCREEN_WIDTH - totalW) / 2;
    int qrY = HEADER_HEIGHT + 36;

    drawSingleQr(startX, qrY, url5000, "Monitor :5000");
    drawSingleQr(startX + qrTotalSize + gap, qrY, url8080, "Dashboard :8080");

    // URL表示
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_TEXT_DIM);
    int urlY = qrY + qrTotalSize + 18;
    int urlW = strlen(url5000) * 6;
    M5.Lcd.setCursor((SCREEN_WIDTH - urlW) / 2, urlY);
    M5.Lcd.printf("IP: %s", net.ip.c_str());
}

// === Settings Screen ===
void UI::drawSettings() {
    drawScreenTitle("Settings");
    int y = HEADER_HEIGHT + 50;

    // Sound toggle
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_TEXT);
    M5.Lcd.setCursor(10, y);
    M5.Lcd.print("Sound Alerts:");
    M5.Lcd.fillRect(130, y, 60, 8, COLOR_BG);
    M5.Lcd.setCursor(130, y);
    if (soundEnabled) {
        M5.Lcd.setTextColor(COLOR_GOOD);
        M5.Lcd.print("ON");
    } else {
        M5.Lcd.setTextColor(COLOR_BAD);
        M5.Lcd.print("OFF");
    }
    y += 24;

    M5.Lcd.setTextColor(COLOR_TEXT_DIM);
    M5.Lcd.setCursor(10, y);
    M5.Lcd.print("Press [Select] to toggle sound");
    y += 30;

    // Alert thresholds info
    M5.Lcd.setTextColor(COLOR_ACCENT);
    M5.Lcd.setCursor(10, y);
    M5.Lcd.print("--- Alert Thresholds ---");
    y += 16;

    char buf[32];
    snprintf(buf, sizeof(buf), ">= %.0f%%", ALERT_CPU_USAGE);
    drawKeyValue(10, y, "CPU Usage:", buf, COLOR_WARN);
    y += 16;
    snprintf(buf, sizeof(buf), ">= %.0f C", ALERT_CPU_TEMP);
    drawKeyValue(10, y, "CPU Temp:", buf, COLOR_WARN);
    y += 16;
    snprintf(buf, sizeof(buf), ">= %.0f%%", ALERT_RAM_USAGE);
    drawKeyValue(10, y, "RAM Usage:", buf, COLOR_WARN);
    y += 16;
    snprintf(buf, sizeof(buf), ">= %.0f%%", ALERT_STORAGE_USAGE);
    drawKeyValue(10, y, "Storage:", buf, COLOR_WARN);
}

// === Services Screen ===
void UI::drawServices(BLEMonitorClient& ble) {
    drawScreenTitle("Services");
    int y = HEADER_HEIGHT + 36;

    int count = ble.getServiceCount();
    if (count == 0) {
        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(COLOR_TEXT_DIM);
        M5.Lcd.setCursor(10, y);
        M5.Lcd.print("No services configured.");
        y += 16;
        M5.Lcd.setCursor(10, y);
        M5.Lcd.print("Edit /etc/rpi-monitor/config.json");
        return;
    }

    // サービスリスト
    M5.Lcd.setTextSize(1);
    int maxVisible = 7;  // 画面に収まる最大数
    for (int i = 0; i < count && i < maxVisible; i++) {
        auto svc = ble.getServiceInfo(i);

        if (i == svcSelectedIndex) {
            M5.Lcd.setTextColor(COLOR_BG);
            M5.Lcd.fillRect(8, y - 2, 304, 16, COLOR_ACCENT);
        } else {
            M5.Lcd.setTextColor(COLOR_TEXT);
        }
        M5.Lcd.setCursor(12, y);

        // ステータスアイコン + 名前
        const char* icon = svc.active ? "[*]" : "[ ]";
        uint16_t statusColor = svc.active ? COLOR_GOOD : COLOR_BAD;

        if (i != svcSelectedIndex) {
            M5.Lcd.setTextColor(statusColor);
        }
        M5.Lcd.print(icon);
        M5.Lcd.print(" ");
        if (i != svcSelectedIndex) {
            M5.Lcd.setTextColor(COLOR_TEXT);
        }
        M5.Lcd.print(svc.name.c_str());

        // 右端にステータス文字
        int statusX = 260;
        M5.Lcd.setCursor(statusX, y);
        if (i != svcSelectedIndex) {
            M5.Lcd.setTextColor(statusColor);
        }
        M5.Lcd.print(svc.active ? "active" : "inactive");

        y += 18;
    }

    y += 8;
    if (svcConfirmMode) {
        auto svc = ble.getServiceInfo(svcSelectedIndex);
        const char* action = svc.active ? "Stop" : "Start";
        M5.Lcd.setTextColor(COLOR_WARN);
        M5.Lcd.setCursor(10, y);
        M5.Lcd.printf("%s %s?", action, svc.name.c_str());
        y += 16;
        M5.Lcd.setCursor(10, y);
        M5.Lcd.print("[<Prev] Cancel  [Select] OK");
    } else {
        M5.Lcd.setTextColor(COLOR_TEXT_DIM);
        M5.Lcd.setCursor(10, y);
        M5.Lcd.print("[<] Up/Prev  [Sel] Toggle  [>] Down/Next");
    }
}

// === Power Menu Screen ===
void UI::drawPowerMenu(BLEMonitorClient& ble) {
    drawScreenTitle("Power");
    int y = HEADER_HEIGHT + 50;

    const char* items[] = {"Reboot", "Shutdown"};
    const uint16_t icons[] = {COLOR_WARN, COLOR_BAD};

    M5.Lcd.setTextSize(2);
    for (int i = 0; i < 2; i++) {
        if (i == pwrSelectedIndex) {
            M5.Lcd.fillRect(8, y - 4, 304, 28, COLOR_ACCENT);
            M5.Lcd.setTextColor(COLOR_BG);
        } else {
            M5.Lcd.setTextColor(icons[i]);
        }
        M5.Lcd.setCursor(20, y);
        M5.Lcd.print(items[i]);
        y += 40;
    }

    y += 10;
    M5.Lcd.setTextSize(1);
    if (pwrConfirmMode) {
        M5.Lcd.setTextColor(COLOR_BAD);
        M5.Lcd.setCursor(10, y);
        M5.Lcd.printf("%s RPi?", items[pwrSelectedIndex]);
        y += 16;
        M5.Lcd.setTextColor(COLOR_WARN);
        M5.Lcd.setCursor(10, y);
        M5.Lcd.print("[<Prev] Cancel  [Select] Execute");
    } else {
        M5.Lcd.setTextColor(COLOR_TEXT_DIM);
        M5.Lcd.setCursor(10, y);
        M5.Lcd.print("[<] Up/Prev  [Sel] Execute  [>] Down/Next");
    }
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
