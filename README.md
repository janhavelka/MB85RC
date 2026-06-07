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

## Production Readiness Summary

This library is production-oriented in API shape and test coverage: the core is
framework-neutral, uses injected I2C callbacks, rejects invalid ranges before
bus traffic, tracks health, and documents FRAM-specific write semantics. Native
unit tests and CI builds cover the supported runtime variants and examples.

Hardware validation remains board- and variant-dependent. Do not treat CI,
native tests, or fake-bus WP simulation as proof that a specific FRAM device,
address-pin strap, pull-up network, WP wiring, brownout profile, or shared-bus
topology has been validated. Record the hardware matrix below for each target
board before relying on the library for production storage.

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

This repository includes pure ESP-IDF component metadata and CI build coverage.
Add the repo as an extra component or dependency, then include
`MB85RC/MB85RC.h` and provide `Config::i2cWrite` / `Config::i2cWriteRead`
callbacks from your project-owned I2C master bus. Local ESP-IDF build
validation requires `idf.py` on PATH.

The ESP-IDF bring-up CLI is implemented as a native IDF diagnostic-only example
with matching diagnostic coverage. Build it with:

```bash
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

The ESP-IDF example uses `app_main`, `driver/i2c_master.h`, `esp_timer`,
`vTaskDelay`, and fixed C command buffers. It does not include Arduino CLI
sources or compatibility facades. The command contract covers Device ID reads,
current-address reads, active-capacity-bounded memory commands, typed demo,
random benchmark, self-test, and stress diagnostics.

Mutating ESP-IDF CLI commands use explicit `!` confirmation forms such as
`write!`, `fill!`, `rw_suite!`, and `typed_demo!`.

The ESP-IDF CLI owns its I2C bus and uses blocking console input. That console input can block the example loop before `tick()` runs; this is acceptable for
the current diagnostic CLI because `tick()` is a no-op for supported FRAM parts.
Production systems must serialize shared-bus access in their injected transport
or application bus manager and should call `tick()` from their own scheduler
cadence if future devices need periodic work.

Validation status: command parity is checked by repo-local contract scripts.
Hardware smoke tests are still pending until target devices are available.

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

The example transport adapter maps Arduino `Wire` failures to `I2C_*` status codes and keeps bus timeout ownership outside the library. Applications that need meaningful health timestamps should inject `Config::nowMs`; otherwise timestamps remain `0`.

## I2C Ownership And Concurrency

The core driver never owns the I2C bus. It does not initialize pins, create
Arduino `Wire` or ESP-IDF handles, configure bus recovery, change clock speed,
or implement shared-bus locking. The application owns those policies through
`Config::i2cWrite`, `Config::i2cWriteRead`, and the user context pointer.

`MB85RC` instances are not internally thread-safe. Use one task, or serialize
all public calls that can touch driver state or I2C. Public I2C APIs are not
ISR-safe because transport callbacks can block until the configured timeout.
Transport callbacks must not recursively call back into the same `MB85RC`
instance.

The Arduino and ESP-IDF CLIs are diagnostic bring-up examples. They own their
example buses and are not production shared-bus manager templates.

## Release 2.0.0 Highlights

- Runtime support now covers every locally documented variant: `MB85RC04V`, `MB85RC16V`, `MB85RC64TA`, `MB85RC256V`, `MB85RC512T`, and `MB85RC1MT`.
- Runtime variant selection through `Config::expectedVariant` with `AUTO` and explicit selectors.
- `begin()`, `probe()`, and `recover()` validate the selected active Device ID where available instead of hard-coding the 256V product ID.
- `capacityBytes()`, `maxAddress()`, `variantName()`, `variantInfo()`, and `deviceId()` expose the active runtime device.
- Memory APIs and `maxAddress()` now use `uint32_t` addresses so the 128 KB `MB85RC1MT` range (`0x00000..0x1FFFF`) is not truncated.
- Address encoding is centralized for one-byte small-density devices, two-byte address-pin devices, and `MB85RC1MT` A16-in-device-address transactions.
- `MB85RC16V` is supported through explicit selection and memory-probe diagnostics because the part has no Device ID command.
- `read`, `write`, `fill`, `verify`, typed helpers, and CLI commands reject ranges that cross the active capacity.
- Arduino and native ESP-IDF CLIs use separate implementations with repo-local contract checks, accept 32-bit addresses, and cover the same diagnostic workflows. ESP-IDF mutating commands require explicit `!` confirmation forms.
- Core health timestamps now come only from injected `Config::nowMs`; framework time sources live in examples and application glue.
- ESP-IDF CLI remains a native `app_main` example using `driver/i2c_master.h`, `esp_timer`, `vTaskDelay`, and fixed C buffers.
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
- `Status writeByte(uint32_t addr, uint8_t value)` - write one byte; success means the I2C transaction was accepted
- `Status write(uint32_t addr, const uint8_t* data, size_t len)` - write a contiguous block with non-atomic chunking
- `WriteResult writeDetailed(uint32_t addr, const uint8_t* data, size_t len)` - write and report requested bytes, accepted prefix, and first failed chunk
- `Status fill(uint32_t addr, uint8_t value, size_t len)` - fill a region with a constant byte using non-atomic chunking
- `WriteResult fillDetailed(uint32_t addr, uint8_t value, size_t len)` - fill and report requested bytes, accepted prefix, and first failed chunk
- `Status verify(uint32_t addr, const uint8_t* expected, size_t len, VerifyResult& out)` - read back and compare FRAM contents against expected bytes
- `VerifyDetailedResult verifyDetailed(uint32_t addr, const uint8_t* expected, size_t len)` - verify with requested and verified byte counts
- `Status writeVerify(uint32_t addr, const uint8_t* data, size_t len, VerifyDetailedResult* out = nullptr)` - write then verify, returning `VERIFY_MISMATCH` on readback mismatch
- `Status fillVerify(uint32_t addr, uint8_t value, size_t len, VerifyDetailedResult* out = nullptr)` - fill then verify, returning `VERIFY_MISMATCH` on readback mismatch

### FRAM Write Semantics

FRAM writes are immediate for supported variants. The driver does not add
EEPROM-style write delays or ACK polling after writes.

`writeByte()`, `write()`, and `fill()` report transport acceptance. A successful
status means the addressed I2C write transaction, or every chunk in a bulk
operation, returned `Status::Ok()` from the injected transport. It does not prove
that bytes persisted when the external `WP` pin is asserted. The device can ACK a
write while hardware write protection prevents memory from changing, and the core
driver has no software-visible WP state.

Bulk `write()` and `fill()` calls may be split into multiple I2C chunks. They are
not atomic: if a later chunk fails, earlier accepted chunks are not rolled back.
The simple APIs return the first failing `Status`; use `writeDetailed()` or
`fillDetailed()` when recovery code needs the accepted-prefix length. Their
`bytesAccepted` field is not committed persistence. Only bytes read back
successfully by `verify()` or `verifyDetailed()` should be treated as verified.
For critical writes or fills, use `writeVerify()` / `fillVerify()` or call
`verify()` after `write()` / `fill()`.

### Current Address Semantics

Current-address read is an I2C/FRAM device feature that returns data from the
device's internal pointer. That pointer is undefined after power-up and can be
disturbed by diagnostics or failed transactions. Use explicit-address
`read(address, ...)` for deterministic production workflows, especially after
power loss, bus errors, `probe()`, or `recover()`. `readCurrentAddress()` is best
reserved for diagnostics or carefully controlled transaction sequences after a
known successful addressed read/write by the same instance.

### Diagnostics

- `Status probe()` - diagnostic presence check after `begin()` using the active variant; it does not initialize, reset, or recover the physical bus
- `Status recover()` - manual driver-state recovery attempt; application-owned bus recovery remains outside the core
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

| Variant | Capacity | I2C address model | Memory address bytes/model | Device ID | Max bus speed claimed | Notes |
| --- | ---: | --- | --- | --- | --- | --- |
| `MB85RC04V` | 512 B | A2/A1 pins select a base pair; A8 selects the transaction address (`0x50`-`0x57` across all straps) | 1 byte plus A8 in I2C address | Yes, product `0x010`; `AUTO` supported | 1 MHz | No high-speed or sleep command support documented in local summary. |
| `MB85RC16V` | 2 KB | `0x50`-`0x57` encodes memory A10:A8; no external address-select pins | 1 byte plus A10:A8 in I2C address | No; select `DeviceVariant::MB85RC16V` explicitly | 1 MHz | Memory-probe diagnostics only; `AUTO` cannot discover it. |
| `MB85RC64TA` | 8 KB | `0x50`-`0x57`; A2/A1/A0 pins select device | 2 bytes, active range `0x0000`-`0x1FFF` | Yes, product `0x358`; `AUTO` supported | 1 MHz normal, 3.4 MHz high-speed after entry command | High-speed and sleep are documented by datasheet but not implemented by the core. |
| `MB85RC256V` | 32 KB | `0x50`-`0x57`; A2/A1/A0 pins select device | 2 bytes, active range `0x0000`-`0x7FFF` | Yes, product `0x510`; default selector | 1 MHz | Legacy default; no high-speed or sleep command support documented in local summary. |
| `MB85RC512T` | 64 KB | `0x50`-`0x57`; A2/A1/A0 pins select device | 2 bytes, active range `0x0000`-`0xFFFF` | Yes, product `0x658`; `AUTO` supported | 1 MHz normal, 3.4 MHz high-speed after entry command | High-speed and sleep are documented by datasheet but not implemented by the core. |
| `MB85RC1MT` | 128 KB | A2/A1 pins select a base pair; A16 selects the transaction address (`0x50`-`0x57` across all straps) | 2 bytes plus A16 in I2C address | Yes, product `0x758`; `AUTO` supported | 1 MHz normal, 3.4 MHz high-speed after entry command | High-speed and sleep are documented by datasheet but not implemented by the core. |

`AUTO` uses the Device ID command and therefore works only on variants that implement Device ID. `MB85RC16V` must be selected explicitly. The driver derives runtime transaction addresses from `Config::i2cAddress` plus the active variant's address model.

Local datasheet summaries in `docs/extracted-md/` list endurance and retention
claims by variant. Use the exact part datasheet for production lifetime budgets:
the local summaries show 10^12 writes/byte for `MB85RC04V`, `MB85RC16V`, and
`MB85RC256V`, and 10^13 writes/byte for `MB85RC64TA`, `MB85RC512T`, and
`MB85RC1MT`. Retention statements vary by part and temperature.

## Validation Status

| Coverage area | Current evidence | Status |
| --- | --- | --- |
| Implemented behavior | Public headers, README contracts, framework-neutral `src/`, runtime variant table, range checks, current-address tracking, detailed write/fill/verify APIs | Implemented in code |
| Unit-test coverage | Native fake-bus tests cover variant selection, address encoding, range boundaries, partial chunk failures, WP-high simulation, current-address invalidation, and health transitions | Covered by native tests |
| CI/build coverage | PlatformIO Arduino builds for ESP32-S2/S3, native tests, guard scripts, package validation, and pure ESP-IDF CI workflow for `examples/espidf_basic` | Covered by CI configuration; local IDF build depends on `idf.py` availability |
| Hardware validation | Board/variant/address-pin/WP/brownout/shared-bus/soak evidence | Pending hardware; use the matrix below |

## Hardware Validation Matrix

Status values below are planning states, not claims. Mark rows complete only
after recording board, MCU, FRAM package/date code, supply voltage, pull-ups,
bus speed, address-pin straps, WP wiring, command log, and captured evidence.

| Scenario | Variant(s) | Address pins | Command/test | Expected evidence | Status |
| --- | --- | --- | --- | --- | --- |
| Device ID read and `begin(AUTO)` | `MB85RC04V`, `MB85RC64TA`, `MB85RC256V`, `MB85RC512T`, `MB85RC1MT` | Each board's selected strap | `id`, `idraw`, `begin(AUTO)` | Manufacturer `0x00A`, expected product ID, active capacity selected | Pending hardware |
| No-ID explicit variant | `MB85RC16V` | A10:A8 encoded in transaction address | `begin(MB85RC16V)`, memory probe, `readDeviceId()` negative check | Explicit begin succeeds on present device; Device ID APIs reject as unsupported | Pending hardware |
| Address pin combinations | `MB85RC04V`, `MB85RC64TA`, `MB85RC256V`, `MB85RC512T`, `MB85RC1MT` | A1/A2 or A0/A1/A2 low/high combinations as applicable | I2C scan plus `begin()` at each strapped address | Only strapped address responds; wrong addresses NACK | Pending hardware |
| Upper-address bits in I2C address | `MB85RC16V` | No external address-select pins | Write/read across `0x00FF`, `0x0100`, and `0x07FF` | A10:A8 transaction address selection works and exact-end byte verifies | Pending hardware |
| Exact-end read/write | All supported variants | Default and at least one nonzero strap | `writeVerify(maxAddress - n + 1, n)` and `read()` | Last valid byte range succeeds and verifies | Pending hardware |
| Boundary rejection | All supported variants | Any valid strap | Cross-end `read`, `write`, `fill`, `verify` | Driver returns `ADDRESS_OUT_OF_RANGE` before bus traffic when observable | Pending hardware |
| Sequential public no-wrap contract | All supported variants | Any valid strap | Attempt bulk range crossing capacity | Public API rejects; no reliance on datasheet rollover | Pending hardware |
| WP high behavior | At least `MB85RC256V`, plus each production BOM variant | WP low/open, then WP high | `write()`, `verify()`, `writeVerify()` | WP low/open persists; WP high may ACK but memory remains unchanged; verify catches mismatch | Pending hardware |
| Bulk write/fill/verify | All supported variants | Any valid strap | `writeDetailed()`, `fillDetailed()`, `verifyDetailed()` over multi-chunk ranges | Full accepted counts and readback match | Pending hardware |
| Current-address read after explicit set | All supported variants | Any valid strap | Addressed `read()` or `write()`, then `readCurrentAddress()` | Pointer advances only after known address-setting transaction | Pending hardware |
| Unplug/NACK and recovery | Representative Device-ID and no-ID variants | Any valid strap | Disconnect device or force wrong address, then `probe()`/`recover()` | Transport errors mapped, health state degrades/offlines, manual recovery documented | Pending hardware |
| Brownout/power-cycle persistence | Each production BOM variant | Production straps and WP wiring | Write record, verify, power-cycle/brownout, read/verify | Data persists or application journal rejects torn record | Pending hardware |
| Pure ESP-IDF CLI | ESP32-S2 and ESP32-S3 with production BOM variant | Production straps | `idf.py` build, flash, `id`, `rw_suite!`, `typed_demo!` | Native IDF CLI runs without Arduino compatibility and commands pass | Pending hardware |
| Shared bus with another device | Production board topology | Production straps | Concurrent application bus manager test with another I2C device | External serialization prevents interleaved transactions and recovers from peer failures | Pending hardware |
| Long soak on sacrificial range | Each production BOM variant | Production straps | Repeated write/read/verify with CRC/generation counter | No mismatches over planned duration; failures logged with supply and temperature | Pending hardware |

## Notes

- `read()`, `write()`, `fill()`, and `verify()` reject ranges where `address + len > capacityBytes()`; they do not silently wrap bulk operations.
- `write()` and `fill()` are not atomic across internal chunks. A failed later chunk can leave an accepted prefix in the target range; the driver does not roll back earlier chunks.
- Successful write/fill status means I2C acceptance, not verified persistence. Use `verify()` for readback confidence, especially when the hardware `WP` pin may be asserted.
- `readCurrentAddress()` is only meaningful after a successful addressed memory read/write because the current address is undefined after power-on.
- The bulk `readCurrentAddress(uint8_t*, size_t)` helper repeats the documented current-address read primitive while preserving tracked pointer behavior and rejecting cross-capacity reads.
- Argument validation errors reject null buffers, zero lengths, and out-of-range start addresses before touching the bus or health counters.
- `recover()` invalidates current-address tracking and records both I2C failures and Device ID mismatches in driver health.
- `verify()` reports the first mismatch without inventing a synthetic device error code; transport failures still return normal `Status` errors.
- The `WP` pin is hardware-only and non-permanent. High disables writes to the entire array, low or open enables writes, and reads still work.
- There is no software block-protect register, OTP lock region, or permanent write lock in this device family.
- The datasheet software-reset bus sequence is transport-owned by design because the library never drives SDA/SCL directly.
- Typed storage policy is intentionally kept out of the core driver. If you need fixed-width numeric encoding, use an explicit codec layer such as `examples/common/TypedMemory.h`.

## Production Storage Pattern

For configuration records or other critical data, keep the transaction policy in
the application layer:

1. Use fixed-size slots or a small journal. Start each record with
   magic/version/length/sequence/CRC fields and a validity marker that is erased
   or invalid while the record is being written.
2. Write the header and payload, with CRC covering the payload and any fields
   needed to reject torn records.
3. Read back with `verify()` and reject the update if any byte mismatches.
4. Mark the record valid last, then verify that marker.
5. On boot, scan records, validate magic/version/length/CRC and the valid marker,
   then choose the newest sequence number.
6. Size the slot count and rewrite cadence from the actual part's endurance and
   retention limits, voltage range, and temperature profile.

## Examples

- `examples/01_basic_bringup_cli/`
  - Arduino diagnostic/bring-up CLI; not a production storage stack or shared-bus manager.
  - `cfg` / `settings` for runtime/config snapshots
  - `read` / `dump` / `hexdump` for active-capacity-bounded hex+ASCII memory dumps
  - `text`, `strings`, `crc`, and `verify` for live memory inspection on hardware
  - `current` / `cur` for current-address reads
  - `id` / `idraw` for parsed and raw Device ID visibility
  - `drv`, `probe`, `recover`, `selftest`, `stress`, `stress_mix` for diagnostics
  - `rw_suite` for read/write/fill/verify diagnostics with best-effort restore
  - `randbench [N]` for random-access timing over a scratch window with compact restore status
  - `typed_demo` for fixed-width integer/float/double storage with compact pass/fail status

- `examples/espidf_basic/`
  - Native ESP-IDF diagnostic-only build of the bring-up CLI command contract.
  - Uses `app_main`, `driver/i2c_master.h`, `esp_timer`, `vTaskDelay`, and fixed C buffers.
  - Owns its example I2C bus and blocks on console input; production systems must serialize shared-bus access externally.
  - Preserves current-address, Device ID, raw ID, active-capacity, stress, self-test, benchmark, and typed-demo command coverage.
  - Requires explicit `!` confirmation forms before changing FRAM contents:
    `write!`, `fill!`, `selftest!`, `rw_suite!`, `stress!`, `stress_mix!`,
    `randbench!`, and `typed_demo!`.
  - `tools/check_idf_example_contract.py` rejects Arduino compatibility facades and checks the native IDF command surface.

### ESP-IDF CLI Inspection Examples

```text
hexdump 0x0000 128        # Hex + ASCII view of a region
text 0x0000 64            # Escaped text-oriented view
strings                   # Scan the whole chip for printable ASCII strings
strings 0x1000 512 6      # Scan a window with a minimum string length
crc 0x0000 1024           # CRC32 over a region for quick verification
verify 0x0020 55 55 55 55 # Compare live FRAM bytes against expected values
idraw                     # Show the raw 3-byte Device ID payload
current 16                # Read 16 bytes from the current internal address
rw_suite!                 # Confirmed deterministic read/write/fill/verify suite
randbench! 4096           # Confirmed random writes + random reads timing
typed_demo!               # Confirmed explicit typed value storage demo
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

1. Concurrency: `MB85RC` instances are not internally thread-safe. Use one task, or provide external serialization around all public methods that can touch driver state or I2C.
2. ISR safety: public APIs are not ISR-safe because they can call I2C transport callbacks and may block until the transport timeout.
3. Transport non-recursion: injected transport callbacks must not recursively call back into the same `MB85RC` instance.
4. Timing model: `tick()` is bounded and currently a no-op; public I2C operations are blocking.
5. Shared-bus ownership: bus, pins, locking, timeout policy, retry policy, and recovery policy remain application-owned via `Config` and the injected transport. The core never initializes or owns `Wire`, ESP-IDF I2C handles, pins, or a global bus.
6. Memory behavior: no heap allocation in steady-state library operation; bulk memory APIs reject cross-end ranges instead of relying on device rollover.
7. Current-address reads: use `readCurrentAddress()` only after a known address-setting transaction, such as a successful addressed `read()`, `readByte()`, `write()`, `writeByte()`, or `fill()` by the same instance. Current-address state is undefined after power-up and is conservatively invalidated after failed I2C memory/current-address transactions and `recover()`. Raw diagnostics such as `probe()` are not address-setting contracts and may disturb the device pointer; use an addressed read after them if current-address state matters.
8. Error handling: all fallible APIs return `Status`; no exceptions and no silent failures.
9. Health behavior: `OFFLINE` is latched. Normal public I2C operations return `BUSY` with `Driver is offline; call recover()` without touching the bus until `recover()` succeeds.

## Validation

```bash
python -m platformio test -e native
python tools/check_cli_contract.py
python tools/check_core_timing_guard.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev

# Build the ESP-IDF full CLI example (requires ESP-IDF on PATH)
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
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
- `Doxyfile` - indexes public headers, the ESP-IDF port notes, the Arduino CLI,
  and the native IDF entry point

## License

MIT License. See [LICENSE](LICENSE).
