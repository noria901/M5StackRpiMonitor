"""Tests for system_info and ble_server modules."""

import json
import subprocess
from unittest.mock import patch, mock_open, MagicMock

import pytest

# Reset lru_cache between tests
import system_info
import ble_server


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


class TestBLELocalName:
    """Tests for BLE advertising name logic."""

    def setup_method(self):
        ble_server._ble_name_override = None

    def teardown_method(self):
        ble_server._ble_name_override = None

    def test_default_rpi(self):
        with patch("ble_server.detect_platform", return_value="rpi"):
            assert ble_server._ble_local_name() == "RPi-Monitor"

    def test_default_jetson(self):
        with patch("ble_server.detect_platform", return_value="jetson"):
            assert ble_server._ble_local_name() == "Jetson-Monitor"

    def test_override(self):
        ble_server._ble_name_override = "MyCustomName"
        assert ble_server._ble_local_name() == "MyCustomName"

    def test_override_takes_precedence(self):
        ble_server._ble_name_override = "Custom"
        with patch("ble_server.detect_platform", return_value="rpi"):
            assert ble_server._ble_local_name() == "Custom"

    def test_empty_override_falls_back(self):
        ble_server._ble_name_override = ""
        with patch("ble_server.detect_platform", return_value="rpi"):
            assert ble_server._ble_local_name() == "RPi-Monitor"


class TestCommandRunner:
    """Tests for CommandRunner background command execution."""

    def test_init_with_commands(self):
        runner = system_info.CommandRunner([
            {"name": "hello", "command": "echo hello"},
            {"name": "sleep", "command": "sleep 1"},
        ])
        status = runner.get_status()
        assert len(status) == 2
        names = [s["name"] for s in status]
        assert "hello" in names
        assert "sleep" in names
        for s in status:
            assert s["state"] == "idle"
            assert s["exit_code"] is None

    def test_init_with_empty_list(self):
        runner = system_info.CommandRunner([])
        assert runner.get_status() == []

    def test_init_skips_invalid_entries(self):
        runner = system_info.CommandRunner([
            {"name": "", "command": "echo"},
            {"name": "ok", "command": ""},
            {"command": "echo"},
            {"name": "valid", "command": "echo hi"},
        ])
        status = runner.get_status()
        assert len(status) == 1
        assert status[0]["name"] == "valid"

    def test_run_unknown_command(self):
        runner = system_info.CommandRunner([
            {"name": "hello", "command": "echo hello"},
        ])
        result = runner.run("unknown")
        assert result["status"] == "error"
        assert "not in config" in result["message"]

    def test_stop_unknown_command(self):
        runner = system_info.CommandRunner([
            {"name": "hello", "command": "echo hello"},
        ])
        result = runner.stop("unknown")
        assert result["status"] == "error"
        assert "not in config" in result["message"]

    def test_stop_idle_command(self):
        runner = system_info.CommandRunner([
            {"name": "hello", "command": "echo hello"},
        ])
        result = runner.stop("hello")
        assert result["status"] == "error"
        assert "not running" in result["message"]

    def test_run_success(self):
        mock_proc = MagicMock()
        mock_proc.poll.return_value = None  # still running
        with patch("subprocess.Popen", return_value=mock_proc):
            runner = system_info.CommandRunner([
                {"name": "test", "command": "echo test"},
            ])
            result = runner.run("test")

        assert result["status"] == "ok"
        status = runner.get_status()
        assert status[0]["state"] == "running"

    def test_run_already_running(self):
        mock_proc = MagicMock()
        mock_proc.poll.return_value = None  # still running
        with patch("subprocess.Popen", return_value=mock_proc):
            runner = system_info.CommandRunner([
                {"name": "test", "command": "sleep 10"},
            ])
            runner.run("test")
            result = runner.run("test")

        assert result["status"] == "error"
        assert "already running" in result["message"]

    def test_process_completion_detected(self):
        mock_proc = MagicMock()
        mock_proc.poll.return_value = None  # running initially
        with patch("subprocess.Popen", return_value=mock_proc):
            runner = system_info.CommandRunner([
                {"name": "test", "command": "echo done"},
            ])
            runner.run("test")

        # Simulate process completing with exit code 0
        mock_proc.poll.return_value = 0
        status = runner.get_status()
        assert status[0]["state"] == "done"
        assert status[0]["exit_code"] == 0

    def test_process_error_detected(self):
        mock_proc = MagicMock()
        mock_proc.poll.return_value = None
        with patch("subprocess.Popen", return_value=mock_proc):
            runner = system_info.CommandRunner([
                {"name": "test", "command": "false"},
            ])
            runner.run("test")

        # Simulate process completing with non-zero exit code
        mock_proc.poll.return_value = 1
        status = runner.get_status()
        assert status[0]["state"] == "error"
        assert status[0]["exit_code"] == 1

    def test_stop_running_command(self):
        mock_proc = MagicMock()
        mock_proc.poll.return_value = None  # running
        with patch("subprocess.Popen", return_value=mock_proc):
            runner = system_info.CommandRunner([
                {"name": "test", "command": "sleep 100"},
            ])
            runner.run("test")
            result = runner.stop("test")

        assert result["status"] == "ok"
        mock_proc.terminate.assert_called_once()
        status = runner.get_status()
        assert status[0]["state"] == "idle"

    def test_run_popen_exception(self):
        with patch("subprocess.Popen", side_effect=OSError("no such cmd")):
            runner = system_info.CommandRunner([
                {"name": "bad", "command": "nonexistent"},
            ])
            result = runner.run("bad")

        assert result["status"] == "error"
        assert "no such cmd" in result["message"]
        status = runner.get_status()
        assert status[0]["state"] == "error"


class TestGetRos2Info:
    """Tests for get_ros2_info() ROS2 monitoring."""

    def _make_run_result(self, stdout="", returncode=0):
        mock = MagicMock()
        mock.stdout = stdout
        mock.returncode = returncode
        return mock

    def test_ros2_available(self):
        """When ROS2 is available, returns active=True with nodes and topics."""
        node_output = "/node_a\n/node_b\n/node_c\n"
        topic_output = "/topic_x\n/topic_y\n"

        def mock_run(cmd, **kwargs):
            shell_cmd = cmd[2]  # bash -c "source ... && ros2 ..."
            if "node list" in shell_cmd:
                return self._make_run_result(stdout=node_output)
            elif "topic list" in shell_cmd:
                return self._make_run_result(stdout=topic_output)
            return self._make_run_result()

        with patch("os.path.isfile", return_value=True):
            with patch("subprocess.run", side_effect=mock_run):
                result = system_info.get_ros2_info("/opt/ros/humble/setup.bash")

        assert result["active"] is True
        assert result["nodes"] == ["/node_a", "/node_b", "/node_c"]
        assert result["topics"] == ["/topic_x", "/topic_y"]
        assert result["n_total"] == 3
        assert result["t_total"] == 2

    def test_ros2_not_installed(self):
        """When ros2 command raises FileNotFoundError, returns active=False."""
        with patch("os.path.isfile", return_value=True):
            with patch("subprocess.run", side_effect=FileNotFoundError):
                result = system_info.get_ros2_info("/opt/ros/humble/setup.bash")

        assert result["active"] is False
        assert result["nodes"] == []
        assert result["topics"] == []
        assert result["n_total"] == 0
        assert result["t_total"] == 0

    def test_ros2_command_timeout(self):
        """When ros2 command times out, returns active=False."""
        with patch("os.path.isfile", return_value=True):
            with patch(
                "subprocess.run",
                side_effect=subprocess.TimeoutExpired(cmd="ros2", timeout=3),
            ):
                result = system_info.get_ros2_info("/opt/ros/humble/setup.bash")

        assert result["active"] is False
        assert result["nodes"] == []
        assert result["topics"] == []

    def test_truncation_to_max_10(self):
        """When more than 10 nodes/topics, lists are truncated but totals show real counts."""
        nodes = [f"/node_{i}" for i in range(25)]
        topics = [f"/topic_{i}" for i in range(15)]

        def mock_run(cmd, **kwargs):
            shell_cmd = cmd[2]
            if "node list" in shell_cmd:
                return self._make_run_result(stdout="\n".join(nodes) + "\n")
            elif "topic list" in shell_cmd:
                return self._make_run_result(stdout="\n".join(topics) + "\n")
            return self._make_run_result()

        with patch("os.path.isfile", return_value=True):
            with patch("subprocess.run", side_effect=mock_run):
                result = system_info.get_ros2_info("/opt/ros/humble/setup.bash")

        assert result["active"] is True
        assert len(result["nodes"]) <= 10
        assert len(result["topics"]) <= 10
        assert result["n_total"] == 25
        assert result["t_total"] == 15

    def test_512_byte_limit(self):
        """With very long names, JSON output stays under 512 bytes."""
        long_prefix = "a" * 80
        nodes = [f"/{long_prefix}_node_{i}" for i in range(15)]
        topics = [f"/{long_prefix}_topic_{i}" for i in range(15)]

        def mock_run(cmd, **kwargs):
            shell_cmd = cmd[2]
            if "node list" in shell_cmd:
                return self._make_run_result(stdout="\n".join(nodes) + "\n")
            elif "topic list" in shell_cmd:
                return self._make_run_result(stdout="\n".join(topics) + "\n")
            return self._make_run_result()

        with patch("os.path.isfile", return_value=True):
            with patch("subprocess.run", side_effect=mock_run):
                result = system_info.get_ros2_info("/opt/ros/humble/setup.bash")

        result_json = json.dumps(result)
        assert len(result_json) <= 512
        assert result["active"] is True
        # Lists were shrunk to fit
        assert result["n_total"] == 15
        assert result["t_total"] == 15

    def test_setup_script_not_exists(self):
        """When setup_script file doesn't exist, returns empty/inactive result."""
        with patch("os.path.isfile", return_value=False):
            result = system_info.get_ros2_info("/nonexistent/setup.bash")

        assert result["active"] is False
        assert result["nodes"] == []
        assert result["topics"] == []
        assert result["n_total"] == 0
        assert result["t_total"] == 0

    def test_custom_setup_script(self):
        """Custom setup_script path is passed to the subprocess command."""
        custom_path = "/opt/ros/iron/setup.bash"

        captured_cmds = []

        def mock_run(cmd, **kwargs):
            captured_cmds.append(cmd[2])
            return self._make_run_result(stdout="/my_node\n")

        with patch("os.path.isfile", return_value=True):
            with patch("subprocess.run", side_effect=mock_run):
                result = system_info.get_ros2_info(custom_path)

        # Verify the custom path appears in the shell commands
        assert len(captured_cmds) == 2
        for shell_cmd in captured_cmds:
            assert custom_path in shell_cmd

    def test_empty_output_returns_inactive(self):
        """When both node list and topic list are empty, returns active=False."""
        def mock_run(cmd, **kwargs):
            return self._make_run_result(stdout="")

        with patch("os.path.isfile", return_value=True):
            with patch("subprocess.run", side_effect=mock_run):
                result = system_info.get_ros2_info("/opt/ros/humble/setup.bash")

        assert result["active"] is False

    def test_nonzero_return_code(self):
        """When ros2 commands return non-zero, treats as empty lists."""
        def mock_run(cmd, **kwargs):
            return self._make_run_result(stdout="some output", returncode=1)

        with patch("os.path.isfile", return_value=True):
            with patch("subprocess.run", side_effect=mock_run):
                result = system_info.get_ros2_info("/opt/ros/humble/setup.bash")

        assert result["active"] is False


class TestScanWifiNetworks:
    def test_scan_returns_networks(self):
        """Scan parses nmcli output correctly."""
        nmcli_output = "HomeWifi:85:WPA2\nOfficeNet:62:WPA2\nOpenNet:30:\n"
        call_count = 0

        def mock_run(cmd, **kwargs):
            nonlocal call_count
            call_count += 1
            m = MagicMock()
            if "rescan" in cmd:
                m.returncode = 0
                m.stdout = ""
            else:
                m.returncode = 0
                m.stdout = nmcli_output
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.scan_wifi_networks()

        assert len(result) == 3
        assert result[0]["ssid"] == "HomeWifi"
        assert result[0]["signal"] == 85
        assert result[0]["security"] == "WPA2"
        assert result[2]["ssid"] == "OpenNet"
        assert result[2]["security"] == "Open"

    def test_scan_sorts_by_signal(self):
        """Networks are sorted by signal strength descending."""
        nmcli_output = "Weak:20:WPA2\nStrong:90:WPA2\nMedium:50:WPA2\n"

        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 0
            m.stdout = nmcli_output if "list" in cmd else ""
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.scan_wifi_networks()

        assert result[0]["ssid"] == "Strong"
        assert result[1]["ssid"] == "Medium"
        assert result[2]["ssid"] == "Weak"

    def test_scan_deduplicates_ssids(self):
        """Duplicate SSIDs are removed."""
        nmcli_output = "MyNet:80:WPA2\nMyNet:70:WPA2\nOther:60:WPA2\n"

        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 0
            m.stdout = nmcli_output if "list" in cmd else ""
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.scan_wifi_networks()

        ssids = [n["ssid"] for n in result]
        assert ssids.count("MyNet") == 1

    def test_scan_empty_result(self):
        """Empty scan returns empty list."""
        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 0
            m.stdout = ""
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.scan_wifi_networks()

        assert result == []

    def test_scan_handles_nmcli_failure(self):
        """nmcli failure returns empty list."""
        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 1
            m.stdout = ""
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.scan_wifi_networks()

        assert result == []


class TestGetSavedWifiConnections:
    def test_returns_wireless_connections(self):
        nmcli_output = "HomeWifi:802-11-wireless\nEth0:802-3-ethernet\nOffice:802-11-wireless\n"

        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 0
            m.stdout = nmcli_output
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.get_saved_wifi_connections()

        assert result == ["HomeWifi", "Office"]

    def test_returns_empty_on_no_wireless(self):
        nmcli_output = "Eth0:802-3-ethernet\n"

        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 0
            m.stdout = nmcli_output
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.get_saved_wifi_connections()

        assert result == []

    def test_handles_exception(self):
        with patch("subprocess.run", side_effect=Exception("fail")):
            result = system_info.get_saved_wifi_connections()

        assert result == []


class TestWifiConnect:
    def test_connect_with_password(self):
        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 0
            m.stderr = ""
            return m

        with patch("subprocess.run", side_effect=mock_run):
            with patch("system_info.get_saved_wifi_connections", return_value=[]):
                result = system_info.wifi_connect("TestNet", "password123")

        assert result["status"] == "ok"

    def test_connect_saved_network_without_password(self):
        """Activates existing profile when no password given."""
        calls = []

        def mock_run(cmd, **kwargs):
            calls.append(cmd)
            m = MagicMock()
            m.returncode = 0
            m.stderr = ""
            return m

        with patch("subprocess.run", side_effect=mock_run):
            with patch("system_info.get_saved_wifi_connections", return_value=["SavedNet"]):
                result = system_info.wifi_connect("SavedNet", "")

        assert result["status"] == "ok"
        # Should use "nmcli con up" for saved profiles
        assert any("con" in c and "up" in c for c in calls)

    def test_connect_empty_ssid(self):
        result = system_info.wifi_connect("")
        assert result["status"] == "error"

    def test_connect_failure(self):
        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 1
            m.stderr = "No network found"
            return m

        with patch("subprocess.run", side_effect=mock_run):
            with patch("system_info.get_saved_wifi_connections", return_value=[]):
                result = system_info.wifi_connect("BadNet", "pass")

        assert result["status"] == "error"
        assert "No network found" in result["message"]

    def test_connect_timeout(self):
        with patch("subprocess.run", side_effect=subprocess.TimeoutExpired("nmcli", 30)):
            with patch("system_info.get_saved_wifi_connections", return_value=[]):
                result = system_info.wifi_connect("SlowNet", "pass")

        assert result["status"] == "error"
        assert "timed out" in result["message"]


class TestWifiDisconnect:
    def test_disconnect_success(self):
        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 0
            m.stderr = ""
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.wifi_disconnect()

        assert result["status"] == "ok"

    def test_disconnect_failure(self):
        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 1
            m.stderr = "Device not managed"
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.wifi_disconnect()

        assert result["status"] == "error"


class TestWifiForget:
    def test_forget_success(self):
        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 0
            m.stderr = ""
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.wifi_forget("OldNet")

        assert result["status"] == "ok"

    def test_forget_empty_ssid(self):
        result = system_info.wifi_forget("")
        assert result["status"] == "error"

    def test_forget_failure(self):
        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 1
            m.stderr = "Connection 'OldNet' not found"
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.wifi_forget("OldNet")

        assert result["status"] == "error"


class TestGetWifiStatus:
    def test_returns_current_and_saved(self):
        def mock_run(cmd, **kwargs):
            m = MagicMock()
            m.returncode = 0
            if "iwgetid" in cmd:
                m.stdout = "MyNetwork\n"
            else:
                m.stdout = "MyNetwork:802-11-wireless\nWork:802-11-wireless\n"
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.get_wifi_status()

        assert result["current_ssid"] == "MyNetwork"
        assert "MyNetwork" in result["saved"]
        assert "Work" in result["saved"]

    def test_not_connected(self):
        def mock_run(cmd, **kwargs):
            m = MagicMock()
            if "iwgetid" in cmd:
                m.returncode = 1
                m.stdout = ""
            else:
                m.returncode = 0
                m.stdout = ""
            return m

        with patch("subprocess.run", side_effect=mock_run):
            result = system_info.get_wifi_status()

        assert result["current_ssid"] == ""
        assert result["saved"] == []


class TestWifiCharacteristic:
    """Test WifiCharacteristic BLE integration."""

    @staticmethod
    def _get_value(char):
        """Read the internal value of a characteristic (bypasses D-Bus decorator)."""
        return json.loads(char._value.decode("utf-8"))

    def test_read_returns_status(self):
        status = {"current_ssid": "Test", "saved": ["Test"]}
        with patch("ble_server.get_wifi_status", return_value=status):
            char = ble_server.WifiCharacteristic("/test/char")

        data = self._get_value(char)
        assert data["current_ssid"] == "Test"

    def test_write_scan(self):
        status = {"current_ssid": "Net", "saved": ["Net"]}
        networks = [{"ssid": "Net", "signal": 80, "security": "WPA2"}]

        with patch("ble_server.get_wifi_status", return_value=status):
            char = ble_server.WifiCharacteristic("/test/char")

        with patch("ble_server.scan_wifi_networks", return_value=networks):
            with patch("ble_server.get_wifi_status", return_value=status):
                cmd = json.dumps({"action": "scan"}).encode("utf-8")
                char.WriteValue(list(cmd), {})

        data = self._get_value(char)
        assert "networks" in data
        assert len(data["networks"]) == 1

    def test_write_connect(self):
        status = {"current_ssid": "", "saved": []}
        with patch("ble_server.get_wifi_status", return_value=status):
            char = ble_server.WifiCharacteristic("/test/char")

        with patch("ble_server.wifi_connect", return_value={"status": "ok"}) as mock:
            cmd = json.dumps({"action": "connect", "ssid": "Net", "password": "pass"}).encode("utf-8")
            char.WriteValue(list(cmd), {})
            mock.assert_called_once_with("Net", "pass")

        data = self._get_value(char)
        assert data["status"] == "ok"

    def test_write_disconnect(self):
        status = {"current_ssid": "Net", "saved": ["Net"]}
        with patch("ble_server.get_wifi_status", return_value=status):
            char = ble_server.WifiCharacteristic("/test/char")

        with patch("ble_server.wifi_disconnect", return_value={"status": "ok"}) as mock:
            cmd = json.dumps({"action": "disconnect"}).encode("utf-8")
            char.WriteValue(list(cmd), {})
            mock.assert_called_once()

    def test_write_forget(self):
        status = {"current_ssid": "", "saved": ["OldNet"]}
        with patch("ble_server.get_wifi_status", return_value=status):
            char = ble_server.WifiCharacteristic("/test/char")

        with patch("ble_server.wifi_forget", return_value={"status": "ok"}) as mock:
            cmd = json.dumps({"action": "forget", "ssid": "OldNet"}).encode("utf-8")
            char.WriteValue(list(cmd), {})
            mock.assert_called_once_with("OldNet")

    def test_write_invalid_action(self):
        status = {"current_ssid": "", "saved": []}
        with patch("ble_server.get_wifi_status", return_value=status):
            char = ble_server.WifiCharacteristic("/test/char")

        cmd = json.dumps({"action": "invalid"}).encode("utf-8")
        char.WriteValue(list(cmd), {})

        data = self._get_value(char)
        assert data["status"] == "error"

    def test_write_invalid_json(self):
        status = {"current_ssid": "", "saved": []}
        with patch("ble_server.get_wifi_status", return_value=status):
            char = ble_server.WifiCharacteristic("/test/char")

        cmd = b"not json"
        char.WriteValue(list(cmd), {})

        data = self._get_value(char)
        assert data["status"] == "error"
