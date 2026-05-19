/// @file Config.h
/// @brief Configuration structure for MB85RC driver
#pragma once

#include <cstddef>
#include <cstdint>
#include "MB85RC/Status.h"

namespace MB85RC {

/// @brief Runtime device variant selection for Device ID validation and memory bounds.
enum class DeviceVariant : uint8_t {
  AUTO = 0,       ///< Select a supported runtime variant from Device ID.
  MB85RC256V,     ///< Expect MB85RC256V, 32 KiB, Product ID 0x510.
  MB85RC64TA,     ///< Expect MB85RC64TA, 8 KiB, Product ID 0x358.
  MB85RC04V,      ///< Expect MB85RC04V, 512 bytes, Product ID 0x010.
  MB85RC16V,      ///< Expect MB85RC16V, 2 KiB, no Device ID command.
  MB85RC512T,     ///< Expect MB85RC512T, 64 KiB, Product ID 0x658.
  MB85RC1MT       ///< Expect MB85RC1MT, 128 KiB, Product ID 0x758.
};

/// I2C write callback signature
/// @param addr I2C device address (7-bit)
/// @param data Pointer to data to write
/// @param len Number of bytes to write
/// @param timeoutMs Maximum time to wait for completion
/// @param user User context pointer passed through from Config
/// @return Status indicating success or failure
using I2cWriteFn = Status (*)(uint8_t addr, const uint8_t* data, size_t len,
                              uint32_t timeoutMs, void* user);

/// I2C write-then-read callback signature
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

/// Millisecond timestamp callback.
/// @param user User context pointer passed through from Config
/// @return Current monotonic milliseconds
using NowMsFn = uint32_t (*)(void* user);

/// @brief Configuration for MB85RC driver.
struct Config {
  // === I2C Transport (required) ===
  I2cWriteFn i2cWrite = nullptr;         ///< I2C write function pointer
  I2cWriteReadFn i2cWriteRead = nullptr; ///< I2C write-read function pointer
  void* i2cUser = nullptr;               ///< User context for callbacks

  // === Timing Hooks (optional) ===
  NowMsFn nowMs = nullptr;               ///< Optional monotonic millisecond source for health timestamps
  void* timeUser = nullptr;              ///< User context for timing hook

  // === Device Settings ===
  uint8_t i2cAddress = 0x50;             ///< 0x50-0x57 depending on A2:A1:A0 pins
  uint32_t i2cTimeoutMs = 50;            ///< I2C transaction timeout in ms
  /// Expected runtime variant. The default preserves legacy 256V behavior.
  /// Use AUTO for Device-ID-capable variants; select MB85RC16V explicitly.
  DeviceVariant expectedVariant = DeviceVariant::MB85RC256V;

  // === Health Tracking ===
  uint8_t offlineThreshold = 5;          ///< Consecutive failures before OFFLINE state
};

}  // namespace MB85RC
