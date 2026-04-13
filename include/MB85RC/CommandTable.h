/// @file CommandTable.h
/// @brief Device constants and address definitions for MB85RC256V FRAM
#pragma once

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
