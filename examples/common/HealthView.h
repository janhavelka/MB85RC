#pragma once

#ifndef MB85RC_EXAMPLE_PLATFORM_IDF
#define MB85RC_EXAMPLE_PLATFORM_IDF 0
#endif

#if MB85RC_EXAMPLE_PLATFORM_IDF
#include "examples/common/IdfArduinoCompat.h"
#else
#include <Arduino.h>
#endif

template <typename DriverT>
inline void printHealthView(const DriverT& driver) {
  Serial.printf("state=%d online=%s failures=%u totalFail=%lu totalOk=%lu\n",
                static_cast<int>(driver.state()), driver.isOnline() ? "true" : "false",
                static_cast<unsigned>(driver.consecutiveFailures()),
                static_cast<unsigned long>(driver.totalFailures()),
                static_cast<unsigned long>(driver.totalSuccess()));
}
