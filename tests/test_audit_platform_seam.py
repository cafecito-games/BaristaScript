#!/usr/bin/env python3
# test_audit_platform_seam.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Behavioural tests for `tests/audit_platform_seam.py`.

Run with:
    python3 tests/test_audit_platform_seam.py
"""

import copy
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "tests" / "audit_platform_seam.py"
MANIFEST = ROOT / "src" / "bs_platform_manifest.json"
SEAM = ROOT / "src" / "bs_platform.h"
GODOT_CPP = ROOT / "godot-cpp"
SITE_FIXTURE = ROOT / "tests" / "fixtures" / "foundry_port_set_includes.txt"

RESOLUTIONS = ("mapped", "vendored", "shimmed", "guarded-out", "deleted-D1")


def run_audit(*arguments):
    return subprocess.run(
        [sys.executable, str(AUDIT), *arguments],
        capture_output=True,
        text=True,
        cwd=str(ROOT),
    )


def real_manifest():
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


class TemporaryManifest:
    """Writes a manifest into a scratch directory and yields its path."""

    def __init__(self, document, name="manifest.json"):
        self.document = document
        self.name = name

    def __enter__(self):
        self.directory = tempfile.TemporaryDirectory()
        path = Path(self.directory.name) / self.name
        if isinstance(self.document, str):
            path.write_text(self.document, encoding="utf-8")
        else:
            path.write_text(json.dumps(self.document, indent=2), encoding="utf-8")
        return path

    def __exit__(self, *exception):
        self.directory.cleanup()
        return False


class RealInputTest(unittest.TestCase):
    """The audit must accept the inputs its real producers emit, byte for byte."""

    def test_checked_in_manifest_audits_clean(self):
        result = run_audit()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for entry in real_manifest()["entries"]:
            self.assertIn(entry["core_header"], result.stdout)

    def test_output_is_deterministic_and_rerunnable(self):
        first = run_audit()
        second = run_audit()
        self.assertEqual(first.stdout, second.stdout)
        self.assertEqual(first.returncode, second.returncode)

    def test_digest_is_semantic_only(self):
        """Copying the manifest to another path must not move the digest."""
        baseline = run_audit()
        digest = re.search(r"^digest\s+([0-9a-f]{64})$", baseline.stdout, re.MULTILINE)
        self.assertIsNotNone(digest, baseline.stdout)
        with TemporaryManifest(real_manifest(), name="relocated_manifest.json") as path:
            relocated = run_audit("--manifest", str(path))
        self.assertEqual(relocated.returncode, 0, relocated.stdout + relocated.stderr)
        self.assertIn(digest.group(1), relocated.stdout)

    def test_every_manifest_site_exists_in_the_upstream_fixture(self):
        """Each `file:line` claim is checked against the verbatim Foundry capture."""
        fixture = SITE_FIXTURE.read_text(encoding="utf-8")
        current = None
        captured = {}
        for line in fixture.splitlines():
            section = re.match(r"^=== modules/foundry_script/(\S+) ===$", line)
            if section:
                current = section.group(1)
                continue
            directive = re.match(r"^(\d+):#include \"([^\"]+)\"$", line)
            if directive and current:
                captured[f"{current}:{directive.group(1)}"] = directive.group(2)
        self.assertTrue(captured, "the upstream include fixture parsed as empty")
        for entry in real_manifest()["entries"]:
            for site in entry["sites"]:
                self.assertIn(site, captured, f"{site} is not in the upstream capture")
                self.assertEqual(captured[site], entry["core_header"], site)

    def test_godot_cpp_header_index_comes_from_the_real_producers(self):
        result = run_audit("--print-index")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        index = set(result.stdout.split())
        # A checked-in header, and a header only the binding generator produces.
        self.assertIn("godot_cpp/templates/vector.hpp", index)
        self.assertIn("godot_cpp/variant/string.hpp", index)

    def test_missing_godot_cpp_tree_is_an_error_not_an_empty_index(self):
        with tempfile.TemporaryDirectory() as empty:
            result = run_audit("--godot-cpp", empty)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("godot-cpp", (result.stdout + result.stderr))


class FailClosedTest(unittest.TestCase):
    """Malformed is not absent, and unknown is not default."""

    def test_absent_manifest(self):
        result = run_audit("--manifest", str(ROOT / "does_not_exist.json"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no manifest at", result.stdout + result.stderr)

    def test_malformed_manifest_is_distinct_from_absent(self):
        with TemporaryManifest("{ this is not json") as path:
            result = run_audit("--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)
        combined = result.stdout + result.stderr
        self.assertIn("is not valid JSON", combined)
        self.assertNotIn("no manifest at", combined)

    def test_manifest_that_is_not_an_object(self):
        with TemporaryManifest([]) as path:
            result = run_audit("--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)

    def test_entry_resolving_to_a_header_godot_cpp_does_not_have(self):
        document = real_manifest()
        document["entries"].append(
            {
                "core_header": "core/os/invented.h",
                "sites": ["fs_parser.cpp:38"],
                "resolution": "mapped",
                "godot_cpp_headers": ["godot_cpp/classes/invented.hpp"],
            }
        )
        with TemporaryManifest(document) as path:
            result = run_audit("--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("godot_cpp/classes/invented.hpp", result.stdout + result.stderr)

    def test_unknown_resolution_is_rejected_not_defaulted(self):
        document = real_manifest()
        document["entries"][0]["resolution"] = "probably-fine"
        with TemporaryManifest(document) as path:
            result = run_audit("--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("probably-fine", result.stdout + result.stderr)

    def test_missing_resolution_is_rejected(self):
        document = real_manifest()
        del document["entries"][0]["resolution"]
        with TemporaryManifest(document) as path:
            result = run_audit("--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)

    def test_duplicate_core_header_is_rejected(self):
        document = real_manifest()
        document["entries"].append(copy.deepcopy(document["entries"][0]))
        with TemporaryManifest(document) as path:
            result = run_audit("--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("listed twice", result.stdout + result.stderr)

    def test_mapped_header_absent_from_the_seam_is_rejected(self):
        document = real_manifest()
        for entry in document["entries"]:
            if entry["core_header"] == "core/templates/vector.h":
                entry["godot_cpp_headers"] = ["godot_cpp/templates/rb_set.hpp"]
        with TemporaryManifest(document) as path:
            result = run_audit("--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("godot_cpp/templates/rb_set.hpp", result.stdout + result.stderr)

    def test_guarded_out_entry_may_not_claim_a_godot_cpp_header(self):
        document = real_manifest()
        for entry in document["entries"]:
            if entry["resolution"] == "guarded-out":
                entry["godot_cpp_headers"] = ["godot_cpp/templates/vector.hpp"]
                break
        with TemporaryManifest(document) as path:
            result = run_audit("--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)

    def test_deleted_symbol_must_be_poisoned_by_the_seam(self):
        document = real_manifest()
        for entry in document["entries"]:
            if entry["resolution"] == "deleted-D1":
                entry["forbidden_symbols"] = ["SomeSymbolTheSeamNeverPoisons"]
        with TemporaryManifest(document) as path:
            result = run_audit("--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SomeSymbolTheSeamNeverPoisons", result.stdout + result.stderr)

    def test_shim_symbol_absent_from_the_seam_is_rejected(self):
        document = real_manifest()
        for entry in document["entries"]:
            if entry["resolution"] == "shimmed":
                entry["shim_symbols"] = ["NotDefinedAnywhere"]
                break
        with TemporaryManifest(document) as path:
            result = run_audit("--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("NotDefinedAnywhere", result.stdout + result.stderr)

    def test_required_macro_missing_from_godot_cpp_is_rejected(self):
        document = real_manifest()
        document["required_macros"].append("ERR_FAIL_ON_A_TUESDAY")
        with TemporaryManifest(document) as path:
            result = run_audit("--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ERR_FAIL_ON_A_TUESDAY", result.stdout + result.stderr)

    def test_site_outside_the_declared_port_set_is_rejected(self):
        document = real_manifest()
        document["entries"][0]["sites"] = ["fs_analyzer.cpp:12"]
        with TemporaryManifest(document) as path:
            result = run_audit("--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("fs_analyzer.cpp", result.stdout + result.stderr)


class VocabularyClosureTest(unittest.TestCase):
    """Every resolution value is handled explicitly; none falls through."""

    MINIMAL = {
        "mapped": {"godot_cpp_headers": ["godot_cpp/templates/vector.hpp"]},
        "vendored": {"vendored_path": "src/thirdparty/example.h"},
        "shimmed": {"shim_symbols": ["StringBuilder"]},
        "guarded-out": {"omission_reason": "unreferenced"},
        "deleted-D1": {"forbidden_symbols": ["NumericType"]},
    }

    REQUIRED_FIELD = {
        "mapped": "godot_cpp_headers",
        "vendored": "vendored_path",
        "shimmed": "shim_symbols",
        "guarded-out": "omission_reason",
        "deleted-D1": "forbidden_symbols",
    }

    # A seam that satisfies every minimal entry above, so a synthetic manifest is judged against a
    # synthetic seam rather than against the real one.
    SYNTHETIC_SEAM = (
        "#pragma once\n"
        "#include <godot_cpp/templates/vector.hpp>\n"
        "class StringBuilder {};\n"
        "#define NumericType BS_NumericType_was_deleted_by_D1\n"
    )

    def entry_for(self, resolution):
        entry = {
            "core_header": "core/templates/vector.h",
            "sites": ["fs_tokenizer.h:35"],
            "resolution": resolution,
        }
        entry.update(self.MINIMAL[resolution])
        return entry

    def document_for(self, entry):
        document = real_manifest()
        document["entries"] = [entry]
        document["required_macros"] = []
        document["seam_support_headers"] = ["godot_cpp/templates/vector.hpp"]
        return document

    def run_against_synthetic_seam(self, document):
        with tempfile.TemporaryDirectory() as scratch:
            manifest = Path(scratch) / "manifest.json"
            manifest.write_text(json.dumps(document, indent=2), encoding="utf-8")
            seam = Path(scratch) / "synthetic_seam.h"
            seam.write_text(self.SYNTHETIC_SEAM, encoding="utf-8")
            return run_audit("--manifest", str(manifest), "--seam", str(seam))

    def test_the_audit_documents_every_resolution(self):
        text = AUDIT.read_text(encoding="utf-8")
        for resolution in RESOLUTIONS:
            self.assertIn(resolution, text)

    def test_every_resolution_is_accepted_when_well_formed(self):
        for resolution in RESOLUTIONS:
            with self.subTest(resolution=resolution):
                result = self.run_against_synthetic_seam(self.document_for(self.entry_for(resolution)))
                if resolution == "vendored":
                    # The vendored tree is proven by its file, which M1 deliberately does not create.
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("src/thirdparty/example.h", result.stdout + result.stderr)
                    continue
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(resolution, result.stdout)

    def test_every_resolution_rejects_a_missing_required_field(self):
        for resolution in RESOLUTIONS:
            with self.subTest(resolution=resolution):
                entry = self.entry_for(resolution)
                del entry[self.REQUIRED_FIELD[resolution]]
                result = self.run_against_synthetic_seam(self.document_for(entry))
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(self.REQUIRED_FIELD[resolution], result.stdout + result.stderr)

    def test_every_resolution_rejects_a_field_belonging_to_another(self):
        """A claim from the wrong vocabulary is rejected, not ignored."""
        for resolution in RESOLUTIONS:
            for other, field in self.REQUIRED_FIELD.items():
                if other == resolution:
                    continue
                if (resolution, field) == ("shimmed", "godot_cpp_headers"):
                    # A shim may name the backing header it extends; see core/string/string_name.h.
                    continue
                with self.subTest(resolution=resolution, foreign_field=field):
                    entry = self.entry_for(resolution)
                    entry[field] = self.MINIMAL[other][field]
                    result = self.run_against_synthetic_seam(self.document_for(entry))
                    self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_unknown_omission_reason_is_rejected(self):
        entry = self.entry_for("guarded-out")
        entry["omission_reason"] = "seemed-fine"
        result = self.run_against_synthetic_seam(self.document_for(entry))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("seemed-fine", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
