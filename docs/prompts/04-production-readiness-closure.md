# AI Coder Prompt: MB85RC Production Readiness Closure

You are working inside the `MB85RC` repository:

`C:\Users\Honza\Documents\Projects\MB85RC`

Goal:
Close the remaining concrete gaps before this library can be called production
grade for the target MB85RC256V use case. Keep the design simple, functional,
robust, and explicit. Reuse existing code, helpers, examples, tests, and HIL
runner structure where feasible.

Do not claim production readiness unless the exact evidence was produced and
recorded. If hardware or tooling is unavailable, complete the code/docs/tooling
work that can be done locally and document the remaining blockers precisely.

## Execution Rules

1. Read `AGENTS.md` first and treat it as binding.
2. Check `git status --short` before editing. Preserve dirty user changes.
3. Inspect the current public API, README, `docs/DEVICE_REFERENCE.md`,
   `docs/RELEASE_CHECKLIST.md`, HIL reports in `docs/reports/`, native tests,
   Arduino CLI, ESP-IDF CLI, and HIL runner before changing code.
4. You may spawn subagents for read-only audits. Keep final judgment, edits,
   and verification in the main agent.
5. Do not add a bus manager, framework facade, production fake device, registry,
   plugin system, or speculative abstraction.
6. Do not introduce Arduino, Wire, ESP-IDF, FreeRTOS, logging, or heap-heavy
   framework types into `include/MB85RC/` or `src/`.
7. Do not commit unless explicitly asked.

## Current Evidence To Start From

- `docs/reports/hil-validation-COM27-20260622.md` is evidence for one fixture
  only. It explicitly says it is not a production-readiness claim.
- That HIL run used Arduino `esp32s3dev` on COM27 and detected `MB85RC64TA`,
  not `MB85RC256V`.
- Functional HIL passed `26 / 0 / 0`.
- The 8-hour soak completed `28800.2 s` with `53191 PASS / 0 FAIL / 30 UNKNOWN`.
  Treat that as completed with anomalies, not a clean production soak.
- ESP-IDF local build/HIL was not run because `idf.py` was unavailable.
- ESP32-S2 hardware HIL was not run.
- MB85RC256V hardware HIL was not run.
- Fault-injection HIL was not run: WP-high, disconnect, wrong address,
  power-cycle/brownout, address strap matrix, and shared-bus behavior remain
  unevidenced.
- Native tests already cover staged transfer budgets including
  `pollTransfer(..., 2)` and high-budget clamp. HIL only covered
  `pollTransfer(..., 0)` and one-instruction polling.

## Production Closure Work

### 1. Freeze Public Numeric Contracts

The public enum values are part of the embedded API contract. Freeze them
explicitly so future append-only changes do not accidentally reorder values.

Required enum values:

```cpp
enum class Err : uint8_t {
  OK = 0,
  NOT_INITIALIZED = 1,
  INVALID_CONFIG = 2,
  I2C_ERROR = 3,
  TIMEOUT = 4,
  INVALID_PARAM = 5,
  DEVICE_NOT_FOUND = 6,
  DEVICE_ID_MISMATCH = 7,
  ADDRESS_OUT_OF_RANGE = 8,
  WRITE_PROTECTED = 9,
  BUSY = 10,
  IN_PROGRESS = 11,
  I2C_NACK_ADDR = 12,
  I2C_NACK_DATA = 13,
  I2C_TIMEOUT = 14,
  I2C_BUS = 15,
  VERIFY_MISMATCH = 16,
  UNSUPPORTED = 17
};

enum class DriverState : uint8_t {
  UNINIT = 0,
  READY = 1,
  DEGRADED = 2,
  OFFLINE = 3
};

enum class SleepState : uint8_t {
  AWAKE = 0,
  ASLEEP = 1,
  WAKING = 2
};

enum class DeviceVariant : uint8_t {
  AUTO = 0,
  MB85RC256V = 1,
  MB85RC64TA = 2,
  MB85RC04V = 3,
  MB85RC16V = 4,
  MB85RC512T = 5,
  MB85RC1MT = 6
};

enum class I2cSpecialOp : uint8_t {
  HIGH_SPEED_WRITE = 0,
  HIGH_SPEED_WRITE_READ = 1,
  ENTER_SLEEP = 2,
  WAKE_FROM_SLEEP = 3
};
```

Add compile-time or native-test assertions for these exact values. Keep future
enum additions append-only.

### 2. Tighten Status And Health Semantics

Make currently implicit status semantics explicit and testable.

Required decisions:

- `Err::DEVICE_NOT_FOUND` remains reserved/deprecated for compatibility. The
  core should not wrap transport absence into `DEVICE_NOT_FOUND`; `begin()`,
  `probe()`, and `recover()` preserve injected transport statuses such as
  `I2C_NACK_ADDR`, `I2C_TIMEOUT`, `I2C_BUS`, or `I2C_ERROR`.
- The core never synthesizes `Err::WRITE_PROTECTED` because hardware WP-high
  cannot be detected from ACK alone. If the injected transport returns
  `WRITE_PROTECTED`, return it as-is but do not increment health/offline
  counters because it is an application/board policy condition, not an I2C bus
  health failure.
- `Err::VERIFY_MISMATCH` does not increment health/offline counters.
- `probe()` remains raw/untracked.
- Validation and precondition errors remain untracked.
- `recover()` is the only semantic-health exception: after a successful tracked
  I2C transaction, a Device ID mismatch may be counted as a recover failure via
  `_recordFailure()`.

Add a public machine-readable detail enum for `Err::BUSY` cases:

```cpp
enum class BusyDetail : int32_t {
  OFFLINE = 1,
  TRANSFER_ACTIVE = 2,
  ASLEEP = 3,
  WAKING = 4,
  ALREADY_ASLEEP = 5,
  TRANSFER_CANCELLED = 6
};
```

Use `Status::detail` with these values wherever the core returns `Err::BUSY`.
Keep the current human-readable messages or tighten them consistently.

### 3. Bound Public Configuration Constants

`Config::i2cTimeoutMs` currently rejects only `0`. Set a deterministic range.

Required constants:

```cpp
static constexpr uint32_t MIN_I2C_TIMEOUT_MS = 1;
static constexpr uint32_t DEFAULT_I2C_TIMEOUT_MS = 50;
static constexpr uint32_t MAX_I2C_TIMEOUT_MS = 1000;
```

Required behavior:

- `Config::i2cTimeoutMs` default remains `50`.
- `begin()` rejects values outside `1..1000` with `Err::INVALID_CONFIG`.
- `Status::detail` is the supplied timeout value when rejected.
- Document that the transport owns the actual controller timeout; this value is
  the per-transaction deadline passed to callbacks.

Publish the staged transfer limits as public constants instead of private
magic values:

```cpp
static constexpr size_t MAX_READ_CHUNK = 128;
static constexpr size_t MAX_WRITE_CHUNK = 126;
static constexpr size_t MAX_FILL_CHUNK = 64;
static constexpr uint8_t MAX_TRANSFER_INSTRUCTIONS_PER_POLL = 8;
```

`MAX_READ_CHUNK` and `MAX_WRITE_CHUNK` already exist in `cmd`; add
`MAX_FILL_CHUNK` and `MAX_TRANSFER_INSTRUCTIONS_PER_POLL` there too, then reuse
those constants from `src/MB85RC.cpp`. Update Doxygen/README so the clamp is no
longer surprising.

### 4. Resolve `tick()` Contract

AGENTS says `tick()` is a no-op for the original MB85RC256V target, while the
current family driver uses it to advance `SleepState::WAKING -> AWAKE` for
Sleep-capable variants.

Required contract:

- Keep `tick(uint32_t nowMs)`.
- It must never perform I2C, delay, retry, allocate, log, or update health.
- It may only advance cached Sleep recovery state from `WAKING` to `AWAKE` when
  the caller-supplied timestamp reaches the recovery deadline.
- For MB85RC256V and other variants without Sleep support, `tick()` is a
  practical no-op.
- Update AGENTS-aligned docs/comments if needed so this exception is explicit.

### 5. Align Health Counter Semantics

AGENTS says lifetime counters wrap at max, but the current implementation
saturates `_totalSuccess` and `_totalFailures`.

Required behavior:

- `_totalSuccess` and `_totalFailures` are `uint32_t` lifetime counters and must
  wrap naturally from `UINT32_MAX` to `0`.
- `_consecutiveFailures` remains a bounded `uint8_t` failure streak and should
  saturate at `UINT8_MAX`; do not let it wrap to zero because that would clear
  the OFFLINE threshold accidentally.
- `offlineThreshold == 0` continues to normalize to `1`.
- State transitions remain:
  - success -> `DriverState::READY`, consecutive failures `0`
  - failure with `consecutiveFailures < offlineThreshold` -> `DEGRADED`
  - failure with `consecutiveFailures >= offlineThreshold` -> `OFFLINE`

Concrete implementation target:

- Replace the saturation checks around `_totalSuccess` and `_totalFailures` in
  `src/MB85RC.cpp` with natural unsigned increment.
- Keep the saturation guard for `_consecutiveFailures`.
- Update Doxygen/README wording to say lifetime counters wrap, while
  consecutive failures saturate.
- Add focused native tests. Do not loop `2^32` times. If no clean test seam
  exists, extract the smallest local helper needed to exercise wrap behavior
  without exposing a new public API. Do not add a broad test framework.

### 6. Settle Staged Request Preflight

Current staged `requestRead`, `requestWrite`, `requestFill`, and
`requestVerify` validate initialization, active transfer, buffers, length, and
range, but they can queue while the driver is OFFLINE, ASLEEP, or WAKING and
then fail later during `pollTransfer()`.

Required behavior:

- A `request*()` call must not queue a transfer when normal memory I2C is not
  currently allowed.
- Return `Err::BUSY` without I2C for these preflight states:
  - active transfer already queued: message `"Transfer in progress"`,
    `detail = BusyDetail::TRANSFER_ACTIVE`
  - `DriverState::OFFLINE`: message `"Driver offline until recover()"`,
    `detail = BusyDetail::OFFLINE`
  - `SleepState::ASLEEP`: message `"Device is asleep; call wake()"`,
    `detail = BusyDetail::ASLEEP`
  - `SleepState::WAKING`: message `"Sleep wake recovery pending"`,
    `detail = BusyDetail::WAKING`
- `request*()` must remain bus-silent.
- `pollTransfer(nowMs, 0)` must remain bus-silent and return the current
  transfer status.
- Do not add retries or hidden recovery.

Concrete implementation target:

- Reuse `_ensureAwakeForI2c()` if it fits without adding I2C or hidden delays.
  If it does not fit cleanly, add a small private helper such as
  `_canQueueTransfer()` returning `Status`.
- Keep status code `Err::BUSY`; use `BusyDetail` for machine-readable detail.
- Update Doxygen for all `request*()` APIs and README staged-transfer wording.
- Add native tests proving:
  - OFFLINE request rejects with `BUSY` and does not change bus counters.
  - ASLEEP request rejects with `BUSY` and does not change bus counters.
  - WAKING request rejects with `BUSY` until `tick()` or request-time wake
    advancement makes the device AWAKE.
  - Existing busy/cancel/boundary behavior still passes.

### 7. Tighten Variant Base-Address Semantics

`Config::i2cAddress` must mean the board's base strap address, not a
memory-bank encoded transaction address. Make this explicit and enforce it
where the active variant's address model makes encoded addresses ambiguous.

Required rules:

- Common range remains `0x50..0x57`.
- `TWO_BYTE_ADDRESS_PINS` variants (`MB85RC64TA`, `MB85RC256V`,
  `MB85RC512T`): accept `0x50..0x57` because A2/A1/A0 are real device-select
  pins.
- `TWO_BYTE_A16_IN_DEVICE_ADDRESS` (`MB85RC1MT`): accept only even base
  addresses `0x50`, `0x52`, `0x54`, `0x56`; reject odd addresses because A16
  is encoded per transaction.
- `ONE_BYTE_A8_IN_DEVICE_ADDRESS` (`MB85RC04V`): accept only even base
  addresses `0x50`, `0x52`, `0x54`, `0x56`; reject odd addresses because A8 is
  encoded per transaction.
- `ONE_BYTE_UPPER_BITS_IN_DEVICE_ADDRESS` (`MB85RC16V`): accept only `0x50`;
  reject `0x51..0x57` because A10:A8 are encoded per transaction and there are
  no external address-select pins in the local datasheet model.
- Use `Err::INVALID_CONFIG` with `detail = config.i2cAddress` for rejected
  base addresses. Suggested message:
  `"I2C address must be base strap for active variant"`.

Concrete implementation target:

- Add a small private validation helper only if it keeps `begin()` readable,
  for example `_validateBaseAddressForVariant(const cmd::VariantInfo&)`.
- For explicit variants, reject invalid base addresses before any bus access.
- For `DeviceVariant::AUTO`, validate after Device ID selection and fail
  `begin()` before marking the driver initialized.
- Update `Config::i2cAddress` Doxygen, README variant table notes, and
  `docs/DEVICE_REFERENCE.md`.
- Add native tests for valid and invalid base addresses for each address model,
  including AUTO-selected `MB85RC04V` and `MB85RC1MT`.

### 8. Clarify `readDeviceIdRaw` Semantics

The public method name means "return raw 3-byte payload"; it should not imply
"raw untracked diagnostic I2C". Public Device ID reads should remain tracked
unless a deliberate API change is made.

Required behavior:

- Keep `readDeviceId()` and `readDeviceIdRaw()` health-tracked public APIs.
- Keep `probe()` diagnostic/raw and untracked.
- Keep begin-time Device ID reads raw because the driver is not initialized yet.
- Update Doxygen/README if this distinction is not already clear.
- Add or update a native test proving public `readDeviceIdRaw()` updates health
  on transport failure and `probe()` does not.

### 9. Complete Restore-Safe Staged HIL Coverage

Current HIL `xfer_demo` verifies staged request APIs, zero-budget polling,
one-instruction polling, busy rejection, multi-chunk transfer, restore, and
verify. It does not exercise multiple instructions in one hardware poll.

Required behavior:

- Prefer extending existing `xfer_demo` / `xfer_demo!` rather than adding a new
  command.
- Add restore-safe checks with explicit output labels:
  - `"poll budget 2 executes two chunks"`
  - `"poll high budget clamps to 8 chunks"`
- Use a scratch length large enough to require more than two chunks for read,
  write, verify, and fill. Suggested length: `320` bytes if it fits the active
  device capacity and scratch window.
- Keep backup/restore/verify behavior. Never leave the scratch range dirty on
  a passing command.
- Keep Arduino command name `xfer_demo`.
- Keep ESP-IDF confirmed command name `xfer_demo!`.
- Update `tools/hil_runner.py` expected tokens only if needed; avoid fragile
  parsing of every line.

### 10. Add Example Heap Telemetry For HIL

The core must remain heap-free in steady paths, but the examples currently do
not expose a simple way for HIL to observe heap drift over long runs.

Required command:

- Add CLI command `heap` to Arduino and ESP-IDF examples.
- Arduino output format:
  `heap: free=<bytes> min_free=<bytes> largest=<bytes>`
- ESP-IDF output format:
  `heap: free=<bytes> min_free=<bytes> largest=<bytes>`
- Use example-local APIs only:
  - Arduino: ESP heap APIs from the Arduino environment.
  - ESP-IDF: `esp_get_free_heap_size()`,
    `esp_get_minimum_free_heap_size()`, and
    `heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)`.
- Do not add heap APIs to the core driver.
- Add `heap` to `tools/check_cli_contract.py`,
  `tools/check_idf_example_contract.py`, README, and `docs/IDF_PORT.md`.

Suggested HIL thresholds:

- After boot settle, record a heap baseline.
- Production soak fails if final `free` drops by more than `1024` bytes from
  baseline.
- Production soak fails if `min_free` drops below `8192` bytes.
- If a board cannot meet these thresholds due to unrelated application load,
  document the board-specific threshold and rationale in the HIL report.

### 11. Make HIL Runner Production-Gate Capable

The runner should make it hard to accidentally treat the wrong fixture or an
UNKNOWN soak as production evidence.

Required runner additions:

- Add optional arguments:
  - `--require-variant MB85RC256V`
  - `--require-product-id 0x510`
  - `--require-capacity 32768`
  - `--strict`
  - `--heap-max-drop-bytes 1024`
  - `--heap-min-free-bytes 8192`
- In strict mode:
  - any functional `UNKNOWN` is failure
  - any soak `UNKNOWN` is failure
  - any `FAIL` is failure
  - wrong required variant/product/capacity is failure
  - final driver state must be READY
  - final consecutive failures must be `0`
  - final total failures must be `0`
  - serial reconnect count must be `0`
  - detected target reset count must be `0`
  - heap thresholds must pass when `heap` is available
- JSON and Markdown artifacts must never report a clean PASS soak when
  `unknown_count > 0`; report `UNKNOWN` and return a nonzero process status.
- Track target resets by recognizing boot banners in transcripts after the
  initial boot, for example ESP ROM boot lines.
- Keep dry-run and parser self-test support.
- Keep `--include-destructive-stress` opt-in only.

Suggested MB85RC256V production HIL command shape:

```powershell
python tools\hil_runner.py --port COMx --baud 115200 --timeout-s 12 --idle-timeout-s 0.35 --boot-settle-s 1 --sample-count 50 --strict --require-variant MB85RC256V --require-product-id 0x510 --require-capacity 32768 --heap-max-drop-bytes 1024 --heap-min-free-bytes 8192 --soak-duration-s 28800 --soak-pacing-s 0.05 --transcript-path docs\reports\hil-runner-MB85RC256V-ESP32S3-YYYYMMDD-8h-transcript.txt --json-path docs\reports\hil-runner-MB85RC256V-ESP32S3-YYYYMMDD-8h.json --markdown-path docs\reports\hil-runner-MB85RC256V-ESP32S3-YYYYMMDD-8h.md
```

### 12. Produce Exact Hardware Evidence

Do not block code cleanup on unavailable hardware, but do not call the library
production grade until these exact evidence rows exist.

Minimum production hardware matrix:

- MB85RC256V on ESP32-S3, Arduino example:
  - build, upload, functional HIL, strict 8-hour soak
- MB85RC256V on ESP32-S2, Arduino example:
  - build, upload, functional HIL, strict 8-hour soak
- MB85RC256V on ESP32-S3, native ESP-IDF example:
  - `idf.py -C examples/espidf_basic set-target esp32s3 build`
  - flash/run CLI
  - `id`, `idraw`, `settings`, `rw_suite!`, `xfer_demo!`, `typed_demo!`,
    `heap`
- MB85RC256V on ESP32-S2, native ESP-IDF example:
  - `idf.py -C examples/espidf_basic set-target esp32s2 build`
  - flash/run CLI
  - same command set as ESP32-S3

Fault-injection evidence, at minimum on the production MB85RC256V fixture:

- wrong I2C address or missing device:
  - `probe()` does not mutate health
  - tracked read/recover failures degrade and then OFFLINE at threshold
  - manual `recover()` returns to READY after restoring the device
- WP high:
  - `write()` may report transport acceptance
  - `verify()` or `writeVerify()` detects unchanged data with
    `Err::VERIFY_MISMATCH`
  - WP low/open write then verify succeeds
- power-cycle:
  - write/verify a known record
  - power-cycle under controlled conditions
  - read/verify the record after reboot
- address straps:
  - at least default `0x50` and one nonzero valid strap for MB85RC256V
  - wrong straps/addresses NACK visibly
- shared bus:
  - run with another I2C device present
  - verify external serialization and no interleaved driver transactions

Report file naming:

- Use `docs/reports/production-readiness-MB85RC256V-ESP32S3-YYYYMMDD.md`
- Use `docs/reports/production-readiness-MB85RC256V-ESP32S2-YYYYMMDD.md`
- Include board, MCU, FRAM package/date code if visible, supply voltage,
  pull-up values, SDA/SCL pins, bus speed, WP wiring, address straps, firmware
  commit, dirty status, exact commands, artifact paths, pass/fail/unknown
  counts, heap baseline/end/min, final driver health, and limitations.

## Verification Commands

Run the smallest relevant tests after each code change, then run the full set:

```powershell
python tools\hil_runner.py --parser-self-test
python tools\hil_runner.py --dry-run --port COM27 --baud 115200 --timeout-s 5 --strict --require-variant MB85RC256V --require-product-id 0x510 --require-capacity 32768 --soak-duration-s 28800
python tools\check_cli_contract.py
python tools\check_idf_example_contract.py
python tools\check_core_timing_guard.py
python scripts\generate_version.py check
python tools\check_metadata_consistency.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack
git diff --check
```

When ESP-IDF is available:

```powershell
idf.py --version
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

Run Doxygen if public comments changed:

```powershell
doxygen Doxyfile
```

Remove generated package and Doxygen artifacts unless the repository
intentionally tracks them.

## Acceptance Criteria

Code/docs/tooling closure:

- Native tests pass.
- Arduino ESP32-S2/S3 builds pass.
- Contract scripts pass.
- HIL runner parser self-test and dry-run pass.
- Doxygen passes if public documentation changed.
- `git diff --check` reports no real whitespace errors.
- No framework headers or logging enter core/public library files.
- No unbounded waits, retries, hidden delays, or production fake devices are
  added.

Production-grade claim:

- Requires strict MB85RC256V HIL on the exact production-class fixtures.
- Requires `0 FAIL` and `0 UNKNOWN`.
- Requires final driver health READY, `consecutiveFailures = 0`,
  `totalFailures = 0`.
- Requires no serial reconnects and no target resets.
- Requires heap thresholds to pass when using the example firmware.
- Requires fault-injection evidence or an explicit product-level waiver with
  rationale.

Final response:

- List files changed.
- List decisions made for counters, staged preflight, base-address validation,
  and Device ID raw semantics.
- Summarize tests/builds/HIL commands run with results.
- State exactly what remains not run and whether production readiness can or
  cannot be claimed.
