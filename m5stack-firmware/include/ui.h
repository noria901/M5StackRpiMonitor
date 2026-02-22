#pragma once

#include <M5Stack.h>
#include "ble_client.h"
#include "config.h"

enum class Screen {
    DASHBOARD = 0,
    CPU_DETAIL,
    MEMORY_DETAIL,
    STORAGE_DETAIL,
    NETWORK,
    SYSTEM_INFO,
    REGISTRATION,
    SCREEN_COUNT
};

class UI {
public:
    void init();
    void update(BLEMonitorClient& ble);
    void nextScreen();
    void prevScreen();
    void buttonAction(BLEMonitorClient& ble);
    void setNeedsRedraw();
    Screen getCurrentScreen();

private:
    Screen currentScreen = Screen::DASHBOARD;
    bool needsFullRedraw = true;
    unsigned long lastUpdate = 0;

    // Registration screen state
    int regSelectedDevice = 0;
    bool regConfirmMode = false;

    void drawHeader(BLEMonitorClient& ble);
    void drawFooter();
    void drawDashboard(BLEMonitorClient& ble);
    void drawCpuDetail(BLEMonitorClient& ble);
    void drawMemoryDetail(BLEMonitorClient& ble);
    void drawStorageDetail(BLEMonitorClient& ble);
    void drawNetwork(BLEMonitorClient& ble);
    void drawSystemInfo(BLEMonitorClient& ble);
    void drawRegistration(BLEMonitorClient& ble);
    void drawDisconnected();

    // Drawing helpers
    void drawProgressBar(int x, int y, int w, int h, float percent, uint16_t color);
    void drawKeyValue(int x, int y, const char* key, const char* value, uint16_t valueColor = COLOR_TEXT);
    void drawScreenTitle(const char* title);
    uint16_t getUsageColor(float percent);
};
