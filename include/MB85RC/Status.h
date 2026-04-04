/// @file Status.h
/// @brief Error codes and status handling for MB85RC driver
#pragma once

#include <cstdint>

namespace MB85RC {

/// Error codes for all MB85RC operations
enum class Err : uint8_t {
  OK = 0,                    ///< Operation successful
  NOT_INITIALIZED,           ///< begin() not called
  INVALID_CONFIG,            ///< Invalid configuration parameter
  I2C_ERROR,                 ///< I2C communication failure
  TIMEOUT,                   ///< Operation timed out
  INVALID_PARAM,             ///< Invalid parameter value
  DEVICE_NOT_FOUND,          ///< Device not responding on I2C bus
  DEVICE_ID_MISMATCH,        ///< Device ID does not match expected MB85RC256V
  ADDRESS_OUT_OF_RANGE,      ///< Memory address exceeds 0x7FFF
  WRITE_PROTECTED,           ///< Write rejected (WP pin may be high)
  BUSY,                      ///< Device is busy
  IN_PROGRESS,               ///< Operation queued / in progress

  // I2C transport details (append-only to preserve existing values)
  I2C_NACK_ADDR,             ///< I2C address not acknowledged
  I2C_NACK_DATA,             ///< I2C data byte not acknowledged
  I2C_TIMEOUT,               ///< I2C transaction timeout
  I2C_BUS                    ///< I2C bus error (arbitration lost, etc.)
};

/// Status structure returned by all fallible operations
struct Status {
  Err code = Err::OK;
  int32_t detail = 0;        ///< Implementation-specific detail (e.g., I2C error code)
  const char* msg = "";      ///< Static string describing the error

  constexpr Status() = default;
  constexpr Status(Err c, int32_t d, const char* m) : code(c), detail(d), msg(m) {}
  
  /// @return true if operation succeeded
  constexpr bool ok() const { return code == Err::OK; }

  /// @return true if operation is in progress (queued, not yet complete)
  constexpr bool inProgress() const { return code == Err::IN_PROGRESS; }

  /// Create a success status
  static constexpr Status Ok() { return Status{Err::OK, 0, "OK"}; }
  
  /// Create an error status
  static constexpr Status Error(Err err, const char* message, int32_t detailCode = 0) {
    return Status{err, detailCode, message};
  }
};

}  // namespace MB85RC
