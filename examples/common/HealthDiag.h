/**
 * @file HealthDiag.h
 * @brief Verbose health diagnostic helpers for MB85RC examples.
 *
 * Provides detailed logging of driver health state, counters, and
 * state transitions for debugging and monitoring.
 *
 * NOT part of the library API. Example-only.
 */

#pragma once

#include <Arduino.h>

#include "MB85RC/MB85RC.h"
#include "MB85RC/Status.h"
#include "examples/common/Log.h"

namespace diag {

/**
 * @brief Convert DriverState enum to human-readable string.
 */
inline const char* stateToString(MB85RC::DriverState state) {
  switch (state) {
    case MB85RC::DriverState::UNINIT:   return "UNINIT";
    case MB85RC::DriverState::READY:    return "READY";
    case MB85RC::DriverState::DEGRADED: return "DEGRADED";
    case MB85RC::DriverState::OFFLINE:  return "OFFLINE";
    default:                             return "UNKNOWN";
  }
}

/**
 * @brief Convert Err enum to human-readable string.
 */
inline const char* errToString(MB85RC::Err err) {
  switch (err) {
    case MB85RC::Err::OK:                    return "OK";
    case MB85RC::Err::NOT_INITIALIZED:       return "NOT_INITIALIZED";
    case MB85RC::Err::INVALID_CONFIG:        return "INVALID_CONFIG";
    case MB85RC::Err::I2C_ERROR:             return "I2C_ERROR";
    case MB85RC::Err::TIMEOUT:               return "TIMEOUT";
    case MB85RC::Err::INVALID_PARAM:         return "INVALID_PARAM";
    case MB85RC::Err::DEVICE_NOT_FOUND:      return "DEVICE_NOT_FOUND";
    case MB85RC::Err::DEVICE_ID_MISMATCH:    return "DEVICE_ID_MISMATCH";
    case MB85RC::Err::ADDRESS_OUT_OF_RANGE:  return "ADDRESS_OUT_OF_RANGE";
    case MB85RC::Err::WRITE_PROTECTED:       return "WRITE_PROTECTED";
    case MB85RC::Err::BUSY:                  return "BUSY";
    case MB85RC::Err::IN_PROGRESS:           return "IN_PROGRESS";
    case MB85RC::Err::I2C_NACK_ADDR:         return "I2C_NACK_ADDR";
    case MB85RC::Err::I2C_NACK_DATA:         return "I2C_NACK_DATA";
    case MB85RC::Err::I2C_TIMEOUT:           return "I2C_TIMEOUT";
    case MB85RC::Err::I2C_BUS:               return "I2C_BUS";
    default:                                  return "UNKNOWN";
  }
}

/**
 * @brief Get state color indicator for terminal output.
 */
inline const char* stateColor(MB85RC::DriverState state) {
  switch (state) {
    case MB85RC::DriverState::READY:    return LOG_COLOR_GREEN;
    case MB85RC::DriverState::DEGRADED: return LOG_COLOR_YELLOW;
    case MB85RC::DriverState::OFFLINE:  return LOG_COLOR_RED;
    case MB85RC::DriverState::UNINIT:   return LOG_COLOR_GRAY;
    default:                             return LOG_COLOR_RESET;
  }
}

inline const char* colorReset() { return LOG_COLOR_RESET; }

inline const char* boolColor(bool value) {
  return value ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

inline const char* successRateColor(float pct) {
  if (pct >= 99.9f) return LOG_COLOR_GREEN;
  if (pct >= 80.0f) return LOG_COLOR_YELLOW;
  return LOG_COLOR_RED;
}

inline const char* failureCountColor(uint32_t failures) {
  if (failures == 0U) return LOG_COLOR_GREEN;
  if (failures < 3U) return LOG_COLOR_YELLOW;
  return LOG_COLOR_RED;
}

inline const char* successCountColor(uint32_t successes) {
  return (successes > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_GRAY;
}

inline const char* totalFailureColor(uint32_t failures) {
  return (failures == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

/**
 * @brief Print a compact one-line health summary.
 */
inline void printHealthOneLine(MB85RC::MB85RC& driver) {
  MB85RC::DriverState st = driver.state();
  LOGI("Health: state=%s%s%s online=%s%s%s consecFail=%s%u%s ok=%s%lu%s fail=%s%lu%s",
       stateColor(st), stateToString(st), colorReset(),
       boolColor(driver.isOnline()), driver.isOnline() ? "true" : "false", colorReset(),
       failureCountColor(driver.consecutiveFailures()), driver.consecutiveFailures(), colorReset(),
       successCountColor(driver.totalSuccess()), (unsigned long)driver.totalSuccess(), colorReset(),
       totalFailureColor(driver.totalFailures()), (unsigned long)driver.totalFailures(), colorReset());
}

/**
 * @brief Print detailed verbose health diagnostics.
 */
inline void printHealthVerbose(MB85RC::MB85RC& driver) {
  MB85RC::DriverState st = driver.state();
  MB85RC::Status lastErr = driver.lastError();
  uint32_t now = millis();

  const bool online = driver.isOnline();
  const uint32_t totalSuccess = driver.totalSuccess();
  const uint32_t totalFailures = driver.totalFailures();
  uint32_t total = totalSuccess + totalFailures;
  float successRate = (total > 0) ? (100.0f * totalSuccess / total) : 0.0f;

  LOG_SERIAL.println();
  LOGI("=== Driver Health ===");
  LOGI("  State: %s%s%s", stateColor(st), stateToString(st), colorReset());
  LOGI("  Online: %s%s%s", boolColor(online), online ? "true" : "false", colorReset());
  LOGI("  Consecutive failures: %s%u%s",
       failureCountColor(driver.consecutiveFailures()),
       driver.consecutiveFailures(),
       colorReset());
  LOGI("  Total success: %s%lu%s",
       successCountColor(totalSuccess),
       (unsigned long)totalSuccess,
       colorReset());
  LOGI("  Total failures: %s%lu%s",
       totalFailureColor(totalFailures),
       (unsigned long)totalFailures,
       colorReset());
  LOGI("  Success rate: %s%.1f%%%s", successRateColor(successRate), successRate, colorReset());

  LOGI("=== Timestamps ===");

  uint32_t lastOk = driver.lastOkMs();
  uint32_t lastFail = driver.lastErrorMs();

  if (lastOk > 0) {
    LOGI("  Last success: %lu ms ago (at %lu ms)",
         (unsigned long)(now - lastOk), (unsigned long)lastOk);
  } else {
    LOGI("  Last success: never");
  }

  if (lastFail > 0) {
    LOGI("  Last failure: %lu ms ago (at %lu ms)",
         (unsigned long)(now - lastFail), (unsigned long)lastFail);
  } else {
    LOGI("  Last failure: never");
  }

  LOGI("=== Last Error ===");

  if (lastErr.ok()) {
    LOGI("  Last error: %snone%s", LOG_COLOR_GREEN, colorReset());
  } else {
    LOGI("  Code: %s%s%s", LOG_COLOR_RED, errToString(lastErr.code), colorReset());
    LOGI("  Detail: %ld", (long)lastErr.detail);
    LOGI("  Message: %s", lastErr.msg ? lastErr.msg : "(null)");
  }
  LOG_SERIAL.println();
}

/**
 * @brief Health snapshot for before/after comparison.
 */
struct HealthSnapshot {
  MB85RC::DriverState state;
  uint8_t consecutiveFailures;
  uint32_t totalSuccess;
  uint32_t totalFailures;
  uint32_t timestamp;

  void capture(MB85RC::MB85RC& driver) {
    state = driver.state();
    consecutiveFailures = driver.consecutiveFailures();
    totalSuccess = driver.totalSuccess();
    totalFailures = driver.totalFailures();
    timestamp = millis();
  }
};

/**
 * @brief Compare two snapshots and print differences.
 */
inline void printHealthDiff(const HealthSnapshot& before, const HealthSnapshot& after) {
  bool changed = false;

  if (before.state != after.state) {
    LOGI("  State: %s%s%s -> %s%s%s",
         stateColor(before.state), stateToString(before.state), colorReset(),
         stateColor(after.state), stateToString(after.state), colorReset());
    changed = true;
  }

  if (before.consecutiveFailures != after.consecutiveFailures) {
    const bool improved = after.consecutiveFailures < before.consecutiveFailures;
    const char* color = improved ? LOG_COLOR_GREEN : LOG_COLOR_RED;
    LOGI("  ConsecFail: %s%u -> %u%s",
         color,
         before.consecutiveFailures,
         after.consecutiveFailures,
         colorReset());
    changed = true;
  }

  if (before.totalSuccess != after.totalSuccess) {
    LOGI("  TotalOK: %lu -> %s%lu (+%lu)%s",
         (unsigned long)before.totalSuccess,
         LOG_COLOR_GREEN,
         (unsigned long)after.totalSuccess,
         (unsigned long)(after.totalSuccess - before.totalSuccess),
         colorReset());
    changed = true;
  }

  if (before.totalFailures != after.totalFailures) {
    LOGI("  TotalFail: %lu -> %s%lu (+%lu)%s",
         (unsigned long)before.totalFailures,
         LOG_COLOR_RED,
         (unsigned long)after.totalFailures,
         (unsigned long)(after.totalFailures - before.totalFailures),
         colorReset());
    changed = true;
  }

  if (!changed) {
    LOGI("  (no changes)");
  }
}

/**
 * @brief Continuously monitor health with periodic logging.
 * Call from loop() for real-time monitoring.
 */
class HealthMonitor {
public:
  /**
   * @brief Initialize monitor with logging interval.
   * @param intervalMs How often to log (0 = only on change)
   */
  void begin(uint32_t intervalMs = 1000) {
    _intervalMs = intervalMs;
    _lastLogMs = 0;
    _lastState = MB85RC::DriverState::UNINIT;
    _lastConsecFail = 0;
  }

  /**
   * @brief Check and optionally log health changes.
   * @param driver Driver instance to monitor
   * @param forceLog If true, always log even if no change
   */
  void tick(MB85RC::MB85RC& driver, bool forceLog = false) {
    uint32_t now = millis();
    MB85RC::DriverState currentState = driver.state();
    uint8_t currentFail = driver.consecutiveFailures();

    bool stateChanged = (currentState != _lastState);
    bool failChanged = (currentFail != _lastConsecFail);
    bool intervalElapsed = (_intervalMs > 0 && (now - _lastLogMs >= _intervalMs));

    if (stateChanged || failChanged || intervalElapsed || forceLog) {
      if (stateChanged) {
        LOGI("[HEALTH] State transition: %s%s%s -> %s%s%s",
             stateColor(_lastState), stateToString(_lastState), colorReset(),
             stateColor(currentState), stateToString(currentState), colorReset());
      }

      if (intervalElapsed || forceLog) {
        printHealthOneLine(driver);
      }

      _lastState = currentState;
      _lastConsecFail = currentFail;
      _lastLogMs = now;
    }
  }

private:
  uint32_t _intervalMs = 1000;
  uint32_t _lastLogMs = 0;
  MB85RC::DriverState _lastState = MB85RC::DriverState::UNINIT;
  uint8_t _lastConsecFail = 0;
};

}  // namespace diag
