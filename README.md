# MB85RC Driver Library

Framework-neutral MB85RC-family FRAM I2C driver with Arduino/PlatformIO and
ESP-IDF examples for ESP32-S2 / ESP32-S3.

Library version: `4.2.0` (package metadata).
Release tag: [v4.2.0](https://github.com/janhavelka/MB85RC/releases/tag/v4.2.0).
Later development changes are listed under [Unreleased](CHANGELOG.md#unreleased).

## Features

- Injected, terminal I2C callbacks with no framework dependency in library code
- Zero-I/O `bind()`, bus-silent `tick()` and `end()`, plus compatibility `begin()`
- Six runtime variants, covering 512 B through 128 KiB
- One-transaction read/write/verify primitives and bounded chunked helpers
- Cooperative jobs with request IDs, cancellation, timeout, partial progress,
  readback reconciliation, and retained exactly-once results
- Current-address reads, Device ID, and variant-gated High-speed/Sleep operations
- Cache-only settings and diagnostic health that never suppresses owner-directed work

## Installation

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps =
  https://github.com/janhavelka/MB85RC.git#<reviewed-immutable-commit>
```

Use `#v4.2.0` for this release. To use changes listed under Unreleased,
pin a reviewed full commit containing them. Keep production dependencies on an
immutable release or commit.

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

Build the native ESP-IDF diagnostic CLI with:

```bash
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

The native boundary, command/confirmation policy, transport mapping, and runtime
version telemetry are maintained in the [ESP-IDF port notes](docs/IDF_PORT.md).

## Quick Start

This snippet uses the repository's example-only Arduino transport adapter from
`examples/common/I2cTransport.h`. It is packaged with the examples and remains
outside the public library API. Production applications should provide an
equivalent adapter around their application-owned bus, locking, timeout, and
recovery policy. Make `examples/` available on the include path for this snippet.

```cpp
#include <Wire.h>
#include "MB85RC/MB85RC.h"
#include "common/I2cTransport.h"

MB85RC::MB85RC device;
transport::WireContext wireContext;

void setup() {
  Serial.begin(115200);
  if (!transport::initWire(wireContext, Wire, 8, 9, 400000, 50)) {
    return;
  }

  MB85RC::Config cfg;
  cfg.i2cWrite = transport::wireWrite;
  cfg.i2cWriteRead = transport::wireWriteRead;
  cfg.i2cSpecial = transport::wireSpecial;
  cfg.i2cUser = &wireContext;
  cfg.maxTxBytes = transport::MAX_TX_BYTES;
  cfg.maxRxBytes = transport::MAX_RX_BYTES;
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

`Config::maxTxBytes` and `Config::maxRxBytes` describe the TX and RX buffer
capacities of the injected transport. TX capacity includes memory-address bytes;
RX capacity covers returned data. `bind()` rejects a capacity too small for one
valid transaction without I2C. Capacities larger than the core's fixed 128-byte
buffers are valid; active operations clamp
to the smaller core limit. For a two-byte-address variant and
`maxTxBytes = 126`, `maxWriteDataBytes()` is 124, matching a 124-byte owner
payload plus its two address bytes.

`Config::i2cUser`, `Config::timeUser`, and the state they reference must remain
valid until `end()` or another accepted binding replaces the configuration.
A rejected `bind()` preserves the previous binding. Compatibility `begin()`
binds first, then identifies or probes the device: a later identification/probe
failure leaves the new configuration active even though `begin()` returns an
error. Prefer `bind()` followed by explicit identity checks for clear lifecycle
control.

Applications that need meaningful health timestamps or Sleep wake gating should
inject `Config::nowMs`. Without it, health timestamps remain `0` and a successful
`wake()` reports `AWAKE` immediately; the caller must enforce the recovery wait.

## I2C Ownership And Concurrency

The core driver never owns the I2C bus. It does not initialize pins, create
Arduino `Wire` or ESP-IDF handles, configure bus recovery, change clock speed,
or implement shared-bus locking. The application owns those policies through
`Config::i2cWrite`, `Config::i2cWriteRead`, `Config::i2cSpecial`, and the user
context pointer.

Each transport callback is synchronous and represents exactly one completed
physical transaction. `TransportCode::OK` means the complete requested TX/RX
lengths were transferred; short completion is rejected by the core. A
write-read callback with both TX and RX must use a repeated START with no
intervening STOP. TX-only requests end with STOP; RX-only requests perform a
current-address read without an address-setting write.
Callbacks return no queued/in-progress state, perform no hidden retry or bus
recovery, and never recursively call the same driver. Failed-read buffers are
unspecified. Failed writes report `WriteCommit::NOT_COMMITTED` only when the
transport can prove no requested data was accepted; an uncertain effect is
`INDETERMINATE`. Completion counts cover the callback buffers, so memory-write
TX counts include the one- or two-byte memory-address prefix. Special-operation
counts cover only `I2cSpecialTransfer::txData`/`rxData`, not hidden Device-ID,
High-speed, Sleep, or wake framing. A failed full `ACCEPTED` claim is valid only
for a later timeout/bus/I/O error; a NACK contradicts full acceptance and is
normalized conservatively.

`MB85RC` instances are not internally thread-safe. Use one task, or serialize
all public calls that can touch driver state or I2C. Public I2C APIs are not
ISR-safe because transport callbacks can block until the configured timeout.

The example Wire adapter temporarily applies each callback's supplied
`1..1000` ms timeout, restoring the prior controller timeout on return. Serialize
the complete callback and keep Wire's mutex uncontended: the controller timeout
does not bound its mutex wait or scheduler overhead.

`TransportCode::NACK_UNSPECIFIED` becomes `Err::I2C_NACK` when the backend cannot
identify which byte was rejected. ESP32 Wire result 2 and IDF NACK outcomes
retain `INDETERMINATE` once a memory write was issued. Wire short reads remain
`IO_ERROR` because their cause is not exposed.

## Bounded Operation Classes

Let `T` be `Config::i2cTimeoutMs`, `W` be `maxWriteDataBytes()`, `R` be
`maxReadDataBytes()`, and `B` be the caller's poll budget clamped to
`cmd::MAX_TRANSFER_INSTRUCTIONS_PER_POLL` (`8`). The core bounds callback counts;
the time estimates below assume the transport enforces its callback deadline.
They exclude owner queueing, framework/scheduler overhead, and bus recovery.

### Steady-State Owner Operations

`readOnce()`, `writeOnce()`, and `verifyOnce()` validate the complete request
before I2C and invoke zero or one transport callback. Valid work therefore has
transport occupancy bounded by `T` when the callback enforces that deadline.
Length must be `1..R` for reads/verifies or `1..W` for writes. There is no hidden
wait, retry, recovery, or allocation.
`writeOnce()` returns the transport's commit knowledge; an accepted write is
still not persistence proof when WP is high.

This is the preferred surface for normal reads/writes performed by an external
bus-owner task when one physical transaction per scheduler poll is required.

### Multi-Step Runtime Operations

`requestRead()`, `requestWrite()`, `requestFill()`, `requestVerify()`, and
`requestVerifiedWrite()` perform zero I2C. `pollTransfer(nowMs, B)` performs at
most `B` complete callbacks, so one call occupies at most `B * T` in transport.
`B = 0` performs no I2C; the budget cannot preempt a callback or enforce an
elapsed-time deadline.
A length-`N` read/verify takes at most `ceil(N/R)` callbacks; a write takes at
most `ceil(N/W)`; a fill uses at most `ceil(N/min(W, 64))`. A verified write
must fit one write and one read transaction and takes at most two callbacks,
normally in separate polls when `B = 1`.

The external owner supplies a request ID in `1..0x7FFFFFFF` and owns the
absolute deadline. Unqualified compatibility requests use the reserved upper
half of the ID space. On expiry the owner calls `timeoutTransfer(requestId)`;
cancellation uses `cancelTransfer(requestId)`. Both terminalize between
callbacks and issue no I2C. A synchronous callback already in flight cannot be
interrupted by the core, so its own `T` bound remains mandatory. Accepted
prefixes are never rolled back.

If the write step of a cooperative verified write fails with
`WriteCommit::INDETERMINATE` or `WriteCommit::ACCEPTED`, it enters
`WAITING_FOR_RECONCILIATION`. Polling then performs zero callbacks until the
owner has recovered the bus and calls `resumeVerifiedWrite(requestId)`. Resume
authorizes readback only; the write is never replayed. Progress/results retain
request ID, kind, terminal state, byte counts, failed chunk, original write
status, readback status, commit state, and mismatch evidence without retaining
buffer pointers in those snapshots. Caller buffers must remain valid while the
request is active or waiting for reconciliation, and input bytes must remain unchanged.
One terminal result blocks new cooperative requests and rebinding until
`takeTransferResult()` consumes it exactly once. Inspect terminal state and
status even when the completed byte count equals the request length: a full
accepted prefix can still end in cancellation, timeout, or failed verification.

### Rare Or Maintenance Operations

Whole-range synchronous helpers are intentionally allowed to use the same
finite chunk formulas in one blocking call. Across the largest supported
128 KiB part, their upper bound is therefore finite and derived from capacity,
`W`, `R`, and `T`. Read, write, fill, and verify use the chunk counts above;
`writeVerify()` adds the write and verify counts, and `fillVerify()` adds the
fill and verify counts. The diagnostic `readCurrentAddress(buffer,N)` uses at
most `N` one-byte callbacks, completing all `N` on success. Each bound
therefore has worst-case transport occupancy equal to its callback count times
`T`. Use these helpers only in startup, diagnostics, commissioning, or a
maintenance window whose caller budget can tolerate that occupancy.

Device ID, Sleep entry, and wake stimulus each use at most one special callback.
High-speed enable/disable changes driver state without I2C; enabled memory
transfers carry the prefix in their own callback. With an injected clock, Sleep
recovery advances from caller-supplied time and inserts no hidden delay.

## High-Speed And Sleep Modes

The supported-variant table below lists High-speed/Sleep capability. Other parts
return `UNSUPPORTED` for High-speed enablement and Sleep entry/wake without bus
traffic.

MB85RC core does not change the MCU I2C clock, pins, controller mode, or bus
locking. `enterHighSpeedMode()` enables HS-prefixed memory/current-address
transfers through the optional `Config::i2cSpecial` callback; each transfer
sends the `0000 1XXX` master-code prefix because a STOP exits HS state. The
application bus manager must configure and validate 3.4 MHz operation if that
bus speed is used. The example adapters' different capabilities are listed in
the Examples section.

Sleep entry is emitted through `Config::i2cSpecial` as `F8h` plus the active
device address word, repeated START, then `86h`. On success the driver marks the
device asleep and invalidates current-address tracking. `wake()` sends the wake
stimulus, then the application must wait `tREC >= 400 us` before access or
`recover()`. With `Config::nowMs` supplied, the core records a conservative
millisecond wake gate; otherwise the caller owns that wait and the driver reports
`AWAKE` immediately. The core inserts no hidden delay. An unspecified NACK,
timeout, bus/I/O error, or malformed completion during Sleep entry can leave the
hardware effect ambiguous; the driver then reports
`SleepState::UNKNOWN`, blocks normal I2C, and requires an explicit `wake()`.
A failed wake remains `UNKNOWN`; with an injected clock, a successful wake enters
`WAKING` until the recovery gate expires.

## Data And Driver State

### FRAM Write Semantics

FRAM writes are immediate for supported variants. There is no EEPROM-style
program cycle, erase procedure, write delay, or ACK polling.

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
`bytesAccepted` field is not committed persistence. Only bytes confirmed equal
by `verify()` or `verifyDetailed()` should be treated as verified.
For critical writes or fills, use `writeVerify()` / `fillVerify()` or call
`verify()` after `write()` / `fill()`.

An uncertain write/fill chunk retains `WriteCommit::INDETERMINATE`; a timeout
alone does not prove that no bytes were accepted. Synchronous `writeVerify()` and `fillVerify()`
return the write/fill error without issuing readback. The cooperative
`requestVerifiedWrite()` instead pauses without bus traffic, lets the external
owner recover the bus, and resumes with readback only. Never resend an
indeterminate write before reconciliation.

Before an intentional maintenance rewrite, verify the desired bytes first and
skip the write when they already match. Endurance and atomic record updates
remain the application's data-layout responsibility.

### Current Address Semantics

Current-address read is an I2C/FRAM device feature that returns data from the
device's internal pointer. That pointer is undefined after power-up and can be
disturbed by diagnostics or failed transactions. Use explicit-address
`read(address, ...)` for deterministic production workflows, especially after
power loss, bus errors, `probe()`, or `recover()`. `readCurrentAddress()` is best
reserved for diagnostics or carefully controlled transaction sequences after a
known successful addressed read/write by the same instance.

### State And Health

`isOnline()` is a compatibility name for "passively bound"; it stays true in
diagnostic `DEGRADED` and `OFFLINE` states. Use `state()` for health display,
never for transport admission policy: `OFFLINE` never suppresses an
owner-requested transaction or claims bus-recovery authority.

`getSettings()` returns a cache-only `SettingsSnapshot` and never touches the
bus or health counters. `totalSuccess()`/`totalFailures()` count tracked results
since the latest accepted binding; `end()` also clears them. They are `uint32_t`
and wrap at `UINT32_MAX`; the `consecutiveFailures()` streak is `uint8_t` and
saturates.

## Supported Runtime Variants

| Variant | Capacity | Identity selection | High-speed / Sleep |
| --- | ---: | --- | --- |
| `MB85RC04V` | 512 B | Device ID, `AUTO` supported | No |
| `MB85RC16V` | 2 KiB | Explicit selection only | No |
| `MB85RC64TA` | 8 KiB | Device ID, `AUTO` supported | Yes |
| `MB85RC256V` | 32 KiB | Device ID, `AUTO` supported | No |
| `MB85RC512T` | 64 KiB | Device ID, `AUTO` supported | Yes |
| `MB85RC1MT` | 128 KiB | Device ID, `AUTO` supported | Yes |

The maintained [device reference](docs/DEVICE_REFERENCE.md) is the canonical
source for address encoding, product IDs, electrical limits, bus modes,
endurance, and retention notes. Use the exact BOM datasheet for final design
decisions.

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

The Arduino CLI in `examples/01_basic_bringup_cli/` and the native ESP-IDF CLI
in `examples/espidf_basic/` own their diagnostic buses. Use an application bus
manager and storage policy for production integrations.

Both bundled CLIs configure `Config::expectedVariant` as `DeviceVariant::AUTO`.
The `variants` command lists supported-part metadata; it does not select a
different part. To use `MB85RC16V`, set the example's `expectedVariant` to
`DeviceVariant::MB85RC16V` and rebuild, because that part has no Device ID
command. Other fixed-variant configurations also require editing this setting.

Both CLIs expose these workflows; `help` lists complete syntax:

| Commands | Purpose |
| --- | --- |
| `cfg` / `settings`, `drv`, `heap` | Configuration, driver health, and firmware heap |
| `id`, `idraw`, `variants`, `size` | Identity, supported parts, and active capacity |
| `read` / `dump` / `hexdump`, `text`, `strings`, `crc`, `verify` | Memory inspection and comparison |
| `current` / `cur` | Current-address reads after a known addressed access |
| `hs`, `sleep` | Capability reports and supported mode transitions |
| `scan`, `probe`, `recover`, `iface_reset` | Bus and device diagnostics/recovery |
| `selftest`, `rw_suite`, `xfer_demo`, `typed_demo` | Read/write/verify, cooperative transfer, and typed storage demos |
| `stress`, `stress_mix`, `randbench` | Scratch-memory stress and timing |

ESP-IDF requires explicit confirmation before changing FRAM contents:
`write!`, `fill!`, `selftest!`, `rw_suite!`, `stress!`, `stress_mix!`,
`xfer_demo!`, `randbench!`, and `typed_demo!`. Arduino uses the unsuffixed
command names. Temporary diagnostic writes require backup and verified
restoration; interruption or a failed restore can leave changed memory. Scratch
ranges and stress limits differ between the examples.

### Adapter Capabilities

The bundled Arduino board configuration uses a 10 ms controller/callback timeout
and configures the pinned ESP32 Wire buffer for 128-byte TX/RX transactions. On
two-byte-address variants this permits 126-byte write-data and 128-byte read
chunks while keeping timeout ownership in the example transport.

If the Arduino interface becomes unready, `scan` and transport operations refuse
further bus traffic. Run `iface_reset` to reinitialize it; a successful reset
rebinds the driver and repeats Device ID selection. The Validation section
below describes the legacy-core recovery exception.

The Arduino `Wire` adapter implements the Device ID special operation only;
`hs enter`, `hs exit`, `sleep enter`, and `sleep wake` return `UNSUPPORTED`
before bus traffic. The native ESP-IDF adapter implements Device ID, HS-prefixed
transfers, Sleep entry, and wake stimulus. Actual 3.4 MHz operation and Sleep
current require hardware qualification.

ESP-IDF startup and `iface_reset` invalidate cached driver state, initialize the
interface, and wake the FRAM before binding and checking identity. This handles
a part that stayed asleep through an MCU reset. See the
[ESP-IDF port notes](docs/IDF_PORT.md) for reset failure and retained-result
behavior.

### ESP-IDF CLI Inspection Examples

```text
hexdump 0x0000 128        # Hex + ASCII view of a region
text 0x0000 64           # Unescaped text display
strings 0x1000 512 6      # Scan a window with a minimum string length
crc 0x0000 1024          # CRC32 over a region for quick verification
verify 0x0020 55 55 55 55 # Compare live FRAM bytes against expected values
current 16               # Continue after a known successful addressed access
rw_suite!                # Confirmed deterministic read/write/fill/verify suite
xfer_demo!               # Confirmed poll-chunked transfer API demo
sleep wake               # Wake, wait recovery interval, then recover
```

### Example Helpers

`examples/common/` is outside the public library API. Its integration helpers are:

| File | Purpose |
|------|---------|
| `BoardConfig.h` | Board-specific pin defaults and `Wire` setup |
| `I2cTransport.h` | Wire-backed transport adapter and owner-level interface reset |
| `IdfI2cTransport.h` | Framework-neutral IDF result mapping and transaction dispatch, shared with native tests |
| `DiagnosticCore.h` | Shared CRC, range, verified-restore, staged-transfer, and enum-name helpers |
| `I2cScanner.h` | Bus scan helper that preserves owner clock/timeout settings |
| `TypedMemory.h` | Example-only fixed-width integer/float/double codec on top of the raw driver |

## Validation

The Arduino ESP32-S3/S2 examples are exact-pinned to pioarduino
Espressif platform `55.03.311` (Arduino-ESP32 `3.3.11`, ESP-IDF `5.5.5`) and
require PlatformIO Core `6.1.19` or newer. The `esp32s3dev_legacy_54`
environment is a build-only source-compatibility check for the previous
`54.03.20` stack; normal builds and HIL use the current pin.
The legacy 3.2.0 Wire core can retain its mutex when buffers are missing. After
an ambiguous close failure the adapter refuses further reset attempts on that
core; restart the board. The current core has a public cleanup path for this
case. Ordinary failed initialization remains retryable with `iface_reset`.

On Windows hosts where long-path support is disabled, the Arduino 3.3.11
package can exceed the default PlatformIO extraction path. Enable Windows long
paths or use a short session-local core path, for example
`$env:PLATFORMIO_CORE_DIR='C:/pio'`, before installing/building the environment.

Run the required native tests, Python contract/evidence checks, and strict
Doxygen build listed in [Contributing](CONTRIBUTING.md#validation). The
[release checklist](docs/RELEASE_CHECKLIST.md) adds complete Arduino/ESP-IDF
builds, packaging, and physical qualification. On Windows, use
`.\scripts\pio.cmd` for PlatformIO commands.

`hil_runner.py` requires an explicit `--port` for both plan-only and real runs;
`--dry-run` never opens hardware. Its current functional and soak plans require
Device ID. Explicit `MB85RC16V` configuration enables CLI memory diagnostics,
but that part needs a dedicated/manual plan without `id`/`idraw`;
`--require-variant` checks identity and does not change the plan.

HIL checks response completeness, memory byte counts, restoration results, and
driver health. Native ESP-IDF's unescaped `text` output is excluded from
automatic byte validation; hexadecimal reads and CRC remain covered. Keep full
transcripts and result excerpts private because they can contain original FRAM
contents. The release checklist describes collection integrity, strict gates,
and how to share reviewed summaries.

Native tests and CI builds cannot qualify electrical behavior. Hardware evidence
applies only to the tested revision, variant, board, address straps, WP wiring,
power profile, and bus topology; qualify each production configuration.

## Documentation

The public headers under `include/MB85RC/` are the authoritative API contract.
Run `doxygen Doxyfile` from the checkout to generate the reference under
`.pio/doxygen/`. Strict generation rejects undocumented public API,
parameter/return omissions, invalid commands, and unresolved documentation links.

- `CHANGELOG.md` - release history and GitHub release note source
- `docs/DEVICE_REFERENCE.md` - maintained MB85RC-family behavior reference
- `docs/IDF_PORT.md` - ESP-IDF portability and native example notes
- `docs/RELEASE_CHECKLIST.md` - release verification checklist
- `docs/reference-pdfs/` - retained vendor datasheets and fact sheet
- `CONTRIBUTING.md` - contribution workflow and required validation
- `SECURITY.md` - supported-version and vulnerability-reporting policy
- `Doxyfile` - builds strict public-header and maintained-documentation output

## License

MIT License. See [LICENSE](LICENSE).
