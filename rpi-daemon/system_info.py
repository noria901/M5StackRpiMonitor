"""System information collector for Raspberry Pi and Jetson Orin."""

import json
import os
import subprocess
from datetime import datetime
from functools import lru_cache
import psutil


@lru_cache(maxsize=1)
def detect_platform() -> str:
    """Detect whether running on Raspberry Pi or Jetson.

    Returns "jetson" or "rpi".
    """
    try:
        with open("/proc/device-tree/model") as f:
            model = f.read().strip("\x00").strip()
            if "NVIDIA" in model.upper() or "Jetson" in model:
                return "jetson"
    except Exception:
        pass
    if os.path.exists("/etc/nv_tegra_release"):
        return "jetson"
    return "rpi"


@lru_cache(maxsize=1)
def detect_wifi_interface() -> str:
    """Detect the active WiFi interface name."""
    try:
        for iface, addrs in psutil.net_if_addrs().items():
            if iface.startswith(("wlan", "wlp")):
                return iface
    except Exception:
        pass
    return "wlan0"


def get_cpu_info() -> dict:
    """Get CPU usage, temperature, and frequency."""
    usage = psutil.cpu_percent(interval=0.5)
    temp = 0.0
    # Sensor names: RPi uses cpu_thermal/cpu-thermal, Jetson uses CPU-therm
    sensor_names = ["cpu_thermal", "cpu-thermal", "CPU-therm", "Tboard_tegra"]
    try:
        temps = psutil.sensors_temperatures()
        for name in sensor_names:
            if name in temps:
                temp = temps[name][0].current
                break
    except Exception:
        # Fallback: scan thermal zones in sysfs
        try:
            for zone in sorted(os.listdir("/sys/class/thermal")):
                if not zone.startswith("thermal_zone"):
                    continue
                ztype_path = f"/sys/class/thermal/{zone}/type"
                ztemp_path = f"/sys/class/thermal/{zone}/temp"
                try:
                    with open(ztype_path) as f:
                        ztype = f.read().strip()
                    if ztype in sensor_names:
                        with open(ztemp_path) as f:
                            temp = int(f.read().strip()) / 1000.0
                        break
                except Exception:
                    continue
            # Last resort: zone0
            if temp == 0.0:
                with open("/sys/class/thermal/thermal_zone0/temp") as f:
                    temp = int(f.read().strip()) / 1000.0
        except Exception:
            pass

    freq = 0
    cpu_freq = psutil.cpu_freq()
    if cpu_freq:
        freq = int(cpu_freq.current)

    return {"usage": round(usage, 1), "temp": round(temp, 1), "freq": freq}


def get_memory_info() -> dict:
    """Get RAM and swap usage in MB."""
    mem = psutil.virtual_memory()
    swap = psutil.swap_memory()
    return {
        "ram_total": int(mem.total / (1024 * 1024)),
        "ram_used": int(mem.used / (1024 * 1024)),
        "swap_total": int(swap.total / (1024 * 1024)),
        "swap_used": int(swap.used / (1024 * 1024)),
    }


def get_storage_info() -> dict:
    """Get root filesystem usage in MB."""
    disk = psutil.disk_usage("/")
    return {
        "total": int(disk.total / (1024 * 1024)),
        "used": int(disk.used / (1024 * 1024)),
        "free": int(disk.free / (1024 * 1024)),
    }


def get_network_info() -> dict:
    """Get WiFi, IP, and hotspot information."""
    info: dict = {
        "wifi_ssid": "",
        "wifi_signal": 0,
        "ip": "",
        "hotspot": False,
        "hotspot_ssid": "",
        "mac": "",
    }

    # Get WiFi SSID
    try:
        result = subprocess.run(
            ["iwgetid", "-r"], capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0:
            info["wifi_ssid"] = result.stdout.strip()
    except Exception:
        pass

    # Get WiFi signal strength
    wifi_iface = detect_wifi_interface()
    try:
        result = subprocess.run(
            ["iwconfig", wifi_iface], capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0:
            for line in result.stdout.split("\n"):
                if "Signal level" in line:
                    # Parse "Signal level=-XX dBm"
                    parts = line.split("Signal level=")
                    if len(parts) > 1:
                        sig_str = parts[1].split(" ")[0]
                        info["wifi_signal"] = int(sig_str)
    except Exception:
        pass

    # Get IP address (prefer wifi, then eth)
    try:
        addrs = psutil.net_if_addrs()
        for iface in [wifi_iface, "eth0", "eth1"]:
            if iface in addrs:
                for addr in addrs[iface]:
                    if addr.family.name == "AF_INET":
                        info["ip"] = addr.address
                        break
                if info["ip"]:
                    break
    except Exception:
        pass

    # Get MAC address
    try:
        addrs = psutil.net_if_addrs()
        if wifi_iface in addrs:
            for addr in addrs[wifi_iface]:
                if addr.family.name == "AF_PACKET":
                    info["mac"] = addr.address.upper()
                    break
    except Exception:
        pass

    # Check hotspot status
    try:
        result = subprocess.run(
            ["systemctl", "is-active", "hostapd"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.stdout.strip() == "active":
            info["hotspot"] = True
            # Try to get hotspot SSID from hostapd config
            try:
                with open("/etc/hostapd/hostapd.conf") as f:
                    for line in f:
                        if line.startswith("ssid="):
                            info["hotspot_ssid"] = line.strip().split("=", 1)[1]
                            break
            except Exception:
                pass
    except Exception:
        pass

    return info


def get_system_info() -> dict:
    """Get hostname, uptime, OS, and kernel information."""
    info: dict = {
        "hostname": "",
        "uptime": 0,
        "os": "",
        "kernel": "",
    }

    info["hostname"] = os.uname().nodename

    # Uptime in seconds
    try:
        with open("/proc/uptime") as f:
            info["uptime"] = int(float(f.read().split()[0]))
    except Exception:
        pass

    # OS info
    try:
        result = subprocess.run(
            ["lsb_release", "-ds"], capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0:
            info["os"] = result.stdout.strip().strip('"')
    except Exception:
        try:
            with open("/etc/os-release") as f:
                for line in f:
                    if line.startswith("PRETTY_NAME="):
                        info["os"] = line.strip().split("=", 1)[1].strip('"')
                        break
        except Exception:
            pass

    info["kernel"] = os.uname().release
    info["time"] = datetime.now().strftime("%H:%M:%S")
    info["platform"] = detect_platform()

    return info


def get_all_info() -> dict:
    """Collect all system information."""
    return {
        "cpu": get_cpu_info(),
        "memory": get_memory_info(),
        "storage": get_storage_info(),
        "network": get_network_info(),
        "system": get_system_info(),
    }


CONFIG_FILE = "/etc/rpi-monitor/config.json"


def load_config() -> dict:
    """Load daemon config file."""
    try:
        with open(CONFIG_FILE) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {"services": []}


def get_services_info(service_names: list[str]) -> list[dict]:
    """Get systemd service statuses for the given service names."""
    results = []
    for name in service_names:
        active = False
        try:
            result = subprocess.run(
                ["systemctl", "is-active", name],
                capture_output=True,
                text=True,
                timeout=5,
            )
            active = result.stdout.strip() == "active"
        except Exception:
            pass
        results.append({"name": name, "active": active})
    return results


def control_service(name: str, action: str, allowed: list[str]) -> dict:
    """Start or stop a systemd service.

    Only services listed in 'allowed' can be controlled.
    """
    if name not in allowed:
        return {"status": "error", "message": f"service '{name}' not in config"}
    if action not in ("start", "stop", "restart"):
        return {"status": "error", "message": f"invalid action '{action}'"}
    try:
        result = subprocess.run(
            ["systemctl", action, name],
            capture_output=True,
            text=True,
            timeout=15,
        )
        if result.returncode == 0:
            return {"status": "ok"}
        return {"status": "error", "message": result.stderr.strip()}
    except Exception as e:
        return {"status": "error", "message": str(e)}


def system_control(action: str) -> dict:
    """Execute a system power command (reboot or shutdown)."""
    if action not in ("reboot", "shutdown"):
        return {"status": "error", "message": f"invalid action '{action}'"}
    try:
        cmd = ["systemctl", action]
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=15
        )
        if result.returncode == 0:
            return {"status": "ok"}
        return {"status": "error", "message": result.stderr.strip()}
    except Exception as e:
        return {"status": "error", "message": str(e)}


if __name__ == "__main__":
    info = get_all_info()
    print(json.dumps(info, indent=2))
