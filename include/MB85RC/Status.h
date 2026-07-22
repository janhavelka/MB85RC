/// @file Status.h
/// @brief Error codes and status handling for MB85RC driver
#pragma once

#include <cstdint>

namespace MB85RC {

/// @brief Error codes for all MB85RC operations.
enum class Err : uint8_t {
  OK = 0,                    ///< Operation successful
  NOT_INITIALIZED = 1,       ///< No passive binding is active
  INVALID_CONFIG = 2,        ///< Invalid configuration parameter
  I2C_ERROR = 3,             ///< I2C communication failure
  TIMEOUT = 4,               ///< Owner-declared staged deadline expired (not an I2C transport timeout)
  INVALID_PARAM = 5,         ///< Invalid parameter value
  DEVICE_NOT_FOUND = 6,      ///< Reserved compatibility code; core preserves transport NACK/timeout details
  DEVICE_ID_MISMATCH = 7,    ///< Device ID does not match expected/active variant
  ADDRESS_OUT_OF_RANGE = 8,  ///< Memory address/range exceeds active variant capacity
  WRITE_PROTECTED = 9,       ///< Application/transport-reported protection; WP-high writes may still ACK
  BUSY = 10,                 ///< Another operation or device power transition is active
  IN_PROGRESS = 11,          ///< Operation queued / in progress

  // I2C transport details (append-only to preserve existing values)
  I2C_NACK_ADDR = 12,        ///< I2C address not acknowledged
  I2C_NACK_DATA = 13,        ///< I2C data byte not acknowledged
  I2C_TIMEOUT = 14,          ///< I2C transport transaction timeout
  I2C_BUS = 15,              ///< I2C bus error (arbitration lost, etc.)
  VERIFY_MISMATCH = 16,      ///< Readback verification did not match expected data
  UNSUPPORTED = 17,          ///< Operation is not supported by the active variant/transport
  NO_RESULT = 18,            ///< No retained terminal staged result is available
  CANCELLED = 19             ///< Operation was explicitly cancelled by its owner
};

/// @brief Machine-readable Status::detail values used with Err::BUSY.
enum class BusyDetail : int32_t {
  OFFLINE = 1,             ///< Reserved legacy diagnostic value; OFFLINE no longer gates I/O.
  TRANSFER_ACTIVE = 2,     ///< A staged transfer is already active.
  ASLEEP = 3,              ///< Device is asleep; call wake().
  WAKING = 4,              ///< Sleep wake recovery deadline has not elapsed.
  ALREADY_ASLEEP = 5,      ///< Sleep entry requested while already asleep.
  TRANSFER_CANCELLED = 6,  ///< Reserved legacy cancellation detail.
  RESULT_PENDING = 7,      ///< A terminal result must be consumed before another request.
  REQUEST_ID_MISMATCH = 8, ///< Supplied request ID does not identify the active request.
  SLEEP_STATE_UNKNOWN = 9  ///< Sleep entry/wake effect requires explicit wake reconciliation.
};

/// @brief Status structure returned by all fallible operations.
struct Status {
  Err code = Err::OK;        ///< Machine-readable status classification.
  int32_t detail = 0;        ///< Implementation-specific detail (e.g., I2C error code)
  const char* msg = "";      ///< Static string describing the error

  /// Construct an OK status with default fields.
  constexpr Status() = default;
  /// Construct an explicit status value.
  /// @param c Machine-readable status classification.
  /// @param d Numeric detail owned by the status producer.
  /// @param m Static-lifetime diagnostic message.
  constexpr Status(Err c, int32_t d, const char* m) : code(c), detail(d), msg(m) {}
  
  /// @return true if operation succeeded
  constexpr bool ok() const { return code == Err::OK; }

  /// @return true if this status has the requested error code
  /// @param err Error code to compare.
  constexpr bool is(Err err) const { return code == err; }

  /// @return true if operation is in progress (queued, not yet complete)
  constexpr bool inProgress() const { return code == Err::IN_PROGRESS; }

  /// @return true if operation succeeded
  constexpr explicit operator bool() const { return ok(); }

  /// Create a success status
  /// @return Status with Err::OK and static `"OK"` text.
  static constexpr Status Ok() { return Status{Err::OK, 0, "OK"}; }
  
  /// Create an error status
  /// @param err Machine-readable error classification.
  /// @param message Static-lifetime diagnostic message.
  /// @param detailCode Numeric diagnostic detail.
  /// @return Status retaining the supplied static message and detail.
  static constexpr Status Error(Err err, const char* message, int32_t detailCode = 0) {
    return Status{err, detailCode, message};
  }
};

}  // namespace MB85RC
