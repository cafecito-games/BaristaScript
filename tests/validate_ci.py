#!/usr/bin/env python3
# validate_ci.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT / "scripts") not in sys.path:
    sys.path.insert(0, str(ROOT / "scripts"))
from corpus_ledger import validate_triage_ledger  # noqa: E402

SUITE_RUNNER = "tests/run_gdscript_suites.py"

BASELINE_PATH = ROOT / "tests" / "corpus_baseline.json"
SUITES_MANIFEST_PATH = ROOT / "tests" / "gdscript_suites.json"

# Printed by project/tests/corpus_harness.gd, which reads it from
# src/bs_corpus_sentinels.h. Read from the same header here so the pattern this
# file derives cannot drift from the line the harness prints.
SENTINEL_HEADER = ROOT / "src" / "bs_corpus_sentinels.h"

CASE_EXTENSION = ".barista"
HELPER_SUFFIX = ".notest" + CASE_EXTENSION

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


def summary_prefix() -> str:
    match = re.search(
        r'SUMMARY_PREFIX\s*=\s*"([^"]+)"', SENTINEL_HEADER.read_text(encoding="utf-8")
    )
    if match is None:
        raise SystemExit(f"could not read SUMMARY_PREFIX from {SENTINEL_HEADER}")
    return match.group(1)


def list_corpus_paths(root: Path) -> tuple[set[str], set[str]]:
    """Return ``(cases, helpers)`` as relative posix paths under ``root``."""
    cases: set[str] = set()
    helpers: set[str] = set()
    for path in root.rglob("*" + CASE_EXTENSION):
        relative = path.relative_to(root).as_posix()
        if path.name.endswith(HELPER_SUFFIX):
            helpers.add(relative)
        else:
            cases.add(relative)
    return cases, helpers


def count_corpus(root: Path) -> tuple[int, int]:
    """The cases and the skipped helpers actually on disk under `root`.

    Counted the way project/tests/corpus_harness.gd counts them, so the number
    committed in the baseline is checked against the tree rather than against
    another copy of itself.
    """
    cases, helpers = list_corpus_paths(root)
    return len(cases), len(helpers)


def check_corpus_baseline() -> str | None:
    """Return a complaint when the corpus baseline, the tree and the CI pin disagree.

    The baseline is the one committed number, and it is only worth committing if
    every way of losing cases is a failure. Three things have to agree: how many
    case files are on disk, what tests/corpus_baseline.json records, and the
    anchored summary line tests/gdscript_suites.json pins. Cases quietly
    vanishing then fails on the first, a case quietly starting to fail fails on
    the third, and a case the baseline records as expected-fail that starts
    passing fails on the third too -- the pinned pass count is exact, so drift in
    either direction is a red build.

    The triage ledger additionally proves that every upstream runnable case is
    either imported or has exactly one path-specific excluded/deferred
    disposition, and that every rewrite/expectation override names an imported
    case with a non-empty reason.
    """
    baseline = json.loads(BASELINE_PATH.read_text())
    manifest = json.loads(SUITES_MANIFEST_PATH.read_text())
    expectations = {
        entry.get("script", "") + " " + " ".join(entry.get("args", [])): entry.get("expect", "")
        for entry in manifest.get("extra_invocations", [])
    }
    prefix = summary_prefix()

    for name, corpus in sorted(baseline["corpora"].items()):
        imported = bool(corpus.get("imported", True))
        root_uri = corpus.get("root")
        if not isinstance(root_uri, str) or not root_uri.startswith("res://"):
            return f"corpus {name!r} root {root_uri!r} is not a res:// path"

        if not imported:
            complaint = validate_triage_ledger(name, corpus)
            if complaint is not None:
                return complaint
            continue

        root = ROOT / "project" / root_uri[len("res://") :]
        if not root.is_dir():
            return f"corpus {name!r} root {root} does not exist"

        disk_cases, disk_helpers = list_corpus_paths(root)
        cases, helpers = len(disk_cases), len(disk_helpers)
        if cases != corpus["total"] or helpers != corpus["skipped"]:
            return (
                f"corpus {name!r} holds {cases} cases and {helpers} skipped helpers, but "
                f"{BASELINE_PATH.name} records {corpus['total']} and {corpus['skipped']}; a case "
                "that vanishes must never shrink the corpus quietly"
            )

        complaint = validate_triage_ledger(
            name,
            corpus,
            disk_cases=disk_cases,
            disk_helpers=disk_helpers,
        )
        if complaint is not None:
            return complaint

        expected_failures = corpus["expected_failures"]
        unknown = sorted(set(expected_failures) - disk_cases)
        if unknown:
            return (
                f"corpus {name!r} records expected failures that are not cases: "
                + ", ".join(unknown)
            )

        passing = corpus["total"] - len(expected_failures)
        required = (
            f"^{re.escape(prefix)} {passing}/{corpus['total']} "
            f"skipped={corpus['skipped']}$"
        )
        key = f"{root_uri} --corpus {root_uri}"
        pinned = expectations.get(key)
        if pinned is None:
            pinned = next(
                (
                    expect
                    for invocation, expect in expectations.items()
                    if root_uri in invocation
                ),
                None,
            )
        if pinned is None:
            return (
                f"{SUITES_MANIFEST_PATH.name} has no invocation running corpus {name!r} at "
                f"{root_uri}; an unrun corpus is not a baseline"
            )
        if pinned != required:
            return (
                f"corpus {name!r} is pinned as {pinned!r} but the baseline requires {required!r}; "
                "the pin is what makes the number mean anything, so it may not be a loose match "
                "and it may not lag the baseline"
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

    baseline_complaint = check_corpus_baseline()
    if baseline_complaint is not None:
        print(baseline_complaint)
        return 1

    print(f"CI configuration matches the Godot 4.7 API ({api_precision}) and avoids duplicate PR runs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
