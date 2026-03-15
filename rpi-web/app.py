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

# Project root (parent of rpi-web/)
PROJECT_ROOT = Path(__file__).resolve().parent.parent

# Firmware directories per target
FIRMWARE_TARGETS: dict[str, dict] = {
    "m5stack": {
        "label": "M5Stack Core (PlatformIO)",
        "dir": os.environ.get(
            "FIRMWARE_DIR",
            str(PROJECT_ROOT / "m5stack-firmware"),
        ),
        "build_tool": "pio",
    },
    "m5dial": {
        "label": "M5Dial (ESP-IDF)",
        "dir": str(PROJECT_ROOT / "m5dial-firmware"),
        "build_tool": "idf",
    },
}

# Legacy alias
FIRMWARE_DIR = FIRMWARE_TARGETS["m5stack"]["dir"]

# ファームウェアビルド/フラッシュの進捗状態
flash_status: dict = {
    "state": "idle",  # idle, building, flashing, done, error
    "message": "",
    "log": [],
}
flash_lock = threading.Lock()

# デプロイ進捗状態
deploy_status: dict = {
    "state": "idle",  # idle, pulling, building, flashing, done, error
    "message": "",
    "log": [],
}
deploy_lock = threading.Lock()


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


def _get_firmware_dir(target: str) -> str | None:
    """Return the firmware directory for a target, or None if invalid."""
    t = FIRMWARE_TARGETS.get(target)
    return t["dir"] if t else None


# --- ESP-IDF environment helpers ---

# Default install location
IDF_DEFAULT_DIR = os.environ.get("IDF_PATH", os.path.expanduser("~/esp/esp-idf"))
IDF_VERSION = os.environ.get("IDF_VERSION", "v5.1.3")

# Setup progress (shared state for polling)
idf_setup_status: dict = {
    "state": "idle",  # idle, installing, done, error
    "message": "",
    "log": [],
}


def _find_idf_export_sh() -> str | None:
    """Find export.sh in known ESP-IDF locations."""
    candidates = [
        os.path.join(IDF_DEFAULT_DIR, "export.sh"),
        os.path.expanduser("~/esp/esp-idf/export.sh"),
        "/opt/esp-idf/export.sh",
    ]
    # Also check IDF_PATH env
    idf_path = os.environ.get("IDF_PATH")
    if idf_path:
        candidates.insert(0, os.path.join(idf_path, "export.sh"))
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


def _is_idf_installed() -> bool:
    """Check if ESP-IDF is installed and export.sh exists."""
    return _find_idf_export_sh() is not None


def _install_idf(log_fn) -> bool:
    """Download and install ESP-IDF. Returns True on success."""
    idf_dir = IDF_DEFAULT_DIR
    parent_dir = os.path.dirname(idf_dir)
    os.makedirs(parent_dir, exist_ok=True)

    # Step 1: Clone ESP-IDF
    log_fn(f"Cloning ESP-IDF {IDF_VERSION} into {idf_dir} ...")
    proc = subprocess.Popen(
        [
            "git", "clone", "--branch", IDF_VERSION, "--depth", "1",
            "--recursive", "--shallow-submodules",
            "https://github.com/espressif/esp-idf.git",
            idf_dir,
        ],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    for line in iter(proc.stdout.readline, ""):
        log_fn(line.rstrip())
    proc.wait()
    if proc.returncode != 0:
        log_fn(f"git clone failed (exit code {proc.returncode})")
        return False

    # Step 2: Run install.sh
    log_fn("")
    log_fn("Running install.sh (downloading toolchain, this may take a while) ...")
    proc = subprocess.Popen(
        ["bash", os.path.join(idf_dir, "install.sh"), "esp32s3"],
        cwd=idf_dir,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    for line in iter(proc.stdout.readline, ""):
        log_fn(line.rstrip())
    proc.wait()
    if proc.returncode != 0:
        log_fn(f"install.sh failed (exit code {proc.returncode})")
        return False

    log_fn("")
    log_fn("ESP-IDF installation complete.")
    return True


def _ensure_idf_installed(log_fn) -> bool:
    """Check if ESP-IDF is installed; install it if not. Returns True if ready."""
    if _is_idf_installed():
        log_fn("ESP-IDF found.")
        return True

    log_fn("ESP-IDF not found. Starting automatic installation...")
    log_fn("")
    return _install_idf(log_fn)


def _run_shell_with_idf(shell_cmd: str, cwd: str, log_fn) -> int:
    """Run a shell command with ESP-IDF environment sourced."""
    export_sh = _find_idf_export_sh()
    if not export_sh:
        log_fn("Error: ESP-IDF export.sh not found")
        return 1
    full_cmd = f'. "{export_sh}" > /dev/null 2>&1 && {shell_cmd}'
    proc = subprocess.Popen(
        ["bash", "-c", full_cmd],
        cwd=cwd,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    for line in iter(proc.stdout.readline, ""):
        log_fn(line.rstrip())
    proc.wait()
    return proc.returncode


def _run_build(target: str, firmware_dir: str, log_fn) -> int:
    """Run the build command for a target. Returns exit code."""
    t = FIRMWARE_TARGETS[target]
    if t["build_tool"] == "pio":
        cmd = ["pio", "run"]
        proc = subprocess.Popen(
            cmd, cwd=firmware_dir,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        for line in iter(proc.stdout.readline, ""):
            log_fn(line.rstrip())
        proc.wait()
        return proc.returncode
    else:
        return _run_shell_with_idf("idf.py build", firmware_dir, log_fn)


def _run_flash_cmd(target: str, firmware_dir: str, port: str, log_fn) -> int:
    """Run the flash command for a target. Returns exit code."""
    t = FIRMWARE_TARGETS[target]
    if t["build_tool"] == "pio":
        cmd = ["pio", "run", "-t", "upload", "--upload-port", port]
        proc = subprocess.Popen(
            cmd, cwd=firmware_dir,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        for line in iter(proc.stdout.readline, ""):
            log_fn(line.rstrip())
        proc.wait()
        return proc.returncode
    else:
        return _run_shell_with_idf(
            f"idf.py -p {port} flash", firmware_dir, log_fn,
        )


def run_flash(port: str, target: str = "m5stack") -> None:
    """Build firmware and flash to device (runs in thread)."""
    global flash_status

    firmware_dir = _get_firmware_dir(target)
    if not firmware_dir:
        flash_status = {"state": "error", "message": f"Unknown target: {target}", "log": []}
        return

    with flash_lock:
        flash_status = {"state": "setup", "message": "Checking build environment...", "log": []}

    def log(msg: str) -> None:
        flash_status["log"].append(msg)
        flash_status["message"] = msg

    try:
        target_label = FIRMWARE_TARGETS[target]["label"]

        # ESP-IDF targets need idf.py
        if FIRMWARE_TARGETS[target]["build_tool"] == "idf":
            log("=== Checking ESP-IDF environment ===")
            if not _ensure_idf_installed(log):
                flash_status["state"] = "error"
                log("ESP-IDF setup failed.")
                return
            log("")

        flash_status["state"] = "building"
        log(f"=== Building {target_label} firmware ===")

        rc = _run_build(target, firmware_dir, log)
        if rc != 0:
            flash_status["state"] = "error"
            log(f"Build failed (exit code {rc})")
            return

        flash_status["state"] = "flashing"
        log("")
        log(f"=== Flashing to {port} ===")

        rc = _run_flash_cmd(target, firmware_dir, port, log)
        if rc != 0:
            flash_status["state"] = "error"
            log(f"Flash failed (exit code {rc})")
            return

        flash_status["state"] = "done"
        log("")
        log("=== Firmware flash completed successfully! ===")

    except FileNotFoundError as e:
        flash_status["state"] = "error"
        tool = FIRMWARE_TARGETS[target]["build_tool"]
        log(f"Error: {tool} not found. {e}")
    except Exception as e:
        flash_status["state"] = "error"
        log(f"Error: {e}")


# === Git helpers for Deploy ===

def _git_run(*args: str, cwd: str | None = None) -> subprocess.CompletedProcess:
    """Run a git command and return the result."""
    return subprocess.run(
        ["git"] + list(args),
        cwd=cwd or str(PROJECT_ROOT),
        capture_output=True, text=True, timeout=60,
    )


def get_git_branches() -> list[dict]:
    """Get local + remote branches."""
    result = _git_run("branch", "-a", "--format=%(refname:short)")
    if result.returncode != 0:
        return []
    branches = []
    seen = set()
    for line in result.stdout.strip().split("\n"):
        name = line.strip()
        if not name:
            continue
        # Normalize remote tracking refs: origin/main -> main
        short = name.replace("origin/", "") if name.startswith("origin/") else name
        if short == "HEAD":
            continue
        if short not in seen:
            seen.add(short)
            branches.append({"name": name, "short": short, "is_remote": name.startswith("origin/")})
    return branches


def get_git_info() -> dict:
    """Get current branch and latest commit info."""
    branch_result = _git_run("rev-parse", "--abbrev-ref", "HEAD")
    branch = branch_result.stdout.strip() if branch_result.returncode == 0 else "unknown"

    commit_result = _git_run("log", "-1", "--format=%h|%s")
    commit_hash = ""
    commit_msg = ""
    if commit_result.returncode == 0 and "|" in commit_result.stdout:
        parts = commit_result.stdout.strip().split("|", 1)
        commit_hash = parts[0]
        commit_msg = parts[1] if len(parts) > 1 else ""

    return {
        "branch": branch,
        "commit_hash": commit_hash,
        "commit_message": commit_msg,
    }


def run_deploy(branch: str, target: str, port: str, steps: list[str]) -> None:
    """Run deploy pipeline: pull, build, flash (runs in thread)."""
    global deploy_status

    firmware_dir = _get_firmware_dir(target)
    if not firmware_dir:
        deploy_status = {"state": "error", "message": f"Unknown target: {target}", "log": []}
        return

    with deploy_lock:
        deploy_status = {"state": "pulling", "message": "Starting deploy...", "log": []}

    def log(msg: str) -> None:
        deploy_status["log"].append(msg)
        deploy_status["message"] = msg

    try:
        # Step 1: Pull
        if "pull" in steps:
            deploy_status["state"] = "pulling"
            log(f"=== Git Pull: {branch} ===")

            # Fetch
            log("> git fetch origin")
            proc = subprocess.Popen(
                ["git", "fetch", "origin"],
                cwd=str(PROJECT_ROOT),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            )
            for line in iter(proc.stdout.readline, ""):
                log(line.rstrip())
            proc.wait()
            if proc.returncode != 0:
                deploy_status["state"] = "error"
                log(f"git fetch failed (exit code {proc.returncode})")
                return

            # Checkout branch
            log(f"> git checkout {branch}")
            proc = subprocess.Popen(
                ["git", "checkout", branch],
                cwd=str(PROJECT_ROOT),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            )
            for line in iter(proc.stdout.readline, ""):
                log(line.rstrip())
            proc.wait()
            if proc.returncode != 0:
                deploy_status["state"] = "error"
                log(f"git checkout failed (exit code {proc.returncode})")
                return

            # Pull
            log(f"> git pull origin {branch}")
            proc = subprocess.Popen(
                ["git", "pull", "origin", branch],
                cwd=str(PROJECT_ROOT),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            )
            for line in iter(proc.stdout.readline, ""):
                log(line.rstrip())
            proc.wait()
            if proc.returncode != 0:
                deploy_status["state"] = "error"
                log(f"git pull failed (exit code {proc.returncode})")
                return

            log("")

        # Step 1.5: Ensure ESP-IDF if needed
        if ("build" in steps or "flash" in steps) and FIRMWARE_TARGETS[target]["build_tool"] == "idf":
            deploy_status["state"] = "setup"
            log("=== Checking ESP-IDF environment ===")
            if not _ensure_idf_installed(log):
                deploy_status["state"] = "error"
                log("ESP-IDF setup failed.")
                return
            log("")

        # Step 2: Build
        if "build" in steps:
            deploy_status["state"] = "building"
            target_label = FIRMWARE_TARGETS[target]["label"]
            log(f"=== Build: {target_label} ===")

            rc = _run_build(target, firmware_dir, log)
            if rc != 0:
                deploy_status["state"] = "error"
                log(f"Build failed (exit code {rc})")
                return
            log("")

        # Step 3: Flash
        if "flash" in steps:
            deploy_status["state"] = "flashing"
            log(f"=== Flash to {port} ===")

            rc = _run_flash_cmd(target, firmware_dir, port, log)
            if rc != 0:
                deploy_status["state"] = "error"
                log(f"Flash failed (exit code {rc})")
                return
            log("")

        deploy_status["state"] = "done"
        log("=== Deploy completed successfully! ===")

    except FileNotFoundError as e:
        deploy_status["state"] = "error"
        log(f"Error: command not found: {e}")
    except Exception as e:
        deploy_status["state"] = "error"
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
    targets = {k: v["label"] for k, v in FIRMWARE_TARGETS.items()}
    return render_template("flash.html", ports=ports, targets=targets)


@app.route("/deploy")
def deploy_page():
    """Deploy page - git pull, build, flash."""
    branches = get_git_branches()
    git_info = get_git_info()
    ports = detect_serial_ports()
    targets = {k: v["label"] for k, v in FIRMWARE_TARGETS.items()}
    return render_template("deploy.html",
                           branches=branches, git_info=git_info,
                           ports=ports, targets=targets)


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
    if flash_status["state"] in ("setup", "building", "flashing"):
        return jsonify({"status": "error", "message": "Flash already in progress"}), 409

    data = request.get_json()
    if not data or "port" not in data:
        return jsonify({"status": "error", "message": "port required"}), 400

    port = data["port"]
    # ポートのバリデーション
    if not port.startswith("/dev/tty"):
        return jsonify({"status": "error", "message": "Invalid port"}), 400

    target = data.get("target", "m5stack")
    if target not in FIRMWARE_TARGETS:
        return jsonify({"status": "error", "message": f"Unknown target: {target}"}), 400

    thread = threading.Thread(target=run_flash, args=(port, target), daemon=True)
    thread.start()
    return jsonify({"status": "ok", "message": "Flash started"})


@app.route("/api/flash/status", methods=["GET"])
def api_flash_status():
    """API: Get current flash progress."""
    return jsonify(flash_status)


@app.route("/api/idf/status", methods=["GET"])
def api_idf_status():
    """API: Check ESP-IDF installation status."""
    return jsonify({
        "installed": _is_idf_installed(),
        "idf_dir": IDF_DEFAULT_DIR,
        "version": IDF_VERSION,
    })


# === Deploy API ===

@app.route("/api/deploy/branches", methods=["GET"])
def api_deploy_branches():
    """API: Get git branches."""
    return jsonify({
        "branches": get_git_branches(),
        "current": get_git_info(),
    })


@app.route("/api/deploy/git-info", methods=["GET"])
def api_deploy_git_info():
    """API: Get current git branch and commit info."""
    return jsonify(get_git_info())


@app.route("/api/deploy/start", methods=["POST"])
def api_deploy_start():
    """API: Start deploy pipeline."""
    if deploy_status["state"] not in ("idle", "done", "error"):
        return jsonify({"status": "error", "message": "Deploy already in progress"}), 409

    data = request.get_json()
    if not data:
        return jsonify({"status": "error", "message": "JSON body required"}), 400

    branch = data.get("branch", "")
    target = data.get("target", "m5stack")
    port = data.get("port", "")
    steps = data.get("steps", [])

    if not steps:
        return jsonify({"status": "error", "message": "No steps specified"}), 400

    if target not in FIRMWARE_TARGETS:
        return jsonify({"status": "error", "message": f"Unknown target: {target}"}), 400

    if "pull" in steps and not branch:
        return jsonify({"status": "error", "message": "Branch required for pull"}), 400

    if "flash" in steps:
        if not port:
            return jsonify({"status": "error", "message": "Port required for flash"}), 400
        if not port.startswith("/dev/tty"):
            return jsonify({"status": "error", "message": "Invalid port"}), 400

    thread = threading.Thread(
        target=run_deploy, args=(branch, target, port, steps), daemon=True
    )
    thread.start()
    return jsonify({"status": "ok", "message": "Deploy started"})


@app.route("/api/deploy/status", methods=["GET"])
def api_deploy_status():
    """API: Get current deploy progress."""
    return jsonify(deploy_status)


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
