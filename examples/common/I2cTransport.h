/**
 * @file I2cTransport.h
 * @brief Wire-based I2C transport adapter for MB85RC examples.
 *
 * This file provides Wire-compatible I2C callbacks that can be
 * used with the MB85RC driver. The library does not depend on Wire
 * directly; this adapter bridges them.
 *
 * NOT part of the library API. Example-only.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "MB85RC/Config.h"

namespace transport {

inline bool interfaceReset(int sda, int scl) {
#if defined(ARDUINO_ARCH_ESP32)
  // 9 SCL pulses + STOP is the standard bus recovery / interface-reset sequence.
  pinMode(scl, OUTPUT);
  pinMode(sda, INPUT_PULLUP);
  for (int i = 0; i < 9; i++) {
    digitalWrite(scl, LOW);
    delayMicroseconds(5);
    digitalWrite(scl, HIGH);
    delayMicroseconds(5);
  }

  pinMode(sda, OUTPUT);
  digitalWrite(sda, LOW);
  delayMicroseconds(5);
  digitalWrite(scl, HIGH);
  delayMicroseconds(5);
  digitalWrite(sda, HIGH);
  delayMicroseconds(5);
#else
  (void)sda;
  (void)scl;
#endif
  return true;
}

inline MB85RC::TransportResult mapWireResult(uint8_t result, size_t txBytes,
                                             size_t rxBytes,
                                             bool memoryWriteMayCommit = false) {
  const MB85RC::WriteCommit uncertainCommit =
      memoryWriteMayCommit ? MB85RC::WriteCommit::INDETERMINATE
                           : MB85RC::WriteCommit::NOT_APPLICABLE;
  switch (result) {
    case 0:
      return MB85RC::TransportResult::Ok(txBytes, rxBytes);
    case 1:
      return MB85RC::TransportResult::Error(
          MB85RC::TransportCode::IO_ERROR, result,
          memoryWriteMayCommit ? MB85RC::WriteCommit::NOT_COMMITTED
                               : MB85RC::WriteCommit::NOT_APPLICABLE);
    case 2:
      return MB85RC::TransportResult::Error(
          MB85RC::TransportCode::NACK_ADDRESS, result,
          memoryWriteMayCommit ? MB85RC::WriteCommit::NOT_COMMITTED
                               : MB85RC::WriteCommit::NOT_APPLICABLE);
    case 3:
      return MB85RC::TransportResult::Error(MB85RC::TransportCode::NACK_DATA,
                                             result, uncertainCommit);
    case 4:
      return MB85RC::TransportResult::Error(MB85RC::TransportCode::BUS_ERROR,
                                             result, uncertainCommit);
    case 5:
      return MB85RC::TransportResult::Error(MB85RC::TransportCode::TIMEOUT,
                                             result, uncertainCommit);
    default:
      return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR,
                                             result, uncertainCommit);
  }
}

/**
 * @brief Wire-based I2C write implementation.
 *
 * Pass to Config::i2cWrite, and pass &Wire (or a custom TwoWire*) to i2cUser.
 * The timeout parameter is advisory; bus timeout ownership stays with initWire().
 *
 * @param addr I2C 7-bit address
 * @param data Data buffer to send
 * @param len Number of bytes
 * @param timeoutMs Timeout requested by the driver (advisory only)
 * @param user Pointer to TwoWire instance
 * @return Terminal result for exactly one physical I2C transaction.
 */
inline MB85RC::TransportResult wireWrite(uint8_t addr, const uint8_t* data,
                                         size_t len, uint32_t timeoutMs,
                                         void* user) {
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, -1,
        MB85RC::WriteCommit::NOT_COMMITTED);
  }
  (void)timeoutMs;
  if (!data || len == 0) {
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, -2,
        MB85RC::WriteCommit::NOT_COMMITTED);
  }

  // Check for oversized writes (ESP32 Wire buffer is 128 bytes)
  if (len > 128) {
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, static_cast<int32_t>(len),
        MB85RC::WriteCommit::NOT_COMMITTED);
  }

  wire->beginTransmission(addr);
  size_t written = wire->write(data, len);
  if (written != len) {
    // Bytes copied into Wire's software buffer have not reached the I2C bus.
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, static_cast<int32_t>(written),
        MB85RC::WriteCommit::NOT_COMMITTED, 0U, 0U);
  }

  uint8_t result = wire->endTransmission(true);  // Send STOP
  return mapWireResult(result, len, 0U, true);
}

/**
 * @brief Wire-based I2C write-read implementation.
 *
 * Pass to Config::i2cWriteRead, and pass &Wire (or a custom TwoWire*) to i2cUser.
 * The timeout parameter is advisory; bus timeout ownership stays with initWire().
 *
 * @param addr I2C 7-bit address
 * @param tx TX buffer to send (nullable when txLen == 0)
 * @param txLen TX length
 * @param rx RX buffer for readback
 * @param rxLen RX length
 * @param timeoutMs Timeout requested by the driver (advisory only)
 * @param user Pointer to TwoWire instance
 * @return Terminal result for exactly one physical I2C transaction.
 */
inline MB85RC::TransportResult wireWriteRead(uint8_t addr, const uint8_t* tx,
                                             size_t txLen, uint8_t* rx,
                                             size_t rxLen, uint32_t timeoutMs,
                                             void* user) {
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR, -1);
  }
  (void)timeoutMs;
  if ((txLen > 0 && tx == nullptr) || (rxLen > 0 && rx == nullptr)) {
    return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR, -2);
  }
  if (txLen == 0 && rxLen == 0) {
    return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR, -3);
  }
  if (txLen > 128 || rxLen > 128) {
    return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR, -4);
  }

  if (txLen > 0) {
    wire->beginTransmission(addr);
    size_t written = wire->write(tx, txLen);
    if (written != txLen) {
      // No endTransmission() means no physical TX phase has started.
      return MB85RC::TransportResult::Error(
          MB85RC::TransportCode::IO_ERROR, static_cast<int32_t>(written),
          MB85RC::WriteCommit::NOT_APPLICABLE, 0U, 0U);
    }

    uint8_t result = wire->endTransmission(false);  // Repeated start
    if (result != 0) {
      return mapWireResult(result, 0U, 0U);
    }
  }

  size_t read = wire->requestFrom(addr, static_cast<uint8_t>(rxLen));
  if (read != rxLen) {
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, static_cast<int32_t>(read),
        MB85RC::WriteCommit::NOT_APPLICABLE, txLen, read);
  }

  for (size_t i = 0; i < rxLen; ++i) {
    if (wire->available()) {
      rx[i] = static_cast<uint8_t>(wire->read());
    } else {
      return MB85RC::TransportResult::Error(
          MB85RC::TransportCode::IO_ERROR, static_cast<int32_t>(i),
          MB85RC::WriteCommit::NOT_APPLICABLE, txLen, i);
    }
  }

  return MB85RC::TransportResult::Ok(txLen, rxLen);
}

/**
 * @brief Wire implementation of the reserved Device ID transaction.
 *
 * Device ID is deliberately routed through the special callback so a normal
 * owner backend and its 0x03..0x77 scan policy do not need to accept 0x7C.
 * High-speed and Sleep controller sequences remain unsupported by this simple
 * example adapter.
 */
inline MB85RC::TransportResult wireSpecial(
    MB85RC::I2cSpecialOp op, const MB85RC::I2cSpecialTransfer& transfer,
    uint32_t timeoutMs, void* user) {
  if (op != MB85RC::I2cSpecialOp::READ_DEVICE_ID ||
      transfer.txData == nullptr || transfer.txLen != 1U ||
      transfer.rxData == nullptr || transfer.rxLen != MB85RC::cmd::DEVICE_ID_LEN) {
    return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR, -5);
  }
  const MB85RC::TransportResult result =
      wireWriteRead(0x7CU, transfer.txData, transfer.txLen, transfer.rxData,
                    transfer.rxLen, timeoutMs, user);
  return result.ok()
             ? MB85RC::TransportResult::Ok(transfer.txLen, transfer.rxLen)
             : MB85RC::TransportResult::Error(result.code, result.detail);
}

/**
 * @brief Initialize Wire with default pins and frequency.
 *
 * @param sda SDA pin number
 * @param scl SCL pin number
 * @param freq I2C clock frequency in Hz (default 400kHz)
 * @param timeoutMs I2C timeout in milliseconds (default 50ms)
 * @return true on success
 */
inline bool initWire(int sda, int scl, uint32_t freq = 400000, uint16_t timeoutMs = 50) {
  interfaceReset(sda, scl);
  Wire.begin(sda, scl);
  Wire.setClock(freq);
  Wire.setTimeOut(timeoutMs);
  return true;
}

}  // namespace transport
