#include "app_ble_scanner.h"
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cstring>

// NimBLE headers
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>

using namespace MOONCAKE::USER_APP;

static const char* TAG = "BLEScan";

// RPi Monitor service UUID for filtering
static const ble_uuid128_t kRpiServiceUuid = BLE_UUID128_INIT(
    0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
);

// Scan duration in seconds
#define SCAN_DURATION_SEC 5

// Shared NimBLE init guard
#include "../utils/nimble_shared.h"

// Global pointer for C callback
static BLE_Scanner* g_scanner = nullptr;

// ---- NimBLE callbacks ----

static void ble_scan_on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ESP_LOGI(TAG, "NimBLE synced");
}

static void ble_scan_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset: %d", reason);
}

static void nimble_host_task_fn(void* param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static int gap_disc_cb(struct ble_gap_event* event, void* arg)
{
    if (!g_scanner) return 0;

    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            struct ble_hs_adv_fields fields;
            int rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                              event->disc.length_data);
            if (rc != 0) break;

            // Check for RPi Monitor service UUID
            bool has_service = false;
            if (fields.uuids128 != nullptr) {
                for (int i = 0; i < fields.num_uuids128; i++) {
                    if (ble_uuid_cmp(&fields.uuids128[i].u, &kRpiServiceUuid.u) == 0) {
                        has_service = true;
                        break;
                    }
                }
            }
            if (!has_service) break;

            // Extract name
            std::string name;
            if (fields.name != nullptr && fields.name_len > 0) {
                name.assign((const char*)fields.name, fields.name_len);
            } else {
                char addr_str[18];
                snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                         event->disc.addr.val[5], event->disc.addr.val[4],
                         event->disc.addr.val[3], event->disc.addr.val[2],
                         event->disc.addr.val[1], event->disc.addr.val[0]);
                name = addr_str;
            }

            // Check duplicates
            bool dup = false;
            for (const auto& d : g_scanner->_data.devices) {
                if (memcmp(d.addr, event->disc.addr.val, 6) == 0) {
                    dup = true;
                    break;
                }
            }

            if (!dup) {
                BLE_SCANNER::FoundDevice dev;
                dev.name = name;
                dev.addr_type = event->disc.addr.type;
                memcpy(dev.addr, event->disc.addr.val, 6);
                dev.rssi = event->disc.rssi;
                g_scanner->_data.devices.push_back(dev);
                ESP_LOGI(TAG, "Found: '%s' RSSI=%d", name.c_str(), event->disc.rssi);
            }
            break;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE:
            g_scanner->_data.scanning = false;
            g_scanner->_data.scan_done = true;
            ESP_LOGI(TAG, "Scan complete, found %d devices",
                     (int)g_scanner->_data.devices.size());
            break;

        default:
            break;
    }
    return 0;
}

// ---- App implementation ----

void BLE_Scanner::onSetup()
{
    setAppName("BLE_Scanner");
    setAllowBgRunning(false);

    BLE_SCANNER::Data_t default_data;
    _data = default_data;
    _data.hal = (HAL::HAL*)getUserData();
}

void BLE_Scanner::onCreate()
{
    _log("onCreate");

    g_scanner = this;

    // Initialize NimBLE (only once)
    if (!g_nimble_initialized) {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            nvs_flash_init();
        }

        nimble_port_init();
        ble_hs_cfg.sync_cb = ble_scan_on_sync;
        ble_hs_cfg.reset_cb = ble_scan_on_reset;
        ble_svc_gap_init();
        ble_svc_gap_device_name_set("M5Dial");
        nimble_port_freertos_init(nimble_host_task_fn);
        g_nimble_initialized = true;

        // Wait for sync
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    _load_saved();
    _start_scan();
}

void BLE_Scanner::onRunning()
{
    // Encoder rotation: scroll through device list
    if (_data.hal->encoder.wasMoved(true)) {
        if (!_data.devices.empty()) {
            if (_data.hal->encoder.getDirection() < 1) {
                // CW: next
                if (_data.selected_index < (int)_data.devices.size() - 1) {
                    _data.selected_index++;
                }
            } else {
                // CCW: prev
                if (_data.selected_index > 0) {
                    _data.selected_index--;
                }
            }
        }
    }

    // Render
    if ((millis() - _data.page_update_time_count) > _data.page_update_interval) {
        _gui.renderPage(
            _data.scanning,
            _data.devices,
            _data.selected_index,
            _data.saved_name
        );
        _data.page_update_time_count = millis();
    }

    // Button press
    if (!_data.hal->encoder.btn.read()) {
        // Wait for release
        while (!_data.hal->encoder.btn.read())
            vTaskDelay(pdMS_TO_TICKS(5));

        if (_data.scanning) {
            // Ignore during scan
        } else if (_data.devices.empty()) {
            // No devices: rescan
            _start_scan();
        } else {
            // Device selected: save and show confirmation
            auto& dev = _data.devices[_data.selected_index];

            if (_data.saved_name == dev.name) {
                // Already saved this one: clear it
                _clear_saved();
            } else {
                // Save selected device
                _data.saved_name = dev.name;
                _save_selected();
            }

            // Show result briefly
            _gui.renderSaved(_data.saved_name);
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
    }
}

void BLE_Scanner::onDestroy()
{
    _log("onDestroy");
    g_scanner = nullptr;

    // Stop scan if running
    if (_data.scanning) {
        ble_gap_disc_cancel();
        _data.scanning = false;
    }
    // Note: NimBLE host task is left running (g_nimble_initialized stays true)
    // so RPi Monitor app can use it without re-init
}

void BLE_Scanner::_start_scan()
{
    _data.devices.clear();
    _data.selected_index = 0;
    _data.scanning = true;
    _data.scan_done = false;

    struct ble_gap_disc_params disc_params = {};
    disc_params.filter_duplicates = 1;
    disc_params.passive = 0;
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, SCAN_DURATION_SEC * 1000,
                          &disc_params, gap_disc_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Scan start failed: %d", rc);
        _data.scanning = false;
    } else {
        ESP_LOGI(TAG, "Scanning...");
    }
}

void BLE_Scanner::_save_selected()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ble", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %d", err);
        return;
    }
    nvs_set_str(nvs, "server", _data.saved_name.c_str());
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Saved server: '%s'", _data.saved_name.c_str());
}

void BLE_Scanner::_clear_saved()
{
    _data.saved_name.clear();

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ble", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return;
    nvs_erase_key(nvs, "server");
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Cleared saved server");
}

void BLE_Scanner::_load_saved()
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ble", NVS_READONLY, &nvs);
    if (err != ESP_OK) return;

    char buf[64] = {0};
    size_t len = sizeof(buf);
    err = nvs_get_str(nvs, "server", buf, &len);
    if (err == ESP_OK) {
        _data.saved_name = buf;
        ESP_LOGI(TAG, "Loaded saved server: '%s'", _data.saved_name.c_str());
    }
    nvs_close(nvs);
}
