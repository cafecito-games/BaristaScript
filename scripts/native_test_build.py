#!/usr/bin/env python3
# native_test_build.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Shared native build identity and artifact recording for SCons and CMake."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def build_identity():
    inputs = sorted({*ROOT.glob("src/*.h"), *ROOT.glob("src/*.cpp"),
                     *ROOT.glob("tests/native/*"), *ROOT.glob("thirdparty/doctest/*"), *ROOT.glob("doc_classes/*.xml"),
                     ROOT / "SConstruct", ROOT / "CMakeLists.txt", ROOT / "build_profile.json", Path(__file__).resolve(),
                     ROOT / "scripts/generate_global_api.py", ROOT / ".gitmodules",
                     ROOT / "godot-cpp/gdextension/extension_api-4-7.json"})
    digest = hashlib.sha256()
    for path in inputs:
        if path.is_file():
            digest.update(str(path.relative_to(ROOT)).encode() + b"\0" + path.read_bytes())
    digest.update(subprocess.check_output(["git", "-C", str(ROOT / "godot-cpp"), "rev-parse", "HEAD"]))
    return digest.hexdigest()


def write_header(destination):
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    content = '#pragma once\n#define BS_NATIVE_BUILD_ID "' + build_identity() + '"\n'
    if not destination.exists() or destination.read_text() != content:
        destination.write_text(content)


def write_profile(destination):
    profile = json.loads((ROOT / "build_profile.json").read_text())
    profile["enabled_classes"] = sorted(set(profile["enabled_classes"]) | {"SceneTree", "ConfigFile"})
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    content = json.dumps(profile, indent=2) + "\n"
    if not destination.exists() or destination.read_text() != content:
        destination.write_text(content)


def record(library, header, destination):
    library = Path(library).resolve()
    build_id = Path(header).read_text().split('"')[1]
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(dict(library=str(library), build_id=build_id,
                                           sha256=hashlib.sha256(library.read_bytes()).hexdigest()), indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--profile", type=Path)
    parser.add_argument("--library", type=Path)
    parser.add_argument("--artifact", type=Path)
    args = parser.parse_args()
    if args.profile:
        write_profile(args.profile)
    elif args.library:
        record(args.library, args.header, args.artifact)
    else:
        write_header(args.header)
