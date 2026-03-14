#pragma once

#include "../../utils/gui_base/gui_base.h"
#include "../app_ble_scanner.h"
#include <vector>
#include <string>

// Forward declare to avoid circular include
namespace MOONCAKE { namespace USER_APP { namespace BLE_SCANNER {
    struct FoundDevice;
}}}

class GUI_BLE_Scanner : public GUI_Base {
public:
    void init() override;

    void renderPage(bool scanning,
                    const std::vector<MOONCAKE::USER_APP::BLE_SCANNER::FoundDevice>& devices,
                    int selected_index,
                    const std::string& saved_name);

    void renderSaved(const std::string& saved_name);
};
