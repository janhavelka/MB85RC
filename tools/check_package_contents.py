#!/usr/bin/env python3
"""Validate the public PlatformIO archive and its local Markdown links."""

from __future__ import annotations

import json
import posixpath
import re
import sys
import tarfile
from pathlib import Path
from urllib.parse import unquote, urlsplit


REQUIRED_FILES = {
    "library.json",
    "LICENSE",
    "README.md",
    "CHANGELOG.md",
    "CONTRIBUTING.md",
    "SECURITY.md",
    "CMakeLists.txt",
    "idf_component.yml",
    "src/MB85RC.cpp",
    "include/MB85RC/MB85RC.h",
    "include/MB85RC/Config.h",
    "include/MB85RC/Status.h",
    "include/MB85RC/CommandTable.h",
    "include/MB85RC/Version.h",
    "examples/01_basic_bringup_cli/main.cpp",
    "examples/espidf_basic/main/main.cpp",
    "examples/common/TypedMemory.h",
    "docs/DEVICE_REFERENCE.md",
    "docs/IDF_PORT.md",
    "docs/RELEASE_CHECKLIST.md",
    "docs/reports/HIL_SUMMARY.md",
}

FORBIDDEN_PREFIXES = (
    ".github/",
    ".pio/",
    ".vscode/",
    "test/",
    "tools/",
    "scripts/",
    "docs/reference-pdfs/",
    "docs/doxygen/",
)

FORBIDDEN_FILES = {
    ".gitignore",
    ".gitattributes",
    "CODEOWNERS",
    "Doxyfile",
    "platformio.ini",
}

MARKDOWN_LINK_RE = re.compile(r"\[[^\]]+\]\((?P<target>[^)]+)\)")


def fail(messages: list[str]) -> int:
    print("Package content check FAILED:", file=sys.stderr)
    for message in messages:
        print(f"- {message}", file=sys.stderr)
    return 1


def local_link_target(raw_target: str) -> str | None:
    target = raw_target.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    elif ' "' in target:
        target = target.split(' "', 1)[0]
    parsed = urlsplit(target)
    if parsed.scheme or parsed.netloc or not parsed.path:
        return None
    return unquote(parsed.path)


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print("Usage: tools/check_package_contents.py <archive.tar.gz>", file=sys.stderr)
        return 2

    archive = Path(argv[0])
    if not archive.is_file():
        return fail([f"archive not found: {archive}"])

    errors: list[str] = []
    with tarfile.open(archive, "r:gz") as package:
        members = {member.name.removeprefix("./"): member for member in package.getmembers()}
        names = set(members)

        for required in sorted(REQUIRED_FILES - names):
            errors.append(f"missing required file: {required}")
        for name in sorted(names & FORBIDDEN_FILES):
            errors.append(f"forbidden repository-only file: {name}")
        for name in sorted(names):
            if any(name.startswith(prefix) for prefix in FORBIDDEN_PREFIXES):
                errors.append(f"forbidden repository-only path: {name}")

        manifest_member = members.get("library.json")
        if manifest_member is not None:
            manifest_file = package.extractfile(manifest_member)
            if manifest_file is None:
                errors.append("library.json is not a regular archive file")
            else:
                manifest = json.loads(manifest_file.read().decode("utf-8"))
                version = str(manifest.get("version", ""))
                if not re.fullmatch(r"\d+\.\d+\.\d+", version):
                    errors.append(f"invalid packaged SemVer: {version!r}")

        for name, member in members.items():
            if not name.endswith(".md") or not member.isfile():
                continue
            extracted = package.extractfile(member)
            if extracted is None:
                continue
            text = extracted.read().decode("utf-8", errors="replace")
            for match in MARKDOWN_LINK_RE.finditer(text):
                target = local_link_target(match.group("target"))
                if target is None:
                    continue
                resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), target))
                prefix = resolved.rstrip("/") + "/"
                if resolved not in names and not any(candidate.startswith(prefix) for candidate in names):
                    errors.append(f"broken packaged Markdown link in {name}: {target}")

    if errors:
        return fail(errors)
    print(f"Package content check PASSED: {archive}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
