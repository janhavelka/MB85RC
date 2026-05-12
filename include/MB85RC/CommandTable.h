/// @file CommandTable.h
/// @brief Device constants and address definitions for MB85RC256V FRAM
#pragma once

#include <cstddef>
#include <cstdint>

namespace MB85RC {
namespace cmd {

// ============================================================================
// Device Identity
// ============================================================================

/// Default I2C base address (A2=A1=A0=GND)
static constexpr uint8_t DEFAULT_ADDRESS = 0x50;

/// Minimum valid 7-bit I2C address (all address pins low)
static constexpr uint8_t MIN_ADDRESS = 0x50;

/// Maximum valid 7-bit I2C address (all address pins high)
static constexpr uint8_t MAX_ADDRESS = 0x57;

/// Device type code in the I2C address word (upper 4 bits = 1010)
static constexpr uint8_t DEVICE_TYPE_CODE = 0xA0;

/// Manufacturer ID (12-bit): Fujitsu / RAMXEED
static constexpr uint16_t MANUFACTURER_ID = 0x00A;

/// Product ID (12-bit): MB85RC256V
static constexpr uint16_t PRODUCT_ID = 0x510;

/// Density code (upper nibble of Product ID)
static constexpr uint8_t DENSITY_CODE = 0x05;

/// @brief FRAM memory-address encoding used by a device variant.
enum class AddressModel : uint8_t {
  TWO_BYTE_ADDRESS_PINS,              ///< Two address bytes; A2:A0 select the device.
  TWO_BYTE_A16_IN_DEVICE_ADDRESS,     ///< Two address bytes; A16 is encoded in the I2C address.
  ONE_BYTE_UPPER_BITS_IN_DEVICE_ADDRESS, ///< One address byte; upper address bits are in the I2C address.
  ONE_BYTE_A8_IN_DEVICE_ADDRESS       ///< One address byte; A8 is encoded in the I2C address.
};

/// @brief Static metadata for a known MB85RC family variant.
struct VariantInfo {
  const char* name;              ///< Marketing part number.
  uint32_t memoryBytes;          ///< Total memory capacity in bytes.
  uint16_t productId;            ///< 12-bit Device ID product field when available.
  uint8_t densityCode;           ///< Upper product-ID density nibble when available.
  bool hasDeviceId;              ///< True when the datasheet defines Device ID readback.
  AddressModel addressModel;     ///< Addressing model used by memory transactions.
  bool uses256vAccessFormat;     ///< True when the 256V two-byte linear access format applies.
  bool supportedByDriver;        ///< True only for the MB85RC256V runtime driver.
  bool highSpeedMode;            ///< True when the variant documents I2C high-speed mode.
  bool sleepMode;                ///< True when the variant documents sleep mode.
};

/// @brief Known MB85RC-family variants referenced by local datasheets.
static constexpr VariantInfo KNOWN_VARIANTS[] = {
  {"MB85RC04V", 512UL, 0x010, 0x00, true,
   AddressModel::ONE_BYTE_A8_IN_DEVICE_ADDRESS, false, false, false, false},
  {"MB85RC16V", 2048UL, 0x000, 0x00, false,
   AddressModel::ONE_BYTE_UPPER_BITS_IN_DEVICE_ADDRESS, false, false, false, false},
  {"MB85RC64TA", 8192UL, 0x358, 0x03, true,
   AddressModel::TWO_BYTE_ADDRESS_PINS, true, false, true, true},
  {"MB85RC256V", 32768UL, PRODUCT_ID, DENSITY_CODE, true,
   AddressModel::TWO_BYTE_ADDRESS_PINS, true, true, false, false},
  {"MB85RC512T", 65536UL, 0x658, 0x06, true,
   AddressModel::TWO_BYTE_ADDRESS_PINS, true, false, true, true},
  {"MB85RC1MT", 131072UL, 0x758, 0x07, true,
   AddressModel::TWO_BYTE_A16_IN_DEVICE_ADDRESS, false, false, true, true},
};

/// @brief Number of entries in KNOWN_VARIANTS.
static constexpr size_t VARIANT_COUNT = sizeof(KNOWN_VARIANTS) / sizeof(KNOWN_VARIANTS[0]);

/// @brief Find a known variant by 12-bit Product ID.
/// @param productId Product ID field decoded from Device ID bytes.
/// @return Pointer to variant metadata, or nullptr when unknown/unavailable.
inline const VariantInfo* findVariantByProductId(uint16_t productId) {
  for (size_t i = 0; i < VARIANT_COUNT; ++i) {
    if (KNOWN_VARIANTS[i].hasDeviceId && KNOWN_VARIANTS[i].productId == productId) {
      return &KNOWN_VARIANTS[i];
    }
  }
  return nullptr;
}

// ============================================================================
// Device ID Read Protocol
// ============================================================================

/// Reserved Slave ID for Device ID write phase
static constexpr uint8_t DEVICE_ID_ADDR_W = 0xF8;

/// Reserved Slave ID for Device ID read phase
static constexpr uint8_t DEVICE_ID_ADDR_R = 0xF9;

/// Number of Device ID bytes returned
static constexpr uint8_t DEVICE_ID_LEN = 3;

// ============================================================================
// Memory Layout
// ============================================================================

/// Memory size in bytes (32,768 = 256 Kbit / 8)
static constexpr uint16_t MEMORY_SIZE = 32768;

/// Maximum valid memory address (15-bit: 0x0000-0x7FFF)
static constexpr uint16_t MAX_MEM_ADDRESS = 0x7FFF;

/// Address mask for the high byte (MSB must be 0)
static constexpr uint8_t ADDR_HIGH_MASK = 0x7F;

/// Number of address bytes sent per transaction
static constexpr uint8_t ADDRESS_BYTES = 2;

// ============================================================================
// I2C Transaction Limits
// ============================================================================

/// Maximum bytes per single write transaction (address + data).
/// Conservative limit to stay within typical I2C controller buffers.
static constexpr size_t MAX_WRITE_CHUNK = 126;

/// Maximum bytes per single read transaction.
static constexpr size_t MAX_READ_CHUNK = 128;

}  // namespace cmd
}  // namespace MB85RC
