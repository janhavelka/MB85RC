# HIL Evidence Summary

This file preserves concise facts from historical HIL runs whose raw runner
artifacts are not part of the repository. Results apply only to the named
fixture and the recorded revision, when available; they do not qualify later
code, another device variant, or another product target.

## MB85RC64TA fixture

- Host: Windows 11, Python 3.12.10, PySerial 3.5.
- Port: `COM27`, baud `115200`.
- Firmware profile: Arduino `examples/01_basic_bringup_cli`.
- MCU/board: ESP32-S3, PlatformIO `esp32s3dev`.
- I2C: SDA 8, SCL 9, 400 kHz, 50 ms Wire timeout.
- Detected I2C devices during the first run: `0x3C`, `0x50`, `0x51`.
- Detected FRAM on all preserved runs: `MB85RC64TA`, Device ID raw `00 A3 58`,
  manufacturer `0x00A`, product `0x358`, capacity 8192 bytes.

These runs are evidence for this MB85RC64TA ESP32-S3 fixture only. They are not
MB85RC256V production-readiness evidence.

### 2026-07-22 COM20 qualification

- Host port: `COM20`, baud `115200`; Arduino pinned ESP32-S3 build from the
  v4.0.0 worktree at `219a6ab`.
- MCU reported by esptool: ESP32-S3 QFN56 revision 0.2, 4 MB embedded flash,
  2 MB embedded PSRAM. This generic fixture is not the production
  TunnelMonitor-node N16R8 target.
- FRAM: `MB85RC64TA`, address `0x50`, manufacturer `0x00A`, product `0x358`,
  8192 bytes. The shared bus also contained devices at `0x3C` and `0x51`.
- The one-hour broad run used a 50 ms example-controller timeout. The later run
  declared 5 ms in `Config`, TX capacity 126, RX capacity 124, and therefore
  124-byte read/write data limits, but the startup scanner silently reset the
  physical Wire timeout to 50 ms. Its 124-byte result remains valid; its 5 ms
  controller-timeout claim is invalidated by the 2026-07-31 audit.
- Full-capacity CRC32 was `0xE30F00B8` before the run, after the one-hour run,
  after the exact-envelope run, and after the application reboot check.
- Raw transcripts and runner JSON/Markdown reports remain local under
  `.pio/hil/`; they are intentionally not release artifacts.

## MB85RC256V fixture

- Port: `COM5`, baud `115200`.
- Firmware profile: Arduino `examples/01_basic_bringup_cli`, library v3.0.0 at
  `f0294b0`.
- MCU/board: ESP32-S3, PlatformIO `esp32s3dev`.
- I2C: address `0x50`, 400 kHz, 50 ms Wire timeout.
- Detected FRAM: `MB85RC256V`, Device ID raw `00 A5 10`, manufacturer
  `0x00A`, product `0x510`, capacity 32768 bytes.

This supports basic v3.0.0 operation on that fixture but is not an immutable
release artifact. It does not exercise the later passive-core API,
external-owner shared-bus scheduling, write-timeout reconciliation, WP-high
behavior, or controlled power loss.

### 2026-07-31 COM4 pioarduino 55.03.311 platform-upgrade regression

- Host port: `COM4`, baud `115200`; Arduino ESP32-S3 build from the dirty
  `ffcb95e` worktree containing the pioarduino migration.
- Toolchain: PlatformIO 6.1.19, pioarduino Espressif platform 55.03.311,
  Arduino-ESP32 3.3.11, ESP-IDF v5.5.5, and esptool 5.3.0.
- MCU reported by esptool: ESP32-S3 QFN56 revision 0.1, 4 MB embedded XMC
  flash, 2 MB embedded PSRAM, and USB Serial/JTAG.
- FRAM: `MB85RC256V`, address `0x50`, manufacturer `0x00A`, product `0x510`,
  capacity 32768 bytes. The declared envelope was a 5 ms timeout, 126-byte TX,
  124-byte RX, and 124-byte read/write data limits. The startup scanner then
  reset Wire to 50 ms, so this run did not prove the 5 ms physical timeout.
- The strict restore-safe run passed all 34 functional checks, including 500
  random writes plus reads, 500 stress cycles, and 500 mixed-operation cycles.
  Its 300.2-second soak passed 770/770 commands with no FAIL or UNKNOWN result.
- Final health was READY with 15920 successes, zero failures, and no last
  error. Free heap was unchanged at 340016 bytes; observed minimum free heap
  was 334712 bytes and final largest block was 278516 bytes.
- No target reset, serial reconnect, or read-only framing sync occurred after
  boot. Raw transcript, JSON, and Markdown evidence remain local under
  `.pio/hil/55.03.311-COM4*`.
- This is platform-upgrade regression evidence, not production hardware
  qualification. The five-minute run does not record the FRAM package/date
  code, supply voltage, pull-ups, address straps, or WP wiring required by the
  production hardware matrix.

### 2026-07-31 COM4 scanner-timeout cleanup regression

- Firmware was rebuilt and flashed from the dirty `d31d2b4` worktree containing
  the documented cleanup. The scanner no longer calls `Wire.setTimeOut()`, so
  the example owner retains the 5 ms controller setting established by
  `BoardConfig`; the run did not inject a stuck-bus timeout to measure that
  deadline externally.
- Runtime gates again observed Arduino-ESP32 `3.3.11`, ESP-IDF `v5.5.5`,
  `MB85RC256V`, product `0x510`, 32768-byte capacity, declared 5 ms timeout,
  126-byte TX, 124-byte RX, and 124-byte read/write data limits.
- All 34 functional checks passed, including 500 random writes plus reads, 500
  backed-up/restored stress cycles, and 500 mixed-operation cycles. The 300.2 s
  soak passed 771/771 commands with no failure, unknown, reset, reconnect, or
  framing sync.
- Final health was READY with 15921 total successes, zero consecutive or total
  failures, and no last error. Heap changed from 340148 to 339988 bytes (160-byte
  drop), with 334720-byte observed minimum and 278516-byte final largest block.
- Raw evidence is local under `.pio/hil/55.03.311-cleanup-COM4*`. This remains
  dirty-worktree regression evidence, not immutable release qualification.

## Runs

| Date | Fixture | Duration | Gate | Functional | Soak | Final health | Heap | Notes |
| --- | --- | ---: | --- | --- | --- | --- | --- | --- |
| 2026-06-22/23 | MB85RC64TA | 8 h | non-strict | 26 PASS / 0 FAIL / 0 UNKNOWN | 53191 PASS / 0 FAIL / 30 UNKNOWN | READY, consecutive failures 0, total failures 0 | not exposed | Completed full duration with bounded serial/prompt UNKNOWN windows. |
| 2026-06-23 | MB85RC64TA | 2 min | strict PASS | 29 PASS / 0 FAIL / 0 UNKNOWN | 219 PASS / 0 FAIL / 0 UNKNOWN | READY, consecutive failures 0, total failures 0 | baseline 344040, final 343784, min 340976 | Strict runner, heap command present, no resets or reconnects. |
| 2026-06-23/24 | MB85RC64TA | 20 h | strict FAIL | 29 PASS / 0 FAIL / 0 UNKNOWN | 142816 PASS / 0 FAIL / 69 UNKNOWN | READY, consecutive failures 0, total failures 0 | baseline 344040, final 343784, min 340976 | Strict failed only because UNKNOWN count was nonzero; no FAIL, no target reset, no serial reconnect. |
| 2026-06-26/28 | MB85RC256V v3.0.0 | 48 h | strict FAIL | 29 PASS / 0 FAIL / 0 UNKNOWN | 316112 PASS / 0 FAIL / 154 UNKNOWN | READY, consecutive failures 0, total failures 0 | baseline 344040, final 343784, min 340976 | Strict failed because UNKNOWN count was nonzero; no FAIL, target reset, or serial reconnect. |
| 2026-07-22 | MB85RC64TA COM20, broad envelope | 1 h | strict PASS | 31 PASS / 0 FAIL / 0 UNKNOWN | 7372 PASS / 0 FAIL / 0 UNKNOWN | READY, 120769 successes, consecutive failures 0, total failures 0 | baseline/final 343012, min 340204, largest final 278516 | Zero target resets/reconnects; worst command 0.875 s; 31 bounded read-only framing syncs; full-chip CRC preserved. |
| 2026-07-22 | MB85RC64TA COM20, configured 5 ms / actual 50 ms, 124-byte envelope | 5 min | strict PASS except invalidated timeout claim | 32 PASS / 0 FAIL / 0 UNKNOWN | 610 PASS / 0 FAIL / 0 UNKNOWN | READY, consecutive failures 0, total failures 0 | baseline/final 343012, min 340204, largest final 278516 | 124-byte and restore/CRC evidence retained; scanner bug invalidates only the physical 5 ms timeout claim. |
| 2026-07-31 | MB85RC256V COM4, pioarduino 55.03.311 regression, actual 50 ms | 5 min | strict PASS except invalidated timeout claim | 34 PASS / 0 FAIL / 0 UNKNOWN | 770 PASS / 0 FAIL / 0 UNKNOWN | READY, 15920 successes, consecutive failures 0, total failures 0 | baseline/final 340016, min 334712, largest final 278516 | Platform/framework and functional regression evidence retained; scanner bug invalidates only the physical 5 ms timeout claim. |
| 2026-07-31 | MB85RC256V COM4, scanner-timeout cleanup | 5 min | strict PASS | 34 PASS / 0 FAIL / 0 UNKNOWN | 771 PASS / 0 FAIL / 0 UNKNOWN | READY, 15921 successes, consecutive failures 0, total failures 0 | baseline 340148, final 339988, drop 160, min 334720, largest final 278516 | Scanner preserved the owner's 5 ms Wire setting; zero target resets/reconnects/framing syncs; exact Arduino 3.3.11 and IDF v5.5.5 gates passed. |

The 2026-07-22 COM20 exact-envelope soak was preceded by a strict 34-check
functional run that also included 200-cycle backed-up/restored `stress` and
`stress_mix` commands.
A separate application-reset run passed 32 checks and preserved the same
full-chip CRC. High-speed/Sleep electrical behavior, WP-high, device removal,
and controlled power-loss cases were not run because this fixture provides no
safe authority for those hardware manipulations.

## UNKNOWN results

The 20-hour strict soak classified 69 command windows as UNKNOWN. The runner
continued without reconnecting, later command cycles passed, final driver health
was clean, and firmware-level pass/fail counters stayed at zero after the next
complete command output.

| Command | Count | Interpretation |
| --- | ---: | --- |
| `probe` | 36 | Host serial/prompt capture timed out before a complete command window. |
| `xfer_demo` | 19 | Partial restore-safe PASS output was captured, then the command window timed out. Later cycles passed and restored the scratch range. |
| `rw_suite` | 11 | Partial PASS output was captured, then the command window timed out. Later cycles passed. |
| `crc` | 2 | Host capture timeout. |
| `drv` | 1 | Host capture timeout. |

The practical diagnosis was PC-side serial/prompt framing under long soak load,
not a demonstrated FRAM data, firmware, reset, or driver-health failure.

The later 48-hour MB85RC256V run likewise ended with 154 UNKNOWN command
windows and no target failure, reset, reconnect, or health failure. Because the
strict gate requires zero UNKNOWN results, both long soaks remain strict-gate
failures and are not release qualification. Current qualification requirements
are maintained in the [README hardware matrix](../../README.md#hardware-validation-matrix)
and [release checklist](../RELEASE_CHECKLIST.md), rather than duplicated here.
