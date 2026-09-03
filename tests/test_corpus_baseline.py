#!/usr/bin/env python3
# test_corpus_baseline.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Tests for the committed conformance baseline and the gate that pins it.

The corpus is only worth what its baseline is worth. `340/340` means something
because three things are made to agree -- the case files on disk, the number in
`tests/corpus_baseline.json`, and the anchored summary line
`tests/gdscript_suites.json` pins -- and because every way of breaking that
agreement is a red build. These tests are what say so: each one takes the
committed configuration, breaks it one way, and requires
`validate_ci.check_corpus_baseline()` to complain.

They also check the imported tree itself for the shapes a bad import produces
without failing: a case with no expectation, an expectation with no case, an
upstream status word that survived, and a sentinel spelled a second way.

Run with:
    python3 tests/test_corpus_baseline.py
"""

import copy
import importlib.util
import json
import re
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "tests" / "corpus_baseline.json"
SUITES = ROOT / "tests" / "gdscript_suites.json"
SENTINEL_HEADER = ROOT / "src" / "bs_corpus_sentinels.h"
IMPORTER = ROOT / "scripts" / "import_parser_corpus.py"

UPSTREAM_STATUS_WORDS = ("FS_TEST_OK", "FS_TEST_PARSER_ERROR", "FS_TEST_ANALYZER_ERROR")


def load_module(name, path):
    specification = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


validate_ci = load_module("validate_ci", ROOT / "tests" / "validate_ci.py")
importer = load_module("import_parser_corpus", IMPORTER)


class BaselineGate(unittest.TestCase):
    """`validate_ci.check_corpus_baseline()` against a broken configuration.

    Each test writes the broken form into a scratch copy of the two JSON files
    and points the module at it, so nothing here can leave the repository in a
    state a later test reads.
    """

    def setUp(self):
        self.baseline = json.loads(BASELINE.read_text(encoding="utf-8"))
        self.suites = json.loads(SUITES.read_text(encoding="utf-8"))
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.original = (validate_ci.BASELINE_PATH, validate_ci.SUITES_MANIFEST_PATH)

        def restore():
            validate_ci.BASELINE_PATH, validate_ci.SUITES_MANIFEST_PATH = self.original

        self.addCleanup(restore)

    def check(self, baseline=None, suites=None):
        scratch = Path(self.directory.name)
        baseline_path = scratch / "corpus_baseline.json"
        suites_path = scratch / "gdscript_suites.json"
        baseline_path.write_text(json.dumps(baseline or self.baseline, indent=2), encoding="utf-8")
        suites_path.write_text(json.dumps(suites or self.suites, indent=2), encoding="utf-8")
        validate_ci.BASELINE_PATH = baseline_path
        validate_ci.SUITES_MANIFEST_PATH = suites_path
        return validate_ci.check_corpus_baseline()

    def parser_pin(self, suites):
        for entry in suites["extra_invocations"]:
            if "corpus/parser" in " ".join(entry.get("args", [])):
                return entry
        raise AssertionError("no parser corpus invocation in the suites manifest")

    def test_committed_configuration_is_consistent(self):
        self.assertIsNone(self.check())

    def test_baseline_total_must_match_the_tree(self):
        baseline = copy.deepcopy(self.baseline)
        baseline["corpora"]["parser"]["total"] += 1
        self.assertIn("cases", self.check(baseline=baseline) or "")

    def test_baseline_skipped_count_must_match_the_tree(self):
        baseline = copy.deepcopy(self.baseline)
        baseline["corpora"]["parser"]["skipped"] = 0
        self.assertIsNotNone(self.check(baseline=baseline))

    def test_a_loose_pin_is_rejected(self):
        suites = copy.deepcopy(self.suites)
        self.parser_pin(suites)["expect"] = "BS_CORPUS"
        self.assertIn("loose match", self.check(suites=suites) or "")

    def test_an_unanchored_pin_is_rejected(self):
        suites = copy.deepcopy(self.suites)
        self.parser_pin(suites)["expect"] = "BS_CORPUS 340/340 skipped=2"
        self.assertIsNotNone(self.check(suites=suites))

    def test_a_pin_that_lags_the_baseline_is_rejected(self):
        suites = copy.deepcopy(self.suites)
        self.parser_pin(suites)["expect"] = "^BS_CORPUS 23/23 skipped=0$"
        self.assertIsNotNone(self.check(suites=suites))

    def test_an_unrun_corpus_is_rejected(self):
        suites = copy.deepcopy(self.suites)
        suites["extra_invocations"] = [
            entry
            for entry in suites["extra_invocations"]
            if "corpus/parser" not in " ".join(entry.get("args", []))
        ]
        self.assertIn("not a baseline", self.check(suites=suites) or "")

    def test_an_expected_failure_must_name_a_real_case(self):
        baseline = copy.deepcopy(self.baseline)
        baseline["corpora"]["parser"]["expected_failures"] = ["errors/no_such_case.barista"]
        self.assertIn("not cases", self.check(baseline=baseline) or "")

    def test_an_expected_failure_lowers_the_pinned_pass_count(self):
        """A case recorded as expected-fail may not also be pinned as passing.

        Without this the two halves of the baseline could disagree: a corpus
        could record a known failure and still pin a full-green summary, and the
        run that unexpectedly fixed it would stay silent.
        """
        baseline = copy.deepcopy(self.baseline)
        parser = baseline["corpora"]["parser"]
        root = ROOT / "project" / parser["root"][len("res://") :]
        some_case = sorted(
            path.relative_to(root).as_posix() for path in root.rglob("*.barista")
        )[0]
        parser["expected_failures"] = [some_case]
        complaint = self.check(baseline=baseline)
        self.assertIsNotNone(complaint)
        self.assertIn(f"{parser['total'] - 1}/{parser['total']}", complaint)


class ImportedTree(unittest.TestCase):
    """The shape of project/tests/corpus/parser, checked without running Godot."""

    @classmethod
    def setUpClass(cls):
        cls.baseline = json.loads(BASELINE.read_text(encoding="utf-8"))["corpora"]["parser"]
        cls.root = ROOT / "project" / cls.baseline["root"][len("res://") :]
        cls.sentinel = importer.success_sentinel()

    def cases(self):
        return sorted(
            path
            for path in self.root.rglob("*.barista")
            if not path.name.endswith(".notest.barista")
        )

    def test_every_case_has_an_expectation(self):
        missing = [
            path.relative_to(self.root).as_posix()
            for path in self.cases()
            if not path.with_suffix(".out").is_file()
        ]
        self.assertEqual(missing, [])

    def test_every_expectation_has_a_case(self):
        orphaned = [
            path.relative_to(self.root).as_posix()
            for path in self.root.rglob("*.out")
            if not path.with_suffix(".barista").is_file()
        ]
        self.assertEqual(orphaned, [])

    def test_helpers_have_no_expectation(self):
        """A `.notest` helper with an `.out` would look like a vanished case."""
        for path in self.root.rglob("*.notest.barista"):
            self.assertFalse(path.with_suffix(".out").is_file(), path)

    def test_no_upstream_status_word_survived(self):
        surviving = []
        for path in self.root.rglob("*.out"):
            text = path.read_text(encoding="utf-8")
            for word in UPSTREAM_STATUS_WORDS:
                if word in text:
                    surviving.append(f"{path.relative_to(self.root)}: {word}")
        self.assertEqual(surviving, [])

    def test_expectations_are_one_line_with_a_trailing_newline(self):
        """The harness reads the first line; anything after it is unread text.

        An expectation carrying a second line would be an assertion nothing
        checks, which is how a corpus grows expectations that are quietly false.
        """
        malformed = []
        for path in self.root.rglob("*.out"):
            text = path.read_text(encoding="utf-8")
            if not text.endswith("\n") or "\n" in text[:-1]:
                malformed.append(path.relative_to(self.root).as_posix())
        self.assertEqual(malformed, [])

    def test_the_success_sentinel_is_never_spelled_a_second_way(self):
        """Every success expectation is byte-identical to the C++ definition."""
        for path in self.root.rglob("*.out"):
            line = path.read_text(encoding="utf-8").split("\n")[0]
            if line.upper().startswith("BS_TEST"):
                self.assertEqual(line, self.sentinel, path)

    def test_the_d1_grep_gate_returns_only_the_removal_case(self):
        """Issue #10's acceptance grep, run over the imported tree.

        It is a regression check on the spellings it covers, not the discovery
        tool the issue took it for: it matches neither bare `long` nor a
        lowercase suffix, which is why the triage table in the importer is
        larger than the nine files it returns.
        """
        pattern = re.compile(r"\b(uint|ulong)\b|as!|[0-9](UL|U|L)\b")
        matching = sorted(
            path.relative_to(self.root).as_posix()
            for path in self.root.rglob("*.barista")
            if pattern.search(path.read_text(encoding="utf-8"))
        )
        self.assertEqual(matching, ["features/fixed_width_integer_literals.barista"])

    def test_the_upstream_ignore_marker_was_not_imported(self):
        """Foundry's corpus root carries an empty `.fsignore`.

        The harness's marker is `.baristaignore` precisely so that file cannot
        skip 340 cases at exit code 0, but a stray copy of either would.
        """
        strays = [
            path.relative_to(self.root).as_posix()
            for path in self.root.rglob("*")
            if path.name in (".fsignore", ".baristaignore")
        ]
        self.assertEqual(strays, [])

    def test_analyzer_deferred_cases_all_exist_and_expect_the_sentinel(self):
        for case, upstream in self.baseline["analyzer_deferred"].items():
            path = self.root / case
            self.assertTrue(path.is_file(), case)
            self.assertEqual(
                path.with_suffix(".out").read_text(encoding="utf-8"), self.sentinel + "\n"
            )
            self.assertTrue(upstream.strip(), case)


class TriageTable(unittest.TestCase):
    """The importer's triage table, which is the only copy of it."""

    def dispositions(self):
        return {
            "deleted": set(importer.DELETIONS),
            "replaced": {importer.REPLACEMENT_PATH},
            "edited": {path for path, _edits, _reason in importer.EDITS},
            "overridden": {
                path for path, _edits, _upstream, _new, _reason in importer.EXPECTATION_OVERRIDES
            },
        }

    def test_no_file_carries_two_dispositions(self):
        seen: dict[str, str] = {}
        for disposition, paths in self.dispositions().items():
            for path in paths:
                self.assertNotIn(
                    path,
                    seen,
                    f"{path} is both {seen.get(path)} and {disposition}",
                )
                seen[path] = disposition

    def test_every_triaged_file_carries_a_reason(self):
        reasons = (
            list(importer.DELETIONS.values())
            + [importer.REPLACEMENT_REASON]
            + [reason for _path, _edits, reason in importer.EDITS]
            + [reason for _p, _e, _u, _n, reason in importer.EXPECTATION_OVERRIDES]
        )
        for reason in reasons:
            self.assertGreater(len(reason), 40, reason)

    def test_an_override_states_the_upstream_text_it_replaces(self):
        """An override that did not would absorb an upstream rewording silently."""
        for path, _edits, upstream, replacement, _reason in importer.EXPECTATION_OVERRIDES:
            self.assertTrue(upstream, path)
            self.assertNotEqual(upstream, replacement, path)

    def test_the_sentinel_comes_from_the_cpp_definition(self):
        header = SENTINEL_HEADER.read_text(encoding="utf-8")
        self.assertIn(f'SUCCESS_SENTINEL = "{importer.success_sentinel()}"', header)
        self.assertNotIn(
            f'"{importer.success_sentinel()}"',
            IMPORTER.read_text(encoding="utf-8"),
            "the importer must read the sentinel, not restate it",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
