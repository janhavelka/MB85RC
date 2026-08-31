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
  UNINIT = 0,   ///< bind()/begin() not called or end() called
  READY = 1,    ///< Bound and last transport operation succeeded
  DEGRADED = 2, ///< Bound with one or more consecutive transport failures
  OFFLINE = 3   ///< Diagnostic threshold reached; transport remains owner-admissible
};

/// @brief Device Sleep power-state tracked by the driver.
///
/// Sleep state is separate from DriverState health. `UNKNOWN` means a failed
/// Sleep command may have taken effect and normal I2C is blocked until wake()
/// succeeds. `WAKING` means the wake stimulus has been sent and the caller must
/// allow the datasheet recovery time before memory or Device ID access.
enum class SleepState : uint8_t {
  AWAKE = 0,  ///< Normal access is allowed.
  ASLEEP = 1, ///< Sleep command accepted; call wake()/wakeFromSleep().
  WAKING = 2, ///< Wake stimulus sent; wait tREC and call tick().
  UNKNOWN = 3 ///< Sleep/wake effect is ambiguous; call wake() to reconcile.
};

/// @brief Device ID fields parsed from 3-byte read.
///
/// Variants without a Device ID command leave these fields at zero in cached
/// snapshots and return `Err::INVALID_PARAM` from explicit Device ID read APIs.
struct DeviceId {
  uint16_t manufacturerId = 0; ///< 12-bit Manufacturer ID (expect 0x00A)
  uint16_t productId = 0;      ///< 12-bit Product ID
  uint8_t densityCode = 0;     ///< Density nibble from Product ID
  DeviceVariant variant = DeviceVariant::AUTO; ///< Exact known variant, or AUTO when unknown.
};

/// @brief Raw 3-byte Device ID payload as returned on the bus.
struct DeviceIdRaw {
  uint8_t bytes[cmd::DEVICE_ID_LEN] = {}; ///< Raw bytes in bus order.
};

/// @brief Snapshot of current driver settings/state without performing I2C.
///
/// The snapshot is cache-only and never performs bus traffic. Use it from
/// diagnostics, status views, and examples when an application needs to display
/// the selected runtime variant and active capacity without disturbing health
/// counters. It also carries cached health counters and last-error state so
/// diagnostics do not need a separate bus-touching query.
struct SettingsSnapshot {
  bool initialized = false;       ///< True after bind()/begin() succeeds and before end()
  DriverState state = DriverState::UNINIT; ///< Current lifecycle/health state.
  bool online = false;            ///< True when bound; health classification never gates I2C.
  uint8_t i2cAddress = cmd::DEFAULT_ADDRESS; ///< Active 7-bit I2C address.
  uint32_t i2cTimeoutMs = 0;      ///< Configured per-transaction I2C timeout.
  size_t maxTxBytes = 0;          ///< Configured total TX transaction capability.
  size_t maxRxBytes = 0;          ///< Configured total RX transaction capability.
  size_t maxWriteDataBytes = 0;   ///< Active address-adjusted write data limit.
  size_t maxReadDataBytes = 0;    ///< Active read data limit.
  uint8_t offlineThreshold = 0;   ///< Optional diagnostic threshold; zero disables OFFLINE.
  uint32_t lastOkMs = 0;          ///< Last successful tracked I2C timestamp.
  uint32_t lastErrorMs = 0;       ///< Last failed tracked I2C timestamp.
  Status lastError = Status::Ok(); ///< Most recent tracked I2C/semantic error.
  uint8_t consecutiveFailures = 0; ///< Consecutive tracked failures since last success.
  uint32_t totalFailures = 0;     ///< Lifetime tracked failure count; wraps at uint32_t max.
  uint32_t totalSuccess = 0;      ///< Lifetime tracked success count; wraps at uint32_t max.
  bool hasNowMsHook = false;      ///< True when Config::nowMs is supplied.
  DeviceVariant expectedVariant = DeviceVariant::AUTO; ///< Configured variant expectation.
  DeviceVariant activeVariant = DeviceVariant::AUTO; ///< Active runtime variant after bind/identity.
  bool variantKnown = false;      ///< True when a supported variant is selected.
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
  bool currentAddressKnown = false; ///< True after a successful memory access seeds the pointer; false after conservative invalidation.
  uint32_t currentAddress = 0;    ///< Next byte address for Current Address Read.
};

/// @brief Result of comparing expected bytes with FRAM contents.
struct VerifyResult {
  bool match = false;               ///< True only when every requested byte matched.
  size_t mismatchOffset = 0;      ///< First mismatching byte offset from the requested start
  uint8_t expected = 0;           ///< Expected byte at mismatchOffset
  uint8_t actual = 0;             ///< Actual byte read at mismatchOffset
};

/// @brief Detailed result for logical write/fill operations split into chunks.
///
/// `bytesAccepted` counts the definitely transport-accepted prefix. Usually
/// that is the prefix of chunks returning `Status::Ok()`, but it also includes
/// a failed terminal result that proves its complete chunk was accepted. Thus
/// `complete` can be true while `status` is non-OK. Acceptance is not proof that
/// memory changed: hardware WP can allow ACK while preventing persistence. Use
/// verifyDetailed(), writeVerify(), or fillVerify() when persistence matters.
struct WriteResult {
  Status status = Status::Ok();   ///< Final transport/preflight status.
  uint32_t address = 0;           ///< Requested start address.
  size_t bytesRequested = 0;      ///< Bytes requested by caller.
  size_t bytesAccepted = 0;       ///< Prefix definitely accepted by transport.
  size_t failedChunkOffset = 0;   ///< Offset of first failed chunk, or bytesRequested on success.
  size_t failedChunkLength = 0;   ///< Length of first failed chunk, or 0 on success.
  WriteCommit writeCommit = WriteCommit::NOT_APPLICABLE; ///< Failed/final chunk effect.
  bool complete = false;          ///< True when all bytes were accepted, even if status reports a later error.
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

/// @brief Kind of cooperative transfer request.
enum class TransferKind : uint8_t {
  NONE = 0,      ///< No cooperative request is active or retained.
  READ,          ///< Addressed memory read into a caller-owned output buffer.
  WRITE,         ///< Addressed memory write from a caller-owned input buffer.
  FILL,          ///< Addressed fill with one repeated byte value.
  VERIFY,        ///< Addressed readback comparison against caller-owned bytes.
  VERIFIED_WRITE ///< Single-chunk write with readback and ambiguity reconciliation.
};

/// @brief Observable lifecycle of a cooperative transfer request.
enum class TransferState : uint8_t {
  IDLE = 0,                    ///< No request is active and no result is retained.
  ACTIVE,                      ///< A request is queued and may advance when polled.
  WAITING_FOR_RECONCILIATION,  ///< An ambiguous write awaits owner-authorized readback.
  SUCCEEDED,                   ///< The request completed successfully.
  FAILED,                      ///< The request ended with an operation failure.
  CANCELLED,                   ///< The owner cancelled the request between callbacks.
  TIMED_OUT                    ///< The owner declared the request deadline expired.
};

/// @brief Snapshot/result retaining no caller-owned transfer-buffer pointers.
///
/// Terminal results remain retained until takeTransferResult() consumes them
/// exactly once. A new request is rejected while a terminal result is pending.
struct TransferResult {
  uint32_t requestId = 0;             ///< Caller-supplied nonzero correlation ID.
  TransferKind kind = TransferKind::NONE; ///< Requested cooperative operation.
  TransferState state = TransferState::IDLE; ///< Current or terminal lifecycle state.
  Status status = Status::Ok();       ///< Current or terminal operation status.
  uint32_t address = 0;               ///< Requested starting memory address.
  size_t bytesRequested = 0;          ///< Total requested memory-data length.
  size_t bytesCompleted = 0;          ///< Definite read/match/accepted prefix.
  size_t failedChunkOffset = 0;       ///< Offset of the first failed chunk.
  size_t failedChunkLength = 0;       ///< Length of the first failed chunk.

  /// Most recent/failed write chunk effect, or VERIFIED after reconciliation.
  WriteCommit writeCommit = WriteCommit::NOT_APPLICABLE;

  /// Most recent write result; original failed write for reconciliation.
  Status writeStatus = Status::Ok();
  Status verifyStatus = Status::Ok(); ///< Most recent readback result, when attempted.
  bool match = false;                  ///< True after complete successful verification.
  size_t mismatchOffset = 0;           ///< First mismatching offset when known.
  uint8_t expected = 0;                ///< Expected byte at mismatchOffset.
  uint8_t actual = 0;                  ///< Observed byte at mismatchOffset.
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
  
  /// Bind configuration and an explicit variant without performing I2C.
  ///
  /// Fixed variants become immediately usable for validation, address encoding,
  /// and owner-directed transactions. AUTO remains bound but cannot perform
  /// memory access until readDeviceId() selects a supported variant.
  /// @param config Configuration including terminal transport callbacks.
  /// @return Status::Ok() when the passive binding is valid.
  Status bind(const Config& config);

  /// Compatibility lifecycle: bind, then perform one explicit presence/identity
  /// transaction. A transport/identity failure is returned but the valid passive
  /// binding is retained so the external owner may try again later.
  /// Does not configure or take ownership of the caller-managed I2C bus.
  /// @param config Configuration including transport callbacks
  /// @return Status::Ok() when binding and the compatibility check succeed.
  Status begin(const Config& config);
  
  /// Process bounded maintenance work (call regularly from loop).
  ///
  /// This hook performs no I2C work, delay, retry, allocation, or health update.
  /// It only uses caller-supplied time to advance Sleep wake recovery from
  /// WAKING to AWAKE. For variants without Sleep support, including MB85RC256V,
  /// this is a practical no-op.
  /// @param nowMs Current timestamp in milliseconds.
  void tick(uint32_t nowMs);
  
  /// Shutdown the driver without touching the caller-managed I2C bus.
  ///
  /// Active staged work is terminalized as CANCELLED without I2C. Its result
  /// retains no caller buffer pointer and remains available for exactly-once
  /// consumption. An already-terminal result is also retained. Call
  /// takeTransferResult() before bind()/begin() when either case leaves a
  /// result pending.
  void end();
  
  // =========================================================================
  // Diagnostics
  // =========================================================================
  
  /// Check if device is present on the bus (no health tracking).
  /// Requires a successful bind()/begin() because the active variant and configured
  /// transport are used. This is a diagnostic check only; it does not
  /// initialize, reset, recover, or take ownership of the physical I2C bus.
  /// Diagnostic probes do not establish a safe current-address-read starting
  /// point; explicit no-Device-ID probes conservatively clear cached
  /// current-address state because they use a raw memory read.
  /// @return Status::Ok() if device responds, error otherwise
  Status probe();
  
  /// Compatibility presence/identity check with health observation.
  /// Uses a tracked Device ID read when available, or a tracked memory-read
  /// probe for explicit no-Device-ID variants. Transport outcomes update
  /// passive health diagnostics; identity mismatches remain semantic errors.
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

  /// Check if bind()/begin() established a valid passive binding.
  /// @return true while a valid configuration is bound.
  bool isInitialized() const { return _initialized; }
  
  /// Check if the driver is passively bound. Health never gates admission.
  /// @return true while bound, including DEGRADED/OFFLINE diagnostic states.
  bool isOnline() const { return _initialized; }

  /// Get the active cached configuration.
  /// @return Reference to the cached configuration supplied to bind()/begin().
  const Config& getConfig() const { return _config; }

  /// Get active runtime variant metadata.
  /// @return Fixed variant metadata after bind(), decoded metadata after AUTO
  /// identity selection, or nullptr while AUTO remains unidentified/unbound.
  const cmd::VariantInfo* variantInfo() const { return _variant; }

  /// Get the active runtime variant name.
  /// @return Active variant name, or "unknown" when not selected.
  const char* variantName() const { return (_variant != nullptr) ? _variant->name : "unknown"; }

  /// Get cached Device ID fields from the last successful identity validation.
  /// @return Cached Device ID fields; zeros for explicit no-Device-ID variants.
  DeviceId deviceId() const { return _deviceId; }

  /// Get active runtime capacity in bytes.
  /// @return Active capacity in bytes, or 0 before AUTO identity selection.
  uint32_t capacityBytes() const;

  /// Get highest valid active runtime memory address.
  /// @return Highest valid byte address for the active runtime variant.
  uint32_t maxAddress() const;

  /// Get active variant's normal-mode I2C maximum bus rate.
  /// @return Datasheet normal-mode bus rate in hertz, or 0 before variant selection.
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
  /// Device ID uses the explicit special callback; probe()/recover() use the
  /// protocol appropriate to the active variant.
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
  /// With a `Config::nowMs` hook the driver enters WAKING and records a
  /// conservative millisecond deadline derived from the datasheet tREC; call
  /// tick() after that interval to transition back to AWAKE. Without a time
  /// hook the driver cannot measure tREC, so it reports AWAKE immediately and
  /// enforcing the recovery interval is entirely the caller's responsibility.
  /// The application must always allow at least sleepRecoveryUs() before
  /// access. No delay is inserted by the core.
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
  /// @return Diagnostic failure streak; never used to gate transport.
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }
  
  /// Total failure count (lifetime).
  /// @return Lifetime tracked failure count.
  uint32_t totalFailures() const { return _totalFailures; }
  
  /// Total success count (lifetime).
  /// @return Lifetime tracked success count.
  uint32_t totalSuccess() const { return _totalSuccess; }

  /// Maximum memory-data bytes accepted by writeOnce() for the active variant.
  /// @return Address-adjusted single-transaction write-data capacity, or 0
  /// before an active variant is selected.
  size_t maxWriteDataBytes() const;

  /// Maximum memory-data bytes accepted by readOnce()/verifyOnce().
  /// @return Single-transaction read-data capacity, or 0 before an active
  /// variant is selected.
  size_t maxReadDataBytes() const;
  
  // =========================================================================
  // Memory Read API
  // =========================================================================
  
  /// Read a single byte from the specified address.
  /// @param address Memory address within the active variant capacity.
  /// @param value Output byte
  /// @return Status::Ok() on success
  Status readByte(uint32_t address, uint8_t& value);

  /// Perform exactly one addressed read transaction.
  /// `len` must fit the configured RX transport capability.
  /// @param address Starting memory address within active capacity.
  /// @param buf Output buffer that receives exactly len bytes on success.
  /// @param len Number of bytes in `1..maxReadDataBytes()`.
  /// @return Status::Ok() after one complete read callback, otherwise the
  /// validation or terminal transport error.
  Status readOnce(uint32_t address, uint8_t* buf, size_t len);

  /// Read multiple bytes starting at the specified address.
  /// This is a blocking, synchronous convenience API. Poll-budgeted systems
  /// that must advance one backend transfer per scheduler poll should use a
  /// staged adapter/API rather than this whole-range helper.
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
  /// This compatibility convenience does not expose failed-write commit state;
  /// use writeOnce() when that provenance is required.
  /// @param address Memory address within the active variant capacity.
  /// @param value Byte to write
  /// @return Status::Ok() on success
  Status writeByte(uint32_t address, uint8_t value);

  /// Perform exactly one addressed write transaction.
  ///
  /// On a transport failure `writeCommit` preserves whether no data was
  /// accepted or the physical effect is indeterminate. This method never
  /// retries. Transport acceptance does not prove persistence while WP is high.
  /// @param address Starting memory address within active capacity.
  /// @param buf Input data that remains valid for the synchronous call.
  /// @param len Number of bytes in `1..maxWriteDataBytes()`.
  /// @param writeCommit Optional output for transport-acceptance evidence.
  /// @return Status::Ok() after one complete write callback, otherwise the
  /// validation or terminal transport error.
  Status writeOnce(uint32_t address, const uint8_t* buf, size_t len,
                   WriteCommit* writeCommit = nullptr);

  /// Write multiple bytes starting at the specified address.
  /// This is a blocking, synchronous convenience API. Poll-budgeted systems
  /// that must advance one backend transfer per scheduler poll should use a
  /// staged adapter/API rather than this whole-range helper.
  /// The full range must fit before the active variant's end address.
  /// No page boundary limitations (unlike EEPROM).
  /// FRAM writes are immediate - no write delay needed.
  /// Large logical writes may be split into I2C chunks and are not atomic:
  /// a later chunk can fail after earlier chunks were accepted by transport.
  /// A successful write means the bus transaction was accepted, not that data
  /// persistence was verified when external WP may be asserted.
  /// This compatibility convenience returns only the first error. Use
  /// writeDetailed() or requestWrite() when accepted-prefix and failed-chunk
  /// provenance must remain observable.
  /// @param address Starting memory address within the active variant capacity.
  /// @param buf Data buffer to write
  /// @param len Number of bytes to write
  /// @return Status::Ok() on success
  Status write(uint32_t address, const uint8_t* buf, size_t len);

  /// Write multiple bytes and report the accepted prefix.
  /// This is a blocking, synchronous convenience API. Poll-budgeted systems
  /// that must advance one backend transfer per scheduler poll should use a
  /// staged adapter/API rather than this whole-range helper.
  /// `bytesAccepted` counts bytes in chunks accepted by the transport. It does
  /// not prove that those bytes persisted when external WP may be asserted.
  /// A failed chunk may also have an unknown physical commit state if the
  /// transport reports a timeout after the device possibly accepted bytes.
  /// @param address Starting memory address within the active variant capacity.
  /// @param buf Data buffer to write
  /// @param len Number of bytes to write
  /// @return Detailed accepted-prefix result. `bytesAccepted` is not proof of persistence.
  WriteResult writeDetailed(uint32_t address, const uint8_t* buf, size_t len);

  /// Fill a range of memory with a constant byte value.
  /// This is a blocking, synchronous convenience API. Poll-budgeted systems
  /// that must advance one backend transfer per scheduler poll should use a
  /// staged adapter/API rather than this whole-range helper.
  /// Large logical fills may be split into I2C chunks and are not atomic.
  /// Status::Ok() means all chunks were accepted by the transport, not that
  /// persistence was verified when external WP may be asserted.
  /// This compatibility convenience returns only the first error. Use
  /// fillDetailed() or requestFill() when accepted-prefix and failed-chunk
  /// provenance must remain observable.
  /// @param address Starting memory address within the active variant capacity.
  /// @param value Fill byte
  /// @param len Number of bytes to fill
  /// @return Status::Ok() on success
  Status fill(uint32_t address, uint8_t value, size_t len);

  /// Fill a range and report the accepted prefix.
  /// This is a blocking, synchronous convenience API. Poll-budgeted systems
  /// that must advance one backend transfer per scheduler poll should use a
  /// staged adapter/API rather than this whole-range helper.
  /// `bytesAccepted` counts bytes in chunks accepted by the transport. It does
  /// not prove that those bytes persisted when external WP may be asserted.
  /// A failed chunk may also have an unknown physical commit state if the
  /// transport reports a timeout after the device possibly accepted bytes.
  /// @param address Starting memory address within the active variant capacity.
  /// @param value Fill byte
  /// @param len Number of bytes to fill
  /// @return Detailed accepted-prefix result. `bytesAccepted` is not proof of persistence.
  WriteResult fillDetailed(uint32_t address, uint8_t value, size_t len);
  
  // =========================================================================
  // Device Information
  // =========================================================================
  
  /// Read the 3-byte Device ID from the device.
  /// Uses the reserved I2C addresses 0xF8/0xF9. The read phase must NACK the
  /// final ID byte and STOP; ACK after byte 3 may repeat the ID stream.
  /// Returns INVALID_PARAM when the active variant has no Device ID command.
  /// @param id Output Device ID fields
  /// @return Status::Ok() on success
  Status readDeviceId(DeviceId& id);

  /// Read the raw 3-byte Device ID payload from the device.
  /// Returns INVALID_PARAM when the active variant has no Device ID command.
  /// @param raw Output raw Device ID bytes as transmitted on the bus
  /// @return Status::Ok() on success
  Status readDeviceIdRaw(DeviceIdRaw& raw);

  /// Pure decode of a raw three-byte Device ID; performs no I2C.
  /// @param raw Raw three-byte Device ID payload.
  /// @return Decoded identity and exact known variant, or AUTO when unknown.
  static DeviceId decodeDeviceId(const DeviceIdRaw& raw);

  /// Read the byte at the device's current internal address pointer.
  /// The pointer is undefined after power-on and only safe after a known
  /// address-setting transaction, such as a successful addressed memory
  /// read/write by this instance. Failed transactions and recovery paths
  /// conservatively invalidate cached current-address state.
  /// @param value Output byte
  /// @return Status::Ok() on success
  Status readCurrentAddress(uint8_t& value);

  /// Read multiple bytes using repeated documented current-address-read operations.
  /// This is a blocking, synchronous convenience API. Poll-budgeted systems
  /// that must advance one backend transfer per scheduler poll should use a
  /// staged adapter/API rather than this whole-range helper.
  /// The pointer is undefined after power-on and only safe after a known
  /// address-setting transaction, such as a successful addressed memory
  /// read/write by this instance. Failed transactions and recovery paths
  /// conservatively invalidate cached current-address state.
  /// Performs exactly `len` one-byte transport callbacks; use read() for bulk
  /// transfers that should use bounded multi-byte chunks.
  /// @param buf Output buffer
  /// @param len Number of bytes to read
  /// @return Status::Ok() on success
  Status readCurrentAddress(uint8_t* buf, size_t len);

  /// Compare FRAM contents against an expected buffer.
  /// This is a blocking, synchronous convenience API. Poll-budgeted systems
  /// that must advance one backend transfer per scheduler poll should use a
  /// staged adapter/API rather than this whole-range helper.
  /// Status::Ok() means the readback transactions completed; check out.match
  /// to determine whether all bytes matched.
  /// @param address Starting memory address within the active variant capacity.
  /// @param expected Expected bytes
  /// @param len Number of bytes to compare
  /// @param out Comparison result
  /// @return Status::Ok() on successful comparison transaction(s)
  Status verify(uint32_t address, const uint8_t* expected, size_t len, VerifyResult& out);

  /// Perform exactly one addressed read transaction and compare its bytes.
  /// Returns VERIFY_MISMATCH on the first mismatch.
  /// @param address Starting memory address within active capacity.
  /// @param expected Expected bytes.
  /// @param len Number of bytes in `1..maxReadDataBytes()`.
  /// @param out Match or first-mismatch details.
  /// @return Status::Ok() when all bytes match, VERIFY_MISMATCH on a content
  /// difference, otherwise the validation or terminal transport error.
  Status verifyOnce(uint32_t address, const uint8_t* expected, size_t len,
                    VerifyResult& out);

  /// Compare FRAM contents against an expected buffer with byte counts.
  /// This is a blocking, synchronous convenience API. Poll-budgeted systems
  /// that must advance one backend transfer per scheduler poll should use a
  /// staged adapter/API rather than this whole-range helper.
  /// `bytesVerified` counts bytes that matched before the first mismatch or
  /// transport failure.
  /// @param address Starting memory address within the active variant capacity.
  /// @param expected Expected bytes
  /// @param len Number of bytes to compare
  /// @return Detailed verification result.
  VerifyDetailedResult verifyDetailed(uint32_t address, const uint8_t* expected, size_t len);

  /// Write and then verify the same bytes by readback.
  /// This is a blocking, synchronous convenience API. Poll-budgeted systems
  /// that must advance one backend transfer per scheduler poll should use a
  /// staged adapter/API rather than this whole-range helper.
  /// Use for writes where transport acceptance is not enough and readback
  /// confirmation is required. Returns `Err::VERIFY_MISMATCH` when the write
  /// transport succeeds but readback differs. If the write phase returns a
  /// transport error, this method returns that error without performing
  /// readback because bus state and failed-chunk commit state are unknown.
  /// @param address Starting memory address within the active variant capacity.
  /// @param buf Data buffer to write and verify
  /// @param len Number of bytes to write and verify
  /// @param verifyOut Optional detailed verification result
  /// @return Status::Ok() only when write transport succeeds and readback matches.
  Status writeVerify(uint32_t address, const uint8_t* buf, size_t len,
                     VerifyDetailedResult* verifyOut = nullptr);

  /// Fill and then verify the same byte value by readback.
  /// This is a blocking, synchronous convenience API. Poll-budgeted systems
  /// that must advance one backend transfer per scheduler poll should use a
  /// staged adapter/API rather than this whole-range helper.
  /// Use for fills where transport acceptance is not enough and readback
  /// confirmation is required. Returns `Err::VERIFY_MISMATCH` when the fill
  /// transport succeeds but readback differs. If the fill phase returns a
  /// transport error, this method returns that error without performing
  /// readback because bus state and failed-chunk commit state are unknown.
  /// @param address Starting memory address within the active variant capacity.
  /// @param value Fill byte to write and verify.
  /// @param len Number of bytes to fill and verify.
  /// @param verifyOut Optional detailed verification result.
  /// @return Status::Ok() only when fill transport succeeds and readback matches.
  Status fillVerify(uint32_t address, uint8_t value, size_t len,
                    VerifyDetailedResult* verifyOut = nullptr);

  // =========================================================================
  // Poll-Chunked Transfer API
  // =========================================================================

  /// Queue a staged explicit-address read transfer.
  ///
  /// The request performs validation only and emits no I2C traffic. Call
  /// pollTransfer() to execute bounded random-read chunks. Each chunk carries
  /// its own memory address and counts as one instruction.
  /// @param address Starting memory address within the active variant capacity.
  /// @param data Output buffer that must remain valid until the request reaches
  /// a terminal state; the completed prefix may change after each poll.
  /// @param length Number of bytes to read.
  /// @return Status::Ok() when queued, BUSY if another transfer is active or
  /// Sleep state blocks memory I2C.
  Status requestRead(uint32_t address, uint8_t* data, size_t length);
  /// Request-qualified overload for external-owner correlation.
  /// @param requestId Caller-supplied nonzero correlation ID.
  /// @param address Starting memory address within active capacity.
  /// @param data Output buffer with the lifetime documented above.
  /// @param length Number of bytes to read.
  /// @return Status::Ok() when queued, otherwise a preflight error.
  Status requestRead(uint32_t requestId, uint32_t address, uint8_t* data, size_t length);

  /// Queue a staged explicit-address write transfer.
  ///
  /// The request performs validation only and emits no I2C traffic. Call
  /// pollTransfer() to execute bounded sequential-write chunks. Successful
  /// chunks are not rolled back if a later chunk fails.
  /// @param address Starting memory address within the active variant capacity.
  /// @param data Source buffer that must remain valid and unmodified until the
  /// request reaches a terminal state.
  /// @param length Number of bytes to write.
  /// @return Status::Ok() when queued, BUSY if another transfer is active or
  /// Sleep state blocks memory I2C.
  Status requestWrite(uint32_t address, const uint8_t* data, size_t length);
  /// Request-qualified overload for external-owner correlation.
  /// @param requestId Caller-supplied nonzero correlation ID.
  /// @param address Starting memory address within active capacity.
  /// @param data Input buffer with the lifetime documented above.
  /// @param length Number of bytes to write.
  /// @return Status::Ok() when queued, otherwise a preflight error.
  Status requestWrite(uint32_t requestId, uint32_t address,
                      const uint8_t* data, size_t length);

  /// Queue a staged explicit-address fill transfer.
  ///
  /// The request performs validation only and emits no I2C traffic. Call
  /// pollTransfer() to execute bounded addressed write chunks using a fixed
  /// local fill buffer. Successful chunks are not rolled back if a later chunk
  /// fails.
  /// @param address Starting memory address within the active variant capacity.
  /// @param value Fill byte.
  /// @param length Number of bytes to fill.
  /// @return Status::Ok() when queued, BUSY if another transfer is active or
  /// Sleep state blocks memory I2C.
  Status requestFill(uint32_t address, uint8_t value, size_t length);
  /// Request-qualified overload for external-owner correlation.
  /// @param requestId Caller-supplied nonzero correlation ID.
  /// @param address Starting memory address within active capacity.
  /// @param value Fill byte.
  /// @param length Number of bytes to fill.
  /// @return Status::Ok() when queued, otherwise a preflight error.
  Status requestFill(uint32_t requestId, uint32_t address, uint8_t value, size_t length);

  /// Queue a staged explicit-address readback verification transfer.
  ///
  /// Each poll instruction reads one addressed chunk and compares it before the
  /// next chunk is attempted. A mismatch terminates the transfer with
  /// Err::VERIFY_MISMATCH and detail set to the first mismatching offset.
  /// @param address Starting memory address within the active variant capacity.
  /// @param data Expected bytes that must remain valid and unmodified until the
  /// request reaches a terminal state.
  /// @param length Number of bytes to verify.
  /// @return Status::Ok() when queued, BUSY if another transfer is active or
  /// Sleep state blocks memory I2C.
  Status requestVerify(uint32_t address, const uint8_t* data, size_t length);
  /// Request-qualified overload for external-owner correlation.
  /// @param requestId Caller-supplied nonzero correlation ID.
  /// @param address Starting memory address within active capacity.
  /// @param data Expected bytes with the lifetime documented above.
  /// @param length Number of bytes to verify.
  /// @return Status::Ok() when queued, otherwise a preflight error.
  Status requestVerify(uint32_t requestId, uint32_t address,
                       const uint8_t* data, size_t length);

  /// Queue a bounded cooperative write followed by readback verification.
  ///
  /// The complete request must fit one configured write and one configured read
  /// transaction. A successful write advances to verify without replay. An
  /// indeterminate failed write enters WAITING_FOR_RECONCILIATION: polling then
  /// performs zero callbacks until resumeVerifiedWrite() authorizes readback.
  /// `data` must remain valid and unmodified until the request reaches a
  /// terminal state, including throughout reconciliation waiting.
  /// @param requestId Caller-supplied nonzero correlation ID.
  /// @param address Starting memory address within active capacity.
  /// @param data Input/expected bytes retained through terminal state.
  /// @param length Length that must fit one write and one read transaction.
  /// @return Status::Ok() when queued, otherwise a preflight error.
  Status requestVerifiedWrite(uint32_t requestId, uint32_t address,
                              const uint8_t* data, size_t length);

  /// Execute bounded work for a queued transfer.
  ///
  /// A random-read chunk, sequential-write chunk, or verify readback chunk is
  /// one instruction. `maxInstructions == 0` performs no I2C and returns the
  /// current transfer status. Values above
  /// cmd::MAX_TRANSFER_INSTRUCTIONS_PER_POLL are clamped. Device ID and
  /// current-address reads remain single-instruction synchronous diagnostics
  /// outside this staged API.
  /// @param nowMs Current timestamp in milliseconds for Sleep wake advancement.
  /// @param maxInstructions Maximum transfer chunks to execute this poll.
  /// @return IN_PROGRESS while queued work remains, OK on completion, or error.
  Status pollTransfer(uint32_t nowMs, uint8_t maxInstructions);

  /// @return true while a staged transfer is queued or being polled.
  bool isTransferBusy() const { return _transferBusy(); }

  /// @return Last staged transfer status, or OK before any transfer.
  Status getTransferStatus() const { return _transfer.result.status; }

  /// Copy active progress or retained terminal result without retaining caller buffer pointers.
  /// @param out Output snapshot of the active or retained result.
  /// @return Status::Ok() when progress/result exists, otherwise NO_RESULT.
  Status getTransferProgress(TransferResult& out) const;

  /// Consume the retained terminal result exactly once.
  /// @param out Output terminal result.
  /// @return Status::Ok() when consumed, otherwise NO_RESULT.
  Status takeTransferResult(TransferResult& out);

  /// Authorize verify-only reconciliation after an indeterminate write.
  /// @param requestId Exact active request correlation ID.
  /// @return Status::Ok() when verify-only readback is resumed.
  Status resumeVerifiedWrite(uint32_t requestId);

  /// Cancel the active staged transfer, if any.
  ///
  /// Cancellation emits no I2C traffic. Already accepted write/fill chunks are
  /// not rolled back; current-address tracking remains whatever the last
  /// successful chunk established.
  /// @return Status::Ok() when the active request is cancelled, otherwise
  /// NO_RESULT.
  Status cancelTransfer();
  /// Cancel an active request only when its identity matches.
  /// @param requestId Exact active request correlation ID.
  /// @return Status::Ok() when cancelled, BUSY on identity mismatch, or
  /// NO_RESULT when no request is active.
  Status cancelTransfer(uint32_t requestId);

  /// Terminalize the active request as timed out without performing I2C.
  /// @param requestId Exact active request correlation ID.
  /// @return Status::Ok() when timed out, BUSY on identity mismatch, or
  /// NO_RESULT when no request is active.
  Status timeoutTransfer(uint32_t requestId);

  /// Get the legacy MB85RC256V memory size in bytes.
  /// Prefer capacityBytes() for runtime-selected variants.
  /// @deprecated Use capacityBytes() after bind()/identity selection.
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
  Status _i2cWriteRaw(uint8_t addr, const uint8_t* buf, size_t len,
                      size_t memoryAddressBytes,
                      WriteCommit* writeCommit = nullptr);

  /// Raw special I2C operation (no health tracking)
  Status _i2cSpecialRaw(I2cSpecialOp op, const I2cSpecialTransfer& transfer,
                        WriteCommit* writeCommit = nullptr,
                        size_t memoryAddressBytes = 0U);
  
  /// Tracked I2C write-read to an explicit address (updates health)
  Status _i2cWriteReadTrackedAddr(uint8_t addr, const uint8_t* txBuf, size_t txLen,
                                  uint8_t* rxBuf, size_t rxLen);

  /// Tracked I2C write to an explicit address (updates health)
  Status _i2cWriteTrackedAddr(uint8_t addr, const uint8_t* buf, size_t len,
                              size_t memoryAddressBytes,
                              WriteCommit* writeCommit = nullptr);

  /// Tracked special I2C operation (updates health)
  Status _i2cSpecialTracked(I2cSpecialOp op, const I2cSpecialTransfer& transfer,
                            WriteCommit* writeCommit = nullptr);

  /// Validate callback completion counts and map terminal transport codes.
  static Status _mapTransportResult(const TransportResult& result,
                                    size_t expectedTx, size_t expectedRx,
                                    bool memoryWrite, size_t memoryAddressBytes,
                                    WriteCommit* writeCommit = nullptr);
  
  // =========================================================================
  // Internal Helpers
  // =========================================================================
  
  /// Encoded memory-address transaction header for the active variant.
  struct EncodedMemoryAddress {
    uint8_t i2cAddress = cmd::DEFAULT_ADDRESS;
    uint8_t bytes[cmd::ADDRESS_BYTES] = {};
    size_t len = 0;
  };

  struct TransferJob {
    TransferResult result;
    uint8_t* data = nullptr;
    const uint8_t* constData = nullptr;
    uint8_t fillValue = 0;
    size_t offset = 0;
    bool resultPending = false;
    bool verifyPhase = false;
  };

  /// Read from memory using tracked path
  Status _readMemory(uint32_t address, uint8_t* buf, size_t len);

  /// Read from memory using raw path (no health tracking)
  Status _readMemoryRaw(uint32_t address, uint8_t* buf, size_t len);

  /// Write to memory using tracked path
  Status _writeMemory(uint32_t address, const uint8_t* buf, size_t len,
                      WriteCommit* writeCommit = nullptr);

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

  /// Select and validate the active runtime variant from a Device ID readback.
  Status _selectVariant(const DeviceId& id);

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

  /// True when a staged transfer is active.
  bool _transferBusy() const;

  /// Return BUSY if a staged transfer is active.
  Status _ensureNoTransferActive() const;

  /// Reset staged transfer state and preserve a terminal status.
  void _finishTransfer(TransferState state, const Status& status);

  /// Queue a validated staged transfer.
  Status _requestTransfer(uint32_t requestId, TransferKind kind,
                          uint32_t address, uint8_t* data,
                           const uint8_t* constData, uint8_t fillValue,
                           size_t length);

  /// Execute one staged transfer instruction.
  Status _pollTransferInstruction();

  /// Allocate a nonzero compatibility request ID not colliding with retained state.
  uint32_t _allocateRequestId();
  
  // =========================================================================
  // Health Management
  // =========================================================================
  
  /// Update health counters and state based on operation result.
  /// Called ONLY from tracked transport wrappers.
  Status _updateHealth(const Status& st);


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
  TransferJob _transfer;
  uint32_t _nextRequestId = 1;
};

}  // namespace MB85RC
