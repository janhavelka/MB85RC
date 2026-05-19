/**
 * @file MB85RC.cpp
 * @brief MB85RC-family FRAM driver implementation.
 */

#include "MB85RC/MB85RC.h"

#include <cstring>
#include <limits>

#if defined(ARDUINO)
#define MB85RC_HAS_ARDUINO_TIME 1
#elif !defined(ESP_PLATFORM) && defined(__has_include)
#if __has_include(<Arduino.h>)
#define MB85RC_HAS_ARDUINO_TIME 1
#endif
#endif

#ifndef MB85RC_HAS_ARDUINO_TIME
#define MB85RC_HAS_ARDUINO_TIME 0
#endif

#if MB85RC_HAS_ARDUINO_TIME
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include <esp_timer.h>
#define MB85RC_HAS_IDF_TIME 1
#endif

#ifndef MB85RC_HAS_IDF_TIME
#define MB85RC_HAS_IDF_TIME 0
#endif

namespace MB85RC {
namespace {

static constexpr size_t MAX_WRITE_BUF = cmd::MAX_WRITE_CHUNK + cmd::ADDRESS_BYTES;
static constexpr size_t FILL_CHUNK_SIZE = 64;

void parseDeviceId(const uint8_t (&raw)[cmd::DEVICE_ID_LEN], DeviceId& id) {
  id.manufacturerId = static_cast<uint16_t>((raw[0] << 4) | (raw[1] >> 4));
  id.productId = static_cast<uint16_t>(((raw[1] & 0x0F) << 8) | raw[2]);
  id.densityCode = static_cast<uint8_t>((id.productId >> 8) & 0x0F);
}

const cmd::VariantInfo* variantForExpected(DeviceVariant expected) {
  const char* name = nullptr;
  switch (expected) {
    case DeviceVariant::MB85RC04V:
      name = "MB85RC04V";
      break;
    case DeviceVariant::MB85RC16V:
      name = "MB85RC16V";
      break;
    case DeviceVariant::MB85RC64TA:
      name = "MB85RC64TA";
      break;
    case DeviceVariant::MB85RC256V:
      name = "MB85RC256V";
      break;
    case DeviceVariant::MB85RC512T:
      name = "MB85RC512T";
      break;
    case DeviceVariant::MB85RC1MT:
      name = "MB85RC1MT";
      break;
    case DeviceVariant::AUTO:
    default:
      return nullptr;
  }

  for (size_t i = 0; i < cmd::VARIANT_COUNT; ++i) {
    if (std::strcmp(cmd::KNOWN_VARIANTS[i].name, name) == 0) {
      return &cmd::KNOWN_VARIANTS[i];
    }
  }
  return nullptr;
}

bool isSupportedRuntimeVariant(const cmd::VariantInfo& variant) {
  if (!variant.supportedByDriver || variant.memoryBytes == 0UL ||
      variant.memoryBytes > cmd::MEMORY_SIZE_MB85RC1MT) {
    return false;
  }

  switch (variant.addressModel) {
    case cmd::AddressModel::TWO_BYTE_ADDRESS_PINS:
    case cmd::AddressModel::TWO_BYTE_A16_IN_DEVICE_ADDRESS:
    case cmd::AddressModel::ONE_BYTE_UPPER_BITS_IN_DEVICE_ADDRESS:
    case cmd::AddressModel::ONE_BYTE_A8_IN_DEVICE_ADDRESS:
      return true;
    default:
      return false;
  }
}

int32_t deviceIdDetail(const DeviceId& id) {
  return static_cast<int32_t>((id.manufacturerId << 12) | id.productId);
}

class ScopedOfflineI2cAllowance {
public:
  explicit ScopedOfflineI2cAllowance(bool& flag, bool allow) : _flag(flag), _old(flag) {
    _flag = allow;
  }

  ~ScopedOfflineI2cAllowance() {
    _flag = _old;
  }

  ScopedOfflineI2cAllowance(const ScopedOfflineI2cAllowance&) = delete;
  ScopedOfflineI2cAllowance& operator=(const ScopedOfflineI2cAllowance&) = delete;

private:
  bool& _flag;
  bool _old;
};

}  // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================

Status MB85RC::begin(const Config& config) {
  _config = Config{};
  _variant = nullptr;
  _deviceId = DeviceId{};
  _initialized = false;
  _driverState = DriverState::UNINIT;

  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _allowOfflineI2c = false;
  _currentAddressKnown = false;
  _currentAddress = 0;

  auto resetAfterFailedBegin = [this](Status failure) -> Status {
    _config = Config{};
    _variant = nullptr;
    _deviceId = DeviceId{};
    _initialized = false;
    _driverState = DriverState::UNINIT;
    _lastOkMs = 0;
    _lastErrorMs = 0;
    _lastError = Status::Ok();
    _consecutiveFailures = 0;
    _totalFailures = 0;
    _totalSuccess = 0;
    _allowOfflineI2c = false;
    _currentAddressKnown = false;
    _currentAddress = 0;
    return failure;
  };

  if (config.i2cWrite == nullptr || config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C callbacks not set");
  }
  if (config.i2cTimeoutMs == 0) {
    return Status::Error(Err::INVALID_CONFIG, "I2C timeout must be > 0");
  }
  if (config.i2cAddress < cmd::MIN_ADDRESS || config.i2cAddress > cmd::MAX_ADDRESS) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid I2C address (must be 0x50-0x57)");
  }
  if (config.expectedVariant != DeviceVariant::AUTO &&
      variantForExpected(config.expectedVariant) == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "Unsupported expected variant");
  }
  const cmd::VariantInfo* explicitVariant = variantForExpected(config.expectedVariant);
  if (explicitVariant != nullptr && !isSupportedRuntimeVariant(*explicitVariant)) {
    return Status::Error(Err::INVALID_CONFIG, "Expected variant not supported by driver");
  }

  _config = config;
  if (_config.offlineThreshold == 0) {
    _config.offlineThreshold = 1;
  }

  // Verify device identity via Device ID read (uses raw path — not yet initialized)
  Status st = Status::Ok();
  if (explicitVariant != nullptr && !explicitVariant->hasDeviceId) {
    _variant = explicitVariant;
    _deviceId = DeviceId{};

    uint8_t scratch = 0;
    st = _readMemoryRaw(0, &scratch, 1);
    if (!st.ok()) {
      return resetAfterFailedBegin(
          Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail));
    }
    _currentAddressKnown = false;
    _currentAddress = 0;
  } else {
    DeviceId id;
    st = _readDeviceIdRaw(id);
    if (!st.ok()) {
      return resetAfterFailedBegin(
          Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail));
    }
    st = _selectVariant(_config.expectedVariant, id);
    if (!st.ok()) {
      return resetAfterFailedBegin(st);
    }
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
  _variant = nullptr;
  _deviceId = DeviceId{};
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _allowOfflineI2c = false;
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
  out.expectedVariant = _config.expectedVariant;
  out.activeVariant = _activeVariantEnum();
  out.variantKnown = (_variant != nullptr);
  out.variantName = variantName();
  out.manufacturerId = _deviceId.manufacturerId;
  out.productId = _deviceId.productId;
  out.densityCode = _deviceId.densityCode;
  out.capacityBytes = capacityBytes();
  out.maxAddress = maxAddress();
  out.currentAddressKnown = _currentAddressKnown;
  out.currentAddress = _currentAddress;
  return Status::Ok();
}

uint32_t MB85RC::capacityBytes() const {
  return (_variant != nullptr) ? _variant->memoryBytes
                               : static_cast<uint32_t>(cmd::MEMORY_SIZE_MB85RC256V);
}

uint32_t MB85RC::maxAddress() const {
  return (_variant != nullptr) ? cmd::maxAddressForVariant(*_variant)
                               : cmd::MAX_MEM_ADDRESS_MB85RC256V;
}

// ===========================================================================
// Diagnostics
// ===========================================================================

Status MB85RC::probe() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_variant != nullptr && !_variant->hasDeviceId) {
    uint8_t scratch = 0;
    Status st = _readMemoryRaw(0, &scratch, 1);
    if (!st.ok()) {
      return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
    }
    return Status::Ok();
  }

  DeviceId id;
  Status st = _readDeviceIdRaw(id);
  if (!st.ok()) {
    return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
  }
  return _validateActiveDeviceId(id);
}

Status MB85RC::recover() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  const bool startedOffline = (_driverState == DriverState::OFFLINE);
  ScopedOfflineI2cAllowance allowOfflineI2c(_allowOfflineI2c, true);
  Status result = [&]() -> Status {
    if (_variant != nullptr && !_variant->hasDeviceId) {
      uint8_t scratch = 0;
      Status st = _readMemory(0, &scratch, 1);
      _currentAddressKnown = false;
      _currentAddress = 0;
      return st;
    }

    // Use tracked path so failures update health counters.
    DeviceId id;
    Status st = _readDeviceIdTracked(id);
    if (!st.ok()) {
      _currentAddressKnown = false;
      _currentAddress = 0;
      return st;
    }
    st = _validateActiveDeviceId(id);
    if (!st.ok()) {
      _currentAddressKnown = false;
      _currentAddress = 0;
      return _recordFailure(st);
    }

    _deviceId = id;
    _currentAddressKnown = false;
    _currentAddress = 0;
    return Status::Ok();
  }();
  if (startedOffline && !result.ok() && !result.inProgress()) {
    _reassertOfflineLatch();
  }
  return result;
}

// ===========================================================================
// Memory Read API
// ===========================================================================

Status MB85RC::readByte(uint32_t address, uint8_t& value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_isValidAddress(address)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Address exceeds active capacity", address);
  }

  return _readMemory(address, &value, 1);
}

Status MB85RC::read(uint32_t address, uint8_t* buf, size_t len) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }
  if (!_fitsRange(address, len)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Read range exceeds active capacity", address);
  }

  // Break large reads into chunks to stay within I2C buffer limits
  size_t offset = 0;
  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > cmd::MAX_READ_CHUNK) {
      chunk = cmd::MAX_READ_CHUNK;
    }

    uint32_t addr = address + static_cast<uint32_t>(offset);

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

Status MB85RC::writeByte(uint32_t address, uint8_t value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_isValidAddress(address)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Address exceeds active capacity", address);
  }

  return _writeMemory(address, &value, 1);
}

Status MB85RC::write(uint32_t address, const uint8_t* buf, size_t len) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid write buffer");
  }
  if (!_fitsRange(address, len)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Write range exceeds active capacity", address);
  }

  // Break large writes into chunks to stay within I2C buffer limits
  size_t offset = 0;
  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > cmd::MAX_WRITE_CHUNK) {
      chunk = cmd::MAX_WRITE_CHUNK;
    }

    uint32_t addr = address + static_cast<uint32_t>(offset);

    Status st = _writeMemory(addr, buf + offset, chunk);
    if (!st.ok()) {
      return st;
    }
    offset += chunk;
  }

  return Status::Ok();
}

Status MB85RC::fill(uint32_t address, uint8_t value, size_t len) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Fill length must be > 0");
  }
  if (!_fitsRange(address, len)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Fill range exceeds active capacity", address);
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

    uint32_t addr = address + static_cast<uint32_t>(offset);

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
  if (_variant != nullptr && !_variant->hasDeviceId) {
    return Status::Error(Err::INVALID_PARAM, "Active variant has no Device ID");
  }

  return _readDeviceIdTracked(id);
}

Status MB85RC::readDeviceIdRaw(DeviceIdRaw& raw) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_variant != nullptr && !_variant->hasDeviceId) {
    return Status::Error(Err::INVALID_PARAM, "Active variant has no Device ID");
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
  if (!_fitsRange(_currentAddress, len)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE,
                         "Current-address range exceeds active capacity",
                         _currentAddress);
  }

  for (size_t offset = 0; offset < len; ++offset) {
    EncodedMemoryAddress enc;
    Status st = _encodeMemoryAddress(_currentAddress, enc);
    if (!st.ok()) {
      return st;
    }
    st = _i2cWriteReadTrackedAddr(enc.i2cAddress, nullptr, 0, &buf[offset], 1);
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

Status MB85RC::verify(uint32_t address, const uint8_t* expected, size_t len, VerifyResult& out) {
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
  if (!_fitsRange(address, len)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Verify range exceeds active capacity", address);
  }

  uint8_t readBuf[cmd::MAX_READ_CHUNK];
  size_t offset = 0;
  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > sizeof(readBuf)) {
      chunk = sizeof(readBuf);
    }

    const uint32_t chunkAddr = address + static_cast<uint32_t>(offset);
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
  if (_initialized && _driverState == DriverState::OFFLINE && !_allowOfflineI2c) {
    return Status::Error(Err::BUSY, "Driver is offline; call recover()");
  }

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
  return _i2cWriteTrackedAddr(_config.i2cAddress, buf, len);
}

Status MB85RC::_i2cWriteTrackedAddr(uint8_t addr, const uint8_t* buf, size_t len) {
  if (_initialized && _driverState == DriverState::OFFLINE && !_allowOfflineI2c) {
    return Status::Error(Err::BUSY, "Driver is offline; call recover()");
  }

  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status st = _i2cWriteRaw(addr, buf, len);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

// ===========================================================================
// Internal Helpers
// ===========================================================================

Status MB85RC::_readMemory(uint32_t address, uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }
  if (len > cmd::MAX_READ_CHUNK) {
    return Status::Error(Err::INVALID_PARAM, "Read chunk too large");
  }

  // Random Read: [S] [addr W] [addrHi] [addrLo] [Sr] [addr R] [data...] [NACK] [P]
  // The transport write-read callback handles the repeated start internally.
  EncodedMemoryAddress enc;
  Status st = _encodeMemoryAddress(address, enc);
  if (!st.ok()) {
    return st;
  }

  st = _i2cWriteReadTrackedAddr(enc.i2cAddress, enc.bytes, enc.len, buf, len);
  if (st.ok()) {
    _setCurrentAddressAfterTransfer(address, len);
  } else if (st.code != Err::INVALID_CONFIG && st.code != Err::INVALID_PARAM) {
    _currentAddressKnown = false;
  }
  return st;
}

Status MB85RC::_readMemoryRaw(uint32_t address, uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }
  if (len > cmd::MAX_READ_CHUNK) {
    return Status::Error(Err::INVALID_PARAM, "Read chunk too large");
  }

  EncodedMemoryAddress enc;
  Status st = _encodeMemoryAddress(address, enc);
  if (!st.ok()) {
    return st;
  }

  return _i2cWriteReadRaw(enc.i2cAddress, enc.bytes, enc.len, buf, len);
}

Status MB85RC::_writeMemory(uint32_t address, const uint8_t* buf, size_t len) {
  // Byte/Sequential Write: [S] [addr W] [addrHi] [addrLo] [data...] [P]
  // No write delay needed - FRAM writes immediately.
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid write buffer");
  }
  if (len > cmd::MAX_WRITE_CHUNK) {
    return Status::Error(Err::INVALID_PARAM, "Write chunk too large");
  }

  uint8_t payload[MAX_WRITE_BUF];
  EncodedMemoryAddress enc;
  Status st = _encodeMemoryAddress(address, enc);
  if (!st.ok()) {
    return st;
  }

  for (size_t i = 0; i < enc.len; ++i) {
    payload[i] = enc.bytes[i];
  }
  std::memcpy(&payload[enc.len], buf, len);

  st = _i2cWriteTrackedAddr(enc.i2cAddress, payload, enc.len + len);
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

bool MB85RC::_isValidAddress(uint32_t address) const {
  return address <= maxAddress();
}

bool MB85RC::_fitsRange(uint32_t address, size_t len) const {
  if (len == 0U || !_isValidAddress(address)) {
    return false;
  }
  const uint32_t capacity = capacityBytes();
  if (address >= capacity) {
    return false;
  }
  const size_t remaining = static_cast<size_t>(capacity - address);
  return len <= remaining;
}

uint32_t MB85RC::_wrapAddress(uint32_t address, size_t offset) const {
  const uint32_t capacity = capacityBytes();
  if (capacity == 0UL) {
    return 0U;
  }
  return (address + static_cast<uint32_t>(offset)) % capacity;
}

Status MB85RC::_encodeMemoryAddress(uint32_t address, EncodedMemoryAddress& out) const {
  if (_variant == nullptr || !isSupportedRuntimeVariant(*_variant)) {
    return Status::Error(Err::INVALID_CONFIG, "Unsupported active variant");
  }
  if (!_isValidAddress(address)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Address exceeds active capacity",
                         static_cast<int32_t>(address));
  }

  out = EncodedMemoryAddress{};
  out.i2cAddress = _config.i2cAddress;

  switch (_variant->addressModel) {
    case cmd::AddressModel::TWO_BYTE_ADDRESS_PINS:
      out.len = 2;
      out.bytes[0] = static_cast<uint8_t>((address >> 8) &
                                          cmd::addressHighMaskForVariant(*_variant));
      out.bytes[1] = static_cast<uint8_t>(address & 0xFFU);
      break;

    case cmd::AddressModel::TWO_BYTE_A16_IN_DEVICE_ADDRESS:
      out.i2cAddress = static_cast<uint8_t>((_config.i2cAddress & 0xFEU) |
                                            ((address >> 16) & 0x01U));
      out.len = 2;
      out.bytes[0] = static_cast<uint8_t>((address >> 8) & 0xFFU);
      out.bytes[1] = static_cast<uint8_t>(address & 0xFFU);
      break;

    case cmd::AddressModel::ONE_BYTE_UPPER_BITS_IN_DEVICE_ADDRESS:
      out.i2cAddress = static_cast<uint8_t>((_config.i2cAddress & 0xF8U) |
                                            ((address >> 8) & 0x07U));
      out.len = 1;
      out.bytes[0] = static_cast<uint8_t>(address & 0xFFU);
      break;

    case cmd::AddressModel::ONE_BYTE_A8_IN_DEVICE_ADDRESS:
      out.i2cAddress = static_cast<uint8_t>((_config.i2cAddress & 0xFEU) |
                                            ((address >> 8) & 0x01U));
      out.len = 1;
      out.bytes[0] = static_cast<uint8_t>(address & 0xFFU);
      break;

    default:
      return Status::Error(Err::INVALID_CONFIG, "Unsupported active address model");
  }
  return Status::Ok();
}

Status MB85RC::_selectVariant(DeviceVariant expected, const DeviceId& id) {
  if (id.manufacturerId != cmd::MANUFACTURER_ID) {
    return Status::Error(Err::DEVICE_ID_MISMATCH, "Device ID manufacturer mismatch",
                         deviceIdDetail(id));
  }

  const cmd::VariantInfo* selected = nullptr;
  if (expected == DeviceVariant::AUTO) {
    selected = cmd::findVariantByProductId(id.productId);
    if (selected == nullptr) {
      return Status::Error(Err::DEVICE_ID_MISMATCH, "Unknown Device ID product",
                           deviceIdDetail(id));
    }
  } else {
    selected = variantForExpected(expected);
    if (selected == nullptr) {
      return Status::Error(Err::INVALID_CONFIG, "Unsupported expected variant");
    }
    if (!selected->hasDeviceId) {
      return Status::Error(Err::INVALID_CONFIG, "Expected variant has no Device ID");
    }
    if (id.productId != selected->productId) {
      return Status::Error(Err::DEVICE_ID_MISMATCH, "Device ID product mismatch",
                           deviceIdDetail(id));
    }
  }

  if (!isSupportedRuntimeVariant(*selected)) {
    return Status::Error(Err::DEVICE_ID_MISMATCH, "Unsupported Device ID product",
                         deviceIdDetail(id));
  }

  _variant = selected;
  _deviceId = id;
  return Status::Ok();
}

Status MB85RC::_validateActiveDeviceId(const DeviceId& id) const {
  if (_variant == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "Active variant not selected");
  }
  if (!_variant->hasDeviceId) {
    return Status::Error(Err::INVALID_PARAM, "Active variant has no Device ID");
  }
  if (id.manufacturerId != cmd::MANUFACTURER_ID) {
    return Status::Error(Err::DEVICE_ID_MISMATCH, "Device ID manufacturer mismatch",
                         deviceIdDetail(id));
  }
  if (id.productId != _variant->productId) {
    return Status::Error(Err::DEVICE_ID_MISMATCH, "Device ID product mismatch",
                         deviceIdDetail(id));
  }
  return Status::Ok();
}

DeviceVariant MB85RC::_activeVariantEnum() const {
  if (_variant == nullptr) {
    return _config.expectedVariant;
  }
  if (std::strcmp(_variant->name, "MB85RC04V") == 0) {
    return DeviceVariant::MB85RC04V;
  }
  if (std::strcmp(_variant->name, "MB85RC16V") == 0) {
    return DeviceVariant::MB85RC16V;
  }
  if (std::strcmp(_variant->name, "MB85RC64TA") == 0) {
    return DeviceVariant::MB85RC64TA;
  }
  if (std::strcmp(_variant->name, "MB85RC256V") == 0) {
    return DeviceVariant::MB85RC256V;
  }
  if (std::strcmp(_variant->name, "MB85RC512T") == 0) {
    return DeviceVariant::MB85RC512T;
  }
  if (std::strcmp(_variant->name, "MB85RC1MT") == 0) {
    return DeviceVariant::MB85RC1MT;
  }
  return DeviceVariant::AUTO;
}

void MB85RC::_setCurrentAddressAfterTransfer(uint32_t address, size_t len) {
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
  if (st.inProgress()) {
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

Status MB85RC::_recordFailure(const Status& st) {
  if (!_initialized || st.ok() || st.inProgress()) {
    return st;
  }

  const uint32_t now = _nowMs();
  const uint32_t maxU32 = std::numeric_limits<uint32_t>::max();
  const uint8_t maxU8 = std::numeric_limits<uint8_t>::max();

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

void MB85RC::_reassertOfflineLatch() {
  _driverState = DriverState::OFFLINE;
  const uint8_t threshold = _config.offlineThreshold == 0 ? 1 : _config.offlineThreshold;
  if (_consecutiveFailures < threshold) {
    _consecutiveFailures = threshold;
  }
}

uint32_t MB85RC::_nowMs() const {
  if (_config.nowMs != nullptr) {
    return _config.nowMs(_config.timeUser);
  }
#if MB85RC_HAS_ARDUINO_TIME
  return millis();
#elif MB85RC_HAS_IDF_TIME
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
#else
  return 0U;
#endif
}

}  // namespace MB85RC
