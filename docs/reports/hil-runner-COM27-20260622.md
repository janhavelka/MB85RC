# MB85RC HIL Runner Results - COM27

- Generated: 2026-06-22T20:52:47+02:00
- Port: `COM27`
- Baud: `115200`
- Detected profile: `arduino`
- Transcript: `C:/Users/Honza/Documents/Projects/MB85RC/docs/reports/hil-runner-COM27-20260622-transcript.txt`
- JSON: `C:/Users/Honza/Documents/Projects/MB85RC/docs/reports/hil-runner-COM27-20260622.json`
- Functional counts: PASS=26 FAIL=0 UNKNOWN=0 NOT RUN=0

## Functional Results

| Test ID | Area | Command | Expected | Observed | Elapsed s | Result | Notes |
| --- | --- | --- | --- | --- | ---: | --- | --- |
| HIL-001 | connectivity | `version` | any: MB85RC library version \| MB85RC  | === Version Info === Example firmware build: Jun 22 2026 20:47:48 MB85RC library version: 3.0.0 MB85RC library full: 3.0.0 (e20c775, 2026-06-22 20:47:46, dirty) MB85RC library build: 2026-06-22 20:47:46 MB85RC library commit: e20c775 (di... | 0.375 | PASS |  |
| HIL-002 | connectivity | `scan` | any: Scan complete \| I2C scan: | [I] Scanning I2C bus (timeout=50ms)... [I] 0 1 2 3 4 5 6 7 8 9 A B C D E F 00: -- -- -- -- -- -- -- -- 10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 30: -- -- -- -- -- -- -- -- --... | 0.500 | PASS |  |
| HIL-003 | state | `settings` | any: === Settings === \| state= + capacity= | === Settings === Initialized: true State: READY I2C address: 0x50 I2C timeout: 50 ms Offline threshold: 5 nowMs hook: present Expected variant: AUTO Active variant: MB85RC64TA Device ID: manufacturer=0x00A product=0x358 density=0x03 Capa... | 0.375 | PASS |  |
| HIL-004 | state | `drv` | any: === Driver Health === \| state= + initialized= | === Driver Health === State: READY Online: yes Consecutive failures: 0 Total success: 0 Total failures: 0 Success rate: 0.0% Last OK: never Last error: never > | 0.375 | PASS |  |
| HIL-005 | identity | `id` | any: Device ID: + Manufacturer=0x \| id: OK + manufacturer=0x | Device ID: Manufacturer=0x00A Product=0x358 Density=0x03 Variant: MB85RC64TA (8192 bytes) Runtime driver support: yes; access format: 2-byte linear address > | 0.375 | PASS |  |
| HIL-006 | identity | `idraw` | any: Device ID raw: \| idraw: OK | Device ID raw: 00 A3 58 > | 0.375 | PASS |  |
| HIL-007 | identity | `variants` | any: Known MB85RC family variants: \| MB85RC256V + bytes= | Known MB85RC family variants: MB85RC04V 512 bytes product=0x010 density=0x0 1-byte address, A8 in device address; runtime driver support=yes; high-speed capability=no; sleep capability=no max normal bus=1000000 Hz; max high-speed bus=0 H... | 0.375 | PASS |  |
| HIL-008 | memory | `size` | any: Active capacity: \| capacity= + variant= | Active capacity: 8192 bytes (max address 0x1FFF, variant MB85RC64TA) Legacy MB85RC256V memorySize(): 32768 bytes > | 0.359 | PASS |  |
| HIL-009 | diagnostics | `probe` | any: Status: + OK \| probe: OK | [I] Probing device (no health tracking)... Status: OK (code=0, detail=0) Message: OK > | 0.375 | PASS |  |
| HIL-010 | memory | `read 0x0000 16` | any: 0000: \| 0x000000: | 0000: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 \|................\| > | 0.375 | PASS |  |
| HIL-011 | memory | `current 1` | any: Current \| current: OK \| 0010: \| 0x | 0010: 00 \|.\| > | 0.375 | PASS |  |
| HIL-012 | memory | `text 0x0000 16` | any: 0000: \| \x \| text | 0000: "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" > | 0.375 | PASS |  |
| HIL-013 | memory | `crc 0x0000 64` | any: CRC32 \| crc32= | CRC32[0x0000 + 64] = 0x758D6336 > | 0.375 | PASS |  |
| HIL-014 | modes | `hs support` | all: High-speed mode:, Support: | High-speed mode: Active variant: MB85RC64TA Support: yes Enabled: no Core bus clock: unchanged; MB85RC core does not change Wire/ESP-IDF I2C clock Diagnostic bus clock: BoardConfig/Wire setting; this example does not prove 3.4 MHz operat... | 0.375 | PASS |  |
| HIL-015 | modes | `hs enter` | any: High-speed mode: + Status: \| High-speed mode: + hs enter: | High-speed mode: Active variant: MB85RC64TA Support: yes Enabled: no Core bus clock: unchanged; MB85RC core does not change Wire/ESP-IDF I2C clock Diagnostic bus clock: BoardConfig/Wire setting; this example does not prove 3.4 MHz operat... | 0.375 | PASS |  |
| HIL-016 | modes | `sleep support` | all: Sleep mode:, Support: | Sleep mode: Active variant: MB85RC64TA Support: yes State: AWAKE Entry: F8h + active device address word + repeated-start 86h Wake: clock active device address word, wait tREC >= 400 us before access/recover Core sleep state: tracked sep... | 0.375 | PASS |  |
| HIL-017 | modes | `sleep enter` | any: Sleep mode: + Status: \| Sleep mode: + sleep enter: | Sleep mode: Active variant: MB85RC64TA Support: yes State: AWAKE Entry: F8h + active device address word + repeated-start 86h Wake: clock active device address word, wait tREC >= 400 us before access/recover Core sleep state: tracked sep... | 0.375 | PASS |  |
| HIL-018 | recovery | `recover` | any: Status: + OK \| recover: OK | [I] Attempting recovery... Status: OK (code=0, detail=0) Message: OK === Driver Health === State: READY Online: yes Consecutive failures: 0 Total success: 7 Total failures: 0 Success rate: 100.0% Last OK: 0 ms ago (at 7610 ms) Last error... | 0.375 | PASS |  |
| HIL-019 | validation | `definitely_not_a_command` | any: Unknown command \| Unknown command. Try 'help'. | [W] Unknown command: definitely_not_a_command > | 0.360 | PASS |  |
| HIL-020 | validation | `read 0xFFFFFFFF 1` | any: Range \| Address out of range \| outside active capacity \| Usage: | [W] Usage: read / dump / hexdump <addr> [len] > | 0.390 | PASS |  |
| HIL-021 | diagnostics | `selftest` | any: Selftest result: \| selftest_pattern=PASS | === MB85RC selftest (diagnostic commands) === [PASS] probe responds [PASS] probe no-health-side-effects [PASS] readDeviceId [PASS] manufacturer ID = 0x00A [PASS] product ID matches active variant [PASS] density code matches active varian... | 0.375 | PASS |  |
| HIL-022 | memory | `rw_suite` | any: Read/write suite result: \| rw_suite restore: OK | === Read/Write Suite === [PASS] backup scratch region [PASS] backup fill region [PASS] backup tail region [PASS] write scratch pattern [PASS] verify scratch transaction [PASS] verify scratch contents [PASS] read scratch pattern [PASS] sc... | 0.391 | PASS |  |
| HIL-023 | staged | `xfer_demo` | any: Transfer demo result: \| xfer_demo_result | === Poll-Chunked Transfer Demo === Scratch: 0x0700 + 160 [PASS] backup staged scratch region [PASS] requestRead queues without I2C [PASS] zero-budget poll remains in progress [PASS] sync read rejected while transfer busy - BUSY [PASS] po... | 0.406 | PASS |  |
| HIL-024 | data | `typed_demo` | any: Typed Value Demo \| typed_demo restore: OK | === Typed Value Demo === Storage format: explicit little-endian, fixed-width, no silent wrap. [PASS] write uint8 [PASS] write uint16 LE [PASS] write int32 LE [PASS] write uint64 LE [PASS] write float32 LE [PASS] write float64 LE [PASS] w... | 0.375 | PASS |  |
| HIL-025 | timing | `randbench 50` | any: Random Access Benchmark \| randbench_ok= | [I] Starting random benchmark: 50 writes + 50 reads === Random Access Benchmark === Window: 0x1C00 + 1024 Final window verify: PASS random-write-byte ops=50 elapsed=7720 us avg=154.40 us/op rate=6476.68 ops/s throughput=6476.68 B/s rando... | 0.469 | PASS |  |
| HIL-026 | state | `drv` | any: === Driver Health === \| state= + initialized= | === Driver Health === State: READY Online: yes Consecutive failures: 0 Total success: 206 Total failures: 0 Success rate: 100.0% Last OK: 375 ms ago (at 10383 ms) Last error: never > | 0.375 | PASS |  |

## Soak Summary

- Status: PASS
- Duration: 60.0 s
- PASS=112 FAIL=0 UNKNOWN=0
- Latency min/mean/max: 0.359/0.485/12.015 s
- Worst read latency: 0.375 s
- Recover commands: 10
