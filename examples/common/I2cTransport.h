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

/// Wire capacity owned by this adapter. The core's staging bound and the
/// framework buffer are independent, so use the smaller value.
#if defined(I2C_BUFFER_LENGTH)
static constexpr size_t WIRE_BUFFER_BYTES = I2C_BUFFER_LENGTH;
#elif defined(BUFFER_LENGTH)
static constexpr size_t WIRE_BUFFER_BYTES = BUFFER_LENGTH;
#else
static constexpr size_t WIRE_BUFFER_BYTES = 32U;
#endif
static constexpr size_t MAX_TX_BYTES =
    (WIRE_BUFFER_BYTES < MB85RC::MAX_TRANSPORT_TX_BYTES)
        ? WIRE_BUFFER_BYTES
        : MB85RC::MAX_TRANSPORT_TX_BYTES;
static constexpr size_t MAX_RX_BYTES =
    (WIRE_BUFFER_BYTES < MB85RC::MAX_TRANSPORT_RX_BYTES)
        ? WIRE_BUFFER_BYTES
        : MB85RC::MAX_TRANSPORT_RX_BYTES;

/// Managed state prevents callbacks from entering Wire after failed re-init,
/// when Arduino-ESP32 may have no buffers and cannot safely close a transaction.
struct WireContext {
  TwoWire* wire = nullptr;
  bool ready = false;
};

inline bool interfaceReset(WireContext& context, int sda, int scl,
                           uint32_t freq, uint16_t timeoutMs) {
  context.ready = false;
  if (context.wire == nullptr) {
    return false;
  }
  TwoWire& wire = *context.wire;
  wire.end();
#if defined(ARDUINO_ARCH_ESP32) || defined(MB85RC_TEST_WIRE_STUB)
  // 9 SCL pulses + STOP is the standard bus recovery / interface-reset sequence.
  // Open drain only: a stuck slave may be holding SDA low, and a push-pull
  // HIGH would short both output stages together.
  pinMode(scl, OUTPUT_OPEN_DRAIN);
  pinMode(sda, OUTPUT_OPEN_DRAIN);
  digitalWrite(sda, HIGH);
  for (int i = 0; i < 9; i++) {
    digitalWrite(scl, LOW);
    delayMicroseconds(5);
    digitalWrite(scl, HIGH);
    delayMicroseconds(5);
  }

  // STOP: SDA rises while SCL is high.
  digitalWrite(sda, LOW);
  delayMicroseconds(5);
  digitalWrite(scl, HIGH);
  delayMicroseconds(5);
  digitalWrite(sda, HIGH);
  delayMicroseconds(5);
  const bool sdaReleased = digitalRead(sda) != LOW;
  if (wire.setBufferSize(WIRE_BUFFER_BYTES) < WIRE_BUFFER_BYTES) {
    return false;
  }
  const bool started = wire.begin(sda, scl, freq);
  wire.setTimeOut(timeoutMs);
  context.ready = started && sdaReleased;
  return context.ready;
#else
  // This example package targets ESP32-S2/S3. Keep transaction callbacks
  // portable, but do not pretend the ESP32 pin-recovery/init API exists on AVR.
  (void)sda;
  (void)scl;
  (void)freq;
  (void)timeoutMs;
  return false;
#endif
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
 * Pass to Config::i2cWrite and pass the initialized WireContext to i2cUser.
 * The timeout parameter is advisory; bus timeout ownership stays with initWire().
 *
 * @param addr I2C 7-bit address
 * @param data Data buffer to send
 * @param len Number of bytes
 * @param timeoutMs Timeout requested by the driver (advisory only)
 * @param user Pointer to the initialized WireContext
 * @return Terminal result for exactly one physical I2C transaction.
 */
inline MB85RC::TransportResult wireWrite(uint8_t addr, const uint8_t* data,
                                         size_t len, uint32_t timeoutMs,
                                         void* user) {
  WireContext* context = static_cast<WireContext*>(user);
  if (context == nullptr || !context->ready || context->wire == nullptr) {
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, -1,
        MB85RC::WriteCommit::NOT_COMMITTED);
  }
  TwoWire* wire = context->wire;
  (void)timeoutMs;
  if (!data || len == 0) {
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, -2,
        MB85RC::WriteCommit::NOT_COMMITTED);
  }

  if (len > MAX_TX_BYTES) {
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, static_cast<int32_t>(len),
        MB85RC::WriteCommit::NOT_COMMITTED);
  }

  wire->beginTransmission(addr);
  size_t written = wire->write(data, len);
  if (written != len) {
    // The managed buffer bound makes this unreachable during normal operation.
    // Close the transaction to release Wire's lock. Only a local-buffer error
    // or address NACK proves no requested data was accepted; other outcomes
    // leave a nonzero buffered prefix indeterminate.
    const uint8_t cleanupResult = wire->endTransmission(true);
    const size_t completed = (cleanupResult == 0U) ? written : 0U;
    const bool noDataAccepted =
        written == 0U || cleanupResult == 1U || cleanupResult == 2U;
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, static_cast<int32_t>(written),
        noDataAccepted ? MB85RC::WriteCommit::NOT_COMMITTED
                       : MB85RC::WriteCommit::INDETERMINATE,
        completed, 0U);
  }

  uint8_t result = wire->endTransmission(true);  // Send STOP
  return mapWireResult(result, len, 0U, true);
}

/**
 * @brief Wire-based I2C write-read implementation.
 *
 * Pass to Config::i2cWriteRead and pass the initialized WireContext to i2cUser.
 * The timeout parameter is advisory; bus timeout ownership stays with initWire().
 *
 * @param addr I2C 7-bit address
 * @param tx TX buffer to send (nullable when txLen == 0)
 * @param txLen TX length
 * @param rx RX buffer for readback
 * @param rxLen RX length
 * @param timeoutMs Timeout requested by the driver (advisory only)
 * @param user Pointer to the initialized WireContext
 * @return Terminal result for exactly one physical I2C transaction.
 */
inline MB85RC::TransportResult wireWriteRead(uint8_t addr, const uint8_t* tx,
                                             size_t txLen, uint8_t* rx,
                                             size_t rxLen, uint32_t timeoutMs,
                                             void* user) {
  WireContext* context = static_cast<WireContext*>(user);
  if (context == nullptr || !context->ready || context->wire == nullptr) {
    return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR, -1);
  }
  TwoWire* wire = context->wire;
  (void)timeoutMs;
  if ((txLen > 0 && tx == nullptr) || (rxLen > 0 && rx == nullptr)) {
    return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR, -2);
  }
  if (txLen == 0 && rxLen == 0) {
    return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR, -3);
  }
  if (txLen > MAX_TX_BYTES || rxLen > MAX_RX_BYTES) {
    return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR, -4);
  }

  if (txLen > 0) {
    wire->beginTransmission(addr);
    size_t written = wire->write(tx, txLen);
    if (written != txLen) {
      // The managed bound makes this abnormal. Closing the transaction may
      // physically send the buffered prefix, which is reported truthfully.
      const uint8_t cleanupResult = wire->endTransmission(true);
      const size_t completed = (cleanupResult == 0U) ? written : 0U;
      return MB85RC::TransportResult::Error(
          MB85RC::TransportCode::IO_ERROR, static_cast<int32_t>(written),
          MB85RC::WriteCommit::NOT_APPLICABLE, completed, 0U);
    }

    const bool stopAfterWrite = rxLen == 0U;
    uint8_t result = wire->endTransmission(stopAfterWrite);
    if (result != 0) {
      return mapWireResult(result, 0U, 0U);
    }
    if (stopAfterWrite) {
      return MB85RC::TransportResult::Ok(txLen, 0U);
    }
  }

  size_t read = wire->requestFrom(addr, static_cast<uint8_t>(rxLen));
  if (read != rxLen) {
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, static_cast<int32_t>(read),
        MB85RC::WriteCommit::NOT_APPLICABLE, 0U, read);
  }

  for (size_t i = 0; i < rxLen; ++i) {
    if (wire->available()) {
      rx[i] = static_cast<uint8_t>(wire->read());
    } else {
      return MB85RC::TransportResult::Error(
          MB85RC::TransportCode::IO_ERROR, static_cast<int32_t>(i),
          MB85RC::WriteCommit::NOT_APPLICABLE, 0U, i);
    }
  }

  return MB85RC::TransportResult::Ok(txLen, rxLen);
}

/**
 * @brief Wire implementation of the reserved Device ID transaction.
 *
 * Device ID is deliberately routed through the special callback. It reuses the
 * Wire transaction helper internally, but Config::i2cWriteRead is never asked
 * by the core to accept the reserved 0x7C address.
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
  return wireWriteRead(0x7CU, transfer.txData, transfer.txLen, transfer.rxData,
                       transfer.rxLen, timeoutMs, user);
}

/**
 * @brief Initialize Wire with default pins and frequency.
 *
 * @param context Adapter state retained for the lifetime of the driver binding
 * @param wire Wire instance owned by the application
 * @param sda SDA pin number
 * @param scl SCL pin number
 * @param freq I2C clock frequency in Hz (default 400kHz)
 * @param timeoutMs I2C timeout in milliseconds (default 50ms)
 * @return true on success
 */
inline bool initWire(WireContext& context, TwoWire& wire, int sda, int scl,
                     uint32_t freq = 400000, uint16_t timeoutMs = 50) {
  context.wire = &wire;
  return interfaceReset(context, sda, scl, freq, timeoutMs);
}

}  // namespace transport
