"""Shared CLI contract data used by the Arduino and native ESP-IDF guards."""

MANDATORY_COMMANDS = [
    "help", "?", "version", "ver", "scan", "cfg", "settings", "read",
    "dump", "hexdump", "text", "strings", "crc", "verify", "write",
    "fill", "current", "cur", "id", "idraw", "variants", "size", "hs",
    "hs support", "hs enter", "hs exit", "sleep", "sleep support",
    "sleep enter", "sleep wake", "heap", "drv", "iface_reset", "probe",
    "recover", "verbose", "stress", "stress_mix", "selftest", "rw_suite",
    "xfer_demo", "randbench", "typed_demo",
]

DEVICE_ID_IDF_TOKENS = [
    "I2cSpecialOp::READ_DEVICE_ID",
    "transmitReceiveWithManualAddress(*bus, 0x7CU",
    "I2C_DEVICE_ADDRESS_NOT_USED",
    "writeAddress = static_cast<uint8_t>(addr << 1)",
    "readAddress = static_cast<uint8_t>((addr << 1) | 0x01U)",
    "I2C_NACK_VAL",
    "i2c_master_execute_defined_operations",
]

IDF_REQUIRED_COMPONENTS = [
    "MB85RC", "esp_driver_i2c", "esp_driver_gpio", "esp_timer", "freertos", "vfs",
]

MODE_CONTRACT_TOKENS = [
    "Active variant:",
    "Support:",
    "Core bus clock: unchanged",
    "MB85RC core does not change Wire/ESP-IDF I2C clock",
    "application bus manager must configure/operate the bus at 3.4 MHz",
    "STOP exits high-speed mode",
    "F8h + active device address word + repeated-start 86h",
    "tREC >= 400 us",
    "Hardware validation: not claimed by this diagnostic",
]
