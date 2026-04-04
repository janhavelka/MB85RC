# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
