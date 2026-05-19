# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- ESP-IDF component metadata and a pure ESP-IDF `examples/espidf_basic` build of the full bring-up CLI.
- `examples/common/IdfArduinoCompat.h` example shim that provides the small Arduino surface used by the CLI while routing I2C through ESP-IDF v6 `i2c_master_*` APIs.
- ESP-IDF port notes in `docs/IDF_PORT.md`.
- ESP-IDF port implementation notes in `docs/IDF_PORT_IMPLEMENTATION.md`.

### Changed

- Core time fallback is now platform-aware: Arduino/native test builds use `millis()`, while ESP-IDF builds use `esp_timer_get_time()`.
- Example helpers now gate Arduino headers behind `MB85RC_EXAMPLE_PLATFORM_IDF` so the same CLI source can compile for both frameworks.
- `library.json` now declares both `arduino` and `espidf` framework support.
- Doxygen input now covers the ESP-IDF port notes, implementation notes, shared CLI source, native IDF entry point, and example-only IDF shims.
- The ESP-IDF example adapter now performs Device ID transactions on reserved address `0x7C` through ESP-IDF defined I2C operations with manual address bytes instead of relying on normal device-handle addressing for a reserved address.
- `tools/check_cli_contract.py` now validates the ESP-IDF wrapper macro, shared-source include, and required CMake dependencies.
- The ESP-IDF CLI parity is structural through shared source; pure IDF `idf.py` builds and hardware validation remain pending until an IDF toolchain and target hardware are available.

## [1.1.1] - 2026-05-17

### Changed

- Polished bringup CLI serial monitor output for successful diagnostic/demo commands.
- Stress progress now keeps color only on `ok` and `fail` counters.
- Stress summaries now label write/read/verify throughput as cycles, mixed-operation throughput as commands, and health counter changes as tracked I2C health deltas.
- `randbench` and `typed_demo` now use compact `[PASS]` status lines on success while preserving detailed `Status` output for failures.
- The bringup loop now uses the shared bounded CLI shell helper for cleaner prompt/output separation.
- The `stress` command parser now accepts only `stress` or `stress <count>`, matching CLI help.
- Public Doxygen comments now document all `SettingsSnapshot` fields and clarify that `BUSY` also represents latched `OFFLINE`.
- Release documentation now includes `docs/releases/v1.1.0.md` and `docs/releases/v1.1.1.md`.

## [1.1.0] - 2026-05-14

### Added
- Public MB85RC family variant metadata, address-model descriptors, and `findVariantByProductId()`.
- Convenience `driverState()` alias and value-returning `getSettings()` overload for cross-library diagnostics.
- Native coverage proving latched `OFFLINE` blocks normal I2C operations without touching the bus while `recover()` remains the explicit recovery path.

### Changed

- Documented validation behavior for null buffers, zero-length transfers, out-of-range addresses, and current-address tracking.
- Reference documentation now uses human-readable vendor PDF names and separates compact chip notes from full PDF extractions under `docs/extracted-md/` and `docs/pdf-extracted-md/`.
- Explicit recovery bypass internals now use the shared `ScopedOfflineI2cAllowance` / `_reassertOfflineLatch()` procedure so failed recovery attempts that begin from `OFFLINE` keep the latch asserted.
- Health behavior is now standardized on latched `OFFLINE`: normal public I2C operations return `BUSY` with `Driver is offline; call recover()` and do not touch I2C until `recover()` succeeds.

### Fixed

- Made `recover()` record Device ID mismatches in health tracking instead of returning a semantic error without updating driver state.
- Guarded health updates against `IN_PROGRESS` statuses and added native coverage for Device ID mismatch and validation no-bus-touch paths.

## [1.0.0] - 2026-04-07

### Added
- First stable release of the MB85RC256V FRAM library.
- Production-grade MB85RC256V driver with chunked `read()`, `write()`, `writeByte()`, and `fill()` support.
- Injected I2C transport architecture via `Config::i2cWrite` and `Config::i2cWriteRead`, with no direct `Wire` dependency in the library core.
- Deterministic managed-synchronous lifecycle with `begin()`, `tick()`, and `end()`.
- Health tracking with `READY`, `DEGRADED`, and `OFFLINE` driver states plus lifetime success/failure counters and timestamps.
- Runtime inspection helpers: `SettingsSnapshot`, `getSettings()`, `isInitialized()`, and `getConfig()`.
- Device ID verification on `begin()` plus public `readDeviceId()` and `readDeviceIdRaw()` helpers.
- Current-address-read support, including multi-byte current-address reads and tracked pointer rollover behavior.
- `verify()` helper for comparing FRAM contents against expected bytes without inventing synthetic transport errors.
- Single bundled bringup CLI example with memory inspection, diagnostics, stress tests, read/write validation, random-access benchmarking, and typed-value demo commands.
- Example-only `examples/common/TypedMemory.h` helpers for explicit little-endian fixed-width integers, `float`, `double`, and `bool`.
- Native Unity test coverage for lifecycle, health tracking, rollover behavior, current-address tracking, verify logic, random-access flows, and typed helper coverage.
- GitHub Actions CI pipeline with ESP32-S3/S2 builds, native tests, version header check, core timing guard, CLI contract enforcement, and library package validation.
- Doxygen configuration and refreshed release documentation for the stable API surface.
- MIT License.

### Changed
- The bringup CLI now serves as the single example entry point and bundles `rw_suite`, `randbench`, and `typed_demo` instead of splitting them into separate example applications.
- Numeric argument parsing in the example CLI is stricter and bounded for better diagnostic behavior on real hardware.
- README, portability notes, and release docs now reflect the finalized `v1.0.0` behavior and installation flow.
- Public header comments were normalized to ASCII-safe wording so generated Doxygen output is clean and stable.

### Fixed
- `readDeviceId()` health tracking now flows only through tracked transport wrappers.
- `begin()` and `end()` now clear stale runtime/config snapshots instead of leaking old state after shutdown or failed re-initialization.
- Internal memory helpers now reject null and zero-length misuse paths before touching fixed buffers.
- The example `Wire` transport now supports read-only transactions required by current-address reads.
- `stress_mix` no longer schedules `currentAddr` immediately after `recover()`, which intentionally invalidates the current-address state.
- README device characteristics and documentation references were aligned with the validated MB85RC256V datasheet behavior.

[Unreleased]: https://github.com/janhavelka/MB85RC/compare/v1.1.1...HEAD
[1.1.1]: https://github.com/janhavelka/MB85RC/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/janhavelka/MB85RC/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/janhavelka/MB85RC/releases/tag/v1.0.0
