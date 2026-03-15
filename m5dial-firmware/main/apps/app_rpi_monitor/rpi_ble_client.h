#pragma once

#include <string>
#include <vector>
#include <cstdint>

// BLE Service and Characteristic UUIDs (same as rpi-daemon)
#define RPI_SERVICE_UUID           "12345678-1234-5678-1234-56789abcdef0"
#define RPI_CHAR_CPU_UUID          "12345678-1234-5678-1234-56789abcdef1"
#define RPI_CHAR_MEMORY_UUID       "12345678-1234-5678-1234-56789abcdef2"
#define RPI_CHAR_STORAGE_UUID      "12345678-1234-5678-1234-56789abcdef3"
#define RPI_CHAR_NETWORK_UUID      "12345678-1234-5678-1234-56789abcdef4"
#define RPI_CHAR_SYSTEM_UUID       "12345678-1234-5678-1234-56789abcdef5"
#define RPI_CHAR_REGISTRATION_UUID "12345678-1234-5678-1234-56789abcdef6"
#define RPI_CHAR_SERVICES_UUID     "12345678-1234-5678-1234-56789abcdef7"
#define RPI_CHAR_SYSTEM_CTRL_UUID  "12345678-1234-5678-1234-56789abcdef8"
#define RPI_CHAR_COMMANDS_UUID     "12345678-1234-5678-1234-56789abcdef9"

// BLE scan duration
#define RPI_BLE_SCAN_DURATION_SEC  5

// Number of characteristics
#define RPI_CHAR_COUNT             9

// Characteristic handle indices
#define RPI_CHAR_IDX_CPU           0
#define RPI_CHAR_IDX_MEMORY        1
#define RPI_CHAR_IDX_STORAGE       2
#define RPI_CHAR_IDX_NETWORK       3
#define RPI_CHAR_IDX_SYSTEM        4
#define RPI_CHAR_IDX_REGISTRATION  5
#define RPI_CHAR_IDX_SERVICES      6
#define RPI_CHAR_IDX_SYSTEM_CTRL   7
#define RPI_CHAR_IDX_COMMANDS      8

enum class BleState {
    DISCONNECTED,
    SCANNING,
    CONNECTING,
    CONNECTED,
};

struct RpiCpuInfo {
    float usage = 0;
    float temp = 0;
    int freq = 0;
};

struct RpiMemoryInfo {
    int ramTotal = 0;
    int ramUsed = 0;
    int swapTotal = 0;
    int swapUsed = 0;
};

struct RpiStorageInfo {
    int total = 0;
    int used = 0;
    int free = 0;
};

struct RpiNetworkInfo {
    std::string wifiSsid;
    int wifiSignal = 0;
    std::string ip;
    bool hotspot = false;
    std::string hotspotSsid;
    std::string mac;
};

struct RpiSystemInfo {
    std::string hostname;
    unsigned long uptime = 0;
    std::string os;
    std::string kernel;
    std::string time;
    std::string platform;
};

struct RpiServiceInfo {
    std::string name;
    bool active = false;
};

struct RpiCommandInfo {
    std::string name;
    std::string state = "idle";  // idle, running, done, error
    int exitCode = -1;
};

struct FoundDevice {
    std::string name;
    uint8_t addrType;
    uint8_t addr[6];
};

class RpiBleClient {
public:
    void init();
    void startScan();
    bool isScanComplete();
    void scanAndAutoConnect();
    bool connectToDevice(int index);
    void disconnect();
    void forgetDevice();
    bool hasSavedServer() const;
    std::string getSavedServerName() const;
    BleState getState() const { return _state; }

    // Check if GAP callback found saved server and wants to auto-connect
    // Returns device index to connect, or -1 if none pending
    int consumePendingAutoConnect();
    bool discoverServiceAndChars();

    bool readAll();
    bool sendRegistration(const char* deviceName);
    bool sendPowerCommand(const char* action);
    bool sendServiceControl(const char* serviceName, const char* action);
    bool sendCommand(const char* name, const char* action);

    int getFoundDeviceCount() const { return (int)_foundDevices.size(); }
    std::string getFoundDeviceName(int index) const;

    const RpiCpuInfo& getCpuInfo() const { return _cpuInfo; }
    const RpiMemoryInfo& getMemoryInfo() const { return _memInfo; }
    const RpiStorageInfo& getStorageInfo() const { return _storageInfo; }
    const RpiNetworkInfo& getNetworkInfo() const { return _netInfo; }
    const RpiSystemInfo& getSystemInfo() const { return _sysInfo; }

    int getServiceCount() const { return (int)_services.size(); }
    const RpiServiceInfo& getServiceInfo(int index) const;
    int getCommandCount() const { return (int)_commands.size(); }
    const RpiCommandInfo& getCommandInfo(int index) const;

    // NimBLE callbacks (called from static C functions)
    void onGapEvent(int event, void* arg);
    void onGattcReadComplete(int status, const uint8_t* data, uint16_t len);
    void onGattcDiscComplete(int status);
    void onGattcCharDiscComplete(int status, uint16_t charIdx, uint16_t valHandle);

private:
    BleState _state = BleState::DISCONNECTED;

    // Connection handle
    uint16_t _connHandle = 0xFFFF;

    // Characteristic value handles
    uint16_t _charHandles[RPI_CHAR_COUNT] = {0};

    // Data
    RpiCpuInfo _cpuInfo;
    RpiMemoryInfo _memInfo;
    RpiStorageInfo _storageInfo;
    RpiNetworkInfo _netInfo;
    RpiSystemInfo _sysInfo;
    std::vector<RpiServiceInfo> _services;
    std::vector<RpiCommandInfo> _commands;

    // Scan results
    std::vector<FoundDevice> _foundDevices;
    unsigned long _scanStartMs = 0;

    // NVS saved server name
    std::string _savedServerName;

    // Read synchronization
    volatile bool _readDone = false;
    uint8_t _readBuf[512];
    uint16_t _readLen = 0;

    // Service/char discovery state
    volatile bool _discDone = false;
    int _discCharIdx = 0;

    // Deferred auto-connect (set from GAP callback, consumed from main loop)
    volatile int _pendingAutoConnectIdx = -1;

    // NVS helpers
    void _loadSavedServer();
    void _saveSavedServer(const std::string& name);
    void _clearSavedServer();

    // JSON parsers (using cJSON)
    void _parseCpuInfo(const char* json);
    void _parseMemoryInfo(const char* json);
    void _parseStorageInfo(const char* json);
    void _parseNetworkInfo(const char* json);
    void _parseSystemInfo(const char* json);
    void _parseServicesInfo(const char* json);
    void _parseCommandsInfo(const char* json);

    // GATT operations
    bool _readCharacteristic(int charIdx, char* outBuf, size_t bufSize);
    bool _writeCharacteristic(int charIdx, const char* data, size_t len);
    bool _discoverService();
    bool _discoverCharacteristics();
};
