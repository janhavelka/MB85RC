# AGENTS.md - MB85RC Production Embedded Guidelines

## Role and Target
You are a professional embedded software engineer building a production-grade MB85RC256V FRAM memory library.

- Target: ESP32-S2 / ESP32-S3, Arduino framework, PlatformIO.
- Goals: deterministic behavior, long-term stability, clean API contracts, portability, no surprises in the field.
- These rules are binding.

---

## Repository Model (Single Library)

```
include/MB85RC/          - Public API headers only (Doxygen)
  CommandTable.h         - Device ID constants, address constants
  Status.h
  Config.h
  MB85RC.h
  Version.h              - Auto-generated (do not edit)
src/                     - Implementation (.cpp)
examples/
  01_*/
  common/                - Example-only helpers (Log.h, BoardConfig.h, I2cTransport.h,
                           I2cScanner.h, CommandHandler.h)
platformio.ini
library.json
README.md
CHANGELOG.md
AGENTS.md
```

Rules:
- `examples/common/` is NOT part of the library. It simulates project glue and keeps examples self-contained.
- No board-specific pins/bus in library code; only in `Config`.
- Public headers only in `include/MB85RC/`.
- Examples demonstrate usage and may use `examples/common/BoardConfig.h`.
- Keep the layout boring and predictable.

---

## Core Engineering Rules (Mandatory)

- Deterministic: no unbounded loops/waits; all timeouts via deadlines, never `delay()` in library code.
- Non-blocking lifecycle: `Status begin(const Config&)`, `void tick(uint32_t nowMs)`, `void end()`.
- No heap allocation in steady state (no `String`, `std::vector`, `new` in normal ops).
- No logging in library code; examples may log.
- No macros for constants; use `static constexpr`. Macros only for conditional compile or logging helpers.
- Prefer simplicity, clarity, correctness, robustness, safety, and readability over clever abstractions or speculative flexibility.
- Before coding, inspect whether existing code can be simplified, reused, or deleted.
- Prefer deleting unnecessary code over adding code.
- Prefer extending existing owners/modules/API contracts over creating parallel abstractions.
- Add a new service, class, file, interface, or abstraction only for a concrete current need with a clear caller or test.
- Do not add placeholder classes, future stubs, empty managers, broad frameworks, plugin systems, registries, or generic layers unless the current task explicitly requires them.
- Keep changes tightly scoped to the user's request.
- Preserve dirty user changes; never revert unrelated work.
- No unbounded waits, retries, loops, allocations, queues, or buffers in steady paths.
- Every hardware operation that can block must have a timeout and an observable failure path.
- Recovery logic must be bounded, deterministic, and testable.
- Prefer explicit state, explicit ownership, and small local helpers over hidden global state.
- Do not hide hardware failures behind silent retries or fake success.
- Avoid dynamic allocation in steady embedded paths unless it is already an accepted local pattern and the bound is clear.

---

## I2C Manager + Transport (Required)

- The library MUST NOT own I2C. It never touches `Wire` directly.
- The I2C bus MUST have one clear owner.
- Device drivers MUST NOT directly own or reconfigure a shared bus unless this repository's architecture explicitly says so.
- `Config` MUST accept a transport adapter (function pointers or abstract interface).
- Transport errors MUST map to `Status` (no leaking `Wire`, `esp_err_t`, etc.).
- The library MUST NOT configure bus timeouts or pins.
- I2C transactions MUST be timeout-bounded and report errors clearly.
- Do not implement chip protocols manually when an existing hardened project library already provides the needed timeout, recovery, and testability behavior.
- Keep chip-level protocol code inside the driver/wrapper. Keep application policy outside the chip driver.
- Do not add fake devices, simulated buses, or test doubles to production paths.

## Framework Boundaries (Mandatory)

- Core/public headers and `src/` MUST NOT require Arduino or ESP-IDF framework headers.
- Arduino examples may use Arduino APIs.
- ESP-IDF examples MUST be native IDF applications using `app_main`, `driver/i2c_master.h`, `esp_timer`, `vTaskDelay`, and fixed C buffers or native console APIs.
- ESP-IDF examples MUST NOT include Arduino CLI sources or use Arduino compatibility facades such as `ArduinoCompat`, `IdfArduinoCompat`, `Arduino.h`, `Wire.h`, `String`, `Serial`, or `TwoWire`.
- Maintain CLI command parity through repo-local command contracts/checkers, not by sharing Arduino implementation source.

---

## Status / Error Handling (Mandatory)

All fallible APIs return `Status`:

```cpp
struct Status {
  Err code;
  int32_t detail;
  const char* msg;  // static string only
};
```

- Silent failure is unacceptable.
- No exceptions.

---

## MB85RC256V Reference Requirements

- I2C address configurable: 0x50–0x57 (3 address pins A0, A1, A2).
- For MB85RC256V, check device presence in `begin()` by reading Device ID.
- For MB85RC256V, verify Manufacturer ID (0x00A) and Product ID (0x510). Family variants use their own product IDs, and no-Device-ID variants require explicit selection.
- 32,768 bytes (256 Kbit), 15-bit addressing (0x0000–0x7FFF).
- No write delay (FRAM writes are immediate — no EEPROM-style polling).
- No page boundary limitations — device sequential writes auto-increment through the entire array.
- The FRAM device protocol can roll over at the physical array end, but public bulk APIs in this driver must reject cross-capacity ranges unless an explicit wrap API is added, documented, and tested.
- Write protection via hardware WP pin (not software-controlled).
- Support operations:
  - **Byte Write**: write single byte at specified address
  - **Sequential Write**: write multiple bytes starting at address (auto-increment)
  - **Random Read**: read single byte at specified address
  - **Sequential Read**: read multiple bytes starting at address (auto-increment)
  - **Device ID Read**: 3-byte read-only identifier via reserved I2C addresses 0xF8/0xF9
- MSB of high address byte must always be 0.
- Current address is undefined after power-on.

---

## Driver Architecture: Managed Synchronous Driver

The driver follows a **managed synchronous** model with health tracking:

- All public I2C operations are **blocking** (no async needed — FRAM has no write delays).
- `tick()` is a no-op for this device (reserved for future use or application-level periodic tasks).
- Health is tracked via **tracked transport wrappers** -- public API never calls `_updateHealth()` directly.
- Recovery is **manual** via `recover()` - the application controls retry strategy.

### DriverState (4 states only)

```cpp
enum class DriverState : uint8_t {
  UNINIT,    // begin() not called or end() called
  READY,     // Operational, consecutiveFailures == 0
  DEGRADED,  // 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    // consecutiveFailures >= offlineThreshold
};
```

State transitions:
- `begin()` success -> READY
- Any I2C failure in READY -> DEGRADED
- Success in DEGRADED/OFFLINE -> READY
- Failures reach `offlineThreshold` -> OFFLINE
- `end()` -> UNINIT

### Transport Wrapper Architecture

All I2C goes through layered wrappers:

```
Public API (read, write, readDeviceId, etc.)
    ↓
Register/memory helpers (readMemory, writeMemory)
    ↓
TRACKED wrappers (_i2cWriteReadTracked, _i2cWriteTracked)
    ↓  <- _updateHealth() called here ONLY
RAW wrappers (_i2cWriteReadRaw, _i2cWriteRaw)
    ↓
Transport callbacks (Config::i2cWrite, i2cWriteRead)
```

**Rules:**
- Public API methods NEVER call `_updateHealth()` directly
- Memory read/write helpers use TRACKED wrappers -> health updated automatically
- `probe()` uses RAW wrappers -> no health tracking (diagnostic only)
- `recover()` tracks probe failures (driver is initialized, so failures count)

### Health Tracking Rules

- `_updateHealth()` called ONLY inside tracked transport wrappers.
- State transitions guarded by `_initialized` (no DEGRADED/OFFLINE before `begin()` succeeds).
- NOT called for config/param validation errors (INVALID_CONFIG, INVALID_PARAM).
- NOT called for precondition errors (NOT_INITIALIZED).
- `probe()` uses raw I2C and does NOT update health (diagnostic only).

### Health Tracking Fields

- `_lastOkMs` - timestamp of last successful I2C operation
- `_lastErrorMs` - timestamp of last failed I2C operation
- `_lastError` - most recent error Status
- `_consecutiveFailures` - failures since last success (resets on success)
- `_totalFailures` / `_totalSuccess` - lifetime counters (wrap at max)

---

## Versioning and Releases

Single source of truth: `library.json`. `Version.h` is auto-generated and must never be edited.

SemVer:
- MAJOR: breaking API/Config/enum changes.
- MINOR: new backward-compatible features or error codes (append only).
- PATCH: bug fixes, refactors, docs.

Release steps:
1. Update `library.json`.
2. Update `CHANGELOG.md` (Added/Changed/Fixed/Removed).
3. Update `README.md` if API or examples changed.
4. Commit and tag: `Release vX.Y.Z`.

---

## Naming Conventions

- Member variables: `_camelCase`
- Methods/Functions: `camelCase`
- Constants: `CAPS_CASE`
- Enum values: `CAPS_CASE`
- Locals/params: `camelCase`
- Config fields: `camelCase`

---

# MB85RC hardening rules

- Core code under `include/` and `src/` must remain framework-neutral: no Arduino, Wire, ESP-IDF, FreeRTOS, logging framework, global buses, hidden delays, or heap-heavy framework types.
- The MB85RC core must not own the I2C bus. Bus ownership, locking, timeouts, retry policy, and recovery policy belong in the injected transport or application bus manager.
- FRAM writes are not EEPROM writes: do not add ACK polling or post-write programming delays unless a specific datasheet variant requires it.
- Multi-chunk write/fill operations are not atomic. Public docs and tests must make prefix-commit behavior explicit.
- I2C ACK on a write does not prove persistence when the hardware WP pin is high. Critical data paths must use verify or application-level journaling.
- Device-ID support is variant-specific. Do not require Device ID for variants that do not support it.
- Current-address read semantics must be conservative after power-up, failed transactions, or address-setting uncertainty.
- Public APIs are not ISR-safe if they can perform I2C or block.
- Instances are not internally thread-safe unless explicitly changed and tested; external serialization is required.
- Do not claim pure ESP-IDF or hardware validation unless the exact commands were run and recorded.
