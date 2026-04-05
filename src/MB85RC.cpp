/**
 * @file MB85RC.cpp
 * @brief MB85RC256V FRAM driver implementation.
 */

#include "MB85RC/MB85RC.h"

#include <Arduino.h>
#include <cstring>
#include <limits>

namespace MB85RC {
namespace {

static constexpr size_t MAX_WRITE_BUF = cmd::MAX_WRITE_CHUNK + cmd::ADDRESS_BYTES;
static constexpr size_t FILL_CHUNK_SIZE = 64;

void parseDeviceId(const uint8_t (&raw)[cmd::DEVICE_ID_LEN], DeviceId& id) {
  id.manufacturerId = static_cast<uint16_t>((raw[0] << 4) | (raw[1] >> 4));
  id.productId = static_cast<uint16_t>(((raw[1] & 0x0F) << 8) | raw[2]);
  id.densityCode = static_cast<uint8_t>((id.productId >> 8) & 0x0F);
}

}  // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================

Status MB85RC::begin(const Config& config) {
  _config = Config{};
  _initialized = false;
  _driverState = DriverState::UNINIT;

  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _currentAddressKnown = false;
  _currentAddress = 0;

  if (config.i2cWrite == nullptr || config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C callbacks not set");
  }
  if (config.i2cTimeoutMs == 0) {
    return Status::Error(Err::INVALID_CONFIG, "I2C timeout must be > 0");
  }
  if (config.i2cAddress < cmd::MIN_ADDRESS || config.i2cAddress > cmd::MAX_ADDRESS) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid I2C address (must be 0x50-0x57)");
  }

  _config = config;
  if (_config.offlineThreshold == 0) {
    _config.offlineThreshold = 1;
  }

  // Verify device identity via Device ID read (uses raw path — not yet initialized)
  DeviceId id;
  Status st = _readDeviceIdRaw(id);
  if (!st.ok()) {
    return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
  }
  if (id.manufacturerId != cmd::MANUFACTURER_ID || id.productId != cmd::PRODUCT_ID) {
    return Status::Error(Err::DEVICE_ID_MISMATCH, "Device ID mismatch",
                         static_cast<int32_t>((id.manufacturerId << 12) | id.productId));
  }

  _initialized = true;
  _driverState = DriverState::READY;

  return Status::Ok();
}

void MB85RC::tick(uint32_t nowMs) {
  (void)nowMs;
  // FRAM has no write delays or async operations.
  // Reserved for future use (e.g., periodic health checks).
}

void MB85RC::end() {
  _config = Config{};
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _currentAddressKnown = false;
  _currentAddress = 0;
}

Status MB85RC::getSettings(SettingsSnapshot& out) const {
  out.initialized = _initialized;
  out.state = _driverState;
  out.i2cAddress = _config.i2cAddress;
  out.i2cTimeoutMs = _config.i2cTimeoutMs;
  out.offlineThreshold = _config.offlineThreshold;
  out.hasNowMsHook = (_config.nowMs != nullptr);
  out.currentAddressKnown = _currentAddressKnown;
  out.currentAddress = _currentAddress;
  return Status::Ok();
}

// ===========================================================================
// Diagnostics
// ===========================================================================

Status MB85RC::probe() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  DeviceId id;
  Status st = _readDeviceIdRaw(id);
  if (!st.ok()) {
    return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
  }
  if (id.manufacturerId != cmd::MANUFACTURER_ID || id.productId != cmd::PRODUCT_ID) {
    return Status::Error(Err::DEVICE_ID_MISMATCH, "Device ID mismatch",
                         static_cast<int32_t>((id.manufacturerId << 12) | id.productId));
  }
  return Status::Ok();
}

Status MB85RC::recover() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  // Use tracked path so failures update health counters
  DeviceId id;
  Status st = _readDeviceIdTracked(id);
  if (!st.ok()) {
    _currentAddressKnown = false;
    _currentAddress = 0;
    return st;
  }
  if (id.manufacturerId != cmd::MANUFACTURER_ID || id.productId != cmd::PRODUCT_ID) {
    _currentAddressKnown = false;
    _currentAddress = 0;
    return Status::Error(Err::DEVICE_ID_MISMATCH, "Device ID mismatch",
                         static_cast<int32_t>((id.manufacturerId << 12) | id.productId));
  }

  _currentAddressKnown = false;
  _currentAddress = 0;
  return Status::Ok();
}

// ===========================================================================
// Memory Read API
// ===========================================================================

Status MB85RC::readByte(uint16_t address, uint8_t& value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_isValidAddress(address)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Address exceeds 0x7FFF", address);
  }

  return _readMemory(address, &value, 1);
}

Status MB85RC::read(uint16_t address, uint8_t* buf, size_t len) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }
  if (!_isValidAddress(address)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Address exceeds 0x7FFF", address);
  }

  // Break large reads into chunks to stay within I2C buffer limits
  size_t offset = 0;
  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > cmd::MAX_READ_CHUNK) {
      chunk = cmd::MAX_READ_CHUNK;
    }

    // Compute effective address with rollover
    uint16_t addr = _wrapAddress(address, offset);

    Status st = _readMemory(addr, buf + offset, chunk);
    if (!st.ok()) {
      return st;
    }
    offset += chunk;
  }

  return Status::Ok();
}

// ===========================================================================
// Memory Write API
// ===========================================================================

Status MB85RC::writeByte(uint16_t address, uint8_t value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_isValidAddress(address)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Address exceeds 0x7FFF", address);
  }

  return _writeMemory(address, &value, 1);
}

Status MB85RC::write(uint16_t address, const uint8_t* buf, size_t len) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid write buffer");
  }
  if (!_isValidAddress(address)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Address exceeds 0x7FFF", address);
  }

  // Break large writes into chunks to stay within I2C buffer limits
  size_t offset = 0;
  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > cmd::MAX_WRITE_CHUNK) {
      chunk = cmd::MAX_WRITE_CHUNK;
    }

    // Compute effective address with rollover
    uint16_t addr = _wrapAddress(address, offset);

    Status st = _writeMemory(addr, buf + offset, chunk);
    if (!st.ok()) {
      return st;
    }
    offset += chunk;
  }

  return Status::Ok();
}

Status MB85RC::fill(uint16_t address, uint8_t value, size_t len) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Fill length must be > 0");
  }
  if (!_isValidAddress(address)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Address exceeds 0x7FFF", address);
  }

  uint8_t chunk[FILL_CHUNK_SIZE];
  std::memset(chunk, value, sizeof(chunk));

  size_t remaining = len;
  size_t offset = 0;
  while (remaining > 0) {
    size_t toWrite = remaining;
    if (toWrite > FILL_CHUNK_SIZE) {
      toWrite = FILL_CHUNK_SIZE;
    }

    uint16_t addr = _wrapAddress(address, offset);

    Status st = _writeMemory(addr, chunk, toWrite);
    if (!st.ok()) {
      return st;
    }
    offset += toWrite;
    remaining -= toWrite;
  }

  return Status::Ok();
}

// ===========================================================================
// Device Information
// ===========================================================================

Status MB85RC::readDeviceId(DeviceId& id) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  return _readDeviceIdTracked(id);
}

Status MB85RC::readDeviceIdRaw(DeviceIdRaw& raw) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  return _readDeviceIdBytesTracked(raw);
}

Status MB85RC::readCurrentAddress(uint8_t& value) {
  return readCurrentAddress(&value, 1);
}

Status MB85RC::readCurrentAddress(uint8_t* buf, size_t len) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid current-address buffer");
  }
  if (!_currentAddressKnown) {
    return Status::Error(Err::INVALID_PARAM,
                         "Current address undefined until a memory access succeeds");
  }

  for (size_t offset = 0; offset < len; ++offset) {
    Status st = _i2cWriteReadTrackedAddr(_config.i2cAddress, nullptr, 0, &buf[offset], 1);
    if (!st.ok()) {
      if (st.code != Err::INVALID_CONFIG && st.code != Err::INVALID_PARAM) {
        _currentAddressKnown = false;
      }
      return st;
    }

    _setCurrentAddressAfterTransfer(_currentAddress, 1);
  }

  return Status::Ok();
}

Status MB85RC::verify(uint16_t address, const uint8_t* expected, size_t len, VerifyResult& out) {
  out.match = false;
  out.mismatchOffset = 0;
  out.expected = 0;
  out.actual = 0;

  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (expected == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid verify buffer");
  }
  if (!_isValidAddress(address)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Address exceeds 0x7FFF", address);
  }

  uint8_t readBuf[cmd::MAX_READ_CHUNK];
  size_t offset = 0;
  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > sizeof(readBuf)) {
      chunk = sizeof(readBuf);
    }

    const uint16_t chunkAddr = _wrapAddress(address, offset);
    Status st = _readMemory(chunkAddr, readBuf, chunk);
    if (!st.ok()) {
      return st;
    }

    for (size_t i = 0; i < chunk; ++i) {
      if (readBuf[i] != expected[offset + i]) {
        out.match = false;
        out.mismatchOffset = offset + i;
        out.expected = expected[offset + i];
        out.actual = readBuf[i];
        return Status::Ok();
      }
    }

    offset += chunk;
  }

  out.match = true;
  return Status::Ok();
}

// ===========================================================================
// Transport Wrappers
// ===========================================================================

Status MB85RC::_i2cWriteReadRaw(uint8_t addr, const uint8_t* txBuf, size_t txLen,
                                uint8_t* rxBuf, size_t rxLen) {
  if ((txLen > 0 && txBuf == nullptr) || (rxLen > 0 && rxBuf == nullptr) ||
      (txLen == 0 && rxLen == 0)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  if (_config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write-read not set");
  }
  return _config.i2cWriteRead(addr, txBuf, txLen, rxBuf, rxLen,
                              _config.i2cTimeoutMs, _config.i2cUser);
}

Status MB85RC::_i2cWriteRaw(uint8_t addr, const uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  if (_config.i2cWrite == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write not set");
  }
  return _config.i2cWrite(addr, buf, len, _config.i2cTimeoutMs, _config.i2cUser);
}

Status MB85RC::_i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                                    uint8_t* rxBuf, size_t rxLen) {
  return _i2cWriteReadTrackedAddr(_config.i2cAddress, txBuf, txLen, rxBuf, rxLen);
}

Status MB85RC::_i2cWriteReadTrackedAddr(uint8_t addr, const uint8_t* txBuf, size_t txLen,
                                        uint8_t* rxBuf, size_t rxLen) {
  if ((txLen > 0 && txBuf == nullptr) || (rxLen > 0 && rxBuf == nullptr) ||
      (txLen == 0 && rxLen == 0)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status st = _i2cWriteReadRaw(addr, txBuf, txLen, rxBuf, rxLen);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

Status MB85RC::_i2cWriteTracked(const uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status st = _i2cWriteRaw(_config.i2cAddress, buf, len);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

// ===========================================================================
// Internal Helpers
// ===========================================================================

Status MB85RC::_readMemory(uint16_t address, uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }
  if (len > cmd::MAX_READ_CHUNK) {
    return Status::Error(Err::INVALID_PARAM, "Read chunk too large");
  }

  // Random Read: [S] [addr W] [addrHi] [addrLo] [Sr] [addr R] [data...] [NACK] [P]
  // The transport write-read callback handles the repeated start internally.
  uint8_t addrBuf[cmd::ADDRESS_BYTES];
  addrBuf[0] = static_cast<uint8_t>((address >> 8) & cmd::ADDR_HIGH_MASK);
  addrBuf[1] = static_cast<uint8_t>(address & 0xFF);

  Status st = _i2cWriteReadTracked(addrBuf, cmd::ADDRESS_BYTES, buf, len);
  if (st.ok()) {
    _setCurrentAddressAfterTransfer(address, len);
  } else if (st.code != Err::INVALID_CONFIG && st.code != Err::INVALID_PARAM) {
    _currentAddressKnown = false;
  }
  return st;
}

Status MB85RC::_writeMemory(uint16_t address, const uint8_t* buf, size_t len) {
  // Byte/Sequential Write: [S] [addr W] [addrHi] [addrLo] [data...] [P]
  // No write delay needed - FRAM writes immediately.
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid write buffer");
  }
  if (len > cmd::MAX_WRITE_CHUNK) {
    return Status::Error(Err::INVALID_PARAM, "Write chunk too large");
  }

  uint8_t payload[MAX_WRITE_BUF];
  payload[0] = static_cast<uint8_t>((address >> 8) & cmd::ADDR_HIGH_MASK);
  payload[1] = static_cast<uint8_t>(address & 0xFF);
  std::memcpy(&payload[cmd::ADDRESS_BYTES], buf, len);

  Status st = _i2cWriteTracked(payload, cmd::ADDRESS_BYTES + len);
  if (st.ok()) {
    _setCurrentAddressAfterTransfer(address, len);
  } else if (st.code != Err::INVALID_CONFIG && st.code != Err::INVALID_PARAM) {
    _currentAddressKnown = false;
  }
  return st;
}

Status MB85RC::_readDeviceIdRaw(DeviceId& id) {
  DeviceIdRaw raw;
  Status st = _readDeviceIdBytesRaw(raw);
  if (!st.ok()) {
    return st;
  }

  parseDeviceId(raw.bytes, id);
  return Status::Ok();
}

Status MB85RC::_readDeviceIdTracked(DeviceId& id) {
  DeviceIdRaw raw;
  Status st = _readDeviceIdBytesTracked(raw);
  if (!st.ok()) {
    return st;
  }

  parseDeviceId(raw.bytes, id);
  return Status::Ok();
}

Status MB85RC::_readDeviceIdBytesRaw(DeviceIdRaw& raw) {
  // Device ID Read uses reserved I2C addresses:
  //   [S] [0xF8] [ACK] [device_addr_word] [ACK]
  //   [Sr] [0xF9] [ACK] [byte1] [ACK] [byte2] [ACK] [byte3] [NACK] [P]
  //
  // The device address word sent after 0xF8 tells the device which
  // chip on the bus should respond.  R/W bit is don't-care.
  uint8_t txBuf[1] = { static_cast<uint8_t>(_config.i2cAddress << 1) };

  // Write phase to reserved address 0xF8, then read phase from 0xF9
  // We use the raw write-read with the reserved Device ID addresses.
  Status st = _i2cWriteReadRaw(cmd::DEVICE_ID_ADDR_W >> 1, txBuf, 1,
                               raw.bytes, cmd::DEVICE_ID_LEN);
  if (!st.ok()) {
    return st;
  }

  return Status::Ok();
}

Status MB85RC::_readDeviceIdBytesTracked(DeviceIdRaw& raw) {
  uint8_t txBuf[1] = { static_cast<uint8_t>(_config.i2cAddress << 1) };

  Status st = _i2cWriteReadTrackedAddr(cmd::DEVICE_ID_ADDR_W >> 1, txBuf, 1,
                                       raw.bytes, cmd::DEVICE_ID_LEN);
  if (!st.ok()) {
    return st;
  }

  return Status::Ok();
}

bool MB85RC::_isValidAddress(uint16_t address) {
  return address <= cmd::MAX_MEM_ADDRESS;
}

uint16_t MB85RC::_wrapAddress(uint16_t address, size_t offset) {
  return static_cast<uint16_t>((address + offset) & cmd::MAX_MEM_ADDRESS);
}

void MB85RC::_setCurrentAddressAfterTransfer(uint16_t address, size_t len) {
  if (len == 0) {
    return;
  }

  _currentAddressKnown = true;
  _currentAddress = _wrapAddress(address, len);
}

// ===========================================================================
// Health Management
// ===========================================================================

Status MB85RC::_updateHealth(const Status& st) {
  if (!_initialized) {
    return st;
  }

  const uint32_t now = _nowMs();
  const uint32_t maxU32 = std::numeric_limits<uint32_t>::max();
  const uint8_t maxU8 = std::numeric_limits<uint8_t>::max();

  if (st.ok()) {
    _lastOkMs = now;
    if (_totalSuccess < maxU32) {
      _totalSuccess++;
    }
    _consecutiveFailures = 0;
    _driverState = DriverState::READY;
    return st;
  }

  _lastError = st;
  _lastErrorMs = now;
  if (_totalFailures < maxU32) {
    _totalFailures++;
  }
  if (_consecutiveFailures < maxU8) {
    _consecutiveFailures++;
  }

  if (_consecutiveFailures >= _config.offlineThreshold) {
    _driverState = DriverState::OFFLINE;
  } else {
    _driverState = DriverState::DEGRADED;
  }

  return st;
}

uint32_t MB85RC::_nowMs() const {
  if (_config.nowMs != nullptr) {
    return _config.nowMs(_config.timeUser);
  }
  return millis();
}

}  // namespace MB85RC
