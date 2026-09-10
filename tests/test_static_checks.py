#!/usr/bin/env python3
# test_static_checks.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Keep format/license checks independent, reproducible, and able to fail CI."""

import copy
import unittest
from pathlib import Path

import yaml
import validate_ci

ROOT = Path(__file__).resolve().parents[1]


class StaticChecksTests(unittest.TestCase):
    def setUp(self):
        self.workflow = yaml.load((ROOT / ".github/workflows/ci.yml").read_text(), Loader=yaml.BaseLoader)
        self.check = getattr(validate_ci, "check_static_checks_wiring", None)
        self.assertTrue(callable(self.check), "CI validator must protect the static-checks job")

    def complaint(self, document):
        return self.check(yaml.safe_dump(document))

    def test_current_job_is_valid(self):
        self.assertIsNone(self.complaint(self.workflow))

    def test_job_cannot_be_missing_conditional_matrix_or_suppressed(self):
        missing = copy.deepcopy(self.workflow)
        del missing["jobs"]["static-checks"]
        self.assertIsNotNone(self.complaint(missing))
        for key, value in (("if", "false"), ("strategy", {"matrix": {"os": ["linux"]}}),
                           ("continue-on-error", "true"), ("needs", ["build"])):
            with self.subTest(key=key):
                altered = copy.deepcopy(self.workflow)
                altered["jobs"]["static-checks"][key] = value
                self.assertIsNotNone(self.complaint(altered))

    def test_required_commands_cannot_be_disabled_or_repair_sources(self):
        for index, step in enumerate(self.workflow["jobs"]["static-checks"]["steps"]):
            if "run" not in step:
                continue
            for key, value in (("if", "false"), ("continue-on-error", "true"),
                               ("run", step["run"] + " || true"), ("run", "# " + step["run"]),
                               ("run", step["run"].replace("--check", "").replace(
                                   "scripts/check_format.py", "scripts/check_format.py --fix"))):
                if key == "run" and value == step["run"]:
                    continue
                with self.subTest(index=index, key=key, value=value):
                    altered = copy.deepcopy(self.workflow)
                    altered["jobs"]["static-checks"]["steps"][index][key] = value
                    self.assertIsNotNone(self.complaint(altered))

    def test_formatter_install_must_consume_the_authoritative_pin(self):
        altered = copy.deepcopy(self.workflow)
        for step in altered["jobs"]["static-checks"]["steps"]:
            if "requirements-format.txt" in step.get("run", ""):
                step["run"] = "python3 -m pip install clang-format"
        self.assertIsNotNone(self.complaint(altered))

    def test_format_check_is_not_duplicated_in_platform_matrix(self):
        self.workflow["jobs"]["build"]["steps"].append({"run": "python3 scripts/check_format.py"})
        self.assertIsNotNone(self.complaint(self.workflow))

    def test_local_hook_calls_the_same_complete_selector(self):
        configuration = yaml.load((ROOT / ".pre-commit-config.yaml").read_text(), Loader=yaml.BaseLoader)
        hooks = [hook for repo in configuration["repos"] for hook in repo["hooks"]
                 if hook["id"] == "clang-format"]
        self.assertEqual(len(hooks), 1)
        hook = hooks[0]
        self.assertEqual(hook["entry"], "python3 scripts/check_format.py")
        self.assertEqual(hook["pass_filenames"], "false")
        self.assertEqual(hook["always_run"], "true")
        self.assertNotIn("files", hook)
        self.assertNotIn("exclude", hook)

    def test_owned_ci_uses_python3_commands(self):
        import re
        for path in sorted((ROOT / ".github").rglob("*.y*ml")):
            with self.subTest(path=path):
                self.assertIsNone(re.search(r"\bpython\s", path.read_text()))


if __name__ == "__main__":
    unittest.main()
