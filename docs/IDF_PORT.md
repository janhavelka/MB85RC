# ESP-IDF Port

The core library is framework-neutral. Public headers and `src/` do not include Arduino or ESP-IDF framework headers, and all hardware access is supplied through `Config` callbacks.

The ESP-IDF example in `examples/espidf_basic` is a native IDF application:

- entry point is `app_main()`
- I2C uses `driver/i2c_master.h`
- timestamps use an injected `Config::nowMs` callback backed by `esp_timer_get_time()`
- delays use `vTaskDelay()`
- command input uses fixed C buffers and `fgets()`

MB85RC runtime variant support remains in the core driver and the IDF CLI exposes the same variant, size, device-id, raw/current-read, stress, selftest, and memory-boundary workflows as the Arduino command contract.

FRAM write/fill/stress/selftest/benchmark/demo commands can change device
contents. The native IDF example therefore uses explicit confirmed command
forms:

- `write! <addr> <byte> [byte...]`
- `fill! <addr> <value> <len>`
- `selftest!`
- `rw_suite!`
- `stress! [N]`
- `stress_mix! [N]`
- `randbench! [N]`
- `typed_demo!`

The unconfirmed forms print the affected operation and the exact confirmed
command form instead of writing memory.

The IDF example must not include Arduino sources or use Arduino compatibility facades. `tools/check_idf_example_contract.py` enforces the native-IDF boundary and command coverage.

ESP-IDF hardware validation is still pending unless a target board, wiring,
command log, and result are recorded separately.
