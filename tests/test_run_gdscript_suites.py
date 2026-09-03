#!/usr/bin/env python3
# test_run_gdscript_suites.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Prove the GDScript suite guard is not vacuous.

The defect this guards against is a suite that stops testing while still
reporting green, so the guard itself has to be shown firing on the shapes that
defect takes: a parse error, a runtime script error, and a new suite that never
prints the sentinel.
"""

import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run_gdscript_suites as runner
import validate_ci


# What Godot actually prints when a suite fails to parse: the diagnostic goes to
# the log and the process still exits 0.
PARSE_ERROR_OUTPUT = (
    "Godot Engine v4.7.2.stable - https://godotengine.org\n"
    "SCRIPT ERROR: Parse Error: Cannot use a non-constant value as a constant.\n"
    "          at: GDScript::reload (res://tests/example_test.gd:12)\n"
)

RUNTIME_ERROR_OUTPUT = (
    "SCRIPT ERROR: Invalid call. Nonexistent function 'nope' in base 'Nil'.\n"
    "BS_SUITE_OK example_test\n"
)

PASSING_OUTPUT = "example contract: all assertions passed\nBS_SUITE_OK example_test\n"


def example_invocation() -> runner.Invocation:
    return runner.Invocation(
        name="example_test",
        script="res://tests/example_test.gd",
        args=[],
        expect=runner.default_expectation("example_test"),
    )


def check(condition: bool, message: str, failures: list) -> None:
    if not condition:
        failures.append(message)


def test_parse_error_that_exits_zero_is_a_failure(failures: list) -> None:
    reasons = runner.evaluate(example_invocation(), 0, PARSE_ERROR_OUTPUT)
    check(reasons != [], "a parse error exiting 0 was accepted as a pass", failures)
    check(
        any("never matched" in reason for reason in reasons),
        "the missing sentinel was not reported: %s" % reasons,
        failures,
    )


def test_runtime_script_error_is_a_failure(failures: list) -> None:
    reasons = runner.evaluate(example_invocation(), 0, RUNTIME_ERROR_OUTPUT)
    check(
        any("SCRIPT ERROR:" in reason for reason in reasons),
        "a runtime script error alongside the sentinel was accepted: %s" % reasons,
        failures,
    )


def test_nonzero_exit_is_a_failure(failures: list) -> None:
    reasons = runner.evaluate(example_invocation(), 1, PASSING_OUTPUT)
    check(reasons != [], "a non-zero exit status was accepted as a pass", failures)


def test_clean_run_passes(failures: list) -> None:
    reasons = runner.evaluate(example_invocation(), 0, PASSING_OUTPUT)
    check(reasons == [], "a clean run was rejected: %s" % reasons, failures)


def test_sentinel_of_another_suite_does_not_count(failures: list) -> None:
    borrowed = "BS_SUITE_OK other_test\n"
    reasons = runner.evaluate(example_invocation(), 0, borrowed)
    check(reasons != [], "another suite's sentinel was accepted as this suite's", failures)


def test_a_new_suite_is_discovered_and_guarded_by_default(failures: list) -> None:
    with tempfile.TemporaryDirectory() as directory:
        project_tests = Path(directory)
        (project_tests / "brand_new_test.gd").write_text("extends SceneTree\n")
        (project_tests / "helper.gd").write_text("extends RefCounted\n")

        invocations = runner.build_invocations(project_tests, {"extra_invocations": []})
        names = [invocation.name for invocation in invocations]
        check(
            names == ["brand_new_test"],
            "discovery did not pick up exactly the new suite: %s" % names,
            failures,
        )
        check(
            invocations[0].expect == runner.default_expectation("brand_new_test"),
            "a newly added suite did not inherit the shared sentinel guard",
            failures,
        )
        reasons = runner.evaluate(invocations[0], 0, "brand new suite ran\n")
        check(
            reasons != [],
            "a newly added suite that never prints the sentinel was accepted",
            failures,
        )


def test_manifest_matches_the_repository(failures: list) -> None:
    manifest = json.loads(runner.MANIFEST_PATH.read_text())
    project_tests = runner.ROOT / "project" / "tests"
    invocations = runner.build_invocations(project_tests, manifest)
    check(len(invocations) > 1, "the repository manifest produced no invocations", failures)

    discovered = set(runner.discover_suites(project_tests))
    covered = {
        invocation.script.removeprefix("res://tests/")
        for invocation in invocations
    }
    missing = sorted(discovered - covered)
    check(missing == [], "suites on disk are not run: %s" % missing, failures)


WORKFLOW_HEADER = "jobs:\n  build:\n    steps:\n"


def test_the_workflow_must_actually_run_the_suite_runner(failures: list) -> None:
    complaint = validate_ci.check_gdscript_suite_wiring(
        WORKFLOW_HEADER + '      - run: echo "no suites here"\n'
    )
    check(complaint is not None, "a workflow that runs no suite at all was accepted", failures)


def test_a_commented_out_runner_is_not_evidence_the_suites_run(failures: list) -> None:
    complaint = validate_ci.check_gdscript_suite_wiring(
        WORKFLOW_HEADER
        + "      # - run: python tests/run_gdscript_suites.py --godot \"$godot_binary\"\n"
        + '      - run: echo "suites disabled"\n'
    )
    check(
        complaint is not None,
        "a commented-out runner invocation satisfied the CI wiring check",
        failures,
    )


def test_a_quoted_direct_suite_invocation_is_rejected(failures: list) -> None:
    for path in ('"res://tests/smoke_test.gd"', "'res://tests/smoke_test.gd'", "res://tests/smoke_test.gd"):
        workflow = (
            WORKFLOW_HEADER
            + f"      - run: |\n          \"$godot_binary\" --headless --script {path}\n"
            + "          python tests/run_gdscript_suites.py --godot \"$godot_binary\"\n"
        )
        complaint = validate_ci.check_gdscript_suite_wiring(workflow)
        check(
            complaint is not None,
            f"a direct suite invocation was accepted with path {path}",
            failures,
        )


def test_the_checked_in_workflow_is_wired_through_the_runner(failures: list) -> None:
    workflow = (validate_ci.ROOT / ".github" / "workflows" / "ci.yml").read_text()
    complaint = validate_ci.check_gdscript_suite_wiring(workflow)
    check(complaint is None, "the checked-in workflow is not wired correctly: %s" % complaint, failures)


def main() -> int:
    failures: list = []
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_")]
    for test in tests:
        test(failures)
    for failure in failures:
        print("FAIL %s" % failure)
    if failures:
        return 1
    print("GDScript suite guard: %d contract tests passed" % len(tests))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
