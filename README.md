# MB85RC Driver Library

Production-grade MB85RC256V FRAM I2C driver for ESP32 (Arduino/PlatformIO).

## Features

- **Injected I2C transport** - no Wire dependency in library code
- **Health monitoring** - automatic state tracking (READY/DEGRADED/OFFLINE)
- **Deterministic behavior** - no unbounded loops, no heap allocations
- **Managed synchronous lifecycle** - blocking I2C ops, no write delay (FRAM)
- **Chunked I/O** - large reads and writes split into bounded I2C transactions
- **Device ID verification** - validates manufacturer and product ID on begin()

## Installation

### PlatformIO (recommended)

Add to `platformio.ini`:

```ini
lib_deps = 
  https://github.com/janhavelka/MB85RC.git
```

### Manual

Copy `include/MB85RC/` and `src/` to your project.

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
  
  auto status = device.begin(cfg);
  if (!status.ok()) {
    Serial.printf("Init failed: %s\n", status.msg);
    return;
  }
  
  // Write a byte
  device.writeByte(0x0000, 0x42);
  
  // Read it back
  uint8_t value = 0;
  device.readByte(0x0000, value);
  Serial.printf("Read: 0x%02X\n", value);
}

void loop() {
  device.tick(millis());
}
```

The example adapter maps Arduino `Wire` failures to specific `I2C_*` status codes and keeps
bus timeout ownership in `transport::initWire()`. If you do not inject `Config::nowMs`, the
driver falls back to `millis()` on Arduino/native-test builds.

## Health Monitoring

The driver tracks I2C communication health:

```cpp
// Check state
if (device.state() == MB85RC::DriverState::OFFLINE) {
  Serial.println("Device offline!");
  device.recover();  // Try to reconnect
}

// Get statistics
Serial.printf("Failures: %u consecutive, %lu total\n",
              device.consecutiveFailures(), device.totalFailures());
```

### Driver States

| State | Description |
|-------|-------------|
| `UNINIT` | `begin()` not called or `end()` called |
| `READY` | Operational, no recent failures |
| `DEGRADED` | 1+ failures, below offline threshold |
| `OFFLINE` | Too many consecutive failures |

## API Reference

### Lifecycle

- `Status begin(const Config& config)` - Initialize driver, verify Device ID
- `void tick(uint32_t nowMs)` - No-op for FRAM (provided for interface uniformity)
- `void end()` - Shutdown driver

### Memory Operations

- `Status readByte(uint16_t addr, uint8_t& out)` - Read single byte
- `Status read(uint16_t addr, uint8_t* buf, size_t len)` - Read block (chunked)
- `Status writeByte(uint16_t addr, uint8_t value)` - Write single byte
- `Status write(uint16_t addr, const uint8_t* data, size_t len)` - Write block (chunked)
- `Status fill(uint16_t addr, uint8_t value, size_t len)` - Fill memory region

### Device Info

- `Status readDeviceId(DeviceId& out)` - Read manufacturer/product/density IDs
- `static constexpr uint16_t memorySize()` - Returns 32768

### Diagnostics

- `Status probe()` - Check device presence (no health tracking)
- `Status recover()` - Attempt recovery from DEGRADED/OFFLINE (re-verifies Device ID)

### State

- `DriverState state()` - Current driver state
- `bool isOnline()` - True if READY or DEGRADED

### Health

- `uint32_t lastOkMs()` - Timestamp of last success
- `uint32_t lastErrorMs()` - Timestamp of last failure
- `Status lastError()` - Most recent error
- `uint8_t consecutiveFailures()` - Failures since last success
- `uint32_t totalFailures()` - Lifetime failure count
- `uint32_t totalSuccess()` - Lifetime success count

## MB85RC256V Device Overview

| Property | Value |
|----------|-------|
| Technology | FRAM (Ferroelectric RAM) |
| Capacity | 32 KB (256 Kbit) |
| Address range | 0x0000 - 0x7FFF |
| I2C address | 0x50 - 0x57 (configurable via A0/A1/A2 pins) |
| I2C speed | Up to 1 MHz |
| Write endurance | 10^13 read/write cycles |
| Data retention | 10 years at 85°C |
| Write latency | None (no page buffer, no write delay) |

## Examples

- `01_basic_bringup_cli/` - Interactive CLI for testing

### Example Helpers (`examples/common/`)

Not part of the library. These simulate project-level glue and keep examples self-contained:

| File | Purpose |
|------|---------|
| `BoardConfig.h` | Pin definitions and Wire init for supported boards |
| `BuildConfig.h` | Compile-time `LOG_LEVEL` configuration |
| `Log.h` | Serial logging macros (`LOGE`/`LOGW`/`LOGI`/`LOGD`/`LOGT`/`LOGV`) |
| `I2cTransport.h` | Wire-based I2C transport adapter (`wireWrite`, `wireWriteRead`, `initWire`) |
| `I2cScanner.h` | I2C bus scanner with table output and bus recovery |
| `BusDiag.h` | Bus diagnostics wrapper (scan + probe) |
| `CliShell.h` | Serial command-line shell with line editing |
| `CommandHandler.h` | Command parsing helpers (`readLine`, `match`, `parseInt`) |
| `HealthView.h` | Compact health status display |
| `HealthDiag.h` | Verbose health diagnostics with color, snapshots, and `HealthMonitor` |
| `TransportAdapter.h` | Transport function pointer adapter |

## Documentation

- `CHANGELOG.md` - full release history
- `docs/IDF_PORT.md` - ESP-IDF portability guidance
- `docs/MB85RC256V_fram_implementation_manual.md` - device implementation reference

## License

MIT License. See [LICENSE](LICENSE).
