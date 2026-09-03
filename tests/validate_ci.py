#!/usr/bin/env python3
# validate_ci.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

SUITE_RUNNER = "tests/run_gdscript_suites.py"

# Godot's `--script`, its documented `-s` alias, and the quoting a workflow author may add.
DIRECT_SUITE_INVOCATION = re.compile(r"""(?:--script|(?<![\w-])-s)\s+['"]?res://\S+""")

# The runner named in a command position, so a workflow that only mentions its path -- in an
# echo, say -- is not mistaken for one that runs it. `--godot` is what makes the invocation
# run the suites; `--list` only prints them.
# `|| true`, `; true` and friends turn the runner's non-zero exit into a green step.
SUPPRESSED_STATUS = re.compile(r"\|\||;\s*true\b|&&\s*true\b")

SUITE_RUNNER_COMMAND = re.compile(
    r"""(?:^|[\s|;&])(?:python3?|py)\s+['"]?[^\s'"#]*"""
    + re.escape(SUITE_RUNNER)
    + r"""(?P<arguments>[^\n]*)""",
    re.MULTILINE,
)


# A `#` that starts a line or follows whitespace begins a comment, in YAML and in the
# shell blocks the workflow embeds.
COMMENT_TAIL = re.compile(r"(?:^|(?<=\s))#.*$")


def executable_lines(workflow: str) -> str:
    """The workflow with its comments -- whole-line and inline -- removed.

    A commented-out command is text a substring search would still find, so the
    wiring checks below must not read one as evidence that CI runs anything.
    """
    return "\n".join(COMMENT_TAIL.sub("", line) for line in workflow.splitlines())


def check_gdscript_suite_wiring(workflow: str) -> str | None:
    """Return a complaint when the workflow could run a GDScript suite unguarded.

    A GDScript parse error makes SceneTree quit 0, so a suite invoked straight
    from the workflow can stop testing while the job stays green. Every suite
    must go through tests/run_gdscript_suites.py, which demands the guard
    sentinel that only an executed suite can print.
    """
    active = executable_lines(workflow)

    direct_invocations = DIRECT_SUITE_INVOCATION.findall(active)
    if direct_invocations:
        return (
            "CI must not invoke a GDScript suite directly "
            f"({', '.join(sorted(set(direct_invocations)))}); "
            f"run it through {SUITE_RUNNER} so a parse error cannot pass as green"
        )

    invocations = [
        match.group("arguments")
        for match in SUITE_RUNNER_COMMAND.finditer(active)
        if "--godot" in match.group("arguments") and "--list" not in match.group("arguments")
    ]
    if not invocations:
        return (
            f"CI must run the GDScript suites through {SUITE_RUNNER} --godot <binary>; "
            "naming the runner, or running it in --list mode, launches no suite"
        )

    # A guard whose non-zero status is swallowed is no guard at all.
    if all(SUPPRESSED_STATUS.search(arguments) for arguments in invocations):
        return (
            f"CI must let {SUITE_RUNNER} fail the job; its exit status is the guard, "
            "so it may not be followed by ||, ; true, or a discarded status"
        )

    return None


def main() -> int:
    api_path = ROOT / "godot-cpp" / "gdextension" / "extension_api-4-7.json"
    workflow_path = ROOT / ".github" / "workflows" / "ci.yml"

    api_precision = json.loads(api_path.read_text())["header"]["precision"]
    workflow = workflow_path.read_text()
    matrix_precisions = re.findall(r"^\s+float-precision:\s+(\w+)\s*$", workflow, re.MULTILINE)

    if not matrix_precisions:
        print("No CI float-precision matrix entries found")
        return 1

    incompatible = sorted({precision for precision in matrix_precisions if precision != api_precision})
    if incompatible:
        print(
            f"CI requests {', '.join(incompatible)} precision, but the Godot 4.7 API is {api_precision} precision"
        )
        return 1

    if "  push:\n    branches: [main]\n" not in workflow:
        print("CI push events must be limited to main to avoid duplicating pull request runs")
        return 1

    suite_wiring_complaint = check_gdscript_suite_wiring(workflow)
    if suite_wiring_complaint is not None:
        print(suite_wiring_complaint)
        return 1

    print(f"CI configuration matches the Godot 4.7 API ({api_precision}) and avoids duplicate PR runs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
