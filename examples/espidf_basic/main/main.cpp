/**
 * @file main.cpp
 * @brief Native ESP-IDF diagnostic bring-up CLI for MB85RC-family FRAM devices.
 *
 * This example owns its I2C master bus and uses blocking console input. It is a
 * diagnostic bring-up tool, not a production shared-bus manager or scheduler
 * template.
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "MB85RC/MB85RC.h"

namespace {

static constexpr gpio_num_t I2C_SDA = GPIO_NUM_8;
static constexpr gpio_num_t I2C_SCL = GPIO_NUM_9;
static constexpr uint32_t I2C_FREQ_HZ = 400000U;
static constexpr uint32_t I2C_TIMEOUT_MS = 50U;
static constexpr size_t LINE_LEN = 192U;
static constexpr size_t CLI_DATA_MAX = 64U;
static constexpr uint32_t DEFAULT_STRESS_COUNT = 10U;
static constexpr uint32_t MAX_STRESS_COUNT = 1000U;
static constexpr uint32_t RW_SUITE_ADDR = 0x0010U;

struct NativeBus {
  i2c_master_bus_handle_t bus = nullptr;
  uint32_t freqHz = I2C_FREQ_HZ;
};

NativeBus gBus;
MB85RC::MB85RC gFram;
MB85RC::Config gCfg;
bool gVerbose = false;

uint32_t nowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

int timeoutArg(uint32_t timeoutMs) {
  return timeoutMs > static_cast<uint32_t>(INT_MAX) ? INT_MAX : static_cast<int>(timeoutMs);
}

MB85RC::Status mapI2c(esp_err_t err, const char* msg) {
  if (err == ESP_OK) {
    return MB85RC::Status::Ok();
  }
  if (err == ESP_ERR_TIMEOUT) {
    return MB85RC::Status::Error(MB85RC::Err::I2C_TIMEOUT, msg, err);
  }
  if (err == ESP_ERR_INVALID_ARG) {
    return MB85RC::Status::Error(MB85RC::Err::INVALID_PARAM, msg, err);
  }
  if (err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_NOT_FOUND) {
    return MB85RC::Status::Error(MB85RC::Err::I2C_NACK_ADDR, msg, err);
  }
  if (err == ESP_FAIL) {
    return MB85RC::Status::Error(MB85RC::Err::I2C_BUS, msg, err);
  }
  return MB85RC::Status::Error(MB85RC::Err::I2C_BUS, msg, err);
}

esp_err_t addDevice(NativeBus& bus, uint8_t addr, i2c_master_dev_handle_t* out) {
  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = addr;
  dev.scl_speed_hz = bus.freqHz;
  return i2c_master_bus_add_device(bus.bus, &dev, out);
}

MB85RC::Status i2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                        uint32_t timeoutMs, void* user) {
  NativeBus* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr) {
    return MB85RC::Status::Error(MB85RC::Err::INVALID_CONFIG, "I2C bus not initialized");
  }
  i2c_master_dev_handle_t dev = nullptr;
  esp_err_t err = addDevice(*bus, addr, &dev);
  if (err == ESP_OK) {
    err = i2c_master_transmit(dev, data, len, timeoutArg(timeoutMs));
  }
  if (dev != nullptr) {
    (void)i2c_master_bus_rm_device(dev);
  }
  return mapI2c(err, "I2C write failed");
}

MB85RC::Status i2cWriteRead(uint8_t addr, const uint8_t* tx, size_t txLen,
                            uint8_t* rx, size_t rxLen, uint32_t timeoutMs,
                            void* user) {
  NativeBus* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr) {
    return MB85RC::Status::Error(MB85RC::Err::INVALID_CONFIG, "I2C bus not initialized");
  }
  i2c_master_dev_handle_t dev = nullptr;
  esp_err_t err = addDevice(*bus, addr, &dev);
  if (err == ESP_OK) {
    if (txLen == 0U) {
      err = i2c_master_receive(dev, rx, rxLen, timeoutArg(timeoutMs));
    } else {
      err = i2c_master_transmit_receive(dev, tx, txLen, rx, rxLen, timeoutArg(timeoutMs));
    }
  }
  if (dev != nullptr) {
    (void)i2c_master_bus_rm_device(dev);
  }
  return mapI2c(err, "I2C write-read failed");
}

bool initBus() {
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = I2C_NUM_0;
  cfg.sda_io_num = I2C_SDA;
  cfg.scl_io_num = I2C_SCL;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&cfg, &gBus.bus) == ESP_OK;
}

void resetBusPins() {
  if (gBus.bus != nullptr) {
    (void)i2c_del_master_bus(gBus.bus);
    gBus.bus = nullptr;
  }
  gpio_set_direction(I2C_SDA, GPIO_MODE_INPUT_OUTPUT_OD);
  gpio_set_pull_mode(I2C_SDA, GPIO_PULLUP_ONLY);
  gpio_set_direction(I2C_SCL, GPIO_MODE_OUTPUT_OD);
  gpio_set_pull_mode(I2C_SCL, GPIO_PULLUP_ONLY);
  gpio_set_level(I2C_SDA, 1);
  for (int i = 0; i < 9; ++i) {
    gpio_set_level(I2C_SCL, 0);
    esp_rom_delay_us(5);
    gpio_set_level(I2C_SCL, 1);
    esp_rom_delay_us(5);
  }
  gpio_set_level(I2C_SDA, 0);
  esp_rom_delay_us(5);
  gpio_set_level(I2C_SCL, 1);
  esp_rom_delay_us(5);
  gpio_set_level(I2C_SDA, 1);
  esp_rom_delay_us(5);
  puts(initBus() ? "iface_reset: OK" : "iface_reset: FAIL");
}

void printStatus(const char* op, MB85RC::Status st) {
  printf("%s: %s (code=%u detail=%ld)\n", op, st.ok() ? "OK" : "FAIL",
         static_cast<unsigned>(st.code), static_cast<long>(st.detail));
  if (!st.ok() && st.msg != nullptr) {
    printf("  %s\n", st.msg);
  }
}

char* trim(char* text) {
  while (*text != '\0' && isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }
  char* end = text + strlen(text);
  while (end > text && isspace(static_cast<unsigned char>(end[-1]))) {
    *--end = '\0';
  }
  return text;
}

bool parseU32(const char* text, uint32_t* out, const char** tail = nullptr) {
  if (text == nullptr || *text == '\0' || out == nullptr) {
    return false;
  }
  char* end = nullptr;
  const unsigned long v = strtoul(text, &end, 0);
  if (end == text) {
    return false;
  }
  while (*end != '\0' && isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  *out = static_cast<uint32_t>(v);
  if (tail != nullptr) {
    *tail = end;
    return true;
  }
  return *end == '\0';
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      crc = ((crc & 1U) != 0U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
    }
  }
  return crc;
}

bool parseByteList(const char* text, uint8_t* out, size_t maxLen, size_t* outLen) {
  if (out == nullptr || outLen == nullptr) {
    return false;
  }
  *outLen = 0;
  const char* cursor = text;
  while (cursor != nullptr && *cursor != '\0') {
    uint32_t value = 0;
    const char* tail = nullptr;
    if (!parseU32(cursor, &value, &tail) || value > 0xFFU || *outLen >= maxLen) {
      return false;
    }
    out[(*outLen)++] = static_cast<uint8_t>(value);
    cursor = tail;
  }
  return *outLen > 0U;
}

bool rangeFits(uint32_t addr, uint32_t len) {
  const uint32_t capacity = gFram.capacityBytes();
  return len > 0U && capacity > 0U && addr < capacity && len <= (capacity - addr);
}

void printConfirmationRequired(const char* command, const char* effect, const char* confirmedForm) {
  printf("%s\n", effect);
  puts("Confirmation required because this command changes FRAM contents.");
  printf("Use exactly: %s\n", confirmedForm);
  printf("Requested command: %s\n", command);
}

void formatWriteConfirmation(uint32_t addr, const uint8_t* data, size_t dataLen,
                             char* out, size_t outLen) {
  if (out == nullptr || outLen == 0U) {
    return;
  }
  int written = snprintf(out, outLen, "write! 0x%lX", static_cast<unsigned long>(addr));
  size_t used = (written > 0) ? static_cast<size_t>(written) : 0U;
  for (size_t i = 0; i < dataLen && used < outLen; ++i) {
    written = snprintf(out + used, outLen - used, " 0x%02X", static_cast<unsigned>(data[i]));
    if (written <= 0) {
      break;
    }
    used += static_cast<size_t>(written);
  }
  if (used >= outLen) {
    out[outLen - 1U] = '\0';
  }
}

void beginDriver() {
  gCfg.i2cWrite = i2cWrite;
  gCfg.i2cWriteRead = i2cWriteRead;
  gCfg.i2cUser = &gBus;
  gCfg.nowMs = nowMs;
  gCfg.i2cTimeoutMs = I2C_TIMEOUT_MS;
  printStatus("begin", gFram.begin(gCfg));
}

void printHelp() {
  puts("Native ESP-IDF MB85RC CLI");
  puts("  help / ? | version / ver | scan | cfg / settings");
  puts("  read / dump / hexdump <addr> [len] | text <addr> [len]");
  puts("  strings [addr len [minLen]] | crc <addr> <len> | verify <addr> <byte> [byte...]");
  puts("  write <addr> <byte> [byte...] | write! <addr> <byte> [byte...]");
  puts("  fill <addr> <value> <len> | fill! <addr> <value> <len>");
  puts("  current / cur [len] | id | idraw | variants | size");
  puts("  drv | iface_reset | probe | recover | verbose [0|1]");
  puts("  stress [N] | stress! [N] | selftest | selftest! | rw_suite | rw_suite!");
  puts("  stress_mix [N] | stress_mix! [N] | randbench [N] | randbench! [N]");
  puts("  typed_demo | typed_demo!");
}

void scanBus() {
  if (gBus.bus == nullptr) {
    puts("I2C scan: bus not initialized");
    return;
  }
  puts("I2C scan:");
  for (uint8_t addr = 0x08U; addr <= 0x77U; ++addr) {
    if (i2c_master_probe(gBus.bus, addr, timeoutArg(I2C_TIMEOUT_MS)) == ESP_OK) {
      printf("  0x%02X\n", addr);
    }
  }
}

void printDrv() {
  MB85RC::SettingsSnapshot snap = gFram.getSettings();
  printf("state=%u initialized=%d online=%d addr=0x%02X capacity=%lu variant=%s ok=%lu fail=%lu consecutive=%u\n",
         static_cast<unsigned>(snap.state), snap.initialized ? 1 : 0,
         gFram.isOnline() ? 1 : 0, snap.i2cAddress,
         static_cast<unsigned long>(snap.capacityBytes), snap.variantName,
         static_cast<unsigned long>(gFram.totalSuccess()),
         static_cast<unsigned long>(gFram.totalFailures()),
         static_cast<unsigned>(gFram.consecutiveFailures()));
}

void dumpMemory(uint32_t addr, uint32_t len) {
  if (!rangeFits(addr, len)) {
    printf("Range outside active capacity: addr=0x%06lX len=%lu capacity=%lu\n",
           static_cast<unsigned long>(addr),
           static_cast<unsigned long>(len),
           static_cast<unsigned long>(gFram.capacityBytes()));
    return;
  }
  uint8_t buf[16];
  uint32_t done = 0;
  while (done < len) {
    const size_t chunk = (len - done) > sizeof(buf) ? sizeof(buf) : static_cast<size_t>(len - done);
    MB85RC::Status st = gFram.read(addr + done, buf, chunk);
    if (!st.ok()) {
      printStatus("read", st);
      return;
    }
    printf("0x%06lX:", static_cast<unsigned long>(addr + done));
    for (size_t i = 0; i < chunk; ++i) {
      printf(" %02X", buf[i]);
    }
    putchar('\n');
    done += static_cast<uint32_t>(chunk);
  }
}

void textMemory(uint32_t addr, uint32_t len) {
  if (!rangeFits(addr, len)) {
    printf("Range outside active capacity: addr=0x%06lX len=%lu capacity=%lu\n",
           static_cast<unsigned long>(addr),
           static_cast<unsigned long>(len),
           static_cast<unsigned long>(gFram.capacityBytes()));
    return;
  }
  uint8_t buf[16];
  uint32_t done = 0;
  while (done < len) {
    const size_t chunk = (len - done) > sizeof(buf) ? sizeof(buf) : static_cast<size_t>(len - done);
    MB85RC::Status st = gFram.read(addr + done, buf, chunk);
    if (!st.ok()) {
      printStatus("text", st);
      return;
    }
    for (size_t i = 0; i < chunk; ++i) {
      const uint8_t b = buf[i];
      if (b >= 0x20U && b <= 0x7EU) {
        putchar(static_cast<int>(b));
      } else {
        printf("\\x%02X", b);
      }
    }
    done += static_cast<uint32_t>(chunk);
  }
  putchar('\n');
}

void stringsMemory(uint32_t addr, uint32_t len, uint32_t minLen) {
  if (!rangeFits(addr, len)) {
    printf("Range outside active capacity: addr=0x%06lX len=%lu capacity=%lu\n",
           static_cast<unsigned long>(addr),
           static_cast<unsigned long>(len),
           static_cast<unsigned long>(gFram.capacityBytes()));
    return;
  }
  uint8_t buf[16];
  char preview[49] = {};
  uint32_t runStart = 0;
  uint32_t runLen = 0;
  size_t previewLen = 0;
  uint32_t matches = 0;
  const uint32_t end = addr + len;
  for (uint32_t cursor = addr; cursor < end;) {
    const size_t chunk =
        (end - cursor) > sizeof(buf) ? sizeof(buf) : static_cast<size_t>(end - cursor);
    MB85RC::Status st = gFram.read(cursor, buf, chunk);
    if (!st.ok()) {
      printStatus("strings", st);
      return;
    }
    for (size_t i = 0; i < chunk; ++i) {
      const uint8_t b = buf[i];
      const bool printable = (b >= 0x20U && b <= 0x7EU);
      if (printable) {
        if (runLen == 0U) {
          runStart = cursor + static_cast<uint32_t>(i);
          previewLen = 0;
          preview[0] = '\0';
        }
        if (previewLen < sizeof(preview) - 1U) {
          preview[previewLen++] = static_cast<char>(b);
          preview[previewLen] = '\0';
        }
        ++runLen;
      } else {
        if (runLen >= minLen) {
          printf("0x%06lX len=%lu \"%s%s\"\n",
                 static_cast<unsigned long>(runStart),
                 static_cast<unsigned long>(runLen),
                 preview,
                 (runLen > previewLen) ? "..." : "");
          ++matches;
        }
        runLen = 0;
      }
    }
    cursor += static_cast<uint32_t>(chunk);
  }
  if (runLen >= minLen) {
    printf("0x%06lX len=%lu \"%s%s\"\n",
           static_cast<unsigned long>(runStart),
           static_cast<unsigned long>(runLen),
           preview,
           (runLen > previewLen) ? "..." : "");
    ++matches;
  }
  printf("strings_matches=%lu\n", static_cast<unsigned long>(matches));
}

void crcMemory(uint32_t addr, uint32_t len) {
  if (!rangeFits(addr, len)) {
    printf("Range outside active capacity: addr=0x%06lX len=%lu capacity=%lu\n",
           static_cast<unsigned long>(addr),
           static_cast<unsigned long>(len),
           static_cast<unsigned long>(gFram.capacityBytes()));
    return;
  }
  uint8_t buf[32];
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t done = 0;
  while (done < len) {
    const size_t chunk = (len - done) > sizeof(buf) ? sizeof(buf) : static_cast<size_t>(len - done);
    MB85RC::Status st = gFram.read(addr + done, buf, chunk);
    if (!st.ok()) {
      printStatus("crc", st);
      return;
    }
    crc = crc32Update(crc, buf, chunk);
    done += static_cast<uint32_t>(chunk);
  }
  crc ^= 0xFFFFFFFFUL;
  printf("crc32=0x%08lX addr=0x%06lX len=%lu\n",
         static_cast<unsigned long>(crc),
         static_cast<unsigned long>(addr),
         static_cast<unsigned long>(len));
}

void verifyMemory(uint32_t addr, const uint8_t* expected, size_t len) {
  if (!rangeFits(addr, static_cast<uint32_t>(len))) {
    printf("Range outside active capacity: addr=0x%06lX len=%u capacity=%lu\n",
           static_cast<unsigned long>(addr),
           static_cast<unsigned>(len),
           static_cast<unsigned long>(gFram.capacityBytes()));
    return;
  }
  MB85RC::VerifyResult result;
  MB85RC::Status st = gFram.verify(addr, expected, len, result);
  printStatus("verify", st);
  if (st.ok()) {
    printf("verify: %s", result.match ? "MATCH" : "MISMATCH");
    if (!result.match) {
      printf(" offset=%lu expected=0x%02X actual=0x%02X",
             static_cast<unsigned long>(result.mismatchOffset),
             result.expected,
             result.actual);
    }
    putchar('\n');
  }
}

void printVariants() {
  for (size_t i = 0; i < MB85RC::cmd::VARIANT_COUNT; ++i) {
    const MB85RC::cmd::VariantInfo& v = MB85RC::cmd::KNOWN_VARIANTS[i];
    printf("%s bytes=%lu device_id=%s supported=%s\n",
           v.name,
           static_cast<unsigned long>(v.memoryBytes),
           v.hasDeviceId ? "yes" : "no",
           v.supportedByDriver ? "yes" : "no");
  }
}

void runSelfTest() {
  uint8_t original = 0;
  MB85RC::Status st = gFram.readByte(0, original);
  printStatus("selftest read original", st);
  if (!st.ok()) {
    return;
  }
  st = gFram.writeByte(0, 0xA5U);
  printStatus("selftest write", st);
  uint8_t readBack = 0;
  if (st.ok()) {
    st = gFram.readByte(0, readBack);
    printStatus("selftest readback", st);
    printf("selftest_pattern=%s\n", (st.ok() && readBack == 0xA5U) ? "PASS" : "FAIL");
  }
  printStatus("selftest restore", gFram.writeByte(0, original));
}

void runStress(uint32_t count) {
  if (count == 0U || count > MAX_STRESS_COUNT) {
    printf("stress count must be 1..%lu\n", static_cast<unsigned long>(MAX_STRESS_COUNT));
    return;
  }
  uint8_t original = 0;
  MB85RC::Status st = gFram.readByte(0, original);
  if (!st.ok()) {
    printStatus("stress backup", st);
    return;
  }
  uint32_t ok = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t pattern = static_cast<uint8_t>((i * 37U) ^ 0x5AU);
    st = gFram.writeByte(0, pattern);
    if (!st.ok()) {
      printStatus("stress write", st);
      break;
    }
    uint8_t readBack = 0;
    st = gFram.readByte(0, readBack);
    if (!st.ok() || readBack != pattern) {
      printStatus("stress read", st);
      break;
    }
    ++ok;
  }
  printStatus("stress restore", gFram.writeByte(0, original));
  printf("stress_ok=%lu/%lu\n", static_cast<unsigned long>(ok), static_cast<unsigned long>(count));
}

void runStressMix(uint32_t count) {
  if (count == 0U || count > MAX_STRESS_COUNT) {
    printf("stress_mix count must be 1..%lu\n", static_cast<unsigned long>(MAX_STRESS_COUNT));
    return;
  }
  uint8_t original[16] = {};
  uint8_t shadow[16] = {};
  MB85RC::Status st = gFram.read(RW_SUITE_ADDR, original, sizeof(original));
  if (!st.ok()) {
    printStatus("stress_mix backup", st);
    return;
  }
  memcpy(shadow, original, sizeof(shadow));
  uint32_t ok = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t index = i % sizeof(shadow);
    const uint8_t value = static_cast<uint8_t>((i * 17U) ^ 0xC3U);
    if ((i % 5U) == 0U) {
      st = gFram.fill(RW_SUITE_ADDR, value, sizeof(shadow));
      if (st.ok()) {
        memset(shadow, value, sizeof(shadow));
      }
    } else {
      st = gFram.writeByte(RW_SUITE_ADDR + index, value);
      if (st.ok()) {
        shadow[index] = value;
      }
    }
    if (!st.ok()) {
      printStatus("stress_mix write", st);
      break;
    }
    MB85RC::VerifyResult verify;
    st = gFram.verify(RW_SUITE_ADDR, shadow, sizeof(shadow), verify);
    if (!st.ok() || !verify.match) {
      printStatus("stress_mix verify", st);
      break;
    }
    ++ok;
  }
  printStatus("stress_mix restore", gFram.write(RW_SUITE_ADDR, original, sizeof(original)));
  printf("stress_mix_ok=%lu/%lu\n",
         static_cast<unsigned long>(ok),
         static_cast<unsigned long>(count));
}

void runRwSuite() {
  uint8_t original[8] = {};
  uint8_t scratch[8] = {0xDEU, 0xADU, 0xBEU, 0xEFU, 0x55U, 0xAAU, 0x11U, 0x22U};
  MB85RC::Status st = gFram.read(RW_SUITE_ADDR, original, sizeof(original));
  printStatus("rw_suite backup", st);
  if (!st.ok()) {
    return;
  }
  st = gFram.write(RW_SUITE_ADDR, scratch, sizeof(scratch));
  printStatus("rw_suite write", st);
  if (st.ok()) {
    verifyMemory(RW_SUITE_ADDR, scratch, sizeof(scratch));
    st = gFram.fill(RW_SUITE_ADDR, 0x00U, sizeof(scratch));
    printStatus("rw_suite fill", st);
  }
  printStatus("rw_suite restore", gFram.write(RW_SUITE_ADDR, original, sizeof(original)));
}

void runRandBench(uint32_t count) {
  if (count == 0U || count > MAX_STRESS_COUNT) {
    printf("randbench count must be 1..%lu in this IDF example\n",
           static_cast<unsigned long>(MAX_STRESS_COUNT));
    return;
  }
  uint8_t original[32] = {};
  uint8_t shadow[32] = {};
  MB85RC::Status st = gFram.read(RW_SUITE_ADDR, original, sizeof(original));
  if (!st.ok()) {
    printStatus("randbench backup", st);
    return;
  }
  memcpy(shadow, original, sizeof(shadow));
  uint32_t seed = 0x12345678UL;
  const int64_t startUs = esp_timer_get_time();
  uint32_t ok = 0;
  for (uint32_t i = 0; i < count; ++i) {
    seed = seed * 1664525UL + 1013904223UL;
    const uint32_t index = seed % sizeof(shadow);
    const uint8_t value = static_cast<uint8_t>(seed >> 16U);
    st = gFram.writeByte(RW_SUITE_ADDR + index, value);
    if (!st.ok()) {
      printStatus("randbench write", st);
      break;
    }
    shadow[index] = value;
    uint8_t readBack = 0;
    st = gFram.readByte(RW_SUITE_ADDR + index, readBack);
    if (!st.ok() || readBack != value) {
      printStatus("randbench read", st);
      break;
    }
    ++ok;
  }
  const int64_t elapsedUs = esp_timer_get_time() - startUs;
  MB85RC::VerifyResult verify;
  st = gFram.verify(RW_SUITE_ADDR, shadow, sizeof(shadow), verify);
  printStatus("randbench final verify", st);
  printf("randbench_ok=%lu/%lu elapsed_us=%lld final_match=%s\n",
         static_cast<unsigned long>(ok),
         static_cast<unsigned long>(count),
         static_cast<long long>(elapsedUs),
         (st.ok() && verify.match) ? "yes" : "no");
  printStatus("randbench restore", gFram.write(RW_SUITE_ADDR, original, sizeof(original)));
}

void runTypedDemo() {
  uint8_t original[16] = {};
  const uint8_t typedBytes[16] = {
      0x7EU, 0x34U, 0x12U, 0x79U, 0x29U, 0xEDU, 0xFFU, 0x01U,
      0x00U, 0x00U, 0x00U, 0x3FU, 0x01U, 0xEFU, 0xBEU, 0xADU};
  MB85RC::Status st = gFram.read(RW_SUITE_ADDR, original, sizeof(original));
  printStatus("typed_demo backup", st);
  if (!st.ok()) {
    return;
  }
  st = gFram.write(RW_SUITE_ADDR, typedBytes, sizeof(typedBytes));
  printStatus("typed_demo write fixed-width bytes", st);
  if (st.ok()) {
    verifyMemory(RW_SUITE_ADDR, typedBytes, sizeof(typedBytes));
  }
  printStatus("typed_demo restore", gFram.write(RW_SUITE_ADDR, original, sizeof(original)));
}

void handleCommand(char* line) {
  char* full = trim(line);
  if (strcmp(full, "help") == 0 || strcmp(full, "?") == 0) {
    printHelp();
  } else if (strcmp(full, "version") == 0 || strcmp(full, "ver") == 0) {
    printf("MB85RC %s %s\n", MB85RC::VERSION, MB85RC::VERSION_FULL);
  } else if (strcmp(full, "scan") == 0) {
    scanBus();
  } else if (strcmp(full, "probe") == 0) {
    printStatus("probe", gFram.probe());
  } else if (strcmp(full, "recover") == 0) {
    printStatus("recover", gFram.recover());
  } else if (strcmp(full, "iface_reset") == 0) {
    resetBusPins();
  } else if (strcmp(full, "drv") == 0 || strcmp(full, "cfg") == 0 ||
             strcmp(full, "settings") == 0) {
    printDrv();
  } else if (strcmp(full, "id") == 0) {
    MB85RC::DeviceId id;
    MB85RC::Status st = gFram.readDeviceId(id);
    printStatus("id", st);
    if (st.ok()) {
      printf("manufacturer=0x%03X product=0x%03X density=0x%X\n",
             id.manufacturerId, id.productId, id.densityCode);
    }
  } else if (strcmp(full, "idraw") == 0) {
    MB85RC::DeviceIdRaw raw;
    MB85RC::Status st = gFram.readDeviceIdRaw(raw);
    printStatus("idraw", st);
    if (st.ok()) {
      printf("%02X %02X %02X\n", raw.bytes[0], raw.bytes[1], raw.bytes[2]);
    }
  } else if (strcmp(full, "size") == 0) {
    printf("capacity=%lu max=0x%06lX variant=%s\n",
           static_cast<unsigned long>(gFram.capacityBytes()),
           static_cast<unsigned long>(gFram.maxAddress()), gFram.variantName());
  } else if (strcmp(full, "current") == 0 || strcmp(full, "cur") == 0) {
    uint8_t value = 0;
    MB85RC::Status st = gFram.readCurrentAddress(value);
    printStatus("current", st);
    if (st.ok()) {
      printf("0x%02X\n", value);
    }
  } else if (strncmp(full, "current ", 8) == 0 || strncmp(full, "cur ", 4) == 0) {
    const char* args = (strncmp(full, "cur ", 4) == 0) ? full + 4 : full + 8;
    uint32_t len = 0;
    if (!parseU32(args, &len) || len == 0U || len > CLI_DATA_MAX) {
      printf("Usage: current [len 1..%u]\n", static_cast<unsigned>(CLI_DATA_MAX));
    } else {
      uint8_t buf[CLI_DATA_MAX] = {};
      MB85RC::Status st = gFram.readCurrentAddress(buf, static_cast<size_t>(len));
      printStatus("current", st);
      if (st.ok()) {
        for (uint32_t i = 0; i < len; ++i) {
          printf("%s%02X", (i == 0U) ? "" : " ", buf[i]);
        }
        putchar('\n');
      }
    }
  } else if (strncmp(full, "read ", 5) == 0 || strncmp(full, "dump ", 5) == 0 ||
             strncmp(full, "hexdump ", 8) == 0) {
    const char* args = (full[0] == 'h') ? full + 8 : full + 5;
    uint32_t addr = 0;
    uint32_t len = 1;
    const char* tail = nullptr;
    if (parseU32(args, &addr, &tail)) {
      (void)parseU32(tail, &len);
      dumpMemory(addr, len);
    } else {
      puts("Usage: read <addr> [len]");
    }
  } else if (strncmp(full, "text ", 5) == 0) {
    uint32_t addr = 0;
    uint32_t len = 64;
    const char* tail = nullptr;
    if (parseU32(full + 5, &addr, &tail)) {
      (void)parseU32(tail, &len);
      textMemory(addr, len);
    } else {
      puts("Usage: text <addr> [len]");
    }
  } else if (strcmp(full, "strings") == 0 || strncmp(full, "strings ", 8) == 0) {
    uint32_t addr = 0;
    uint32_t len = gFram.capacityBytes();
    uint32_t minLen = 4;
    const char* tail = nullptr;
    const char* tail2 = nullptr;
    if (strncmp(full, "strings ", 8) == 0 &&
        (!parseU32(full + 8, &addr, &tail) || !parseU32(tail, &len, &tail2))) {
      puts("Usage: strings [addr len [minLen]]");
    } else {
      if (tail2 != nullptr && *tail2 != '\0') {
        (void)parseU32(tail2, &minLen);
      }
      stringsMemory(addr, len, minLen);
    }
  } else if (strncmp(full, "crc ", 4) == 0) {
    uint32_t addr = 0;
    uint32_t len = 0;
    const char* tail = nullptr;
    if (parseU32(full + 4, &addr, &tail) && parseU32(tail, &len)) {
      crcMemory(addr, len);
    } else {
      puts("Usage: crc <addr> <len>");
    }
  } else if (strncmp(full, "verify ", 7) == 0) {
    uint32_t addr = 0;
    const char* tail = nullptr;
    uint8_t expected[CLI_DATA_MAX] = {};
    size_t expectedLen = 0;
    if (parseU32(full + 7, &addr, &tail) &&
        parseByteList(tail, expected, sizeof(expected), &expectedLen)) {
      verifyMemory(addr, expected, expectedLen);
    } else {
      printf("Usage: verify <addr> <byte> [byte...] (max %u bytes)\n",
             static_cast<unsigned>(CLI_DATA_MAX));
    }
  } else if (strncmp(full, "write ", 6) == 0) {
    uint32_t addr = 0;
    const char* tail = nullptr;
    uint8_t data[CLI_DATA_MAX] = {};
    size_t dataLen = 0;
    if (parseU32(full + 6, &addr, &tail) && parseByteList(tail, data, sizeof(data), &dataLen)) {
      char confirmed[LINE_LEN];
      formatWriteConfirmation(addr, data, dataLen, confirmed, sizeof(confirmed));
      printConfirmationRequired(full, "Would write byte(s) to FRAM.", confirmed);
    } else {
      printf("Usage: write <addr> <byte> [byte...] (max %u bytes)\n",
             static_cast<unsigned>(CLI_DATA_MAX));
    }
  } else if (strncmp(full, "write! ", 7) == 0) {
    uint32_t addr = 0;
    const char* tail = nullptr;
    uint8_t data[CLI_DATA_MAX] = {};
    size_t dataLen = 0;
    if (parseU32(full + 7, &addr, &tail) && parseByteList(tail, data, sizeof(data), &dataLen)) {
      printStatus("write!", gFram.write(addr, data, dataLen));
    } else {
      printf("Usage: write! <addr> <byte> [byte...] (max %u bytes)\n",
             static_cast<unsigned>(CLI_DATA_MAX));
    }
  } else if (strncmp(full, "fill ", 5) == 0) {
    uint32_t addr = 0;
    uint32_t value = 0;
    uint32_t len = 0;
    const char* tail = nullptr;
    const char* tail2 = nullptr;
    if (parseU32(full + 5, &addr, &tail) && parseU32(tail, &value, &tail2) &&
        parseU32(tail2, &len)) {
      char confirmed[96];
      snprintf(confirmed, sizeof(confirmed), "fill! 0x%lX 0x%02lX %lu",
               static_cast<unsigned long>(addr),
               static_cast<unsigned long>(value & 0xFFU),
               static_cast<unsigned long>(len));
      printConfirmationRequired(full, "Would fill a FRAM range with one byte value.", confirmed);
    } else {
      puts("Usage: fill <addr> <value> <len>");
    }
  } else if (strncmp(full, "fill! ", 6) == 0) {
    uint32_t addr = 0;
    uint32_t value = 0;
    uint32_t len = 0;
    const char* tail = nullptr;
    const char* tail2 = nullptr;
    if (parseU32(full + 6, &addr, &tail) && parseU32(tail, &value, &tail2) &&
        parseU32(tail2, &len)) {
      printStatus("fill!", gFram.fill(addr, static_cast<uint8_t>(value), len));
    } else {
      puts("Usage: fill! <addr> <value> <len>");
    }
  } else if (strcmp(full, "verbose") == 0 || strncmp(full, "verbose ", 8) == 0) {
    gVerbose = strstr(full, " 0") == nullptr && (strstr(full, " 1") != nullptr || !gVerbose);
    printf("verbose=%d\n", gVerbose ? 1 : 0);
  } else if (strcmp(full, "variants") == 0) {
    printVariants();
  } else if (strcmp(full, "selftest") == 0) {
    printConfirmationRequired(full,
                              "Would run a write/read/restore self-test at address 0x000000.",
                              "selftest!");
  } else if (strcmp(full, "selftest!") == 0) {
    runSelfTest();
  } else if (strcmp(full, "rw_suite") == 0) {
    printConfirmationRequired(full,
                              "Would run a write/fill/verify suite in a scratch FRAM range.",
                              "rw_suite!");
  } else if (strcmp(full, "rw_suite!") == 0) {
    runRwSuite();
  } else if (strcmp(full, "stress") == 0 || strncmp(full, "stress ", 7) == 0) {
    char confirmed[32];
    if (strncmp(full, "stress ", 7) == 0) {
      snprintf(confirmed, sizeof(confirmed), "stress! %s", full + 7);
    } else {
      snprintf(confirmed, sizeof(confirmed), "stress!");
    }
    printConfirmationRequired(full,
                              "Would run repeated writes and reads at address 0x000000.",
                              confirmed);
  } else if (strcmp(full, "stress!") == 0 || strncmp(full, "stress! ", 8) == 0) {
    uint32_t count = DEFAULT_STRESS_COUNT;
    if (strncmp(full, "stress! ", 8) == 0 && !parseU32(full + 8, &count)) {
      puts("Usage: stress! [N]");
    } else {
      runStress(count);
    }
  } else if (strcmp(full, "stress_mix") == 0 || strncmp(full, "stress_mix ", 11) == 0) {
    char confirmed[40];
    if (strncmp(full, "stress_mix ", 11) == 0) {
      snprintf(confirmed, sizeof(confirmed), "stress_mix! %s", full + 11);
    } else {
      snprintf(confirmed, sizeof(confirmed), "stress_mix!");
    }
    printConfirmationRequired(full,
                              "Would run repeated mixed writes/fills/verifies in a scratch FRAM range.",
                              confirmed);
  } else if (strcmp(full, "stress_mix!") == 0 || strncmp(full, "stress_mix! ", 12) == 0) {
    uint32_t count = DEFAULT_STRESS_COUNT;
    if (strncmp(full, "stress_mix! ", 12) == 0 && !parseU32(full + 12, &count)) {
      puts("Usage: stress_mix! [N]");
    } else {
      runStressMix(count);
    }
  } else if (strcmp(full, "randbench") == 0 || strncmp(full, "randbench ", 10) == 0) {
    char confirmed[40];
    if (strncmp(full, "randbench ", 10) == 0) {
      snprintf(confirmed, sizeof(confirmed), "randbench! %s", full + 10);
    } else {
      snprintf(confirmed, sizeof(confirmed), "randbench!");
    }
    printConfirmationRequired(full,
                              "Would run random write/read timing in a scratch FRAM range.",
                              confirmed);
  } else if (strcmp(full, "randbench!") == 0 || strncmp(full, "randbench! ", 11) == 0) {
    uint32_t count = DEFAULT_STRESS_COUNT;
    if (strncmp(full, "randbench! ", 11) == 0 && !parseU32(full + 11, &count)) {
      puts("Usage: randbench! [N]");
    } else {
      runRandBench(count);
    }
  } else if (strcmp(full, "typed_demo") == 0) {
    printConfirmationRequired(full,
                              "Would write fixed-width typed demo bytes in a scratch FRAM range.",
                              "typed_demo!");
  } else if (strcmp(full, "typed_demo!") == 0) {
    runTypedDemo();
  } else {
    puts("Unknown command. Try 'help'.");
  }
}

}  // namespace

extern "C" void app_main(void) {
  setvbuf(stdin, nullptr, _IONBF, 0);
  setvbuf(stdout, nullptr, _IONBF, 0);
  puts("\nMB85RC native ESP-IDF CLI");
  puts("Diagnostic-only example: owns the I2C bus and blocks on console input.");
  puts("Production systems should serialize shared-bus access in their own bus manager.");
  if (!initBus()) {
    puts("I2C init failed");
  }
  beginDriver();
  printHelp();
  char line[LINE_LEN] = {};
  while (true) {
    printf("> ");
    // Blocking console input is acceptable here because tick() is a no-op for
    // the supported FRAM parts. Do not copy this loop as a scheduler template.
    if (fgets(line, sizeof(line), stdin) != nullptr) {
      handleCommand(line);
    }
    gFram.tick(nowMs(nullptr));
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
