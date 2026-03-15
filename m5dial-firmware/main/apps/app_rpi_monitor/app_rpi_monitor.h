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

    // Screen navigation (Registration removed - handled by BLE Scanner app)
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
        ROS2,
        QR_CODE,
        SETTINGS,
        SCREEN_COUNT
    };
    Screen _currentScreen = Screen::DASHBOARD;
    static constexpr int SCREEN_COUNT = static_cast<int>(Screen::SCREEN_COUNT);

    // Services screen state
    int _svcSelectedIndex = 0;
    bool _svcConfirmMode = false;

    // Power menu state
    int _pwrSelectedIndex = 0;
    bool _pwrConfirmMode = false;

    // Commands screen state
    int _cmdSelectedIndex = 0;
    bool _cmdConfirmMode = false;

    // ROS2 screen state
    int _ros2Tab = 0;           // 0=Nodes, 1=Topics
    int _ros2ScrollOffset = 0;

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
    unsigned long _lastTouchAction = 0;
    static constexpr unsigned long TOUCH_DEBOUNCE_MS = 300;

    bool _needsRedraw = true;

    // Input handlers
    void _handleEncoder();
    void _handleButton();
    void _handleTouch();

    // Screen navigation
    void _nextScreen();
    void _prevScreen();
    void _resetConfirmModes();

    // Touch actions (center tap = confirm/action)
    void _touchAction();
    void _touchScrollUp();
    void _touchScrollDown();

    // Screen-specific actions (called by touch center)
    void _servicesAction();
    void _powerAction();
    void _commandsAction();
    void _settingsAction();

    // NVS settings
    void _loadSettings();
    void _saveSettings();

    // Helper: is current screen a list screen?
    bool _isListScreen() const;
};

}  // namespace USER_APP
}  // namespace MOONCAKE
