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
from corpus_registry import validate_registration  # noqa: E402

SUITE_RUNNER = "tests/run_gdscript_suites.py"

BASELINE_PATH = ROOT / "tests" / "corpus_baseline.json"
SUITES_MANIFEST_PATH = ROOT / "tests" / "gdscript_suites.json"

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
    try:
        validate_registration(ROOT, baseline_path=BASELINE_PATH, suites_path=SUITES_MANIFEST_PATH)
    except (ValueError, OSError) as error:
        return str(error)
    return None


def check_corpus_reproducibility_wiring(workflow: str) -> str | None:
    """Audit the executable job structure, including acquisition and failure propagation.

    BaseLoader retains YAML scalar spellings (not YAML 1.1's boolean `on`).
    A narrow command allowlist makes shell wrappers, mutable refs and hidden
    status suppression reviewable changes instead of substring matches.
    """
    try:
        import yaml
    except ImportError:
        return "CI audit requires PyYAML: python3 -m pip install -r tests/requirements.txt"

    class UniqueLoader(yaml.BaseLoader):
        def construct_mapping(self, node, deep=False):
            result = {}
            for key_node, value_node in node.value:
                key = self.construct_object(key_node, deep=deep)
                if not isinstance(key, str) or key in result:
                    raise ValueError("duplicate or non-string YAML mapping key")
                result[key] = self.construct_object(value_node, deep=deep)
            return result

    try:
        document = yaml.load(workflow, Loader=UniqueLoader)
        if not isinstance(document, dict):
            return "CI workflow must be a mapping"
        events = document.get("on")
        if (not isinstance(events, dict)
                or set(events) != {"push", "pull_request", "merge_group", "workflow_call"}
                or events["push"] != {"branches": ["main"]}
                or any(events[event] not in ("", None, {})
                       for event in ("pull_request", "merge_group", "workflow_call"))):
            return "CI must preserve main push, pull_request, merge_group and workflow_call events without filters"
        if "defaults" in document or "env" in document:
            return "corpus-reproducibility must not inherit workflow shell or environment overrides"
        jobs = document.get("jobs", {})
        if not isinstance(jobs, dict):
            return "CI jobs must be a mapping"
        job = jobs.get("corpus-reproducibility")
        if (not isinstance(job, dict) or set(job) - {"name", "runs-on", "permissions", "steps"}
                or job.get("name", "corpus-reproducibility") != "corpus-reproducibility"
                or job.get("runs-on") != "ubuntu-22.04"
                or job.get("permissions") != {"contents": "read"}):
            return "CI requires one unsuppressed Linux corpus-reproducibility job outside the matrix"
        steps = job.get("steps")
        if not isinstance(steps, list) or len(steps) != 7:
            return "corpus-reproducibility requires checkout, Python setup, dependency, outputs, upstream checkout, tests and check steps"
        expected = [
            {"uses": "actions/checkout@v4", "with": {"persist-credentials": "false", "submodules": "false"}},
            {"uses": "actions/setup-python@v5", "with": {"python-version": "3.x"}},
            {"shell": "bash", "run": "python3 -m pip install -r tests/requirements.txt"},
            {"id": "source", "shell": "bash", "run": 'python3 scripts/check_corpus_reproducibility.py --github-output "$GITHUB_OUTPUT"'},
            {"uses": "actions/checkout@v4", "with": {
                "repository": "${{ steps.source.outputs.repository }}",
                "ref": "${{ steps.source.outputs.revision }}",
                "path": ".upstream-foundry", "submodules": "false",
                "persist-credentials": "false", "fetch-depth": "1",
                "sparse-checkout": "${{ steps.source.outputs.sparse_paths }}"}},
            {"shell": "bash", "run": "python3 tests/test_corpus_reproducibility.py --foundry .upstream-foundry"},
            {"shell": "bash", "run": "python3 scripts/check_corpus_reproducibility.py --foundry .upstream-foundry"},
        ]
        for index, (step, required) in enumerate(zip(steps, expected), 1):
            if not isinstance(step, dict):
                return f"corpus-reproducibility step {index} must be a mapping"
            actual = {key: value.strip() if key == "run" and isinstance(value, str) else value
                      for key, value in step.items() if key != "name"}
            if actual != required:
                return f"corpus-reproducibility step {index} must retain its validated inputs and unsuppressed command"
        # The build matrix validates before setup-godot-cpp. Provision Python
        # here too: hosted macOS's system/Homebrew pip is externally managed.
        build_steps = jobs.get("build", {}).get("steps", [])
        build_prefix = [
            {"uses": "actions/setup-python@v5", "with": {"python-version": "3.x"}},
            {"shell": "bash", "run": "\n".join((
                "python3 -m pip install -r tests/requirements.txt",
                "python3 tests/validate_ci.py",
                "python3 tests/test_run_gdscript_suites.py",
                "python3 tests/test_corpus_baseline.py",
                "python3 tests/test_corpus_expectations.py"))},
        ]
        if not isinstance(build_steps, list) or len(build_steps) < 3:
            return "build matrix requires setup-managed Python before offline validation"
        for step, required in zip(build_steps[1:3], build_prefix):
            if not isinstance(step, dict):
                return "build matrix Python setup and validation steps must be mappings"
            actual = {key: value.strip() if key == "run" and isinstance(value, str) else value
                      for key, value in step.items() if key != "name"}
            if actual != required:
                return "build matrix requires unsuppressed setup-managed Python before offline validation"
        # Naming this command in another job would duplicate the gate per matrix
        # entry; only the dedicated job owns upstream acquisition/checking.
        for name, other in jobs.items():
            if name != "corpus-reproducibility" and isinstance(other, dict):
                if any(isinstance(step, dict) and "scripts/check_corpus_reproducibility.py" in step.get("run", "")
                       for step in other.get("steps", [])):
                    return "corpus reproducibility must run only in its dedicated job"
    except (yaml.YAMLError, ValueError, TypeError) as error:
        return f"invalid CI YAML: {error}"
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

    corpus_wiring_complaint = check_corpus_reproducibility_wiring(workflow)
    if corpus_wiring_complaint is not None:
        print(corpus_wiring_complaint)
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
