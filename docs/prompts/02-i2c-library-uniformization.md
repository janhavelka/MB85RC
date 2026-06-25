# MB85RC I2C Uniformization Prompt

Repository: `MB85RC`

Absolute path: `C:\Users\Honza\Documents\Projects\MB85RC`

## Execution Rules

You are working inside this single repository. Implement this prompt directly;
do not repeat the cross-repository audit.

You may spawn subagents for read-only inspection of APIs, tests, I2C
transactions, docs, and diagnostics. Keep final judgment, edits, and
verification in the main agent.

Prefer simple, robust, readable code. Before adding code, inspect whether
existing code can be simplified, reused, tightened, or deleted.

Preserve dirty user changes. Do not commit unless explicitly asked.

## Common Uniformization Target

Apply this shared I2C library contract: injected non-owning transport, `Status` returns, cache-only `getSettings(SettingsSnapshot&) const`, active `probe()`/diagnostics named explicitly, `DriverState` with `state()` and `driverState()`, `isOnline()`, `lastOkMs()`, `lastErrorMs()`, `lastError()`, `consecutiveFailures()`, `totalFailures()`, and `totalSuccess()`.

Keep the common `Err` vocabulary append-only where missing: `OK`, `NOT_INITIALIZED`, `INVALID_CONFIG`, `INVALID_PARAM`, `I2C_ERROR`, `I2C_NACK_ADDR`, `I2C_NACK_DATA`, `I2C_TIMEOUT`, `I2C_BUS`, `DEVICE_NOT_FOUND`, `TIMEOUT`, `BUSY`, and `IN_PROGRESS`. Preserve MB85RC-specific device-ID, address-range, write-protect, verify, and memory behavior.

Uniformization is not a new base class or framework. Make only local, source-compatible additions and tests.

## Current State

- Public lifecycle and health are in `include\MB85RC\MB85RC.h`: `SettingsSnapshot` at line 54, `isInitialized()` at line 200, device ID snapshot access at line 223, `getSettings()`/`getSettings(SettingsSnapshot&)` at lines 307-312, and common health/state accessors including `state()`, `driverState()`, `isOnline()`, `lastOkMs()`, `lastErrorMs()`, `lastError()`, `consecutiveFailures()`, `totalFailures()`, and `totalSuccess()` around lines 323-459.
- The memory API tracks current-address pointer knowledge and invalidates it around raw access; see `include\MB85RC\MB85RC.h:78`, `:459-473`, and `src\MB85RC.cpp:1275`.
- Native tests cover example transport mapping, read-only transactions, health transitions, and memory sizing; `pio test -e native` passed 115 tests.
- No public register helper API is needed for this memory driver; address/data transactions are the real contract.
- No explicit HIL runner was found.

## Best Sources To Adapt

- Keep MB85RC's pointer/cache invalidation as the memory-specific equivalent of dirty-state diagnostics.
- Use BME280/SHT3x health snapshots as the comparison source for verifying that `SettingsSnapshot` carries common health fields and last/root error state, not just memory pointer state.
- For HIL, adapt BME280/SHT3x runner structure only if the examples expose a real CLI with non-destructive read/write/verify commands.
- For health vocabulary, MB85RC already matches the common `driverState()`, `lastOkMs()`, `lastError()`, `totalSuccess()` style.

## Implementation Tasks

1. Do not add generic `readRegister()`/`writeRegister()` APIs. They do not match FRAM semantics and would create a misleading abstraction.
2. Ensure all public raw/memory operations that disturb the current-address pointer invalidate `currentAddressKnown` in `SettingsSnapshot`.
3. Verify that `state()`, `driverState()`, `isOnline()`, `lastOkMs()`, `lastErrorMs()`, `lastError()`, `consecutiveFailures()`, `totalFailures()`, `totalSuccess()`, and `getSettings(SettingsSnapshot&) const` are all present; add missing aliases only as non-breaking wrappers.
4. Confirm `getSettings()` and `getSettings(SettingsSnapshot&)` remain cache-only and include common health fields plus memory-specific pointer/device-ID fields.
5. Audit every wait/poll path, including write-cycle/verify/recover flows, for finite timeout bounds and visible status returns. Normal memory APIs must not hide retries; recovery remains explicit and application-scheduled.
6. Review README and Doxygen so write protection, ACK-vs-persisted data, verify behavior, and current-address pointer invalidation are explicit.
7. Add HIL automation only if the repo has a safe fixture and CLI. If present, cover the common minimum contract: `version`, `scan`, `probe`, `settings`, `health`, failure-token classification, dry-run/parser test support, and non-destructive default commands. Destructive writes require an explicit scratch address/range.

## API Changes Required

- None expected.

## Simplifications Before Adding Code

- Prefer documenting memory-specific pointer invalidation over adding a generic dirty-state field.

## Tests To Add Or Update

- Native test for any newly documented pointer invalidation case.
- Native test proving `getSettings()` is bus-silent.
- HIL parser tests only if a real runner is added.

## Commands To Run

- `pio test -e native`
- `pio run -e esp32s3dev`

## Constraints And Non-Goals

- Do not add register-style APIs.
- Do not add destructive HIL writes without explicit scratch-range configuration.
- Do not drive write-protect pins from the core driver.
- Injected transport only: no global `Wire`, new bus manager, pin ownership, or shared bus reset from the device driver.
- Preserve distinct timeout, address NACK/device-not-found, data NACK, bus, device-ID, address-range, write-protect, and verify statuses. Do not collapse them into generic `I2C_ERROR` or use `DEVICE_NOT_FOUND` for timeout/data/bus failures.

## Risks And Open Questions

- Open: whether a safe standardized HIL fixture exists for destructive write/verify tests across MB85RC variants.
