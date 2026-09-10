#!/usr/bin/env python3
# test_run_native_suites.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Failure propagation tests; --godot additionally exercises actual native subprocesses."""
import argparse
import json
import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import run_native_suites as runner

GODOT = None
BUILD_DIR = runner.DEFAULT_BUILD_DIR


class ResultTests(unittest.TestCase):
    def record(self, **overrides):
        record = dict(protocol=runner.PROTOCOL_VERSION, suite="tokenizer", case="", nonce="run-1",
                      build_id="build-1", cases=13, assertions=600, failed_cases=0,
                      failed_assertions=0)
        record.update(overrides)
        return runner.RESULT_PREFIX + json.dumps(record)

    def evaluate(self, output, code=0):
        return runner.evaluate(code, output, "tokenizer", "", "run-1", "build-1")

    def test_only_matching_executed_completion_is_success(self):
        self.assertEqual([], self.evaluate(self.record()))

    def test_exit_zero_empty_and_query_are_not_evidence(self):
        for output in ("", "[doctest] listing test cases", "[doctest] all tests passed"):
            with self.subTest(output=output):
                self.assertTrue(self.evaluate(output))

    def test_crash_and_assertion_failure(self):
        for code in (-11, 1):
            self.assertTrue(self.evaluate(self.record(), code))
        for field in ("failed_cases", "failed_assertions"):
            self.assertTrue(self.evaluate(self.record(**{field: 1})))

    def test_empty_unknown_and_mismatched_results(self):
        for key, value in dict(protocol=99, suite="other", case="other", nonce="old",
                               build_id="wrong", cases=0, assertions=0).items():
            with self.subTest(key=key):
                self.assertTrue(self.evaluate(self.record(**{key: value})))
        self.assertTrue(self.evaluate(self.record() + "\n" + self.record()))
        self.assertTrue(self.evaluate(runner.RESULT_PREFIX + "not json"))
        self.assertTrue(self.evaluate(self.record(cases=True)))
        self.assertTrue(self.evaluate(self.record(protocol=True)))
        self.assertTrue(self.evaluate(self.record(extra=1)))

    def test_timeout_is_failure_with_diagnostics(self):
        result = runner.supervise([sys.executable, "-c", "import time; time.sleep(5)"], 0.05)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("timeout", result.stdout)

    def test_ci_requires_unsuppressed_whole_manifest_execution(self):
        import validate_ci
        good = '  - name: Native tests\n    run: python3 tests/run_native_suites.py --godot "$godot_binary"\n'
        self.assertIsNone(validate_ci.check_native_suite_wiring(good))
        for bad in (good.replace("--godot", "--list --godot"),
                    good.replace("--godot", "--suite tokenizer --godot"),
                    good.replace("--godot", "--case some_case --godot"),
                    good.replace('"$godot_binary"', '"$godot_binary" || true'),
                    good.replace("    run:", "    continue-on-error: true\n    run:"), ""):
            self.assertIsNotNone(validate_ci.check_native_suite_wiring(bad))

    def test_unknown_manifest_suite_is_rejected(self):
        self.assertRaises(ValueError, runner.select_suites, ["missing"])

    def test_disabled_build_has_no_native_framework(self):
        import verify_native_surface
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assertTrue(verify_native_surface.verify_surface(root, "template_debug"))
            binary = root / "library.template_debug.so"
            binary.write_bytes(b"ordinary extension")
            self.assertEqual([], verify_native_surface.verify_surface(root, "template_debug"))
            for marker in verify_native_surface.NATIVE_MARKERS + verify_native_surface.RETIRED_ADAPTER_MARKERS:
                binary.write_bytes(b"ordinary extension" + marker)
                self.assertTrue(verify_native_surface.verify_surface(root, "template_debug"))

    def test_wrong_or_stale_artifact_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "native.so"
            library.write_bytes(b"test artifact")
            artifact = dict(library=str(library), sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
                            build_id=runner.build_identity())
            metadata = root / "native-artifact.json"
            metadata.write_text(json.dumps(artifact))
            self.assertEqual(artifact, runner.read_artifact(root))
            library.write_bytes(b"wrong artifact")
            self.assertRaises(ValueError, runner.read_artifact, root)
            library.write_bytes(b"test artifact")
            artifact["build_id"] = "previous source revision"
            metadata.write_text(json.dumps(artifact))
            self.assertRaises(ValueError, runner.read_artifact, root)

    def test_missing_library_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            self.assertRaises((ValueError, OSError), runner.read_artifact, Path(temporary))


@unittest.skipUnless(GODOT, "pass --godot to run real native subprocess checks")
class RuntimeTests(unittest.TestCase):
    def test_runtime_state_is_disposable(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            state = project.parent
            completed = runner.invoke(GODOT, project, "tokenizer", "integer_range_is_exact", "state-check", 60)
            self.assertEqual([], runner.evaluate(completed.returncode, completed.stdout, "tokenizer",
                              "integer_range_is_exact", "state-check", artifact["build_id"]), completed.stdout)
            self.assertTrue((state / "logs/godot.log").is_file(), "Godot user:// logs must stay in disposable state")
        self.assertFalse(state.exists())

    def test_real_assertion_failure(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            completed = runner.invoke(GODOT, project, "runner_failure", "intentional failing assertion", "failure-check", 60)
        reasons = runner.evaluate(completed.returncode, completed.stdout, "runner_failure", "intentional failing assertion",
                                  "failure-check", artifact["build_id"])
        self.assertTrue(reasons, completed.stdout)
        self.assertNotEqual(completed.returncode, 0, completed.stdout)
        self.assertIn('"failed_assertions":1', completed.stdout)

    def test_warning_settings_restore_after_real_failed_assertions(self):
        artifact = runner.read_artifact(BUILD_DIR)
        case = "warning settings restore after failed assertions"
        with runner.staged_project(artifact) as project:
            completed = runner.invoke(GODOT, project, "runner_failure", case, "settings-cleanup", 60)
        self.assertNotEqual(completed.returncode, 0, completed.stdout)
        records = [json.loads(line[len(runner.RESULT_PREFIX):]) for line in completed.stdout.splitlines()
                   if line.startswith(runner.RESULT_PREFIX)]
        self.assertEqual(len(records), 1, completed.stdout)
        record = records[0]
        self.assertEqual(record["cases"], 1, completed.stdout)
        self.assertEqual(record["failed_cases"], 1, completed.stdout)
        self.assertEqual(record["failed_assertions"], 2, completed.stdout)
        self.assertGreater(record["assertions"], 2, completed.stdout)
        self.assertIn("settings restoration checked after both failed assertions", completed.stdout)

    def test_missing_library_subprocess(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            for library in (project / "bin").glob("native.*"):
                library.unlink()
            completed = runner.invoke(GODOT, project, "tokenizer", "", "missing-check", 5)
        self.assertTrue(runner.evaluate(completed.returncode, completed.stdout, "tokenizer", "",
                                        "missing-check", artifact["build_id"]))
        self.assertNotIn(runner.RESULT_PREFIX, completed.stdout)

    def test_wrong_library_cannot_supply_runner(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            for library in (project / "bin").glob("native.*"):
                library.write_bytes(b"not a Godot extension")
            completed = runner.invoke(GODOT, project, "tokenizer", "", "wrong-library-check", 5)
        self.assertTrue(runner.evaluate(completed.returncode, completed.stdout, "tokenizer", "",
                                        "wrong-library-check", artifact["build_id"]))
        self.assertNotIn(runner.RESULT_PREFIX, completed.stdout)

    def test_unknown_case_and_query_emit_no_execution_evidence(self):
        artifact = runner.read_artifact(BUILD_DIR)
        for case, listing in (("no such case", False), ("INTEGER_RANGE_IS_EXACT", False), ("", True)):
            with runner.staged_project(artifact) as project:
                completed = runner.invoke(GODOT, project, "tokenizer", case, "query-check", 60,
                                          listing=listing)
            self.assertTrue(runner.evaluate(completed.returncode, completed.stdout, "tokenizer", case,
                                            "query-check", artifact["build_id"]))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--godot")
    parser.add_argument("--build-dir", type=Path, default=BUILD_DIR)
    args, remaining = parser.parse_known_args()
    GODOT, BUILD_DIR = args.godot, args.build_dir
    RuntimeTests.__unittest_skip__ = not GODOT
    unittest.main(argv=[sys.argv[0], *remaining])
