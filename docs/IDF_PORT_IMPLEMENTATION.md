# ESP-IDF Port Implementation

The driver core remains portable by requiring applications to inject transport and timing callbacks. When `Config::nowMs` is null, health timestamps are `0`; Sleep wake gating still depends on caller-supplied time passed to `tick()`. Framework time sources belong in examples or application glue.

The native ESP-IDF example owns only example-local resources:

- `i2c_new_master_bus`, `i2c_master_transmit`, `i2c_master_transmit_receive`
- `esp_timer_get_time()` through `Config::nowMs`
- `vTaskDelay()` for the CLI loop
- fixed command buffers for console input

The Arduino example and ESP-IDF example share a command contract, not implementation source. Device ID, HS/Sleep diagnostics, and memory accesses continue to use the core driver's runtime variant metadata and range validation.

The IDF example implements read-only memory inspection commands directly:
`read`/`dump`/`hexdump`, `text`, `strings`, `crc`, `verify`, `current`, Device
ID reads, variant listing, HS/Sleep diagnostics, and driver diagnostics.
Destructive FRAM commands
are split into unconfirmed and confirmed forms. Unconfirmed `write`, `fill`,
`selftest`, `rw_suite`, `stress`, `stress_mix`, `randbench`, and `typed_demo`
commands print the exact `!` form required before changing memory.

The native IDF transport also implements the optional special callback for
HS-prefixed transfers and Sleep entry/wake sequences. This demonstrates the
protocol path but does not prove real 3.4 MHz operation or Sleep current on
hardware until board validation is recorded.
