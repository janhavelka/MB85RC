# MB85RC Driver Library

Production-oriented MB85RC-family FRAM I2C driver for ESP32-S2 / ESP32-S3 using Arduino/PlatformIO and ESP-IDF.

Library version: `4.1.0`

Latest published tag: `v4.1.0`

Release `v4.1.0` uses pioarduino `55.03.311`. The previous `54.03.20` stack
remains available as a named build-only compatibility environment.

## Features

- Injected, terminal I2C transport with no `Wire` dependency in library code
- Zero-I/O `bind()` plus compatibility `begin()`, bus-silent `tick()`, and `end()`
- Diagnostic `READY`, `DEGRADED`, and `OFFLINE` health that never gates owner-directed work
- Runtime variant selection for `MB85RC04V`, `MB85RC16V`, `MB85RC64TA`, `MB85RC256V`, `MB85RC512T`, and `MB85RC1MT`
- Exact one-transaction read, write, and verify primitives plus bounded chunked helpers
- Current-address read support for the documented internal address-pointer flow, including multi-byte helper coverage
- Explicit Device ID through a special transport operation, without broadening normal 7-bit scan policy
- Raw Device ID access where available and verify/compare helpers for diagnostics
- Runtime settings snapshot API for bus-silent examples and diagnostics
- Request identity, retained exactly-once results, cancellation, timeout, partial progress, and indeterminate-write reconciliation for cooperative jobs

## Production Readiness Summary

This library is production-oriented in API shape and test coverage: the core is
framework-neutral, uses injected I2C callbacks, rejects invalid ranges before
bus traffic, tracks health, and documents FRAM-specific write semantics. Native
unit tests and CI builds cover the supported runtime variants and examples.

Hardware validation remains board- and variant-dependent. Recorded runs and
their limitations live in the [HIL evidence ledger](docs/reports/HIL_SUMMARY.md);
the [release checklist](docs/RELEASE_CHECKLIST.md) is the canonical outstanding
qualification matrix. Do not treat CI, native tests, fake-bus WP simulation, or
evidence from one fixture as proof for a different FRAM variant, board, address
strap, pull-up network, WP wiring, power profile, or shared-bus topology.

## Installation

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps =
  https://github.com/janhavelka/MB85RC.git#<reviewed-immutable-commit>
```

Use `#v4.1.0` for the reviewed release. Production integrations should pin the
release tag or its full commit, not a branch name.

### Manual

Copy `include/MB85RC/` and `src/` into your project.

### ESP-IDF Component

This repository includes pure ESP-IDF component metadata and CI build coverage.
Add the repo as an extra component or dependency, then include
`MB85RC/MB85RC.h` and provide `Config::i2cWrite` / `Config::i2cWriteRead`
callbacks from your project-owned I2C master bus. Provide `Config::i2cSpecial`
when using `DeviceVariant::AUTO`, Device ID, High-speed, or Sleep operations.
The component metadata requires ESP-IDF 6.0.1 or newer. CI builds the declared
6.0.1 floor and 6.0.2 for both supported targets. Local validation requires
`idf.py` on PATH.

The ESP-IDF bring-up CLI is implemented as a native IDF diagnostic-only example
with matching diagnostic coverage. Build it with:

```bash
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

The native boundary, command/confirmation policy, transport mapping, and runtime
version telemetry are maintained in the [ESP-IDF port notes](docs/IDF_PORT.md).
Revision-specific hardware evidence is recorded in the
[HIL summary](docs/reports/HIL_SUMMARY.md).

## Quick Start

This snippet uses the repository's example-only Arduino transport adapter from
`examples/common/I2cTransport.h`. It is not installed as part of the public
library. Production applications should provide an equivalent adapter around
their application-owned bus, locking, timeout, and recovery policy.

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
  cfg.i2cSpecial = transport::wireSpecial;
  cfg.i2cUser = &Wire;
  cfg.i2cAddress = 0x50;
  cfg.expectedVariant = MB85RC::DeviceVariant::MB85RC256V;

  // Passive configuration: validates everything and performs zero I2C.
  MB85RC::Status st = device.bind(cfg);
  if (!st.ok()) {
    Serial.printf("Bind failed: %s\n", st.msg);
    return;
  }

  // Presence/identity is an explicit owner-scheduled operation.
  MB85RC::DeviceId identity;
  if (!device.readDeviceId(identity).ok()) {
    return;
  }

  const uint8_t written = 0x42;
  MB85RC::WriteCommit commit = MB85RC::WriteCommit::NOT_APPLICABLE;
  if (!device.writeOnce(0x0000, &written, 1, &commit).ok()) {
    return;
  }

  uint8_t value = 0;
  if (device.readByte(0x0000, value).ok()) {
    Serial.printf("Read: 0x%02X\n", value);
  }
}

void loop() {
  device.tick(millis());
}
```

The default `expectedVariant` is `DeviceVariant::AUTO`. `bind()` is still
bus-silent in that mode and requires `Config::i2cSpecial`; memory access remains
unavailable until an explicit Device ID read selects a supported variant.
Production fixed-BOM integrations should select the exact variant and validate
its identity before publishing the device ready. `MB85RC16V` has no Device ID
command and must be selected explicitly.

`Config::i2cAddress` is the board strap base address, not a memory-bank encoded
transaction address. Two-byte A2/A1/A0 variants accept `0x50`-`0x57`; variants
that encode upper memory bits in the transaction address accept only
unambiguous bases (`MB85RC04V` and `MB85RC1MT`: `0x50`, `0x52`, `0x54`,
`0x56`; `MB85RC16V`: `0x50`).

`Config::i2cTimeoutMs` defaults to `MB85RC::DEFAULT_I2C_TIMEOUT_MS` (`50`) and
must be in `MB85RC::MIN_I2C_TIMEOUT_MS..MB85RC::MAX_I2C_TIMEOUT_MS`
(`1..1000`). The injected transport owns the actual controller timeout; this
value is the per-transaction deadline passed to callbacks.

`Config::maxTxBytes` and `Config::maxRxBytes` describe the complete transaction
capacity of the injected transport, including memory-address bytes. `bind()`
rejects a capacity too small for one valid transaction without I2C. Capabilities
larger than the core's fixed 128-byte buffers are valid; active operations clamp
to the smaller core limit. For a two-byte-address variant and
`maxTxBytes = 126`, `maxWriteDataBytes()` is 124, matching a 124-byte owner
payload plus its two address bytes.

`Config::i2cUser`, `Config::timeUser`, and the state they reference must remain
valid until `end()` or a later successful `bind()`/`begin()` replaces the
configuration. A rejected replacement leaves the previous binding active.

The example transport adapter maps Arduino `Wire` outcomes to terminal
`TransportResult` values and keeps bus timeout ownership outside the library.
Applications that need meaningful health timestamps or Sleep wake gating should
inject `Config::nowMs`; otherwise timestamps remain `0` and wake gating
advances only when the caller supplies time to `tick()`.

## I2C Ownership And Concurrency

The core driver never owns the I2C bus. It does not initialize pins, create
Arduino `Wire` or ESP-IDF handles, configure bus recovery, change clock speed,
or implement shared-bus locking. The application owns those policies through
`Config::i2cWrite`, `Config::i2cWriteRead`, `Config::i2cSpecial`, and the user
context pointer.

Each transport callback is synchronous and represents exactly one completed
physical transaction. `TransportCode::OK` means the complete requested TX/RX
lengths were transferred; short completion is rejected by the core. A
write-read callback must use a repeated START with no intervening STOP.
Callbacks return no queued/in-progress state, perform no hidden retry or bus
recovery, and never recursively call the same driver. Failed-read buffers are
unspecified. Failed writes report `WriteCommit::NOT_COMMITTED` only when the
transport can prove no requested data was accepted; otherwise they report
`INDETERMINATE`. Completion counts cover the callback buffers, so memory-write
TX counts include the one- or two-byte memory-address prefix. Special-operation
counts cover only `I2cSpecialTransfer::txData`/`rxData`, not hidden Device-ID,
High-speed, Sleep, or wake framing. A failed full `ACCEPTED` claim is valid only
for a later timeout/bus/I/O error; a NACK contradicts full acceptance and is
normalized conservatively.

`MB85RC` instances are not internally thread-safe. Use one task, or serialize
all public calls that can touch driver state or I2C. Public I2C APIs are not
ISR-safe because transport callbacks can block until the configured timeout.
Transport callbacks must not recursively call back into the same `MB85RC`
instance.

The Arduino and ESP-IDF CLIs are diagnostic bring-up examples. They own their
example buses and are not production shared-bus manager templates.

## Bounded Operation Classes

Let `T` be `Config::i2cTimeoutMs`, `W` be `maxWriteDataBytes()`, `R` be
`maxReadDataBytes()`, and `B` be the caller's poll budget clamped to
`cmd::MAX_TRANSFER_INSTRUCTIONS_PER_POLL` (`8`). These bounds exclude caller-
owned queueing time and bus recovery, which the library never performs.

### Steady-State Owner Operations

`readOnce()`, `writeOnce()`, and `verifyOnce()` validate the complete request
before I2C and invoke zero or one transport callback. Valid work therefore has
a worst-case callback occupancy of `T`. Length must be `1..R` for reads/verifies
or `1..W` for writes. There is no hidden wait, retry, recovery, or allocation.
`writeOnce()` returns the transport's commit knowledge; an accepted write is
still not persistence proof when WP is high.

This is the preferred surface for normal reads/writes performed by an external
bus-owner task when one physical transaction per scheduler poll is required.

### Multi-Step Runtime Operations

`requestRead()`, `requestWrite()`, `requestFill()`, `requestVerify()`, and
`requestVerifiedWrite()` perform zero I2C. `pollTransfer(nowMs, B)` performs at
most `B` complete callbacks, so one call occupies at most `B * T` in transport.
A length-`N` read/verify takes at most `ceil(N/R)` callbacks; a write takes at
most `ceil(N/W)`; a fill uses at most `ceil(N/min(W, 64))`. A verified write
must fit one write and one read transaction and takes at most two callbacks,
normally in separate polls when `B = 1`.

The external owner supplies a nonzero request ID and owns the absolute
deadline. On expiry it calls `timeoutTransfer(requestId)`; cancellation uses
`cancelTransfer(requestId)`. Both terminalize between callbacks and issue no
I2C. A synchronous callback already in flight cannot be interrupted by the
core, so its own `T` bound remains mandatory. Accepted prefixes are never
rolled back.

An indeterminate verified-write failure enters
`WAITING_FOR_RECONCILIATION`. Polling then performs zero callbacks until the
owner has recovered the bus and calls `resumeVerifiedWrite(requestId)`. Resume
authorizes readback only; the write is never replayed. Progress/results retain
request ID, kind, terminal state, byte counts, failed chunk, original write and
verify statuses, commit state, and mismatch evidence without retaining buffer
pointers. One terminal result blocks replacement work until
`takeTransferResult()` consumes it exactly once.

### Rare Or Maintenance Operations

Whole-range synchronous helpers are intentionally allowed to use the same
finite chunk formulas in one blocking call. Across the largest supported
128 KiB part, their upper bound is therefore finite and derived from capacity,
`W`, `R`, and `T`. For length `N`, read/verify use at most `ceil(N/R)`
callbacks, write uses `ceil(N/W)`, fill uses `ceil(N/min(W,64))`,
`writeVerify()` uses `ceil(N/W) + ceil(N/R)`, and `fillVerify()` uses
`ceil(N/min(W,64)) + ceil(N/R)`. The diagnostic
`readCurrentAddress(buffer,N)` uses exactly `N` one-byte callbacks. Each bound
therefore has worst-case transport occupancy equal to its callback count times
`T`. Use these helpers only in startup, diagnostics, commissioning, or a
maintenance window whose caller budget can tolerate that occupancy.

Device ID is one explicit special callback. High-speed entry selection, Sleep
entry, and wake stimulus are also individually bounded special operations;
Sleep recovery advances from caller-supplied time and inserts no hidden delay.
FRAM has no EEPROM-style program cycle, ACK polling, erase procedure, or
automatic retry. Large destructive writes remain non-atomic, endurance remains
the application's data-layout concern, and ambiguous effects must be verified
before any repair write.

Before an intentional maintenance rewrite, verify the desired bytes first and
skip the write when they already match. This keeps rewrite policy explicit and
avoids consuming endurance unnecessarily without adding hidden driver reads.

## High-Speed And Sleep Modes

Local datasheets document High-speed mode and Sleep mode only for
MB85RC64TA, MB85RC512T, and MB85RC1MT. The driver exposes variant-gated
capability metadata and APIs for those parts. `MB85RC04V`, `MB85RC16V`, and
`MB85RC256V` return `UNSUPPORTED` for these mode requests and perform no bus
traffic.

MB85RC core does not change the MCU I2C clock, pins, controller mode, or bus
locking. `enterHighSpeedMode()` enables HS-prefixed memory/current-address
transfers through the optional `Config::i2cSpecial` callback; each transfer
sends the `0000 1XXX` master-code prefix because a STOP exits HS state. The
application bus manager must configure and validate 3.4 MHz operation if that
bus speed is used. The Arduino diagnostic CLI reports capability and missing
raw-special callback support honestly. The native ESP-IDF diagnostic CLI can
emit the HS prefix through `Config::i2cSpecial`. Neither example proves real
3.4 MHz hardware operation without board-level validation.

Sleep entry is emitted through `Config::i2cSpecial` as `F8h` plus the active
device address word, repeated START, then `86h`. On success the driver marks the
device asleep and invalidates current-address tracking. `wake()` sends the wake
stimulus, then the application must wait `tREC >= 400 us` before access or
`recover()`; the core records a conservative millisecond wake gate and inserts
no hidden delay. A timeout, bus/I/O error, or malformed completion during Sleep
entry can leave the hardware effect ambiguous; the driver then reports
`SleepState::UNKNOWN`, blocks normal I2C, and requires an explicit `wake()`.
A failed wake remains `UNKNOWN`; a successful wake enters `WAKING` until tREC.

## API Documentation

The public headers under `include/MB85RC/` are the authoritative API contract.
Run `doxygen Doxyfile` to build the complete reference; strict generation fails
on undocumented public members and enum values, missing parameter/return
contracts, invalid commands, and unresolved documentation links. Release and
migration history lives in [CHANGELOG.md](CHANGELOG.md).

For orientation, the API is grouped into passive lifecycle and variant
selection, one-transaction primitives, synchronous chunked convenience calls,
request-qualified cooperative jobs, High-speed/Sleep control, and bus-silent
diagnostics. The operation bounds and scheduling guidance above explain when to
use each group. Prefer the generated reference over copying method inventories
into integration documentation.

### FRAM Write Semantics

FRAM writes are immediate for supported variants. The driver does not add
EEPROM-style write delays or ACK polling after writes.

`writeByte()`, `write()`, and `fill()` report transport acceptance. A successful
status means the addressed I2C write transaction, or every chunk in a bulk
operation, returned a complete terminal `TransportResult::OK`. It does not prove
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

If a write or fill chunk returns `I2C_TIMEOUT` or another indeterminate
transport failure, the failed chunk's physical effect remains observable as
`WriteCommit::INDETERMINATE`. Synchronous `writeVerify()` and `fillVerify()`
return the write/fill error without issuing readback. The cooperative
`requestVerifiedWrite()` instead pauses without bus traffic, lets the external
owner recover the bus, and resumes with readback only. Never resend an
indeterminate write before reconciliation.

### Current Address Semantics

Current-address read is an I2C/FRAM device feature that returns data from the
device's internal pointer. That pointer is undefined after power-up and can be
disturbed by diagnostics or failed transactions. Use explicit-address
`read(address, ...)` for deterministic production workflows, especially after
power loss, bus errors, `probe()`, or `recover()`. `readCurrentAddress()` is best
reserved for diagnostics or carefully controlled transaction sequences after a
known successful addressed read/write by the same instance.

### Diagnostics

- `Status probe()` - diagnostic presence check after binding using the active variant; it does not initialize, reset, or recover the physical bus
- `Status recover()` - compatibility presence/identity check that updates diagnostics; it does not recover the bus or control future admission
- `Status readDeviceId(DeviceId& out)` - read manufacturer, product, and density fields where supported
- `Status readDeviceIdRaw(DeviceIdRaw& out)` - read the raw 3-byte Device ID payload where supported; this public API is health-tracked like `readDeviceId()`
- `static DeviceId decodeDeviceId(const DeviceIdRaw& raw)` - bus-silent pure decode, including the exact matched variant when known
- `Status getSettings(SettingsSnapshot& out)` - snapshot active config/runtime/health state without I2C
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

`isOnline()` is a compatibility name for “passively bound”; it remains true in
diagnostic `DEGRADED` and `OFFLINE` states. Use `state()` for health display,
not for transport admission policy.

`SettingsSnapshot` is a cache-only view. It includes bound/state/online status,
I2C timeout/capacities, health counters and library-owned last-error text,
active variant and
Device ID fields, Sleep mode state, and conservative current-address tracking.
Calling either `getSettings()` overload must not probe the device or change
health counters. Lifetime `totalSuccess()` and `totalFailures()` counters are
`uint32_t` values and wrap naturally at `UINT32_MAX`; the
`consecutiveFailures()` streak is `uint8_t` and saturates. `OFFLINE` is an
optional diagnostic classification only; it never suppresses an owner-requested
transaction or claims bus-recovery authority.

## Supported Runtime Variants

| Variant | Capacity | Identity selection | High-speed / Sleep |
| --- | ---: | --- | --- |
| `MB85RC04V` | 512 B | Device ID, `AUTO` supported | No |
| `MB85RC16V` | 2 KiB | Explicit selection only | No |
| `MB85RC64TA` | 8 KiB | Device ID, `AUTO` supported | Yes |
| `MB85RC256V` | 32 KiB | Device ID, `AUTO` supported | No |
| `MB85RC512T` | 64 KiB | Device ID, `AUTO` supported | Yes |
| `MB85RC1MT` | 128 KiB | Device ID, `AUTO` supported | Yes |

`AUTO` uses the Device ID command and therefore works only on variants that
implement Device ID. `MB85RC16V` must be selected explicitly. The driver
derives runtime transaction addresses from `Config::i2cAddress` plus the active
variant's address model, and rejects ambiguous base addresses before normal
operation.

The maintained [device reference](docs/DEVICE_REFERENCE.md) is the canonical
source for address encoding, product IDs, electrical limits, bus modes,
endurance, and retention notes. Use the exact BOM datasheet for final design
decisions.

## Hardware Validation

The latest completed run is the 24-hour MB85RC256V strict soak ending
2026-08-01: 34/34 functional checks and 221,222 soak checks passed, with no
failures, unknowns, target resets, serial reconnects, or framing recoveries. The
driver finished `READY` after 3,837,088 successful operations and zero failures;
the final heap drop was 160 bytes.

That firmware identified itself as `d31d2b4-dirty`, so the result is strong
fixture regression and endurance evidence, not immutable clean-release
qualification. WP-high behavior, unplug/NACK recovery, controlled power loss,
native ESP-IDF hardware execution, and full variant/strap coverage remain open.
See the [HIL evidence ledger](docs/reports/HIL_SUMMARY.md) for exact provenance
and the [release checklist](docs/RELEASE_CHECKLIST.md) for the remaining matrix.

## Production Storage Pattern

For configuration records or other critical data, keep the transaction policy in
the application layer:

1. Use fixed-size slots or a small journal. Start each record with
   magic/version/length/sequence/CRC fields and keep its validity marker invalid
   while the record is being written.
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
  - `hs`, `hs support`, `hs enter`, and `hs exit` for High-speed capability diagnostics
  - `sleep`, `sleep support`, `sleep enter`, and `sleep wake` for Sleep mode diagnostics
  - `drv`, `heap`, `probe`, `recover`, and restore-verified `selftest`, `stress`, `stress_mix` diagnostics
  - `rw_suite` for read/write/fill/verify diagnostics with readback-verified restore
  - `xfer_demo` for poll-chunked transfer API diagnostics with a staged verify of the restored bytes, including zero-budget, two-instruction, and high-budget-clamp polling checks
  - `randbench [N]` for random-access timing over a scratch window with compact restore status
  - `typed_demo` for fixed-width integer/float/double storage with compact pass/fail status

The Arduino stress commands temporarily mutate only a bounded scratch byte or
16-byte scratch window, back it up first, and write-verify restoration on every
completed run. A failed restore is reported explicitly; the commands never
claim that temporary writes are atomic or safe against power loss.

The bundled board configuration uses a 5 ms controller/callback timeout and
declares 126 TX bytes plus 124 RX bytes. On two-byte-address variants this
exercises 124-byte read/write data chunks, matching a conservative external
I2C-owner integration envelope while keeping timeout ownership in the example
transport.

- `examples/espidf_basic/`
  - Native ESP-IDF diagnostic-only build of the bring-up CLI command contract.
  - Uses `app_main`, `driver/i2c_master.h`, `esp_timer`, `vTaskDelay`, and fixed C buffers.
  - Owns its example I2C bus and blocks on console input; production systems must serialize shared-bus access externally.
  - Preserves current-address, Device ID, raw ID, active-capacity, heap, HS/Sleep diagnostics, stress, self-test, benchmark, and typed-demo command coverage.
  - Requires explicit `!` confirmation forms before changing FRAM contents:
    `write!`, `fill!`, `selftest!`, `rw_suite!`, `stress!`, `stress_mix!`,
    `xfer_demo!`, `randbench!`, and `typed_demo!`.
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
heap                      # Example firmware heap telemetry
current 16                # Read 16 bytes from the current internal address
rw_suite!                 # Confirmed deterministic read/write/fill/verify suite
xfer_demo!                # Confirmed poll-chunked transfer API demo
randbench! 4096           # Confirmed random writes + random reads timing
typed_demo!               # Confirmed explicit typed value storage demo
hs support                # Show active variant High-speed capability
hs enter / hs exit        # Enable/disable HS-prefixed transfers when the adapter supports raw HS
sleep support             # Show active variant Sleep capability and tREC
sleep enter               # Send Sleep entry sequence if supported
sleep wake                # Wake, wait recovery interval, then recover
```

The bundled Arduino `Wire` adapter implements the Device ID special operation
only. Its CLI rejects `hs enter`, `sleep enter`, and `sleep wake` as
`UNSUPPORTED` before bus traffic. Full HS/Sleep diagnostics
require an application-owned raw special-operation adapter, such as the native
ESP-IDF example transport.

### Example Helpers

`examples/common/` is example-only glue and is not part of the public library API.

| File | Purpose |
|------|---------|
| `BoardConfig.h` | Board-specific pin defaults and `Wire` setup |
| `BuildConfig.h` | Compile-time log-level configuration |
| `Log.h` | Serial logging helpers |
| `I2cTransport.h` | Wire-backed transport adapter and owner-level interface reset |
| `I2cScanner.h` | Bus scan helper that preserves owner clock/timeout settings |
| `CliStyle.h` | CLI prompt, help, and color formatting helpers |
| `CliShell.h` | Simple serial shell helper |
| `TypedMemory.h` | Example-only fixed-width integer/float/double codec on top of the raw driver |

## Validation

Release `v4.1.0` Arduino ESP32-S3/S2 examples are exact-pinned to pioarduino
Espressif platform `55.03.311` (Arduino-ESP32 `3.3.11`, ESP-IDF `5.5.5`) and
require PlatformIO Core `6.1.19` or newer. The `esp32s3dev_legacy_54`
environment is a build-only source-compatibility check for the previous
`54.03.20` stack; normal builds and HIL use the current pin.

On Windows hosts where long-path support is disabled, the Arduino 3.3.11
package can exceed the default PlatformIO extraction path. Enable Windows long
paths or use a short session-local core path, for example
`$env:PLATFORMIO_CORE_DIR='C:/pio'`, before installing/building the environment.

```powershell
.\scripts\pio.cmd test -e native
python tools/hil_runner.py --parser-self-test
python tools/check_cli_contract.py
python tools/check_core_timing_guard.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python tools/check_metadata_consistency.py
doxygen Doxyfile
```

`hil_runner.py` requires an explicit `--port` for both plan-only and real runs;
`--dry-run` never opens hardware. The canonical full build, package, and real
strict-HIL commands—including framework, transport-envelope, heap, and soak
gates—are in the [release checklist](docs/RELEASE_CHECKLIST.md).

## Documentation

- `CHANGELOG.md` - release history and GitHub release note source
- `docs/DEVICE_REFERENCE.md` - maintained MB85RC-family behavior reference
- `docs/IDF_PORT.md` - ESP-IDF portability and native example notes
- `docs/RELEASE_CHECKLIST.md` - release verification checklist
- `docs/reports/HIL_SUMMARY.md` - revision-specific hardware evidence ledger
- `docs/reference-pdfs/` - retained vendor datasheets and fact sheet
- `CONTRIBUTING.md` - contribution workflow and required validation
- `SECURITY.md` - supported-version and vulnerability-reporting policy
- `Doxyfile` - builds strict public-header and maintained-documentation output

## License

MIT License. See [LICENSE](LICENSE).
