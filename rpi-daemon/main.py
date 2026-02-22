#!/usr/bin/env python3
"""RPi Monitor BLE daemon - main entry point."""

import asyncio
import logging
import signal
import sys

from ble_server import BLEServer

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("rpi-monitor")


async def main():
    server = BLEServer()

    loop = asyncio.get_event_loop()

    def shutdown():
        logger.info("Shutdown signal received")
        asyncio.ensure_future(server.stop())
        loop.stop()

    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, shutdown)

    try:
        await server.start()
        logger.info("RPi Monitor daemon is running. Press Ctrl+C to stop.")
        # Run forever
        await asyncio.Event().wait()
    except Exception as e:
        logger.error(f"Fatal error: {e}")
        await server.stop()
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())
