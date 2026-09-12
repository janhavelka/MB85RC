/// @file Wire.h
/// @brief Minimal Wire stub for native testing
#pragma once

#include <cstddef>
#include <cstdint>

#define MB85RC_TEST_WIRE_STUB 1
#ifndef I2C_BUFFER_LENGTH
#define I2C_BUFFER_LENGTH 32U
#endif

static constexpr int OUTPUT_OPEN_DRAIN = 1;
static constexpr int HIGH = 1;
static constexpr int LOW = 0;
struct ArduinoStubPin {
  int mode = 0;
  int level = HIGH;
  bool heldLow = false;
  uint32_t lowReadsRemaining = 0;
};
inline ArduinoStubPin arduinoStubPins[64];
inline uint32_t arduinoStubMicros = 0;
inline bool arduinoStubWireActive = false;
inline uint32_t arduinoStubGpioWhileWireActive = 0;
inline void resetArduinoStubPins() {
  for (auto& pin : arduinoStubPins) pin = ArduinoStubPin{};
  arduinoStubGpioWhileWireActive = 0;
}
inline void pinMode(int pin, int mode) {
  if (arduinoStubWireActive) ++arduinoStubGpioWhileWireActive;
  arduinoStubPins[pin].mode = mode;
}
inline void digitalWrite(int pin, int level) { arduinoStubPins[pin].level = level; }
inline int digitalRead(int pin) {
  auto& state = arduinoStubPins[pin];
  if (state.heldLow) return LOW;
  if (state.lowReadsRemaining > 0) {
    --state.lowReadsRemaining;
    return LOW;
  }
  return state.level;
}
inline uint32_t micros() { return arduinoStubMicros; }
inline void delayMicroseconds(unsigned int us) { arduinoStubMicros += us; }
inline void delay(uint32_t ms) { arduinoStubMicros += ms * 1000U; }

class TwoWire {
public:
  bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0U) {
    ++_beginCalls;
    (void)sda;
    (void)scl;
    (void)frequency;
    _buffersFreed = !_beginResult;
    arduinoStubWireActive = _beginResult;
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
    if (_buffersFreed) {
      return 0U;
    }
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
    if (_buffersFreed) {
      return 4U;
    }
    if (!stop) {
      // Arduino-ESP32 defers the whole combined transaction to requestFrom().
      return _nonStopEndTransmissionResult;
    }
    _openTransaction = false;
    if (!_physicalAttempt()) return 5U;
    return _endTransmissionResult;
  }

  size_t requestFrom(uint8_t addr, size_t len) {
    (void)addr;
    _openTransaction = false;
    if (_buffersFreed) {
      return 0U;
    }
    if (len > 0U && !_physicalAttempt()) return 0U;
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

  void end() { ++_endCalls; arduinoStubWireActive = false; }

  // Test helpers
  void _setEndTransmissionResult(uint8_t result) { _endTransmissionResult = result; }
  void _clearEndTransmissionResult() { _endTransmissionResult = 0; }
  void _setNonStopEndTransmissionResult(uint8_t result) {
    _nonStopEndTransmissionResult = result;
  }
  void _setBuffersFreed(bool freed = true) { _buffersFreed = freed; }
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

  uint32_t _lastPhysicalTimeoutMs = 0U;
  uint32_t _physicalCalls = 0U;
  uint32_t _requiredPhysicalWaitMs = 0U;
  uint8_t _addr = 0;
  uint32_t _endCalls = 0U;
  uint32_t _beginCalls = 0U;
  uint8_t _txBuf[256] = {};
  size_t _txLen = 0;

private:
  bool _physicalAttempt() {
    _lastPhysicalTimeoutMs = _timeoutMs;
    ++_physicalCalls;
    const uint32_t elapsedMs =
        _requiredPhysicalWaitMs > _timeoutMs ? _timeoutMs : _requiredPhysicalWaitMs;
    arduinoStubMicros += elapsedMs * 1000U;
    return _requiredPhysicalWaitMs <= _timeoutMs;
  }

  uint32_t _timeoutMs = 50;
  bool _beginResult = true;
  uint8_t _endTransmissionResult = 0;
  uint8_t _nonStopEndTransmissionResult = 0;
  bool _buffersFreed = false;
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
