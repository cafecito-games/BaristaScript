#!/usr/bin/env python3
# test_runtime_scene.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Supervise the committed runtime fixture under a stock Godot binary.

The fixture is a real Godot project: one scene, one Node, one `.barista` script. Its assertions are
written in BaristaScript and its exit code is the number of failed checks, so this file asserts
nothing about the language itself -- it stages a disposable copy of the project with the built
extension, runs stock Godot on it, and requires exit 0 together with the exact sentinel the script
prints only when every check passed. A run that exits 0 without the sentinel is a failure: an
engine that never reached the script would also exit 0.

The staged copy is disposable and the committed fixture is never written to, so a failed run leaves
the repository exactly as it found it.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from build_config import load_config  # noqa: E402

FIXTURE = ROOT / "tests/runtime_scene_project"
SENTINEL = "BS_RUNTIME_SCENE_OK"
FAILURE_PREFIX = "BS_RUNTIME_SCENE_FAIL"

PLATFORM_DIRECTORIES = {"darwin": "macos", "linux": "linux", "win32": "windows"}


def platform_directory() -> str:
    for prefix, name in PLATFORM_DIRECTORIES.items():
        if sys.platform.startswith(prefix):
            return name
    raise SystemExit(f"unsupported platform {sys.platform!r}")


def stage(binary_directory: Path) -> Path:
    """Copy the fixture and the built extension into a fresh temporary project."""
    staged = Path(tempfile.mkdtemp(prefix="barista-runtime-scene-"))
    project = staged / "project"
    shutil.copytree(FIXTURE, project)
    (project / "bin").mkdir()
    shutil.copy2(ROOT / "project/bin/barista_script.gdextension", project / "bin")
    shutil.copytree(binary_directory, project / "bin" / binary_directory.name)
    (project / ".godot").mkdir()
    shutil.copy2(ROOT / "project/.godot/extension_list.cfg", project / ".godot/extension_list.cfg")
    return staged


def run(godot: Path, project: Path, timeout: float) -> subprocess.CompletedProcess:
    command = [str(godot), "--headless", "--path", str(project), "main.tscn"]
    try:
        return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        return subprocess.CompletedProcess(command, 124, output + f"\ntimeout after {timeout}s\n")
    except OSError as error:
        return subprocess.CompletedProcess(command, 127, str(error))


def evaluate(completed: subprocess.CompletedProcess) -> list[str]:
    failures = []
    if completed.returncode != 0:
        failures.append(f"the fixture exited {completed.returncode}")
    lines = completed.stdout.splitlines()
    if lines.count(SENTINEL) != 1:
        failures.append(f"expected exactly one {SENTINEL} line, found {lines.count(SENTINEL)}")
    for line in lines:
        if line.startswith(FAILURE_PREFIX):
            failures.append(line)
        if line.startswith("SCRIPT ERROR"):
            failures.append(line)
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", required=True, help="explicit stock Godot executable")
    parser.add_argument("--binary-dir", type=Path,
                        help="the built extension's platform directory (default: project/bin/<platform>)")
    parser.add_argument("--timeout", type=float, default=120)
    arguments = parser.parse_args()

    load_config()
    binary_directory = arguments.binary_dir or (ROOT / "project/bin" / platform_directory())
    if not binary_directory.is_dir() or not any(binary_directory.iterdir()):
        print(f"FAIL runtime_scene: no built extension in {binary_directory}")
        return 1

    staged = stage(binary_directory)
    try:
        completed = run(Path(arguments.godot), staged / "project", arguments.timeout)
        failures = evaluate(completed)
    finally:
        shutil.rmtree(staged, ignore_errors=True)

    if failures:
        print(completed.stdout)
        for failure in failures:
            print(f"FAIL runtime_scene: {failure}")
        return 1
    print("PASS runtime_scene")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
