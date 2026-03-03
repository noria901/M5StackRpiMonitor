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


class TestNavLinks:
    def test_index_has_settings_link(self, client):
        resp = client.get("/")
        assert b'/settings' in resp.data

    def test_flash_has_settings_link(self, client):
        resp = client.get("/flash")
        assert b'/settings' in resp.data
