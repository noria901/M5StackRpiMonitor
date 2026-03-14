/**
 * @brief Integration patch for M5Dial-UserDemo launcher.
 *
 * To add BLE Scanner and RPi Monitor apps to the UserDemo launcher:
 *
 * 1. In launcher.cpp, add these includes:
 *    #include "../apps/app_ble_scanner/app_ble_scanner.h"
 *    #include "../apps/app_rpi_monitor/app_rpi_monitor.h"
 *
 * 2. Replace the BLE Server app registration (case 5) with BLE Scanner:
 *    // Before:
 *    case 5: _app = new BLE_Server; break;
 *    // After:
 *    case 5: _app = new MOONCAKE::USER_APP::BLE_Scanner; break;
 *
 * 3. Add RPi Monitor as a new app (e.g. case 7 or wherever appropriate):
 *    case 7: _app = new MOONCAKE::USER_APP::RpiMonitor; break;
 *
 * 4. Add the RPi Monitor icon to launcher_icons/:
 *    - Create a 64x64 icon header (icon_rpi.h) following the same format
 *      as other launcher icons (e.g. icon_setting.h)
 *
 * 5. Update launcher icon array to include the new icon.
 *
 * Flow:
 *   BLE Scanner app: Scan → Select RPi → Save to NVS ("ble"/"server")
 *   RPi Monitor app: Read NVS → Auto-connect to saved RPi → Show dashboard
 */
