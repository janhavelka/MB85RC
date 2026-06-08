# MB85RC IDF-Merged Industry-Readiness Audit

Date: 2026-05-29
Repository: `C:\Users\HonzovoSpectre\Documents\Projects\MB85RC`
Branch: `audit/mb85rc-idf-merged-industry-readiness`
Audit mode: report-only / no implementation
IDF merge classification: `QUALIFYING_IDF_MERGED`

Historical note: this report is the pre-hardening audit snapshot that launched
the Phase 00-07 work. Current implementation and release-readiness status is
tracked in `docs/MB85RC_HARDENING_FINAL_REPORT.md`; where the two documents
differ, the final hardening report supersedes this audit for current status.

## Executive Summary

The ESP-IDF port is merged into `main`, the core is framework-neutral, and the native/fault-injection coverage is materially stronger than the other audited repositories. The library has a good foundation for production FRAM use: injected I2C callbacks, variant-aware address encoding, range checks, chunked read/write paths, current-address semantics, and no EEPROM-style write delay assumption.

The gaps are still real. Pure ESP-IDF builds are not proven locally or in CI, multi-chunk writes are not atomic and do not report how much committed before a failure, write success cannot prove persistence when the hardware WP pin is high, and implicit copy/move operations appear possible. This repository is a good next candidate for a focused hardening pass.

## IDF Merge Evidence

- Default branch evidence: `origin/HEAD -> refs/heads/main`.
- Merge commit: `42a730d0d0a77ffdb2a86f1ae917dfa0280efa92`.
- Merge message: `Merge pull request #1 from janhavelka:feature/mb85rc-idf-port`.
- Merged branch tip: `93a469374c7d7b9b66f32d2cd8f332995876d998`.
- Branch ancestry: `feature/mb85rc-idf-port` is an ancestor of `main`.
- Relevant later `main` commits:
  - `68514c7` Add time ownership and timer domain audit documentation.
  - `4e2a7ad` Enhance ESP-IDF CLI with explicit confirmation for destructive commands and update documentation.
- IDF artifacts present on `main`:
  - `CMakeLists.txt`
  - `idf_component.yml`
  - `examples/espidf_basic/main/main.cpp`
  - `examples/espidf_basic/main/CMakeLists.txt`
  - `tools/check_idf_example_contract.py`
  - `docs/IDF_PORT.md`
  - `docs/IDF_PORT_IMPLEMENTATION.md`
- Limitation: the native ESP-IDF example was not built with `idf.py` in this audit because `idf.py` is not installed or not on `PATH`.

## Readiness Classification

Pre-production candidate pending validation.

The architecture and native tests are comparatively strong. The remaining blockers are clear and bounded: pure IDF build coverage, explicit partial-write semantics, WP/persistence honesty, copy/move deletion, and hardware validation across at least the claimed FRAM variants.

## Scope Reviewed

- `include/MB85RC/`
- `src/`
- `examples/01_basic_bringup_cli/`
- `examples/espidf_basic/`
- `examples/common/`
- `test/`
- `tools/`
- `docs/`
- `README.md`
- `platformio.ini`
- `library.json`
- `CMakeLists.txt`
- `idf_component.yml`
- `.github/workflows/ci.yml`

## Datasheet / Documentation Sources Found

- `docs/MB85RC04V-DS5v1-E.pdf`
- `docs/MB85RC16V-DS11v0-E.pdf`
- `docs/MB85RC1MT-DS5v1-E.pdf`
- `docs/MB85RC256V-Data-Sheet-DS501-00017-11v2-E.pdf`
- `docs/MB85RC256V-Fact-Sheet-NP501-00019-2v0-E.pdf`
- `docs/MB85RC256V_fram_implementation_manual.md`
- `docs/MB85RC512T-DS6v1-E.pdf`
- `docs/MB85RC64TA-DS5v1-E.pdf`
- `docs/time_ownership_audit.md`
- `docs/IDF_PORT.md`
- `docs/IDF_PORT_IMPLEMENTATION.md`
- `docs/extracted-md/*.md`

## Scorecard

| Area | Rating | Notes |
| --- | --- | --- |
| IDF merge evidence | Strong | Merge commit, branch ancestry, IDF artifacts, and docs are present. |
| Core framework neutrality | Strong | No Arduino, Wire, ESP-IDF, FreeRTOS, logging, global bus, or heap use found in core. |
| I2C ownership/injection | Strong | I2C callbacks and user context are injected through `Config`. |
| ESP-IDF component correctness | Medium | Component files exist; no `idf.py build` proof. |
| ESP-IDF example correctness | Good | Native IDF APIs and fixed buffers; no Arduino facade found. |
| Status/error model | Good | Precise status enum and transport mapping exist; WP/persistence remains semantic, not detectable by ACK. |
| Timing/determinism | Good | No conversion waits; memory APIs are bounded by transaction count and `i2cTimeoutMs`. |
| Device-specific correctness | Good | Variant-aware addressing and no EEPROM write delay assumption are strong. |
| Partial hardware state handling | Medium | Multi-chunk writes can partially commit with no byte-count/dirty report. |
| Health/recovery behavior | Good | Fake tests cover recovery/offline; `tick()` is no-op except health timestamps. |
| Thread/ISR contract | Medium | README says not thread-safe; public headers should be clearer. |
| Tests/fault injection | Good | 73 native tests with fake bus/fault paths. |
| ESP-IDF build coverage | Weak | Pure IDF build missing locally and in CI. |
| Arduino ESP32-S2/S3 readiness | Good | `esp32s3dev` and `esp32s2dev` PlatformIO builds passed. |
| Documentation honesty | Good | README documents WP and current-address caveats; partial-write semantics need more detail. |
| Hardware validation | Unknown | No hardware commands were run in this audit. |

## What Is Strong

- Core is framework-neutral and uses injected transport callbacks (`include/MB85RC/Config.h:34`, `include/MB85RC/Config.h:46`, `include/MB85RC/Config.h:58`).
- Device variant metadata and address models are centralized in `include/MB85RC/CommandTable.h`.
- Read/write/fill/verify operations reject out-of-range accesses before bus traffic (`README.md:202`, `src/MB85RC.cpp:382`).
- FRAM writes do not add EEPROM-style delays (`src/MB85RC.cpp:671`).
- Native tests passed 73/73 and include fake transport, variant simulation, read/write failure injection, Device-ID faults, recovery, and offline behavior.
- The IDF example uses native IDF I2C APIs and maps `esp_err_t` to `Status` (`examples/espidf_basic/main/main.cpp:13`, `examples/espidf_basic/main/main.cpp:52`, `examples/espidf_basic/main/main.cpp:900`).

## High-Severity Findings

### H1. Pure ESP-IDF build is not validated locally or in CI

Severity: High

Evidence:
- `CMakeLists.txt`, `idf_component.yml`, and `examples/espidf_basic/` exist on `main`.
- CI runs PlatformIO build/test/package flows but no `idf.py build` job was found.
- Local command `idf.py --version` failed: `The term 'idf.py' is not recognized as the name of a cmdlet, function, script file, or operable program.`

Impact:
- A native ESP-IDF consumer can receive a component that passes PlatformIO but fails under IDF CMake, component dependency resolution, or target-specific IDF settings.

Recommended remediation:
- Add CI jobs:
  - `idf.py -C examples/espidf_basic set-target esp32s3 build`
  - `idf.py -C examples/espidf_basic set-target esp32s2 build`
- Record ESP-IDF version in the final hardening report.

Suggested tests:
- Pure IDF S2/S3 build matrix.
- Negative contract check that no Arduino symbols enter the IDF example.

### H2. Multi-chunk write/fill operations can partially commit without reporting committed extent

Severity: High

Evidence:
- `write()` chunks large writes and returns immediately on the first failing chunk (`src/MB85RC.cpp:386`, `src/MB85RC.cpp:396`, `src/MB85RC.cpp:397`).
- `_writeMemory()` updates current-address state only if the transaction succeeds (`src/MB85RC.cpp:693`, `src/MB85RC.cpp:695`, `src/MB85RC.cpp:697`).
- No public result reports how many bytes were committed before failure.

Impact:
- For data logging or configuration storage, a caller may know the operation failed but not which prefix of the region was written. Recovery then requires a full read/verify or application-level journaling.

Recommended remediation:
- Document `write()` and `fill()` as non-atomic across chunks.
- Add optional extended result APIs or diagnostics with `bytesAttempted` / `bytesCommitted`.
- Recommend `verify()` or application-level journaling for critical data.

Suggested tests:
- Fail on each chunk position and verify prefix bytes changed, suffix bytes unchanged, and diagnostics are exact.
- Test power-loss-style partial write behavior with a fake backing store.

## Medium-Severity Findings

### M1. Hardware WP can make ACK-only write success misleading

Severity: Medium

Evidence:
- `write()` accepts bus success as write success (`src/MB85RC.cpp:375`, `src/MB85RC.cpp:396`).
- `_writeMemory()` performs a normal I2C write and returns its status (`src/MB85RC.cpp:671`, `src/MB85RC.cpp:693`, `src/MB85RC.cpp:699`).
- Local docs state WP high protects the entire memory array while reads still work (`docs/MB85RC256V_fram_implementation_manual.md:287`, `docs/MB85RC256V_fram_implementation_manual.md:289`, `docs/MB85RC256V_fram_implementation_manual.md:523`).

Impact:
- If a board holds WP high, the bus transaction may not be enough to prove data was persisted. Applications can falsely assume durable writes unless they verify.

Recommended remediation:
- Keep `write()` semantics but document that ACK means transaction accepted, not persistence under WP.
- Promote `verify()` after critical writes.
- Consider a convenience `writeVerify()` API or example command for production provisioning.

Suggested tests:
- Fake WP mode where writes ACK but backing store does not change.
- `writeVerify()` or application-level verify tests when added.

### M2. Public copy/move operations are not explicitly disabled

Severity: Medium

Evidence:
- `class MB85RC` starts at `include/MB85RC/MB85RC.h:72`.
- No deleted copy or move constructor/assignment was found.
- The instance stores callback pointers, user context, cached variant, current address, and health counters.

Impact:
- Accidental copies can duplicate state while sharing a transport context, causing misleading current-address and health diagnostics.

Recommended remediation:
- Delete copy and move operations explicitly.

Suggested tests:
- Add compile-time `static_assert` checks for non-copyable and non-movable driver objects.

### M3. IDF diagnostic CLI can starve `tick()`

Severity: Medium

Evidence:
- The IDF CLI loop prints a prompt, then blocks in `fgets()` before calling `gFram.tick()` (`examples/espidf_basic/main/main.cpp:910`, `examples/espidf_basic/main/main.cpp:912`, `examples/espidf_basic/main/main.cpp:915`).

Impact:
- `tick()` is currently a no-op except timestamps, so this is not an immediate driver failure. It is still a bad production example pattern for a managed driver family because future tick-driven behavior would be starved.

Recommended remediation:
- Make IDF CLI input nonblocking or move stdin to a task/queue pattern like SHT3x.
- Label the CLI as diagnostic, not a production event loop.

Suggested tests:
- Contract check that IDF CLIs with `tick()` do not block before ticking.

## Low-Severity Findings

### L1. `readCurrentAddress(buf, len)` performs one transaction per byte

Severity: Low

Evidence:
- `readCurrentAddress()` loops over every byte and issues a read-only transaction per byte (`src/MB85RC.cpp:488`, `src/MB85RC.cpp:494`, `src/MB85RC.cpp:502`).

Impact:
- This is faithful to current-address semantics but can monopolize the bus for large `len` and surprise users expecting a sequential burst.

Recommended remediation:
- Keep behavior, but document transaction count prominently.
- Recommend `read(address, buf, len)` for bulk reads.

Suggested tests:
- Transaction-count test for `readCurrentAddress()`.

### L2. Dedicated IDF example contract is not run directly in CI

Severity: Low

Evidence:
- `tools/check_idf_example_contract.py` exists and passes locally.
- CI indirectly invokes it through `tools/check_cli_contract.py` (`tools/check_cli_contract.py:169`), but a direct CI step is clearer.

Impact:
- Future CI edits can unintentionally drop the stronger IDF boundary check.

Recommended remediation:
- Add a direct CI step: `python tools/check_idf_example_contract.py`.

Suggested tests:
- CI-only guard.

## Device-Specific Correctness Checklist

| Item | Status | Notes |
| --- | --- | --- |
| Supported part table/capacity | PASS | Variant table and capacity checks exist. |
| Address-byte protocol variants | PASS | One-byte, two-byte, and A16-in-address variants are modeled. |
| Device ID protocol | PASS/PARTIAL | Supported where part implements Device ID; no-ID variants require explicit selection. |
| WP behavior | PARTIAL | Documented, but write APIs cannot prove persistence without verify. |
| No EEPROM-style write delay | PASS | No write delay is added. |
| Sequential wraparound boundaries | PASS | Bulk ranges reject cross-capacity writes/reads. |
| Chunking across transport limits | PASS | Reads/writes/fills are chunked. |
| High-speed mode | NOT IMPLEMENTED | Do not claim HS mode validation. |
| Sleep mode | UNKNOWN | Not validated in this audit. |
| Data retention/endurance docs | PASS | Datasheets and docs exist. |
| Power loss during write | PARTIAL | Needs application-level atomicity/journaling guidance. |
| Hardware validation | UNKNOWN | No hardware commands were run. |

## API Latency / Blocking Model

| API | I2C transactions | Other waits | Worst-case bound | Notes |
| --- | ---: | --- | --- | --- |
| `begin()` | 1 Device-ID write-read, or 1 memory read for explicit no-ID variants | None | `i2cTimeoutMs` for one transaction | Device-ID availability depends on variant. |
| `tick()` | 0 | None | O(1) | No-op except time/health timestamp support. |
| `read(addr, len)` | `ceil(len / 128)` | None | Per transaction bounded by `i2cTimeoutMs` | Random-read chunking. |
| `write(addr, len)` | `ceil(len / 126)` | None | Per transaction bounded by `i2cTimeoutMs` | Non-atomic across chunks. |
| `fill(addr, len)` | `ceil(len / 64)` | None | Per transaction bounded by `i2cTimeoutMs` | Non-atomic across chunks. |
| `verify(addr, expected, len)` | `ceil(len / 128)` reads | CPU compare | Per transaction bounded by `i2cTimeoutMs` | Reports first mismatch. |
| `readCurrentAddress(buf, len)` | `len` read-only transactions | None | `len * i2cTimeoutMs` worst case | Use addressed `read()` for bulk. |
| `probe()` | 1 diagnostic transaction | None | `i2cTimeoutMs` | Diagnostic semantics. |
| `recover()` | Device-ID or probe transaction | None | `i2cTimeoutMs` | Rebuilds active state. |

## Partial-State / Cache Consistency Assessment

There are no device configuration registers to keep in sync, so the main partial-state risk is memory content. Multi-chunk writes/fills can commit a prefix and fail on a later chunk. The current address cache is invalidated on transport failure, which is good, but the public API does not report committed length. For critical data, callers need `verify()` or an application-level journal until an extended result API exists.

## Tests and Build Coverage

Run locally:
- `git status --short`: clean before report edits.
- `python --version`: `Python 3.13.12`.
- `python -m platformio --version`: `PlatformIO Core, version 6.1.19`.
- `python tools/check_core_timing_guard.py`: PASS, `Core timing guard PASSED`.
- `python tools/check_cli_contract.py`: PASS, `CLI contract PASSED`.
- `python tools/check_idf_example_contract.py`: PASS, `IDF example contract PASSED`.
- `python scripts/generate_version.py check`: PASS, `include\MB85RC\Version.h` up to date.
- `python -m platformio test -e native`: PASS, `73 test cases: 73 succeeded`.
- `python -m platformio run -e esp32s3dev`: PASS, `SUCCESS`.
- `python -m platformio run -e esp32s2dev`: PASS, `SUCCESS`.
- `python -m platformio pkg pack`: PASS, wrote `MB85RC-2.0.0.tar.gz`; tarball removed after audit.
- `idf.py --version`: FAIL, command not found.

Present in CI:
- PlatformIO S2/S3 builds.
- Native tests.
- Version check.
- Core timing guard.
- CLI contract.
- `pio pkg pack`.

Not run:
- `idf.py -C examples/espidf_basic set-target esp32s3 build`: not run because `idf.py` is unavailable.
- `idf.py -C examples/espidf_basic set-target esp32s2 build`: not run because `idf.py` is unavailable.
- Hardware validation: not run.

Missing:
- Pure IDF build in CI.
- Direct CI invocation of `tools/check_idf_example_contract.py`.
- Native test for WP-high ACK/no-write behavior.
- Native test/reporting for partial multi-chunk write extent.

## ESP-IDF Port Assessment

- Pure ESP-IDF component: Present.
- Pure ESP-IDF example: Present at `examples/espidf_basic`.
- Native IDF APIs, not Arduino: Yes, uses `app_main`, `driver/i2c_master.h`, `esp_timer`, FreeRTOS delay, and fixed buffers.
- External bus ownership: Yes. Example creates/owns IDF bus/device handles.
- Error mapping: Good. `esp_err_t` is mapped into the library `Status`.
- Locking: Not demonstrated. The diagnostic example adds/removes a device handle per transaction and has no shared-bus lock.
- Task/tick behavior: Weak pattern, because `fgets()` can block before `tick()`.
- CI IDF build: Missing.

## Documentation Assessment

Missing or incomplete documentation contracts:
- Explicit public-header thread/ISR contract.
- Write/fill non-atomicity and partial committed prefix behavior.
- WP warning tied directly to `write()` semantics, not only hardware notes.
- Hardware validation matrix by variant and address pins.
- Pure IDF build status and exact ESP-IDF version.

## Hardware Validation Needed

| Scenario | Target | Expected evidence |
| --- | --- | --- |
| Device-ID variants | MB85RC64/256/512/1MT as available | Correct manufacturer/product ID and capacity. |
| No-ID variant | MB85RC16V if available | Explicit variant selection and safe presence check. |
| Address pin variants | A0/A1/A2 combinations | Correct active I2C address and memory address encoding. |
| Bulk read/write/fill | Each density | No wraparound, verified content. |
| WP high | Board with WP control | Writes do not persist; verify detects mismatch. |
| Partial write failure | Fake or fault jig | Prefix semantics documented and tested. |
| Shared bus contention | Production bus manager | External lock prevents concurrent callbacks. |
| Pure IDF CLI | ESP32-S2/S3 | `probe`, `selftest`, `stress`, destructive confirmation behavior. |

## Recommended Implementation Plan

### P0 - Must fix before production claim

- Add pure ESP-IDF S2/S3 CI builds.
- Document and test non-atomic multi-chunk writes/fills.
- Add WP-high persistence tests or a `writeVerify()` production helper.
- Delete copy/move operations.

### P1 - Should fix before release/merge

- Make IDF CLI input nonblocking or isolate it in an input task.
- Add direct CI step for `tools/check_idf_example_contract.py`.
- Add public-header thread/ISR contract.
- Add partial-write committed-prefix diagnostics or extended result APIs.

### P2 - Nice hardening / later

- Add sleep/high-speed support only if backed by datasheet tests and hardware validation.
- Add a production storage pattern example using journaling and `verify()`.

## Proposed Branch for Future Hardening

`hardening/mb85rc-industry-readiness`

## Final Verdict

Not industry-grade yet, but close enough for a focused hardening pass. It is one of the easiest qualifying repositories to harden next because the architecture and native fake tests are already strong.
