#pragma once

/**
 * BLE Scanner App - scans for RPi Monitor BLE servers and saves
 * the selected device name to NVS for auto-connect by RPi Monitor app.
 *
 * Replaces app_ble_server in the UserDemo launcher.
 * Uses the same NVS namespace ("ble", key "server") as app_rpi_monitor.
 */

#include "../app.h"
#include "../../hal/hal.h"
#include "gui/gui_ble_scanner.h"
#include <vector>
#include <string>

namespace MOONCAKE {
namespace USER_APP {

namespace BLE_SCANNER {

struct FoundDevice {
    std::string name;
    uint8_t addr_type;
    uint8_t addr[6];
    int rssi;
};

struct Data_t {
    HAL::HAL* hal = nullptr;

    // Scan results
    std::vector<FoundDevice> devices;
    bool scanning = false;
    bool scan_done = false;

    // Selection state
    int selected_index = 0;
    int scroll_offset = 0;

    // Saved device
    std::string saved_name;

    // Timing
    uint32_t page_update_time_count = 0;
    uint32_t page_update_interval = 200;
};

} // namespace BLE_SCANNER

class BLE_Scanner : public APP_BASE {
private:
    const char* _tag = "BLEScan";

    void _start_scan();
    void _save_selected();
    void _clear_saved();
    void _load_saved();

public:
    BLE_SCANNER::Data_t _data;
    GUI_BLE_Scanner _gui;

    GUI_Base* getGui() override { return &_gui; }

    void onSetup() override;
    void onCreate() override;
    void onRunning() override;
    void onDestroy() override;
};

} // namespace USER_APP
} // namespace MOONCAKE
