# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Runtime settings snapshot and current-address-read coverage are now validated by the native test suite, including rollover and chunked transfer paths.
- The bringup CLI now includes `hexdump`, `text`, `strings`, and `crc` commands for practical on-hardware memory inspection.
- Added raw Device ID access, multi-byte current-address reads, and a `verify()` helper for content comparison.
- Added `examples/common/TypedMemory.h`, an example-only fixed-width codec layer for little-endian integers, `float`, `double`, and `bool`.
- Added native coverage for deterministic random-access write/read/verify flows and the typed helper layer.

### Changed
- `getSettings()` now reports the cached runtime snapshot even before `begin()` so the example CLI can inspect defaults without extra I2C.
- The bringup CLI now uses stricter numeric parsing and bounded stress-count validation for closer parity with the stronger sibling libraries.
- The bringup CLI now exposes `idraw` and `verify`, and `current <len>` uses the library bulk current-address helper.
- The single bringup CLI example now bundles the read/write suite, random benchmark, and typed-value demo instead of splitting them into separate example entry points.

### Fixed
- `readDeviceId()` health tracking now goes through the tracked transport wrapper path instead of calling `_updateHealth()` manually.
- README device characteristics now match the MB85RC256V documentation for endurance and retained-data wording.
- `begin()` and `end()` now clear stale runtime/config snapshots instead of leaking old state after shutdown or failed re-initialization.
- Internal memory helpers now defensively validate null/zero-length misuse paths before touching fixed buffers.

### Added
- `SettingsSnapshot`, `getSettings()`, `isInitialized()`, and `getConfig()` for runtime/config inspection
- `readCurrentAddress()` plus CLI `current` / `cur` support for the documented current-address read flow
- Wrap-aware CLI `dump` alias and rollover-friendly read/write/fill example behavior
- Native tests for rollover, current-address tracking, settings snapshots, and read-only transport calls

### Fixed
- Device ID tracked reads now update health only through tracked transport wrappers
- Example `Wire` transport now supports read-only transactions required by current-address reads
- README documentation now points to the correct implementation-manual path and documents transport-owned bus reset handling

## [1.0.0] - 2026-04-04

### Added
- **First stable release**
- Complete MB85RC256V FRAM driver with chunked read/write/fill
- Injected I2C transport architecture (no Wire dependency in library)
- Health monitoring with automatic state tracking (READY/DEGRADED/OFFLINE)
- Device ID verification (manufacturer, product, density) on `begin()`
- Configurable I2C address (0x50-0x57 via A0/A1/A2 pins)
- Non-blocking tick-based architecture (tick is no-op for FRAM)
- Probe and recover diagnostics with proper health tracking separation
- Chunked I/O with bounded I2C transactions (MAX_WRITE_CHUNK=126, MAX_READ_CHUNK=128)
- `fill()` operation for bulk memory initialization
- `stress_mix` mixed-operation stress test in CLI
- Basic CLI example (`01_basic_bringup_cli`)
- Comprehensive Doxygen documentation in public headers
- Native test suite with Unity
- MIT License

[Unreleased]: https://github.com/janhavelka/MB85RC/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/janhavelka/MB85RC/releases/tag/v1.0.0
