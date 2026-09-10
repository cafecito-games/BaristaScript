#!/usr/bin/env python3
# test_check_format.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Exercise the static gate in disposable Git repositories, without editing this checkout."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class FormatCheckTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="barista-format-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.environment = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)
        for name in ("scripts/check_format.py", "scripts/add_license_header.py",
                     "scripts/requirements-format.txt", "scripts/build_config.py", "build_versions.json", ".clang-format"):
            source = ROOT / name
            if source.exists():
                destination = self.root / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
        self.git("init", "-q")
        self.add_source("src/valid.cpp", "int answer = 42;\n")

    def git(self, *arguments):
        return subprocess.run(["git", *arguments], cwd=self.root, env=self.environment,
                              capture_output=True, text=True, check=True)

    def add_source(self, name, body):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("// SPDX-License-Identifier: MIT\n" + body)
        self.git("add", "-f", "--", name)
        return path

    def check(self, *arguments, script="scripts/check_format.py", cwd=None):
        return subprocess.run([sys.executable, str(self.root / script), *arguments],
                              cwd=cwd or self.root, env=self.environment,
                              capture_output=True, text=True)

    def test_clean_check_is_repeatable_and_works_outside_root(self):
        before = self.git("diff").stdout
        for _ in range(2):
            result = self.check(cwd=self.root / "src")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("1 maintained C/C++ file", result.stdout)
            self.assertEqual(self.git("diff").stdout, before)

    def test_native_headers_and_names_with_spaces_are_checked(self):
        path = self.add_source("tests/native/future test.hpp", "int  answer=42;\n")
        before = path.read_bytes()
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("future test.hpp", result.stderr)
        self.assertEqual(path.read_bytes(), before)

    def test_fix_is_explicit_and_uses_the_same_selection(self):
        path = self.add_source("tests/native/future.cpp", "int  answer=42;\n")
        failed = self.check()
        self.assertNotEqual(failed.returncode, 0)
        fixed = self.check("--fix")
        self.assertEqual(fixed.returncode, 0, fixed.stdout + fixed.stderr)
        self.assertIn("int answer = 42;", path.read_text())
        self.assertEqual(self.check().returncode, 0)

    def test_excluded_and_untracked_sources_are_untouched(self):
        names = ("godot-cpp/src/vendor.cpp", "thirdparty/doctest/doctest.h",
                 "build/native-scons/generated.cpp", "build/native-cmake/generated.cpp",
                 "project/bin/generated.h", "src/gen/generated.cpp", "project/.godot/cache.cpp")
        paths = [self.add_source(name, "int  excluded=1;\n") for name in names]
        untracked = self.root / "src/untracked.cpp"
        untracked.write_text("int  untracked=1;\n")
        paths.append(untracked)
        before = {path: path.read_bytes() for path in paths}
        for arguments in ((), ("--fix",)):
            result = self.check(*arguments)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("1 maintained C/C++ file", result.stdout)
        self.assertEqual({path: path.read_bytes() for path in paths}, before)

    def test_empty_discovery_fails(self):
        self.git("rm", "-f", "src/valid.cpp")
        self.add_source("thirdparty/doctest/doctest.h", "int excluded;\n")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No maintained", result.stderr)

    def test_missing_tracked_source_fails(self):
        (self.root / "src/valid.cpp").unlink()
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("src/valid.cpp", result.stderr)

    def test_missing_formatter_has_install_diagnostic(self):
        result = self.check("--clang-format", str(self.root / "missing-clang-format"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requirements-format.txt", result.stderr)
        self.assertIn("missing-clang-format", result.stderr)

    def test_wrong_formatter_is_rejected_before_source_changes(self):
        path = self.add_source("tests/native/future.cpp", "int  answer=42;\n")
        before = path.read_bytes()
        result = self.check("--fix", "--clang-format", sys.executable)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires clang-format", result.stderr)
        self.assertIn("Python", result.stderr)
        self.assertEqual(path.read_bytes(), before)

    def test_pin_mismatch_is_rejected(self):
        pin = self.root / "scripts/requirements-format.txt"
        pin.write_text("clang-format==0.0.0\n")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("formatter requirements projection", result.stderr)

    def test_missing_license_in_filename_with_spaces_fails(self):
        path = self.add_source("tests/native/missing license.cpp", "int value;\n")
        path.write_text("int value;\n")
        result = self.check("--check", script="scripts/add_license_header.py")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing license.cpp", result.stdout)
        self.assertEqual(path.read_text(), "int value;\n")

    def test_missing_license_fails_without_editing(self):
        path = self.add_source("tests/native/unlicensed.cpp", "int value;\n")
        path.write_text("int value;\n")
        excluded = self.add_source("thirdparty/doctest/doctest.h", "int value;\n")
        excluded.write_text("int value;\n")
        result = self.check("--check", script="scripts/add_license_header.py")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unlicensed.cpp", result.stdout)
        self.assertNotIn("doctest.h", result.stdout)
        self.assertEqual(path.read_text(), "int value;\n")
        self.assertEqual(excluded.read_text(), "int value;\n")


if __name__ == "__main__":
    unittest.main()
