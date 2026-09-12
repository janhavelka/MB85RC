/**
 * @file I2cScanner.h
 * @brief Simple I2C bus scanner utility for examples.
 *
 * NOT part of the library API. This is a diagnostic tool for examples.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "examples/common/Log.h"
#include "I2cTransport.h"

namespace i2c_scanner {

/**
 * @brief Scan I2C bus and print found devices.
 * @param context Adapter context owning the initialized Wire object.
 *
 * The scanner deliberately preserves the application-owned bus clock and
 * timeout configuration.
 */
inline void scan(transport::WireContext& context) {
  if (!context.ready || context.wire == nullptr) {
    LOGE("I2C interface is not ready; run iface_reset first");
    return;
  }
  TwoWire& wire = *context.wire;
  LOGI("Scanning I2C bus (owner-configured timeout)...");

  LOGI("     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F");

  uint8_t count = 0;
  for (uint8_t row = 0; row < 8; row++) {
    LOG_SERIAL.printf("%02X: ", row * 16);

    for (uint8_t col = 0; col < 16; col++) {
      uint8_t addr = row * 16 + col;
      if (addr < 0x08 || addr > 0x77) {
        LOG_SERIAL.print("   ");
        continue;
      }

      wire.beginTransmission(addr);
      uint8_t error = transport::closeTransmission(context, addr);
      if (!context.ready) {
        LOGE("I2C interface failed during scan; run iface_reset first");
        return;
      }

      if (error == 0) {
        LOG_SERIAL.printf("%02X ", addr);
        count++;
      } else if (error == 5) {
        LOG_SERIAL.print("TO ");
      } else {
        LOG_SERIAL.print("-- ");
      }

      yield();
      delay(1);
    }
    LOG_SERIAL.println();
  }

  LOGI("Scan complete. Found %d device(s).", count);

  if (count > 0) {
    LOGI("Common addresses: 0x3C/0x3D=OLED, 0x50-0x57=FRAM, 0x51=RV3032, 0x76/0x77=BME280");
  }
}

}  // namespace i2c_scanner
