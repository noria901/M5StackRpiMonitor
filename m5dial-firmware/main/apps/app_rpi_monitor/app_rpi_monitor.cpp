#include "app_rpi_monitor.h"
#include <esp_log.h>
#include <cmath>

using namespace MOONCAKE::USER_APP;

void RpiMonitor::onSetup()
{
    setAppName("RPi Monitor");
    setAllowBgRunning(false);

    _data.hal = (HAL::HAL*)getUserData();
}

void RpiMonitor::onCreate()
{
    if (!_data.hal) {
        ESP_LOGE(_tag, "HAL is null");
        destroyApp();
        return;
    }

    _ble.init();
    _gui.init(_data.hal);

    // If a saved server exists, start on Dashboard for auto-reconnect
    if (_ble.hasSavedServer()) {
        _currentScreen = Screen::DASHBOARD;
    } else {
        _currentScreen = Screen::REGISTRATION;
    }

    _needsRedraw = true;
    ESP_LOGI(_tag, "RpiMonitor created");
}

void RpiMonitor::onRunning()
{
    if (!_data.hal) return;

    unsigned long now = (unsigned long)(esp_timer_get_time() / 1000);  // ms

    // Handle input
    _handleEncoder();
    _handleButton();
    _handleTouch();

    // Check BLE scan completion
    if (_ble.getState() == BleState::SCANNING && _ble.isScanComplete()) {
        _needsRedraw = true;
    }

    // Auto-reconnect: if we have a saved server but not connected, try periodically
    if (_ble.hasSavedServer() &&
        _ble.getState() == BleState::DISCONNECTED &&
        (now - _lastReconnectAttempt >= RECONNECT_MS))
    {
        _lastReconnectAttempt = now;
        _ble.scanAndAutoConnect();
    }

    // Periodic data update when connected
    if (_ble.getState() == BleState::CONNECTED &&
        (now - _lastDataUpdate >= DATA_UPDATE_MS))
    {
        _ble.readAll();
        _lastDataUpdate = now;
        _needsRedraw = true;
    }

    // Draw current screen
    if (_needsRedraw) {
        auto* canvas = _data.hal->canvas;
        canvas->fillSprite(0);  // clear

        int screenIdx = static_cast<int>(_currentScreen);

        switch (_currentScreen) {
            case Screen::DASHBOARD:
                _gui.drawDashboard(_ble);
                break;
            case Screen::CPU_DETAIL:
                _gui.drawCpuDetail(_ble);
                break;
            case Screen::MEMORY_DETAIL:
                _gui.drawMemoryDetail(_ble);
                break;
            case Screen::STORAGE_DETAIL:
                _gui.drawStorageDetail(_ble);
                break;
            case Screen::NETWORK:
                _gui.drawNetwork(_ble);
                break;
            case Screen::SYSTEM_INFO:
                _gui.drawSystemInfo(_ble);
                break;
            case Screen::REGISTRATION:
                _gui.drawRegistration(_ble, _regSelectedDevice, _regConfirmMode);
                break;
        }

        // Draw page indicator dots (skip for Registration)
        if (_currentScreen != Screen::REGISTRATION) {
            _gui.drawPageIndicator(screenIdx, SCREEN_COUNT - 1);  // -1 to exclude Registration
        }

        // Show disconnected overlay if not connected and not on Registration screen
        if (_ble.getState() != BleState::CONNECTED &&
            _currentScreen != Screen::REGISTRATION)
        {
            _gui.drawDisconnectedOverlay(_ble.hasSavedServer(),
                                          _ble.getSavedServerName().c_str());
        }

        canvas->pushSprite(0, 0);
        _needsRedraw = false;
    }
}

void RpiMonitor::onDestroy()
{
    _ble.disconnect();
    ESP_LOGI(_tag, "RpiMonitor destroyed");
}

// --- Input Handlers ---

void RpiMonitor::_handleEncoder()
{
    if (!_data.hal->encoder.wasMoved(true)) return;

    unsigned long now = (unsigned long)(esp_timer_get_time() / 1000);
    if (now - _lastEncoderMove < ENCODER_DEBOUNCE_MS) return;
    _lastEncoderMove = now;

    int dir = _data.hal->encoder.getDirection();
    // dir < 1 = CW (next), dir >= 1 = CCW (prev)

    if (_currentScreen == Screen::REGISTRATION) {
        // In Registration: encoder scrolls device list
        if (dir < 1) {
            _registrationScrollDown(_ble.getFoundDeviceCount());
        } else {
            _registrationScrollUp();
        }
    } else {
        // Normal: encoder switches screens
        if (dir < 1) {
            _nextScreen();
        } else {
            _prevScreen();
        }
    }

    // Buzzer feedback
    _data.hal->buzz(4000, 20);

    _needsRedraw = true;
}

void RpiMonitor::_handleButton()
{
    if (!_data.hal->encoder.btn.pressed()) return;

    unsigned long now = (unsigned long)(esp_timer_get_time() / 1000);
    if (now - _lastButtonPress < BUTTON_DEBOUNCE_MS) return;
    _lastButtonPress = now;

    if (_currentScreen == Screen::REGISTRATION) {
        _registrationAction();
    } else if (_ble.getState() == BleState::CONNECTED) {
        // Connected: button forces data refresh
        _ble.readAll();
        _lastDataUpdate = (unsigned long)(esp_timer_get_time() / 1000);
    } else if (!_ble.hasSavedServer()) {
        // Not connected, no saved server: go back to launcher
        destroyApp();
        return;
    }

    _data.hal->buzz(4000, 20);
    _needsRedraw = true;
}

void RpiMonitor::_handleTouch()
{
    if (!_data.hal->tp.isTouched()) return;

    _data.hal->tp.update();
    auto point = _data.hal->tp.getTouchPointBuffer();

    // Calculate distance from center (120, 120)
    int dx = point.x - 120;
    int dy = point.y - 120;
    float dist = sqrtf(dx * dx + dy * dy);

    // Center tap (r < 50) acts like button press
    if (dist < 50) {
        unsigned long now = (unsigned long)(esp_timer_get_time() / 1000);
        if (now - _lastButtonPress < BUTTON_DEBOUNCE_MS) return;
        _lastButtonPress = now;

        if (_currentScreen == Screen::REGISTRATION) {
            _registrationAction();
        } else if (_ble.getState() == BleState::CONNECTED) {
            _ble.readAll();
            _lastDataUpdate = now;
        }
        _data.hal->buzz(4000, 20);
        _needsRedraw = true;
    }
}

// --- Screen Navigation ---

void RpiMonitor::_nextScreen()
{
    int next = (static_cast<int>(_currentScreen) + 1) % SCREEN_COUNT;
    _currentScreen = static_cast<Screen>(next);
    _regConfirmMode = false;
}

void RpiMonitor::_prevScreen()
{
    int prev = (static_cast<int>(_currentScreen) - 1 + SCREEN_COUNT) % SCREEN_COUNT;
    _currentScreen = static_cast<Screen>(prev);
    _regConfirmMode = false;
}

// --- Registration Actions ---

void RpiMonitor::_registrationAction()
{
    if (_ble.getState() == BleState::CONNECTED) {
        // Already connected: forget device (disconnect + clear NVS)
        _ble.forgetDevice();
        _needsRedraw = true;
        return;
    }

    if (_ble.getState() == BleState::SCANNING) {
        return;  // scan in progress, ignore
    }

    int count = _ble.getFoundDeviceCount();
    if (count == 0) {
        // No devices found: start scan
        _ble.startScan();
        _needsRedraw = true;
        return;
    }

    if (_regConfirmMode) {
        // Confirm mode: connect to selected device
        if (_regSelectedDevice >= 0 && _regSelectedDevice < count) {
            _ble.connectToDevice(_regSelectedDevice);
            if (_ble.getState() == BleState::CONNECTED) {
                _ble.sendRegistration("M5Dial-RpiMon");
                _currentScreen = Screen::DASHBOARD;
            }
        }
        _regConfirmMode = false;
    } else {
        // Enter confirm mode
        _regConfirmMode = true;
    }
    _needsRedraw = true;
}

void RpiMonitor::_registrationScrollUp()
{
    if (_regConfirmMode) {
        _regConfirmMode = false;  // cancel confirm
    } else if (_regSelectedDevice > 0) {
        _regSelectedDevice--;
    }
}

void RpiMonitor::_registrationScrollDown(int deviceCount)
{
    if (!_regConfirmMode && _regSelectedDevice < deviceCount - 1) {
        _regSelectedDevice++;
    }
}
