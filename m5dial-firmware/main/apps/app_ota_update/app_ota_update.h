#pragma once

#include "../../hal/hal.h"
#include "../app.h"
#include "ota_ble_client.h"

namespace MOONCAKE {
namespace USER_APP {

class OtaUpdate : public APP_BASE {
public:
    void onSetup() override;
    void onCreate() override;
    void onRunning() override;
    void onDestroy() override;

private:
    static constexpr const char* _tag = "OtaUpd";

    struct Data_t {
        HAL::HAL* hal = nullptr;
    };
    Data_t _data;

    OtaBleClient _ble;

    enum class State {
        IDLE,           // Initial state, show "Scan" button
        SCANNING,       // BLE scanning for OTA servers
        SELECT_DEVICE,  // Show found devices
        CONNECTING,     // Connecting to selected device
        READING_INFO,   // Reading firmware info
        TRANSFERRING,   // Downloading firmware chunks
        FINALIZING,     // Writing OTA end, setting boot partition
        COMPLETE,       // OTA complete, ready to reboot
        ERROR,          // Error occurred
    };
    State _state = State::IDLE;

    // Device selection
    int _selectedDevice = 0;

    // OTA progress
    OtaFirmwareInfo _fwInfo;
    uint32_t _currentOffset = 0;
    int _progressPercent = 0;
    std::string _errorMsg;

    // Timing
    unsigned long _lastUpdate = 0;
    unsigned long _lastButtonPress = 0;
    static constexpr unsigned long BUTTON_DEBOUNCE_MS = 250;

    bool _needsRedraw = true;

    // OTA flash handle
    void* _otaHandle = nullptr;

    // Actions
    void _startScan();
    void _connectAndUpdate(int deviceIndex);
    bool _performOtaTransfer();
    void _handleInput();
    void _draw();

    // Draw helpers
    void _drawIdle();
    void _drawScanning();
    void _drawSelectDevice();
    void _drawConnecting();
    void _drawTransferring();
    void _drawComplete();
    void _drawError();
    void _drawCentered(const char* text, int y, uint32_t color = TFT_WHITE);
    void _drawProgressBar(int y, int percent, uint32_t color);
};

}  // namespace USER_APP
}  // namespace MOONCAKE
