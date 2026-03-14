#include "app_ota_update.h"
#include "../common_define.h"
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

using namespace MOONCAKE::USER_APP;

void OtaUpdate::onSetup()
{
    setAppName("OTA Update");
    setAllowBgRunning(false);
    _data.hal = (HAL::HAL*)getUserData();
}

void OtaUpdate::onCreate()
{
    if (!_data.hal) {
        ESP_LOGE(_tag, "HAL is null");
        destroyApp();
        return;
    }

    _ble.init();
    _state = State::IDLE;
    _needsRedraw = true;
    ESP_LOGI(_tag, "OTA Update app created");
}

void OtaUpdate::onRunning()
{
    if (!_data.hal) return;

    _handleInput();

    // State machine
    switch (_state) {
        case State::SCANNING:
            if (_ble.isScanComplete()) {
                if (_ble.getFoundDeviceCount() > 0) {
                    _state = State::SELECT_DEVICE;
                    _selectedDevice = 0;
                } else {
                    _state = State::IDLE;
                    _errorMsg = "No OTA server found";
                }
                _needsRedraw = true;
            }
            break;

        case State::TRANSFERRING:
            // Transfer is done in _connectAndUpdate, triggered by button press
            break;

        default:
            break;
    }

    if (_needsRedraw) {
        _draw();
        _needsRedraw = false;
    }
}

void OtaUpdate::onDestroy()
{
    _ble.disconnect();
    ESP_LOGI(_tag, "OTA Update app destroyed");
}

// ---- Input Handling ----

void OtaUpdate::_handleInput()
{
    unsigned long now = millis();

    // Encoder rotation
    if (_data.hal->encoder.wasMoved(true)) {
        int dir = _data.hal->encoder.getDirection();

        if (_state == State::SELECT_DEVICE) {
            int count = _ble.getFoundDeviceCount();
            if (dir < 1 && _selectedDevice < count - 1) {
                _selectedDevice++;
            } else if (dir >= 1 && _selectedDevice > 0) {
                _selectedDevice--;
            }
            _data.hal->buzz.tone(4000, 20);
            _needsRedraw = true;
        }
    }

    // Button press
    bool buttonPressed = _data.hal->encoder.btn.pressed();

    // Touch center
    if (!buttonPressed && _data.hal->tp.isTouched()) {
        _data.hal->tp.update();
        auto point = _data.hal->tp.getTouchPointBuffer();
        int dx = point.x - 120;
        int dy = point.y - 120;
        if ((dx * dx + dy * dy) < (50 * 50)) {
            buttonPressed = true;
        }
    }

    if (!buttonPressed) return;
    if (now - _lastButtonPress < BUTTON_DEBOUNCE_MS) return;
    _lastButtonPress = now;

    _data.hal->buzz.tone(4000, 20);

    switch (_state) {
        case State::IDLE:
            _startScan();
            break;

        case State::SELECT_DEVICE:
            // Start OTA with selected device
            _connectAndUpdate(_selectedDevice);
            break;

        case State::COMPLETE:
            // Reboot
            ESP_LOGI(_tag, "Rebooting after OTA...");
            esp_restart();
            break;

        case State::ERROR:
            // Go back to idle
            _state = State::IDLE;
            _errorMsg.clear();
            _needsRedraw = true;
            break;

        default:
            // Long press to exit app in any state
            // Check if button held for 1 second
            {
                int held = 0;
                while (!_data.hal->encoder.btn.read() && held < 10) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    held++;
                }
                if (held >= 10) {
                    _ble.disconnect();
                    destroyApp();
                }
            }
            break;
    }
}

// ---- Actions ----

void OtaUpdate::_startScan()
{
    _state = State::SCANNING;
    _errorMsg.clear();
    _ble.startScan();
    _needsRedraw = true;
}

void OtaUpdate::_connectAndUpdate(int deviceIndex)
{
    // Connecting
    _state = State::CONNECTING;
    _needsRedraw = true;
    _draw();

    if (!_ble.connectToDevice(deviceIndex)) {
        _state = State::ERROR;
        _errorMsg = "Connection failed";
        _needsRedraw = true;
        return;
    }

    // Reading firmware info
    _state = State::READING_INFO;
    _needsRedraw = true;
    _draw();

    if (!_ble.readFirmwareInfo(_fwInfo)) {
        _state = State::ERROR;
        _errorMsg = "Failed to read FW info";
        _ble.disconnect();
        _needsRedraw = true;
        return;
    }

    // Start OTA transfer
    _state = State::TRANSFERRING;
    _currentOffset = 0;
    _progressPercent = 0;
    _needsRedraw = true;

    if (!_performOtaTransfer()) {
        _ble.disconnect();
        _needsRedraw = true;
        return;
    }

    _ble.disconnect();
    _state = State::COMPLETE;
    _needsRedraw = true;
}

bool OtaUpdate::_performOtaTransfer()
{
    // Find OTA partition
    const esp_partition_t* otaPart = esp_ota_get_next_update_partition(nullptr);
    if (!otaPart) {
        _state = State::ERROR;
        _errorMsg = "No OTA partition";
        return false;
    }

    ESP_LOGI(_tag, "OTA partition: %s, offset=0x%lx, size=%lu",
             otaPart->label, (unsigned long)otaPart->address,
             (unsigned long)otaPart->size);

    if (_fwInfo.size > otaPart->size) {
        _state = State::ERROR;
        _errorMsg = "FW too large for partition";
        return false;
    }

    // Begin OTA
    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(otaPart, _fwInfo.size, &handle);
    if (err != ESP_OK) {
        _state = State::ERROR;
        char buf[64];
        snprintf(buf, sizeof(buf), "OTA begin failed: %d", err);
        _errorMsg = buf;
        return false;
    }

    // Transfer chunks
    uint8_t chunkBuf[512];
    _currentOffset = 0;

    while (_currentOffset < _fwInfo.size) {
        // Set offset on server
        if (!_ble.setOffset(_currentOffset)) {
            esp_ota_abort(handle);
            _state = State::ERROR;
            _errorMsg = "Failed to set offset";
            return false;
        }

        // Small delay for server to prepare
        vTaskDelay(pdMS_TO_TICKS(10));

        // Read data chunk
        uint16_t chunkLen = 0;
        if (!_ble.readDataChunk(chunkBuf, sizeof(chunkBuf), chunkLen) || chunkLen == 0) {
            esp_ota_abort(handle);
            _state = State::ERROR;
            _errorMsg = "Failed to read chunk";
            return false;
        }

        // Write to OTA partition
        err = esp_ota_write(handle, chunkBuf, chunkLen);
        if (err != ESP_OK) {
            esp_ota_abort(handle);
            _state = State::ERROR;
            char buf[64];
            snprintf(buf, sizeof(buf), "OTA write failed: %d", err);
            _errorMsg = buf;
            return false;
        }

        _currentOffset += chunkLen;
        int newPercent = (int)((uint64_t)_currentOffset * 100 / _fwInfo.size);
        if (newPercent != _progressPercent) {
            _progressPercent = newPercent;
            _needsRedraw = true;
            _draw();
            _needsRedraw = false;
        }
    }

    // Finalize
    _state = State::FINALIZING;
    _needsRedraw = true;
    _draw();

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        _state = State::ERROR;
        char buf[64];
        snprintf(buf, sizeof(buf), "OTA end failed: %d", err);
        _errorMsg = buf;
        return false;
    }

    err = esp_ota_set_boot_partition(otaPart);
    if (err != ESP_OK) {
        _state = State::ERROR;
        char buf[64];
        snprintf(buf, sizeof(buf), "Set boot failed: %d", err);
        _errorMsg = buf;
        return false;
    }

    ESP_LOGI(_tag, "OTA complete! Next boot from '%s'", otaPart->label);
    return true;
}

// ---- Drawing ----

void OtaUpdate::_draw()
{
    auto* canvas = _data.hal->canvas;
    canvas->fillSprite(TFT_BLACK);

    switch (_state) {
        case State::IDLE:       _drawIdle(); break;
        case State::SCANNING:   _drawScanning(); break;
        case State::SELECT_DEVICE: _drawSelectDevice(); break;
        case State::CONNECTING:
        case State::READING_INFO:
            _drawConnecting(); break;
        case State::TRANSFERRING:
        case State::FINALIZING:
            _drawTransferring(); break;
        case State::COMPLETE:   _drawComplete(); break;
        case State::ERROR:      _drawError(); break;
    }

    canvas->pushSprite(0, 0);
}

void OtaUpdate::_drawCentered(const char* text, int y, uint32_t color)
{
    auto* canvas = _data.hal->canvas;
    canvas->setTextColor(color);
    canvas->drawCenterString(text, 120, y);
}

void OtaUpdate::_drawProgressBar(int y, int percent, uint32_t color)
{
    auto* canvas = _data.hal->canvas;
    int barX = 30;
    int barW = 180;
    int barH = 16;

    // Background
    canvas->fillRoundRect(barX, y, barW, barH, 4, 0x333333);
    // Fill
    int fillW = barW * percent / 100;
    if (fillW > 0) {
        canvas->fillRoundRect(barX, y, fillW, barH, 4, color);
    }
    // Text
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    canvas->setTextColor(TFT_WHITE);
    canvas->drawCenterString(buf, 120, y + barH + 4);
}

void OtaUpdate::_drawIdle()
{
    auto* canvas = _data.hal->canvas;
    canvas->setFont(&fonts::Font2);

    _drawCentered("OTA Update", 50, 0x00BCD4);
    _drawCentered("Press to scan", 100, TFT_WHITE);
    _drawCentered("for OTA servers", 120, 0xAAAAAA);

    if (!_errorMsg.empty()) {
        _drawCentered(_errorMsg.c_str(), 160, 0xFF6666);
    }

    // Draw circle hint
    canvas->drawCircle(120, 200, 15, 0x555555);
    _drawCentered("SCAN", 194, 0x555555);
}

void OtaUpdate::_drawScanning()
{
    auto* canvas = _data.hal->canvas;
    canvas->setFont(&fonts::Font2);

    _drawCentered("OTA Update", 50, 0x00BCD4);
    _drawCentered("Scanning...", 100, 0xFFFF00);

    // Animated dots
    unsigned long now = millis();
    int dots = (now / 500) % 4;
    char buf[8] = "   ";
    for (int i = 0; i < dots; i++) buf[i] = '.';
    _drawCentered(buf, 120, 0xFFFF00);
}

void OtaUpdate::_drawSelectDevice()
{
    auto* canvas = _data.hal->canvas;
    canvas->setFont(&fonts::Font2);

    _drawCentered("Select Server", 30, 0x00BCD4);

    int count = _ble.getFoundDeviceCount();
    int startY = 60;
    int lineH = 24;
    int maxVisible = 5;

    int startIdx = 0;
    if (_selectedDevice >= maxVisible) {
        startIdx = _selectedDevice - maxVisible + 1;
    }

    for (int i = startIdx; i < count && (i - startIdx) < maxVisible; i++) {
        int y = startY + (i - startIdx) * lineH;
        bool selected = (i == _selectedDevice);

        if (selected) {
            canvas->fillRoundRect(20, y - 2, 200, lineH - 2, 4, 0x004455);
            canvas->setTextColor(0x00FFFF);
        } else {
            canvas->setTextColor(0xAAAAAA);
        }

        std::string name = _ble.getFoundDeviceName(i);
        if (name.length() > 20) {
            name = name.substr(0, 17) + "...";
        }

        char label[32];
        snprintf(label, sizeof(label), "%s%s", selected ? "> " : "  ", name.c_str());
        canvas->drawString(label, 25, y);
    }

    _drawCentered("Press to connect", 200, 0x555555);
}

void OtaUpdate::_drawConnecting()
{
    auto* canvas = _data.hal->canvas;
    canvas->setFont(&fonts::Font2);

    _drawCentered("OTA Update", 50, 0x00BCD4);

    if (_state == State::CONNECTING) {
        _drawCentered("Connecting...", 100, 0xFFFF00);
    } else {
        _drawCentered("Reading FW info...", 100, 0xFFFF00);
    }
}

void OtaUpdate::_drawTransferring()
{
    auto* canvas = _data.hal->canvas;
    canvas->setFont(&fonts::Font2);

    _drawCentered("OTA Update", 30, 0x00BCD4);

    if (_state == State::FINALIZING) {
        _drawCentered("Finalizing...", 60, 0xFFFF00);
    } else {
        // Firmware name
        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "%.20s", _fwInfo.name.c_str());
        _drawCentered(nameBuf, 55, 0xAAAAAA);

        // Transfer info
        char sizeBuf[32];
        snprintf(sizeBuf, sizeof(sizeBuf), "%lu / %lu KB",
                 (unsigned long)(_currentOffset / 1024),
                 (unsigned long)(_fwInfo.size / 1024));
        _drawCentered(sizeBuf, 75, TFT_WHITE);
    }

    _drawProgressBar(100, _progressPercent, 0x00BCD4);

    // Speed estimate
    if (_currentOffset > 0 && _state == State::TRANSFERRING) {
        _drawCentered("Transferring via BLE", 160, 0x555555);
    }
}

void OtaUpdate::_drawComplete()
{
    auto* canvas = _data.hal->canvas;
    canvas->setFont(&fonts::Font2);

    _drawCentered("OTA Complete!", 70, 0x4CAF50);
    _drawCentered("Firmware updated", 100, TFT_WHITE);
    _drawCentered("successfully", 120, TFT_WHITE);

    canvas->drawCircle(120, 180, 15, 0x4CAF50);
    _drawCentered("REBOOT", 174, 0x4CAF50);
}

void OtaUpdate::_drawError()
{
    auto* canvas = _data.hal->canvas;
    canvas->setFont(&fonts::Font2);

    _drawCentered("OTA Error", 60, 0xFF4444);
    _drawCentered(_errorMsg.c_str(), 100, TFT_WHITE);

    canvas->drawCircle(120, 180, 15, 0x555555);
    _drawCentered("BACK", 174, 0x555555);
}
