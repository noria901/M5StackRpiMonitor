#!/usr/bin/env python3
"""RPi Monitor - M5Stack device management web UI."""

import json
import glob
import os
import subprocess
import threading
from pathlib import Path

from flask import Flask, render_template, request, jsonify, Response, stream_with_context

app = Flask(__name__)

DEVICES_FILE = os.environ.get(
    "DEVICES_FILE", "/var/lib/rpi-monitor/devices.json"
)
DAEMON_CONFIG_FILE = os.environ.get(
    "DAEMON_CONFIG_FILE", "/etc/rpi-monitor/config.json"
)
FIRMWARE_DIR = os.environ.get(
    "FIRMWARE_DIR",
    str(Path(__file__).resolve().parent.parent / "m5stack-firmware"),
)

# ファームウェアビルド/フラッシュの進捗状態
flash_status: dict = {
    "state": "idle",  # idle, building, flashing, done, error
    "message": "",
    "log": [],
}
flash_lock = threading.Lock()


def load_daemon_config() -> dict:
    try:
        with open(DAEMON_CONFIG_FILE) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {"services": []}


def save_daemon_config(config: dict) -> None:
    os.makedirs(os.path.dirname(DAEMON_CONFIG_FILE), exist_ok=True)
    with open(DAEMON_CONFIG_FILE, "w") as f:
        json.dump(config, f, indent=2)


def load_devices() -> list:
    try:
        with open(DEVICES_FILE) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return []


def save_devices(devices: list) -> None:
    os.makedirs(os.path.dirname(DEVICES_FILE), exist_ok=True)
    with open(DEVICES_FILE, "w") as f:
        json.dump(devices, f, indent=2)


def detect_serial_ports() -> list[dict]:
    """Detect connected USB serial ports (ESP32/M5Stack)."""
    ports = []
    for dev in sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*")):
        info = {"port": dev, "description": ""}
        # udevadm で詳細取得
        try:
            result = subprocess.run(
                ["udevadm", "info", "--query=property", f"--name={dev}"],
                capture_output=True, text=True, timeout=5,
            )
            props = {}
            for line in result.stdout.strip().split("\n"):
                if "=" in line:
                    k, v = line.split("=", 1)
                    props[k] = v
            vendor = props.get("ID_VENDOR", "")
            model = props.get("ID_MODEL", "")
            serial = props.get("ID_SERIAL_SHORT", "")
            parts = [p for p in [vendor, model] if p]
            info["description"] = " ".join(parts) if parts else "Unknown device"
            if serial:
                info["description"] += f" ({serial})"
        except Exception:
            info["description"] = "Serial device"
        ports.append(info)
    return ports


def run_flash(port: str) -> None:
    """Build firmware with PlatformIO and flash to M5Stack (runs in thread)."""
    global flash_status

    with flash_lock:
        flash_status = {"state": "building", "message": "Building firmware...", "log": []}

    def log(msg: str) -> None:
        flash_status["log"].append(msg)
        flash_status["message"] = msg

    try:
        # Step 1: pio run (build)
        log("=== Building firmware with PlatformIO ===")
        proc = subprocess.Popen(
            ["pio", "run"],
            cwd=FIRMWARE_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        for line in iter(proc.stdout.readline, ""):
            log(line.rstrip())
        proc.wait()
        if proc.returncode != 0:
            flash_status["state"] = "error"
            log(f"Build failed (exit code {proc.returncode})")
            return

        # Step 2: pio run -t upload (flash)
        flash_status["state"] = "flashing"
        log("")
        log(f"=== Flashing to {port} ===")
        env_with_port = os.environ.copy()
        proc = subprocess.Popen(
            ["pio", "run", "-t", "upload", "--upload-port", port],
            cwd=FIRMWARE_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env=env_with_port,
        )
        for line in iter(proc.stdout.readline, ""):
            log(line.rstrip())
        proc.wait()
        if proc.returncode != 0:
            flash_status["state"] = "error"
            log(f"Flash failed (exit code {proc.returncode})")
            return

        flash_status["state"] = "done"
        log("")
        log("=== Firmware flash completed successfully! ===")

    except FileNotFoundError:
        flash_status["state"] = "error"
        log("Error: PlatformIO (pio) not found. Install with: pip install platformio")
    except Exception as e:
        flash_status["state"] = "error"
        log(f"Error: {e}")


# === Routes ===

@app.route("/")
def index():
    """Main page - list registered M5Stack devices."""
    devices = load_devices()
    return render_template("index.html", devices=devices)


@app.route("/flash")
def flash_page():
    """Firmware flash page."""
    ports = detect_serial_ports()
    return render_template("flash.html", ports=ports)


@app.route("/devices", methods=["GET"])
def api_devices():
    """API: Get all registered devices."""
    return jsonify(load_devices())


@app.route("/devices/<mac>", methods=["DELETE"])
def api_delete_device(mac: str):
    """API: Remove a registered device by MAC address."""
    devices = load_devices()
    devices = [d for d in devices if d.get("mac") != mac]
    save_devices(devices)
    return jsonify({"status": "ok"})


@app.route("/devices/<mac>/rename", methods=["POST"])
def api_rename_device(mac: str):
    """API: Rename a registered device."""
    data = request.get_json()
    if not data or "name" not in data:
        return jsonify({"status": "error", "message": "name required"}), 400

    devices = load_devices()
    for d in devices:
        if d.get("mac") == mac:
            d["device_name"] = data["name"]
            break
    save_devices(devices)
    return jsonify({"status": "ok"})


@app.route("/settings")
def settings_page():
    """Settings page - daemon configuration."""
    config = load_daemon_config()
    return render_template("settings.html", config=config)


@app.route("/api/config", methods=["GET"])
def api_get_config():
    """API: Get daemon configuration."""
    return jsonify(load_daemon_config())


@app.route("/api/config", methods=["POST"])
def api_set_config():
    """API: Update daemon configuration."""
    data = request.get_json()
    if not data:
        return jsonify({"status": "error", "message": "JSON body required"}), 400

    config = load_daemon_config()
    if "ble_name" in data:
        name = data["ble_name"].strip()
        if name:
            config["ble_name"] = name
        else:
            config.pop("ble_name", None)
    save_daemon_config(config)
    return jsonify({"status": "ok", "restart_required": True})


@app.route("/commands")
def commands_page():
    """Commands page - run and monitor custom commands."""
    config = load_daemon_config()
    commands = config.get("commands", [])
    return render_template("commands.html", commands=commands)


@app.route("/api/commands/run", methods=["POST"])
def api_commands_run():
    """API: Run or stop a command."""
    data = request.get_json()
    if not data or "name" not in data or "action" not in data:
        return jsonify({"status": "error", "message": "name and action required"}), 400

    name = data["name"]
    action = data["action"]

    result = _command_runner_exec(name, action)
    return jsonify(result)


@app.route("/api/commands/status", methods=["GET"])
def api_commands_status():
    """API: Get status of all configured commands."""
    runner = _get_command_runner()
    return jsonify(runner.get_status())


def _get_command_runner():
    """Lazy-init a shared CommandRunner from daemon config."""
    if not hasattr(app, "_command_runner"):
        import importlib
        sys_path = os.path.join(
            os.path.dirname(__file__), "..", "rpi-daemon"
        )
        import sys
        if sys_path not in sys.path:
            sys.path.insert(0, sys_path)
        from system_info import CommandRunner
        config = load_daemon_config()
        app._command_runner = CommandRunner(config.get("commands", []))
    return app._command_runner


def _command_runner_exec(name: str, action: str) -> dict:
    runner = _get_command_runner()
    if action == "run":
        return runner.run(name)
    elif action == "stop":
        return runner.stop(name)
    return {"status": "error", "message": f"invalid action '{action}'"}


@app.route("/api/serial-ports", methods=["GET"])
def api_serial_ports():
    """API: Detect connected serial ports."""
    return jsonify(detect_serial_ports())


@app.route("/api/flash", methods=["POST"])
def api_flash():
    """API: Start firmware build & flash."""
    if flash_status["state"] in ("building", "flashing"):
        return jsonify({"status": "error", "message": "Flash already in progress"}), 409

    data = request.get_json()
    if not data or "port" not in data:
        return jsonify({"status": "error", "message": "port required"}), 400

    port = data["port"]
    # ポートのバリデーション
    if not port.startswith("/dev/tty"):
        return jsonify({"status": "error", "message": "Invalid port"}), 400

    thread = threading.Thread(target=run_flash, args=(port,), daemon=True)
    thread.start()
    return jsonify({"status": "ok", "message": "Flash started"})


@app.route("/api/flash/status", methods=["GET"])
def api_flash_status():
    """API: Get current flash progress."""
    return jsonify(flash_status)


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
