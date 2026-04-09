"""BLE GATT server for RPi Monitor using D-Bus/BlueZ."""

import asyncio
import json
import logging
import struct
from typing import Optional

from dbus_next.aio import MessageBus
from dbus_next.service import ServiceInterface, method, dbus_property
from dbus_next import Variant, BusType
from dbus_next.constants import PropertyAccess

from system_info import (
    CommandRunner,
    control_service,
    detect_platform,
    get_cpu_info,
    get_memory_info,
    get_services_info,
    get_storage_info,
    get_network_info,
    get_system_info,
    get_wifi_status,
    load_config,
    scan_wifi_networks,
    system_control,
    wifi_connect,
    wifi_disconnect,
    wifi_forget,
)

logger = logging.getLogger(__name__)

# UUIDs
SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0"
CHAR_CPU_UUID = "12345678-1234-5678-1234-56789abcdef1"
CHAR_MEMORY_UUID = "12345678-1234-5678-1234-56789abcdef2"
CHAR_STORAGE_UUID = "12345678-1234-5678-1234-56789abcdef3"
CHAR_NETWORK_UUID = "12345678-1234-5678-1234-56789abcdef4"
CHAR_SYSTEM_UUID = "12345678-1234-5678-1234-56789abcdef5"
CHAR_REGISTRATION_UUID = "12345678-1234-5678-1234-56789abcdef6"
CHAR_SERVICES_UUID = "12345678-1234-5678-1234-56789abcdef7"
CHAR_SYSTEM_CTRL_UUID = "12345678-1234-5678-1234-56789abcdef8"
CHAR_COMMANDS_UUID = "12345678-1234-5678-1234-56789abcdef9"
CHAR_WIFI_UUID = "12345678-1234-5678-1234-56789abcdefa"

BLUEZ_SERVICE = "org.bluez"
LE_ADVERTISING_MANAGER_IFACE = "org.bluez.LEAdvertisingManager1"
GATT_MANAGER_IFACE = "org.bluez.GattManager1"
ADAPTER_IFACE = "org.bluez.Adapter1"

REGISTERED_DEVICES_FILE = "/var/lib/rpi-monitor/devices.json"


def load_registered_devices() -> list:
    """Load registered devices from file."""
    try:
        with open(REGISTERED_DEVICES_FILE) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return []


def save_registered_devices(devices: list) -> None:
    """Save registered devices to file."""
    import os

    os.makedirs(os.path.dirname(REGISTERED_DEVICES_FILE), exist_ok=True)
    with open(REGISTERED_DEVICES_FILE, "w") as f:
        json.dump(devices, f, indent=2)


_ble_name_override: Optional[str] = None


def _ble_local_name() -> str:
    """Return BLE local name.

    If a custom name was set via config (``ble_name``), that value is used.
    Otherwise the name is derived from the detected platform.
    """
    if _ble_name_override:
        return _ble_name_override
    platform = detect_platform()
    return "Jetson-Monitor" if platform == "jetson" else "RPi-Monitor"


class Advertisement(ServiceInterface):
    """BLE advertisement for the monitor service."""

    def __init__(self, index: int):
        self._path = f"/org/bluez/rpimonitor/advertisement{index}"
        super().__init__("org.bluez.LEAdvertisement1")

    @dbus_property(access=PropertyAccess.READ)
    def Type(self) -> "s":
        return "peripheral"

    @dbus_property(access=PropertyAccess.READ)
    def ServiceUUIDs(self) -> "as":
        return [SERVICE_UUID]

    @dbus_property(access=PropertyAccess.READ)
    def LocalName(self) -> "s":
        return _ble_local_name()

    @dbus_property(access=PropertyAccess.READ)
    def Includes(self) -> "as":
        return ["tx-power"]

    @method()
    def Release(self):
        logger.info("Advertisement released")

    @property
    def path(self) -> str:
        return self._path


class Characteristic(ServiceInterface):
    """Base GATT characteristic."""

    def __init__(self, uuid: str, flags: list[str], path: str):
        self._uuid = uuid
        self._flags = flags
        self._path = path
        self._value = b""
        super().__init__("org.bluez.GattCharacteristic1")

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":
        return self._uuid

    @dbus_property(access=PropertyAccess.READ)
    def Service(self) -> "o":
        # Parent service path
        return "/".join(self._path.split("/")[:-1])

    @dbus_property(access=PropertyAccess.READ)
    def Flags(self) -> "as":
        return self._flags

    @method()
    def ReadValue(self, options: "a{sv}") -> "ay":
        return bytes(self._value)

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}"):
        self._value = bytes(value)

    def set_value(self, data: str):
        self._value = data.encode("utf-8")

    @property
    def path(self) -> str:
        return self._path


class CpuCharacteristic(Characteristic):
    def __init__(self, path: str):
        super().__init__(CHAR_CPU_UUID, ["read", "notify"], path)

    def update(self):
        self.set_value(json.dumps(get_cpu_info()))


class MemoryCharacteristic(Characteristic):
    def __init__(self, path: str):
        super().__init__(CHAR_MEMORY_UUID, ["read", "notify"], path)

    def update(self):
        self.set_value(json.dumps(get_memory_info()))


class StorageCharacteristic(Characteristic):
    def __init__(self, path: str):
        super().__init__(CHAR_STORAGE_UUID, ["read", "notify"], path)

    def update(self):
        self.set_value(json.dumps(get_storage_info()))


class NetworkCharacteristic(Characteristic):
    def __init__(self, path: str):
        super().__init__(CHAR_NETWORK_UUID, ["read", "notify"], path)

    def update(self):
        self.set_value(json.dumps(get_network_info()))


class SystemCharacteristic(Characteristic):
    def __init__(self, path: str):
        super().__init__(CHAR_SYSTEM_UUID, ["read", "notify"], path)

    def update(self):
        self.set_value(json.dumps(get_system_info()))


class RegistrationCharacteristic(Characteristic):
    def __init__(self, path: str):
        super().__init__(CHAR_REGISTRATION_UUID, ["read", "write"], path)

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}"):
        data = bytes(value).decode("utf-8")
        logger.info(f"Registration request: {data}")
        try:
            req = json.loads(data)
            if req.get("action") == "register":
                devices = load_registered_devices()
                device_entry = {
                    "device_name": req.get("device_name", "Unknown"),
                    "mac": req.get("mac", ""),
                }
                # Update or add
                existing = [
                    d for d in devices if d.get("mac") == device_entry["mac"]
                ]
                if existing:
                    existing[0].update(device_entry)
                else:
                    devices.append(device_entry)
                save_registered_devices(devices)
                self.set_value(json.dumps({"status": "ok"}))
                logger.info(f"Device registered: {device_entry}")
        except (json.JSONDecodeError, KeyError) as e:
            logger.error(f"Registration error: {e}")
            self.set_value(json.dumps({"status": "error", "message": str(e)}))


class ServicesCharacteristic(Characteristic):
    """BLE characteristic for systemd service monitoring and control."""

    def __init__(self, path: str, config: dict):
        super().__init__(CHAR_SERVICES_UUID, ["read", "write"], path)
        self._config = config
        self._service_names: list[str] = config.get("services", [])

    def update(self):
        self.set_value(json.dumps(get_services_info(self._service_names)))

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}"):
        data = bytes(value).decode("utf-8")
        logger.info(f"Service control request: {data}")
        try:
            req = json.loads(data)
            action = req.get("action", "")
            service = req.get("service", "")
            result = control_service(service, action, self._service_names)
            self.set_value(json.dumps(result))
            # 制御後にステータスを再取得
            self.update()
        except (json.JSONDecodeError, KeyError) as e:
            logger.error(f"Service control error: {e}")
            self.set_value(json.dumps({"status": "error", "message": str(e)}))


class SystemControlCharacteristic(Characteristic):
    """BLE characteristic for system power control (reboot/shutdown)."""

    def __init__(self, path: str):
        super().__init__(CHAR_SYSTEM_CTRL_UUID, ["read", "write"], path)

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}"):
        data = bytes(value).decode("utf-8")
        logger.info(f"System control request: {data}")
        try:
            req = json.loads(data)
            action = req.get("action", "")
            result = system_control(action)
            self.set_value(json.dumps(result))
        except (json.JSONDecodeError, KeyError) as e:
            logger.error(f"System control error: {e}")
            self.set_value(json.dumps({"status": "error", "message": str(e)}))


class CommandsCharacteristic(Characteristic):
    """BLE characteristic for custom command execution."""

    def __init__(self, path: str, runner: CommandRunner):
        super().__init__(CHAR_COMMANDS_UUID, ["read", "write"], path)
        self._runner = runner

    def update(self):
        self.set_value(json.dumps(self._runner.get_status()))

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}"):
        data = bytes(value).decode("utf-8")
        logger.info(f"Command request: {data}")
        try:
            req = json.loads(data)
            action = req.get("action", "")
            name = req.get("name", "")
            if action == "run":
                result = self._runner.run(name)
            elif action == "stop":
                result = self._runner.stop(name)
            else:
                result = {"status": "error", "message": f"invalid action '{action}'"}
            self.set_value(json.dumps(result))
        except (json.JSONDecodeError, KeyError) as e:
            logger.error(f"Command error: {e}")
            self.set_value(json.dumps({"status": "error", "message": str(e)}))


class WifiCharacteristic(Characteristic):
    """BLE characteristic for WiFi STA configuration."""

    def __init__(self, path: str):
        super().__init__(CHAR_WIFI_UUID, ["read", "write"], path)
        # Initialize with current status
        self.update()

    def update(self):
        self.set_value(json.dumps(get_wifi_status()))

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}"):
        data = bytes(value).decode("utf-8")
        logger.info(f"WiFi config request: {data}")
        try:
            req = json.loads(data)
            action = req.get("action", "")
            if action == "scan":
                networks = scan_wifi_networks()
                status = get_wifi_status()
                status["networks"] = networks
                self.set_value(json.dumps(status))
            elif action == "connect":
                ssid = req.get("ssid", "")
                password = req.get("password", "")
                result = wifi_connect(ssid, password)
                self.set_value(json.dumps(result))
            elif action == "disconnect":
                result = wifi_disconnect()
                self.set_value(json.dumps(result))
            elif action == "forget":
                ssid = req.get("ssid", "")
                result = wifi_forget(ssid)
                self.set_value(json.dumps(result))
            else:
                self.set_value(json.dumps({
                    "status": "error",
                    "message": f"invalid action '{action}'",
                }))
        except (json.JSONDecodeError, KeyError) as e:
            logger.error(f"WiFi config error: {e}")
            self.set_value(json.dumps({"status": "error", "message": str(e)}))

    @method()
    def ReadValue(self, options: "a{sv}") -> "ay":
        return bytes(self._value)


class GattService(ServiceInterface):
    """GATT Service definition."""

    def __init__(self, path: str):
        self._path = path
        super().__init__("org.bluez.GattService1")

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":
        return SERVICE_UUID

    @dbus_property(access=PropertyAccess.READ)
    def Primary(self) -> "b":
        return True

    @property
    def path(self) -> str:
        return self._path


class BLEServer:
    """Manages the BLE GATT server lifecycle."""

    def __init__(self):
        self.bus: Optional[MessageBus] = None
        self.service: Optional[GattService] = None
        self.characteristics: list[Characteristic] = []
        self.advertisement: Optional[Advertisement] = None
        self._update_task: Optional[asyncio.Task] = None

    async def start(self):
        """Initialize and start the BLE GATT server."""
        logger.info("Starting BLE GATT server...")
        self.bus = await MessageBus(bus_type=BusType.SYSTEM).connect()

        base_path = "/org/bluez/rpimonitor"
        service_path = f"{base_path}/service0"

        # Create service
        self.service = GattService(service_path)

        # Load config
        config = load_config()
        logger.info(f"Config loaded: services={config.get('services', [])}")

        # Apply custom BLE name from config
        global _ble_name_override
        custom_name = config.get("ble_name", "").strip()
        if custom_name:
            _ble_name_override = custom_name
            logger.info(f"BLE name override: {custom_name}")
        else:
            _ble_name_override = None

        # Create command runner
        cmd_runner = CommandRunner(config.get("commands", []))
        logger.info(f"Commands configured: {list(cmd_runner._allowed.keys())}")

        # Create characteristics
        cpu_char = CpuCharacteristic(f"{service_path}/char0")
        mem_char = MemoryCharacteristic(f"{service_path}/char1")
        storage_char = StorageCharacteristic(f"{service_path}/char2")
        net_char = NetworkCharacteristic(f"{service_path}/char3")
        sys_char = SystemCharacteristic(f"{service_path}/char4")
        reg_char = RegistrationCharacteristic(f"{service_path}/char5")
        svc_char = ServicesCharacteristic(f"{service_path}/char6", config)
        sysctrl_char = SystemControlCharacteristic(f"{service_path}/char7")
        cmd_char = CommandsCharacteristic(f"{service_path}/char8", cmd_runner)
        wifi_char = WifiCharacteristic(f"{service_path}/char9")

        self.characteristics = [
            cpu_char,
            mem_char,
            storage_char,
            net_char,
            sys_char,
            reg_char,
            svc_char,
            sysctrl_char,
            cmd_char,
            wifi_char,
        ]

        # Export objects on D-Bus
        self.bus.export(service_path, self.service)
        for char in self.characteristics:
            self.bus.export(char.path, char)

        # Create and register advertisement
        self.advertisement = Advertisement(0)
        self.bus.export(self.advertisement.path, self.advertisement)

        # Register with BlueZ
        await self._register_application()
        await self._register_advertisement()

        # Start periodic updates
        self._update_task = asyncio.create_task(self._update_loop())

        logger.info("BLE GATT server started successfully")

    async def _register_application(self):
        """Register GATT application with BlueZ."""
        introspection = await self.bus.introspect(BLUEZ_SERVICE, "/org/bluez/hci0")
        proxy = self.bus.get_proxy_object(
            BLUEZ_SERVICE, "/org/bluez/hci0", introspection
        )

        # Enable adapter
        adapter = proxy.get_interface(ADAPTER_IFACE)
        await adapter.set_powered(True)  # type: ignore
        await adapter.set_discoverable(True)  # type: ignore
        await adapter.set_alias(_ble_local_name())  # type: ignore

        # Register GATT manager
        gatt_manager = proxy.get_interface(GATT_MANAGER_IFACE)
        await gatt_manager.call_register_application(  # type: ignore
            "/org/bluez/rpimonitor", {}
        )
        logger.info("GATT application registered")

    async def _register_advertisement(self):
        """Register BLE advertisement."""
        introspection = await self.bus.introspect(BLUEZ_SERVICE, "/org/bluez/hci0")
        proxy = self.bus.get_proxy_object(
            BLUEZ_SERVICE, "/org/bluez/hci0", introspection
        )
        ad_manager = proxy.get_interface(LE_ADVERTISING_MANAGER_IFACE)
        await ad_manager.call_register_advertisement(  # type: ignore
            self.advertisement.path, {}
        )
        logger.info("Advertisement registered")

    async def _update_loop(self):
        """Periodically update characteristic values."""
        while True:
            try:
                for char in self.characteristics:
                    if hasattr(char, "update"):
                        char.update()
            except Exception as e:
                logger.error(f"Update error: {e}")
            await asyncio.sleep(2)

    async def stop(self):
        """Stop the BLE server."""
        if self._update_task:
            self._update_task.cancel()
        if self.bus:
            self.bus.disconnect()
        logger.info("BLE server stopped")
