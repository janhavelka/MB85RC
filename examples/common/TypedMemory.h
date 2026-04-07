/**
 * @file TypedMemory.h
 * @brief Example-only fixed-width typed access helpers for MB85RC FRAM.
 *
 * These helpers sit above the raw byte-oriented driver API. They intentionally:
 * - encode multi-byte values in little-endian order
 * - reject values that would cross 0x7FFF -> 0x0000
 * - avoid storing raw struct layouts with padding/ABI ambiguity
 *
 * NOT part of the public library API.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits>

#include "MB85RC/MB85RC.h"

namespace typed_memory {

inline bool fitsContiguous(uint16_t address, size_t len) {
  return len > 0U &&
         address <= MB85RC::cmd::MAX_MEM_ADDRESS &&
         (static_cast<uint32_t>(address) + static_cast<uint32_t>(len)) <=
             static_cast<uint32_t>(MB85RC::cmd::MEMORY_SIZE);
}

inline MB85RC::Status writeBytes(MB85RC::MB85RC& device,
                                 uint16_t address,
                                 const uint8_t* data,
                                 size_t len) {
  if (data == nullptr || len == 0U) {
    return MB85RC::Status::Error(MB85RC::Err::INVALID_PARAM, "Typed write buffer invalid");
  }
  if (!fitsContiguous(address, len)) {
    return MB85RC::Status::Error(MB85RC::Err::ADDRESS_OUT_OF_RANGE,
                                 "Typed value crosses end of memory",
                                 static_cast<int32_t>(address));
  }
  return device.write(address, data, len);
}

inline MB85RC::Status readBytes(MB85RC::MB85RC& device,
                                uint16_t address,
                                uint8_t* data,
                                size_t len) {
  if (data == nullptr || len == 0U) {
    return MB85RC::Status::Error(MB85RC::Err::INVALID_PARAM, "Typed read buffer invalid");
  }
  if (!fitsContiguous(address, len)) {
    return MB85RC::Status::Error(MB85RC::Err::ADDRESS_OUT_OF_RANGE,
                                 "Typed value crosses end of memory",
                                 static_cast<int32_t>(address));
  }
  return device.read(address, data, len);
}

inline void encodeUint16Le(uint16_t value, uint8_t out[2]) {
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

inline void encodeUint32Le(uint32_t value, uint8_t out[4]) {
  out[0] = static_cast<uint8_t>(value & 0xFFUL);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xFFUL);
  out[2] = static_cast<uint8_t>((value >> 16) & 0xFFUL);
  out[3] = static_cast<uint8_t>((value >> 24) & 0xFFUL);
}

inline void encodeUint64Le(uint64_t value, uint8_t out[8]) {
  for (size_t i = 0; i < 8U; ++i) {
    out[i] = static_cast<uint8_t>((value >> (8U * i)) & 0xFFULL);
  }
}

inline uint16_t decodeUint16Le(const uint8_t in[2]) {
  return static_cast<uint16_t>(static_cast<uint16_t>(in[0]) |
                               (static_cast<uint16_t>(in[1]) << 8));
}

inline uint32_t decodeUint32Le(const uint8_t in[4]) {
  return static_cast<uint32_t>(static_cast<uint32_t>(in[0]) |
                               (static_cast<uint32_t>(in[1]) << 8) |
                               (static_cast<uint32_t>(in[2]) << 16) |
                               (static_cast<uint32_t>(in[3]) << 24));
}

inline uint64_t decodeUint64Le(const uint8_t in[8]) {
  uint64_t value = 0;
  for (size_t i = 0; i < 8U; ++i) {
    value |= (static_cast<uint64_t>(in[i]) << (8U * i));
  }
  return value;
}

inline MB85RC::Status writeBool(MB85RC::MB85RC& device, uint16_t address, bool value) {
  return device.writeByte(address, value ? 1U : 0U);
}

inline MB85RC::Status readBool(MB85RC::MB85RC& device, uint16_t address, bool& value) {
  uint8_t raw = 0;
  MB85RC::Status st = device.readByte(address, raw);
  if (!st.ok()) {
    return st;
  }
  value = (raw != 0U);
  return MB85RC::Status::Ok();
}

inline MB85RC::Status writeUint8(MB85RC::MB85RC& device, uint16_t address, uint8_t value) {
  return device.writeByte(address, value);
}

inline MB85RC::Status readUint8(MB85RC::MB85RC& device, uint16_t address, uint8_t& value) {
  return device.readByte(address, value);
}

inline MB85RC::Status writeInt8(MB85RC::MB85RC& device, uint16_t address, int8_t value) {
  return device.writeByte(address, static_cast<uint8_t>(value));
}

inline MB85RC::Status readInt8(MB85RC::MB85RC& device, uint16_t address, int8_t& value) {
  uint8_t raw = 0;
  MB85RC::Status st = device.readByte(address, raw);
  if (!st.ok()) {
    return st;
  }
  value = static_cast<int8_t>(raw);
  return MB85RC::Status::Ok();
}

inline MB85RC::Status writeUint16Le(MB85RC::MB85RC& device, uint16_t address, uint16_t value) {
  uint8_t raw[2];
  encodeUint16Le(value, raw);
  return writeBytes(device, address, raw, sizeof(raw));
}

inline MB85RC::Status readUint16Le(MB85RC::MB85RC& device, uint16_t address, uint16_t& value) {
  uint8_t raw[2] = {};
  MB85RC::Status st = readBytes(device, address, raw, sizeof(raw));
  if (!st.ok()) {
    return st;
  }
  value = decodeUint16Le(raw);
  return MB85RC::Status::Ok();
}

inline MB85RC::Status writeInt16Le(MB85RC::MB85RC& device, uint16_t address, int16_t value) {
  return writeUint16Le(device, address, static_cast<uint16_t>(value));
}

inline MB85RC::Status readInt16Le(MB85RC::MB85RC& device, uint16_t address, int16_t& value) {
  uint16_t raw = 0;
  MB85RC::Status st = readUint16Le(device, address, raw);
  if (!st.ok()) {
    return st;
  }
  value = static_cast<int16_t>(raw);
  return MB85RC::Status::Ok();
}

inline MB85RC::Status writeUint32Le(MB85RC::MB85RC& device, uint16_t address, uint32_t value) {
  uint8_t raw[4];
  encodeUint32Le(value, raw);
  return writeBytes(device, address, raw, sizeof(raw));
}

inline MB85RC::Status readUint32Le(MB85RC::MB85RC& device, uint16_t address, uint32_t& value) {
  uint8_t raw[4] = {};
  MB85RC::Status st = readBytes(device, address, raw, sizeof(raw));
  if (!st.ok()) {
    return st;
  }
  value = decodeUint32Le(raw);
  return MB85RC::Status::Ok();
}

inline MB85RC::Status writeInt32Le(MB85RC::MB85RC& device, uint16_t address, int32_t value) {
  return writeUint32Le(device, address, static_cast<uint32_t>(value));
}

inline MB85RC::Status readInt32Le(MB85RC::MB85RC& device, uint16_t address, int32_t& value) {
  uint32_t raw = 0;
  MB85RC::Status st = readUint32Le(device, address, raw);
  if (!st.ok()) {
    return st;
  }
  value = static_cast<int32_t>(raw);
  return MB85RC::Status::Ok();
}

inline MB85RC::Status writeUint64Le(MB85RC::MB85RC& device, uint16_t address, uint64_t value) {
  uint8_t raw[8];
  encodeUint64Le(value, raw);
  return writeBytes(device, address, raw, sizeof(raw));
}

inline MB85RC::Status readUint64Le(MB85RC::MB85RC& device, uint16_t address, uint64_t& value) {
  uint8_t raw[8] = {};
  MB85RC::Status st = readBytes(device, address, raw, sizeof(raw));
  if (!st.ok()) {
    return st;
  }
  value = decodeUint64Le(raw);
  return MB85RC::Status::Ok();
}

inline MB85RC::Status writeInt64Le(MB85RC::MB85RC& device, uint16_t address, int64_t value) {
  return writeUint64Le(device, address, static_cast<uint64_t>(value));
}

inline MB85RC::Status readInt64Le(MB85RC::MB85RC& device, uint16_t address, int64_t& value) {
  uint64_t raw = 0;
  MB85RC::Status st = readUint64Le(device, address, raw);
  if (!st.ok()) {
    return st;
  }
  value = static_cast<int64_t>(raw);
  return MB85RC::Status::Ok();
}

inline MB85RC::Status writeFloat32Le(MB85RC::MB85RC& device, uint16_t address, float value) {
  static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32-bit");
  static_assert(std::numeric_limits<float>::is_iec559, "float must use IEEE-754 encoding");

  uint32_t raw = 0;
  memcpy(&raw, &value, sizeof(raw));
  return writeUint32Le(device, address, raw);
}

inline MB85RC::Status readFloat32Le(MB85RC::MB85RC& device, uint16_t address, float& value) {
  static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32-bit");
  static_assert(std::numeric_limits<float>::is_iec559, "float must use IEEE-754 encoding");

  uint32_t raw = 0;
  MB85RC::Status st = readUint32Le(device, address, raw);
  if (!st.ok()) {
    return st;
  }
  memcpy(&value, &raw, sizeof(value));
  return MB85RC::Status::Ok();
}

inline MB85RC::Status writeFloat64Le(MB85RC::MB85RC& device, uint16_t address, double value) {
  static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64-bit");
  static_assert(std::numeric_limits<double>::is_iec559, "double must use IEEE-754 encoding");

  uint64_t raw = 0;
  memcpy(&raw, &value, sizeof(raw));
  return writeUint64Le(device, address, raw);
}

inline MB85RC::Status readFloat64Le(MB85RC::MB85RC& device, uint16_t address, double& value) {
  static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64-bit");
  static_assert(std::numeric_limits<double>::is_iec559, "double must use IEEE-754 encoding");

  uint64_t raw = 0;
  MB85RC::Status st = readUint64Le(device, address, raw);
  if (!st.ok()) {
    return st;
  }
  memcpy(&value, &raw, sizeof(value));
  return MB85RC::Status::Ok();
}

}  // namespace typed_memory
