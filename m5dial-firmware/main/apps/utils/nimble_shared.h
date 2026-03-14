#pragma once

// Shared NimBLE initialization guard.
// Both BLE Scanner and RPi Monitor apps use NimBLE.
// The NimBLE host task must only be started once.
// Both apps check this flag before calling nimble_port_init().
extern bool g_nimble_initialized;
