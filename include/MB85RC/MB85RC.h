/// @file MB85RC.h
/// @brief Main driver class for supported MB85RC-family FRAM variants
#pragma once

#include <cstddef>
#include <cstdint>
#include "MB85RC/Status.h"
#include "MB85RC/Config.h"
#include "MB85RC/CommandTable.h"
#include "MB85RC/Version.h"

namespace MB85RC {

/// @brief Driver state for health monitoring.
enum class DriverState : uint8_t {
  UNINIT,    ///< begin() not called or end() called
  READY,     ///< Operational, consecutiveFailures == 0
  DEGRADED,  ///< 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    ///< consecutiveFailures >= offlineThreshold
};

/// @brief Device ID fields parsed from 3-byte read.
///
/// Variants without a Device ID command leave these fields at zero in cached
/// snapshots and return `Err::INVALID_PARAM` from explicit Device ID read APIs.
struct DeviceId {
  uint16_t manufacturerId = 0; ///< 12-bit Manufacturer ID (expect 0x00A)
  uint16_t productId = 0;      ///< 12-bit Product ID
  uint8_t densityCode = 0;     ///< Density nibble from Product ID
};

/// @brief Raw 3-byte Device ID payload as returned on the bus.
struct DeviceIdRaw {
  uint8_t bytes[cmd::DEVICE_ID_LEN] = {};
};

/// @brief Snapshot of current driver settings/state without performing I2C.
///
/// The snapshot is cache-only and never performs bus traffic. Use it from
/// diagnostics, status views, and examples when an application needs to display
/// the selected runtime variant and active capacity without disturbing health
/// counters.
struct SettingsSnapshot {
  bool initialized = false;       ///< True after begin() succeeds and before end()
  DriverState state = DriverState::UNINIT; ///< Current lifecycle/health state.
  uint8_t i2cAddress = cmd::DEFAULT_ADDRESS; ///< Active 7-bit I2C address.
  uint32_t i2cTimeoutMs = 0;      ///< Configured per-transaction I2C timeout.
  uint8_t offlineThreshold = 0;   ///< Consecutive failures required to enter OFFLINE.
  bool hasNowMsHook = false;      ///< True when Config::nowMs is supplied.
  DeviceVariant expectedVariant = DeviceVariant::MB85RC256V; ///< Configured variant expectation.
  DeviceVariant activeVariant = DeviceVariant::MB85RC256V; ///< Active runtime variant after begin().
  bool variantKnown = false;      ///< True when begin() selected a supported variant.
  const char* variantName = "unknown"; ///< Active runtime variant name, or "unknown".
  uint16_t manufacturerId = 0;    ///< Cached Device ID manufacturer field.
  uint16_t productId = 0;         ///< Cached Device ID product field.
  uint8_t densityCode = 0;        ///< Cached Device ID density field.
  uint32_t capacityBytes = 0;     ///< Active runtime memory capacity in bytes.
  uint32_t maxAddress = 0;        ///< Highest valid runtime memory address.
  bool currentAddressKnown = false; ///< True after a successful memory access seeds the pointer.
  uint32_t currentAddress = 0;    ///< Next byte address for Current Address Read.
};

/// @brief Result of comparing expected bytes with FRAM contents.
struct VerifyResult {
  bool match = false;
  size_t mismatchOffset = 0;      ///< First mismatching byte offset from the requested start
  uint8_t expected = 0;           ///< Expected byte at mismatchOffset
  uint8_t actual = 0;             ///< Actual byte read at mismatchOffset
};

/// @brief MB85RC-family FRAM driver class.
class MB85RC {
public:
  // =========================================================================
  // Lifecycle
  // =========================================================================
  
  /// Initialize the driver with configuration.
  /// Verifies device presence by Device ID when available, or by a safe
  /// memory-read probe for explicit no-Device-ID variants.
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
  /// Uses a tracked Device ID read when available, or a tracked memory-read
  /// probe for explicit no-Device-ID variants. Transport failures and ID
  /// mismatches update health counters while clearing current-address tracking.
  /// @return Status::Ok() if device now responsive, error otherwise
  Status recover();
  
  // =========================================================================
  // Driver State
  // =========================================================================
  
  /// Get current driver state.
  /// @return Current lifecycle/health state.
  DriverState state() const { return _driverState; }

  /// Alias for state() for cross-driver diagnostics.
  /// @return Current lifecycle/health state.
  DriverState driverState() const { return state(); }

  /// Check if begin() has completed successfully.
  /// @return true after begin() succeeds and before end() is called.
  bool isInitialized() const { return _initialized; }
  
  /// Check if driver is ready for operations
  /// @return true in READY or DEGRADED; false in UNINIT or OFFLINE.
  bool isOnline() const {
    return _driverState == DriverState::READY ||
           _driverState == DriverState::DEGRADED;
  }

  /// Get a copy of the active configuration.
  /// @return Reference to the cached configuration supplied to begin().
  const Config& getConfig() const { return _config; }

  /// Get the active runtime variant metadata, or nullptr before begin().
  /// @return Active variant metadata, or nullptr when not selected.
  const cmd::VariantInfo* variantInfo() const { return _variant; }

  /// Get the active runtime variant name.
  /// @return Active variant name, or "unknown" when not selected.
  const char* variantName() const { return (_variant != nullptr) ? _variant->name : "unknown"; }

  /// Get cached Device ID fields from the last successful begin()/recover() validation.
  /// @return Cached Device ID fields; zeros for explicit no-Device-ID variants.
  DeviceId deviceId() const { return _deviceId; }

  /// Get active runtime capacity in bytes.
  /// @return Active capacity in bytes, or the legacy MB85RC256V size before selection.
  uint32_t capacityBytes() const;

  /// Get highest valid active runtime memory address.
  /// @return Highest valid byte address for the active runtime variant.
  uint32_t maxAddress() const;

  /// Get a snapshot of current configuration/runtime state (no I2C).
  /// @param out Output snapshot populated from cached state.
  /// @return Status::Ok() after writing the snapshot.
  Status getSettings(SettingsSnapshot& out) const;

  /// Get a snapshot of current configuration/runtime state (no I2C).
  /// @return Snapshot populated from cached state.
  SettingsSnapshot getSettings() const {
    SettingsSnapshot out;
    (void)getSettings(out);
    return out;
  }
  
  // =========================================================================
  // Health Tracking
  // =========================================================================
  
  /// Timestamp of last successful I2C operation.
  /// @return Millisecond timestamp from Config::nowMs, or 0 when no hook is supplied.
  uint32_t lastOkMs() const { return _lastOkMs; }
  
  /// Timestamp of last failed I2C operation.
  /// @return Millisecond timestamp from Config::nowMs, or 0 when no hook is supplied.
  uint32_t lastErrorMs() const { return _lastErrorMs; }
  
  /// Most recent error status.
  /// @return Last tracked error status.
  Status lastError() const { return _lastError; }
  
  /// Consecutive failures since last success.
  /// @return Failure count used to enter OFFLINE.
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }
  
  /// Total failure count (lifetime).
  /// @return Lifetime tracked failure count.
  uint32_t totalFailures() const { return _totalFailures; }
  
  /// Total success count (lifetime).
  /// @return Lifetime tracked success count.
  uint32_t totalSuccess() const { return _totalSuccess; }
  
  // =========================================================================
  // Memory Read API
  // =========================================================================
  
  /// Read a single byte from the specified address.
  /// @param address Memory address within the active variant capacity.
  /// @param value Output byte
  /// @return Status::Ok() on success
  Status readByte(uint32_t address, uint8_t& value);

  /// Read multiple bytes starting at the specified address.
  /// The full range must fit before the active variant's end address.
  /// @param address Starting memory address within the active variant capacity.
  /// @param buf Output buffer
  /// @param len Number of bytes to read
  /// @return Status::Ok() on success
  Status read(uint32_t address, uint8_t* buf, size_t len);
  
  // =========================================================================
  // Memory Write API
  // =========================================================================
  
  /// Write a single byte to the specified address.
  /// FRAM writes are immediate - no write delay needed.
  /// @param address Memory address within the active variant capacity.
  /// @param value Byte to write
  /// @return Status::Ok() on success
  Status writeByte(uint32_t address, uint8_t value);

  /// Write multiple bytes starting at the specified address.
  /// The full range must fit before the active variant's end address.
  /// No page boundary limitations (unlike EEPROM).
  /// FRAM writes are immediate - no write delay needed.
  /// @param address Starting memory address within the active variant capacity.
  /// @param buf Data buffer to write
  /// @param len Number of bytes to write
  /// @return Status::Ok() on success
  Status write(uint32_t address, const uint8_t* buf, size_t len);

  /// Fill a range of memory with a constant byte value.
  /// @param address Starting memory address within the active variant capacity.
  /// @param value Fill byte
  /// @param len Number of bytes to fill
  /// @return Status::Ok() on success
  Status fill(uint32_t address, uint8_t value, size_t len);
  
  // =========================================================================
  // Device Information
  // =========================================================================
  
  /// Read the 3-byte Device ID from the device.
  /// Uses the reserved I2C addresses 0xF8/0xF9.
  /// Returns INVALID_PARAM when the active variant has no Device ID command.
  /// @param id Output Device ID fields
  /// @return Status::Ok() on success
  Status readDeviceId(DeviceId& id);

  /// Read the raw 3-byte Device ID payload from the device.
  /// Returns INVALID_PARAM when the active variant has no Device ID command.
  /// @param raw Output raw Device ID bytes as transmitted on the bus
  /// @return Status::Ok() on success
  Status readDeviceIdRaw(DeviceIdRaw& raw);

  /// Read the byte at the device's current internal address pointer.
  /// The pointer is undefined after power-on until a memory read/write succeeds.
  /// @param value Output byte
  /// @return Status::Ok() on success
  Status readCurrentAddress(uint8_t& value);

  /// Read multiple bytes using repeated documented current-address-read operations.
  /// The pointer is undefined after power-on until a memory read/write succeeds.
  /// @param buf Output buffer
  /// @param len Number of bytes to read
  /// @return Status::Ok() on success
  Status readCurrentAddress(uint8_t* buf, size_t len);

  /// Compare FRAM contents against an expected buffer.
  /// @param address Starting memory address within the active variant capacity.
  /// @param expected Expected bytes
  /// @param len Number of bytes to compare
  /// @param out Comparison result
  /// @return Status::Ok() on successful comparison transaction(s)
  Status verify(uint32_t address, const uint8_t* expected, size_t len, VerifyResult& out);

  /// Get the legacy MB85RC256V memory size in bytes.
  /// Prefer capacityBytes() for runtime-selected variants.
  /// @deprecated Use capacityBytes() after begin() for runtime-selected variants.
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
  
  /// Tracked I2C write-read to an explicit address (updates health)
  Status _i2cWriteReadTrackedAddr(uint8_t addr, const uint8_t* txBuf, size_t txLen,
                                  uint8_t* rxBuf, size_t rxLen);

  /// Tracked I2C write-read (updates health)
  Status _i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                              uint8_t* rxBuf, size_t rxLen);
  
  /// Tracked I2C write (updates health)
  Status _i2cWriteTracked(const uint8_t* buf, size_t len);

  /// Tracked I2C write to an explicit address (updates health)
  Status _i2cWriteTrackedAddr(uint8_t addr, const uint8_t* buf, size_t len);
  
  // =========================================================================
  // Internal Helpers
  // =========================================================================
  
  /// Encoded memory-address transaction header for the active variant.
  struct EncodedMemoryAddress {
    uint8_t i2cAddress = cmd::DEFAULT_ADDRESS;
    uint8_t bytes[cmd::ADDRESS_BYTES] = {};
    size_t len = 0;
  };

  /// Read from memory using tracked path
  Status _readMemory(uint32_t address, uint8_t* buf, size_t len);

  /// Read from memory using raw path (no health tracking)
  Status _readMemoryRaw(uint32_t address, uint8_t* buf, size_t len);

  /// Write to memory using tracked path
  Status _writeMemory(uint32_t address, const uint8_t* buf, size_t len);

  /// Read Device ID using raw path (for begin/probe)
  Status _readDeviceIdRaw(DeviceId& id);

  /// Read Device ID using tracked path (for public API)
  Status _readDeviceIdTracked(DeviceId& id);

  /// Read raw Device ID bytes using raw path
  Status _readDeviceIdBytesRaw(DeviceIdRaw& raw);

  /// Read raw Device ID bytes using tracked path
  Status _readDeviceIdBytesTracked(DeviceIdRaw& raw);

  /// Validate address is within the active runtime capacity.
  bool _isValidAddress(uint32_t address) const;

  /// Validate a contiguous address range against the active runtime capacity.
  bool _fitsRange(uint32_t address, size_t len) const;

  /// Wrap an address into the active runtime memory address space.
  uint32_t _wrapAddress(uint32_t address, size_t offset) const;

  /// Encode a runtime memory address for the active variant.
  Status _encodeMemoryAddress(uint32_t address, EncodedMemoryAddress& out) const;

  /// Select and validate the active runtime variant from Device ID.
  Status _selectVariant(DeviceVariant expected, const DeviceId& id);

  /// Validate a Device ID against the active runtime variant.
  Status _validateActiveDeviceId(const DeviceId& id) const;

  /// Convert active variant metadata to public enum.
  DeviceVariant _activeVariantEnum() const;

  /// Update the tracked current address after a successful memory transaction.
  void _setCurrentAddressAfterTransfer(uint32_t address, size_t len);
  
  // =========================================================================
  // Health Management
  // =========================================================================
  
  /// Update health counters and state based on operation result.
  /// Called ONLY from tracked transport wrappers.
  Status _updateHealth(const Status& st);

  /// Record a semantic recover failure after a successful tracked I2C transaction.
  Status _recordFailure(const Status& st);
  void _reassertOfflineLatch();

  /// Get current time using the injected callback, or 0 when no callback exists.
  uint32_t _nowMs() const;
  
  // =========================================================================
  // State
  // =========================================================================
  
  Config _config;
  const cmd::VariantInfo* _variant = nullptr;
  DeviceId _deviceId;
  bool _initialized = false;
  DriverState _driverState = DriverState::UNINIT;
  bool _allowOfflineI2c = false;
  
  // Health counters
  uint32_t _lastOkMs = 0;
  uint32_t _lastErrorMs = 0;
  Status _lastError = Status::Ok();
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalFailures = 0;
  uint32_t _totalSuccess = 0;
  bool _currentAddressKnown = false;
  uint32_t _currentAddress = 0;
};

}  // namespace MB85RC
