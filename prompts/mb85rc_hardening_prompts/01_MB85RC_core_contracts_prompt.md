# Prompt 01 — MB85RC Core Contracts: Copy/Move, Thread/ISR, Bounds, Current Address

You are continuing on branch:

```text
hardening/mb85rc-industry-readiness
```

This is Prompt 01. Do not implement partial-write extended APIs yet; that is Prompt 02. Keep this phase focused on low-risk core contracts and documentation.

## Start

```bash
git status --short
git branch --show-current
git pull --ff-only
```

If dirty or pull fails, stop and report.

## Subagents

Spawn:

1. `core-contracts-agent`
2. `fram-semantics-agent`
3. `tests-contract-agent`
4. `integration-review-agent`

Ask them to inspect first, then implement only grounded changes.

## Goals

Implement and test the core contract items that are low-risk and should not require API redesign:

1. Explicitly delete copy and move operations for the `MB85RC` driver class.
2. Add public-header and README contracts for:
   - not thread-safe without external serialization;
   - not ISR-safe;
   - transport callbacks must not recursively call into the same instance;
   - core does not own the I2C bus;
   - current-address reads are only safe after a known address-setting transaction.
3. Confirm zero-length, address+length overflow, and out-of-range behavior are deterministic and tested.
4. Confirm failed transactions invalidate or conservatively handle current-address cache/state.
5. Confirm no framework leakage into `include/` or `src/`.

## Implementation details

### 1. Delete copy/move

In the main public class header, add:

```cpp
MB85RC(const MB85RC&) = delete;
MB85RC& operator=(const MB85RC&) = delete;
MB85RC(MB85RC&&) = delete;
MB85RC& operator=(MB85RC&&) = delete;
```

Use the actual class name and namespace.

Add native compile-time checks where practical:

```cpp
static_assert(!std::is_copy_constructible_v<MB85RC::MB85RC>);
static_assert(!std::is_copy_assignable_v<MB85RC::MB85RC>);
static_assert(!std::is_move_constructible_v<MB85RC::MB85RC>);
static_assert(!std::is_move_assignable_v<MB85RC::MB85RC>);
```

Adapt namespace/name to the repository.

### 2. Public contracts

Add Doxygen near the class and relevant APIs.

Required wording, adapted to style:

```text
MB85RC instances are not internally thread-safe. Use one task or provide external serialization around all public methods that can touch driver state or I2C. Public APIs are not ISR-safe because they can call I2C transport callbacks and may block until the transport timeout. Transport callbacks must not recursively call back into the same MB85RC instance.
```

Add README section for:
- concurrency;
- ISR safety;
- shared-bus ownership;
- current-address read caveats.

### 3. Boundary behavior

Audit and test:
- `read(address, nullptr, len)` if applicable;
- `write(address, nullptr, len)` if applicable;
- zero-length read/write/fill/verify;
- `address + length` overflow;
- operations ending exactly at capacity;
- operations crossing capacity;
- current-address read before any known address set;
- current-address read after failed random read/write;
- current-address state after successful random read/write, if the library exposes this.

Do not invent behavior. If current behavior is already sensible, lock it with tests and docs.

### 4. No framework leakage

Run/strengthen guard if needed. The core should not include:
- `Arduino.h`;
- `Wire.h`;
- ESP-IDF headers;
- FreeRTOS headers;
- logging macros;
- framework `String`;
- global bus objects.

Do not move framework-specific example code into core.

## Tests to add/update

Add native tests for:

1. Copy/move deletion.
2. Thread/ISR contract docs are present if a contract guard exists.
3. Zero-length operation behavior.
4. Address overflow and exact-end boundaries.
5. Current-address invalidation after failed transaction.
6. No core framework leakage guard still passes.

## Verification commands

Run:

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
```

Run package validation if quick:

```bash
python -m platformio pkg pack
```

Remove generated package artifacts after validation unless the repo intentionally keeps them.

## Report update

Update `docs/MB85RC_HARDENING_FINAL_REPORT.md`:

- mark Phase 01 complete;
- list exact API changes;
- list tests added;
- list exact commands and results;
- list remaining concerns.

Do not claim hardware validation.

## Commit and sync

If checks pass:

```bash
git add include src test README.md docs tools
git commit -m "fix: tighten MB85RC core contracts"
git push
```

If some checks cannot run, decide whether the phase is still safe to commit. Record exact limitations.

## Stop condition

Stop after this prompt and summarize:
- root cause addressed;
- files changed;
- tests passed/failed;
- commit hash;
- push result;
- what Prompt 02 should handle next.
