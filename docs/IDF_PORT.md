# ESP-IDF Port

The core library is framework-neutral. Public headers and `src/` do not include
Arduino or ESP-IDF framework headers, and all hardware access is supplied
through `Config` callbacks.

The native ESP-IDF example in `examples/espidf_basic` owns only example-local
resources:

- entry point: `app_main()`
- I2C: `driver/i2c_master.h`
- timing hook: `esp_timer_get_time()` through `Config::nowMs`
- CLI loop delay: `vTaskDelay()`
- command input: fixed C buffers and `fgets()`

The Arduino and ESP-IDF examples share a command contract, not implementation
source. The IDF example must not include Arduino sources or compatibility
facades such as `Arduino.h`, `Wire.h`, `String`, `Serial`, or `TwoWire`.
`tools/check_idf_example_contract.py` enforces this native-IDF boundary and the
expected command coverage.

## Command Coverage

The native IDF CLI exposes the same driver-facing workflows as the Arduino CLI:

- variant selection, active capacity, and Device ID diagnostics
- addressed read, dump/hexdump, text, strings, CRC, and verify commands
- current-address reads for diagnostics
- HS/Sleep support, entry/wake, and driver diagnostics
- stress, selftest, random benchmark, and typed demo commands

Destructive FRAM commands require explicit confirmation forms:

- `write! <addr> <byte> [byte...]`
- `fill! <addr> <value> <len>`
- `selftest!`
- `rw_suite!`
- `stress! [N]`
- `stress_mix! [N]`
- `randbench! [N]`
- `typed_demo!`

Unconfirmed forms print the affected operation and exact `!` command required
instead of writing memory.

## Transport Notes

The native IDF transport implements normal write/write-read callbacks and the
optional special callback for HS-prefixed transfers, Sleep entry, and Sleep wake
stimulus. This demonstrates the protocol path, but it does not prove real
3.4 MHz operation or Sleep current on hardware until board validation is
recorded.

When `Config::nowMs` is null, core health timestamps remain `0`. The IDF
example supplies `nowMs` from `esp_timer_get_time() / 1000`, intentionally
matching the driver's `uint32_t` millisecond contract.

ESP-IDF hardware validation remains pending unless a target board, wiring,
command log, commit, and result are recorded separately.
