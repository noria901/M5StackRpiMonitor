#include "ble_client.h"

class ScanCallback : public BLEAdvertisedDeviceCallbacks {
public:
    std::vector<BLEAdvertisedDevice>* pFoundDevices;

    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        if (advertisedDevice.haveServiceUUID() &&
            advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
            pFoundDevices->push_back(advertisedDevice);
            Serial.printf("Found RPi Monitor: %s\n", advertisedDevice.getName().c_str());
        }
    }
};

class ClientCallback : public BLEClientCallbacks {
public:
    BLEState* pState;

    void onConnect(BLEClient* client) override {
        Serial.println("BLE Connected");
    }

    void onDisconnect(BLEClient* client) override {
        Serial.println("BLE Disconnected");
        *pState = BLEState::DISCONNECTED;
    }
};

void BLEMonitorClient::init() {
    BLEDevice::init(BLE_DEVICE_NAME);
    pClient = BLEDevice::createClient();

    auto* cb = new ClientCallback();
    cb->pState = &state;
    pClient->setClientCallbacks(cb);

    pScan = BLEDevice::getScan();
    auto* scanCb = new ScanCallback();
    scanCb->pFoundDevices = &foundDevices;
    pScan->setAdvertisedDeviceCallbacks(scanCb);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
}

void BLEMonitorClient::scan() {
    state = BLEState::SCANNING;
    foundDevices.clear();
    Serial.println("Starting BLE scan...");
    pScan->start(BLE_SCAN_DURATION, false);
    pScan->clearResults();

    if (foundDevices.empty()) {
        state = BLEState::DISCONNECTED;
    }
    Serial.printf("Scan complete. Found %d devices.\n", foundDevices.size());
}

bool BLEMonitorClient::connectToServer(BLEAdvertisedDevice* device) {
    state = BLEState::CONNECTING;
    Serial.printf("Connecting to %s...\n", device->getName().c_str());

    pClient->connect(device);
    if (!pClient->isConnected()) {
        state = BLEState::DISCONNECTED;
        return false;
    }

    pService = pClient->getService(BLEUUID(SERVICE_UUID));
    if (pService == nullptr) {
        Serial.println("Service not found");
        pClient->disconnect();
        state = BLEState::DISCONNECTED;
        return false;
    }

    serverName = device->getName().c_str();
    state = BLEState::CONNECTED;
    Serial.println("Connected successfully");
    return true;
}

void BLEMonitorClient::disconnect() {
    if (pClient && pClient->isConnected()) {
        pClient->disconnect();
    }
    state = BLEState::DISCONNECTED;
    serverName = "";
}

bool BLEMonitorClient::isConnected() {
    return state == BLEState::CONNECTED && pClient && pClient->isConnected();
}

BLEState BLEMonitorClient::getState() {
    return state;
}

bool BLEMonitorClient::readCharacteristic(const char* uuid, String& result) {
    if (!isConnected() || !pService) return false;

    BLERemoteCharacteristic* pChar = pService->getCharacteristic(BLEUUID(uuid));
    if (pChar == nullptr) return false;

    try {
        std::string value = pChar->readValue();
        result = String(value.c_str());
        return true;
    } catch (...) {
        return false;
    }
}

bool BLEMonitorClient::readAll() {
    if (!isConnected()) return false;

    String data;

    if (readCharacteristic(CHAR_CPU_UUID, data)) {
        parseCpuInfo(data);
    }
    if (readCharacteristic(CHAR_MEMORY_UUID, data)) {
        parseMemoryInfo(data);
    }
    if (readCharacteristic(CHAR_STORAGE_UUID, data)) {
        parseStorageInfo(data);
    }
    if (readCharacteristic(CHAR_NETWORK_UUID, data)) {
        parseNetworkInfo(data);
    }
    if (readCharacteristic(CHAR_SYSTEM_UUID, data)) {
        parseSystemInfo(data);
    }

    return true;
}

bool BLEMonitorClient::sendRegistration(const String& deviceName) {
    if (!isConnected() || !pService) return false;

    BLERemoteCharacteristic* pChar = pService->getCharacteristic(BLEUUID(CHAR_REGISTRATION_UUID));
    if (pChar == nullptr) return false;

    JsonDocument doc;
    doc["action"] = "register";
    doc["device_name"] = deviceName;
    doc["mac"] = BLEDevice::getAddress().toString();

    String json;
    serializeJson(doc, json);
    pChar->writeValue(json.c_str(), json.length());
    return true;
}

void BLEMonitorClient::parseCpuInfo(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) == DeserializationError::Ok) {
        cpuInfo.usage = doc["usage"] | 0.0f;
        cpuInfo.temp = doc["temp"] | 0.0f;
        cpuInfo.freq = doc["freq"] | 0;
    }
}

void BLEMonitorClient::parseMemoryInfo(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) == DeserializationError::Ok) {
        memoryInfo.ramTotal = doc["ram_total"] | 0;
        memoryInfo.ramUsed = doc["ram_used"] | 0;
        memoryInfo.swapTotal = doc["swap_total"] | 0;
        memoryInfo.swapUsed = doc["swap_used"] | 0;
    }
}

void BLEMonitorClient::parseStorageInfo(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) == DeserializationError::Ok) {
        storageInfo.total = doc["total"] | 0;
        storageInfo.used = doc["used"] | 0;
        storageInfo.free = doc["free"] | 0;
    }
}

void BLEMonitorClient::parseNetworkInfo(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) == DeserializationError::Ok) {
        networkInfo.wifiSsid = doc["wifi_ssid"] | "";
        networkInfo.wifiSignal = doc["wifi_signal"] | 0;
        networkInfo.ip = doc["ip"] | "";
        networkInfo.hotspot = doc["hotspot"] | false;
        networkInfo.hotspotSsid = doc["hotspot_ssid"] | "";
        networkInfo.mac = doc["mac"] | "";
    }
}

void BLEMonitorClient::parseSystemInfo(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) == DeserializationError::Ok) {
        systemInfo.hostname = doc["hostname"] | "";
        systemInfo.uptime = doc["uptime"] | 0UL;
        systemInfo.os = doc["os"] | "";
        systemInfo.kernel = doc["kernel"] | "";
    }
}

CpuInfo BLEMonitorClient::getCpuInfo() { return cpuInfo; }
MemoryInfo BLEMonitorClient::getMemoryInfo() { return memoryInfo; }
StorageInfo BLEMonitorClient::getStorageInfo() { return storageInfo; }
NetworkInfo BLEMonitorClient::getNetworkInfo() { return networkInfo; }
SystemInfo BLEMonitorClient::getSystemInfo() { return systemInfo; }

String BLEMonitorClient::getServerName() { return serverName; }
int BLEMonitorClient::getFoundDeviceCount() { return foundDevices.size(); }
String BLEMonitorClient::getFoundDeviceName(int index) {
    if (index < 0 || index >= (int)foundDevices.size()) return "";
    return String(foundDevices[index].getName().c_str());
}
BLEAdvertisedDevice* BLEMonitorClient::getFoundDevice(int index) {
    if (index < 0 || index >= (int)foundDevices.size()) return nullptr;
    return &foundDevices[index];
}
