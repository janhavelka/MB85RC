/// @file Config.h
/// @brief Configuration structure for MB85RC driver
#pragma once

#include <cstddef>
#include <cstdint>
#include "MB85RC/CommandTable.h"
#include "MB85RC/Status.h"

namespace MB85RC {

/// @brief Runtime device variant selection for Device ID validation and memory bounds.
///
/// `AUTO` selects a supported runtime variant from Device ID readback. It can
/// only identify variants that implement the Device ID command. `MB85RC16V`
/// has no Device ID command in the local datasheet set, so applications must
/// select it explicitly when using that part.
enum class DeviceVariant : uint8_t {
  AUTO = 0,       ///< Select a supported runtime variant from Device ID.
  MB85RC256V = 1, ///< Expect MB85RC256V, 32 KiB, Product ID 0x510.
  MB85RC64TA = 2, ///< Expect MB85RC64TA, 8 KiB, Product ID 0x358.
  MB85RC04V = 3,  ///< Expect MB85RC04V, 512 bytes, Product ID 0x010.
  MB85RC16V = 4,  ///< Expect MB85RC16V, 2 KiB, no Device ID command.
  MB85RC512T = 5, ///< Expect MB85RC512T, 64 KiB, Product ID 0x658.
  MB85RC1MT = 6   ///< Expect MB85RC1MT, 128 KiB, Product ID 0x758.
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
/// @return Status indicating success or failure
using I2cWriteFn = Status (*)(uint8_t addr, const uint8_t* data, size_t len,
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
/// @return Status indicating success or failure
using I2cWriteReadFn = Status (*)(uint8_t addr, const uint8_t* txData, size_t txLen,
                                  uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                                  void* user);

/// @brief Optional bus-level operations that do not fit normal 7-bit callbacks.
///
/// These operations remain application-owned. The core requests them only when
/// the active variant documents the feature and the application supplied
/// Config::i2cSpecial.
enum class I2cSpecialOp : uint8_t {
  HIGH_SPEED_WRITE = 0,      ///< HS master code, expected NACK, then a write transaction.
  HIGH_SPEED_WRITE_READ = 1, ///< HS master code, expected NACK, then a write/read transaction.
  ENTER_SLEEP = 2,           ///< F8h + device address word + repeated-start 86h; R/W bit is don't-care.
  WAKE_FROM_SLEEP = 3        ///< Device address wake stimulus; ACK may be indeterminate.
};

/// @brief Parameters for optional special I2C operations.
///
/// For High-speed operations, the callback must consume the expected NACK from
/// the `0000 1XXX` master-code byte internally and return OK only when the
/// complete HS-prefixed transaction succeeds. Generic I2C NACKs must remain
/// failures outside that narrow prefix. For Sleep command address words, the
/// R/W bit is don't-care; for MB85RC1MT, A16 is also don't-care in that command
/// form.
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
/// It must not recursively call public methods on the same MB85RC instance.
using I2cSpecialFn = Status (*)(I2cSpecialOp op, const I2cSpecialTransfer& transfer,
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

/// @brief Configuration for MB85RC driver.
struct Config {
  // === I2C Transport (required) ===
  I2cWriteFn i2cWrite = nullptr;         ///< I2C write function pointer
  I2cWriteReadFn i2cWriteRead = nullptr; ///< I2C write-read function pointer
  I2cSpecialFn i2cSpecial = nullptr;     ///< Optional HS/Sleep special operation function
  void* i2cUser = nullptr;               ///< User context for callbacks

  // === Timing Hooks (optional) ===
  NowMsFn nowMs = nullptr;               ///< Optional monotonic ms source for health and Sleep wake gating
  void* timeUser = nullptr;              ///< User context for timing hook

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
  uint8_t highSpeedMasterCode = cmd::HIGH_SPEED_MASTER_CODE_DEFAULT; ///< Raw `0000 1XXX` HS code
  uint16_t sleepRecoveryUs = cmd::SLEEP_RECOVERY_US; ///< Must be 0 or >= active variant tREC
  /// Expected runtime variant.
  ///
  /// The default is `AUTO` so Device-ID-capable parts select their active
  /// capacity from the device identifier instead of silently assuming 256V.
  /// Production fixed-BOM integrations should set the exact part number so
  /// unexpected substitutions fail early. Select `MB85RC16V` explicitly because
  /// that variant has no Device ID command and cannot be discovered by `AUTO`.
  DeviceVariant expectedVariant = DeviceVariant::AUTO;

  // === Health Tracking ===
  uint8_t offlineThreshold = 5;          ///< Consecutive failures before OFFLINE state
};

}  // namespace MB85RC
