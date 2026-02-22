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
- Web UIからワンクリックでM5Stackファームウェア書き込み

## Architecture

```
┌─────────────┐     BLE GATT      ┌──────────────────┐
│  M5Stack    │◄──────────────────│  Raspberry Pi    │
│  Core       │   Read/Notify     │                  │
│  (Client)   │                   │  rpi-daemon      │
│             │                   │  (BLE Server)    │
└──────┬──────┘                   │                  │
       │ USB                      │  rpi-web         │
       └──────────────────────────│  (Flask :5000)   │
         Firmware Flash           └──────────────────┘
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
    ├── index.html       デバイス管理画面
    └── flash.html       ファームウェア書き込み画面
```

## Requirements

- Raspberry Pi (Bluetooth対応, 3B+以降推奨)
- Raspberry Pi OS (Bookworm以降推奨)
- Python 3.9+
- BlueZ (Bluetooth stack)
- M5Stack Core (ESP32)
- USBケーブル (Type-C, M5Stack書き込み用)

## Setup - クイックスタート

### セットアップスクリプト (推奨)

全部まとめてやるなら:

```bash
git clone <repository-url> ~/M5StackRpiMonitor
cd ~/M5StackRpiMonitor

# フルセットアップ (system deps + Bluetooth + daemon + Web UI)
sudo ./setup.sh --all
```

個別にやることもできます:

```bash
./setup.sh --web       # Web UIだけセットアップ
./setup.sh --daemon    # BLEデーモンだけセットアップ
./setup.sh --flash     # M5StackにUSBでファームウェア書き込み
./setup.sh --help      # ヘルプ表示
```

`sudo ./setup.sh --all` を実行したら、あとは `./run.sh start` で起動するだけです。

### 起動 / 停止

```bash
./run.sh start              # 全サービス起動 (Web UI + BLE daemon)
./run.sh stop               # 全サービス停止
./run.sh restart             # 再起動
./run.sh status             # 稼働状態を確認

./run.sh start web          # Web UIだけ起動
./run.sh start daemon       # BLEデーモンだけ起動
./run.sh stop web           # Web UIだけ停止

./run.sh logs               # 直近ログ表示
./run.sh logs web           # Web UIログをtail -f
./run.sh logs daemon        # BLEデーモンログをtail -f
```

---

### 手動セットアップ (詳細)

### 1. RPiの前提パッケージをインストール

```bash
# Bluetooth関連
sudo apt update
sudo apt install -y bluez bluetooth pi-bluetooth

# Bluetoothサービスの有効化
sudo systemctl enable bluetooth
sudo systemctl start bluetooth

# 現在のユーザーをbluetoothグループに追加 (再ログイン必要)
sudo usermod -aG bluetooth $USER

# Python venv用
sudo apt install -y python3-venv python3-pip
```

### 2. プロジェクトのクローンとPython環境構築

```bash
git clone <repository-url> ~/M5StackRpiMonitor
cd ~/M5StackRpiMonitor

# Python仮想環境を作成
python3 -m venv venv
source venv/bin/activate
```

### 3. M5Stackファームウェアの書き込み (2通りの方法)

#### 方法A: Web UIからワンクリック書き込み (推奨)

M5StackをRPiにUSB接続した状態で、Web UI から操作できます。

```bash
# Web UIの依存パッケージインストール (PlatformIO含む)
cd ~/M5StackRpiMonitor
source venv/bin/activate
pip install -r rpi-web/requirements.txt

# Web UI起動
python3 rpi-web/app.py
```

1. ブラウザで `http://<RPi-IP>:5000/flash` を開く
2. M5StackをUSBで接続
3. "Refresh" でポートを検出
4. **"Flash Firmware"** をクリック
5. ビルド → 書き込みが自動で実行される (ログがリアルタイム表示)

#### 方法B: コマンドラインから書き込み

```bash
cd ~/M5StackRpiMonitor
source venv/bin/activate
pip install platformio

# M5StackをUSBで接続してから
cd m5stack-firmware
pio run -t upload        # ビルド & 書き込み
pio run -t monitor       # シリアルモニタで動作確認
```

### 4. RPi BLEデーモンのセットアップ

```bash
cd ~/M5StackRpiMonitor
source venv/bin/activate
pip install -r rpi-daemon/requirements.txt

# 動作テスト (フォアグラウンド実行)
sudo $(which python3) rpi-daemon/main.py
# Ctrl+C で停止
```

#### systemdサービスとして常駐させる

```bash
# サービスファイルをコピー (パスを環境に合わせて編集)
sudo cp rpi-daemon/rpi-monitor.service /etc/systemd/system/

# ExecStartのパスを確認・修正
sudo systemctl edit rpi-monitor --force
# [Service]
# ExecStart=/home/<user>/M5StackRpiMonitor/venv/bin/python3 /home/<user>/M5StackRpiMonitor/rpi-daemon/main.py
# WorkingDirectory=/home/<user>/M5StackRpiMonitor/rpi-daemon

# 有効化 & 起動
sudo systemctl daemon-reload
sudo systemctl enable rpi-monitor
sudo systemctl start rpi-monitor

# 状態確認
sudo systemctl status rpi-monitor
journalctl -u rpi-monitor -f    # ログ確認
```

### 5. RPi Web UIの起動

```bash
cd ~/M5StackRpiMonitor
source venv/bin/activate
pip install -r rpi-web/requirements.txt
python3 rpi-web/app.py
```

ブラウザで `http://<RPi-IP>:5000` にアクセス。

| ページ | パス | 機能 |
|--------|------|------|
| Devices | `/` | 登録済みM5Stack一覧、リネーム、削除 |
| Flash | `/flash` | ファームウェア書き込み (USB接続) |

### 6. M5Stackとの接続

1. M5Stackの電源を入れる
2. ボタン操作で **Register** 画面へ移動 (BtnC を数回押す)
3. **BtnB** を押してBLEスキャン開始
4. "RPi-Monitor" が見つかったら選択して **BtnB** で接続
5. 接続成功 → Dashboard画面にRPiの情報が表示される

## M5Stack操作方法

### ボタン
| ボタン | 機能 |
|--------|------|
| BtnA (左) | 前の画面へ |
| BtnB (中央) | 決定 / データ更新 |
| BtnC (右) | 次の画面へ |

### 画面一覧
| # | Screen | Description |
|---|--------|-------------|
| 1 | Dashboard | CPU/RAM/Storage概要 + 温度・IP |
| 2 | CPU Detail | 使用率、温度、周波数 |
| 3 | Memory | RAM/Swap使用量のバーグラフ |
| 4 | Storage | ディスク使用量 |
| 5 | Network | WiFi SSID、信号強度、IP、MAC、Hotspot状態 |
| 6 | System | ホスト名、稼働時間、OS、カーネル |
| 7 | Register | BLEスキャン・デバイス選択・接続・登録 |

## トラブルシューティング

### BLEが動かない
```bash
# Bluetoothアダプタの状態確認
hciconfig
bluetoothctl show

# BlueZサービス再起動
sudo systemctl restart bluetooth
```

### M5Stackのシリアルポートが検出されない
```bash
# 接続中のUSBデバイス確認
lsusb
ls -la /dev/ttyUSB* /dev/ttyACM*

# ドライバ確認 (CP2104/CH340)
dmesg | tail -20

# 権限追加
sudo usermod -aG dialout $USER
# 再ログインが必要
```

### Web UIにアクセスできない
```bash
# ファイアウォール確認
sudo ufw status
sudo ufw allow 5000/tcp   # 必要なら

# プロセス確認
ss -tlnp | grep 5000
```

## License

MIT
