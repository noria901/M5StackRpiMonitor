#include "rpi_ble_client.h"

#include <cstring>
#include <cstdio>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cJSON.h>

// NimBLE headers
#include <os/os_mbuf.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>

static const char* TAG = "RpiBLE";

// Global instance pointer for C callbacks
static RpiBleClient* g_instance = nullptr;

// ---- UUID helpers ----

static const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(
    0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
);

// Characteristic UUIDs: def1..def8
static const ble_uuid128_t kCharUuids[RPI_CHAR_COUNT] = {
    BLE_UUID128_INIT(  // CPU (def1)
        0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
    ),
    BLE_UUID128_INIT(  // Memory (def2)
        0xf2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
    ),
    BLE_UUID128_INIT(  // Storage (def3)
        0xf3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
    ),
    BLE_UUID128_INIT(  // Network (def4)
        0xf4, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
    ),
    BLE_UUID128_INIT(  // System (def5)
        0xf5, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
    ),
    BLE_UUID128_INIT(  // Registration (def6)
        0xf6, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
    ),
    BLE_UUID128_INIT(  // Services (def7)
        0xf7, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
    ),
    BLE_UUID128_INIT(  // System Control (def8)
        0xf8, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
    ),
    BLE_UUID128_INIT(  // Commands (def9)
        0xf9, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
    ),
};

// ---- NimBLE C Callbacks ----

static int gap_event_cb(struct ble_gap_event* event, void* arg)
{
    if (g_instance) {
        g_instance->onGapEvent(event->type, event);
    }
    return 0;
}

static int gattc_read_cb(uint16_t conn_handle,
                          const struct ble_gatt_error* error,
                          struct ble_gatt_attr* attr,
                          void* arg)
{
    if (!g_instance) return 0;

    if (error->status == 0 && attr && attr->om) {
        uint16_t len = OS_MBUF_PKTLEN(attr->om);
        uint8_t buf[512];
        if (len > sizeof(buf)) len = sizeof(buf);
        os_mbuf_copydata(attr->om, 0, len, buf);
        g_instance->onGattcReadComplete(0, buf, len);
    } else {
        g_instance->onGattcReadComplete(error->status, nullptr, 0);
    }
    return 0;
}

static int gattc_write_cb(uint16_t conn_handle,
                           const struct ble_gatt_error* error,
                           struct ble_gatt_attr* attr,
                           void* arg)
{
    if (error->status != 0) {
        ESP_LOGW(TAG, "Write failed: status=%d", error->status);
    }
    return 0;
}

static int gattc_svc_disc_cb(uint16_t conn_handle,
                              const struct ble_gatt_error* error,
                              const struct ble_gatt_svc* service,
                              void* arg)
{
    if (!g_instance) return 0;

    if (error->status == 0 && service) {
        // Service found - we just need to know it exists
        ESP_LOGI(TAG, "Service discovered");
    }
    if (error->status == BLE_HS_EDONE) {
        g_instance->onGattcDiscComplete(0);
    } else if (error->status != 0) {
        g_instance->onGattcDiscComplete(error->status);
    }
    return 0;
}

static int gattc_chr_disc_cb(uint16_t conn_handle,
                              const struct ble_gatt_error* error,
                              const struct ble_gatt_chr* chr,
                              void* arg)
{
    if (!g_instance) return 0;

    if (error->status == 0 && chr) {
        // Match UUID to our characteristic list
        for (int i = 0; i < RPI_CHAR_COUNT; i++) {
            if (ble_uuid_cmp(&chr->uuid.u, &kCharUuids[i].u) == 0) {
                g_instance->onGattcCharDiscComplete(0, i, chr->val_handle);
                break;
            }
        }
    }
    if (error->status == BLE_HS_EDONE) {
        g_instance->onGattcDiscComplete(0);
    } else if (error->status != 0 && error->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "Char discovery error: %d", error->status);
    }
    return 0;
}

static void nimble_host_task(void* param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_on_sync(void)
{
    // Ensure we have proper identity address
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
    }
    ESP_LOGI(TAG, "NimBLE host synced");
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset: reason=%d", reason);
}

// ---- RpiBleClient Implementation ----

// Shared NimBLE init guard
#include "../utils/nimble_shared.h"

void RpiBleClient::init()
{
    g_instance = this;

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    _loadSavedServer();

    // Initialize NimBLE only if not already done (e.g. by BLE Scanner app)
    if (!g_nimble_initialized) {
        nimble_port_init();

        ble_hs_cfg.sync_cb = ble_on_sync;
        ble_hs_cfg.reset_cb = ble_on_reset;

        ble_svc_gap_init();
        ble_svc_gap_device_name_set("M5Dial-RpiMon");

        nimble_port_freertos_init(nimble_host_task);
        g_nimble_initialized = true;

        // Wait for NimBLE host sync
        vTaskDelay(pdMS_TO_TICKS(500));
    } else {
        // NimBLE already running, just update callbacks
        ble_hs_cfg.sync_cb = ble_on_sync;
        ble_hs_cfg.reset_cb = ble_on_reset;
    }

    ESP_LOGI(TAG, "BLE initialized, saved server: '%s'", _savedServerName.c_str());
}

void RpiBleClient::startScan()
{
    if (_state == BleState::SCANNING || _state == BleState::CONNECTED) return;

    _foundDevices.clear();
    _state = BleState::SCANNING;
    _scanStartMs = (unsigned long)(esp_timer_get_time() / 1000);

    struct ble_gap_disc_params disc_params = {};
    disc_params.filter_duplicates = 1;
    disc_params.passive = 0;  // active scan
    disc_params.itvl = 0;     // use default
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, RPI_BLE_SCAN_DURATION_SEC * 1000,
                          &disc_params, gap_event_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Scan start failed: %d", rc);
        _state = BleState::DISCONNECTED;
    } else {
        ESP_LOGI(TAG, "Scan started");
    }
}

bool RpiBleClient::isScanComplete()
{
    if (_state != BleState::SCANNING) return true;

    unsigned long now = (unsigned long)(esp_timer_get_time() / 1000);
    if (now - _scanStartMs >= (unsigned long)(RPI_BLE_SCAN_DURATION_SEC * 1000)) {
        ble_gap_disc_cancel();
        _state = BleState::DISCONNECTED;
        ESP_LOGI(TAG, "Scan complete, found %d devices", (int)_foundDevices.size());
        return true;
    }
    return false;
}

void RpiBleClient::scanAndAutoConnect()
{
    if (_state != BleState::DISCONNECTED) return;
    if (_savedServerName.empty()) return;

    // Start a scan; in the GAP callback, we'll auto-connect if the saved server is found
    startScan();
}

bool RpiBleClient::connectToDevice(int index)
{
    if (index < 0 || index >= (int)_foundDevices.size()) return false;
    if (_state == BleState::CONNECTED) return false;

    // Stop scanning first
    ble_gap_disc_cancel();

    _state = BleState::CONNECTING;
    const FoundDevice& dev = _foundDevices[index];

    ble_addr_t addr;
    addr.type = dev.addrType;
    memcpy(addr.val, dev.addr, 6);

    ESP_LOGI(TAG, "Connecting to '%s'...", dev.name.c_str());

    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, 10000,
                             nullptr, gap_event_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Connect failed: %d", rc);
        _state = BleState::DISCONNECTED;
        return false;
    }

    // Wait for connection event (up to 15 seconds)
    // ble_gap_connect timeout is 10s, so we need to wait longer to avoid race
    for (int i = 0; i < 150 && _state == BleState::CONNECTING; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (_state != BleState::CONNECTED) {
        ESP_LOGW(TAG, "Connection timed out");
        ble_gap_conn_cancel();
        _state = BleState::DISCONNECTED;
        return false;
    }

    // Discover service and characteristics
    if (!_discoverService() || !_discoverCharacteristics()) {
        ESP_LOGE(TAG, "Service discovery failed");
        ble_gap_terminate(_connHandle, BLE_ERR_REM_USER_CONN_TERM);
        _state = BleState::DISCONNECTED;
        return false;
    }

    // Save server name to NVS
    _saveSavedServer(dev.name);

    ESP_LOGI(TAG, "Connected and ready");
    return true;
}

void RpiBleClient::disconnect()
{
    if (_connHandle != 0xFFFF) {
        ble_gap_terminate(_connHandle, BLE_ERR_REM_USER_CONN_TERM);
    }
    _state = BleState::DISCONNECTED;
    _connHandle = 0xFFFF;
    memset(_charHandles, 0, sizeof(_charHandles));
}

void RpiBleClient::forgetDevice()
{
    disconnect();
    _clearSavedServer();
    _foundDevices.clear();
    ESP_LOGI(TAG, "Device forgotten");
}

bool RpiBleClient::hasSavedServer() const
{
    return !_savedServerName.empty();
}

std::string RpiBleClient::getSavedServerName() const
{
    return _savedServerName;
}

std::string RpiBleClient::getFoundDeviceName(int index) const
{
    if (index < 0 || index >= (int)_foundDevices.size()) return "";
    return _foundDevices[index].name;
}

// ---- GATT Operations ----

bool RpiBleClient::_discoverService()
{
    _discDone = false;

    int rc = ble_gattc_disc_svc_by_uuid(_connHandle,
                                         &kServiceUuid.u,
                                         gattc_svc_disc_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Service disc start failed: %d", rc);
        return false;
    }

    // Wait for completion
    for (int i = 0; i < 50 && !_discDone; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return _discDone;
}

bool RpiBleClient::_discoverCharacteristics()
{
    _discDone = false;

    // Discover all characteristics of the service
    // Use a wide handle range since we've confirmed the service exists
    int rc = ble_gattc_disc_all_chrs(_connHandle, 1, 0xFFFF,
                                      gattc_chr_disc_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Char disc start failed: %d", rc);
        return false;
    }

    // Wait for completion
    for (int i = 0; i < 50 && !_discDone; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Verify we found at least CPU characteristic
    if (_charHandles[RPI_CHAR_IDX_CPU] == 0) {
        ESP_LOGW(TAG, "CPU characteristic not found");
        return false;
    }

    ESP_LOGI(TAG, "Discovered %d characteristics", RPI_CHAR_COUNT);
    return true;
}

bool RpiBleClient::_readCharacteristic(int charIdx, char* outBuf, size_t bufSize)
{
    if (_state != BleState::CONNECTED || _connHandle == 0xFFFF) return false;
    if (charIdx < 0 || charIdx >= RPI_CHAR_COUNT) return false;
    if (_charHandles[charIdx] == 0) return false;

    _readDone = false;
    _readLen = 0;

    int rc = ble_gattc_read(_connHandle, _charHandles[charIdx],
                             gattc_read_cb, nullptr);
    if (rc != 0) {
        ESP_LOGW(TAG, "Read start failed (char %d): %d", charIdx, rc);
        return false;
    }

    // Wait for read completion (up to 3 seconds)
    for (int i = 0; i < 30 && !_readDone; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!_readDone || _readLen == 0) return false;

    size_t copyLen = (_readLen < bufSize - 1) ? _readLen : (bufSize - 1);
    memcpy(outBuf, _readBuf, copyLen);
    outBuf[copyLen] = '\0';
    return true;
}

bool RpiBleClient::_writeCharacteristic(int charIdx, const char* data, size_t len)
{
    if (_state != BleState::CONNECTED || _connHandle == 0xFFFF) return false;
    if (charIdx < 0 || charIdx >= RPI_CHAR_COUNT) return false;
    if (_charHandles[charIdx] == 0) return false;

    int rc = ble_gattc_write_flat(_connHandle, _charHandles[charIdx],
                                   data, len, gattc_write_cb, nullptr);
    if (rc != 0) {
        ESP_LOGW(TAG, "Write failed (char %d): %d", charIdx, rc);
        return false;
    }
    return true;
}

bool RpiBleClient::readAll()
{
    if (_state != BleState::CONNECTED) return false;

    char buf[512];

    if (_readCharacteristic(RPI_CHAR_IDX_CPU, buf, sizeof(buf))) {
        _parseCpuInfo(buf);
    }
    if (_readCharacteristic(RPI_CHAR_IDX_MEMORY, buf, sizeof(buf))) {
        _parseMemoryInfo(buf);
    }
    if (_readCharacteristic(RPI_CHAR_IDX_STORAGE, buf, sizeof(buf))) {
        _parseStorageInfo(buf);
    }
    if (_readCharacteristic(RPI_CHAR_IDX_NETWORK, buf, sizeof(buf))) {
        _parseNetworkInfo(buf);
    }
    if (_readCharacteristic(RPI_CHAR_IDX_SYSTEM, buf, sizeof(buf))) {
        _parseSystemInfo(buf);
    }
    if (_readCharacteristic(RPI_CHAR_IDX_SERVICES, buf, sizeof(buf))) {
        _parseServicesInfo(buf);
    }
    if (_readCharacteristic(RPI_CHAR_IDX_COMMANDS, buf, sizeof(buf))) {
        _parseCommandsInfo(buf);
    }

    return true;
}

bool RpiBleClient::sendRegistration(const char* deviceName)
{
    if (_state != BleState::CONNECTED) return false;

    // Get own BLE address
    uint8_t ownAddr[6];
    int rc = ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, ownAddr, nullptr);
    char macStr[18];
    if (rc == 0) {
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 ownAddr[5], ownAddr[4], ownAddr[3],
                 ownAddr[2], ownAddr[1], ownAddr[0]);
    } else {
        strcpy(macStr, "00:00:00:00:00:00");
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "action", "register");
    cJSON_AddStringToObject(root, "device_name", deviceName);
    cJSON_AddStringToObject(root, "mac", macStr);

    char* json = cJSON_PrintUnformatted(root);
    bool ok = _writeCharacteristic(RPI_CHAR_IDX_REGISTRATION, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ok;
}

bool RpiBleClient::sendPowerCommand(const char* action)
{
    if (_state != BleState::CONNECTED) return false;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "action", action);

    char* json = cJSON_PrintUnformatted(root);
    bool ok = _writeCharacteristic(RPI_CHAR_IDX_SYSTEM_CTRL, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ok;
}

bool RpiBleClient::sendServiceControl(const char* serviceName, const char* action)
{
    if (_state != BleState::CONNECTED) return false;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddStringToObject(root, "service", serviceName);

    char* json = cJSON_PrintUnformatted(root);
    bool ok = _writeCharacteristic(RPI_CHAR_IDX_SERVICES, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ok;
}

bool RpiBleClient::sendCommand(const char* name, const char* action)
{
    if (_state != BleState::CONNECTED) return false;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddStringToObject(root, "name", name);

    char* json = cJSON_PrintUnformatted(root);
    bool ok = _writeCharacteristic(RPI_CHAR_IDX_COMMANDS, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ok;
}

static const RpiServiceInfo kEmptyService;
static const RpiCommandInfo kEmptyCommand;

const RpiServiceInfo& RpiBleClient::getServiceInfo(int index) const
{
    if (index < 0 || index >= (int)_services.size()) return kEmptyService;
    return _services[index];
}

const RpiCommandInfo& RpiBleClient::getCommandInfo(int index) const
{
    if (index < 0 || index >= (int)_commands.size()) return kEmptyCommand;
    return _commands[index];
}

// ---- GAP Event Handler ----

void RpiBleClient::onGapEvent(int event, void* arg)
{
    struct ble_gap_event* e = (struct ble_gap_event*)arg;

    switch (event) {
        case BLE_GAP_EVENT_DISC: {
            // Check if this device advertises our service UUID
            struct ble_hs_adv_fields fields;
            int rc = ble_hs_adv_parse_fields(&fields, e->disc.data,
                                              e->disc.length_data);
            if (rc != 0) break;

            // Check for our service UUID in the advertisement
            bool hasService = false;
            if (fields.uuids128 != nullptr) {
                for (int i = 0; i < fields.num_uuids128; i++) {
                    if (ble_uuid_cmp(&fields.uuids128[i].u, &kServiceUuid.u) == 0) {
                        hasService = true;
                        break;
                    }
                }
            }
            if (!hasService) break;

            // Extract device name
            std::string name;
            if (fields.name != nullptr && fields.name_len > 0) {
                name.assign((const char*)fields.name, fields.name_len);
            } else {
                char addrStr[18];
                snprintf(addrStr, sizeof(addrStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                         e->disc.addr.val[5], e->disc.addr.val[4],
                         e->disc.addr.val[3], e->disc.addr.val[2],
                         e->disc.addr.val[1], e->disc.addr.val[0]);
                name = addrStr;
            }

            // Check for duplicates
            bool dup = false;
            for (const auto& d : _foundDevices) {
                if (memcmp(d.addr, e->disc.addr.val, 6) == 0) {
                    dup = true;
                    break;
                }
            }

            if (!dup) {
                FoundDevice dev;
                dev.name = name;
                dev.addrType = e->disc.addr.type;
                memcpy(dev.addr, e->disc.addr.val, 6);
                _foundDevices.push_back(dev);
                ESP_LOGI(TAG, "Found RPi: '%s'", name.c_str());

                // Auto-connect if this is the saved server
                // NOTE: Do NOT call connectToDevice() here - we're in GAP callback context
                // and vTaskDelay loops would block NimBLE from delivering events.
                // Instead, set a flag for the main loop to consume.
                if (!_savedServerName.empty() && name == _savedServerName) {
                    ESP_LOGI(TAG, "Found saved server, deferring auto-connect...");
                    ble_gap_disc_cancel();
                    _state = BleState::DISCONNECTED;
                    _pendingAutoConnectIdx = (int)_foundDevices.size() - 1;
                }
            }
            break;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE:
            if (_state == BleState::SCANNING) {
                _state = BleState::DISCONNECTED;
                ESP_LOGI(TAG, "Scan complete (event)");
            }
            break;

        case BLE_GAP_EVENT_CONNECT:
            if (e->connect.status == 0) {
                _connHandle = e->connect.conn_handle;
                _state = BleState::CONNECTED;
                ESP_LOGI(TAG, "Connected, handle=%d", _connHandle);
            } else {
                ESP_LOGW(TAG, "Connection failed: %d", e->connect.status);
                _state = BleState::DISCONNECTED;
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected: reason=%d", e->disconnect.reason);
            _connHandle = 0xFFFF;
            memset(_charHandles, 0, sizeof(_charHandles));
            _state = BleState::DISCONNECTED;
            break;

        default:
            break;
    }
}

void RpiBleClient::onGattcReadComplete(int status, const uint8_t* data, uint16_t len)
{
    if (status == 0 && data && len > 0) {
        _readLen = (len < sizeof(_readBuf)) ? len : sizeof(_readBuf);
        memcpy(_readBuf, data, _readLen);
    } else {
        _readLen = 0;
    }
    _readDone = true;
}

void RpiBleClient::onGattcDiscComplete(int status)
{
    _discDone = true;
}

void RpiBleClient::onGattcCharDiscComplete(int status, uint16_t charIdx, uint16_t valHandle)
{
    if (status == 0 && charIdx < RPI_CHAR_COUNT) {
        _charHandles[charIdx] = valHandle;
        ESP_LOGI(TAG, "Char[%d] handle=%d", charIdx, valHandle);
    }
}

int RpiBleClient::consumePendingAutoConnect()
{
    int idx = _pendingAutoConnectIdx;
    _pendingAutoConnectIdx = -1;
    return idx;
}

bool RpiBleClient::discoverServiceAndChars()
{
    if (_state != BleState::CONNECTED) return false;
    if (!_discoverService() || !_discoverCharacteristics()) {
        ESP_LOGE(TAG, "Service discovery failed");
        return false;
    }
    return true;
}

// ---- NVS Helpers ----

void RpiBleClient::_loadSavedServer()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ble", NVS_READONLY, &nvs);
    if (err != ESP_OK) return;

    char buf[64] = {0};
    size_t len = sizeof(buf);
    err = nvs_get_str(nvs, "server", buf, &len);
    if (err == ESP_OK) {
        _savedServerName = buf;
    }
    nvs_close(nvs);
}

void RpiBleClient::_saveSavedServer(const std::string& name)
{
    _savedServerName = name;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ble", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return;

    nvs_set_str(nvs, "server", name.c_str());
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Saved server: '%s'", name.c_str());
}

void RpiBleClient::_clearSavedServer()
{
    _savedServerName.clear();

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ble", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return;

    nvs_erase_key(nvs, "server");
    nvs_commit(nvs);
    nvs_close(nvs);
}

// ---- JSON Parsers ----

void RpiBleClient::_parseCpuInfo(const char* json)
{
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    cJSON* item;
    item = cJSON_GetObjectItem(root, "usage");
    if (item) _cpuInfo.usage = (float)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "temp");
    if (item) _cpuInfo.temp = (float)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "freq");
    if (item) _cpuInfo.freq = (int)cJSON_GetNumberValue(item);

    cJSON_Delete(root);
}

void RpiBleClient::_parseMemoryInfo(const char* json)
{
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    cJSON* item;
    item = cJSON_GetObjectItem(root, "ram_total");
    if (item) _memInfo.ramTotal = (int)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "ram_used");
    if (item) _memInfo.ramUsed = (int)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "swap_total");
    if (item) _memInfo.swapTotal = (int)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "swap_used");
    if (item) _memInfo.swapUsed = (int)cJSON_GetNumberValue(item);

    cJSON_Delete(root);
}

void RpiBleClient::_parseStorageInfo(const char* json)
{
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    cJSON* item;
    item = cJSON_GetObjectItem(root, "total");
    if (item) _storageInfo.total = (int)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "used");
    if (item) _storageInfo.used = (int)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "free");
    if (item) _storageInfo.free = (int)cJSON_GetNumberValue(item);

    cJSON_Delete(root);
}

void RpiBleClient::_parseNetworkInfo(const char* json)
{
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    cJSON* item;
    item = cJSON_GetObjectItem(root, "wifi_ssid");
    if (item && cJSON_IsString(item)) _netInfo.wifiSsid = item->valuestring;
    item = cJSON_GetObjectItem(root, "wifi_signal");
    if (item) _netInfo.wifiSignal = (int)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "ip");
    if (item && cJSON_IsString(item)) _netInfo.ip = item->valuestring;
    item = cJSON_GetObjectItem(root, "hotspot");
    if (item) _netInfo.hotspot = cJSON_IsTrue(item);
    item = cJSON_GetObjectItem(root, "hotspot_ssid");
    if (item && cJSON_IsString(item)) _netInfo.hotspotSsid = item->valuestring;
    item = cJSON_GetObjectItem(root, "mac");
    if (item && cJSON_IsString(item)) _netInfo.mac = item->valuestring;

    cJSON_Delete(root);
}

void RpiBleClient::_parseSystemInfo(const char* json)
{
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    cJSON* item;
    item = cJSON_GetObjectItem(root, "hostname");
    if (item && cJSON_IsString(item)) _sysInfo.hostname = item->valuestring;
    item = cJSON_GetObjectItem(root, "uptime");
    if (item) _sysInfo.uptime = (unsigned long)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "os");
    if (item && cJSON_IsString(item)) _sysInfo.os = item->valuestring;
    item = cJSON_GetObjectItem(root, "kernel");
    if (item && cJSON_IsString(item)) _sysInfo.kernel = item->valuestring;
    item = cJSON_GetObjectItem(root, "time");
    if (item && cJSON_IsString(item)) _sysInfo.time = item->valuestring;
    item = cJSON_GetObjectItem(root, "platform");
    if (item && cJSON_IsString(item)) _sysInfo.platform = item->valuestring;

    cJSON_Delete(root);
}

void RpiBleClient::_parseServicesInfo(const char* json)
{
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    _services.clear();
    int count = cJSON_GetArraySize(root);
    for (int i = 0; i < count; i++) {
        cJSON* obj = cJSON_GetArrayItem(root, i);
        if (!obj) continue;
        RpiServiceInfo si;
        cJSON* item = cJSON_GetObjectItem(obj, "name");
        if (item && cJSON_IsString(item)) si.name = item->valuestring;
        item = cJSON_GetObjectItem(obj, "active");
        if (item) si.active = cJSON_IsTrue(item);
        _services.push_back(si);
    }

    cJSON_Delete(root);
}

void RpiBleClient::_parseCommandsInfo(const char* json)
{
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    _commands.clear();
    int count = cJSON_GetArraySize(root);
    for (int i = 0; i < count; i++) {
        cJSON* obj = cJSON_GetArrayItem(root, i);
        if (!obj) continue;
        RpiCommandInfo ci;
        cJSON* item = cJSON_GetObjectItem(obj, "name");
        if (item && cJSON_IsString(item)) ci.name = item->valuestring;
        item = cJSON_GetObjectItem(obj, "state");
        if (item && cJSON_IsString(item)) ci.state = item->valuestring;
        item = cJSON_GetObjectItem(obj, "exit_code");
        if (item) ci.exitCode = (int)cJSON_GetNumberValue(item);
        _commands.push_back(ci);
    }

    cJSON_Delete(root);
}
