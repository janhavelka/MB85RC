/// @file MB85RC.h
/// @brief Main driver class for MB85RC256V FRAM
#pragma once

#include <cstddef>
#include <cstdint>
#include "MB85RC/Status.h"
#include "MB85RC/Config.h"
#include "MB85RC/CommandTable.h"
#include "MB85RC/Version.h"

namespace MB85RC {

/// Driver state for health monitoring
enum class DriverState : uint8_t {
  UNINIT,    ///< begin() not called or end() called
  READY,     ///< Operational, consecutiveFailures == 0
  DEGRADED,  ///< 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    ///< consecutiveFailures >= offlineThreshold
};

/// Device ID fields parsed from 3-byte read
struct DeviceId {
  uint16_t manufacturerId = 0; ///< 12-bit Manufacturer ID (expect 0x00A)
  uint16_t productId = 0;      ///< 12-bit Product ID (expect 0x510)
  uint8_t densityCode = 0;     ///< Density nibble from Product ID (expect 0x5)
};

/// MB85RC256V FRAM driver class
class MB85RC {
public:
  // =========================================================================
  // Lifecycle
  // =========================================================================
  
  /// Initialize the driver with configuration.
  /// Verifies device presence by reading Device ID.
  /// @param config Configuration including transport callbacks
  /// @return Status::Ok() on success, error otherwise
  Status begin(const Config& config);
  
  /// Process pending operations (call regularly from loop).
  /// Currently a no-op for FRAM (no write delays or async operations).
  /// @param nowMs Current timestamp in milliseconds
  void tick(uint32_t nowMs);
  
  /// Shutdown the driver and release resources.
  void end();
  
  // =========================================================================
  // Diagnostics
  // =========================================================================
  
  /// Check if device is present on the bus (no health tracking).
  /// @return Status::Ok() if device responds, error otherwise
  Status probe();
  
  /// Attempt to recover from DEGRADED/OFFLINE state.
  /// @return Status::Ok() if device now responsive, error otherwise
  Status recover();
  
  // =========================================================================
  // Driver State
  // =========================================================================
  
  /// Get current driver state
  DriverState state() const { return _driverState; }
  
  /// Check if driver is ready for operations
  bool isOnline() const { 
    return _driverState == DriverState::READY || 
           _driverState == DriverState::DEGRADED; 
  }
  
  // =========================================================================
  // Health Tracking
  // =========================================================================
  
  /// Timestamp of last successful I2C operation
  uint32_t lastOkMs() const { return _lastOkMs; }
  
  /// Timestamp of last failed I2C operation
  uint32_t lastErrorMs() const { return _lastErrorMs; }
  
  /// Most recent error status
  Status lastError() const { return _lastError; }
  
  /// Consecutive failures since last success
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }
  
  /// Total failure count (lifetime)
  uint32_t totalFailures() const { return _totalFailures; }
  
  /// Total success count (lifetime)
  uint32_t totalSuccess() const { return _totalSuccess; }
  
  // =========================================================================
  // Memory Read API
  // =========================================================================
  
  /// Read a single byte from the specified address.
  /// @param address Memory address (0x0000–0x7FFF)
  /// @param value Output byte
  /// @return Status::Ok() on success
  Status readByte(uint16_t address, uint8_t& value);

  /// Read multiple bytes starting at the specified address.
  /// Address auto-increments and wraps at 0x7FFF -> 0x0000.
  /// @param address Starting memory address (0x0000–0x7FFF)
  /// @param buf Output buffer
  /// @param len Number of bytes to read
  /// @return Status::Ok() on success
  Status read(uint16_t address, uint8_t* buf, size_t len);
  
  // =========================================================================
  // Memory Write API
  // =========================================================================
  
  /// Write a single byte to the specified address.
  /// FRAM writes are immediate — no write delay needed.
  /// @param address Memory address (0x0000–0x7FFF)
  /// @param value Byte to write
  /// @return Status::Ok() on success
  Status writeByte(uint16_t address, uint8_t value);

  /// Write multiple bytes starting at the specified address.
  /// Address auto-increments and wraps at 0x7FFF -> 0x0000.
  /// No page boundary limitations (unlike EEPROM).
  /// FRAM writes are immediate — no write delay needed.
  /// @param address Starting memory address (0x0000–0x7FFF)
  /// @param buf Data buffer to write
  /// @param len Number of bytes to write
  /// @return Status::Ok() on success
  Status write(uint16_t address, const uint8_t* buf, size_t len);

  /// Fill a range of memory with a constant byte value.
  /// @param address Starting memory address (0x0000–0x7FFF)
  /// @param value Fill byte
  /// @param len Number of bytes to fill
  /// @return Status::Ok() on success
  Status fill(uint16_t address, uint8_t value, size_t len);
  
  // =========================================================================
  // Device Information
  // =========================================================================
  
  /// Read the 3-byte Device ID from the device.
  /// Uses the reserved I2C addresses 0xF8/0xF9.
  /// @param id Output Device ID fields
  /// @return Status::Ok() on success
  Status readDeviceId(DeviceId& id);

  /// Get the memory size in bytes (32768 for MB85RC256V).
  /// @return Memory size
  static constexpr uint16_t memorySize() { return cmd::MEMORY_SIZE; }

private:
  // =========================================================================
  // Transport Wrappers
  // =========================================================================
  
  /// Raw I2C write-read (no health tracking)
  Status _i2cWriteReadRaw(uint8_t addr, const uint8_t* txBuf, size_t txLen, 
                          uint8_t* rxBuf, size_t rxLen);
  
  /// Raw I2C write (no health tracking)
  Status _i2cWriteRaw(uint8_t addr, const uint8_t* buf, size_t len);
  
  /// Tracked I2C write-read (updates health)
  Status _i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen, 
                              uint8_t* rxBuf, size_t rxLen);
  
  /// Tracked I2C write (updates health)
  Status _i2cWriteTracked(const uint8_t* buf, size_t len);
  
  // =========================================================================
  // Internal Helpers
  // =========================================================================
  
  /// Read from memory using tracked path
  Status _readMemory(uint16_t address, uint8_t* buf, size_t len);

  /// Write to memory using tracked path
  Status _writeMemory(uint16_t address, const uint8_t* buf, size_t len);

  /// Read Device ID using raw path (for begin/probe)
  Status _readDeviceIdRaw(DeviceId& id);

  /// Read Device ID using tracked path (for public API)
  Status _readDeviceIdTracked(DeviceId& id);

  /// Validate address is within range
  static bool _isValidAddress(uint16_t address);
  
  // =========================================================================
  // Health Management
  // =========================================================================
  
  /// Update health counters and state based on operation result.
  /// Called ONLY from tracked transport wrappers.
  Status _updateHealth(const Status& st);

  /// Get current time using injected callback or millis() fallback
  uint32_t _nowMs() const;
  
  // =========================================================================
  // State
  // =========================================================================
  
  Config _config;
  bool _initialized = false;
  DriverState _driverState = DriverState::UNINIT;
  
  // Health counters
  uint32_t _lastOkMs = 0;
  uint32_t _lastErrorMs = 0;
  Status _lastError = Status::Ok();
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalFailures = 0;
  uint32_t _totalSuccess = 0;
};

}  // namespace MB85RC
