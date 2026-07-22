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
- The one-hour broad run used the earlier 50 ms example-controller timeout.
  The post-fix qualification used a 5 ms timeout, TX capacity 126, RX capacity
  124, and therefore 124-byte read/write data limits.
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

## Runs

| Date | Fixture | Duration | Gate | Functional | Soak | Final health | Heap | Notes |
| --- | --- | ---: | --- | --- | --- | --- | --- | --- |
| 2026-06-22/23 | MB85RC64TA | 8 h | non-strict | 26 PASS / 0 FAIL / 0 UNKNOWN | 53191 PASS / 0 FAIL / 30 UNKNOWN | READY, consecutive failures 0, total failures 0 | not exposed | Completed full duration with bounded serial/prompt UNKNOWN windows. |
| 2026-06-23 | MB85RC64TA | 2 min | strict PASS | 29 PASS / 0 FAIL / 0 UNKNOWN | 219 PASS / 0 FAIL / 0 UNKNOWN | READY, consecutive failures 0, total failures 0 | baseline 344040, final 343784, min 340976 | Strict runner, heap command present, no resets or reconnects. |
| 2026-06-23/24 | MB85RC64TA | 20 h | strict FAIL | 29 PASS / 0 FAIL / 0 UNKNOWN | 142816 PASS / 0 FAIL / 69 UNKNOWN | READY, consecutive failures 0, total failures 0 | baseline 344040, final 343784, min 340976 | Strict failed only because UNKNOWN count was nonzero; no FAIL, no target reset, no serial reconnect. |
| 2026-06-26/28 | MB85RC256V v3.0.0 | 48 h | strict FAIL | 29 PASS / 0 FAIL / 0 UNKNOWN | 316112 PASS / 0 FAIL / 154 UNKNOWN | READY, consecutive failures 0, total failures 0 | baseline 344040, final 343784, min 340976 | Strict failed because UNKNOWN count was nonzero; no FAIL, target reset, or serial reconnect. |
| 2026-07-22 | MB85RC64TA COM20, broad envelope | 1 h | strict PASS | 31 PASS / 0 FAIL / 0 UNKNOWN | 7372 PASS / 0 FAIL / 0 UNKNOWN | READY, 120769 successes, consecutive failures 0, total failures 0 | baseline/final 343012, min 340204, largest final 278516 | Zero target resets/reconnects; worst command 0.875 s; 31 bounded read-only framing syncs; full-chip CRC preserved. |
| 2026-07-22 | MB85RC64TA COM20, 5 ms/124-byte envelope | 5 min | strict PASS | 32 PASS / 0 FAIL / 0 UNKNOWN | 610 PASS / 0 FAIL / 0 UNKNOWN | READY, consecutive failures 0, total failures 0 | baseline/final 343012, min 340204, largest final 278516 | Zero target resets/reconnects; worst command 0.750 s; four framing syncs; full-chip CRC preserved. |

The exact-envelope soak was preceded by a strict 34-check functional run that
also included 200-cycle backed-up/restored `stress` and `stress_mix` commands.
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
