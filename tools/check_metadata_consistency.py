#!/usr/bin/env python3
"""Check release/package metadata that should stay in sync."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+$")


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def fail(message: str) -> None:
    print(f"metadata check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def require_contains(path: str, needle: str) -> None:
    if needle not in read(path):
        fail(f"{path} does not contain {needle!r}")


def main() -> int:
    library = json.loads(read("library.json"))
    version = str(library.get("version", ""))
    if not SEMVER_RE.match(version):
        fail(f"library.json version is not SemVer: {version!r}")

    idf_component = read("idf_component.yml")
    require_contains("idf_component.yml", f'version: "{version}"')
    require_contains("include/MB85RC/Version.h", f'#define MB85RC_VERSION_STRING "{version}"')
    require_contains("Doxyfile", f'PROJECT_NUMBER         = "{version}"')
    require_contains("README.md", f"Released library version: `v{version}`")

    frameworks = set(library.get("frameworks", []))
    if frameworks != {"arduino", "espidf"}:
        fail(f"library.json frameworks changed unexpectedly: {sorted(frameworks)!r}")

    platforms = set(library.get("platforms", []))
    if platforms != {"espressif32"}:
        fail(f"library.json platforms changed unexpectedly: {sorted(platforms)!r}")

    if "esp32s2" not in idf_component or "esp32s3" not in idf_component:
        fail("idf_component.yml must list esp32s2 and esp32s3 targets")
    if 'idf: ">=6.0.1"' not in idf_component:
        fail("idf_component.yml must keep the documented ESP-IDF >=6.0.1 floor")

    print("Metadata consistency check PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
