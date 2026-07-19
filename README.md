# MB85RC Driver Library

Production-oriented MB85RC-family FRAM I2C driver for ESP32-S2 / ESP32-S3 using Arduino/PlatformIO and ESP-IDF.

Library version: `4.0.0` (release tag pending)

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
  https://github.com/janhavelka/MB85RC.git#<reviewed-immutable-commit>
```

Use `#v4.0.0` after the release tag is published. Production integrations
should pin the reviewed full commit, not a branch name.

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
current-address reads, active-capacity-bounded memory commands, HS/Sleep
diagnostics, typed demo, random benchmark, self-test, and stress diagnostics.

Mutating ESP-IDF CLI commands use explicit `!` confirmation forms such as
`write!`, `fill!`, `rw_suite!`, and `typed_demo!`.

The ESP-IDF CLI owns its I2C bus and uses blocking console input. That console input can block the example loop before `tick()` runs; this is acceptable for
the current diagnostic CLI because `tick()` does no async I2C or write-delay
work. It only advances Sleep `WAKING` to `AWAKE` state from caller-supplied
time after a wake operation.
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
Production fixed-BOM integrations
should select the exact variant and validate its identity before publishing the
device ready. `MB85RC16V` has no Device ID command and must be selected
explicitly.

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
Applications that
need meaningful health timestamps or Sleep wake gating should inject
`Config::nowMs`; otherwise timestamps remain `0` and wake gating advances only
when the caller supplies time to `tick()`.

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

## Release 4.0.0 Highlights

- Adds zero-I/O `bind()`; compatibility `begin()` retains a valid binding after
  a presence/identity failure so later owner-directed attempts remain possible.
- Replaces callback `Status` with terminal `TransportResult`, including exact
  byte counts, typed transport codes, and write-commit knowledge.
- Routes Device ID through `I2cSpecialOp::READ_DEVICE_ID`, keeping reserved
  address `0x7C` outside normal scan and device-transfer policy.
- Adds transport `maxTxBytes`/`maxRxBytes`, exact one-transaction primitives,
  and active-variant `maxWriteDataBytes()`/`maxReadDataBytes()`.
- Makes health states diagnostic-only. Previous failures never block a later
  transaction requested by the application bus owner.
- Adds request-qualified cooperative transfers whose results retain no caller
  buffer pointers, with exactly-once terminal consumption, cancellation,
  explicit timeout, and verified-write reconciliation that never blindly
  replays an ambiguous write.
- Pins PlatformIO 6.1.18 and pioarduino Espressif platform 54.03.20 in one
  stable ESP32-S3 CI reference while retaining broader ESP32-S2/S3
  compatibility builds.

Breaking notes: all injected I2C callbacks now return `TransportResult`, and
production staged callers should use nonzero request IDs and consume terminal
results. `Config` adds explicit transport capacities. The compatibility
`begin()`, legacy request overloads, and observational `recover()` remain for
incremental migration, but OFFLINE no longer changes admission.

## Release 3.0.0 Highlights

- Adds accepted-prefix reporting with `writeDetailed()` and `fillDetailed()` for non-atomic bulk writes/fills.
- Adds readback verification helpers: `VerifyDetailedResult`, `verifyDetailed()`, `writeVerify()`, `fillVerify()`, and `VERIFY_MISMATCH`.
- Adds variant-gated High-speed and Sleep APIs for `MB85RC64TA`, `MB85RC512T`, and `MB85RC1MT`, with unsupported variants returning `UNSUPPORTED` before bus traffic.
- Adds optional `Config::i2cSpecial` for HS-prefixed transfers, Sleep entry, and Sleep wake stimulus; the core still does not change the MCU I2C clock or insert hidden delays.
- Deletes `MB85RC` copy/move operations and documents thread/ISR/reentrancy contracts.
- Tightens current-address invalidation after failed or diagnostic transactions.
- Adds pure ESP-IDF CI build configuration for ESP32-S2/S3 and native IDF Device-ID manual-address handling.
- Expands Arduino and ESP-IDF diagnostic CLI parity, including HS/Sleep diagnostics and explicit confirmation for destructive IDF commands.
- Adds production documentation for WP-high ACK/no-persistence behavior, accepted-prefix versus verified persistence, and hardware-validation planning.

Breaking notes: applications that copied/moved `MB85RC` instances must keep a
single instance and pass it by reference or pointer. Applications using
positional aggregate initialization for `Config` should switch to default
construction plus named member assignment.

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

- Historical 1.1.0 behavior latched `OFFLINE`; 4.0.0 supersedes that admission policy and keeps health observational.
- Public MB85RC family variant metadata documents family address models.
- Cross-library diagnostics have `driverState()` and a value-returning `getSettings()` overload.
- Validation and recovery paths keep health counters aligned with transport failures and Device ID mismatches.
- The bringup CLI and documentation cover current validation, health, and family-reference behavior.

## API Reference

### Lifecycle

- `Status bind(const Config& config)` - validate and retain configuration with zero I2C
- `Status begin(const Config& config)` - compatibility composition of passive bind plus one explicit presence/identity transaction; binding survives I/O failure
- `void tick(uint32_t nowMs)` - bounded maintenance hook; no async I2C or write-delay work, but advances Sleep `WAKING` to `AWAKE` from caller-supplied time
- `void end()` - shut down without bus traffic; active work becomes a retained
  `CANCELLED` result that must be consumed before rebinding

### Variant Selection

- `Config::expectedVariant` - `AUTO` by default, or an explicit `DeviceVariant` for fixed-BOM validation.
- `const cmd::VariantInfo* variantInfo() const` - active variant metadata after fixed-variant bind or AUTO identity selection.
- `const char* variantName() const` - active variant name, or `unknown` before selection.
- `DeviceId deviceId() const` - cached Device ID from the last successful identity validation.
- `uint32_t capacityBytes() const` - active runtime capacity.
- `uint32_t maxAddress() const` - active highest valid memory address.
- `uint32_t maxNormalBusHz() const` - active variant normal-mode I2C bus limit.
- `uint32_t maxHighSpeedBusHz() const` - active variant HS bus limit, or `0` when unsupported.
- `static constexpr uint16_t memorySize()` - legacy MB85RC256V size helper retained for existing users.

### High-Speed And Sleep APIs

- `bool supportsHighSpeedMode() const` - true only for variants with local HS-mode datasheet support.
- `Status enterHighSpeedMode()` / `Status exitHighSpeedMode()` - enable or disable HS-prefixed memory/current-address transfers; the MCU bus clock remains application-owned.
- `Status setHighSpeedMode(bool enabled)` - explicit form of the same HS transfer-mode toggle.
- `bool supportsSleepMode() const` - true only for variants with local Sleep-mode datasheet support.
- `SleepState sleepState() const` - `AWAKE`, `ASLEEP`, `WAKING`, or ambiguity-safe `UNKNOWN`, separate from driver health.
- `uint16_t sleepRecoveryUs() const` - active variant `tREC` contract in microseconds.
- `Status enterSleep()` - send the Sleep entry sequence; ambiguous failure enters `UNKNOWN` and blocks normal access.
- `Status wake()` / `Status wakeFromSleep()` - reconcile `ASLEEP`/`UNKNOWN`, send the wake stimulus, and enter `WAKING`; call `tick()` after the recovery interval before normal access.

### Memory Operations

- `Status readOnce(uint32_t addr, uint8_t* data, size_t len)` - exactly one addressed read transaction
- `Status writeOnce(uint32_t addr, const uint8_t* data, size_t len, WriteCommit* out = nullptr)` - exactly one addressed write transaction with commit knowledge
- `Status verifyOnce(uint32_t addr, const uint8_t* expected, size_t len, VerifyResult& out)` - exactly one addressed read/compare transaction
- `Status readByte(uint32_t addr, uint8_t& out)` - read one byte
- `Status read(uint32_t addr, uint8_t* buf, size_t len)` - synchronously read a contiguous block with internal chunking
- `Status readCurrentAddress(uint8_t& out)` - read from the device's current internal address pointer
- `Status readCurrentAddress(uint8_t* buf, size_t len)` - repeat documented current-address reads into a buffer
- `Status writeByte(uint32_t addr, uint8_t value)` - write one byte; success means the I2C transaction was accepted
- `Status write(uint32_t addr, const uint8_t* data, size_t len)` - synchronously write a contiguous block with non-atomic chunking
- `WriteResult writeDetailed(uint32_t addr, const uint8_t* data, size_t len)` - write and report requested bytes, accepted prefix, and first failed chunk
- `Status fill(uint32_t addr, uint8_t value, size_t len)` - synchronously fill a region with a constant byte using non-atomic chunking
- `WriteResult fillDetailed(uint32_t addr, uint8_t value, size_t len)` - fill and report requested bytes, accepted prefix, and first failed chunk
- `Status verify(uint32_t addr, const uint8_t* expected, size_t len, VerifyResult& out)` - synchronously read back and compare FRAM contents against expected bytes
- `VerifyDetailedResult verifyDetailed(uint32_t addr, const uint8_t* expected, size_t len)` - verify with requested and verified byte counts
- `Status writeVerify(uint32_t addr, const uint8_t* data, size_t len, VerifyDetailedResult* out = nullptr)` - write then verify, returning `VERIFY_MISMATCH` on readback mismatch
- `Status fillVerify(uint32_t addr, uint8_t value, size_t len, VerifyDetailedResult* out = nullptr)` - fill then verify, returning `VERIFY_MISMATCH` on readback mismatch
- `Status requestRead(uint32_t id, uint32_t addr, uint8_t* data, size_t len)` - queue a request-qualified addressed read
- `Status requestWrite(uint32_t id, uint32_t addr, const uint8_t* data, size_t len)` - queue a request-qualified addressed write
- `Status requestFill(uint32_t id, uint32_t addr, uint8_t value, size_t len)` - queue a request-qualified fill
- `Status requestVerify(uint32_t id, uint32_t addr, const uint8_t* expected, size_t len)` - queue a request-qualified verify
- `Status requestVerifiedWrite(uint32_t id, uint32_t addr, const uint8_t* data, size_t len)` - queue a single-chunk write/readback job with ambiguity reconciliation
- `Status pollTransfer(uint32_t nowMs, uint8_t maxInstructions)` - execute up to the requested number of queued chunks
- `bool isTransferBusy() const` - true while a queued transfer remains active
- `Status getTransferStatus() const` - current or terminal staged-transfer status
- `Status getTransferProgress(TransferResult& out) const` - copy progress/terminal state without retaining caller-buffer pointers
- `Status takeTransferResult(TransferResult& out)` - consume one retained terminal result exactly once
- `Status resumeVerifiedWrite(uint32_t id)` - authorize verify-only readback after an indeterminate write
- `Status cancelTransfer(uint32_t id)` - cancel between callbacks without rolling back accepted chunks
- `Status timeoutTransfer(uint32_t id)` - owner-directed deadline expiry without I2C

### Synchronous Bulk Convenience APIs

The whole-range helpers are blocking convenience APIs: `read()`, `write()`,
`fill()`, `verify()`, `writeDetailed()`, `fillDetailed()`,
`verifyDetailed()`, `writeVerify()`, `fillVerify()`, and
`readCurrentAddress(uint8_t*, size_t)`. They may perform several backend I2C
transactions before returning. That is appropriate for simple applications and
diagnostics, but it does not preserve a scheduler model that advances one
backend transfer per poll. TunnelMonitor-style integrations should treat these
helpers as convenience-only and use the one-transaction or staged API for
poll-budgeted FRAM work. Their maximum callback counts are the formulas in
Bounded Operation Classes; no helper retries or waits for FRAM programming.

### Poll-Chunked Transfer API

The `request*()` / `pollTransfer()` API preserves a poll-budgeted scheduler
model. Requests validate request identity, buffer, length, active-capacity
range, bound state, result/transfer exclusivity, and Sleep gating, then return
without I2C traffic. Diagnostic OFFLINE classification never blocks a request.
Caller-owned write/verify input buffers must remain valid and unmodified until
the request reaches a terminal state; read output buffers must remain valid and
may contain the completed prefix after each poll. The core clears all retained
caller-buffer pointers when a request terminalizes.
`pollTransfer(nowMs, maxInstructions)` advances Sleep wake state from `nowMs`
and executes whole addressed chunks only: one random-read chunk, one
sequential-write chunk, or one verify readback chunk is one instruction.
`maxInstructions == 0` emits no bus traffic and leaves the job in progress.
Large reads/verifies use the smaller of configured RX capacity and the core's
128-byte buffer. Writes subtract active memory-address bytes from configured TX
capacity, up to the core's 126 data-byte ceiling. Fills additionally cap chunks
at `cmd::MAX_FILL_CHUNK` (`64` bytes). `pollTransfer()` clamps high budgets to
`cmd::MAX_TRANSFER_INSTRUCTIONS_PER_POLL` (`8`) chunks per call. Every chunk
encodes its own memory address and does not rely on current-address state across
polls.

While a staged transfer is active, other public bus-touching APIs return
`Err::BUSY` without I2C so the queued job cannot be interleaved with synchronous
operations. A transport failure ends the transfer and preserves the transport
status. `requestVerify()` returns `Err::VERIFY_MISMATCH` from
`pollTransfer()` when readback differs, with `Status::detail` set to the first
mismatching offset.

External-owner firmware should queue with its own nonzero request ID, retain its
original absolute deadline, call `pollTransfer(nowMs, 1)` when one physical
transaction per owner poll is required, and consume the matching terminal once.
Use `requestVerifiedWrite()` for critical single-chunk writes so an ambiguous
failure can pause for owner recovery and resume as readback only. Application
journaling remains required when atomic multi-record durability matters.

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

| Variant | Capacity | I2C address model | Memory address bytes/model | Device ID | Max bus speed claimed | Notes |
| --- | ---: | --- | --- | --- | --- | --- |
| `MB85RC04V` | 512 B | Base strap must be even (`0x50`, `0x52`, `0x54`, `0x56`); A8 selects the per-transaction address | 1 byte plus A8 in I2C address | Yes, product `0x010`; `AUTO` supported | 1 MHz | No high-speed or sleep command support documented in local summary. |
| `MB85RC16V` | 2 KB | Base must be `0x50`; transaction address encodes memory A10:A8 and there are no external address-select pins in the local model | 1 byte plus A10:A8 in I2C address | No; select `DeviceVariant::MB85RC16V` explicitly | 1 MHz | Memory-probe diagnostics only; `AUTO` cannot discover it. |
| `MB85RC64TA` | 8 KB | `0x50`-`0x57`; A2/A1/A0 pins select device | 2 bytes, active range `0x0000`-`0x1FFF` | Yes, product `0x358`; `AUTO` supported | 1 MHz normal, 3.4 MHz high-speed after entry command | High-speed transfer mode and sleep entry/wake are variant-gated; application controls the MCU bus clock and wake delay. |
| `MB85RC256V` | 32 KB | `0x50`-`0x57`; A2/A1/A0 pins select device | 2 bytes, active range `0x0000`-`0x7FFF` | Yes, product `0x510`; `AUTO` supported | 1 MHz | No high-speed or sleep command support documented in local summary. |
| `MB85RC512T` | 64 KB | `0x50`-`0x57`; A2/A1/A0 pins select device | 2 bytes, active range `0x0000`-`0xFFFF` | Yes, product `0x658`; `AUTO` supported | 1 MHz normal, 3.4 MHz high-speed after entry command | High-speed transfer mode and sleep entry/wake are variant-gated; application controls the MCU bus clock and wake delay. |
| `MB85RC1MT` | 128 KB | Base strap must be even (`0x50`, `0x52`, `0x54`, `0x56`); A16 selects the per-transaction address | 2 bytes plus A16 in I2C address | Yes, product `0x758`; `AUTO` supported | 1 MHz normal, 3.4 MHz high-speed after entry command | High-speed transfer mode and sleep entry/wake are variant-gated; application controls the MCU bus clock and wake delay. |

`AUTO` uses the Device ID command and therefore works only on variants that
implement Device ID. `MB85RC16V` must be selected explicitly. The driver
derives runtime transaction addresses from `Config::i2cAddress` plus the active
variant's address model, and rejects ambiguous base addresses before normal
operation.

The maintained device reference in `docs/DEVICE_REFERENCE.md` lists endurance
and retention claims by variant. Use the exact part datasheet for production
lifetime budgets: the local reference shows 10^12 writes/byte for
`MB85RC04V`, `MB85RC16V`, and `MB85RC256V`, and 10^13 writes/byte for
`MB85RC64TA`, `MB85RC512T`, and `MB85RC1MT`. Retention statements vary by part
and temperature.

## Validation Status

| Coverage area | Current evidence | Status |
| --- | --- | --- |
| Implemented behavior | Passive binding, terminal typed transport, explicit Device ID special operation, one-transaction primitives, cooperative result identity/reconciliation, range checks, and diagnostic health | Implemented in code |
| Unit-test coverage | Native fake-bus tests cover binding silence, terminal callback validation, configured capacities, variant/address boundaries, staged budgets/results/cancel/timeout, ambiguous writes, WP-high simulation, HS/Sleep gating, and health observation | Covered by native tests |
| CI/build coverage | Stable PlatformIO 6.1.18/pioarduino 54.03.20 ESP32-S3 reference, broader PlatformIO ESP32-S2/S3 builds, native tests, guards, package validation, and pure ESP-IDF example jobs | Covered by CI configuration; local IDF build depends on `idf.py` availability |
| HIL runner gate | `tools/hil_runner.py --strict` can require variant/product/capacity, zero UNKNOWNs, final READY health, zero failures, zero resets/reconnects, and heap thresholds | Tooling implemented; production evidence still requires the exact hardware rows below |
| Hardware validation | Board/variant/address-pin/WP/brownout/shared-bus/soak evidence | Pending hardware; use the matrix below |

Suggested production soak thresholds for the diagnostic examples are
`--heap-max-drop-bytes 1024` and `--heap-min-free-bytes 8192`. Board-specific
threshold changes must be documented in the HIL report with rationale.

## Hardware Validation Matrix

Status values below are planning states, not claims. Mark rows complete only
after recording board, MCU, FRAM package/date code, supply voltage, pull-ups,
bus speed, address-pin straps, WP wiring, command log, and captured evidence.

| Scenario | Variant(s) | Address pins | Command/test | Expected evidence | Status |
| --- | --- | --- | --- | --- | --- |
| Passive bind plus explicit Device ID | `MB85RC04V`, `MB85RC64TA`, `MB85RC256V`, `MB85RC512T`, `MB85RC1MT` | Each board's selected strap | `bind(AUTO)`, `id`, `idraw` | Bind emits no traffic; special ID operation returns manufacturer `0x00A`, expected product ID, and selects capacity | Pending hardware |
| No-ID explicit variant | `MB85RC16V` | A10:A8 encoded in transaction address | `bind(MB85RC16V)`, memory probe, `readDeviceId()` negative check | Explicit bind succeeds without I2C; probe can run later; Device ID APIs reject as unsupported | Pending hardware |
| High-speed entry and 3.4 MHz access | `MB85RC64TA`, `MB85RC512T`, `MB85RC1MT` | Production straps | `hs support`, `hs enter`, reconfigure application bus to 3.4 MHz, `read`/`writeVerify` on sacrificial range | HS entry prefix observed; 3.4 MHz transactions verify; STOP exit behavior understood | Pending hardware |
| Unsupported High-speed rejection | `MB85RC04V`, `MB85RC16V`, `MB85RC256V` | Any valid strap | `hs support`, `hs enter` | CLI/API report unsupported; no HS bus sequence is emitted | Pending hardware |
| Sleep enter/wake/recover | `MB85RC64TA`, `MB85RC512T`, `MB85RC1MT` | Production straps | `sleep support`, `sleep enter`, `sleep wake`, `recover`, then `read`/`writeVerify` | Sleep current/reduced activity observed if measured; wake waits `tREC >= 400 us`; access recovers | Pending hardware |
| Unsupported Sleep rejection | `MB85RC04V`, `MB85RC16V`, `MB85RC256V` | Any valid strap | `sleep support`, `sleep enter`, `sleep wake` | CLI/API report unsupported; no sleep bus sequence is emitted | Pending hardware |
| Address pin combinations | `MB85RC04V`, `MB85RC64TA`, `MB85RC256V`, `MB85RC512T`, `MB85RC1MT` | A1/A2 or A0/A1/A2 low/high combinations as applicable | I2C scan plus `begin()` at each strapped address | Only strapped address responds; wrong addresses NACK | Pending hardware |
| Upper-address bits in I2C address | `MB85RC16V` | No external address-select pins | Write/read across `0x00FF`, `0x0100`, and `0x07FF` | A10:A8 transaction address selection works and exact-end byte verifies | Pending hardware |
| Exact-end read/write | All supported variants | Default and at least one nonzero strap | `writeVerify(maxAddress - n + 1, n)` and `read()` | Last valid byte range succeeds and verifies | Pending hardware |
| Boundary rejection | All supported variants | Any valid strap | Cross-end `read`, `write`, `fill`, `verify` | Driver returns `ADDRESS_OUT_OF_RANGE` before bus traffic when observable | Pending hardware |
| Sequential public no-wrap contract | All supported variants | Any valid strap | Attempt bulk range crossing capacity | Public API rejects; no reliance on datasheet rollover | Pending hardware |
| WP high behavior | At least `MB85RC256V`, plus each production BOM variant | WP low/open, then WP high | `write()`, `verify()`, `writeVerify()` | WP low/open persists; WP high may ACK but memory remains unchanged; verify catches mismatch | Pending hardware |
| Bulk write/fill/verify | All supported variants | Any valid strap | `writeDetailed()`, `fillDetailed()`, `verifyDetailed()` over multi-chunk ranges | Full accepted counts and readback match | Pending hardware |
| Current-address read after explicit set | All supported variants | Any valid strap | Addressed `read()` or `write()`, then `readCurrentAddress()` | Pointer advances only after known address-setting transaction | Pending hardware |
| Unplug/NACK and recovery | Representative Device-ID and no-ID variants | Any valid strap | Disconnect device or force wrong address, then explicit identity/probe after owner recovery | Transport error retained, health observation degrades/offlines, and later owner attempt is never gated | Pending hardware |
| Brownout/power-cycle persistence | Each production BOM variant | Production straps and WP wiring | Write record, verify, power-cycle/brownout, read/verify | Data persists or application journal rejects torn record | Pending hardware |
| Pure ESP-IDF CLI | ESP32-S2 and ESP32-S3 with production BOM variant | Production straps | `idf.py` build, flash, `id`, `idraw`, `settings`, `rw_suite!`, `xfer_demo!`, `typed_demo!`, `heap` | Native IDF CLI runs without Arduino compatibility and commands pass | Pending hardware |
| Shared bus with another device | Production board topology | Production straps | Concurrent application bus manager test with another I2C device | External serialization prevents interleaved transactions and recovers from peer failures | Pending hardware |
| Long soak on sacrificial range | Each production BOM variant | Production straps | Repeated write/read/verify with CRC/generation counter | No mismatches over planned duration; failures logged with supply and temperature | Pending hardware |

## Notes

- `read()`, `write()`, `fill()`, and `verify()` reject ranges where `address + len > capacityBytes()`; they do not silently wrap bulk operations.
- `write()` and `fill()` are not atomic across internal chunks. A failed later chunk can leave an accepted prefix in the target range; the driver does not roll back earlier chunks.
- Successful write/fill status means I2C acceptance, not verified persistence. Use `verify()` for readback confidence, especially when the hardware `WP` pin may be asserted.
- `readCurrentAddress()` is only meaningful after a successful addressed memory read/write because the current address is undefined after power-on.
- The bulk `readCurrentAddress(uint8_t*, size_t)` helper repeats the documented current-address read primitive while preserving tracked pointer behavior and rejecting cross-capacity reads.
- Argument validation errors reject null buffers, zero lengths, and out-of-range start addresses before touching the bus or health counters.
- `recover()` invalidates current-address tracking and records transport
  failures in health. A successful identity transaction followed by a semantic
  Device ID mismatch returns that mismatch but remains health-neutral.
- `verify()` reports the first mismatch without inventing a synthetic device error code; transport failures still return normal `Status` errors.
- Use `I2C_TIMEOUT` for injected transport transaction timeouts. The generic
  `TIMEOUT` code records owner-declared staged deadline expiry through
  `timeoutTransfer()`.
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
  - `hs`, `hs support`, and `hs enter` for High-speed capability diagnostics
  - `sleep`, `sleep support`, `sleep enter`, and `sleep wake` for Sleep mode diagnostics
  - `drv`, `heap`, `probe`, `recover`, `selftest`, `stress`, `stress_mix` for diagnostics
  - `rw_suite` for read/write/fill/verify diagnostics with best-effort restore
  - `xfer_demo` for poll-chunked transfer API diagnostics with best-effort restore, including zero-budget, two-instruction, and high-budget-clamp polling checks
  - `randbench [N]` for random-access timing over a scratch window with compact restore status
  - `typed_demo` for fixed-width integer/float/double storage with compact pass/fail status

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
hs enter                  # Enable HS-prefixed transfers if supported by variant/transport
sleep support             # Show active variant Sleep capability and tREC
sleep enter               # Send Sleep entry sequence if supported
sleep wake                # Wake, wait recovery interval, then recover
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
4. Timing model: `tick()` is bounded and performs no async I2C or write-delay work; it only advances Sleep `WAKING` to `AWAKE` from caller-supplied time. Public I2C operations are blocking.
5. Shared-bus ownership: bus, pins, locking, timeout policy, retry policy, and recovery policy remain application-owned via `Config` and the injected transport. The core never initializes or owns `Wire`, ESP-IDF I2C handles, pins, or a global bus.
6. Memory behavior: no heap allocation in steady-state library operation; bulk memory APIs reject cross-end ranges instead of relying on device rollover.
7. Current-address reads: use `readCurrentAddress()` only after a known address-setting transaction, such as a successful addressed `read()`, `readByte()`, `write()`, `writeByte()`, or `fill()` by the same instance. Current-address state is undefined after power-up and is conservatively invalidated after failed I2C memory/current-address transactions and `recover()`. Raw diagnostics such as `probe()` are not address-setting contracts and may disturb the device pointer; use an addressed read after them if current-address state matters.
8. Error handling: all fallible APIs return `Status`; no exceptions and no silent failures. Public enum numeric values are part of the embedded API contract and future additions are append-only.
9. High-speed and Sleep: HS/Sleep APIs are variant-gated and require the optional special transport callback. The core does not change the MCU bus clock or insert Sleep wake delays; the application bus manager owns those policies.
10. Health behavior: `READY`, `DEGRADED`, and optional `OFFLINE` are diagnostics only. A previous failure never blocks the next owner-requested transaction. Validation/precondition errors, `WRITE_PROTECTED`, `VERIFY_MISMATCH`, and raw `probe()` diagnostics do not increment transport-health counters.
11. Result lifetime: cooperative terminal results retain no caller-buffer pointers, are request-qualified, remain until one successful `takeTransferResult()`, and are never silently replaced. Any `Status::msg` is library-owned static text.
12. Cancellation/deadline: cancel and timeout act only between synchronous callbacks, issue no I2C, preserve accepted-prefix/indeterminate-write evidence, and cannot interrupt a callback already in flight.

## Validation

```bash
python -m platformio test -e native
python tools/hil_runner.py --parser-self-test
python tools/hil_runner.py --dry-run --port COM27 --baud 115200 --timeout-s 5 --strict --require-variant MB85RC256V --require-product-id 0x510 --require-capacity 32768 --soak-duration-s 28800
python tools/check_cli_contract.py
python tools/check_core_timing_guard.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python tools/check_metadata_consistency.py
python -m platformio run -e esp32s3dev_pinned
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack

# Build the ESP-IDF full CLI example (requires ESP-IDF on PATH)
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
doxygen Doxyfile
```

## Documentation

- `CHANGELOG.md` - release history and GitHub release note source
- `docs/README.md` - documentation index and vendor PDF map
- `docs/DEVICE_REFERENCE.md` - maintained MB85RC-family behavior reference
- `docs/IDF_PORT.md` - ESP-IDF portability and native example notes
- `docs/RELEASE_CHECKLIST.md` - release verification checklist
- `docs/TUNNELMONITOR_NODE_SUITABILITY_AUDIT.md` - current external-owner suitability dispositions and remaining gates
- `docs/reference-pdfs/` - retained vendor datasheets and fact sheet
- `Doxyfile` - indexes public headers, the ESP-IDF port notes, the Arduino CLI,
  and the native IDF entry point

## License

MIT License. See [LICENSE](LICENSE).
