#!/usr/bin/env python3
# test_warning_code_removal.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""A warning code that a language delta removes must break the build, not the runtime.

`docs/GRAMMAR.md` section 0.2 deletes language features, and a deleted feature takes its warnings
with it. The registry's fail-closed contract says a surviving reference to a removed code is a
compile error -- never a stub, never a runtime "unknown warning" string, never a silently dead
branch. Every other property of the registry is asserted from inside headless Godot; this one
cannot be, because the proof is that a translation unit does not compile.

So this compiles two. The control references a code the registry has and must succeed, which is
what stops the probe from "passing" because of an unrelated include error. The probe references a
code the registry does not have and must fail, naming the identifier.

The compilation is syntax-only, but it needs the generated godot-cpp headers that `bs_platform.h`
pulls in, so run a build first:

    scons api_version=4.7 target=template_debug
    python3 tests/test_warning_code_removal.py
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GODOT_CPP = ROOT / "godot-cpp"

LIVE_CODE = "EMPTY_FILE"
# Deliberately not an enumerator, and named so that a reader of the compiler output knows why.
REMOVED_CODE = "CODE_REMOVED_BY_A_LANGUAGE_DELTA"

TRANSLATION_UNIT = """
#include "bs_warning.h"

barista_script::BSWarning::Code referenced_code() {{
	return barista_script::BSWarning::{code};
}}
"""


def find_compiler():
    for candidate in (os.environ.get("CXX"), "c++", "g++", "clang++"):
        if candidate and shutil.which(candidate):
            return shutil.which(candidate)
    return None


def compile_probe(compiler, code):
    """Syntax-check a translation unit referencing `code`. Returns (returncode, output)."""
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "probe.cpp"
        source.write_text(TRANSLATION_UNIT.format(code=code), encoding="utf-8")
        command = [
            compiler,
            "-std=c++17",
            "-fsyntax-only",
            "-DDEBUG_ENABLED",
            "-I",
            str(ROOT / "src"),
            "-I",
            str(GODOT_CPP / "include"),
            "-I",
            str(GODOT_CPP / "gen" / "include"),
            "-I",
            str(GODOT_CPP / "gdextension"),
            str(source),
        ]
        completed = subprocess.run(command, capture_output=True, text=True)
        return completed.returncode, completed.stdout + completed.stderr


class RemovedWarningCodeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = find_compiler()
        if cls.compiler is None:
            raise unittest.SkipTest("no C++ compiler on PATH; set CXX to run the compile probes")
        generated = GODOT_CPP / "gen" / "include" / "godot_cpp"
        if not generated.is_dir():
            raise AssertionError(
                "no generated godot-cpp headers at {}; run 'scons api_version=4.7 "
                "target=template_debug' before this test".format(generated)
            )

    def test_a_live_code_compiles(self):
        """The control. Without it, a failing probe would prove nothing but a broken include path."""
        returncode, output = compile_probe(self.compiler, LIVE_CODE)
        self.assertEqual(returncode, 0, "referencing {} did not compile:\n{}".format(LIVE_CODE, output))

    def test_a_removed_code_does_not_compile(self):
        returncode, output = compile_probe(self.compiler, REMOVED_CODE)
        self.assertNotEqual(
            returncode, 0, "referencing the removed code {} compiled anyway".format(REMOVED_CODE)
        )
        self.assertIn(
            REMOVED_CODE,
            output,
            "the compiler failed without naming {}:\n{}".format(REMOVED_CODE, output),
        )

    def test_the_failure_is_the_missing_enumerator_and_not_something_else(self):
        """A stub, a fallback enumerator or a catch-all would make this the wrong diagnostic."""
        _, output = compile_probe(self.compiler, REMOVED_CODE)
        lowered = output.lower()
        self.assertTrue(
            "no member named" in lowered or "is not a member of" in lowered or "has no member" in lowered,
            "the removed code failed for some reason other than not existing:\n{}".format(output),
        )


if __name__ == "__main__":
    result = unittest.main(argv=[sys.argv[0], "-v"], exit=False).result
    raise SystemExit(0 if result.wasSuccessful() else 1)
