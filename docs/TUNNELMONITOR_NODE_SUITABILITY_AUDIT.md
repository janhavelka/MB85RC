# TunnelMonitor-node suitability audit

## MB85RC I2C FRAM library

Date: 2026-07-18

Audit result: **promising base, not suitable unchanged**

The MB85RC v3 code is a good chip-protocol base. It is framework-neutral,
bounded, heap-free in normal paths, supports the likely 8 KiB part, and has
useful native tests. The main gaps are ownership and failure semantics, not the
basic FRAM protocol.

Do not integrate the v2-era audit branch. Continue from current `main` at
`fe38e72`, make the focused refactors in this report, release a new immutable
revision, then integrate it behind an owner-private TunnelMonitor adapter.
Until that work is complete, the current direct FRAM implementation in
`I2cTask` is the safer implementation.

## Audit basis

The audit used these exact revisions:

| Repository | Revision | Notes |
| --- | --- | --- |
| TunnelMonitor-node | `fff99fe17e60b9287ec4d8d3eca5b3230ae44223` | Branch `prompt-44b-sequence`; architecture and current direct FRAM path |
| MB85RC previous local checkout | `bebad8a` | v2-era audit branch present when the original audit was written; not the recommended integration base |
| MB85RC v3 tag | `f0294b0d365776902951aa142c96d2722334f817` | Tag `v3.0.0` |
| MB85RC current local and remote main | `fe38e72b1b5b5d65d3acea9566315fb3db365fbb` | Audited as the latest candidate; two commits after the v3 tag |

Unless stated otherwise, MB85RC source line references below mean
`origin/main@fe38e72`. TunnelMonitor line references mean the revision above.

Primary chip facts were checked against the local RAMXEED/Fujitsu datasheets in
`docs/reference-pdfs/`, especially `MB85RC64TA-DS5v1-E.pdf` and
`MB85RC256V-Data-Sheet-DS501-00017-11v2-E.pdf`.

This was a suitability audit. It did not change firmware or library source,
select a production dependency revision, or run physical hardware tests.

## Latest branch revalidation

Revalidated after `git fetch origin --prune --tags` on 2026-07-18:

- GitHub reports `main` as the remote default branch.
- `origin/main@fe38e72` is also the newest remote branch tip by commit date.
  The next newest branch is
  `origin/hardening/mb85rc-industry-readiness@a6c9c59` and is already in the
  `main` history.
- The previous local checkout was
  `audit/mb85rc-idf-merged-industry-readiness@bebad8a`, 20 commits behind
  `origin/main`. It had no local source changes; only this audit report was
  untracked.
- The checkout was safely changed to `main` and local `main` was
  fast-forwarded from `4e2a7ad` to `fe38e72`. The report was preserved.
- The complete 20-commit delta from the previous `bebad8a` checkout to
  `fe38e72` was reviewed, including the changed public headers, core
  implementation, tests, examples, CI, and documentation. The staged transfer
  API and v3 variant work improve the candidate, but do not close H-02 through
  H-12.
- The final checked-out implementation was re-read at `fe38e72`; this was not
  only a branch-name or commit-label comparison. Current disposition by hard
  finding is:

| Finding | Recheck against final `main` |
| --- | --- |
| H-01 | Local baseline is resolved: the checkout is now v3-lineage `main`. A reviewed immutable integration pin is still required. |
| H-02 | Confirmed: `begin()` still performs presence/identity I2C and clears the binding after failure. |
| H-03 | Confirmed: Device ID still reaches the callback as reserved 7-bit address `0x7C`. |
| H-04 | Confirmed: `offlineThreshold`, `OFFLINE` admission gating, and library `recover()` remain mandatory core policy. |
| H-05 | Confirmed: `writeVerify()` still stops after a write error and performs no reconciliation read. |
| H-06 | Confirmed: callbacks still return general `Status`; callback `IN_PROGRESS` can cause the same staged write chunk to be submitted again. |
| H-07 | Confirmed: `MAX_WRITE_CHUNK` is still documented as total bytes but used as data bytes; transport TX/RX limits are not configured. |
| H-08 | Confirmed: terminal clear/cancel still erases staged offset and no terminal progress snapshot is retained. |
| H-09 | Confirmed: `DeviceVariant::AUTO` remains the default and the TunnelMonitor production ordering code is not established by library code. |
| H-10 | Confirmed: retained HIL still does not cover TunnelMonitor's 5 ms timeout, shared bus, ambiguous-write reconciliation, or full fault matrix. |
| H-11 | Confirmed: cached `Status` still retains the callback-supplied `const char* msg`. |
| H-12 | Confirmed: PlatformIO and its Espressif platform remain broadly resolved rather than exactly pinned by this repository. |

The native suite was re-run on the final HEAD: 126 of 126 tests passed.

## Decision summary

### Use after a focused refactor

The v3 library is worth refactoring. The following are release gates for a
TunnelMonitor integration:

1. Provide zero-I/O configuration. Device absence must not make the library
   unusable for later owner-controlled attempts.
2. Remove the mandatory library-owned `OFFLINE` latch and `recover()` policy
   from the passive core.
3. Provide an explicit terminal transport contract. A callback must represent
   exactly one completed physical transaction and must never cause a staged
   write to be replayed accidentally.
4. Build single-transaction read, write, and verify primitives, then expose a
   small cooperative verified-write job with a provable one-callback-per-poll
   contract.
5. Preserve TunnelMonitor's write-timeout reconciliation: never blindly resend
   an ambiguous write; read it back in a later owner step.
6. Handle Device ID as an explicit special transaction. TunnelMonitor's normal
   backend currently rejects the reserved 7-bit address `0x7C`.
7. Confirm the exact FRAM ordering code on hardware revision 2.0.0 and configure
   that exact variant. Keep the application map fixed at 8 KiB even if the
   physical part is larger.
8. Pin the reviewed result by immutable commit and qualify it in
   TunnelMonitor's exact ESP32-S3 build and on the actual shared I2C bus.

### Do not solve this with configuration tricks

These are not acceptable long-term fixes:

- setting `offlineThreshold` to 255;
- calling `begin()` repeatedly after every failure;
- allowing all reserved I2C addresses through the normal scan/transfer path;
- using synchronous `writeVerify()` in the TunnelMonitor path;
- retrying a timed-out write before readback;
- copying library source into TunnelMonitor;
- exposing MB85RC types in TunnelMonitor public contract headers.

## TunnelMonitor requirements

The relevant requirements are already implemented and tested in TunnelMonitor.
The library must fit them; the firmware should not weaken them to fit the
library.

| Requirement | Current authority/evidence | Consequence for MB85RC |
| --- | --- | --- |
| One I2C owner | `docs/guidelines/ownership.md:42-48,254-280` | `I2cTask` owns the bus, scheduling, deadlines, retries, recovery, and device health. The library is an owner-private chip helper only. |
| Passive dependency | `docs/guidelines/dependency_policy.md:27-31` | Library configuration must not perform I2C, provision hardware, recover the bus, or latch a separate offline policy. |
| Missing FRAM is survivable | `docs/guidelines/rtc_fram.md:17-30` | Missing/corrupt FRAM must not fail the whole I2C owner or cause a boot loop. Later presence attempts must remain possible. |
| Fixed logical map | `include/TunnelMonitor/contracts/RtcFram.h:13-30` | Usable range remains `0x0000..0x1FFF` (8192 bytes), regardless of larger physical capacity. |
| Fixed command size | `include/TunnelMonitor/contracts/RtcFram.h:31` | A public FRAM command carries 1 to 124 data bytes. Do not expand this ABI because the library can carry more. |
| Fixed timing | `include/TunnelMonitor/contracts/RtcFram.h:32-35` | Each physical FRAM transfer receives at most 5 ms. Read operation deadline is 50 ms; write operation deadline is 750 ms. Owner deadlines use 64-bit time and are not renewed by internal steps. |
| Stepped owner work | `src/i2c/I2cTask.cpp:1903-2017` | Current write and later verify are separate backend calls in separate owner polls. One library call must not hide several physical transfers. |
| Ambiguous write handling | `src/i2c/I2cTask.cpp:1940-2017` | A recoverable write error is followed by readback. Matching data is reported as verified after timeout. The write is not sent twice. |
| Application-owned durability | `docs/guidelines/rtc_fram.md:368-545` | A/B records, CRC, generation selection, save coalescing, record maps, and sample-sequence reservation stay in TunnelMonitor. |
| Private adapter boundary | `docs/guidelines/ownership.md:271-280` | MB85RC types may not leak into public commands, results, CLI, web, or service contracts. |

## What already fits

These v3 properties should be preserved:

- The core uses injected callbacks and does not include Arduino, ESP-IDF, or
  `Wire` in `include/` or `src/`.
- Normal core paths use fixed stack buffers. No heap allocation or deliberate
  delay was found in the core.
- The driver does not own bus initialization or locks.
- Copy and move are deleted (`include/MB85RC/MB85RC.h:131-148`).
- `DeviceVariant::MB85RC64TA` describes 8192 bytes, last address `0x1FFF`, and a
  two-byte memory address (`include/MB85RC/CommandTable.h:88-107,189-196`).
- v3 validates ranges before transfer and supports explicit variants.
- `WriteResult` correctly says transport acceptance is not proof of persistence
  when WP is high (`include/MB85RC/MB85RC.h:98-113`).
- Readback helpers recognize that an ACKed write may not persist.
- The staged request API performs no I/O when a request is queued and can limit
  `pollTransfer()` to one chunk.
- Public copyable data does not contain framework driver objects.
- Native tests cover variants, addressing, partial synchronous write results,
  WP behavior in the fake, staged transfers, and lifecycle states.

The datasheets also support several current implementation choices:

- FRAM does not need EEPROM-style write-cycle delays or ACK polling.
- WP high can leave reads working and can allow a write transaction to be ACKed
  while preventing the memory change. Readback is therefore required where
  persistence matters.
- A random read uses an address write followed by a repeated START and read.
- Application range checking should prevent accidental sequential wrap at the
  end of the physical array.

## Hard findings

### H-01: integration must use the v3 lineage, not the old audit branch

Priority: repository baseline; local checkout corrected on 2026-07-18

When the original audit was written, the active MB85RC checkout was `bebad8a`,
a v2-era branch. The checkout is now `main@fe38e72`. The current v3 tag is
`f0294b0`, two commits behind that tip. v3 materially changes the suitability
picture: it adds staged transfers, partial synchronous-write results, explicit
WP documentation/tests, copy/move deletion, more variants, and ESP-IDF CI
configuration.

TunnelMonitor currently records only historical `v2.0.0` evidence and has no
active MB85RC dependency (`docs/guidelines/dependency_policy.md:17-20,102-116`).

Required action and current state:

- Resolved locally: base refactoring on `main@fe38e72`, not the v2 audit branch.
- Publish a new reviewed tag after the refactor.
- Pin the immutable commit in TunnelMonitor. Do not pin a branch or an old local
  checkout.

### H-02: `begin()` performs bus traffic and loses configuration on absence

Priority: architecture blocker

`begin()` is documented to verify presence (`include/MB85RC/MB85RC.h:154-160`).
It performs either a memory read or Device ID transaction before setting
`_initialized` (`src/MB85RC.cpp:245-281`). On failure it clears configuration,
variant, identity, health, and transfer state (`src/MB85RC.cpp:183-202`).

This does not fit a system where the FRAM may be absent at boot but the I2C owner
and other devices must continue. It also hides I2C work inside lifecycle setup.

Required refactor:

- Add a zero-I/O `bind()` or `configure()` operation.
- Let an explicit fixed variant become available for address encoding and range
  checks immediately after binding.
- Make presence and identity checks explicit I2C operations with their original
  typed transport result preserved.
- Keep a compatibility `begin()` facade only if existing standalone users need
  it. It should compose the passive calls, not define the core lifecycle.

### H-03: mandatory Device ID uses an address TunnelMonitor rejects

Priority: integration blocker

For Device-ID-capable parts, the library sends the reserved `0xF8/0xF9`
transaction through the normal callback as 7-bit address `0x7C`
(`src/MB85RC.cpp:1415-1439`). TunnelMonitor's backend accepts normal 7-bit
device transfers only from `0x03` through `0x77`
(`include/TunnelMonitor/contracts/FieldBus.h:84-94` and
`src/i2c/IdfI2cBackend.cpp:150-157`). Current `begin()` therefore cannot succeed
through the existing backend for an MB85RC64TA or MB85RC256V.

Required refactor/design:

- Treat Device ID as an explicit special operation, for example
  `I2cSpecialOp::READ_DEVICE_ID`, rather than pretending it is a normal scanned
  device address.
- Add only the exact owner-private backend support required for that protocol.
- Do not broaden normal scans or general device transfers above `0x77`.
- A normal ACK at `0x50` proves presence, not product identity.

### H-04: the core owns a competing offline and recovery policy

Priority: architecture blocker

`Config::offlineThreshold` is mandatory (`include/MB85RC/Config.h:144-145`).
Tracked failures move the driver to `OFFLINE`
(`src/MB85RC.cpp:1652-1719`). Normal and staged operations then return `BUSY`
without touching the transport (`src/MB85RC.cpp:1111-1115,1137-1140,1185-1209`).
Only library `recover()` is allowed to bypass the latch.

TunnelMonitor already owns exactly this policy in `I2cTask`. A second latch can
refuse an owner-directed presence check or post-timeout verify, even after the
owner has recovered the physical bus.

Required refactor:

- Make the core transport-only. A previous failure must not block a later
  owner-requested transaction.
- Remove `recover()` and mandatory `OFFLINE` gating from the passive core.
- If standalone users still need managed health, place it in a separate
  optional facade. Do not put a mode check through every core I/O path.
- Cache-only counters are acceptable as observation, provided they do not
  change admission or recovery behavior.

### H-05: `writeVerify()` violates TunnelMonitor's ambiguous-write rule

Priority: durability blocker

`writeVerify()` returns immediately when the write phase reports an error and
does not perform readback (`include/MB85RC/MB85RC.h:514-529` and
`src/MB85RC.cpp:905-930`). The tests explicitly preserve this behavior.

TunnelMonitor does the opposite for recoverable write failures: it does not
repeat the write, and it reads the same range later. A match proves the write
committed despite the transport error; a mismatch or failed verify remains a
failure. This behavior is in `src/i2c/I2cTask.cpp:1940-2017`.

Required refactor:

- Add public single-transaction `readOnce`, `writeOnce`, and `verifyOnce`
  primitives, or equivalent internal/public primitives with the same exact
  contract.
- Each primitive performs at most one injected callback.
- Return the original write transport status without converting it into a
  second write attempt.
- Add a cooperative `requestVerifiedWrite()` job on top of those primitives.
  One poll with budget one performs at most one transport callback.
- After an ambiguous write result, retain the address and expected buffer, mark
  the phase as waiting for owner recovery, and never resubmit the write.
- Let the owner signal that its recovery step is complete, then perform only the
  readback phase in a later poll.
- Preserve both the original write result and the verify result so the terminal
  result can report `VerifiedAfterTransportError` truthfully.

The cooperative job owns only the FRAM operation phase. `I2cTask` still owns
admission, deadlines, bus recovery, and the decision to resume verification.
This removes the duplicate FRAM protocol state from the firmware without
creating a second resource owner.

TunnelMonitor must not call the current synchronous `writeVerify()`.

### H-06: the transport callback contract can replay a staged write

Priority: correctness blocker for platform use

The transport callbacks return the general `Status` type
(`include/MB85RC/Config.h:30-57`). That type includes `Err::IN_PROGRESS`. If a
write callback returns `IN_PROGRESS`, `_pollTransferInstruction()` does not
advance the offset (`src/MB85RC.cpp:1260-1269`) and `pollTransfer()` returns
early (`src/MB85RC.cpp:1030-1035`). The next poll submits the same write again.

The callback documentation also does not state that `OK` means the complete TX
and RX lengths were transferred. It does not explicitly require repeated START
for the write-read callback.

Required refactor:

- Prefer a small transport-specific terminal result enum instead of returning
  the complete driver `Status` domain.
- If `Status` is retained, reject `IN_PROGRESS` and other non-terminal driver
  states from callbacks at runtime.
- Specify and test all of these rules:
  - one callback represents one physical I2C transaction;
  - the callback is synchronous and returns a terminal result;
  - `OK` means all requested bytes were transferred;
  - short reads/writes are errors;
  - write-read uses a repeated START with no STOP between address write and read;
  - no hidden retry or bus recovery occurs inside the callback;
  - RX contents are unspecified after a failed transaction;
  - a failed write can have unknown physical commit state;
  - callback context remains valid for the bound driver's lifetime;
  - recursive entry into the same driver is forbidden.

### H-07: transaction-size terminology is wrong and capacity is fixed

Priority: required platform refactor

`MAX_WRITE_CHUNK` is documented as the total address-plus-data size and has the
value 126 (`include/MB85RC/CommandTable.h:247-249`). The implementation uses it
as 126 data bytes, then prepends one or two address bytes
(`src/MB85RC.cpp:1362-1385`). For a two-byte-address part, the actual callback TX
length is 128 bytes.

This happens to fit the tested ESP32 transport. It is not a truthful portable
contract. `Config` also has no TX/RX capacity supplied by the transport.

Required refactor:

- Rename the value to `MAX_WRITE_DATA_BYTES` if 126 is the intended data limit,
  or make the existing value mean the documented total transaction size.
- Add fixed `maxTxBytes` and `maxRxBytes` transport capabilities, or an equally
  explicit compile-time transport contract.
- Compute usable write data from TX capacity minus the active variant's encoded
  address length.
- Validate impossible capacities during zero-I/O binding.
- Keep TunnelMonitor at 124 data bytes. Its actual write transaction remains
  126 bytes including the two-byte address.

### H-08: staged transfer progress is erased at failure/cancel

Priority: platform contract gap; not an immediate 124-byte TunnelMonitor bug

The staged job tracks `offset` privately
(`include/MB85RC/MB85RC.h:684-693`). Public observation returns only the status.
Completion, failure, and cancellation clear the job
(`src/MB85RC.cpp:1192-1195`). The accepted prefix is therefore unavailable after
a later-chunk failure, even though the synchronous `writeDetailed()` reports it.

Required for a general platform API:

- Preserve a cache-only terminal `TransferResult` after the active job clears.
- Include operation kind, start address, bytes requested, bytes completed or
  accepted, failed chunk offset/length, terminal status, and verification
  disposition.
- Do not expose retained caller buffer pointers in the snapshot.

TunnelMonitor commands fit one chunk, so this finding does not justify adding a
large async framework to the firmware. Single-transaction primitives are the
simpler TunnelMonitor integration.

### H-09: the exact TunnelMonitor FRAM part is not frozen

Priority: hardware admission prerequisite

TunnelMonitor freezes address `0x50` and an 8 KiB logical map, but the
architecture files name only the MB85RC family. An 8 KiB map strongly matches
MB85RC64TA; it does not prove that this is the populated ordering code. A larger
part can use only its first 8 KiB.

The v3 default is `DeviceVariant::AUTO`
(`include/MB85RC/Config.h:135-145`). That is useful for tools, but it accepts any
supported family part and is weaker than a fixed-BOM production check.

Required action:

- Confirm the hardware revision 2.0.0 BOM and read the Device ID on a production
  representative board.
- Bind the exact production variant.
- Validate manufacturer and product ID explicitly before declaring the FRAM
  ready, when the selected part supports Device ID.
- Keep all application range checks limited to `0x1FFF`, even if identity shows
  a larger physical capacity.

### H-10: retained HIL does not qualify the TunnelMonitor operating point

Priority: release blocker after integration

The v3 retained HIL covers one ESP32-S3 plus MB85RC64TA fixture at 400 kHz with a
50 ms Wire timeout (`docs/reports/HIL_SUMMARY.md:8-20`). A two-minute strict run
passed. A 20-hour strict run failed its strict gate because it contained 69
`UNKNOWN` host serial windows, although it recorded no target failure or reset
(`docs/reports/HIL_SUMMARY.md:22-46`).

The retained evidence does not include WP-high, controlled brownout/power-cycle,
wrong address or missing device, address straps, or shared-bus fault HIL
(`docs/reports/HIL_SUMMARY.md:48-56`). TunnelMonitor uses a 5 ms transfer timeout
and shares the bus with RTC, environment, power, and display devices.

Required action:

- Qualify the actual production FRAM on the actual TunnelMonitor ESP32-S3 board.
- Use 400 kHz, 124-byte data chunks, and the production 5 ms transfer timeout.
- Exercise write, later readback, ambiguous write reconciliation, shared-bus
  load, forced settings save, reboot/load, and A/B recovery.
- Exercise WP high if the fixture can control or safely strap WP.
- Exercise missing/wrong-address behavior if the fixture permits it.
- Retain a soak with an unambiguous runner result.

This does not require destructive qualification of every supported MB85RC part.
The actual TunnelMonitor BOM part is the release requirement; broader family
coverage is a library maintenance decision.

### H-11: cached `Status` can retain a caller-owned message pointer

Priority: should refactor for platform safety

`Status` contains `const char* msg` and documents it as static
(`include/MB85RC/Status.h:43-70`). Transport callbacks also return `Status`, and
tracked errors are cached in `SettingsSnapshot::lastError`
(`include/MB85RC/MB85RC.h:55-72`). The core cannot enforce that an adapter's
message pointer is static. A stack or temporary transport message would leave a
dangling pointer in diagnostics.

Recommended refactor:

- Store only `Err`/transport code and numeric detail in durable snapshots.
- Generate text through `toString()` functions that return library-owned static
  strings.
- Do not cache an arbitrary callback-provided pointer.

### H-12: the library build is not fully reproducible by itself

Priority: release hygiene

The v3 `platformio.ini` uses unpinned `platform = espressif32`. CI installs the
latest PlatformIO package. The local successful Arduino builds resolved
Espressif platform `54.3.20`, Arduino `3.2.0`, and ESP-IDF libraries `5.4.0`, but
that resolution is not frozen by the library repository.

The repository now contains a pure ESP-IDF CI job using
`espressif/idf:release-v6.0`, while `idf_component.yml` declares
`idf >=6.0.1`. A local `idf.py` build was not possible because `idf.py` is not
installed in this environment.

Required before the integration pin:

- Run the library inside TunnelMonitor's exact pinned pioarduino
  `54.03.20` environment and its `tunnelmonitor_s3_hw200` board profile.
- Pin at least one known library CI toolchain so future changes have a stable
  reference build.
- Keep the broader compatibility job if desired, but do not use it as the only
  build gate.

## Recommended refactor shape

Keep the core small. It does not need a service registry, task, queue, heap
container, logger, or generic plugin system.

### 1. Passive binding

Suggested responsibility:

```cpp
Status bind(const Config& config, DeviceVariant expectedVariant);
void unbind();
```

`bind()` validates callbacks, address, timeout, capacities, and variant metadata
without I2C. It leaves the instance usable even when the physical device is
absent. Identity and presence are separate operations.

### 2. Terminal one-transaction primitives

Suggested responsibility:

```cpp
ReadResult readOnce(uint32_t address, uint8_t* data, size_t length);
WriteAttemptResult writeOnce(uint32_t address,
                             const uint8_t* data,
                             size_t length);
VerifyOnceResult verifyOnce(uint32_t address,
                            const uint8_t* expected,
                            size_t length);
```

Each operation validates the full range and capacity before I2C and performs
zero or one transport callback. It never retries, recovers, sleeps, or changes
future admission because of a past failure.

Whole-range synchronous helpers may remain for simple applications, but their
names and documentation must make the multi-callback behavior clear.

### 3. Cooperative verified write

Suggested responsibility:

```cpp
Status requestVerifiedWrite(uint32_t address,
                            const uint8_t* data,
                            size_t length);
Status pollTransfer(uint8_t maxCallbacks);
Status resumeVerificationAfterRecovery();
VerifiedWriteResult verifiedWriteResult() const;
```

The names are illustrative. The important contract is:

- the request performs zero I2C;
- one poll with budget one performs zero or one physical callback;
- successful write advances to later readback;
- ambiguous write error advances to an explicit wait-for-owner state;
- resuming after owner recovery can only read and compare; it cannot write;
- absolute operation deadline remains outside the library;
- cancellation and terminal result retain phase, progress, original write
  status, verify status, and write disposition.

### 4. Explicit identity operation

Identity should return a typed value, not select policy implicitly:

```cpp
struct DeviceIdentity {
  uint16_t manufacturerId;
  uint16_t productId;
  uint8_t densityCode;
  DeviceVariant variant;
};
```

For Device-ID-capable parts, use an exact special transport operation for the
reserved address sequence. The owner decides when identity failure changes
device readiness.

### 5. Clear write disposition

A useful result enum is:

```cpp
enum class WriteDisposition : uint8_t {
  NotAttempted,
  AcceptedUnverified,
  Verified,
  VerifiedAfterTransportError,
  Mismatch,
  Indeterminate,
};
```

Do not infer `VerifiedAfterTransportError` inside the write call. It is known
only after a later readback. Preserve the original transport error alongside
the final disposition.

### 6. Typed transport result

A narrow transport result avoids mixing driver lifecycle values with physical
I2C completion:

```cpp
enum class TransportCode : uint8_t {
  Ok,
  NackAddress,
  NackData,
  Timeout,
  BusError,
  Unsupported,
};

enum class CommitState : uint8_t {
  NotStarted,
  Completed,
  Unknown,
};
```

`CommitState::Unknown` is important for a timed-out write. It means “verify
before deciding,” not “retry.” If the backend cannot distinguish states, it
should report the conservative value.

### 7. Variant metadata without string identity

`VariantInfo` stores a name but not its `DeviceVariant`. The core converts the
active entry back to an enum with `strcmp`
(`src/MB85RC.cpp:1574-1596`). Add the enum directly to `VariantInfo` and use the
name only for display. The duplicate legacy capability fields
`highSpeedMode`/`supportsHighSpeedMode` and `sleepMode`/`supportsSleepMode` can
also be collapsed (`include/MB85RC/CommandTable.h:64-80`).

This is a small cleanup that reduces string coupling and duplicated truth.

## Useful public helpers and types

### Needed for the refactor

- `DeviceVariant` in a small framework-neutral public types header.
- `DeviceIdentity` with numeric identity fields and exact variant.
- `TransportCode` and `CommitState`, or an equally narrow terminal transport
  result.
- `TransferLimits { maxTxBytes, maxRxBytes }`.
- `ReadResult`, `WriteAttemptResult`, and `VerifyOnceResult` with byte counts and
  terminal status.
- `TransferResult` for the staged API if staged multi-chunk operations remain.
- A typed mismatch outcome. Do not require callers to combine `Status::OK` with
  `match == false` correctly.

### Nice to have

- `toString(Err)`, `toString(TransportCode)`, and
  `toString(DeviceVariant)` returning static library-owned strings.
- `variantInfo(DeviceVariant)` returning a value or stable const reference.
- `decodeDeviceId(const DeviceIdRaw&)` as a pure tested helper.
- `fitsRange(DeviceVariant, address, length)` as a pure overflow-safe helper.
- `maxWriteDataBytes(DeviceVariant, TransferLimits)`.
- `TransferSnapshot` with phase and progress but no retained data pointers.
- `isAmbiguousWriteFailure(TransportCode)` only if its meaning is defined by
  transport contract. TunnelMonitor may instead keep this policy in `I2cTask`.

These helpers should be fixed-size, allocation-free, and usable in native tests.

## Do not add to the library

The following belong to TunnelMonitor or its I2C owner, not to the MB85RC chip
library:

- TunnelMonitor's FRAM map, settings structs, A/B slots, CRC, generations, or
  sequence reservation;
- bus creation, locks, retry cadence, physical recovery, SCL pulses, or device
  health roles;
- board pins or WP GPIO ownership;
- FreeRTOS tasks, queues, command registries, logging, CLI, web, or raw FRAM
  maintenance commands;
- automatic writes during configuration or identity checks;
- unbounded buffers or dynamic STL containers.

High-speed and sleep support are not needed by TunnelMonitor's 400 kHz FRAM
path. They can remain optional library capabilities, but the TunnelMonitor
adapter should not supply or enter them.

## Required validation after refactor

### Library native tests

- `bind()` performs zero transport callbacks.
- Binding succeeds with the device absent; later identity/read attempts remain
  possible.
- Explicit variant and address validation, including MB85RC64TA at `0x50`.
- Device ID emits the exact reserved-address special operation and preserves
  NACK, timeout, and mismatch separately.
- `readOnce`, `writeOnce`, and `verifyOnce` issue exactly one callback for
  lengths 1 and 124.
- A queued verified write emits its write and readback in different polls.
- An ambiguous write enters the wait-for-owner phase, and resume can emit only
  readback. No path emits the write twice.
- Length 0, length above configured capacity, and out-of-range requests fail
  with zero callbacks.
- The two address bytes are correct at `0x0000`, `0x1FFF`, and nearby
  boundaries.
- `OK` means full transfer; simulated short completion is rejected.
- A transport `IN_PROGRESS` or other invalid non-terminal result is rejected
  and never replays a write.
- WP-high simulation: write may be accepted, verify reports mismatch.
- Timeout before commit, timeout after commit, verify failure, and mismatch
  produce distinct truthful results with no blind write retry.
- Previous failures do not create an independent offline admission latch.
- Terminal staged progress remains observable after error and cancel, if the
  staged API remains.

### TunnelMonitor adapter tests

- MB85RC types appear only in owner-private source/header files.
- Exact `0x50`, 124-byte data ceiling, two-byte address, and 5 ms callback
  timeout.
- One backend callback per normal owner poll.
- The original 64-bit 50/750 ms owner deadline is never renewed.
- Read recovery and its one bounded retry remain owner policy.
- A write is attempted once, then verified in a later poll.
- Timeout after physical commit returns success with
  `kFramWriteFlagVerifiedAfterWriteTimeout`.
- Timeout without commit, WP mismatch, and failed verification return failure.
- Missing FRAM at boot does not fail `I2cTask`, RTC, environment, power, or
  display operation.
- Owner recovery never leaves the library blocked by a stale internal latch.
- NACK, timeout, bus error, backend detail, and measured duration map exactly to
  existing TunnelMonitor result contracts.
- Existing A/B, corrupt-slot, save/reboot/load, and sample-sequence tests remain
  unchanged and pass.

The current v3 native fake callbacks ignore their `timeoutMs` argument. Add an
adapter spy that asserts the exact propagated value; otherwise the production
5 ms contract is not proven by native tests.

### Builds and HIL

- MB85RC native tests.
- TunnelMonitor `native` tests.
- TunnelMonitor `tunnelmonitor_wifi` and relevant HIL build.
- Exact production board, exact FRAM part, 400 kHz, 5 ms transfers.
- Shared-bus traffic, settings save/reboot/load, corrupt slot, timeout ambiguity,
  and retained soak evidence.

Do not claim power-loss, WP, missing-device, or long-soak behavior unless that
case was physically run and retained.

## Documentation issues found in TunnelMonitor

These do not change the chip-library recommendation, but they should be cleaned
up before the adapter acceptance checklist cites them:

- `docs/guidelines/open_questions.md:75-85` still says 48-byte FRAM chunks.
  Current authority, code, and tests use 124 bytes.
- `docs/guidelines/rtc_fram.md:384-393` describes a 404-byte settings payload,
  while `docs/guidelines/settings.md:47-58` and current contract tests describe
  440 bytes.
- `docs/reference/hardware_and_build_facts.md:143` says “timeout 50 ms” without
  separating the 5 ms physical transfer timeout from the 50 ms read operation
  deadline and 750 ms write operation deadline.
- Dependency authority still records MB85RC v2 as legacy/deferred evidence. A
  separate accepted integration scope and exact immutable pin are required
  before firmware code changes.

## Verification performed for this audit

| Check | Result |
| --- | --- |
| MB85RC v2 native tests at the previous audit branch | PASS, 73/73 |
| MB85RC v3 native tests at `fe38e72` | PASS, 126/126; re-run after updating the checkout on 2026-07-18 |
| MB85RC v3 Arduino ESP32-S3 build | PASS; re-run after checkout update; Espressif platform 54.3.20, Arduino 3.2.0 |
| MB85RC v3 Arduino ESP32-S2 build | PASS; re-run after checkout update; Espressif platform 54.3.20, Arduino 3.2.0 |
| Version, metadata, timing, CLI, and native-IDF example guards | PASS; re-run after checkout update |
| Framework/heap/delay scan of v3 core | No Arduino/ESP-IDF/Wire dependency, heap allocation, or deliberate delay found in `include/` and `src/` |
| Local ESP-IDF build | Not run; `idf.py` unavailable |
| TunnelMonitor tests/builds | Not run; no TunnelMonitor code or build behavior changed |
| Physical HIL | Not run; only existing retained reports were reviewed |

## Final recommendation

Refactor MB85RC v3 into a passive chip core and publish a new immutable release.
The minimum useful TunnelMonitor surface is small: zero-I/O binding, explicit
identity, terminal one-transaction primitives, one cooperative verified-write
job, accurate transfer limits, and typed results that preserve ambiguous write
state. `I2cTask` keeps all scheduling, deadlines, retry, recovery, and
device-health policy.

This removes duplicated protocol bytes from TunnelMonitor without creating a
second resource owner. It also gives the MB85RC library a cleaner platform API
for other owner-managed embedded systems.
