"""Raspberry Pi system information collector."""

import json
import os
import subprocess
import psutil


def get_cpu_info() -> dict:
    """Get CPU usage, temperature, and frequency."""
    usage = psutil.cpu_percent(interval=0.5)
    temp = 0.0
    try:
        temps = psutil.sensors_temperatures()
        if "cpu_thermal" in temps:
            temp = temps["cpu_thermal"][0].current
        elif "cpu-thermal" in temps:
            temp = temps["cpu-thermal"][0].current
    except Exception:
        # Fallback: read from sysfs
        try:
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
    try:
        result = subprocess.run(
            ["iwconfig", "wlan0"], capture_output=True, text=True, timeout=5
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

    # Get IP address
    try:
        addrs = psutil.net_if_addrs()
        for iface in ["wlan0", "eth0", "wlan1"]:
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
        if "wlan0" in addrs:
            for addr in addrs["wlan0"]:
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


if __name__ == "__main__":
    info = get_all_info()
    print(json.dumps(info, indent=2))
