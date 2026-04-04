/// @file main.cpp
/// @brief Basic bringup example for MB85RC256V FRAM
/// @note This is an EXAMPLE, not part of the library

#include <Arduino.h>

#include "examples/common/Log.h"
#include "examples/common/BoardConfig.h"
#include "examples/common/BusDiag.h"
#include "examples/common/I2cTransport.h"
#include "examples/common/I2cScanner.h"

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

// ============================================================================
// Helper Functions
// ============================================================================

uint32_t exampleNowMs(void*) {
  return millis();
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

const char* stateColor(MB85RC::DriverState st, bool online, uint8_t consecutiveFailures) {
  if (st == MB85RC::DriverState::UNINIT) {
    return LOG_COLOR_RESET;
  }
  return LOG_COLOR_STATE(online, consecutiveFailures);
}

const char* goodIfZeroColor(uint32_t value) {
  return (value == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

const char* goodIfNonZeroColor(uint32_t value) {
  return (value > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW;
}

const char* successRateColor(float pct) {
  if (pct >= 99.9f) return LOG_COLOR_GREEN;
  if (pct >= 80.0f) return LOG_COLOR_YELLOW;
  return LOG_COLOR_RED;
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

void printHexDump(uint16_t startAddr, const uint8_t* data, size_t len) {
  for (size_t offset = 0; offset < len; offset += 16) {
    Serial.printf("  %04X: ", startAddr + static_cast<uint16_t>(offset));
    const size_t lineLen = (offset + 16 <= len) ? 16 : (len - offset);
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
  }
}

bool parseAddress(const String& token, uint16_t& outAddr) {
  if (token.startsWith("0x") || token.startsWith("0X")) {
    outAddr = static_cast<uint16_t>(strtoul(token.c_str(), nullptr, 16));
  } else {
    outAddr = static_cast<uint16_t>(strtoul(token.c_str(), nullptr, 10));
  }
  return outAddr <= MB85RC::cmd::MAX_MEM_ADDRESS;
}

bool parseByte(const String& token, uint8_t& outVal) {
  unsigned long val;
  if (token.startsWith("0x") || token.startsWith("0X")) {
    val = strtoul(token.c_str(), nullptr, 16);
  } else {
    val = strtoul(token.c_str(), nullptr, 10);
  }
  if (val > 0xFF) return false;
  outVal = static_cast<uint8_t>(val);
  return true;
}

bool parseUint16(const String& token, uint16_t& outVal) {
  unsigned long val;
  if (token.startsWith("0x") || token.startsWith("0X")) {
    val = strtoul(token.c_str(), nullptr, 16);
  } else {
    val = strtoul(token.c_str(), nullptr, 10);
  }
  if (val > 0xFFFF) return false;
  outVal = static_cast<uint16_t>(val);
  return true;
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
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(durationMs));
  if (durationMs > 0) {
    const float rate = 1000.0f * static_cast<float>(stressStats.attempts) /
                       static_cast<float>(durationMs);
    Serial.printf("  Rate: %.2f ops/s\n", rate);
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

  for (int i = 0; i < count; ++i) {
    stressStats.attempts++;
    const uint16_t addr = static_cast<uint16_t>(i % MB85RC::cmd::MEMORY_SIZE);
    const uint8_t pattern = static_cast<uint8_t>(i & 0xFF);

    MB85RC::Status st = device.writeByte(addr, pattern);
    if (!st.ok()) {
      stressStats.errors++;
      stressStats.lastError = st;
      if (verboseMode) {
        Serial.printf("  [%d] write failed: %s\n", i, errToStr(st.code));
      }
      continue;
    }

    uint8_t readBack = 0;
    st = device.readByte(addr, readBack);
    if (!st.ok()) {
      stressStats.errors++;
      stressStats.lastError = st;
      if (verboseMode) {
        Serial.printf("  [%d] read failed: %s\n", i, errToStr(st.code));
      }
      continue;
    }

    if (readBack != pattern) {
      stressStats.errors++;
      stressStats.lastError = MB85RC::Status::Error(MB85RC::Err::I2C_ERROR, "data mismatch");
      if (verboseMode) {
        Serial.printf("  [%d] mismatch: wrote 0x%02X read 0x%02X\n", i, pattern, readBack);
      }
      continue;
    }

    stressStats.success++;
  }

  const uint32_t successDelta = device.totalSuccess() - succBefore;
  const uint32_t failDelta = device.totalFailures() - failBefore;

  finishStressStats();
  Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
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
      {"recover",      0, 0},
  };
  const int opCount = static_cast<int>(sizeof(stats) / sizeof(stats[0]));

  const uint32_t succBefore = device.totalSuccess();
  const uint32_t failBefore = device.totalFailures();
  const uint32_t startMs = millis();

  for (int i = 0; i < count; ++i) {
    const int op = i % opCount;
    MB85RC::Status st = MB85RC::Status::Ok();

    switch (op) {
      case 0: {
        const uint16_t addr = static_cast<uint16_t>(i % MB85RC::cmd::MEMORY_SIZE);
        st = device.writeByte(addr, static_cast<uint8_t>(i & 0xFF));
        break;
      }
      case 1: {
        uint8_t val = 0;
        st = device.readByte(static_cast<uint16_t>(i % MB85RC::cmd::MEMORY_SIZE), val);
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
        st = device.readDeviceId(id);
        if (st.ok() && id.manufacturerId != MB85RC::cmd::MANUFACTURER_ID) {
          st = MB85RC::Status::Error(MB85RC::Err::DEVICE_ID_MISMATCH, "unexpected manufacturer");
        }
        break;
      }
      case 6: {
        st = device.recover();
        break;
      }
      default:
        break;
    }

    if (st.ok()) {
      stats[op].ok++;
    } else {
      stats[op].fail++;
      if (verboseMode) {
        Serial.printf("  [%d] %s failed: %s\n", i, stats[op].name, errToStr(st.code));
      }
    }
  }

  const uint32_t elapsed = millis() - startMs;
  uint32_t okTotal = 0;
  uint32_t failTotal = 0;
  for (int i = 0; i < opCount; ++i) {
    okTotal += stats[i].ok;
    failTotal += stats[i].fail;
  }

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
    Serial.printf("  Rate: %.2f ops/s\n", (1000.0f * static_cast<float>(count)) / elapsed);
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
  Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
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

  Serial.println("=== MB85RC selftest (safe commands) ===");

  const uint32_t succBefore = device.totalSuccess();
  const uint32_t failBefore = device.totalFailures();
  const uint8_t consBefore = device.consecutiveFailures();

  // Probe (no health tracking)
  MB85RC::Status st = device.probe();
  if (st.code == MB85RC::Err::NOT_INITIALIZED) {
    reportSkip("probe responds", "driver not initialized");
    reportSkip("remaining checks", "selftest aborted");
    Serial.printf("Selftest result: pass=%lu fail=%lu skip=%lu\n",
                  static_cast<unsigned long>(result.pass),
                  static_cast<unsigned long>(result.fail),
                  static_cast<unsigned long>(result.skip));
    return;
  }
  reportCheck("probe responds", st.ok(), st.ok() ? "" : errToStr(st.code));
  const bool probeNoTrack = device.totalSuccess() == succBefore &&
                            device.totalFailures() == failBefore &&
                            device.consecutiveFailures() == consBefore;
  reportCheck("probe no-health-side-effects", probeNoTrack, "");

  // Device ID
  MB85RC::DeviceId id;
  st = device.readDeviceId(id);
  reportCheck("readDeviceId", st.ok(), st.ok() ? "" : errToStr(st.code));
  reportCheck("manufacturer ID = 0x00A",
              st.ok() && id.manufacturerId == MB85RC::cmd::MANUFACTURER_ID, "");
  reportCheck("product ID = 0x510",
              st.ok() && id.productId == MB85RC::cmd::PRODUCT_ID, "");
  reportCheck("density code = 0x05",
              st.ok() && id.densityCode == MB85RC::cmd::DENSITY_CODE, "");

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
  device.writeByte(0x0000, origVal);

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
  device.write(0x0010, origBuf, 4);

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
  device.write(0x0020, origFill, 8);

  // Recover
  st = device.recover();
  reportCheck("recover", st.ok(), st.ok() ? "" : errToStr(st.code));
  reportCheck("isOnline", device.isOnline(), "");

  // Memory size
  reportCheck("memorySize = 32768", MB85RC::MB85RC::memorySize() == 32768, "");

  Serial.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                goodIfNonZeroColor(result.pass), static_cast<unsigned long>(result.pass), LOG_COLOR_RESET,
                goodIfZeroColor(result.fail), static_cast<unsigned long>(result.fail), LOG_COLOR_RESET,
                (result.skip > 0 ? LOG_COLOR_YELLOW : LOG_COLOR_RESET),
                static_cast<unsigned long>(result.skip), LOG_COLOR_RESET);
}

void printHelp() {
  auto helpSection = [](const char* title) {
    Serial.printf("\n%s[%s]%s\n", LOG_COLOR_GREEN, title, LOG_COLOR_RESET);
  };
  auto helpItem = [](const char* cmd, const char* desc) {
    Serial.printf("  %s%-32s%s - %s\n", LOG_COLOR_CYAN, cmd, LOG_COLOR_RESET, desc);
  };

  Serial.println();
  Serial.printf("%s=== MB85RC FRAM CLI Help ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);
  helpSection("Common");
  helpItem("help / ?", "Show this help");
  helpItem("version / ver", "Print firmware and library version info");
  helpItem("scan", "Scan I2C bus");

  helpSection("Memory Operations");
  helpItem("read <addr> [len]", "Read bytes (default len=1, hex dump)");
  helpItem("write <addr> <byte> [byte...]", "Write byte(s) to address");
  helpItem("fill <addr> <value> <len>", "Fill memory region with value");

  helpSection("Device Info");
  helpItem("id", "Read device ID (manufacturer, product, density)");
  helpItem("size", "Print memory size");

  helpSection("Diagnostics");
  helpItem("drv", "Show driver state and health");
  helpItem("probe", "Probe device (no health tracking)");
  helpItem("recover", "Manual recovery attempt");
  helpItem("verbose [0|1]", "Enable/disable verbose output");
  helpItem("stress [N]", "Run N write/read/verify cycles (default 10)");
  helpItem("stress_mix [N]", "Run N mixed-operation cycles (default 10)");
  helpItem("selftest", "Run safe self-test report");
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

  if (cmd == "id") {
    MB85RC::DeviceId id;
    MB85RC::Status st = device.readDeviceId(id);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Device ID: Manufacturer=0x%03X Product=0x%03X Density=0x%02X\n",
                  id.manufacturerId, id.productId, id.densityCode);
    return;
  }

  if (cmd == "size") {
    Serial.printf("Memory size: %u bytes (0x%04X)\n",
                  MB85RC::MB85RC::memorySize(), MB85RC::MB85RC::memorySize());
    return;
  }

  if (cmd.startsWith("read ")) {
    String args = cmd.substring(5);
    args.trim();

    const int split = args.indexOf(' ');
    String addrStr;
    uint16_t len = 1;

    if (split < 0) {
      addrStr = args;
    } else {
      addrStr = args.substring(0, split);
      String lenStr = args.substring(split + 1);
      lenStr.trim();
      if (!parseUint16(lenStr, len) || len == 0) {
        LOGW("Invalid length");
        return;
      }
    }

    uint16_t addr;
    if (!parseAddress(addrStr, addr)) {
      LOGW("Address out of range (max 0x%04X)", MB85RC::cmd::MAX_MEM_ADDRESS);
      return;
    }

    if (static_cast<uint32_t>(addr) + len > MB85RC::cmd::MEMORY_SIZE) {
      LOGW("Read would exceed memory bounds");
      return;
    }

    // Read in chunks for display
    static uint8_t readBuf[256];
    uint16_t remaining = len;
    uint16_t offset = 0;

    while (remaining > 0) {
      const uint16_t chunk = (remaining > 256) ? 256 : remaining;
      MB85RC::Status st = device.read(addr + offset, readBuf, chunk);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      printHexDump(addr + offset, readBuf, chunk);
      offset += chunk;
      remaining -= chunk;
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

    uint16_t addr;
    if (!parseAddress(addrStr, addr)) {
      LOGW("Address out of range (max 0x%04X)", MB85RC::cmd::MAX_MEM_ADDRESS);
      return;
    }

    // Parse data bytes
    uint8_t writeBuf[64];
    size_t count = 0;
    while (dataStr.length() > 0 && count < sizeof(writeBuf)) {
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

    if (static_cast<uint32_t>(addr) + count > MB85RC::cmd::MEMORY_SIZE) {
      LOGW("Write would exceed memory bounds");
      return;
    }

    MB85RC::Status st = device.write(addr, writeBuf, count);
    if (!st.ok()) {
      printStatus(st);
    } else {
      Serial.printf("Wrote %u byte(s) at 0x%04X\n", static_cast<unsigned>(count), addr);
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

    uint16_t addr;
    if (!parseAddress(addrStr, addr)) {
      LOGW("Address out of range");
      return;
    }
    uint8_t value;
    if (!parseByte(valStr, value)) {
      LOGW("Invalid fill value");
      return;
    }
    uint16_t len;
    if (!parseUint16(lenStr, len) || len == 0) {
      LOGW("Invalid length");
      return;
    }
    if (static_cast<uint32_t>(addr) + len > MB85RC::cmd::MEMORY_SIZE) {
      LOGW("Fill would exceed memory bounds");
      return;
    }

    MB85RC::Status st = device.fill(addr, value, len);
    if (!st.ok()) {
      printStatus(st);
    } else {
      Serial.printf("Filled %u byte(s) at 0x%04X with 0x%02X\n",
                    static_cast<unsigned>(len), addr, value);
    }
    return;
  }

  if (cmd == "drv") {
    printDriverHealth();
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
    const int val = cmd.substring(8).toInt();
    verboseMode = (val != 0);
    LOGI("Verbose mode: %s", verboseMode ? "ON" : "OFF");
    return;
  }

  if (cmd == "selftest") {
    runSelfTest();
    return;
  }

  if (cmd == "stress_mix") {
    LOGI("Starting stress_mix: 10 mixed-operation cycles");
    runStressMix(10);
    return;
  }

  if (cmd.startsWith("stress_mix ")) {
    int count = cmd.substring(11).toInt();
    if (count <= 0) {
      LOGW("Invalid stress_mix count");
      return;
    }
    LOGI("Starting stress_mix: %d mixed-operation cycles", count);
    runStressMix(count);
    return;
  }

  if (cmd.startsWith("stress")) {
    int count = 10;
    if (cmd.length() > 6) {
      count = cmd.substring(6).toInt();
    }
    if (count <= 0) {
      LOGW("Invalid stress count");
      return;
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
  cfg.i2cUser = &Wire;
  cfg.i2cAddress = 0x50;
  cfg.i2cTimeoutMs = board::I2C_TIMEOUT_MS;
  cfg.nowMs = exampleNowMs;
  cfg.offlineThreshold = 5;

  MB85RC::Status st = device.begin(cfg);
  if (!st.ok()) {
    LOGE("Failed to initialize device");
    printStatus(st);
    return;
  }

  LOGI("Device initialized successfully");
  printDriverHealth();
  printHelp();
  Serial.print("> ");
}

void loop() {
  device.tick(millis());

  static String inputBuffer;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        processCommand(inputBuffer);
        inputBuffer = "";
        Serial.print("> ");
      }
    } else {
      inputBuffer += c;
    }
  }
}
