/**
 * @file IdfI2cTransport.h
 * @brief Example-only I2C result mapping and dispatch, testable without an SDK.
 *
 * The native ESP-IDF example supplies its error constants and transaction
 * functions. This header has no framework dependency and is not public API.
 */

#pragma once

#include "MB85RC/Config.h"

namespace idf_transport {

struct ResultMapper {
  int32_t ok;
  int32_t timeout;
  int32_t invalidArgument;
  int32_t invalidResponse;
  int32_t notFound;

  constexpr MB85RC::TransportCode mapI2cCode(int32_t err) const {
    return err == ok
               ? MB85RC::TransportCode::OK
               : (err == timeout ? MB85RC::TransportCode::TIMEOUT
                  : (err == invalidResponse || err == notFound
                         ? MB85RC::TransportCode::NACK_UNSPECIFIED
                         : MB85RC::TransportCode::IO_ERROR));
  }

  constexpr MB85RC::WriteCommit mapI2cFailureCommit(
      int32_t err, MB85RC::WriteCommit failureCommit) const {
    // NACKs do not identify the rejected byte. Only invalid arguments prove
    // that a submitted memory write could not start; otherwise retain the
    // caller's evidence (including failures before a transaction was submitted).
    return err == invalidArgument &&
                   failureCommit != MB85RC::WriteCommit::NOT_APPLICABLE
               ? MB85RC::WriteCommit::NOT_COMMITTED
               : failureCommit;
  }

  MB85RC::TransportResult mapI2c(
      int32_t err, size_t txBytes, size_t rxBytes,
      MB85RC::WriteCommit failureCommit = MB85RC::WriteCommit::NOT_APPLICABLE) const {
    if (err == ok) {
      return MB85RC::TransportResult::Ok(txBytes, rxBytes);
    }
    return MB85RC::TransportResult::Error(
        mapI2cCode(err), err, mapI2cFailureCommit(err, failureCommit));
  }
};

// A TX-only request must use a STOP-terminated transmit. SDK transmit-receive
// functions require a nonempty receive buffer.
template <typename Device, typename Transmit, typename Receive,
          typename TransmitReceive>
int32_t writeRead(Device dev, const uint8_t* tx, size_t txLen,
                  uint8_t* rx, size_t rxLen, int timeoutMs,
                  Transmit transmit, Receive receive,
                  TransmitReceive transmitReceive) {
  if (txLen == 0U) {
    return receive(dev, rx, rxLen, timeoutMs);
  }
  if (rxLen == 0U) {
    return transmit(dev, tx, txLen, timeoutMs);
  }
  return transmitReceive(dev, tx, txLen, rx, rxLen, timeoutMs);
}

}  // namespace idf_transport
