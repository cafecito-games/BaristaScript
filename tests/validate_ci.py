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

# A `#` that starts a line or follows whitespace begins a comment, in YAML and in the shell
# blocks the workflow embeds.
COMMENT_TAIL = re.compile(r"(?:^|(?<=\s))#.*$")

# The YAML scaffolding around an embedded shell command: the list dash and the `run:` key.
COMMAND_LINE_PREFIX = re.compile(r"^\s*(?:-\s*)?(?:run:\s*[|>]?[-+]?\s*)?")

# Where one shell command ends and the next begins, with the operator kept.
COMMAND_SEPARATOR = re.compile(r"(\|\||&&|[;|])")

# The runner run as a command, not merely named as an argument to some other command: the
# segment has to start with the interpreter, allowing only leading environment assignments.
SUITE_RUNNER_COMMAND = re.compile(
    r"""^(?:\w+=\S*\s+)*(?:python3?|py)\s+['"]?[^\s'"]*"""
    + re.escape(SUITE_RUNNER)
    + r"""(?P<arguments>\s.*|$)"""
)

# `|| true`, `; true` and friends turn the runner's non-zero exit into a green step.
SUPPRESSED_STATUS = re.compile(r"\|\||;\s*true\b|&&\s*true\b")

# Anything but an explicit `false` lets the step's failure through as a success, including the
# `${{ true }}` expression form GitHub Actions accepts.
CONTINUE_ON_ERROR = re.compile(r"continue-on-error:\s*(?!false\b|'false'|\"false\")\S")

STEP_START = re.compile(r"^(\s*)-\s")


def executable_lines(workflow: str) -> list[str]:
    """The workflow's lines with their comments -- whole-line and inline -- removed.

    A commented-out command is text a substring search would still find, so the
    wiring checks below must not read one as evidence that CI runs anything.
    """
    return [COMMENT_TAIL.sub("", line) for line in workflow.splitlines()]


def line_invocations(line: str) -> list[str]:
    """The text following each command in `line` that actually runs the suite runner.

    The returned text is the rest of the shell line, so a caller can see both the
    runner's own arguments and whatever the line does with its exit status.
    """
    body = COMMAND_LINE_PREFIX.sub("", line)
    pieces = COMMAND_SEPARATOR.split(body)
    invocations: list[str] = []
    for index in range(0, len(pieces), 2):
        match = SUITE_RUNNER_COMMAND.match(pieces[index].strip())
        if match is not None:
            invocations.append(match.group("arguments") + "".join(pieces[index + 1 :]))
    return invocations


def enclosing_step(lines: list[str], index: int) -> str:
    """The workflow step containing `lines[index]`, as its own text block."""
    start = index
    indent = 0
    while start >= 0:
        match = STEP_START.match(lines[start])
        if match is not None:
            indent = len(match.group(1))
            break
        start -= 1
    if start < 0:
        start = index
    end = start + 1
    while end < len(lines):
        match = STEP_START.match(lines[end])
        if match is not None and len(match.group(1)) <= indent:
            break
        end += 1
    return "\n".join(lines[start:end])


def runner_invocations(lines: list[str]) -> list[tuple[str, str]]:
    """Every suite-runner command, paired with the workflow step that holds it."""
    invocations: list[tuple[str, str]] = []
    for index, line in enumerate(lines):
        for arguments in line_invocations(line):
            invocations.append((arguments, enclosing_step(lines, index)))
    return invocations


def check_gdscript_suite_wiring(workflow: str) -> str | None:
    """Return a complaint when the workflow could run a GDScript suite unguarded.

    A GDScript parse error makes SceneTree quit 0, so a suite invoked straight
    from the workflow can stop testing while the job stays green. Every suite
    must go through tests/run_gdscript_suites.py, which demands the guard
    sentinel that only an executed suite can print, and the runner's own exit
    status must be allowed to fail the job.
    """
    lines = executable_lines(workflow)
    active = "\n".join(lines)

    direct_invocations = DIRECT_SUITE_INVOCATION.findall(active)
    if direct_invocations:
        return (
            "CI must not invoke a GDScript suite directly "
            f"({', '.join(sorted(set(direct_invocations)))}); "
            f"run it through {SUITE_RUNNER} so a parse error cannot pass as green"
        )

    invocations = [
        (arguments, step)
        for arguments, step in runner_invocations(lines)
        if "--godot" in arguments and "--list" not in arguments
    ]
    if not invocations:
        return (
            f"CI must run the GDScript suites through {SUITE_RUNNER} --godot <binary>; "
            "naming the runner, or running it in --list mode, launches no suite"
        )

    # A guard whose non-zero status is swallowed is no guard at all, so at least one
    # invocation must be able to fail the job: unsuppressed in the shell, and in a step
    # that does not continue on error.
    effective = [
        arguments
        for arguments, step in invocations
        if not SUPPRESSED_STATUS.search(arguments) and not CONTINUE_ON_ERROR.search(step)
    ]
    if not effective:
        return (
            f"CI must let {SUITE_RUNNER} fail the job; its exit status is the guard, so no "
            "invocation may be followed by ||, ; true, or sit in a continue-on-error step"
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
