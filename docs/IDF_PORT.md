# ESP-IDF Port

The core library is framework-neutral. Public headers and `src/` do not include
Arduino or ESP-IDF framework headers, and all hardware access is supplied
through `Config` callbacks.

`idf_component.yml` declares ESP-IDF 6.0.1 or newer and the `esp32s2` and
`esp32s3` targets. Build the native example with an initialized ESP-IDF shell:

```bash
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

The native ESP-IDF example in `examples/espidf_basic` owns only example-local
resources:

- entry point: `app_main()`
- I2C: `driver/i2c_master.h`
- timing hook: `esp_timer_get_time()` through `Config::nowMs`
- heap telemetry: `esp_get_free_heap_size()`,
  `esp_get_minimum_free_heap_size()`, and
  `heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)`
- CLI loop delay: `vTaskDelay()`
- command input: fixed C buffers and `fgets()`

The Arduino and ESP-IDF examples share a command contract, not implementation
source. The IDF example must not include Arduino sources or compatibility
facades such as `Arduino.h`, `Wire.h`, `String`, `Serial`, or `TwoWire`.
`tools/check_idf_example_contract.py` enforces this native-IDF boundary and the
expected command coverage.

## Command Coverage

The native IDF CLI exposes the same driver-facing workflows as the Arduino CLI:

- variant selection, active capacity, and Device ID diagnostics
- addressed read, dump/hexdump, text, strings, CRC, and verify commands
- current-address reads for diagnostics
- HS/Sleep support, entry/wake, and driver diagnostics
- heap telemetry with `heap`
- stress, selftest, random benchmark, and typed demo commands

Destructive FRAM commands require explicit confirmation forms:

- `write! <addr> <byte> [byte...]`
- `fill! <addr> <value> <len>`
- `selftest!`
- `rw_suite!`
- `xfer_demo!`
- `stress! [N]`
- `stress_mix! [N]`
- `randbench! [N]`
- `typed_demo!`

Unconfirmed forms print the affected operation and exact `!` command required
instead of writing memory.

## Transport Notes

The native IDF transport implements terminal `TransportResult` write/write-read
callbacks and the special callback for the reserved Device ID sequence,
HS-prefixed transfers, Sleep entry, and Sleep wake stimulus. Each callback maps
one completed physical attempt, exact TX/RX lengths, and conservative write
commit knowledge; it performs no retry or bus recovery. The example calls
zero-I/O `bind()` before scheduling the explicit identity read.

ESP-IDF's transmit result does not identify which byte was NACKed. The example
therefore maps an invalid-response/not-found result to a general transport I/O
error and retains `WriteCommit::INDETERMINATE` once a memory write was issued;
it never upgrades that outcome to `NOT_COMMITTED` or encourages a blind retry.

This demonstrates the protocol path, but it does not prove real
3.4 MHz operation or Sleep current on hardware until board validation is
recorded.

The Device ID path uses the reserved `0x7C` controller sequence only inside
`I2cSpecialOp::READ_DEVICE_ID`; normal scan/device transfers retain conventional
7-bit policy.

For a production adapter:

- keep the bus handle, locking, controller timeout, retry, and recovery policy
  outside the MB85RC instance;
- return only terminal results and enforce the supplied per-transaction timeout;
- report exact callback-buffer TX/RX progress, excluding hidden special framing;
- preserve `WriteCommit::INDETERMINATE` unless the backend can prove that no
  requested memory data was accepted;
- implement Device ID, High-speed, Sleep, and wake framing only through
  `Config::i2cSpecial`, without admitting reserved `0x7C` as a normal device;
- keep callback contexts alive through `end()` or successful rebinding; and
- serialize all calls touching the same instance because the driver is not
  internally thread-safe or ISR-safe.

When `Config::nowMs` is null, core health timestamps remain `0`. The IDF
example supplies `nowMs` from `esp_timer_get_time() / 1000`, intentionally
matching the driver's `uint32_t` millisecond contract.

ESP-IDF hardware validation remains pending unless a target board, wiring,
command log, commit, and result are recorded separately.
