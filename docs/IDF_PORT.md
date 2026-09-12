# ESP-IDF Port

The core library is framework-neutral. Public headers and `src/` do not include
Arduino or ESP-IDF framework headers, and all hardware access is supplied
through `Config` callbacks.

The bundled native application is a diagnostic-only bring-up example, not a
production shared-bus manager or storage service.

`idf_component.yml` declares ESP-IDF 6.0.1 or newer and the `esp32s2` and
`esp32s3` targets. CI pins both the declared 6.0.1 floor and 6.0.2 for each
target rather than following a mutable release branch. Build the native example
with an initialized ESP-IDF shell:

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
- runtime framework telemetry: `esp_get_idf_version()` in `version` / `ver`

The Arduino and ESP-IDF examples share a command contract and the pure helpers
in `examples/common/DiagnosticCore.h`: CRC updates, range checks, verified
restoration, staged-result handling, bounded polling with an injected clock,
and enum names. That header also supplies the native example's reset/rebind
ordering, tested without an SDK. Each main retains its own printing and
diagnostic demo suites. SDK-independent result mapping and transaction dispatch live in
`examples/common/IdfI2cTransport.h`, shared with native tests. The example binds
the SDK's error constants and transaction functions to those helpers.
The IDF example must not include Arduino sources or compatibility
facades such as `Arduino.h`, `Wire.h`, `String`, `Serial`, or `TwoWire`.
`tools/check_idf_example_contract.py` enforces this native-IDF boundary and the
expected command coverage.

## Command Coverage

The native IDF CLI exposes the same driver-facing workflows as the Arduino CLI:

- AUTO variant discovery, active capacity, and Device ID diagnostics
- addressed read, dump/hexdump, text, strings, CRC, and verify commands
- current-address reads for diagnostics
- HS/Sleep support, entry/wake, and driver diagnostics
- heap telemetry with `heap`
- stress, selftest, random benchmark, and typed demo commands

Both example CLIs configure `DeviceVariant::AUTO`; `variants` lists device
metadata and does not select a variant. For the no-Device-ID `MB85RC16V`, set
`expectedVariant` explicitly in the example configuration and rebuild before
running memory diagnostics. The core already supports that explicit binding.

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
therefore maps an invalid-response/not-found result to
`TransportCode::NACK_UNSPECIFIED` (`Err::I2C_NACK`) and retains
`WriteCommit::INDETERMINATE` once a memory write was issued;
it never upgrades that outcome to `NOT_COMMITTED` or encourages a blind retry.

Startup and `iface_reset` clear cached driver state before initializing the
interface. Once the bus is usable, the example sends an address-only wake and
waits at least the configured/datasheet recovery time before binding and checking
identity. This also handles a FRAM that stayed asleep while the MCU restarted.
An interface or wake failure leaves the driver unbound so a later reset can
retry. Failed identification retains the new binding for explicit `id`/`recover`
attempts. Reset does not consume retained cooperative results or restore
partially written memory.

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
- keep callback contexts alive until `end()` or an accepted replacement binding,
  including when `begin()` binds successfully but its identity check fails; and
- serialize all calls touching the same instance because the driver is not
  internally thread-safe or ISR-safe.

When `Config::nowMs` is null, core health timestamps remain `0`. The IDF
example supplies `nowMs` from `esp_timer_get_time() / 1000`, intentionally
matching the driver's `uint32_t` millisecond contract.
