# M5Dial RPi Monitor Firmware

M5Stack Core版 RPi Status MonitorのM5Dial（ESP32-S3 / 240x240丸型ディスプレイ）移植版。

## 前提条件

- ESP-IDF v5.1.3+
- M5Dial-UserDemo: https://github.com/m5stack/M5Dial-UserDemo

## セットアップ手順

### 1. M5Dial-UserDemoをクローン

```bash
cd m5dial-firmware
git clone https://github.com/m5stack/M5Dial-UserDemo.git _userdemo
```

### 2. アプリファイルをUserDemoに配置

```bash
# app_rpi_monitor ディレクトリをUserDemoのapps/以下にコピー
cp -r main/apps/app_rpi_monitor _userdemo/main/apps/

# ランチャーアイコンをコピー
cp main/apps/launcher/launcher_icons/icon_rpi.h \
   _userdemo/main/apps/launcher/launcher_icons/
```

### 3. ランチャーに登録

`launcher_rpi_patch.h` の手順に従って、以下のファイルを編集:

1. `_userdemo/main/apps/launcher/launcher.h` — include追加
2. `_userdemo/main/apps/launcher/launcher.cpp` — switch文にcase追加
3. `_userdemo/main/apps/launcher/launcher_render_callback.hpp` — アイコン配列に追加

### 4. BLE設定を追加

`_userdemo/sdkconfig` に以下を追加（または `sdkconfig.defaults` をマージ）:

```
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_ROLE_CENTRAL=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=n
```

### 5. ビルド & 書き込み

```bash
cd _userdemo
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## アーキテクチャ

```
app_rpi_monitor/
├── app_rpi_monitor.h/.cpp  — MOONCAKEアプリ (ライフサイクル・入力処理)
├── rpi_ble_client.h/.cpp   — NimBLE BLE Central (スキャン・接続・GATT Read/Write)
└── rpi_monitor_gui.h/.cpp  — 丸型ディスプレイUI (LovyanGFX)
```

### 入力マッピング

| M5Stack Core (旧) | M5Dial          | 動作                |
|--------------------|-----------------|---------------------|
| BtnA (左)          | ロータリー左回転 | 前の画面            |
| BtnC (右)          | ロータリー右回転 | 次の画面            |
| BtnB (決定)        | エンコーダ押下   | データ更新/接続     |
| —                  | タッチ中央       | エンコーダ押下と同等 |

### 画面一覧

| # | 画面         | 内容                                |
|---|-------------|-------------------------------------|
| 0 | Dashboard   | CPU/RAMアークゲージ + 温度 + IP      |
| 1 | CPU Detail  | 大型アークゲージ + 温度 + 周波数      |
| 2 | Memory      | RAM/Swap プログレスバー              |
| 3 | Storage     | 大型アークゲージ + 容量テキスト       |
| 4 | Network     | WiFi SSID/信号 + IP + Hotspot       |
| 5 | System      | ホスト名 + uptime + OS + カーネル    |
| 6 | Registration| BLEスキャン・デバイス選択・接続管理  |

## BLE互換性

rpi-daemon側のBLE GATTサーバーはそのまま利用可能（変更不要）。
同一のService UUID / Characteristic UUIDを使用。

## 今後のTODO

- [ ] ランチャーアイコン画像の作成（42x42px RPiロゴ）
- [ ] Services画面（systemdサービス制御）
- [ ] Power Menu画面（再起動/シャットダウン）
- [ ] アラート機能（CPU温度/使用率しきい値超過時ブザー）
- [ ] エッジスワイプ画面切替
