/**
 * @file DiagnosticCore.h
 * @brief Example-only diagnostic helpers with no framework or output dependency.
 *
 * Callers own the device, clock, buffers, and presentation. Not public API.
 */
#pragma once

#include <limits>
#include "MB85RC/MB85RC.h"

namespace diagnostic {

inline const char* errToStr(MB85RC::Err err) {
  using namespace MB85RC;
  switch (err) {
    case Err::OK:                   return "OK";
    case Err::NOT_INITIALIZED:      return "NOT_INITIALIZED";
    case Err::INVALID_CONFIG:       return "INVALID_CONFIG";
    case Err::I2C_ERROR:            return "I2C_ERROR";
    case Err::TIMEOUT:              return "TIMEOUT";
    case Err::INVALID_PARAM:        return "INVALID_PARAM";
    case Err::DEVICE_NOT_FOUND:     return "DEVICE_NOT_FOUND";
    case Err::DEVICE_ID_MISMATCH:   return "DEVICE_ID_MISMATCH";
    case Err::ADDRESS_OUT_OF_RANGE: return "ADDRESS_OUT_OF_RANGE";
    case Err::WRITE_PROTECTED:      return "WRITE_PROTECTED";
    case Err::BUSY:                 return "BUSY";
    case Err::IN_PROGRESS:          return "IN_PROGRESS";
    case Err::I2C_NACK_ADDR:        return "I2C_NACK_ADDR";
    case Err::I2C_NACK_DATA:        return "I2C_NACK_DATA";
    case Err::I2C_TIMEOUT:          return "I2C_TIMEOUT";
    case Err::I2C_BUS:              return "I2C_BUS";
    case Err::VERIFY_MISMATCH:      return "VERIFY_MISMATCH";
    case Err::UNSUPPORTED:          return "UNSUPPORTED";
    case Err::NO_RESULT:            return "NO_RESULT";
    case Err::CANCELLED:            return "CANCELLED";
    case Err::I2C_NACK:             return "I2C_NACK";
    default:                        return "UNKNOWN";
  }
}

inline const char* stateToStr(MB85RC::DriverState st) {
  using namespace MB85RC;
  switch (st) {
    case DriverState::UNINIT:   return "UNINIT";
    case DriverState::READY:    return "READY";
    case DriverState::DEGRADED: return "DEGRADED";
    case DriverState::OFFLINE:  return "OFFLINE";
    default:                    return "UNKNOWN";
  }
}

inline const char* sleepStateToStr(MB85RC::SleepState st) {
  using namespace MB85RC;
  switch (st) {
    case SleepState::AWAKE:  return "AWAKE";
    case SleepState::ASLEEP: return "ASLEEP";
    case SleepState::WAKING: return "WAKING";
    default:                 return "UNKNOWN";
  }
}

inline const char* addressModelToStr(MB85RC::cmd::AddressModel model) {
  using MB85RC::cmd::AddressModel;
  switch (model) {
    case AddressModel::TWO_BYTE_ADDRESS_PINS:
      return "2-byte address, A2/A1/A0 select device";
    case AddressModel::TWO_BYTE_A16_IN_DEVICE_ADDRESS:
      return "2-byte address, A16 in device address";
    case AddressModel::ONE_BYTE_UPPER_BITS_IN_DEVICE_ADDRESS:
      return "1-byte address, upper address bits in device address";
    case AddressModel::ONE_BYTE_A8_IN_DEVICE_ADDRESS:
      return "1-byte address, A8 in device address";
    default:
      return "unknown";
  }
}

inline const char* deviceVariantToStr(MB85RC::DeviceVariant variant) {
  switch (variant) {
    case MB85RC::DeviceVariant::AUTO:
      return "AUTO";
    case MB85RC::DeviceVariant::MB85RC256V:
      return "MB85RC256V";
    case MB85RC::DeviceVariant::MB85RC64TA:
      return "MB85RC64TA";
    case MB85RC::DeviceVariant::MB85RC04V:
      return "MB85RC04V";
    case MB85RC::DeviceVariant::MB85RC16V:
      return "MB85RC16V";
    case MB85RC::DeviceVariant::MB85RC512T:
      return "MB85RC512T";
    case MB85RC::DeviceVariant::MB85RC1MT:
      return "MB85RC1MT";
    default:
      return "UNKNOWN";
  }
}

inline uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if ((crc & 1U) != 0U) {
        crc = (crc >> 1) ^ 0xEDB88320UL;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

inline bool rangeFits(const MB85RC::MB85RC& dev, uint32_t address, size_t len) {
  const uint32_t capacity = dev.capacityBytes();
  return len > 0U && address < capacity && len <= capacity - address;
}

inline MB85RC::Status restoreVerified(MB85RC::MB85RC& dev, uint32_t address,
                                      const uint8_t* original, size_t len) {
  return dev.writeVerify(address, original, len);
}

inline MB85RC::Status takeStagedTerminal(MB85RC::MB85RC& dev,
                                         MB85RC::Status terminal) {
  MB85RC::TransferResult result;
  const MB85RC::Status taken = dev.takeTransferResult(result);
  return taken.ok() ? result.status : terminal;
}

inline MB85RC::Status pollStagedTransferToCompletion(
    MB85RC::MB85RC& dev, size_t len, size_t chunkSize,
    MB85RC::NowMsFn nowMs, void* timeUser = nullptr) {
  if (len == 0U || chunkSize == 0U || nowMs == nullptr) {
    return MB85RC::Status::Error(MB85RC::Err::INVALID_PARAM, "Invalid staged transfer poll bounds");
  }
  const size_t expectedChunks = (len - 1U) / chunkSize + 1U;
  if (expectedChunks > std::numeric_limits<size_t>::max() - 3U) {
    return MB85RC::Status::Error(MB85RC::Err::INVALID_PARAM, "Staged poll limit overflows");
  }
  const size_t pollLimit = expectedChunks + 3U;
  for (size_t i = 0; i < pollLimit; ++i) {
    MB85RC::Status st = dev.pollTransfer(nowMs(timeUser), 1);
    if (st.inProgress()) {
      continue;
    }
    return takeStagedTerminal(dev, st);
  }
  (void)dev.cancelTransfer();
  MB85RC::TransferResult cancelled;
  (void)dev.takeTransferResult(cancelled);
  return MB85RC::Status::Error(MB85RC::Err::TIMEOUT, "Staged transfer poll limit exhausted");
}

}  // namespace diagnostic
