/// @file main.cpp
/// @brief Diagnostic bring-up CLI for supported MB85RC-family FRAM variants
/// @note This is an EXAMPLE, not part of the library
/// @note This CLI owns the example bus and is not a production shared-bus manager,
///       scheduler template, or storage stack.

#include <Arduino.h>
#include <cstdlib>
#include <cstring>

#include "examples/common/CliStyle.h"
#include "examples/common/Log.h"
#include "examples/common/BoardConfig.h"
#include "examples/common/BusDiag.h"
#include "examples/common/CliShell.h"
#include "examples/common/I2cTransport.h"
#include "examples/common/I2cScanner.h"
#include "examples/common/TypedMemory.h"

#include "MB85RC/MB85RC.h"

// ============================================================================
// Globals
// ============================================================================

struct StressStats {
  bool active = false;
  uint32_t startMs = 0;
  uint32_t endMs = 0;
  int target = 0;
  int attempts = 0;
  int success = 0;
  uint32_t errors = 0;
  MB85RC::Status lastError = MB85RC::Status::Ok();
};

MB85RC::MB85RC device;
bool verboseMode = false;
StressStats stressStats;

static constexpr int DEFAULT_STRESS_COUNT = 10;
static constexpr int MAX_STRESS_COUNT = 100000;
static constexpr uint32_t STRESS_PROGRESS_UPDATES = 10U;
static constexpr int DEFAULT_RANDBENCH_OPS = 4096;

static constexpr uint32_t RW_SUITE_ADDR = 0x0400;
static constexpr uint32_t RW_SUITE_FILL_ADDR = 0x0460;
static constexpr uint32_t TYPED_DEMO_ADDR = 0x0600;
static constexpr uint32_t RANDOM_BENCH_ADDR = 0x2000;
static constexpr uint32_t XFER_DEMO_ADDR = 0x0700;

static constexpr size_t RW_SUITE_LEN = 64;
static constexpr size_t RW_SUITE_FILL_LEN = 16;
static constexpr size_t TYPED_DEMO_LEN = 28;
static constexpr size_t RANDOM_BENCH_LEN = 1024;
static constexpr size_t XFER_DEMO_LEN = 640;

// ============================================================================
// Helper Functions
// ============================================================================

uint32_t exampleNowMs(void*) {
  return millis();
}

uint32_t nextRandom(uint32_t& state) {
  if (state == 0U) {
    state = 0xA341316CU;
  }
  state ^= (state << 13);
  state ^= (state >> 17);
  state ^= (state << 5);
  return state;
}

const char* errToStr(MB85RC::Err err) {
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
    default:                        return "UNKNOWN";
  }
}

const char* stateToStr(MB85RC::DriverState st) {
  using namespace MB85RC;
  switch (st) {
    case DriverState::UNINIT:   return "UNINIT";
    case DriverState::READY:    return "READY";
    case DriverState::DEGRADED: return "DEGRADED";
    case DriverState::OFFLINE:  return "OFFLINE";
    default:                    return "UNKNOWN";
  }
}

const char* sleepStateToStr(MB85RC::SleepState st) {
  using namespace MB85RC;
  switch (st) {
    case SleepState::AWAKE:  return "AWAKE";
    case SleepState::ASLEEP: return "ASLEEP";
    case SleepState::WAKING: return "WAKING";
    default:                 return "UNKNOWN";
  }
}

const char* stateColor(MB85RC::DriverState st, bool online, uint8_t consecutiveFailures) {
  if (st == MB85RC::DriverState::UNINIT) {
    return LOG_COLOR_RESET;
  }
  return LOG_COLOR_STATE(online, consecutiveFailures);
}

const char* addressModelToStr(MB85RC::cmd::AddressModel model) {
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

const char* deviceVariantToStr(MB85RC::DeviceVariant variant) {
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

void printVariantInfo(const MB85RC::cmd::VariantInfo& variant) {
  Serial.printf("  %-10s %6lu bytes",
                variant.name,
                static_cast<unsigned long>(variant.memoryBytes));
  if (variant.hasDeviceId) {
    Serial.printf("  product=0x%03X  density=0x%X\n",
                  variant.productId,
                  variant.densityCode);
  } else {
    Serial.println("  product=n/a  density=n/a");
  }
  Serial.printf("    %s; runtime driver support=%s; high-speed capability=%s; sleep capability=%s\n",
                addressModelToStr(variant.addressModel),
                variant.supportedByDriver ? "yes" : "no",
                variant.supportsHighSpeedMode ? "yes" : "no",
                variant.supportsSleepMode ? "yes" : "no");
  Serial.printf("    max normal bus=%lu Hz; max high-speed bus=%lu Hz; sleep tREC=%u us\n",
                static_cast<unsigned long>(variant.maxNormalBusHz),
                static_cast<unsigned long>(variant.maxHighSpeedBusHz),
                static_cast<unsigned>(variant.sleepRecoveryUs));
}

void printVariantCatalog() {
  Serial.println("Known MB85RC family variants:");
  for (size_t i = 0; i < MB85RC::cmd::VARIANT_COUNT; ++i) {
    printVariantInfo(MB85RC::cmd::KNOWN_VARIANTS[i]);
  }
}

const char* goodIfZeroColor(uint32_t value) {
  return cli::zeroGoodColor(value);
}

const char* goodIfNonZeroColor(uint32_t value) {
  return cli::nonZeroGoodColor(value);
}

const char* skipCountColor(uint32_t value) {
  return (value == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW;
}

const char* successRateColor(float pct) {
  return cli::successRateColor(pct);
}

uint32_t stressProgressStep(uint32_t total) {
  if (total == 0U) {
    return 0U;
  }
  const uint32_t step = total / STRESS_PROGRESS_UPDATES;
  return (step == 0U) ? 1U : step;
}

void printStressProgress(uint32_t completed, uint32_t total, uint32_t okCount, uint32_t failCount) {
  if (completed == 0U || total == 0U) {
    return;
  }
  const uint32_t step = stressProgressStep(total);
  if (step == 0U || (completed != total && (completed % step) != 0U)) {
    return;
  }
  const float pct = (100.0f * static_cast<float>(completed)) / static_cast<float>(total);
  Serial.printf("  Progress: %lu/%lu (%.0f%%, ok=%s%lu%s, fail=%s%lu%s)\n",
                static_cast<unsigned long>(completed),
                static_cast<unsigned long>(total),
                pct,
                goodIfNonZeroColor(okCount),
                static_cast<unsigned long>(okCount),
                LOG_COLOR_RESET,
                goodIfZeroColor(failCount),
                static_cast<unsigned long>(failCount),
                LOG_COLOR_RESET);
}

void printStatus(const MB85RC::Status& st) {
  Serial.printf("  Status: %s%s%s (code=%u, detail=%ld)\n",
                LOG_COLOR_RESULT(st.ok()),
                errToStr(st.code),
                LOG_COLOR_RESET,
                static_cast<unsigned>(st.code),
                static_cast<long>(st.detail));
  if (st.msg && st.msg[0]) {
    Serial.printf("  Message: %s%s%s\n", LOG_COLOR_YELLOW, st.msg, LOG_COLOR_RESET);
  }
}

void printCheckStatus(const char* name, const MB85RC::Status& st) {
  Serial.printf("  [%s%s%s] %s\n",
                LOG_COLOR_RESULT(st.ok()),
                st.ok() ? "PASS" : "FAIL",
                LOG_COLOR_RESET,
                name);
  if (!st.ok()) {
    printStatus(st);
  }
}

void printDriverHealth() {
  const uint32_t now = millis();
  const uint32_t totalOk = device.totalSuccess();
  const uint32_t totalFail = device.totalFailures();
  const uint32_t total = totalOk + totalFail;
  const float successRate = (total > 0U)
                                ? (100.0f * static_cast<float>(totalOk) / static_cast<float>(total))
                                : 0.0f;
  const MB85RC::Status lastErr = device.lastError();
  const MB85RC::DriverState st = device.state();
  const bool online = device.isOnline();

  Serial.println("=== Driver Health ===");
  Serial.printf("  State: %s%s%s\n",
                stateColor(st, online, device.consecutiveFailures()),
                stateToStr(st),
                LOG_COLOR_RESET);
  Serial.printf("  Online: %s%s%s\n",
                online ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                log_bool_str(online),
                LOG_COLOR_RESET);
  Serial.printf("  Consecutive failures: %s%u%s\n",
                goodIfZeroColor(device.consecutiveFailures()),
                device.consecutiveFailures(),
                LOG_COLOR_RESET);
  Serial.printf("  Total success: %s%lu%s\n",
                goodIfNonZeroColor(totalOk),
                static_cast<unsigned long>(totalOk),
                LOG_COLOR_RESET);
  Serial.printf("  Total failures: %s%lu%s\n",
                goodIfZeroColor(totalFail),
                static_cast<unsigned long>(totalFail),
                LOG_COLOR_RESET);
  Serial.printf("  Success rate: %s%.1f%%%s\n",
                successRateColor(successRate),
                successRate,
                LOG_COLOR_RESET);

  const uint32_t lastOkMs = device.lastOkMs();
  if (lastOkMs > 0U) {
    Serial.printf("  Last OK: %lu ms ago (at %lu ms)\n",
                  static_cast<unsigned long>(now - lastOkMs),
                  static_cast<unsigned long>(lastOkMs));
  } else {
    Serial.println("  Last OK: never");
  }

  const uint32_t lastErrorMs = device.lastErrorMs();
  if (lastErrorMs > 0U) {
    Serial.printf("  Last error: %lu ms ago (at %lu ms)\n",
                  static_cast<unsigned long>(now - lastErrorMs),
                  static_cast<unsigned long>(lastErrorMs));
  } else {
    Serial.println("  Last error: never");
  }

  if (!lastErr.ok()) {
    Serial.printf("  Error code: %s%s%s\n",
                  LOG_COLOR_RED,
                  errToStr(lastErr.code),
                  LOG_COLOR_RESET);
    Serial.printf("  Error detail: %ld\n", static_cast<long>(lastErr.detail));
    if (lastErr.msg && lastErr.msg[0]) {
      Serial.printf("  Error msg: %s\n", lastErr.msg);
    }
  }
}

uint32_t memoryAddressAtOffset(uint32_t address, size_t offset);

void printHexDump(uint32_t startAddr, const uint8_t* data, size_t len) {
  for (size_t offset = 0; offset < len;) {
    const uint32_t lineAddr = memoryAddressAtOffset(startAddr, offset);
    size_t lineLen = len - offset;
    if (lineLen > 16) {
      lineLen = 16;
    }

    Serial.printf("  %04lX: ", static_cast<unsigned long>(lineAddr));
    for (size_t i = 0; i < lineLen; ++i) {
      Serial.printf("%02X ", data[offset + i]);
    }
    for (size_t i = lineLen; i < 16; ++i) {
      Serial.print("   ");
    }
    Serial.print(" |");
    for (size_t i = 0; i < lineLen; ++i) {
      const char c = static_cast<char>(data[offset + i]);
      Serial.print((c >= 0x20 && c < 0x7F) ? c : '.');
    }
    Serial.println("|");
    offset += lineLen;
  }
}

bool parseUnsignedToken(const String& token, unsigned long maxValue, unsigned long& outValue) {
  if (token.length() == 0) {
    return false;
  }

  const bool isHex = token.startsWith("0x") || token.startsWith("0X");
  char* end = nullptr;
  const unsigned long value = strtoul(token.c_str(), &end, isHex ? 16 : 10);
  if (end == token.c_str() || *end != '\0' || value > maxValue) {
    return false;
  }

  outValue = value;
  return true;
}

bool parseAddress(const String& token, uint32_t& outAddr) {
  unsigned long value = 0;
  if (!parseUnsignedToken(token, device.maxAddress(), value)) {
    return false;
  }
  outAddr = static_cast<uint32_t>(value);
  return true;
}

bool parseByte(const String& token, uint8_t& outVal) {
  unsigned long value = 0;
  if (!parseUnsignedToken(token, 0xFFUL, value)) {
    return false;
  }
  outVal = static_cast<uint8_t>(value);
  return true;
}

bool parseUint16(const String& token, uint16_t& outVal) {
  unsigned long value = 0;
  if (!parseUnsignedToken(token, 0xFFFFUL, value)) {
    return false;
  }
  outVal = static_cast<uint16_t>(value);
  return true;
}

bool parseUint32(const String& token, uint32_t& outVal) {
  unsigned long value = 0;
  if (!parseUnsignedToken(token, 0xFFFFFFFFUL, value)) {
    return false;
  }
  outVal = static_cast<uint32_t>(value);
  return true;
}

bool parseCountArg(const String& token, int& outCount) {
  unsigned long value = 0;
  if (!parseUnsignedToken(token, static_cast<unsigned long>(MAX_STRESS_COUNT), value) ||
      value == 0) {
    return false;
  }
  outCount = static_cast<int>(value);
  return true;
}

uint32_t memoryAddressAtOffset(uint32_t address, size_t offset) {
  return address + static_cast<uint32_t>(offset);
}

bool rangeFitsActiveCapacity(uint32_t address, size_t len) {
  if (len == 0U || address > device.maxAddress()) {
    return false;
  }
  const uint32_t capacity = device.capacityBytes();
  if (address >= capacity) {
    return false;
  }
  const size_t remaining = static_cast<size_t>(capacity - address);
  return len <= remaining;
}

void printRangeError(uint32_t address, size_t len) {
  Serial.printf("  Range 0x%04lX + %lu exceeds active capacity (%lu bytes, max 0x%04lX)\n",
                static_cast<unsigned long>(address),
                static_cast<unsigned long>(len),
                static_cast<unsigned long>(device.capacityBytes()),
                static_cast<unsigned long>(device.maxAddress()));
}

uint32_t randomBenchAddress() {
  const uint32_t capacity = device.capacityBytes();
  if (capacity >= static_cast<uint32_t>(RANDOM_BENCH_ADDR) + RANDOM_BENCH_LEN) {
    return RANDOM_BENCH_ADDR;
  }
  if (capacity > RANDOM_BENCH_LEN) {
    return capacity - RANDOM_BENCH_LEN;
  }
  return 0U;
}

void printBenchmarkLine(const char* label, uint32_t ops, uint32_t elapsedUs, size_t bytesPerOp) {
  const uint32_t totalBytes = ops * static_cast<uint32_t>(bytesPerOp);
  const float avgUs = (ops > 0U) ? static_cast<float>(elapsedUs) / static_cast<float>(ops) : 0.0f;
  const float opsPerSec =
      (elapsedUs > 0U) ? (1000000.0f * static_cast<float>(ops) / static_cast<float>(elapsedUs))
                       : 0.0f;
  const float bytesPerSec =
      (elapsedUs > 0U)
          ? (1000000.0f * static_cast<float>(totalBytes) / static_cast<float>(elapsedUs))
          : 0.0f;

  Serial.printf("  %-18s ops=%lu elapsed=%lu us avg=%.2f us/op rate=%.2f ops/s throughput=%.2f B/s\n",
                label,
                static_cast<unsigned long>(ops),
                static_cast<unsigned long>(elapsedUs),
                avgUs,
                opsPerSec,
                bytesPerSec);
}

bool isPrintableAscii(uint8_t value) {
  return value >= 0x20U && value <= 0x7EU;
}

void printEscapedByte(uint8_t value) {
  switch (value) {
    case '\\':
      Serial.print("\\\\");
      return;
    case '"':
      Serial.print("\\\"");
      return;
    case '\r':
      Serial.print("\\r");
      return;
    case '\n':
      Serial.print("\\n");
      return;
    case '\t':
      Serial.print("\\t");
      return;
    case '\0':
      Serial.print("\\0");
      return;
    default:
      break;
  }

  if (isPrintableAscii(value)) {
    Serial.write(static_cast<char>(value));
  } else {
    Serial.printf("\\x%02X", value);
  }
}

bool parseAddressLengthArgs(const String& args,
                            uint32_t& outAddr,
                            uint32_t& outLen,
                            uint32_t defaultLen) {
  String trimmed = args;
  trimmed.trim();
  if (trimmed.length() == 0) {
    return false;
  }

  const int split = trimmed.indexOf(' ');
  String addrStr;
  if (split < 0) {
    if (defaultLen == 0U) {
      return false;
    }
    addrStr = trimmed;
    outLen = defaultLen;
  } else {
    addrStr = trimmed.substring(0, split);
    String lenStr = trimmed.substring(split + 1);
    lenStr.trim();
    if (!parseUint32(lenStr, outLen) || outLen == 0U) {
      return false;
    }
  }

  if (!parseAddress(addrStr, outAddr)) {
    return false;
  }
  return true;
}

template <typename Visitor>
bool visitMemoryRange(uint32_t address, uint32_t len, Visitor visitor) {
  if (!rangeFitsActiveCapacity(address, len)) {
    printRangeError(address, len);
    return false;
  }

  static uint8_t readBuf[256];
  uint32_t remaining = len;
  uint32_t offset = 0;

  while (remaining > 0) {
    const size_t chunk = (remaining > sizeof(readBuf)) ? sizeof(readBuf)
                                                       : static_cast<size_t>(remaining);
    const uint32_t chunkAddr = memoryAddressAtOffset(address, offset);
    MB85RC::Status st = device.read(chunkAddr, readBuf, chunk);
    if (!st.ok()) {
      printStatus(st);
      return false;
    }

    visitor(chunkAddr, readBuf, chunk);
    offset += static_cast<uint32_t>(chunk);
    remaining -= static_cast<uint32_t>(chunk);
  }

  return true;
}

void printSettings() {
  MB85RC::SettingsSnapshot snap;
  MB85RC::Status st = device.getSettings(snap);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  Serial.println("=== Settings ===");
  Serial.printf("  Initialized: %s%s%s\n",
                snap.initialized ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                snap.initialized ? "true" : "false",
                LOG_COLOR_RESET);
  Serial.printf("  State: %s%s%s\n",
                stateColor(snap.state, device.isOnline(), device.consecutiveFailures()),
                stateToStr(snap.state),
                LOG_COLOR_RESET);
  Serial.printf("  I2C address: 0x%02X\n", snap.i2cAddress);
  Serial.printf("  I2C timeout: %lu ms\n", static_cast<unsigned long>(snap.i2cTimeoutMs));
  Serial.printf("  Offline threshold: %u\n", snap.offlineThreshold);
  Serial.printf("  nowMs hook: %s%s%s\n",
                snap.hasNowMsHook ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW,
                snap.hasNowMsHook ? "present" : "millis() fallback",
                LOG_COLOR_RESET);
  Serial.printf("  Expected variant: %s\n", deviceVariantToStr(snap.expectedVariant));
  Serial.printf("  Active variant: %s%s%s\n",
                snap.variantKnown ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW,
                snap.variantName,
                LOG_COLOR_RESET);
  Serial.printf("  Device ID: manufacturer=0x%03X product=0x%03X density=0x%02X\n",
                snap.manufacturerId,
                snap.productId,
                snap.densityCode);
  Serial.printf("  Capacity: %lu bytes, max address 0x%04lX\n",
                static_cast<unsigned long>(snap.capacityBytes),
                static_cast<unsigned long>(snap.maxAddress));
  Serial.printf("  Bus limits: normal=%lu Hz high-speed=%lu Hz\n",
                static_cast<unsigned long>(snap.maxNormalBusHz),
                static_cast<unsigned long>(snap.maxHighSpeedBusHz));
  Serial.printf("  High-speed mode: support=%s enabled=%s\n",
                snap.highSpeedModeSupported ? "yes" : "no",
                snap.highSpeedModeEnabled ? "yes" : "no");
  Serial.printf("  Sleep mode: support=%s state=%s tREC=%u us wakeReadyMs=%lu\n",
                snap.sleepModeSupported ? "yes" : "no",
                sleepStateToStr(snap.sleepState),
                static_cast<unsigned>(snap.sleepRecoveryUs),
                static_cast<unsigned long>(snap.sleepWakeReadyMs));
  Serial.println("  Cross-end bulk operations: rejected");
  if (snap.currentAddressKnown) {
    Serial.printf("  Current address: 0x%04lX\n", static_cast<unsigned long>(snap.currentAddress));
  } else {
    Serial.println("  Current address: unknown (needs successful memory read/write first)");
  }
}

void printHighSpeedSupport() {
  MB85RC::SettingsSnapshot snap = device.getSettings();
  Serial.println("High-speed mode:");
  Serial.printf("  Active variant: %s\n", snap.variantName);
  Serial.printf("  Support: %s\n", snap.highSpeedModeSupported ? "yes" : "no");
  Serial.printf("  Enabled: %s\n", snap.highSpeedModeEnabled ? "yes" : "no");
  Serial.println("  Core bus clock: unchanged; MB85RC core does not change Wire/ESP-IDF I2C clock");
  Serial.println("  Diagnostic bus clock: BoardConfig/Wire setting; this example does not prove 3.4 MHz operation");
  Serial.println("  App action: application bus manager must configure/operate the bus at 3.4 MHz after HS entry");
  Serial.println("  Note: STOP exits high-speed mode; enabled driver transfers send the HS prefix per transaction");
  Serial.println("  Arduino Wire diagnostic transport: raw HS master-code callback not installed");
  Serial.println("  Hardware validation: not claimed by this diagnostic");
}

void handleHighSpeedCommand(const String& cmd) {
  printHighSpeedSupport();
  if (cmd == "hs" || cmd == "hs support") {
    return;
  }
  if (cmd == "hs enter") {
    MB85RC::Status st = device.enterHighSpeedMode();
    printStatus(st);
    if (st.ok()) {
      LOGI("High-speed transfer mode requested; entry prefix is sent with each memory transfer.");
    } else if (st.code == MB85RC::Err::INVALID_CONFIG) {
      LOGW("No raw special I2C callback is configured for this diagnostic Arduino transport.");
    }
    return;
  }
  LOGW("Usage: hs | hs support | hs enter");
}

void printSleepSupport() {
  MB85RC::SettingsSnapshot snap = device.getSettings();
  Serial.println("Sleep mode:");
  Serial.printf("  Active variant: %s\n", snap.variantName);
  Serial.printf("  Support: %s\n", snap.sleepModeSupported ? "yes" : "no");
  Serial.printf("  State: %s\n", sleepStateToStr(snap.sleepState));
  Serial.println("  Entry: F8h + active device address word + repeated-start 86h");
  Serial.println("  Wake: clock active device address word, wait tREC >= 400 us before access/recover");
  Serial.println("  Core sleep state: tracked separately from driver health; no hidden delay is inserted");
  Serial.println("  Arduino Wire diagnostic transport: raw Sleep callback not installed");
  Serial.println("  Hardware validation: not claimed by this diagnostic");
}

void handleSleepCommand(const String& cmd) {
  printSleepSupport();
  if (cmd == "sleep" || cmd == "sleep support") {
    return;
  }
  if (cmd == "sleep enter") {
    MB85RC::Status st = device.enterSleep();
    printStatus(st);
    if (st.code == MB85RC::Err::INVALID_CONFIG) {
      LOGW("No raw special I2C callback is configured for this diagnostic Arduino transport.");
    }
    return;
  }
  if (cmd == "sleep wake") {
    MB85RC::Status st = device.wake();
    printStatus(st);
    if (st.ok()) {
      delay(1);
      device.tick(millis());
      printCheckStatus("recover after sleep wake", device.recover());
    } else if (st.code == MB85RC::Err::INVALID_CONFIG) {
      LOGW("No raw special I2C callback is configured for this diagnostic Arduino transport.");
    }
    return;
  }
  LOGW("Usage: sleep | sleep support | sleep enter | sleep wake");
}

void readRangeForDump(uint32_t address, uint32_t len) {
  visitMemoryRange(address, len, [](uint32_t chunkAddr, const uint8_t* data, size_t chunkLen) {
    printHexDump(chunkAddr, data, chunkLen);
  });
}

void printTextRange(uint32_t address, uint32_t len) {
  visitMemoryRange(address, len, [](uint32_t chunkAddr, const uint8_t* data, size_t chunkLen) {
    for (size_t offset = 0; offset < chunkLen;) {
      const uint32_t lineAddr = memoryAddressAtOffset(chunkAddr, offset);
      size_t lineLen = chunkLen - offset;
      if (lineLen > 32U) {
        lineLen = 32U;
      }

      Serial.printf("  %04lX: \"", static_cast<unsigned long>(lineAddr));
      for (size_t i = 0; i < lineLen; ++i) {
        printEscapedByte(data[offset + i]);
      }
      Serial.println("\"");
      offset += lineLen;
    }
  });
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
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

void printRangeCrc32(uint32_t address, uint32_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  if (!visitMemoryRange(address, len,
                        [&crc](uint32_t, const uint8_t* data, size_t chunkLen) {
                          crc = crc32Update(crc, data, chunkLen);
                        })) {
    return;
  }

  crc ^= 0xFFFFFFFFUL;
  Serial.printf("CRC32[0x%04lX + %lu] = 0x%08lX\n",
                static_cast<unsigned long>(address),
                static_cast<unsigned long>(len),
                static_cast<unsigned long>(crc));
}

struct StringScanState {
  uint16_t minLen = 4;
  uint32_t currentLen = 0;
  uint32_t startAddr = 0;
  uint32_t matches = 0;
  uint8_t previewLen = 0;
  bool active = false;
  bool truncated = false;
  char preview[49] = {0};
};

void flushStringScan(StringScanState& state) {
  if (!state.active) {
    return;
  }

  if (state.currentLen >= state.minLen) {
    Serial.printf("  %04lX len=%lu \"",
                  static_cast<unsigned long>(state.startAddr),
                  static_cast<unsigned long>(state.currentLen));
    for (uint8_t i = 0; i < state.previewLen; ++i) {
      printEscapedByte(static_cast<uint8_t>(state.preview[i]));
    }
    if (state.truncated) {
      Serial.print("...");
    }
    Serial.println("\"");
    state.matches++;
  }

  state.active = false;
  state.currentLen = 0;
  state.previewLen = 0;
  state.preview[0] = '\0';
  state.truncated = false;
}

void scanPrintableStrings(uint32_t address, uint32_t len, uint16_t minLen) {
  StringScanState state;
  state.minLen = minLen;

  if (!visitMemoryRange(address, len,
                        [&state](uint32_t chunkAddr, const uint8_t* data, size_t chunkLen) {
                          for (size_t i = 0; i < chunkLen; ++i) {
                            const uint8_t value = data[i];
                            const uint32_t byteAddr = memoryAddressAtOffset(chunkAddr, i);
                            if (isPrintableAscii(value)) {
                              if (!state.active) {
                                state.active = true;
                                state.startAddr = byteAddr;
                                state.currentLen = 0;
                                state.previewLen = 0;
                                state.preview[0] = '\0';
                                state.truncated = false;
                              }

                              state.currentLen++;
                              if (state.previewLen + 1U < sizeof(state.preview)) {
                                state.preview[state.previewLen++] = static_cast<char>(value);
                                state.preview[state.previewLen] = '\0';
                              } else {
                                state.truncated = true;
                              }
                            } else {
                              flushStringScan(state);
                            }
                          }
                        })) {
    return;
  }

  flushStringScan(state);
  if (state.matches == 0U) {
    Serial.println("  No printable strings found.");
    return;
  }

  Serial.printf("Found %lu printable string(s).\n", static_cast<unsigned long>(state.matches));
}

void printCurrentAddressRead() {
  MB85RC::SettingsSnapshot snap;
  MB85RC::Status st = device.getSettings(snap);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  const bool hadCurrentAddress = snap.currentAddressKnown;
  const uint32_t currentAddr = snap.currentAddress;
  uint8_t value = 0;
  st = device.readCurrentAddress(value);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  if (hadCurrentAddress) {
    Serial.printf("Current[0x%04lX] = 0x%02X\n",
                  static_cast<unsigned long>(currentAddr),
                  value);
  } else {
    Serial.printf("Current = 0x%02X\n", value);
  }
}

void printCurrentAddressReadRange(uint32_t len) {
  MB85RC::SettingsSnapshot snap;
  MB85RC::Status st = device.getSettings(snap);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  if (!snap.currentAddressKnown) {
    LOGW("Current address is unknown. Perform a successful addressed read/write first.");
    return;
  }

  static uint8_t readBuf[256];
  uint32_t remaining = len;
  uint32_t startAddr = snap.currentAddress;
  if (!rangeFitsActiveCapacity(startAddr, len)) {
    printRangeError(startAddr, len);
    return;
  }

  while (remaining > 0) {
    const size_t chunk = (remaining > sizeof(readBuf)) ? sizeof(readBuf)
                                                       : static_cast<size_t>(remaining);
    st = device.readCurrentAddress(readBuf, chunk);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    printHexDump(startAddr, readBuf, chunk);
    startAddr = memoryAddressAtOffset(startAddr, chunk);
    remaining -= static_cast<uint32_t>(chunk);
  }
}

void resetStressStats(int target) {
  stressStats.active = true;
  stressStats.startMs = millis();
  stressStats.endMs = 0;
  stressStats.target = target;
  stressStats.attempts = 0;
  stressStats.success = 0;
  stressStats.errors = 0;
  stressStats.lastError = MB85RC::Status::Ok();
}

void finishStressStats() {
  stressStats.active = false;
  stressStats.endMs = millis();
  const uint32_t durationMs = stressStats.endMs - stressStats.startMs;
  const float successPct =
      (stressStats.attempts > 0)
          ? (100.0f * static_cast<float>(stressStats.success) /
             static_cast<float>(stressStats.attempts))
          : 0.0f;

  Serial.println("=== Stress Summary ===");
  Serial.printf("  Target: %d\n", stressStats.target);
  Serial.printf("  Attempts: %d\n", stressStats.attempts);
  Serial.printf("  Success: %s%d%s\n",
                goodIfNonZeroColor(static_cast<uint32_t>(stressStats.success)),
                stressStats.success,
                LOG_COLOR_RESET);
  Serial.printf("  Errors: %s%lu%s\n",
                goodIfZeroColor(stressStats.errors),
                static_cast<unsigned long>(stressStats.errors),
                LOG_COLOR_RESET);
  Serial.printf("  Success rate: %s%.2f%%%s\n",
                successRateColor(successPct),
                successPct,
                LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(durationMs));
  if (durationMs > 0) {
    const float rate = 1000.0f * static_cast<float>(stressStats.attempts) /
                       static_cast<float>(durationMs);
    Serial.printf("  Rate: %.2f cycles/s\n", rate);
  }

  if (!stressStats.lastError.ok()) {
    Serial.printf("  Last error: %s\n", errToStr(stressStats.lastError.code));
    if (stressStats.lastError.msg && stressStats.lastError.msg[0]) {
      Serial.printf("  Message: %s\n", stressStats.lastError.msg);
    }
  }
}

void runStress(int count) {
  resetStressStats(count);
  const uint32_t succBefore = device.totalSuccess();
  const uint32_t failBefore = device.totalFailures();
  const uint32_t capacity = device.capacityBytes();
  if (capacity == 0UL) {
    LOGW("Active capacity is zero");
    finishStressStats();
    return;
  }

  for (int i = 0; i < count; ++i) {
    stressStats.attempts++;
    const uint32_t addr = static_cast<uint32_t>(i) % capacity;
    const uint8_t pattern = static_cast<uint8_t>(i & 0xFF);

    MB85RC::Status st = device.writeByte(addr, pattern);
    if (!st.ok()) {
      stressStats.errors++;
      stressStats.lastError = st;
      if (verboseMode) {
        Serial.printf("  [%d] write failed: %s\n", i, errToStr(st.code));
      }
    } else {
      uint8_t readBack = 0;
      st = device.readByte(addr, readBack);
      if (!st.ok()) {
        stressStats.errors++;
        stressStats.lastError = st;
        if (verboseMode) {
          Serial.printf("  [%d] read failed: %s\n", i, errToStr(st.code));
        }
      } else if (readBack != pattern) {
        stressStats.errors++;
        stressStats.lastError = MB85RC::Status::Error(MB85RC::Err::I2C_ERROR, "data mismatch");
        if (verboseMode) {
          Serial.printf("  [%d] mismatch: wrote 0x%02X read 0x%02X\n", i, pattern, readBack);
        }
      } else {
        stressStats.success++;
      }
    }

    printStressProgress(static_cast<uint32_t>(stressStats.attempts),
                        static_cast<uint32_t>(stressStats.target),
                        static_cast<uint32_t>(stressStats.success),
                        stressStats.errors);
  }

  const uint32_t successDelta = device.totalSuccess() - succBefore;
  const uint32_t failDelta = device.totalFailures() - failBefore;

  finishStressStats();
  Serial.printf("  Tracked I2C health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                goodIfNonZeroColor(successDelta),
                static_cast<unsigned long>(successDelta),
                LOG_COLOR_RESET,
                goodIfZeroColor(failDelta),
                static_cast<unsigned long>(failDelta),
                LOG_COLOR_RESET);
}

void runStressMix(int count) {
  struct OpStats {
    const char* name;
    uint32_t ok;
    uint32_t fail;
  };
  OpStats stats[] = {
      {"writeByte",    0, 0},
      {"readByte",     0, 0},
      {"writeMulti",   0, 0},
      {"readMulti",    0, 0},
      {"fill",         0, 0},
      {"readDeviceId", 0, 0},
      {"currentAddr",  0, 0},
      {"recover",      0, 0},
      {"settings",     0, 0},
  };
  const int opCount = static_cast<int>(sizeof(stats) / sizeof(stats[0]));

  const uint32_t succBefore = device.totalSuccess();
  const uint32_t failBefore = device.totalFailures();
  const uint32_t startMs = millis();
  const uint32_t capacity = device.capacityBytes();
  if (capacity == 0UL) {
    LOGW("Active capacity is zero");
    return;
  }
  uint32_t okTotal = 0;
  uint32_t failTotal = 0;

  for (int i = 0; i < count; ++i) {
    const int op = i % opCount;
    MB85RC::Status st = MB85RC::Status::Ok();

    switch (op) {
      case 0: {
        const uint32_t addr = static_cast<uint32_t>(i) % capacity;
        st = device.writeByte(addr, static_cast<uint8_t>(i & 0xFF));
        break;
      }
      case 1: {
        uint8_t val = 0;
        st = device.readByte(static_cast<uint32_t>(i) % capacity, val);
        break;
      }
      case 2: {
        uint8_t buf[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        st = device.write(0x0100, buf, 4);
        break;
      }
      case 3: {
        uint8_t buf[4] = {};
        st = device.read(0x0100, buf, 4);
        break;
      }
      case 4: {
        st = device.fill(0x0200, static_cast<uint8_t>(i & 0xFF), 8);
        break;
      }
      case 5: {
        MB85RC::DeviceId id;
        const MB85RC::cmd::VariantInfo* info = device.variantInfo();
        if (info != nullptr && !info->hasDeviceId) {
          st = MB85RC::Status::Ok();
        } else {
          st = device.readDeviceId(id);
          if (st.ok() && id.manufacturerId != MB85RC::cmd::MANUFACTURER_ID) {
            st = MB85RC::Status::Error(MB85RC::Err::DEVICE_ID_MISMATCH, "unexpected manufacturer");
          }
        }
        break;
      }
      case 6: {
        // current-address reads need a successful addressed memory access first.
        // Keep this before recover(), because recover() intentionally invalidates
        // the cached current-address pointer.
        uint8_t value = 0;
        st = device.readCurrentAddress(value);
        break;
      }
      case 7: {
        st = device.recover();
        break;
      }
      case 8: {
        MB85RC::SettingsSnapshot snap;
        st = device.getSettings(snap);
        break;
      }
      default:
        break;
    }

    if (st.ok()) {
      stats[op].ok++;
      okTotal++;
    } else {
      stats[op].fail++;
      failTotal++;
      if (verboseMode) {
        Serial.printf("  [%d] %s failed: %s\n", i, stats[op].name, errToStr(st.code));
      }
    }

    printStressProgress(static_cast<uint32_t>(i + 1),
                        static_cast<uint32_t>(count),
                        okTotal,
                        failTotal);
  }

  const uint32_t elapsed = millis() - startMs;

  Serial.println("=== stress_mix summary ===");
  const float successPct =
      (count > 0) ? (100.0f * static_cast<float>(okTotal) / static_cast<float>(count)) : 0.0f;
  Serial.printf("  Total: %sok=%lu%s %sfail=%lu%s (%s%.2f%%%s)\n",
                goodIfNonZeroColor(okTotal),
                static_cast<unsigned long>(okTotal),
                LOG_COLOR_RESET,
                goodIfZeroColor(failTotal),
                static_cast<unsigned long>(failTotal),
                LOG_COLOR_RESET,
                successRateColor(successPct),
                successPct,
                LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(elapsed));
  if (elapsed > 0) {
    Serial.printf("  Rate: %.2f commands/s\n", (1000.0f * static_cast<float>(count)) / elapsed);
  }
  for (int i = 0; i < opCount; ++i) {
    Serial.printf("  %-14s %sok=%lu%s %sfail=%lu%s\n",
                  stats[i].name,
                  goodIfNonZeroColor(stats[i].ok),
                  static_cast<unsigned long>(stats[i].ok),
                  LOG_COLOR_RESET,
                  goodIfZeroColor(stats[i].fail),
                  static_cast<unsigned long>(stats[i].fail),
                  LOG_COLOR_RESET);
  }
  const uint32_t successDelta = device.totalSuccess() - succBefore;
  const uint32_t failDelta = device.totalFailures() - failBefore;
  Serial.printf("  Tracked I2C health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                goodIfNonZeroColor(successDelta),
                static_cast<unsigned long>(successDelta),
                LOG_COLOR_RESET,
                goodIfZeroColor(failDelta),
                static_cast<unsigned long>(failDelta),
                LOG_COLOR_RESET);
}

void runSelfTest() {
  struct Result {
    uint32_t pass = 0;
    uint32_t fail = 0;
    uint32_t skip = 0;
  } result;

  enum class SelftestOutcome : uint8_t { PASS, FAIL, SKIP };
  auto report = [&](const char* name, SelftestOutcome outcome, const char* note) {
    const bool ok = (outcome == SelftestOutcome::PASS);
    const bool skip = (outcome == SelftestOutcome::SKIP);
    const char* color = skip ? LOG_COLOR_YELLOW : LOG_COLOR_RESULT(ok);
    const char* tag = skip ? "SKIP" : (ok ? "PASS" : "FAIL");
    Serial.printf("  [%s%s%s] %s", color, tag, LOG_COLOR_RESET, name);
    if (note && note[0]) {
      Serial.printf(" - %s", note);
    }
    Serial.println();
    if (skip) {
      result.skip++;
    } else if (ok) {
      result.pass++;
    } else {
      result.fail++;
    }
  };
  auto reportCheck = [&](const char* name, bool ok, const char* note) {
    report(name, ok ? SelftestOutcome::PASS : SelftestOutcome::FAIL, note);
  };
  auto reportSkip = [&](const char* name, const char* note) {
    report(name, SelftestOutcome::SKIP, note);
  };

  Serial.println("=== MB85RC selftest (diagnostic commands) ===");

  const uint32_t succBefore = device.totalSuccess();
  const uint32_t failBefore = device.totalFailures();
  const uint8_t consBefore = device.consecutiveFailures();
  const uint32_t maxAddr = device.maxAddress();
  const uint32_t capacity = device.capacityBytes();
  const uint32_t invalidAddr = maxAddr + 1U;
  const MB85RC::cmd::VariantInfo* activeVariant = device.variantInfo();

  // Probe (no health tracking)
  MB85RC::Status st = device.probe();
  if (st.code == MB85RC::Err::NOT_INITIALIZED) {
    reportSkip("probe responds", "driver not initialized");
    reportSkip("remaining checks", "selftest aborted");
    Serial.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                  goodIfNonZeroColor(result.pass), static_cast<unsigned long>(result.pass), LOG_COLOR_RESET,
                  goodIfZeroColor(result.fail), static_cast<unsigned long>(result.fail), LOG_COLOR_RESET,
                  skipCountColor(result.skip), static_cast<unsigned long>(result.skip), LOG_COLOR_RESET);
    return;
  }
  reportCheck("probe responds", st.ok(), st.ok() ? "" : errToStr(st.code));
  const bool probeNoTrack = device.totalSuccess() == succBefore &&
                            device.totalFailures() == failBefore &&
                            device.consecutiveFailures() == consBefore;
  reportCheck("probe no-health-side-effects", probeNoTrack, "");

  // Device ID
  if (activeVariant != nullptr && !activeVariant->hasDeviceId) {
    reportSkip("readDeviceId", "active variant has no Device ID command");
    reportSkip("readDeviceIdRaw", "active variant has no Device ID command");
  } else {
    MB85RC::DeviceId id;
    st = device.readDeviceId(id);
    reportCheck("readDeviceId", st.ok(), st.ok() ? "" : errToStr(st.code));
    reportCheck("manufacturer ID = 0x00A",
                st.ok() && id.manufacturerId == MB85RC::cmd::MANUFACTURER_ID, "");
    reportCheck("product ID matches active variant",
                st.ok() && activeVariant != nullptr && id.productId == activeVariant->productId, "");
    reportCheck("density code matches active variant",
                st.ok() && activeVariant != nullptr && id.densityCode == activeVariant->densityCode, "");
    MB85RC::DeviceIdRaw rawId;
    st = device.readDeviceIdRaw(rawId);
    reportCheck("readDeviceIdRaw", st.ok(), st.ok() ? "" : errToStr(st.code));
  }

  // Write/Read test at address 0x0000
  // Save original value first
  uint8_t origVal = 0;
  st = device.readByte(0x0000, origVal);
  reportCheck("readByte(0x0000)", st.ok(), st.ok() ? "" : errToStr(st.code));

  const uint8_t testPattern = 0xA5;
  st = device.writeByte(0x0000, testPattern);
  reportCheck("writeByte(0x0000, 0xA5)", st.ok(), st.ok() ? "" : errToStr(st.code));

  uint8_t readBack = 0;
  st = device.readByte(0x0000, readBack);
  reportCheck("readBack(0x0000)", st.ok(), st.ok() ? "" : errToStr(st.code));
  reportCheck("verify data = 0xA5", st.ok() && readBack == testPattern, "");

  // Restore original value
  st = device.writeByte(0x0000, origVal);
  reportCheck("restoreByte(0x0000)", st.ok(), st.ok() ? "" : errToStr(st.code));
  if (st.ok()) {
    uint8_t restored = 0;
    st = device.readByte(0x0000, restored);
    reportCheck("verify restore(0x0000)", st.ok() && restored == origVal,
                st.ok() ? "" : errToStr(st.code));
  } else {
    reportCheck("verify restore(0x0000)", false, errToStr(st.code));
  }

  // Multi-byte write/read
  uint8_t testBuf[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  uint8_t origBuf[4] = {};
  st = device.read(0x0010, origBuf, 4);
  reportCheck("read(0x0010, 4)", st.ok(), st.ok() ? "" : errToStr(st.code));

  st = device.write(0x0010, testBuf, 4);
  reportCheck("write(0x0010, 4)", st.ok(), st.ok() ? "" : errToStr(st.code));

  uint8_t verifyBuf[4] = {};
  st = device.read(0x0010, verifyBuf, 4);
  reportCheck("readBack(0x0010, 4)", st.ok(), st.ok() ? "" : errToStr(st.code));
  const bool multimatch = (verifyBuf[0] == 0xDE && verifyBuf[1] == 0xAD &&
                           verifyBuf[2] == 0xBE && verifyBuf[3] == 0xEF);
  reportCheck("verify multi-byte data", st.ok() && multimatch, "");

  // Restore original
  st = device.write(0x0010, origBuf, 4);
  reportCheck("restore(0x0010, 4)", st.ok(), st.ok() ? "" : errToStr(st.code));
  if (st.ok()) {
    uint8_t restoredBuf[4] = {};
    st = device.read(0x0010, restoredBuf, 4);
    const bool restoreMatch = st.ok() && (std::memcmp(restoredBuf, origBuf, sizeof(origBuf)) == 0);
    reportCheck("verify restore(0x0010, 4)", restoreMatch, st.ok() ? "" : errToStr(st.code));
  } else {
    reportCheck("verify restore(0x0010, 4)", false, errToStr(st.code));
  }

  // Fill test
  uint8_t origFill[8] = {};
  st = device.read(0x0020, origFill, 8);
  reportCheck("read fill area", st.ok(), st.ok() ? "" : errToStr(st.code));

  st = device.fill(0x0020, 0x55, 8);
  reportCheck("fill(0x0020, 0x55, 8)", st.ok(), st.ok() ? "" : errToStr(st.code));

  uint8_t fillVerify[8] = {};
  st = device.read(0x0020, fillVerify, 8);
  reportCheck("readBack fill area", st.ok(), st.ok() ? "" : errToStr(st.code));
  bool fillOk = true;
  for (int i = 0; i < 8; ++i) {
    if (fillVerify[i] != 0x55) {
      fillOk = false;
      break;
    }
  }
  reportCheck("verify fill data", st.ok() && fillOk, "");

  // Restore original
  st = device.write(0x0020, origFill, 8);
  reportCheck("restore fill area", st.ok(), st.ok() ? "" : errToStr(st.code));
  if (st.ok()) {
    uint8_t restoredFill[8] = {};
    st = device.read(0x0020, restoredFill, 8);
    const bool restoreFillOk = st.ok() && (std::memcmp(restoredFill, origFill, sizeof(origFill)) == 0);
    reportCheck("verify restore fill area", restoreFillOk, st.ok() ? "" : errToStr(st.code));
  } else {
    reportCheck("verify restore fill area", false, errToStr(st.code));
  }

  // Settings snapshot + current address read at the active variant boundary
  MB85RC::SettingsSnapshot snap;
  st = device.getSettings(snap);
  reportCheck("getSettings", st.ok(), st.ok() ? "" : errToStr(st.code));
  reportCheck("current address known", st.ok() && snap.currentAddressKnown, "");
  reportCheck("runtime capacity valid", st.ok() && snap.capacityBytes == capacity, "");

  uint8_t lastOrig = 0;
  uint8_t firstOrig = 0;
  st = device.readByte(maxAddr, lastOrig);
  reportCheck("readByte(max address)", st.ok(), st.ok() ? "" : errToStr(st.code));
  st = device.readByte(0x0000, firstOrig);
  reportCheck("readByte(0x0000)", st.ok(), st.ok() ? "" : errToStr(st.code));

  st = device.writeByte(maxAddr, 0x3C);
  reportCheck("writeByte(max address, 0x3C)", st.ok(), st.ok() ? "" : errToStr(st.code));
  st = device.writeByte(0x0000, 0xC3);
  reportCheck("writeByte(0x0000, 0xC3)", st.ok(), st.ok() ? "" : errToStr(st.code));

  uint8_t tailValue = 0;
  st = device.readByte(maxAddr, tailValue);
  reportCheck("verify tail data = 0x3C", st.ok() && tailValue == 0x3C,
              st.ok() ? "" : errToStr(st.code));

  uint8_t currentVal = 0;
  st = device.readCurrentAddress(currentVal);
  reportCheck("readCurrentAddress follows max address", st.ok() && currentVal == 0xC3,
              st.ok() ? "" : errToStr(st.code));

  st = device.getSettings(snap);
  reportCheck("tracked current address = 0x0001",
              st.ok() && snap.currentAddressKnown && snap.currentAddress == 0x0001,
              st.ok() ? "" : errToStr(st.code));

  st = device.writeByte(maxAddr, lastOrig);
  reportCheck("restoreByte(max address)", st.ok(), st.ok() ? "" : errToStr(st.code));
  st = device.writeByte(0x0000, firstOrig);
  reportCheck("restoreByte(0x0000 after boundary check)", st.ok(), st.ok() ? "" : errToStr(st.code));
  if (st.ok()) {
    uint8_t verifyFirst = 0;
    st = device.readByte(0x0000, verifyFirst);
    reportCheck("verify restore(0x0000 after boundary check)",
                st.ok() && verifyFirst == firstOrig,
                st.ok() ? "" : errToStr(st.code));
  } else {
    reportCheck("verify restore(0x0000 after boundary check)", false, errToStr(st.code));
  }
  uint8_t verifyLast = 0;
  st = device.readByte(maxAddr, verifyLast);
  reportCheck("verify restore(max address)",
              st.ok() && verifyLast == lastOrig,
              st.ok() ? "" : errToStr(st.code));

  // Invalid address handling
  uint8_t invalidRead = 0;
  st = device.readByte(invalidAddr, invalidRead);
  reportCheck("readByte(first invalid address) rejects",
              st.code == MB85RC::Err::ADDRESS_OUT_OF_RANGE,
              st.ok() ? "" : errToStr(st.code));
  st = device.writeByte(invalidAddr, 0x00);
  reportCheck("writeByte(first invalid address) rejects",
              st.code == MB85RC::Err::ADDRESS_OUT_OF_RANGE,
              st.ok() ? "" : errToStr(st.code));
  st = device.fill(invalidAddr, 0x00, 1);
  reportCheck("fill(first invalid address, 1) rejects",
              st.code == MB85RC::Err::ADDRESS_OUT_OF_RANGE,
              st.ok() ? "" : errToStr(st.code));

  // Recover
  st = device.recover();
  reportCheck("recover", st.ok(), st.ok() ? "" : errToStr(st.code));
  reportCheck("isOnline", device.isOnline(), "");

  // Memory size
  reportCheck("legacy memorySize = 32768", MB85RC::MB85RC::memorySize() == 32768, "");
  reportCheck("runtime capacity matches active variant",
              activeVariant != nullptr && capacity == activeVariant->memoryBytes, "");

  Serial.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                goodIfNonZeroColor(result.pass), static_cast<unsigned long>(result.pass), LOG_COLOR_RESET,
                goodIfZeroColor(result.fail), static_cast<unsigned long>(result.fail), LOG_COLOR_RESET,
                skipCountColor(result.skip),
                static_cast<unsigned long>(result.skip), LOG_COLOR_RESET);
}

void runReadWriteSuite() {
  struct Result {
    uint32_t pass = 0;
    uint32_t fail = 0;
  } result;

  auto reportCheck = [&](const char* name, bool ok, const char* note) {
    Serial.printf("  [%s%s%s] %s",
                  LOG_COLOR_RESULT(ok),
                  ok ? "PASS" : "FAIL",
                  LOG_COLOR_RESET,
                  name);
    if (note && note[0]) {
      Serial.printf(" - %s", note);
    }
    Serial.println();
    if (ok) {
      result.pass++;
    } else {
      result.fail++;
    }
  };
  auto reportStatus = [&](const char* name, const MB85RC::Status& st) {
    reportCheck(name, st.ok(), st.ok() ? "" : errToStr(st.code));
  };

  Serial.println("=== Read/Write Suite ===");

  uint8_t originalScratch[RW_SUITE_LEN] = {};
  uint8_t originalFill[RW_SUITE_FILL_LEN] = {};
  uint8_t originalTail[8] = {};
  bool haveScratch = false;
  bool haveFill = false;
  bool haveTail = false;
  const uint32_t tailAddr = device.maxAddress() - (sizeof(originalTail) - 1U);

  MB85RC::Status st = typed_memory::readBytes(device,
                                              RW_SUITE_ADDR,
                                              originalScratch,
                                              sizeof(originalScratch));
  haveScratch = st.ok();
  reportStatus("backup scratch region", st);

  st = typed_memory::readBytes(device,
                               RW_SUITE_FILL_ADDR,
                               originalFill,
                               sizeof(originalFill));
  haveFill = st.ok();
  reportStatus("backup fill region", st);

  st = device.read(tailAddr, originalTail, sizeof(originalTail));
  haveTail = st.ok();
  reportStatus("backup tail region", st);

  uint8_t scratchPattern[RW_SUITE_LEN] = {};
  for (size_t i = 0; i < sizeof(scratchPattern); ++i) {
    scratchPattern[i] = static_cast<uint8_t>(0x21U + static_cast<uint8_t>(i * 13U));
  }

  st = device.write(RW_SUITE_ADDR, scratchPattern, sizeof(scratchPattern));
  reportStatus("write scratch pattern", st);

  MB85RC::VerifyResult verify;
  st = device.verify(RW_SUITE_ADDR, scratchPattern, sizeof(scratchPattern), verify);
  reportStatus("verify scratch transaction", st);
  reportCheck("verify scratch contents", st.ok() && verify.match, "");

  uint8_t readBack[RW_SUITE_LEN] = {};
  st = device.read(RW_SUITE_ADDR, readBack, sizeof(readBack));
  reportStatus("read scratch pattern", st);
  reportCheck("scratch round-trip bytes match",
              st.ok() && std::memcmp(readBack, scratchPattern, sizeof(readBack)) == 0,
              "");

  st = device.fill(RW_SUITE_FILL_ADDR, 0xA5, RW_SUITE_FILL_LEN);
  reportStatus("fill test region", st);

  uint8_t fillReadBack[RW_SUITE_FILL_LEN] = {};
  st = device.read(RW_SUITE_FILL_ADDR, fillReadBack, sizeof(fillReadBack));
  reportStatus("read fill region", st);
  bool fillOk = true;
  for (size_t i = 0; i < sizeof(fillReadBack); ++i) {
    if (fillReadBack[i] != 0xA5U) {
      fillOk = false;
      break;
    }
  }
  reportCheck("fill bytes match expected", st.ok() && fillOk, "");

  const uint8_t tailPattern[8] = {0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7};
  st = device.write(tailAddr, tailPattern, sizeof(tailPattern));
  reportStatus("write tail pattern", st);

  uint8_t tailReadBack[8] = {};
  st = device.read(tailAddr, tailReadBack, sizeof(tailReadBack));
  reportStatus("read tail pattern", st);
  reportCheck("tail bytes match",
              st.ok() && std::memcmp(tailReadBack, tailPattern, sizeof(tailReadBack)) == 0,
              "");

  if (haveScratch) {
    reportStatus("restore scratch region",
                 typed_memory::writeBytes(device,
                                          RW_SUITE_ADDR,
                                          originalScratch,
                                          sizeof(originalScratch)));
  }
  if (haveFill) {
    reportStatus("restore fill region",
                 typed_memory::writeBytes(device,
                                          RW_SUITE_FILL_ADDR,
                                          originalFill,
                                          sizeof(originalFill)));
  }
  if (haveTail) {
    reportStatus("restore tail region", device.write(tailAddr, originalTail, sizeof(originalTail)));
  }

  Serial.printf("Read/write suite result: pass=%s%lu%s fail=%s%lu%s\n",
                goodIfNonZeroColor(result.pass), static_cast<unsigned long>(result.pass), LOG_COLOR_RESET,
                goodIfZeroColor(result.fail), static_cast<unsigned long>(result.fail), LOG_COLOR_RESET);
}

struct XferDemoResult {
  uint32_t pass = 0;
  uint32_t fail = 0;
};

void reportXferDemoCheck(XferDemoResult& result, const char* name, bool ok, const char* note = "") {
  Serial.printf("  [%s%s%s] %s",
                LOG_COLOR_RESULT(ok),
                ok ? "PASS" : "FAIL",
                LOG_COLOR_RESET,
                name);
  if (note != nullptr && note[0] != '\0') {
    Serial.printf(" - %s", note);
  }
  Serial.println();
  if (ok) {
    result.pass++;
  } else {
    result.fail++;
  }
}

void reportXferDemoStatus(XferDemoResult& result, const char* name, const MB85RC::Status& st) {
  reportXferDemoCheck(result, name, st.ok(), st.ok() ? "" : errToStr(st.code));
}

uint32_t stagedDemoAddress() {
  const uint32_t capacity = device.capacityBytes();
  if (capacity > XFER_DEMO_ADDR && XFER_DEMO_LEN <= (capacity - XFER_DEMO_ADDR)) {
    return XFER_DEMO_ADDR;
  }
  return 0U;
}

size_t stagedDemoLength(uint32_t address) {
  const uint32_t capacity = device.capacityBytes();
  if (capacity == 0U || address >= capacity) {
    return 0U;
  }
  const uint32_t remaining = capacity - address;
  return (remaining > XFER_DEMO_LEN) ? XFER_DEMO_LEN : static_cast<size_t>(remaining);
}

MB85RC::Status takeStagedTerminal(MB85RC::Status terminal) {
  MB85RC::TransferResult result;
  const MB85RC::Status taken = device.takeTransferResult(result);
  return taken.ok() ? result.status : terminal;
}

MB85RC::Status pollStagedTransferToCompletion(size_t len, size_t chunkSize) {
  if (len == 0U || chunkSize == 0U) {
    return MB85RC::Status::Error(MB85RC::Err::INVALID_PARAM, "Invalid staged transfer poll bounds");
  }
  const uint32_t expectedChunks =
      static_cast<uint32_t>((len + chunkSize - 1U) / chunkSize);
  const uint32_t pollLimit = expectedChunks + 3U;
  for (uint32_t i = 0; i < pollLimit; ++i) {
    MB85RC::Status st = device.pollTransfer(millis(), 1);
    if (st.inProgress()) {
      continue;
    }
    return takeStagedTerminal(st);
  }
  (void)device.cancelTransfer();
  MB85RC::TransferResult cancelled;
  (void)device.takeTransferResult(cancelled);
  return MB85RC::Status::Error(MB85RC::Err::TIMEOUT, "Staged transfer poll limit exhausted");
}

void printHeapTelemetry() {
  Serial.printf("heap: free=%lu min_free=%lu largest=%lu\n",
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()),
                static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}

void runTransferDemo() {
  XferDemoResult result;
  Serial.println("=== Poll-Chunked Transfer Demo ===");

  const uint32_t addr = stagedDemoAddress();
  const size_t len = stagedDemoLength(addr);
  if (len == 0U) {
    reportXferDemoCheck(result, "select scratch range", false, "active capacity is zero");
    Serial.printf("Transfer demo result: pass=%s%lu%s fail=%s%lu%s\n",
                  goodIfNonZeroColor(result.pass), static_cast<unsigned long>(result.pass), LOG_COLOR_RESET,
                  goodIfZeroColor(result.fail), static_cast<unsigned long>(result.fail), LOG_COLOR_RESET);
    return;
  }

  Serial.printf("  Scratch: 0x%04lX + %u\n",
                static_cast<unsigned long>(addr),
                static_cast<unsigned>(len));

  static uint8_t original[XFER_DEMO_LEN] = {};
  static uint8_t readBack[XFER_DEMO_LEN] = {};
  static uint8_t pattern[XFER_DEMO_LEN] = {};
  static uint8_t fillExpected[XFER_DEMO_LEN] = {};
  for (size_t i = 0; i < len; ++i) {
    pattern[i] = static_cast<uint8_t>(0x30U + ((i * 17U) & 0x7FU));
    fillExpected[i] = 0x5AU;
  }

  MB85RC::Status st = device.read(addr, original, len);
  reportXferDemoStatus(result, "backup staged scratch region", st);
  if (!st.ok()) {
    Serial.printf("Transfer demo result: pass=%s%lu%s fail=%s%lu%s\n",
                  goodIfNonZeroColor(result.pass), static_cast<unsigned long>(result.pass), LOG_COLOR_RESET,
                  goodIfZeroColor(result.fail), static_cast<unsigned long>(result.fail), LOG_COLOR_RESET);
    return;
  }

  st = device.requestRead(addr, readBack, len);
  reportXferDemoStatus(result, "requestRead queues without I2C", st);
  if (st.ok()) {
    MB85RC::Status zeroBudget = device.pollTransfer(millis(), 0);
    reportXferDemoCheck(result, "zero-budget poll remains in progress", zeroBudget.inProgress(),
                        zeroBudget.inProgress() ? "" : errToStr(zeroBudget.code));
    uint8_t tmp = 0;
    MB85RC::Status busy = device.readByte(addr, tmp);
    reportXferDemoCheck(result, "sync read rejected while transfer busy",
                        busy.code == MB85RC::Err::BUSY,
                        busy.ok() ? "" : errToStr(busy.code));
    MB85RC::Status budgetTwo = device.pollTransfer(millis(), 2);
    reportXferDemoCheck(result, "poll budget 2 executes two chunks",
                        budgetTwo.inProgress(),
                        budgetTwo.inProgress() ? "" : errToStr(budgetTwo.code));
    st = pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_READ_CHUNK);
    reportXferDemoStatus(result, "poll read with one-instruction budget", st);
    reportXferDemoCheck(result, "staged read bytes match backup",
                        st.ok() && std::memcmp(readBack, original, len) == 0, "");
  }

  st = device.requestWrite(addr, pattern, len);
  reportXferDemoStatus(result, "requestWrite staged pattern", st);
  if (st.ok()) {
    st = pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_WRITE_CHUNK);
    reportXferDemoStatus(result, "poll write with one-instruction budget", st);
  }

  st = device.requestVerify(addr, pattern, len);
  reportXferDemoStatus(result, "requestVerify staged pattern", st);
  if (st.ok()) {
    st = pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_READ_CHUNK);
    reportXferDemoStatus(result, "poll verify staged pattern", st);
  }

  st = device.requestFill(addr, 0x5AU, len);
  reportXferDemoStatus(result, "requestFill staged pattern", st);
  if (st.ok()) {
    const bool highBudgetShouldRemainActive =
        len > (static_cast<size_t>(MB85RC::cmd::MAX_TRANSFER_INSTRUCTIONS_PER_POLL) *
               static_cast<size_t>(MB85RC::cmd::MAX_FILL_CHUNK));
    MB85RC::Status highBudget = device.pollTransfer(millis(), 255);
    reportXferDemoCheck(result, "poll high budget clamps to 8 chunks",
                        highBudgetShouldRemainActive ? highBudget.inProgress() : highBudget.ok(),
                        highBudget.msg);
    st = highBudget.inProgress()
             ? pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_FILL_CHUNK)
             : takeStagedTerminal(highBudget);
    reportXferDemoStatus(result, "poll fill with one-instruction budget", st);
  }

  st = device.requestVerify(addr, fillExpected, len);
  reportXferDemoStatus(result, "requestVerify staged fill", st);
  if (st.ok()) {
    st = pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_READ_CHUNK);
    reportXferDemoStatus(result, "poll verify staged fill", st);
  }

  st = device.requestWrite(addr, original, len);
  reportXferDemoStatus(result, "requestWrite restore backup", st);
  if (st.ok()) {
    st = pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_WRITE_CHUNK);
    reportXferDemoStatus(result, "poll restore backup", st);
  }

  st = device.requestVerify(addr, original, len);
  reportXferDemoStatus(result, "requestVerify restored bytes", st);
  if (st.ok()) {
    st = pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_READ_CHUNK);
    reportXferDemoStatus(result, "poll verify restored bytes", st);
  }

  Serial.printf("Transfer demo result: pass=%s%lu%s fail=%s%lu%s\n",
                goodIfNonZeroColor(result.pass), static_cast<unsigned long>(result.pass), LOG_COLOR_RESET,
                goodIfZeroColor(result.fail), static_cast<unsigned long>(result.fail), LOG_COLOR_RESET);
}

void runRandomBench(int count) {
  Serial.println("=== Random Access Benchmark ===");
  const uint32_t benchAddr = randomBenchAddress();
  if (!rangeFitsActiveCapacity(benchAddr, RANDOM_BENCH_LEN)) {
    printRangeError(benchAddr, RANDOM_BENCH_LEN);
    return;
  }
  Serial.printf("  Window: 0x%04lX + %u\n",
                static_cast<unsigned long>(benchAddr),
                static_cast<unsigned>(RANDOM_BENCH_LEN));

  uint8_t originalWindow[RANDOM_BENCH_LEN] = {};
  MB85RC::Status st = typed_memory::readBytes(device,
                                              benchAddr,
                                              originalWindow,
                                              sizeof(originalWindow));
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  uint8_t shadowWindow[RANDOM_BENCH_LEN];
  std::memcpy(shadowWindow, originalWindow, sizeof(shadowWindow));

  uint32_t seed = 0x13579BDFU;
  uint32_t writeElapsedUs = 0;
  uint32_t readElapsedUs = 0;

  uint32_t startUs = micros();
  for (int i = 0; i < count; ++i) {
    const size_t index = static_cast<size_t>(nextRandom(seed) % RANDOM_BENCH_LEN);
    const uint8_t value = static_cast<uint8_t>(nextRandom(seed) & 0xFFU);
    st = device.writeByte(benchAddr + static_cast<uint32_t>(index), value);
    if (!st.ok()) {
      break;
    }
    shadowWindow[index] = value;
  }
  writeElapsedUs = micros() - startUs;

  if (!st.ok()) {
    printStatus(st);
    (void)typed_memory::writeBytes(device,
                                   benchAddr,
                                   originalWindow,
                                   sizeof(originalWindow));
    return;
  }

  uint32_t mismatches = 0;
  seed = 0x2468ACE1U;
  startUs = micros();
  for (int i = 0; i < count; ++i) {
    const size_t index = static_cast<size_t>(nextRandom(seed) % RANDOM_BENCH_LEN);
    uint8_t value = 0;
    st = device.readByte(benchAddr + static_cast<uint32_t>(index), value);
    if (!st.ok()) {
      break;
    }
    if (value != shadowWindow[index]) {
      mismatches++;
    }
  }
  readElapsedUs = micros() - startUs;

  if (!st.ok()) {
    printStatus(st);
    (void)typed_memory::writeBytes(device,
                                   benchAddr,
                                   originalWindow,
                                   sizeof(originalWindow));
    return;
  }

  MB85RC::VerifyResult verify;
  st = device.verify(benchAddr, shadowWindow, sizeof(shadowWindow), verify);
  if (!st.ok()) {
    printStatus(st);
  } else {
    Serial.printf("  Final window verify: %s%s%s\n",
                  LOG_COLOR_RESULT(verify.match),
                  verify.match ? "PASS" : "FAIL",
                  LOG_COLOR_RESET);
    if (!verify.match) {
      Serial.printf("  Mismatch offset: %lu expected=0x%02X actual=0x%02X\n",
                    static_cast<unsigned long>(verify.mismatchOffset),
                    verify.expected,
                    verify.actual);
    }
  }

  printBenchmarkLine("random-write-byte", static_cast<uint32_t>(count), writeElapsedUs, 1U);
  printBenchmarkLine("random-read-byte", static_cast<uint32_t>(count), readElapsedUs, 1U);
  Serial.printf("  Read mismatches: %lu\n", static_cast<unsigned long>(mismatches));

  st = typed_memory::writeBytes(device,
                                benchAddr,
                                originalWindow,
                                sizeof(originalWindow));
  printCheckStatus("restore benchmark window", st);
}

void runTypedDemo() {
  Serial.println("=== Typed Value Demo ===");
  Serial.println("  Storage format: explicit little-endian, fixed-width, no silent wrap.");

  uint8_t original[TYPED_DEMO_LEN] = {};
  MB85RC::Status st = typed_memory::readBytes(device, TYPED_DEMO_ADDR, original, sizeof(original));
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  uint32_t cursor = TYPED_DEMO_ADDR;
  printCheckStatus("write uint8", typed_memory::writeUint8(device, cursor, 0x7EU));
  cursor += 1U;
  printCheckStatus("write uint16 LE", typed_memory::writeUint16Le(device, cursor, 0x1234U));
  cursor += 2U;
  printCheckStatus("write int32 LE", typed_memory::writeInt32Le(device, cursor, -1234567));
  cursor += 4U;
  printCheckStatus("write uint64 LE",
                   typed_memory::writeUint64Le(device, cursor, 0x1122334455667788ULL));
  cursor += 8U;
  printCheckStatus("write float32 LE", typed_memory::writeFloat32Le(device, cursor, 1.25f));
  cursor += 4U;
  printCheckStatus("write float64 LE", typed_memory::writeFloat64Le(device, cursor, -42.5));
  cursor += 8U;
  printCheckStatus("write bool", typed_memory::writeBool(device, cursor, true));

  uint8_t u8 = 0;
  uint16_t u16 = 0;
  int32_t i32 = 0;
  uint64_t u64 = 0;
  float f32 = 0.0f;
  double f64 = 0.0;
  bool flag = false;

  cursor = TYPED_DEMO_ADDR;
  st = typed_memory::readUint8(device, cursor, u8);
  if (!st.ok()) {
    printStatus(st);
  }
  cursor += 1U;
  st = typed_memory::readUint16Le(device, cursor, u16);
  if (!st.ok()) {
    printStatus(st);
  }
  cursor += 2U;
  st = typed_memory::readInt32Le(device, cursor, i32);
  if (!st.ok()) {
    printStatus(st);
  }
  cursor += 4U;
  st = typed_memory::readUint64Le(device, cursor, u64);
  if (!st.ok()) {
    printStatus(st);
  }
  cursor += 8U;
  st = typed_memory::readFloat32Le(device, cursor, f32);
  if (!st.ok()) {
    printStatus(st);
  }
  cursor += 4U;
  st = typed_memory::readFloat64Le(device, cursor, f64);
  if (!st.ok()) {
    printStatus(st);
  }
  cursor += 8U;
  st = typed_memory::readBool(device, cursor, flag);
  if (!st.ok()) {
    printStatus(st);
  }

  Serial.printf("  uint8   = 0x%02X\n", u8);
  Serial.printf("  uint16  = 0x%04X\n", u16);
  Serial.printf("  int32   = %ld\n", static_cast<long>(i32));
  Serial.printf("  uint64  = 0x%08lX%08lX\n",
                static_cast<unsigned long>(u64 >> 32),
                static_cast<unsigned long>(u64 & 0xFFFFFFFFULL));
  Serial.printf("  float   = %.6f\n", static_cast<double>(f32));
  Serial.printf("  double  = %.6f\n", f64);
  Serial.printf("  bool    = %s\n", flag ? "true" : "false");

  const uint32_t crossAddr = device.maxAddress() - 1U;
  st = typed_memory::writeUint32Le(device, crossAddr, 0xCAFEBABEU);
  Serial.printf("  Cross-boundary guard: %s%s%s\n",
                LOG_COLOR_RESULT(st.code == MB85RC::Err::ADDRESS_OUT_OF_RANGE),
                st.code == MB85RC::Err::ADDRESS_OUT_OF_RANGE ? "PASS" : "FAIL",
                LOG_COLOR_RESET);

  st = typed_memory::writeBytes(device, TYPED_DEMO_ADDR, original, sizeof(original));
  printCheckStatus("restore typed demo region", st);
}

void printHelp() {
  Serial.println();
  cli::printHelpHeader("MB85RC FRAM CLI Help");
  cli::printHelpSection("Common");
  cli::printHelpItem("help / ?", "Show this help");
  cli::printHelpItem("version / ver", "Print firmware and library version info");
  cli::printHelpItem("scan", "Scan I2C bus");
  cli::printHelpItem("cfg / settings", "Show active configuration snapshot");

  cli::printHelpSection("Memory Operations");
  cli::printHelpItem("read / dump / hexdump <addr> [len]", "Hex+ASCII dump within active capacity (default len=1)");
  cli::printHelpItem("text <addr> [len]", "Escaped ASCII-focused view (default len=64)");
  cli::printHelpItem("strings [addr len [minLen]]", "Scan printable ASCII strings (default whole chip, min=4)");
  cli::printHelpItem("crc <addr> <len>", "Compute CRC32 over a memory region");
  cli::printHelpItem("verify <addr> <byte> [byte...]", "Compare FRAM contents against expected bytes");
  cli::printHelpItem("write <addr> <byte> [byte...]", "Write byte(s) to address");
  cli::printHelpItem("fill <addr> <value> <len>", "Fill memory region with value");
  cli::printHelpItem("current / cur [len]", "Read byte(s) from current internal address");

  cli::printHelpSection("Device Info");
  cli::printHelpItem("id", "Read device ID (manufacturer, product, density)");
  cli::printHelpItem("idraw", "Read raw 3-byte Device ID payload");
  cli::printHelpItem("variants", "List known family variants and driver support");
  cli::printHelpItem("size", "Print active variant capacity");
  cli::printHelpItem("hs / hs support", "Report active variant High-speed capability");
  cli::printHelpItem("hs enter", "Enable HS-prefixed transfers if a raw special callback is configured");
  cli::printHelpItem("sleep / sleep support", "Report active variant Sleep capability and tREC");
  cli::printHelpItem("sleep enter", "Send Sleep entry sequence if a raw special callback is configured");
  cli::printHelpItem("sleep wake", "Send wake stimulus, wait tREC, then recover if supported");

  cli::printHelpSection("Diagnostics");
  cli::printHelpItem("heap", "Show example firmware heap telemetry");
  cli::printHelpItem("drv", "Show driver state and health");
  cli::printHelpItem("iface_reset", "Send 9 SCL pulses + STOP bus recovery sequence");
  cli::printHelpItem("probe", "Probe device (no health tracking)");
  cli::printHelpItem("recover", "Manual recovery attempt");
  cli::printHelpItem("verbose [0|1]", "Enable/disable verbose output");
  cli::printHelpItem("stress [N]", "Run N write/read/verify cycles (default 10)");
  cli::printHelpItem("stress_mix [N]", "Run N mixed-operation cycles (default 10)");
  cli::printHelpItem("selftest", "Run diagnostic self-test report with restore checks");
  cli::printHelpItem("rw_suite", "Run read/write/fill/verify suite with best-effort restore");
  cli::printHelpItem("xfer_demo", "Run poll-chunked transfer API demo with best-effort restore");
  cli::printHelpItem("randbench [N]", "Run N random writes + N random reads with timing (default 4096)");
  cli::printHelpItem("typed_demo", "Run fixed-width typed storage demo and restore data");
}

void printVersionInfo() {
  Serial.println("=== Version Info ===");
  Serial.printf("  Example firmware build: %s %s\n", __DATE__, __TIME__);
  Serial.printf("  MB85RC library version: %s\n", MB85RC::VERSION);
  Serial.printf("  MB85RC library full: %s\n", MB85RC::VERSION_FULL);
  Serial.printf("  MB85RC library build: %s\n", MB85RC::BUILD_TIMESTAMP);
  Serial.printf("  MB85RC library commit: %s (%s)\n", MB85RC::GIT_COMMIT, MB85RC::GIT_STATUS);
}

// ============================================================================
// Command Processing
// ============================================================================

void processCommand(const String& cmdLine) {
  String cmd = cmdLine;
  cmd.trim();
  if (cmd.length() == 0) {
    return;
  }

  if (cmd == "help" || cmd == "?") {
    printHelp();
    return;
  }

  if (cmd == "version" || cmd == "ver") {
    printVersionInfo();
    return;
  }

  if (cmd == "scan") {
    bus_diag::scan();
    return;
  }

  if (cmd == "hs" || cmd == "hs support" || cmd == "hs enter") {
    handleHighSpeedCommand(cmd);
    return;
  }

  if (cmd == "sleep" || cmd == "sleep support" ||
      cmd == "sleep enter" || cmd == "sleep wake") {
    handleSleepCommand(cmd);
    return;
  }

  if (cmd == "id") {
    MB85RC::DeviceId id;
    MB85RC::Status st = device.readDeviceId(id);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Device ID: Manufacturer=0x%03X Product=0x%03X Density=0x%02X\n",
                  id.manufacturerId, id.productId, id.densityCode);
    const MB85RC::cmd::VariantInfo* variant = MB85RC::cmd::findVariantByProductId(id.productId);
    if (variant != nullptr) {
      Serial.printf("  Variant: %s (%lu bytes)\n",
                    variant->name,
                    static_cast<unsigned long>(variant->memoryBytes));
      Serial.printf("  Runtime driver support: %s; access format: %s\n",
                    variant->supportedByDriver ? "yes" : "no",
                    variant->uses256vAccessFormat ? "2-byte linear address" : "variant-specific");
    } else {
      Serial.println("  Variant: unknown product ID");
    }
    return;
  }

  if (cmd == "variants") {
    printVariantCatalog();
    return;
  }

  if (cmd == "idraw") {
    MB85RC::DeviceIdRaw raw;
    MB85RC::Status st = device.readDeviceIdRaw(raw);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Device ID raw: %02X %02X %02X\n",
                  raw.bytes[0], raw.bytes[1], raw.bytes[2]);
    return;
  }

  if (cmd == "size") {
    Serial.printf("Active capacity: %lu bytes (max address 0x%04lX, variant %s)\n",
                  static_cast<unsigned long>(device.capacityBytes()),
                  static_cast<unsigned long>(device.maxAddress()),
                  device.variantName());
    Serial.printf("Legacy MB85RC256V memorySize(): %u bytes\n",
                  MB85RC::MB85RC::memorySize());
    return;
  }

  if (cmd == "current" || cmd == "cur") {
    printCurrentAddressRead();
    return;
  }

  if (cmd.startsWith("current ") || cmd.startsWith("cur ")) {
    const String lenStr = cmd.startsWith("current ") ? cmd.substring(8) : cmd.substring(4);
    uint32_t len = 0;
    if (!parseUint32(lenStr, len) || len == 0) {
      LOGW("Invalid length");
      return;
    }
    printCurrentAddressReadRange(len);
    return;
  }

  if (cmd == "read" || cmd == "dump" || cmd == "hexdump") {
    LOGW("Usage: read / dump / hexdump <addr> [len]");
    return;
  }

  if (cmd.startsWith("read ") || cmd.startsWith("dump ") || cmd.startsWith("hexdump ")) {
    String args = cmd.startsWith("hexdump ") ? cmd.substring(8) : cmd.substring(5);
    uint32_t addr = 0;
    uint32_t len = 0;
    if (!parseAddressLengthArgs(args, addr, len, 1U)) {
      LOGW("Usage: read / dump / hexdump <addr> [len]");
      return;
    }

    readRangeForDump(addr, len);
    return;
  }

  if (cmd == "text") {
    LOGW("Usage: text <addr> [len]");
    return;
  }

  if (cmd.startsWith("text ")) {
    uint32_t addr = 0;
    uint32_t len = 0;
    if (!parseAddressLengthArgs(cmd.substring(5), addr, len, 64U)) {
      LOGW("Usage: text <addr> [len]");
      return;
    }

    printTextRange(addr, len);
    return;
  }

  if (cmd == "strings") {
    Serial.printf("Scanning printable strings across full chip (minLen=%u)...\n", 4U);
    scanPrintableStrings(0x0000, device.capacityBytes(), 4U);
    return;
  }

  if (cmd.startsWith("strings ")) {
    String args = cmd.substring(8);
    args.trim();

    const int s1 = args.indexOf(' ');
    if (s1 < 0) {
      LOGW("Usage: strings [<addr> <len> [minLen]]");
      return;
    }

    const String addrStr = args.substring(0, s1);
    String rest = args.substring(s1 + 1);
    rest.trim();
    const int s2 = rest.indexOf(' ');

    const String lenStr = (s2 < 0) ? rest : rest.substring(0, s2);
    const String minStr = (s2 < 0) ? String() : rest.substring(s2 + 1);

    uint32_t addr = 0;
    uint32_t len = 0;
    uint16_t minLen = 4;
    if (!parseAddress(addrStr, addr)) {
      LOGW("Address out of range (max 0x%04lX)",
           static_cast<unsigned long>(device.maxAddress()));
      return;
    }
    if (!parseUint32(lenStr, len) || len == 0U) {
      LOGW("Invalid length");
      return;
    }
    if (minStr.length() > 0) {
      if (!parseUint16(minStr, minLen) || minLen == 0U) {
        LOGW("Invalid minLen");
        return;
      }
    }
    if (!rangeFitsActiveCapacity(addr, len)) {
      printRangeError(addr, len);
      return;
    }

    Serial.printf("Scanning printable strings at 0x%04lX len=%lu minLen=%u...\n",
                  static_cast<unsigned long>(addr),
                  static_cast<unsigned long>(len),
                  static_cast<unsigned>(minLen));
    scanPrintableStrings(addr, len, minLen);
    return;
  }

  if (cmd == "crc") {
    LOGW("Usage: crc <addr> <len>");
    return;
  }

  if (cmd.startsWith("crc ")) {
    uint32_t addr = 0;
    uint32_t len = 0;
    if (!parseAddressLengthArgs(cmd.substring(4), addr, len, 0U)) {
      LOGW("Usage: crc <addr> <len>");
      return;
    }

    printRangeCrc32(addr, len);
    return;
  }

  if (cmd == "verify") {
    LOGW("Usage: verify <addr> <byte> [byte...]");
    return;
  }

  if (cmd.startsWith("verify ")) {
    String args = cmd.substring(7);
    args.trim();

    const int firstSpace = args.indexOf(' ');
    if (firstSpace < 0) {
      LOGW("Usage: verify <addr> <byte> [byte...]");
      return;
    }

    const String addrStr = args.substring(0, firstSpace);
    String dataStr = args.substring(firstSpace + 1);
    dataStr.trim();

    uint32_t addr = 0;
    if (!parseAddress(addrStr, addr)) {
      LOGW("Address out of range (max 0x%04lX)",
           static_cast<unsigned long>(device.maxAddress()));
      return;
    }

    uint8_t expected[MB85RC::cmd::MAX_WRITE_CHUNK];
    size_t count = 0;
    while (dataStr.length() > 0) {
      if (count >= sizeof(expected)) {
        LOGW("Too many verify bytes (max %u per command)",
             static_cast<unsigned>(sizeof(expected)));
        return;
      }

      dataStr.trim();
      const int space = dataStr.indexOf(' ');
      String token;
      if (space < 0) {
        token = dataStr;
        dataStr = "";
      } else {
        token = dataStr.substring(0, space);
        dataStr = dataStr.substring(space + 1);
      }

      if (!parseByte(token, expected[count])) {
        LOGW("Invalid byte value: %s", token.c_str());
        return;
      }
      count++;
    }

    if (count == 0) {
      LOGW("No verify data provided");
      return;
    }
    if (!rangeFitsActiveCapacity(addr, count)) {
      printRangeError(addr, count);
      return;
    }

    MB85RC::VerifyResult result;
    MB85RC::Status st = device.verify(addr, expected, count, result);
    if (!st.ok()) {
      printStatus(st);
      return;
    }

    if (result.match) {
      Serial.printf("Verified %u byte(s) at 0x%04lX\n",
                    static_cast<unsigned>(count),
                    static_cast<unsigned long>(addr));
    } else {
      const uint32_t mismatchAddr = memoryAddressAtOffset(addr, result.mismatchOffset);
      Serial.printf("Verify mismatch at +0x%04lX (addr 0x%04lX): expected 0x%02X, actual 0x%02X\n",
                    static_cast<unsigned long>(result.mismatchOffset),
                    static_cast<unsigned long>(mismatchAddr),
                    result.expected,
                    result.actual);
    }
    return;
  }

  if (cmd.startsWith("write ")) {
    String args = cmd.substring(6);
    args.trim();

    const int firstSpace = args.indexOf(' ');
    if (firstSpace < 0) {
      LOGW("Usage: write <addr> <byte> [byte...]");
      return;
    }

    String addrStr = args.substring(0, firstSpace);
    String dataStr = args.substring(firstSpace + 1);
    dataStr.trim();

    uint32_t addr = 0;
    if (!parseAddress(addrStr, addr)) {
      LOGW("Address out of range (max 0x%04lX)",
           static_cast<unsigned long>(device.maxAddress()));
      return;
    }

    // Parse data bytes
    uint8_t writeBuf[MB85RC::cmd::MAX_WRITE_CHUNK];
    size_t count = 0;
    while (dataStr.length() > 0) {
      if (count >= sizeof(writeBuf)) {
        LOGW("Too many data bytes (max %u per command)",
             static_cast<unsigned>(sizeof(writeBuf)));
        return;
      }
      dataStr.trim();
      const int space = dataStr.indexOf(' ');
      String token;
      if (space < 0) {
        token = dataStr;
        dataStr = "";
      } else {
        token = dataStr.substring(0, space);
        dataStr = dataStr.substring(space + 1);
      }
      if (!parseByte(token, writeBuf[count])) {
        LOGW("Invalid byte value: %s", token.c_str());
        return;
      }
      count++;
    }

    if (count == 0) {
      LOGW("No data to write");
      return;
    }
    if (!rangeFitsActiveCapacity(addr, count)) {
      printRangeError(addr, count);
      return;
    }

    MB85RC::Status st = device.write(addr, writeBuf, count);
    if (!st.ok()) {
      printStatus(st);
    } else {
      Serial.printf("Wrote %u byte(s) at 0x%04lX\n",
                    static_cast<unsigned>(count),
                    static_cast<unsigned long>(addr));
    }
    return;
  }

  if (cmd.startsWith("fill ")) {
    String args = cmd.substring(5);
    args.trim();

    // Parse: fill <addr> <value> <len>
    const int s1 = args.indexOf(' ');
    if (s1 < 0) {
      LOGW("Usage: fill <addr> <value> <len>");
      return;
    }
    String addrStr = args.substring(0, s1);
    String rest = args.substring(s1 + 1);
    rest.trim();

    const int s2 = rest.indexOf(' ');
    if (s2 < 0) {
      LOGW("Usage: fill <addr> <value> <len>");
      return;
    }
    String valStr = rest.substring(0, s2);
    String lenStr = rest.substring(s2 + 1);
    lenStr.trim();

    uint32_t addr = 0;
    if (!parseAddress(addrStr, addr)) {
      LOGW("Address out of range (max 0x%04lX)",
           static_cast<unsigned long>(device.maxAddress()));
      return;
    }
    uint8_t value;
    if (!parseByte(valStr, value)) {
      LOGW("Invalid fill value");
      return;
    }
    uint32_t len = 0;
    if (!parseUint32(lenStr, len) || len == 0) {
      LOGW("Invalid length");
      return;
    }
    if (!rangeFitsActiveCapacity(addr, len)) {
      printRangeError(addr, len);
      return;
    }

    MB85RC::Status st = device.fill(addr, value, len);
    if (!st.ok()) {
      printStatus(st);
    } else {
      Serial.printf("Filled %lu byte(s) at 0x%04lX with 0x%02X\n",
                    static_cast<unsigned long>(len),
                    static_cast<unsigned long>(addr),
                    value);
    }
    return;
  }

  if (cmd == "cfg" || cmd == "settings") {
    printSettings();
    return;
  }

  if (cmd == "heap") {
    printHeapTelemetry();
    return;
  }

  if (cmd == "drv") {
    printDriverHealth();
    return;
  }

  if (cmd == "iface_reset") {
    if (!transport::interfaceReset(board::I2C_SDA, board::I2C_SCL)) {
      LOGE("Interface reset failed");
      return;
    }
    LOGI("Interface reset sequence sent (9 SCL pulses + STOP)");
    LOGI("Current-address tracking may be stale until the next addressed read/write.");
    return;
  }

  if (cmd == "probe") {
    LOGI("Probing device (no health tracking)...");
    MB85RC::Status st = device.probe();
    printStatus(st);
    return;
  }

  if (cmd == "recover") {
    LOGI("Attempting recovery...");
    MB85RC::Status st = device.recover();
    printStatus(st);
    printDriverHealth();
    return;
  }

  if (cmd == "verbose") {
    Serial.printf("  Verbose: %s%s%s\n",
                  verboseMode ? LOG_COLOR_GREEN : LOG_COLOR_RESET,
                  verboseMode ? "ON" : "OFF",
                  LOG_COLOR_RESET);
    return;
  }

  if (cmd.startsWith("verbose ")) {
    uint16_t value = 0;
    if (!parseUint16(cmd.substring(8), value) || value > 1U) {
      LOGW("Verbose value must be 0 or 1");
      return;
    }
    verboseMode = (value != 0U);
    LOGI("Verbose mode: %s", verboseMode ? "ON" : "OFF");
    return;
  }

  if (cmd == "selftest") {
    runSelfTest();
    return;
  }

  if (cmd == "rw_suite") {
    runReadWriteSuite();
    return;
  }

  if (cmd == "xfer_demo") {
    runTransferDemo();
    return;
  }

  if (cmd == "typed_demo") {
    runTypedDemo();
    return;
  }

  if (cmd == "randbench") {
    LOGI("Starting random benchmark: %d writes + %d reads", DEFAULT_RANDBENCH_OPS, DEFAULT_RANDBENCH_OPS);
    runRandomBench(DEFAULT_RANDBENCH_OPS);
    return;
  }

  if (cmd.startsWith("randbench ")) {
    int count = 0;
    if (!parseCountArg(cmd.substring(10), count)) {
      LOGW("Invalid randbench count (1-%d)", MAX_STRESS_COUNT);
      return;
    }
    LOGI("Starting random benchmark: %d writes + %d reads", count, count);
    runRandomBench(count);
    return;
  }

  if (cmd == "stress_mix") {
    LOGI("Starting stress_mix: %d mixed-operation cycles", DEFAULT_STRESS_COUNT);
    runStressMix(DEFAULT_STRESS_COUNT);
    return;
  }

  if (cmd.startsWith("stress_mix ")) {
    int count = 0;
    if (!parseCountArg(cmd.substring(11), count)) {
      LOGW("Invalid stress_mix count (1-%d)", MAX_STRESS_COUNT);
      return;
    }
    LOGI("Starting stress_mix: %d mixed-operation cycles", count);
    runStressMix(count);
    return;
  }

  if (cmd == "stress" || cmd.startsWith("stress ")) {
    int count = DEFAULT_STRESS_COUNT;
    if (cmd.length() > 6) {
      if (!parseCountArg(cmd.substring(6), count)) {
        LOGW("Invalid stress count (1-%d)", MAX_STRESS_COUNT);
        return;
      }
    }

    LOGI("Starting stress test: %d write/read/verify cycles", count);
    runStress(count);
    return;
  }

  LOGW("Unknown command: %s", cmd.c_str());
}

// ============================================================================
// Setup and Loop
// ============================================================================

void setup() {
  log_begin(115200);

  LOGI("=== MB85RC FRAM Bringup Example ===");

  if (!board::initI2c()) {
    LOGE("Failed to initialize I2C");
    return;
  }
  LOGI("I2C initialized (SDA=%d, SCL=%d)", board::I2C_SDA, board::I2C_SCL);

  bus_diag::scan();

  MB85RC::Config cfg;
  cfg.i2cWrite = transport::wireWrite;
  cfg.i2cWriteRead = transport::wireWriteRead;
  cfg.i2cSpecial = transport::wireSpecial;
  cfg.i2cUser = &Wire;
  cfg.i2cAddress = 0x50;
  cfg.i2cTimeoutMs = board::I2C_TIMEOUT_MS;
  cfg.expectedVariant = MB85RC::DeviceVariant::AUTO;
  cfg.nowMs = exampleNowMs;
  cfg.offlineThreshold = 5;

  MB85RC::Status st = device.bind(cfg);
  if (!st.ok()) {
    LOGE("Failed to bind device configuration");
    printStatus(st);
    return;
  }

  MB85RC::DeviceId identity;
  st = device.readDeviceId(identity);
  if (st.ok()) {
    LOGI("Device binding and identity check succeeded");
  } else {
    LOGW("Device identity unavailable; passive binding retained for later owner attempts");
    printStatus(st);
  }
  printDriverHealth();
  printHelp();
  cli::printPrompt();
}

void loop() {
  device.tick(millis());

  String line;
  while (cli_shell::readLine(line)) {
    Serial.println();
    processCommand(line);
    cli::printPrompt();
  }
}
