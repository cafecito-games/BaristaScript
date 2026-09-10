#!/usr/bin/env python3
# verify_native_surface.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Fail if native tests or retired adapters occur in an ordinary artifact."""
import argparse
from pathlib import Path
from verify_parse_cache_surface import find_artifacts

NATIVE_MARKERS = (b"BaristaNativeTestRunner", b"BS_NATIVE_RESULT", b"[doctest]", b"token_vocabulary_is_closed",
                  b"native-storage-", b"storage assertion failure restores state")
RETIRED_ADAPTER_MARKERS = (b"BaristaScriptGlobalClassProbe",)


def verify_surface(binary_dir, target_type):
    artifacts = find_artifacts(binary_dir, target_type)
    if not artifacts:
        return [f"no {target_type} artifacts found under {binary_dir}"]
    failures = []
    for artifact in artifacts:
        data = artifact.read_bytes()
        for marker in NATIVE_MARKERS + RETIRED_ADAPTER_MARKERS:
            if marker in data:
                failures.append(f"test-only or retired marker {marker!r} leaked into {artifact}")
    return failures


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary-dir", type=Path, required=True)
    parser.add_argument("--target-type", choices=("template_debug", "template_release"), required=True)
    args = parser.parse_args()
    failures = verify_surface(args.binary_dir, args.target_type)
    for failure in failures:
        print("FAIL:", failure)
    if not failures:
        print(f"native tests and retired adapters absent from ordinary {args.target_type} artifacts")
    raise SystemExit(bool(failures))
