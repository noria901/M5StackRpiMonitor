#pragma once

#include <string>
#include <vector>
#include <cstdint>

// OTA BLE Service and Characteristic UUIDs
#define OTA_SERVICE_UUID      "12345678-1234-5678-9abc-def012345670"
#define OTA_CHAR_INFO_UUID    "12345678-1234-5678-9abc-def012345671"
#define OTA_CHAR_DATA_UUID    "12345678-1234-5678-9abc-def012345672"
#define OTA_CHAR_CONTROL_UUID "12345678-1234-5678-9abc-def012345673"

// BLE scan duration
#define OTA_BLE_SCAN_DURATION_SEC  5

// Characteristic count and indices
#define OTA_CHAR_COUNT   3
#define OTA_CHAR_IDX_INFO    0
#define OTA_CHAR_IDX_DATA    1
#define OTA_CHAR_IDX_CONTROL 2

enum class OtaBleState {
    DISCONNECTED,
    SCANNING,
    CONNECTING,
    CONNECTED,
};

struct OtaFoundDevice {
    std::string name;
    uint8_t addrType;
    uint8_t addr[6];
};

struct OtaFirmwareInfo {
    uint32_t size = 0;
    uint16_t chunkSize = 0;
    std::string name;
};

class OtaBleClient {
public:
    void init();
    void startScan();
    bool isScanComplete();
    bool connectToDevice(int index);
    void disconnect();

    OtaBleState getState() const { return _state; }
    int getFoundDeviceCount() const { return (int)_foundDevices.size(); }
    std::string getFoundDeviceName(int index) const;

    // OTA operations
    bool readFirmwareInfo(OtaFirmwareInfo& info);
    bool setOffset(uint32_t offset);
    bool readDataChunk(uint8_t* buf, uint16_t bufSize, uint16_t& outLen);

    // NimBLE callbacks
    void onGapEvent(int event, void* arg);
    void onGattcReadComplete(int status, const uint8_t* data, uint16_t len);
    void onGattcDiscComplete(int status);
    void onGattcCharDiscComplete(int status, uint16_t charIdx, uint16_t valHandle);

private:
    OtaBleState _state = OtaBleState::DISCONNECTED;
    uint16_t _connHandle = 0xFFFF;
    uint16_t _charHandles[OTA_CHAR_COUNT] = {0};

    std::vector<OtaFoundDevice> _foundDevices;
    unsigned long _scanStartMs = 0;

    // Read synchronization
    volatile bool _readDone = false;
    uint8_t _readBuf[520];
    uint16_t _readLen = 0;

    // Service/char discovery state
    volatile bool _discDone = false;

    // GATT operations
    bool _readCharacteristic(int charIdx, uint8_t* outBuf, size_t bufSize, uint16_t& outLen);
    bool _writeCharacteristic(int charIdx, const uint8_t* data, size_t len);
    bool _discoverService();
    bool _discoverCharacteristics();
};
