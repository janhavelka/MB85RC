# MB85RC Driver Library

Production-grade MB85RC-family FRAM I2C driver for ESP32-S2 / ESP32-S3 using Arduino/PlatformIO and ESP-IDF.

Library version: `v2.0.0`

## Features

- Injected I2C transport with no `Wire` dependency in library code
- Health monitoring with `READY`, `DEGRADED`, and `OFFLINE` states
- Deterministic managed-synchronous lifecycle: `begin()`, `tick()`, `end()`
- Runtime variant selection for `MB85RC04V`, `MB85RC16V`, `MB85RC64TA`, `MB85RC256V`, `MB85RC512T`, and `MB85RC1MT`
- Chunked read/write support bounded by the active variant capacity
- Current-address read support for the documented internal address-pointer flow, including multi-byte helper coverage
- Device ID verification on `begin()` where supported (`Manufacturer ID = 0x00A`, variant-specific Product ID), with memory-probe presence checks for no-Device-ID variants
- Raw Device ID access where available and verify/compare helpers for diagnostics
- Runtime settings snapshot API for examples and diagnostics
- Manual recovery that records transport failures and Device ID mismatches in health tracking

## Installation

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps =
  https://github.com/janhavelka/MB85RC.git#v2.0.0
```

### Manual

Copy `include/MB85RC/` and `src/` into your project.

### ESP-IDF Component

This repository also builds as a pure ESP-IDF component. Add the repo as an
extra component or dependency, then include `MB85RC/MB85RC.h` and provide
`Config::i2cWrite` / `Config::i2cWriteRead` callbacks from your project-owned
I2C master bus.

The full bring-up CLI is shared between Arduino and ESP-IDF:

```bash
cd examples/espidf_basic
idf.py set-target esp32s3
idf.py build
```

The ESP-IDF example uses `driver/i2c_master.h` through
`examples/common/IdfArduinoCompat.h` so it exposes the same commands and serial
output as `examples/01_basic_bringup_cli`, including Device ID reads,
current-address reads, active-capacity-bounded memory commands, typed demo, random benchmark,
self-test, and stress diagnostics.

Validation status: command parity is structural through shared source. Native
tests and Arduino ESP32-S2/S3 example builds passed during this port pass; pure
ESP-IDF `idf.py` builds and hardware smoke tests are still pending until an IDF
toolchain and target devices are available.

## Quick Start

```cpp
#include <Wire.h>
#include "MB85RC/MB85RC.h"
#include "common/I2cTransport.h"

MB85RC::MB85RC device;

void setup() {
  Serial.begin(115200);
  transport::initWire(8, 9, 400000, 50);

  MB85RC::Config cfg;
  cfg.i2cWrite = transport::wireWrite;
  cfg.i2cWriteRead = transport::wireWriteRead;
  cfg.i2cUser = &Wire;
  cfg.i2cAddress = 0x50;
  cfg.expectedVariant = MB85RC::DeviceVariant::AUTO; // or an explicit DeviceVariant

  MB85RC::Status st = device.begin(cfg);
  if (!st.ok()) {
    Serial.printf("Init failed: %s\n", st.msg);
    return;
  }

  device.writeByte(0x0000, 0x42);

  uint8_t value = 0;
  if (device.readByte(0x0000, value).ok()) {
    Serial.printf("Read: 0x%02X\n", value);
  }
}

void loop() {
  device.tick(millis());
}
```

The default `expectedVariant` is `MB85RC256V` for compatibility with existing users. New integrations should set `DeviceVariant::AUTO` or an explicit expected variant so `begin()`, `probe()`, and `recover()` validate the actual Device ID and capacity. `MB85RC16V` has no Device ID command, so select it explicitly with `DeviceVariant::MB85RC16V`; `AUTO` cannot discover it.

The example transport adapter maps Arduino `Wire` failures to `I2C_*` status codes and keeps bus timeout ownership outside the library. If `Config::nowMs` is not provided, the driver falls back to `millis()` on Arduino/native-test builds and `esp_timer_get_time()` on ESP-IDF builds.

## Release 2.0.0 Highlights

- Runtime support now covers every locally documented variant: `MB85RC04V`, `MB85RC16V`, `MB85RC64TA`, `MB85RC256V`, `MB85RC512T`, and `MB85RC1MT`.
- Runtime variant selection through `Config::expectedVariant` with `AUTO` and explicit selectors.
- `begin()`, `probe()`, and `recover()` validate the selected active Device ID where available instead of hard-coding the 256V product ID.
- `capacityBytes()`, `maxAddress()`, `variantName()`, `variantInfo()`, and `deviceId()` expose the active runtime device.
- Memory APIs and `maxAddress()` now use `uint32_t` addresses so the 128 KB `MB85RC1MT` range (`0x00000..0x1FFFF`) is not truncated.
- Address encoding is centralized for one-byte small-density devices, two-byte address-pin devices, and `MB85RC1MT` A16-in-device-address transactions.
- `MB85RC16V` is supported through explicit selection and memory-probe diagnostics because the part has no Device ID command.
- `read`, `write`, `fill`, `verify`, typed helpers, and CLI commands reject ranges that cross the active capacity.
- The shared Arduino/ESP-IDF CLI accepts 32-bit addresses and preserves the same command surface in both frameworks.
- Native tests cover explicit begin, AUTO selection where Device ID exists, address encoding, cross-end rejection, current-address behavior, and no-Device-ID diagnostics across the variant matrix.

## Release 1.1.1 Highlights

- Bringup CLI status output is quieter and more consistent for successful diagnostic/demo commands.
- Stress progress keeps color on success/fail counters only, so the percentage remains plain text.
- Stress summaries now distinguish cycles, mixed commands, and tracked I2C health transactions.
- Serial command input now uses the shared bounded CLI shell helper for cleaner monitor output.

## Release 1.1.0 Highlights

- Latched `OFFLINE` behavior keeps normal I2C operations off the bus until explicit `recover()` succeeds.
- Public MB85RC family variant metadata documents family address models.
- Cross-library diagnostics have `driverState()` and a value-returning `getSettings()` overload.
- Validation and recovery paths keep health counters aligned with transport failures and Device ID mismatches.
- The bringup CLI and documentation cover current validation, health, and family-reference behavior.

## API Reference

### Lifecycle

- `Status begin(const Config& config)` - initialize driver and verify Device ID or explicit no-Device-ID presence
- `void tick(uint32_t nowMs)` - reserved no-op for FRAM
- `void end()` - shut down driver and clear runtime state

### Variant Selection

- `Config::expectedVariant` - `MB85RC256V` by default, or `AUTO` / an explicit `DeviceVariant` for runtime selection.
- `const cmd::VariantInfo* variantInfo() const` - active variant metadata after `begin()`.
- `const char* variantName() const` - active variant name, or `unknown` before selection.
- `DeviceId deviceId() const` - cached Device ID from the last successful `begin()` / `recover()` validation.
- `uint32_t capacityBytes() const` - active runtime capacity.
- `uint32_t maxAddress() const` - active highest valid memory address.
- `static constexpr uint16_t memorySize()` - legacy MB85RC256V size helper retained for existing users.

### Memory Operations

- `Status readByte(uint32_t addr, uint8_t& out)` - read one byte
- `Status read(uint32_t addr, uint8_t* buf, size_t len)` - read a contiguous block with chunking
- `Status readCurrentAddress(uint8_t& out)` - read from the device's current internal address pointer
- `Status readCurrentAddress(uint8_t* buf, size_t len)` - repeat documented current-address reads into a buffer
- `Status writeByte(uint32_t addr, uint8_t value)` - write one byte
- `Status write(uint32_t addr, const uint8_t* data, size_t len)` - write a contiguous block with chunking
- `Status fill(uint32_t addr, uint8_t value, size_t len)` - fill a region with a constant byte
- `Status verify(uint32_t addr, const uint8_t* expected, size_t len, VerifyResult& out)` - compare FRAM contents against expected bytes

### Diagnostics

- `Status probe()` - check presence without health tracking
- `Status recover()` - manual recovery attempt
- `Status readDeviceId(DeviceId& out)` - read manufacturer, product, and density fields where supported
- `Status readDeviceIdRaw(DeviceIdRaw& out)` - read the raw 3-byte Device ID payload where supported
- `Status getSettings(SettingsSnapshot& out)` - snapshot active config/runtime state without I2C
- `SettingsSnapshot getSettings() const` - value-returning snapshot helper for diagnostics

### State And Health

- `DriverState state() const`
- `DriverState driverState() const`
- `bool isInitialized() const`
- `bool isOnline() const`
- `const Config& getConfig() const`
- `uint32_t capacityBytes() const`
- `uint32_t maxAddress() const`
- `uint32_t lastOkMs() const`
- `uint32_t lastErrorMs() const`
- `Status lastError() const`
- `uint8_t consecutiveFailures() const`
- `uint32_t totalFailures() const`
- `uint32_t totalSuccess() const`

## Supported Runtime Variants

| Variant | Product ID | Capacity | Address range | Config selector |
|---------|------------|----------|---------------|-----------------|
| `MB85RC04V` | `0x010` | 512 B (4 Kbit) | `0x0000` - `0x01FF` | `DeviceVariant::MB85RC04V` / `AUTO` |
| `MB85RC16V` | n/a | 2 KB (16 Kbit) | `0x0000` - `0x07FF` | `DeviceVariant::MB85RC16V` only |
| `MB85RC64TA` | `0x358` | 8 KB (64 Kbit) | `0x0000` - `0x1FFF` | `DeviceVariant::MB85RC64TA` |
| `MB85RC256V` | `0x510` | 32 KB (256 Kbit) | `0x0000` - `0x7FFF` | `DeviceVariant::MB85RC256V` |
| `MB85RC512T` | `0x658` | 64 KB (512 Kbit) | `0x0000` - `0xFFFF` | `DeviceVariant::MB85RC512T` / `AUTO` |
| `MB85RC1MT` | `0x758` | 128 KB (1 Mbit) | `0x00000` - `0x1FFFF` | `DeviceVariant::MB85RC1MT` / `AUTO` |

`AUTO` uses the Device ID command and therefore works only on variants that implement Device ID. `MB85RC16V` must be selected explicitly. Small-density variants use upper memory-address bits in the I2C device address; `MB85RC1MT` uses A16 in the I2C device address; the driver derives those transaction addresses from `Config::i2cAddress`.

## Notes

- `read()`, `write()`, `fill()`, and `verify()` reject ranges where `address + len > capacityBytes()`; they do not silently wrap bulk operations.
- `readCurrentAddress()` is only meaningful after a successful addressed memory read/write because the current address is undefined after power-on.
- The bulk `readCurrentAddress(uint8_t*, size_t)` helper repeats the documented current-address read primitive while preserving tracked pointer behavior and rejecting cross-capacity reads.
- Argument validation errors reject null buffers, zero lengths, and out-of-range start addresses before touching the bus or health counters.
- `recover()` invalidates current-address tracking and records both I2C failures and Device ID mismatches in driver health.
- `verify()` reports the first mismatch without inventing a synthetic device error code; transport failures still return normal `Status` errors.
- The `WP` pin is hardware-only and non-permanent. High disables writes to the entire array, low or open enables writes, and reads still work.
- There is no software block-protect register, OTP lock region, or permanent write lock in this device family.
- The datasheet software-reset bus sequence is transport-owned by design because the library never drives SDA/SCL directly.
- Typed storage policy is intentionally kept out of the core driver. If you need fixed-width numeric encoding, use an explicit codec layer such as `examples/common/TypedMemory.h`.

## Examples

- `examples/01_basic_bringup_cli/`
  - `cfg` / `settings` for runtime/config snapshots
  - `read` / `dump` / `hexdump` for active-capacity-bounded hex+ASCII memory dumps
  - `text`, `strings`, `crc`, and `verify` for live memory inspection on hardware
  - `current` / `cur` for current-address reads
  - `id` / `idraw` for parsed and raw Device ID visibility
  - `drv`, `probe`, `recover`, `selftest`, `stress`, `stress_mix` for diagnostics
  - `rw_suite` for safe read/write/fill/verify coverage with restore
  - `randbench [N]` for random-access timing over a scratch window with compact restore status
  - `typed_demo` for fixed-width integer/float/double storage with compact pass/fail status

- `examples/espidf_basic/`
  - Pure ESP-IDF build of the same bring-up CLI.
  - Includes the Arduino example source with `MB85RC_EXAMPLE_PLATFORM_IDF=1`.
  - Supplies a fixed-capacity `String`/serial/GPIO/Wire-compatible shim backed by ESP-IDF v6 `i2c_master_*` APIs.
  - Explicitly supports current-address reads (`txLen == 0`) and Device ID reads through reserved address `0x7C` using ESP-IDF defined I2C operations with manual address bytes.
  - `tools/check_cli_contract.py` checks the full CLI command surface, IDF entry point, CMake dependency surface, Device ID reserved-address shim invariants, and core Device ID address construction so future wrapper edits cannot silently drop parity.

### CLI Inspection Examples

```text
hexdump 0x0000 128        # Hex + ASCII view of a region
text 0x0000 64            # Escaped text-oriented view
strings                   # Scan the whole chip for printable ASCII strings
strings 0x1000 512 6      # Scan a window with a minimum string length
crc 0x0000 1024           # CRC32 over a region for quick verification
verify 0x0020 55 55 55 55 # Compare live FRAM bytes against expected values
idraw                     # Show the raw 3-byte Device ID payload
current 16                # Read 16 bytes from the current internal address
rw_suite                  # Run a deterministic read/write/fill/verify suite
randbench 4096            # Time 4096 random writes + 4096 random reads
typed_demo                # Demonstrate explicit typed value storage
```

### Example Helpers

`examples/common/` is example-only glue and is not part of the public library API.

| File | Purpose |
|------|---------|
| `BoardConfig.h` | Board-specific pin defaults and `Wire` setup |
| `BuildConfig.h` | Compile-time log-level configuration |
| `Log.h` | Serial logging helpers |
| `I2cTransport.h` | Wire-backed transport adapter |
| `I2cScanner.h` | Bus scan helper |
| `BusDiag.h` | Bus diagnostics wrapper |
| `CliStyle.h` | CLI prompt, help, and color formatting helpers |
| `CliShell.h` | Simple serial shell helper |
| `CommandHandler.h` | Legacy char-buffer command parsing helpers |
| `HealthView.h` | Compact health display helper |
| `HealthDiag.h` | Verbose health diagnostics helper |
| `TransportAdapter.h` | Transport alias helper |
| `TypedMemory.h` | Example-only fixed-width integer/float/double codec on top of the raw driver |

## Behavioral Contracts

1. Threading model: single-threaded by default; not thread-safe.
2. Timing model: `tick()` is bounded and currently a no-op; public I2C operations are blocking.
3. Resource ownership: bus, pins, and timeout policy remain application-owned via `Config`.
4. Memory behavior: no heap allocation in steady-state library operation; bulk memory APIs reject cross-end ranges instead of relying on device rollover.
5. Error handling: all fallible APIs return `Status`; no exceptions and no silent failures.
6. Health behavior: `OFFLINE` is latched. Normal public I2C operations return `BUSY` with `Driver is offline; call recover()` without touching the bus until `recover()` succeeds.

## Validation

```bash
pio test -e native
python tools/check_cli_contract.py
python tools/check_core_timing_guard.py
pio run -e esp32s3dev
pio run -e esp32s2dev

# Build the ESP-IDF full CLI example (requires ESP-IDF on PATH)
cd examples/espidf_basic
idf.py build
doxygen Doxyfile
```

## Documentation

- `CHANGELOG.md` - release history and GitHub release note source
- `docs/IDF_PORT.md` - ESP-IDF portability notes
- `docs/IDF_PORT_IMPLEMENTATION.md` - ESP-IDF implementation and audit closure notes
- `docs/MB85RC04V-DS5v1-E.pdf` - 04V one-byte address/A8 transaction reference
- `docs/MB85RC16V-DS11v0-E.pdf` - 16V no-Device-ID and A10:A8 transaction reference
- `docs/MB85RC64TA-DS5v1-E.pdf` - 64TA 8 KB runtime support reference
- `docs/MB85RC256V-Data-Sheet-DS501-00017-11v2-E.pdf` - 256V two-byte address reference
- `docs/MB85RC512T-DS6v1-E.pdf` - 512T 64 KB two-byte address reference
- `docs/MB85RC1MT-DS5v1-E.pdf` - 1MT A16-in-device-address transaction reference
- `docs/MB85RC256V-Fact-Sheet-NP501-00019-2v0-E.pdf` - short fact sheet used for cross-checking
- `docs/MB85RC256V_fram_implementation_manual.md` - extracted device behavior reference used for implementation review
- `Doxyfile` - indexes public headers, the ESP-IDF port notes, the shared Arduino
  CLI source, the native IDF entry point, and example-only framework shims

## License

MIT License. See [LICENSE](LICENSE).
