/// @file Wire.h
/// @brief Minimal Wire stub for native testing
#pragma once

#include <cstdint>
#include <cstddef>

class TwoWire {
public:
  void begin(int sda = -1, int scl = -1) { (void)sda; (void)scl; }
  void setClock(uint32_t freq) { (void)freq; }
  void setTimeOut(uint32_t timeoutMs) { _timeoutMs = timeoutMs; }
  uint32_t getTimeOut() const { return _timeoutMs; }
  
  void beginTransmission(uint8_t addr) { _addr = addr; _txLen = 0; }
  size_t write(uint8_t data) { _txBuf[_txLen++] = data; return 1; }
  size_t write(const uint8_t* data, size_t len) { 
    for (size_t i = 0; i < len && _txLen < sizeof(_txBuf); i++) {
      _txBuf[_txLen++] = data[i];
    }
    return len;
  }
  uint8_t endTransmission(bool stop = true) { (void)stop; return _endTransmissionResult; }
  
  size_t requestFrom(uint8_t addr, size_t len) { 
    (void)addr;
    if (_requestFromOverrideEnabled) {
      _rxLen = _requestFromOverride;
    } else {
      _rxLen = len;
    }
    _rxPos = 0;
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
  void _setRequestFromOverride(size_t len) {
    _requestFromOverrideEnabled = true;
    _requestFromOverride = len;
  }
  void _clearRequestFromOverride() {
    _requestFromOverrideEnabled = false;
    _requestFromOverride = 0;
  }
  
  uint8_t _addr = 0;
  uint8_t _txBuf[256] = {};
  size_t _txLen = 0;
  
private:
  uint32_t _timeoutMs = 50;
  uint8_t _endTransmissionResult = 0;
  uint8_t _rxBuf[256] = {};
  size_t _rxLen = 0;
  size_t _rxPos = 0;
  bool _requestFromOverrideEnabled = false;
  size_t _requestFromOverride = 0;
};

extern TwoWire Wire;
