#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import runpy
import sys

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

REQUIRED_README_TOKENS = [
    "idf.py -C examples/espidf_basic set-target esp32s3 build",
    "idf.py -C examples/espidf_basic set-target esp32s2 build",
    "diagnostic-only",
    "console input can block",
    "production systems must serialize",
]

REQUIRED_CONFIRMATION_TOKENS = [
    "write!",
    "fill!",
    "stress!",
    "stress_mix!",
    "rw_suite!",
    "randbench!",
    "typed_demo!",
    "Confirmation required because this command changes FRAM contents.",
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
    ns = runpy.run_path(str(ROOT / "tools" / "check_cli_contract.py"))
    commands = ns.get("MANDATORY_COMMANDS", [])
    components = ns.get("IDF_REQUIRED_COMPONENTS", [])
    main_path = ROOT / "examples" / "espidf_basic" / "main" / "main.cpp"
    cmake_path = ROOT / "examples" / "espidf_basic" / "main" / "CMakeLists.txt"
    workflow_path = ROOT / ".github" / "workflows" / "ci.yml"
    readme_path = ROOT / "README.md"
    text = main_path.read_text(encoding="utf-8", errors="replace")
    cmake = cmake_path.read_text(encoding="utf-8", errors="replace")
    workflow = workflow_path.read_text(encoding="utf-8", errors="replace")
    readme = readme_path.read_text(encoding="utf-8", errors="replace")

    for path in example_contract_files(ROOT):
        rel = path.relative_to(ROOT)
        body = path.read_text(encoding="utf-8", errors="replace")
        for token in FORBIDDEN_TOKENS:
            if token in body:
                fail(f"forbidden Arduino compatibility token in {rel}: {token}")
    for token in REQUIRED_NATIVE_TOKENS:
        if token not in text:
            fail(f"native ESP-IDF token missing: {token}")
    for token in REQUIRED_CONFIRMATION_TOKENS:
        if token not in text:
            fail(f"confirmation/handler token missing: {token}")
    for cmd in commands:
        if cmd == "?":
            if '"?"' not in text and " / ?" not in text and " | ?" not in text:
                fail("mandatory command '?' missing from IDF example")
        elif re.search(rf"\b{re.escape(cmd)}\b", text) is None:
            fail(f"mandatory command '{cmd}' missing from IDF example")
    for component in components:
        if re.search(rf"\b{re.escape(component)}\b", cmake) is None:
            fail(f"ESP-IDF CMake file missing component '{component}'")
    for token in ("../../..", "../../common"):
        if token in cmake:
            fail(f"ESP-IDF main CMake uses avoidable broad include path: {token}")
    for token in REQUIRED_CI_TOKENS:
        if token not in workflow:
            fail(f"CI workflow missing ESP-IDF token: {token}")
    for token in REQUIRED_README_TOKENS:
        if token not in readme:
            fail(f"README missing ESP-IDF verification/diagnostic wording: {token}")

    print("IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
