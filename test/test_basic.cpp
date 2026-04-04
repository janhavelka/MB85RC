/// @file test_basic.cpp
/// @brief Native contract tests for MB85RC lifecycle and health behavior.

#include <unity.h>

#include "Arduino.h"
#include "Wire.h"

SerialClass Serial;
TwoWire Wire;

#include "MB85RC/MB85RC.h"
#include "common/I2cTransport.h"

using namespace MB85RC;

namespace {

struct FakeBus {
  uint32_t nowMs = 1000;
  uint32_t writeCalls = 0;
  uint32_t readCalls = 0;

  int readErrorRemaining = 0;
  int writeErrorRemaining = 0;
  Status readError = Status::Error(Err::I2C_ERROR, "forced read error", -1);
  Status writeError = Status::Error(Err::I2C_ERROR, "forced write error", -2);

  // Simulated memory: 32KB
  uint8_t mem[32768] = {};
};

/// Device ID raw bytes for MB85RC256V: Manufacturer 0x00A, Product 0x510
/// Byte 0: 0x00, Byte 1: 0xA5, Byte 2: 0x10
static constexpr uint8_t DEVID_BYTE0 = 0x00;
static constexpr uint8_t DEVID_BYTE1 = 0xA5;
static constexpr uint8_t DEVID_BYTE2 = 0x10;

Status fakeWrite(uint8_t addr, const uint8_t* data, size_t len, uint32_t, void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->writeCalls++;
  if (data == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "invalid fake write args");
  }
  if (bus->writeErrorRemaining > 0) {
    bus->writeErrorRemaining--;
    return bus->writeError;
  }

  // If writing to device address (0x50-0x57) and len >= 3, it's a memory write
  if (addr >= cmd::MIN_ADDRESS && addr <= cmd::MAX_ADDRESS && len >= 3) {
    uint16_t memAddr = static_cast<uint16_t>(((data[0] & 0x7F) << 8) | data[1]);
    for (size_t i = 2; i < len; ++i) {
      bus->mem[memAddr & cmd::MAX_MEM_ADDRESS] = data[i];
      memAddr++;
    }
  }

  return Status::Ok();
}

Status fakeWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen, uint8_t* rxData,
                     size_t rxLen, uint32_t, void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->readCalls++;
  if (txData == nullptr || txLen == 0 || (rxLen > 0 && rxData == nullptr)) {
    return Status::Error(Err::INVALID_PARAM, "invalid fake write-read args");
  }
  if (bus->readErrorRemaining > 0) {
    bus->readErrorRemaining--;
    return bus->readError;
  }

  // Device ID read: addr is 0x7C (0xF8 >> 1)
  if (addr == (cmd::DEVICE_ID_ADDR_W >> 1) && rxLen == cmd::DEVICE_ID_LEN) {
    rxData[0] = DEVID_BYTE0;
    rxData[1] = DEVID_BYTE1;
    rxData[2] = DEVID_BYTE2;
    return Status::Ok();
  }

  // Memory read: addr is device address (0x50-0x57)
  if (addr >= cmd::MIN_ADDRESS && addr <= cmd::MAX_ADDRESS && txLen == 2) {
    uint16_t memAddr = static_cast<uint16_t>(((txData[0] & 0x7F) << 8) | txData[1]);
    for (size_t i = 0; i < rxLen; ++i) {
      rxData[i] = bus->mem[memAddr & cmd::MAX_MEM_ADDRESS];
      memAddr++;
    }
    return Status::Ok();
  }

  // Default: zero fill
  for (size_t i = 0; i < rxLen; ++i) {
    rxData[i] = 0;
  }
  return Status::Ok();
}

uint32_t fakeNowMs(void* user) {
  return static_cast<FakeBus*>(user)->nowMs;
}

Config makeConfig(FakeBus& bus) {
  Config cfg;
  cfg.i2cWrite = fakeWrite;
  cfg.i2cWriteRead = fakeWriteRead;
  cfg.i2cUser = &bus;
  cfg.nowMs = fakeNowMs;
  cfg.timeUser = &bus;
  cfg.i2cTimeoutMs = 10;
  cfg.offlineThreshold = 3;
  return cfg;
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
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK), static_cast<uint8_t>(st.code));
}

void test_status_error() {
  Status st = Status::Error(Err::I2C_ERROR, "Test error", 42);
  TEST_ASSERT_FALSE(st.ok());
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
  TEST_ASSERT_EQUAL_HEX8(0x50, cfg.i2cAddress);
  TEST_ASSERT_EQUAL_UINT16(50, cfg.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5, cfg.offlineThreshold);
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
}

void test_begin_detects_device_not_found() {
  FakeBus bus;
  bus.readErrorRemaining = 1;
  MB85RC::MB85RC dev;
  Status st = dev.begin(makeConfig(bus));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_NOT_FOUND),
                          static_cast<uint8_t>(st.code));
}

void test_end_transitions_to_uninit() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  dev.end();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
}

void test_now_ms_fallback_uses_millis_when_callback_missing() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  Config cfg = makeConfig(bus);
  cfg.nowMs = nullptr;
  cfg.timeUser = nullptr;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  setMillis(4321);
  Status st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(4321u, dev.lastOkMs());
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
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_NOT_FOUND),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(beforeSuccess, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(beforeFailures, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(beforeState),
                          static_cast<uint8_t>(dev.state()));
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

// ===========================================================================
// Memory size test
// ===========================================================================

void test_memory_size() {
  TEST_ASSERT_EQUAL_UINT16(32768, MB85RC::MB85RC::memorySize());
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

  // Lifecycle
  RUN_TEST(test_begin_rejects_missing_callbacks);
  RUN_TEST(test_begin_rejects_invalid_address);
  RUN_TEST(test_begin_success_sets_ready_and_health);
  RUN_TEST(test_begin_detects_device_not_found);
  RUN_TEST(test_end_transitions_to_uninit);
  RUN_TEST(test_now_ms_fallback_uses_millis_when_callback_missing);

  // Probe
  RUN_TEST(test_probe_failure_does_not_update_health);

  // Recover
  RUN_TEST(test_recover_failure_updates_health_once);
  RUN_TEST(test_recover_success_returns_ready);

  // Memory write/read
  RUN_TEST(test_write_read_single_byte);
  RUN_TEST(test_write_read_multi_byte);
  RUN_TEST(test_write_rejects_invalid_address);
  RUN_TEST(test_read_rejects_invalid_address);
  RUN_TEST(test_write_not_initialized);
  RUN_TEST(test_read_not_initialized);
  RUN_TEST(test_fill_memory);

  // Device ID
  RUN_TEST(test_read_device_id);

  // Health tracking
  RUN_TEST(test_write_failure_transitions_to_degraded);
  RUN_TEST(test_consecutive_failures_reach_offline);
  RUN_TEST(test_success_after_degraded_returns_to_ready);

  // Transport adapter
  RUN_TEST(test_example_transport_maps_wire_errors);

  // Memory size
  RUN_TEST(test_memory_size);

  return UNITY_END();
}
