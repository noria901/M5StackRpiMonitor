#include "app_rpi_monitor.h"
#include <esp_log.h>
#include <nvs.h>
#include <cmath>

using namespace MOONCAKE::USER_APP;

// ============================================================
// M5Dial RPi Monitor - Input Mapping
// ============================================================
//
//  Input               Action
//  ------------------  ----------------------------------------
//  Encoder rotate      Always: switch screens (CW=next, CCW=prev)
//  Button press        Always: exit app (back to launcher)
//  Touch center        Confirm / action (screen-dependent)
//  Touch upper half    List screens: scroll up
//  Touch lower half    List screens: scroll down
//
// Screen types:
//  - Display screens (Dashboard..System, QR Code):
//      touch center = force data refresh
//  - List screens (Services, Power, Commands):
//      touch center = select/confirm, touch top/bottom = scroll
//  - Settings:
//      touch center = toggle sound
// ============================================================

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
    _loadSettings();

    _currentScreen = Screen::DASHBOARD;
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

    // Check for deferred auto-connect from GAP callback
    int pendingIdx = _ble.consumePendingAutoConnect();
    if (pendingIdx >= 0) {
        ESP_LOGI(_tag, "Executing deferred auto-connect to device %d", pendingIdx);
        if (_ble.connectToDevice(pendingIdx)) {
            _currentScreen = Screen::DASHBOARD;
            _needsRedraw = true;
        }
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
            case Screen::SERVICES:
                _gui.drawServices(_ble, _svcSelectedIndex, _svcConfirmMode);
                break;
            case Screen::POWER_MENU:
                _gui.drawPowerMenu(_pwrSelectedIndex, _pwrConfirmMode);
                break;
            case Screen::COMMANDS:
                _gui.drawCommands(_ble, _cmdSelectedIndex, _cmdConfirmMode);
                break;
            case Screen::QR_CODE:
                _gui.drawQrCode(_ble);
                break;
            case Screen::SETTINGS:
                _gui.drawSettings(_soundEnabled);
                break;
            default:
                break;
        }

        // Page indicator dots
        _gui.drawPageIndicator(screenIdx, SCREEN_COUNT);

        // Disconnected overlay on data/action screens
        // (skip Settings, QR Code - they work without connection)
        if (_ble.getState() != BleState::CONNECTED &&
            _currentScreen != Screen::SETTINGS &&
            _currentScreen != Screen::QR_CODE)
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

// ============================================================
// Input Handlers
// ============================================================

// Encoder rotation: ALWAYS switch screens
void RpiMonitor::_handleEncoder()
{
    if (!_data.hal->encoder.wasMoved(true)) return;

    unsigned long now = (unsigned long)(esp_timer_get_time() / 1000);
    if (now - _lastEncoderMove < ENCODER_DEBOUNCE_MS) return;
    _lastEncoderMove = now;

    int dir = _data.hal->encoder.getDirection();
    // dir < 1 = CW (next), dir >= 1 = CCW (prev)

    if (dir < 1) _nextScreen();
    else         _prevScreen();

    _data.hal->buzz.tone(4000, 20);
    _needsRedraw = true;
}

// Button press: ALWAYS exit app (back to launcher)
void RpiMonitor::_handleButton()
{
    if (!_data.hal->encoder.btn.pressed()) return;

    _data.hal->buzz.tone(4000, 20);
    destroyApp();
}

// Touch: center = action, upper/lower = scroll on list screens
void RpiMonitor::_handleTouch()
{
    if (!_data.hal->tp.isTouched()) return;

    unsigned long now = (unsigned long)(esp_timer_get_time() / 1000);
    if (now - _lastTouchAction < TOUCH_DEBOUNCE_MS) return;

    _data.hal->tp.update();
    auto point = _data.hal->tp.getTouchPointBuffer();

    // Calculate distance and angle from center (120, 120)
    int dx = point.x - 120;
    int dy = point.y - 120;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < 45) {
        // --- Center tap: confirm / action ---
        _lastTouchAction = now;
        _touchAction();
        _data.hal->buzz.tone(4000, 20);
        _needsRedraw = true;
    } else if (dist < 110 && _isListScreen()) {
        // --- Outer ring on list screens: scroll up/down ---
        _lastTouchAction = now;
        if (dy < 0) {
            _touchScrollUp();   // upper half
        } else {
            _touchScrollDown(); // lower half
        }
        _data.hal->buzz.tone(4000, 20);
        _needsRedraw = true;
    }
}

// ============================================================
// Screen Navigation
// ============================================================

void RpiMonitor::_nextScreen()
{
    int next = (static_cast<int>(_currentScreen) + 1) % SCREEN_COUNT;
    _currentScreen = static_cast<Screen>(next);
    _resetConfirmModes();
}

void RpiMonitor::_prevScreen()
{
    int prev = (static_cast<int>(_currentScreen) - 1 + SCREEN_COUNT) % SCREEN_COUNT;
    _currentScreen = static_cast<Screen>(prev);
    _resetConfirmModes();
}

void RpiMonitor::_resetConfirmModes()
{
    _svcConfirmMode = false;
    _pwrConfirmMode = false;
    _cmdConfirmMode = false;
}

bool RpiMonitor::_isListScreen() const
{
    return _currentScreen == Screen::SERVICES ||
           _currentScreen == Screen::POWER_MENU ||
           _currentScreen == Screen::COMMANDS;
}

// ============================================================
// Touch Actions
// ============================================================

// Touch center: screen-specific action
void RpiMonitor::_touchAction()
{
    switch (_currentScreen) {
        case Screen::SERVICES:
            _servicesAction();
            break;
        case Screen::POWER_MENU:
            _powerAction();
            break;
        case Screen::COMMANDS:
            _commandsAction();
            break;
        case Screen::SETTINGS:
            _settingsAction();
            break;
        default:
            // Display screens: force data refresh
            if (_ble.getState() == BleState::CONNECTED) {
                _ble.readAll();
                _lastDataUpdate = (unsigned long)(esp_timer_get_time() / 1000);
            }
            break;
    }
}

// Touch upper half on list screens: scroll up / cancel confirm
void RpiMonitor::_touchScrollUp()
{
    switch (_currentScreen) {
        case Screen::SERVICES:
            if (_svcConfirmMode) _svcConfirmMode = false;
            else if (_svcSelectedIndex > 0) _svcSelectedIndex--;
            break;
        case Screen::POWER_MENU:
            if (_pwrConfirmMode) _pwrConfirmMode = false;
            else if (_pwrSelectedIndex > 0) _pwrSelectedIndex--;
            break;
        case Screen::COMMANDS:
            if (_cmdConfirmMode) _cmdConfirmMode = false;
            else if (_cmdSelectedIndex > 0) _cmdSelectedIndex--;
            break;
        default:
            break;
    }
}

// Touch lower half on list screens: scroll down
void RpiMonitor::_touchScrollDown()
{
    switch (_currentScreen) {
        case Screen::SERVICES:
            if (!_svcConfirmMode && _svcSelectedIndex < _ble.getServiceCount() - 1)
                _svcSelectedIndex++;
            break;
        case Screen::POWER_MENU:
            if (!_pwrConfirmMode && _pwrSelectedIndex < 1)
                _pwrSelectedIndex++;
            break;
        case Screen::COMMANDS:
            if (!_cmdConfirmMode && _cmdSelectedIndex < _ble.getCommandCount() - 1)
                _cmdSelectedIndex++;
            break;
        default:
            break;
    }
}

// ============================================================
// Screen-Specific Actions (called by touch center)
// ============================================================

void RpiMonitor::_servicesAction()
{
    if (_ble.getState() != BleState::CONNECTED) return;
    if (_ble.getServiceCount() == 0) return;

    if (_svcConfirmMode) {
        auto& svc = _ble.getServiceInfo(_svcSelectedIndex);
        const char* action = svc.active ? "stop" : "start";
        _ble.sendServiceControl(svc.name.c_str(), action);
        _svcConfirmMode = false;
        vTaskDelay(pdMS_TO_TICKS(500));
        _ble.readAll();
        _lastDataUpdate = (unsigned long)(esp_timer_get_time() / 1000);
    } else {
        _svcConfirmMode = true;
    }
    _needsRedraw = true;
}

void RpiMonitor::_powerAction()
{
    if (_ble.getState() != BleState::CONNECTED) return;

    if (_pwrConfirmMode) {
        const char* action = (_pwrSelectedIndex == 0) ? "reboot" : "shutdown";
        _ble.sendPowerCommand(action);
        _pwrConfirmMode = false;
    } else {
        _pwrConfirmMode = true;
    }
    _needsRedraw = true;
}

void RpiMonitor::_commandsAction()
{
    if (_ble.getState() != BleState::CONNECTED) return;
    if (_ble.getCommandCount() == 0) return;

    if (_cmdConfirmMode) {
        auto& cmd = _ble.getCommandInfo(_cmdSelectedIndex);
        const char* action = (cmd.state == "running") ? "stop" : "run";
        _ble.sendCommand(cmd.name.c_str(), action);
        _cmdConfirmMode = false;
        vTaskDelay(pdMS_TO_TICKS(300));
        _ble.readAll();
        _lastDataUpdate = (unsigned long)(esp_timer_get_time() / 1000);
    } else {
        _cmdConfirmMode = true;
    }
    _needsRedraw = true;
}

void RpiMonitor::_settingsAction()
{
    _soundEnabled = !_soundEnabled;
    _saveSettings();
    if (_soundEnabled) {
        _data.hal->buzz.tone(1000, 150);
    }
    _needsRedraw = true;
}

// ============================================================
// NVS Settings
// ============================================================

void RpiMonitor::_loadSettings()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ui", NVS_READONLY, &nvs);
    if (err != ESP_OK) return;

    uint8_t val = 1;
    nvs_get_u8(nvs, "sound", &val);
    _soundEnabled = (val != 0);
    nvs_close(nvs);
}

void RpiMonitor::_saveSettings()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ui", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return;

    nvs_set_u8(nvs, "sound", _soundEnabled ? 1 : 0);
    nvs_commit(nvs);
    nvs_close(nvs);
}
