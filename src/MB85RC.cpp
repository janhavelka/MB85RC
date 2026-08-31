/**
 * @file MB85RC.cpp
 * @brief MB85RC-family FRAM driver implementation.
 */

#include "MB85RC/MB85RC.h"

#include <cstring>
#include <limits>

namespace MB85RC {
namespace {

void parseDeviceId(const uint8_t (&raw)[cmd::DEVICE_ID_LEN], DeviceId& id) {
  id.manufacturerId = static_cast<uint16_t>((raw[0] << 4) | (raw[1] >> 4));
  id.productId = static_cast<uint16_t>(((raw[1] & 0x0F) << 8) | raw[2]);
  id.densityCode = static_cast<uint8_t>((id.productId >> 8) & 0x0F);
  const cmd::VariantInfo* variant =
      (id.manufacturerId == cmd::MANUFACTURER_ID)
          ? cmd::findVariantByProductId(id.productId)
          : nullptr;
  id.variant = (variant != nullptr) ? variant->variant : DeviceVariant::AUTO;
}

const cmd::VariantInfo* variantForExpected(DeviceVariant expected) {
  for (size_t i = 0; i < cmd::VARIANT_COUNT; ++i) {
    if (cmd::KNOWN_VARIANTS[i].variant == expected) {
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
  return (static_cast<int32_t>(id.manufacturerId) << 12) |
         static_cast<int32_t>(id.productId);
}

bool isValidHighSpeedMasterCode(uint8_t value) {
  return value >= cmd::HIGH_SPEED_MASTER_CODE_MIN &&
         value <= cmd::HIGH_SPEED_MASTER_CODE_MAX;
}

bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

int32_t detailFromU32(uint32_t value) {
  if (value > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
    return std::numeric_limits<int32_t>::max();
  }
  return static_cast<int32_t>(value);
}

Status busyStatus(BusyDetail detail, const char* message) {
  return Status::Error(Err::BUSY, message, static_cast<int32_t>(detail));
}

bool isBaseAddressValidForVariant(uint8_t address, const cmd::VariantInfo& variant) {
  switch (variant.addressModel) {
    case cmd::AddressModel::TWO_BYTE_ADDRESS_PINS:
      return address >= cmd::MIN_ADDRESS && address <= cmd::MAX_ADDRESS;
    case cmd::AddressModel::TWO_BYTE_A16_IN_DEVICE_ADDRESS:
    case cmd::AddressModel::ONE_BYTE_A8_IN_DEVICE_ADDRESS:
      return address >= cmd::MIN_ADDRESS && address <= cmd::MAX_ADDRESS &&
             ((address & 0x01U) == 0U);
    case cmd::AddressModel::ONE_BYTE_UPPER_BITS_IN_DEVICE_ADDRESS:
      return address == cmd::DEFAULT_ADDRESS;
    default:
      return false;
  }
}

Status validateBaseAddressForVariant(uint8_t address, const cmd::VariantInfo& variant) {
  if (isBaseAddressValidForVariant(address, variant)) {
    return Status::Ok();
  }
  return Status::Error(Err::INVALID_CONFIG,
                       "I2C address must be base strap for active variant",
                       address);
}

size_t addressBytesForVariant(const cmd::VariantInfo& variant) {
  switch (variant.addressModel) {
    case cmd::AddressModel::ONE_BYTE_UPPER_BITS_IN_DEVICE_ADDRESS:
    case cmd::AddressModel::ONE_BYTE_A8_IN_DEVICE_ADDRESS:
      return 1U;
    case cmd::AddressModel::TWO_BYTE_ADDRESS_PINS:
    case cmd::AddressModel::TWO_BYTE_A16_IN_DEVICE_ADDRESS:
    default:
      return 2U;
  }
}

bool shouldTrackHealthFailure(Err code) {
  switch (code) {
    case Err::OK:
    case Err::IN_PROGRESS:
    case Err::NOT_INITIALIZED:
    case Err::INVALID_CONFIG:
    case Err::INVALID_PARAM:
    case Err::WRITE_PROTECTED:
    case Err::BUSY:
    case Err::VERIFY_MISMATCH:
    case Err::UNSUPPORTED:
      return false;
    default:
      return true;
  }
}

}  // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================

Status MB85RC::bind(const Config& config) {
  if (_transferBusy()) {
    return busyStatus(BusyDetail::TRANSFER_ACTIVE, "Transfer in progress");
  }
  if (_transfer.resultPending) {
    return busyStatus(BusyDetail::RESULT_PENDING,
                      "Terminal transfer result not consumed");
  }

  if (config.i2cWrite == nullptr || config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C callbacks not set");
  }
  if (config.i2cTimeoutMs < MIN_I2C_TIMEOUT_MS ||
      config.i2cTimeoutMs > MAX_I2C_TIMEOUT_MS) {
    return Status::Error(Err::INVALID_CONFIG, "I2C timeout outside supported range",
                         detailFromU32(config.i2cTimeoutMs));
  }
  if (config.i2cAddress < cmd::MIN_ADDRESS || config.i2cAddress > cmd::MAX_ADDRESS) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid I2C address (must be 0x50-0x57)",
                         config.i2cAddress);
  }
  if (config.maxTxBytes == 0U) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid transport TX capacity",
                         detailFromU32(static_cast<uint32_t>(config.maxTxBytes)));
  }
  if (config.maxRxBytes == 0U) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid transport RX capacity",
                         detailFromU32(static_cast<uint32_t>(config.maxRxBytes)));
  }
  if (!isValidHighSpeedMasterCode(config.highSpeedMasterCode)) {
    return Status::Error(Err::INVALID_CONFIG, "High-speed master code must be 0x08-0x0F");
  }
  if (config.expectedVariant != DeviceVariant::AUTO &&
      variantForExpected(config.expectedVariant) == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "Unsupported expected variant");
  }
  if (config.expectedVariant == DeviceVariant::AUTO &&
      config.i2cSpecial == nullptr) {
    return Status::Error(Err::INVALID_CONFIG,
                         "AUTO requires Device ID special transport");
  }
  const cmd::VariantInfo* explicitVariant = variantForExpected(config.expectedVariant);
  if (explicitVariant != nullptr && !isSupportedRuntimeVariant(*explicitVariant)) {
    return Status::Error(Err::INVALID_CONFIG, "Expected variant not supported by driver");
  }
  if (explicitVariant != nullptr) {
    Status addressStatus = validateBaseAddressForVariant(config.i2cAddress, *explicitVariant);
    if (!addressStatus.ok()) {
      return addressStatus;
    }
  }
  const size_t minimumTxBytes = (explicitVariant != nullptr)
                                    ? addressBytesForVariant(*explicitVariant) + 1U
                                    : cmd::ADDRESS_BYTES + 1U;
  if (config.maxTxBytes < minimumTxBytes) {
    return Status::Error(Err::INVALID_CONFIG,
                         "Transport TX capacity cannot carry address and data");
  }
  if (config.expectedVariant == DeviceVariant::AUTO &&
      config.maxRxBytes < cmd::DEVICE_ID_LEN) {
    return Status::Error(Err::INVALID_CONFIG,
                         "AUTO requires capacity for Device ID read");
  }
  if (config.sleepRecoveryUs != 0U && explicitVariant != nullptr &&
      explicitVariant->supportsSleepMode &&
      config.sleepRecoveryUs < explicitVariant->sleepRecoveryUs) {
    return Status::Error(Err::INVALID_CONFIG, "Sleep recovery time below datasheet tREC");
  }

  _transfer = TransferJob{};
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
  _highSpeedModeEnabled = false;
  _sleepState = SleepState::AWAKE;
  _sleepWakeReadyMs = 0;
  _currentAddressKnown = false;
  _currentAddress = 0;

  _config = config;
  _variant = explicitVariant;

  // Establish a passive binding. Identity and presence checks are explicit.
  _initialized = true;
  _driverState = DriverState::READY;

  return Status::Ok();
}

Status MB85RC::begin(const Config& config) {
  Status st = bind(config);
  if (!st.ok()) {
    return st;
  }
  if (_variant != nullptr && !_variant->hasDeviceId) {
    uint8_t scratch = 0;
    st = _readMemory(0, &scratch, 1);
    _currentAddressKnown = false;
    _currentAddress = 0;
    return st;
  }
  DeviceId id;
  return readDeviceId(id);
}

void MB85RC::tick(uint32_t nowMs) {
  _advanceWakeState(nowMs);
  // No I2C, delay, retry, allocation, or health update is allowed here.
}

void MB85RC::end() {
  if (_transferBusy()) {
    if (_transfer.result.state != TransferState::WAITING_FOR_RECONCILIATION) {
      _transfer.result.failedChunkOffset = _transfer.result.bytesCompleted;
      _transfer.result.failedChunkLength = 0;
    }
    _finishTransfer(TransferState::CANCELLED,
                    Status::Error(Err::CANCELLED,
                                  "Transfer cancelled by end"));
  } else if (!_transfer.resultPending) {
    _transfer = TransferJob{};
  }
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
  _highSpeedModeEnabled = false;
  _sleepState = SleepState::AWAKE;
  _sleepWakeReadyMs = 0;
  _currentAddressKnown = false;
  _currentAddress = 0;
}

Status MB85RC::getSettings(SettingsSnapshot& out) const {
  out.initialized = _initialized;
  out.state = _driverState;
  out.online = isOnline();
  out.i2cAddress = _config.i2cAddress;
  out.i2cTimeoutMs = _config.i2cTimeoutMs;
  out.maxTxBytes = _config.maxTxBytes;
  out.maxRxBytes = _config.maxRxBytes;
  out.maxWriteDataBytes = maxWriteDataBytes();
  out.maxReadDataBytes = maxReadDataBytes();
  out.offlineThreshold = _config.offlineThreshold;
  out.lastOkMs = _lastOkMs;
  out.lastErrorMs = _lastErrorMs;
  out.lastError = _lastError;
  out.consecutiveFailures = _consecutiveFailures;
  out.totalFailures = _totalFailures;
  out.totalSuccess = _totalSuccess;
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
  out.maxNormalBusHz = maxNormalBusHz();
  out.maxHighSpeedBusHz = maxHighSpeedBusHz();
  out.highSpeedModeSupported = supportsHighSpeedMode();
  out.highSpeedModeEnabled = _highSpeedModeEnabled;
  out.sleepModeSupported = supportsSleepMode();
  out.sleepState = _sleepState;
  out.sleepWakeReadyMs = _sleepWakeReadyMs;
  out.sleepRecoveryUs = sleepRecoveryUs();
  out.currentAddressKnown = _currentAddressKnown;
  out.currentAddress = _currentAddress;
  return Status::Ok();
}

uint32_t MB85RC::capacityBytes() const {
  return (_variant != nullptr) ? _variant->memoryBytes : 0UL;
}

uint32_t MB85RC::maxAddress() const {
  return (_variant != nullptr) ? cmd::maxAddressForVariant(*_variant) : 0UL;
}

uint32_t MB85RC::maxNormalBusHz() const {
  return (_variant != nullptr) ? _variant->maxNormalBusHz : 0UL;
}

uint32_t MB85RC::maxHighSpeedBusHz() const {
  return supportsHighSpeedMode() ? _variant->maxHighSpeedBusHz : 0UL;
}

bool MB85RC::supportsHighSpeedMode() const {
  return _variant != nullptr && _variant->supportsHighSpeedMode;
}

bool MB85RC::supportsSleepMode() const {
  return _variant != nullptr && _variant->supportsSleepMode;
}

uint16_t MB85RC::sleepRecoveryUs() const {
  if (!supportsSleepMode()) {
    return 0U;
  }
  return (_config.sleepRecoveryUs != 0U) ? _config.sleepRecoveryUs : _variant->sleepRecoveryUs;
}

size_t MB85RC::maxWriteDataBytes() const {
  if (_variant == nullptr) {
    return 0U;
  }
  const size_t addressBytes = addressBytesForVariant(*_variant);
  if (_config.maxTxBytes <= addressBytes) {
    return 0U;
  }
  size_t limit = _config.maxTxBytes - addressBytes;
  if (limit > cmd::MAX_WRITE_DATA_BYTES) {
    limit = cmd::MAX_WRITE_DATA_BYTES;
  }
  return limit;
}

size_t MB85RC::maxReadDataBytes() const {
  if (_variant == nullptr) {
    return 0U;
  }
  return (_config.maxRxBytes < cmd::MAX_READ_CHUNK)
             ? _config.maxRxBytes
             : cmd::MAX_READ_CHUNK;
}

Status MB85RC::setHighSpeedMode(bool enabled) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }
  if (!enabled) {
    _highSpeedModeEnabled = false;
    return Status::Ok();
  }
  if (!supportsHighSpeedMode()) {
    return Status::Error(Err::UNSUPPORTED, "Active variant does not support High-speed mode");
  }
  if (_config.i2cSpecial == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "Special I2C callback not set");
  }
  if (!isValidHighSpeedMasterCode(_config.highSpeedMasterCode)) {
    return Status::Error(Err::INVALID_CONFIG, "High-speed master code must be 0x08-0x0F");
  }
  _highSpeedModeEnabled = true;
  return Status::Ok();
}

Status MB85RC::enterSleep() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }
  if (!supportsSleepMode()) {
    return Status::Error(Err::UNSUPPORTED, "Active variant does not support Sleep mode");
  }
  _advanceWakeState(_nowMs());
  if (_config.i2cSpecial == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "Special I2C callback not set");
  }
  if (_sleepState == SleepState::ASLEEP) {
    return busyStatus(BusyDetail::ALREADY_ASLEEP, "Device is already asleep");
  }
  if (_sleepState == SleepState::WAKING) {
    return busyStatus(BusyDetail::WAKING, "Sleep wake recovery pending");
  }
  if (_sleepState == SleepState::UNKNOWN) {
    return busyStatus(BusyDetail::SLEEP_STATE_UNKNOWN,
                      "Sleep state unknown; call wake()");
  }

  I2cSpecialTransfer transfer = _specialTransfer(_config.i2cAddress);
  Status st = _i2cSpecialTracked(I2cSpecialOp::ENTER_SLEEP, transfer);
  _currentAddressKnown = false;
  _currentAddress = 0;
  if (!st.ok()) {
    if (st.code != Err::I2C_NACK_ADDR && st.code != Err::I2C_NACK_DATA) {
      _sleepState = SleepState::UNKNOWN;
    }
    return st;
  }

  _sleepState = SleepState::ASLEEP;
  _sleepWakeReadyMs = 0;
  return Status::Ok();
}

Status MB85RC::wake() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }
  if (!supportsSleepMode()) {
    return Status::Error(Err::UNSUPPORTED, "Active variant does not support Sleep mode");
  }
  _advanceWakeState(_nowMs());
  if (_sleepState == SleepState::AWAKE) {
    return Status::Ok();
  }
  if (_config.i2cSpecial == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "Special I2C callback not set");
  }
  if (_sleepState == SleepState::WAKING) {
    return busyStatus(BusyDetail::WAKING, "Sleep wake recovery pending");
  }

  I2cSpecialTransfer transfer = _specialTransfer(_config.i2cAddress);
  Status st = _i2cSpecialTracked(I2cSpecialOp::WAKE_FROM_SLEEP, transfer);
  _currentAddressKnown = false;
  _currentAddress = 0;
  if (!st.ok()) {
    _sleepState = SleepState::UNKNOWN;
    return st;
  }

  if (_config.nowMs == nullptr) {
    // Without a time source the core can neither measure tREC nor ever leave
    // WAKING, which would block all further I2C. Report AWAKE and leave the
    // documented recovery wait to the caller.
    _sleepState = SleepState::AWAKE;
    _sleepWakeReadyMs = 0;
    return Status::Ok();
  }

  uint32_t recoveryMs = (static_cast<uint32_t>(sleepRecoveryUs()) + 999UL) / 1000UL;
  if (recoveryMs < cmd::SLEEP_RECOVERY_MS) {
    recoveryMs = cmd::SLEEP_RECOVERY_MS;
  }
  _sleepState = SleepState::WAKING;
  _sleepWakeReadyMs = _nowMs() + recoveryMs;
  return Status::Ok();
}

// ===========================================================================
// Diagnostics
// ===========================================================================

Status MB85RC::probe() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }
  if (_variant != nullptr && !_variant->hasDeviceId) {
    uint8_t scratch = 0;
    Status st = _readMemoryRaw(0, &scratch, 1);
    _currentAddressKnown = false;
    _currentAddress = 0;
    if (!st.ok()) {
      return st;
    }
    return Status::Ok();
  }

  DeviceId id;
  Status st = _readDeviceIdRaw(id);
  if (!st.ok()) {
    return st;
  }
  if (_variant != nullptr) {
    return _validateActiveDeviceId(id);
  }
  if (id.manufacturerId != cmd::MANUFACTURER_ID ||
      cmd::findVariantByProductId(id.productId) == nullptr) {
    return Status::Error(Err::DEVICE_ID_MISMATCH,
                         "Unknown Device ID", deviceIdDetail(id));
  }
  return Status::Ok();
}

Status MB85RC::recover() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }

  Status st;
  if (_variant != nullptr && !_variant->hasDeviceId) {
    uint8_t scratch = 0;
    st = _readMemory(0, &scratch, 1);
  } else {
    DeviceId id;
    st = readDeviceId(id);
  }
  _currentAddressKnown = false;
  _currentAddress = 0;
  return st;
}

// ===========================================================================
// Memory Read API
// ===========================================================================

Status MB85RC::readByte(uint32_t address, uint8_t& value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }
  if (!_isValidAddress(address)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Address exceeds active capacity",
                         detailFromU32(address));
  }

  return _readMemory(address, &value, 1);
}

Status MB85RC::readOnce(uint32_t address, uint8_t* buf, size_t len) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }
  if (buf == nullptr || len == 0U || len > maxReadDataBytes()) {
    return Status::Error(Err::INVALID_PARAM, "Read exceeds one-transaction limit");
  }
  if (!_fitsRange(address, len)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE,
                         "Read range exceeds active capacity", detailFromU32(address));
  }
  return _readMemory(address, buf, len);
}

Status MB85RC::read(uint32_t address, uint8_t* buf, size_t len) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }
  if (!_fitsRange(address, len)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Read range exceeds active capacity",
                         detailFromU32(address));
  }

  // Break large reads into chunks to stay within I2C buffer limits
  size_t offset = 0;
  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > maxReadDataBytes()) {
      chunk = maxReadDataBytes();
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
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }
  if (!_isValidAddress(address)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE, "Address exceeds active capacity",
                         detailFromU32(address));
  }

  return _writeMemory(address, &value, 1);
}

Status MB85RC::writeOnce(uint32_t address, const uint8_t* buf, size_t len,
                         WriteCommit* writeCommit) {
  if (writeCommit != nullptr) {
    *writeCommit = WriteCommit::NOT_APPLICABLE;
  }
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }
  if (buf == nullptr || len == 0U || len > maxWriteDataBytes()) {
    return Status::Error(Err::INVALID_PARAM, "Write exceeds one-transaction limit");
  }
  if (!_fitsRange(address, len)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE,
                         "Write range exceeds active capacity", detailFromU32(address));
  }
  return _writeMemory(address, buf, len, writeCommit);
}

Status MB85RC::write(uint32_t address, const uint8_t* buf, size_t len) {
  return writeDetailed(address, buf, len).status;
}

WriteResult MB85RC::writeDetailed(uint32_t address, const uint8_t* buf, size_t len) {
  WriteResult result;
  result.address = address;
  result.bytesRequested = len;

  if (!_initialized) {
    result.status = Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
    return result;
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    result.status = busy;
    return result;
  }
  if (buf == nullptr || len == 0) {
    result.status = Status::Error(Err::INVALID_PARAM, "Invalid write buffer");
    return result;
  }
  if (!_fitsRange(address, len)) {
    result.status = Status::Error(Err::ADDRESS_OUT_OF_RANGE,
                                  "Write range exceeds active capacity",
                                  detailFromU32(address));
    return result;
  }
  Status ready = _ensureAwakeForI2c();
  if (!ready.ok()) {
    result.status = ready;
    return result;
  }

  // Break large writes into chunks to stay within I2C buffer limits
  size_t offset = 0;
  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > maxWriteDataBytes()) {
      chunk = maxWriteDataBytes();
    }

    uint32_t addr = address + static_cast<uint32_t>(offset);

    WriteCommit commit = WriteCommit::INDETERMINATE;
    Status st = _writeMemory(addr, buf + offset, chunk, &commit);
    result.writeCommit = commit;
    if (!st.ok()) {
      result.status = st;
      result.failedChunkOffset = offset;
      result.failedChunkLength = chunk;
      if (commit == WriteCommit::ACCEPTED) {
        result.bytesAccepted = offset + chunk;
        result.complete = (result.bytesAccepted == result.bytesRequested);
      }
      return result;
    }
    offset += chunk;
    result.bytesAccepted = offset;
  }

  result.status = Status::Ok();
  result.writeCommit = WriteCommit::ACCEPTED;
  result.failedChunkOffset = result.bytesRequested;
  result.failedChunkLength = 0;
  result.complete = true;
  return result;
}

Status MB85RC::fill(uint32_t address, uint8_t value, size_t len) {
  return fillDetailed(address, value, len).status;
}

WriteResult MB85RC::fillDetailed(uint32_t address, uint8_t value, size_t len) {
  WriteResult result;
  result.address = address;
  result.bytesRequested = len;

  if (!_initialized) {
    result.status = Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
    return result;
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    result.status = busy;
    return result;
  }
  if (len == 0) {
    result.status = Status::Error(Err::INVALID_PARAM, "Fill length must be > 0");
    return result;
  }
  if (!_fitsRange(address, len)) {
    result.status = Status::Error(Err::ADDRESS_OUT_OF_RANGE,
                                  "Fill range exceeds active capacity",
                                  detailFromU32(address));
    return result;
  }
  Status ready = _ensureAwakeForI2c();
  if (!ready.ok()) {
    result.status = ready;
    return result;
  }

  uint8_t chunk[cmd::MAX_FILL_CHUNK];
  std::memset(chunk, value, sizeof(chunk));

  size_t remaining = len;
  size_t offset = 0;
  while (remaining > 0) {
    size_t toWrite = remaining;
    size_t fillLimit = cmd::MAX_FILL_CHUNK;
    if (fillLimit > maxWriteDataBytes()) {
      fillLimit = maxWriteDataBytes();
    }
    if (toWrite > fillLimit) {
      toWrite = fillLimit;
    }

    uint32_t addr = address + static_cast<uint32_t>(offset);

    WriteCommit commit = WriteCommit::INDETERMINATE;
    Status st = _writeMemory(addr, chunk, toWrite, &commit);
    result.writeCommit = commit;
    if (!st.ok()) {
      result.status = st;
      result.failedChunkOffset = offset;
      result.failedChunkLength = toWrite;
      if (commit == WriteCommit::ACCEPTED) {
        result.bytesAccepted = offset + toWrite;
        result.complete = (result.bytesAccepted == result.bytesRequested);
      }
      return result;
    }
    offset += toWrite;
    result.bytesAccepted = offset;
    remaining -= toWrite;
  }

  result.status = Status::Ok();
  result.writeCommit = WriteCommit::ACCEPTED;
  result.failedChunkOffset = result.bytesRequested;
  result.failedChunkLength = 0;
  result.complete = true;
  return result;
}

// ===========================================================================
// Device Information
// ===========================================================================

Status MB85RC::readDeviceId(DeviceId& id) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }
  if (_variant != nullptr && !_variant->hasDeviceId) {
    return Status::Error(Err::INVALID_PARAM, "Active variant has no Device ID");
  }

  Status st = _readDeviceIdTracked(id);
  if (!st.ok()) {
    return st;
  }

  if (_config.expectedVariant == DeviceVariant::AUTO) {
    st = _selectVariant(id);
    if (!st.ok()) {
      _variant = nullptr;
      _deviceId = DeviceId{};
      _highSpeedModeEnabled = false;
      _currentAddressKnown = false;
      _currentAddress = 0;
      return st;
    }
    st = validateBaseAddressForVariant(_config.i2cAddress, *_variant);
    if (!st.ok()) {
      _variant = nullptr;
      _deviceId = DeviceId{};
      _highSpeedModeEnabled = false;
      _currentAddressKnown = false;
      _currentAddress = 0;
      return st;
    }
    if (_config.sleepRecoveryUs != 0U && _variant->supportsSleepMode &&
        _config.sleepRecoveryUs < _variant->sleepRecoveryUs) {
      _variant = nullptr;
      _deviceId = DeviceId{};
      _highSpeedModeEnabled = false;
      _currentAddressKnown = false;
      _currentAddress = 0;
      return Status::Error(Err::INVALID_CONFIG,
                           "Sleep recovery time below datasheet tREC");
    }
  } else {
    st = _validateActiveDeviceId(id);
    if (!st.ok()) {
      _deviceId = DeviceId{};
      return st;
    }
    _deviceId = id;
  }
  return Status::Ok();
}

Status MB85RC::readDeviceIdRaw(DeviceIdRaw& raw) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }
  if (_variant != nullptr && !_variant->hasDeviceId) {
    return Status::Error(Err::INVALID_PARAM, "Active variant has no Device ID");
  }

  return _readDeviceIdBytesTracked(raw);
}

DeviceId MB85RC::decodeDeviceId(const DeviceIdRaw& raw) {
  DeviceId id;
  parseDeviceId(raw.bytes, id);
  return id;
}

Status MB85RC::readCurrentAddress(uint8_t& value) {
  return readCurrentAddress(&value, 1);
}

Status MB85RC::readCurrentAddress(uint8_t* buf, size_t len) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
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
                         detailFromU32(_currentAddress));
  }

  for (size_t offset = 0; offset < len; ++offset) {
    // Variants that carry upper memory-address bits in the slave byte
    // (MB85RC04V A8, MB85RC16V A10:A8, MB85RC1MT A16) compose the current
    // address from those bits plus the low bits held in the device's own
    // buffer, and then read the following byte. The slave byte must therefore
    // encode the last accessed byte, not the byte about to be read.
    const uint32_t lastAccessed =
        (_currentAddress == 0U) ? maxAddress() : (_currentAddress - 1U);
    EncodedMemoryAddress enc;
    Status st = _encodeMemoryAddress(lastAccessed, enc);
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

  VerifyDetailedResult detailed = verifyDetailed(address, expected, len);
  if (!detailed.status.ok()) {
    return detailed.status;
  }

  out.match = detailed.match;
  out.mismatchOffset = detailed.firstMismatchOffset;
  out.expected = detailed.expected;
  out.actual = detailed.actual;
  return Status::Ok();
}

Status MB85RC::verifyOnce(uint32_t address, const uint8_t* expected, size_t len,
                          VerifyResult& out) {
  out = VerifyResult{};
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    return busy;
  }
  if (expected == nullptr || len == 0U || len > maxReadDataBytes()) {
    return Status::Error(Err::INVALID_PARAM, "Verify exceeds one-transaction limit");
  }
  if (!_fitsRange(address, len)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE,
                         "Verify range exceeds active capacity", detailFromU32(address));
  }

  uint8_t readBuf[cmd::MAX_READ_CHUNK];
  Status st = _readMemory(address, readBuf, len);
  if (!st.ok()) {
    return st;
  }
  for (size_t i = 0; i < len; ++i) {
    if (readBuf[i] != expected[i]) {
      out.match = false;
      out.mismatchOffset = i;
      out.expected = expected[i];
      out.actual = readBuf[i];
      return Status::Error(Err::VERIFY_MISMATCH, "Verify mismatch",
                           static_cast<int32_t>(i));
    }
  }
  out.match = true;
  return Status::Ok();
}

VerifyDetailedResult MB85RC::verifyDetailed(uint32_t address, const uint8_t* expected,
                                            size_t len) {
  VerifyDetailedResult result;
  result.address = address;
  result.bytesRequested = len;

  if (!_initialized) {
    result.status = Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
    return result;
  }
  Status busy = _ensureNoTransferActive();
  if (!busy.ok()) {
    result.status = busy;
    return result;
  }
  if (expected == nullptr || len == 0) {
    result.status = Status::Error(Err::INVALID_PARAM, "Invalid verify buffer");
    return result;
  }
  if (!_fitsRange(address, len)) {
    result.status = Status::Error(Err::ADDRESS_OUT_OF_RANGE,
                                  "Verify range exceeds active capacity",
                                  detailFromU32(address));
    return result;
  }

  uint8_t readBuf[cmd::MAX_READ_CHUNK];
  size_t offset = 0;
  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > maxReadDataBytes()) {
      chunk = maxReadDataBytes();
    }

    const uint32_t chunkAddr = address + static_cast<uint32_t>(offset);
    Status st = _readMemory(chunkAddr, readBuf, chunk);
    if (!st.ok()) {
      result.status = st;
      return result;
    }

    for (size_t i = 0; i < chunk; ++i) {
      if (readBuf[i] != expected[offset + i]) {
        result.status = Status::Ok();
        result.match = false;
        result.bytesVerified = offset + i;
        result.firstMismatchOffset = offset + i;
        result.expected = expected[offset + i];
        result.actual = readBuf[i];
        return result;
      }
    }

    offset += chunk;
    result.bytesVerified = offset;
  }

  result.status = Status::Ok();
  result.match = true;
  return result;
}

Status MB85RC::writeVerify(uint32_t address, const uint8_t* buf, size_t len,
                           VerifyDetailedResult* verifyOut) {
  WriteResult wr = writeDetailed(address, buf, len);
  if (!wr.status.ok()) {
    if (verifyOut != nullptr) {
      VerifyDetailedResult result;
      result.status = wr.status;
      result.address = address;
      result.bytesRequested = len;
      *verifyOut = result;
    }
    return wr.status;
  }

  VerifyDetailedResult vr = verifyDetailed(address, buf, len);
  if (verifyOut != nullptr) {
    *verifyOut = vr;
  }
  if (!vr.status.ok()) {
    return vr.status;
  }
  if (!vr.match) {
    return Status::Error(Err::VERIFY_MISMATCH, "Verify mismatch",
                         static_cast<int32_t>(vr.firstMismatchOffset));
  }
  return Status::Ok();
}

Status MB85RC::fillVerify(uint32_t address, uint8_t value, size_t len,
                          VerifyDetailedResult* verifyOut) {
  WriteResult wr = fillDetailed(address, value, len);
  if (!wr.status.ok()) {
    if (verifyOut != nullptr) {
      VerifyDetailedResult result;
      result.status = wr.status;
      result.address = address;
      result.bytesRequested = len;
      *verifyOut = result;
    }
    return wr.status;
  }

  VerifyDetailedResult result;
  result.address = address;
  result.bytesRequested = len;

  uint8_t readBuf[cmd::MAX_READ_CHUNK];
  size_t offset = 0;
  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > maxReadDataBytes()) {
      chunk = maxReadDataBytes();
    }

    Status st = _readMemory(address + static_cast<uint32_t>(offset), readBuf, chunk);
    if (!st.ok()) {
      result.status = st;
      if (verifyOut != nullptr) {
        *verifyOut = result;
      }
      return st;
    }

    for (size_t i = 0; i < chunk; ++i) {
      if (readBuf[i] != value) {
        result.status = Status::Ok();
        result.match = false;
        result.bytesVerified = offset + i;
        result.firstMismatchOffset = offset + i;
        result.expected = value;
        result.actual = readBuf[i];
        if (verifyOut != nullptr) {
          *verifyOut = result;
        }
        return Status::Error(Err::VERIFY_MISMATCH, "Verify mismatch",
                             static_cast<int32_t>(result.firstMismatchOffset));
      }
    }

    offset += chunk;
    result.bytesVerified = offset;
  }

  result.status = Status::Ok();
  result.match = true;
  if (verifyOut != nullptr) {
    *verifyOut = result;
  }
  return Status::Ok();
}

// ===========================================================================
// Poll-Chunked Transfer API
// ===========================================================================

Status MB85RC::requestRead(uint32_t address, uint8_t* data, size_t length) {
  return _requestTransfer(_allocateRequestId(), TransferKind::READ, address,
                          data, nullptr, 0, length);
}

Status MB85RC::requestRead(uint32_t requestId, uint32_t address,
                           uint8_t* data, size_t length) {
  if (requestId >= AUTOMATIC_REQUEST_ID_FIRST) {
    return Status::Error(Err::INVALID_PARAM, "Caller request ID uses reserved range");
  }
  return _requestTransfer(requestId, TransferKind::READ, address,
                          data, nullptr, 0, length);
}

Status MB85RC::requestWrite(uint32_t address, const uint8_t* data, size_t length) {
  return _requestTransfer(_allocateRequestId(), TransferKind::WRITE, address,
                          nullptr, data, 0, length);
}

Status MB85RC::requestWrite(uint32_t requestId, uint32_t address,
                            const uint8_t* data, size_t length) {
  if (requestId >= AUTOMATIC_REQUEST_ID_FIRST) {
    return Status::Error(Err::INVALID_PARAM, "Caller request ID uses reserved range");
  }
  return _requestTransfer(requestId, TransferKind::WRITE, address,
                          nullptr, data, 0, length);
}

Status MB85RC::requestFill(uint32_t address, uint8_t value, size_t length) {
  return _requestTransfer(_allocateRequestId(), TransferKind::FILL, address,
                          nullptr, nullptr, value, length);
}

Status MB85RC::requestFill(uint32_t requestId, uint32_t address,
                           uint8_t value, size_t length) {
  if (requestId >= AUTOMATIC_REQUEST_ID_FIRST) {
    return Status::Error(Err::INVALID_PARAM, "Caller request ID uses reserved range");
  }
  return _requestTransfer(requestId, TransferKind::FILL, address,
                          nullptr, nullptr, value, length);
}

Status MB85RC::requestVerify(uint32_t address, const uint8_t* data, size_t length) {
  return _requestTransfer(_allocateRequestId(), TransferKind::VERIFY, address,
                          nullptr, data, 0, length);
}

Status MB85RC::requestVerify(uint32_t requestId, uint32_t address,
                             const uint8_t* data, size_t length) {
  if (requestId >= AUTOMATIC_REQUEST_ID_FIRST) {
    return Status::Error(Err::INVALID_PARAM, "Caller request ID uses reserved range");
  }
  return _requestTransfer(requestId, TransferKind::VERIFY, address,
                          nullptr, data, 0, length);
}

Status MB85RC::requestVerifiedWrite(uint32_t requestId, uint32_t address,
                                    const uint8_t* data, size_t length) {
  if (requestId >= AUTOMATIC_REQUEST_ID_FIRST) {
    return Status::Error(Err::INVALID_PARAM, "Caller request ID uses reserved range");
  }
  return _requestTransfer(requestId, TransferKind::VERIFIED_WRITE, address,
                          nullptr, data, 0, length);
}

Status MB85RC::pollTransfer(uint32_t nowMs, uint8_t maxInstructions) {
  _advanceWakeState(nowMs);
  if (!_transferBusy()) {
    return _transfer.resultPending ? _transfer.result.status : Status::Ok();
  }
  if (maxInstructions == 0U) {
    return _transfer.result.status;
  }
  if (_transfer.result.state == TransferState::WAITING_FOR_RECONCILIATION) {
    return _transfer.result.status;
  }

  uint8_t budget = maxInstructions;
  if (budget > cmd::MAX_TRANSFER_INSTRUCTIONS_PER_POLL) {
    budget = cmd::MAX_TRANSFER_INSTRUCTIONS_PER_POLL;
  }

  while (budget > 0U && _transferBusy()) {
    Status st = _pollTransferInstruction();
    if (st.inProgress()) {
      _transfer.result.status = st;
      return st;
    }
    if (!st.ok()) {
      _finishTransfer(TransferState::FAILED, st);
      return st;
    }
    if (_transfer.offset >= _transfer.result.bytesRequested) {
      _finishTransfer(TransferState::SUCCEEDED, Status::Ok());
      return Status::Ok();
    }
    --budget;
  }

  return _transfer.result.status;
}

Status MB85RC::getTransferProgress(TransferResult& out) const {
  if (!_transferBusy() && !_transfer.resultPending) {
    return Status::Error(Err::NO_RESULT, "No transfer result available");
  }
  out = _transfer.result;
  return Status::Ok();
}

Status MB85RC::takeTransferResult(TransferResult& out) {
  if (!_transfer.resultPending || _transferBusy()) {
    return Status::Error(Err::NO_RESULT, "No terminal transfer result available");
  }
  out = _transfer.result;
  _transfer = TransferJob{};
  return Status::Ok();
}

Status MB85RC::resumeVerifiedWrite(uint32_t requestId) {
  if (!_transferBusy() || _transfer.result.kind != TransferKind::VERIFIED_WRITE ||
      _transfer.result.state != TransferState::WAITING_FOR_RECONCILIATION) {
    return Status::Error(Err::INVALID_PARAM,
                         "Verified write is not awaiting reconciliation");
  }
  if (_transfer.result.requestId != requestId) {
    return busyStatus(BusyDetail::REQUEST_ID_MISMATCH, "Request ID mismatch");
  }
  _transfer.verifyPhase = true;
  _transfer.result.state = TransferState::ACTIVE;
  _transfer.result.status = Status::Error(Err::IN_PROGRESS,
                                          "Transfer in progress");
  return Status::Ok();
}

Status MB85RC::cancelTransfer() {
  if (!_transferBusy()) {
    return Status::Error(Err::NO_RESULT, "No active transfer to cancel");
  }
  return cancelTransfer(_transfer.result.requestId);
}

Status MB85RC::cancelTransfer(uint32_t requestId) {
  if (!_transferBusy()) {
    return Status::Error(Err::NO_RESULT, "No active transfer to cancel");
  }
  if (_transfer.result.requestId != requestId) {
    return busyStatus(BusyDetail::REQUEST_ID_MISMATCH, "Request ID mismatch");
  }
  if (_transfer.result.state != TransferState::WAITING_FOR_RECONCILIATION) {
    _transfer.result.failedChunkOffset = _transfer.result.bytesCompleted;
    _transfer.result.failedChunkLength = 0;
  }
  _finishTransfer(TransferState::CANCELLED,
                  Status::Error(Err::CANCELLED, "Transfer cancelled"));
  return Status::Ok();
}

Status MB85RC::timeoutTransfer(uint32_t requestId) {
  if (!_transferBusy()) {
    return Status::Error(Err::NO_RESULT, "No active transfer to time out");
  }
  if (_transfer.result.requestId != requestId) {
    return busyStatus(BusyDetail::REQUEST_ID_MISMATCH, "Request ID mismatch");
  }
  if (_transfer.result.state != TransferState::WAITING_FOR_RECONCILIATION) {
    _transfer.result.failedChunkOffset = _transfer.result.bytesCompleted;
    _transfer.result.failedChunkLength = 0;
  }
  _finishTransfer(TransferState::TIMED_OUT,
                  Status::Error(Err::TIMEOUT, "Transfer timed out"));
  return Status::Ok();
}

// ===========================================================================
// Transport Wrappers
// ===========================================================================

Status MB85RC::_i2cWriteReadRaw(uint8_t addr, const uint8_t* txBuf, size_t txLen,
                                uint8_t* rxBuf, size_t rxLen) {
  Status ready = _ensureAwakeForI2c();
  if (!ready.ok()) {
    return ready;
  }
  if ((txLen > 0 && txBuf == nullptr) || (rxLen > 0 && rxBuf == nullptr) ||
      (txLen == 0 && rxLen == 0)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  if (_config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write-read not set");
  }
  if (txLen > _config.maxTxBytes || rxLen > _config.maxRxBytes) {
    return Status::Error(Err::INVALID_PARAM, "I2C transfer exceeds transport capacity");
  }
  if (_highSpeedModeEnabled && addr >= cmd::MIN_ADDRESS && addr <= cmd::MAX_ADDRESS) {
    I2cSpecialTransfer transfer = _specialTransfer(addr, txBuf, txLen, rxBuf, rxLen);
    return _i2cSpecialRaw(I2cSpecialOp::HIGH_SPEED_WRITE_READ, transfer);
  }
  const TransportResult result = _config.i2cWriteRead(
      addr, txBuf, txLen, rxBuf, rxLen, _config.i2cTimeoutMs, _config.i2cUser);
  return _mapTransportResult(result, txLen, rxLen, false, 0U);
}

Status MB85RC::_i2cWriteRaw(uint8_t addr, const uint8_t* buf, size_t len,
                            size_t memoryAddressBytes,
                            WriteCommit* writeCommit) {
  if (writeCommit != nullptr) {
    *writeCommit = WriteCommit::NOT_APPLICABLE;
  }
  Status ready = _ensureAwakeForI2c();
  if (!ready.ok()) {
    return ready;
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  if (_config.i2cWrite == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write not set");
  }
  if (len > _config.maxTxBytes) {
    return Status::Error(Err::INVALID_PARAM, "I2C write exceeds transport capacity");
  }
  if (_highSpeedModeEnabled && addr >= cmd::MIN_ADDRESS && addr <= cmd::MAX_ADDRESS) {
    I2cSpecialTransfer transfer = _specialTransfer(addr, buf, len);
    return _i2cSpecialRaw(I2cSpecialOp::HIGH_SPEED_WRITE, transfer, writeCommit,
                          memoryAddressBytes);
  }
  const TransportResult result = _config.i2cWrite(
      addr, buf, len, _config.i2cTimeoutMs, _config.i2cUser);
  return _mapTransportResult(result, len, 0U, true, memoryAddressBytes,
                             writeCommit);
}

Status MB85RC::_i2cSpecialRaw(I2cSpecialOp op, const I2cSpecialTransfer& transfer,
                              WriteCommit* writeCommit,
                              size_t memoryAddressBytes) {
  if (_config.i2cSpecial == nullptr) {
    return Status::Error(Err::UNSUPPORTED, "Special I2C callback not set");
  }
  if (transfer.txLen > _config.maxTxBytes || transfer.rxLen > _config.maxRxBytes) {
    return Status::Error(Err::INVALID_PARAM,
                         "Special I2C transfer exceeds transport capacity");
  }
  const TransportResult result = _config.i2cSpecial(
      op, transfer, _config.i2cTimeoutMs, _config.i2cUser);
  return _mapTransportResult(result, transfer.txLen, transfer.rxLen,
                             op == I2cSpecialOp::HIGH_SPEED_WRITE,
                             memoryAddressBytes, writeCommit);
}

Status MB85RC::_i2cWriteReadTrackedAddr(uint8_t addr, const uint8_t* txBuf, size_t txLen,
                                        uint8_t* rxBuf, size_t rxLen) {
  Status ready = _ensureAwakeForI2c();
  if (!ready.ok()) {
    return ready;
  }

  if ((txLen > 0 && txBuf == nullptr) || (rxLen > 0 && rxBuf == nullptr) ||
      (txLen == 0 && rxLen == 0)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status st = _i2cWriteReadRaw(addr, txBuf, txLen, rxBuf, rxLen);
  return _updateHealth(st);
}

Status MB85RC::_i2cWriteTrackedAddr(uint8_t addr, const uint8_t* buf, size_t len,
                                    size_t memoryAddressBytes,
                                    WriteCommit* writeCommit) {
  Status ready = _ensureAwakeForI2c();
  if (!ready.ok()) {
    return ready;
  }

  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status st = _i2cWriteRaw(addr, buf, len, memoryAddressBytes, writeCommit);
  return _updateHealth(st);
}

Status MB85RC::_i2cSpecialTracked(I2cSpecialOp op,
                                  const I2cSpecialTransfer& transfer,
                                  WriteCommit* writeCommit) {
  Status st = _i2cSpecialRaw(op, transfer, writeCommit);
  return _updateHealth(st);
}

Status MB85RC::_mapTransportResult(const TransportResult& result,
                                   size_t expectedTx, size_t expectedRx,
                                   bool memoryWrite, size_t memoryAddressBytes,
                                   WriteCommit* writeCommit) {
  const bool knownFailureCode =
      result.code == TransportCode::NACK_ADDRESS ||
      result.code == TransportCode::NACK_DATA ||
      result.code == TransportCode::TIMEOUT ||
      result.code == TransportCode::BUS_ERROR ||
      result.code == TransportCode::IO_ERROR;
  const bool postAcceptanceFailure =
      result.code == TransportCode::TIMEOUT ||
      result.code == TransportCode::BUS_ERROR ||
      result.code == TransportCode::IO_ERROR;
  if (writeCommit != nullptr) {
    if (!memoryWrite) {
      *writeCommit = WriteCommit::NOT_APPLICABLE;
    } else if (result.ok()) {
      *writeCommit = WriteCommit::ACCEPTED;
    } else if (knownFailureCode &&
               result.writeCommit == WriteCommit::NOT_COMMITTED &&
               result.completedTxBytes <= memoryAddressBytes &&
               result.completedRxBytes == expectedRx) {
      *writeCommit = WriteCommit::NOT_COMMITTED;
    } else if (postAcceptanceFailure &&
               result.writeCommit == WriteCommit::ACCEPTED &&
               result.completedTxBytes == expectedTx &&
               result.completedRxBytes == expectedRx) {
      *writeCommit = WriteCommit::ACCEPTED;
    } else {
      *writeCommit = WriteCommit::INDETERMINATE;
    }
  }

  if (result.ok()) {
    if (result.completedTxBytes != expectedTx ||
        result.completedRxBytes != expectedRx) {
      if (writeCommit != nullptr && memoryWrite) {
        *writeCommit = WriteCommit::INDETERMINATE;
      }
      return Status::Error(Err::I2C_ERROR,
                           "Transport reported short completion",
                           result.detail);
    }
    return Status::Ok();
  }

  switch (result.code) {
    case TransportCode::NACK_ADDRESS:
      return Status::Error(Err::I2C_NACK_ADDR, "I2C address not acknowledged",
                           result.detail);
    case TransportCode::NACK_DATA:
      return Status::Error(Err::I2C_NACK_DATA, "I2C data not acknowledged",
                           result.detail);
    case TransportCode::TIMEOUT:
      return Status::Error(Err::I2C_TIMEOUT, "I2C transport timeout",
                           result.detail);
    case TransportCode::BUS_ERROR:
      return Status::Error(Err::I2C_BUS, "I2C bus error", result.detail);
    case TransportCode::IO_ERROR:
    case TransportCode::OK:
    default:
      return Status::Error(Err::I2C_ERROR, "I2C transport error", result.detail);
  }
}

// ===========================================================================
// Internal Helpers
// ===========================================================================

bool MB85RC::_transferBusy() const {
  return _transfer.result.state == TransferState::ACTIVE ||
         _transfer.result.state == TransferState::WAITING_FOR_RECONCILIATION;
}

Status MB85RC::_ensureNoTransferActive() const {
  if (_transferBusy()) {
    return busyStatus(BusyDetail::TRANSFER_ACTIVE, "Transfer in progress");
  }
  return Status::Ok();
}

void MB85RC::_finishTransfer(TransferState state, const Status& status) {
  _transfer.result.state = state;
  _transfer.result.status = status;
  if (state == TransferState::SUCCEEDED) {
    _transfer.result.failedChunkOffset = _transfer.result.bytesRequested;
    _transfer.result.failedChunkLength = 0U;
  }
  _transfer.resultPending = true;
  _transfer.data = nullptr;
  _transfer.constData = nullptr;
}

Status MB85RC::_requestTransfer(uint32_t requestId, TransferKind kind,
                                uint32_t address, uint8_t* data,
                                const uint8_t* constData, uint8_t fillValue,
                                size_t length) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_transferBusy()) {
    return busyStatus(BusyDetail::TRANSFER_ACTIVE, "Transfer in progress");
  }
  if (_transfer.resultPending) {
    return busyStatus(BusyDetail::RESULT_PENDING,
                      "Terminal transfer result not consumed");
  }
  if (requestId == 0U) {
    return Status::Error(Err::INVALID_PARAM, "Request ID must be nonzero");
  }
  if (length == 0U) {
    return Status::Error(Err::INVALID_PARAM, "Transfer length must be > 0");
  }
  if ((kind == TransferKind::READ && data == nullptr) ||
      ((kind == TransferKind::WRITE || kind == TransferKind::VERIFY ||
        kind == TransferKind::VERIFIED_WRITE) &&
       constData == nullptr)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid transfer buffer");
  }
  if (kind == TransferKind::VERIFIED_WRITE &&
      (length > maxWriteDataBytes() || length > maxReadDataBytes())) {
    return Status::Error(Err::INVALID_PARAM,
                         "Verified write exceeds one-transaction limit");
  }
  if (!_fitsRange(address, length)) {
    return Status::Error(Err::ADDRESS_OUT_OF_RANGE,
                         "Transfer range exceeds active capacity",
                         detailFromU32(address));
  }
  Status ready = _ensureAwakeForI2c();
  if (!ready.ok()) {
    return ready;
  }

  _transfer = TransferJob{};
  _transfer.result.requestId = requestId;
  _transfer.result.kind = kind;
  _transfer.result.state = TransferState::ACTIVE;
  _transfer.result.status = Status::Error(Err::IN_PROGRESS, "Transfer in progress");
  _transfer.result.address = address;
  _transfer.result.bytesRequested = length;
  _transfer.result.writeCommit =
      (kind == TransferKind::WRITE || kind == TransferKind::FILL ||
       kind == TransferKind::VERIFIED_WRITE)
          ? WriteCommit::NOT_COMMITTED
          : WriteCommit::NOT_APPLICABLE;
  if (kind == TransferKind::WRITE || kind == TransferKind::FILL ||
      kind == TransferKind::VERIFIED_WRITE) {
    _transfer.result.writeStatus =
        Status::Error(Err::IN_PROGRESS, "Write not attempted");
  }
  if (kind == TransferKind::VERIFY || kind == TransferKind::VERIFIED_WRITE) {
    _transfer.result.verifyStatus =
        Status::Error(Err::IN_PROGRESS, "Verify not attempted");
  }
  _transfer.data = data;
  _transfer.constData = constData;
  _transfer.fillValue = fillValue;
  _transfer.offset = 0;
  return Status::Ok();
}

Status MB85RC::_pollTransferInstruction() {
  if (!_transferBusy()) {
    return _transfer.result.status;
  }
  if (_transfer.result.state == TransferState::WAITING_FOR_RECONCILIATION) {
    return _transfer.result.status;
  }
  if (_transfer.offset >= _transfer.result.bytesRequested) {
    return Status::Ok();
  }

  const uint32_t chunkAddress =
      _transfer.result.address + static_cast<uint32_t>(_transfer.offset);
  size_t chunk = _transfer.result.bytesRequested - _transfer.offset;

  switch (_transfer.result.kind) {
    case TransferKind::READ:
      if (chunk > maxReadDataBytes()) {
        chunk = maxReadDataBytes();
      }
      {
        Status st = _readMemory(chunkAddress, _transfer.data + _transfer.offset, chunk);
        if (st.ok()) {
          _transfer.offset += chunk;
          _transfer.result.bytesCompleted = _transfer.offset;
        } else {
          _transfer.result.failedChunkOffset = _transfer.offset;
          _transfer.result.failedChunkLength = chunk;
        }
        return st;
      }

    case TransferKind::WRITE:
      if (chunk > maxWriteDataBytes()) {
        chunk = maxWriteDataBytes();
      }
      {
        WriteCommit commit = WriteCommit::INDETERMINATE;
        Status st = _writeMemory(chunkAddress,
                                 _transfer.constData + _transfer.offset,
                                 chunk, &commit);
        _transfer.result.writeCommit = commit;
        _transfer.result.writeStatus = st;
        if (st.ok()) {
          _transfer.offset += chunk;
          _transfer.result.bytesCompleted = _transfer.offset;
        } else {
          _transfer.result.failedChunkOffset = _transfer.offset;
          _transfer.result.failedChunkLength = chunk;
          if (commit == WriteCommit::ACCEPTED) {
            _transfer.result.bytesCompleted = _transfer.offset + chunk;
          }
        }
        return st;
      }

    case TransferKind::FILL:
      if (chunk > cmd::MAX_FILL_CHUNK) {
        chunk = cmd::MAX_FILL_CHUNK;
      }
      if (chunk > maxWriteDataBytes()) {
        chunk = maxWriteDataBytes();
      }
      {
        uint8_t fillBuf[cmd::MAX_FILL_CHUNK];
        std::memset(fillBuf, _transfer.fillValue, chunk);
        WriteCommit commit = WriteCommit::INDETERMINATE;
        Status st = _writeMemory(chunkAddress, fillBuf, chunk, &commit);
        _transfer.result.writeCommit = commit;
        _transfer.result.writeStatus = st;
        if (st.ok()) {
          _transfer.offset += chunk;
          _transfer.result.bytesCompleted = _transfer.offset;
        } else {
          _transfer.result.failedChunkOffset = _transfer.offset;
          _transfer.result.failedChunkLength = chunk;
          if (commit == WriteCommit::ACCEPTED) {
            _transfer.result.bytesCompleted = _transfer.offset + chunk;
          }
        }
        return st;
      }

    case TransferKind::VERIFY:
      if (chunk > maxReadDataBytes()) {
        chunk = maxReadDataBytes();
      }
      {
        uint8_t readBuf[cmd::MAX_READ_CHUNK];
        Status st = _readMemory(chunkAddress, readBuf, chunk);
        if (!st.ok()) {
          _transfer.result.verifyStatus = st;
          _transfer.result.failedChunkOffset = _transfer.offset;
          _transfer.result.failedChunkLength = chunk;
          return st;
        }

        for (size_t i = 0; i < chunk; ++i) {
          if (readBuf[i] != _transfer.constData[_transfer.offset + i]) {
            const size_t mismatch = _transfer.offset + i;
            _transfer.offset = mismatch;
            _transfer.result.bytesCompleted = mismatch;
            _transfer.result.mismatchOffset = mismatch;
            _transfer.result.failedChunkOffset = mismatch;
            _transfer.result.failedChunkLength = chunk - i;
            _transfer.result.expected = _transfer.constData[mismatch];
            _transfer.result.actual = readBuf[i];
            _transfer.result.match = false;
            _transfer.result.verifyStatus =
                Status::Error(Err::VERIFY_MISMATCH, "Verify mismatch",
                              static_cast<int32_t>(mismatch));
            return Status::Error(Err::VERIFY_MISMATCH, "Verify mismatch",
                                 static_cast<int32_t>(mismatch));
          }
        }
        _transfer.offset += chunk;
        _transfer.result.bytesCompleted = _transfer.offset;
        _transfer.result.match = (_transfer.offset == _transfer.result.bytesRequested);
        _transfer.result.verifyStatus = Status::Ok();
        return Status::Ok();
      }

    case TransferKind::VERIFIED_WRITE:
      if (!_transfer.verifyPhase) {
        WriteCommit commit = WriteCommit::INDETERMINATE;
        Status st = _writeMemory(_transfer.result.address,
                                 _transfer.constData,
                                 _transfer.result.bytesRequested,
                                 &commit);
        _transfer.result.writeStatus = st;
        _transfer.result.writeCommit = commit;
        if (st.ok()) {
          _transfer.verifyPhase = true;
          _transfer.result.bytesCompleted = _transfer.result.bytesRequested;
          return Status::Ok();
        }
        _transfer.result.failedChunkOffset = 0;
        _transfer.result.failedChunkLength = _transfer.result.bytesRequested;
        if (commit == WriteCommit::ACCEPTED) {
          _transfer.result.bytesCompleted = _transfer.result.bytesRequested;
        }
        if (commit == WriteCommit::INDETERMINATE ||
            commit == WriteCommit::ACCEPTED) {
          _transfer.result.state = TransferState::WAITING_FOR_RECONCILIATION;
          _transfer.result.status =
              Status::Error(Err::IN_PROGRESS,
                            "Waiting for owner reconciliation");
          return _transfer.result.status;
        }
        return st;
      }
      {
        uint8_t readBuf[cmd::MAX_READ_CHUNK];
        Status st = _readMemory(_transfer.result.address, readBuf,
                                _transfer.result.bytesRequested);
        _transfer.result.verifyStatus = st;
        if (!st.ok()) {
          _transfer.result.failedChunkOffset = 0;
          _transfer.result.failedChunkLength =
              _transfer.result.bytesRequested;
          return st;
        }
        for (size_t i = 0; i < _transfer.result.bytesRequested; ++i) {
          if (readBuf[i] != _transfer.constData[i]) {
            _transfer.offset = i;
            _transfer.result.bytesCompleted = i;
            _transfer.result.mismatchOffset = i;
            _transfer.result.failedChunkOffset = i;
            _transfer.result.failedChunkLength = _transfer.result.bytesRequested - i;
            _transfer.result.expected = _transfer.constData[i];
            _transfer.result.actual = readBuf[i];
            _transfer.result.match = false;
            _transfer.result.verifyStatus =
                Status::Error(Err::VERIFY_MISMATCH, "Verify mismatch",
                              static_cast<int32_t>(i));
            return _transfer.result.verifyStatus;
          }
        }
        _transfer.offset = _transfer.result.bytesRequested;
        _transfer.result.bytesCompleted = _transfer.result.bytesRequested;
        _transfer.result.match = true;
        _transfer.result.verifyStatus = Status::Ok();
        _transfer.result.writeCommit = WriteCommit::VERIFIED;
        return Status::Ok();
      }

    case TransferKind::NONE:
    default:
      return Status::Ok();
  }
}

uint32_t MB85RC::_allocateRequestId() {
  const uint32_t id = _nextRequestId;
  _nextRequestId = (id == std::numeric_limits<uint32_t>::max())
                       ? AUTOMATIC_REQUEST_ID_FIRST
                       : (id + 1U);
  return id;
}

Status MB85RC::_readMemory(uint32_t address, uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }
  if (len > maxReadDataBytes()) {
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
  if (len > maxReadDataBytes()) {
    return Status::Error(Err::INVALID_PARAM, "Read chunk too large");
  }

  EncodedMemoryAddress enc;
  Status st = _encodeMemoryAddress(address, enc);
  if (!st.ok()) {
    return st;
  }

  st = _i2cWriteReadRaw(enc.i2cAddress, enc.bytes, enc.len, buf, len);
  // Raw memory reads bypass normal cache advancement; do not leave a possibly
  // stale current-address pointer behind after probe/begin diagnostics.
  _currentAddressKnown = false;
  _currentAddress = 0;
  return st;
}

Status MB85RC::_writeMemory(uint32_t address, const uint8_t* buf, size_t len,
                            WriteCommit* writeCommit) {
  // Byte/Sequential Write: [S] [addr W] [addrHi] [addrLo] [data...] [P]
  // No write delay needed - FRAM writes immediately.
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid write buffer");
  }
  if (len > maxWriteDataBytes()) {
    return Status::Error(Err::INVALID_PARAM, "Write chunk too large");
  }

  uint8_t payload[MAX_TRANSPORT_TX_BYTES];
  EncodedMemoryAddress enc;
  Status st = _encodeMemoryAddress(address, enc);
  if (!st.ok()) {
    return st;
  }

  for (size_t i = 0; i < enc.len; ++i) {
    payload[i] = enc.bytes[i];
  }
  std::memcpy(&payload[enc.len], buf, len);

  st = _i2cWriteTrackedAddr(enc.i2cAddress, payload, enc.len + len, enc.len,
                            writeCommit);
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
  const uint8_t txBuf[1] = { static_cast<uint8_t>(_config.i2cAddress << 1) };
  Status ready = _ensureAwakeForI2c();
  if (!ready.ok()) {
    return ready;
  }
  I2cSpecialTransfer transfer = _specialTransfer(
      _config.i2cAddress, txBuf, 1, raw.bytes, cmd::DEVICE_ID_LEN);
  return _i2cSpecialRaw(I2cSpecialOp::READ_DEVICE_ID, transfer);
}

Status MB85RC::_readDeviceIdBytesTracked(DeviceIdRaw& raw) {
  const uint8_t txBuf[1] = { static_cast<uint8_t>(_config.i2cAddress << 1) };
  Status ready = _ensureAwakeForI2c();
  if (!ready.ok()) {
    return ready;
  }
  I2cSpecialTransfer transfer = _specialTransfer(
      _config.i2cAddress, txBuf, 1, raw.bytes, cmd::DEVICE_ID_LEN);
  return _i2cSpecialTracked(I2cSpecialOp::READ_DEVICE_ID, transfer);
}

bool MB85RC::_isValidAddress(uint32_t address) const {
  return _variant != nullptr && address <= maxAddress();
}

bool MB85RC::_fitsRange(uint32_t address, size_t len) const {
  if (len == 0U || !_isValidAddress(address)) {
    return false;
  }
  // Compare without casting: the usual arithmetic conversions widen whichever
  // side is narrower. Narrowing the remaining count to size_t instead would
  // truncate a 64 KiB+ capacity to zero wherever size_t is 16-bit.
  const uint32_t remaining = capacityBytes() - address;
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
                         detailFromU32(address));
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

Status MB85RC::_selectVariant(const DeviceId& id) {
  if (id.manufacturerId != cmd::MANUFACTURER_ID) {
    return Status::Error(Err::DEVICE_ID_MISMATCH, "Device ID manufacturer mismatch",
                         deviceIdDetail(id));
  }

  const cmd::VariantInfo* selected = cmd::findVariantByProductId(id.productId);
  if (selected == nullptr) {
    return Status::Error(Err::DEVICE_ID_MISMATCH, "Unknown Device ID product",
                         deviceIdDetail(id));
  }
  if (!isSupportedRuntimeVariant(*selected)) {
    return Status::Error(Err::DEVICE_ID_MISMATCH, "Unsupported Device ID product",
                         deviceIdDetail(id));
  }

  if (_variant != selected) {
    // High-speed enablement and the cached device address pointer belong to the
    // previous variant's capabilities and address space. Neither survives
    // re-identification. Sleep state needs no reset: reaching this point
    // required a Device ID read, which is only admitted while AWAKE.
    _highSpeedModeEnabled = false;
    _currentAddressKnown = false;
    _currentAddress = 0;
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
  return (_variant != nullptr) ? _variant->variant : DeviceVariant::AUTO;
}

Status MB85RC::_ensureAwakeForI2c() {
  if (!_initialized) {
    return Status::Ok();
  }
  if (_sleepState == SleepState::WAKING) {
    _advanceWakeState(_nowMs());
  }
  if (_sleepState == SleepState::UNKNOWN) {
    return busyStatus(BusyDetail::SLEEP_STATE_UNKNOWN,
                      "Sleep state unknown; call wake()");
  }
  if (_sleepState == SleepState::ASLEEP) {
    return busyStatus(BusyDetail::ASLEEP, "Device is asleep; call wake()");
  }
  if (_sleepState == SleepState::WAKING) {
    return busyStatus(BusyDetail::WAKING, "Sleep wake recovery pending");
  }
  return Status::Ok();
}

void MB85RC::_advanceWakeState(uint32_t nowMs) {
  if (_sleepState == SleepState::WAKING && deadlineReached(nowMs, _sleepWakeReadyMs)) {
    _sleepState = SleepState::AWAKE;
    _sleepWakeReadyMs = 0;
  }
}

I2cSpecialTransfer MB85RC::_specialTransfer(uint8_t i2cAddress,
                                            const uint8_t* txData,
                                            size_t txLen,
                                            uint8_t* rxData,
                                            size_t rxLen) const {
  I2cSpecialTransfer transfer;
  transfer.i2cAddress = i2cAddress;
  transfer.hsMasterCode = _config.highSpeedMasterCode;
  transfer.txData = txData;
  transfer.txLen = txLen;
  transfer.rxData = rxData;
  transfer.rxLen = rxLen;
  const uint16_t activeRecoveryUs = sleepRecoveryUs();
  transfer.recoveryUs =
      (activeRecoveryUs != 0U) ? activeRecoveryUs : cmd::SLEEP_RECOVERY_US;
  return transfer;
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

  if (!st.ok() && !shouldTrackHealthFailure(st.code)) {
    return st;
  }

  const uint32_t now = _nowMs();
  if (st.ok()) {
    _lastOkMs = now;
    _totalSuccess++;
    _consecutiveFailures = 0;
    _driverState = DriverState::READY;
    return st;
  }

  const uint8_t maxU8 = std::numeric_limits<uint8_t>::max();
  _lastError = st;
  _lastErrorMs = now;
  _totalFailures++;
  if (_consecutiveFailures < maxU8) {
    _consecutiveFailures++;
  }

  if (_config.offlineThreshold != 0U &&
      _consecutiveFailures >= _config.offlineThreshold) {
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
  return 0U;
}

}  // namespace MB85RC
