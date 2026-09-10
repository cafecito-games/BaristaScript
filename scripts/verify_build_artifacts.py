#!/usr/bin/env python3
# verify_build_artifacts.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Gate every distribution library in an upload tree against the checkout actually built."""

import argparse
import hashlib
import sys
from pathlib import Path

from build_config import ROOT, canonical_json, load_config
from build_metadata import create_metadata, inspect_artifact_bytes, verify_identity

LIBRARY_SUFFIXES = {".so", ".dylib", ".dll", ".wasm"}


def verify_artifacts(binary_dir, root, config, selection):
    binary_dir = Path(binary_dir)
    artifacts = sorted(path for path in binary_dir.rglob("*") if path.is_file() and path.suffix.lower() in LIBRARY_SUFFIXES)
    if not artifacts:
        raise ValueError(f"no distribution library artifacts found under {binary_dir}")
    expected = create_metadata(root, config, selection, native_tests=False)
    records = []
    for path in artifacts:
        try:
            content = path.read_bytes()
            actual = inspect_artifact_bytes(content)
            verify_identity(actual, expected, release=True)
        except (OSError, ValueError) as error:
            raise ValueError(f"{path}: {error}") from error
        records.append(dict(artifact=str(path.relative_to(binary_dir)), sha256=hashlib.sha256(content).hexdigest(), build_info=actual))
    return records


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--binary-dir", type=Path, required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--architecture", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--api", required=True)
    parser.add_argument("--precision", required=True)
    args = parser.parse_args(argv)
    try:
        config = load_config(args.config or args.root / "build_versions.json")
        selection = {key: getattr(args, key) for key in ("platform", "architecture", "target", "api", "precision")}
        print(canonical_json({"artifacts": verify_artifacts(args.binary_dir, args.root, config, selection)}))
        return 0
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
