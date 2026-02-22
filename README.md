# M5Stack RPi Monitor

M5Stack Core を使った Raspberry Pi ステータスモニター。RPi上の常駐デーモンがBLE経由でシステム情報をM5Stackに送信し、リアルタイムで表示します。

## Features

**M5Stack側 (BLE Client)**
- CPU使用率・温度・周波数
- RAM/Swap使用量
- ストレージ使用量
- WiFi/Hotspot状態・IPアドレス・信号強度
- ホスト名・稼働時間・OS情報
- ボタン操作で画面切替 (BtnA=Prev, BtnB=Select, BtnC=Next)
- BLEデバイス登録UI

**RPi側 (BLE Server + Web UI)**
- BLE GATT Serverでシステム情報を公開
- systemdサービスとして常駐
- Web UI (Flask) でM5Stackデバイスの登録管理

## Architecture

```
┌─────────────┐     BLE GATT      ┌──────────────────┐
│  M5Stack    │◄──────────────────│  Raspberry Pi    │
│  Core       │   Read/Notify     │                  │
│  (Client)   │                   │  rpi-daemon      │
│             │                   │  (BLE Server)    │
└─────────────┘                   │                  │
                                  │  rpi-web         │
                                  │  (Flask :5000)   │
                                  └──────────────────┘
```

## Project Structure

```
m5stack-firmware/    M5Stack Core Arduino firmware (PlatformIO)
├── platformio.ini
├── include/
│   ├── config.h         定数・UUID定義
│   ├── ble_client.h     BLEクライアント
│   └── ui.h             UI画面管理
└── src/
    ├── main.cpp         メインエントリ
    ├── ble_client.cpp   BLE接続・データ取得
    └── ui.cpp           画面描画

rpi-daemon/          RPi BLEデーモン
├── main.py              エントリポイント
├── ble_server.py        BLE GATTサーバー
├── system_info.py       システム情報収集
├── requirements.txt
└── rpi-monitor.service  systemdユニットファイル

rpi-web/             RPi 管理Web UI
├── app.py               Flaskアプリ
├── requirements.txt
└── templates/
    └── index.html       デバイス管理画面
```

## Setup

### M5Stack Firmware

```bash
cd m5stack-firmware
pio run -t upload
```

### RPi Daemon

```bash
cd rpi-daemon
pip install -r requirements.txt

# テスト実行
sudo python3 main.py

# systemdサービスとして登録
sudo cp rpi-monitor.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable rpi-monitor
sudo systemctl start rpi-monitor
```

### RPi Web UI

```bash
cd rpi-web
pip install -r requirements.txt
python3 app.py
# http://<RPi-IP>:5000 でアクセス
```

## M5Stack Screens

| Screen | Description |
|--------|-------------|
| Dashboard | CPU/RAM/Storage概要 + 温度・IP |
| CPU Detail | 使用率、温度、周波数 |
| Memory | RAM/Swap使用量のバーグラフ |
| Storage | ディスク使用量 |
| Network | WiFi/Hotspot/IP/MAC |
| System | ホスト名、稼働時間、OS |
| Register | BLEスキャン・接続・登録 |

## Requirements

- M5Stack Core (ESP32)
- Raspberry Pi (Bluetooth対応, 3B+以降推奨)
- PlatformIO (M5Stackファームウェアビルド用)
- Python 3.9+ (RPi側)

## License

MIT
