#!/usr/bin/env bash
# M5Stack RPi Monitor - セットアップスクリプト
# Usage: ./setup.sh [--all | --daemon | --web | --flash]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV_DIR="$SCRIPT_DIR/venv"

# --- Colors ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

# --- Helper functions ---

check_root_for_system() {
    if [[ $EUID -ne 0 ]]; then
        warn "systemd service registration requires root. Run with sudo for full setup."
        return 1
    fi
    return 0
}

ensure_venv() {
    if [[ ! -d "$VENV_DIR" ]]; then
        info "Creating Python virtual environment..."
        python3 -m venv "$VENV_DIR"
        ok "venv created at $VENV_DIR"
    else
        ok "venv already exists"
    fi
    # shellcheck disable=SC1091
    source "$VENV_DIR/bin/activate"
}

install_system_deps() {
    info "Installing system packages..."
    if command -v apt-get &>/dev/null; then
        sudo apt-get update -qq
        sudo apt-get install -y -qq \
            bluez bluetooth pi-bluetooth \
            python3-venv python3-pip \
            python3-dev libglib2.0-dev
        ok "System packages installed"
    else
        warn "apt-get not found. Install manually: bluez bluetooth python3-venv python3-pip"
    fi
}

setup_bluetooth() {
    info "Configuring Bluetooth..."
    sudo systemctl enable bluetooth 2>/dev/null || true
    sudo systemctl start bluetooth 2>/dev/null || true

    if ! groups "$USER" | grep -q bluetooth; then
        sudo usermod -aG bluetooth "$USER"
        warn "Added $USER to bluetooth group (re-login required)"
    fi
    ok "Bluetooth configured"
}

setup_serial_permissions() {
    if ! groups "$USER" | grep -q dialout; then
        info "Adding $USER to dialout group (for USB serial access)..."
        sudo usermod -aG dialout "$USER"
        warn "Added $USER to dialout group (re-login required)"
    else
        ok "Serial port permissions OK"
    fi
}

# --- Component setup ---

setup_daemon() {
    info "=== Setting up BLE Daemon ==="
    ensure_venv
    pip install -q -r "$SCRIPT_DIR/rpi-daemon/requirements.txt"
    ok "Daemon dependencies installed"

    # systemd service
    if check_root_for_system 2>/dev/null; then
        install_daemon_service
    else
        info "Skipping systemd service (run with sudo to install)"
        info "  Manual run: source venv/bin/activate && sudo python3 rpi-daemon/main.py"
    fi
}

install_daemon_service() {
    info "Installing systemd service..."
    local service_file="/etc/systemd/system/rpi-monitor.service"
    local python_path="$VENV_DIR/bin/python3"
    local main_path="$SCRIPT_DIR/rpi-daemon/main.py"
    local work_dir="$SCRIPT_DIR/rpi-daemon"

    cat > "$service_file" <<UNIT
[Unit]
Description=RPi Monitor BLE Daemon
After=bluetooth.target
Requires=bluetooth.service

[Service]
Type=simple
ExecStart=$python_path $main_path
WorkingDirectory=$work_dir
Restart=always
RestartSec=5
User=root

[Install]
WantedBy=multi-user.target
UNIT

    systemctl daemon-reload
    systemctl enable rpi-monitor
    systemctl start rpi-monitor
    ok "rpi-monitor service installed and started"
}

setup_web() {
    info "=== Setting up Web UI ==="
    ensure_venv
    pip install -q -r "$SCRIPT_DIR/rpi-web/requirements.txt"
    ok "Web UI dependencies installed (includes PlatformIO)"
    info "Start with: source venv/bin/activate && python3 rpi-web/app.py"
    info "Then open: http://$(hostname -I 2>/dev/null | awk '{print $1}' || echo '<RPi-IP>'):5000"
}

flash_firmware() {
    info "=== Flashing M5Stack Firmware ==="
    ensure_venv

    # PlatformIO がなければインストール
    if ! command -v pio &>/dev/null && ! "$VENV_DIR/bin/pio" --version &>/dev/null 2>&1; then
        info "Installing PlatformIO..."
        pip install -q platformio
    fi
    ok "PlatformIO ready"

    # ポート自動検出
    local port=""
    for dev in /dev/ttyUSB* /dev/ttyACM*; do
        if [[ -e "$dev" ]]; then
            port="$dev"
            break
        fi
    done

    if [[ -z "$port" ]]; then
        error "No USB serial device found. Connect M5Stack via USB and retry."
        exit 1
    fi

    info "Detected port: $port"
    info "Building and flashing firmware..."
    cd "$SCRIPT_DIR/m5stack-firmware"
    "$VENV_DIR/bin/pio" run -t upload --upload-port "$port"
    ok "Firmware flashed successfully to $port"
}

# --- Main ---

usage() {
    cat <<HELP
M5Stack RPi Monitor Setup

Usage: $0 [OPTION]

Options:
  --all       Full setup (system deps + Bluetooth + daemon + web UI)
  --daemon    Set up BLE daemon only
  --web       Set up Web UI only
  --flash     Build and flash M5Stack firmware via USB
  --help      Show this help

Examples:
  sudo ./setup.sh --all           # Full initial setup
  ./setup.sh --web                # Web UI only (no sudo needed)
  ./setup.sh --flash              # Flash M5Stack via USB
HELP
}

main() {
    echo ""
    echo -e "${CYAN}╔══════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║   M5Stack RPi Monitor Setup          ║${NC}"
    echo -e "${CYAN}╚══════════════════════════════════════╝${NC}"
    echo ""

    local cmd="${1:---help}"

    case "$cmd" in
        --all)
            install_system_deps
            setup_bluetooth
            setup_serial_permissions
            setup_daemon
            setup_web
            echo ""
            ok "=== Setup complete! ==="
            info "Next steps:"
            info "  1. Re-login (if group changes were made)"
            info "  2. source venv/bin/activate && python3 rpi-web/app.py"
            info "  3. Open http://$(hostname -I 2>/dev/null | awk '{print $1}' || echo '<RPi-IP>'):5000/flash"
            info "  4. Connect M5Stack via USB and click 'Flash Firmware'"
            ;;
        --daemon)
            setup_daemon
            ;;
        --web)
            setup_web
            ;;
        --flash)
            setup_serial_permissions 2>/dev/null || true
            flash_firmware
            ;;
        --help|-h|*)
            usage
            ;;
    esac
}

main "$@"
