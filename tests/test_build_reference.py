# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Exercise the real documentation pipeline, including its fail-closed boundaries."""
import hashlib
import runpy
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReferenceTest(unittest.TestCase):
    def invoke(self, *arguments):
        return subprocess.run([sys.executable, str(ROOT / "scripts/build_reference.py"),
                               *map(str, arguments)], text=True, capture_output=True, cwd=ROOT)

    def test_reference_build_is_reproducible_and_check_preserves_xml(self):
        sources = sorted((ROOT / "doc_classes").glob("*.xml"))
        before = [(p.read_bytes(), p.stat().st_mtime_ns) for p in sources]
        checked = self.invoke("--check")
        self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
        self.assertEqual(before, [(p.read_bytes(), p.stat().st_mtime_ns) for p in sources])
        with tempfile.TemporaryDirectory() as temporary:
            outputs = [Path(temporary) / name for name in ("one", "two")]
            digests = []
            for output in outputs:
                result = self.invoke("--output", output)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                pages = sorted(output.glob("class_*.html"))
                self.assertEqual([p.stem for p in pages], ["class_baristascript", "class_baristascriptlanguage", "class_baristascriptresourceloader"])
                digests.append({p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in pages})
            self.assertEqual(*digests)

    def test_godot_rejects_self_closing_container_tags(self):
        builder = runpy.run_path(str(ROOT / "scripts/build_reference.py"))
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "doc_classes"
            shutil.copytree(ROOT / "doc_classes", source)
            path = source / "BaristaScript.xml"
            path.write_text(path.read_text().replace("<tutorials>\n\t</tutorials>", "<tutorials />"))
            with self.assertRaisesRegex(ValueError, "self-closing"):
                builder["validate_sources"](source)

    def test_missing_generated_page_and_broken_fragment_fail(self):
        builder = runpy.run_path(str(ROOT / "scripts/build_reference.py"))
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            with self.assertRaisesRegex(ValueError, "missing generated class page"):
                builder["check_html"](output)
            for name in builder["SUPPORTED"]:
                (output / f"class_{name.lower()}.html").write_text('<p id="present">Class</p>')
            (output / "index.html").write_text('<a href="class_baristascript.html#absent">Broken</a>')
            with self.assertRaisesRegex(ValueError, "broken internal fragment"):
                builder["check_html"](output)

    def test_invalid_xml_unknown_reference_and_private_classes_fail(self):
        mutations = [
            ("BaristaScript.xml", lambda text: text + "<broken", "BaristaScript.xml"),
            ("BaristaScript.xml", lambda text: text.replace("[method is_valid]", "[method missing_method]", 1), "missing_method"),
            ("BaristaScript.xml", lambda text: text.replace("[Script]", "[MissingClass]", 1), "MissingClass"),
            ("BaristaScript.xml", lambda text: text.replace("[Script]", '[url=missing.html]Missing[/url]', 1), "missing.html"),
        ]
        for filename, mutation, error in mutations:
            with self.subTest(error=error), tempfile.TemporaryDirectory() as temporary:
                source = Path(temporary) / "doc_classes"
                shutil.copytree(ROOT / "doc_classes", source)
                path = source / filename
                path.write_text(mutation(path.read_text()))
                result = self.invoke("--check", "--source", source)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(error, result.stdout + result.stderr)
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "doc_classes"
            shutil.copytree(ROOT / "doc_classes", source)
            (source / "BaristaScriptNativeTestRunner.xml").write_text('<class name="BaristaScriptNativeTestRunner" />')
            result = self.invoke("--check", "--source", source)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("unsupported class", result.stdout + result.stderr)
            (source / "BaristaScriptNativeTestRunner.xml").unlink()
            (source / "BaristaScriptLanguage.xml").unlink()
            result = self.invoke("--check", "--source", source)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("missing supported class", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
