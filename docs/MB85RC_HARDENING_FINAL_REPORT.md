# MB85RC Industry-Readiness Hardening Final Report

Date: 2026-06-05
Last updated: 2026-06-07
Branch: `hardening/mb85rc-industry-readiness`

## Phase Status

| Phase | Status | Commit | Notes |
| --- | --- | --- | --- |
| 00 Kickoff / AGENTS / plan | Complete | `3b1b22f` | Branch created, hardening rules added, final report skeleton started, and Phase 00 checks passed. |
| 01 Core contracts | Complete | TBD | Copy/move disabled, public contracts documented, guard strengthened, and current-address failure handling tightened. |
| 02 Partial write + WP persistence | Pending | TBD | Not started. |
| 03 ESP-IDF CI and CLI polish | Pending | TBD | Not started. |
| 04 Docs, examples, hardware validation matrix | Pending | TBD | Not started. |
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

### Remaining Concerns

- Prompt 02 must still handle partial multi-chunk write/fill accepted-prefix reporting and WP-high ACK/no-persistence behavior.
- Pure ESP-IDF CI/build proof, IDF CLI blocking behavior, documentation/hardware validation matrix, and production storage guidance remain later-phase work.
