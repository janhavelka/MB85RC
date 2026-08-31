/// @file Wire.h
/// @brief Minimal Wire stub for native testing
#pragma once

#include <cstddef>
#include <cstdint>

#define MB85RC_TEST_WIRE_STUB 1
#define I2C_BUFFER_LENGTH 128U

static constexpr int OUTPUT_OPEN_DRAIN = 1;
static constexpr int HIGH = 1;
static constexpr int LOW = 0;
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline void delayMicroseconds(unsigned int) {}

class TwoWire {
public:
  bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0U) {
    (void)sda;
    (void)scl;
    (void)frequency;
    return _beginResult;
  }
  void setTimeOut(uint32_t timeoutMs) { _timeoutMs = timeoutMs; }
  uint32_t getTimeOut() const { return _timeoutMs; }
  size_t setBufferSize(size_t size) {
    _bufferSize = size <= sizeof(_txBuf) ? size : sizeof(_txBuf);
    return _bufferSize;
  }

  void beginTransmission(uint8_t addr) {
    _addr = addr;
    _txLen = 0;
    _openTransaction = true;
  }
  size_t write(const uint8_t* data, size_t len) {
    size_t accepted = _writeReturnOverrideEnabled ? _writeReturnOverride : len;
    if (accepted > len) {
      accepted = len;
    }
    const size_t available = _bufferSize - _txLen;
    if (accepted > available) {
      accepted = available;
    }
    for (size_t i = 0; i < accepted; i++) {
      _txBuf[_txLen++] = data[i];
    }
    return accepted;
  }
  uint8_t endTransmission(bool stop = true) {
    _lastStop = stop;
    if (!stop) {
      // Arduino-ESP32 defers the whole combined transaction to requestFrom().
      return 0U;
    }
    _openTransaction = false;
    return _endTransmissionResult;
  }

  size_t requestFrom(uint8_t addr, size_t len) {
    (void)addr;
    size_t returned = len > _bufferSize ? _bufferSize : len;
    if (_requestReturnOverrideEnabled && _requestReturnOverride < returned) {
      returned = _requestReturnOverride;
    }
    _rxLen = returned;
    _rxPos = 0;
    _openTransaction = false;
    return _rxLen;
  }

  int available() { return _rxPos < _rxLen ? 1 : 0; }
  int read() { return _rxPos < _rxLen ? _rxBuf[_rxPos++] : -1; }

  void end() {}

  // Test helpers
  void _setEndTransmissionResult(uint8_t result) { _endTransmissionResult = result; }
  void _clearEndTransmissionResult() { _endTransmissionResult = 0; }
  void _setRxBuffer(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len && i < sizeof(_rxBuf); i++) {
      _rxBuf[i] = data[i];
    }
    _rxLen = len;
    _rxPos = 0;
  }
  void _setWriteReturnOverride(size_t len) {
    _writeReturnOverrideEnabled = true;
    _writeReturnOverride = len;
  }
  void _clearWriteReturnOverride() {
    _writeReturnOverrideEnabled = false;
    _writeReturnOverride = 0;
  }
  void _setBeginResult(bool result) { _beginResult = result; }
  void _setRequestReturnOverride(size_t len) {
    _requestReturnOverrideEnabled = true;
    _requestReturnOverride = len;
  }
  void _clearRequestReturnOverride() {
    _requestReturnOverrideEnabled = false;
    _requestReturnOverride = 0;
  }
  bool _isTransactionOpen() const { return _openTransaction; }
  bool _lastEndTransmissionSentStop() const { return _lastStop; }

  uint8_t _addr = 0;
  uint8_t _txBuf[256] = {};
  size_t _txLen = 0;

private:
  uint32_t _timeoutMs = 50;
  bool _beginResult = true;
  uint8_t _endTransmissionResult = 0;
  uint8_t _rxBuf[256] = {};
  size_t _rxLen = 0;
  size_t _rxPos = 0;
  bool _writeReturnOverrideEnabled = false;
  size_t _writeReturnOverride = 0;
  size_t _bufferSize = I2C_BUFFER_LENGTH;
  bool _openTransaction = false;
  bool _lastStop = true;
  bool _requestReturnOverrideEnabled = false;
  size_t _requestReturnOverride = 0;
};

extern TwoWire Wire;
