# MB85RC ESP-IDF Port Implementation Notes

Date: 2026-05-19.
Branch: `feature/mb85rc-idf-port`.

## Scope

- Kept `include/MB85RC/` and `src/MB85RC.cpp` as a framework-neutral driver
  core with application-owned I2C callbacks.
- Added runtime-aware MB85RC64TA support on the same framework-neutral core:
  Device ID product `0x358`, 8 KB capacity, `0x0000..0x1FFF` bounds, and
  variant-specific address high-byte masking.
- Added ESP-IDF component metadata and a native IDF entry point for the full
  bring-up CLI.
- Added an example-only ESP-IDF compatibility layer so the Arduino and ESP-IDF
  examples share one CLI command implementation.
- Preserved the full MB85RC example surface across both frameworks: scan,
  diagnostics, Device ID reads, current-address reads, active-capacity read/write,
  fill, text, string, CRC, verify, settings, health, interface reset, self-test,
  read/write suite, random benchmark, typed demo, stress, and stress_mix flows.
  Memory commands are now active-capacity-bounded so the same CLI is safe on
  both MB85RC64TA and MB85RC256V.

## Files Added

- `CMakeLists.txt`
- `idf_component.yml`
- `examples/common/IdfArduinoCompat.h`
- `examples/espidf_basic/CMakeLists.txt`
- `examples/espidf_basic/main/CMakeLists.txt`
- `examples/espidf_basic/main/main.cpp`

## Audit Resolution

- `docs/IDF_PORT.md` blocker: missing root `CMakeLists.txt`.
  - Resolved with an IDF component that builds `src/MB85RC.cpp` and exports
    `include/`.
- `docs/IDF_PORT.md` blocker: missing `idf_component.yml`.
  - Resolved with metadata for ESP32-S2/S3 and IDF `>=6.0.1`.
- `docs/IDF_PORT.md` blocker: Arduino-only default timing fallback.
  - Resolved with `esp_timer_get_time()` for ESP-IDF builds when
    `Config::nowMs` is not provided.
- `docs/IDF_PORT.md` blocker: missing IDF example with Arduino feature parity.
  - Resolved with `examples/espidf_basic`, which includes the same
    `examples/01_basic_bringup_cli/main.cpp` command implementation under
    `MB85RC_EXAMPLE_PLATFORM_IDF=1`.
  - The IDF shim supplies the Arduino-shaped console, GPIO, timing, and
    `TwoWire` adapter surface through native ESP-IDF APIs.
- MB85RC transport pitfall:
  - The IDF adapter preserves current-address reads with `i2c_master_receive()`.
  - Device ID transactions on reserved address `0x7C` use
    `i2c_master_execute_defined_operations()` with manual address bytes through
    `I2C_DEVICE_ADDRESS_NOT_USED`, avoiding normal device-handle addressing for
    the reserved address.
  - `tools/check_cli_contract.py` statically guards the memory/Device ID/demo
    CLI command surface, the reserved-address IDF shim tokens, and the core
    Device ID address-byte construction.
- Arduino-ESP32 pitfall:
  - Do not infer native IDF mode from `ESP_PLATFORM`; Arduino-ESP32 defines it
    too. The shared CLI uses the explicit `MB85RC_EXAMPLE_PLATFORM_IDF` flag.
- TunnelMonitor MB85RC64TA gap:
  - `Config::expectedVariant` lets firmware request `MB85RC64TA` explicitly or
    use `AUTO`.
  - `begin()` selects and validates the active runtime variant from Device ID
    instead of accepting only Product ID `0x510`.
  - Runtime APIs expose `variantInfo()`, `variantName()`, `deviceId()`,
    `capacityBytes()`, and `maxAddress()`.
  - `readByte`, `read`, `writeByte`, `write`, `fill`, `verify`, and
    `readCurrentAddress(uint8_t*, size_t)` all validate against active
    capacity and reject cross-end operations.
  - `probe()` and `recover()` validate the already selected active variant.

## Remaining Hardware Checks

- Build the IDF example for `esp32s3` and `esp32s2`; this shell did not have
  `idf.py` on PATH during the implementation pass.
- Run scan/probe and disconnected-device timeout checks on hardware through
  both Arduino and ESP-IDF entry points.
- Verify Device ID, current-address read, active-capacity bounds, write protection,
  interface reset, CRC, typed demo, benchmark, stress, and bus-recovery flows on
  the target board.

## Verification

- `python -m platformio test -e native`: passed, including MB85RC64TA runtime
  selection, bounds, address encoding, probe, and recover coverage.
- `python -m platformio run -e esp32s3dev`: passed.
- `python -m platformio run -e esp32s2dev`: passed.
- `python tools/check_cli_contract.py`: passed.
- `python tools/check_core_timing_guard.py`: passed.
- `python scripts/generate_version.py check`: passed.
- `doxygen Doxyfile`: completed.
- `git diff --check`: passed during the implementation pass.
