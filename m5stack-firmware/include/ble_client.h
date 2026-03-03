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
    String time = "";
    String platform = "";
};

struct ServiceInfo {
    String name = "";
    bool active = false;
};

struct CommandInfo {
    String name = "";
    String state = "idle";  // idle, running, done, error
    int exitCode = -1;
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
    bool sendServiceControl(const String& serviceName, const String& action);
    bool sendPowerCommand(const String& action);
    bool sendCommand(const String& name, const String& action);

    int getServiceCount();
    ServiceInfo getServiceInfo(int index);

    int getCommandCount();
    CommandInfo getCommandInfo(int index);

    CpuInfo getCpuInfo();
    MemoryInfo getMemoryInfo();
    StorageInfo getStorageInfo();
    NetworkInfo getNetworkInfo();
    SystemInfo getSystemInfo();

    String getServerName();
    unsigned long getLastDataMillis();
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
    std::vector<ServiceInfo> services;
    std::vector<CommandInfo> commands;

    String serverName = "";
    std::vector<BLEAdvertisedDevice> foundDevices;
    unsigned long scanStartTime = 0;
    unsigned long lastDataMillis = 0;
    Preferences prefs;

    bool readCharacteristic(const char* uuid, String& result);
    void parseCpuInfo(const String& json);
    void parseMemoryInfo(const String& json);
    void parseStorageInfo(const String& json);
    void parseNetworkInfo(const String& json);
    void parseSystemInfo(const String& json);
    void parseServicesInfo(const String& json);
    void parseCommandsInfo(const String& json);
    bool writeCharacteristic(const char* uuid, const String& data);
};
