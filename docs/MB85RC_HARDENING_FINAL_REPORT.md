# MB85RC Industry-Readiness Hardening Final Report

Date: 2026-06-05
Last updated: 2026-06-07
Branch: `hardening/mb85rc-industry-readiness`

## Phase Status

| Phase | Status | Commit | Notes |
| --- | --- | --- | --- |
| 00 Kickoff / AGENTS / plan | Complete | `3b1b22f` | Branch created, hardening rules added, final report skeleton started, and Phase 00 checks passed. |
| 01 Core contracts | Complete | `22d4294` | Copy/move disabled, public contracts documented, guard strengthened, and current-address failure handling tightened. |
| 02 Partial write + WP persistence | Complete | `b89bc82` | Accepted-prefix APIs, readback verification helpers, WP-high simulation, and Phase 02 checks passed. |
| 03 ESP-IDF CI and CLI polish | Complete | `ab67f83` | Pure ESP-IDF CI job added, IDF guard strengthened, and diagnostic CLI blocking/bus-ownership caveats documented. |
| 04 Docs, examples, hardware validation matrix | Complete | TBD | Production FRAM semantics documented, variant table expanded, and hardware validation matrix added with all hardware rows pending. |
| 05 Final verification and release report | Pending | TBD | Not started. |

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

## Remaining Concerns

- Real hardware WP validation is still required: confirm WP low/open permits writes, WP high can ACK while blocking persistence, reads still work while WP high, and write/readback verification detects the protected state.
- Actual hardware execution of the validation matrix and final release/version metadata remain later-phase work.
