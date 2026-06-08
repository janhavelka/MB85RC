# Time Ownership & Timer Domain Audit

**Repository:** MB85RC  
**Date:** 2026-05-26  
**Scope:** All modules — library core, examples, example-common helpers, tools

---

## 1. Time Owner Per Component

| Component | Time owner | Clock source |
|---|---|---|
| `src/MB85RC.cpp` / `include/MB85RC/` (library core) | **Application** — time is injected | `Config::nowMs` callback (`NowMsFn`), or `0` when not supplied |
| `examples/01_basic_bringup_cli/main.cpp` | **Self** — calls `millis()` directly for both `exampleNowMs` (injected into `Config::nowMs`) and local display/stress timing | `millis()` (Arduino) |
| `examples/espidf_basic/main/main.cpp` | **Self** — local `nowMs()` function calls `esp_timer_get_time()` | `esp_timer_get_time()` (IDF `int64_t` µs), truncated to `uint32_t` ms |
| `examples/common/HealthDiag.h` | **Self** — calls `millis()` directly in `printHealthVerbose()`, `HealthSnapshot::capture()`, `HealthMonitor::tick()` | `millis()` (Arduino) |
| `examples/common/HealthView.h` | None — no timestamps | — |
| `examples/common/I2cTransport.h`, `I2cScanner.h` | None — `delayMicroseconds()` only in bus-recovery, not a clock domain | Arduino `delayMicroseconds()` |

---

## 2. Injected vs Internally-Read Clock Sources

### Library core (`src/`, `include/MB85RC/`)

- `_nowMs()` in `src/MB85RC.cpp` calls only `_config.nowMs(_config.timeUser)` or returns `0U`.
  No framework headers are in scope for the core.
- `tick(uint32_t nowMs)` accepts injected time and performs bounded maintenance only. After Phase 06 it advances Sleep wake recovery from `WAKING` to `AWAKE`; it still performs no async I2C work and adds no FRAM write delay.
- The timing guard tool `tools/check_core_timing_guard.py` enforces this:
  `millis`, `micros`, `esp_timer_get_time`, `delayMicroseconds` are **forbidden** in `src/` and `include/`.

### Arduino example (`examples/01_basic_bringup_cli/main.cpp`)

- `exampleNowMs()` reads `millis()` and injects it into `Config::nowMs`. Correct.
- `printDriverHealth()` also calls `millis()` directly to compute `now - lastOkMs` / `now - lastErrorMs`.
  Uses the same `millis()` source as the injected hook — **consistent**.
- `resetStressStats()` / `finishStressStats()` call `millis()` directly for benchmark timing.

### IDF example (`examples/espidf_basic/main/main.cpp`)

- `nowMs()`: `static_cast<uint32_t>(esp_timer_get_time() / 1000LL)`
  - The native counter is `int64_t` microseconds (wraps in ~292,000 years).
  - Division to milliseconds and cast to `uint32_t` **truncates to 32 bits** — wraps at ~49.7 days.
- `gFram.tick(nowMs(nullptr))` in the main loop passes the same truncated value.

### `examples/common/HealthDiag.h` — cross-source issue

- `printHealthVerbose()`: `uint32_t now = millis()`.
  Then `now - lastOk` / `now - lastFail` — where `lastOk`/`lastFail` come from
  `driver.lastOkMs()`, which were written by the injected `Config::nowMs` callback.
- If an application injects a clock source other than `millis()` (e.g. a scaled clock, a
  simulator, or the IDF `esp_timer`-based `nowMs()`), `now - lastOkMs` produces **garbage**
  because the two values originate from different domains.
- The same issue exists in `HealthMonitor::tick()` and `HealthSnapshot::capture()`.

---

## 3. 32-bit Timer Risks

| Location | Field | Width | Risk |
|---|---|---|---|
| `include/MB85RC/MB85RC.h` (private state) | `_lastOkMs`, `_lastErrorMs` | `uint32_t` | Wraps at 49.7 days; sourced from injected callback which is also `uint32_t` — wraps in sync with the source |
| `include/MB85RC/MB85RC.h` (public API) | `lastOkMs()`, `lastErrorMs()` | `uint32_t` | Callers receive a raw wrapped value after rollover; `now - lastOkMs()` is still wrap-safe via unsigned subtraction |
| `examples/espidf_basic/main/main.cpp:41` | Local `nowMs()` return | `int64_t` → `uint32_t` | Truncation to `uint32_t` is intentional (matches `NowMsFn` contract) but means the injected clock wraps at 49.7 days even though the underlying IDF counter does not |
| `examples/01_basic_bringup_cli/main.cpp` | `StressStats::startMs`, `endMs` | `uint32_t` | `endMs - startMs` is unsigned-wrap-safe for any stress run finishing within 49.7 days |
| `examples/common/HealthDiag.h` | `HealthMonitor::_intervalMs`, `_lastLogMs` | `uint32_t` | `now - _lastLogMs >= _intervalMs` is wrap-safe (unsigned arithmetic) |
| `examples/common/HealthDiag.h` | `HealthSnapshot::timestamp` | `uint32_t` | Records `millis()` but is **never used** in any comparison — dead field |

The library core uses one 32-bit deadline comparison for Sleep wake recovery,
with unsigned arithmetic suitable for wrap-safe caller-supplied milliseconds.
It still does not use framework time sources internally.

---

## 4. Public/Status Timestamp Risks

| Risk | Location | Impact |
|---|---|---|
| `lastOkMs()` / `lastErrorMs()` are raw `uint32_t` exposed over public API | `include/MB85RC/MB85RC.h` | After rollover the absolute value is misleading; elapsed time (`now - lastXxxMs()`) is still correct via unsigned wrap; **no leak to FRAM or `SettingsSnapshot`** |
| `SettingsSnapshot` does **not** contain timestamps — only `hasNowMsHook` (bool) | `include/MB85RC/MB85RC.h` | No timestamp leaks into persisted snapshots. Correct. |
| `HealthDiag.h::HealthSnapshot::timestamp` stores `millis()` | `examples/common/HealthDiag.h` | Value is never used in comparisons or output — dead field. Not logged, not in any output path. Low risk but noise. |
| `printDriverHealth()` displays raw `lastOkMs` as `"at %lu ms"` | `examples/01_basic_bringup_cli/main.cpp` | After 49.7-day rollover the displayed absolute value wraps and shows a small/confusing number; elapsed `now - lastOkMs` is still correct |
| No timestamps enter FRAM storage, logs, or persisted state anywhere | — | **Confirmed safe.** |

---

## 5. Required Fixes (No Behavior Changes)

### F-1 — `HealthDiag.h`: Cross-source `now - lastOkMs` subtraction

**Files:** `examples/common/HealthDiag.h`  
**Functions:** `printHealthVerbose`, `HealthMonitor::tick`, `HealthSnapshot::capture`

`printHealthVerbose`, `HealthMonitor::tick`, and `HealthSnapshot::capture` read `millis()` for
`now`, then subtract `driver.lastOkMs()` / `driver.lastErrorMs()` which were stamped by
`Config::nowMs`. If those two sources differ (e.g. IDF example injects `esp_timer`-based
`nowMs` but `HealthDiag.h` reads `millis()`), the elapsed computation is wrong.

**Required fix:** `printHealthVerbose` and `HealthMonitor::tick` must accept a `nowMs`
parameter (or a `NowMsFn` pointer) that matches the one supplied to `Config::nowMs`, rather
than calling `millis()` independently. `HealthSnapshot::capture` should do the same or
accept an explicit timestamp.

---

### F-2 — IDF `nowMs()`: Document the `int64_t` → `uint32_t` truncation

**File:** `examples/espidf_basic/main/main.cpp`, line 41

```cpp
// Current (undocumented truncation):
return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
```

`esp_timer_get_time()` returns an `int64_t` that will not overflow in practice.
The cast to `uint32_t` silently wraps at ~49.7 days. The `NowMsFn` contract is `uint32_t`,
so wrapping is unavoidable, but there is no comment noting this.

**Required fix:** Add a comment documenting the intentional truncation and 49.7-day rollover:

```cpp
// esp_timer_get_time() is int64_t microseconds since boot (~292k-year range).
// NowMsFn requires uint32_t milliseconds; truncation wraps at ~49.7 days.
// Callers using (uint32_t)(now - then) remain correct across the wrap.
return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
```

---

### F-3 — `lastOkMs()` / `lastErrorMs()` public API documentation

**File:** `include/MB85RC/MB85RC.h`

The Doxygen comments do not state:
- That the values are raw `uint32_t` timestamps from the injected `nowMs` source.
- That they wrap at the same rate as that source (~49.7 days for typical ms clocks).
- That callers must use `(uint32_t)(now - lastOkMs())` for correct elapsed time.

**Required fix:** Expand the Doxygen comment for both getters to include wrap behavior
and correct usage pattern. Example:

```cpp
/// Timestamp of last successful I2C operation.
/// @return Millisecond timestamp from Config::nowMs, or 0 when no hook is supplied.
/// @note This is a raw uint32_t value from the injected clock source. It wraps at
///       the same rate as that source (~49.7 days for a millisecond counter).
///       Compute elapsed time as `(uint32_t)(nowMs() - lastOkMs())` to remain
///       correct across a rollover. Do not compare the raw value to wall-clock time.
uint32_t lastOkMs() const { return _lastOkMs; }
```

---

### F-4 — `HealthSnapshot::timestamp` is a dead field using a mismatched source

**File:** `examples/common/HealthDiag.h`

`HealthSnapshot::timestamp` is populated via `millis()` in `capture()` but is never read
in `printHealthDiff` or anywhere else. It introduces the same cross-source assumption as
F-1 with no benefit.

**Required fix:** Remove the field, or replace the `millis()` call with a passed-in
`nowMs` value if the field is ever used.

---

### F-5 — Arduino `printDriverHealth()` implicit source consistency assumption

**File:** `examples/01_basic_bringup_cli/main.cpp`

`printDriverHealth()` reads `millis()` for `now` and then subtracts `device.lastOkMs()`
which was stamped by `Config::nowMs = exampleNowMs` (also `millis()`). Currently safe, but
the dependency is implicit and will silently break if `Config::nowMs` is ever changed to a
different source.

**Required fix:** Add a comment asserting the assumption. No behavioral change needed now.

```cpp
// NOTE: 'now' must come from the same source as Config::nowMs (currently millis()).
// If Config::nowMs changes, update this call site to match.
const uint32_t now = millis();
```

---

## Summary Table

| ID | Severity | File(s) | Issue |
|---|---|---|---|
| F-1 | **High** | `examples/common/HealthDiag.h` | `millis()` mixed with injected `Config::nowMs` timestamps in elapsed-time computations |
| F-2 | Low | `examples/espidf_basic/main/main.cpp` | Undocumented `int64_t` → `uint32_t` truncation in `nowMs()` |
| F-3 | Low | `include/MB85RC/MB85RC.h` | Missing wrap-behavior documentation on `lastOkMs()` / `lastErrorMs()` |
| F-4 | Low | `examples/common/HealthDiag.h` | Dead `HealthSnapshot::timestamp` field populated from wrong source |
| F-5 | Low | `examples/01_basic_bringup_cli/main.cpp` | Implicit source-consistency assumption in `printDriverHealth()` |

**Library core is clean:** no direct clock reads, no wrap-unsafe arithmetic, no timestamps
in persisted data or `SettingsSnapshot`.
