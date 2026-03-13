#pragma once

/**
 * Launcher integration patch for RPi Monitor app.
 *
 * Instructions to integrate into M5Dial-UserDemo's launcher:
 *
 * 1. In launcher.h, add this include:
 *    #include "../app_rpi_monitor/app_rpi_monitor.h"
 *
 * 2. In launcher.cpp, in the _app_open_callback() switch statement,
 *    add a new case for the RPi Monitor app:
 *
 *    case <N>:  // Choose the next available slot number
 *        app_ptr = new MOONCAKE::USER_APP::RpiMonitor;
 *        break;
 *
 * 3. In launcher_render_callback.hpp, add to the icon arrays:
 *
 *    #include "launcher_icons/icon_rpi.h"
 *
 *    // In icon_list[] array:
 *    (const uint16_t*)icon_rpi_42x42,
 *
 *    // In icon_color_list[] array:
 *    ICON_RPI_COLOR,
 *
 *    // In icon_tag_list[] array:
 *    ICON_RPI_TAG,
 *
 * 4. Update the total app count constant if needed.
 *
 * Example diff for launcher.cpp:
 * -----------------------------------------------
 *  #include "launcher.h"
 * +#include "../app_rpi_monitor/app_rpi_monitor.h"
 *
 *  void Launcher::_app_open_callback(int selectedNum)
 *  {
 *      APP_BASE* app_ptr = nullptr;
 *      switch (selectedNum) {
 *          case 0: app_ptr = new MOONCAKE::USER_APP::LCD_Test; break;
 *          case 1: app_ptr = new MOONCAKE::USER_APP::RTC_Test; break;
 *          // ... existing apps ...
 * +        case 8: app_ptr = new MOONCAKE::USER_APP::RpiMonitor; break;
 *      }
 *      if (app_ptr) {
 *          app_ptr->setAppData(_data.hal);
 *          _simple_app_manager(app_ptr);
 *          delete app_ptr;
 *      }
 *  }
 * -----------------------------------------------
 */
