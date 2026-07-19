# TunnelMonitor-node suitability re-audit

## MB85RC I2C FRAM library

Date: 2026-07-19

Result: **library-side blockers resolved after follow-up self-audit; downstream
pin/adapter work, exact BOM identity, target build, downstream guideline
consistency, and hardware qualification remain external gates**

This report supersedes the 2026-07-18 audit. Old line numbers and suggested
signatures are historical only. The finding IDs remain stable so review and
integration evidence can be traced.

## Audit basis

| Repository | Revision/state used | Scope |
| --- | --- | --- |
| MB85RC baseline | `7e67d7f1325127afb919c8848a88a395673a3958`, branch `hardening/tunnelmonitor-suitability-reaudit`, initially clean and synchronized | v3-lineage source, API, tests, examples, docs, metadata, CI, and the previous audit |
| MB85RC implementation | `hardening/tunnelmonitor-suitability-reaudit@fc939b3` (full `fc939b35a9cb134af32f1c252edb45913489fb56`), clean and synchronized before this report-only update | Core/API/tests `a8815e4`, first review fix `df6eec8`, v4 docs/CI `18fad0f`, follow-up transport/lifecycle/test fix `47afad1`, and final public-enum/release-ledger fix `fc939b3` |
| TunnelMonitor-node functional authority | `develop@0897f12c1a1369367747d1063936906005391580`, clean | I2C ownership, deadlines, result identity, FRAM map/ABI, recovery, and dependency policy |
| TunnelMonitor-node current branch | `docs/mb85rc-suitability-contract-facts@b708f511964db6c51e949e99c67820476f00f9c7`, clean and synchronized | User-requested revert of `322a7b2`; tree `1e7ae125548addb34dea22a6c58a65d623f642e3` is identical to `develop@0897f12`, so no TunnelMonitor content change remains |

The TunnelMonitor inspection was deliberately limited to:

- `AGENTS.md`;
- `docs/guidelines/ownership.md`;
- `docs/guidelines/dependency_policy.md`;
- `docs/guidelines/rtc_fram.md`;
- relevant FRAM/settings entries in `settings.md`, `open_questions.md`,
  `i2c_peripherals.md`, `target_architecture.md`, and
  `reference/hardware_and_build_facts.md`;
- `include/TunnelMonitor/contracts/RtcFram.h` and `FieldBus.h`;
- FRAM execution/deadline code in `src/i2c/I2cTask.cpp`;
- normal address admission and transfer code in
  `src/i2c/IdfI2cBackend.cpp`;
- exact dependency/toolchain pins in `platformio.ini`.

No TunnelMonitor source, documentation content, dependency selection, or
runtime behavior differs from `develop@0897f12` after the requested revert.

## Current TunnelMonitor integration contract

| Contract | Current authority/evidence | Library/integration consequence |
| --- | --- | --- |
| One I2C owner | `ownership.md`; `I2cTask` worker boundary | `I2cTask` owns the bus, locking, scheduling, retries, recovery, deadlines, and device-health policy. MB85RC is an owner-private chip helper. |
| Passive dependency | `dependency_policy.md` | Binding performs zero I2C and never provisions hardware, recovers the bus, or creates an admission latch. |
| Missing FRAM is survivable | `rtc_fram.md` | A missing device cannot invalidate a valid binding or stop unrelated RTC/ENV/power/display work. Later owner attempts remain possible. |
| Fixed logical map | `RtcFram.h`: `kFramUsableBytes=8192`, `kFramLastAddress=0x1FFF` | TunnelMonitor retains its 8 KiB logical range even if the physical part is larger. |
| Fixed command data | `RtcFram.h`: `kFramChunkBytes=124` | Configure at least TX 126 bytes for a two-byte address plus 124 data bytes, and RX 124 bytes. Do not widen the public ABI. |
| Fixed timing | `RtcFram.h`: 5 ms transfer, 50 ms read operation, 750 ms write operation | Set the library callback timeout to 5 ms. The owner retains the original 64-bit absolute operation deadline and calls library timeout between polls; it never renews the deadline. |
| One normal transfer per owner poll | `ownership.md`; `rtc_fram.md`; `I2cTask.cpp` | Use exact one-transaction primitives or `pollTransfer(..., 1)`. A callback represents one complete physical attempt. |
| Ambiguous write handling | `I2cTask.cpp` write then later verify path | Never resend an indeterminate write. Pause, let the owner recover, then authorize readback only. |
| Exact result identity | `FieldBus.h`; `ownership.md` | TunnelMonitor externally matches `(requestId, submissionToken, device, operation)`. Its private adapter maps that identity to a nonzero library request ID and consumes one terminal result exactly once. |
| Private dependency boundary | `ownership.md`; `dependency_policy.md` | No MB85RC type may escape into TunnelMonitor public commands, results, CLI, web, or service contracts. |
| Dependency state | `dependency_policy.md`; `platformio.ini` | MB85RC remains deferred and unselected. Integration requires a reviewed full immutable commit; a branch or copied source is unacceptable. |

## Operation-class contract

The implemented public contract has three explicit scheduling classes. Full
bounds and formulas are maintained in the README.

### 1. Steady-state owner operations

`readOnce()`, `writeOnce()`, and `verifyOnce()` perform at most one callback and
therefore occupy transport for at most the configured per-transaction timeout.
They validate range, buffer, and configured capacity before I2C; they do not
retry, wait, recover, allocate, or change later admission.

### 2. Multi-step runtime operations

Request functions perform zero I2C. Caller-supplied request IDs qualify active
work and retained results. Each poll has a caller-selected callback budget,
clamped to eight; budget one means at most one physical transaction. The owner
keeps its absolute deadline and can terminalize by request ID as cancelled or
timed out between callbacks. Partial accepted prefixes and indeterminate effects
remain visible. Results retain no caller-buffer pointers, use library-owned
static `Status` text, and are consumed exactly once.

Verified write uses one coherent phase model. A successful write advances to
readback. An indeterminate write enters a zero-I2C reconciliation wait. Only an
explicit same-request resume permits later readback; no path replays that write.
An ambiguous Sleep entry/wake effect enters `SleepState::UNKNOWN`; normal I2C is
blocked until an explicit wake succeeds and its bounded recovery deadline
elapses.

### 3. Rare or maintenance operations

Whole-range synchronous helpers remain available for startup, diagnostics,
commissioning, or maintenance windows. For length `N`, read/verify use at most
`ceil(N/R)` callbacks, write uses `ceil(N/W)`, fill uses
`ceil(N/min(W,64))`, write-verify and fill-verify add the corresponding read
bound, and diagnostic current-address buffer read uses exactly `N` callbacks.
Each bound's worst-case transport occupancy is its callback count times `T`.
There is no hidden program delay or retry. Device ID, High-speed special
operations, Sleep entry, and wake stimulus are individually bounded. FRAM has
no EEPROM program cycle, erase procedure, or ACK-poll requirement. Maintenance
code should verify and skip a rewrite when the desired bytes already match.

These helpers are not appropriate inside a normal owner poll when their derived
multi-callback worst case exceeds that poll's budget. Endurance, application
journaling, and repair policy remain outside the chip driver.

## Finding disposition

| ID | Severity | Current disposition | Status |
| --- | --- | --- | --- |
| H-01 | Repository/integration gate | Work is based on current v3 lineage, not the obsolete audit branch. The complete reviewed library candidate is immutable at `fc939b3`; this report-only successor changes no package behavior. | Library lineage resolved; downstream pin pending |
| H-02 | Architecture blocker | Added zero-I/O `bind()`. Fixed variants are usable for validation/encoding immediately. Compatibility `begin()` composes bind plus one explicit check and retains binding after I/O/identity failure. | Resolved in library |
| H-03 | Integration blocker | Device ID is `I2cSpecialOp::READ_DEVICE_ID`; normal callbacks and scans need not admit reserved address `0x7C`. Examples implement the exact special path. | Resolved in library; private TunnelMonitor adapter pending |
| H-04 | Architecture blocker | Health is observation only. Diagnostic OFFLINE never gates an owner-requested transaction. `recover()` remains a compatibility presence/identity check, not bus recovery authority. | Resolved in library |
| H-05 | Durability blocker | Added `readOnce`, `writeOnce`, `verifyOnce`, and cooperative verified write. An indeterminate write waits for explicit owner recovery and resumes with readback only. | Resolved in library |
| H-06 | Correctness blocker | Callbacks now return terminal `TransportResult`; queued/in-progress outcomes are unrepresentable. Counts distinguish address-prefix progress from memory data, malformed NACK/full-acceptance claims are rejected, and example adapters preserve ambiguity. | Resolved in library |
| H-07 | Platform blocker | `MAX_WRITE_DATA_BYTES` names data capacity truthfully; `Config::maxTxBytes/maxRxBytes` declare transport capacity; active write data subtracts address bytes and impossible bindings fail without I2C. | Resolved in library |
| H-08 | Result-contract gap | Public `TransferResult` retains kind, request ID, state, byte progress, failed chunk, write/verify statuses, commit/mismatch evidence, and no caller pointers until exactly-once take. | Resolved in library |
| H-09 | Hardware admission prerequisite | TunnelMonitor still establishes only family, address `0x50`, and an 8 KiB logical map. No authoritative exact populated ordering code was found. | External BOM/device-ID gate open |
| H-10 | Release qualification | Current-code target HIL has not run at TunnelMonitor's exact operating point. Historical evidence does not close the 5 ms/shared-bus/ambiguity/fault/soak matrix. | External hardware gate open |
| H-11 | Platform safety | Transport results carry typed/numeric data only. Durable diagnostics store library-mapped `Status` values with library-owned static text, never a callback-owned message pointer. | Resolved in library |
| H-12 | Release hygiene | CI pins PlatformIO 6.1.18 and pioarduino platform-espressif32 54.03.20 in one stable ESP32-S3 reference while retaining broader ESP32-S2/S3 compatibility jobs. This is a stable reference lane, not a hermetic build environment. | Library CI resolved; exact TunnelMonitor build still pending |

## Per-finding evidence and resolution

### H-01 — current lineage and immutable pin

`7e67d7f` descends from audited v3 `main@fe38e72`; only the audit itself was
added before this implementation. The obsolete v2 checkout is not an
integration candidate. Version metadata advances to 4.0.0 because callback and
configuration contracts are breaking. Core/API/tests are pushed as `a8815e4`,
the first review correction as `df6eec8`, v4 documentation/CI as `18fad0f`,
the follow-up transport/lifecycle/test fix as `47afad1`, and the final
public-enum/release-ledger fix as `fc939b3`. That final code candidate contains
source, API, tests, examples, public docs, and CI. A release tag does not exist
yet and is not claimed.

### H-02 — passive lifecycle

`MB85RC::bind(const Config&)` validates callbacks, timeout, capacities, address,
and variant without invoking transport. Explicit fixed variants expose their
capacity immediately. AUTO requires a later explicit identity operation before
memory use. `begin()` remains source-compatible in purpose but retains the valid
binding after absence or identity failure, allowing later owner attempts.

### H-03 — reserved Device ID transport

At TunnelMonitor `0897f12`, `IdfI2cBackend::transfer()` rejects addresses above
`0x77`; that normal policy remains correct. The library now requests the F8/F9
Device ID protocol only through `READ_DEVICE_ID`, carrying the active device
address and three-byte destination in a special envelope. Arduino and native
IDF examples keep `0x7C` inside their special adapter implementation. A future
TunnelMonitor private adapter needs the same exact operation and must not widen
normal scan or transfer admission.

### H-04 — health and recovery ownership

`offlineThreshold` is optional diagnostic configuration and defaults to zero.
READY/DEGRADED/OFFLINE describe observed transport history but do not reject
work. The application may issue another presence, identity, read, or verify
after its own bus recovery without calling a library state-reset operation.

### H-05 — ambiguity-safe writes

`writeOnce()` reports `WriteCommit`. The verified-write job records the original
write status separately. `NOT_COMMITTED` can fail terminally; `INDETERMINATE`
enters `WAITING_FOR_RECONCILIATION`, where polls perform zero I2C. A matching
`resumeVerifiedWrite(requestId)` can only advance to read/compare. WP-high ACK
still requires readback because transport acceptance is not persistence.

### H-06 — terminal transport contract

The callback domain is limited to `OK`, address/data NACK, timeout, bus error,
and other I/O error. `OK` supplies exact completed lengths, which the core
checks against the request. Write failures include conservative commit
knowledge. TX progress includes the active one- or two-byte memory-address
prefix, so `NOT_COMMITTED` remains valid while no memory data completed.
`ACCEPTED` on an error is retained only for a full TX followed by timeout,
bus, or generic I/O failure; NACK and invalid-code contradictions normalize to
`INDETERMINATE`. The ESP-IDF example never invents no-commit evidence when its
backend cannot locate the NACKed byte. No callback message pointer is accepted
or retained.

### H-07 — transaction capacity

The core has fixed 128-byte TX/RX buffers. Binding accepts larger declared
transport capabilities and clamps active operations to those core bounds; it
rejects only capacities too small for a valid transaction. A TunnelMonitor
two-byte-address configuration should use `maxTxBytes=126` and
`maxRxBytes=124`, yielding exactly 124 write/read data bytes per callback.

### H-08 — progress and terminal lifetime

Request-qualified cooperative work preserves accepted/read/verified byte
counts, failed chunk, terminal state, and write provenance. Cancellation and
timeout issue no callback and preserve previous effects. A terminal result
blocks replacement work until `takeTransferResult()` consumes it; a second take
returns `NO_RESULT`.

### H-09 — production part identity

Search of current TunnelMonitor architecture, contracts, board pins, dependency
evidence, and source found no authoritative `MB85RC64TA`, `MB85RC256V`, or other
ordering code for hardware revision 2.0.0. The 8 KiB logical map does not prove
physical density. Before integration acceptance, confirm the BOM/schematic and
read manufacturer/product ID on a representative board. Configure the exact
variant and retain the application limit `0x0000..0x1FFF`.

### H-10 — operating-point qualification

Tracked HIL summary evidence covers an MB85RC64TA/ESP32-S3 fixture with 50 ms
Wire timeout. During this re-audit, a 48-hour MB85RC256V v3.0.0 report was also
reviewed from local ignored file
`docs/reports/hil-runner-COM5-MB85RC256V-ESP32S3-20260626-strict-48h.md`
(8820 bytes; SHA-256
`46C31C5D80AA90150868F6DAE4B89F8123EBD3735C54249E6B86EE0DC9DCAEEA`).
It records 29/29 functional PASS and a 316112 PASS / 0 FAIL / 154 UNKNOWN soak,
zero target resets/reconnects, and final READY with zero driver failures. Its
strict gate failed because UNKNOWN was nonzero, and the report/JSON/transcript
are not retained or synchronized repository evidence.

Neither fixture validates this v4 implementation or TunnelMonitor hardware
revision 2.0.0 at 5 ms. Required HIL remains: exact populated part, 400 kHz,
124-byte chunks, shared-bus load, write/later-readback ambiguity, forced
save/reboot/load and A/B recovery, missing/wrong address, WP high where safely
possible, controlled power fault where authorized, and an unambiguous strict
soak result.

### H-11 — durable diagnostic text

Callbacks return `TransportResult` with code, numeric detail, commit knowledge,
and counts. The mapping layer creates library-owned static `Status` text.
Settings/progress snapshots therefore cannot retain a transport adapter's stack
or temporary string.

### H-12 — stable reference build

The stable `esp32s3dev_pinned` environment uses the same pioarduino platform
release as TunnelMonitor (`54.03.20`). CI installs exact PlatformIO `6.1.18`
and builds that environment in addition to the unpinned ESP32-S3 and ESP32-S2
compatibility environments. This supplies a stable reference without reducing
the advertised compatibility jobs. Mutable runner images, action major tags,
the ESP-IDF release container, and package-manager state mean the overall CI is
not hermetic. TunnelMonitor must still add the reviewed MB85RC commit to its own
exact-pinned `tunnelmonitor_wifi` build and run that build before acceptance.

## Follow-up self-audit disposition

Focused independent core, fault-test, and docs/integration reviews were run
again against `18fad0f`; the lead re-read the original prompt, full previous
audit, both repositories' applicable guidelines, and the complete diff. All
confirmed library-side gaps were closed in `47afad1`; a fresh final integration
review then found two release-contract gaps closed in `fc939b3`. The report-only
status drift caused by the requested TunnelMonitor revert is corrected here:

| ID | Confirmed gap | Resolution/evidence | Status |
| --- | --- | --- | --- |
| SA-01 | Failed-write completion counts conflated address-prefix progress with memory-data acceptance. | Mapper now receives the active 1/2-byte prefix length; tests cover both address models, partial claims, contradictory NACK/full acceptance, and invalid codes. | Resolved |
| SA-02 | Ambiguous Sleep entry could leave cached `AWAKE` and admit memory I2C. | Added append-only `SleepState::UNKNOWN`; ambiguous entry/wake failures block normal I2C and explicit wake reconciles through `WAKING`. Fault tests prove no hidden retry. | Resolved |
| SA-03 | `AUTO` without `i2cSpecial` created a bound instance with no public identity-selection path. | `bind()` rejects that configuration without I2C; fixed variants may still omit unused special operations. | Resolved |
| SA-04 | Several staged failure phases and lifecycle boundaries lacked direct regression coverage. | Native suite expanded from 148 to 155 cases, covering staged read/fill/verify failures, accepted-write cancellation/timeout before verify, default health threshold, one-byte capacities, active rebind, malformed special completion, and enum contracts. | Resolved |
| SA-05 | ESP-IDF invalid-response mapping could falsely claim `NOT_COMMITTED`; Wire short-buffer counts described software buffering as physical TX. | IDF preserves indeterminate writes and maps an unlocated NACK to general I/O; Wire reports zero bus progress before `endTransmission()`. Source guard and native adapter test cover the contracts. | Resolved |
| SA-06 | Callback-context lifetime, exact rare-operation formulas, legacy write restrictions, and removed v3 fields were incompletely documented. | Public Doxygen, README, IDF port notes, and CHANGELOG now state the exact ownership, latency, ambiguity, migration, and removal contracts. | Resolved |
| SA-07 | The requested TunnelMonitor revert made this report's downstream-doc status stale. | Report now records `b708f51`, no net TunnelMonitor content changes, and the three restored guideline contradictions as open downstream documentation issues. | Resolved in this report; downstream docs remain open by user direction |
| SA-08 | Adding `READ_DEVICE_ID` at value zero had renumbered four existing public `I2cSpecialOp` values despite the append-only enum contract. | `fc939b3` restores the v3 values 0 through 3 and appends `READ_DEVICE_ID = 4`; compile-time assertions lock all five values. The native suite and all three Arduino builds passed afterward. | Resolved |
| SA-09 | Changelog comparison links still treated v3.0.0 as the newest release. | `fc939b3` advances `[Unreleased]` to compare from v4.0.0 and adds the v3.0.0-to-v4.0.0 release comparison. | Resolved |

## Rejected approaches

- Do not reintroduce an OFFLINE admission latch or automatic core recovery.
- Do not map a queued/in-progress backend result into a completed callback.
- Do not widen normal I2C scans or normal transfer admission to `0x7C`.
- Do not retry an indeterminate write before readback reconciliation.
- Do not infer persistence from ACK when hardware WP may be high.
- Do not expose MB85RC types in TunnelMonitor contracts.
- Do not pin a branch, copy source into firmware, or treat a tag label as a full
  immutable dependency pin.
- Do not infer the production ordering code from an 8 KiB logical map.
- Do not claim current-code or target-board HIL from historical generic-fixture
  results.

## TunnelMonitor documentation status after requested revert

The user-requested `b708f51` reverts the earlier docs-only `322a7b2`. Its tree
is identical to `develop@0897f12`, so this task leaves no TunnelMonitor content
changes. Consequently, three downstream guideline conflicts are open:

- `open_questions.md` says 48-byte FRAM chunks while the executable contract
  uses `kFramChunkBytes = 124`;
- `rtc_fram.md` says `SettingsRecordPayload` is 404 bytes while the executable
  constant, static assertion, and native contract test require 440 bytes;
- `reference/hardware_and_build_facts.md` describes 50 ms as the I2C timeout
  without distinguishing the 5 ms physical-transfer timeout from the original
  50 ms read and 750 ms write operation deadlines.

These are downstream documentation inconsistencies, not MB85RC core blockers.
They remain intentionally unfixed here by the user's explicit revert request.

## Verification ledger

| Check | Result |
| --- | --- |
| Initial MB85RC status/revision/remotes/tags | Clean hardening branch at `7e67d7f`; recorded before edits |
| Initial version/metadata/timing/CLI/native-IDF guards | PASS: `generate_version.py check`, metadata, timing, CLI, and IDF example guards |
| Stable PlatformIO environment parse | PASS: `pio project config --json-output` resolves `esp32s3dev_pinned` to pioarduino 54.03.20 |
| TunnelMonitor revert proof | Clean synchronized `docs/mb85rc-suitability-contract-facts@b708f51`; tree `1e7ae125548addb34dea22a6c58a65d623f642e3` is identical to `develop@0897f12`, with no net content changes |
| Final native suite | PASS: `pio test -e native`, 155/155 tests in 2.321 seconds |
| Final Arduino ESP32-S3 pinned / broad S3 / S2 builds | PASS: PlatformIO 6.1.18; all three environments built successfully against pioarduino 54.03.20 |
| Final documentation/metadata/contract guards | PASS: generated version, release metadata, core timing, CLI parity, native-IDF boundary including the conservative IDF mapper source guard, and warning-free Doxygen |
| Focused independent reviews | Core, fault-test, docs/integration, and final-integration reviewers identified SA-01 through SA-09; every library-side item was corrected in `47afad1` or `fc939b3`, and the native suite and all three Arduino builds passed again after the final fix |
| Package validation | PASS: `pio pkg pack . --output .pio/MB85RC-4.0.0-audit.tar.gz` produced an archive, which was then removed |
| Local ESP-IDF build | NOT RUN: `idf.py` is unavailable locally; CI remains configured for ESP32-S2/S3 |
| Physical HIL on current code | NOT RUN |
| TunnelMonitor native/firmware build with MB85RC pin | NOT RUN; dependency is intentionally not selected yet |

## Remaining acceptance gates

The final source/tests/examples candidate at `fc939b3` passes the native suite,
guards, Arduino builds, documentation generation, package validation, and
focused independent reviews recorded above. All confirmed follow-up findings
were corrected and retested. This report-only successor changes no library or
example behavior.

After that, TunnelMonitor acceptance still requires genuinely external work:

1. confirm hardware revision 2.0.0's exact populated FRAM ordering code;
2. review and pin the final full MB85RC commit behind an owner-private adapter;
3. map `READ_DEVICE_ID` through an exact special backend path;
4. prove `0x50`, logical `0x1FFF`, TX 126/RX 124, 5 ms callbacks, original
   50/750 ms deadlines, one callback per normal poll, and exact result identity;
5. map absolute-deadline expiry and cancellation to `timeoutTransfer` and
   `cancelTransfer` using the exact request identity, consume each completion
   once, and route indeterminate writes through owner recovery plus readback
   reconciliation without blind replay;
6. separately correct or explicitly accept the three restored downstream
   guideline contradictions listed above;
7. run TunnelMonitor native and exact `tunnelmonitor_wifi`/relevant HIL builds;
8. complete the physical fault and soak matrix above.

Until those gates are met, TunnelMonitor's existing direct FRAM path remains
the selected production implementation. The library is general-purpose and
ready for that later adapter; it does not embed TunnelMonitor policy.
