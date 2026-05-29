# ESP-IDF Port Implementation

The driver core remains portable by requiring applications to inject transport and timing callbacks. When `Config::nowMs` is null, health timestamps are `0`; framework time sources belong in examples or application glue.

The native ESP-IDF example owns only example-local resources:

- `i2c_new_master_bus`, `i2c_master_transmit`, `i2c_master_transmit_receive`
- `esp_timer_get_time()` through `Config::nowMs`
- `vTaskDelay()` for the CLI loop
- fixed command buffers for console input

The Arduino example and ESP-IDF example share a command contract, not implementation source. Device ID and memory accesses continue to use the core driver's runtime variant metadata and range validation.

The IDF example implements read-only memory inspection commands directly:
`read`/`dump`/`hexdump`, `text`, `strings`, `crc`, `verify`, `current`, Device
ID reads, variant listing, and driver diagnostics. Destructive FRAM commands
are split into unconfirmed and confirmed forms. Unconfirmed `write`, `fill`,
`selftest`, `rw_suite`, `stress`, `stress_mix`, `randbench`, and `typed_demo`
commands print the exact `!` form required before changing memory.
