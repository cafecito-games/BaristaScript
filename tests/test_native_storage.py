#!/usr/bin/env python3
# test_native_storage.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Storage-suite preparation and isolation checks, including real failure cleanup."""
import argparse
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import run_native_suites as runner

GODOT = None
BUILD_DIR = runner.DEFAULT_BUILD_DIR


class PreparationTests(unittest.TestCase):
    def test_global_class_requires_editor_import(self):
        imports = getattr(runner, "editor_import_suites", None)
        self.assertTrue(callable(imports), "manifest must own native editor preparation")
        self.assertEqual(imports(), ["global_class"])

    def test_invalid_editor_import_manifest_fails(self):
        imports = getattr(runner, "editor_import_suites", None)
        self.assertTrue(callable(imports), "manifest must own native editor preparation")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "tests").mkdir()
            for invalid in (["unknown"], ["global_class", "global_class"], "global_class", [1]):
                (root / "tests/native_suites.json").write_text(json.dumps(
                    {"suites": ["global_class"], "editor_import": invalid}))
                with self.subTest(invalid=invalid), patch.object(runner, "ROOT", root):
                    self.assertRaises(ValueError, imports)

    def test_unrelated_suite_does_not_launch_editor(self):
        prepare = getattr(runner, "prepare_project", None)
        self.assertTrue(callable(prepare), "native supervisor must prepare the selected suite")
        with tempfile.TemporaryDirectory() as temporary:
            prepare("missing-godot", Path(temporary), "tokenizer", 1)

    def test_failed_or_missing_editor_output_cannot_pass(self):
        prepare = getattr(runner, "prepare_project", None)
        self.assertTrue(callable(prepare), "native supervisor must prepare the selected suite")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for completed in (subprocess.CompletedProcess([], 1, "import failed"),
                              subprocess.CompletedProcess([], 0, "no cache produced")):
                with patch.object(runner, "supervise", return_value=completed):
                    self.assertRaises(ValueError, prepare, "godot", root, "global_class", 1)

    def test_native_profile_contains_editor_cache_reader(self):
        sys.path.insert(0, str(runner.ROOT / "scripts"))
        import native_test_build
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "profile.json"
            native_test_build.write_profile(path)
            self.assertIn("ConfigFile", json.loads(path.read_text())["enabled_classes"])


@unittest.skipUnless(GODOT, "pass --godot to exercise real native storage subprocesses")
class RuntimeTests(unittest.TestCase):
    def test_editor_import_is_required_and_real(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            failed = runner.invoke(GODOT, project, "global_class", "registry", "missing-editor", 60)
            self.assertTrue(runner.evaluate(failed.returncode, failed.stdout, "global_class", "registry",
                                           "missing-editor", artifact["build_id"]), failed.stdout)
            self.assertFalse((project / ".godot/global_script_class_cache.cfg").exists())
            runner.prepare_project(GODOT, project, "global_class", 120)
            self.assertTrue((project / ".godot/global_script_class_cache.cfg").is_file())
            passed = runner.invoke(GODOT, project, "global_class", "registry", "real-editor", 60)
            self.assertEqual([], runner.evaluate(passed.returncode, passed.stdout, "global_class", "registry",
                                                "real-editor", artifact["build_id"]), passed.stdout)

    def test_shared_state_leak_is_rejected(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            completed = runner.invoke(GODOT, project, "runner_failure", "unscoped storage mutation is rejected",
                                      "storage-leak", 60)
        self.assertNotEqual(completed.returncode, 0, completed.stdout)
        self.assertIn("get_bootstrap_allowed_dependency_root() == bootstrap", completed.stdout)
        self.assertIn('"failed_assertions":2', completed.stdout)

    def test_failed_assertion_restores_state_and_files(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            state = project.parent
            completed = runner.invoke(GODOT, project, "runner_failure", "storage assertion failure restores state",
                                      "storage-failure", 60)
            self.assertNotEqual(completed.returncode, 0, completed.stdout)
            self.assertIn('"failed_assertions":1', completed.stdout)
            self.assertIn("BS_STORAGE_CLEANUP_OK", completed.stdout)
            self.assertFalse(list(state.glob("native-storage-*")), "native case scopes must remove their own scratch roots")
        self.assertFalse(state.exists(), "outer supervision must remove failed-process user state")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--godot")
    parser.add_argument("--build-dir", type=Path, default=BUILD_DIR)
    arguments, unittest_arguments = parser.parse_known_args()
    GODOT, BUILD_DIR = arguments.godot, arguments.build_dir
    RuntimeTests.__unittest_skip__ = not GODOT
    unittest.main(argv=[sys.argv[0], *unittest_arguments])
