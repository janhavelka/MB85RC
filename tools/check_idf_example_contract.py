#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

from _contract_data import (
    DEVICE_ID_IDF_TOKENS,
    IDF_REQUIRED_COMPONENTS,
    MANDATORY_COMMANDS,
    MODE_CONTRACT_TOKENS,
)

ROOT = pathlib.Path(__file__).resolve().parents[1]

FORBIDDEN_TOKENS = [
    "ArduinoCompat",
    "IdfArduinoCompat",
    "ARDUINO",
    "arduino",
    "Arduino.h",
    "Wire.h",
    "String",
    "Serial",
    "TwoWire",
    "examples/01_basic_bringup_cli/main.cpp",
    "Command is present in the native IDF contract",
]

REQUIRED_NATIVE_TOKENS = [
    'extern "C" void app_main(void)',
    "driver/i2c_master.h",
    "esp_get_free_heap_size",
    "esp_get_minimum_free_heap_size",
    "heap_caps_get_largest_free_block",
    "MALLOC_CAP_DEFAULT",
    "esp_timer_get_time",
    "vTaskDelay",
    "fgets",
    "i2c_new_master_bus",
]

REQUIRED_CI_TOKENS = [
    "python tools/check_idf_example_contract.py",
    "idf.py --version",
    "idf.py -C examples/espidf_basic set-target",
    "esp32s3",
    "esp32s2",
]

REQUIRED_IDF_DOC_TOKENS = [
    "idf.py -C examples/espidf_basic set-target esp32s3 build",
    "idf.py -C examples/espidf_basic set-target esp32s2 build",
    "diagnostic-only",
    "fixed C buffers",
    "Config::i2cSpecial",
    "esp_get_idf_version()",
]

REQUIRED_CONFIRMATION_TOKENS = [
    "write!",
    "fill!",
    "stress!",
    "stress_mix!",
    "rw_suite!",
    "xfer_demo!",
    "randbench!",
    "typed_demo!",
    "Confirmation required because this command changes FRAM contents.",
]

REQUIRED_MODE_TOKENS = [
    "I2cSpecialOp::HIGH_SPEED_WRITE",
    "I2cSpecialOp::HIGH_SPEED_WRITE_READ",
    "I2cSpecialOp::ENTER_SLEEP",
    "I2cSpecialOp::WAKE_FROM_SLEEP",
    "HIGH_SPEED_MASTER_CODE_DEFAULT",
    "SLEEP_ENTRY_COMMAND",
    "SLEEP_RECOVERY_MS",
    "Hardware validation: not claimed by this diagnostic",
]

def fail(msg: str) -> None:
    print(f"IDF example contract FAILED: {msg}")
    raise SystemExit(1)


def example_contract_files(root: pathlib.Path) -> list[pathlib.Path]:
    example_root = root / "examples" / "espidf_basic"
    skipped_dirs = {"build", "managed_components"}
    files: list[pathlib.Path] = []
    for path in example_root.rglob("*"):
        if not path.is_file():
            continue
        rel_parts = path.relative_to(example_root).parts
        if any(part in skipped_dirs for part in rel_parts):
            continue
        if path.name == "CMakeLists.txt" or path.suffix in {".c", ".cc", ".cpp", ".h", ".hpp"}:
            files.append(path)
    return files


def main() -> int:
    main_path = ROOT / "examples" / "espidf_basic" / "main" / "main.cpp"
    cmake_path = ROOT / "examples" / "espidf_basic" / "main" / "CMakeLists.txt"
    workflow_path = ROOT / ".github" / "workflows" / "ci.yml"
    readme_path = ROOT / "README.md"
    idf_doc_path = ROOT / "docs" / "IDF_PORT.md"
    text = main_path.read_text(encoding="utf-8", errors="replace")
    cmake = cmake_path.read_text(encoding="utf-8", errors="replace")
    workflow = workflow_path.read_text(encoding="utf-8", errors="replace")
    readme = readme_path.read_text(encoding="utf-8", errors="replace")
    idf_doc = idf_doc_path.read_text(encoding="utf-8", errors="replace")

    for path in example_contract_files(ROOT):
        rel = path.relative_to(ROOT)
        body = path.read_text(encoding="utf-8", errors="replace")
        for token in FORBIDDEN_TOKENS:
            if token in body:
                fail(f"forbidden Arduino compatibility token in {rel}: {token}")
    for token in REQUIRED_NATIVE_TOKENS:
        if token not in text:
            fail(f"native ESP-IDF token missing: {token}")
    tx_only = re.search(
        r"else\s+if\s*\(\s*rxLen\s*==\s*0U\s*\)\s*\{\s*"
        r"err\s*=\s*i2c_master_transmit\s*\(\s*dev\s*,\s*tx\s*,\s*txLen\s*,",
        text,
    )
    if tx_only is None:
        fail("native ESP-IDF write-read callback lacks a TX-only transaction path")
    for token in REQUIRED_CONFIRMATION_TOKENS:
        if token not in text:
            fail(f"confirmation/handler token missing: {token}")
    for token in REQUIRED_MODE_TOKENS:
        if token not in text:
            fail(f"HS/Sleep native token missing: {token}")
    for token in MODE_CONTRACT_TOKENS:
        if token not in text:
            fail(f"ESP-IDF HS/Sleep diagnostic wording missing: {token}")
    for token in DEVICE_ID_IDF_TOKENS:
        if token not in text:
            fail(f"ESP-IDF Device ID manual-address token missing: {token}")
    for cmd in MANDATORY_COMMANDS:
        if cmd == "?":
            if '"?"' not in text and " / ?" not in text and " | ?" not in text:
                fail("mandatory command '?' missing from IDF example")
        elif re.search(rf"\b{re.escape(cmd)}\b", text) is None:
            fail(f"mandatory command '{cmd}' missing from IDF example")
    for component in IDF_REQUIRED_COMPONENTS:
        if re.search(rf"\b{re.escape(component)}\b", cmake) is None:
            fail(f"ESP-IDF CMake file missing component '{component}'")
    for token in ("../../..", "../../common"):
        if token in cmake:
            fail(f"ESP-IDF main CMake uses avoidable broad include path: {token}")
    for token in REQUIRED_CI_TOKENS:
        if token not in workflow:
            fail(f"CI workflow missing ESP-IDF token: {token}")
    if "docs/IDF_PORT.md" not in readme:
        fail("README must link the canonical ESP-IDF port notes")
    for token in REQUIRED_IDF_DOC_TOKENS:
        if token not in idf_doc:
            fail(f"IDF port notes missing verification/diagnostic wording: {token}")

    print("IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
