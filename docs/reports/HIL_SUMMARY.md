# HIL Evidence Summary

This file indexes useful historical HIL facts. The MB85RC64TA facts were
already condensed into this tracked file. The MB85RC256V facts below were
recovered during the 2026-07-19 re-audit from a local ignored runner report;
that lower evidence grade is recorded explicitly. None of these runs qualify
later code or another product target.

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

## Locally recovered MB85RC256V fixture

- Source: local ignored file
  `docs/reports/hil-runner-COM5-MB85RC256V-ESP32S3-20260626-strict-48h.md`,
  8820 bytes, SHA-256
  `46C31C5D80AA90150868F6DAE4B89F8123EBD3735C54249E6B86EE0DC9DCAEEA`.
  The source report, JSON, and transcript are not tracked repository evidence.
- Port: `COM5`, baud `115200`.
- Firmware profile: Arduino `examples/01_basic_bringup_cli`, library v3.0.0 at
  `f0294b0`.
- MCU/board: ESP32-S3, PlatformIO `esp32s3dev`.
- I2C: address `0x50`, 400 kHz, 50 ms Wire timeout.
- Detected FRAM: `MB85RC256V`, Device ID raw `00 A5 10`, manufacturer
  `0x00A`, product `0x510`, capacity 32768 bytes.

The local report supports basic v3.0.0 operation on that fixture but is not an
immutable release artifact. It does not exercise the later passive-core API, a
5 ms transfer timeout, external-owner shared-bus scheduling, write-timeout
reconciliation, WP-high behavior, or controlled power loss.

## Runs

| Date | Duration | Gate | Functional | Soak | Final health | Heap | Notes |
| --- | ---: | --- | --- | --- | --- | --- | --- |
| 2026-06-22/23 | 8 h | non-strict | 26 PASS / 0 FAIL / 0 UNKNOWN | 53191 PASS / 0 FAIL / 30 UNKNOWN | READY, consecutive failures 0, total failures 0 | not exposed | Completed full duration with bounded serial/prompt UNKNOWN windows. |
| 2026-06-23 | 2 min | strict PASS | 29 PASS / 0 FAIL / 0 UNKNOWN | 219 PASS / 0 FAIL / 0 UNKNOWN | READY, consecutive failures 0, total failures 0 | baseline 344040, final 343784, min 340976 | Strict runner, heap command present, no resets or reconnects. |
| 2026-06-23/24 | 20 h | strict FAIL | 29 PASS / 0 FAIL / 0 UNKNOWN | 142816 PASS / 0 FAIL / 69 UNKNOWN | READY, consecutive failures 0, total failures 0 | baseline 344040, final 343784, min 340976 | Strict failed only because UNKNOWN count was nonzero; no FAIL, no target reset, no serial reconnect. |
| 2026-06-26/28 | 48 h | strict FAIL | 29 PASS / 0 FAIL / 0 UNKNOWN | 316112 PASS / 0 FAIL / 154 UNKNOWN | READY, consecutive failures 0, total failures 0 | baseline 344040, final 343784, min 340976 | Local ignored MB85RC256V v3.0.0 report. Strict failed because UNKNOWN count was nonzero; no FAIL, target reset, or serial reconnect. |

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
failures and are not release qualification.

## Production evidence still not run

- ESP32-S2 hardware HIL.
- Native ESP-IDF hardware HIL and local `idf.py` builds; `idf.py` was not on
  PATH during the recorded validation.
- Current passive-core revision HIL on either retained fixture.
- Owner-integrated target hardware with its exact populated FRAM ordering code,
  configured shared-bus speed, production chunk sizes, and physical transfer
  timeout.
- Fault-injection HIL: ambiguous-write reconciliation, wrong address/missing
  device, WP-high behavior, controlled power cycle/brownout, address-strap
  matrix, and shared-bus load.
- A strict long soak with an unambiguous zero-UNKNOWN runner result.
