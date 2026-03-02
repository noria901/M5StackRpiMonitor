# CLAUDE.md - M5Stack RPi Status Monitor

## Project Overview
M5Stack Core を使った Raspberry Pi / Jetson Orin ステータスモニター。
ホスト上の常駐プロセスがBLE経由でM5Stackにシステム情報を送信し、M5Stack上でリアルタイム表示する。
プラットフォームは自動検出され、BLE広告名・UI表示が動的に切り替わる。

## Architecture
```
[RPi] -- BLE GATT Server --> [M5Stack Core] Display
  |
  +-- Web UI (Flask) for M5Stack device management & firmware flash
  |
  +-- USB --> [M5Stack] Firmware flashing via PlatformIO/esptool
```

### Components
1. **m5stack-firmware/** - M5Stack Core用 Arduino/PlatformIO ファームウェア
2. **rpi-daemon/** - RPi側 BLE常駐デーモン (Python)
3. **rpi-web/** - RPi側 M5Stack登録管理 & ファームウェア書き込み Web UI (Flask)

## Tech Stack
- **M5Stack**: C++ / Arduino framework / PlatformIO
- **RPi Daemon**: Python 3 / dbus-next (BlueZ D-Bus) / psutil
- **RPi Web UI**: Python 3 / Flask / PlatformIO (firmware flash) / HTML+CSS+JS

## BLE Protocol
- GATT Service UUID: `12345678-1234-5678-1234-56789abcdef0`
- Characteristics:
  - CPU Info: `12345678-1234-5678-1234-56789abcdef1` (Read/Notify)
  - Memory Info: `12345678-1234-5678-1234-56789abcdef2` (Read/Notify)
  - Storage Info: `12345678-1234-5678-1234-56789abcdef3` (Read/Notify)
  - Network Info: `12345678-1234-5678-1234-56789abcdef4` (Read/Notify)
  - System Info: `12345678-1234-5678-1234-56789abcdef5` (Read/Notify)
  - Registration: `12345678-1234-5678-1234-56789abcdef6` (Read/Write)
  - Services: `12345678-1234-5678-1234-56789abcdef7` (Read/Write)
  - System Control: `12345678-1234-5678-1234-56789abcdef8` (Read/Write)

### Registration Characteristic
- **Write**: M5Stackが登録リクエストを送信
  ```json
  {"action": "register", "device_name": "M5Stack-RpiMon", "mac": "AA:BB:CC:DD:EE:FF"}
  ```
- **Read**: RPiが登録結果を返す
  ```json
  {"status": "ok"}
  ```
- RPi側の登録データ保存先: `/var/lib/rpi-monitor/devices.json`

### Services Characteristic
- **Read**: 設定されたsystemdサービスの状態リスト
  ```json
  [{"name": "rpi-monitor", "active": true}, {"name": "nginx", "active": false}]
  ```
- **Write**: サービスの起動/停止コマンド
  ```json
  {"action": "start", "service": "nginx"}
  ```
- デーモン設定ファイル: `/etc/rpi-monitor/config.json`
  ```json
  {"services": ["rpi-monitor"]}
  ```

### System Control Characteristic
- **Write**: M5Stackが再起動/シャットダウンコマンドを送信
  ```json
  {"action": "reboot"}
  {"action": "shutdown"}
  ```
- **Read**: RPiが実行結果を返す
  ```json
  {"status": "ok"}
  ```

## Auto-Connect (自動接続)
M5Stackは一度登録したRPiに電源再投入後も自動で再接続する。

### 仕組み
- ESP32 NVS (Preferences API) にサーバー名を永続保存
  - Namespace: `"ble"`, Key: `"server"`
- 起動時に `init()` でNVSから復元 → 既存の再接続ループが自動で動作
- 再接続間隔: 3秒 (`BLE_RECONNECT_INTERVAL`)

### 動作フロー
1. **初回登録**: Registration画面 → Scan → Connect → Register → NVSに保存
2. **再起動後**: NVSから復元 → Dashboard画面で "Auto-connecting..." 表示 → 3秒後スキャン → 自動接続
3. **接続断**: serverName保持 → 自動再スキャン → 再接続
4. **明示的切断**: Registration画面で[Select] → `forgetDevice()` → NVSクリア → 自動接続しなくなる

### 関連メソッド
- `connectToServer()`: 接続成功時にNVSへ保存
- `disconnect()`: BLE切断のみ (serverName/NVS保持、自動再接続可能)
- `forgetDevice()`: BLE切断 + serverName + NVSクリア (自動接続無効化)

## M5Stack UI Screens (Button Navigation)
- BtnA: 前の画面へ
- BtnB: 決定 / 詳細表示
- BtnC: 次の画面へ

### Screens
1. **Dashboard** - CPU/RAM/Storage概要
2. **CPU Detail** - CPU使用率、温度、周波数
3. **Memory Detail** - RAM/Swap使用量
4. **Storage Detail** - ディスク使用量
5. **Network** - WiFi/Hotspot状態、IP、信号強度
6. **System** - ホスト名、稼働時間、OS情報
7. **Services** - systemdサービスの状態表示・起動/停止操作
8. **Power** - RPi再起動/シャットダウン（確認ダイアログ付き）
9. **QR Code** - Web UI アクセス用QRコード
10. **Settings** - サウンド設定・アラート閾値
11. **Registration** - BLEペアリング/登録UI

## Port Assignments
| Port | Service | Notes |
|------|---------|-------|
| 5000 | RPi Monitor Web UI (Flask) | 本プロジェクト |
| 8080 | rpi5-pm (ダッシュボード) | 使用中 - 使わないこと |
| 3000 | Claude Code Web UI | 使用中 - 使わないこと |
| 8443 | code-server | 使用中 - 使わないこと |
| 8888 | Server Simulator (sample) | 使用中 - 使わないこと |

## Build & Development

### M5Stack Firmware
```bash
cd m5stack-firmware
pio run                  # Build
pio run -t upload        # Upload to device
pio run -t monitor       # Serial monitor
```

### RPi Daemon
```bash
cd rpi-daemon
pip install -r requirements.txt
python main.py           # Run daemon
sudo systemctl start rpi-monitor  # Run as service
```

### RPi Web UI
```bash
cd rpi-web
pip install -r requirements.txt
python app.py            # Run web server (port 5000)
# http://<RPi-IP>:5000       デバイス管理
# http://<RPi-IP>:5000/flash ファームウェア書き込み
```

### M5Stack Firmware Flash (Web UI経由)
1. M5StackをRPiにUSB接続
2. Web UI `/flash` ページを開く
3. ポート検出 → "Flash Firmware" ボタンで自動ビルド & 書き込み

## Coding Conventions
- Python: PEP 8, type hints推奨
- C++: PlatformIO formatter, Arduino naming conventions
- コミットメッセージ: 英語、imperative mood
- 日本語コメントOK

## Data Format (BLE Characteristics)
JSON encoded UTF-8 strings (max 512 bytes per characteristic):
```json
// CPU Info
{"usage": 45.2, "temp": 52.3, "freq": 1500}

// Memory Info
{"ram_total": 4096, "ram_used": 2048, "swap_total": 1024, "swap_used": 128}

// Storage Info
{"total": 32000, "used": 15000, "free": 17000}

// Network Info
{"wifi_ssid": "MyNet", "wifi_signal": -45, "ip": "192.168.1.10", "hotspot": false, "hotspot_ssid": "", "mac": "AA:BB:CC:DD:EE:FF"}

// System Info
{"hostname": "raspberrypi", "uptime": 86400, "os": "Raspberry Pi OS", "kernel": "6.1.0", "time": "14:32:05", "platform": "rpi"}

// Registration (Write: M5Stack→RPi)
{"action": "register", "device_name": "M5Stack-RpiMon", "mac": "AA:BB:CC:DD:EE:FF"}

// Registration (Read: RPi→M5Stack)
{"status": "ok"}

// Services (Read: RPi→M5Stack)
[{"name": "rpi-monitor", "active": true}]

// Services (Write: M5Stack→RPi)
{"action": "start", "service": "rpi-monitor"}

// System Control (Write: M5Stack→RPi)
{"action": "reboot"}
{"action": "shutdown"}

// System Control (Read: RPi→M5Stack)
{"status": "ok"}
```

## Web UI Routes
| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | 登録済みデバイス一覧 |
| GET | `/flash` | ファームウェア書き込みページ |
| GET | `/devices` | API: デバイス一覧 JSON |
| DELETE | `/devices/<mac>` | API: デバイス削除 |
| POST | `/devices/<mac>/rename` | API: デバイスリネーム |
| GET | `/api/serial-ports` | API: USBシリアルポート検出 |
| POST | `/api/flash` | API: ビルド & フラッシュ開始 |
| GET | `/api/flash/status` | API: フラッシュ進捗取得 |

## Configuration (config.h)
| Define | Value | Description |
|--------|-------|-------------|
| `BLE_DEVICE_NAME` | `"M5Stack-RpiMon"` | M5Stack側BLEデバイス名 |
| `BLE_SCAN_DURATION` | `5` (秒) | BLEスキャン時間 |
| `BLE_RECONNECT_INTERVAL` | `3000` (ms) | 自動再接続スキャン間隔 |
| `DATA_UPDATE_INTERVAL` | `2000` (ms) | UI データ更新間隔 |

## Testing
- Python: pytest
- M5Stack: Manual testing via serial monitor
