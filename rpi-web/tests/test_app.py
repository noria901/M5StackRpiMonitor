"""Tests for rpi-web Flask app."""

import json
import os
import tempfile

import pytest

# Set env vars before importing app
_tmpdir = tempfile.mkdtemp()
os.environ["DEVICES_FILE"] = os.path.join(_tmpdir, "devices.json")
os.environ["DAEMON_CONFIG_FILE"] = os.path.join(_tmpdir, "config.json")

from app import app  # noqa: E402


@pytest.fixture
def client():
    app.config["TESTING"] = True
    with app.test_client() as client:
        # Reset files
        _write(os.environ["DEVICES_FILE"], "[]")
        _write(os.environ["DAEMON_CONFIG_FILE"], '{"services": []}')
        yield client


def _write(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(content)


def _read_json(path):
    with open(path) as f:
        return json.load(f)


class TestSettingsPage:
    def test_settings_page_renders(self, client):
        resp = client.get("/settings")
        assert resp.status_code == 200
        assert b"BLE" in resp.data

    def test_settings_page_shows_current_name(self, client):
        _write(os.environ["DAEMON_CONFIG_FILE"],
               json.dumps({"services": [], "ble_name": "TestBLE"}))
        resp = client.get("/settings")
        assert b"TestBLE" in resp.data


class TestConfigAPI:
    def test_get_config(self, client):
        _write(os.environ["DAEMON_CONFIG_FILE"],
               json.dumps({"services": ["svc1"], "ble_name": "MyName"}))
        resp = client.get("/api/config")
        assert resp.status_code == 200
        data = resp.get_json()
        assert data["ble_name"] == "MyName"
        assert data["services"] == ["svc1"]

    def test_set_ble_name(self, client):
        resp = client.post("/api/config",
                           json={"ble_name": "NewName"})
        assert resp.status_code == 200
        data = resp.get_json()
        assert data["status"] == "ok"
        assert data["restart_required"] is True

        config = _read_json(os.environ["DAEMON_CONFIG_FILE"])
        assert config["ble_name"] == "NewName"

    def test_clear_ble_name(self, client):
        _write(os.environ["DAEMON_CONFIG_FILE"],
               json.dumps({"services": [], "ble_name": "Old"}))
        resp = client.post("/api/config",
                           json={"ble_name": ""})
        assert resp.status_code == 200

        config = _read_json(os.environ["DAEMON_CONFIG_FILE"])
        assert "ble_name" not in config

    def test_preserves_existing_config(self, client):
        _write(os.environ["DAEMON_CONFIG_FILE"],
               json.dumps({"services": ["svc1", "svc2"]}))
        resp = client.post("/api/config",
                           json={"ble_name": "Test"})
        assert resp.status_code == 200

        config = _read_json(os.environ["DAEMON_CONFIG_FILE"])
        assert config["services"] == ["svc1", "svc2"]
        assert config["ble_name"] == "Test"

    def test_rejects_empty_body(self, client):
        resp = client.post("/api/config",
                           content_type="application/json",
                           data="")
        assert resp.status_code == 400


class TestCommandsPage:
    def test_commands_page_renders(self, client):
        resp = client.get("/commands")
        assert resp.status_code == 200
        assert b"Commands" in resp.data

    def test_commands_page_shows_configured_commands(self, client):
        _write(os.environ["DAEMON_CONFIG_FILE"], json.dumps({
            "services": [],
            "commands": [
                {"name": "hello", "command": "echo hello"},
                {"name": "sleep-test", "command": "sleep 10"},
            ]
        }))
        resp = client.get("/commands")
        assert b"hello" in resp.data
        assert b"sleep-test" in resp.data

    def test_commands_page_empty_state(self, client):
        resp = client.get("/commands")
        assert b"No Commands" in resp.data


class TestCommandsAPI:
    def _setup_commands(self):
        _write(os.environ["DAEMON_CONFIG_FILE"], json.dumps({
            "services": [],
            "commands": [
                {"name": "hello", "command": "echo hello"},
            ]
        }))
        # Reset cached command runner
        if hasattr(app, "_command_runner"):
            del app._command_runner

    def test_commands_status_endpoint(self, client):
        self._setup_commands()
        resp = client.get("/api/commands/status")
        assert resp.status_code == 200
        data = resp.get_json()
        assert isinstance(data, list)
        assert len(data) == 1
        assert data[0]["name"] == "hello"
        assert data[0]["state"] == "idle"

    def test_commands_run_missing_params(self, client):
        resp = client.post("/api/commands/run", json={})
        assert resp.status_code == 400

    def test_commands_run_missing_action(self, client):
        resp = client.post("/api/commands/run", json={"name": "hello"})
        assert resp.status_code == 400

    def test_commands_run_invalid_action(self, client):
        self._setup_commands()
        resp = client.post("/api/commands/run",
                           json={"name": "hello", "action": "delete"})
        data = resp.get_json()
        assert data["status"] == "error"
        assert "invalid action" in data["message"]

    def test_commands_run_unknown_command(self, client):
        self._setup_commands()
        resp = client.post("/api/commands/run",
                           json={"name": "unknown", "action": "run"})
        data = resp.get_json()
        assert data["status"] == "error"
        assert "not in config" in data["message"]

    def test_commands_stop_idle(self, client):
        self._setup_commands()
        resp = client.post("/api/commands/run",
                           json={"name": "hello", "action": "stop"})
        data = resp.get_json()
        assert data["status"] == "error"
        assert "not running" in data["message"]


class TestNavLinks:
    def test_index_has_settings_link(self, client):
        resp = client.get("/")
        assert b'/settings' in resp.data

    def test_flash_has_settings_link(self, client):
        resp = client.get("/flash")
        assert b'/settings' in resp.data

    def test_index_has_commands_link(self, client):
        resp = client.get("/")
        assert b'/commands' in resp.data

    def test_commands_has_settings_link(self, client):
        resp = client.get("/commands")
        assert b'/settings' in resp.data
