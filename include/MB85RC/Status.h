/// @file Status.h
/// @brief Error codes and status handling for MB85RC driver
#pragma once

#include <cstdint>

namespace MB85RC {

/// @brief Error codes for all MB85RC operations.
enum class Err : uint8_t {
  OK = 0,                    ///< Operation successful
  NOT_INITIALIZED = 1,       ///< begin() not called
  INVALID_CONFIG = 2,        ///< Invalid configuration parameter
  I2C_ERROR = 3,             ///< I2C communication failure
  TIMEOUT = 4,               ///< Core-owned deadline timed out (not an I2C transport timeout)
  INVALID_PARAM = 5,         ///< Invalid parameter value
  DEVICE_NOT_FOUND = 6,      ///< Reserved compatibility code; core preserves transport NACK/timeout details
  DEVICE_ID_MISMATCH = 7,    ///< Device ID does not match expected/active variant
  ADDRESS_OUT_OF_RANGE = 8,  ///< Memory address/range exceeds active variant capacity
  WRITE_PROTECTED = 9,       ///< Application/transport-reported protection; WP-high writes may still ACK
  BUSY = 10,                 ///< Device is busy or driver is latched OFFLINE until recover()
  IN_PROGRESS = 11,          ///< Operation queued / in progress

  // I2C transport details (append-only to preserve existing values)
  I2C_NACK_ADDR = 12,        ///< I2C address not acknowledged
  I2C_NACK_DATA = 13,        ///< I2C data byte not acknowledged
  I2C_TIMEOUT = 14,          ///< I2C transport transaction timeout
  I2C_BUS = 15,              ///< I2C bus error (arbitration lost, etc.)
  VERIFY_MISMATCH = 16,      ///< Readback verification did not match expected data
  UNSUPPORTED = 17           ///< Operation is not supported by the active variant/transport
};

/// @brief Machine-readable Status::detail values used with Err::BUSY.
enum class BusyDetail : int32_t {
  OFFLINE = 1,             ///< Driver is OFFLINE until recover() succeeds.
  TRANSFER_ACTIVE = 2,     ///< A staged transfer is already active.
  ASLEEP = 3,              ///< Device is asleep; call wake().
  WAKING = 4,              ///< Sleep wake recovery deadline has not elapsed.
  ALREADY_ASLEEP = 5,      ///< Sleep entry requested while already asleep.
  TRANSFER_CANCELLED = 6   ///< Active staged transfer was cancelled.
};

/// @brief Status structure returned by all fallible operations.
struct Status {
  Err code = Err::OK;
  int32_t detail = 0;        ///< Implementation-specific detail (e.g., I2C error code)
  const char* msg = "";      ///< Static string describing the error

  constexpr Status() = default;
  constexpr Status(Err c, int32_t d, const char* m) : code(c), detail(d), msg(m) {}
  
  /// @return true if operation succeeded
  constexpr bool ok() const { return code == Err::OK; }

  /// @return true if this status has the requested error code
  constexpr bool is(Err err) const { return code == err; }

  /// @return true if operation is in progress (queued, not yet complete)
  constexpr bool inProgress() const { return code == Err::IN_PROGRESS; }

  /// @return true if operation succeeded
  constexpr explicit operator bool() const { return ok(); }

  /// Create a success status
  static constexpr Status Ok() { return Status{Err::OK, 0, "OK"}; }
  
  /// Create an error status
  static constexpr Status Error(Err err, const char* message, int32_t detailCode = 0) {
    return Status{err, detailCode, message};
  }
};

}  // namespace MB85RC
