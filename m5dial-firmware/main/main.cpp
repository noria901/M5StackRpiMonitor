/**
 * M5Dial RPi Monitor - Main Entry Point
 *
 * This file provides a minimal standalone entry point for reference.
 * In the actual M5Dial-UserDemo integration, the main.cpp from
 * UserDemo handles HAL initialization and launches the Launcher app.
 * The RpiMonitor app is then started from the Launcher menu.
 *
 * For standalone testing (without the full UserDemo framework),
 * this main.cpp directly initializes the HAL and runs RpiMonitor.
 */

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "apps/app_rpi_monitor/app_rpi_monitor.h"

static const char* TAG = "main";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "M5Dial RPi Monitor starting...");

    // Initialize HAL
    // Note: In the full UserDemo build, this is done by the framework.
    // For standalone mode, you need to initialize the HAL manually:
    //
    //   HAL::HAL hal;
    //   hal.init();
    //
    //   auto* app = new MOONCAKE::USER_APP::RpiMonitor;
    //   app->setAppData(&hal);
    //   app->onSetup();
    //   app->onCreate();
    //   while (!app->shouldDestroy()) {
    //       app->onRunning();
    //       vTaskDelay(pdMS_TO_TICKS(16));  // ~60fps
    //   }
    //   app->onDestroy();
    //   delete app;

    ESP_LOGI(TAG, "Build OK - integrate with M5Dial-UserDemo for full functionality");
    ESP_LOGI(TAG, "See launcher_rpi_patch.h for integration instructions");

    // Keep task alive
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
