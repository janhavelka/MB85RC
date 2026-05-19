# MB85RC ESP-IDF Port

Scope: keep the supported MB85RC-family driver usable from both
Arduino/PlatformIO and pure ESP-IDF while preserving the same bring-up example
functionality.

## Result

- Core driver remains framework-neutral for I2C ownership. All bus access still
  goes through `Config::i2cWrite` and `Config::i2cWriteRead`.
- Runtime variant selection is framework-neutral. `DeviceVariant::AUTO`,
  `MB85RC04V`, `MB85RC64TA`, `MB85RC256V`, `MB85RC512T`, and `MB85RC1MT`
  use the same callback-only transport path in both frameworks. `MB85RC16V`
  is also supported when selected explicitly because it has no Device ID
  command for AUTO discovery.
- `src/MB85RC.cpp` no longer requires Arduino headers for ESP-IDF builds.
- If `Config::nowMs` is not supplied, Arduino/native-test builds use
  `millis()` and ESP-IDF builds use `esp_timer_get_time() / 1000`.
- Root `CMakeLists.txt` and `idf_component.yml` make the library consumable as
  an ESP-IDF component.
- `examples/espidf_basic` builds the same CLI implementation used by
  `examples/01_basic_bringup_cli`.

## Shared Example Strategy

The Arduino CLI is the source of truth. The ESP-IDF example sets
`MB85RC_EXAMPLE_PLATFORM_IDF=1`, includes `examples/common/IdfArduinoCompat.h`,
defines `Serial` and `Wire`, then includes the Arduino CLI source.

This keeps these flows aligned across both frameworks:

- scan and bus diagnostics
- Device ID and raw Device ID commands
- active-capacity-bounded read, write, fill, text, strings, CRC, and verify commands
- current-address reads
- settings and health views
- interface reset
- self-test, read/write suite, random benchmark, typed demo, stress, and stress_mix

## ESP-IDF Example Glue

`examples/common/IdfArduinoCompat.h` is example-only. It is not part of the
driver API.

It provides:

- `millis`, `micros`, `delay`, `delayMicroseconds`, and `yield`
- GPIO helpers used by bus recovery and interface reset
- a fixed-capacity `String` subset used by the CLI parser
- a nonblocking stdin/stdout `Serial` replacement
- a `TwoWire`-shaped adapter backed by ESP-IDF v6 `driver/i2c_master.h`
- direct `writeStatus` and `writeReadStatus` helpers used by
  `examples/common/I2cTransport.h` under ESP-IDF

The adapter preserves MB85RC-specific transport behavior:

- normal memory reads use `i2c_master_transmit_receive()`
- current-address reads use `i2c_master_receive()` when `txLen == 0`
- Device ID reads use reserved 7-bit address `0x7C` through ESP-IDF defined
  I2C operations with manual address bytes and a handle configured with
  `I2C_DEVICE_ADDRESS_NOT_USED`
- write-only fallback through `writeReadStatus(..., rxLen == 0, ...)` uses
  `i2c_master_transmit()`
- interface reset stays in example glue and never moves into the driver

ESP-IDF errors map to library `Status` values:

- `ESP_OK` -> `Status::Ok()`
- `ESP_ERR_TIMEOUT` -> `Err::I2C_TIMEOUT`
- `ESP_ERR_INVALID_ARG` -> `Err::INVALID_PARAM`
- `ESP_ERR_INVALID_RESPONSE` / `ESP_ERR_NOT_FOUND` -> `Err::I2C_BUS`
- other errors -> `Err::I2C_ERROR`

Timeouts are clamped before passing into ESP-IDF transfer APIs so an overflow
cannot become an infinite wait.

## Component Files

Core component:

```cmake
idf_component_register(
  SRCS "src/MB85RC.cpp"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES esp_timer
)
```

Example component:

```cmake
idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "." "../../common" "../../.."
  REQUIRES MB85RC esp_driver_i2c esp_driver_gpio esp_timer freertos vfs
)
```

The IDF example targets ESP32-S2 and ESP32-S3 and requires ESP-IDF `>=6.0.1`.

## Remaining Integration Notes

- Applications still own SDA/SCL pins, pull-ups, I2C clock, optional WP GPIO,
  and bus lifetime. The library does not create buses or devices.
- Applications should set `Config::expectedVariant = DeviceVariant::AUTO` for
  Device-ID-capable parts, or set an explicit selector (`MB85RC04V`,
  `MB85RC16V`, `MB85RC64TA`, `MB85RC256V`, `MB85RC512T`, `MB85RC1MT`) when the
  board BOM is fixed. The legacy default remains `MB85RC256V` so existing 256V
  projects keep their old validation behavior. `MB85RC16V` must be explicit
  because it has no Device ID command.
- Bulk memory operations intentionally reject ranges that exceed
  `capacityBytes()`; firmware should split or shorten requests instead of
  expecting wraparound.
- A real ESP-IDF application can either reuse the example adapter or provide a
  smaller project-specific adapter directly from its own `i2c_master_dev_handle_t`.
- The example adapter already avoids normal device-handle addressing for the
  reserved Device ID address. Hardware validation still has to prove the target
  FRAM ACKs the defined-operation sequence under the selected IDF version.
- The CLI shim is intentionally narrow. If the Arduino example starts using more
  of Arduino `String`, `Print`, or `TwoWire`, extend the shim in the same commit
  as the example change.

## Validation

Completed locally:

- `python -m platformio test -e native`
- `python -m platformio run -e esp32s3dev`
- `python -m platformio run -e esp32s2dev`
- `python tools/check_cli_contract.py`
- `python tools/check_core_timing_guard.py`
- `python scripts/generate_version.py check`
- `doxygen Doxyfile`

`tools/check_cli_contract.py` verifies both the Arduino CLI command surface and
the ESP-IDF wrapper contract: platform macro, `IdfArduinoCompat.h`, shared
source include, `app_main()`, required IDF CMake dependencies, Device ID
reserved-address shim invariants, and core Device ID address construction.

Pending in this shell:

- `idf.py build` for `examples/espidf_basic`

`idf.py` was not available on PATH during this audit, so the ESP-IDF example is
implemented and documented but still needs a real ESP-IDF toolchain build before
release.
