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
  uint16_t currentAddr = 0;
  bool currentAddrValid = false;
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
    bus->currentAddr = static_cast<uint16_t>(memAddr & cmd::MAX_MEM_ADDRESS);
    bus->currentAddrValid = true;
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
    bus->currentAddr = static_cast<uint16_t>(memAddr & cmd::MAX_MEM_ADDRESS);
    bus->currentAddrValid = true;
    return Status::Ok();
  }

  // Current Address Read: direct read with no address phase
  if (addr >= cmd::MIN_ADDRESS && addr <= cmd::MAX_ADDRESS && txLen == 0 && rxLen > 0) {
    for (size_t i = 0; i < rxLen; ++i) {
      rxData[i] = bus->mem[bus->currentAddr & cmd::MAX_MEM_ADDRESS];
      bus->currentAddr = static_cast<uint16_t>((bus->currentAddr + 1) & cmd::MAX_MEM_ADDRESS);
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

void test_get_settings_before_begin_reports_defaults() {
  MB85RC::MB85RC dev;
  SettingsSnapshot settings;
  Status st = dev.getSettings(settings);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(settings.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(settings.state));
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_ADDRESS, settings.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(50u, settings.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5u, settings.offlineThreshold);
  TEST_ASSERT_FALSE(settings.hasNowMsHook);
  TEST_ASSERT_FALSE(settings.currentAddressKnown);
  TEST_ASSERT_EQUAL_UINT16(0u, settings.currentAddress);
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
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_NOT_FOUND),
                          static_cast<uint8_t>(st.code));
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
  TEST_ASSERT_EQUAL_HEX8(0x50, snap.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(10u, snap.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(1u, snap.offlineThreshold);
  TEST_ASSERT_TRUE(snap.hasNowMsHook);
  TEST_ASSERT_TRUE(snap.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX16(0x0011, snap.currentAddress);
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

void test_write_wraps_across_end_of_memory() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint8_t pattern[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  TEST_ASSERT_TRUE(dev.write(0x7FFE, pattern, sizeof(pattern)).ok());

  uint8_t readBack[4] = {};
  TEST_ASSERT_TRUE(dev.read(0x7FFE, readBack, sizeof(readBack)).ok());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pattern, readBack, sizeof(pattern));
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

void test_fill_wraps_across_end_of_memory() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.fill(0x7FFD, 0x5A, 6).ok());

  uint8_t rbuf[6] = {};
  TEST_ASSERT_TRUE(dev.read(0x7FFD, rbuf, sizeof(rbuf)).ok());
  for (size_t i = 0; i < sizeof(rbuf); ++i) {
    TEST_ASSERT_EQUAL_HEX8(0x5A, rbuf[i]);
  }
}

void test_read_wraps_across_end_of_memory() {
  FakeBus bus;
  bus.mem[0x7FFF] = 0xAA;
  bus.mem[0x0000] = 0xBB;
  bus.mem[0x0001] = 0xCC;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t rbuf[3] = {};
  TEST_ASSERT_TRUE(dev.read(0x7FFF, rbuf, 3).ok());
  TEST_ASSERT_EQUAL_HEX8(0xAA, rbuf[0]);
  TEST_ASSERT_EQUAL_HEX8(0xBB, rbuf[1]);
  TEST_ASSERT_EQUAL_HEX8(0xCC, rbuf[2]);
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
  TEST_ASSERT_EQUAL_HEX16(0x1237, settings.currentAddress);

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readCurrentAddress(value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x5A, value);

  TEST_ASSERT_TRUE(dev.getSettings(settings).ok());
  TEST_ASSERT_TRUE(settings.currentAddressKnown);
  TEST_ASSERT_EQUAL_HEX16(0x1238, settings.currentAddress);
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

void test_read_current_address_requires_known_pointer() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t value = 0;
  Status st = dev.readCurrentAddress(value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
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
  TEST_ASSERT_EQUAL_HEX16(0x0012, snap.currentAddress);
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
  TEST_ASSERT_EQUAL_HEX16(0x0014, snap.currentAddress);
}

void test_verify_reports_match_and_first_mismatch() {
  FakeBus bus;
  bus.mem[0x7FFE] = 0x11;
  bus.mem[0x7FFF] = 0x22;
  bus.mem[0x0000] = 0x33;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  VerifyResult result;
  const uint8_t matchExpected[3] = {0x11, 0x22, 0x33};
  TEST_ASSERT_TRUE(dev.verify(0x7FFE, matchExpected, sizeof(matchExpected), result).ok());
  TEST_ASSERT_TRUE(result.match);

  const uint8_t mismatchExpected[3] = {0x11, 0x99, 0x33};
  TEST_ASSERT_TRUE(dev.verify(0x7FFE, mismatchExpected, sizeof(mismatchExpected), result).ok());
  TEST_ASSERT_FALSE(result.match);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(result.mismatchOffset));
  TEST_ASSERT_EQUAL_HEX8(0x99, result.expected);
  TEST_ASSERT_EQUAL_HEX8(0x22, result.actual);
}

void test_verify_rejects_invalid_args() {
  FakeBus bus;
  MB85RC::MB85RC dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  VerifyResult result;
  const uint8_t expected[1] = {0x00};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.verify(0x0000, nullptr, 1, result).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.verify(0x0000, expected, 0, result).code));
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
  RUN_TEST(test_begin_normalizes_zero_offline_threshold_in_settings);
  RUN_TEST(test_begin_detects_device_not_found);
  RUN_TEST(test_failed_begin_clears_stale_runtime_snapshot);
  RUN_TEST(test_end_transitions_to_uninit);
  RUN_TEST(test_now_ms_fallback_uses_millis_when_callback_missing);
  RUN_TEST(test_get_settings_returns_runtime_snapshot);

  // Probe
  RUN_TEST(test_probe_failure_does_not_update_health);
  RUN_TEST(test_diag_methods_reject_not_initialized);

  // Recover
  RUN_TEST(test_recover_failure_updates_health_once);
  RUN_TEST(test_recover_success_returns_ready);
  RUN_TEST(test_recover_reaches_offline_when_threshold_is_one);
  RUN_TEST(test_recover_preserves_transport_error_code);

  // Memory write/read
  RUN_TEST(test_write_read_single_byte);
  RUN_TEST(test_write_read_multi_byte);
  RUN_TEST(test_write_read_large_transfer_uses_chunking);
  RUN_TEST(test_write_rejects_invalid_address);
  RUN_TEST(test_read_rejects_invalid_address);
  RUN_TEST(test_write_not_initialized);
  RUN_TEST(test_read_not_initialized);
  RUN_TEST(test_fill_memory);
  RUN_TEST(test_read_wraps_across_end_of_memory);
  RUN_TEST(test_write_wraps_across_end_of_memory);
  RUN_TEST(test_fill_wraps_across_end_of_memory);

  // Device ID
  RUN_TEST(test_read_device_id);
  RUN_TEST(test_read_device_id_raw);
  RUN_TEST(test_current_address_requires_prior_memory_access);
  RUN_TEST(test_current_address_tracks_memory_operations_and_settings);
  RUN_TEST(test_recover_invalidates_current_address_tracking);
  RUN_TEST(test_read_current_address_requires_known_pointer);
  RUN_TEST(test_read_current_address_reads_next_byte_and_advances);
  RUN_TEST(test_read_current_address_range_reads_multiple_bytes_and_advances);
  RUN_TEST(test_verify_reports_match_and_first_mismatch);
  RUN_TEST(test_verify_rejects_invalid_args);

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
