#pragma once

#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config.h"

enum class BLEState {
    DISCONNECTED,
    SCANNING,
    CONNECTING,
    CONNECTED,
    REGISTERING
};

struct CpuInfo {
    float usage = 0;
    float temp = 0;
    int freq = 0;
};

struct MemoryInfo {
    int ramTotal = 0;
    int ramUsed = 0;
    int swapTotal = 0;
    int swapUsed = 0;
};

struct StorageInfo {
    int total = 0;
    int used = 0;
    int free = 0;
};

struct NetworkInfo {
    String wifiSsid = "";
    int wifiSignal = 0;
    String ip = "";
    bool hotspot = false;
    String hotspotSsid = "";
    String mac = "";
};

struct SystemInfo {
    String hostname = "";
    unsigned long uptime = 0;
    String os = "";
    String kernel = "";
};

class BLEMonitorClient {
public:
    void init();
    void scan();
    bool isScanComplete();
    bool connectToServer(BLEAdvertisedDevice* device);
    void disconnect();
    void forgetDevice();
    bool isConnected();
    bool hasSavedServer();
    BLEState getState();

    bool readAll();
    bool sendRegistration(const String& deviceName);

    CpuInfo getCpuInfo();
    MemoryInfo getMemoryInfo();
    StorageInfo getStorageInfo();
    NetworkInfo getNetworkInfo();
    SystemInfo getSystemInfo();

    String getServerName();
    int getFoundDeviceCount();
    String getFoundDeviceName(int index);
    BLEAdvertisedDevice* getFoundDevice(int index);

private:
    BLEState state = BLEState::DISCONNECTED;
    BLEClient* pClient = nullptr;
    BLEScan* pScan = nullptr;
    BLERemoteService* pService = nullptr;

    CpuInfo cpuInfo;
    MemoryInfo memoryInfo;
    StorageInfo storageInfo;
    NetworkInfo networkInfo;
    SystemInfo systemInfo;

    String serverName = "";
    std::vector<BLEAdvertisedDevice> foundDevices;
    unsigned long scanStartTime = 0;
    Preferences prefs;

    bool readCharacteristic(const char* uuid, String& result);
    void parseCpuInfo(const String& json);
    void parseMemoryInfo(const String& json);
    void parseStorageInfo(const String& json);
    void parseNetworkInfo(const String& json);
    void parseSystemInfo(const String& json);
};
