#!/usr/bin/env python3
# run_gdscript_suites.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Run every GDScript suite in project/tests/ and fail when one did not run.

SceneTree quits with status 0 when a script fails to parse, so a suite whose
first line is a syntax error prints "SCRIPT ERROR: Parse Error" and still looks
like a pass. Exit codes alone therefore cannot tell "the suite passed" from "the
suite never ran", and every assertion in an unparseable suite is skipped while
CI stays green.

This runner closes that gap for every suite at once:

  * it discovers project/tests/*_test.gd itself, so a newly added suite runs
    without anyone remembering to wire it into CI;
  * it requires the suite to print the shared sentinel from
    project/tests/suite_guard.gd, which only executed code can produce, so a
    suite that fails to parse fails the job;
  * it rejects any output carrying an engine script error, because a runtime
    script error has the same silently-green shape as a parse error.

Usage:
    python3 tests/run_gdscript_suites.py --godot /path/to/godot [--project project]
    python3 tests/run_gdscript_suites.py --list
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "tests" / "gdscript_suites.json"
SUITE_GLOB = "*_test.gd"

# Printed by project/tests/suite_guard.gd. A suite that never loaded cannot
# print it, which is the whole point of the guard.
SUCCESS_PREFIX = "BS_SUITE_OK"

# Engine diagnostics that mean the script did not run, or ran and broke, while
# the process still exits 0.
FORBIDDEN_OUTPUT_PATTERNS = (
    "SCRIPT ERROR:",
    "Parse Error:",
    "Failed to load script",
    "Script does not inherit from SceneTree",
)


class Invocation:
    """One `godot --script` run, together with the evidence that it ran."""

    def __init__(self, name: str, script: str, args: list[str], expect: str) -> None:
        self.name = name
        self.script = script
        self.args = args
        self.expect = expect

    def command(self, godot: str, project: str) -> list[str]:
        command = [godot, "--headless", "--path", project, "--script", self.script]
        if self.args:
            command.append("--")
            command.extend(self.args)
        return command

    def describe(self) -> str:
        return f"{self.name} [{self.script}{' ' + ' '.join(self.args) if self.args else ''}]"


def default_expectation(stem: str) -> str:
    return rf"^{re.escape(SUCCESS_PREFIX)} {re.escape(stem)}(\s|$)"


def discover_suites(project_tests: Path) -> list[str]:
    return sorted(path.name for path in project_tests.glob(SUITE_GLOB))


def build_invocations(project_tests: Path, manifest: dict) -> list[Invocation]:
    """Discovered suites first, then the manifest's extra invocations.

    A discovered suite needs no manifest entry: it runs with the default
    expectation, so omitting the shared guard is an error rather than a silent
    pass. The manifest exists only for scripts that are not named *_test.gd and
    for suites that need arguments.
    """
    overrides = manifest.get("overrides", {})
    discovered = discover_suites(project_tests)

    stale = sorted(set(overrides) - set(discovered))
    if stale:
        raise SystemExit(
            "tests/gdscript_suites.json overrides suites that do not exist: " + ", ".join(stale)
        )

    invocations: list[Invocation] = []
    for filename in discovered:
        stem = Path(filename).stem
        override = overrides.get(filename, {})
        invocations.append(
            Invocation(
                name=stem,
                script=f"res://tests/{filename}",
                args=list(override.get("args", [])),
                expect=override.get("expect", default_expectation(stem)),
            )
        )

    for entry in manifest.get("extra_invocations", []):
        for field in ("name", "script", "expect"):
            if field not in entry:
                raise SystemExit(f"tests/gdscript_suites.json entry is missing '{field}': {entry}")
        invocations.append(
            Invocation(
                name=entry["name"],
                script=entry["script"],
                args=list(entry.get("args", [])),
                expect=entry["expect"],
            )
        )
    return invocations


def evaluate(invocation: Invocation, returncode: int, output: str) -> list[str]:
    """Return the reasons this run is not acceptable evidence of a pass."""
    failures: list[str] = []
    if returncode != 0:
        failures.append(f"exited with status {returncode}")
    for pattern in FORBIDDEN_OUTPUT_PATTERNS:
        if pattern in output:
            failures.append(
                f"output contains {pattern!r}; the suite did not run cleanly even though it "
                f"exited {returncode}"
            )
    if re.search(invocation.expect, output, re.MULTILINE) is None:
        failures.append(
            f"output never matched {invocation.expect!r}; a suite that fails to parse quits 0 "
            "without printing its sentinel"
        )
    return failures


def run(invocation: Invocation, godot: str, project: str) -> list[str]:
    command = invocation.command(godot, project)
    print(f"--- {invocation.describe()}", flush=True)
    completed = subprocess.run(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
    )
    sys.stdout.write(completed.stdout)
    sys.stdout.flush()
    return evaluate(invocation, completed.returncode, completed.stdout)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", help="path to the Godot binary")
    parser.add_argument("--project", default="project", help="Godot project directory")
    parser.add_argument(
        "--list", action="store_true", help="print the invocations and their guards, then exit"
    )
    arguments = parser.parse_args(argv)

    project_tests = ROOT / arguments.project / "tests"
    manifest = json.loads(MANIFEST_PATH.read_text())
    invocations = build_invocations(project_tests, manifest)

    if not invocations:
        print("no GDScript suites discovered; the runner would vacuously pass")
        return 1

    if arguments.list:
        for invocation in invocations:
            print(f"{invocation.describe()} -> {invocation.expect}")
        return 0

    if not arguments.godot:
        parser.error("--godot is required unless --list is given")

    failed: list[str] = []
    for invocation in invocations:
        reasons = run(invocation, arguments.godot, arguments.project)
        if reasons:
            failed.append(invocation.describe())
            for reason in reasons:
                print(f"FAIL {invocation.describe()}: {reason}")
        else:
            print(f"PASS {invocation.describe()}")

    print()
    if failed:
        print(f"{len(failed)} of {len(invocations)} GDScript suite runs failed:")
        for description in failed:
            print(f"  {description}")
        return 1
    print(f"all {len(invocations)} GDScript suite runs printed their guard sentinel")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
