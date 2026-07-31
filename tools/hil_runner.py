#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
PROMPT_RE = re.compile(r"(?m)(?:^|\n)> ?$")
ANSI_RE = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")

DEFAULT_FAIL_TOKENS = (
    "[FAIL]",
    ": FAIL",
    "Guru Meditation",
    "assert failed",
    "abort()",
    "Traceback",
    "I2C_NACK",
    "I2C_TIMEOUT",
    "I2C_BUS",
    "DEVICE_ID_MISMATCH",
    "NOT_INITIALIZED",
    "VERIFY_MISMATCH",
)
DEFAULT_FAIL_PATTERNS = (
    re.compile(r"\bfail=([1-9][0-9]*)\b", re.IGNORECASE),
    re.compile(r"\bfail ([1-9][0-9]*)\b", re.IGNORECASE),
    re.compile(r"\bErrors:\s*([1-9][0-9]*)\b", re.IGNORECASE),
    re.compile(r"\bRead mismatches:\s*([1-9][0-9]*)\b", re.IGNORECASE),
    re.compile(r"\bfinal_match=no\b", re.IGNORECASE),
    re.compile(
        r"\b(?:Status|[a-z_][a-z_ ]*):\s*"
        r"(?:BUSY|INVALID_CONFIG|INVALID_PARAM|UNSUPPORTED|I2C_[A-Z_]+|"
        r"DEVICE_ID_MISMATCH|NOT_INITIALIZED|VERIFY_MISMATCH|CANCELLED|TIMEOUT)\b",
        re.IGNORECASE,
    ),
)
RESET_PATTERNS = (
    re.compile(r"\bESP-ROM:", re.IGNORECASE),
    re.compile(r"\brst:0x[0-9a-f]+", re.IGNORECASE),
    re.compile(r"\bboot:0x[0-9a-f]+", re.IGNORECASE),
    re.compile(r"\bGuru Meditation", re.IGNORECASE),
)
HEAP_RE = re.compile(r"heap:\s*free=(\d+)\s+min_free=(\d+)\s+largest=(\d+)", re.IGNORECASE)
STATE_NAME_RE = re.compile(r"\bState:\s*([A-Z]+)\b")
STATE_NUM_RE = re.compile(r"\bstate=(\d+)\b")
INITIALIZED_RE = re.compile(r"\binitialized=(\d+)\b")
CONSECUTIVE_RE = re.compile(r"\bConsecutive failures:\s*(\d+)\b", re.IGNORECASE)
CONSECUTIVE_KV_RE = re.compile(r"\bconsecutive=(\d+)\b")
TOTAL_FAILURES_RE = re.compile(r"\bTotal failures:\s*(\d+)\b", re.IGNORECASE)
TOTAL_FAILURES_KV_RE = re.compile(r"\bstate=.*?\bfail=(\d+)\b.*?\bconsecutive=", re.IGNORECASE | re.DOTALL)
MANUFACTURER_RE = re.compile(r"\bmanufacturer(?:=|Id=| ID:?\s*)0x([0-9a-f]+)", re.IGNORECASE)
PRODUCT_RE = re.compile(r"\bproduct(?:=|Id=| ID:?\s*)0x([0-9a-f]+)", re.IGNORECASE)
VARIANT_PATTERNS = (
    re.compile(r"\bvariant[=:]\s*(MB85RC[0-9A-Z]+)\b", re.IGNORECASE),
    re.compile(r"\bActive variant:\s*(MB85RC[0-9A-Z]+)\b", re.IGNORECASE),
    re.compile(r"\bVariant:\s*(MB85RC[0-9A-Z]+)\s*\(", re.IGNORECASE),
)
CAPACITY_PATTERNS = (
    re.compile(r"\bcapacity=(\d+)\b", re.IGNORECASE),
    re.compile(r"\bActive capacity:\s*(\d+)\s*bytes\b", re.IGNORECASE),
    re.compile(r"\bVariant:\s*MB85RC[0-9A-Z]+\s*\((\d+)\s*bytes\)", re.IGNORECASE),
)
TIMEOUT_PATTERNS = (
    re.compile(r"\bI2C timeout:\s*(\d+)\s*ms\b", re.IGNORECASE),
    re.compile(r"\btransport_timeout_ms=(\d+)\b", re.IGNORECASE),
)
TRANSPORT_CAPACITY_PATTERNS = (
    re.compile(r"\bTransport capacity:\s*TX=(\d+)\s+RX=(\d+)\s+bytes\b", re.IGNORECASE),
    re.compile(r"\bmax_tx=(\d+)\s+max_rx=(\d+)\b", re.IGNORECASE),
)
DATA_CAPACITY_PATTERNS = (
    re.compile(r"\bOne-transaction data:\s*write=(\d+)\s+read=(\d+)\s+bytes\b", re.IGNORECASE),
    re.compile(r"\bwrite_data=(\d+)\s+read_data=(\d+)\b", re.IGNORECASE),
)
ARDUINO_CORE_RE = re.compile(r"\bArduino-ESP32:\s*(\S+)", re.IGNORECASE)
ESP_IDF_RE = re.compile(r"\bESP-IDF:\s*(\S+)", re.IGNORECASE)
STATE_NUMBERS = {
    0: "UNINIT",
    1: "READY",
    2: "DEGRADED",
    3: "OFFLINE",
}


@dataclass(frozen=True)
class CommandStep:
    test_id: str
    area: str
    command: str
    expected_all: tuple[str, ...] = ()
    expected_any: tuple[tuple[str, ...], ...] = ()
    fail_tokens: tuple[str, ...] = DEFAULT_FAIL_TOKENS
    fail_patterns: tuple[re.Pattern[str], ...] = DEFAULT_FAIL_PATTERNS
    timeout_s: float | None = None
    notes: str = ""


@dataclass
class StepResult:
    test_id: str
    area: str
    command: str
    expected: str
    observed: str
    elapsed_s: float
    status: str
    notes: str = ""


@dataclass(frozen=True)
class CommandCapture:
    output: str
    elapsed_s: float
    prompt_detected: bool
    completed_within_deadline: bool


@dataclass
class SoakSummary:
    status: str = "NOT RUN"
    start: str = ""
    end: str = ""
    duration_s: float = 0.0
    command_counts: dict[str, int] = field(default_factory=dict)
    pass_count: int = 0
    fail_count: int = 0
    unknown_count: int = 0
    consecutive_failure_bursts: int = 0
    worst_consecutive_failures: int = 0
    min_latency_s: float = 0.0
    mean_latency_s: float = 0.0
    max_latency_s: float = 0.0
    worst_read_latency_s: float = 0.0
    recover_count: int = 0
    reconnect_count: int = 0
    framing_sync_count: int = 0


@dataclass
class Observations:
    arduino_core_version: str | None = None
    esp_idf_version: str | None = None
    variant: str | None = None
    manufacturer_id: int | None = None
    product_id: int | None = None
    capacity: int | None = None
    i2c_timeout_ms: int | None = None
    max_tx_bytes: int | None = None
    max_rx_bytes: int | None = None
    max_write_data_bytes: int | None = None
    max_read_data_bytes: int | None = None
    final_state: str | None = None
    final_initialized: bool | None = None
    final_consecutive_failures: int | None = None
    final_total_failures: int | None = None
    heap_baseline_free: int | None = None
    heap_final_free: int | None = None
    heap_min_free_observed: int | None = None
    heap_final_largest: int | None = None
    target_reset_count: int = 0
    serial_reconnect_count: int = 0
    serial_framing_sync_count: int = 0


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def normalize_output(text: str) -> str:
    return strip_ansi(text).replace("\r\n", "\n").replace("\r", "\n")


def excerpt(text: str, limit: int = 240) -> str:
    clean = " ".join(normalize_output(text).split())
    if len(clean) <= limit:
        return clean
    return clean[: limit - 3] + "..."


def expected_summary(step: CommandStep) -> str:
    parts: list[str] = []
    if step.expected_all:
        parts.append("all: " + ", ".join(step.expected_all))
    if step.expected_any:
        alts = [" + ".join(group) for group in step.expected_any]
        parts.append("any: " + " | ".join(alts))
    return "; ".join(parts) if parts else "prompt returns bounded output"


def classify(
    step: CommandStep,
    output: str,
    elapsed_s: float,
    *,
    prompt_detected: bool = True,
    completed_within_deadline: bool = True,
) -> StepResult:
    clean = normalize_output(output)
    failure_notes: list[str] = []
    for token in step.fail_tokens:
        if token and token in clean:
            failure_notes.append(f"failure token: {token}")
            break
    if not failure_notes:
        for pattern in step.fail_patterns:
            match = pattern.search(clean)
            if match is not None:
                failure_notes.append(f"failure pattern: {pattern.pattern}")
                break

    missing = [token for token in step.expected_all if token not in clean]
    any_ok = True
    if step.expected_any:
        any_ok = any(all(token in clean for token in group) for group in step.expected_any)

    if failure_notes:
        status = "FAIL"
        notes = "; ".join(failure_notes)
    elif not prompt_detected:
        status = "UNKNOWN"
        notes = "command prompt was not recovered"
    elif not completed_within_deadline:
        status = "UNKNOWN"
        notes = "command exceeded its bounded response deadline"
    elif missing:
        status = "UNKNOWN"
        notes = "missing expected token(s): " + ", ".join(missing)
    elif not any_ok:
        status = "UNKNOWN"
        notes = "no expected alternative matched"
    else:
        status = "PASS"
        notes = step.notes

    return StepResult(
        test_id=step.test_id,
        area=step.area,
        command=step.command,
        expected=expected_summary(step),
        observed=excerpt(clean),
        elapsed_s=elapsed_s,
        status=status,
        notes=notes,
    )


def count_target_resets(output: str) -> int:
    clean = normalize_output(output)
    if not clean:
        return 0
    return 1 if any(pattern.search(clean) for pattern in RESET_PATTERNS) else 0


def update_observations(observations: Observations, output: str, *, count_resets: bool) -> None:
    clean = normalize_output(output)
    if count_resets:
        observations.target_reset_count += count_target_resets(clean)

    match = ARDUINO_CORE_RE.search(clean)
    if match is not None:
        observations.arduino_core_version = match.group(1)
    match = ESP_IDF_RE.search(clean)
    if match is not None:
        observations.esp_idf_version = match.group(1)

    for match in HEAP_RE.finditer(clean):
        free = int(match.group(1))
        min_free = int(match.group(2))
        largest = int(match.group(3))
        if observations.heap_baseline_free is None:
            observations.heap_baseline_free = free
        observations.heap_final_free = free
        observations.heap_final_largest = largest
        if observations.heap_min_free_observed is None:
            observations.heap_min_free_observed = min_free
        else:
            observations.heap_min_free_observed = min(observations.heap_min_free_observed, min_free)

    match = MANUFACTURER_RE.search(clean)
    if match is not None:
        observations.manufacturer_id = int(match.group(1), 16)

    for line in clean.splitlines():
        if "Device ID" not in line and "manufacturer" not in line.lower():
            continue
        match = PRODUCT_RE.search(line)
        if match is not None:
            observations.product_id = int(match.group(1), 16)
            break

    for pattern in VARIANT_PATTERNS:
        match = pattern.search(clean)
        if match is not None:
            observations.variant = match.group(1).upper()
            break

    for pattern in CAPACITY_PATTERNS:
        match = pattern.search(clean)
        if match is not None:
            observations.capacity = int(match.group(1))
            break

    for pattern in TIMEOUT_PATTERNS:
        match = pattern.search(clean)
        if match is not None:
            observations.i2c_timeout_ms = int(match.group(1))
            break

    for pattern in TRANSPORT_CAPACITY_PATTERNS:
        match = pattern.search(clean)
        if match is not None:
            observations.max_tx_bytes = int(match.group(1))
            observations.max_rx_bytes = int(match.group(2))
            break

    for pattern in DATA_CAPACITY_PATTERNS:
        match = pattern.search(clean)
        if match is not None:
            observations.max_write_data_bytes = int(match.group(1))
            observations.max_read_data_bytes = int(match.group(2))
            break

    match = STATE_NAME_RE.search(clean)
    if match is not None:
        observations.final_state = match.group(1).upper()
    match = STATE_NUM_RE.search(clean)
    if match is not None:
        observations.final_state = STATE_NUMBERS.get(int(match.group(1)), f"UNKNOWN({match.group(1)})")

    match = INITIALIZED_RE.search(clean)
    if match is not None:
        observations.final_initialized = match.group(1) != "0"

    match = CONSECUTIVE_RE.search(clean)
    if match is not None:
        observations.final_consecutive_failures = int(match.group(1))
    match = CONSECUTIVE_KV_RE.search(clean)
    if match is not None:
        observations.final_consecutive_failures = int(match.group(1))

    match = TOTAL_FAILURES_RE.search(clean)
    if match is not None:
        observations.final_total_failures = int(match.group(1))
    match = TOTAL_FAILURES_KV_RE.search(clean)
    if match is not None:
        observations.final_total_failures = int(match.group(1))


def profile_command(profile: str, base: str) -> str:
    if profile == "idf" and base in {
        "selftest",
        "rw_suite",
        "xfer_demo",
        "typed_demo",
    }:
        return base + "!"
    if profile == "idf" and (base.startswith("randbench ") or base.startswith("stress ")):
        head, tail = base.split(" ", 1)
        return f"{head}! {tail}"
    if profile == "idf" and (base.startswith("stress_mix ")):
        return "stress_mix! " + base.split(" ", 1)[1]
    return base


def make_functional_steps(profile: str, sample_count: int, include_stress: bool) -> list[CommandStep]:
    def pc(command: str) -> str:
        return profile_command(profile, command)

    if profile == "arduino":
        hs_enter_expected = (("High-speed mode:", "Status:", "UNSUPPORTED"),)
        hs_enter_fail_tokens: tuple[str, ...] = ()
        hs_enter_fail_patterns: tuple[re.Pattern[str], ...] = ()
    else:
        hs_enter_expected = (("High-speed mode:", "hs enter: OK"),)
        hs_enter_fail_tokens = DEFAULT_FAIL_TOKENS
        hs_enter_fail_patterns = DEFAULT_FAIL_PATTERNS

    steps = [
        CommandStep("HIL-001", "connectivity", "version",
                    expected_any=(("MB85RC library version",), ("MB85RC ",))),
        CommandStep("HIL-002", "connectivity", "scan",
                    expected_any=(("Scan complete",), ("I2C scan:",)), timeout_s=25),
        CommandStep("HIL-003", "state", "settings",
                    expected_any=(("=== Settings ===",), ("state=", "capacity="))),
        CommandStep("HIL-004", "state", "drv",
                    expected_any=(("=== Driver Health ===",), ("state=", "initialized="))),
        CommandStep("HIL-004H", "state", "heap",
                    expected_all=("heap:", "free=", "min_free=", "largest=")),
        CommandStep("HIL-005", "identity", "id",
                    expected_any=(("Device ID:", "Manufacturer=0x"), ("id: OK", "manufacturer=0x"))),
        CommandStep("HIL-006", "identity", "idraw",
                    expected_any=(("Device ID raw:",), ("idraw: OK",))),
        CommandStep("HIL-007", "identity", "variants",
                    expected_any=(("Known MB85RC family variants:",), ("MB85RC256V", "bytes="))),
        CommandStep("HIL-008", "memory", "size",
                    expected_any=(("Active capacity:",), ("capacity=", "variant="))),
        CommandStep("HIL-009", "diagnostics", "probe",
                    expected_any=(("Status:", "OK"), ("probe: OK",))),
        CommandStep("HIL-010", "memory", "read 0x0000 16",
                    expected_any=(("0000:",), ("0x000000:",))),
        CommandStep("HIL-011", "memory", "current 1",
                    expected_any=(("Current",), ("current: OK",), ("0010:",), ("0x",))),
        CommandStep("HIL-012", "memory", "text 0x0000 16",
                    expected_any=(("0000:",), ("\\x",), ("text",))),
        CommandStep("HIL-013", "memory", "crc 0x0000 64",
                    expected_any=(("CRC32",), ("crc32=",))),
        CommandStep("HIL-014", "modes", "hs support",
                    expected_all=("High-speed mode:", "Support:")),
        CommandStep("HIL-015", "modes", "hs enter",
                    expected_any=hs_enter_expected,
                    fail_tokens=hs_enter_fail_tokens,
                    fail_patterns=hs_enter_fail_patterns),
        CommandStep("HIL-015A", "modes", "hs exit",
                    expected_any=(("High-speed mode:", "Status:", "OK"),
                                  ("High-speed mode:", "hs exit: OK"))),
        CommandStep("HIL-015B", "modes", "hs support",
                    expected_all=("High-speed mode:", "Enabled: no")),
        CommandStep("HIL-016", "modes", "sleep support",
                    expected_all=("Sleep mode:", "Support:", "State: AWAKE")),
        CommandStep("HIL-018", "recovery", "recover",
                    expected_any=(("Status:", "OK"), ("recover: OK",))),
        CommandStep("HIL-019", "validation", "definitely_not_a_command",
                    expected_any=(("Unknown command",), ("Unknown command. Try 'help'.",)),
                    fail_tokens=(), fail_patterns=()),
        CommandStep("HIL-020", "validation", "read 0xFFFFFFFF 1",
                    expected_any=(("Range",), ("Address out of range",), ("outside active capacity",), ("Usage:",)),
                    fail_tokens=(), fail_patterns=()),
        CommandStep("HIL-021", "diagnostics", pc("selftest"),
                    expected_any=(("Selftest result:",), ("selftest_pattern=PASS",)),
                    timeout_s=20),
        CommandStep("HIL-022", "memory", pc("rw_suite"),
                    expected_any=(("Read/write suite result:",), ("rw_suite restore: OK",)),
                    timeout_s=25),
        CommandStep("HIL-023", "staged", pc("xfer_demo"),
                    expected_any=(("Transfer demo result:",), ("xfer_demo_result",)),
                    timeout_s=25),
        CommandStep("HIL-024", "data", pc("typed_demo"),
                    expected_any=(("Typed Value Demo",), ("typed_demo restore: OK",)),
                    timeout_s=25),
        CommandStep("HIL-025", "timing", pc(f"randbench {sample_count}"),
                    expected_any=(("Random Access Benchmark",), ("randbench_ok=",)),
                    timeout_s=60),
        CommandStep("HIL-026", "state", "drv",
                    expected_any=(("=== Driver Health ===",), ("state=", "initialized="))),
    ]

    if profile == "idf":
        sleep_steps = [
            CommandStep("HIL-017", "modes", "sleep enter",
                        expected_any=(("Sleep mode:", "sleep enter: OK"),)),
            CommandStep("HIL-017A", "modes", "sleep wake",
                        expected_any=(("Sleep mode:", "sleep wake: OK",
                                       "recover after sleep wake: OK"),),
                        timeout_s=10),
        ]
    else:
        sleep_steps = [
            CommandStep("HIL-017", "modes", "sleep enter",
                        expected_any=(("Sleep mode:", "Status: UNSUPPORTED"),),
                        fail_tokens=(), fail_patterns=()),
            CommandStep("HIL-017A", "modes", "sleep wake",
                        expected_any=(("Sleep mode:", "Status: UNSUPPORTED"),),
                        fail_tokens=(), fail_patterns=()),
        ]
    sleep_index = next(i for i, step in enumerate(steps) if step.test_id == "HIL-018")
    steps[sleep_index:sleep_index] = sleep_steps

    if include_stress:
        steps.insert(-1, CommandStep("HIL-025A", "stress", pc(f"stress {sample_count}"),
                                     expected_any=(("Stress Summary",), ("stress_ok=",)),
                                     timeout_s=60,
                                     notes="Uses a backed-up scratch byte and restores it."))
        steps.insert(-1, CommandStep("HIL-025B", "stress", pc(f"stress_mix {sample_count}"),
                                     expected_any=(("stress_mix summary",), ("stress_mix_ok=",)),
                                     timeout_s=60,
                                     notes="Uses a backed-up scratch window and restores it."))
    return steps


def make_soak_steps(profile: str, sample_count: int) -> list[CommandStep]:
    def pc(command: str) -> str:
        return profile_command(profile, command)

    count = max(1, min(sample_count, 50))
    return [
        CommandStep("SOAK-DRV", "soak", "drv",
                    expected_any=(("=== Driver Health ===",), ("state=", "initialized="))),
        CommandStep("SOAK-HEAP", "soak", "heap",
                    expected_all=("heap:", "free=", "min_free=", "largest=")),
        CommandStep("SOAK-ID", "soak", "id",
                    expected_any=(("Device ID:",), ("id: OK",))),
        CommandStep("SOAK-READ", "soak", "read 0x0000 16",
                    expected_any=(("0000:",), ("0x000000:",))),
        CommandStep("SOAK-CRC", "soak", "crc 0x0000 64",
                    expected_any=(("CRC32",), ("crc32=",))),
        CommandStep("SOAK-PROBE", "soak", "probe",
                    expected_any=(("Status:", "OK"), ("probe: OK",))),
        CommandStep("SOAK-RECOVER", "soak", "recover",
                    expected_any=(("Status:", "OK"), ("recover: OK",))),
        CommandStep("SOAK-HS", "soak", "hs support",
                    expected_all=("High-speed mode:", "Support:")),
        CommandStep("SOAK-SLEEP", "soak", "sleep support",
                    expected_all=("Sleep mode:", "Support:")),
        CommandStep("SOAK-RW", "soak", pc("rw_suite"),
                    expected_any=(("Read/write suite result:",), ("rw_suite restore: OK",)),
                    timeout_s=25),
        CommandStep("SOAK-XFER", "soak", pc("xfer_demo"),
                    expected_any=(("Transfer demo result:",), ("xfer_demo_result",)),
                    timeout_s=25),
        CommandStep("SOAK-RAND", "soak", pc(f"randbench {count}"),
                    expected_any=(("Random Access Benchmark",), ("randbench_ok=",)),
                    timeout_s=60),
    ]


def make_final_steps() -> list[CommandStep]:
    return [
        CommandStep("FINAL-DRV", "final", "drv",
                    expected_any=(("=== Driver Health ===",), ("state=", "initialized="))),
        CommandStep("FINAL-HEAP", "final", "heap",
                    expected_all=("heap:", "free=", "min_free=", "largest=")),
    ]


class SerialSession:
    def __init__(
        self,
        port: str,
        baud: int,
        timeout_s: float,
        idle_timeout_s: float,
        transcript_path: Path,
        verbose: bool,
    ) -> None:
        import serial  # type: ignore

        self.serial_mod = serial
        self.port = port
        self.baud = baud
        self.timeout_s = timeout_s
        self.idle_timeout_s = idle_timeout_s
        self.transcript_path = transcript_path
        self.verbose = verbose
        self.reconnect_count = 0
        self.framing_sync_count = 0
        self.framing_lost = False
        self.transcript_path.parent.mkdir(parents=True, exist_ok=True)
        self.ser = self._open_serial()
        time.sleep(0.1)

    def close(self) -> None:
        try:
            if self.ser is not None and self.ser.is_open:
                self.ser.close()
        except (self.serial_mod.SerialException, OSError):
            pass

    def _open_serial(self):
        # Configure modem-control lines before opening. Passing port directly
        # opens first and can pulse DTR/RTS, which resets ESP32-S3 native USB.
        ser = self.serial_mod.Serial(
            port=None,
            baudrate=self.baud,
            timeout=0.05,
            write_timeout=self.timeout_s,
        )
        ser.dtr = False
        ser.rts = False
        ser.port = self.port
        ser.open()
        return ser

    def _reopen_until(self, deadline: float, *, count_reconnect: bool) -> None:
        try:
            self.close()
        except (self.serial_mod.SerialException, OSError):
            pass
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            try:
                self.ser = self._open_serial()
                if count_reconnect:
                    self.reconnect_count += 1
                return
            except (self.serial_mod.SerialException, OSError) as exc:
                last_error = exc
                time.sleep(0.1)
        raise self.serial_mod.SerialException(
            f"failed to reopen {self.port} before reconnect deadline: {last_error}"
        )

    def reset(self) -> None:
        try:
            # A runtime reset must leave the boot strap deasserted. Driving
            # DTR and RTS high together can place ESP32-S3 USB-JTAG/Serial
            # targets in the ROM downloader instead of restarting the app.
            self.ser.dtr = False
            self.ser.rts = False
            time.sleep(0.1)
            self.ser.rts = True
            time.sleep(0.2)
            self.ser.rts = False
            time.sleep(0.2)
        finally:
            # ESP32-S3 native USB CDC disappears during reset. Reopen it using
            # a bounded deadline instead of assuming the old handle survives.
            self._reopen_until(time.monotonic() + max(10.0, self.timeout_s),
                               count_reconnect=False)

    def append_transcript(self, label: str, text: str) -> None:
        with self.transcript_path.open("a", encoding="utf-8", errors="replace") as fh:
            fh.write(f"\n\n===== {label} =====\n")
            fh.write(text)
            if not text.endswith("\n"):
                fh.write("\n")

    def read_until_prompt(self, timeout_s: float | None = None) -> tuple[str, bool]:
        timeout = self.timeout_s if timeout_s is None else timeout_s
        deadline = time.monotonic() + timeout
        last_rx = time.monotonic()
        chunks: list[str] = []
        nudged = False
        required_prompts = 1
        while time.monotonic() < deadline:
            try:
                waiting = self.ser.in_waiting
                data = self.ser.read(waiting or 1)
            except (self.serial_mod.SerialException, OSError):
                self._reopen_until(deadline, count_reconnect=True)
                continue
            if data:
                text = data.decode("utf-8", errors="replace")
                chunks.append(text)
                last_rx = time.monotonic()
            elif chunks and (time.monotonic() - last_rx) >= self.idle_timeout_s:
                clean = normalize_output("".join(chunks))
                if len(PROMPT_RE.findall(clean)) >= required_prompts:
                    return "".join(chunks), True
                if not nudged:
                    # Native USB CDC can retain the tail of a short response
                    # until the host transmits again. A single read-only version
                    # command provides a bounded framing marker without
                    # replaying the original command. An empty line is not
                    # sufficient because both CLI shells may ignore it.
                    try:
                        self.ser.write(b"version\n")
                        self.ser.flush()
                        self.framing_sync_count += 1
                        # Consume both the original command prompt and the
                        # read-only sync prompt. Returning after the first
                        # would leave the sync response queued for the next
                        # command and could misattribute its result.
                        required_prompts = 2
                    except (self.serial_mod.SerialException, OSError):
                        self._reopen_until(deadline, count_reconnect=True)
                    nudged = True
        output = "".join(chunks)
        prompt_count = len(PROMPT_RE.findall(normalize_output(output)))
        return output, prompt_count >= required_prompts

    def wait_for_prompt(self, boot_settle_s: float, timeout_s: float) -> str:
        time.sleep(boot_settle_s)
        boot, prompt_detected = self.read_until_prompt(timeout_s)
        if not prompt_detected:
            self.ser.write(b"version\n")
            self.ser.flush()
            self.framing_sync_count += 1
            more, _ = self.read_until_prompt(timeout_s)
            boot += more
        self.append_transcript("BOOT", boot)
        return boot

    def command(self, command: str, timeout_s: float | None = None) -> CommandCapture:
        if self.verbose:
            print(f">>> {command}")
        start = time.monotonic()
        try:
            self.ser.write((command + "\n").encode("utf-8"))
            self.ser.flush()
        except (self.serial_mod.SerialException, OSError) as exc:
            deadline = start + (self.timeout_s if timeout_s is None else timeout_s)
            self._reopen_until(deadline, count_reconnect=True)
            output = f"HOST SERIAL WRITE ERROR: {exc}\n"
            elapsed = time.monotonic() - start
            self.framing_lost = True
            self.append_transcript(f"COMMAND {command} ({elapsed:.3f}s)", output)
            return CommandCapture(
                output=output,
                elapsed_s=elapsed,
                prompt_detected=False,
                completed_within_deadline=False,
            )
        output, prompt_detected = self.read_until_prompt(timeout_s)
        completed_within_deadline = prompt_detected
        if not prompt_detected:
            # Do not issue another command into an unfinished response. Allow
            # one bounded framing-recovery window, while retaining the timeout
            # as an UNKNOWN result for the command that exceeded its deadline.
            more, prompt_detected = self.read_until_prompt(self.timeout_s)
            output += more
        if not prompt_detected:
            self.framing_lost = True
        elapsed = time.monotonic() - start
        self.append_transcript(f"COMMAND {command} ({elapsed:.3f}s)", output)
        if self.verbose:
            print(excerpt(output, 500))
        return CommandCapture(
            output=output,
            elapsed_s=elapsed,
            prompt_detected=prompt_detected,
            completed_within_deadline=completed_within_deadline,
        )


def detect_profile(requested: str, boot: str) -> str:
    if requested != "auto":
        return requested
    clean = normalize_output(boot)
    if "Native ESP-IDF" in clean or "native ESP-IDF" in clean:
        return "idf"
    return "arduino"


def run_steps(
    session: SerialSession,
    steps: Iterable[CommandStep],
    default_timeout_s: float,
    observations: Observations,
) -> list[StepResult]:
    results: list[StepResult] = []
    for step in steps:
        capture = session.command(step.command, step.timeout_s or default_timeout_s)
        update_observations(observations, capture.output, count_resets=True)
        observations.serial_reconnect_count = session.reconnect_count
        observations.serial_framing_sync_count = session.framing_sync_count
        result = classify(
            step,
            capture.output,
            capture.elapsed_s,
            prompt_detected=capture.prompt_detected,
            completed_within_deadline=capture.completed_within_deadline,
        )
        results.append(result)
        if not capture.prompt_detected:
            break

    return results


def run_soak(
    session: SerialSession,
    profile: str,
    duration_s: float,
    sample_count: int,
    pacing_s: float,
    default_timeout_s: float,
    max_consecutive_failures: int,
    observations: Observations,
) -> tuple[SoakSummary, list[StepResult]]:
    if duration_s <= 0:
        return SoakSummary(), []

    steps = make_soak_steps(profile, sample_count)
    summary = SoakSummary(status="PASS", start=datetime.now().astimezone().isoformat(timespec="seconds"))
    start = time.monotonic()
    deadline = start + duration_s
    reconnect_start = session.reconnect_count
    framing_sync_start = session.framing_sync_count
    latencies: list[float] = []
    results: list[StepResult] = []
    consecutive = 0
    in_failure_burst = False

    index = 0
    while time.monotonic() < deadline:
        step = steps[index % len(steps)]
        index += 1
        capture = session.command(step.command, step.timeout_s or default_timeout_s)
        output = capture.output
        elapsed = capture.elapsed_s
        update_observations(observations, output, count_resets=True)
        observations.serial_reconnect_count = session.reconnect_count
        observations.serial_framing_sync_count = session.framing_sync_count
        result = classify(
            step,
            output,
            elapsed,
            prompt_detected=capture.prompt_detected,
            completed_within_deadline=capture.completed_within_deadline,
        )
        results.append(result)
        summary.command_counts[step.command] = summary.command_counts.get(step.command, 0) + 1
        latencies.append(elapsed)
        if step.command.startswith("read "):
            summary.worst_read_latency_s = max(summary.worst_read_latency_s, elapsed)
        if step.command == "recover":
            summary.recover_count += 1

        if result.status == "PASS":
            summary.pass_count += 1
            consecutive = 0
            in_failure_burst = False
        elif result.status == "FAIL":
            summary.fail_count += 1
            consecutive += 1
            if not in_failure_burst:
                summary.consecutive_failure_bursts += 1
                in_failure_burst = True
        else:
            summary.unknown_count += 1
            consecutive += 1
            if not in_failure_burst:
                summary.consecutive_failure_bursts += 1
                in_failure_burst = True

        summary.worst_consecutive_failures = max(summary.worst_consecutive_failures, consecutive)
        if not capture.prompt_detected:
            summary.status = "FAIL"
            break
        if consecutive >= max_consecutive_failures:
            summary.status = "FAIL"
            break
        if pacing_s > 0:
            time.sleep(pacing_s)

    summary.end = datetime.now().astimezone().isoformat(timespec="seconds")
    summary.duration_s = time.monotonic() - start
    summary.reconnect_count = session.reconnect_count - reconnect_start
    summary.framing_sync_count = session.framing_sync_count - framing_sync_start
    if latencies:
        summary.min_latency_s = min(latencies)
        summary.mean_latency_s = statistics.fmean(latencies)
        summary.max_latency_s = max(latencies)
    if summary.status == "PASS" and summary.unknown_count > 0:
        summary.status = "UNKNOWN"
    if summary.status == "PASS" and summary.duration_s + 1.0 < duration_s:
        summary.status = "UNKNOWN"
    return summary, results


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")


def markdown_table(results: list[StepResult]) -> str:
    lines = [
        "| Test ID | Area | Command | Expected | Observed | Elapsed s | Result | Notes |",
        "| --- | --- | --- | --- | --- | ---: | --- | --- |",
    ]
    for r in results:
        row = [
            r.test_id,
            r.area,
            f"`{r.command}`",
            r.expected.replace("|", "\\|"),
            r.observed.replace("|", "\\|"),
            f"{r.elapsed_s:.3f}",
            r.status,
            r.notes.replace("|", "\\|"),
        ]
        lines.append("| " + " | ".join(row) + " |")
    return "\n".join(lines) + "\n"


def write_markdown(
    path: Path,
    port: str,
    baud: int,
    profile: str,
    results: list[StepResult],
    soak: SoakSummary,
    transcript_path: Path,
    json_path: Path,
    observations: Observations,
    strict_reasons: list[str],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    counts = count_results(results)
    body = [
        f"# MB85RC HIL Runner Results - {port}",
        "",
        f"- Generated: {datetime.now().astimezone().isoformat(timespec='seconds')}",
        f"- Port: `{port}`",
        f"- Baud: `{baud}`",
        f"- Detected profile: `{profile}`",
        f"- Transcript: `{transcript_path.as_posix()}`",
        f"- JSON: `{json_path.as_posix()}`",
        f"- Functional counts: PASS={counts['PASS']} FAIL={counts['FAIL']} UNKNOWN={counts['UNKNOWN']} NOT RUN=0",
        f"- Strict gate: `{'PASS' if not strict_reasons else 'FAIL'}`",
        "",
        "## Functional Results",
        "",
        markdown_table(results),
        "## Soak Summary",
        "",
        f"- Status: {soak.status}",
        f"- Duration: {soak.duration_s:.1f} s",
        f"- PASS={soak.pass_count} FAIL={soak.fail_count} UNKNOWN={soak.unknown_count}",
        f"- Latency min/mean/max: {soak.min_latency_s:.3f}/{soak.mean_latency_s:.3f}/{soak.max_latency_s:.3f} s",
        f"- Worst read latency: {soak.worst_read_latency_s:.3f} s",
        f"- Recover commands: {soak.recover_count}",
        f"- Read-only serial framing syncs: {soak.framing_sync_count}",
        "",
        "## Observations",
        "",
        f"- Arduino-ESP32: `{observations.arduino_core_version or 'unknown'}`",
        f"- ESP-IDF: `{observations.esp_idf_version or 'unknown'}`",
        f"- Variant: `{observations.variant or 'unknown'}`",
        f"- Product ID: `{('unknown' if observations.product_id is None else f'0x{observations.product_id:03X}')}`",
        f"- Capacity: `{observations.capacity if observations.capacity is not None else 'unknown'}`",
        f"- I2C timeout: `{observations.i2c_timeout_ms if observations.i2c_timeout_ms is not None else 'unknown'} ms`",
        f"- Transport capacity: TX=`{observations.max_tx_bytes if observations.max_tx_bytes is not None else 'unknown'}` RX=`{observations.max_rx_bytes if observations.max_rx_bytes is not None else 'unknown'}`",
        f"- One-transaction data: write=`{observations.max_write_data_bytes if observations.max_write_data_bytes is not None else 'unknown'}` read=`{observations.max_read_data_bytes if observations.max_read_data_bytes is not None else 'unknown'}`",
        f"- Final health: state=`{observations.final_state or 'unknown'}` consecutiveFailures=`{observations.final_consecutive_failures if observations.final_consecutive_failures is not None else 'unknown'}` totalFailures=`{observations.final_total_failures if observations.final_total_failures is not None else 'unknown'}`",
        f"- Target resets after boot: `{observations.target_reset_count}`",
        f"- Serial reconnects: `{observations.serial_reconnect_count}`",
        f"- Read-only serial framing syncs: `{observations.serial_framing_sync_count}`",
        f"- Heap: baseline=`{observations.heap_baseline_free if observations.heap_baseline_free is not None else 'unknown'}` final=`{observations.heap_final_free if observations.heap_final_free is not None else 'unknown'}` min_free=`{observations.heap_min_free_observed if observations.heap_min_free_observed is not None else 'unknown'}` largest_final=`{observations.heap_final_largest if observations.heap_final_largest is not None else 'unknown'}`",
        "",
    ]
    if strict_reasons:
        body.extend(["## Strict Gate Failures", ""])
        body.extend(f"- {reason}" for reason in strict_reasons)
        body.append("")
    path.write_text("\n".join(body), encoding="utf-8")


def count_results(results: Iterable[StepResult]) -> dict[str, int]:
    counts = {"PASS": 0, "FAIL": 0, "UNKNOWN": 0}
    for result in results:
        counts[result.status] = counts.get(result.status, 0) + 1
    return counts


def result_to_dict(result: StepResult) -> dict:
    return {
        "test_id": result.test_id,
        "area": result.area,
        "command": result.command,
        "expected": result.expected,
        "observed": result.observed,
        "elapsed_s": result.elapsed_s,
        "status": result.status,
        "notes": result.notes,
    }


def soak_to_dict(soak: SoakSummary) -> dict:
    return {
        "status": soak.status,
        "start": soak.start,
        "end": soak.end,
        "duration_s": soak.duration_s,
        "command_counts": soak.command_counts,
        "pass_count": soak.pass_count,
        "fail_count": soak.fail_count,
        "unknown_count": soak.unknown_count,
        "consecutive_failure_bursts": soak.consecutive_failure_bursts,
        "worst_consecutive_failures": soak.worst_consecutive_failures,
        "min_latency_s": soak.min_latency_s,
        "mean_latency_s": soak.mean_latency_s,
        "max_latency_s": soak.max_latency_s,
        "worst_read_latency_s": soak.worst_read_latency_s,
        "recover_count": soak.recover_count,
        "reconnect_count": soak.reconnect_count,
        "framing_sync_count": soak.framing_sync_count,
    }


def observations_to_dict(observations: Observations) -> dict:
    heap_drop = None
    if observations.heap_baseline_free is not None and observations.heap_final_free is not None:
        heap_drop = observations.heap_baseline_free - observations.heap_final_free
    return {
        "arduino_core_version": observations.arduino_core_version,
        "esp_idf_version": observations.esp_idf_version,
        "variant": observations.variant,
        "manufacturer_id": observations.manufacturer_id,
        "manufacturer_id_hex": None if observations.manufacturer_id is None else f"0x{observations.manufacturer_id:03X}",
        "product_id": observations.product_id,
        "product_id_hex": None if observations.product_id is None else f"0x{observations.product_id:03X}",
        "capacity": observations.capacity,
        "i2c_timeout_ms": observations.i2c_timeout_ms,
        "max_tx_bytes": observations.max_tx_bytes,
        "max_rx_bytes": observations.max_rx_bytes,
        "max_write_data_bytes": observations.max_write_data_bytes,
        "max_read_data_bytes": observations.max_read_data_bytes,
        "final_state": observations.final_state,
        "final_initialized": observations.final_initialized,
        "final_consecutive_failures": observations.final_consecutive_failures,
        "final_total_failures": observations.final_total_failures,
        "heap_baseline_free": observations.heap_baseline_free,
        "heap_final_free": observations.heap_final_free,
        "heap_drop_bytes": heap_drop,
        "heap_min_free_observed": observations.heap_min_free_observed,
        "heap_final_largest": observations.heap_final_largest,
        "target_reset_count": observations.target_reset_count,
        "serial_reconnect_count": observations.serial_reconnect_count,
        "serial_framing_sync_count": observations.serial_framing_sync_count,
    }


def strict_failure_reasons(
    args: argparse.Namespace,
    counts: dict[str, int],
    soak: SoakSummary,
    observations: Observations,
) -> list[str]:
    if not args.strict:
        return []

    reasons: list[str] = []
    if counts.get("FAIL", 0) > 0:
        reasons.append(f"functional FAIL count is {counts['FAIL']}")
    if counts.get("UNKNOWN", 0) > 0:
        reasons.append(f"functional UNKNOWN count is {counts['UNKNOWN']}")

    if soak.status != "NOT RUN":
        if soak.fail_count > 0:
            reasons.append(f"soak FAIL count is {soak.fail_count}")
        if soak.unknown_count > 0:
            reasons.append(f"soak UNKNOWN count is {soak.unknown_count}")
        if soak.status != "PASS":
            reasons.append(f"soak status is {soak.status}")

    required_frameworks = (
        ("Arduino-ESP32", args.require_arduino_version, observations.arduino_core_version),
        ("ESP-IDF", args.require_idf_version, observations.esp_idf_version),
    )
    for label, expected, observed in required_frameworks:
        if expected is not None and observed != expected:
            reasons.append(
                f"required {label} {expected}, observed {observed or 'unknown'}"
            )

    if args.require_variant is not None:
        expected = args.require_variant.upper()
        if observations.variant != expected:
            reasons.append(f"required variant {expected}, observed {observations.variant or 'unknown'}")

    if args.require_product_id is not None:
        if observations.product_id != args.require_product_id:
            observed = "unknown" if observations.product_id is None else f"0x{observations.product_id:03X}"
            reasons.append(f"required product ID 0x{args.require_product_id:03X}, observed {observed}")

    if args.require_capacity is not None:
        if observations.capacity != args.require_capacity:
            reasons.append(f"required capacity {args.require_capacity}, observed {observations.capacity or 'unknown'}")

    required_transport = (
        ("I2C timeout", args.require_timeout_ms, observations.i2c_timeout_ms),
        ("max write data", args.require_max_write_data, observations.max_write_data_bytes),
        ("max read data", args.require_max_read_data, observations.max_read_data_bytes),
    )
    for label, expected, observed in required_transport:
        if expected is not None and observed != expected:
            reasons.append(
                f"required {label} {expected}, observed "
                f"{'unknown' if observed is None else observed}"
            )

    if observations.final_state != "READY":
        reasons.append(f"final driver state is {observations.final_state or 'unknown'}")
    if observations.final_consecutive_failures != 0:
        observed = "unknown" if observations.final_consecutive_failures is None else str(observations.final_consecutive_failures)
        reasons.append(f"final consecutive failures is {observed}")
    if observations.final_total_failures != 0:
        observed = "unknown" if observations.final_total_failures is None else str(observations.final_total_failures)
        reasons.append(f"final total failures is {observed}")

    if observations.serial_reconnect_count != 0:
        reasons.append(f"serial reconnect count is {observations.serial_reconnect_count}")
    if observations.target_reset_count != 0:
        reasons.append(f"target reset count is {observations.target_reset_count}")

    if args.heap_max_drop_bytes is not None:
        if observations.heap_baseline_free is None or observations.heap_final_free is None:
            reasons.append("heap threshold requested but heap telemetry was not observed")
        else:
            drop = observations.heap_baseline_free - observations.heap_final_free
            if drop > args.heap_max_drop_bytes:
                reasons.append(f"heap free drop {drop} bytes exceeds {args.heap_max_drop_bytes}")

    if args.heap_min_free_bytes is not None:
        if observations.heap_min_free_observed is None:
            reasons.append("heap minimum threshold requested but heap telemetry was not observed")
        elif observations.heap_min_free_observed < args.heap_min_free_bytes:
            reasons.append(
                f"heap min_free {observations.heap_min_free_observed} is below {args.heap_min_free_bytes}"
            )

    return reasons


def parser_self_test() -> int:
    samples = [
        (
            CommandStep("T1", "parser", "probe", expected_any=(("Status:", "OK"),)),
            "\x1b[32m  Status: OK\x1b[0m (code=0)\n> ",
            "PASS",
        ),
        (
            CommandStep("T2", "parser", "rw_suite", expected_any=(("Read/write suite result:",),)),
            "Read/write suite result: pass=8 fail=1\n> ",
            "FAIL",
        ),
        (
            CommandStep("T3", "parser", "bad", expected_any=(("Unknown command",),), fail_tokens=(), fail_patterns=()),
            "Unknown command: bad\n> ",
            "PASS",
        ),
        (
            CommandStep("T4", "parser", "id", expected_any=(("Device ID:",),)),
            "Status: I2C_TIMEOUT (code=15)\n> ",
            "FAIL",
        ),
        (
            CommandStep("T5", "parser", "heap", expected_all=("heap:", "free=", "min_free=", "largest=")),
            "heap: free=240000 min_free=230000 largest=120000\n> ",
            "PASS",
        ),
        (
            CommandStep("T6", "parser", "recover", expected_any=(("Status:", "OK"),)),
            "  Status: BUSY (code=11, detail=9)\n> ",
            "FAIL",
        ),
    ]
    ok = True
    for step, output, expected in samples:
        result = classify(step, output, 0.1)
        if result.status != expected:
            ok = False
            print(f"parser self-test failed for {step.test_id}: got {result.status}, expected {expected}")
    incomplete = classify(
        CommandStep("T7", "parser", "scan", expected_any=(("I2C scan:",),)),
        "I2C scan:\n  0x50",
        5.0,
        prompt_detected=False,
        completed_within_deadline=False,
    )
    if incomplete.status != "UNKNOWN":
        ok = False
        print("parser self-test failed for incomplete prompt framing")
    observations = Observations()
    update_observations(
        observations,
        "Device ID: Manufacturer=0x00A Product=0x510 Density=0x05\n"
        "  Variant: MB85RC256V (32768 bytes)\n"
        "  State: READY\n"
        "  Consecutive failures: 0\n"
        "  Total failures: 0\n"
        "  I2C timeout: 5 ms\n"
        "  Transport capacity: TX=126 RX=124 bytes\n"
        "  One-transaction data: write=124 read=124 bytes\n"
        "  Arduino-ESP32: 3.3.11\n"
        "  ESP-IDF: v5.5.5\n"
        "heap: free=240000 min_free=230000 largest=120000\n> ",
        count_resets=True,
    )
    if observations.variant != "MB85RC256V" or observations.product_id != 0x510:
        ok = False
        print("parser self-test failed for observations: identity not parsed")
    if (observations.arduino_core_version != "3.3.11" or
            observations.esp_idf_version != "v5.5.5"):
        ok = False
        print("parser self-test failed for observations: framework versions not parsed")
    if observations.capacity != 32768 or observations.final_state != "READY":
        ok = False
        print("parser self-test failed for observations: capacity/health not parsed")
    if observations.heap_baseline_free != 240000 or observations.heap_min_free_observed != 230000:
        ok = False
        print("parser self-test failed for observations: heap not parsed")
    if (observations.i2c_timeout_ms != 5 or observations.max_tx_bytes != 126 or
            observations.max_rx_bytes != 124 or observations.max_write_data_bytes != 124 or
            observations.max_read_data_bytes != 124):
        ok = False
        print("parser self-test failed for observations: transport envelope not parsed")
    update_observations(
        observations,
        "Known MB85RC family variants:\n"
        "MB85RC04V 512 bytes product=0x010 density=0x0\n"
        "MB85RC256V 32768 bytes product=0x510 density=0x5\n"
        "> ",
        count_resets=True,
    )
    if observations.product_id != 0x510:
        ok = False
        print("parser self-test failed for observations: variant catalog overwrote active product")
    if count_target_resets("rst:0x1 (POWERON_RESET),boot:0x8\n> ") != 1:
        ok = False
        print("parser self-test failed for reset detection")

    gate_args = argparse.Namespace(
        strict=True,
        require_arduino_version="3.3.11",
        require_idf_version="v5.5.5",
        require_variant=None,
        require_product_id=None,
        require_capacity=None,
        require_timeout_ms=None,
        require_max_write_data=None,
        require_max_read_data=None,
        heap_max_drop_bytes=None,
        heap_min_free_bytes=None,
    )
    gate_counts = {"PASS": 1, "FAIL": 0, "UNKNOWN": 0}
    if strict_failure_reasons(gate_args, gate_counts, SoakSummary(), observations):
        ok = False
        print("parser self-test failed for matching framework strict gates")

    gate_args.require_arduino_version = "3.3.10"
    gate_args.require_idf_version = "v5.5.4"
    mismatch_reasons = strict_failure_reasons(
        gate_args, gate_counts, SoakSummary(), observations
    )
    if not any("required Arduino-ESP32 3.3.10" in reason for reason in mismatch_reasons):
        ok = False
        print("parser self-test failed for mismatched Arduino framework gate")
    if not any("required ESP-IDF v5.5.4" in reason for reason in mismatch_reasons):
        ok = False
        print("parser self-test failed for mismatched ESP-IDF framework gate")

    missing_observations = Observations(
        final_state="READY",
        final_consecutive_failures=0,
        final_total_failures=0,
    )
    missing_reasons = strict_failure_reasons(
        gate_args, gate_counts, SoakSummary(), missing_observations
    )
    if not any("required Arduino-ESP32" in reason and "unknown" in reason
               for reason in missing_reasons):
        ok = False
        print("parser self-test failed for missing Arduino framework telemetry")
    if not any("required ESP-IDF" in reason and "unknown" in reason
               for reason in missing_reasons):
        ok = False
        print("parser self-test failed for missing ESP-IDF framework telemetry")
    if ok:
        print("HIL parser self-test PASSED")
        return 0
    return 1


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Bounded serial HIL runner for the MB85RC CLI examples.")
    parser.add_argument("--port", help="serial port for a dry-run plan or hardware run")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout-s", type=float, default=5.0)
    parser.add_argument("--idle-timeout-s", type=float, default=0.25)
    parser.add_argument("--boot-settle-s", type=float, default=2.0)
    parser.add_argument("--profile", choices=("auto", "arduino", "idf"), default="auto")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--parser-self-test", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--sample-count", type=int, default=50)
    parser.add_argument(
        "--include-stress",
        "--include-destructive-stress",
        dest="include_stress",
        action="store_true",
        help="include temporarily mutating, restore-safe stress diagnostics",
    )
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--require-variant")
    parser.add_argument("--require-arduino-version")
    parser.add_argument("--require-idf-version")
    parser.add_argument("--require-product-id", type=lambda value: int(value, 0))
    parser.add_argument("--require-capacity", type=int)
    parser.add_argument("--require-timeout-ms", type=int)
    parser.add_argument("--require-max-write-data", type=int)
    parser.add_argument("--require-max-read-data", type=int)
    parser.add_argument("--heap-max-drop-bytes", type=int)
    parser.add_argument("--heap-min-free-bytes", type=int)
    parser.add_argument("--soak-duration-s", type=float, default=0.0)
    parser.add_argument("--soak-pacing-s", type=float, default=0.1)
    parser.add_argument("--soak-max-consecutive-failures", type=int, default=3)
    parser.add_argument("--transcript-path", type=Path)
    parser.add_argument("--json-path", type=Path)
    parser.add_argument("--markdown-path", type=Path)
    return parser.parse_args(argv)


def default_artifact_paths(port: str) -> tuple[Path, Path, Path]:
    date = datetime.now().strftime("%Y%m%d")
    safe_port = port.replace(":", "").replace("\\", "").replace("/", "")
    root = ROOT / "docs" / "reports"
    stem = f"hil-runner-{safe_port}-{date}"
    return (
        root / f"{stem}-transcript.txt",
        root / f"{stem}.json",
        root / f"{stem}.md",
    )


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.parser_self_test:
        return parser_self_test()
    if not args.port:
        print("HIL runner requires --port unless --parser-self-test is used")
        return 2

    transcript_path, json_path, markdown_path = default_artifact_paths(args.port)
    transcript_path = args.transcript_path or transcript_path
    json_path = args.json_path or json_path
    markdown_path = args.markdown_path or markdown_path

    dry_profile = "arduino" if args.profile == "auto" else args.profile
    dry_steps = make_functional_steps(dry_profile, args.sample_count, args.include_stress)
    if args.dry_run:
        print("HIL dry run command plan:")
        for step in dry_steps:
            print(f"{step.test_id}: {step.command}")
        if args.soak_duration_s > 0:
            print("Soak command cycle:")
            for step in make_soak_steps(dry_profile, args.sample_count):
                print(f"{step.test_id}: {step.command}")
        print("Final command checks:")
        for step in make_final_steps():
            print(f"{step.test_id}: {step.command}")
        return 0

    observations = Observations()
    try:
        session = SerialSession(
            port=args.port,
            baud=args.baud,
            timeout_s=args.timeout_s,
            idle_timeout_s=args.idle_timeout_s,
            transcript_path=transcript_path,
            verbose=args.verbose,
        )
    except Exception as exc:
        print(f"HIL runner failed to open {args.port}: {exc}")
        return 2
    try:
        if args.reset:
            session.reset()
        boot = session.wait_for_prompt(args.boot_settle_s, args.timeout_s)
        if not PROMPT_RE.search(normalize_output(boot)):
            print("HIL runner failed: prompt not detected during boot")
            return 2
        # Reconnects caused by an explicitly requested reset are part of boot,
        # not runtime stability observations used by the strict gate.
        session.reconnect_count = 0
        session.framing_sync_count = 0
        update_observations(observations, boot, count_resets=False)
        profile = detect_profile(args.profile, boot)
        steps = make_functional_steps(profile, args.sample_count, args.include_stress)
        results = run_steps(session, steps, args.timeout_s, observations)
        if session.framing_lost:
            # Never enqueue more device commands after an incomplete response.
            # A requested soak is a failed run; without a requested soak this
            # remains an ordinary functional failure/UNKNOWN result.
            soak = SoakSummary(status="FAIL" if args.soak_duration_s > 0 else "NOT RUN")
            soak_results = []
        else:
            soak, soak_results = run_soak(
                session,
                profile,
                args.soak_duration_s,
                args.sample_count,
                args.soak_pacing_s,
                args.timeout_s,
                args.soak_max_consecutive_failures,
                observations,
            )
        if not session.framing_lost:
            final_results = run_steps(session, make_final_steps(), args.timeout_s, observations)
            results.extend(final_results)
    except (session.serial_mod.SerialException, OSError) as exc:
        print(f"HIL runner serial failure: {exc}")
        return 2
    finally:
        session.close()

    counts = count_results(results)
    strict_reasons = strict_failure_reasons(args, counts, soak, observations)
    data = {
        "generated": datetime.now().astimezone().isoformat(timespec="seconds"),
        "port": args.port,
        "baud": args.baud,
        "profile": profile,
        "strict": args.strict,
        "strict_gate": "PASS" if not strict_reasons else "FAIL",
        "strict_failures": strict_reasons,
        "requirements": {
            "arduino_core_version": args.require_arduino_version,
            "esp_idf_version": args.require_idf_version,
            "variant": args.require_variant,
            "product_id": None if args.require_product_id is None else f"0x{args.require_product_id:03X}",
            "capacity": args.require_capacity,
            "timeout_ms": args.require_timeout_ms,
            "max_write_data": args.require_max_write_data,
            "max_read_data": args.require_max_read_data,
            "heap_max_drop_bytes": args.heap_max_drop_bytes,
            "heap_min_free_bytes": args.heap_min_free_bytes,
        },
        "timeout_s": args.timeout_s,
        "idle_timeout_s": args.idle_timeout_s,
        "boot_prompt_detected": True,
        "functional_results": [result_to_dict(r) for r in results],
        "soak_summary": soak_to_dict(soak),
        "soak_results": [result_to_dict(r) for r in soak_results],
        "observations": observations_to_dict(observations),
        "transcript_path": str(transcript_path),
    }
    write_json(json_path, data)
    write_markdown(
        markdown_path,
        args.port,
        args.baud,
        profile,
        results,
        soak,
        transcript_path,
        json_path,
        observations,
        strict_reasons,
    )

    print(f"HIL functional summary: PASS={counts['PASS']} FAIL={counts['FAIL']} UNKNOWN={counts['UNKNOWN']}")
    print(f"HIL soak summary: {soak.status} duration={soak.duration_s:.1f}s pass={soak.pass_count} fail={soak.fail_count} unknown={soak.unknown_count}")
    print(f"HIL strict gate: {'PASS' if not strict_reasons else 'FAIL'}")
    for reason in strict_reasons:
        print(f"  - {reason}")
    print(f"Transcript: {transcript_path}")
    print(f"JSON: {json_path}")
    print(f"Markdown: {markdown_path}")
    return 1 if counts["FAIL"] > 0 or soak.status in {"FAIL", "UNKNOWN"} or strict_reasons else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
