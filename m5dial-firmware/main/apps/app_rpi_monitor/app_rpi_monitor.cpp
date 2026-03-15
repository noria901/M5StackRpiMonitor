#include "app_rpi_monitor.h"
#include <esp_log.h>
#include <nvs.h>
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
    _loadSettings();

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
            case Screen::REGISTRATION:
                _gui.drawRegistration(_ble, _regSelectedDevice, _regConfirmMode);
                break;
            default:
                break;
        }

        // Draw page indicator dots (skip for Registration)
        if (_currentScreen != Screen::REGISTRATION) {
            _gui.drawPageIndicator(screenIdx, SCREEN_COUNT - 1);  // -1 to exclude Registration
        }

        // Show disconnected overlay on data screens only
        // (skip Registration, Settings, QR Code - they work without connection)
        if (_ble.getState() != BleState::CONNECTED &&
            _currentScreen != Screen::REGISTRATION &&
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

// --- Input Handlers ---

void RpiMonitor::_handleEncoder()
{
    if (!_data.hal->encoder.wasMoved(true)) return;

    unsigned long now = (unsigned long)(esp_timer_get_time() / 1000);
    if (now - _lastEncoderMove < ENCODER_DEBOUNCE_MS) return;
    _lastEncoderMove = now;

    int dir = _data.hal->encoder.getDirection();
    // dir < 1 = CW (next), dir >= 1 = CCW (prev)

    // List screens: encoder scrolls items within the list
    // All other screens: encoder switches between screens
    switch (_currentScreen) {
        case Screen::REGISTRATION:
            if (dir < 1) _registrationScrollDown(_ble.getFoundDeviceCount());
            else         _registrationScrollUp();
            break;
        case Screen::SERVICES:
            if (dir < 1) _listScrollDown(_svcSelectedIndex, _svcConfirmMode, _ble.getServiceCount());
            else         _listScrollUp(_svcSelectedIndex, _svcConfirmMode);
            break;
        case Screen::POWER_MENU:
            if (dir < 1) _listScrollDown(_pwrSelectedIndex, _pwrConfirmMode, 2);
            else         _listScrollUp(_pwrSelectedIndex, _pwrConfirmMode);
            break;
        case Screen::COMMANDS:
            if (dir < 1) _listScrollDown(_cmdSelectedIndex, _cmdConfirmMode, _ble.getCommandCount());
            else         _listScrollUp(_cmdSelectedIndex, _cmdConfirmMode);
            break;
        default:
            // Display / settings screens: encoder navigates between screens
            if (dir < 1) _nextScreen();
            else         _prevScreen();
            break;
    }

    // Buzzer feedback
    _data.hal->buzz.tone(4000, 20);

    _needsRedraw = true;
}

void RpiMonitor::_handleButton()
{
    if (!_data.hal->encoder.btn.pressed()) return;

    unsigned long now = (unsigned long)(esp_timer_get_time() / 1000);
    if (now - _lastButtonPress < BUTTON_DEBOUNCE_MS) return;
    _lastButtonPress = now;

    switch (_currentScreen) {
        case Screen::REGISTRATION:
            _registrationAction();
            break;
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
            // Display screens: button forces data refresh or exits app
            if (_ble.getState() == BleState::CONNECTED) {
                _ble.readAll();
                _lastDataUpdate = (unsigned long)(esp_timer_get_time() / 1000);
            } else if (!_ble.hasSavedServer()) {
                destroyApp();
                return;
            }
            break;
    }

    _data.hal->buzz.tone(4000, 20);
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

        // Center tap = same as button press (reuse _handleButton logic)
        switch (_currentScreen) {
            case Screen::REGISTRATION:  _registrationAction(); break;
            case Screen::SERVICES:      _servicesAction(); break;
            case Screen::POWER_MENU:    _powerAction(); break;
            case Screen::COMMANDS:      _commandsAction(); break;
            case Screen::SETTINGS:      _settingsAction(); break;
            default:
                if (_ble.getState() == BleState::CONNECTED) {
                    _ble.readAll();
                    _lastDataUpdate = now;
                }
                break;
        }
        _data.hal->buzz.tone(4000, 20);
        _needsRedraw = true;
    }
}

// --- Screen Navigation ---

void RpiMonitor::_nextScreen()
{
    int next = (static_cast<int>(_currentScreen) + 1) % SCREEN_COUNT;
    _currentScreen = static_cast<Screen>(next);
    _regConfirmMode = false;
    _svcConfirmMode = false;
    _pwrConfirmMode = false;
    _cmdConfirmMode = false;
}

void RpiMonitor::_prevScreen()
{
    int prev = (static_cast<int>(_currentScreen) - 1 + SCREEN_COUNT) % SCREEN_COUNT;
    _currentScreen = static_cast<Screen>(prev);
    _regConfirmMode = false;
    _svcConfirmMode = false;
    _pwrConfirmMode = false;
    _cmdConfirmMode = false;
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

// --- List Screen Helpers ---

void RpiMonitor::_listScrollUp(int& selectedIndex, bool& confirmMode)
{
    if (confirmMode) {
        confirmMode = false;
    } else if (selectedIndex > 0) {
        selectedIndex--;
    }
}

void RpiMonitor::_listScrollDown(int& selectedIndex, bool& confirmMode, int count)
{
    if (!confirmMode && selectedIndex < count - 1) {
        selectedIndex++;
    }
}

// --- Services Action ---

void RpiMonitor::_servicesAction()
{
    if (_ble.getState() != BleState::CONNECTED) return;

    int count = _ble.getServiceCount();
    if (count == 0) return;

    if (_svcConfirmMode) {
        // Execute toggle
        auto& svc = _ble.getServiceInfo(_svcSelectedIndex);
        const char* action = svc.active ? "stop" : "start";
        _ble.sendServiceControl(svc.name.c_str(), action);
        _svcConfirmMode = false;
        // Refresh data after a short delay
        vTaskDelay(pdMS_TO_TICKS(500));
        _ble.readAll();
        _lastDataUpdate = (unsigned long)(esp_timer_get_time() / 1000);
    } else {
        _svcConfirmMode = true;
    }
    _needsRedraw = true;
}

// --- Power Action ---

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

// --- Commands Action ---

void RpiMonitor::_commandsAction()
{
    if (_ble.getState() != BleState::CONNECTED) return;

    int count = _ble.getCommandCount();
    if (count == 0) return;

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

// --- Settings Action ---

void RpiMonitor::_settingsAction()
{
    _soundEnabled = !_soundEnabled;
    _saveSettings();
    if (_soundEnabled) {
        _data.hal->buzz.tone(1000, 150);
    }
    _needsRedraw = true;
}

// --- NVS Settings ---

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
