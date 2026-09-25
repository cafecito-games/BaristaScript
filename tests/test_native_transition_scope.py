#!/usr/bin/env python3
# test_native_transition_scope.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""The native build-system transition gate scopes pull requests only, and fails open."""

import ast
from contextlib import redirect_stdout
import io
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests"))
import native_transition_scope as scope

HEAD = "a" * 40
BASE = "b" * 40
PULL_REQUEST_HEAD = "c" * 40


def fake_git(diff: str, parents: str = f"{HEAD} {BASE} {PULL_REQUEST_HEAD}\n", failing: str = ""):
    calls = []

    def git(*arguments):
        calls.append(arguments)
        if failing and arguments[0] == failing:
            raise subprocess.CalledProcessError(128, ["git", *arguments])
        return {"rev-parse": HEAD + "\n", "fetch": "", "rev-list": parents, "diff": diff}[arguments[0]]

    return git, calls


def name_status(*changes):
    return "".join(f"{status}\0{path}\0" for status, path in changes)


class Classification(unittest.TestCase):
    def test_ordinary_changes_do_not_require_the_transitions(self):
        for path in ("src/bs_analyzer.cpp", "src/bs_analyzer.h", "docs/analyzer-discovery.md",
                     "tests/corpus/analyzer/errors/case.barista", "tests/corpus_baseline.json",
                     "project/tests/smoke_test.gd", "README.md", "AGENTS.md", "doc_classes/BaristaScript.xml",
                     "scripts/run_corpus_triage.py", "tests/test_run_corpus_triage.py"):
            with self.subTest(path=path):
                self.assertEqual(scope.triggering_changes([("M", path)]), [])

    def test_a_native_test_file_does_not_require_the_transitions(self):
        """Both builds glob tests/native/, so a case file needs no wiring in either of them."""
        for path in ("tests/native/tokenizer_test.cpp", "tests/native/new_suite_test.cpp",
                     "tests/native/analyzer_helpers.cpp", "tests/native/analyzer_helpers.h",
                     "tests/native/native_test_runner.h", "tests/native/parser_fixtures.h",
                     "tests/native/README.md", "tests/native/analyzer-parity.md"):
            # Adding and removing a case file are the interesting statuses: unlike src/, neither
            # build names these files, so neither can fall behind the other when the set changes.
            for status in ("M", "A", "D"):
                with self.subTest(path=path, status=status):
                    self.assertEqual(scope.triggering_changes([(status, path)]), [])

    def test_a_build_description_under_the_native_tests_requires_the_transitions(self):
        """The narrowing drops the directory prefix; the repository-wide name and suffix rules stay."""
        for path in ("tests/native/CMakeLists.txt", "tests/native/SCsub", "tests/native/SConscript",
                     "tests/native/native_tests.cmake"):
            for status in ("M", "A", "D"):
                with self.subTest(path=path, status=status):
                    self.assertEqual(scope.triggering_changes([(status, path)]), [path])

    def test_a_native_test_file_alongside_a_build_input_still_requires_the_transitions(self):
        """The narrowing excuses one path from the changed set, never the set that contains it."""
        self.assertEqual(
            scope.triggering_changes([("A", "tests/native/new_suite_test.cpp"), ("M", "SConstruct")]),
            ["SConstruct"],
        )

    def test_build_inputs_require_the_transitions(self):
        for path in ("SConstruct", "CMakeLists.txt", "methods.py", "custom.py", "build_profile.json",
                     "build_versions.json", ".gitmodules", "godot-cpp", ".github/workflows/ci.yml",
                     ".github/actions/setup-godot-cpp/action.yml", "scripts/native_test_build.py",
                     "scripts/build_config.py", "scripts/build_metadata.py", "scripts/generate_global_api.py",
                     "scripts/native_transition_scope.py", "thirdparty/doctest/doctest.h",
                     "tests/verify_native_surface.py", "tests/verify_parse_cache_surface.py",
                     "tests/test_native_cmake_rebuild.py", "tests/test_cmake_api_inputs.py",
                     "tests/run_native_suites.py", "tests/native_suites.json", "tests/test_run_native_suites.py",
                     "tests/test_native_storage.py", "cmake/toolchain.cmake",
                     "src/SCsub", "tools/extension.cmake"):
            for status in ("M", "A", "D"):
                with self.subTest(path=path, status=status):
                    self.assertEqual(scope.triggering_changes([(status, path)]), [path])

    def test_adding_or_removing_an_extension_source_requires_the_transitions(self):
        # CMakeLists.txt names every source, so the file set is a CMake-only build input.
        for status in ("A", "D", "T"):
            with self.subTest(status=status):
                self.assertEqual(scope.triggering_changes([(status, "src/bs_new.cpp")]), ["src/bs_new.cpp"])

    def test_every_python_helper_a_build_or_gated_command_loads_is_a_build_input(self):
        """Follow SConstruct, CMakeLists.txt and the gated commands into every repository module."""
        import validate_ci
        pending = ["SConstruct", "CMakeLists.txt"]
        transition_steps = [
            step for step in validate_ci.LINUX_VERIFICATION_STEPS
            if step["if"] == validate_ci.LINUX_TRANSITION_CONDITION
        ] + validate_ci.CMAKE_VERIFICATION_STEPS
        for step in transition_steps:
            pending += re.findall(r"(?:scripts|tests)/\w+\.py", step["run"])
        self.assertIn("tests/test_native_cmake_rebuild.py", pending)
        seen = set()
        while pending:
            path = pending.pop()
            if path in seen:
                continue
            seen.add(path)
            text = (ROOT / path).read_text()
            if path in ("SConstruct", "CMakeLists.txt"):
                # Both builds execute helpers by path as well as importing them.
                pending += re.findall(r"scripts/\w+\.py", text)
            if path == "CMakeLists.txt":
                continue
            modules = set()
            # Module-level imports are what loading a helper requires; a test's function-level
            # import of the CI audit is not part of any build.
            for node in ast.parse(text).body:
                if isinstance(node, ast.Import):
                    modules.update(alias.name for alias in node.names)
                elif isinstance(node, ast.ImportFrom) and node.module:
                    modules.add(node.module)
            for module in modules:
                for directory in ("", "scripts/", "tests/"):
                    if (ROOT / f"{directory}{module}.py").is_file():
                        pending.append(f"{directory}{module}.py")
        self.assertIn("scripts/build_metadata.py", seen)
        self.assertIn("tests/test_cmake_api_inputs.py", seen)
        missing = sorted(path for path in seen if not scope.is_build_input(path))
        self.assertEqual(missing, [])

    def test_name_status_parsing(self):
        self.assertEqual(scope.parse_name_status(name_status(("M", "a b"), ("A", "c"))), [("M", "a b"), ("A", "c")])
        self.assertEqual(scope.parse_name_status(""), [])
        with self.assertRaises(ValueError):
            scope.parse_name_status("M\0")


class Decision(unittest.TestCase):
    def test_every_other_event_runs_the_full_verification_without_consulting_git(self):
        for event in ("push", "merge_group", "workflow_dispatch", "schedule", "pull_request_target", ""):
            git, calls = fake_git(name_status(("M", "docs/x.md")))
            with self.subTest(event=event):
                self.assertTrue(scope.decide(event, PULL_REQUEST_HEAD, git)[0])
                self.assertEqual(calls, [])

    def test_a_pull_request_touching_only_ordinary_paths_skips(self):
        git, calls = fake_git(name_status(("M", "src/bs_analyzer.cpp"), ("M", "docs/x.md")))
        self.assertEqual(scope.decide("pull_request", PULL_REQUEST_HEAD, git)[0], False)
        self.assertIn(("diff", "--no-renames", "--name-status", "-z", BASE, HEAD), calls)

    def test_a_pull_request_touching_a_build_input_runs(self):
        git, _ = fake_git(name_status(("M", "src/bs_analyzer.cpp"), ("M", "SConstruct")))
        required, reason = scope.decide("pull_request", PULL_REQUEST_HEAD, git)
        self.assertTrue(required)
        self.assertIn("SConstruct", reason)

    def test_the_decision_fails_open(self):
        cases = {
            "fetch fails": fake_git(name_status(("M", "docs/x.md")), failing="fetch")[0],
            "diff fails": fake_git(name_status(("M", "docs/x.md")), failing="diff")[0],
            "revision unknown": fake_git(name_status(("M", "docs/x.md")), failing="rev-parse")[0],
            "not a merge": fake_git(name_status(("M", "docs/x.md")), parents=f"{HEAD} {BASE}\n")[0],
            "another head": fake_git(name_status(("M", "docs/x.md")),
                                     parents=f"{HEAD} {BASE} {'d' * 40}\n")[0],
            "malformed diff": fake_git("M\0")[0],
        }
        for name, git in cases.items():
            with self.subTest(case=name):
                self.assertTrue(scope.decide("pull_request", PULL_REQUEST_HEAD, git)[0])
        for head in ("", "HEAD", "C" * 40):
            with self.subTest(head=head):
                self.assertTrue(scope.decide("pull_request", head, fake_git("")[0])[0])

    def test_main_writes_true_unless_the_decision_is_an_explicit_skip(self):
        with tempfile.TemporaryDirectory() as directory, redirect_stdout(io.StringIO()):
            output = Path(directory) / "output"
            for decision, expected in (((False, "skip"), "required=false\n"), ((True, "run"), "required=true\n")):
                output.write_text("")
                with patch.object(scope, "decide", return_value=decision):
                    self.assertEqual(scope.main(["--event", "pull_request", "--github-output", str(output)]), 0)
                self.assertEqual(output.read_text(), expected)
            output.write_text("")
            with patch.object(scope, "decide", side_effect=RuntimeError("boom")):
                self.assertEqual(scope.main(["--event", "pull_request", "--github-output", str(output)]), 0)
            self.assertEqual(output.read_text(), "required=true\n")


class ShallowMergeCheckout(unittest.TestCase):
    """Reproduce the checkout a pull_request run receives: a depth-one clone of the merge commit."""

    def git(self, directory, *arguments):
        return subprocess.run(["git", "-C", str(directory), *arguments], check=True,
                              capture_output=True, text=True).stdout.strip()

    def commit(self, directory, path, content, message):
        (directory / path).parent.mkdir(parents=True, exist_ok=True)
        (directory / path).write_text(content)
        self.git(directory, "add", path)
        self.git(directory, "-c", "user.name=CI", "-c", "user.email=ci@example.invalid", "commit", "-q", "-m", message)
        return self.git(directory, "rev-parse", "HEAD")

    def decide_in_checkout(self, changed_path):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            author = directory / "author"
            author.mkdir()
            self.git(author, "init", "-q", "-b", "main")
            self.commit(author, "SConstruct", "base\n", "base")
            self.git(author, "checkout", "-q", "-b", "topic")
            pull_request_head = self.commit(author, changed_path, "changed\n", "topic")
            self.git(author, "checkout", "-q", "main")
            self.commit(author, "docs/main.md", "main moved\n", "main")
            self.git(author, "-c", "user.name=CI", "-c", "user.email=ci@example.invalid",
                     "merge", "-q", "--no-ff", "-m", "merge", "topic")
            merge = self.git(author, "rev-parse", "HEAD")
            origin = directory / "origin.git"
            subprocess.run(["git", "clone", "-q", "--bare", str(author), str(origin)], check=True)
            self.git(origin, "config", "uploadpack.allowAnySHA1InWant", "true")
            checkout = directory / "checkout"
            subprocess.run(["git", "clone", "-q", "--depth=1", "--no-single-branch",
                            origin.as_uri(), str(checkout)], check=True)
            self.assertEqual(self.git(checkout, "rev-parse", "HEAD"), merge)
            self.assertEqual(self.git(checkout, "rev-parse", "--is-shallow-repository"), "true")

            def git(*arguments):
                return self.git(checkout, *arguments)

            return scope.decide("pull_request", pull_request_head, git)

    def test_the_merge_is_compared_with_its_base_after_deepening(self):
        required, reason = self.decide_in_checkout("docs/topic.md")
        self.assertFalse(required, reason)
        self.assertIn("1 changed path", reason)
        required, reason = self.decide_in_checkout("tests/native/new_test.cpp")
        self.assertFalse(required, reason)
        required, reason = self.decide_in_checkout("tests/native/SCsub")
        self.assertTrue(required, reason)
        self.assertIn("tests/native/SCsub", reason)


if __name__ == "__main__":
    unittest.main()
