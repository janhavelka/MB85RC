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

/// @brief Device Sleep power-state tracked by the driver.
///
/// Sleep state is separate from DriverState health. `WAKING` means the wake
/// stimulus has been sent and the caller must allow the datasheet recovery time
/// before memory or Device ID access.
enum class SleepState : uint8_t {
  AWAKE,   ///< Normal access is allowed.
  ASLEEP,  ///< Sleep command accepted; call wake()/wakeFromSleep().
  WAKING   ///< Wake stimulus sent; wait tREC and call tick().
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
  uint32_t maxNormalBusHz = 0;    ///< Datasheet normal-mode I2C limit for active variant.
  uint32_t maxHighSpeedBusHz = 0; ///< Datasheet HS-mode I2C limit, or 0 when unsupported.
  bool highSpeedModeSupported = false; ///< True when active variant documents HS mode.
  bool highSpeedModeEnabled = false; ///< True when memory transfers use HS-prefixed callbacks.
  bool sleepModeSupported = false; ///< True when active variant documents Sleep mode.
  SleepState sleepState = SleepState::AWAKE; ///< Current tracked Sleep state.
  uint32_t sleepWakeReadyMs = 0;   ///< Millisecond deadline for WAKING -> AWAKE.
  uint16_t sleepRecoveryUs = 0;    ///< Datasheet tREC contract for active variant.
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

/// @brief Detailed result for logical write/fill operations split into chunks.
///
/// `bytesAccepted` counts bytes in chunks for which the injected I2C transport
/// returned `Status::Ok()`. It is an accepted prefix, not proof that memory
/// content changed; a hardware WP pin can allow ACK while preventing
/// persistence. Use verifyDetailed(), writeVerify(), or fillVerify() when
/// persistence matters.
struct WriteResult {
  Status status = Status::Ok();   ///< Final transport/preflight status.
  uint32_t address = 0;           ///< Requested start address.
  size_t bytesRequested = 0;      ///< Bytes requested by caller.
  size_t bytesAccepted = 0;       ///< Prefix accepted by successful I2C chunks.
  size_t failedChunkOffset = 0;   ///< Offset of first failed chunk, or bytesRequested on success.
  size_t failedChunkLength = 0;   ///< Length of first failed chunk, or 0 on success.
  bool complete = false;          ///< True when all requested bytes were accepted.
};

/// @brief Detailed readback verification result.
///
/// `bytesVerified` counts bytes confirmed equal before the first mismatch or
/// transport failure. A mismatch is reported with `status == Status::Ok()` and
/// `match == false`; transport/preflight failures return their normal status.
struct VerifyDetailedResult {
  Status status = Status::Ok();   ///< Preflight/transport status.
  uint32_t address = 0;           ///< Requested start address.
  size_t bytesRequested = 0;      ///< Bytes requested by caller.
  size_t bytesVerified = 0;       ///< Bytes confirmed equal before failure/mismatch.
  size_t firstMismatchOffset = 0; ///< First mismatching byte offset.
  uint8_t expected = 0;           ///< Expected byte at firstMismatchOffset.
  uint8_t actual = 0;             ///< Actual byte at firstMismatchOffset.
  bool match = false;             ///< True when all requested bytes matched.
};

/// @brief MB85RC-family FRAM driver class.
///
/// MB85RC instances are not internally thread-safe. Use one task or provide
/// external serialization around all public methods that can touch driver state
/// or I2C. APIs that perform I2C are not ISR-safe because they can call
/// transport callbacks and may block until the transport timeout. Transport
/// callbacks must not recursively call back into the same MB85RC instance.
///
/// The core driver does not own the I2C bus: bus initialization, locking,
/// timeout policy, retry policy, and recovery policy belong to the injected
/// transport callbacks or the application bus manager.
class MB85RC {
public:
  MB85RC() = default;
  MB85RC(const MB85RC&) = delete;
  MB85RC& operator=(const MB85RC&) = delete;
  MB85RC(MB85RC&&) = delete;
  MB85RC& operator=(MB85RC&&) = delete;

  // =========================================================================
  // Lifecycle
  // =========================================================================
  
  /// Initialize the driver with configuration.
  /// Verifies device presence by Device ID when available, or by a read-only
  /// memory presence probe for explicit no-Device-ID variants.
  /// Does not configure or take ownership of the caller-managed I2C bus.
  /// @param config Configuration including transport callbacks
  /// @return Status::Ok() on success, error otherwise
  Status begin(const Config& config);
  
  /// Process pending operations (call regularly from loop).
  /// Currently a no-op for FRAM (no write delays or async operations).
  /// @param nowMs Current timestamp in milliseconds
  void tick(uint32_t nowMs);
  
  /// Shutdown the driver and release resources.
  /// Does not deinitialize or release the caller-managed I2C bus.
  void end();
  
  // =========================================================================
  // Diagnostics
  // =========================================================================
  
  /// Check if device is present on the bus (no health tracking).
  /// Requires a successful begin() because the active variant and configured
  /// transport are used. This is a diagnostic check only; it does not
  /// initialize, reset, recover, or take ownership of the physical I2C bus.
  /// Diagnostic probes do not establish a safe current-address-read starting
  /// point; explicit no-Device-ID probes conservatively clear cached
  /// current-address state because they use a raw memory read.
  /// @return Status::Ok() if device responds, error otherwise
  Status probe();
  
  /// Attempt to recover from DEGRADED/OFFLINE state.
  /// Uses a tracked Device ID read when available, or a tracked memory-read
  /// probe for explicit no-Device-ID variants. Transport failures and ID
  /// mismatches update health counters while clearing current-address tracking.
  /// Does not reset, reconfigure, or recover the physical I2C bus; application
  /// bus recovery and retry policy remain outside the core driver.
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

  /// Get active variant's normal-mode I2C maximum bus rate.
  /// @return Datasheet normal-mode bus rate in hertz, or family default before variant selection.
  uint32_t maxNormalBusHz() const;

  /// Get active variant's High-speed-mode I2C maximum bus rate.
  /// @return Datasheet HS bus rate in hertz, or 0 when unsupported.
  uint32_t maxHighSpeedBusHz() const;

  /// Check whether the active variant documents I2C High-speed mode.
  /// @return true for variants with local datasheet HS support.
  bool supportsHighSpeedMode() const;

  /// Check whether HS-prefixed memory/current-address transfers are enabled.
  /// @return true when setHighSpeedMode(true) or enterHighSpeedMode() succeeded.
  bool highSpeedModeEnabled() const { return _highSpeedModeEnabled; }

  /// Enable or disable HS-prefixed memory/current-address transfers.
  ///
  /// The core does not change the controller clock. When enabled, each memory
  /// or current-address transaction is emitted through Config::i2cSpecial with
  /// a High-speed master-code prefix because a STOP exits the bus HS state.
  /// Device ID, probe(), and recover() continue to use normal callbacks.
  /// @param enabled true to use HS-prefixed transfers; false to use normal I2C.
  /// @return Status::Ok() when the request is accepted.
  Status setHighSpeedMode(bool enabled);

  /// Compatibility alias for setHighSpeedMode(true).
  ///
  /// This enables HS-prefixed transfers; it does not itself change the MCU bus
  /// clock or leave the bus permanently in HS after a STOP.
  /// @return Status::Ok() when HS transfer mode is enabled.
  Status enterHighSpeedMode() { return setHighSpeedMode(true); }

  /// Compatibility alias for setHighSpeedMode(false).
  /// @return Status::Ok() after disabling HS-prefixed transfers.
  Status exitHighSpeedMode() { return setHighSpeedMode(false); }

  /// Check whether the active variant documents Sleep mode.
  /// @return true for variants with local datasheet Sleep support.
  bool supportsSleepMode() const;

  /// Get tracked device Sleep state.
  /// @return Current Sleep power-state tracked by the driver.
  SleepState sleepState() const { return _sleepState; }

  /// Get the active variant's Sleep recovery time contract.
  /// @return tREC in microseconds, or 0 when Sleep is unsupported.
  uint16_t sleepRecoveryUs() const;

  /// Send the active variant's Sleep entry sequence.
  ///
  /// The sequence is emitted through Config::i2cSpecial. On success the driver
  /// marks the device ASLEEP and invalidates current-address tracking. Memory
  /// and Device ID operations return BUSY until wake()/wakeFromSleep() and the
  /// recovery interval complete. No delay is inserted by the core.
  /// @return Status::Ok() when the sleep entry sequence is accepted.
  Status enterSleep();

  /// Send the wake stimulus for a sleeping device.
  ///
  /// On success the driver enters WAKING and records a conservative millisecond
  /// deadline derived from the datasheet tREC. The application must allow at
  /// least sleepRecoveryUs() before access; call tick() after that interval to
  /// transition back to AWAKE. No delay is inserted by the core.
  /// @return Status::Ok() when the wake stimulus is accepted.
  Status wake();

  /// Compatibility alias for wake().
  /// @return Status::Ok() when the wake stimulus is accepted.
  Status wakeFromSleep() { return wake(); }

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
  /// Status::Ok() means the I2C write was accepted by the transport, not that
  /// persistence was verified when external WP may be asserted.
  /// @param address Memory address within the active variant capacity.
  /// @param value Byte to write
  /// @return Status::Ok() on success
  Status writeByte(uint32_t address, uint8_t value);

  /// Write multiple bytes starting at the specified address.
  /// The full range must fit before the active variant's end address.
  /// No page boundary limitations (unlike EEPROM).
  /// FRAM writes are immediate - no write delay needed.
  /// Large logical writes may be split into I2C chunks and are not atomic:
  /// a later chunk can fail after earlier chunks were accepted by transport.
  /// A successful write means the bus transaction was accepted, not that data
  /// persistence was verified when external WP may be asserted.
  /// @param address Starting memory address within the active variant capacity.
  /// @param buf Data buffer to write
  /// @param len Number of bytes to write
  /// @return Status::Ok() on success
  Status write(uint32_t address, const uint8_t* buf, size_t len);

  /// Write multiple bytes and report the accepted prefix.
  /// `bytesAccepted` counts bytes in chunks accepted by the transport. It does
  /// not prove that those bytes persisted when external WP may be asserted.
  /// @param address Starting memory address within the active variant capacity.
  /// @param buf Data buffer to write
  /// @param len Number of bytes to write
  /// @return Detailed accepted-prefix result. `bytesAccepted` is not proof of persistence.
  WriteResult writeDetailed(uint32_t address, const uint8_t* buf, size_t len);

  /// Fill a range of memory with a constant byte value.
  /// Large logical fills may be split into I2C chunks and are not atomic.
  /// Status::Ok() means all chunks were accepted by the transport, not that
  /// persistence was verified when external WP may be asserted.
  /// @param address Starting memory address within the active variant capacity.
  /// @param value Fill byte
  /// @param len Number of bytes to fill
  /// @return Status::Ok() on success
  Status fill(uint32_t address, uint8_t value, size_t len);

  /// Fill a range and report the accepted prefix.
  /// `bytesAccepted` counts bytes in chunks accepted by the transport. It does
  /// not prove that those bytes persisted when external WP may be asserted.
  /// @param address Starting memory address within the active variant capacity.
  /// @param value Fill byte
  /// @param len Number of bytes to fill
  /// @return Detailed accepted-prefix result. `bytesAccepted` is not proof of persistence.
  WriteResult fillDetailed(uint32_t address, uint8_t value, size_t len);
  
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
  /// The pointer is undefined after power-on and only safe after a known
  /// address-setting transaction, such as a successful addressed memory
  /// read/write by this instance. Failed transactions and recovery paths
  /// conservatively invalidate cached current-address state.
  /// @param value Output byte
  /// @return Status::Ok() on success
  Status readCurrentAddress(uint8_t& value);

  /// Read multiple bytes using repeated documented current-address-read operations.
  /// The pointer is undefined after power-on and only safe after a known
  /// address-setting transaction, such as a successful addressed memory
  /// read/write by this instance. Failed transactions and recovery paths
  /// conservatively invalidate cached current-address state.
  /// @param buf Output buffer
  /// @param len Number of bytes to read
  /// @return Status::Ok() on success
  Status readCurrentAddress(uint8_t* buf, size_t len);

  /// Compare FRAM contents against an expected buffer.
  /// Status::Ok() means the readback transactions completed; check out.match
  /// to determine whether all bytes matched.
  /// @param address Starting memory address within the active variant capacity.
  /// @param expected Expected bytes
  /// @param len Number of bytes to compare
  /// @param out Comparison result
  /// @return Status::Ok() on successful comparison transaction(s)
  Status verify(uint32_t address, const uint8_t* expected, size_t len, VerifyResult& out);

  /// Compare FRAM contents against an expected buffer with byte counts.
  /// `bytesVerified` counts bytes that matched before the first mismatch or
  /// transport failure.
  /// @param address Starting memory address within the active variant capacity.
  /// @param expected Expected bytes
  /// @param len Number of bytes to compare
  /// @return Detailed verification result.
  VerifyDetailedResult verifyDetailed(uint32_t address, const uint8_t* expected, size_t len);

  /// Write and then verify the same bytes by readback.
  /// Use for writes where transport acceptance is not enough and readback
  /// confirmation is required. Returns `Err::VERIFY_MISMATCH` when the write
  /// transport succeeds but readback differs.
  /// @param address Starting memory address within the active variant capacity.
  /// @param buf Data buffer to write and verify
  /// @param len Number of bytes to write and verify
  /// @param verifyOut Optional detailed verification result
  /// @return Status::Ok() only when write transport succeeds and readback matches.
  Status writeVerify(uint32_t address, const uint8_t* buf, size_t len,
                     VerifyDetailedResult* verifyOut = nullptr);

  /// Fill and then verify the same byte value by readback.
  /// Use for fills where transport acceptance is not enough and readback
  /// confirmation is required. Returns `Err::VERIFY_MISMATCH` when the fill
  /// transport succeeds but readback differs.
  /// @param address Starting memory address within the active variant capacity.
  /// @param value Fill byte to write and verify.
  /// @param len Number of bytes to fill and verify.
  /// @param verifyOut Optional detailed verification result.
  /// @return Status::Ok() only when fill transport succeeds and readback matches.
  Status fillVerify(uint32_t address, uint8_t value, size_t len,
                    VerifyDetailedResult* verifyOut = nullptr);

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

  /// Raw special I2C operation (no health tracking)
  Status _i2cSpecialRaw(I2cSpecialOp op, const I2cSpecialTransfer& transfer);
  
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

  /// Tracked special I2C operation (updates health)
  Status _i2cSpecialTracked(I2cSpecialOp op, const I2cSpecialTransfer& transfer);
  
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

  /// Return OK only when public bus access is allowed by Sleep state.
  Status _ensureAwakeForI2c();

  /// Advance WAKING -> AWAKE when the caller-supplied time reaches the deadline.
  void _advanceWakeState(uint32_t nowMs);

  /// Build the special-operation transfer envelope for this instance.
  I2cSpecialTransfer _specialTransfer(uint8_t i2cAddress,
                                      const uint8_t* txData = nullptr,
                                      size_t txLen = 0,
                                      uint8_t* rxData = nullptr,
                                      size_t rxLen = 0) const;

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
  bool _highSpeedModeEnabled = false;
  SleepState _sleepState = SleepState::AWAKE;
  uint32_t _sleepWakeReadyMs = 0;
  
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
