/// @file test_basic.cpp
/// @brief Native contract tests for MB85RC lifecycle and health behavior.

#include <unity.h>

#include <limits>
#include <type_traits>

#include "Arduino.h"
#include "Wire.h"

SerialClass Serial;
TwoWire Wire;

#include "MB85RC/MB85RC.h"
#include "common/I2cTransport.h"
#include "common/TypedMemory.h"

using namespace MB85RC;

static_assert(!std::is_copy_constructible_v<::MB85RC::MB85RC>,
              "MB85RC driver instances must not be copy constructible");
static_assert(!std::is_copy_assignable_v<::MB85RC::MB85RC>,
              "MB85RC driver instances must not be copy assignable");
static_assert(!std::is_move_constructible_v<::MB85RC::MB85RC>,
              "MB85RC driver instances must not be move constructible");
static_assert(!std::is_move_assignable_v<::MB85RC::MB85RC>,
              "MB85RC driver instances must not be move assignable");

namespace {

struct FakeBus {
  uint32_t nowMs = 1000;
  uint32_t writeCalls = 0;
  uint32_t readCalls = 0;
  uint32_t specialCalls = 0;
  uint32_t hsWriteCalls = 0;
  uint32_t hsWriteReadCalls = 0;
  uint32_t sleepEntryCalls = 0;
  uint32_t wakeCalls = 0;

  int readErrorRemaining = 0;
  int writeErrorRemaining = 0;
  int specialErrorRemaining = 0;
  uint32_t writeErrorOnCall = 0;
  uint32_t writeErrorAfterApplyOnCall = 0;
  uint32_t specialErrorOnCall = 0;
  Status readError = Status::Error(Err::I2C_ERROR, "forced read error", -1);
  Status writeError = Status::Error(Err::I2C_ERROR, "forced write error", -2);
  Status specialError = Status::Error(Err::I2C_ERROR, "forced special error", -4);
  bool badDeviceId = false;
  bool deviceIdSupported = true;
  bool writeProtectHigh = false;
  bool sleeping = false;
  uint16_t productId = cmd::PRODUCT_ID_MB85RC256V;
  uint32_t memoryBytes = cmd::MEMORY_SIZE_MB85RC256V;
  cmd::AddressModel addressModel = cmd::AddressModel::TWO_BYTE_ADDRESS_PINS;
  I2cSpecialOp lastSpecialOp = I2cSpecialOp::HIGH_SPEED_WRITE;
  uint8_t lastHsMasterCode = cmd::HIGH_SPEED_MASTER_CODE_DEFAULT;
  uint16_t lastRecoveryUs = 0;
  uint8_t lastI2cAddress = cmd::DEFAULT_ADDRESS;
  size_t lastAddressLen = 0;
  uint8_t lastAddrHigh = 0;
  uint8_t lastAddrLow = 0;
  uint32_t lastMemoryAddress = 0;

  // Simulated memory: large enough for the biggest runtime-supported variant.
  uint8_t mem[cmd::MEMORY_SIZE_MB85RC1MT] = {};
  uint32_t currentAddr = 0;
  bool currentAddrValid = false;

  uint32_t maxAddress() const {
    return memoryBytes - 1UL;
  }

  uint8_t highMask() const {
    return static_cast<uint8_t>((maxAddress() >> 8) & 0xFFU);
  }
};

/// Device ID raw bytes for MB85RC256V: Manufacturer 0x00A, Product 0x510
/// Byte 0: 0x00, Byte 1: 0xA5, Byte 2: 0x10
static constexpr uint8_t DEVID_BYTE0 = 0x00;
static constexpr uint8_t DEVID_BYTE1 = 0xA5;
static constexpr uint8_t DEVID_BYTE2 = 0x10;

void encodeDeviceId(uint16_t productId, uint8_t out[cmd::DEVICE_ID_LEN]) {
  out[0] = static_cast<uint8_t>((cmd::MANUFACTURER_ID >> 4) & 0xFFU);
  out[1] = static_cast<uint8_t>(((cmd::MANUFACTURER_ID & 0x0FU) << 4) |
                                ((productId >> 8) & 0x0FU));
  out[2] = static_cast<uint8_t>(productId & 0xFFU);
}

uint8_t memoryAddressLen(cmd::AddressModel model) {
  switch (model) {
    case cmd::AddressModel::ONE_BYTE_UPPER_BITS_IN_DEVICE_ADDRESS:
    case cmd::AddressModel::ONE_BYTE_A8_IN_DEVICE_ADDRESS:
      return 1U;
    case cmd::AddressModel::TWO_BYTE_ADDRESS_PINS:
    case cmd::AddressModel::TWO_BYTE_A16_IN_DEVICE_ADDRESS:
    default:
      return 2U;
  }
}

uint32_t decodeMemoryAddress(FakeBus* bus, uint8_t addr, const uint8_t* data) {
  switch (bus->addressModel) {
    case cmd::AddressModel::ONE_BYTE_A8_IN_DEVICE_ADDRESS:
      return static_cast<uint32_t>(((addr & 0x01U) << 8) | data[0]);
    case cmd::AddressModel::ONE_BYTE_UPPER_BITS_IN_DEVICE_ADDRESS:
      return static_cast<uint32_t>(((addr & 0x07U) << 8) | data[0]);
    case cmd::AddressModel::TWO_BYTE_A16_IN_DEVICE_ADDRESS:
      return static_cast<uint32_t>(((addr & 0x01UL) << 16) |
                                   (static_cast<uint32_t>(data[0]) << 8) |
                                   data[1]);
    case cmd::AddressModel::TWO_BYTE_ADDRESS_PINS:
    default:
      return static_cast<uint32_t>(((data[0] & bus->highMask()) << 8) | data[1]);
  }
}

void recordMemoryAddress(FakeBus* bus, uint8_t addr, const uint8_t* data, size_t addrLen) {
  bus->lastI2cAddress = addr;
  bus->lastAddressLen = addrLen;
  bus->lastAddrHigh = (addrLen >= 2U) ? data[0] : 0U;
  bus->lastAddrLow = (addrLen >= 2U) ? data[1] : data[0];
  bus->lastMemoryAddress = decodeMemoryAddress(bus, addr, data);
}

Status fakeWrite(uint8_t addr, const uint8_t* data, size_t len, uint32_t, void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->writeCalls++;
  if (data == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "invalid fake write args");
  }
  if (bus->writeErrorOnCall != 0U && bus->writeCalls == bus->writeErrorOnCall) {
    return bus->writeError;
  }
  if (bus->writeErrorRemaining > 0) {
    bus->writeErrorRemaining--;
    return bus->writeError;
  }

  // If writing to device address (0x50-0x57), it's a memory write.
  const size_t addrLen = memoryAddressLen(bus->addressModel);
  if (addr >= cmd::MIN_ADDRESS && addr <= cmd::MAX_ADDRESS && len > addrLen) {
    recordMemoryAddress(bus, addr, data, addrLen);
    uint32_t memAddr = bus->lastMemoryAddress;
    for (size_t i = addrLen; i < len; ++i) {
      if (!bus->writeProtectHigh) {
        bus->mem[memAddr % bus->memoryBytes] = data[i];
      }
      memAddr++;
    }
    bus->currentAddr = memAddr % bus->memoryBytes;
    bus->currentAddrValid = true;
  }

  if (bus->writeErrorAfterApplyOnCall != 0U &&
      bus->writeCalls == bus->writeErrorAfterApplyOnCall) {
    return bus->writeError;
  }

  return Status::Ok();
}

Status fakeWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen, uint8_t* rxData,
                     size_t rxLen, uint32_t, void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->readCalls++;
  if ((txLen > 0 && txData == nullptr) || (rxLen > 0 && rxData == nullptr) ||
      (txLen == 0 && rxLen == 0)) {
    return Status::Error(Err::INVALID_PARAM, "invalid fake write-read args");
  }
  if (bus->readErrorRemaining > 0) {
    bus->readErrorRemaining--;
    return bus->readError;
  }

  // Device ID read: addr is 0x7C (0xF8 >> 1)
  if (addr == (cmd::DEVICE_ID_ADDR_W >> 1) && rxLen == cmd::DEVICE_ID_LEN) {
    if (!bus->deviceIdSupported) {
      return Status::Error(Err::I2C_NACK_ADDR, "fake device id unsupported", -3);
    }
    uint8_t id[cmd::DEVICE_ID_LEN] = {};
    encodeDeviceId(bus->productId, id);
    rxData[0] = bus->badDeviceId ? 0xFF : id[0];
    rxData[1] = bus->badDeviceId ? 0xFF : id[1];
    rxData[2] = bus->badDeviceId ? 0xFF : id[2];
    return Status::Ok();
  }

  // Memory read: addr is device address (0x50-0x57)
  const size_t addrLen = memoryAddressLen(bus->addressModel);
  if (addr >= cmd::MIN_ADDRESS && addr <= cmd::MAX_ADDRESS && txLen == addrLen) {
    recordMemoryAddress(bus, addr, txData, addrLen);
    uint32_t memAddr = bus->lastMemoryAddress;
    for (size_t i = 0; i < rxLen; ++i) {
      rxData[i] = bus->mem[memAddr % bus->memoryBytes];
      memAddr++;
    }
    bus->currentAddr = memAddr % bus->memoryBytes;
    bus->currentAddrValid = true;
    return Status::Ok();
  }

  // Current Address Read: direct read with no address phase
  if (addr >= cmd::MIN_ADDRESS && addr <= cmd::MAX_ADDRESS && txLen == 0 && rxLen > 0) {
    bus->lastI2cAddress = addr;
    bus->lastAddressLen = 0;
    bus->lastAddrHigh = 0;
    bus->lastAddrLow = 0;
    bus->lastMemoryAddress = bus->currentAddr;
    for (size_t i = 0; i < rxLen; ++i) {
      rxData[i] = bus->mem[bus->currentAddr % bus->memoryBytes];
      bus->currentAddr = (bus->currentAddr + 1) % bus->memoryBytes;
    }
    bus->currentAddrValid = true;
    return Status::Ok();
  }

  // Default: zero fill
  for (size_t i = 0; i < rxLen; ++i) {
    rxData[i] = 0;
  }
  return Status::Ok();
}

Status fakeSpecial(I2cSpecialOp op, const I2cSpecialTransfer& transfer,
                   uint32_t, void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->specialCalls++;
  bus->lastSpecialOp = op;
  bus->lastI2cAddress = transfer.i2cAddress;
  bus->lastHsMasterCode = transfer.hsMasterCode;
  bus->lastRecoveryUs = transfer.recoveryUs;

  if (bus->specialErrorOnCall != 0U && bus->specialCalls == bus->specialErrorOnCall) {
    return bus->specialError;
  }
  if (bus->specialErrorRemaining > 0) {
    bus->specialErrorRemaining--;
    return bus->specialError;
  }

  switch (op) {
    case I2cSpecialOp::HIGH_SPEED_WRITE: {
      bus->hsWriteCalls++;
      if (transfer.hsMasterCode < cmd::HIGH_SPEED_MASTER_CODE_MIN ||
          transfer.hsMasterCode > cmd::HIGH_SPEED_MASTER_CODE_MAX ||
          transfer.txData == nullptr || transfer.txLen == 0U) {
        return Status::Error(Err::INVALID_PARAM, "invalid fake HS write");
      }
      const size_t addrLen = memoryAddressLen(bus->addressModel);
      if (transfer.i2cAddress >= cmd::MIN_ADDRESS &&
          transfer.i2cAddress <= cmd::MAX_ADDRESS &&
          transfer.txLen > addrLen) {
        recordMemoryAddress(bus, transfer.i2cAddress, transfer.txData, addrLen);
        uint32_t memAddr = bus->lastMemoryAddress;
        for (size_t i = addrLen; i < transfer.txLen; ++i) {
          if (!bus->writeProtectHigh) {
            bus->mem[memAddr % bus->memoryBytes] = transfer.txData[i];
          }
          memAddr++;
        }
        bus->currentAddr = memAddr % bus->memoryBytes;
        bus->currentAddrValid = true;
      }
      return Status::Ok();
    }

    case I2cSpecialOp::HIGH_SPEED_WRITE_READ: {
      bus->hsWriteReadCalls++;
      if (transfer.hsMasterCode < cmd::HIGH_SPEED_MASTER_CODE_MIN ||
          transfer.hsMasterCode > cmd::HIGH_SPEED_MASTER_CODE_MAX ||
          (transfer.txLen > 0U && transfer.txData == nullptr) ||
          (transfer.rxLen > 0U && transfer.rxData == nullptr) ||
          (transfer.txLen == 0U && transfer.rxLen == 0U)) {
        return Status::Error(Err::INVALID_PARAM, "invalid fake HS write-read");
      }
      const size_t addrLen = memoryAddressLen(bus->addressModel);
      if (transfer.i2cAddress >= cmd::MIN_ADDRESS &&
          transfer.i2cAddress <= cmd::MAX_ADDRESS &&
          transfer.txLen == addrLen) {
        recordMemoryAddress(bus, transfer.i2cAddress, transfer.txData, addrLen);
        uint32_t memAddr = bus->lastMemoryAddress;
        for (size_t i = 0; i < transfer.rxLen; ++i) {
          transfer.rxData[i] = bus->mem[memAddr % bus->memoryBytes];
          memAddr++;
        }
        bus->currentAddr = memAddr % bus->memoryBytes;
        bus->currentAddrValid = true;
        return Status::Ok();
      }
      if (transfer.i2cAddress >= cmd::MIN_ADDRESS &&
          transfer.i2cAddress <= cmd::MAX_ADDRESS &&
          transfer.txLen == 0U && transfer.rxLen > 0U) {
        bus->lastAddressLen = 0;
        bus->lastAddrHigh = 0;
        bus->lastAddrLow = 0;
        bus->lastMemoryAddress = bus->currentAddr;
        for (size_t i = 0; i < transfer.rxLen; ++i) {
          transfer.rxData[i] = bus->mem[bus->currentAddr % bus->memoryBytes];
          bus->currentAddr = (bus->currentAddr + 1) % bus->memoryBytes;
        }
        bus->currentAddrValid = true;
        return Status::Ok();
      }
      return Status::Error(Err::I2C_BUS, "unexpected fake HS write-read");
    }

    case I2cSpecialOp::ENTER_SLEEP:
      bus->sleepEntryCalls++;
      if (transfer.i2cAddress < cmd::MIN_ADDRESS || transfer.i2cAddress > cmd::MAX_ADDRESS) {
        return Status::Error(Err::INVALID_PARAM, "invalid fake sleep address");
      }
      bus->sleeping = true;
      bus->currentAddrValid = false;
      return Status::Ok();

    case I2cSpecialOp::WAKE_FROM_SLEEP:
      bus->wakeCalls++;
      if (transfer.i2cAddress < cmd::MIN_ADDRESS || transfer.i2cAddress > cmd::MAX_ADDRESS) {
        return Status::Error(Err::INVALID_PARAM, "invalid fake wake address");
      }
      bus->sleeping = false;
      return Status::Ok();

    default:
      return Status::Error(Err::INVALID_PARAM, "unknown fake special op");
  }
}

uint32_t fakeNowMs(void* user) {
  return static_cast<FakeBus*>(user)->nowMs;
}

Config makeConfig(FakeBus& bus) {
  Config cfg;
  cfg.i2cWrite = fakeWrite;
  cfg.i2cWriteRead = fakeWriteRead;
  cfg.i2cSpecial = fakeSpecial;
  cfg.i2cUser = &bus;
  cfg.nowMs = fakeNowMs;
  cfg.timeUser = &bus;
  cfg.i2cTimeoutMs = 10;
  cfg.offlineThreshold = 3;
  return cfg;
}

const cmd::VariantInfo* variantInfoByExpected(DeviceVariant variant) {
  switch (variant) {
    case DeviceVariant::MB85RC04V:
      return cmd::findVariantByProductId(cmd::PRODUCT_ID_MB85RC04V);
    case DeviceVariant::MB85RC64TA:
      return cmd::findVariantByProductId(cmd::PRODUCT_ID_MB85RC64TA);
    case DeviceVariant::MB85RC256V:
      return cmd::findVariantByProductId(cmd::PRODUCT_ID_MB85RC256V);
    case DeviceVariant::MB85RC512T:
      return cmd::findVariantByProductId(cmd::PRODUCT_ID_MB85RC512T);
    case DeviceVariant::MB85RC1MT:
      return cmd::findVariantByProductId(cmd::PRODUCT_ID_MB85RC1MT);
    case DeviceVariant::MB85RC16V:
      for (size_t i = 0; i < cmd::VARIANT_COUNT; ++i) {
        if (cmd::KNOWN_VARIANTS[i].memoryBytes == cmd::MEMORY_SIZE_MB85RC16V) {
          return &cmd::KNOWN_VARIANTS[i];
        }
      }
      return nullptr;
    case DeviceVariant::AUTO:
    default:
      return nullptr;
  }
}

Config makeVariantConfig(FakeBus& bus, DeviceVariant variant) {
  const cmd::VariantInfo* info = variantInfoByExpected(variant);
  TEST_ASSERT_NOT_NULL(info);
  bus.productId = info->productId;
  bus.memoryBytes = info->memoryBytes;
  bus.deviceIdSupported = info->hasDeviceId;
  bus.addressModel = info->addressModel;
  Config cfg = makeConfig(bus);
  cfg.expectedVariant = variant;
  return cfg;
}

Config make64TaConfig(FakeBus& bus) {
  return makeVariantConfig(bus, DeviceVariant::MB85RC64TA);
}

uint32_t busTraffic(const FakeBus& bus) {
  return bus.writeCalls + bus.readCalls + bus.specialCalls;
}

uint32_t nextRandom(uint32_t& state) {
  if (state == 0U) {
    state = 0xA341316CU;
  }
  state ^= (state << 13);
  state ^= (state >> 17);
  state ^= (state << 5);
  return state;
}

void assertWriteVerifyFailsWhenWriteProtectHigh(MB85RC::MB85RC& dev, FakeBus& bus) {
  static constexpr uint32_t ADDR = 0x0220;
  const uint8_t original[3] = {0x14, 0x25, 0x36};
  const uint8_t attempted[3] = {0xA1, 0xB2, 0xC3};
  bus.mem[ADDR] = original[0];
  bus.mem[ADDR + 1] = original[1];
  bus.mem[ADDR + 2] = original[2];
  bus.writeProtectHigh = true;

  const uint32_t writesBefore = bus.writeCalls;
  const uint32_t readsBefore = bus.readCalls;
  VerifyDetailedResult verify;
  Status st = dev.writeVerify(ADDR, attempted, sizeof(attempted), &verify);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::VERIFY_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_GREATER_THAN_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_GREATER_THAN_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(original, &bus.mem[ADDR], sizeof(original));
  TEST_ASSERT_EQUAL_HEX32(ADDR, verify.address);
  TEST_ASSERT_EQUAL_UINT32(sizeof(attempted), static_cast<uint32_t>(verify.bytesRequested));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(verify.bytesVerified));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(verify.firstMismatchOffset));
  TEST_ASSERT_EQUAL_HEX8(attempted[0], verify.expected);
  TEST_ASSERT_EQUAL_HEX8(original[0], verify.actual);
  TEST_ASSERT_FALSE(verify.match);
}

void assertWriteVerifySucceedsWhenWriteProtectDisabled(MB85RC::MB85RC& dev, FakeBus& bus) {
  static constexpr uint32_t ADDR = 0x0230;
  const uint8_t attempted[4] = {0x4A, 0x5B, 0x6C, 0x7D};
  bus.writeProtectHigh = false;

  VerifyDetailedResult verify;
  Status st = dev.writeVerify(ADDR, attempted, sizeof(attempted), &verify);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(attempted, &bus.mem[ADDR], sizeof(attempted));
  TEST_ASSERT_EQUAL_HEX32(ADDR, verify.address);
  TEST_ASSERT_EQUAL_UINT32(sizeof(attempted), static_cast<uint32_t>(verify.bytesRequested));
  TEST_ASSERT_EQUAL_UINT32(sizeof(attempted), static_cast<uint32_t>(verify.bytesVerified));
  TEST_ASSERT_TRUE(verify.match);

  VerifyResult result;
  st = dev.verify(ADDR, attempted, sizeof(attempted), result);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(result.match);
}

void setMemoryRange(FakeBus& bus, uint32_t address, size_t len, uint8_t value) {
  for (size_t i = 0; i < len; ++i) {
    bus.mem[(address + static_cast<uint32_t>(i)) % bus.memoryBytes] = value;
  }
}

void assertMemoryRangeEquals(const FakeBus& bus, uint32_t address, size_t len, uint8_t value) {
  for (size_t i = 0; i < len; ++i) {
    TEST_ASSERT_EQUAL_HEX8(value,
                           bus.mem[(address + static_cast<uint32_t>(i)) % bus.memoryBytes]);
  }
}

void assertMemoryMatches(const FakeBus& bus, uint32_t address, const uint8_t* expected,
                         size_t len) {
  for (size_t i = 0; i < len; ++i) {
    TEST_ASSERT_EQUAL_HEX8(expected[i],
                           bus.mem[(address + static_cast<uint32_t>(i)) % bus.memoryBytes]);
  }
}

static constexpr uint8_t DETAILED_SENTINEL = 0xEE;
static constexpr size_t DETAILED_FILL_CHUNK = 64;

void seedDetailedWindow(FakeBus& bus, uint32_t address, size_t len) {
  setMemoryRange(bus, address - 1U, len + 2U, DETAILED_SENTINEL);
}

void assertDetailedGuardsUntouched(const FakeBus& bus, uint32_t address, size_t len) {
  assertMemoryRangeEquals(bus, address - 1U, 1U, DETAILED_SENTINEL);
  assertMemoryRangeEquals(bus, address + static_cast<uint32_t>(len), 1U, DETAILED_SENTINEL);
}

void assertDetailedPatternPrefixAndSentinelSuffix(const FakeBus& bus, uint32_t address,
                                                  const uint8_t* expected, size_t acceptedLen,
                                                  size_t requestedLen) {
  assertMemoryMatches(bus, address, expected, acceptedLen);
  assertMemoryRangeEquals(bus, address + static_cast<uint32_t>(acceptedLen),
                          requestedLen - acceptedLen, DETAILED_SENTINEL);
  assertDetailedGuardsUntouched(bus, address, requestedLen);
}

void assertDetailedFillPrefixAndSentinelSuffix(const FakeBus& bus, uint32_t address,
                                               uint8_t value, size_t acceptedLen,
                                               size_t requestedLen) {
  assertMemoryRangeEquals(bus, address, acceptedLen, value);
  assertMemoryRangeEquals(bus, address + static_cast<uint32_t>(acceptedLen),
                          requestedLen - acceptedLen, DETAILED_SENTINEL);
  assertDetailedGuardsUntouched(bus, address, requestedLen);
}

void assertWriteResultSuccess(const WriteResult& result, uint32_t address, size_t requestedLen) {
  TEST_ASSERT_TRUE(result.status.ok());
  TEST_ASSERT_EQUAL_HEX32(address, result.address);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(requestedLen),
                           static_cast<uint32_t>(result.bytesRequested));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(requestedLen),
                           static_cast<uint32_t>(result.bytesAccepted));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(requestedLen),
                           static_cast<uint32_t>(result.failedChunkOffset));
  TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(result.failedChunkLength));
  TEST_ASSERT_TRUE(result.complete);
}

void assertWriteResultFailure(const WriteResult& result, Err expectedCode, uint32_t address,
                              size_t requestedLen, size_t acceptedLen, size_t failedChunkLen) {
  TEST_ASSERT_FALSE(result.status.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expectedCode),
                          static_cast<uint8_t>(result.status.code));
  TEST_ASSERT_EQUAL_HEX32(address, result.address);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(requestedLen),
                           static_cast<uint32_t>(result.bytesRequested));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(acceptedLen),
                           static_cast<uint32_t>(result.bytesAccepted));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(acceptedLen),
                           static_cast<uint32_t>(result.failedChunkOffset));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(failedChunkLen),
                           static_cast<uint32_t>(result.failedChunkLength));
  TEST_ASSERT_FALSE(result.complete);
}

void assertWriteResultPreflightFailure(const WriteResult& result, Err expectedCode,
                                       uint32_t address, size_t requestedLen) {
  TEST_ASSERT_FALSE(result.status.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expectedCode),
                          static_cast<uint8_t>(result.status.code));
  TEST_ASSERT_EQUAL_HEX32(address, result.address);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(requestedLen),
                           static_cast<uint32_t>(result.bytesRequested));
  TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(result.bytesAccepted));
  TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(result.failedChunkOffset));
  TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(result.failedChunkLength));
  TEST_ASSERT_FALSE(result.complete);
}

void assertTransferInProgress(const Status& status) {
  TEST_ASSERT_TRUE(status.inProgress());
  TEST_ASSERT_TRUE(status.is(Err::IN_PROGRESS));
}

}  // namespace

void setUp() {
  setMillis(0);
  Wire._clearEndTransmissionResult();
  Wire._clearRequestFromOverride();
}

void tearDown() {}

// ===========================================================================
// Status tests
// ===========================================================================

void test_status_ok() {
  Status st = Status::Ok();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(st);
  TEST_ASSERT_TRUE(st.is(Err::OK));
  TEST_ASSERT_FALSE(st.is(Err::I2C_ERROR));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK), static_cast<uint8_t>(st.code));
}

void test_status_error() {
  Status st = Status::Error(Err::I2C_ERROR, "Test error", 42);
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_FALSE(st);
  TEST_ASSERT_TRUE(st.is(Err::I2C_ERROR));
  TEST_ASSERT_FALSE(st.is(Err::OK));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(42, st.detail);
}

void test_status_in_progress() {
  Status st = Status::Error(Err::IN_PROGRESS, "queued");
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_TRUE(st.inProgress());
}

// ===========================================================================
// Config tests
// ===========================================================================

void test_config_defaults() {
  Config cfg;
  TEST_ASSERT_NULL(cfg.i2cWrite);
  TEST_ASSERT_NULL(cfg.i2cWriteRead);
  TEST_ASSERT_NULL(cfg.i2cSpecial);
  TEST_ASSERT_EQUAL_HEX8(0x50, cfg.i2cAddress);
  TEST_ASSERT_EQUAL_UINT16(50, cfg.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_HEX8(cmd::HIGH_SPEED_MASTER_CODE_DEFAULT, cfg.highSpeedMasterCode);
  TEST_ASSERT_EQUAL_UINT16(cmd::SLEEP_RECOVERY_US, cfg.sleepRecoveryUs);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceVariant::AUTO),
                          static_cast<uint8_t>(cfg.expectedVariant));
  TEST_ASSERT_EQUAL_UINT8(5, cfg.offlineThreshold);
}

void test_get_settings_before_begin_reports_defaults() {
  MB85RC::MB85RC dev;
  SettingsSnapshot settings;
  Status st = dev.getSettings(settings);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(settings.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(settings.state));
  TEST_ASSERT_FALSE(settings.online);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_ADDRESS, settings.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(50u, settings.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5u, settings.offlineThreshold);
  TEST_ASSERT_EQUAL_UINT32(0u, settings.lastOkMs);
  TEST_ASSERT_EQUAL_UINT32(0u, settings.lastErrorMs);
  TEST_ASSERT_TRUE(settings.lastError.ok());
  TEST_ASSERT_EQUAL_UINT8(0u, settings.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(0u, settings.totalFailures);
  TEST_ASSERT_EQUAL_UINT32(0u, settings.totalSuccess);
  TEST_ASSERT_FALSE(settings.hasNowMsHook);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceVariant::AUTO),
                          static_cast<uint8_t>(settings.expectedVariant));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceVariant::AUTO),
                          static_cast<uint8_t>(settings.activeVariant));
  TEST_ASSERT_FALSE(settings.variantKnown);
  TEST_ASSERT_EQUAL_STRING("unknown", settings.variantName);
  TEST_ASSERT_EQUAL_HEX16(0u, settings.manufacturerId);
  TEST_ASSERT_EQUAL_HEX16(0u, settings.productId);
  TEST_ASSERT_EQUAL_UINT8(0u, settings.densityCode);
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC256V, settings.capacityBytes);
  TEST_ASSERT_EQUAL_HEX32(cmd::MAX_MEM_ADDRESS_MB85RC256V, settings.maxAddress);
  TEST_ASSERT_EQUAL_UINT32(cmd::NORMAL_BUS_HZ, settings.maxNormalBusHz);
  TEST_ASSERT_EQUAL_UINT32(0u, settings.maxHighSpeedBusHz);
  TEST_ASSERT_FALSE(settings.highSpeedModeSupported);
  TEST_ASSERT_FALSE(settings.highSpeedModeEnabled);
  TEST_ASSERT_FALSE(settings.sleepModeSupported);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SleepState::AWAKE),
                          static_cast<uint8_t>(settings.sleepState));
  TEST_ASSERT_EQUAL_UINT32(0u, settings.sleepWakeReadyMs);
  TEST_ASSERT_EQUAL_UINT16(0u, settings.sleepRecoveryUs);
  TEST_ASSERT_FALSE(settings.currentAddressKnown);
  TEST_ASSERT_EQUAL_UINT32(0u, settings.currentAddress);

  const SettingsSnapshot byValue = dev.getSettings();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(settings.state),
                          static_cast<uint8_t>(byValue.state));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(dev.state()),
                          static_cast<uint8_t>(dev.driverState()));
}

// ===========================================================================
// Lifecycle tests
// ===========================================================================

void test_begin_rejects_missing_callbacks() {
  MB85RC::MB85RC dev;
  Config cfg;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_rejects_invalid_address() {
  FakeBus bus;
  Config cfg = makeConfig(bus);
  cfg.i2cAddress = 0x48;  // not in 0x50-0x57 range
  MB85RC::MB85RC dev;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG), static_cast<uint8_t>(st.code));
}

void test_begin_rejects_zero_timeout() {
  FakeBus bus;
  Config cfg = makeConfig(bus);
  cfg.i2cTimeoutMs = 0;
  MB85RC::MB85RC dev;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
}

void test_begin_success_sets_ready_and_health() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  Status st = dev.begin(makeConfig(bus));
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_TRUE(dev.isOnline());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(0u, dev.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.lastOkMs());
  TEST_ASSERT_EQUAL_STRING("MB85RC256V", dev.variantName());
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC256V, dev.capacityBytes());
  TEST_ASSERT_EQUAL_HEX32(cmd::MAX_MEM_ADDRESS_MB85RC256V, dev.maxAddress());
  TEST_ASSERT_EQUAL_HEX16(cmd::PRODUCT_ID_MB85RC256V, dev.deviceId().productId);
}

void test_begin_default_auto_selects_detected_device_id_variant() {
  FakeBus bus;
  bus.productId = cmd::PRODUCT_ID_MB85RC64TA;
  bus.memoryBytes = cmd::MEMORY_SIZE_MB85RC64TA;
  bus.addressModel = cmd::AddressModel::TWO_BYTE_ADDRESS_PINS;

  MB85RC::MB85RC dev;
  Config cfg = makeConfig(bus);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceVariant::AUTO),
                          static_cast<uint8_t>(cfg.expectedVariant));
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceVariant::AUTO),
                          static_cast<uint8_t>(snap.expectedVariant));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceVariant::MB85RC64TA),
                          static_cast<uint8_t>(snap.activeVariant));
  TEST_ASSERT_EQUAL_STRING("MB85RC64TA", snap.variantName);
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC64TA, snap.capacityBytes);
}

void test_begin_success_for_explicit_64ta_variant() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  Status st = dev.begin(make64TaConfig(bus));
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_STRING("MB85RC64TA", dev.variantName());
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC64TA, dev.capacityBytes());
  TEST_ASSERT_EQUAL_HEX32(cmd::MAX_MEM_ADDRESS_MB85RC64TA, dev.maxAddress());
  TEST_ASSERT_EQUAL_HEX16(cmd::MANUFACTURER_ID, dev.deviceId().manufacturerId);
  TEST_ASSERT_EQUAL_HEX16(cmd::PRODUCT_ID_MB85RC64TA, dev.deviceId().productId);

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.variantKnown);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceVariant::MB85RC64TA),
                          static_cast<uint8_t>(snap.expectedVariant));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceVariant::MB85RC64TA),
                          static_cast<uint8_t>(snap.activeVariant));
  TEST_ASSERT_EQUAL_STRING("MB85RC64TA", snap.variantName);
  TEST_ASSERT_EQUAL_HEX16(cmd::PRODUCT_ID_MB85RC64TA, snap.productId);
  TEST_ASSERT_EQUAL_UINT8(0x03, snap.densityCode);
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC64TA, snap.capacityBytes);
  TEST_ASSERT_EQUAL_HEX32(cmd::MAX_MEM_ADDRESS_MB85RC64TA, snap.maxAddress);
}

void test_begin_success_for_all_explicit_variants() {
  struct Case {
    DeviceVariant selector;
    const char* name;
    uint32_t capacity;
    uint32_t maxAddress;
    bool hasDeviceId;
    uint16_t productId;
  };

  const Case cases[] = {
      {DeviceVariant::MB85RC04V, "MB85RC04V", cmd::MEMORY_SIZE_MB85RC04V,
       cmd::MAX_MEM_ADDRESS_MB85RC04V, true, cmd::PRODUCT_ID_MB85RC04V},
      {DeviceVariant::MB85RC16V, "MB85RC16V", cmd::MEMORY_SIZE_MB85RC16V,
       cmd::MAX_MEM_ADDRESS_MB85RC16V, false, 0x000},
      {DeviceVariant::MB85RC64TA, "MB85RC64TA", cmd::MEMORY_SIZE_MB85RC64TA,
       cmd::MAX_MEM_ADDRESS_MB85RC64TA, true, cmd::PRODUCT_ID_MB85RC64TA},
      {DeviceVariant::MB85RC256V, "MB85RC256V", cmd::MEMORY_SIZE_MB85RC256V,
       cmd::MAX_MEM_ADDRESS_MB85RC256V, true, cmd::PRODUCT_ID_MB85RC256V},
      {DeviceVariant::MB85RC512T, "MB85RC512T", cmd::MEMORY_SIZE_MB85RC512T,
       cmd::MAX_MEM_ADDRESS_MB85RC512T, true, cmd::PRODUCT_ID_MB85RC512T},
      {DeviceVariant::MB85RC1MT, "MB85RC1MT", cmd::MEMORY_SIZE_MB85RC1MT,
       cmd::MAX_MEM_ADDRESS_MB85RC1MT, true, cmd::PRODUCT_ID_MB85RC1MT},
  };

  for (const Case& c : cases) {
    FakeBus bus;
    MB85RC::MB85RC dev;
    Status st = dev.begin(makeVariantConfig(bus, c.selector));
    TEST_ASSERT_TRUE(st.ok());
    TEST_ASSERT_EQUAL_STRING(c.name, dev.variantName());
    TEST_ASSERT_EQUAL_UINT32(c.capacity, dev.capacityBytes());
    TEST_ASSERT_EQUAL_HEX32(c.maxAddress, dev.maxAddress());

    SettingsSnapshot snap;
    TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(c.selector),
                            static_cast<uint8_t>(snap.expectedVariant));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(c.selector),
                            static_cast<uint8_t>(snap.activeVariant));
    TEST_ASSERT_EQUAL_UINT32(c.capacity, snap.capacityBytes);
    TEST_ASSERT_EQUAL_HEX32(c.maxAddress, snap.maxAddress);
    if (c.hasDeviceId) {
      TEST_ASSERT_EQUAL_HEX16(cmd::MANUFACTURER_ID, snap.manufacturerId);
      TEST_ASSERT_EQUAL_HEX16(c.productId, snap.productId);
    } else {
      TEST_ASSERT_EQUAL_HEX16(0u, snap.manufacturerId);
      TEST_ASSERT_EQUAL_HEX16(0u, snap.productId);
    }
  }
}

void test_begin_rejects_expected_variant_mismatch() {
  FakeBus bus;
  MB85RC::MB85RC dev;

  Config cfg = makeConfig(bus);
  cfg.expectedVariant = DeviceVariant::MB85RC64TA;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_ID_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));

  bus.productId = cmd::PRODUCT_ID_MB85RC64TA;
  bus.memoryBytes = cmd::MEMORY_SIZE_MB85RC64TA;
  cfg = makeConfig(bus);
  cfg.expectedVariant = DeviceVariant::MB85RC256V;
  st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_ID_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_auto_selects_supported_runtime_variant() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  Config cfg = makeConfig(bus);
  cfg.expectedVariant = DeviceVariant::AUTO;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  TEST_ASSERT_EQUAL_STRING("MB85RC256V", dev.variantName());
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC256V, dev.capacityBytes());

  dev.end();
  bus.productId = cmd::PRODUCT_ID_MB85RC64TA;
  bus.memoryBytes = cmd::MEMORY_SIZE_MB85RC64TA;
  cfg = makeConfig(bus);
  cfg.expectedVariant = DeviceVariant::AUTO;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  TEST_ASSERT_EQUAL_STRING("MB85RC64TA", dev.variantName());
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC64TA, dev.capacityBytes());

  dev.end();
  bus = FakeBus{};
  bus.productId = cmd::PRODUCT_ID_MB85RC04V;
  bus.memoryBytes = cmd::MEMORY_SIZE_MB85RC04V;
  bus.addressModel = cmd::AddressModel::ONE_BYTE_A8_IN_DEVICE_ADDRESS;
  cfg = makeConfig(bus);
  cfg.expectedVariant = DeviceVariant::AUTO;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  TEST_ASSERT_EQUAL_STRING("MB85RC04V", dev.variantName());
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC04V, dev.capacityBytes());

  dev.end();
  bus = FakeBus{};
  bus.productId = cmd::PRODUCT_ID_MB85RC512T;
  bus.memoryBytes = cmd::MEMORY_SIZE_MB85RC512T;
  bus.addressModel = cmd::AddressModel::TWO_BYTE_ADDRESS_PINS;
  cfg = makeConfig(bus);
  cfg.expectedVariant = DeviceVariant::AUTO;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  TEST_ASSERT_EQUAL_STRING("MB85RC512T", dev.variantName());
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC512T, dev.capacityBytes());

  dev.end();
  bus = FakeBus{};
  bus.productId = cmd::PRODUCT_ID_MB85RC1MT;
  bus.memoryBytes = cmd::MEMORY_SIZE_MB85RC1MT;
  bus.addressModel = cmd::AddressModel::TWO_BYTE_A16_IN_DEVICE_ADDRESS;
  cfg = makeConfig(bus);
  cfg.expectedVariant = DeviceVariant::AUTO;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  TEST_ASSERT_EQUAL_STRING("MB85RC1MT", dev.variantName());
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC1MT, dev.capacityBytes());
}

void test_begin_auto_cannot_select_no_device_id_variant() {
  FakeBus bus;
  bus.deviceIdSupported = false;
  bus.memoryBytes = cmd::MEMORY_SIZE_MB85RC16V;
  bus.addressModel = cmd::AddressModel::ONE_BYTE_UPPER_BITS_IN_DEVICE_ADDRESS;

  MB85RC::MB85RC dev;
  Config cfg = makeConfig(bus);
  cfg.expectedVariant = DeviceVariant::AUTO;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(st.code));
}

void test_begin_auto_rejects_unknown_device_id_product() {
  FakeBus bus;
  bus.productId = 0x123U;

  MB85RC::MB85RC dev;
  Config cfg = makeConfig(bus);
  cfg.expectedVariant = DeviceVariant::AUTO;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_ID_MISMATCH),
                          static_cast<uint8_t>(st.code));

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_FALSE(snap.initialized);
  TEST_ASSERT_FALSE(snap.variantKnown);
}

void test_begin_normalizes_zero_offline_threshold_in_settings() {
  FakeBus bus;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 0;

  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  SettingsSnapshot settings;
  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_TRUE(settings.initialized);
  TEST_ASSERT_EQUAL_UINT8(1u, settings.offlineThreshold);
  TEST_ASSERT_TRUE(settings.hasNowMsHook);
}

void test_begin_detects_device_not_found() {
  FakeBus bus;
  bus.readErrorRemaining = 1;
  MB85RC::MB85RC dev;
  Status st = dev.begin(makeConfig(bus));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_FALSE(snap.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(snap.state));
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_ADDRESS, snap.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(50u, snap.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5u, snap.offlineThreshold);
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
}

void test_begin_detects_device_id_mismatch() {
  FakeBus bus;
  bus.badDeviceId = true;
  MB85RC::MB85RC dev;
  Status st = dev.begin(makeConfig(bus));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_ID_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_FALSE(snap.initialized);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_ADDRESS, snap.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(50u, snap.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5u, snap.offlineThreshold);
  TEST_ASSERT_FALSE(snap.currentAddressKnown);
}

void test_failed_begin_clears_stale_runtime_snapshot() {
  FakeBus bus;
  MB85RC::MB85RC dev;

  Config good = makeConfig(bus);
  good.i2cAddress = 0x57;
  good.i2cTimeoutMs = 25;
  good.offlineThreshold = 4;
  TEST_ASSERT_TRUE(dev.begin(good).ok());
  TEST_ASSERT_TRUE(dev.writeByte(0x0001, 0xAA).ok());

  Config bad = makeConfig(bus);
  bad.i2cWrite = nullptr;
  bad.i2cWriteRead = nullptr;
  Status st = dev.begin(bad);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_FALSE(snap.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(snap.state));
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_ADDRESS, snap.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(50u, snap.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5u, snap.offlineThreshold);
  TEST_ASSERT_FALSE(snap.currentAddressKnown);
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(0u, dev.consecutiveFailures());
}

void test_end_transitions_to_uninit() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(dev.writeByte(0x0000, 0x11).ok());
  dev.end();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_FALSE(snap.initialized);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_ADDRESS, snap.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(50u, snap.i2cTimeoutMs);
  TEST_ASSERT_FALSE(snap.currentAddressKnown);
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(0u, dev.consecutiveFailures());
}

void test_now_ms_missing_callback_keeps_health_timestamps_zero() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  Config cfg = makeConfig(bus);
  cfg.nowMs = nullptr;
  cfg.timeUser = nullptr;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  setMillis(4321);
  Status st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.lastOkMs());

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_FALSE(snap.hasNowMsHook);
}

void test_get_settings_returns_runtime_snapshot() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 0;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  TEST_ASSERT_TRUE(dev.writeByte(0x0010, 0x5A).ok());

  SettingsSnapshot snap;
  Status st = dev.getSettings(snap);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(snap.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(snap.state));
  TEST_ASSERT_TRUE(snap.online);
  TEST_ASSERT_EQUAL_HEX8(0x50, snap.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(10u, snap.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(1u, snap.offlineThreshold);
  TEST_ASSERT_EQUAL_UINT32(dev.lastOkMs(), snap.lastOkMs);
  TEST_ASSERT_EQUAL_UINT32(dev.lastErrorMs(), snap.lastErrorMs);
  TEST_ASSERT_TRUE(snap.lastError.ok());
  TEST_ASSERT_EQUAL_UINT8(dev.consecutiveFailures(), snap.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(dev.totalFailures(), snap.totalFailures);
  TEST_ASSERT_EQUAL_UINT32(dev.totalSuccess(), snap.totalSuccess);
  TEST_ASSERT_TRUE(snap.hasNowMsHook);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceVariant::AUTO),
                          static_cast<uint8_t>(snap.expectedVariant));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceVariant::MB85RC256V),
                          static_cast<uint8_t>(snap.activeVariant));
  TEST_ASSERT_TRUE(snap.variantKnown);
  TEST_ASSERT_EQUAL_STRING("MB85RC256V", snap.variantName);
  TEST_ASSERT_EQUAL_HEX16(cmd::MANUFACTURER_ID, snap.manufacturerId);
  TEST_ASSERT_EQUAL_HEX16(cmd::PRODUCT_ID_MB85RC256V, snap.productId);
  TEST_ASSERT_EQUAL_UINT8(cmd::DENSITY_CODE, snap.densityCode);
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC256V, snap.capacityBytes);
  TEST_ASSERT_EQUAL_HEX32(cmd::MAX_MEM_ADDRESS_MB85RC256V, snap.maxAddress);
  TEST_ASSERT_EQUAL_UINT32(cmd::NORMAL_BUS_HZ, snap.maxNormalBusHz);
  TEST_ASSERT_EQUAL_UINT32(0u, snap.maxHighSpeedBusHz);
  TEST_ASSERT_FALSE(snap.highSpeedModeSupported);
  TEST_ASSERT_FALSE(snap.highSpeedModeEnabled);
  TEST_ASSERT_FALSE(snap.sleepModeSupported);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SleepState::AWAKE),
                          static_cast<uint8_t>(snap.sleepState));
  TEST_ASSERT_EQUAL_UINT16(0u, snap.sleepRecoveryUs);
  TEST_ASSERT_TRUE(snap.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX32(0x0011, snap.currentAddress);
}

void test_get_settings_is_bus_silent_after_begin_and_memory_traffic() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(dev.writeByte(0x0010, 0x5A).ok());

  const uint32_t readsBefore = bus.readCalls;
  const uint32_t writesBefore = bus.writeCalls;
  const uint32_t specialsBefore = bus.specialCalls;
  const uint32_t trafficBefore = busTraffic(bus);

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  const SettingsSnapshot byValue = dev.getSettings();

  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(specialsBefore, bus.specialCalls);
  TEST_ASSERT_EQUAL_UINT32(trafficBefore, busTraffic(bus));
  TEST_ASSERT_EQUAL_UINT32(dev.lastOkMs(), snap.lastOkMs);
  TEST_ASSERT_EQUAL_UINT32(dev.totalSuccess(), snap.totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(snap.totalSuccess, byValue.totalSuccess);
  TEST_ASSERT_TRUE(snap.currentAddressKnown);
}

// ===========================================================================
// Probe tests
// ===========================================================================

void test_probe_failure_does_not_update_health() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t beforeSuccess = dev.totalSuccess();
  const uint32_t beforeFailures = dev.totalFailures();
  const DriverState beforeState = dev.state();

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_ERROR, "forced probe error", -7);
  Status st = dev.probe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(beforeSuccess, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(beforeFailures, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(beforeState),
                          static_cast<uint8_t>(dev.state()));
}

void test_probe_id_mismatch_does_not_update_health() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t beforeSuccess = dev.totalSuccess();
  const uint32_t beforeFailures = dev.totalFailures();
  bus.badDeviceId = true;

  Status st = dev.probe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_ID_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(beforeSuccess, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(beforeFailures, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
}

void test_probe_validates_active_64ta_variant_without_health_update() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(make64TaConfig(bus)).ok());

  const uint32_t beforeSuccess = dev.totalSuccess();
  const uint32_t beforeFailures = dev.totalFailures();

  bus.productId = cmd::PRODUCT_ID_MB85RC256V;
  bus.memoryBytes = cmd::MEMORY_SIZE_MB85RC256V;
  Status st = dev.probe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_ID_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(beforeSuccess, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(beforeFailures, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
}

void test_diag_methods_reject_not_initialized() {
  MB85RC::MB85RC dev;
  DeviceId id;
  DeviceIdRaw raw;
  uint8_t value = 0;
  uint8_t values[2] = {};
  const uint8_t expected[1] = {0};
  VerifyResult verify;

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.probe().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.recover().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readDeviceId(id).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readDeviceIdRaw(raw).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readCurrentAddress(value).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readCurrentAddress(values, 2).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.verify(0x0000, expected, 1, verify).code));
}

// ===========================================================================
// Recover tests
// ===========================================================================

void test_recover_failure_updates_health_once() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_ERROR, "forced recover error", -8);
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(1u, dev.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
}

void test_recover_device_id_mismatch_updates_health_once() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(dev.writeByte(0x0000, 0x5A).ok());

  bus.nowMs = 2222;
  bus.badDeviceId = true;
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_ID_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(1u, dev.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_ID_MISMATCH),
                          static_cast<uint8_t>(dev.lastError().code));
  TEST_ASSERT_EQUAL_UINT32(2222u, dev.lastErrorMs());

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_FALSE(snap.currentAddressKnown);
}

void test_recover_validates_active_64ta_variant_and_keeps_capacity() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(make64TaConfig(bus)).ok());

  bus.nowMs = 3333;
  bus.productId = cmd::PRODUCT_ID_MB85RC256V;
  bus.memoryBytes = cmd::MEMORY_SIZE_MB85RC256V;
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_ID_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(1u, dev.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT32(3333u, dev.lastErrorMs());
  TEST_ASSERT_EQUAL_STRING("MB85RC64TA", dev.variantName());
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC64TA, dev.capacityBytes());
}

void test_recover_success_returns_ready() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_ERROR, "forced recover error", -9);
  (void)dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));

  bus.nowMs = 4321;
  Status st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(0u, dev.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(4321u, dev.lastOkMs());
}

void test_recover_reaches_offline_when_threshold_is_one() {
  FakeBus bus;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;

  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_NACK_ADDR, "forced recover nack", 7);
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
}

void test_offline_latches_normal_write_without_i2c_until_recover() {
  FakeBus bus;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;

  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::TIMEOUT, "forced timeout", -11);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::TIMEOUT),
                          static_cast<uint8_t>(dev.recover().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  const uint32_t readsBefore = bus.readCalls;
  const uint32_t writesBefore = bus.writeCalls;
  Status st = dev.writeByte(0x0000, 0xA5);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  TEST_ASSERT_TRUE(dev.recover().ok());
  TEST_ASSERT_GREATER_THAN_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
}

void test_failed_recover_from_offline_preserves_latch_after_partial_success() {
  FakeBus bus;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 3;

  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 3;
  bus.readError = Status::Error(Err::TIMEOUT, "forced timeout", -12);
  uint8_t value = 0;
  for (uint8_t i = 0; i < 3; ++i) {
    Status st = dev.readByte(0x0000, value);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::TIMEOUT),
                            static_cast<uint8_t>(st.code));
  }
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(3u, dev.consecutiveFailures());

  bus.badDeviceId = true;
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_ID_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_TRUE(dev.consecutiveFailures() >= 3u);

  bus.badDeviceId = false;
  const uint32_t writesBefore = bus.writeCalls;
  st = dev.writeByte(0x0000, 0xA5);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
}

void test_recover_preserves_transport_error_code() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_NACK_ADDR, "forced recover nack", 7);
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(dev.lastError().code));
}

// ===========================================================================
// Memory write/read tests
// ===========================================================================

void test_write_read_single_byte() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeByte(0x0000, 0xAB).ok());
  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readByte(0x0000, value).ok());
  TEST_ASSERT_EQUAL_HEX8(0xAB, value);
}

void test_write_read_multi_byte() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t wbuf[4] = {0x11, 0x22, 0x33, 0x44};
  TEST_ASSERT_TRUE(dev.write(0x0100, wbuf, 4).ok());

  uint8_t rbuf[4] = {};
  TEST_ASSERT_TRUE(dev.read(0x0100, rbuf, 4).ok());
  TEST_ASSERT_EQUAL_HEX8(0x11, rbuf[0]);
  TEST_ASSERT_EQUAL_HEX8(0x22, rbuf[1]);
  TEST_ASSERT_EQUAL_HEX8(0x33, rbuf[2]);
  TEST_ASSERT_EQUAL_HEX8(0x44, rbuf[3]);
}

void test_write_read_large_transfer_uses_chunking() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t writeBuf[200];
  for (size_t i = 0; i < sizeof(writeBuf); ++i) {
    writeBuf[i] = static_cast<uint8_t>(i);
  }

  const uint32_t writesBefore = bus.writeCalls;
  TEST_ASSERT_TRUE(dev.write(0x0100, writeBuf, sizeof(writeBuf)).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 2u, bus.writeCalls);

  uint8_t readBuf[200] = {};
  const uint32_t readsBefore = bus.readCalls;
  TEST_ASSERT_TRUE(dev.read(0x0100, readBuf, sizeof(readBuf)).ok());
  TEST_ASSERT_EQUAL_UINT32(readsBefore + 2u, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(writeBuf, readBuf, sizeof(writeBuf));
}

void test_transfer_read_respects_single_instruction_budget() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  static constexpr uint32_t ADDR = 0x0100;
  uint8_t expected[300] = {};
  for (size_t i = 0; i < sizeof(expected); ++i) {
    expected[i] = static_cast<uint8_t>(0x20U + (i & 0x7FU));
    bus.mem[ADDR + static_cast<uint32_t>(i)] = expected[i];
  }
  uint8_t actual[sizeof(expected)] = {};

  const uint32_t readsBefore = bus.readCalls;
  TEST_ASSERT_TRUE(dev.requestRead(ADDR, actual, sizeof(actual)).ok());
  TEST_ASSERT_TRUE(dev.isTransferBusy());
  assertTransferInProgress(dev.getTransferStatus());

  Status st = dev.pollTransfer(bus.nowMs, 0);
  assertTransferInProgress(st);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_TRUE(dev.isTransferBusy());

  st = dev.pollTransfer(bus.nowMs, 1);
  assertTransferInProgress(st);
  TEST_ASSERT_EQUAL_UINT32(readsBefore + 1U, bus.readCalls);
  TEST_ASSERT_EQUAL_HEX32(ADDR, bus.lastMemoryAddress);

  st = dev.pollTransfer(bus.nowMs, 1);
  assertTransferInProgress(st);
  TEST_ASSERT_EQUAL_UINT32(readsBefore + 2U, bus.readCalls);
  TEST_ASSERT_EQUAL_HEX32(ADDR + cmd::MAX_READ_CHUNK, bus.lastMemoryAddress);

  st = dev.pollTransfer(bus.nowMs, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.isTransferBusy());
  TEST_ASSERT_EQUAL_UINT32(readsBefore + 3U, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, sizeof(expected));

  st = dev.pollTransfer(bus.nowMs, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(readsBefore + 3U, bus.readCalls);
}

void test_transfer_write_respects_two_instruction_budget() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  static constexpr uint32_t ADDR = 0x0300;
  uint8_t data[300] = {};
  for (size_t i = 0; i < sizeof(data); ++i) {
    data[i] = static_cast<uint8_t>(0x40U + (i & 0x3FU));
  }
  setMemoryRange(bus, ADDR, sizeof(data), DETAILED_SENTINEL);

  const uint32_t writesBefore = bus.writeCalls;
  TEST_ASSERT_TRUE(dev.requestWrite(ADDR, data, sizeof(data)).ok());

  Status st = dev.pollTransfer(bus.nowMs, 2);
  assertTransferInProgress(st);
  TEST_ASSERT_TRUE(dev.isTransferBusy());
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 2U, bus.writeCalls);
  TEST_ASSERT_EQUAL_HEX32(ADDR + cmd::MAX_WRITE_CHUNK, bus.lastMemoryAddress);
  assertMemoryMatches(bus, ADDR, data, cmd::MAX_WRITE_CHUNK * 2U);
  assertMemoryRangeEquals(bus, ADDR + static_cast<uint32_t>(cmd::MAX_WRITE_CHUNK * 2U),
                          sizeof(data) - (cmd::MAX_WRITE_CHUNK * 2U),
                          DETAILED_SENTINEL);

  st = dev.pollTransfer(bus.nowMs, 2);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.isTransferBusy());
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 3U, bus.writeCalls);
  assertMemoryMatches(bus, ADDR, data, sizeof(data));
}

void test_transfer_fill_clamps_high_instruction_budget() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  static constexpr uint32_t ADDR = 0x0500;
  static constexpr size_t LEN = 600U;
  static constexpr uint8_t VALUE = 0xC7;
  setMemoryRange(bus, ADDR, LEN, DETAILED_SENTINEL);

  const uint32_t writesBefore = bus.writeCalls;
  TEST_ASSERT_TRUE(dev.requestFill(ADDR, VALUE, LEN).ok());

  Status st = dev.pollTransfer(bus.nowMs, 255);
  assertTransferInProgress(st);
  TEST_ASSERT_TRUE(dev.isTransferBusy());
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 8U, bus.writeCalls);
  assertMemoryRangeEquals(bus, ADDR, DETAILED_FILL_CHUNK * 8U, VALUE);
  assertMemoryRangeEquals(bus, ADDR + static_cast<uint32_t>(DETAILED_FILL_CHUNK * 8U),
                          LEN - (DETAILED_FILL_CHUNK * 8U), DETAILED_SENTINEL);

  st = dev.pollTransfer(bus.nowMs, 255);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.isTransferBusy());
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 10U, bus.writeCalls);
  assertMemoryRangeEquals(bus, ADDR, LEN, VALUE);
}

void test_transfer_verify_respects_budget_and_reports_mismatch() {
  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

    static constexpr uint32_t ADDR = 0x0800;
    uint8_t expected[300] = {};
    for (size_t i = 0; i < sizeof(expected); ++i) {
      expected[i] = static_cast<uint8_t>(0x60U + (i & 0x1FU));
      bus.mem[ADDR + static_cast<uint32_t>(i)] = expected[i];
    }

    const uint32_t readsBefore = bus.readCalls;
    TEST_ASSERT_TRUE(dev.requestVerify(ADDR, expected, sizeof(expected)).ok());
    Status st = dev.pollTransfer(bus.nowMs, 2);
    assertTransferInProgress(st);
    TEST_ASSERT_EQUAL_UINT32(readsBefore + 2U, bus.readCalls);

    st = dev.pollTransfer(bus.nowMs, 2);
    TEST_ASSERT_TRUE(st.ok());
    TEST_ASSERT_FALSE(dev.isTransferBusy());
    TEST_ASSERT_EQUAL_UINT32(readsBefore + 3U, bus.readCalls);
  }

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

    static constexpr uint32_t ADDR = 0x0900;
    uint8_t expected[300] = {};
    for (size_t i = 0; i < sizeof(expected); ++i) {
      expected[i] = static_cast<uint8_t>(0x80U + (i & 0x0FU));
      bus.mem[ADDR + static_cast<uint32_t>(i)] = expected[i];
    }
    bus.mem[ADDR + 130U] ^= 0x7FU;

    const uint32_t readsBefore = bus.readCalls;
    TEST_ASSERT_TRUE(dev.requestVerify(ADDR, expected, sizeof(expected)).ok());
    Status st = dev.pollTransfer(bus.nowMs, 2);
    TEST_ASSERT_TRUE(st.is(Err::VERIFY_MISMATCH));
    TEST_ASSERT_EQUAL_INT32(130, st.detail);
    TEST_ASSERT_FALSE(dev.isTransferBusy());
    TEST_ASSERT_EQUAL_UINT32(readsBefore + 2U, bus.readCalls);
    TEST_ASSERT_TRUE(dev.getTransferStatus().is(Err::VERIFY_MISMATCH));
    TEST_ASSERT_EQUAL_UINT32(0U, dev.totalFailures());
  }
}

void test_transfer_preflight_busy_cancel_and_exact_end_boundary() {
  FakeBus bus;
  bus.mem[cmd::MAX_MEM_ADDRESS_MB85RC256V] = 0xA6;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t writesBefore = bus.writeCalls;
  const uint32_t readsBefore = bus.readCalls;
  uint8_t byte = 0;

  Status st = dev.requestRead(0x0000, nullptr, 1);
  TEST_ASSERT_TRUE(st.is(Err::INVALID_PARAM));
  st = dev.requestRead(0x0000, &byte, 0);
  TEST_ASSERT_TRUE(st.is(Err::INVALID_PARAM));
  st = dev.requestRead(cmd::MAX_MEM_ADDRESS_MB85RC256V, &byte, 2);
  TEST_ASSERT_TRUE(st.is(Err::ADDRESS_OUT_OF_RANGE));
  TEST_ASSERT_FALSE(dev.isTransferBusy());
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);

  TEST_ASSERT_TRUE(dev.requestRead(cmd::MAX_MEM_ADDRESS_MB85RC256V, &byte, 1).ok());
  st = dev.readByte(0x0000, byte);
  TEST_ASSERT_TRUE(st.is(Err::BUSY));
  st = dev.requestFill(0x0000, 0x00, 1);
  TEST_ASSERT_TRUE(st.is(Err::BUSY));
  dev.cancelTransfer();
  TEST_ASSERT_FALSE(dev.isTransferBusy());
  TEST_ASSERT_TRUE(dev.getTransferStatus().is(Err::BUSY));

  TEST_ASSERT_TRUE(dev.requestRead(cmd::MAX_MEM_ADDRESS_MB85RC256V, &byte, 1).ok());
  st = dev.pollTransfer(bus.nowMs, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xA6, byte);
  TEST_ASSERT_FALSE(dev.isTransferBusy());
}

void test_transfer_timeout_after_possible_write_can_be_verified_afterwards() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  static constexpr uint32_t ADDR = 0x0A80;
  uint8_t data[300] = {};
  for (size_t i = 0; i < sizeof(data); ++i) {
    data[i] = static_cast<uint8_t>(0xA0U + (i & 0x1FU));
  }
  seedDetailedWindow(bus, ADDR, sizeof(data));

  const uint32_t writesBefore = bus.writeCalls;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "forced timeout after apply", -71);
  bus.writeErrorAfterApplyOnCall = bus.writeCalls + 2U;

  TEST_ASSERT_TRUE(dev.requestWrite(ADDR, data, sizeof(data)).ok());
  Status st = dev.pollTransfer(bus.nowMs, 3);
  TEST_ASSERT_TRUE(st.is(Err::I2C_TIMEOUT));
  TEST_ASSERT_EQUAL_INT32(-71, st.detail);
  TEST_ASSERT_FALSE(dev.isTransferBusy());
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 2U, bus.writeCalls);
  assertMemoryMatches(bus, ADDR, data, cmd::MAX_WRITE_CHUNK * 2U);
  assertMemoryRangeEquals(bus, ADDR + static_cast<uint32_t>(cmd::MAX_WRITE_CHUNK * 2U),
                          sizeof(data) - (cmd::MAX_WRITE_CHUNK * 2U),
                          DETAILED_SENTINEL);

  SettingsSnapshot settings;
  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_FALSE(settings.currentAddressKnown);
  TEST_ASSERT_EQUAL_UINT32(1U, dev.totalFailures());

  VerifyDetailedResult verify = dev.verifyDetailed(ADDR, data, sizeof(data));
  TEST_ASSERT_TRUE(verify.status.ok());
  TEST_ASSERT_FALSE(verify.match);
  TEST_ASSERT_EQUAL_UINT32(cmd::MAX_WRITE_CHUNK * 2U,
                           static_cast<uint32_t>(verify.bytesVerified));
  TEST_ASSERT_EQUAL_UINT32(cmd::MAX_WRITE_CHUNK * 2U,
                           static_cast<uint32_t>(verify.firstMismatchOffset));
}

void test_write_detailed_reports_single_and_multi_chunk_success() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint8_t small[4] = {0x11, 0x22, 0x33, 0x44};
  uint32_t writesBefore = bus.writeCalls;
  WriteResult wr = dev.writeDetailed(0x0200, small, sizeof(small));
  TEST_ASSERT_TRUE(wr.status.ok());
  TEST_ASSERT_TRUE(wr.complete);
  TEST_ASSERT_EQUAL_HEX32(0x0200, wr.address);
  TEST_ASSERT_EQUAL_UINT32(sizeof(small), static_cast<uint32_t>(wr.bytesRequested));
  TEST_ASSERT_EQUAL_UINT32(sizeof(small), static_cast<uint32_t>(wr.bytesAccepted));
  TEST_ASSERT_EQUAL_UINT32(sizeof(small), static_cast<uint32_t>(wr.failedChunkOffset));
  TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(wr.failedChunkLength));
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 1U, bus.writeCalls);
  assertMemoryMatches(bus, 0x0200, small, sizeof(small));

  uint8_t large[300] = {};
  for (size_t i = 0; i < sizeof(large); ++i) {
    large[i] = static_cast<uint8_t>(i ^ 0x5AU);
  }

  writesBefore = bus.writeCalls;
  wr = dev.writeDetailed(0x0300, large, sizeof(large));
  TEST_ASSERT_TRUE(wr.status.ok());
  TEST_ASSERT_TRUE(wr.complete);
  TEST_ASSERT_EQUAL_UINT32(sizeof(large), static_cast<uint32_t>(wr.bytesRequested));
  TEST_ASSERT_EQUAL_UINT32(sizeof(large), static_cast<uint32_t>(wr.bytesAccepted));
  TEST_ASSERT_EQUAL_UINT32(sizeof(large), static_cast<uint32_t>(wr.failedChunkOffset));
  TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(wr.failedChunkLength));
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 3U, bus.writeCalls);
  assertMemoryMatches(bus, 0x0300, large, sizeof(large));
}

void test_fill_detailed_reports_single_and_multi_chunk_success() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  static constexpr uint32_t SMALL_ADDR = 0x0350;
  static constexpr size_t SMALL_LEN = 19U;
  static constexpr uint8_t SMALL_VALUE = 0x5C;
  seedDetailedWindow(bus, SMALL_ADDR, SMALL_LEN);
  uint32_t writesBefore = bus.writeCalls;
  WriteResult wr = dev.fillDetailed(SMALL_ADDR, SMALL_VALUE, SMALL_LEN);
  assertWriteResultSuccess(wr, SMALL_ADDR, SMALL_LEN);
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 1U, bus.writeCalls);
  assertMemoryRangeEquals(bus, SMALL_ADDR, SMALL_LEN, SMALL_VALUE);
  assertDetailedGuardsUntouched(bus, SMALL_ADDR, SMALL_LEN);

  static constexpr uint32_t LARGE_ADDR = 0x0380;
  static constexpr size_t LARGE_LEN = (DETAILED_FILL_CHUNK * 2U) + 7U;
  static constexpr uint8_t LARGE_VALUE = 0x6D;
  seedDetailedWindow(bus, LARGE_ADDR, LARGE_LEN);
  writesBefore = bus.writeCalls;
  wr = dev.fillDetailed(LARGE_ADDR, LARGE_VALUE, LARGE_LEN);
  assertWriteResultSuccess(wr, LARGE_ADDR, LARGE_LEN);
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 3U, bus.writeCalls);
  assertMemoryRangeEquals(bus, LARGE_ADDR, LARGE_LEN, LARGE_VALUE);
  assertDetailedGuardsUntouched(bus, LARGE_ADDR, LARGE_LEN);
}

void test_write_detailed_reports_failed_chunk_and_accepted_prefix() {
  static constexpr uint32_t ADDR = 0x0400;
  uint8_t data[300] = {};
  for (size_t i = 0; i < sizeof(data); ++i) {
    data[i] = static_cast<uint8_t>(i + 1U);
  }

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    seedDetailedWindow(bus, ADDR, sizeof(data));
    bus.writeError = Status::Error(Err::I2C_NACK_DATA, "forced first chunk nack", -31);
    bus.writeErrorOnCall = bus.writeCalls + 1U;

    WriteResult wr = dev.writeDetailed(ADDR, data, sizeof(data));
    assertWriteResultFailure(wr, Err::I2C_NACK_DATA, ADDR, sizeof(data), 0U,
                             cmd::MAX_WRITE_CHUNK);
    assertDetailedPatternPrefixAndSentinelSuffix(bus, ADDR, data, 0U, sizeof(data));
  }

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    seedDetailedWindow(bus, ADDR, sizeof(data));
    bus.writeError = Status::Error(Err::I2C_TIMEOUT, "forced middle chunk timeout", -32);
    bus.writeErrorOnCall = bus.writeCalls + 2U;

    WriteResult wr = dev.writeDetailed(ADDR, data, sizeof(data));
    assertWriteResultFailure(wr, Err::I2C_TIMEOUT, ADDR, sizeof(data),
                             cmd::MAX_WRITE_CHUNK, cmd::MAX_WRITE_CHUNK);
    assertDetailedPatternPrefixAndSentinelSuffix(bus, ADDR, data, cmd::MAX_WRITE_CHUNK,
                                                 sizeof(data));
  }

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    seedDetailedWindow(bus, ADDR, sizeof(data));
    bus.writeError = Status::Error(Err::I2C_BUS, "forced last chunk bus error", -33);
    bus.writeErrorOnCall = bus.writeCalls + 3U;

    WriteResult wr = dev.writeDetailed(ADDR, data, sizeof(data));
    assertWriteResultFailure(wr, Err::I2C_BUS, ADDR, sizeof(data), 252U, 48U);
    assertDetailedPatternPrefixAndSentinelSuffix(bus, ADDR, data, 252U, sizeof(data));
  }
}

void test_fill_detailed_reports_failed_chunk_and_accepted_prefix() {
  static constexpr uint32_t ADDR = 0x0600;
  static constexpr size_t LEN = 160U;

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    seedDetailedWindow(bus, ADDR, LEN);
    bus.writeError = Status::Error(Err::I2C_NACK_DATA, "forced fill first chunk nack", -41);
    bus.writeErrorOnCall = bus.writeCalls + 1U;

    WriteResult wr = dev.fillDetailed(ADDR, 0x5A, LEN);
    assertWriteResultFailure(wr, Err::I2C_NACK_DATA, ADDR, LEN, 0U,
                             DETAILED_FILL_CHUNK);
    assertDetailedFillPrefixAndSentinelSuffix(bus, ADDR, 0x5A, 0U, LEN);
  }

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    seedDetailedWindow(bus, ADDR, LEN);
    bus.writeError = Status::Error(Err::I2C_TIMEOUT, "forced fill middle timeout", -42);
    bus.writeErrorOnCall = bus.writeCalls + 2U;

    WriteResult wr = dev.fillDetailed(ADDR, 0x5A, LEN);
    assertWriteResultFailure(wr, Err::I2C_TIMEOUT, ADDR, LEN, DETAILED_FILL_CHUNK,
                             DETAILED_FILL_CHUNK);
    assertDetailedFillPrefixAndSentinelSuffix(bus, ADDR, 0x5A, DETAILED_FILL_CHUNK, LEN);
  }

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    seedDetailedWindow(bus, ADDR, LEN);
    bus.writeError = Status::Error(Err::I2C_BUS, "forced fill last bus error", -43);
    bus.writeErrorOnCall = bus.writeCalls + 3U;

    WriteResult wr = dev.fillDetailed(ADDR, 0x5A, LEN);
    assertWriteResultFailure(wr, Err::I2C_BUS, ADDR, LEN, 128U, 32U);
    assertDetailedFillPrefixAndSentinelSuffix(bus, ADDR, 0x5A, 128U, LEN);
  }
}

void test_detailed_write_fill_preflight_rejects_without_bus_or_health() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t writesBefore = bus.writeCalls;
  const uint32_t readsBefore = bus.readCalls;
  const uint32_t failuresBefore = dev.totalFailures();
  const uint32_t invalidAddress = cmd::MAX_MEM_ADDRESS_MB85RC256V + 1UL;
  const uint32_t nearEnd = cmd::MAX_MEM_ADDRESS_MB85RC256V - 1UL;
  const size_t overflowLen = static_cast<size_t>(std::numeric_limits<uint32_t>::max());
  uint8_t value = 0xA5;

  WriteResult wr = dev.writeDetailed(invalidAddress, &value, 1U);
  assertWriteResultPreflightFailure(wr, Err::ADDRESS_OUT_OF_RANGE, invalidAddress, 1U);

  wr = dev.fillDetailed(invalidAddress, 0x00, 1U);
  assertWriteResultPreflightFailure(wr, Err::ADDRESS_OUT_OF_RANGE, invalidAddress, 1U);

  wr = dev.writeDetailed(nearEnd, &value, overflowLen);
  assertWriteResultPreflightFailure(wr, Err::ADDRESS_OUT_OF_RANGE, nearEnd, overflowLen);

  wr = dev.fillDetailed(nearEnd, 0x00, overflowLen);
  assertWriteResultPreflightFailure(wr, Err::ADDRESS_OUT_OF_RANGE, nearEnd, overflowLen);

  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(failuresBefore, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
}

void test_write_protect_ack_does_not_prove_persistence() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  static constexpr uint32_t ADDR = 0x0800;
  const uint8_t original[3] = {0x10, 0x20, 0x30};
  const uint8_t attempted[3] = {0xA0, 0xB0, 0xC0};
  for (size_t i = 0; i < sizeof(original); ++i) {
    bus.mem[ADDR + i] = original[i];
  }

  bus.writeProtectHigh = true;
  Status st = dev.write(ADDR, attempted, sizeof(attempted));
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(original, &bus.mem[ADDR], sizeof(original));

  VerifyResult result;
  st = dev.verify(ADDR, attempted, sizeof(attempted), result);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(result.match);
  TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(result.mismatchOffset));
  TEST_ASSERT_EQUAL_HEX8(attempted[0], result.expected);
  TEST_ASSERT_EQUAL_HEX8(original[0], result.actual);

  VerifyDetailedResult detailed = dev.verifyDetailed(ADDR, attempted, sizeof(attempted));
  TEST_ASSERT_TRUE(detailed.status.ok());
  TEST_ASSERT_FALSE(detailed.match);
  TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(detailed.bytesVerified));
}

void test_write_verify_success_and_wp_high_failure() {
  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

    const uint8_t data[4] = {0x44, 0x55, 0x66, 0x77};
    VerifyDetailedResult detailed;
    Status st = dev.writeVerify(0x0900, data, sizeof(data), &detailed);
    TEST_ASSERT_TRUE(st.ok());
    TEST_ASSERT_TRUE(detailed.status.ok());
    TEST_ASSERT_TRUE(detailed.match);
    TEST_ASSERT_EQUAL_UINT32(sizeof(data), static_cast<uint32_t>(detailed.bytesVerified));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, &bus.mem[0x0900], sizeof(data));
  }

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

    const uint8_t original[3] = {0x01, 0x02, 0x03};
    const uint8_t attempted[3] = {0x91, 0x92, 0x93};
    for (size_t i = 0; i < sizeof(original); ++i) {
      bus.mem[0x0910 + i] = original[i];
    }
    bus.writeProtectHigh = true;

    VerifyDetailedResult detailed;
    Status st = dev.writeVerify(0x0910, attempted, sizeof(attempted), &detailed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::VERIFY_MISMATCH),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_TRUE(detailed.status.ok());
    TEST_ASSERT_FALSE(detailed.match);
    TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(detailed.firstMismatchOffset));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original, &bus.mem[0x0910], sizeof(original));
  }
}

void test_fill_verify_success_and_wp_high_failure() {
  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

    VerifyDetailedResult detailed;
    Status st = dev.fillVerify(0x0940, 0x6D, 9, &detailed);
    TEST_ASSERT_TRUE(st.ok());
    TEST_ASSERT_TRUE(detailed.status.ok());
    TEST_ASSERT_TRUE(detailed.match);
    TEST_ASSERT_EQUAL_UINT32(9U, static_cast<uint32_t>(detailed.bytesVerified));
    assertMemoryRangeEquals(bus, 0x0940, 9, 0x6D);
  }

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

    setMemoryRange(bus, 0x0960, 5, 0x11);
    bus.writeProtectHigh = true;

    VerifyDetailedResult detailed;
    Status st = dev.fillVerify(0x0960, 0x22, 5, &detailed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::VERIFY_MISMATCH),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_TRUE(detailed.status.ok());
    TEST_ASSERT_FALSE(detailed.match);
    TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(detailed.firstMismatchOffset));
    TEST_ASSERT_EQUAL_HEX8(0x22, detailed.expected);
    TEST_ASSERT_EQUAL_HEX8(0x11, detailed.actual);
    assertMemoryRangeEquals(bus, 0x0960, 5, 0x11);
  }
}

void test_write_verify_reports_timeout_after_accepted_prefix_without_readback_claim() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  static constexpr uint32_t ADDR = 0x0A00;
  uint8_t data[300] = {};
  for (size_t i = 0; i < sizeof(data); ++i) {
    data[i] = static_cast<uint8_t>(0x30U + (i & 0x3FU));
  }

  seedDetailedWindow(bus, ADDR, sizeof(data));
  const uint32_t readsBefore = bus.readCalls;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "forced possible-write timeout", -61);
  bus.writeErrorOnCall = bus.writeCalls + 2U;

  VerifyDetailedResult detailed;
  Status st = dev.writeVerify(ADDR, data, sizeof(data), &detailed);
  TEST_ASSERT_TRUE(st.is(Err::I2C_TIMEOUT));
  TEST_ASSERT_TRUE(detailed.status.is(Err::I2C_TIMEOUT));
  TEST_ASSERT_EQUAL_INT32(-61, detailed.status.detail);
  TEST_ASSERT_EQUAL_HEX32(ADDR, detailed.address);
  TEST_ASSERT_EQUAL_UINT32(sizeof(data), static_cast<uint32_t>(detailed.bytesRequested));
  TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(detailed.bytesVerified));
  TEST_ASSERT_FALSE(detailed.match);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(1U, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  assertDetailedPatternPrefixAndSentinelSuffix(bus, ADDR, data, cmd::MAX_WRITE_CHUNK,
                                               sizeof(data));
}

void test_write_verify_timeout_after_possible_write_leaves_failed_chunk_unverified() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  static constexpr uint32_t ADDR = 0x0B80;
  uint8_t data[300] = {};
  for (size_t i = 0; i < sizeof(data); ++i) {
    data[i] = static_cast<uint8_t>(0x80U + (i & 0x7FU));
  }

  seedDetailedWindow(bus, ADDR, sizeof(data));
  const uint32_t readsBefore = bus.readCalls;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "forced timeout after write", -63);
  bus.writeErrorAfterApplyOnCall = bus.writeCalls + 2U;

  VerifyDetailedResult detailed;
  Status st = dev.writeVerify(ADDR, data, sizeof(data), &detailed);
  TEST_ASSERT_TRUE(st.is(Err::I2C_TIMEOUT));
  TEST_ASSERT_TRUE(detailed.status.is(Err::I2C_TIMEOUT));
  TEST_ASSERT_EQUAL_INT32(-63, detailed.status.detail);
  TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(detailed.bytesVerified));
  TEST_ASSERT_FALSE(detailed.match);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(1U, dev.totalFailures());

  const size_t physicallyChanged = cmd::MAX_WRITE_CHUNK * 2U;
  assertDetailedPatternPrefixAndSentinelSuffix(bus, ADDR, data, physicallyChanged,
                                               sizeof(data));
}

void test_fill_verify_reports_timeout_after_accepted_prefix_without_readback_claim() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  static constexpr uint32_t ADDR = 0x0C00;
  static constexpr size_t LEN = 160U;
  static constexpr uint8_t VALUE = 0xA7;

  seedDetailedWindow(bus, ADDR, LEN);
  const uint32_t readsBefore = bus.readCalls;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "forced possible-fill timeout", -62);
  bus.writeErrorOnCall = bus.writeCalls + 2U;

  VerifyDetailedResult detailed;
  Status st = dev.fillVerify(ADDR, VALUE, LEN, &detailed);
  TEST_ASSERT_TRUE(st.is(Err::I2C_TIMEOUT));
  TEST_ASSERT_TRUE(detailed.status.is(Err::I2C_TIMEOUT));
  TEST_ASSERT_EQUAL_INT32(-62, detailed.status.detail);
  TEST_ASSERT_EQUAL_HEX32(ADDR, detailed.address);
  TEST_ASSERT_EQUAL_UINT32(LEN, static_cast<uint32_t>(detailed.bytesRequested));
  TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(detailed.bytesVerified));
  TEST_ASSERT_FALSE(detailed.match);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(1U, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  assertDetailedFillPrefixAndSentinelSuffix(bus, ADDR, VALUE, DETAILED_FILL_CHUNK, LEN);
}

void test_failed_multichunk_write_invalidates_current_address_tracking() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeByte(0x0010, 0x5A).ok());
  SettingsSnapshot settings;
  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_TRUE(settings.currentAddressKnown);

  uint8_t data[300] = {};
  for (size_t i = 0; i < sizeof(data); ++i) {
    data[i] = static_cast<uint8_t>(0x80U + i);
  }
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "forced multichunk timeout", -51);
  bus.writeErrorOnCall = bus.writeCalls + 2U;

  WriteResult wr = dev.writeDetailed(0x0A00, data, sizeof(data));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(wr.status.code));
  TEST_ASSERT_EQUAL_UINT32(cmd::MAX_WRITE_CHUNK,
                           static_cast<uint32_t>(wr.bytesAccepted));

  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_FALSE(settings.currentAddressKnown);

  uint8_t value = 0;
  const uint32_t readsBefore = bus.readCalls;
  Status st = dev.readCurrentAddress(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
}

void test_failed_multichunk_fill_invalidates_current_address_tracking() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeByte(0x0010, 0x5A).ok());
  SettingsSnapshot settings;
  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_TRUE(settings.currentAddressKnown);

  bus.writeError = Status::Error(Err::I2C_BUS, "forced fill multichunk bus error", -52);
  bus.writeErrorOnCall = bus.writeCalls + 2U;

  static constexpr uint32_t ADDR = 0x0B00;
  static constexpr size_t LEN = (DETAILED_FILL_CHUNK * 2U) + 1U;
  WriteResult wr = dev.fillDetailed(ADDR, 0xB8, LEN);
  assertWriteResultFailure(wr, Err::I2C_BUS, ADDR, LEN, DETAILED_FILL_CHUNK,
                           DETAILED_FILL_CHUNK);

  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_FALSE(settings.currentAddressKnown);

  uint8_t value = 0;
  const uint32_t readsBefore = bus.readCalls;
  Status st = dev.readCurrentAddress(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
}

void test_write_rejects_cross_end_of_memory() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint8_t pattern[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  Status st = dev.write(0x7FFE, pattern, sizeof(pattern));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  TEST_ASSERT_TRUE(dev.write(0x7FFE, pattern, 2).ok());
  TEST_ASSERT_EQUAL_HEX8(0xDE, bus.mem[0x7FFE]);
  TEST_ASSERT_EQUAL_HEX8(0xAD, bus.mem[0x7FFF]);
}

void test_write_rejects_invalid_address() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  Status st = dev.writeByte(0x8000, 0x00);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
}

void test_read_rejects_invalid_address() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t value = 0;
  Status st = dev.readByte(0x8000, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
}

void test_memory_operations_reject_invalid_args_without_bus_or_health() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t writesBefore = bus.writeCalls;
  const uint32_t readsBefore = bus.readCalls;

  uint8_t value = 0;
  Status st = dev.read(0x0000, nullptr, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  st = dev.read(0x0000, nullptr, 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  st = dev.read(0x0000, &value, 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  st = dev.write(0x0000, nullptr, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  st = dev.write(0x0000, nullptr, 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  st = dev.write(0x0000, &value, 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  st = dev.fill(0x0000, 0x00, 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));

  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
}

void test_write_not_initialized() {
  MB85RC::MB85RC dev;
  Status st = dev.writeByte(0x0000, 0x00);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(st.code));
}

void test_read_not_initialized() {
  MB85RC::MB85RC dev;
  uint8_t value = 0;
  Status st = dev.readByte(0x0000, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(st.code));
}

void test_fill_memory() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.fill(0x0200, 0xFF, 10).ok());

  uint8_t rbuf[10] = {};
  TEST_ASSERT_TRUE(dev.read(0x0200, rbuf, 10).ok());
  for (int i = 0; i < 10; ++i) {
    TEST_ASSERT_EQUAL_HEX8(0xFF, rbuf[i]);
  }
}

void test_fill_rejects_cross_end_of_memory() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  Status st = dev.fill(0x7FFD, 0x5A, 6);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  TEST_ASSERT_TRUE(dev.fill(0x7FFD, 0x5A, 3).ok());
  for (uint16_t addr = 0x7FFD; addr <= 0x7FFF; ++addr) {
    TEST_ASSERT_EQUAL_HEX8(0x5A, bus.mem[addr]);
  }
}

void test_read_rejects_cross_end_of_memory() {
  FakeBus bus;
  bus.mem[0x7FFE] = 0x11;
  bus.mem[0x7FFF] = 0xAA;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t rbuf[3] = {};
  Status st = dev.read(0x7FFF, rbuf, 3);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  uint8_t exact[2] = {};
  TEST_ASSERT_TRUE(dev.read(0x7FFE, exact, sizeof(exact)).ok());
  TEST_ASSERT_EQUAL_HEX8(0x11, exact[0]);
  TEST_ASSERT_EQUAL_HEX8(0xAA, exact[1]);
}

void test_256v_exact_end_and_cross_end_boundaries() {
  FakeBus bus;
  bus.mem[cmd::MAX_MEM_ADDRESS_MB85RC256V] = 0x5E;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t maxAddress = cmd::MAX_MEM_ADDRESS_MB85RC256V;
  uint8_t byte = 0;
  TEST_ASSERT_TRUE(dev.read(maxAddress, &byte, 1).ok());
  TEST_ASSERT_EQUAL_HEX8(0x5E, byte);

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX32(0x0000, snap.currentAddress);

  const uint8_t writeValue = 0xA5;
  TEST_ASSERT_TRUE(dev.write(maxAddress, &writeValue, 1).ok());
  TEST_ASSERT_EQUAL_HEX8(writeValue, bus.mem[maxAddress]);
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX32(0x0000, snap.currentAddress);

  TEST_ASSERT_TRUE(dev.fill(maxAddress, 0x3C, 1).ok());
  const uint8_t expected = 0x3C;
  VerifyResult result;
  TEST_ASSERT_TRUE(dev.verify(maxAddress, &expected, 1, result).ok());
  TEST_ASSERT_TRUE(result.match);

  const uint32_t writesBefore = bus.writeCalls;
  const uint32_t readsBefore = bus.readCalls;
  const uint32_t failuresBefore = dev.totalFailures();
  uint8_t twoBytes[2] = {};

  Status st = dev.read(maxAddress, twoBytes, sizeof(twoBytes));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
  st = dev.write(maxAddress, twoBytes, sizeof(twoBytes));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
  st = dev.fill(maxAddress, 0x00, sizeof(twoBytes));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
  st = dev.verify(maxAddress, twoBytes, sizeof(twoBytes), result);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(failuresBefore, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
}

void test_memory_operations_reject_address_overflow_without_bus_or_health() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t writesBefore = bus.writeCalls;
  const uint32_t readsBefore = bus.readCalls;
  const uint32_t failuresBefore = dev.totalFailures();
  const uint32_t invalidAddress = std::numeric_limits<uint32_t>::max();
  const size_t oversizedLen = static_cast<size_t>(std::numeric_limits<uint32_t>::max());
  const uint32_t nearEnd = cmd::MAX_MEM_ADDRESS_MB85RC256V - 1UL;
  uint8_t byte = 0;
  VerifyResult result;

  Status st = dev.read(invalidAddress, &byte, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
  st = dev.write(invalidAddress, &byte, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
  st = dev.fill(invalidAddress, 0x00, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
  st = dev.verify(invalidAddress, &byte, 1, result);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  st = dev.read(nearEnd, &byte, oversizedLen);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
  st = dev.write(nearEnd, &byte, oversizedLen);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
  st = dev.fill(nearEnd, 0x00, oversizedLen);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
  st = dev.verify(nearEnd, &byte, oversizedLen, result);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(failuresBefore, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
}

void test_64ta_memory_address_encoding_and_bounds() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(make64TaConfig(bus)).ok());

  const uint8_t value = 0x5A;
  TEST_ASSERT_TRUE(dev.writeByte(0x0000, value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x00, bus.lastAddrHigh);
  TEST_ASSERT_EQUAL_HEX8(0x00, bus.lastAddrLow);
  TEST_ASSERT_EQUAL_HEX32(0x0000, bus.lastMemoryAddress);

  TEST_ASSERT_TRUE(dev.writeByte(0x0010, value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x00, bus.lastAddrHigh);
  TEST_ASSERT_EQUAL_HEX8(0x10, bus.lastAddrLow);
  TEST_ASSERT_EQUAL_HEX32(0x0010, bus.lastMemoryAddress);

  TEST_ASSERT_TRUE(dev.writeByte(0x1FFE, value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x1F, bus.lastAddrHigh);
  TEST_ASSERT_EQUAL_HEX8(0xFE, bus.lastAddrLow);
  TEST_ASSERT_EQUAL_HEX32(0x1FFE, bus.lastMemoryAddress);

  TEST_ASSERT_TRUE(dev.writeByte(0x1FFF, value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x1F, bus.lastAddrHigh);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.lastAddrLow);
  TEST_ASSERT_EQUAL_HEX32(0x1FFF, bus.lastMemoryAddress);

  Status st = dev.writeByte(0x2000, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  uint8_t readValue = 0;
  st = dev.readByte(0x2000, readValue);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
}

void test_variant_specific_memory_address_encoding_and_bounds() {
  const uint8_t value = 0x5A;

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeVariantConfig(bus, DeviceVariant::MB85RC04V)).ok());
    TEST_ASSERT_TRUE(dev.writeByte(0x0000, value).ok());
    TEST_ASSERT_EQUAL_HEX8(0x50, bus.lastI2cAddress);
    TEST_ASSERT_EQUAL_UINT32(1u, bus.lastAddressLen);
    TEST_ASSERT_EQUAL_HEX8(0x00, bus.lastAddrLow);
    TEST_ASSERT_EQUAL_HEX32(0x0000, bus.lastMemoryAddress);

    TEST_ASSERT_TRUE(dev.writeByte(0x0100, value).ok());
    TEST_ASSERT_EQUAL_HEX8(0x51, bus.lastI2cAddress);
    TEST_ASSERT_EQUAL_HEX8(0x00, bus.lastAddrLow);
    TEST_ASSERT_EQUAL_HEX32(0x0100, bus.lastMemoryAddress);

    TEST_ASSERT_TRUE(dev.writeByte(0x01FF, value).ok());
    TEST_ASSERT_EQUAL_HEX8(0x51, bus.lastI2cAddress);
    TEST_ASSERT_EQUAL_HEX8(0xFF, bus.lastAddrLow);
    TEST_ASSERT_EQUAL_HEX32(0x01FF, bus.lastMemoryAddress);

    Status st = dev.writeByte(0x0200, value);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                            static_cast<uint8_t>(st.code));
  }

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeVariantConfig(bus, DeviceVariant::MB85RC16V)).ok());
    TEST_ASSERT_TRUE(dev.writeByte(0x0000, value).ok());
    TEST_ASSERT_EQUAL_HEX8(0x50, bus.lastI2cAddress);
    TEST_ASSERT_EQUAL_UINT32(1u, bus.lastAddressLen);
    TEST_ASSERT_EQUAL_HEX8(0x00, bus.lastAddrLow);
    TEST_ASSERT_EQUAL_HEX32(0x0000, bus.lastMemoryAddress);

    TEST_ASSERT_TRUE(dev.writeByte(0x0100, value).ok());
    TEST_ASSERT_EQUAL_HEX8(0x51, bus.lastI2cAddress);
    TEST_ASSERT_EQUAL_HEX8(0x00, bus.lastAddrLow);
    TEST_ASSERT_EQUAL_HEX32(0x0100, bus.lastMemoryAddress);

    TEST_ASSERT_TRUE(dev.writeByte(0x07FF, value).ok());
    TEST_ASSERT_EQUAL_HEX8(0x57, bus.lastI2cAddress);
    TEST_ASSERT_EQUAL_HEX8(0xFF, bus.lastAddrLow);
    TEST_ASSERT_EQUAL_HEX32(0x07FF, bus.lastMemoryAddress);

    Status st = dev.writeByte(0x0800, value);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                            static_cast<uint8_t>(st.code));
  }

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeVariantConfig(bus, DeviceVariant::MB85RC512T)).ok());
    TEST_ASSERT_TRUE(dev.writeByte(0xFFFF, value).ok());
    TEST_ASSERT_EQUAL_HEX8(0x50, bus.lastI2cAddress);
    TEST_ASSERT_EQUAL_UINT32(2u, bus.lastAddressLen);
    TEST_ASSERT_EQUAL_HEX8(0xFF, bus.lastAddrHigh);
    TEST_ASSERT_EQUAL_HEX8(0xFF, bus.lastAddrLow);
    TEST_ASSERT_EQUAL_HEX32(0xFFFF, bus.lastMemoryAddress);

    Status st = dev.writeByte(0x10000, value);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                            static_cast<uint8_t>(st.code));
  }

  {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeVariantConfig(bus, DeviceVariant::MB85RC1MT)).ok());
    TEST_ASSERT_TRUE(dev.writeByte(0x00000, value).ok());
    TEST_ASSERT_EQUAL_HEX8(0x50, bus.lastI2cAddress);
    TEST_ASSERT_EQUAL_UINT32(2u, bus.lastAddressLen);
    TEST_ASSERT_EQUAL_HEX8(0x00, bus.lastAddrHigh);
    TEST_ASSERT_EQUAL_HEX8(0x00, bus.lastAddrLow);
    TEST_ASSERT_EQUAL_HEX32(0x00000, bus.lastMemoryAddress);

    TEST_ASSERT_TRUE(dev.writeByte(0x10000, value).ok());
    TEST_ASSERT_EQUAL_HEX8(0x51, bus.lastI2cAddress);
    TEST_ASSERT_EQUAL_HEX8(0x00, bus.lastAddrHigh);
    TEST_ASSERT_EQUAL_HEX8(0x00, bus.lastAddrLow);
    TEST_ASSERT_EQUAL_HEX32(0x10000, bus.lastMemoryAddress);

    TEST_ASSERT_TRUE(dev.writeByte(0x1FFFF, value).ok());
    TEST_ASSERT_EQUAL_HEX8(0x51, bus.lastI2cAddress);
    TEST_ASSERT_EQUAL_HEX8(0xFF, bus.lastAddrHigh);
    TEST_ASSERT_EQUAL_HEX8(0xFF, bus.lastAddrLow);
    TEST_ASSERT_EQUAL_HEX32(0x1FFFF, bus.lastMemoryAddress);

    Status st = dev.writeByte(0x20000, value);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                            static_cast<uint8_t>(st.code));
  }
}

void test_64ta_bulk_operations_reject_cross_end_ranges() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(make64TaConfig(bus)).ok());

  const uint8_t bytes[4] = {0x10, 0x11, 0x12, 0x13};
  uint8_t readBack[4] = {};
  VerifyResult verify;

  Status st = dev.write(0x1FFE, bytes, sizeof(bytes));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  st = dev.read(0x1FFE, readBack, sizeof(readBack));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  st = dev.fill(0x1FFE, 0xA5, sizeof(bytes));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  st = dev.verify(0x1FFE, bytes, sizeof(bytes), verify);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  TEST_ASSERT_TRUE(dev.write(0x1FFE, bytes, 2).ok());
  TEST_ASSERT_TRUE(dev.read(0x1FFE, readBack, 2).ok());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, readBack, 2);
}

void test_all_variants_bulk_exact_end_and_cross_end_bounds() {
  const DeviceVariant variants[] = {
      DeviceVariant::MB85RC04V,
      DeviceVariant::MB85RC16V,
      DeviceVariant::MB85RC64TA,
      DeviceVariant::MB85RC256V,
      DeviceVariant::MB85RC512T,
      DeviceVariant::MB85RC1MT,
  };

  for (DeviceVariant variant : variants) {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeVariantConfig(bus, variant)).ok());

    const uint32_t exactStart = dev.maxAddress() - 1UL;
    const uint32_t crossStart = dev.maxAddress();
    const uint8_t bytes[2] = {0x71, 0x72};
    uint8_t readBack[2] = {};
    VerifyResult verify;

    TEST_ASSERT_TRUE(dev.write(exactStart, bytes, sizeof(bytes)).ok());
    TEST_ASSERT_TRUE(dev.read(exactStart, readBack, sizeof(readBack)).ok());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, readBack, sizeof(bytes));
    TEST_ASSERT_TRUE(dev.verify(exactStart, bytes, sizeof(bytes), verify).ok());
    TEST_ASSERT_TRUE(verify.match);

    VerifyDetailedResult detailed;
    TEST_ASSERT_TRUE(dev.writeVerify(exactStart, bytes, sizeof(bytes), &detailed).ok());
    TEST_ASSERT_TRUE(detailed.match);
    TEST_ASSERT_TRUE(dev.fillVerify(exactStart, 0x5E, sizeof(bytes), &detailed).ok());
    TEST_ASSERT_TRUE(detailed.match);

    Status st = dev.write(crossStart, bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                            static_cast<uint8_t>(st.code));
    st = dev.read(crossStart, readBack, sizeof(readBack));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                            static_cast<uint8_t>(st.code));
    st = dev.fill(crossStart, 0x00, sizeof(bytes));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                            static_cast<uint8_t>(st.code));
    st = dev.verify(crossStart, bytes, sizeof(bytes), verify);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                            static_cast<uint8_t>(st.code));
    st = dev.writeVerify(crossStart, bytes, sizeof(bytes), &detailed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                            static_cast<uint8_t>(st.code));
    st = dev.fillVerify(crossStart, 0x00, sizeof(bytes), &detailed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                            static_cast<uint8_t>(st.code));
  }
}

void test_memory_operations_reject_oversized_size_t_lengths() {
  if (sizeof(size_t) <= sizeof(uint32_t)) {
    TEST_IGNORE_MESSAGE("size_t is not wider than uint32_t on this host");
  }

  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(make64TaConfig(bus)).ok());

  const size_t hugeLen = static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 1U;
  uint8_t byte = 0xA5;
  VerifyResult verify;

  Status st = dev.read(0x0000, &byte, hugeLen);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  st = dev.write(0x0000, &byte, hugeLen);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  st = dev.fill(0x0000, 0x00, hugeLen);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  st = dev.verify(0x0000, &byte, hugeLen, verify);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  TEST_ASSERT_TRUE(dev.writeByte(0x0000, 0x11).ok());
  st = dev.readCurrentAddress(&byte, hugeLen);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  st = typed_memory::writeBytes(dev, 0x0000, &byte, hugeLen);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  st = typed_memory::readBytes(dev, 0x0000, &byte, hugeLen);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
}

// ===========================================================================
// Device ID tests
// ===========================================================================

void test_read_device_id() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  DeviceId id;
  Status st = dev.readDeviceId(id);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX16(0x00A, id.manufacturerId);
  TEST_ASSERT_EQUAL_HEX16(0x510, id.productId);
  TEST_ASSERT_EQUAL_UINT8(0x05, id.densityCode);
}

void test_read_device_id_raw() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  DeviceIdRaw raw;
  Status st = dev.readDeviceIdRaw(raw);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(DEVID_BYTE0, raw.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(DEVID_BYTE1, raw.bytes[1]);
  TEST_ASSERT_EQUAL_HEX8(DEVID_BYTE2, raw.bytes[2]);
}

void test_variant_catalog_identifies_known_device_ids() {
  const cmd::VariantInfo* current = cmd::findVariantByProductId(cmd::PRODUCT_ID);
  TEST_ASSERT_NOT_NULL(current);
  TEST_ASSERT_EQUAL_STRING("MB85RC256V", current->name);
  TEST_ASSERT_EQUAL_UINT32(32768UL, current->memoryBytes);
  TEST_ASSERT_EQUAL_UINT8(cmd::DENSITY_CODE, current->densityCode);
  TEST_ASSERT_TRUE(current->supportedByDriver);
  TEST_ASSERT_TRUE(current->uses256vAccessFormat);
  TEST_ASSERT_FALSE(current->supportsHighSpeedMode);
  TEST_ASSERT_FALSE(current->supportsSleepMode);
  TEST_ASSERT_EQUAL_UINT32(cmd::NORMAL_BUS_HZ, current->maxNormalBusHz);
  TEST_ASSERT_EQUAL_UINT32(0u, current->maxHighSpeedBusHz);
  TEST_ASSERT_EQUAL_UINT16(0u, current->sleepRecoveryUs);

  const cmd::VariantInfo* rc64ta = cmd::findVariantByProductId(cmd::PRODUCT_ID_MB85RC64TA);
  TEST_ASSERT_NOT_NULL(rc64ta);
  TEST_ASSERT_EQUAL_STRING("MB85RC64TA", rc64ta->name);
  TEST_ASSERT_EQUAL_UINT32(8192UL, rc64ta->memoryBytes);
  TEST_ASSERT_EQUAL_UINT8(0x03, rc64ta->densityCode);
  TEST_ASSERT_TRUE(rc64ta->supportedByDriver);
  TEST_ASSERT_TRUE(rc64ta->uses256vAccessFormat);
  TEST_ASSERT_TRUE(rc64ta->supportsHighSpeedMode);
  TEST_ASSERT_TRUE(rc64ta->supportsSleepMode);
  TEST_ASSERT_EQUAL_UINT32(cmd::HIGH_SPEED_BUS_HZ, rc64ta->maxHighSpeedBusHz);
  TEST_ASSERT_EQUAL_UINT16(cmd::SLEEP_RECOVERY_US, rc64ta->sleepRecoveryUs);

  const cmd::VariantInfo* rc512 = cmd::findVariantByProductId(0x658);
  TEST_ASSERT_NOT_NULL(rc512);
  TEST_ASSERT_EQUAL_STRING("MB85RC512T", rc512->name);
  TEST_ASSERT_EQUAL_UINT32(65536UL, rc512->memoryBytes);
  TEST_ASSERT_TRUE(rc512->supportedByDriver);
  TEST_ASSERT_TRUE(rc512->uses256vAccessFormat);
  TEST_ASSERT_TRUE(rc512->sleepMode);
  TEST_ASSERT_TRUE(rc512->highSpeedMode);
  TEST_ASSERT_TRUE(rc512->supportsHighSpeedMode);
  TEST_ASSERT_TRUE(rc512->supportsSleepMode);

  const cmd::VariantInfo* rc04 = cmd::findVariantByProductId(cmd::PRODUCT_ID_MB85RC04V);
  TEST_ASSERT_NOT_NULL(rc04);
  TEST_ASSERT_EQUAL_STRING("MB85RC04V", rc04->name);
  TEST_ASSERT_TRUE(rc04->supportedByDriver);
  TEST_ASSERT_FALSE(rc04->uses256vAccessFormat);
  TEST_ASSERT_FALSE(rc04->supportsHighSpeedMode);
  TEST_ASSERT_FALSE(rc04->supportsSleepMode);

  const cmd::VariantInfo* rc1mt = cmd::findVariantByProductId(cmd::PRODUCT_ID_MB85RC1MT);
  TEST_ASSERT_NOT_NULL(rc1mt);
  TEST_ASSERT_EQUAL_STRING("MB85RC1MT", rc1mt->name);
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC1MT, rc1mt->memoryBytes);
  TEST_ASSERT_TRUE(rc1mt->supportedByDriver);
  TEST_ASSERT_FALSE(rc1mt->uses256vAccessFormat);
  TEST_ASSERT_TRUE(rc1mt->supportsHighSpeedMode);
  TEST_ASSERT_TRUE(rc1mt->supportsSleepMode);

  TEST_ASSERT_NULL(cmd::findVariantByProductId(0x123));
  TEST_ASSERT_NULL(cmd::findVariantByProductId(0x000));
}

void test_current_address_requires_prior_memory_access() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t value = 0;
  Status st = dev.readCurrentAddress(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
}

void test_current_address_tracks_memory_operations_and_settings() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint8_t pattern[3] = {0x11, 0x22, 0x33};
  TEST_ASSERT_TRUE(dev.write(0x1234, pattern, sizeof(pattern)).ok());
  bus.mem[0x1237] = 0x5A;

  SettingsSnapshot settings;
  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_TRUE(settings.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX32(0x1237, settings.currentAddress);

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readCurrentAddress(value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x5A, value);

  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_TRUE(settings.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX32(0x1238, settings.currentAddress);
}

void test_recover_invalidates_current_address_tracking() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeByte(0x0000, 0xA5).ok());

  SettingsSnapshot settings;
  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_TRUE(settings.currentAddressKnown);

  TEST_ASSERT_TRUE(dev.recover().ok());
  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_FALSE(settings.currentAddressKnown);

  uint8_t value = 0;
  Status st = dev.readCurrentAddress(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
}

void test_probe_no_device_id_variant_invalidates_current_address_tracking() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeVariantConfig(bus, DeviceVariant::MB85RC16V)).ok());

  TEST_ASSERT_TRUE(dev.writeByte(0x0004, 0xA5).ok());

  SettingsSnapshot settings;
  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_TRUE(settings.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX32(0x0005, settings.currentAddress);

  const uint32_t successBefore = dev.totalSuccess();
  const uint32_t failureBefore = dev.totalFailures();
  TEST_ASSERT_TRUE(dev.probe().ok());
  TEST_ASSERT_EQUAL_UINT32(successBefore, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(failureBefore, dev.totalFailures());

  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_FALSE(settings.currentAddressKnown);

  uint8_t value = 0;
  Status st = dev.readCurrentAddress(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
}

void test_failed_random_read_invalidates_current_address_tracking() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeByte(0x0010, 0x5A).ok());

  SettingsSnapshot settings;
  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_TRUE(settings.currentAddressKnown);

  const uint32_t readsBefore = bus.readCalls;
  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::TIMEOUT, "forced random read timeout", -21);
  uint8_t value = 0;
  Status st = dev.readByte(0x0020, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::TIMEOUT), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(readsBefore + 1U, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));

  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_FALSE(settings.currentAddressKnown);

  const uint32_t readsAfterFailure = bus.readCalls;
  st = dev.readCurrentAddress(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(readsAfterFailure, bus.readCalls);
}

void test_failed_write_invalidates_current_address_tracking() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeByte(0x0010, 0x5A).ok());

  SettingsSnapshot settings;
  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_TRUE(settings.currentAddressKnown);

  const uint32_t writesBefore = bus.writeCalls;
  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_DATA, "forced random write nack", -22);
  Status st = dev.writeByte(0x0020, 0xA5);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 1U, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));

  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_FALSE(settings.currentAddressKnown);

  uint8_t value = 0;
  const uint32_t readsBefore = bus.readCalls;
  st = dev.readCurrentAddress(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
}

void test_failed_current_address_read_invalidates_current_address_tracking() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeByte(0x0010, 0x5A).ok());

  SettingsSnapshot settings;
  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_TRUE(settings.currentAddressKnown);

  const uint32_t readsBefore = bus.readCalls;
  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_TIMEOUT, "forced current read timeout", -23);
  uint8_t value = 0;
  Status st = dev.readCurrentAddress(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(readsBefore + 1U, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));

  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_FALSE(settings.currentAddressKnown);

  const uint32_t readsAfterFailure = bus.readCalls;
  st = dev.readCurrentAddress(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(readsAfterFailure, bus.readCalls);
}

void test_read_current_address_requires_known_pointer() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t value = 0;
  Status st = dev.readCurrentAddress(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
}

void test_read_current_address_rejects_invalid_args_without_bus_or_health() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(dev.writeByte(0x0000, 0x11).ok());

  const uint32_t readsBefore = bus.readCalls;
  Status st = dev.readCurrentAddress(nullptr, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  st = dev.readCurrentAddress(nullptr, 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));

  uint8_t value = 0;
  st = dev.readCurrentAddress(&value, 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));

  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
}

void test_read_current_address_reads_next_byte_and_advances() {
  FakeBus bus;
  bus.mem[0x0010] = 0xAB;
  bus.mem[0x0011] = 0xCD;
  bus.mem[0x0012] = 0xEF;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t first = 0;
  TEST_ASSERT_TRUE(dev.readByte(0x0010, first).ok());
  TEST_ASSERT_EQUAL_HEX8(0xAB, first);

  uint8_t current = 0;
  TEST_ASSERT_TRUE(dev.readCurrentAddress(current).ok());
  TEST_ASSERT_EQUAL_HEX8(0xCD, current);

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX32(0x0012, snap.currentAddress);
}

void test_read_current_address_range_reads_multiple_bytes_and_advances() {
  FakeBus bus;
  bus.mem[0x0010] = 0xAB;
  bus.mem[0x0011] = 0xCD;
  bus.mem[0x0012] = 0xEF;
  bus.mem[0x0013] = 0x42;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t first = 0;
  TEST_ASSERT_TRUE(dev.readByte(0x0010, first).ok());
  TEST_ASSERT_EQUAL_HEX8(0xAB, first);

  uint8_t current[3] = {};
  TEST_ASSERT_TRUE(dev.readCurrentAddress(current, sizeof(current)).ok());
  TEST_ASSERT_EQUAL_HEX8(0xCD, current[0]);
  TEST_ASSERT_EQUAL_HEX8(0xEF, current[1]);
  TEST_ASSERT_EQUAL_HEX8(0x42, current[2]);

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX32(0x0014, snap.currentAddress);
}

void test_64ta_current_address_respects_active_capacity() {
  FakeBus bus;
  bus.mem[0x1FFD] = 0xA1;
  bus.mem[0x1FFE] = 0xA2;
  bus.mem[0x1FFF] = 0xA3;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(make64TaConfig(bus)).ok());

  uint8_t seed = 0;
  TEST_ASSERT_TRUE(dev.readByte(0x1FFD, seed).ok());
  TEST_ASSERT_EQUAL_HEX8(0xA1, seed);

  uint8_t crossing[3] = {};
  Status st = dev.readCurrentAddress(crossing, sizeof(crossing));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  uint8_t exact[2] = {};
  TEST_ASSERT_TRUE(dev.readCurrentAddress(exact, sizeof(exact)).ok());
  TEST_ASSERT_EQUAL_HEX8(0xA2, exact[0]);
  TEST_ASSERT_EQUAL_HEX8(0xA3, exact[1]);

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX32(0x0000, snap.currentAddress);
}

void test_1mt_current_address_uses_dynamic_i2c_address_and_32bit_range() {
  FakeBus bus;
  bus.mem[0x10000] = 0xA1;
  bus.mem[0x10001] = 0xA2;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeVariantConfig(bus, DeviceVariant::MB85RC1MT)).ok());

  uint8_t first = 0;
  TEST_ASSERT_TRUE(dev.readByte(0x10000, first).ok());
  TEST_ASSERT_EQUAL_HEX8(0xA1, first);
  TEST_ASSERT_EQUAL_HEX8(0x51, bus.lastI2cAddress);
  TEST_ASSERT_EQUAL_HEX32(0x10000, bus.lastMemoryAddress);

  uint8_t current = 0;
  TEST_ASSERT_TRUE(dev.readCurrentAddress(current).ok());
  TEST_ASSERT_EQUAL_HEX8(0xA2, current);
  TEST_ASSERT_EQUAL_HEX8(0x51, bus.lastI2cAddress);

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX32(0x10002, snap.currentAddress);

  uint8_t bytes[4] = {};
  Status st = dev.read(0x1FFFE, bytes, sizeof(bytes));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  TEST_ASSERT_TRUE(dev.writeByte(0x1FFFF, 0x5C).ok());
  TEST_ASSERT_EQUAL_HEX8(0x51, bus.lastI2cAddress);
  TEST_ASSERT_EQUAL_HEX32(0x1FFFF, bus.lastMemoryAddress);
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX32(0x00000, snap.currentAddress);
}

void test_no_device_id_variant_probe_recover_and_id_access() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeVariantConfig(bus, DeviceVariant::MB85RC16V)).ok());
  TEST_ASSERT_EQUAL_STRING("MB85RC16V", dev.variantName());

  const uint32_t successBeforeProbe = dev.totalSuccess();
  const uint32_t failureBeforeProbe = dev.totalFailures();
  TEST_ASSERT_TRUE(dev.probe().ok());
  TEST_ASSERT_EQUAL_UINT32(successBeforeProbe, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(failureBeforeProbe, dev.totalFailures());

  TEST_ASSERT_TRUE(dev.writeByte(0x0000, 0xA5).ok());
  TEST_ASSERT_TRUE(dev.recover().ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));

  DeviceId id;
  DeviceIdRaw raw;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.readDeviceId(id).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.readDeviceIdRaw(raw).code));
}

void test_verify_reports_match_and_first_mismatch() {
  FakeBus bus;
  bus.mem[0x7FFD] = 0x10;
  bus.mem[0x7FFE] = 0x11;
  bus.mem[0x7FFF] = 0x22;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  VerifyResult result;
  const uint8_t matchExpected[3] = {0x10, 0x11, 0x22};
  TEST_ASSERT_TRUE(dev.verify(0x7FFD, matchExpected, sizeof(matchExpected), result).ok());
  TEST_ASSERT_TRUE(result.match);

  const uint8_t mismatchExpected[3] = {0x10, 0x99, 0x22};
  TEST_ASSERT_TRUE(dev.verify(0x7FFD, mismatchExpected, sizeof(mismatchExpected), result).ok());
  TEST_ASSERT_FALSE(result.match);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(result.mismatchOffset));
  TEST_ASSERT_EQUAL_HEX8(0x99, result.expected);
  TEST_ASSERT_EQUAL_HEX8(0x11, result.actual);
}

void test_write_ack_ok_under_wp_high_but_verify_reports_mismatch() {
  FakeBus bus;
  static constexpr uint32_t ADDR = 0x0210;
  const uint8_t original[4] = {0x10, 0x20, 0x30, 0x40};
  const uint8_t attempted[4] = {0xA5, 0x5A, 0xC3, 0x3C};
  bus.mem[ADDR] = original[0];
  bus.mem[ADDR + 1] = original[1];
  bus.mem[ADDR + 2] = original[2];
  bus.mem[ADDR + 3] = original[3];
  bus.writeProtectHigh = true;

  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t writesBefore = bus.writeCalls;
  Status st = dev.write(ADDR, attempted, sizeof(attempted));
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 1u, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(original, &bus.mem[ADDR], sizeof(original));
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalSuccess());

  VerifyResult result;
  st = dev.verify(ADDR, attempted, sizeof(attempted), result);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(result.match);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(result.mismatchOffset));
  TEST_ASSERT_EQUAL_HEX8(attempted[0], result.expected);
  TEST_ASSERT_EQUAL_HEX8(original[0], result.actual);
}

void test_write_verify_fails_when_wp_high_leaves_backing_store_unchanged() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  assertWriteVerifyFailsWhenWriteProtectHigh(dev, bus);
}

void test_write_verify_succeeds_when_wp_disabled() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  assertWriteVerifySucceedsWhenWriteProtectDisabled(dev, bus);
}

void test_verify_rejects_invalid_args() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t readsBefore = bus.readCalls;
  VerifyResult result;
  const uint8_t expected[1] = {0x00};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.verify(0x0000, nullptr, 1, result).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.verify(0x0000, nullptr, 0, result).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.verify(0x0000, expected, 0, result).code));

  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
}

void test_random_access_write_read_verify_sequence() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  static constexpr uint16_t WINDOW_ADDR = 0x0300;
  static constexpr size_t WINDOW_LEN = 256;
  static constexpr uint32_t OPS = 2048;

  uint8_t expected[WINDOW_LEN] = {};
  uint32_t seed = 0x13579BDFU;

  for (uint32_t i = 0; i < OPS; ++i) {
    const size_t index = static_cast<size_t>(nextRandom(seed) % WINDOW_LEN);
    const uint8_t value = static_cast<uint8_t>(nextRandom(seed) & 0xFFU);
    expected[index] = value;
    TEST_ASSERT_TRUE(dev.writeByte(static_cast<uint16_t>(WINDOW_ADDR + index), value).ok());
  }

  uint8_t actual[WINDOW_LEN] = {};
  TEST_ASSERT_TRUE(dev.read(WINDOW_ADDR, actual, sizeof(actual)).ok());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, sizeof(actual));

  VerifyResult result;
  TEST_ASSERT_TRUE(dev.verify(WINDOW_ADDR, expected, sizeof(expected), result).ok());
  TEST_ASSERT_TRUE(result.match);
}

void test_typed_memory_round_trips_fixed_width_values() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(typed_memory::writeUint8(dev, 0x0100, 0xABU).ok());
  TEST_ASSERT_TRUE(typed_memory::writeUint16Le(dev, 0x0101, 0x1234U).ok());
  TEST_ASSERT_TRUE(typed_memory::writeInt32Le(dev, 0x0103, -1234567).ok());
  TEST_ASSERT_TRUE(typed_memory::writeUint32Le(dev, 0x0107, 0x11223344UL).ok());
  TEST_ASSERT_TRUE(typed_memory::writeUint64Le(dev, 0x010B, 0x0123456789ABCDEFULL).ok());
  TEST_ASSERT_TRUE(typed_memory::writeFloat32Le(dev, 0x0113, 1.25f).ok());
  TEST_ASSERT_TRUE(typed_memory::writeFloat64Le(dev, 0x0117, -42.5).ok());
  TEST_ASSERT_TRUE(typed_memory::writeBool(dev, 0x011F, true).ok());

  uint8_t u8 = 0;
  uint16_t u16 = 0;
  int32_t i32 = 0;
  uint32_t u32 = 0;
  uint64_t u64 = 0;
  float f32 = 0.0f;
  double f64 = 0.0;
  bool flag = false;

  TEST_ASSERT_TRUE(typed_memory::readUint8(dev, 0x0100, u8).ok());
  TEST_ASSERT_TRUE(typed_memory::readUint16Le(dev, 0x0101, u16).ok());
  TEST_ASSERT_TRUE(typed_memory::readInt32Le(dev, 0x0103, i32).ok());
  TEST_ASSERT_TRUE(typed_memory::readUint32Le(dev, 0x0107, u32).ok());
  TEST_ASSERT_TRUE(typed_memory::readUint64Le(dev, 0x010B, u64).ok());
  TEST_ASSERT_TRUE(typed_memory::readFloat32Le(dev, 0x0113, f32).ok());
  TEST_ASSERT_TRUE(typed_memory::readFloat64Le(dev, 0x0117, f64).ok());
  TEST_ASSERT_TRUE(typed_memory::readBool(dev, 0x011F, flag).ok());

  TEST_ASSERT_EQUAL_HEX8(0xAB, u8);
  TEST_ASSERT_EQUAL_HEX16(0x1234, u16);
  TEST_ASSERT_EQUAL_INT32(-1234567, i32);
  TEST_ASSERT_EQUAL_HEX32(0x11223344UL, u32);
  TEST_ASSERT_EQUAL_UINT64(0x0123456789ABCDEFULL, u64);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.25f, f32);
  TEST_ASSERT_TRUE(f64 > -42.5000001 && f64 < -42.4999999);
  TEST_ASSERT_TRUE(flag);

  TEST_ASSERT_EQUAL_HEX8(0x34, bus.mem[0x0101]);
  TEST_ASSERT_EQUAL_HEX8(0x12, bus.mem[0x0102]);
  TEST_ASSERT_EQUAL_HEX8(0x44, bus.mem[0x0107]);
  TEST_ASSERT_EQUAL_HEX8(0x33, bus.mem[0x0108]);
  TEST_ASSERT_EQUAL_HEX8(0x22, bus.mem[0x0109]);
  TEST_ASSERT_EQUAL_HEX8(0x11, bus.mem[0x010A]);
}

void test_typed_memory_rejects_cross_boundary_values() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  Status st = typed_memory::writeUint32Le(dev, 0x7FFE, 0xCAFEBABEUL);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));

  uint64_t value = 0;
  st = typed_memory::readUint64Le(dev, 0x7FF9, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::ADDRESS_OUT_OF_RANGE),
                          static_cast<uint8_t>(st.code));
}

// ===========================================================================
// High-speed and Sleep mode tests
// ===========================================================================

void test_begin_rejects_invalid_high_speed_master_code() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  Config cfg = make64TaConfig(bus);
  cfg.highSpeedMasterCode = 0x10U;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, busTraffic(bus));
}

void test_begin_rejects_sleep_recovery_below_datasheet_minimum() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  Config cfg = make64TaConfig(bus);
  cfg.sleepRecoveryUs = cmd::SLEEP_RECOVERY_US - 1U;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, busTraffic(bus));

  bus = FakeBus{};
  cfg = makeConfig(bus);
  cfg.expectedVariant = DeviceVariant::AUTO;
  cfg.sleepRecoveryUs = cmd::SLEEP_RECOVERY_US - 1U;
  bus.productId = cmd::PRODUCT_ID_MB85RC512T;
  bus.memoryBytes = cmd::MEMORY_SIZE_MB85RC512T;
  bus.addressModel = cmd::AddressModel::TWO_BYTE_ADDRESS_PINS;
  st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
}

void test_high_speed_rejects_unsupported_variants_without_bus_traffic() {
  const DeviceVariant variants[] = {
      DeviceVariant::MB85RC04V,
      DeviceVariant::MB85RC16V,
      DeviceVariant::MB85RC256V,
  };

  for (DeviceVariant variant : variants) {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeVariantConfig(bus, variant)).ok());
    const uint32_t trafficBefore = busTraffic(bus);

    Status st = dev.enterHighSpeedMode();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::UNSUPPORTED),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_UINT32(trafficBefore, busTraffic(bus));
    TEST_ASSERT_FALSE(dev.highSpeedModeEnabled());
  }
}

void test_high_speed_requires_special_callback_for_supported_variant() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  Config cfg = make64TaConfig(bus);
  cfg.i2cSpecial = nullptr;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  const uint32_t trafficBefore = busTraffic(bus);

  Status st = dev.enterHighSpeedMode();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(trafficBefore, busTraffic(bus));
  TEST_ASSERT_FALSE(dev.highSpeedModeEnabled());
}

void test_high_speed_supported_variants_use_special_transfer_path() {
  const DeviceVariant variants[] = {
      DeviceVariant::MB85RC64TA,
      DeviceVariant::MB85RC512T,
      DeviceVariant::MB85RC1MT,
  };

  for (DeviceVariant variant : variants) {
    FakeBus bus;
    MB85RC::MB85RC dev;
    Config cfg = makeVariantConfig(bus, variant);
    cfg.highSpeedMasterCode = cmd::HIGH_SPEED_MASTER_CODE_MAX;
    TEST_ASSERT_TRUE(dev.begin(cfg).ok());
    const uint32_t normalWritesBefore = bus.writeCalls;
    const uint32_t normalReadsBefore = bus.readCalls;

    TEST_ASSERT_TRUE(dev.supportsHighSpeedMode());
    TEST_ASSERT_EQUAL_UINT32(cmd::HIGH_SPEED_BUS_HZ, dev.maxHighSpeedBusHz());
    TEST_ASSERT_TRUE(dev.enterHighSpeedMode().ok());
    TEST_ASSERT_TRUE(dev.highSpeedModeEnabled());

    TEST_ASSERT_TRUE(dev.writeByte(0x0001, 0xA5).ok());
    TEST_ASSERT_EQUAL_UINT32(normalWritesBefore, bus.writeCalls);
    TEST_ASSERT_EQUAL_UINT32(1u, bus.hsWriteCalls);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(I2cSpecialOp::HIGH_SPEED_WRITE),
                            static_cast<uint8_t>(bus.lastSpecialOp));
    TEST_ASSERT_EQUAL_HEX8(cmd::HIGH_SPEED_MASTER_CODE_MAX, bus.lastHsMasterCode);

    uint8_t value = 0;
    TEST_ASSERT_TRUE(dev.readByte(0x0001, value).ok());
    TEST_ASSERT_EQUAL_HEX8(0xA5, value);
    TEST_ASSERT_EQUAL_UINT32(normalReadsBefore, bus.readCalls);
    TEST_ASSERT_EQUAL_UINT32(1u, bus.hsWriteReadCalls);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(I2cSpecialOp::HIGH_SPEED_WRITE_READ),
                            static_cast<uint8_t>(bus.lastSpecialOp));

    TEST_ASSERT_TRUE(dev.exitHighSpeedMode().ok());
    TEST_ASSERT_FALSE(dev.highSpeedModeEnabled());
  }
}

void test_generic_nack_remains_failure_outside_high_speed_prefix() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_ADDR, "forced nack", -11);
  Status st = dev.writeByte(0x0001, 0x22);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
}

void test_sleep_rejects_unsupported_variants_without_bus_traffic() {
  const DeviceVariant variants[] = {
      DeviceVariant::MB85RC04V,
      DeviceVariant::MB85RC16V,
      DeviceVariant::MB85RC256V,
  };

  for (DeviceVariant variant : variants) {
    FakeBus bus;
    MB85RC::MB85RC dev;
    TEST_ASSERT_TRUE(dev.begin(makeVariantConfig(bus, variant)).ok());
    const uint32_t trafficBefore = busTraffic(bus);

    Status st = dev.enterSleep();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::UNSUPPORTED),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_UINT32(trafficBefore, busTraffic(bus));

    st = dev.wake();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::UNSUPPORTED),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_UINT32(trafficBefore, busTraffic(bus));
  }
}

void test_sleep_requires_special_callback_for_supported_variant() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  Config cfg = make64TaConfig(bus);
  cfg.i2cSpecial = nullptr;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  const uint32_t trafficBefore = busTraffic(bus);

  Status st = dev.enterSleep();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(trafficBefore, busTraffic(bus));
}

void test_wake_is_noop_when_awake_even_without_special_callback() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  Config cfg = make64TaConfig(bus);
  cfg.i2cSpecial = nullptr;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  const uint32_t trafficBefore = busTraffic(bus);

  Status st = dev.wake();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(trafficBefore, busTraffic(bus));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SleepState::AWAKE),
                          static_cast<uint8_t>(dev.sleepState()));
}

void test_sleep_enter_wake_gates_memory_access_and_invalidates_current_address() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(make64TaConfig(bus)).ok());
  TEST_ASSERT_TRUE(dev.writeByte(0x0002, 0x5A).ok());

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.currentAddressKnown);
  TEST_ASSERT_TRUE(snap.sleepModeSupported);
  TEST_ASSERT_EQUAL_UINT16(cmd::SLEEP_RECOVERY_US, snap.sleepRecoveryUs);

  const uint32_t trafficBeforeSleep = busTraffic(bus);
  TEST_ASSERT_TRUE(dev.enterSleep().ok());
  TEST_ASSERT_EQUAL_UINT32(trafficBeforeSleep + 1U, busTraffic(bus));
  TEST_ASSERT_EQUAL_UINT32(1u, bus.sleepEntryCalls);
  TEST_ASSERT_TRUE(bus.sleeping);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SleepState::ASLEEP),
                          static_cast<uint8_t>(dev.sleepState()));

  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_FALSE(snap.currentAddressKnown);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SleepState::ASLEEP),
                          static_cast<uint8_t>(snap.sleepState));

  const uint32_t trafficWhileAsleep = busTraffic(bus);
  uint8_t value = 0;
  Status st = dev.readByte(0x0002, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(trafficWhileAsleep, busTraffic(bus));

  TEST_ASSERT_TRUE(dev.wake().ok());
  TEST_ASSERT_FALSE(bus.sleeping);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.wakeCalls);
  TEST_ASSERT_EQUAL_UINT16(cmd::SLEEP_RECOVERY_US, bus.lastRecoveryUs);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SleepState::WAKING),
                          static_cast<uint8_t>(dev.sleepState()));

  const uint32_t trafficWhileWaking = busTraffic(bus);
  st = dev.readByte(0x0002, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(trafficWhileWaking, busTraffic(bus));

  bus.nowMs += cmd::SLEEP_RECOVERY_MS;
  dev.tick(bus.nowMs);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SleepState::AWAKE),
                          static_cast<uint8_t>(dev.sleepState()));
  TEST_ASSERT_TRUE(dev.readByte(0x0002, value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x5A, value);
}

void test_sleep_entry_failure_updates_health_once_and_invalidates_current_address() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(make64TaConfig(bus)).ok());
  TEST_ASSERT_TRUE(dev.writeByte(0x0004, 0x7C).ok());

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_TRUE(snap.currentAddressKnown);

  const uint32_t failuresBefore = dev.totalFailures();
  bus.specialErrorOnCall = bus.specialCalls + 1U;
  bus.specialError = Status::Error(Err::I2C_NACK_ADDR, "sleep nack", -12);
  Status st = dev.enterSleep();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(failuresBefore + 1U, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SleepState::AWAKE),
                          static_cast<uint8_t>(dev.sleepState()));

  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_FALSE(snap.currentAddressKnown);
}

// ===========================================================================
// Health tracking tests
// ===========================================================================

void test_write_failure_transitions_to_degraded() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_ERROR, "forced write error", -5);
  Status st = dev.writeByte(0x0000, 0x00);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(1u, dev.consecutiveFailures());
}

void test_consecutive_failures_reach_offline() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 3;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.writeErrorRemaining = 3;
  bus.writeError = Status::Error(Err::I2C_ERROR, "forced write error", -5);
  (void)dev.writeByte(0x0000, 0x00);
  (void)dev.writeByte(0x0001, 0x00);
  (void)dev.writeByte(0x0002, 0x00);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(3u, dev.consecutiveFailures());
}

void test_success_after_degraded_returns_to_ready() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_ERROR, "forced error", -1);
  (void)dev.writeByte(0x0000, 0x00);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));

  // Next successful operation
  TEST_ASSERT_TRUE(dev.writeByte(0x0000, 0x00).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(0u, dev.consecutiveFailures());
}

// ===========================================================================
// Transport adapter tests
// ===========================================================================

void test_example_transport_maps_wire_errors() {
  Wire._clearEndTransmissionResult();
  Wire._clearRequestFromOverride();

  TEST_ASSERT_TRUE(transport::initWire(8, 9, 400000, 77));
  TEST_ASSERT_EQUAL_UINT32(77u, Wire.getTimeOut());

  const uint8_t byte = 0x55;

  Wire._setEndTransmissionResult(2);
  Status st = transport::wireWrite(0x50, &byte, 1, 123, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(st.code));

  Wire._setEndTransmissionResult(3);
  st = transport::wireWrite(0x50, &byte, 1, 999, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));

  Wire._setEndTransmissionResult(5);
  st = transport::wireWrite(0x50, &byte, 1, 10, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));

  Wire._setEndTransmissionResult(4);
  st = transport::wireWrite(0x50, &byte, 1, 10, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));

  Wire._clearEndTransmissionResult();
}

void test_example_transport_supports_read_only_transactions() {
  Wire._clearEndTransmissionResult();
  Wire._clearRequestFromOverride();

  uint8_t rxSeed[2] = {0xDE, 0xAD};
  Wire._setRxBuffer(rxSeed, 2);

  uint8_t rx[2] = {};
  Status st = transport::wireWriteRead(0x50, nullptr, 0, rx, 2, 50, &Wire);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xDE, rx[0]);
  TEST_ASSERT_EQUAL_HEX8(0xAD, rx[1]);
}

// ===========================================================================
// Memory size test
// ===========================================================================

void test_memory_size() {
  TEST_ASSERT_EQUAL_UINT16(32768, MB85RC::MB85RC::memorySize());

  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC256V, dev.capacityBytes());

  dev.end();
  TEST_ASSERT_TRUE(dev.begin(make64TaConfig(bus)).ok());
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC64TA, dev.capacityBytes());

  dev.end();
  bus = FakeBus{};
  TEST_ASSERT_TRUE(dev.begin(makeVariantConfig(bus, DeviceVariant::MB85RC512T)).ok());
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC512T, dev.capacityBytes());

  dev.end();
  bus = FakeBus{};
  TEST_ASSERT_TRUE(dev.begin(makeVariantConfig(bus, DeviceVariant::MB85RC1MT)).ok());
  TEST_ASSERT_EQUAL_UINT32(cmd::MEMORY_SIZE_MB85RC1MT, dev.capacityBytes());
}

// ===========================================================================
// Test runner
// ===========================================================================

int main() {
  UNITY_BEGIN();

  // Status
  RUN_TEST(test_status_ok);
  RUN_TEST(test_status_error);
  RUN_TEST(test_status_in_progress);

  // Config
  RUN_TEST(test_config_defaults);
  RUN_TEST(test_get_settings_before_begin_reports_defaults);

  // Lifecycle
  RUN_TEST(test_begin_rejects_missing_callbacks);
  RUN_TEST(test_begin_rejects_invalid_address);
  RUN_TEST(test_begin_rejects_zero_timeout);
  RUN_TEST(test_begin_success_sets_ready_and_health);
  RUN_TEST(test_begin_default_auto_selects_detected_device_id_variant);
  RUN_TEST(test_begin_success_for_explicit_64ta_variant);
  RUN_TEST(test_begin_success_for_all_explicit_variants);
  RUN_TEST(test_begin_rejects_expected_variant_mismatch);
  RUN_TEST(test_begin_auto_selects_supported_runtime_variant);
  RUN_TEST(test_begin_auto_cannot_select_no_device_id_variant);
  RUN_TEST(test_begin_auto_rejects_unknown_device_id_product);
  RUN_TEST(test_begin_normalizes_zero_offline_threshold_in_settings);
  RUN_TEST(test_begin_detects_device_not_found);
  RUN_TEST(test_begin_detects_device_id_mismatch);
  RUN_TEST(test_failed_begin_clears_stale_runtime_snapshot);
  RUN_TEST(test_end_transitions_to_uninit);
  RUN_TEST(test_now_ms_missing_callback_keeps_health_timestamps_zero);
  RUN_TEST(test_get_settings_returns_runtime_snapshot);
  RUN_TEST(test_get_settings_is_bus_silent_after_begin_and_memory_traffic);

  // Probe
  RUN_TEST(test_probe_failure_does_not_update_health);
  RUN_TEST(test_probe_id_mismatch_does_not_update_health);
  RUN_TEST(test_probe_validates_active_64ta_variant_without_health_update);
  RUN_TEST(test_diag_methods_reject_not_initialized);

  // Recover
  RUN_TEST(test_recover_failure_updates_health_once);
  RUN_TEST(test_recover_device_id_mismatch_updates_health_once);
  RUN_TEST(test_recover_validates_active_64ta_variant_and_keeps_capacity);
  RUN_TEST(test_recover_success_returns_ready);
  RUN_TEST(test_recover_reaches_offline_when_threshold_is_one);
  RUN_TEST(test_offline_latches_normal_write_without_i2c_until_recover);
  RUN_TEST(test_failed_recover_from_offline_preserves_latch_after_partial_success);
  RUN_TEST(test_recover_preserves_transport_error_code);

  // Memory write/read
  RUN_TEST(test_write_read_single_byte);
  RUN_TEST(test_write_read_multi_byte);
  RUN_TEST(test_write_read_large_transfer_uses_chunking);
  RUN_TEST(test_transfer_read_respects_single_instruction_budget);
  RUN_TEST(test_transfer_write_respects_two_instruction_budget);
  RUN_TEST(test_transfer_fill_clamps_high_instruction_budget);
  RUN_TEST(test_transfer_verify_respects_budget_and_reports_mismatch);
  RUN_TEST(test_transfer_preflight_busy_cancel_and_exact_end_boundary);
  RUN_TEST(test_transfer_timeout_after_possible_write_can_be_verified_afterwards);
  RUN_TEST(test_write_detailed_reports_single_and_multi_chunk_success);
  RUN_TEST(test_fill_detailed_reports_single_and_multi_chunk_success);
  RUN_TEST(test_write_detailed_reports_failed_chunk_and_accepted_prefix);
  RUN_TEST(test_fill_detailed_reports_failed_chunk_and_accepted_prefix);
  RUN_TEST(test_detailed_write_fill_preflight_rejects_without_bus_or_health);
  RUN_TEST(test_failed_multichunk_write_invalidates_current_address_tracking);
  RUN_TEST(test_failed_multichunk_fill_invalidates_current_address_tracking);
  RUN_TEST(test_write_rejects_invalid_address);
  RUN_TEST(test_read_rejects_invalid_address);
  RUN_TEST(test_memory_operations_reject_invalid_args_without_bus_or_health);
  RUN_TEST(test_write_not_initialized);
  RUN_TEST(test_read_not_initialized);
  RUN_TEST(test_fill_memory);
  RUN_TEST(test_read_rejects_cross_end_of_memory);
  RUN_TEST(test_write_rejects_cross_end_of_memory);
  RUN_TEST(test_fill_rejects_cross_end_of_memory);
  RUN_TEST(test_256v_exact_end_and_cross_end_boundaries);
  RUN_TEST(test_memory_operations_reject_address_overflow_without_bus_or_health);
  RUN_TEST(test_64ta_memory_address_encoding_and_bounds);
  RUN_TEST(test_variant_specific_memory_address_encoding_and_bounds);
  RUN_TEST(test_64ta_bulk_operations_reject_cross_end_ranges);
  RUN_TEST(test_all_variants_bulk_exact_end_and_cross_end_bounds);
  RUN_TEST(test_memory_operations_reject_oversized_size_t_lengths);

  // Device ID
  RUN_TEST(test_read_device_id);
  RUN_TEST(test_read_device_id_raw);
  RUN_TEST(test_variant_catalog_identifies_known_device_ids);
  RUN_TEST(test_current_address_requires_prior_memory_access);
  RUN_TEST(test_current_address_tracks_memory_operations_and_settings);
  RUN_TEST(test_recover_invalidates_current_address_tracking);
  RUN_TEST(test_probe_no_device_id_variant_invalidates_current_address_tracking);
  RUN_TEST(test_failed_random_read_invalidates_current_address_tracking);
  RUN_TEST(test_failed_write_invalidates_current_address_tracking);
  RUN_TEST(test_failed_current_address_read_invalidates_current_address_tracking);
  RUN_TEST(test_read_current_address_requires_known_pointer);
  RUN_TEST(test_read_current_address_rejects_invalid_args_without_bus_or_health);
  RUN_TEST(test_read_current_address_reads_next_byte_and_advances);
  RUN_TEST(test_read_current_address_range_reads_multiple_bytes_and_advances);
  RUN_TEST(test_64ta_current_address_respects_active_capacity);
  RUN_TEST(test_1mt_current_address_uses_dynamic_i2c_address_and_32bit_range);
  RUN_TEST(test_no_device_id_variant_probe_recover_and_id_access);
  RUN_TEST(test_verify_reports_match_and_first_mismatch);
  RUN_TEST(test_write_ack_ok_under_wp_high_but_verify_reports_mismatch);
  RUN_TEST(test_write_protect_ack_does_not_prove_persistence);
  RUN_TEST(test_write_verify_fails_when_wp_high_leaves_backing_store_unchanged);
  RUN_TEST(test_write_verify_succeeds_when_wp_disabled);
  RUN_TEST(test_write_verify_success_and_wp_high_failure);
  RUN_TEST(test_fill_verify_success_and_wp_high_failure);
  RUN_TEST(test_write_verify_reports_timeout_after_accepted_prefix_without_readback_claim);
  RUN_TEST(test_write_verify_timeout_after_possible_write_leaves_failed_chunk_unverified);
  RUN_TEST(test_fill_verify_reports_timeout_after_accepted_prefix_without_readback_claim);
  RUN_TEST(test_verify_rejects_invalid_args);
  RUN_TEST(test_random_access_write_read_verify_sequence);
  RUN_TEST(test_typed_memory_round_trips_fixed_width_values);
  RUN_TEST(test_typed_memory_rejects_cross_boundary_values);

  // High-speed and Sleep
  RUN_TEST(test_begin_rejects_invalid_high_speed_master_code);
  RUN_TEST(test_begin_rejects_sleep_recovery_below_datasheet_minimum);
  RUN_TEST(test_high_speed_rejects_unsupported_variants_without_bus_traffic);
  RUN_TEST(test_high_speed_requires_special_callback_for_supported_variant);
  RUN_TEST(test_high_speed_supported_variants_use_special_transfer_path);
  RUN_TEST(test_generic_nack_remains_failure_outside_high_speed_prefix);
  RUN_TEST(test_sleep_rejects_unsupported_variants_without_bus_traffic);
  RUN_TEST(test_sleep_requires_special_callback_for_supported_variant);
  RUN_TEST(test_wake_is_noop_when_awake_even_without_special_callback);
  RUN_TEST(test_sleep_enter_wake_gates_memory_access_and_invalidates_current_address);
  RUN_TEST(test_sleep_entry_failure_updates_health_once_and_invalidates_current_address);

  // Health tracking
  RUN_TEST(test_write_failure_transitions_to_degraded);
  RUN_TEST(test_consecutive_failures_reach_offline);
  RUN_TEST(test_success_after_degraded_returns_to_ready);

  // Transport adapter
  RUN_TEST(test_example_transport_maps_wire_errors);
  RUN_TEST(test_example_transport_supports_read_only_transactions);

  // Memory size
  RUN_TEST(test_memory_size);

  return UNITY_END();
}
