#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import runpy
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

REQUIRED_COMMON = [
    "BoardConfig.h",
    "BuildConfig.h",
    "Log.h",
    "I2cTransport.h",
    "I2cScanner.h",
    "CommandHandler.h",
    "TransportAdapter.h",
    "BusDiag.h",
    "CliShell.h",
    "CliStyle.h",
    "HealthView.h",
    "HealthDiag.h",
]

MANDATORY_COMMANDS = [
    "help",
    "?",
    "version",
    "ver",
    "scan",
    "cfg",
    "settings",
    "read",
    "dump",
    "hexdump",
    "text",
    "strings",
    "crc",
    "verify",
    "write",
    "fill",
    "current",
    "cur",
    "id",
    "idraw",
    "variants",
    "size",
    "hs",
    "hs support",
    "hs enter",
    "sleep",
    "sleep support",
    "sleep enter",
    "sleep wake",
    "heap",
    "drv",
    "iface_reset",
    "probe",
    "recover",
    "verbose",
    "stress",
    "stress_mix",
    "selftest",
    "rw_suite",
    "xfer_demo",
    "randbench",
    "typed_demo",
]
DEVICE_ID_IDF_TOKENS = [
    "I2cSpecialOp::READ_DEVICE_ID",
    "transmitReceiveWithManualAddress(*bus, 0x7CU",
    "transmitReceiveWithManualAddress",
    "I2C_DEVICE_ADDRESS_NOT_USED",
    "writeAddress = static_cast<uint8_t>(addr << 1)",
    "readAddress = static_cast<uint8_t>((addr << 1) | 0x01U)",
    "I2C_NACK_VAL",
    "i2c_master_execute_defined_operations",
]
DEVICE_ID_CORE_TOKENS = [
    "I2cSpecialOp::READ_DEVICE_ID",
    "_config.i2cAddress << 1",
]

IDF_REQUIRED_COMPONENTS = [
    "MB85RC",
    "esp_driver_i2c",
    "esp_driver_gpio",
    "esp_timer",
    "freertos",
    "vfs",
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


def fail(msg: str) -> None:
    print(f"CLI contract FAILED: {msg}")
    raise SystemExit(1)


def ensure_exists(path: pathlib.Path, label: str) -> None:
    if not path.exists():
        fail(f"missing {label}: {path.as_posix()}")


def ensure_missing(path: pathlib.Path, label: str) -> None:
    if path.exists():
        fail(f"forbidden {label} still present: {path.as_posix()}")


def require_token(text: str, token: str, label: str) -> None:
    if token == "?":
        if '"?"' not in text:
            fail(f"{label} '{token}' missing")
        return
    if re.search(rf"\b{re.escape(token)}\b", text) is None:
        fail(f"{label} '{token}' missing")


def require_literal(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"{label} missing token '{token}'")


def require_dispatch(text: str, token: str) -> None:
    quoted = re.escape(f'"{token}"')
    starts_with_arg = rf'"{re.escape(token)}(?:\s|")'
    patterns = [
        rf"cmd\s*==\s*{quoted}",
        rf"cmd\.startsWith\(\s*{starts_with_arg}",
    ]
    if not any(re.search(pattern, text) for pattern in patterns):
        fail(f"mandatory command '{token}' missing from processCommand() dispatch")


def require_help(text: str, token: str) -> None:
    if token == "?":
        return
    pattern = rf"printHelpItem\s*\(\s*\"[^\"]*\b{re.escape(token)}\b"
    if re.search(pattern, text) is None:
        fail(f"mandatory command '{token}' missing from help text")


def main() -> int:
    common_dir = ROOT / "examples" / "common"
    bringup_main = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"
    idf_main = ROOT / "examples" / "espidf_basic" / "main" / "main.cpp"
    idf_cmake = ROOT / "examples" / "espidf_basic" / "main" / "CMakeLists.txt"

    ensure_exists(common_dir, "common example directory")
    ensure_exists(bringup_main, "bringup CLI example")
    ensure_exists(idf_main, "ESP-IDF bringup entry point")
    ensure_exists(idf_cmake, "ESP-IDF bringup CMake file")

    ensure_missing(ROOT / "examples" / "00_smoke_boot", "deprecated example 00_smoke_boot")
    ensure_missing(
        ROOT / "examples" / "03_feature_walkthrough",
        "deprecated example 03_feature_walkthrough",
    )

    for name in REQUIRED_COMMON:
        ensure_exists(common_dir / name, f"common helper {name}")

    text = bringup_main.read_text(encoding="utf-8", errors="replace")

    for cmd in MANDATORY_COMMANDS:
        require_token(text, cmd, "mandatory command")
        require_dispatch(text, cmd)
        require_help(text, cmd)

    idf_text = idf_main.read_text(encoding="utf-8", errors="replace")
    for token in MODE_CONTRACT_TOKENS:
        require_literal(text, token, "Arduino HS/Sleep diagnostic wording")
        require_literal(idf_text, token, "ESP-IDF HS/Sleep diagnostic wording")

    if 'extern "C" void app_main(void)' not in idf_text:
        fail("ESP-IDF entry point must define app_main()")

    cmake_text = idf_cmake.read_text(encoding="utf-8", errors="replace")
    for component in IDF_REQUIRED_COMPONENTS:
        if re.search(rf"\b{re.escape(component)}\b", cmake_text) is None:
            fail(f"ESP-IDF CMake file missing required component '{component}'")

    idf_contract = runpy.run_path(str(ROOT / "tools" / "check_idf_example_contract.py"))
    idf_contract["main"]()

    core_text = (ROOT / "src" / "MB85RC.cpp").read_text(
        encoding="utf-8", errors="replace"
    )
    for token in DEVICE_ID_CORE_TOKENS:
        require_literal(core_text, token, "core Device ID address construction")

    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
