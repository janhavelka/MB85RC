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
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
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
static constexpr uint32_t XFER_DEMO_ADDR = 0x0100U;
static constexpr size_t XFER_DEMO_LEN = 640U;

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

MB85RC::TransportResult mapI2c(
    esp_err_t err, size_t txBytes, size_t rxBytes,
    MB85RC::WriteCommit failureCommit = MB85RC::WriteCommit::NOT_APPLICABLE) {
  if (err == ESP_OK) {
    return MB85RC::TransportResult::Ok(txBytes, rxBytes);
  }
  if (err == ESP_ERR_TIMEOUT) {
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::TIMEOUT, err, failureCommit);
  }
  if (err == ESP_ERR_INVALID_ARG) {
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, err,
        MB85RC::WriteCommit::NOT_COMMITTED);
  }
  if (err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_NOT_FOUND) {
    // ESP-IDF does not report which transmitted byte was NACKed. Preserve an
    // indeterminate memory-write effect instead of inventing address-NACK proof.
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, err, failureCommit);
  }
  if (err == ESP_FAIL) {
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::BUS_ERROR, err, failureCommit);
  }
  return MB85RC::TransportResult::Error(
      MB85RC::TransportCode::IO_ERROR, err, failureCommit);
}

const char* sleepStateName(MB85RC::SleepState state) {
  switch (state) {
    case MB85RC::SleepState::AWAKE:
      return "AWAKE";
    case MB85RC::SleepState::ASLEEP:
      return "ASLEEP";
    case MB85RC::SleepState::WAKING:
      return "WAKING";
    default:
      return "UNKNOWN";
  }
}

esp_err_t addDevice(NativeBus& bus, uint8_t addr, i2c_master_dev_handle_t* out) {
  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = addr;
  dev.scl_speed_hz = bus.freqHz;
  return i2c_master_bus_add_device(bus.bus, &dev, out);
}

esp_err_t transmitReceiveWithManualAddress(NativeBus& bus, uint8_t addr,
                                           const uint8_t* tx, size_t txLen,
                                           uint8_t* rx, size_t rxLen,
                                           uint32_t timeoutMs) {
  if (tx == nullptr || txLen == 0U || rx == nullptr || rxLen == 0U) {
    return ESP_ERR_INVALID_ARG;
  }

  i2c_device_config_t devCfg = {};
  devCfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  devCfg.device_address = I2C_DEVICE_ADDRESS_NOT_USED;
  devCfg.scl_speed_hz = bus.freqHz;

  i2c_master_dev_handle_t dev = nullptr;
  esp_err_t err = i2c_master_bus_add_device(bus.bus, &devCfg, &dev);
  if (err != ESP_OK) {
    return err;
  }

  uint8_t writeAddress = static_cast<uint8_t>(addr << 1);
  uint8_t readAddress = static_cast<uint8_t>((addr << 1) | 0x01U);
  i2c_operation_job_t ops[8] = {};
  size_t op = 0U;

  ops[op++].command = I2C_MASTER_CMD_START;

  ops[op].command = I2C_MASTER_CMD_WRITE;
  ops[op].write.ack_check = true;
  ops[op].write.data = &writeAddress;
  ops[op].write.total_bytes = 1U;
  ++op;

  ops[op].command = I2C_MASTER_CMD_WRITE;
  ops[op].write.ack_check = true;
  ops[op].write.data = const_cast<uint8_t*>(tx);
  ops[op].write.total_bytes = txLen;
  ++op;

  ops[op++].command = I2C_MASTER_CMD_START;

  ops[op].command = I2C_MASTER_CMD_WRITE;
  ops[op].write.ack_check = true;
  ops[op].write.data = &readAddress;
  ops[op].write.total_bytes = 1U;
  ++op;

  if (rxLen > 1U) {
    ops[op].command = I2C_MASTER_CMD_READ;
    ops[op].read.ack_value = I2C_ACK_VAL;
    ops[op].read.data = rx;
    ops[op].read.total_bytes = rxLen - 1U;
    ++op;
  }

  ops[op].command = I2C_MASTER_CMD_READ;
  ops[op].read.ack_value = I2C_NACK_VAL;
  ops[op].read.data = rx + (rxLen - 1U);
  ops[op].read.total_bytes = 1U;
  ++op;

  ops[op++].command = I2C_MASTER_CMD_STOP;

  err = i2c_master_execute_defined_operations(dev, ops, op, timeoutArg(timeoutMs));
  (void)i2c_master_bus_rm_device(dev);
  return err;
}

esp_err_t executeRawOperations(NativeBus& bus, i2c_operation_job_t* ops, size_t opCount,
                               uint32_t timeoutMs) {
  i2c_device_config_t devCfg = {};
  devCfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  devCfg.device_address = I2C_DEVICE_ADDRESS_NOT_USED;
  devCfg.scl_speed_hz = bus.freqHz;

  i2c_master_dev_handle_t dev = nullptr;
  esp_err_t err = i2c_master_bus_add_device(bus.bus, &devCfg, &dev);
  if (err == ESP_OK) {
    err = i2c_master_execute_defined_operations(dev, ops, opCount, timeoutArg(timeoutMs));
  }
  if (dev != nullptr) {
    (void)i2c_master_bus_rm_device(dev);
  }
  return err;
}

esp_err_t highSpeedWrite(NativeBus& bus, const MB85RC::I2cSpecialTransfer& transfer,
                         uint32_t timeoutMs) {
  if (transfer.txData == nullptr || transfer.txLen == 0U) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t hsMasterCode = transfer.hsMasterCode;
  uint8_t writeAddress = static_cast<uint8_t>(transfer.i2cAddress << 1);
  i2c_operation_job_t ops[6] = {};
  size_t op = 0U;

  ops[op++].command = I2C_MASTER_CMD_START;
  ops[op].command = I2C_MASTER_CMD_WRITE;
  ops[op].write.ack_check = false;
  ops[op].write.data = &hsMasterCode;
  ops[op].write.total_bytes = 1U;
  ++op;
  ops[op++].command = I2C_MASTER_CMD_START;
  ops[op].command = I2C_MASTER_CMD_WRITE;
  ops[op].write.ack_check = true;
  ops[op].write.data = &writeAddress;
  ops[op].write.total_bytes = 1U;
  ++op;
  ops[op].command = I2C_MASTER_CMD_WRITE;
  ops[op].write.ack_check = true;
  ops[op].write.data = const_cast<uint8_t*>(transfer.txData);
  ops[op].write.total_bytes = transfer.txLen;
  ++op;
  ops[op++].command = I2C_MASTER_CMD_STOP;

  return executeRawOperations(bus, ops, op, timeoutMs);
}

esp_err_t highSpeedWriteRead(NativeBus& bus, const MB85RC::I2cSpecialTransfer& transfer,
                             uint32_t timeoutMs) {
  if ((transfer.txLen > 0U && transfer.txData == nullptr) ||
      (transfer.rxLen > 0U && transfer.rxData == nullptr) ||
      transfer.rxLen == 0U) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t hsMasterCode = transfer.hsMasterCode;
  uint8_t writeAddress = static_cast<uint8_t>(transfer.i2cAddress << 1);
  uint8_t readAddress = static_cast<uint8_t>((transfer.i2cAddress << 1) | 0x01U);
  i2c_operation_job_t ops[10] = {};
  size_t op = 0U;

  ops[op++].command = I2C_MASTER_CMD_START;
  ops[op].command = I2C_MASTER_CMD_WRITE;
  ops[op].write.ack_check = false;
  ops[op].write.data = &hsMasterCode;
  ops[op].write.total_bytes = 1U;
  ++op;
  ops[op++].command = I2C_MASTER_CMD_START;

  if (transfer.txLen > 0U) {
    ops[op].command = I2C_MASTER_CMD_WRITE;
    ops[op].write.ack_check = true;
    ops[op].write.data = &writeAddress;
    ops[op].write.total_bytes = 1U;
    ++op;
    ops[op].command = I2C_MASTER_CMD_WRITE;
    ops[op].write.ack_check = true;
    ops[op].write.data = const_cast<uint8_t*>(transfer.txData);
    ops[op].write.total_bytes = transfer.txLen;
    ++op;
    ops[op++].command = I2C_MASTER_CMD_START;
  }

  ops[op].command = I2C_MASTER_CMD_WRITE;
  ops[op].write.ack_check = true;
  ops[op].write.data = &readAddress;
  ops[op].write.total_bytes = 1U;
  ++op;

  if (transfer.rxLen > 1U) {
    ops[op].command = I2C_MASTER_CMD_READ;
    ops[op].read.ack_value = I2C_ACK_VAL;
    ops[op].read.data = transfer.rxData;
    ops[op].read.total_bytes = transfer.rxLen - 1U;
    ++op;
  }

  ops[op].command = I2C_MASTER_CMD_READ;
  ops[op].read.ack_value = I2C_NACK_VAL;
  ops[op].read.data = transfer.rxData + (transfer.rxLen - 1U);
  ops[op].read.total_bytes = 1U;
  ++op;
  ops[op++].command = I2C_MASTER_CMD_STOP;

  return executeRawOperations(bus, ops, op, timeoutMs);
}

esp_err_t enterSleepRaw(NativeBus& bus, const MB85RC::I2cSpecialTransfer& transfer,
                        uint32_t timeoutMs) {
  uint8_t reserved = MB85RC::cmd::SLEEP_RESERVED_ADDR_W;
  uint8_t deviceWord = static_cast<uint8_t>(transfer.i2cAddress << 1);
  uint8_t sleepCommand = MB85RC::cmd::SLEEP_ENTRY_COMMAND;
  i2c_operation_job_t ops[6] = {};
  size_t op = 0U;

  ops[op++].command = I2C_MASTER_CMD_START;
  ops[op].command = I2C_MASTER_CMD_WRITE;
  ops[op].write.ack_check = true;
  ops[op].write.data = &reserved;
  ops[op].write.total_bytes = 1U;
  ++op;
  ops[op].command = I2C_MASTER_CMD_WRITE;
  ops[op].write.ack_check = true;
  ops[op].write.data = &deviceWord;
  ops[op].write.total_bytes = 1U;
  ++op;
  ops[op++].command = I2C_MASTER_CMD_START;
  ops[op].command = I2C_MASTER_CMD_WRITE;
  ops[op].write.ack_check = true;
  ops[op].write.data = &sleepCommand;
  ops[op].write.total_bytes = 1U;
  ++op;
  ops[op++].command = I2C_MASTER_CMD_STOP;

  return executeRawOperations(bus, ops, op, timeoutMs);
}

esp_err_t wakeRaw(NativeBus& bus, const MB85RC::I2cSpecialTransfer& transfer,
                  uint32_t timeoutMs) {
  uint8_t deviceWord = static_cast<uint8_t>(transfer.i2cAddress << 1);
  i2c_operation_job_t ops[3] = {};
  size_t op = 0U;

  ops[op++].command = I2C_MASTER_CMD_START;
  ops[op].command = I2C_MASTER_CMD_WRITE;
  ops[op].write.ack_check = false;
  ops[op].write.data = &deviceWord;
  ops[op].write.total_bytes = 1U;
  ++op;
  ops[op++].command = I2C_MASTER_CMD_STOP;

  return executeRawOperations(bus, ops, op, timeoutMs);
}

MB85RC::TransportResult i2cWrite(uint8_t addr, const uint8_t* data,
                                 size_t len, uint32_t timeoutMs, void* user) {
  NativeBus* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr) {
    return MB85RC::TransportResult::Error(
        MB85RC::TransportCode::IO_ERROR, ESP_ERR_INVALID_STATE,
        MB85RC::WriteCommit::NOT_COMMITTED);
  }
  i2c_master_dev_handle_t dev = nullptr;
  esp_err_t err = addDevice(*bus, addr, &dev);
  MB85RC::WriteCommit failureCommit = MB85RC::WriteCommit::NOT_COMMITTED;
  if (err == ESP_OK) {
    failureCommit = MB85RC::WriteCommit::INDETERMINATE;
    err = i2c_master_transmit(dev, data, len, timeoutArg(timeoutMs));
  }
  if (dev != nullptr) {
    (void)i2c_master_bus_rm_device(dev);
  }
  return mapI2c(err, len, 0U, failureCommit);
}

MB85RC::TransportResult i2cWriteRead(uint8_t addr, const uint8_t* tx,
                                     size_t txLen, uint8_t* rx, size_t rxLen,
                                     uint32_t timeoutMs, void* user) {
  NativeBus* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr) {
    return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR,
                                           ESP_ERR_INVALID_STATE);
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
  return mapI2c(err, txLen, rxLen);
}

MB85RC::TransportResult i2cSpecial(
    MB85RC::I2cSpecialOp op, const MB85RC::I2cSpecialTransfer& transfer,
    uint32_t timeoutMs, void* user) {
  NativeBus* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr) {
    return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR,
                                           ESP_ERR_INVALID_STATE);
  }

  esp_err_t err = ESP_ERR_INVALID_ARG;
  size_t completedTxBytes = transfer.txLen;
  size_t completedRxBytes = transfer.rxLen;
  MB85RC::WriteCommit failureCommit = MB85RC::WriteCommit::NOT_APPLICABLE;
  switch (op) {
    case MB85RC::I2cSpecialOp::READ_DEVICE_ID: {
      if (transfer.txData == nullptr || transfer.txLen != 1U ||
          transfer.rxData == nullptr ||
          transfer.rxLen != MB85RC::cmd::DEVICE_ID_LEN) {
        break;
      }
      err = transmitReceiveWithManualAddress(*bus, 0x7CU,
                                             transfer.txData, transfer.txLen,
                                             transfer.rxData, transfer.rxLen,
                                             timeoutMs);
      break;
    }
    case MB85RC::I2cSpecialOp::HIGH_SPEED_WRITE:
      failureCommit = MB85RC::WriteCommit::INDETERMINATE;
      err = highSpeedWrite(*bus, transfer, timeoutMs);
      break;
    case MB85RC::I2cSpecialOp::HIGH_SPEED_WRITE_READ:
      err = highSpeedWriteRead(*bus, transfer, timeoutMs);
      break;
    case MB85RC::I2cSpecialOp::ENTER_SLEEP:
      err = enterSleepRaw(*bus, transfer, timeoutMs);
      break;
    case MB85RC::I2cSpecialOp::WAKE_FROM_SLEEP:
      err = wakeRaw(*bus, transfer, timeoutMs);
      break;
    default:
      return MB85RC::TransportResult::Error(MB85RC::TransportCode::IO_ERROR,
                                             ESP_ERR_INVALID_ARG);
  }
  return mapI2c(err, completedTxBytes, completedRxBytes, failureCommit);
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
  gCfg.i2cSpecial = i2cSpecial;
  gCfg.i2cUser = &gBus;
  gCfg.nowMs = nowMs;
  gCfg.i2cTimeoutMs = I2C_TIMEOUT_MS;
  gCfg.expectedVariant = MB85RC::DeviceVariant::AUTO;
  const MB85RC::Status bound = gFram.bind(gCfg);
  printStatus("bind", bound);
  if (!bound.ok()) {
    return;
  }
  MB85RC::DeviceId identity;
  printStatus("identity", gFram.readDeviceId(identity));
}

void printHelp() {
  puts("Native ESP-IDF MB85RC CLI");
  puts("  help / ? | version / ver | scan | cfg / settings");
  puts("  read / dump / hexdump <addr> [len] | text <addr> [len]");
  puts("  strings [addr len [minLen]] | crc <addr> <len> | verify <addr> <byte> [byte...]");
  puts("  write <addr> <byte> [byte...] | write! <addr> <byte> [byte...]");
  puts("  fill <addr> <value> <len> | fill! <addr> <value> <len>");
  puts("  current / cur [len] | id | idraw | variants | size");
  puts("  hs | hs support | hs enter");
  puts("  sleep | sleep support | sleep enter | sleep wake");
  puts("  drv | heap | iface_reset | probe | recover | verbose [0|1]");
  puts("  stress [N] | stress! [N] | selftest | selftest! | rw_suite | rw_suite!");
  puts("  xfer_demo | xfer_demo!");
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
  printf("hs_support=%s hs_enabled=%s normal_hz=%lu hs_hz=%lu sleep_support=%s sleep_state=%s tREC_us=%u wake_ready_ms=%lu\n",
         snap.highSpeedModeSupported ? "yes" : "no",
         snap.highSpeedModeEnabled ? "yes" : "no",
         static_cast<unsigned long>(snap.maxNormalBusHz),
         static_cast<unsigned long>(snap.maxHighSpeedBusHz),
         snap.sleepModeSupported ? "yes" : "no",
         sleepStateName(snap.sleepState),
         static_cast<unsigned>(snap.sleepRecoveryUs),
         static_cast<unsigned long>(snap.sleepWakeReadyMs));
}

void printHeapTelemetry() {
  printf("heap: free=%lu min_free=%lu largest=%lu\n",
         static_cast<unsigned long>(esp_get_free_heap_size()),
         static_cast<unsigned long>(esp_get_minimum_free_heap_size()),
         static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)));
}

void printHighSpeedSupport() {
  MB85RC::SettingsSnapshot snap = gFram.getSettings();
  puts("High-speed mode:");
  printf("  Active variant: %s\n", snap.variantName);
  printf("  Support: %s\n", snap.highSpeedModeSupported ? "yes" : "no");
  printf("  Enabled: %s\n", snap.highSpeedModeEnabled ? "yes" : "no");
  printf("  Default HS master code: 0x%02X\n", MB85RC::cmd::HIGH_SPEED_MASTER_CODE_DEFAULT);
  puts("  Core bus clock: unchanged; MB85RC core does not change Wire/ESP-IDF I2C clock");
  printf("  Diagnostic bus clock: %lu Hz; this example emits the HS prefix but does not prove 3.4 MHz operation\n",
         static_cast<unsigned long>(gBus.freqHz));
  puts("  App action: application bus manager must configure/operate the bus at 3.4 MHz after HS entry");
  puts("  Note: STOP exits high-speed mode; enabled driver transfers send the HS prefix per transaction");
  puts("  Hardware validation: not claimed by this diagnostic");
}

void handleHighSpeedCommand(const char* full) {
  printHighSpeedSupport();
  if (strcmp(full, "hs") == 0 || strcmp(full, "hs support") == 0) {
    return;
  }
  if (strcmp(full, "hs enter") == 0) {
    MB85RC::Status st = gFram.enterHighSpeedMode();
    printStatus("hs enter", st);
    if (st.ok()) {
      puts("High-speed transfer mode requested; entry prefix is sent with each memory transfer.");
    }
    return;
  }
  puts("Usage: hs | hs support | hs enter");
}

void printSleepSupport() {
  MB85RC::SettingsSnapshot snap = gFram.getSettings();
  puts("Sleep mode:");
  printf("  Active variant: %s\n", snap.variantName);
  printf("  Support: %s\n", snap.sleepModeSupported ? "yes" : "no");
  printf("  State: %s\n", sleepStateName(snap.sleepState));
  puts("  Entry: F8h + active device address word + repeated-start 86h");
  puts("  Wake: clock active device address word, wait tREC >= 400 us before access/recover");
  puts("  Core sleep state: tracked separately from driver health; no hidden delay is inserted");
  puts("  Hardware validation: not claimed by this diagnostic");
}

void handleSleepCommand(const char* full) {
  printSleepSupport();
  if (strcmp(full, "sleep") == 0 || strcmp(full, "sleep support") == 0) {
    return;
  }
  if (strcmp(full, "sleep enter") == 0) {
    printStatus("sleep enter", gFram.enterSleep());
    return;
  }
  if (strcmp(full, "sleep wake") == 0) {
    MB85RC::Status st = gFram.wake();
    printStatus("sleep wake", st);
    if (st.ok()) {
      vTaskDelay(pdMS_TO_TICKS(MB85RC::cmd::SLEEP_RECOVERY_MS));
      gFram.tick(nowMs(nullptr));
      printStatus("recover after sleep wake", gFram.recover());
    }
    return;
  }
  puts("Usage: sleep | sleep support | sleep enter | sleep wake");
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
    printf("%s bytes=%lu device_id=%s supported=%s hs=%s sleep=%s normal_hz=%lu hs_hz=%lu tREC_us=%u\n",
           v.name,
           static_cast<unsigned long>(v.memoryBytes),
           v.hasDeviceId ? "yes" : "no",
           v.supportedByDriver ? "yes" : "no",
           v.supportsHighSpeedMode ? "yes" : "no",
           v.supportsSleepMode ? "yes" : "no",
           static_cast<unsigned long>(v.maxNormalBusHz),
           static_cast<unsigned long>(v.maxHighSpeedBusHz),
           static_cast<unsigned>(v.sleepRecoveryUs));
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

MB85RC::Status takeStagedTerminal(MB85RC::Status terminal) {
  MB85RC::TransferResult result;
  const MB85RC::Status taken = gFram.takeTransferResult(result);
  return taken.ok() ? result.status : terminal;
}

MB85RC::Status pollStagedTransferToCompletion(size_t len, size_t chunkSize) {
  if (len == 0U || chunkSize == 0U) {
    return MB85RC::Status::Error(MB85RC::Err::INVALID_PARAM, "Invalid staged transfer bounds");
  }
  const uint32_t expectedChunks =
      static_cast<uint32_t>((len + chunkSize - 1U) / chunkSize);
  const uint32_t pollLimit = expectedChunks + 3U;
  for (uint32_t i = 0; i < pollLimit; ++i) {
    MB85RC::Status st = gFram.pollTransfer(nowMs(nullptr), 1);
    if (st.inProgress()) {
      continue;
    }
    return takeStagedTerminal(st);
  }
  (void)gFram.cancelTransfer();
  MB85RC::TransferResult cancelled;
  (void)gFram.takeTransferResult(cancelled);
  return MB85RC::Status::Error(MB85RC::Err::TIMEOUT, "Staged transfer poll limit exhausted");
}

void printXferCheck(const char* name, bool ok, const char* note = "") {
  printf("xfer_demo %s: %s", name, ok ? "PASS" : "FAIL");
  if (note != nullptr && note[0] != '\0') {
    printf(" (%s)", note);
  }
  putchar('\n');
}

void runXferDemo() {
  uint32_t pass = 0;
  uint32_t fail = 0;
  auto check = [&](const char* name, bool ok, const char* note = "") {
    printXferCheck(name, ok, note);
    if (ok) {
      ++pass;
    } else {
      ++fail;
    }
  };
  auto checkStatus = [&](const char* name, MB85RC::Status st) {
    check(name, st.ok(), st.ok() ? "" : st.msg);
    if (!st.ok()) {
      printStatus(name, st);
    }
  };

  const uint32_t addr =
      rangeFits(XFER_DEMO_ADDR, static_cast<uint32_t>(XFER_DEMO_LEN)) ? XFER_DEMO_ADDR : 0U;
  const size_t len = rangeFits(addr, static_cast<uint32_t>(XFER_DEMO_LEN))
                         ? XFER_DEMO_LEN
                         : static_cast<size_t>(gFram.capacityBytes());
  if (len == 0U || !rangeFits(addr, static_cast<uint32_t>(len))) {
    check("select scratch range", false, "active capacity is zero");
    printf("xfer_demo_result pass=%lu fail=%lu\n",
           static_cast<unsigned long>(pass),
           static_cast<unsigned long>(fail));
    return;
  }

  static uint8_t original[XFER_DEMO_LEN] = {};
  static uint8_t readBack[XFER_DEMO_LEN] = {};
  static uint8_t pattern[XFER_DEMO_LEN] = {};
  static uint8_t fillExpected[XFER_DEMO_LEN] = {};
  for (size_t i = 0; i < len; ++i) {
    pattern[i] = static_cast<uint8_t>(0x40U + ((i * 19U) & 0x7FU));
    fillExpected[i] = 0x5AU;
  }

  MB85RC::Status st = gFram.read(addr, original, len);
  checkStatus("backup", st);
  if (!st.ok()) {
    printf("xfer_demo_result pass=%lu fail=%lu\n",
           static_cast<unsigned long>(pass),
           static_cast<unsigned long>(fail));
    return;
  }

  st = gFram.requestRead(addr, readBack, len);
  checkStatus("requestRead", st);
  if (st.ok()) {
    MB85RC::Status zeroBudget = gFram.pollTransfer(nowMs(nullptr), 0);
    check("zeroBudgetInProgress", zeroBudget.inProgress(), zeroBudget.msg);
    uint8_t tmp = 0;
    MB85RC::Status busy = gFram.readByte(addr, tmp);
    check("busyDuringTransfer", busy.code == MB85RC::Err::BUSY, busy.msg);
    MB85RC::Status budgetTwo = gFram.pollTransfer(nowMs(nullptr), 2);
    check("poll budget 2 executes two chunks", budgetTwo.inProgress(),
          budgetTwo.inProgress() ? "" : budgetTwo.msg);
    st = pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_READ_CHUNK);
    checkStatus("pollRead", st);
    check("readMatchesBackup", st.ok() && memcmp(readBack, original, len) == 0);
  }

  st = gFram.requestWrite(addr, pattern, len);
  checkStatus("requestWrite", st);
  if (st.ok()) {
    st = pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_WRITE_CHUNK);
    checkStatus("pollWrite", st);
  }

  st = gFram.requestVerify(addr, pattern, len);
  checkStatus("requestVerifyWrite", st);
  if (st.ok()) {
    st = pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_READ_CHUNK);
    checkStatus("pollVerifyWrite", st);
  }

  st = gFram.requestFill(addr, 0x5AU, len);
  checkStatus("requestFill", st);
  if (st.ok()) {
    const bool highBudgetShouldRemainActive =
        len > (static_cast<size_t>(MB85RC::cmd::MAX_TRANSFER_INSTRUCTIONS_PER_POLL) *
               static_cast<size_t>(MB85RC::cmd::MAX_FILL_CHUNK));
    MB85RC::Status highBudget = gFram.pollTransfer(nowMs(nullptr), 255);
    check("poll high budget clamps to 8 chunks",
          highBudgetShouldRemainActive ? highBudget.inProgress() : highBudget.ok(),
          highBudget.msg);
    st = highBudget.inProgress()
             ? pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_FILL_CHUNK)
             : takeStagedTerminal(highBudget);
    checkStatus("pollFill", st);
  }

  st = gFram.requestVerify(addr, fillExpected, len);
  checkStatus("requestVerifyFill", st);
  if (st.ok()) {
    st = pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_READ_CHUNK);
    checkStatus("pollVerifyFill", st);
  }

  st = gFram.requestWrite(addr, original, len);
  checkStatus("requestRestore", st);
  if (st.ok()) {
    st = pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_WRITE_CHUNK);
    checkStatus("pollRestore", st);
  }

  st = gFram.requestVerify(addr, original, len);
  checkStatus("requestVerifyRestore", st);
  if (st.ok()) {
    st = pollStagedTransferToCompletion(len, MB85RC::cmd::MAX_READ_CHUNK);
    checkStatus("pollVerifyRestore", st);
  }

  printf("xfer_demo_result pass=%lu fail=%lu\n",
         static_cast<unsigned long>(pass),
         static_cast<unsigned long>(fail));
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
  } else if (strcmp(full, "hs") == 0 || strcmp(full, "hs support") == 0 ||
             strcmp(full, "hs enter") == 0) {
    handleHighSpeedCommand(full);
  } else if (strcmp(full, "sleep") == 0 || strcmp(full, "sleep support") == 0 ||
             strcmp(full, "sleep enter") == 0 || strcmp(full, "sleep wake") == 0) {
    handleSleepCommand(full);
  } else if (strcmp(full, "probe") == 0) {
    printStatus("probe", gFram.probe());
  } else if (strcmp(full, "recover") == 0) {
    printStatus("recover", gFram.recover());
  } else if (strcmp(full, "iface_reset") == 0) {
    resetBusPins();
  } else if (strcmp(full, "drv") == 0 || strcmp(full, "cfg") == 0 ||
             strcmp(full, "settings") == 0) {
    printDrv();
  } else if (strcmp(full, "heap") == 0) {
    printHeapTelemetry();
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
  } else if (strcmp(full, "xfer_demo") == 0) {
    printConfirmationRequired(full,
                              "Would run poll-chunked read/write/fill/verify operations in a scratch FRAM range.",
                              "xfer_demo!");
  } else if (strcmp(full, "xfer_demo!") == 0) {
    runXferDemo();
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
    // Blocking console input is acceptable here because tick() performs no
    // async I2C or write-delay work. It only advances Sleep wake state from
    // caller-supplied time. Do not copy this loop as a scheduler template.
    if (fgets(line, sizeof(line), stdin) != nullptr) {
      handleCommand(line);
    }
    gFram.tick(nowMs(nullptr));
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
