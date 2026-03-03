#pragma once

#include <M5Stack.h>
#include <Preferences.h>
#include "ble_client.h"
#include "config.h"

enum class Screen {
    DASHBOARD = 0,
    CPU_DETAIL,
    MEMORY_DETAIL,
    STORAGE_DETAIL,
    NETWORK,
    SYSTEM_INFO,
    SERVICES,
    POWER_MENU,
    COMMANDS,
    QR_CODE,
    SETTINGS,
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
    void registrationBtnA();
    void registrationBtnC(int deviceCount);
    void servicesBtnA();
    void servicesBtnC(int serviceCount);
    void powerBtnA();
    void powerBtnC();
    void commandsBtnA();
    void commandsBtnC(int commandCount);
    void setNeedsRedraw();
    Screen getCurrentScreen();

    // サウンド通知
    void playTone(int freq, int duration);
    void onBleConnected();
    void onBleDisconnected();
    void checkAlerts(BLEMonitorClient& ble);

    bool isSoundEnabled() { return soundEnabled; }

private:
    Screen currentScreen = Screen::DASHBOARD;
    bool needsFullRedraw = true;
    bool lastConnected = false;
    unsigned long lastUpdate = 0;

    // Registration screen state
    int regSelectedDevice = 0;
    bool regConfirmMode = false;

    // Services screen state
    int svcSelectedIndex = 0;
    bool svcConfirmMode = false;

    // Power menu state
    int pwrSelectedIndex = 0;  // 0=Reboot, 1=Shutdown
    bool pwrConfirmMode = false;

    // Commands screen state
    int cmdSelectedIndex = 0;
    bool cmdConfirmMode = false;

    // Settings
    bool soundEnabled = true;
    int settingsSelection = 0;
    Preferences uiPrefs;

    // Alert cooldowns
    unsigned long lastAlertCpu = 0;
    unsigned long lastAlertTemp = 0;
    unsigned long lastAlertRam = 0;
    unsigned long lastAlertStorage = 0;

    void loadSettings();
    void saveSettings();

    void drawHeader(BLEMonitorClient& ble);
    void drawFooter();
    void drawDashboard(BLEMonitorClient& ble);
    void drawCpuDetail(BLEMonitorClient& ble);
    void drawMemoryDetail(BLEMonitorClient& ble);
    void drawStorageDetail(BLEMonitorClient& ble);
    void drawNetwork(BLEMonitorClient& ble);
    void drawSystemInfo(BLEMonitorClient& ble);
    void drawRegistration(BLEMonitorClient& ble);
    void drawServices(BLEMonitorClient& ble);
    void drawPowerMenu(BLEMonitorClient& ble);
    void drawCommands(BLEMonitorClient& ble);
    void drawQrCodeScreen(BLEMonitorClient& ble);
    void drawSettings();
    void drawDisconnected(BLEMonitorClient& ble);

    // Drawing helpers
    void drawProgressBar(int x, int y, int w, int h, float percent, uint16_t color);
    void drawKeyValue(int x, int y, const char* key, const char* value, uint16_t valueColor = COLOR_TEXT);
    void drawScreenTitle(const char* title);
    void drawSingleQr(int x, int y, const char* text, const char* label);
    uint16_t getUsageColor(float percent);
};
