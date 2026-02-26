"""Tests for system_info module."""

import json
from unittest.mock import patch, mock_open, MagicMock

import pytest

# Reset lru_cache between tests
import system_info


@pytest.fixture(autouse=True)
def clear_caches():
    """Clear lru_cache between tests."""
    system_info.detect_platform.cache_clear()
    system_info.detect_wifi_interface.cache_clear()
    yield
    system_info.detect_platform.cache_clear()
    system_info.detect_wifi_interface.cache_clear()


class TestDetectPlatform:
    def test_rpi_from_device_tree(self):
        data = "Raspberry Pi 5 Model B Rev 1.0\x00"
        with patch("builtins.open", mock_open(read_data=data)):
            assert system_info.detect_platform() == "rpi"

    def test_jetson_from_device_tree(self):
        system_info.detect_platform.cache_clear()
        data = "NVIDIA Jetson Orin Nano\x00"
        with patch("builtins.open", mock_open(read_data=data)):
            assert system_info.detect_platform() == "jetson"

    def test_jetson_from_tegra_release(self):
        system_info.detect_platform.cache_clear()
        with patch("builtins.open", side_effect=FileNotFoundError):
            with patch("os.path.exists", return_value=True):
                assert system_info.detect_platform() == "jetson"

    def test_fallback_to_rpi(self):
        system_info.detect_platform.cache_clear()
        with patch("builtins.open", side_effect=FileNotFoundError):
            with patch("os.path.exists", return_value=False):
                assert system_info.detect_platform() == "rpi"


class TestDetectWifiInterface:
    def test_finds_wlan0(self):
        mock_addrs = {"lo": [], "eth0": [], "wlan0": []}
        with patch("psutil.net_if_addrs", return_value=mock_addrs):
            assert system_info.detect_wifi_interface() == "wlan0"

    def test_finds_wlp_interface(self):
        system_info.detect_wifi_interface.cache_clear()
        mock_addrs = {"lo": [], "eth0": [], "wlp2s0": []}
        with patch("psutil.net_if_addrs", return_value=mock_addrs):
            assert system_info.detect_wifi_interface() == "wlp2s0"

    def test_fallback_when_no_wifi(self):
        system_info.detect_wifi_interface.cache_clear()
        mock_addrs = {"lo": [], "eth0": []}
        with patch("psutil.net_if_addrs", return_value=mock_addrs):
            assert system_info.detect_wifi_interface() == "wlan0"


class TestGetCpuInfo:
    def test_returns_expected_keys(self):
        with patch("psutil.cpu_percent", return_value=45.2):
            with patch("psutil.sensors_temperatures", return_value={
                "cpu_thermal": [MagicMock(current=52.3)]
            }):
                with patch("psutil.cpu_freq", return_value=MagicMock(current=1500)):
                    result = system_info.get_cpu_info()

        assert result == {"usage": 45.2, "temp": 52.3, "freq": 1500}

    def test_jetson_thermal_zone(self):
        with patch("psutil.cpu_percent", return_value=30.0):
            with patch("psutil.sensors_temperatures", return_value={
                "CPU-therm": [MagicMock(current=41.5)]
            }):
                with patch("psutil.cpu_freq", return_value=MagicMock(current=1200)):
                    result = system_info.get_cpu_info()

        assert result["temp"] == 41.5


class TestGetMemoryInfo:
    def test_returns_expected_keys(self):
        mock_mem = MagicMock(total=4 * 1024**3, used=2 * 1024**3)
        mock_swap = MagicMock(total=1 * 1024**3, used=128 * 1024**2)
        with patch("psutil.virtual_memory", return_value=mock_mem):
            with patch("psutil.swap_memory", return_value=mock_swap):
                result = system_info.get_memory_info()

        assert result["ram_total"] == 4096
        assert result["ram_used"] == 2048
        assert result["swap_total"] == 1024
        assert result["swap_used"] == 128


class TestGetStorageInfo:
    def test_returns_expected_keys(self):
        mock_disk = MagicMock(
            total=32 * 1024**3, used=15 * 1024**3, free=17 * 1024**3
        )
        with patch("psutil.disk_usage", return_value=mock_disk):
            result = system_info.get_storage_info()

        assert result["total"] == 32768
        assert result["used"] == 15360
        assert result["free"] == 17408


class TestGetSystemInfo:
    def test_contains_required_fields(self):
        with patch("os.uname") as mock_uname:
            mock_uname.return_value = MagicMock(
                nodename="testhost", release="6.1.0"
            )
            with patch("builtins.open", side_effect=FileNotFoundError):
                with patch("subprocess.run", side_effect=FileNotFoundError):
                    result = system_info.get_system_info()

        assert result["hostname"] == "testhost"
        assert result["kernel"] == "6.1.0"
        assert "time" in result
        assert "platform" in result

    def test_time_format(self):
        with patch("os.uname") as mock_uname:
            mock_uname.return_value = MagicMock(
                nodename="test", release="6.1.0"
            )
            with patch("builtins.open", side_effect=FileNotFoundError):
                with patch("subprocess.run", side_effect=FileNotFoundError):
                    result = system_info.get_system_info()

        # HH:MM:SS format
        assert len(result["time"]) == 8
        assert result["time"][2] == ":"
        assert result["time"][5] == ":"


class TestLoadConfig:
    def test_loads_valid_config(self):
        data = '{"services": ["rpi-monitor", "nginx"]}'
        with patch("builtins.open", mock_open(read_data=data)):
            result = system_info.load_config()

        assert result["services"] == ["rpi-monitor", "nginx"]

    def test_returns_empty_on_missing_file(self):
        with patch("builtins.open", side_effect=FileNotFoundError):
            result = system_info.load_config()

        assert result == {"services": []}

    def test_returns_empty_on_invalid_json(self):
        with patch("builtins.open", mock_open(read_data="not json")):
            result = system_info.load_config()

        assert result == {"services": []}


class TestGetServicesInfo:
    def test_returns_service_statuses(self):
        def mock_run(cmd, **kwargs):
            name = cmd[2]
            mock = MagicMock()
            if name == "rpi-monitor":
                mock.stdout = "active\n"
            else:
                mock.stdout = "inactive\n"
            return mock

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.get_services_info(["rpi-monitor", "nginx"])

        assert result == [
            {"name": "rpi-monitor", "active": True},
            {"name": "nginx", "active": False},
        ]

    def test_handles_empty_list(self):
        result = system_info.get_services_info([])
        assert result == []

    def test_handles_subprocess_error(self):
        with patch("subprocess.run", side_effect=Exception("fail")):
            result = system_info.get_services_info(["test-svc"])

        assert result == [{"name": "test-svc", "active": False}]


class TestControlService:
    def test_rejects_unlisted_service(self):
        result = system_info.control_service("evil", "start", ["rpi-monitor"])
        assert result["status"] == "error"
        assert "not in config" in result["message"]

    def test_rejects_invalid_action(self):
        result = system_info.control_service("rpi-monitor", "delete", ["rpi-monitor"])
        assert result["status"] == "error"
        assert "invalid action" in result["message"]

    def test_successful_start(self):
        mock_result = MagicMock(returncode=0)
        with patch("subprocess.run", return_value=mock_result):
            result = system_info.control_service(
                "rpi-monitor", "start", ["rpi-monitor"]
            )

        assert result == {"status": "ok"}

    def test_failed_stop(self):
        mock_result = MagicMock(returncode=1, stderr="Permission denied")
        with patch("subprocess.run", return_value=mock_result):
            result = system_info.control_service(
                "rpi-monitor", "stop", ["rpi-monitor"]
            )

        assert result["status"] == "error"
        assert "Permission denied" in result["message"]

    def test_allows_restart(self):
        mock_result = MagicMock(returncode=0)
        with patch("subprocess.run", return_value=mock_result):
            result = system_info.control_service(
                "rpi-monitor", "restart", ["rpi-monitor"]
            )

        assert result == {"status": "ok"}
