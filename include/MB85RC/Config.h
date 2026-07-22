/// @file Config.h
/// @brief Configuration structure for MB85RC driver
#pragma once

#include <cstddef>
#include <cstdint>
#include "MB85RC/CommandTable.h"
#include "MB85RC/Status.h"

namespace MB85RC {

/// @brief Terminal result code returned by an injected transport callback.
///
/// A callback represents exactly one completed physical I2C transaction. It
/// must return only after that transaction has reached a terminal outcome; the
/// transport contract has no queued or in-progress result.
enum class TransportCode : uint8_t {
  OK = 0,          ///< The complete requested transaction was transferred.
  NACK_ADDRESS,    ///< The addressed device did not acknowledge.
  NACK_DATA,       ///< A transmitted data byte did not acknowledge.
  TIMEOUT,         ///< The transport's per-transaction deadline expired.
  BUS_ERROR,       ///< Arbitration, controller, or physical bus failure.
  IO_ERROR         ///< Other terminal transport failure.
};

/// @brief Knowledge of a failed write transaction's physical effect.
///
/// This describes transport acceptance, not durable FRAM persistence: hardware
/// WP can allow an acknowledged transaction while suppressing the memory
/// change. `INDETERMINATE` prevents the core from replaying a possibly accepted
/// write; callers must reconcile it by readback.
enum class WriteCommit : uint8_t {
  NOT_APPLICABLE = 0, ///< No memory-data write was requested.
  NOT_COMMITTED,      ///< Transport proves that no requested data was accepted.
  ACCEPTED,           ///< The full write transaction was acknowledged.
  INDETERMINATE,      ///< Some or all requested data may have been accepted.
  VERIFIED            ///< Requested content was later observed by readback.
};

/// @brief Typed, terminal outcome of one injected transport callback.
struct TransportResult {
  TransportCode code = TransportCode::IO_ERROR; ///< Terminal transport classification.
  int32_t detail = 0; ///< Transport-owned numeric detail; no borrowed text pointer.
  WriteCommit writeCommit = WriteCommit::INDETERMINATE; ///< Memory-write effect knowledge.

  /// Bytes completed in `txData`/the normal callback TX buffer. For memory
  /// writes this count includes the one- or two-byte memory-address prefix;
  /// `writeCommit` separately describes acceptance of the requested data.
  /// Hidden special-operation envelope bytes (reserved Device-ID address,
  /// High-speed master code, Sleep command framing) are not included.
  size_t completedTxBytes = 0;

  /// Bytes completed in `rxData`/the normal callback RX buffer. Hidden
  /// special-operation envelope bytes are not included.
  size_t completedRxBytes = 0;

  /// Construct a terminal transport result.
  ///
  /// The explicit constructor keeps value construction available on C++11
  /// toolchains, where a type with default member initializers is not an
  /// aggregate.
  /// @param resultCode Terminal transport classification.
  /// @param detailCode Transport-owned numeric diagnostic detail.
  /// @param commit Memory-write effect knowledge.
  /// @param txBytes Callback-buffer TX bytes completed.
  /// @param rxBytes Callback-buffer RX bytes completed.
  constexpr TransportResult(TransportCode resultCode = TransportCode::IO_ERROR,
                            int32_t detailCode = 0,
                            WriteCommit commit = WriteCommit::INDETERMINATE,
                            size_t txBytes = 0, size_t rxBytes = 0)
      : code(resultCode),
        detail(detailCode),
        writeCommit(commit),
        completedTxBytes(txBytes),
        completedRxBytes(rxBytes) {}

  /// @return true only when the complete callback transaction succeeded.
  constexpr bool ok() const { return code == TransportCode::OK; }

  /// Construct a successful terminal result.
  /// @param txBytes Complete callback-buffer TX byte count.
  /// @param rxBytes Complete callback-buffer RX byte count.
  /// @return Successful terminal result with no write-effect claim.
  static constexpr TransportResult Ok(size_t txBytes, size_t rxBytes) {
    return TransportResult{TransportCode::OK, 0, WriteCommit::NOT_APPLICABLE,
                           txBytes, rxBytes};
  }

  /// Construct a failed terminal result.
  /// @param error Terminal transport failure classification.
  /// @param detailCode Transport-owned numeric diagnostic detail.
  /// @param commit Memory-write effect knowledge.
  /// @param txBytes Callback-buffer TX bytes completed before failure.
  /// @param rxBytes Callback-buffer RX bytes completed before failure.
  /// @return Failed terminal result retaining the supplied evidence.
  static constexpr TransportResult Error(TransportCode error, int32_t detailCode = 0,
                                         WriteCommit commit = WriteCommit::INDETERMINATE,
                                         size_t txBytes = 0, size_t rxBytes = 0) {
    return TransportResult{error, detailCode, commit, txBytes, rxBytes};
  }
};

/// I2C write callback signature.
///
/// The application-owned transport controls bus locking, timeout enforcement,
/// retry policy, and bus recovery. Callbacks must not recursively call public
/// methods on the same MB85RC instance.
/// @param addr I2C device address (7-bit)
/// @param data Pointer to data to write
/// @param len Number of bytes to write
/// @param timeoutMs Maximum time to wait for completion
/// @param user User context pointer passed through from Config
/// `OK` means every requested byte was transferred and is normalized to
/// `WriteCommit::ACCEPTED`. On failure, `writeCommit` must say whether none of
/// the requested memory data was accepted, all of it was accepted before a
/// later controller/STOP error, or its effect is indeterminate. TX completion
/// counts include memory-address prefix bytes when the core supplies them.
/// The callback must not retry or recover the bus internally.
/// @return Terminal result of this one physical transaction.
using I2cWriteFn = TransportResult (*)(uint8_t addr, const uint8_t* data, size_t len,
                                       uint32_t timeoutMs, void* user);

/// I2C write-then-read callback signature.
///
/// The application-owned transport controls bus locking, timeout enforcement,
/// retry policy, and bus recovery. Callbacks must not recursively call public
/// methods on the same MB85RC instance.
/// @param addr I2C device address (7-bit)
/// @param txData Pointer to data to write (nullable when txLen == 0)
/// @param txLen Number of bytes to write (0 allowed for read-only transactions)
/// @param rxData Pointer to buffer for read data (nullable when rxLen == 0)
/// @param rxLen Number of bytes to read
/// @param timeoutMs Maximum time to wait for completion
/// @param user User context pointer passed through from Config
/// `OK` means the complete TX and RX lengths were transferred. When both phases
/// are present, the callback must issue a repeated START with no STOP between
/// them. RX contents are unspecified after failure. The callback must not retry
/// or recover the bus internally.
/// @return Terminal result of this one physical transaction.
using I2cWriteReadFn = TransportResult (*)(uint8_t addr, const uint8_t* txData, size_t txLen,
                                           uint8_t* rxData, size_t rxLen,
                                           uint32_t timeoutMs, void* user);

/// @brief Bus-level operations that do not fit normal 7-bit callbacks.
///
/// These operations remain application-owned. The callback is optional for
/// fixed variants that do not use these features, and required when AUTO needs
/// identity selection. The core requests it only for a documented feature.
enum class I2cSpecialOp : uint8_t {
  HIGH_SPEED_WRITE = 0,      ///< HS master code, expected NACK, then a write transaction.
  HIGH_SPEED_WRITE_READ = 1, ///< HS master code, expected NACK, then a write/read transaction.
  ENTER_SLEEP = 2,           ///< F8h + device address word + repeated-start 86h; R/W bit is don't-care.
  WAKE_FROM_SLEEP = 3,       ///< Device address wake stimulus; ACK may be indeterminate.
  READ_DEVICE_ID = 4         ///< Reserved F8h/F9h Device ID sequence; not a normal 7-bit transfer.
};

/// @brief Parameters for optional special I2C operations.
///
/// For High-speed operations, the callback must consume the expected NACK from
/// the `0000 1XXX` master-code byte internally and return OK only when the
/// complete HS-prefixed transaction succeeds. Generic I2C NACKs must remain
/// failures outside that narrow prefix. For Sleep command address words, the
/// R/W bit is don't-care; for MB85RC1MT, A16 is also don't-care in that command
/// form. TransportResult completion counts cover only the `txData` and `rxData`
/// buffers below. They exclude hidden reserved-address, High-speed master-code,
/// Sleep-command, and wake-stimulus envelope bytes.
struct I2cSpecialTransfer {
  uint8_t i2cAddress = cmd::DEFAULT_ADDRESS; ///< Active/encoded 7-bit device address.
  uint8_t hsMasterCode = cmd::HIGH_SPEED_MASTER_CODE_DEFAULT; ///< Raw 8-bit HS master code.
  const uint8_t* txData = nullptr; ///< Transaction write bytes, if any.
  size_t txLen = 0;                ///< Number of transaction write bytes.
  uint8_t* rxData = nullptr;       ///< Transaction read buffer, if any.
  size_t rxLen = 0;                ///< Number of transaction read bytes.
  uint16_t recoveryUs = cmd::SLEEP_RECOVERY_US; ///< Sleep wake recovery contract.
};

/// Optional special I2C operation callback signature.
///
/// The callback owns raw controller details, expected-NACK handling for HS
/// master code, bus clock selection, sleep wake timing policy, and locking.
/// READ_DEVICE_ID must implement the reserved F8h/F9h sequence described by the
/// datasheet; it must not route address 0x7C through a normal-device backend.
/// It must not recursively call public methods on the same MB85RC instance.
using I2cSpecialFn = TransportResult (*)(I2cSpecialOp op,
                                         const I2cSpecialTransfer& transfer,
                                         uint32_t timeoutMs, void* user);

/// Millisecond timestamp callback.
/// @param user User context pointer passed through from Config
/// @return Current monotonic milliseconds
using NowMsFn = uint32_t (*)(void* user);

/// Minimum accepted per-transaction timeout passed to injected I2C callbacks.
static constexpr uint32_t MIN_I2C_TIMEOUT_MS = 1UL;

/// Default per-transaction timeout passed to injected I2C callbacks.
static constexpr uint32_t DEFAULT_I2C_TIMEOUT_MS = 50UL;

/// Maximum accepted per-transaction timeout passed to injected I2C callbacks.
static constexpr uint32_t MAX_I2C_TIMEOUT_MS = 1000UL;

/// Fixed core TX buffer capacity; larger transport capabilities are accepted.
static constexpr size_t MAX_TRANSPORT_TX_BYTES = 128U;

/// Fixed core RX buffer capacity; larger transport capabilities are accepted.
static constexpr size_t MAX_TRANSPORT_RX_BYTES = 128U;

/// @brief Configuration for MB85RC driver.
///
/// `i2cUser`, `timeUser`, and the state they reference must remain valid until
/// end() or until a later successful bind()/begin() replaces the configuration.
/// A failed replacement leaves the previous binding intact. Callback
/// invocations are synchronous and never outlive the public call that made
/// them.
struct Config {
  // === I2C Transport (required) ===
  I2cWriteFn i2cWrite = nullptr;         ///< I2C write function pointer
  I2cWriteReadFn i2cWriteRead = nullptr; ///< I2C write-read function pointer
  I2cSpecialFn i2cSpecial = nullptr;     ///< Device-ID/HS/Sleep function; required for AUTO
  void* i2cUser = nullptr;               ///< Context with the Config lifetime documented above

  // === Timing Hooks (optional) ===
  NowMsFn nowMs = nullptr;               ///< Optional monotonic ms source for health and Sleep wake gating
  void* timeUser = nullptr;              ///< Timing context with the same required lifetime

  // === Device Settings ===
  /// Base 7-bit I2C address.
  ///
  /// This is the board strap base address, not a memory-bank encoded
  /// transaction address. Common values remain `0x50`-`0x57`, but variants that
  /// encode memory address bits into the I2C address accept only unambiguous
  /// bases: `MB85RC04V` and `MB85RC1MT` use even bases `0x50`, `0x52`, `0x54`,
  /// or `0x56`; `MB85RC16V` uses only `0x50`.
  uint8_t i2cAddress = 0x50;
  uint32_t i2cTimeoutMs = DEFAULT_I2C_TIMEOUT_MS; ///< Callback deadline in `MIN_I2C_TIMEOUT_MS..MAX_I2C_TIMEOUT_MS`
  size_t maxTxBytes = MAX_TRANSPORT_TX_BYTES; ///< Maximum total TX bytes accepted by callbacks.
  size_t maxRxBytes = MAX_TRANSPORT_RX_BYTES; ///< Maximum total RX bytes accepted by callbacks.
  uint8_t highSpeedMasterCode = cmd::HIGH_SPEED_MASTER_CODE_DEFAULT; ///< Raw `0000 1XXX` HS code
  uint16_t sleepRecoveryUs = cmd::SLEEP_RECOVERY_US; ///< Must be 0 or >= active variant tREC

  /// Expected runtime variant.
  ///
  /// The default is `AUTO` so Device-ID-capable parts select their active
  /// capacity from the device identifier instead of silently assuming 256V.
  /// AUTO requires `i2cSpecial` because Device ID uses reserved-address framing.
  /// Production fixed-BOM integrations should set the exact part number so
  /// unexpected substitutions fail early. Select `MB85RC16V` explicitly because
  /// that variant has no Device ID command and cannot be discovered by `AUTO`.
  DeviceVariant expectedVariant = DeviceVariant::AUTO;

  // === Health Tracking ===
  /// Optional diagnostic threshold. Zero disables OFFLINE classification.
  /// READY/DEGRADED/OFFLINE are observations only and never gate transport.
  uint8_t offlineThreshold = 0;
};

}  // namespace MB85RC
