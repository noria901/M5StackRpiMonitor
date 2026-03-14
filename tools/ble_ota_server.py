#!/usr/bin/env python3
"""
BLE OTA Server for M5Dial firmware update.

Runs on the development PC. Advertises a BLE GATT service that the M5Dial
OTA Update app can connect to and download firmware from.

Requires: Linux with BlueZ 5.43+

Usage:
    python3 ble_ota_server.py <firmware.bin>
    python3 ble_ota_server.py m5dial-firmware/build/m5dial.bin

Protocol:
    Service UUID:  12345678-1234-5678-9abc-def012345670
    Info Char:     12345678-1234-5678-9abc-def012345671 (Read)
    Data Char:     12345678-1234-5678-9abc-def012345672 (Read)
    Control Char:  12345678-1234-5678-9abc-def012345673 (Write)

    1. Client reads Info char -> JSON with firmware size and chunk_size
    2. Client writes 4-byte LE offset to Control char
    3. Client reads Data char -> binary chunk at that offset
    4. Repeat until all data transferred
"""

import asyncio
import json
import struct
import sys
import os
from pathlib import Path

from dbus_next.aio import MessageBus
from dbus_next.service import ServiceInterface, method, dbus_property
from dbus_next import Variant, BusType

# BLE UUIDs
OTA_SERVICE_UUID = "12345678-1234-5678-9abc-def012345670"
OTA_INFO_UUID = "12345678-1234-5678-9abc-def012345671"
OTA_DATA_UUID = "12345678-1234-5678-9abc-def012345672"
OTA_CONTROL_UUID = "12345678-1234-5678-9abc-def012345673"

BLUEZ_SERVICE = "org.bluez"
ADAPTER_IFACE = "org.bluez.Adapter1"
LE_ADV_MGR_IFACE = "org.bluez.LEAdvertisingManager1"
LE_ADV_IFACE = "org.bluez.LEAdvertisement1"
GATT_MGR_IFACE = "org.bluez.GattManager1"
GATT_SERVICE_IFACE = "org.bluez.GattService1"
GATT_CHAR_IFACE = "org.bluez.GattCharacteristic1"

CHUNK_SIZE = 480  # bytes per read (fits well within 512 ATT MTU)


class OtaAdvertisement(ServiceInterface):
    """BLE advertisement for OTA service."""

    def __init__(self):
        super().__init__(LE_ADV_IFACE)

    @dbus_property()
    def Type(self) -> "s":
        return "peripheral"

    @dbus_property()
    def ServiceUUIDs(self) -> "as":
        return [OTA_SERVICE_UUID]

    @dbus_property()
    def LocalName(self) -> "s":
        return "M5Dial-OTA-Server"

    @dbus_property()
    def Includes(self) -> "as":
        return ["tx-power"]

    @method()
    def Release(self):
        pass


class OtaService(ServiceInterface):
    """GATT Service for OTA."""

    def __init__(self):
        super().__init__(GATT_SERVICE_IFACE)

    @dbus_property()
    def UUID(self) -> "s":
        return OTA_SERVICE_UUID

    @dbus_property()
    def Primary(self) -> "b":
        return True


class OtaInfoCharacteristic(ServiceInterface):
    """Info characteristic - returns firmware metadata as JSON."""

    def __init__(self, firmware_data: bytes, firmware_name: str):
        super().__init__(GATT_CHAR_IFACE)
        self._firmware_data = firmware_data
        self._firmware_name = firmware_name

    @dbus_property()
    def UUID(self) -> "s":
        return OTA_INFO_UUID

    @dbus_property()
    def Service(self) -> "o":
        return "/org/bluez/ota/service0"

    @dbus_property()
    def Flags(self) -> "as":
        return ["read"]

    @method()
    def ReadValue(self, options: "a{sv}") -> "ay":
        info = {
            "size": len(self._firmware_data),
            "chunk_size": CHUNK_SIZE,
            "name": self._firmware_name,
        }
        data = json.dumps(info).encode("utf-8")
        print(f"[INFO] Read: size={len(self._firmware_data)}, chunk={CHUNK_SIZE}")
        return list(data)


class OtaDataCharacteristic(ServiceInterface):
    """Data characteristic - returns firmware chunk at current offset."""

    def __init__(self, firmware_data: bytes):
        super().__init__(GATT_CHAR_IFACE)
        self._firmware_data = firmware_data
        self._offset = 0

    @dbus_property()
    def UUID(self) -> "s":
        return OTA_DATA_UUID

    @dbus_property()
    def Service(self) -> "o":
        return "/org/bluez/ota/service0"

    @dbus_property()
    def Flags(self) -> "as":
        return ["read"]

    def set_offset(self, offset: int):
        self._offset = offset

    @method()
    def ReadValue(self, options: "a{sv}") -> "ay":
        start = self._offset
        end = min(start + CHUNK_SIZE, len(self._firmware_data))
        chunk = self._firmware_data[start:end]

        percent = int(start * 100 / len(self._firmware_data)) if len(self._firmware_data) > 0 else 0
        print(f"[DATA] Read offset={start}, len={len(chunk)}, progress={percent}%")

        return list(chunk)


class OtaControlCharacteristic(ServiceInterface):
    """Control characteristic - receives offset from client."""

    def __init__(self, data_char: OtaDataCharacteristic):
        super().__init__(GATT_CHAR_IFACE)
        self._data_char = data_char

    @dbus_property()
    def UUID(self) -> "s":
        return OTA_CONTROL_UUID

    @dbus_property()
    def Service(self) -> "o":
        return "/org/bluez/ota/service0"

    @dbus_property()
    def Flags(self) -> "as":
        return ["write", "write-without-response"]

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}"):
        if len(value) >= 4:
            offset = struct.unpack("<I", bytes(value[:4]))[0]
            self._data_char.set_offset(offset)


class OtaServer:
    """BLE OTA GATT Server using BlueZ D-Bus API."""

    def __init__(self, firmware_path: str):
        self._fw_path = firmware_path
        self._fw_data = Path(firmware_path).read_bytes()
        self._fw_name = Path(firmware_path).name
        self._bus = None

    async def run(self):
        print(f"BLE OTA Server")
        print(f"Firmware: {self._fw_path}")
        print(f"Size: {len(self._fw_data)} bytes ({len(self._fw_data) / 1024:.1f} KB)")
        print(f"Chunk size: {CHUNK_SIZE} bytes")
        print()

        self._bus = await MessageBus(bus_type=BusType.SYSTEM).connect()

        # Get adapter
        introspection = await self._bus.introspect(BLUEZ_SERVICE, "/org/bluez/hci0")
        adapter_obj = self._bus.get_proxy_object(BLUEZ_SERVICE, "/org/bluez/hci0", introspection)

        # Power on adapter
        adapter_props = adapter_obj.get_interface("org.freedesktop.DBus.Properties")
        await adapter_props.call_set(ADAPTER_IFACE, "Powered", Variant("b", True))

        # Register GATT application
        await self._register_gatt_application()

        # Register advertisement
        await self._register_advertisement()

        print("Advertising OTA service... waiting for M5Dial to connect")
        print("Press Ctrl+C to stop")
        print()

        try:
            await asyncio.Event().wait()
        except asyncio.CancelledError:
            pass

    async def _register_gatt_application(self):
        # Create service
        service = OtaService()
        self._bus.export("/org/bluez/ota/service0", service)

        # Create characteristics
        data_char = OtaDataCharacteristic(self._fw_data)
        info_char = OtaInfoCharacteristic(self._fw_data, self._fw_name)
        control_char = OtaControlCharacteristic(data_char)

        self._bus.export("/org/bluez/ota/service0/char0", info_char)
        self._bus.export("/org/bluez/ota/service0/char1", data_char)
        self._bus.export("/org/bluez/ota/service0/char2", control_char)

        # Export ObjectManager for the application
        app_iface = _ApplicationInterface()
        self._bus.export("/org/bluez/ota", app_iface)

        # Register with BlueZ
        introspection = await self._bus.introspect(BLUEZ_SERVICE, "/org/bluez/hci0")
        gatt_mgr_obj = self._bus.get_proxy_object(BLUEZ_SERVICE, "/org/bluez/hci0", introspection)
        gatt_mgr = gatt_mgr_obj.get_interface(GATT_MGR_IFACE)

        await gatt_mgr.call_register_application("/org/bluez/ota", {})
        print("GATT application registered")

    async def _register_advertisement(self):
        adv = OtaAdvertisement()
        self._bus.export("/org/bluez/ota/adv0", adv)

        introspection = await self._bus.introspect(BLUEZ_SERVICE, "/org/bluez/hci0")
        adv_mgr_obj = self._bus.get_proxy_object(BLUEZ_SERVICE, "/org/bluez/hci0", introspection)
        adv_mgr = adv_mgr_obj.get_interface(LE_ADV_MGR_IFACE)

        await adv_mgr.call_register_advertisement("/org/bluez/ota/adv0", {})
        print("Advertisement registered")


class _ApplicationInterface(ServiceInterface):
    """ObjectManager interface for GATT application."""

    def __init__(self):
        super().__init__("org.freedesktop.DBus.ObjectManager")

    @method()
    def GetManagedObjects(self) -> "a{oa{sa{sv}}}":
        return {
            "/org/bluez/ota/service0": {
                GATT_SERVICE_IFACE: {
                    "UUID": Variant("s", OTA_SERVICE_UUID),
                    "Primary": Variant("b", True),
                }
            },
            "/org/bluez/ota/service0/char0": {
                GATT_CHAR_IFACE: {
                    "UUID": Variant("s", OTA_INFO_UUID),
                    "Service": Variant("o", "/org/bluez/ota/service0"),
                    "Flags": Variant("as", ["read"]),
                }
            },
            "/org/bluez/ota/service0/char1": {
                GATT_CHAR_IFACE: {
                    "UUID": Variant("s", OTA_DATA_UUID),
                    "Service": Variant("o", "/org/bluez/ota/service0"),
                    "Flags": Variant("as", ["read"]),
                }
            },
            "/org/bluez/ota/service0/char2": {
                GATT_CHAR_IFACE: {
                    "UUID": Variant("s", OTA_CONTROL_UUID),
                    "Service": Variant("o", "/org/bluez/ota/service0"),
                    "Flags": Variant("as", ["write", "write-without-response"]),
                }
            },
        }


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <firmware.bin>")
        print()
        print("Example:")
        print(f"  {sys.argv[0]} m5dial-firmware/build/m5dial.bin")
        sys.exit(1)

    fw_path = sys.argv[1]
    if not os.path.isfile(fw_path):
        print(f"Error: File not found: {fw_path}")
        sys.exit(1)

    server = OtaServer(fw_path)

    try:
        asyncio.run(server.run())
    except KeyboardInterrupt:
        print("\nStopped")


if __name__ == "__main__":
    main()
