#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

from _contract_data import MANDATORY_COMMANDS, MODE_CONTRACT_TOKENS

ROOT = pathlib.Path(__file__).resolve().parents[1]

REQUIRED_COMMON = [
    "BoardConfig.h",
    "BuildConfig.h",
    "Log.h",
    "I2cTransport.h",
    "I2cScanner.h",
    "CliShell.h",
    "CliStyle.h",
    "TypedMemory.h",
]

DEVICE_ID_CORE_TOKENS = [
    "I2cSpecialOp::READ_DEVICE_ID",
    "_config.i2cAddress << 1",
]

def fail(msg: str) -> None:
    print(f"CLI contract FAILED: {msg}")
    raise SystemExit(1)


def ensure_exists(path: pathlib.Path, label: str) -> None:
    if not path.exists():
        fail(f"missing {label}: {path.as_posix()}")


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
        if re.search(r'printHelpItem\s*\(\s*"[^"]*\?', text) is None:
            fail("mandatory command '?' missing from help text")
        return
    pattern = rf"printHelpItem\s*\(\s*\"[^\"]*\b{re.escape(token)}\b"
    if re.search(pattern, text) is None:
        fail(f"mandatory command '{token}' missing from help text")


def main() -> int:
    common_dir = ROOT / "examples" / "common"
    bringup_main = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"
    ensure_exists(common_dir, "common example directory")
    ensure_exists(bringup_main, "bringup CLI example")

    for name in REQUIRED_COMMON:
        ensure_exists(common_dir / name, f"common helper {name}")

    text = bringup_main.read_text(encoding="utf-8", errors="replace")

    for cmd in MANDATORY_COMMANDS:
        require_dispatch(text, cmd)
        require_help(text, cmd)

    for token in MODE_CONTRACT_TOKENS:
        require_literal(text, token, "Arduino HS/Sleep diagnostic wording")

    core_text = (ROOT / "src" / "MB85RC.cpp").read_text(
        encoding="utf-8", errors="replace"
    )
    for token in DEVICE_ID_CORE_TOKENS:
        require_literal(core_text, token, "core Device ID address construction")

    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
