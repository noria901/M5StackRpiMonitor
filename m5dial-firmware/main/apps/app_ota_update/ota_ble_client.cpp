#include "ota_ble_client.h"

#include <cstring>
#include <cstdio>
#include <esp_log.h>
#include <nvs_flash.h>
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

static const char* TAG = "OtaBLE";

// Global instance pointer for C callbacks
static OtaBleClient* g_ota_instance = nullptr;

// ---- UUID helpers ----

static const ble_uuid128_t kOtaServiceUuid = BLE_UUID128_INIT(
    0x70, 0x56, 0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
);

static const ble_uuid128_t kOtaCharUuids[OTA_CHAR_COUNT] = {
    BLE_UUID128_INIT(  // Info (5671)
        0x71, 0x56, 0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
    ),
    BLE_UUID128_INIT(  // Data (5672)
        0x72, 0x56, 0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
    ),
    BLE_UUID128_INIT(  // Control (5673)
        0x73, 0x56, 0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
    ),
};

// ---- NimBLE C Callbacks ----

static int ota_gap_event_cb(struct ble_gap_event* event, void* arg)
{
    if (g_ota_instance) {
        g_ota_instance->onGapEvent(event->type, event);
    }
    return 0;
}

static int ota_gattc_read_cb(uint16_t conn_handle,
                              const struct ble_gatt_error* error,
                              struct ble_gatt_attr* attr,
                              void* arg)
{
    if (!g_ota_instance) return 0;

    if (error->status == 0 && attr && attr->om) {
        uint16_t len = OS_MBUF_PKTLEN(attr->om);
        uint8_t buf[520];
        if (len > sizeof(buf)) len = sizeof(buf);
        os_mbuf_copydata(attr->om, 0, len, buf);
        g_ota_instance->onGattcReadComplete(0, buf, len);
    } else {
        g_ota_instance->onGattcReadComplete(error->status, nullptr, 0);
    }
    return 0;
}

static int ota_gattc_write_cb(uint16_t conn_handle,
                               const struct ble_gatt_error* error,
                               struct ble_gatt_attr* attr,
                               void* arg)
{
    if (error->status != 0) {
        ESP_LOGW(TAG, "Write failed: status=%d", error->status);
    }
    return 0;
}

static int ota_gattc_svc_disc_cb(uint16_t conn_handle,
                                  const struct ble_gatt_error* error,
                                  const struct ble_gatt_svc* service,
                                  void* arg)
{
    if (!g_ota_instance) return 0;

    if (error->status == 0 && service) {
        ESP_LOGI(TAG, "OTA Service discovered");
    }
    if (error->status == BLE_HS_EDONE) {
        g_ota_instance->onGattcDiscComplete(0);
    } else if (error->status != 0) {
        g_ota_instance->onGattcDiscComplete(error->status);
    }
    return 0;
}

static int ota_gattc_chr_disc_cb(uint16_t conn_handle,
                                  const struct ble_gatt_error* error,
                                  const struct ble_gatt_chr* chr,
                                  void* arg)
{
    if (!g_ota_instance) return 0;

    if (error->status == 0 && chr) {
        for (int i = 0; i < OTA_CHAR_COUNT; i++) {
            if (ble_uuid_cmp(&chr->uuid.u, &kOtaCharUuids[i].u) == 0) {
                g_ota_instance->onGattcCharDiscComplete(0, i, chr->val_handle);
                break;
            }
        }
    }
    if (error->status == BLE_HS_EDONE) {
        g_ota_instance->onGattcDiscComplete(0);
    } else if (error->status != 0 && error->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "Char discovery error: %d", error->status);
    }
    return 0;
}

static void ota_nimble_host_task(void* param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ota_ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
    }
    ESP_LOGI(TAG, "NimBLE host synced (OTA)");
}

static void ota_ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset: reason=%d", reason);
}

// ---- OtaBleClient Implementation ----

// Shared NimBLE init guard
#include "../utils/nimble_shared.h"

void OtaBleClient::init()
{
    g_ota_instance = this;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    if (!g_nimble_initialized) {
        nimble_port_init();
        ble_hs_cfg.sync_cb = ota_ble_on_sync;
        ble_hs_cfg.reset_cb = ota_ble_on_reset;
        ble_svc_gap_init();
        ble_svc_gap_device_name_set("M5Dial-OTA");
        nimble_port_freertos_init(ota_nimble_host_task);
        g_nimble_initialized = true;
        vTaskDelay(pdMS_TO_TICKS(500));
    } else {
        ble_hs_cfg.sync_cb = ota_ble_on_sync;
        ble_hs_cfg.reset_cb = ota_ble_on_reset;
    }

    ESP_LOGI(TAG, "OTA BLE initialized");
}

void OtaBleClient::startScan()
{
    if (_state == OtaBleState::SCANNING || _state == OtaBleState::CONNECTED) return;

    _foundDevices.clear();
    _state = OtaBleState::SCANNING;
    _scanStartMs = (unsigned long)(esp_timer_get_time() / 1000);

    struct ble_gap_disc_params disc_params = {};
    disc_params.filter_duplicates = 1;
    disc_params.passive = 0;
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, OTA_BLE_SCAN_DURATION_SEC * 1000,
                          &disc_params, ota_gap_event_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Scan start failed: %d", rc);
        _state = OtaBleState::DISCONNECTED;
    } else {
        ESP_LOGI(TAG, "OTA Scan started");
    }
}

bool OtaBleClient::isScanComplete()
{
    if (_state != OtaBleState::SCANNING) return true;

    unsigned long now = (unsigned long)(esp_timer_get_time() / 1000);
    if (now - _scanStartMs >= (unsigned long)(OTA_BLE_SCAN_DURATION_SEC * 1000)) {
        ble_gap_disc_cancel();
        _state = OtaBleState::DISCONNECTED;
        ESP_LOGI(TAG, "Scan complete, found %d OTA servers", (int)_foundDevices.size());
        return true;
    }
    return false;
}

bool OtaBleClient::connectToDevice(int index)
{
    if (index < 0 || index >= (int)_foundDevices.size()) return false;
    if (_state == OtaBleState::CONNECTED) return false;

    ble_gap_disc_cancel();
    _state = OtaBleState::CONNECTING;
    const OtaFoundDevice& dev = _foundDevices[index];

    ble_addr_t addr;
    addr.type = dev.addrType;
    memcpy(addr.val, dev.addr, 6);

    ESP_LOGI(TAG, "Connecting to OTA server '%s'...", dev.name.c_str());

    // Request higher MTU for faster transfer
    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, 10000,
                             nullptr, ota_gap_event_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Connect failed: %d", rc);
        _state = OtaBleState::DISCONNECTED;
        return false;
    }

    // Wait for connection
    for (int i = 0; i < 100 && _state == OtaBleState::CONNECTING; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (_state != OtaBleState::CONNECTED) {
        ESP_LOGW(TAG, "Connection timed out");
        _state = OtaBleState::DISCONNECTED;
        return false;
    }

    // Request MTU exchange for larger chunks
    ble_gattc_exchange_mtu(_connHandle, nullptr, nullptr);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Discover service and characteristics
    if (!_discoverService() || !_discoverCharacteristics()) {
        ESP_LOGE(TAG, "OTA service discovery failed");
        ble_gap_terminate(_connHandle, BLE_ERR_REM_USER_CONN_TERM);
        _state = OtaBleState::DISCONNECTED;
        return false;
    }

    ESP_LOGI(TAG, "Connected to OTA server");
    return true;
}

void OtaBleClient::disconnect()
{
    if (_connHandle != 0xFFFF) {
        ble_gap_terminate(_connHandle, BLE_ERR_REM_USER_CONN_TERM);
    }
    _state = OtaBleState::DISCONNECTED;
    _connHandle = 0xFFFF;
    memset(_charHandles, 0, sizeof(_charHandles));
}

std::string OtaBleClient::getFoundDeviceName(int index) const
{
    if (index < 0 || index >= (int)_foundDevices.size()) return "";
    return _foundDevices[index].name;
}

// ---- OTA Operations ----

bool OtaBleClient::readFirmwareInfo(OtaFirmwareInfo& info)
{
    uint16_t len = 0;
    uint8_t buf[512];
    if (!_readCharacteristic(OTA_CHAR_IDX_INFO, buf, sizeof(buf), len)) return false;

    buf[len] = '\0';
    cJSON* root = cJSON_Parse((const char*)buf);
    if (!root) return false;

    cJSON* item;
    item = cJSON_GetObjectItem(root, "size");
    if (item) info.size = (uint32_t)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "chunk_size");
    if (item) info.chunkSize = (uint16_t)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "name");
    if (item && cJSON_IsString(item)) info.name = item->valuestring;

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Firmware: '%s', size=%lu, chunk=%d",
             info.name.c_str(), (unsigned long)info.size, info.chunkSize);
    return info.size > 0 && info.chunkSize > 0;
}

bool OtaBleClient::setOffset(uint32_t offset)
{
    uint8_t buf[4];
    buf[0] = (uint8_t)(offset & 0xFF);
    buf[1] = (uint8_t)((offset >> 8) & 0xFF);
    buf[2] = (uint8_t)((offset >> 16) & 0xFF);
    buf[3] = (uint8_t)((offset >> 24) & 0xFF);
    return _writeCharacteristic(OTA_CHAR_IDX_CONTROL, buf, 4);
}

bool OtaBleClient::readDataChunk(uint8_t* buf, uint16_t bufSize, uint16_t& outLen)
{
    return _readCharacteristic(OTA_CHAR_IDX_DATA, buf, bufSize, outLen);
}

// ---- GATT Operations ----

bool OtaBleClient::_discoverService()
{
    _discDone = false;

    int rc = ble_gattc_disc_svc_by_uuid(_connHandle,
                                         &kOtaServiceUuid.u,
                                         ota_gattc_svc_disc_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Service disc start failed: %d", rc);
        return false;
    }

    for (int i = 0; i < 50 && !_discDone; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return _discDone;
}

bool OtaBleClient::_discoverCharacteristics()
{
    _discDone = false;

    int rc = ble_gattc_disc_all_chrs(_connHandle, 1, 0xFFFF,
                                      ota_gattc_chr_disc_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Char disc start failed: %d", rc);
        return false;
    }

    for (int i = 0; i < 50 && !_discDone; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (_charHandles[OTA_CHAR_IDX_INFO] == 0) {
        ESP_LOGW(TAG, "OTA Info characteristic not found");
        return false;
    }

    ESP_LOGI(TAG, "OTA characteristics discovered");
    return true;
}

bool OtaBleClient::_readCharacteristic(int charIdx, uint8_t* outBuf, size_t bufSize, uint16_t& outLen)
{
    if (_state != OtaBleState::CONNECTED || _connHandle == 0xFFFF) return false;
    if (charIdx < 0 || charIdx >= OTA_CHAR_COUNT) return false;
    if (_charHandles[charIdx] == 0) return false;

    _readDone = false;
    _readLen = 0;

    int rc = ble_gattc_read(_connHandle, _charHandles[charIdx],
                             ota_gattc_read_cb, nullptr);
    if (rc != 0) {
        ESP_LOGW(TAG, "Read start failed (char %d): %d", charIdx, rc);
        return false;
    }

    // Wait for read completion (up to 5 seconds for data chunks)
    for (int i = 0; i < 50 && !_readDone; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!_readDone || _readLen == 0) return false;

    uint16_t copyLen = (_readLen < bufSize) ? _readLen : (uint16_t)bufSize;
    memcpy(outBuf, _readBuf, copyLen);
    outLen = copyLen;
    return true;
}

bool OtaBleClient::_writeCharacteristic(int charIdx, const uint8_t* data, size_t len)
{
    if (_state != OtaBleState::CONNECTED || _connHandle == 0xFFFF) return false;
    if (charIdx < 0 || charIdx >= OTA_CHAR_COUNT) return false;
    if (_charHandles[charIdx] == 0) return false;

    int rc = ble_gattc_write_flat(_connHandle, _charHandles[charIdx],
                                   data, len, ota_gattc_write_cb, nullptr);
    if (rc != 0) {
        ESP_LOGW(TAG, "Write failed (char %d): %d", charIdx, rc);
        return false;
    }

    // Wait a bit for write to complete
    vTaskDelay(pdMS_TO_TICKS(30));
    return true;
}

// ---- GAP Event Handler ----

void OtaBleClient::onGapEvent(int event, void* arg)
{
    struct ble_gap_event* e = (struct ble_gap_event*)arg;

    switch (event) {
        case BLE_GAP_EVENT_DISC: {
            struct ble_hs_adv_fields fields;
            int rc = ble_hs_adv_parse_fields(&fields, e->disc.data,
                                              e->disc.length_data);
            if (rc != 0) break;

            // Check for OTA service UUID in advertisement
            bool hasOtaService = false;
            if (fields.uuids128 != nullptr) {
                for (int i = 0; i < fields.num_uuids128; i++) {
                    if (ble_uuid_cmp(&fields.uuids128[i].u, &kOtaServiceUuid.u) == 0) {
                        hasOtaService = true;
                        break;
                    }
                }
            }
            if (!hasOtaService) break;

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
                OtaFoundDevice dev;
                dev.name = name;
                dev.addrType = e->disc.addr.type;
                memcpy(dev.addr, e->disc.addr.val, 6);
                _foundDevices.push_back(dev);
                ESP_LOGI(TAG, "Found OTA server: '%s'", name.c_str());
            }
            break;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE:
            if (_state == OtaBleState::SCANNING) {
                _state = OtaBleState::DISCONNECTED;
            }
            break;

        case BLE_GAP_EVENT_CONNECT:
            if (e->connect.status == 0) {
                _connHandle = e->connect.conn_handle;
                _state = OtaBleState::CONNECTED;
                ESP_LOGI(TAG, "Connected, handle=%d", _connHandle);
            } else {
                ESP_LOGW(TAG, "Connection failed: %d", e->connect.status);
                _state = OtaBleState::DISCONNECTED;
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected: reason=%d", e->disconnect.reason);
            _connHandle = 0xFFFF;
            memset(_charHandles, 0, sizeof(_charHandles));
            _state = OtaBleState::DISCONNECTED;
            break;

        default:
            break;
    }
}

void OtaBleClient::onGattcReadComplete(int status, const uint8_t* data, uint16_t len)
{
    if (status == 0 && data && len > 0) {
        _readLen = (len < sizeof(_readBuf)) ? len : sizeof(_readBuf);
        memcpy(_readBuf, data, _readLen);
    } else {
        _readLen = 0;
    }
    _readDone = true;
}

void OtaBleClient::onGattcDiscComplete(int status)
{
    _discDone = true;
}

void OtaBleClient::onGattcCharDiscComplete(int status, uint16_t charIdx, uint16_t valHandle)
{
    if (status == 0 && charIdx < OTA_CHAR_COUNT) {
        _charHandles[charIdx] = valHandle;
        ESP_LOGI(TAG, "OTA Char[%d] handle=%d", charIdx, valHandle);
    }
}
