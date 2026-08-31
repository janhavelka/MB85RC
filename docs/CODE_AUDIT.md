# MB85RC Library Audit

Working document. Every finding was checked against the vendor datasheets in
`docs/reference-pdfs/` or against the pinned Arduino/ESP-IDF backend sources —
not against general I2C folklore. Delete this file once the open items are
actioned.

Two states are used:

- **FIXED** - applied in this pass. The full native suite (156 tests) and every
  guard script pass after each change.
- **OPEN** - needs a decision or a larger edit. Each carries a concrete proposal.

---

## 0. Verification setup

`pio` is not installed on this host, so the native suite was built directly:

```bash
g++ -std=c++17 -Wall -Wextra -Iinclude -Iexamples -Itest/stubs -I<unity-shim> \
    -o tests test/test_basic.cpp src/MB85RC.cpp <unity-shim>/unity_main.cpp
```

against a minimal local Unity shim. Result before and after every change:
`156 Tests 0 Failures`. Guards run clean: `check_core_timing_guard`,
`check_metadata_consistency`, `check_cli_contract`, `check_idf_example_contract`,
`generate_version check`, `hil_runner --parser-self-test`.

`doxygen Doxyfile` also builds clean (strict: `WARN_AS_ERROR = FAIL_ON_WARNINGS`).

Datasheet facts were re-extracted with `pdftotext -layout` from the seven local
PDFs and cited per variant.

---

## 1. Datasheet conformance

The `KNOWN_VARIANTS` table in `include/MB85RC/CommandTable.h` was checked field
by field against each part's own datasheet. **Capacities, product IDs, density
nibbles, address models, HS/Sleep flags, the 400 us tREC, the even-base rule for
MB85RC04V/MB85RC1MT, and the single-base rule for MB85RC16V are all correct.**
Two things were not.

### 1.1 FIXED - Current-address read used the wrong slave byte on three variants

`src/MB85RC.cpp`, `readCurrentAddress()`.

All three datasheets that carry memory-address bits in the slave byte say the
same thing. MB85RC04V p.8:

> the memory address that was accessed last remains in the memory address
> buffer (the length is 9 bits). [...] it is possible to read from the memory
> address n+1 which adds 1 to the total 9-bit memory address n, **which consists
> of the 1-bit memory upper address from the device address word input and the
> lower 8-bit of the memory address buffer**.

MB85RC16V p.8 says the same for A10:A8 with an 11-bit buffer; MB85RC1MT p.8 for
A16 with a 17-bit buffer.

So the device does **not** use its own upper address bits for a current-address
read: it takes them from the slave byte of that transaction and combines them
with the low bits it has buffered. The driver was encoding the slave byte from
`_currentAddress`, which is the *next* byte to read, so at every boundary where
the upper bits change the read landed 256 bytes (04V/16V) or 64 KiB (1MT) away.

Failure case, MB85RC16V: `readByte(0x0FF)` leaves `_currentAddress = 0x100`.
`readCurrentAddress()` then encoded slave `0x51` (upper bits `1`), the device
computed `n = {1, 0xFF} = 0x1FF` and returned address `0x200` instead of
`0x100` - with `Status::Ok()`. Silent wrong data, once per 256 bytes.

Fix applied: encode the slave byte from the last accessed byte
(`_currentAddress - 1`, wrapping to `maxAddress()` at zero), recomputed on every
iteration of the multi-byte loop. Verified against all four cases including the
end-of-array wrap. No effect on `TWO_BYTE_ADDRESS_PINS` variants, whose slave
byte carries no address bits.

**This path has no boundary-crossing test.** See item 5.2.

### 1.2 FIXED - MB85RC04V / MB85RC16V bus rate over-reported by 2.5x

`maxNormalBusHz` was a flat `1000000` for all six variants. Both the MB85RC04V
(p.13) and MB85RC16V (p.12) AC tables qualify Fast Mode Plus with:

> Power supply voltage : STANDARD MODE and FAST MODE 3.0 V to 5.5 V;
> **FAST MODE PLUS 4.5 V to 5.5 V**

At 3.3 V - the obvious case for an ESP32 board - those two parts are specified
to 400 kHz, not 1 MHz. The other four specify 1 MHz across their whole supply
range. The driver publishes this value for integrators to size their bus clock,
so an optimistic figure is an out-of-spec recommendation.

Fix applied: added `cmd::FAST_MODE_BUS_HZ = 400000` and used it for those two
entries; `maxNormalBusHz` is now documented as *the rate guaranteed over the
variant's full supply range*. The table now uses named constants throughout
instead of repeating literals. `docs/DEVICE_REFERENCE.md` records the voltage
condition so the 1 MHz figure is not simply lost.

### 1.3 Verified correct - no change needed

- The ESP-IDF example sets `ack_check = true` on the `86h` sleep command byte
  (`examples/espidf_basic/main/main.cpp`). The datasheets show `A` after `86h`
  and state "The slave moves to Sleep mode after ACK response to the master."
  ACK checking is right.
- HS master code `0000 1XXX` is NACKed by design, and HS state ends only at a
  STOP - matching `I2cSpecialTransfer`'s contract and the core's decision to
  re-prefix every transaction.
- Address masks `0x1F` / `0x7F` / `0xFF` match the 13-bit / 15-bit / 16-bit
  usable address widths of MB85RC64TA / MB85RC256V / MB85RC512T.
- `_wrapAddress`'s modulo-capacity model is right: all six parts roll over at
  the end of the *array*, not at a 256-byte page, including across the bits
  carried in the slave byte.

---

## 2. Core driver

### 2.1 FIXED - `_fitsRange()` truncated capacity arithmetic to `size_t`

```cpp
const size_t remaining = static_cast<size_t>(capacity - address);
```

`size_t` is 16 bits on AVR-class targets. For MB85RC512T (65536 bytes),
`capacity - address` at address 0 truncates to `0`, so **every** read, write,
fill, verify and staged request is rejected with `ADDRESS_OUT_OF_RANGE`. Same
for MB85RC1MT at `0` and `0x10000`; every other address under-reports remaining
capacity.

Latent today - `library.json` declares `platforms: ["espressif32"]`, where
`size_t` is 32-bit - but the header is portable C++ and `frameworks` lists
`arduino`. Fixed by keeping the remaining count in `uint32_t` and letting the
usual arithmetic conversions widen whichever side is narrower - the cast
disappears rather than moving, and no target is forced into 64-bit math.

### 2.2 FIXED - `deviceIdDetail()` signed-overflow on 16-bit `int`

`id.manufacturerId << 12` promotes a `uint16_t` to `int`; the expected
`0x00A << 12 == 40960` exceeds `INT16_MAX`, which is UB on AVR and in practice
turns the diagnostic detail of every `DEVICE_ID_MISMATCH` into garbage. Fixed by
casting to `int32_t` before the shift.

### 2.3 FIXED - Re-identification kept state belonging to the previous variant

`_selectVariant()` reassigned `_variant` without touching anything scoped to the
old one. With `expectedVariant = AUTO`:

- `setHighSpeedMode(true)` on an MB85RC512T, then a `recover()` that identifies
  an MB85RC256V, left `_highSpeedModeEnabled == true` while
  `supportsHighSpeedMode()` reported `false`. Every subsequent memory transfer
  emitted an HS master code at a part with no HS mode.
- The cached current-address pointer survived into a different address space.
  `recover()` cleared it; a direct `readDeviceId()` did not.

Fixed: both are reset when the selected variant actually changes. Sleep state
needs no reset - reaching `_selectVariant()` requires a Device ID read, which
`_ensureAwakeForI2c()` only admits while AWAKE.

### 2.4 FIXED - `wake()` could strand the driver in WAKING forever

`Config::nowMs` is optional and defaults to `nullptr`, in which case `_nowMs()`
returns `0`. `wake()` then set `_sleepWakeReadyMs = 0 + 1 = 1`, and
`_ensureAwakeForI2c()` evaluated `deadlineReached(0, 1)` as false on every call.
An application that woke the device and went straight to `readByte()` without
ever calling `tick()` got `Err::BUSY / WAKING` permanently - `enterSleep()` and
`wake()` both reject WAKING, so only `bind()`/`end()` could recover.

The mirror-image problem: an application that *did* call `tick(millis())` left
WAKING on the very first tick, because any real `millis() >= 1` satisfies the
deadline. tREC was never actually enforced either way.

Fixed by removing the state rather than patching the arithmetic: with no time
hook the core cannot measure tREC, so `wake()` now reports AWAKE immediately and
the header documents that enforcing the recovery interval is the caller's job.
With a time hook the WAKING gate behaves as before.

### 2.5 FIXED - `wake()` / `enterSleep()` did not advance an expired WAKING

Every other sleep-aware entry point calls `_advanceWakeState()` first;
these two did not. Calling `wake()` again after the recovery interval - an
ordinary idempotent-retry pattern - returned `BUSY / WAKING` even though the
device was long recoverable. Both now advance first.

### 2.6 FIXED - Staged verify mismatch left `failedChunkOffset` at zero

`TransferResult::failedChunkOffset` / `failedChunkLength` are documented as
"offset/length of the first failed chunk", but the `VERIFY` and
`VERIFIED_WRITE` mismatch paths returned without setting them, so a consumer
keying retry logic off `failedChunkOffset` restarted from 0. Both sites now
record the mismatch offset and the remaining chunk length.

### 2.7 FIXED - smaller items

| Item | Change |
| --- | --- |
| `_selectVariant()`'s non-AUTO branch was unreachable (the only call site passed `AUTO`; the fixed-variant path uses `_validateActiveDeviceId()`) | Branch and parameter removed |
| `_specialTransfer()` used the global `cmd::SLEEP_RECOVERY_US` while `sleepRecoveryUs()` preferred the variant value | Both now use `sleepRecoveryUs()` |
| `_requestTransfer()` ran the sleep gate before parameter validation, so a malformed request while ASLEEP reported `BUSY` instead of `INVALID_PARAM` | Validation moved ahead of the gate |
| Read staging buffers were sized from `MAX_TRANSPORT_RX_BYTES` in two places and `cmd::MAX_READ_CHUNK` in two others | Unified on `cmd::MAX_READ_CHUNK` |
| The `TX buffer >= address prefix + write chunk` invariant lived unlinked across three headers | Three `static_assert`s added in `Config.h` |
| `getConfig()` doc said "copy", returns a reference | Doc corrected |

### 2.8 OPEN - `probe()` under AUTO accepts any known part

`src/MB85RC.cpp`, `probe()`. Once AUTO has selected a variant, `probe()` still
takes the AUTO branch and accepts *any* product ID in `KNOWN_VARIANTS`. Swapping
an MB85RC512T for an MB85RC256V on the bus yields `Status::Ok()` while
`capacityBytes()` still reports 64 KiB.

**Proposal.** Make presence checking consistent with the active binding:

```cpp
DeviceId id;
Status st = _readDeviceIdRaw(id);
if (!st.ok()) return st;
if (_variant != nullptr) return _validateActiveDeviceId(id);
// AUTO, not yet identified: any supported part is a valid presence result.
if (id.manufacturerId != cmd::MANUFACTURER_ID ||
    cmd::findVariantByProductId(id.productId) == nullptr) {
  return Status::Error(Err::DEVICE_ID_MISMATCH, "Unknown Device ID",
                       deviceIdDetail(id));
}
return Status::Ok();
```

Three lines shorter than the current version and removes the
`expectedVariant`-vs-`_variant` split. Update the `probe()` doc comment to say
it validates the active variant once one is selected.

### 2.9 OPEN - `failedChunkOffset` has opposite success conventions

`WriteResult::failedChunkOffset` is set to `bytesRequested` on success;
`TransferResult::failedChunkOffset` stays `0`. Same field name, opposite
meaning, and `cancelTransfer()`/`timeoutTransfer()`/`end()` overwrite it with
`_transfer.offset`, making it state-dependent.

**Proposal.** Initialise `_transfer.result.failedChunkOffset = length` in
`_requestTransfer()` so both structs mean "no failed chunk" the same way, and
document the cancel/timeout overwrite explicitly in the `TransferResult` field
comment. Two existing tests assert progress fields on cancelled transfers -
check `test_cancel_and_timeout_are_distinct_exactly_once_terminal_results` and
`test_cancel_retains_already_completed_read_prefix` before landing.

### 2.10 OPEN - Compatibility request IDs can collide with caller-chosen ones

`_allocateRequestId()` starts at 1 and walks up. An application that mixes
`requestVerifiedWrite(5, ...)` with the unqualified `requestRead(addr, buf, n)`
can be handed `5` for an unrelated later request, defeating the whole point of
`requestId` correlation. The existing collision guard cannot fire, because
`_requestTransfer()` already rejects both conditions it tests.

**Proposal.** Delete the dead guard and start `_nextRequestId` at
`0x80000000u`, so allocated IDs never alias the small integers applications
naturally choose. Document the reserved band on the unqualified overloads.

### 2.11 OPEN - Three redundant health filters

`_i2cWriteTrackedAddr`, `_i2cWriteReadTrackedAddr` and `_i2cSpecialTracked` each
filter a slightly different set of "not a transport failure" codes before
calling `_updateHealth()` - and the sets disagree (`UNSUPPORTED` is filtered in
one, not the others). It is harmless only because `shouldTrackHealthFailure()`
already excludes all of them.

**Proposal.** Delete all three ad-hoc filters and let
`shouldTrackHealthFailure()` be the single authority. Net removal of ~10 lines
and one class of future divergence.

### 2.12 OPEN - Undocumented return codes across the public surface

Nearly every public method can return `NOT_INITIALIZED` and
`BUSY(TRANSFER_ACTIVE)`; every memory and Device-ID method can return
`BUSY(ASLEEP/WAKING/SLEEP_STATE_UNKNOWN)`. Only the four `request*` overloads
document the BUSY contract. `bind()` does not document that it returns BUSY when
a terminal result is unconsumed, even though `end()` tells callers to expect it.

**Proposal.** Rather than repeating the list on ~40 methods, add one
`@note`-style paragraph to the class-level Doxygen block stating the two
universal preconditions, and reference it from the handful of methods where the
sleep gate is the surprising part (`readByte`, `writeByte`, `probe`, `recover`,
`readDeviceId`). Doxygen is configured strict, so this must not regress
`WARN_NO_PARAMDOC`.

---

## 3. Example transports

### 3.1 FIXED - Wire HAL lock leaked on the short-write path

`examples/common/I2cTransport.h`. `TwoWire::beginTransmission()` takes the Wire
HAL mutex and releases it only in a STOP-issuing `endTransmission()` or in
`requestFrom()`. Both short-write early returns in `wireWrite()` and
`wireWriteRead()` skipped both, leaving the mutex held. Any other task touching
`Wire` - including `Wire.end()` and `Wire.setBufferSize()` - then blocks on
`portMAX_DELAY` forever; the owning task self-heals only on its next fully
completed transaction.

Reachable in practice: `iface_reset` calls `Wire.end()` (which frees the TX
buffer) and then `Wire.begin()`; if `begin()` fails, the next memory command has
`write()` return 0 and leaks the lock. Also reachable on any port where the Wire
buffer is smaller than the requested length (item 3.3).

Fixed: both paths now issue `endTransmission(true)` before returning.

### 3.2 FIXED - Bus recovery drove SDA/SCL push-pull

The 9-pulse recovery exists precisely because a slave is stuck holding SDA low.
Driving SDA HIGH push-pull into that slave shorts both output stages together;
`digitalWrite(scl, HIGH)` does the same against clock stretching. The ESP-IDF
example already uses `GPIO_MODE_OUTPUT_OD` here, so the Arduino side was the
outlier.

Fixed: `OUTPUT_OPEN_DRAIN` on both lines, which also removes the
`INPUT_PULLUP` / `OUTPUT` flip-flopping.

### 3.3 OPEN - Transfer bounds taken from a library constant, not the Wire buffer

`I2cTransport.h` guards on `MB85RC::MAX_TRANSPORT_TX_BYTES` (128). That constant
describes the *core's* staging buffer and says nothing about what `TwoWire` can
carry. The real bound is `I2C_BUFFER_LENGTH`: 128 on ESP32, **32** on AVR/SAMD,
and user-settable down to 32 via `Wire.setBufferSize()`.

There is no silent truncation - `write()` returns a short count and
`requestFrom()` returns the real length, so both are caught - but the failure is
an opaque `IO_ERROR detail=32`, and on the write path it used to leak the lock
(3.1). The 128-vs-128 match on ESP32 is the only reason this works today.

**Proposal.** Publish the real bound from the adapter and derive `Config` from
it:

```cpp
// examples/common/I2cTransport.h
#if defined(I2C_BUFFER_LENGTH)
static constexpr size_t WIRE_BUFFER_BYTES = I2C_BUFFER_LENGTH;
#elif defined(BUFFER_LENGTH)
static constexpr size_t WIRE_BUFFER_BYTES = BUFFER_LENGTH;
#else
static constexpr size_t WIRE_BUFFER_BYTES = 32U;   // conservative floor
#endif
static constexpr size_t MAX_TX_BYTES =
    (WIRE_BUFFER_BYTES < MB85RC::MAX_TRANSPORT_TX_BYTES)
        ? WIRE_BUFFER_BYTES : MB85RC::MAX_TRANSPORT_TX_BYTES;
static constexpr size_t MAX_RX_BYTES =
    (WIRE_BUFFER_BYTES < MB85RC::MAX_TRANSPORT_RX_BYTES)
        ? WIRE_BUFFER_BYTES : MB85RC::MAX_TRANSPORT_RX_BYTES;
```

then use those in the two guards, and in the Arduino example replace the
hardcoded `126` / `124` with `transport::MAX_TX_BYTES` / `MAX_RX_BYTES`. This
also removes the unexplained divergence between the two examples (Arduino caps
at 126/124, the IDF example uses the 128/128 default for the same parts) - the
`124` RX cap in particular has no justification, since reads carry no address
prefix.

### 3.4 OPEN - Neither backend can produce a NACK code

This is one bug wearing two hats, and it means the core's transport taxonomy is
completely unexercised by both diagnostics.

**Arduino.** On ESP32, `endTransmission(false)` performs no I2C traffic at all -
it sets `nonStop` and returns 0. The combined transaction happens inside
`requestFrom()`, which returns only a byte count. So in `wireWriteRead()` the
`result != 0` branch is dead, and address NACK, data NACK, timeout and bus error
all arrive as `read != rxLen` and collapse to `IO_ERROR`. `Err::I2C_NACK_ADDR`,
`I2C_NACK_DATA`, `I2C_TIMEOUT` and `I2C_BUS` are never produced for any read.

**ESP-IDF.** `mapI2c()` maps `ESP_FAIL` to `TransportCode::BUS_ERROR`. But
`i2c_master_transmit()` documents `ESP_FAIL` as *"slave hasn't ACK the
transfer"* - it is the NACK path, and the most common failure during bring-up
(no device, wrong strap, wrong address). Reporting it as `Err::I2C_BUS`
("arbitration lost") is actively misleading in the one tool whose job is
bring-up. `ESP_ERR_INVALID_STATE` is unhandled and falls through to `IO_ERROR`.

The existing comment's reasoning - that IDF cannot say *which* byte NACKed - is
a good argument for choosing `NACK_ADDRESS` with `WriteCommit::INDETERMINATE`,
not for discarding the NACK classification.

**Proposal.** One shared `esp_err_t -> TransportCode` helper used by both
examples:

```cpp
if (err == ESP_FAIL || err == ESP_ERR_INVALID_STATE ||
    err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_RESPONSE) {
  // The backend cannot say which byte NACKed; report an address NACK and keep
  // the memory-write effect indeterminate rather than claiming "no bytes".
  return MB85RC::TransportResult::Error(
      MB85RC::TransportCode::NACK_ADDRESS, err, failureCommit);
}
if (err == ESP_ERR_TIMEOUT) {
  return MB85RC::TransportResult::Error(
      MB85RC::TransportCode::TIMEOUT, err, failureCommit);
}
```

On the Arduino side, classify from `TwoWire::lastError()` instead of from the
byte count. **This must land together with item 4.2**, which currently pins the
bug in place.

### 3.5 OPEN - Three CLI commands never call the driver

`examples/01_basic_bringup_cli/main.cpp`: `hs enter`, `sleep enter` and
`sleep wake` fabricate an `UNSUPPORTED` status and print it. They never call
`enterHighSpeedMode()`, `enterSleep()` or `wake()`. Meanwhile `hs exit` *does*
call `exitHighSpeedMode()`, so the family is half-wired, and the help text
claims behaviour three of the four do not have.

Three separate mechanisms freeze this in place: `check_cli_contract.py` requires
the tokens to exist (presence, never behaviour); `hil_runner.py` hardcodes
`UNSUPPORTED` as the *expected* Arduino result; `check_idf_example_contract.py`
cross-requires the same wording in the other example.

**Proposal.** Wire them to the driver as the IDF example already does, and let
the adapter's real limitation surface:

```cpp
if (cmd == "hs enter")    { printStatus(device.enterHighSpeedMode()); return; }
if (cmd == "sleep enter") { printStatus(device.enterSleep()); return; }
```

Note `wireSpecial()` returns `IO_ERROR` for HS/Sleep ops, which the core maps to
`Err::I2C_ERROR` and feeds to `_updateHealth()` - so "my adapter does not
implement this" would count as a physical bus failure and degrade
`DriverState`. `TransportCode` has no `UNSUPPORTED` member, so the cheapest
correct fix is for the CLI to check `snap.highSpeedModeSupported` /
`snap.sleepModeSupported` first and only call the driver when the variant
supports the feature. Then update item 4.1's expectations to accept `OK` or
`UNSUPPORTED`.

---

## 4. Tooling

### 4.1 OPEN - HIL expectations are profile-conditional, and three steps disable crash detection

`tools/hil_runner.py`. The IDF-profile expectations for `hs enter`,
`sleep enter` and `sleep wake` require `OK`. But `MB85RC256V` is neither HS- nor
Sleep-capable, so those steps would report FAIL on the documented 256V fixture.
The IDF profile has never been run against hardware, so this was never caught.

Separately, `HIL-015`, `HIL-017`, `HIL-017A`, `HIL-019` and `HIL-020` pass
`fail_tokens=(), fail_patterns=()` to stop the expected `UNSUPPORTED` from
tripping the default matcher. That also removes `"Guru Meditation"`,
`"assert failed"`, `"abort()"` and `"Traceback"` for those steps - a panic
during sleep entry would score PASS. (Target *resets* are still caught by
`RESET_PATTERNS`, so this is a hole rather than a blind spot.)

**Proposal.** Make the expectation capability-based, not profile-based:

```python
hs_enter_expected = (("High-speed mode:", "hs enter: OK"),
                     ("High-speed mode:", "UNSUPPORTED"))
```

and the same for both sleep steps. Then subtract only the conflicting pattern
instead of blanking the tuples:

```python
NO_UNSUPPORTED = tuple(p for p in DEFAULT_FAIL_PATTERNS
                       if "UNSUPPORTED" not in p.pattern)
```

keeping `fail_tokens=DEFAULT_FAIL_TOKENS` on all five steps. Once 3.5 lands, the
`if profile == "arduino"` split disappears entirely.

### 4.2 OPEN - A contract checker pins the NACK bug in place

`tools/check_idf_example_contract.py` requires the literal source text
`"MB85RC::TransportCode::IO_ERROR, err, failureCommit"`. That is a verbatim
assertion that the IDF backend must map NACK-ish errors to `IO_ERROR` - so
fixing 3.4 breaks CI. It is also brittle for its stated purpose: reflowing that
`return` across lines fails the check without changing behaviour.

**Proposal.** The intent - do not over-claim `NOT_COMMITTED` on an ambiguous
NACK - is worth keeping; the encoding is not. Replace both token lists with the
single meaningful invariant (forbid `WriteCommit::NOT_COMMITTED` inside
`mapI2c`'s NACK branch), or drop the guard and cover it with a native test
against a fake `esp_err_t` table.

### 4.3 OPEN - Contract scripts couple through `runpy`

`check_idf_example_contract.py` uses `runpy.run_path()` to scrape
`DEVICE_ID_IDF_TOKENS`, `IDF_REQUIRED_COMPONENTS` and `MODE_CONTRACT_TOKENS` out
of `check_cli_contract.py`, which does not use two of them itself. Worse,
`MODE_CONTRACT_TOKENS` is nine literal prose strings (e.g.
`"Core bus clock: unchanged"`) required to appear verbatim in *both* example
`.cpp` files - CI actively enforces copy-paste of prose across two sources.

**Proposal.** Move the shared data into `tools/_contract_data.py`, imported
normally by both scripts. Then, as part of item 5.1, move the nine mode-contract
strings into a shared example header as `constexpr const char*` arrays so both
checkers assert against one definition instead of two source files. Also drop
the redundant `DEVICE_ID_IDF_TOKENS` entry that is a strict prefix of another.

### 4.4 OPEN - `check_core_timing_guard.py` carries ~30 lines of unreachable code

`ALLOWED_CALL_COUNTS` is `{}`, so the comparison loop never executes and the
`expected = ALLOWED_CALL_COUNTS[rel]` branch is unreachable - every hit takes
the early `continue`. The effective policy is "zero forbidden timing calls in
`src/` and `include/`", which is the right policy.

**Proposal.** Delete `ALLOWED_CALL_COUNTS` and both comparison loops, replacing
them with a flat `errors.append(f"forbidden timing calls in {rel} -> {counts}")`.
Identical behaviour, ~30 fewer lines.

### 4.5 OPEN - `check_cli_contract.py` minor redundancy

`require_token()` matches anywhere in the file, so a command name appearing only
in a comment satisfies it; `require_dispatch()` does the real work.
`require_help()` returns immediately for `"?"`, so `?` is never checked in help.

**Proposal.** Drop `require_token()` and fix or remove the `"?"` special case.

---

## 5. Examples and tests

### 5.1 OPEN - ~1000 lines of semantic duplication between the two examples

Textual duplication is low (the two files use `Serial.printf`/`String` versus
`printf`/`char*`), but the *logic* duplication is large and has already drifted:
different scratch addresses, different read chunk sizes (256 vs 16), different
`MAX_STRESS_COUNT` (100000 vs 1000), and a confirmation-`!` model in one file
that is only partly mirrored in the other. A HIL run of one example therefore
proves little about the other.

Framework-independent twins include `crc32Update`, `takeStagedTerminal`,
`pollStagedTransferToCompletion`, `restoreVerified`, the range guard, the
enum-to-string helpers, and the whole `xfer_demo` / `stress` / `stress_mix` /
`randbench` / `typed_demo` / `rw_suite` suite.

**Proposal.** Add one framework-neutral `examples/common/DiagnosticCore.h`
taking a print sink:

```cpp
struct Sink { void (*printf)(void* ctx, const char* fmt, ...); void* ctx; };
```

and migrate in three stages, smallest risk first:

1. Pure helpers - `crc32Update`, `takeStagedTerminal`,
   `pollStagedTransferToCompletion`, `restoreVerified`, `rangeFits`,
   `sleepStateToStr`, `errToStr`, `deviceVariantToStr`, `addressModelToStr`.
   ~120 lines removed, zero behaviour change.
2. The nine `MODE_CONTRACT_TOKENS` strings plus
   `printHighSpeedSupport(sink, snap)` / `printSleepSupport(sink, snap)`,
   which also resolves item 4.3.
3. The six demo suites - ~600 lines collapse to one implementation and the two
   examples stop drifting.

`examples/common/` is already on the include path for the PlatformIO and native
environments; the IDF example's `main/CMakeLists.txt` needs
`INCLUDE_DIRS "." "../../.."`, which `check_idf_example_contract.py` currently
forbids and would need a specific allowance.

### 5.2 OPEN - Test coverage gaps that let these bugs survive

The core suite is genuinely strong - 156 tests, all registered, covering
`WriteCommit` normalization, short-completion rejection, staged provenance,
clock wrap and per-variant addressing. **No test encodes a wrong expectation
about the core.** The gaps are elsewhere:

- **The current-address boundary (item 1.1) is untested.** Every existing test
  reads well inside one bank. Add a case for MB85RC16V at `0x0FF -> 0x100`,
  MB85RC04V at `0x0FF`, and MB85RC1MT at `0xFFFF`, asserting both the byte
  returned and the encoded slave address. The `FakeBus` needs to model the
  datasheet composition rule (upper bits from the slave byte, low bits from its
  own buffer) rather than tracking a single linear pointer, or it will pass
  either way.
- **`wireSpecial` has zero coverage.** The reserved F8h/F9h Device ID sequence
  is the most protocol-specific thing in the adapter. Add one test asserting the
  `0x7C` address, the transmitted device-address word, the exact TX/RX
  completion counts, and that non-`READ_DEVICE_ID` ops are rejected.
- **The Wire stub cannot model the failure modes that bite.** It ignores the
  `stop` argument of `endTransmission()` (so ESP32's no-I/O `endTransmission(false)`
  is inexpressible - which is why 3.4 was invisible), always returns the full
  requested length from `requestFrom()` (so short reads are untestable), has no
  buffer bound (so 3.3 is untestable), and models no lock (so 3.1 is
  untestable). Adding `_openTransaction`, a `BUFFER_LENGTH`, and a
  `_requestReturnOverride` turns 3.1, 3.3 and 3.4 into one-line assertions.
- **`test_example_transport_maps_wire_errors` never checks `WriteCommit`**,
  even though `mapWireResult` deliberately picks `NOT_COMMITTED` for result 2
  and `INDETERMINATE` for 3/4/5 - the safety-critical half of the contract.

### 5.3 FIXED - small example defects

| Item | Change |
| --- | --- |
| `errToStr()` was missing `Err::NO_RESULT` and `Err::CANCELLED`, both reachable from `pollStagedTransferToCompletion` | Cases added |
| Comment claimed "both data limits are 124"; on one-byte-address variants the write limit is 125 | Comment corrected |

### 5.4 OPEN - remaining example defects

| Item | Location | Proposal |
| --- | --- | --- |
| `runReadWriteSuite` computes `tailAddr = maxAddress() - 7`; unbound, `maxAddress()` is 0 and this underflows to `0xFFFFFFF9` | Arduino CLI | Early-return when `capacityBytes() < 8` |
| `printCurrentAddressReadRange` range-checks the whole length once against the starting address, then loops while the pointer wraps | Arduino CLI | Re-check per chunk, or cap `len` to `capacity - currentAddress` |
| `gVerbose` is written but never read, and the parse is wrong (bare `verbose` toggles instead of reporting; `verbose 10` sets true) | IDF example | Delete it, or mirror the Arduino behaviour and actually gate the per-op prints |
| `StressStats::active` assigned, never read | Arduino CLI | Delete the field |
| CLI byte buffers are 126 bytes but the line reader caps at 127 chars (~24 byte tokens), so the "too many data bytes" branches are unreachable | Arduino CLI | Size to `MAX_LINE_LENGTH / 3`, or raise `MAX_LINE_LENGTH` |
| `interfaceReset()` operates on the global `Wire`, ignoring the `TwoWire*` passed as `i2cUser` | `I2cTransport.h` | Take `TwoWire&` and thread it through `initWire` and `iface_reset` |
| The `#else` branch of `interfaceReset()` calls `Wire.begin(sda, scl, freq)` and `Wire.setTimeOut()`, neither of which exists on AVR | `I2cTransport.h` | Guard the whole body, or document the header as ESP32-only |
| `wireSpecial`'s comment says Device ID is routed through the special callback so a normal backend "does not need to accept 0x7C", then calls `wireWriteRead(0x7CU, ...)` | `I2cTransport.h` | The wire sequence is correct. Reword to "reuses the Wire helper but is never registered as `Config::i2cWriteRead`" |
| `board::I2C_TIMEOUT_MS = 5` against a ~2.9 ms 128-byte transfer at 400 kHz leaves 1.7x margin before scheduling and clock stretching | `BoardConfig.h` | Raise to 10 ms, or state the margin in the comment |
| `BuildConfig.h` exists for a `LOG_LEVEL` flag no build ever sets | `examples/common/` | Add `-DLOG_LEVEL=` to a build env, or fold the three lines into `Log.h` (note `check_cli_contract.py` requires the file to exist) |

---

## 6. Documentation cleanup (done)

| Change | Rationale |
| --- | --- |
| Deleted `docs/reports/HIL_SUMMARY.md` (212 lines) and the now-empty `docs/reports/` | Point-in-time fixture logs - COM port numbers, esptool MCU revisions, heap byte counts, dirty-worktree hashes - shipped to every library consumer. It also named a specific unrelated firmware target, which a reusable library must not do. Half the file described runs superseded by later ones, and the `## Runs` table repeated the prose verbatim. Git history retains it. |
| Removed the ledger from `library.json` export, `check_package_contents.py`, `Doxyfile` INPUT, `README.md` (3 links), `docs/IDF_PORT.md`, `docs/RELEASE_CHECKLIST.md` | Keep the package and the docs build consistent |
| Retargeted `hil_runner.py` artifacts from `docs/reports/` to `.pio/hil/` and dropped the three matching `.gitignore` rules | Runner output was only in `docs/` to sit beside the ledger |
| Deleted README "Hardware Validation" (14 lines) | Hardcoded one fixture's 2026-08-01 soak numbers and a dirty commit hash on the front page; stale by construction |
| Deleted README "Diagnostics" + "State And Health" method inventories (40 lines), keeping the two genuinely non-obvious semantic notes | Hand-maintained lists already missing 12 real APIs, and directly contradicted by the "prefer the generated reference over copying method inventories" line above them |
| Removed "Latest published tag" and the pioarduino pin from the README header | `generate_version.py` only rewrites "Library version", so the tag line was hand-maintained and guaranteed to drift; the platform pin is stated better in the Validation section |
| Replaced smart quotes and em dashes with ASCII | README is the Doxygen main page and the repo standardised on ASCII-safe wording |
| Rewrote `scripts/pio.cmd`'s error message | It printed agent-prompt phrasing ("Stop and report the missing installation") to a human user's terminal |
| Reduced `AGENTS.md` to the PlatformIO wrapper note plus a link | The rest was generic git-workflow prompt text with no MB85RC content, duplicating `CONTRIBUTING.md` |
| `CODEOWNERS` -> `* @janhavelka` | Bare emails match no reviewer on a personal repo, so the file was inert; three of its five lines were the same owner repeated |
| `SECURITY.md`: 4.x-only support table, ASCII Yes/No, best-effort response | There is no 3.x maintenance branch, and the 48h/14d SLA was unenforceable. The emoji shortcodes rendered literally in Doxygen output |
| `Doxyfile`: dropped `*.cpp` from `FILE_PATTERNS`, deleted all five `EXCLUDE_PATTERNS`, removed the deleted ledger from INPUT, turned off `SOURCE_BROWSER` and the two cross-reference relations | `src/` is not in INPUT, so `*.cpp` matched nothing and the cross-reference graphs were near-empty; none of the five exclude patterns could ever match. Note `docs/RELEASE_CHECKLIST.md` must stay in INPUT: README and CONTRIBUTING link to it, and Doxygen resolves relative Markdown links only against its own INPUT set, so removing it fails the strict build |
| Removed `docs/prompts/` and `hil-validation-*.md` from `.gitignore` | A prompt-era directory that never existed and a pattern nothing produces |
| De-specified the two ~400-character HIL commands in `RELEASE_CHECKLIST.md` | They hardcoded `COM4`, `MB85RC256V`, `0x510`, `32768`, `124`, `28800`; replaced with the gate categories that must be set from the BOM |
| `DEVICE_REFERENCE.md`: removed the "replaces the older extracted Markdown dumps" meta-note, added the 04V/16V voltage condition, the per-variant device-pin counts, the one-16V-per-bus rule, and the two slave-byte addressing rules from item 1.1 | The bus-speed column was wrong for two parts, and the rule that made item 1.1 a bug was documented nowhere |

### 6.1 OPEN - remaining README verbosity

The README is 525 lines after the cuts above. Two sections are still
substantially self-repeating and are the best remaining maintainability win:

- **"Bounded Operation Classes"** (~70 lines) states the same `ceil(N/W)` chunk
  arithmetic three times for three call styles. **Proposal:** one paragraph
  defining the terms, one three-row table (steady-state / cooperative /
  whole-range) with a callbacks column, and keep the reconciliation paragraph
  verbatim - it is genuinely non-obvious and documented nowhere else.
- **"I2C Ownership And Concurrency"** (~31 lines) restates the `Config.h`
  callback contracts sentence for sentence. **Proposal:** keep the three-sentence
  ownership statement and the thread-safety paragraph, then link to `Config.h`.

Also duplicated across files: the validation command list (README /
`CONTRIBUTING.md` / `RELEASE_CHECKLIST.md` / `ci.yml`, already drifting on
whether PlatformIO 6.1.19 is a pin or a floor), the base-address rules
(README / `DEVICE_REFERENCE.md` / `Config.h`), and the IDF example command list
(README / `IDF_PORT.md` / `check_idf_example_contract.py`). Each should keep one
canonical home with links from the others.

---

## 7. Suggested order

1. **3.4 + 4.2 together** - the NACK taxonomy and the checker that pins it.
   Highest bring-up value; they cannot land separately.
2. **5.2's Wire stub work** - do it before 3.3 and 3.5 so those land with tests
   that would have caught them.
3. **5.2's current-address boundary test** - locks down the fix in item 1.1.
4. **3.3** - `transport::MAX_TX_BYTES`, which also resolves the unexplained
   126/124-vs-128/128 divergence between the two examples.
5. **3.5 + 4.1** - wire the stub CLI commands, then make the HIL expectations
   capability-based and restore crash detection.
6. **2.8 - 2.12** - core polish; all are removals or single-site changes.
7. **5.1** - the shared example header, in its three stages.
8. **4.3 - 4.5, 5.4, 6.1** - opportunistically.
