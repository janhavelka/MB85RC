# MB85RC Driver Library

Production-grade MB85RC256V FRAM I2C driver for ESP32-S2 / ESP32-S3 using Arduino and PlatformIO.

## Features

- Injected I2C transport with no `Wire` dependency in library code
- Health monitoring with `READY`, `DEGRADED`, and `OFFLINE` states
- Deterministic managed-synchronous lifecycle: `begin()`, `tick()`, `end()`
- Chunked read/write support with rollover at `0x7FFF -> 0x0000`
- Current-address read support for the documented internal address-pointer flow, including multi-byte helper coverage
- Device ID verification on `begin()` (`Manufacturer ID = 0x00A`, `Product ID = 0x510`)
- Raw Device ID access and verify/compare helpers for diagnostics
- Runtime settings snapshot API for examples and diagnostics

## Installation

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps =
  https://github.com/janhavelka/MB85RC.git
```

### Manual

Copy `include/MB85RC/` and `src/` into your project.

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

The example transport adapter maps Arduino `Wire` failures to `I2C_*` status codes and keeps bus timeout ownership outside the library. If `Config::nowMs` is not provided, the driver falls back to `millis()`.

## API Reference

### Lifecycle

- `Status begin(const Config& config)` - initialize driver and verify Device ID
- `void tick(uint32_t nowMs)` - reserved no-op for FRAM
- `void end()` - shut down driver and clear runtime state

### Memory Operations

- `Status readByte(uint16_t addr, uint8_t& out)` - read one byte
- `Status read(uint16_t addr, uint8_t* buf, size_t len)` - read a block with chunking and rollover
- `Status readCurrentAddress(uint8_t& out)` - read from the device's current internal address pointer
- `Status readCurrentAddress(uint8_t* buf, size_t len)` - repeat documented current-address reads into a buffer
- `Status writeByte(uint16_t addr, uint8_t value)` - write one byte
- `Status write(uint16_t addr, const uint8_t* data, size_t len)` - write a block with chunking and rollover
- `Status fill(uint16_t addr, uint8_t value, size_t len)` - fill a region with a constant byte
- `Status verify(uint16_t addr, const uint8_t* expected, size_t len, VerifyResult& out)` - compare FRAM contents against expected bytes

### Diagnostics

- `Status probe()` - check presence without health tracking
- `Status recover()` - manual recovery attempt
- `Status readDeviceId(DeviceId& out)` - read manufacturer, product, and density fields
- `Status readDeviceIdRaw(DeviceIdRaw& out)` - read the raw 3-byte Device ID payload
- `Status getSettings(SettingsSnapshot& out)` - snapshot active config/runtime state without I2C

### State And Health

- `DriverState state() const`
- `bool isInitialized() const`
- `bool isOnline() const`
- `const Config& getConfig() const`
- `uint32_t lastOkMs() const`
- `uint32_t lastErrorMs() const`
- `Status lastError() const`
- `uint8_t consecutiveFailures() const`
- `uint32_t totalFailures() const`
- `uint32_t totalSuccess() const`

## MB85RC256V Overview

| Property | Value |
|----------|-------|
| Technology | FRAM (Ferroelectric RAM) |
| Capacity | 32 KB (256 Kbit) |
| Address range | `0x0000` - `0x7FFF` |
| I2C address | `0x50` - `0x57` via A2/A1/A0 |
| I2C speed | Up to 1 MHz |
| Write endurance | `10^12` read/write cycles per byte |
| Data retention | 10 years at 85 C |
| Write latency | None; no write-cycle delay or ACK polling |

## Notes

- `read()`, `write()`, and `fill()` intentionally preserve the documented rollover from `0x7FFF` to `0x0000`.
- `readCurrentAddress()` is only meaningful after a successful addressed memory read/write because the current address is undefined after power-on.
- The bulk `readCurrentAddress(uint8_t*, size_t)` helper repeats the documented current-address read primitive while preserving tracked pointer behavior.
- `verify()` reports the first mismatch without inventing a synthetic device error code; transport failures still return normal `Status` errors.
- The `WP` pin is hardware-only. The driver does not control or sense write-protect state over I2C.
- The datasheet software-reset bus sequence is transport-owned by design because the library never drives SDA/SCL directly.

## Examples

- `examples/01_basic_bringup_cli/`
  - `cfg` / `settings` for runtime/config snapshots
  - `read` / `dump` / `hexdump` for wrap-aware hex+ASCII memory dumps
  - `text`, `strings`, `crc`, and `verify` for live memory inspection on hardware
  - `current` / `cur` for current-address reads
  - `id` / `idraw` for parsed and raw Device ID visibility
  - `drv`, `probe`, `recover`, `selftest`, `stress`, `stress_mix` for diagnostics

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
| `CliShell.h` | Simple serial shell helper |
| `CommandHandler.h` | Example command parsing helpers |
| `HealthView.h` | Compact health display helper |
| `HealthDiag.h` | Verbose health diagnostics helper |
| `TransportAdapter.h` | Transport alias helper |

## Behavioral Contracts

1. Threading model: single-threaded by default; not thread-safe.
2. Timing model: `tick()` is bounded and currently a no-op; public I2C operations are blocking.
3. Resource ownership: bus, pins, and timeout policy remain application-owned via `Config`.
4. Memory behavior: no heap allocation in steady-state library operation.
5. Error handling: all fallible APIs return `Status`; no exceptions and no silent failures.

## Validation

```bash
pio test -e native
python tools/check_cli_contract.py
python tools/check_core_timing_guard.py
pio run -e esp32s3dev
```

## Documentation

- `CHANGELOG.md` - release history
- `docs/IDF_PORT.md` - ESP-IDF portability notes
- `MB85RC256V_fram_implementation_manual.md` - extracted device behavior reference used for implementation review

## License

MIT License. See [LICENSE](LICENSE).
