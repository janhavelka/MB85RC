# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/janhavelka/MB85RC/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/janhavelka/MB85RC/releases/tag/v1.0.0
