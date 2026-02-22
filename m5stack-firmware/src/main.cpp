#include <M5Stack.h>
#include "ble_client.h"
#include "ui.h"
#include "config.h"

BLEMonitorClient bleClient;
UI ui;

unsigned long lastReconnectAttempt = 0;

void setup() {
    M5.begin(true, false, true, true);  // LCD, SD, Serial, I2C
    M5.Power.begin();
    M5.Lcd.setBrightness(80);

    Serial.println("M5Stack RPi Monitor starting...");

    bleClient.init();
    ui.init();

    Serial.println("Setup complete. Use buttons to navigate.");
    Serial.println("BtnA=Prev  BtnB=Select  BtnC=Next");
}

void loop() {
    M5.update();

    // Button handling
    if (M5.BtnA.wasPressed()) {
        if (ui.getCurrentScreen() == Screen::REGISTRATION && bleClient.getFoundDeviceCount() > 0) {
            // Registration画面ではBtnAでリスト上移動 or キャンセル
            ui.buttonAction(bleClient);  // handled in registration context
        }
        ui.prevScreen();
    }

    if (M5.BtnB.wasPressed()) {
        ui.buttonAction(bleClient);
    }

    if (M5.BtnC.wasPressed()) {
        ui.nextScreen();
    }

    // 自動再接続 (Registration画面以外で切断中の場合)
    if (!bleClient.isConnected() &&
        ui.getCurrentScreen() != Screen::REGISTRATION &&
        bleClient.getServerName().length() > 0) {
        unsigned long now = millis();
        if (now - lastReconnectAttempt > BLE_RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            // 前回接続していたサーバーへの再接続を試みる
            bleClient.scan();
            for (int i = 0; i < bleClient.getFoundDeviceCount(); i++) {
                if (bleClient.getFoundDeviceName(i) == bleClient.getServerName()) {
                    bleClient.connectToServer(bleClient.getFoundDevice(i));
                    break;
                }
            }
        }
    }

    // UI更新
    ui.update(bleClient);

    delay(50);
}
