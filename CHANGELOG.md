# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

No unreleased changes.

## [4.0.0] - 2026-07-22

### Added

- Zero-I/O `bind()` lifecycle for passive external-owner integrations.
- Terminal transport types (`TransportResult`, `TransportCode`, and
  `WriteCommit`) with exact completed lengths and conservative physical-effect
  reporting.
- Explicit `I2cSpecialOp::READ_DEVICE_ID` routing for the reserved F8/F9
  protocol without weakening normal 7-bit address policy.
- Pure `decodeDeviceId()` and exact decoded `DeviceId::variant` identity.
- Configured TX/RX capabilities and active-variant
  `maxWriteDataBytes()`/`maxReadDataBytes()` limits.
- Exact one-transaction `readOnce()`, `writeOnce()`, and `verifyOnce()` APIs.
- Request-qualified cooperative jobs whose results retain no caller-buffer
  pointers, with exactly-once terminal consumption, cancellation,
  owner-directed timeout, and write/readback reconciliation after an
  indeterminate write.
- Stable CI reference using PlatformIO 6.1.18 and pioarduino Espressif platform
  54.03.20 while retaining broader ESP32-S2/S3 compatibility builds.

### Changed

- Breaking: injected normal and special I2C callbacks return terminal
  `TransportResult` instead of general driver `Status`.
- `begin()` now composes passive binding with one compatibility presence or
  identity check and retains the valid binding after I/O/identity failure.
- Invalid `bind()` requests leave an existing valid binding unchanged; declared
  transport capabilities may exceed the fixed core buffers and are clamped by
  each operation.
- Driver health is observational only. DEGRADED/OFFLINE no longer suppresses
  owner-requested transport or claims bus-recovery authority.
- Staged terminal progress and ambiguous write effects remain observable until
  explicitly consumed.
- Ambiguous Sleep-entry and wake failures now enter `SleepState::UNKNOWN`, which
  blocks normal access until an explicit successful wake is allowed to recover.
- Examples use typed terminal callbacks, explicit Device ID special transport,
  passive binding, and retained staged-result consumption.
- Public documentation distinguishes steady-state, multi-step runtime, and
  rare/maintenance operation bounds and scheduling suitability.
- Consolidated maintained documentation around durable API, transport, and
  qualification contracts; removed the obsolete product-specific report,
  duplicate README release summaries, and transient audit/status narration.
- Made Doxygen fail on undocumented public API, missing parameter/return
  contracts, and documentation warnings; documented all public headers and
  excluded internal agent instructions from generated docs.
- Aligned README, native ESP-IDF adapter guidance, contribution validation,
  release checks, documentation indexes, and the security policy with the v4
  transport and lifecycle contracts.

### Fixed

- Restored C++11 Arduino build compatibility for `TransportResult` value
  construction while preserving its terminal-result defaults.
- Made strict Doxygen generation portable without Graphviz and removed the
  ambiguous second `README.md` main-page candidate.
- Prevented a non-terminal callback status from replaying a staged write.
- Corrected write-chunk terminology so configured TX capacity includes memory
  address bytes and data capacity does not.
- Removed callback-owned message pointers from durable diagnostic state.
- Preserved partial progress and request provenance across failure,
  cancellation, and timeout.
- Counted a full `WriteCommit::ACCEPTED` error outcome in accepted-prefix
  progress while retaining the transport error and failed-chunk evidence.
- Preserved indeterminate failed-write evidence through cancel, timeout, and
  `end()`, and require a matching manufacturer before reporting an exact
  decoded variant.
- Preserved proven `NOT_COMMITTED` results when only a one- or two-byte memory
  address prefix completed, while normalizing contradictory NACK/full-acceptance
  claims conservatively.
- Rejected unusable `AUTO` bindings that omit the required Device-ID special
  transport.
- Kept ESP-IDF invalid-response writes indeterminate when the backend cannot
  identify the NACKed byte, and corrected Wire buffer-short counts to report
  zero physical TX progress before `endTransmission()`.
- Preserved the v3 numeric values of existing `I2cSpecialOp` members and
  appended `READ_DEVICE_ID` without renumbering the public enum.

### Removed

- Breaking: removed redundant public `VariantInfo::highSpeedMode` and
  `VariantInfo::sleepMode` fields; use `supportsHighSpeedMode` and
  `supportsSleepMode` capability fields instead.

## [3.0.0] - 2026-06-25

### Added

- `Status::is(Err)` and explicit `Status` bool conversion convenience helpers
  while preserving existing `ok()` and `inProgress()` calls.
- Native tests for write/fill verify behavior when a later chunk times out
  after an earlier prefix may already have been accepted by the transport.
- Native all-variant exact-end and cross-end tests for synchronous bulk read,
  write, fill, verify, write-verify, and fill-verify helpers.
- Poll-chunked transfer API:
  `requestRead()`, `requestWrite()`, `requestFill()`, `requestVerify()`,
  `pollTransfer()`, `isTransferBusy()`, `getTransferStatus()`, and
  `cancelTransfer()`.
- Machine-readable `BusyDetail` values for `Err::BUSY` cases through
  `Status::detail`.
- Public timeout contract constants:
  `MIN_I2C_TIMEOUT_MS`, `DEFAULT_I2C_TIMEOUT_MS`, and
  `MAX_I2C_TIMEOUT_MS`.
- Public staged-transfer limit constants:
  `cmd::MAX_FILL_CHUNK` and `cmd::MAX_TRANSFER_INSTRUCTIONS_PER_POLL`, matching
  the existing `cmd::MAX_READ_CHUNK` and `cmd::MAX_WRITE_CHUNK` limits.
- Native transfer-budget tests for `maxInstructions` 1, 2, high-budget clamp,
  exact-end/preflight rejection, active-transfer busy handling, verify
  mismatch, and timeout-after-possible-write readback behavior.
- Native contract tests for enum numeric values, bounded I2C timeout config,
  variant base-address validation, health-counter wrap behavior, request-time
  staged-transfer preflight, and public `readDeviceIdRaw()` health tracking.
- Simplified `docs/` into maintained entry points plus `reference-pdfs/`,
  merging extracted device notes and ESP-IDF implementation notes while removing
  historical audit/extraction leftovers.
- Variant-gated High-speed and Sleep APIs:
  `supportsHighSpeedMode()`, `enterHighSpeedMode()`,
  `exitHighSpeedMode()`, `setHighSpeedMode()`, `supportsSleepMode()`,
  `enterSleep()`, `wake()`, and `wakeFromSleep()`.
- Optional `Config::i2cSpecial` transport callback for HS-prefixed
  transfers, Sleep entry, and Sleep wake stimulus without changing the
  existing normal write/write-read callbacks.
- Variant metadata fields for HS/Sleep capability, normal/HS bus limits, and
  Sleep recovery time.
- `WriteResult`, `writeDetailed()`, and `fillDetailed()` report requested bytes,
  transport-accepted prefix length, first failed chunk offset, and completion
  state for non-atomic bulk write/fill operations.
- `VerifyDetailedResult`, `verifyDetailed()`, `writeVerify()`, `fillVerify()`,
  and `Err::VERIFY_MISMATCH` support explicit readback verification when I2C
  acceptance is not enough to prove persistence.
- Native fake-bus coverage for partial write/fill chunk failures, WP-high
  ACK-without-persistence behavior, and write/fill verify mismatch handling.
- Native fake-bus coverage for HS/Sleep variant gating, special-transfer
  routing, expected HS master-code NACK scoping, Sleep state transitions, and
  wake recovery gating.
- Pure ESP-IDF CI job configuration that builds `examples/espidf_basic` for
  `esp32s2` and `esp32s3`.
- Hardware-validation matrix covering supported variants, address straps,
  WP behavior, High-speed/Sleep checks, pure ESP-IDF CLI, shared-bus behavior,
  and soak testing.
- Arduino and native ESP-IDF `heap` CLI commands for HIL heap telemetry.
- HIL runner strict production gates for required variant, product ID,
  capacity, zero UNKNOWNs, final READY health, zero failures, no resets or
  reconnects, and heap thresholds.

### Changed

- `Config::expectedVariant` now defaults to `DeviceVariant::AUTO` so
  Device-ID-capable parts select active capacity from readback. Fixed-BOM
  production integrations should still set the exact expected variant.
- Documented synchronous whole-range memory helpers as convenience APIs for
  poll-budgeted external-owner integrations.
- Documented timeout status policy and possible-write ambiguity after
  transport timeouts.
- Documented the staged transfer API as the recommended external-owner
  integration path for preserving one or more bounded backend FRAM chunks per
  scheduler poll.
- `Config::i2cTimeoutMs` is now bounded to
  `MIN_I2C_TIMEOUT_MS..MAX_I2C_TIMEOUT_MS`; the configured value remains the
  per-transaction deadline passed to injected transport callbacks.
- `Config::i2cAddress` is now treated as the board's base strap address.
  Variants that encode memory address bits in the transaction address reject
  ambiguous base addresses during `begin()`.
- Staged `request*()` APIs now reject OFFLINE, ASLEEP, WAKING, and active
  transfer states before queuing work, without touching the bus.
- `tick(uint32_t nowMs)` remains bus-silent and only advances cached Sleep wake
  recovery state when the caller-supplied timestamp reaches the recovery
  deadline.
- Lifetime `totalSuccess()` and `totalFailures()` health counters now wrap
  naturally as `uint32_t`; `consecutiveFailures()` remains saturating.
- Public `readDeviceId()` and `readDeviceIdRaw()` remain health-tracked, while
  diagnostic `probe()` remains raw and untracked.
- Doxygen input now points only at maintained docs instead of audit reports or
  generated extraction dumps.
- Replaced bulky prompt-era HIL transcripts and runner dumps with a compact
  `docs/reports/HIL_SUMMARY.md` evidence ledger.
- Breaking: `MB85RC` copy and move construction/assignment are now explicitly
  deleted. Applications should keep one driver instance per device and pass it
  by reference or pointer.
- Breaking for positional aggregate initialization: `Config` now contains
  optional High-speed/Sleep transport fields. Prefer default construction and
  named member assignment for stable configuration code.
- Public docs now state the thread-safety, ISR-safety, and non-recursive
  transport callback contracts.
- Current-address tracking is invalidated more conservatively after failed or
  diagnostic transactions that can disturb the device pointer.
- Arduino and native ESP-IDF diagnostic CLIs now include `hs`, `hs support`,
  `hs enter`, `sleep`, `sleep support`, `sleep enter`, and `sleep wake`
  commands. Hardware validation remains pending for real HS/Sleep mode use.
- ESP-IDF CLI destructive FRAM workflows now require explicit `!` confirmation
  forms (`write!`, `fill!`, `selftest!`, `rw_suite!`, `stress!`,
  `stress_mix!`, `randbench!`, and `typed_demo!`) and unconfirmed forms print
  the exact confirmed command required.
- ESP-IDF CLI parity was expanded with native fixed-buffer handlers for
  `text`, `strings`, `crc`, `verify`, `variants`, and current-address bulk
  reads instead of advertising them through a generic placeholder message.
- ESP-IDF port docs now document the confirmation policy and pending hardware
  validation status.
- Native ESP-IDF Device-ID access now uses a manual-address reserved-ID
  transaction path instead of normal device-handle addressing for the reserved
  `0xF8`/`0xF9` sequence.

### Fixed

- `begin()` and diagnostic `probe()` preserve original transport status codes
  instead of wrapping them as `DEVICE_NOT_FOUND`.
- The core no longer treats transport-reported `WRITE_PROTECTED` or
  `VERIFY_MISMATCH` as I2C health failures.
- ESP-IDF GitHub Actions container jobs now source the ESP-IDF export script
  before invoking `idf.py`.
- HIL runner active Product ID observation parsing now ignores product IDs from
  the variant catalog, so strict `--require-product-id` gates use the active
  device identity.
- Version and release metadata now consistently identify this hardening release
  as `3.0.0`.

## [2.0.0] - 2026-05-20

### Added

- Runtime variant selection with `DeviceVariant::AUTO` and explicit selectors for `MB85RC04V`, `MB85RC16V`, `MB85RC64TA`, `MB85RC256V`, `MB85RC512T`, and `MB85RC1MT`.
- `Config::expectedVariant` for explicit Device ID validation while keeping the legacy default at `MB85RC256V`.
- First-class runtime support for every locally documented variant, including `MB85RC64TA` Product ID `0x358`, 8 KB capacity, and `MB85RC1MT` 128 KB addressing.
- Variant-specific memory address encoding for one-byte address models, two-byte address-pin models, and `MB85RC1MT` A16-in-device-address transactions.
- Runtime diagnostics: `variantInfo()`, `variantName()`, `deviceId()`, `capacityBytes()`, `maxAddress()`, and expanded `SettingsSnapshot` fields.
- Native tests for every explicit runtime variant, Device ID AUTO selection where supported, variant mismatches, address encoding, active-capacity bounds, current-address bounds, active-variant `probe()` / `recover()`, and no-Device-ID 16V diagnostics.
- ESP-IDF component metadata and a native ESP-IDF `examples/espidf_basic` build of the full bring-up CLI command contract.
- ESP-IDF port notes, now consolidated in `docs/IDF_PORT.md`.

### Changed

- Breaking API change: memory addresses and `maxAddress()` are now `uint32_t` instead of `uint16_t` so `MB85RC1MT` can expose its full `0x00000..0x1FFFF` range.
- `begin()`, `probe()`, and `recover()` now validate the selected active variant instead of hard-coding the MB85RC256V product ID.
- `read()`, `write()`, `fill()`, `verify()`, `readCurrentAddress(uint8_t*, size_t)`, typed helpers, and CLI memory commands now reject ranges that cross the active variant capacity instead of silently relying on device rollover.
- Arduino and native ESP-IDF CLIs now initialize with `DeviceVariant::AUTO`, print active variant/capacity diagnostics, accept 32-bit addresses, and size full-chip/benchmark operations from the runtime capacity.
- `probe()` and `recover()` now use a memory-read presence check for `MB85RC16V`, because that part has no Device ID command.
- `readDeviceId()` and `readDeviceIdRaw()` now return `INVALID_PARAM` when the active explicit variant has no Device ID command.
- `library.json`, `idf_component.yml`, and README now describe the library as an MB85RC-family driver and include all supported runtime variants.
- `library.json` now declares both `arduino` and `espidf` framework support.
- Doxygen input now covers maintained docs, Arduino CLI source, and native IDF entry point.
- The ESP-IDF example adapter now performs Device ID transactions on reserved address `0x7C` through ESP-IDF defined I2C operations with manual address bytes instead of relying on normal device-handle addressing for a reserved address.
- `tools/check_idf_example_contract.py` now validates the native ESP-IDF boundary, command surface, required CMake dependencies, and core Device ID address construction.
- ESP-IDF CLI parity is checked through repo-local command contracts; hardware validation remains pending until target hardware is available.
- Core health timestamps now come only from injected `Config::nowMs`; framework time sources live in examples/application glue.
- ESP-IDF example is now a native IDF CLI using `app_main`, `driver/i2c_master.h`, `esp_timer`, `vTaskDelay`, and fixed C buffers.
- Public Doxygen comments now clarify runtime variant selection, active-capacity accessors, and legacy compatibility helpers.

### Fixed

- Active-capacity range checks now compare `size_t` lengths without truncating to `uint32_t`, so oversized 64-bit host lengths are rejected before any buffer access.

## [1.1.1] - 2026-05-17

### Changed

- Polished bringup CLI serial monitor output for successful diagnostic/demo commands.
- Stress progress now keeps color only on `ok` and `fail` counters.
- Stress summaries now label write/read/verify throughput as cycles, mixed-operation throughput as commands, and health counter changes as tracked I2C health deltas.
- `randbench` and `typed_demo` now use compact `[PASS]` status lines on success while preserving detailed `Status` output for failures.
- The bringup loop now uses the shared bounded CLI shell helper for cleaner prompt/output separation.
- The `stress` command parser now accepts only `stress` or `stress <count>`, matching CLI help.
- Public Doxygen comments now document all `SettingsSnapshot` fields and clarify that `BUSY` also represents latched `OFFLINE`.
- Changelog now carries release history directly without separate release-note files.

## [1.1.0] - 2026-05-14

### Added
- Public MB85RC family variant metadata, address-model descriptors, and `findVariantByProductId()`.
- Convenience `driverState()` alias and value-returning `getSettings()` overload for cross-library diagnostics.
- Native coverage proving latched `OFFLINE` blocks normal I2C operations without touching the bus while `recover()` remains the explicit recovery path.

### Changed

- Documented validation behavior for null buffers, zero-length transfers, out-of-range addresses, and current-address tracking.
- Reference documentation now keeps vendor PDFs and maintained compact device notes in `docs/`.
- Explicit recovery bypass internals now use the shared `ScopedOfflineI2cAllowance` / `_reassertOfflineLatch()` procedure so failed recovery attempts that begin from `OFFLINE` keep the latch asserted.
- Health behavior is now standardized on latched `OFFLINE`: normal public I2C operations return `BUSY` with `Driver is offline; call recover()` and do not touch I2C until `recover()` succeeds.

### Fixed

- Made `recover()` record Device ID mismatches in health tracking instead of returning a semantic error without updating driver state.
- Guarded health updates against `IN_PROGRESS` statuses and added native coverage for Device ID mismatch and validation no-bus-touch paths.

## [1.0.0] - 2026-04-07

### Added
- First stable release of the MB85RC256V FRAM library.
- Production-grade MB85RC256V driver with chunked `read()`, `write()`, `writeByte()`, and `fill()` support.
- Injected I2C transport architecture via `Config::i2cWrite` and `Config::i2cWriteRead`, with no direct `Wire` dependency in the library core.
- Deterministic managed-synchronous lifecycle with `begin()`, `tick()`, and `end()`.
- Health tracking with `READY`, `DEGRADED`, and `OFFLINE` driver states plus lifetime success/failure counters and timestamps.
- Runtime inspection helpers: `SettingsSnapshot`, `getSettings()`, `isInitialized()`, and `getConfig()`.
- Device ID verification on `begin()` plus public `readDeviceId()` and `readDeviceIdRaw()` helpers.
- Current-address-read support, including multi-byte current-address reads and tracked pointer rollover behavior.
- `verify()` helper for comparing FRAM contents against expected bytes without inventing synthetic transport errors.
- Single bundled bringup CLI example with memory inspection, diagnostics, stress tests, read/write validation, random-access benchmarking, and typed-value demo commands.
- Example-only `examples/common/TypedMemory.h` helpers for explicit little-endian fixed-width integers, `float`, `double`, and `bool`.
- Native Unity test coverage for lifecycle, health tracking, rollover behavior, current-address tracking, verify logic, random-access flows, and typed helper coverage.
- GitHub Actions CI pipeline with ESP32-S3/S2 builds, native tests, version header check, core timing guard, CLI contract enforcement, and library package validation.
- Doxygen configuration and refreshed release documentation for the stable API surface.
- MIT License.

### Changed
- The bringup CLI now serves as the single example entry point and bundles `rw_suite`, `randbench`, and `typed_demo` instead of splitting them into separate example applications.
- Numeric argument parsing in the example CLI is stricter and bounded for better diagnostic behavior on real hardware.
- README, portability notes, and release docs now reflect the finalized `v1.0.0` behavior and installation flow.
- Public header comments were normalized to ASCII-safe wording so generated Doxygen output is clean and stable.

### Fixed
- `readDeviceId()` health tracking now flows only through tracked transport wrappers.
- `begin()` and `end()` now clear stale runtime/config snapshots instead of leaking old state after shutdown or failed re-initialization.
- Internal memory helpers now reject null and zero-length misuse paths before touching fixed buffers.
- The example `Wire` transport now supports read-only transactions required by current-address reads.
- `stress_mix` no longer schedules `currentAddr` immediately after `recover()`, which intentionally invalidates the current-address state.
- README device characteristics and documentation references were aligned with the validated MB85RC256V datasheet behavior.

[Unreleased]: https://github.com/janhavelka/MB85RC/compare/v4.0.0...HEAD
[4.0.0]: https://github.com/janhavelka/MB85RC/compare/v3.0.0...v4.0.0
[3.0.0]: https://github.com/janhavelka/MB85RC/compare/v2.0.0...v3.0.0
[2.0.0]: https://github.com/janhavelka/MB85RC/compare/v1.1.1...v2.0.0
[1.1.1]: https://github.com/janhavelka/MB85RC/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/janhavelka/MB85RC/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/janhavelka/MB85RC/releases/tag/v1.0.0
