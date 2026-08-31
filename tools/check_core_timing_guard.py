#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCAN_DIRS = ("src", "include")
VALID_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}

FORBIDDEN_CALLS = {
    "delay": re.compile(r"\bdelay\s*\("),
    "millis": re.compile(r"\bmillis\s*\("),
    "micros": re.compile(r"\bmicros\s*\("),
    "esp_timer_get_time": re.compile(r"\besp_timer_get_time\s*\("),
    "delayMicroseconds": re.compile(r"\bdelayMicroseconds\s*\("),
    "vTaskDelay": re.compile(r"\bvTaskDelay\s*\("),
    "sleep_for": re.compile(r"\bsleep_for\s*\("),
    "usleep": re.compile(r"\busleep\s*\("),
    "nanosleep": re.compile(r"\bnanosleep\s*\("),
    "Sleep": re.compile(r"\bSleep\s*\("),
    "esp_rom_delay_us": re.compile(r"\besp_rom_delay_us\s*\("),
    "ets_delay_us": re.compile(r"\bets_delay_us\s*\("),
    "yield": re.compile(r"\byield\s*\("),
}

FORBIDDEN_FRAMEWORK_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<\"]'
    r'(?P<header>'
    r'Arduino\.h|Wire\.h|WString\.h|HardwareSerial\.h|'
    r'esp_[^>\"]+\.h|driver/[^>\"]+|freertos/[^>\"]+|hal/[^>\"]+|soc/[^>\"]+|'
    r'sdkconfig\.h'
    r')[>\"]',
    re.MULTILINE,
)
FORBIDDEN_FRAMEWORK_TOKENS = {
    "Arduino String": re.compile(r"\bString\b"),
    "Serial": re.compile(r"\bSerial\b"),
    "TwoWire": re.compile(r"\bTwoWire\b"),
    "Wire": re.compile(r"\bWire\b"),
    "ESP_LOG": re.compile(r"\bESP_LOG[EDIWV]?\s*\("),
    "ESP_ERROR_CHECK": re.compile(r"\bESP_ERROR_CHECK\s*\("),
    "FreeRTOS task API": re.compile(r"\b(?:xTask|vTask|ulTask|uxTask)\w*\s*\("),
    "FreeRTOS semaphore API": re.compile(r"\b(?:xSemaphore|vSemaphore)\w*\s*\("),
}
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT_RE = re.compile(r"//[^\n]*")
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')

def strip_comments(text: str) -> str:
    text = BLOCK_COMMENT_RE.sub("", text)
    return LINE_COMMENT_RE.sub("", text)


def strip_non_code(text: str) -> str:
    return STRING_RE.sub('""', strip_comments(text))


def collect_sources() -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for dirname in SCAN_DIRS:
        root = ROOT / dirname
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix.lower() in VALID_SUFFIXES:
                files.append(path)
    return files


def main() -> int:
    observed_calls: dict[str, dict[str, int]] = {}
    observed_includes: dict[str, list[str]] = {}
    observed_tokens: dict[str, dict[str, int]] = {}

    for path in collect_sources():
        rel = path.relative_to(ROOT).as_posix()
        raw = path.read_text(encoding="utf-8", errors="replace")
        commentless = strip_comments(raw)
        code = strip_non_code(raw)

        call_counts: dict[str, int] = {}
        for call_name, pattern in FORBIDDEN_CALLS.items():
            count = len(pattern.findall(code))
            if count > 0:
                call_counts[call_name] = count
        if call_counts:
            observed_calls[rel] = call_counts

        includes = [
            match.group("header")
            for match in FORBIDDEN_FRAMEWORK_INCLUDE_RE.finditer(commentless)
        ]
        if includes:
            observed_includes[rel] = includes

        token_counts: dict[str, int] = {}
        for token_name, pattern in FORBIDDEN_FRAMEWORK_TOKENS.items():
            count = len(pattern.findall(code))
            if count > 0:
                token_counts[token_name] = count
        if token_counts:
            observed_tokens[rel] = token_counts

    errors: list[str] = []

    for rel, counts in observed_calls.items():
        errors.append(f"forbidden timing calls in core/public file: {rel} -> {counts}")

    for rel, includes in observed_includes.items():
        errors.append(f"forbidden framework include in core/public file: {rel} -> {includes}")

    for rel, counts in observed_tokens.items():
        errors.append(f"forbidden framework token in core/public file: {rel} -> {counts}")

    if errors:
        print("Core timing guard FAILED:")
        for err in errors:
            print(f"- {err}")
        return 1

    print("Core timing guard PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
