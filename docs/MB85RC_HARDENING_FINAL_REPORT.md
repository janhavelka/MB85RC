# MB85RC Industry-Readiness Hardening Final Report

Date: 2026-06-07
Last updated: 2026-06-08
Branch: `hardening/mb85rc-industry-readiness`

## Phase Status

| Phase | Status | Commit | Notes |
| --- | --- | --- | --- |
| 00 Kickoff / AGENTS / plan | Complete | `3b1b22f` | Branch created, hardening rules added, final report skeleton started, and Phase 00 checks passed. |
| 01 Core contracts | Complete | `22d4294` | Copy/move disabled, public contracts documented, guard strengthened, and current-address failure handling tightened. |
| 02 Partial write + WP persistence | Complete | `b89bc82` | Accepted-prefix APIs, readback verification helpers, WP-high simulation, and Phase 02 checks passed. |
| 03 ESP-IDF CI and CLI polish | Complete | `ab67f83` | Pure ESP-IDF CI job added, IDF guard strengthened, and diagnostic CLI blocking/bus-ownership caveats documented. |
| 04 Docs, examples, hardware validation matrix | Complete | `4071f08`, `a159e95` | Production FRAM semantics documented, variant table expanded, hardware validation matrix added, and replay wording fixes applied. |
| 05 Final verification and release report | Complete | `da2a5b6`, `c51fde0` | Final review defects fixed, full local verification rerun, and merge/release readiness documented. |
| 06 High-Speed and Sleep support | Complete | `b5f3998` | Variant-gated HS/Sleep APIs, optional special transport callback, diagnostic CLI parity, tests, and documentation added. |
| 07 Release docs and metadata cleanup | Complete | This release-prep commit | Version metadata finalized, changelog released, prompt archive added, docs reconciled, and release checks rerun. |

## Starting Audit Findings

The IDF-merged industry-readiness audit found a strong foundation: framework-neutral
core code, injected I2C callbacks, variant-aware address encoding, range checks,
chunked read/write, conservative current-address semantics, no EEPROM-style write
delay, and passing native tests. The hardening sequence intentionally avoided a
broad rewrite.

The main audit targets handled by this branch were:

- Pure ESP-IDF build coverage in CI and direct guard invocation.
- Explicit partial multi-chunk write/fill accepted-prefix reporting.
- WP-high ACK/no-persistence honesty and write/fill verify convenience APIs.
- Deleted copy/move operations and documented thread/ISR/reentrancy contracts.
- IDF CLI diagnostic labeling, confirmation syntax, and native IDF boundary checks.
- Hardware validation documentation by FRAM variant and address-pin/address-bit mode.

## Phase 00 Kickoff Notes

- Scope: documentation-only kickoff. No core, test, example, CI, or metadata behavior was changed in this phase.
- Branch setup: no local or remote `hardening/mb85rc-industry-readiness` branch existed; the branch was created from clean `audit/mb85rc-idf-merged-industry-readiness` state, then remote refs were fetched.
- Subagents used: Core contracts, FRAM semantics, Tests/fault-injection, IDF/CI, and Docs/examples.
- Handoff concerns for later phases: copy/move deletion, public-header thread/ISR/reentrancy contract, partial multi-chunk write/fill semantics, WP-high ACK/no-persistence testing, pure ESP-IDF build coverage, IDF CLI/tick/input behavior, hardware validation matrix, and production persistence guidance.
- Additional subagent concerns to triage: no-Device-ID `probe()` current-address cache semantics, documented device rollover versus public non-wrapping range contract, variant-specific I2C address wording, and IDF Device-ID guard/documentation drift.

## Phase 00 Checks

| Command | Result |
| --- | --- |
| `git status --short` | PASS: only `AGENTS.md` modified and `docs/MB85RC_HARDENING_FINAL_REPORT.md` untracked before commit. |
| `python --version` | PASS: `Python 3.12.10`. |
| `python -m platformio --version` | PASS: `PlatformIO Core, version 6.1.18`. |
| `python tools/check_core_timing_guard.py` | PASS: `Core timing guard PASSED`. |
| `python tools/check_cli_contract.py` | PASS: `IDF example contract PASSED`; `CLI contract PASSED`. |
| `python tools/check_idf_example_contract.py` | PASS: `IDF example contract PASSED`. |
| `python scripts/generate_version.py check` | PASS: `include\MB85RC\Version.h` up to date. |

## Phase 01 Core Contracts

### API And Core Changes

- Explicitly deleted `MB85RC` copy and move construction/assignment in the public class.
- Added public Doxygen contracts that `MB85RC` instances are not internally thread-safe, public I2C APIs are not ISR-safe, transport callbacks must not recursively call into the same instance, and bus ownership/locking/retry policy stay outside the core.
- Documented that current-address reads are only safe after a known address-setting transaction by the same instance.
- Conservatively clear current-address cache state after raw memory reads used by diagnostics/probes, including no-Device-ID variant probes.
- Strengthened `tools/check_core_timing_guard.py` so the core guard rejects broader Arduino, Wire, ESP-IDF, FreeRTOS, logging, task, semaphore, and framework-type leakage in `include/` and `src/`.

### Tests Added

- Compile-time `static_assert` checks that `MB85RC` is not copy-constructible, copy-assignable, move-constructible, or move-assignable.
- Native tests for zero-length/null-buffer no-bus behavior and exact-end/cross-end/address-overflow range behavior.
- Native tests for current-address invalidation after no-Device-ID `probe()`, failed random read, failed write, and failed current-address read.

### Phase 01 Verification

| Command | Result |
| --- | --- |
| `python tools/check_core_timing_guard.py` | PASS: `Core timing guard PASSED`. |
| `python tools/check_cli_contract.py` | PASS: `IDF example contract PASSED`; `CLI contract PASSED`. |
| `python tools/check_idf_example_contract.py` | PASS: `IDF example contract PASSED`. |
| `python scripts/generate_version.py check` | PASS: `include\MB85RC\Version.h` up to date. |
| `python -m platformio test -e native` | PASS: 79 test cases, 79 succeeded. |
| `python -m platformio run -e esp32s3dev` | PASS: `esp32s3dev` succeeded. |
| `python -m platformio run -e esp32s2dev` | PASS: `esp32s2dev` succeeded. |
| `python -m platformio pkg pack` | PASS: wrote `MB85RC-2.0.0.tar.gz`; artifact removed after validation. |

## Phase 02 Partial Write + WP Persistence

### API And Core Changes

- Added `WriteResult`, `writeDetailed()`, and `fillDetailed()` so callers can distinguish requested bytes, transport-accepted prefix length, first failed chunk offset/length, and full completion.
- Added `VerifyDetailedResult`, `verifyDetailed()`, `writeVerify()`, `fillVerify()`, and append-only `Err::VERIFY_MISMATCH` for explicit readback confidence when I2C acceptance is not enough.
- Preserved simple `write()` and `fill()` compatibility: they still return the first failing `Status`, with no rollback, no EEPROM-style write delay, and no ACK polling.
- Updated public Doxygen and README wording to describe `bytesAccepted` as an accepted prefix, not committed or persistent data.
- Updated example status string maps so `VERIFY_MISMATCH` is not reported as `UNKNOWN`.

### Accepted Prefix Vs Persistence

- `bytesAccepted` means chunks returned `Status::Ok()` from the injected transport. It does not prove memory contents changed.
- `bytesVerified` means readback matched the expected data before a mismatch or transport failure.
- WP-high behavior is represented in native tests by a fake backing store mode where writes ACK but memory is unchanged; `verify()`, `verifyDetailed()`, `writeVerify()`, and `fillVerify()` detect the mismatch.
- The core still does not infer WP state or expose a fake WP-detected status because the device does not provide that signal over I2C.

### Tests Added

- Native tests for single-chunk and multi-chunk `writeDetailed()` / `fillDetailed()` success.
- Native tests for first, middle, and last chunk failures with exact accepted-prefix and suffix-unchanged assertions.
- Native tests proving preflight range and overflow failures touch neither bus nor health counters.
- Native tests for current-address invalidation after failed multi-chunk write/fill operations.
- Native WP-high simulation tests proving simple write ACK can succeed without persistence, while readback verification reports mismatch.
- Native tests for `writeVerify()` and `fillVerify()` success and WP-high mismatch failure.

### Phase 02 Verification

| Command | Result |
| --- | --- |
| `python tools/check_core_timing_guard.py` | PASS: `Core timing guard PASSED`. |
| `python tools/check_cli_contract.py` | PASS: `IDF example contract PASSED`; `CLI contract PASSED`. |
| `python tools/check_idf_example_contract.py` | PASS: `IDF example contract PASSED`. |
| `python scripts/generate_version.py check` | PASS: `include\MB85RC\Version.h` up to date. |
| `python -m platformio test -e native` | PASS: 92 test cases, 92 succeeded. |
| `python -m platformio run -e esp32s3dev` | PASS: `esp32s3dev` succeeded. |
| `python -m platformio run -e esp32s2dev` | PASS: `esp32s2dev` succeeded. |
| `python -m platformio pkg pack` | PASS: wrote `MB85RC-2.0.0.tar.gz`; artifact removed after validation. |
| `git diff --check` | PASS: no whitespace errors reported. |

### Scope Control

- No CI workflows, IDF example sources, guard tools, or Prompt 03+ implementation files were changed in this phase.
- Example edits were limited to status-name mapping for the new `VERIFY_MISMATCH` error code.

## Phase 03 ESP-IDF CI And CLI Polish

### CI And Guard Changes

- Added a dedicated GitHub Actions `esp-idf-build` job using `espressif/idf:release-v6.0`.
- The new job records `idf.py --version` and builds `examples/espidf_basic` for `esp32s3` and `esp32s2` with `idf.py -C examples/espidf_basic set-target <target> build`.
- Added direct CI invocation of `python tools/check_idf_example_contract.py`; CI no longer relies only on the indirect call through `check_cli_contract.py`.
- Strengthened `tools/check_idf_example_contract.py` so it scans the IDF example source/CMake tree for Arduino compatibility tokens, rejects broad main-component include paths, verifies CI contains the IDF build commands, and verifies README IDF validation/diagnostic wording.

### IDF Example Changes

- Narrowed `examples/espidf_basic/main/CMakeLists.txt` to `INCLUDE_DIRS "."`; the public `MB85RC` include path now comes from the `MB85RC` component dependency.
- Kept the IDF CLI as a diagnostic-only blocking console instead of adding a task/mutex framework in this phase.
- Added IDF CLI startup text and README wording that the example owns its I2C bus, console input can block before `tick()`, and production systems must serialize shared-bus access in their own transport or bus manager.
- Added a null-bus guard to `scanBus()` so a failed `initBus()` does not call `i2c_master_probe()` with a null bus handle.
- Updated IDF CLI help to include the confirmed destructive `typed_demo!` command form.
- Left transport callbacks single-task and diagnostic-owned; no production shared-bus locking is claimed.

### Phase 03 Verification

| Command | Result |
| --- | --- |
| `python tools/check_core_timing_guard.py` | PASS: `Core timing guard PASSED`. |
| `python tools/check_cli_contract.py` | PASS: `IDF example contract PASSED`; `CLI contract PASSED`. |
| `python tools/check_idf_example_contract.py` | PASS: `IDF example contract PASSED`. |
| `python scripts/generate_version.py check` | PASS: `include\MB85RC\Version.h` up to date. |
| `python -m platformio test -e native` | PASS: 92 test cases, 92 succeeded. |
| `python -m platformio run -e esp32s3dev` | PASS: `esp32s3dev` succeeded. |
| `python -m platformio run -e esp32s2dev` | PASS: `esp32s2dev` succeeded. |
| `idf.py --version` | UNAVAILABLE: PowerShell reported `idf.py : The term 'idf.py' is not recognized as the name of a cmdlet, function, script file, or operable program.` |
| `idf.py -C examples/espidf_basic set-target esp32s3 build` | SKIPPED locally because `idf.py` is unavailable; CI is expected to run this command. |
| `idf.py -C examples/espidf_basic set-target esp32s2 build` | SKIPPED locally because `idf.py` is unavailable; CI is expected to run this command. |

### Phase 03 Remaining Concerns

- Pure ESP-IDF builds are now represented in CI, but local IDF build proof is still pending until ESP-IDF is activated on the local PATH or CI results are reviewed.
- The IDF CLI remains a diagnostic bring-up tool. It is not a production scheduler or shared-bus manager template.

## Phase 04 Documentation, Hardware Matrix, And Production Guidance

### Documentation Changes

- Added a README production-readiness summary that separates production-oriented API/test/CI coverage from board- and variant-specific hardware validation.
- Added a dedicated README I2C ownership and concurrency section covering injected transport ownership, external locking, ISR safety, and non-recursive callbacks.
- Expanded FRAM write and current-address guidance: no EEPROM-style write delay or ACK polling, non-atomic multi-chunk writes/fills, WP-high ACK/no-persistence risk, verify/writeVerify use, and explicit-address reads for deterministic production paths.
- Replaced the supported variant table with capacity, I2C address model, memory-address model, Device-ID support, max documented bus speed, and high-speed/sleep notes for all supported runtime variants.
- Added endurance/retention guidance that points production users back to the exact part datasheet and board conditions.
- Added a production hardware validation matrix with all rows marked pending hardware, including Device-ID/no-ID paths, address pins/upper-address bits, exact-end access, boundary rejection, WP-high, bulk verify, current-address reads, unplug/NACK, brownout/power-cycle persistence, pure IDF CLI on ESP32-S2/S3, shared bus, and long soak.
- Clarified `Config` transport callback docs, `probe()` and `recover()` Doxygen, and the Arduino CLI example label.
- Replay audit tightened README variant address wording for `MB85RC04V` and `MB85RC1MT`, removed "safe" overstatements from mutating Arduino diagnostics, and listed ESP-IDF `stress_mix!` / `randbench!` help forms.

### Hardware Validation Matrix Status

- No hardware tests were run in Phase 04.
- README hardware validation rows are planning entries only. They must not be changed to pass/validated without board, FRAM variant, address-pin strap, supply, bus speed, WP wiring, command transcript, date, and commit evidence.

### Phase 04 Verification

| Command | Result |
| --- | --- |
| `python tools/check_core_timing_guard.py` | PASS: `Core timing guard PASSED`. |
| `python tools/check_cli_contract.py` | PASS: `IDF example contract PASSED`; `CLI contract PASSED`. |
| `python tools/check_idf_example_contract.py` | PASS: `IDF example contract PASSED`. |
| `python scripts/generate_version.py check` | PASS: `include\MB85RC\Version.h` up to date. |
| `python -m platformio test -e native` | PASS: 92 test cases, 92 succeeded. |
| `python -m platformio run -e esp32s3dev` | PASS: `esp32s3dev` succeeded. |
| `python -m platformio run -e esp32s2dev` | PASS: `esp32s2dev` succeeded. |
| `doxygen Doxyfile` | PASS: Doxygen 1.13.2 completed; generated `docs/doxygen` output was removed after the check. |

### Phase 04 Remaining Hardware Work

- Run the full hardware validation matrix on each production BOM variant and address strap.
- Record WP low/open and WP high persistence behavior on real boards.
- Record brownout/power-cycle persistence and application journal behavior.
- Record shared-bus behavior with the actual application bus manager.
- Record long-soak write/read/verify evidence over a sacrificial range.

## Phase 05 Final Verification And Merge Readiness

### Final Review Fixes

- Preserved original transport status codes from `begin()` and diagnostic `probe()` failures instead of wrapping them as `DEVICE_NOT_FOUND`; native tests were updated to lock the precise error model.
- Added a native ESP-IDF Device-ID manual-address transaction path using `I2C_DEVICE_ADDRESS_NOT_USED` and `i2c_master_execute_defined_operations()` for the reserved `0xF8`/`0xF9` sequence.
- Wired the existing ESP-IDF Device-ID manual-address token list into `tools/check_idf_example_contract.py`.
- Set the native ESP-IDF diagnostic CLI to `DeviceVariant::AUTO`, matching Arduino CLI behavior and changelog wording.
- Clarified `AGENTS.md` so the device-level rollover capability is not confused with the public driver contract: public bulk APIs reject cross-capacity ranges unless a future explicit wrap API is added and tested.
- Tightened README and public-header wording for ESP-IDF validation status, variant-aware `Config::i2cAddress`, and the no-Device-ID read-only presence probe.

### Public API Changes

- `MB85RC` copy and move construction/assignment are explicitly deleted.
- `WriteResult`, `writeDetailed()`, and `fillDetailed()` report requested bytes, accepted prefix, failed chunk offset/length, and completion.
- `VerifyDetailedResult`, `verifyDetailed()`, `writeVerify()`, `fillVerify()`, and `Err::VERIFY_MISMATCH` add readback verification for critical writes/fills.
- Existing simple `read()`, `write()`, `fill()`, and `verify()` remain available and keep the public no-wrap active-capacity range contract.
- Public headers document thread/ISR/reentrancy limitations, injected transport ownership, current-address caveats, non-atomic bulk writes/fills, and WP-high verification caveats.

### Core Changes

- Core under `include/` and `src/` remains framework-neutral: no Arduino, Wire, ESP-IDF, FreeRTOS, logging framework, global bus, hidden delay, or framework heap type is used.
- All core I2C remains injected through `Config::i2cWrite` and `Config::i2cWriteRead`; the core does not own, lock, reset, or configure the physical I2C bus.
- No post-write delay or EEPROM-style ACK polling was added.
- Device-ID behavior remains variant-aware, including explicit no-Device-ID handling for `MB85RC16V`.
- Current-address tracking is conservative after failures, recovery, and diagnostics that can disturb the device pointer.
- Final status mapping fix keeps raw transport errors visible from `begin()` and diagnostic `probe()` failures.

### ESP-IDF And CI Changes

- `.github/workflows/ci.yml` includes PlatformIO ESP32-S2/ESP32-S3 builds, native tests, guard scripts, package validation, and an ESP-IDF job using `espressif/idf:release-v6.0`.
- The ESP-IDF CI job records `idf.py --version` and runs `idf.py -C examples/espidf_basic set-target esp32s3 build` and `idf.py -C examples/espidf_basic set-target esp32s2 build`.
- The native ESP-IDF example remains `app_main`/`driver/i2c_master.h` based and does not include Arduino compatibility sources.
- ESP-IDF CLI remains a diagnostic bring-up example; it owns the example bus and is not a production scheduler or shared-bus manager.
- Local workflow syntax inspection parsed `.github/workflows/ci.yml` successfully with PyYAML.

### Test Additions And Coverage

- Native tests cover copy/move deletion, boundary/overflow behavior, exact-end/cross-end rejection, partial multi-chunk write/fill accepted-prefix reporting, failure on first/middle/last chunks, WP-high ACK/no-persistence simulation, write/fill verify success and mismatch failure, Device-ID success/failure paths, and current-address invalidation after failure.
- Guard scripts cover framework leakage, core timing, Arduino/IDF command-contract parity, native IDF example boundaries, confirmation forms, CI IDF command presence, and IDF Device-ID manual-address tokens.

### Documentation Changes

- README now separates implemented behavior, unit-test coverage, CI/build coverage, and pending hardware validation.
- README and public headers document non-atomic write/fill behavior, accepted prefix versus verified persistence, WP-high caveat, no EEPROM-style write delay, current-address caveat, thread/ISR/reentrancy contract, and diagnostic versus production example scope.
- README contains a variant table covering capacity, I2C address model, memory-address model, Device-ID support, max documented bus speed, and high-speed/sleep notes.
- README contains a hardware validation matrix with all hardware rows still marked pending.
- Production storage guidance recommends record metadata, CRC/generation counters, verify-before-commit, commit marker last, boot scan, and part-specific endurance/retention budgeting.

## Phase 06 High-Speed And Sleep Support

### API And Core Changes

- Added explicit variant metadata for High-speed and Sleep capability, normal and HS bus-rate limits, and Sleep recovery time.
- Added append-only `Err::UNSUPPORTED` for variant-gated mode APIs that are not documented for the active part.
- Added optional `Config::i2cSpecial` with `I2cSpecialOp` / `I2cSpecialTransfer` for bus sequences that cannot be represented by normal 7-bit write/write-read callbacks.
- Added `supportsHighSpeedMode()`, `maxNormalBusHz()`, `maxHighSpeedBusHz()`, `enterHighSpeedMode()`, `exitHighSpeedMode()`, and `setHighSpeedMode()`.
- Added `SleepState`, `supportsSleepMode()`, `sleepRecoveryUs()`, `enterSleep()`, `wake()`, and `wakeFromSleep()`.
- Kept the core framework-neutral and non-owning: it does not change the MCU I2C clock, does not own bus timing/locking, and inserts no hidden Sleep wake delay.

### Protocol Boundaries

- High-speed mode is implemented as a transfer-mode toggle. While enabled, memory and current-address operations use the special callback so each transaction can be prefixed with `0000 1XXX`; STOP exits HS state per local datasheets.
- Device ID, `probe()`, and `recover()` continue to use normal callbacks.
- The expected NACK on the HS master-code prefix is scoped to the special callback only. Generic NACKs remain failures.
- Sleep entry uses the documented `F8h` + active device address word + repeated-start `86h` sequence through the special callback.
- Sleep wake sends a wake stimulus, enters `WAKING`, and requires caller-managed `tREC >= 400 us` before normal access.

### Supported And Unsupported Variants

- HS/Sleep supported by local docs and APIs: `MB85RC64TA`, `MB85RC512T`, `MB85RC1MT`.
- HS/Sleep unsupported by local docs and APIs: `MB85RC04V`, `MB85RC16V`, `MB85RC256V`.
- Unsupported mode requests return `UNSUPPORTED` before bus traffic.

### Tests Added

- Native fake-bus tests for invalid HS master code, unsupported HS/Sleep no-traffic behavior, missing special callback behavior, HS special-transfer routing, generic NACK failure outside HS prefix, Sleep entry/wake state transitions, wake recovery gating, and Sleep entry failure health/current-address behavior.
- Native test count increased to 104 passing cases after Phase 06 additions.

### Examples, Guards, And Documentation

- Arduino diagnostic CLI gained `hs`, `hs support`, `hs enter`, `sleep`, `sleep support`, `sleep enter`, and `sleep wake` commands. The default Wire transport does not install a raw special callback, so commands report capability and transport limitations honestly.
- Native ESP-IDF diagnostic CLI gained the same commands and implements the special callback with `i2c_master_execute_defined_operations()`.
- Example HS diagnostics emit the HS prefix through the configured bus but do not claim real 3.4 MHz operation; application bus-manager clock changes and hardware validation remain pending.
- Guard scripts now require HS/Sleep CLI command parity and conservative diagnostic wording.
- README, extracted protocol notes, changelog, and hardware matrix now document HS/Sleep semantics and pending hardware validation.

### Phase 06 Verification

| Command | Result |
| --- | --- |
| `python tools/check_core_timing_guard.py` | PASS: `Core timing guard PASSED`. |
| `python tools/check_cli_contract.py` | PASS: `IDF example contract PASSED`; `CLI contract PASSED`. |
| `python tools/check_idf_example_contract.py` | PASS: `IDF example contract PASSED`. |
| `python scripts/generate_version.py check` | PASS: `include\MB85RC\Version.h` up to date. |
| `python -m platformio test -e native` | PASS: 104 test cases, 104 succeeded after adding HS/Sleep coverage. |
| `python -m platformio run -e esp32s3dev` | PASS: `esp32s3dev` succeeded. |
| `python -m platformio run -e esp32s2dev` | PASS: `esp32s2dev` succeeded. |
| `python -m platformio pkg pack` | PASS: wrote `MB85RC-2.0.0.tar.gz`; artifact removed after validation. |
| `doxygen Doxyfile` | PASS: Doxygen completed; generated `docs/doxygen` output was removed after the check. |
| `idf.py --version` | UNAVAILABLE: PowerShell reported `idf.py : The term 'idf.py' is not recognized as the name of a cmdlet, function, script file, or operable program.` |
| `git diff --check` | PASS: no whitespace errors; Git reported only local LF-to-CRLF normalization warnings. |

## Phase 07 Release Documentation And Metadata Cleanup

### Version Decision

- Selected release version: `3.0.0`.
- The prompt expected `2.1.0` if the release was purely additive. Final review found source-compatibility breaks: `MB85RC` copy/move operations are now deleted, and `Config` gained fields that can break positional aggregate initialization. The release metadata therefore uses a major version.
- `library.json` remains the source of truth for generated `include/MB85RC/Version.h`; `python scripts/generate_version.py set 3.0.0` regenerated the header.

### Files Updated

- Release metadata: `library.json`, `idf_component.yml`, `Doxyfile`, and generated `include/MB85RC/Version.h`.
- Release docs: `CHANGELOG.md`, `README.md`, and this final report.
- Documentation hygiene: `docs/IDF_PORT.md`, `docs/IDF_PORT_IMPLEMENTATION.md`, `docs/time_ownership_audit.md`, `docs/MB85RC_IDF_MERGED_INDUSTRY_READINESS_AUDIT.md`, and `docs/MB85RC256V_fram_implementation_manual.md`.
- Release process: added `docs/RELEASE_CHECKLIST.md`.
- Prompt hygiene: archived Phase 00-05 prompts under `docs/prompts/archive/mb85rc-industry-readiness/` with an index explaining that Phase 06 superseded older HS/Sleep future-work wording.

### Documentation Cleanup

- README now advertises `v3.0.0`, adds release highlights, preserves `2.0.0` as historical release notes, and explains the breaking compatibility notes.
- Changelog now has a dated `3.0.0` section and an empty reserved `Unreleased` section.
- Doxygen metadata now says `3.0.0`; Doxygen input is scoped to public headers and current release docs rather than publishing example helper symbols or historical extracted docs as public API.
- Historical audit/extraction docs now carry superseded-status notes so old "HS not implemented" / "Sleep unknown" findings are not mistaken for current implementation status.
- `tick()` documentation now states the current behavior: no async I2C and no write-delay work, but Sleep wake recovery advances from caller-supplied time.

### Phase 07 Verification

| Command | Result |
| --- | --- |
| `git status --short` | PASS at start: clean. After edits: only intended release/docs changes and prompt moves. |
| `git branch --show-current` | PASS: `hardening/mb85rc-industry-readiness`. |
| `git pull --ff-only` | PASS: already up to date. |
| `python --version` | PASS: `Python 3.12.10`. |
| `python -m platformio --version` | PASS: `PlatformIO Core, version 6.1.18`. |
| `python tools/check_core_timing_guard.py` | PASS: `Core timing guard PASSED`. |
| `python tools/check_cli_contract.py` | PASS: `IDF example contract PASSED`; `CLI contract PASSED`. |
| `python tools/check_idf_example_contract.py` | PASS: `IDF example contract PASSED`. |
| `python scripts/generate_version.py check` | PASS: `include\MB85RC\Version.h` up to date. |
| `python -m platformio test -e native` | PASS: 104 test cases, 104 succeeded. |
| `python -m platformio run -e esp32s3dev` | PASS: `esp32s3dev` succeeded. |
| `python -m platformio run -e esp32s2dev` | PASS: `esp32s2dev` succeeded. |
| `python -m platformio pkg pack` | PASS: wrote `MB85RC-3.0.0.tar.gz`; artifact removed after validation. |
| `doxygen Doxyfile` | PASS: Doxygen 1.13.2 completed; generated `docs/doxygen` output was removed after the check. |
| `git diff --check` | PASS: no whitespace errors; Git reported only local LF-to-CRLF normalization warnings. |

PlatformIO emitted a non-fatal warning that obsolete PlatformIO Core 6.1.18 is
active while 6.1.19 was previously seen on the machine. Builds and tests still
completed successfully.

### Commands Not Run

| Command | Reason |
| --- | --- |
| `idf.py --version` | UNAVAILABLE locally: PowerShell reported `idf.py : The term 'idf.py' is not recognized as the name of a cmdlet, function, script file, or operable program.` |
| `idf.py -C examples/espidf_basic set-target esp32s3 build` | SKIPPED locally because `idf.py` is unavailable; configured in CI. |
| `idf.py -C examples/espidf_basic set-target esp32s2 build` | SKIPPED locally because `idf.py` is unavailable; configured in CI. |
| `gh run list --branch hardening/mb85rc-industry-readiness --limit 10` | UNAVAILABLE locally: PowerShell reported `gh : The term 'gh' is not recognized as the name of a cmdlet, function, script file, or operable program.` |
| GitHub Actions CI run result | Not observed locally because `gh` is unavailable and this workflow is configured for pushes/PRs to `main`; CI pass is not claimed. |
| Real hardware validation | Not run in this sequence; README matrix remains pending hardware. |

### Hardware Validation Matrix Status

- No hardware validation was run or claimed in this hardening sequence.
- The README matrix remains the source of planned hardware evidence by variant, address pin/address-bit mode, WP wiring, power-cycle/brownout behavior, shared bus, pure IDF CLI on ESP32-S2/S3, and long soak.
- Rows must remain pending until board, FRAM part/package, address straps, voltage, pull-ups, bus speed, WP state, command transcript, date, and commit evidence are recorded.

### Remaining Risks

- Real WP-pin behavior still needs board validation: WP low/open should persist writes; WP high may ACK while blocking memory modification; readback verification should catch that state.
- Real High-speed 3.4 MHz operation and Sleep current/wake behavior still need board validation on `MB85RC64TA`, `MB85RC512T`, and `MB85RC1MT`.
- Pure ESP-IDF builds are configured in CI but were not locally run because `idf.py` is unavailable on PATH.
- Hardware behavior across all variants, address straps, power profiles, and shared-bus topologies is not field-proven by this branch.
- GitHub Actions status was not observed locally because `gh` is unavailable; review normal PR CI before tagging.

### Future Work

- Execute the hardware validation matrix by supported variant and address pin/address-bit mode.
- Validate WP pin behavior on representative real boards.
- Run or review pure ESP-IDF S2/S3 CI build results before claiming IDF build proof.
- Hardware-validate High-speed and Sleep diagnostics on `MB85RC64TA`, `MB85RC512T`, and `MB85RC1MT` before claiming real-board HS/Sleep behavior.
- Add a production journaling example if users need an application-level storage pattern beyond README guidance.
- Tag and publish `v3.0.0` only after normal PR CI is reviewed.

### Merge Readiness Verdict

This branch is ready to merge and prepare as MB85RC v3.0.0 after normal PR CI is
reviewed, including the configured ESP-IDF ESP32-S2/S3 jobs. It should not be
described as fully field-proven across all supported MB85RC variants until
hardware validation is completed on representative parts and boards.

### Release Wording Recommendation

- Recommended tag after CI review: `v3.0.0`.
- Suggested wording: "MB85RC v3.0.0 adds accepted-prefix write/fill reporting, readback verification helpers, variant-gated High-speed/Sleep support, stronger contracts, ESP-IDF CI coverage, and production documentation. Hardware validation remains board- and variant-dependent."
