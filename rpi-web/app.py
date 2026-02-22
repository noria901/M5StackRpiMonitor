#!/usr/bin/env python3
"""RPi Monitor - M5Stack device management web UI."""

import json
import os
from pathlib import Path

from flask import Flask, render_template, request, jsonify, redirect, url_for

app = Flask(__name__)

DEVICES_FILE = os.environ.get(
    "DEVICES_FILE", "/var/lib/rpi-monitor/devices.json"
)


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


@app.route("/")
def index():
    """Main page - list registered M5Stack devices."""
    devices = load_devices()
    return render_template("index.html", devices=devices)


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


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
