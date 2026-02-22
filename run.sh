#!/usr/bin/env bash
# M5Stack RPi Monitor - 起動/停止スクリプト
# Usage: ./run.sh [start|stop|restart|status|logs]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV_DIR="$SCRIPT_DIR/venv"
PIDFILE_WEB="$SCRIPT_DIR/.web.pid"
PIDFILE_DAEMON="$SCRIPT_DIR/.daemon.pid"

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

# --- Helper ---

check_venv() {
    if [[ ! -d "$VENV_DIR" ]]; then
        error "venv not found. Run ./setup.sh --all first."
        exit 1
    fi
}

get_ip() {
    hostname -I 2>/dev/null | awk '{print $1}' || echo "localhost"
}

is_running() {
    local pidfile="$1"
    if [[ -f "$pidfile" ]]; then
        local pid
        pid=$(cat "$pidfile")
        if kill -0 "$pid" 2>/dev/null; then
            return 0
        fi
        # stale pidfile
        rm -f "$pidfile"
    fi
    return 1
}

# --- Web UI ---

start_web() {
    if is_running "$PIDFILE_WEB"; then
        warn "Web UI already running (PID $(cat "$PIDFILE_WEB"))"
        return
    fi

    check_venv
    info "Starting Web UI on port 5000..."
    source "$VENV_DIR/bin/activate"
    nohup "$VENV_DIR/bin/python3" "$SCRIPT_DIR/rpi-web/app.py" \
        >> "$SCRIPT_DIR/logs/web.log" 2>&1 &
    echo $! > "$PIDFILE_WEB"
    ok "Web UI started (PID $!) - http://$(get_ip):5000"
}

stop_web() {
    if is_running "$PIDFILE_WEB"; then
        local pid
        pid=$(cat "$PIDFILE_WEB")
        info "Stopping Web UI (PID $pid)..."
        kill "$pid" 2>/dev/null || true
        # 終了を待つ
        for _ in {1..10}; do
            if ! kill -0 "$pid" 2>/dev/null; then
                break
            fi
            sleep 0.5
        done
        # まだ生きてたら SIGKILL
        if kill -0 "$pid" 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null || true
        fi
        rm -f "$PIDFILE_WEB"
        ok "Web UI stopped"
    else
        info "Web UI is not running"
    fi
}

# --- BLE Daemon ---

start_daemon() {
    # systemd service があればそちらを使う
    if systemctl list-unit-files rpi-monitor.service &>/dev/null 2>&1; then
        info "Starting BLE daemon via systemd..."
        sudo systemctl start rpi-monitor
        ok "BLE daemon started (systemd)"
        return
    fi

    if is_running "$PIDFILE_DAEMON"; then
        warn "BLE daemon already running (PID $(cat "$PIDFILE_DAEMON"))"
        return
    fi

    check_venv
    info "Starting BLE daemon..."
    sudo nohup "$VENV_DIR/bin/python3" "$SCRIPT_DIR/rpi-daemon/main.py" \
        >> "$SCRIPT_DIR/logs/daemon.log" 2>&1 &
    echo $! > "$PIDFILE_DAEMON"
    ok "BLE daemon started (PID $!)"
}

stop_daemon() {
    if systemctl list-unit-files rpi-monitor.service &>/dev/null 2>&1; then
        info "Stopping BLE daemon via systemd..."
        sudo systemctl stop rpi-monitor
        ok "BLE daemon stopped (systemd)"
        return
    fi

    if is_running "$PIDFILE_DAEMON"; then
        local pid
        pid=$(cat "$PIDFILE_DAEMON")
        info "Stopping BLE daemon (PID $pid)..."
        sudo kill "$pid" 2>/dev/null || true
        for _ in {1..10}; do
            if ! kill -0 "$pid" 2>/dev/null; then
                break
            fi
            sleep 0.5
        done
        rm -f "$PIDFILE_DAEMON"
        ok "BLE daemon stopped"
    else
        info "BLE daemon is not running"
    fi
}

# --- Status ---

show_status() {
    echo ""
    echo -e "${CYAN}=== M5Stack RPi Monitor Status ===${NC}"
    echo ""

    # Web UI
    if is_running "$PIDFILE_WEB"; then
        echo -e "  Web UI:     ${GREEN}running${NC} (PID $(cat "$PIDFILE_WEB")) - http://$(get_ip):5000"
    else
        echo -e "  Web UI:     ${RED}stopped${NC}"
    fi

    # BLE Daemon
    if systemctl list-unit-files rpi-monitor.service &>/dev/null 2>&1; then
        if systemctl is-active rpi-monitor &>/dev/null; then
            echo -e "  BLE Daemon: ${GREEN}running${NC} (systemd)"
        else
            echo -e "  BLE Daemon: ${RED}stopped${NC} (systemd)"
        fi
    elif is_running "$PIDFILE_DAEMON"; then
        echo -e "  BLE Daemon: ${GREEN}running${NC} (PID $(cat "$PIDFILE_DAEMON"))"
    else
        echo -e "  BLE Daemon: ${RED}stopped${NC}"
    fi

    echo ""
}

# --- Logs ---

show_logs() {
    local target="${1:-all}"
    case "$target" in
        web)
            if [[ -f "$SCRIPT_DIR/logs/web.log" ]]; then
                tail -f "$SCRIPT_DIR/logs/web.log"
            else
                error "No web log found"
            fi
            ;;
        daemon)
            if systemctl list-unit-files rpi-monitor.service &>/dev/null 2>&1; then
                journalctl -u rpi-monitor -f
            elif [[ -f "$SCRIPT_DIR/logs/daemon.log" ]]; then
                tail -f "$SCRIPT_DIR/logs/daemon.log"
            else
                error "No daemon log found"
            fi
            ;;
        all|*)
            echo -e "${CYAN}=== Recent Web UI logs ===${NC}"
            if [[ -f "$SCRIPT_DIR/logs/web.log" ]]; then
                tail -20 "$SCRIPT_DIR/logs/web.log"
            else
                echo "  (no log)"
            fi
            echo ""
            echo -e "${CYAN}=== Recent BLE Daemon logs ===${NC}"
            if systemctl list-unit-files rpi-monitor.service &>/dev/null 2>&1; then
                journalctl -u rpi-monitor -n 20 --no-pager 2>/dev/null || echo "  (no log)"
            elif [[ -f "$SCRIPT_DIR/logs/daemon.log" ]]; then
                tail -20 "$SCRIPT_DIR/logs/daemon.log"
            else
                echo "  (no log)"
            fi
            ;;
    esac
}

# --- Main ---

usage() {
    cat <<HELP
M5Stack RPi Monitor - Run Script

Usage: $0 COMMAND [OPTIONS]

Commands:
  start             Start all services (Web UI + BLE daemon)
  stop              Stop all services
  restart           Restart all services
  status            Show running status

  start web         Start Web UI only
  stop web          Stop Web UI only
  start daemon      Start BLE daemon only
  stop daemon       Stop BLE daemon only

  logs              Show recent logs (all)
  logs web          Follow Web UI log
  logs daemon       Follow BLE daemon log

Examples:
  ./run.sh start            # Start everything
  ./run.sh status           # Check what's running
  ./run.sh restart          # Restart everything
  ./run.sh logs web         # Tail web UI log
HELP
}

main() {
    # logs ディレクトリ確保
    mkdir -p "$SCRIPT_DIR/logs"

    local cmd="${1:-help}"
    local target="${2:-}"

    case "$cmd" in
        start)
            echo ""
            echo -e "${CYAN}╔══════════════════════════════════════╗${NC}"
            echo -e "${CYAN}║   M5Stack RPi Monitor                ║${NC}"
            echo -e "${CYAN}╚══════════════════════════════════════╝${NC}"
            echo ""
            case "$target" in
                web)    start_web ;;
                daemon) start_daemon ;;
                *)      start_daemon; start_web ;;
            esac
            echo ""
            show_status
            ;;
        stop)
            case "$target" in
                web)    stop_web ;;
                daemon) stop_daemon ;;
                *)      stop_web; stop_daemon ;;
            esac
            ;;
        restart)
            case "$target" in
                web)    stop_web;    start_web ;;
                daemon) stop_daemon; start_daemon ;;
                *)      stop_web; stop_daemon; start_daemon; start_web ;;
            esac
            echo ""
            show_status
            ;;
        status)
            show_status
            ;;
        logs)
            show_logs "$target"
            ;;
        help|-h|--help|*)
            usage
            ;;
    esac
}

main "$@"
