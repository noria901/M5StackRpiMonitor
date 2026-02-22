#include <M5Stack.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include "ble_client.h"
#include "ui.h"
#include "config.h"

BLEMonitorClient bleClient;
UI ui;

unsigned long lastReconnectAttempt = 0;
bool prevConnected = false;

void disableUnusedPeripherals() {
    // WiFi無効化（BLEのみ使用）
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    esp_wifi_deinit();

    // BLE Classic無効化（BLE LEのみ使用）
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    Serial.println("WiFi and BT Classic disabled for power saving");
}

void setup() {
    M5.begin(true, false, true, false);  // LCD, SD(off), Serial, I2C(off)
    M5.Power.begin();
    M5.Lcd.setBrightness(LCD_BRIGHTNESS);

    // スピーカー初期化
    M5.Speaker.begin();
    M5.Speaker.setVolume(3);

    Serial.println("M5Stack RPi Monitor starting...");

    // 省電力: 未使用ペリフェラルを無効化
    disableUnusedPeripherals();

    // CPU周波数を160MHzに下げる（デフォルト240MHz）
    setCpuFrequencyMhz(160);
    Serial.printf("CPU freq: %d MHz\n", getCpuFrequencyMhz());

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
        if (bleClient.getState() != BLEState::SCANNING) {
            ui.buttonAction(bleClient);
        }
    }

    if (M5.BtnC.wasPressed()) {
        ui.nextScreen();
    }

    // BLE接続状態の変化を検出して音を鳴らす
    bool nowConnected = bleClient.isConnected();
    if (nowConnected != prevConnected) {
        if (nowConnected) {
            ui.onBleConnected();
        } else {
            ui.onBleDisconnected();
        }
        prevConnected = nowConnected;
    }

    // スキャン完了チェック
    if (bleClient.getState() == BLEState::SCANNING) {
        if (bleClient.isScanComplete()) {
            // 自動再接続: 前回のサーバーが見つかったら自動接続
            if (ui.getCurrentScreen() != Screen::REGISTRATION &&
                bleClient.getServerName().length() > 0) {
                for (int i = 0; i < bleClient.getFoundDeviceCount(); i++) {
                    if (bleClient.getFoundDeviceName(i) == bleClient.getServerName()) {
                        bleClient.connectToServer(bleClient.getFoundDevice(i));
                        break;
                    }
                }
            }
            ui.setNeedsRedraw();
        }
    }

    // 自動再接続 (Registration画面以外で切断中の場合)
    if (!bleClient.isConnected() &&
        bleClient.getState() != BLEState::SCANNING &&
        ui.getCurrentScreen() != Screen::REGISTRATION &&
        bleClient.getServerName().length() > 0) {
        unsigned long now = millis();
        if (now - lastReconnectAttempt > BLE_RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            // 前回接続していたサーバーへの再接続を試みる
            bleClient.scan();
        }
    }

    // UI更新
    ui.update(bleClient);

    // アラートチェック（UI更新後、最新データで判定）
    ui.checkAlerts(bleClient);

    delay(50);
}
