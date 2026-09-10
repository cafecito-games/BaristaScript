#!/usr/bin/env python3
# test_native_cmake_rebuild.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Exercise incremental CMake identity/profile updates; restore all edited source bytes."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

import run_native_suites as runner


def verify(args):
    subprocess.run(["cmake", "--build", str(args.build_dir), "--parallel", str(args.jobs)], check=True)
    if runner.main(["--godot", args.godot, "--build-dir", str(args.build_dir)]):
        raise RuntimeError("rebuilt CMake artifact did not pass native supervision")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", required=True)
    parser.add_argument("--build-dir", type=Path, default=runner.ROOT / "build/native-cmake")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    # Require a completed native build for this source tree before editing any input.
    runner.read_artifact(args.build_dir)
    cache = (args.build_dir / "CMakeCache.txt").read_text()
    if f"CMAKE_HOME_DIRECTORY:INTERNAL={runner.ROOT}\n" not in cache or "BARISTA_TESTS:BOOL=ON\n" not in cache:
        parser.error("--build-dir must be this source tree's test-enabled CMake build")

    xml = runner.ROOT / "doc_classes/BaristaScript.xml"
    sconstruct = runner.ROOT / "SConstruct"
    profile_path = runner.ROOT / "build_profile.json"
    originals = {path: path.read_bytes() for path in (xml, sconstruct, profile_path)}
    profile = json.loads(originals[profile_path])
    # A normally implicit parent class changes the derived inventory without adding APIs.
    added_class = next(name for name in ("RefCounted", "Object", "MainLoop")
                       if name not in profile["enabled_classes"])
    profile["enabled_classes"].append(added_class)
    changes = ((xml, originals[xml] + b"\n<!-- native incremental identity regression -->\n"),
               (sconstruct, originals[sconstruct] + b"\n# Native incremental identity regression.\n"),
               (profile_path, (json.dumps(profile, indent=2) + "\n").encode()))
    failure = None
    try:
        for path, content in changes:
            path.write_bytes(content)
            print(f"Checking incremental rebuild after {path.relative_to(runner.ROOT)} edit", flush=True)
            verify(args)
            if path == profile_path:
                derived = json.loads((args.build_dir / "native-build-profile.json").read_text())
                if added_class not in derived["enabled_classes"]:
                    raise RuntimeError("CMake did not regenerate the derived native bindings profile")
    except BaseException as error:
        failure = error
    finally:
        for path, content in originals.items():
            path.write_bytes(content)
        try:
            print("Checking rebuilt artifact after restoring exact source bytes", flush=True)
            verify(args)
        except BaseException as cleanup_error:
            if failure is None:
                failure = cleanup_error
            else:
                print(f"Restored source; additional rebuild failure: {cleanup_error}", file=sys.stderr)
    if failure is not None:
        raise failure
    print("PASS native CMake incremental XML, build-script and profile changes; sources restored")


if __name__ == "__main__":
    main()
