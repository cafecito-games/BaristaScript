#!/usr/bin/env python3
# verify_parse_cache_surface.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Verify that the GDScript parse-cache adapter exists only in debug artifacts."""

import argparse
import sys
from pathlib import Path


ADAPTER_MARKER = b"BaristaScriptParseCache"
TARGET_TYPES = ("template_debug", "template_release")


def find_artifacts(binary_dir: Path, target_type: str) -> list[Path]:
    """Return regular build artifacts whose filename identifies the requested target."""
    return sorted(
        path
        for path in binary_dir.rglob("*")
        if path.is_file() and target_type in path.name
    )


def verify_surface(binary_dir: Path, target_type: str) -> list[str]:
    """Return policy failures for the requested target's compiled artifacts."""
    artifacts = find_artifacts(binary_dir, target_type)
    if not artifacts:
        return [f"no {target_type} artifacts found under {binary_dir}"]

    containing_marker = [path for path in artifacts if ADAPTER_MARKER in path.read_bytes()]
    if target_type == "template_debug" and not containing_marker:
        return [
            "BaristaScriptParseCache is missing from every debug artifact: "
            + ", ".join(str(path) for path in artifacts)
        ]
    if target_type == "template_release" and containing_marker:
        return [
            "BaristaScriptParseCache leaked into release artifact: "
            + ", ".join(str(path) for path in containing_marker)
        ]
    return []


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary-dir", required=True, type=Path)
    parser.add_argument("--target-type", required=True, choices=TARGET_TYPES)
    arguments = parser.parse_args()

    failures = verify_surface(arguments.binary_dir, arguments.target_type)
    if failures:
        for failure in failures:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1

    expectation = "present" if arguments.target_type == "template_debug" else "absent"
    print(
        f"parse-cache adapter marker is {expectation} as required in "
        f"{arguments.target_type} artifacts under {arguments.binary_dir}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
