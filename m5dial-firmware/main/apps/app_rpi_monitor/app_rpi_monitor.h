#pragma once

#include "../../hal/hal.h"
#include "../app.h"
#include "rpi_ble_client.h"
#include "rpi_monitor_gui.h"

namespace MOONCAKE {
namespace USER_APP {

class RpiMonitor : public APP_BASE {
public:
    void onSetup() override;
    void onCreate() override;
    void onRunning() override;
    void onDestroy() override;

private:
    static constexpr const char* _tag = "RpiMon";

    struct Data_t {
        HAL::HAL* hal = nullptr;
    };
    Data_t _data;

    RpiBleClient _ble;
    RpiMonitorGui _gui;

    // Screen navigation
    enum class Screen : int {
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
    Screen _currentScreen = Screen::DASHBOARD;
    static constexpr int SCREEN_COUNT = static_cast<int>(Screen::SCREEN_COUNT);

    // Registration screen state
    int _regSelectedDevice = 0;
    bool _regConfirmMode = false;

    // Services screen state
    int _svcSelectedIndex = 0;
    bool _svcConfirmMode = false;

    // Power menu state
    int _pwrSelectedIndex = 0;
    bool _pwrConfirmMode = false;

    // Commands screen state
    int _cmdSelectedIndex = 0;
    bool _cmdConfirmMode = false;

    // Settings state
    bool _soundEnabled = true;

    // Update timing
    unsigned long _lastDataUpdate = 0;
    static constexpr unsigned long DATA_UPDATE_MS = 2000;

    // Auto-reconnect timing
    unsigned long _lastReconnectAttempt = 0;
    static constexpr unsigned long RECONNECT_MS = 3000;

    // Input debounce
    unsigned long _lastEncoderMove = 0;
    static constexpr unsigned long ENCODER_DEBOUNCE_MS = 150;
    unsigned long _lastButtonPress = 0;
    static constexpr unsigned long BUTTON_DEBOUNCE_MS = 250;

    bool _needsRedraw = true;

    // Input handlers
    void _handleEncoder();
    void _handleButton();
    void _handleTouch();

    // Screen navigation
    void _nextScreen();
    void _prevScreen();

    // Registration actions
    void _registrationAction();
    void _registrationScrollUp();
    void _registrationScrollDown(int deviceCount);

    // List screen helpers
    void _listScrollUp(int& selectedIndex, bool& confirmMode);
    void _listScrollDown(int& selectedIndex, bool& confirmMode, int count);

    // Screen-specific actions
    void _servicesAction();
    void _powerAction();
    void _commandsAction();
    void _settingsAction();

    // NVS settings
    void _loadSettings();
    void _saveSettings();
};

}  // namespace USER_APP
}  // namespace MOONCAKE
