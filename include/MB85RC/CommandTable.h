/// @file CommandTable.h
/// @brief Device constants and address definitions for MB85RC-family FRAM
#pragma once

#include <cstddef>
#include <cstdint>

namespace MB85RC {

/// @brief Runtime device variant selection and decoded identity.
enum class DeviceVariant : uint8_t {
  AUTO = 0,
  MB85RC256V = 1,
  MB85RC64TA = 2,
  MB85RC04V = 3,
  MB85RC16V = 4,
  MB85RC512T = 5,
  MB85RC1MT = 6
};

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

/// Product ID (12-bit): MB85RC64TA
static constexpr uint16_t PRODUCT_ID_MB85RC64TA = 0x358;

/// Product ID (12-bit): MB85RC256V
static constexpr uint16_t PRODUCT_ID_MB85RC256V = 0x510;

/// Product ID (12-bit): MB85RC04V
static constexpr uint16_t PRODUCT_ID_MB85RC04V = 0x010;

/// Product ID (12-bit): MB85RC512T
static constexpr uint16_t PRODUCT_ID_MB85RC512T = 0x658;

/// Product ID (12-bit): MB85RC1MT
static constexpr uint16_t PRODUCT_ID_MB85RC1MT = 0x758;

/// Legacy product ID alias for MB85RC256V.
static constexpr uint16_t PRODUCT_ID = PRODUCT_ID_MB85RC256V;

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
///
/// The driver uses this table to validate Device ID responses, derive active
/// memory capacity, and encode runtime memory addresses. Entries with
/// `hasDeviceId == false` cannot be selected by `DeviceVariant::AUTO`.
struct VariantInfo {
  DeviceVariant variant;          ///< Stable typed identity; never inferred from name text.
  const char* name;              ///< Marketing part number.
  uint32_t memoryBytes;          ///< Total memory capacity in bytes.
  uint16_t productId;            ///< 12-bit Device ID product field when available.
  uint8_t densityCode;           ///< Upper product-ID density nibble when available.
  bool hasDeviceId;              ///< True when the datasheet defines Device ID readback.
  AddressModel addressModel;     ///< Addressing model used by memory transactions.
  bool uses256vAccessFormat;     ///< True when the 256V two-byte linear access format applies.
  bool supportedByDriver;        ///< True when runtime memory operations are implemented/tested.
  bool supportsHighSpeedMode;    ///< True when the variant documents I2C High-speed mode.
  bool supportsSleepMode;        ///< True when the variant documents Sleep mode.
  uint32_t maxNormalBusHz;       ///< Maximum normal-mode I2C bus rate from local datasheets.
  uint32_t maxHighSpeedBusHz;    ///< Maximum HS-mode bus rate, or 0 when unsupported.
  uint16_t sleepRecoveryUs;      ///< Sleep wake recovery time tREC, or 0 when unsupported.
};

/// @brief Known MB85RC-family variants referenced by local datasheets.
///
/// Only entries marked `supportedByDriver` are accepted for runtime memory
/// access. Optional High Speed and Sleep capability flags gate the core APIs,
/// but the reusable driver still does not own the I2C controller clock. HS
/// clock changes, bus locking, and wake-delay policy remain application-owned.
static constexpr VariantInfo KNOWN_VARIANTS[] = {
  {DeviceVariant::MB85RC04V, "MB85RC04V", 512UL, PRODUCT_ID_MB85RC04V, 0x00, true,
   AddressModel::ONE_BYTE_A8_IN_DEVICE_ADDRESS, false, true,
   false, false, 1000000UL, 0UL, 0U},
  {DeviceVariant::MB85RC16V, "MB85RC16V", 2048UL, 0x000, 0x00, false,
   AddressModel::ONE_BYTE_UPPER_BITS_IN_DEVICE_ADDRESS, false, true,
   false, false, 1000000UL, 0UL, 0U},
  {DeviceVariant::MB85RC64TA, "MB85RC64TA", 8192UL, PRODUCT_ID_MB85RC64TA, 0x03, true,
   AddressModel::TWO_BYTE_ADDRESS_PINS, true, true,
   true, true, 1000000UL, 3400000UL, 400U},
  {DeviceVariant::MB85RC256V, "MB85RC256V", 32768UL, PRODUCT_ID, DENSITY_CODE, true,
   AddressModel::TWO_BYTE_ADDRESS_PINS, true, true,
   false, false, 1000000UL, 0UL, 0U},
  {DeviceVariant::MB85RC512T, "MB85RC512T", 65536UL, PRODUCT_ID_MB85RC512T, 0x06, true,
   AddressModel::TWO_BYTE_ADDRESS_PINS, true, true,
   true, true, 1000000UL, 3400000UL, 400U},
  {DeviceVariant::MB85RC1MT, "MB85RC1MT", 131072UL, PRODUCT_ID_MB85RC1MT, 0x07, true,
   AddressModel::TWO_BYTE_A16_IN_DEVICE_ADDRESS, false, true,
   true, true, 1000000UL, 3400000UL, 400U},
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

/// @brief Return the maximum address for a known runtime variant.
/// @param variant Variant metadata
/// @return Highest valid byte address, or 0 when capacity is invalid
inline uint32_t maxAddressForVariant(const VariantInfo& variant) {
  if (variant.memoryBytes == 0UL) {
    return 0U;
  }
  return variant.memoryBytes - 1UL;
}

/// @brief Return the high address byte mask for a two-byte address variant.
/// @param variant Variant metadata
/// @return Mask applied to the high memory-address byte
inline uint8_t addressHighMaskForVariant(const VariantInfo& variant) {
  return static_cast<uint8_t>((maxAddressForVariant(variant) >> 8) & 0xFFU);
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
// Optional High-Speed / Sleep Protocol
// ============================================================================

/// Minimum raw 8-bit High-speed master code (`0000 1XXX`).
static constexpr uint8_t HIGH_SPEED_MASTER_CODE_MIN = 0x08;

/// Maximum raw 8-bit High-speed master code (`0000 1XXX`).
static constexpr uint8_t HIGH_SPEED_MASTER_CODE_MAX = 0x0F;

/// Default raw 8-bit High-speed master code used by Config.
static constexpr uint8_t HIGH_SPEED_MASTER_CODE_DEFAULT = 0x08;

/// Maximum documented High-speed-mode bus rate for capable local variants.
static constexpr uint32_t HIGH_SPEED_BUS_HZ = 3400000UL;

/// Maximum documented normal-mode bus rate for supported local variants.
static constexpr uint32_t NORMAL_BUS_HZ = 1000000UL;

/// Reserved 8-bit Device ID/Sleep command byte used for Sleep entry phase 1.
static constexpr uint8_t SLEEP_RESERVED_ADDR_W = 0xF8;

/// Reserved 8-bit Sleep command byte used after the repeated START.
static constexpr uint8_t SLEEP_ENTRY_COMMAND = 0x86;

/// Documented internal-regulator recovery time after Sleep wake.
static constexpr uint16_t SLEEP_RECOVERY_US = 400U;

/// Conservative millisecond gate for the driver state machine.
static constexpr uint32_t SLEEP_RECOVERY_MS = 1UL;

// ============================================================================
// Memory Layout
// ============================================================================

/// Memory size in bytes for MB85RC64TA (8,192 = 64 Kbit / 8)
static constexpr uint16_t MEMORY_SIZE_MB85RC64TA = 8192;

/// Maximum valid MB85RC64TA memory address (0x0000-0x1FFF)
static constexpr uint16_t MAX_MEM_ADDRESS_MB85RC64TA = 0x1FFF;

/// High address byte mask for MB85RC64TA
static constexpr uint8_t ADDR_HIGH_MASK_MB85RC64TA = 0x1F;

/// Memory size in bytes for MB85RC256V (32,768 = 256 Kbit / 8)
static constexpr uint16_t MEMORY_SIZE_MB85RC256V = 32768;

/// Maximum valid MB85RC256V memory address (15-bit: 0x0000-0x7FFF)
static constexpr uint16_t MAX_MEM_ADDRESS_MB85RC256V = 0x7FFF;

/// Address mask for the MB85RC256V high byte (MSB must be 0)
static constexpr uint8_t ADDR_HIGH_MASK_MB85RC256V = 0x7F;

/// Memory size in bytes for MB85RC04V (512 = 4 Kbit / 8)
static constexpr uint32_t MEMORY_SIZE_MB85RC04V = 512UL;

/// Maximum valid MB85RC04V memory address (0x0000-0x01FF)
static constexpr uint32_t MAX_MEM_ADDRESS_MB85RC04V = 0x01FFUL;

/// Memory size in bytes for MB85RC16V (2,048 = 16 Kbit / 8)
static constexpr uint32_t MEMORY_SIZE_MB85RC16V = 2048UL;

/// Maximum valid MB85RC16V memory address (0x0000-0x07FF)
static constexpr uint32_t MAX_MEM_ADDRESS_MB85RC16V = 0x07FFUL;

/// Memory size in bytes for MB85RC512T (65,536 = 512 Kbit / 8)
static constexpr uint32_t MEMORY_SIZE_MB85RC512T = 65536UL;

/// Maximum valid MB85RC512T memory address (0x0000-0xFFFF)
static constexpr uint32_t MAX_MEM_ADDRESS_MB85RC512T = 0xFFFFUL;

/// Memory size in bytes for MB85RC1MT (131,072 = 1 Mbit / 8)
static constexpr uint32_t MEMORY_SIZE_MB85RC1MT = 131072UL;

/// Maximum valid MB85RC1MT memory address (0x00000-0x1FFFF)
static constexpr uint32_t MAX_MEM_ADDRESS_MB85RC1MT = 0x1FFFFUL;

/// Legacy memory size alias for MB85RC256V (32,768 = 256 Kbit / 8)
static constexpr uint16_t MEMORY_SIZE = 32768;

/// Legacy maximum valid memory address alias for MB85RC256V (0x0000-0x7FFF)
static constexpr uint16_t MAX_MEM_ADDRESS = 0x7FFF;

/// Legacy address mask alias for the MB85RC256V high byte
static constexpr uint8_t ADDR_HIGH_MASK = 0x7F;

/// Number of address bytes sent per transaction
static constexpr uint8_t ADDRESS_BYTES = 2;

// ============================================================================
// I2C Transaction Limits
// ============================================================================

/// Maximum memory-data bytes in one write transaction. Address bytes are extra.
static constexpr size_t MAX_WRITE_DATA_BYTES = 126;

/// Legacy alias. This value has always been used as memory-data bytes.
static constexpr size_t MAX_WRITE_CHUNK = MAX_WRITE_DATA_BYTES;

/// Maximum bytes per single read transaction.
static constexpr size_t MAX_READ_CHUNK = 128;

/// Maximum bytes per single fill transaction.
static constexpr size_t MAX_FILL_CHUNK = 64;

/// Maximum staged transfer chunks executed by one pollTransfer() call.
static constexpr uint8_t MAX_TRANSFER_INSTRUCTIONS_PER_POLL = 8;

}  // namespace cmd
}  // namespace MB85RC
