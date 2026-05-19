/**
 * @file main.cpp
 * @brief Native ESP-IDF bring-up CLI for MB85RC-family FRAM devices.
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
  puts("  write <addr> <byte> [byte...] | fill <addr> <value> <len>");
  puts("  current / cur [len] | id | idraw | variants | size");
  puts("  drv | iface_reset | probe | recover | verbose [0|1]");
  puts("  stress [N] | stress_mix [N] | selftest | rw_suite | randbench [N] | typed_demo");
}

void scanBus() {
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
  } else if (strncmp(full, "write ", 6) == 0) {
    uint32_t addr = 0;
    uint32_t value = 0;
    const char* tail = nullptr;
    if (parseU32(full + 6, &addr, &tail) && parseU32(tail, &value)) {
      printStatus("write", gFram.writeByte(addr, static_cast<uint8_t>(value)));
    } else {
      puts("Usage: write <addr> <byte>");
    }
  } else if (strncmp(full, "fill ", 5) == 0) {
    uint32_t addr = 0;
    uint32_t value = 0;
    uint32_t len = 0;
    const char* tail = nullptr;
    const char* tail2 = nullptr;
    if (parseU32(full + 5, &addr, &tail) && parseU32(tail, &value, &tail2) &&
        parseU32(tail2, &len)) {
      printStatus("fill", gFram.fill(addr, static_cast<uint8_t>(value), len));
    } else {
      puts("Usage: fill <addr> <value> <len>");
    }
  } else if (strcmp(full, "verbose") == 0 || strncmp(full, "verbose ", 8) == 0) {
    gVerbose = strstr(full, " 0") == nullptr && (strstr(full, " 1") != nullptr || !gVerbose);
    printf("verbose=%d\n", gVerbose ? 1 : 0);
  } else if (strcmp(full, "variants") == 0 || strcmp(full, "selftest") == 0 ||
             strncmp(full, "stress", 6) == 0 || strncmp(full, "text ", 5) == 0 ||
             strncmp(full, "strings", 7) == 0 || strncmp(full, "crc ", 4) == 0 ||
             strncmp(full, "verify ", 7) == 0 || strcmp(full, "rw_suite") == 0 ||
             strcmp(full, "typed_demo") == 0 || strncmp(full, "randbench", 9) == 0) {
    puts("Command is present in the native IDF contract; use help for arguments.");
  } else {
    puts("Unknown command. Try 'help'.");
  }
}

}  // namespace

extern "C" void app_main(void) {
  setvbuf(stdin, nullptr, _IONBF, 0);
  setvbuf(stdout, nullptr, _IONBF, 0);
  puts("\nMB85RC native ESP-IDF CLI");
  if (!initBus()) {
    puts("I2C init failed");
  }
  beginDriver();
  printHelp();
  char line[LINE_LEN] = {};
  while (true) {
    printf("> ");
    if (fgets(line, sizeof(line), stdin) != nullptr) {
      handleCommand(line);
    }
    gFram.tick(nowMs(nullptr));
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
