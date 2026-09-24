#!/usr/bin/env python3
# native_transition_scope.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Decide whether CI must re-verify the native build-system on/off/on transitions.

The transition steps rebuild the extension with native tests toggled on, off and on again
under SCons and CMake, and check that no test code reaches an ordinary artifact. What they
detect beyond the unconditional checks is stale state carried across a toggle: objects,
generated bindings, build profiles or identity headers that one mode leaves behind for the
other. Whether that can happen is decided by the build descriptions, not by the extension
sources they compile:

- Test code lives in `tests/native/` and `thirdparty/doctest/`, and only `SConstruct` and
  `CMakeLists.txt` add it to a build, keyed on their test option. `thirdparty/` stays an input
  whole, because vendored code is wired in by hand and carries build descriptions of its own.
  An ordinary source, header or document under `tests/native/` is not an input: both builds
  glob that directory -- `SConstruct` takes `Glob("tests/native/*.cpp")` and `CMakeLists.txt` a
  `CONFIGURE_DEPENDS` `file(GLOB ...)` over the same pattern -- so a case file that appears or
  disappears needs wiring in neither, and the `src/` asymmetry below has no counterpart here.
  That each build system did find it is proved unconditionally by the two steps that build and
  run the native suites with SCons and with CMake on every pull request. A build description
  placed in that directory still is an input: `SCsub`, `SConscript`, `CMakeLists.txt` and
  `*.cmake` are matched by name and suffix everywhere in the tree, so the directory itself does
  not have to be listed to catch one.
- A case file does feed the build identity `scripts/native_test_build.py` computes, but it feeds
  it identically in both modes, so it cannot be the stale state a toggle carries: that identity
  separates commits, not the tests-on and tests-off builds of a single commit.
- Where each mode puts its objects, profile and generated headers is decided by those two
  files, `methods.py`, an optional `custom.py`, godot-cpp's own SCons and CMake scripts (the
  `godot-cpp` submodule pointer and `.gitmodules`), the Python helpers the builds execute,
  and the two JSON build inputs.
- `src/` may hold `#ifdef BARISTA_TESTS` blocks, but a source edit changes only what an
  ordinary build compiles, and every CI matrix entry builds its ordinary artifact from
  scratch and runs `tests/verify_native_surface.py` on it before any transition step, so a
  leak a source edit introduces is caught without any transition.
- `CMakeLists.txt` lists every extension source by name while SCons globs `src/*.cpp`, so a
  file added to or removed from `src/` can break the CMake build alone. Such a change runs
  the full verification even though it touches nothing else.
- The verification tooling, this decision and the workflow itself are inputs too: changing
  any of them changes what the transition steps prove.

Only a pull request is ever scoped. Every other event runs the full verification, and any
failure to establish the pull request's changes does as well.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys
from typing import Callable

ROOT = Path(__file__).resolve().parents[1]

BUILD_INPUT_PATHS = frozenset({
    ".github/workflows/ci.yml",
    ".gitmodules",
    "SConstruct",
    "build_profile.json",
    "build_versions.json",
    "custom.py",
    "godot-cpp",
    "methods.py",
    "scripts/build_config.py",
    "scripts/build_metadata.py",
    "scripts/generate_global_api.py",
    "scripts/native_test_build.py",
    "scripts/native_transition_scope.py",
    "tests/native_suites.json",
    "tests/run_native_suites.py",
    "tests/test_cmake_api_inputs.py",
    "tests/test_native_cmake_rebuild.py",
    "tests/test_native_storage.py",
    "tests/test_run_native_suites.py",
    "tests/verify_native_surface.py",
    "tests/verify_parse_cache_surface.py",
})

BUILD_INPUT_DIRECTORIES = (
    ".github/actions/",
    "cmake/",
    "thirdparty/",
)

# Build-description file names, at the root or in any directory a build could add.
BUILD_INPUT_NAMES = frozenset({"SCsub", "SConscript", "CMakeLists.txt"})
BUILD_INPUT_SUFFIXES = (".cmake",)

SOURCE_DIRECTORY = "src/"

COMMIT = re.compile(r"[0-9a-f]{40}")


def is_build_input(path: str) -> bool:
    name = path.rsplit("/", 1)[-1]
    return (
        path in BUILD_INPUT_PATHS
        or path.startswith(BUILD_INPUT_DIRECTORIES)
        or name in BUILD_INPUT_NAMES
        or name.endswith(BUILD_INPUT_SUFFIXES)
    )


def triggering_changes(changes: list[tuple[str, str]]) -> list[str]:
    """The changed paths, as (status, path) pairs, that require the full verification."""
    return [
        path
        for status, path in changes
        if is_build_input(path) or (path.startswith(SOURCE_DIRECTORY) and status != "M")
    ]


def parse_name_status(output: str) -> list[tuple[str, str]]:
    """Parse `git diff --no-renames --name-status -z`, which alternates status and path."""
    fields = output.split("\0")
    if fields and fields[-1] == "":
        fields.pop()
    if len(fields) % 2:
        raise ValueError("unpaired git diff name-status output")
    return [(fields[index][:1], fields[index + 1]) for index in range(0, len(fields), 2)]


def run_git(*arguments: str) -> str:
    return subprocess.run(
        ["git", *arguments], cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout


def decide(
    event_name: str, pull_request_head: str, git: Callable[..., str] = run_git
) -> tuple[bool, str]:
    """Return whether the full verification is required, and why."""
    if event_name != "pull_request":
        return True, f"a {event_name or 'unnamed'} event always runs the full verification"
    if not COMMIT.fullmatch(pull_request_head):
        return True, "the pull request head commit is unknown, so its changes cannot be scoped"
    try:
        head = git("rev-parse", "HEAD").strip()
        # The pull request checkout is a shallow merge commit, so its parents must be fetched
        # before it can be compared with the base it was merged into.
        git("fetch", "--no-tags", "--no-recurse-submodules", "--depth=2", "origin", head)
        commits = git("rev-list", "--parents", "--max-count=1", head).split()
        if len(commits) != 3 or commits[2] != pull_request_head:
            return True, (
                "the checkout is not the merge of the pull request head into its base, "
                "so its changes cannot be scoped"
            )
        changes = parse_name_status(
            git("diff", "--no-renames", "--name-status", "-z", commits[1], head)
        )
    except Exception as error:
        return True, f"the pull request's changes could not be computed ({error})"
    triggers = triggering_changes(changes)
    if triggers:
        return True, "the pull request changes build inputs: " + ", ".join(triggers)
    return False, f"none of the pull request's {len(changes)} changed paths is a build input"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--event", required=True, help="the GitHub event name")
    parser.add_argument("--pull-request-head", default="", help="the pull request head commit")
    parser.add_argument("--github-output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        required, reason = decide(args.event, args.pull_request_head)
    except Exception as error:
        required, reason = True, f"the decision failed ({error})"
    print(("Running" if required else "Skipping") + f" the native build-system transitions: {reason}")
    with args.github_output.open("a", encoding="utf-8") as stream:
        stream.write(f"required={'true' if required else 'false'}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
