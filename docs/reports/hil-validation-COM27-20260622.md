# MB85RC HIL Validation Report - COM27 - 2026-06-22

Date/time: 2026-06-22 20:38 to 2026-06-23 04:53 Europe/Prague (+02:00)  
Repository: `C:\Users\Honza\Documents\Projects\MB85RC`  
Branch: `hardening/mb85rc-industry-readiness`  
Commit at start: `e20c7757925a67aa484791d2abee0de9f5a9b2a3`  
Initial dirty status: clean. Final dirty status: HIL runner, example CLI, docs, contract scripts, and report artifacts changed.  
Host OS: Microsoft Windows 11 Education  
Python: 3.12.10. PySerial: 3.5. Git: 2.52.0.windows.1. Doxygen: 1.13.2.  
PlatformIO: Core 6.1.18. PlatformIO warned that multiple core versions exist and 6.1.18 is obsolete versus 6.1.19.

This report is evidence for this fixture only. It is not a production-readiness claim.

## Hardware And Firmware

| Item | Value |
| --- | --- |
| Upload port | `COM27` |
| HIL serial port | `COM27` |
| Baud | `115200` |
| Board/environment used | PlatformIO `esp32s3dev`, board `esp32-s3-devkitc-1`, Arduino framework |
| Upload target detected | ESP32-S3, MAC `1c:db:d4:80:c9:5c` |
| Example firmware | `examples/01_basic_bringup_cli` |
| I2C pins/frequency | SDA 8, SCL 9, 400 kHz, 50 ms Wire timeout |
| Detected I2C devices | `0x3C`, `0x50`, `0x51` |
| Detected FRAM | `MB85RC64TA` at configured address `0x50` |
| Device ID | Manufacturer `0x00A`, product `0x358`, raw `00 A3 58` |
| Active capacity | 8192 bytes, max address `0x1FFF` |
| Fixture assumptions | Existing safe bench fixture, no WP control, no power switching, no disconnect/fault injection, no current measurement |

Note: the repository target text still references MB85RC256V, but the connected hardware identified itself as MB85RC64TA. MB85RC256V-specific HIL is therefore NOT RUN in this report.

## Artifacts

| Artifact | Path |
| --- | --- |
| 8-hour raw transcript | `docs/reports/hil-runner-COM27-20260622-8h-transcript.txt` |
| 8-hour JSON results | `docs/reports/hil-runner-COM27-20260622-8h.json` |
| 8-hour runner markdown | `docs/reports/hil-runner-COM27-20260622-8h.md` |
| 60-second rehearsal JSON | `docs/reports/hil-runner-COM27-20260622.json` |
| This report | `docs/reports/hil-validation-COM27-20260622.md` |

## Exact Commands

Environment and setup:

```powershell
git status --porcelain=v1
git branch --show-current
git rev-parse HEAD
Get-Date -Format "yyyy-MM-dd HH:mm:ss K"
python --version
python -m platformio --version
python -m platformio device list
```

Build, flash, and functional HIL:

```powershell
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s3dev -t upload --upload-port COM27
python tools\hil_runner.py --port COM27 --baud 115200 --timeout-s 12 --idle-timeout-s 0.35 --boot-settle-s 1 --sample-count 50
```

Soak rehearsal and 8-hour soak:

```powershell
python tools\hil_runner.py --port COM27 --baud 115200 --timeout-s 12 --idle-timeout-s 0.35 --boot-settle-s 1 --sample-count 50 --soak-duration-s 60 --soak-pacing-s 0.05
python tools\hil_runner.py --port COM27 --baud 115200 --timeout-s 12 --idle-timeout-s 0.35 --boot-settle-s 1 --sample-count 50 --soak-duration-s 28800 --soak-pacing-s 0.05 --transcript-path docs\reports\hil-runner-COM27-20260622-8h-transcript.txt --json-path docs\reports\hil-runner-COM27-20260622-8h.json --markdown-path docs\reports\hil-runner-COM27-20260622-8h.md
```

Final verification:

```powershell
python tools\hil_runner.py --parser-self-test
python tools\hil_runner.py --dry-run --port COM27 --baud 115200 --timeout-s 5 --soak-duration-s 28800
python tools\check_cli_contract.py
python tools\check_core_timing_guard.py
python scripts\generate_version.py check
python tools\check_metadata_consistency.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack
git diff --check
```

Additional command notes:

- `python -m platformio pkg list -e esp32s3dev` initially failed with a Windows `cp1252` `UnicodeEncodeError`. It passed with `PYTHONIOENCODING=utf-8`.
- `idf.py --version` failed because `idf.py` is not on PATH; ESP-IDF HIL/build validation is NOT RUN locally.
- `MB85RC-3.0.0.tar.gz` from `pio pkg pack` was removed after the package check.

## Summary

| Area | PASS | FAIL | UNKNOWN | NOT RUN | Notes |
| --- | ---: | ---: | ---: | ---: | --- |
| Functional HIL prelude | 26 | 0 | 0 | 0 | Bounded serial CLI suite after upload |
| 60-second soak rehearsal | 112 | 0 | 0 | 0 | Used to validate runner prompt timing |
| 8-hour soak | 53191 | 0 | 30 | 0 | Completed 28800.2 s; UNKNOWNs were bounded command-window/prompt timeouts |
| Hardware fault injection groups | 0 | 0 | 0 | 10 | No safe fixture for disconnect, WP, power cycle, straps, 3.4 MHz, current measurement, or MB85RC256V |

The 8-hour run completed the full requested duration but is classified here as COMPLETED_WITH_UNKNOWN_ANOMALIES, not a clean all-pass result, because 30 of 53221 soak command windows hit bounded serial/prompt timeouts. No data mismatch, driver health failure, target reboot, serial reconnect, or persistent offline state was observed.

## Functional HIL Detail

| Test ID | Area | Command | Result | Elapsed s | Observed summary |
| --- | --- | --- | --- | ---: | --- |
| HIL-001 | connectivity | `version` | PASS | 0.375 | Version `3.0.0`, commit `e20c775`, firmware marked dirty due local edits |
| HIL-002 | connectivity | `scan` | PASS | 0.485 | I2C devices at `0x3C`, `0x50`, `0x51` |
| HIL-003 | state | `settings` | PASS | 0.375 | Initialized true, READY, address `0x50`, variant `MB85RC64TA` |
| HIL-004 | state | `drv` | PASS | 0.375 | READY, online yes, 0 failures |
| HIL-005 | identity | `id` | PASS | 0.375 | Manufacturer `0x00A`, product `0x358`, `MB85RC64TA` |
| HIL-006 | identity | `idraw` | PASS | 0.375 | Raw Device ID `00 A3 58` |
| HIL-007 | identity | `variants` | PASS | 0.375 | Variant catalog printed |
| HIL-008 | memory | `size` | PASS | 0.359 | Active capacity 8192 bytes, max `0x1FFF` |
| HIL-009 | diagnostics | `probe` | PASS | 0.375 | Status OK, no health tracking expected |
| HIL-010 | memory | `read 0x0000 16` | PASS | 0.375 | 16 bytes read from address 0 |
| HIL-011 | memory | `current 1` | PASS | 0.375 | Current-address read followed addressed read |
| HIL-012 | memory | `text 0x0000 16` | PASS | 0.375 | Text/escaped view printed |
| HIL-013 | memory | `crc 0x0000 64` | PASS | 0.375 | CRC32 `0x758D6336` |
| HIL-014 | modes | `hs support` | PASS | 0.359 | Variant supports HS; Arduino transport does not prove 3.4 MHz |
| HIL-015 | modes | `hs enter` | PASS | 0.375 | Returned visible unsupported/config status for Arduino transport path |
| HIL-016 | modes | `sleep support` | PASS | 0.375 | Variant supports Sleep; diagnostic notes no hidden delay |
| HIL-017 | modes | `sleep enter` | PASS | 0.375 | Returned `INVALID_CONFIG` because Arduino special callback is absent |
| HIL-018 | recovery | `recover` | PASS | 0.375 | Status OK, READY, failures 0 |
| HIL-019 | validation | `definitely_not_a_command` | PASS | 0.360 | Unknown command visible |
| HIL-020 | validation | `read 0xFFFFFFFF 1` | PASS | 0.375 | CLI rejected malformed/out-of-range read with usage text |
| HIL-021 | diagnostics | `selftest` | PASS | 0.375 | Diagnostic self-test all checks passed |
| HIL-022 | memory | `rw_suite` | PASS | 0.390 | Scratch, fill, and tail regions backed up and restored |
| HIL-023 | staged | `xfer_demo` | PASS | 0.407 | Staged read/write/fill/verify, zero-budget, busy behavior, restore all passed |
| HIL-024 | data | `typed_demo` | PASS | 0.375 | Fixed-width typed demo and restore passed |
| HIL-025 | timing | `randbench 50` | PASS | 0.468 | 50 random writes and 50 random reads, final verify PASS |
| HIL-026 | state | `drv` | PASS | 0.375 | READY, total success 206, total failures 0 |

## 8-Hour Soak Detail

| Metric | Value |
| --- | --- |
| Start | 2026-06-22T20:53:20+02:00 |
| End | 2026-06-23T04:53:20+02:00 |
| Duration | 28800.2 seconds |
| Command counts | `drv` 4839, `id` 4839, `read` 4839, `crc` 4838, `probe` 4838, `recover` 4838, `hs support` 4838, `sleep support` 4838, `rw_suite` 4838, `xfer_demo` 4838, `randbench 50` 4838 |
| Total command windows | 53221 |
| PASS / FAIL / UNKNOWN | 53191 / 0 / 30 |
| Worst consecutive UNKNOWN/FAIL burst | 1 |
| Serial reconnects / target resets | 0 / 0 |
| Recover commands | 4838 |
| Latency min / mean / max | 0.359 s / 0.490 s / 25.047 s |
| Worst explicit `read 0x0000 16` latency | 0.391 s |
| Effective CLI command rate | 1.848 commands/s |
| Random byte write benchmark | 4839 samples of 50 ops: min 6459.95 ops/s, mean 6470.21 ops/s, max 6478.36 ops/s |
| Random byte read benchmark | 4839 samples of 50 ops: min 5015.55 ops/s, mean 5029.10 ops/s, max 5041.85 ops/s |
| Final observed health near end | READY, online yes, consecutive failures 0, total success 769448, total failures 0, last error never |

UNKNOWN details:

| Command | Count | Observed behavior |
| --- | ---: | --- |
| `probe` | 19 | Timed out at 12 s after printing "Probing device"; next `recover` returned OK |
| `xfer_demo` | 8 | Timed out at 25 s with partial PASS output; later cycles passed and restored |
| `rw_suite` | 2 | Timed out at 25 s with partial PASS output; later cycles passed |
| `hs support` | 1 | Timed out at 12 s with no complete prompt capture |

Interpretation: these are serial/host command-window anomalies, not observed FRAM data failures. They were non-consecutive, the CLI resynchronized without reconnect, and the final driver health had zero total failures.

## Coverage And Limitations

| Requirement area | Result |
| --- | --- |
| Serial boot/prompt | PASS after runner set DTR/RTS low to match PlatformIO monitor settings |
| Version/build | PASS |
| Bus scan | PASS |
| Device ID | PASS for connected `MB85RC64TA`; MB85RC256V NOT RUN |
| Settings/health/probe/recover | PASS |
| Current-address read | PASS after addressed read |
| Read/text/CRC/verify paths | PASS for safe read paths and restore suites |
| Write/fill/verify | PASS through `selftest`, `rw_suite`, `typed_demo`, `randbench`, and `xfer_demo` restore windows |
| Staged jobs and budgets | PASS through new `xfer_demo`: zero-budget poll, busy rejection, one-instruction polling, read/write/fill/verify, restore |
| High-speed mode | Diagnostic support PASS; actual 3.4 MHz HS transfer NOT RUN because Arduino transport lacks `i2cSpecial` and no 3.4 MHz fixture validation was available |
| Sleep mode | Diagnostic support PASS; actual Sleep current/entry/wake NOT RUN because Arduino transport lacks `i2cSpecial` and no current/wake fixture was available |
| Wrong address/fault/disconnect | NOT RUN; no safe fault-injection or address-restrap fixture |
| WP-high behavior | NOT RUN; WP pin not controlled by fixture |
| Power cycle/brownout persistence | NOT RUN; no controlled power-cycle fixture |
| ESP-IDF HIL | NOT RUN; `idf.py` not on PATH, firmware used Arduino `esp32s3dev` |
| ESP32-S2 HIL | NOT RUN; build passed, but no S2 target flashed |
| Memory/heap telemetry | NOT RUN; example CLI does not expose heap metrics |

## Fixes Implemented During Run

| Fix | Evidence | Files |
| --- | --- | --- |
| Added bounded host HIL runner with parser self-test, dry-run, transcript, JSON, Markdown, command timeouts, soak mode, and failure-token classification | No existing HIL runner; prompt required HIL automation | `tools/hil_runner.py` |
| Set HIL runner DTR/RTS low on serial open | Initial pyserial attach on COM27 produced no boot transcript; direct probe worked only after DTR/RTS low | `tools/hil_runner.py` |
| Changed prompt detection to require idle after prompt | 60-second rehearsal initially shifted command outputs by one command | `tools/hil_runner.py` |
| Changed future soak summary policy to `UNKNOWN` when any soak command is unknown | 8-hour soak completed with 30 bounded UNKNOWN command windows | `tools/hil_runner.py` |
| Exposed staged transfer API through restore-safe CLI commands | HIL prompt required staged/budgeted job coverage; existing CLI did not expose `request*`/`pollTransfer` | `examples/01_basic_bringup_cli/main.cpp`, `examples/espidf_basic/main/main.cpp` |
| Updated command contract and docs for `xfer_demo`/`xfer_demo!` | Prevents Arduino/ESP-IDF command drift | `tools/check_cli_contract.py`, `tools/check_idf_example_contract.py`, `README.md`, `docs/IDF_PORT.md` |

## Audit Findings

| Severity | Location | Finding | Risk | Simplest safe fix | Native test | HIL regression | Implemented |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Medium | `tools/hil_runner.py` prompt handling | Runner originally accepted prompt too early and shifted command output during the 60-second soak rehearsal | False PASS/UNKNOWN attribution in reports | Require idle after prompt and classify UNKNOWN soak windows | Parser self-test | 60-second soak rehearsal | Yes |
| Medium | `examples/01_basic_bringup_cli/main.cpp`, `examples/espidf_basic/main/main.cpp` | Staged transfer API had native tests but no hardware CLI exposure | HIL could not validate `request*`/`pollTransfer` behavior | Add restore-safe `xfer_demo`/`xfer_demo!` | Existing staged-transfer tests plus CLI contract | Functional HIL `xfer_demo` and soak cycle | Yes |
| Low | `src/MB85RC.cpp:161`, `src/MB85RC.cpp:1349`, `src/MB85RC.cpp:1418` | `Config::i2cAddress` accepts `0x50-0x57` for all variants, while some address models mask low bits for memory transactions and Device ID uses the configured address word directly | User could pass an encoded bank address instead of the base strap and get surprising Device ID/probe behavior | Clarify and validate base-address expectations per variant after selection, or document exact accepted semantics | Explicit variant tests for low-bit address configs on banked variants | Strap/address matrix HIL | No |
| Low | `src/MB85RC.cpp:1593`, `src/MB85RC.cpp:1603`, `src/MB85RC.cpp:1630` | Health counters saturate at max, while repository guidance says counters wrap at max | Long-running diagnostic counters may not match documented behavior | Decide saturate versus wrap and align code/docs | Counter overflow unit test | Long counter soak only if practical | No |
| Low | `src/MB85RC.cpp:1128` | `requestRead`/`requestWrite`/`requestFill`/`requestVerify` queue while asleep or offline; failure is deferred to `pollTransfer()` | Request-time validation wording may imply stronger preflight than actual behavior | Either preflight awake/online state in `_requestTransfer()` or document deferred hardware-state validation | Offline/asleep request tests | HIL sleep/offline staged request test with safe fixture | No |
| Low | `examples/01_basic_bringup_cli/main.cpp:944`, `examples/01_basic_bringup_cli/main.cpp:1006` | Arduino `stress` and `stress_mix` can leave FRAM contents changed | HIL stress can dirty unknown user data | Add restore-safe stress variants or make HIL runner use only restore-safe commands by default | CLI contract and restore tests | HIL restore-safe stress command | Partially: runner avoids destructive stress by default |

## Final Verification Results

| Command | Result |
| --- | --- |
| `python tools\hil_runner.py --parser-self-test` | PASS |
| `python tools\hil_runner.py --dry-run --port COM27 --baud 115200 --timeout-s 5 --soak-duration-s 28800` | PASS |
| `python tools\check_cli_contract.py` | PASS |
| `python tools\check_core_timing_guard.py` | PASS |
| `python scripts\generate_version.py check` | PASS |
| `python tools\check_metadata_consistency.py` | PASS |
| `python -m platformio test -e native` | PASS, 116/116 tests |
| `python -m platformio run -e esp32s3dev` | PASS |
| `python -m platformio run -e esp32s2dev` | PASS |
| `python -m platformio pkg pack` | PASS, generated archive removed |
| `git diff --check` | PASS; Git printed CRLF conversion warnings only |
| `idf.py --version` | NOT RUN / unavailable, `idf.py` not on PATH |

