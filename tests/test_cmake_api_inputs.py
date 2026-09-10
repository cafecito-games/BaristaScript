#!/usr/bin/env python3
# test_cmake_api_inputs.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Reject changed external API inputs during real incremental CMake builds."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build_config import api_path, load_config


class ApiInputTests(unittest.TestCase):
    jobs = 2

    def command(self, label, command):
        result = subprocess.run([str(value) for value in command], cwd=ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        print(json.dumps(dict(label=label, command=list(map(str, command)), exit=result.returncode)), flush=True)
        print(result.stdout, flush=True)
        return result

    def exercise(self, route):
        pinned = api_path(load_config())
        original = pinned.read_bytes()
        with tempfile.TemporaryDirectory(prefix="barista-external-api-") as temporary:
            directory = Path(temporary)
            selected = directory / pinned.name
            selected.write_bytes(original)
            if route == "custom":
                option = "-DGODOTCPP_CUSTOM_API_FILE=" + str(selected)
            else:
                shutil.copy2(pinned.parent / "gdextension_interface.json", directory)
                option = "-DGODOTCPP_GDEXTENSION_DIR=" + str(directory)
            build = directory / "build"
            configured = self.command(route + "-configure", ["cmake", "-S", ROOT, "-B", build,
                                      "-DCMAKE_BUILD_TYPE=Debug", "-DPython3_EXECUTABLE=" + sys.executable, option])
            self.assertEqual(configured.returncode, 0, configured.stdout)
            command = ["cmake", "--build", build, "--target", "barista_build_identity", "generate_bindings",
                       "--parallel", str(self.jobs)]
            baseline = self.command(route + "-baseline", command)
            self.assertEqual(baseline.returncode, 0, baseline.stdout)
            trimmed = build / "godot-cpp/extension_api.json"
            original_trimmed = hashlib.sha256(trimmed.read_bytes()).hexdigest()
            changed = json.loads(original)
            changed["header"]["precision"] = "double"
            for label, content in (("precision", json.dumps(changed).encode()), ("malformed", b"{"), ("missing", None)):
                with self.subTest(route=route, change=label):
                    try:
                        if content is None:
                            selected.unlink()
                        else:
                            selected.write_bytes(content)
                        refused = self.command(route + "-" + label, command)
                        self.assertNotEqual(refused.returncode, 0, "incremental build accepted changed external API")
                        self.assertIn(str(selected), refused.stdout)
                        self.assertIn("Configuring incomplete", refused.stdout)
                        if label == "precision":
                            self.assertIn("Actual bindings API differs from shared configuration", refused.stdout)
                    finally:
                        selected.write_bytes(original)
                        restored = self.command(route + "-" + label + "-restored", command)
                        self.assertEqual(restored.returncode, 0, restored.stdout)
                        self.assertEqual(hashlib.sha256(trimmed.read_bytes()).hexdigest(), original_trimmed)

    def test_custom_api_file(self):
        self.exercise("custom")

    def test_gdextension_directory(self):
        self.exercise("directory")


def verify_external_api_inputs(jobs):
    ApiInputTests.jobs = jobs
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ApiInputTests))
    if not result.wasSuccessful():
        raise RuntimeError("incremental external CMake API checks failed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4:
        parser.error("--jobs must be between 1 and 4")
    verify_external_api_inputs(args.jobs)
