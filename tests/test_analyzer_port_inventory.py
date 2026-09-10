#!/usr/bin/env python3
# test_analyzer_port_inventory.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Bounded negative controls for the explicit source/evidence ledger."""

from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import validate_analyzer_port_inventory as inventory

FOUNDRY = None


class InventoryContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = inventory.read_json(
            inventory.ROOT / "tests/analyzer_port_inventory.json"
        )

    def rejected(self, mutate, message, *, foundry=None, complete=True):
        data = copy.deepcopy(self.original)
        mutate(data)
        with self.assertRaisesRegex(ValueError, message):
            inventory.validate(data, foundry_dir=foundry, complete=complete)

    def test_complete_inventory(self):
        result = inventory.validate(self.original, foundry_dir=FOUNDRY, complete=True)
        self.assertEqual(result["files"], 19)
        self.assertEqual(result["historical_markers"], 58)

    def test_root_pin_schema_and_missing_file(self):
        self.rejected(lambda d: d.update(schema_version=True), "schema_version")
        self.rejected(lambda d: d.update(upstream_revision="0" * 40), "stale pin")
        self.rejected(lambda d: d["upstream_files"].pop(), "missing or duplicate")
        self.rejected(lambda d: d.update(extra=[]), "root schema")

    def test_authoritative_platform_pin_cannot_be_satisfied_by_unrelated_note(self):
        platform = inventory.read_json(inventory.ROOT / "src/bs_platform_manifest.json")
        platform["upstream"]["revision"] = "0" * 40
        platform["unrelated_note"] = inventory.UPSTREAM
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "scripts").mkdir()
            (root / "src").mkdir()
            shutil.copyfile(
                inventory.ROOT / "scripts/corpus_sources.json",
                root / "scripts/corpus_sources.json",
            )
            (root / "src/bs_platform_manifest.json").write_text(json.dumps(platform))
            with self.assertRaisesRegex(ValueError, "platform/corpus pin mismatch"):
                inventory.validate(self.original, root=root)

    def test_signature_identity_fingerprint_and_classification(self):
        self.rejected(
            lambda d: d["upstream_files"][0]["functions"].append(
                copy.deepcopy(d["upstream_files"][0]["functions"][0])
            ),
            "duplicate function",
        )
        self.rejected(
            lambda d: d["upstream_files"][0]["functions"][0].update(
                signature="renamed()"
            ),
            "stale signature hash",
        )
        self.rejected(
            lambda d: d["upstream_files"][0]["functions"][0].update(ordinal=True),
            "ordinal/count",
        )
        self.rejected(
            lambda d: d["upstream_files"][0]["functions"].append(
                {
                    "id": "unclassified",
                    "qualified_name": "missing",
                    "signature": "void missing()",
                    "sha256": inventory.digest(b"void missing()"),
                    "ordinal": 1,
                    "occurrences": 1,
                }
            ),
            "unclassified listed",
        )

    def test_duplicate_signature_with_distinct_id_is_rejected(self):
        def duplicate(d):
            function = copy.deepcopy(d["upstream_files"][0]["functions"][0])
            function["id"] = "new-id-same-signature"
            d["upstream_files"][0]["functions"].append(function)

        self.rejected(duplicate, "duplicate signature identity")
        self.rejected(
            lambda d: d["behavior_families"][0].update(branch_keys=[]), "branch keys"
        )

    def test_unknown_pending_and_ownerless_dispositions(self):
        self.rejected(
            lambda d: d["behavior_families"][0].update(disposition="assumed"),
            "unknown disposition",
        )
        self.rejected(
            lambda d: d["behavior_families"][0].update(disposition="pending"),
            "pending M3",
        )
        self.rejected(
            lambda d: d["behavior_families"][0].update(
                disposition="downstream", milestone="M5", owner=""
            ),
            "boundary/owner",
        )
        self.rejected(
            lambda d: d["behavior_families"][0].update(
                disposition="downstream", milestone="M5", boundary=""
            ),
            "boundary/owner",
        )
        self.rejected(
            lambda d: d["behavior_families"][0].update(
                disposition="downstream", milestone="later"
            ),
            "downstream milestone",
        )
        self.rejected(
            lambda d: d["behavior_families"][0].update(owner="#37"), "unrelated owner"
        )

    def test_missing_local_symbol_and_behavior_test(self):
        self.rejected(
            lambda d: d["behavior_families"][0]["symbols"][0].update(
                symbol="not_a_real_symbol"
            ),
            "missing local symbol/test",
        )
        self.rejected(
            lambda d: d["behavior_families"][0]["tests"][0].update(
                path="tests/missing.py"
            ),
            "missing local source",
        )
        self.rejected(
            lambda d: d["behavior_families"][0].update(tests=[]), "missing code/test"
        )

    def test_historical_and_renamed_current_markers(self):
        self.rejected(lambda d: d["markers"].pop(0), "58-marker")
        self.rejected(
            lambda d: d["markers"][0].update(families=[]), "unclassified marker"
        )
        self.rejected(
            lambda d: d["markers"][0].update(replacement={}), "replacement code/test"
        )

        def rename(d):
            marker = next(m for m in d["markers"] if m["origin"] == "current")
            marker["text"] += " renamed"
            marker["sha256"] = inventory.digest(marker["text"].encode())

        self.rejected(rename, "renamed current marker")
        self.rejected(
            lambda d: d["markers"].append(copy.deepcopy(d["markers"][0])),
            "duplicate marker",
        )
        # Removed integration markers remain valid with their original immutable identity
        # and explicit replacement code/test references in the complete real document.
        current = {
            (m["path"], m["text"])
            for m in self.original["markers"]
            if m["origin"] == "current"
        }
        self.assertTrue(
            any(
                (m["path"], m["text"]) not in current
                for m in self.original["markers"]
                if m["origin"] == "integration"
            )
        )

    def test_candidate_exact_blobs_and_results(self):
        document = copy.deepcopy(self.original)
        family = next(f for f in document["behavior_families"] if f["id"] == "S5-builtin-metadata")
        paths = sorted({ref["path"] for ref in family["symbols"] + family["tests"]})
        # Synthetic evidence exercises the schema only; it is never production provenance.
        fixture_result = "unit-fixture-only: synthetic successful candidate result"
        family["evidence"] = {
            "kind": "candidate",
            "pr": {
                "kind": "provisional",
                "repository": "cafecito-games/BaristaScript",
                "head_ref": "m3/issue-141-restoration-warnings",
                "issue": 141,
            },
            "blobs": [{"path": path, "sha256": inventory.digest((inventory.ROOT / path).read_bytes())}
                      for path in paths],
            "results": [{"command": ["unit-fixture-only"], "exit": 0,
                         "result": fixture_result, "log_sha256": inventory.digest(fixture_result.encode())}],
        }
        inventory.validate(document, foundry_dir=FOUNDRY, complete=True)

        def candidate(d):
            return next(f["evidence"] for f in d["behavior_families"] if f["id"] == "S5-builtin-metadata")

        def rejected(mutate, message):
            changed = copy.deepcopy(document)
            mutate(changed)
            with self.assertRaisesRegex(ValueError, message):
                inventory.validate(changed, foundry_dir=FOUNDRY, complete=True)

        rejected(
            lambda d: candidate(d)["blobs"][0].update(sha256="0" * 64),
            "stale candidate blob",
        )
        rejected(
            lambda d: candidate(d)["results"][0].update(exit=1),
            "invalid candidate result",
        )
        rejected(
            lambda d: candidate(d)["pr"].update(head_ref="wrong"),
            "candidate PR locator",
        )
        rejected(
            lambda d: candidate(d)["blobs"][0].update(path="tests/analyzer_port_inventory.json"),
            "circular candidate",
        )

    def test_missing_pin_and_source_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "missing pin/source"):
                inventory.validate(self.original, foundry_dir=Path(directory))
        if FOUNDRY is not None:
            self.rejected(
                lambda d: d["upstream_files"][0].update(sha256="0" * 64),
                "stale upstream hash",
                foundry=FOUNDRY,
            )

    def test_shallow_history_is_explicitly_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            shallow = Path(directory) / "shallow"
            subprocess.run(
                [
                    "git",
                    "clone",
                    "--quiet",
                    "--depth",
                    "1",
                    "--no-checkout",
                    inventory.ROOT.as_uri(),
                    str(shallow),
                ],
                check=True,
                capture_output=True,
            )
            self.assertEqual(
                subprocess.check_output(
                    ["git", "rev-parse", "--is-shallow-repository"],
                    cwd=shallow,
                    text=True,
                ).strip(),
                "true",
            )
            with self.assertRaisesRegex(ValueError, "missing integration history"):
                inventory.historical_markers(shallow)

    def test_detached_and_merged_checkout_do_not_require_author_branch(self):
        # A disposable clone carries real ancestry and the exact candidate file bytes.
        # No branch or Git configuration in the author's checkout is modified.
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory) / "checkout"
            subprocess.run(
                [
                    "git",
                    "clone",
                    "--quiet",
                    "--shared",
                    "--no-checkout",
                    str(inventory.ROOT),
                    str(checkout),
                ],
                check=True,
                capture_output=True,
            )
            subprocess.run(
                ["git", "checkout", "--quiet", "--detach", "HEAD"],
                cwd=checkout,
                check=True,
                capture_output=True,
            )
            paths = (
                subprocess.check_output(["git", "ls-files", "-z"], cwd=inventory.ROOT)
                .decode()
                .split("\0")
            )
            paths += [
                "tests/analyzer_port_inventory.json",
                "tests/validate_analyzer_port_inventory.py",
                "tests/test_analyzer_port_inventory.py",
                "tests/native/get_node_analyzer_tests.cpp",
            ]
            for relative in paths:
                source = inventory.ROOT / relative
                if relative and source.is_file():
                    target = checkout / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source, target)
            subprocess.run(
                ["git", "add", "."], cwd=checkout, check=True, capture_output=True
            )
            inventory.validate(self.original, root=checkout, complete=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Offline Inventory Test",
                    "-c",
                    "user.email=test@example.invalid",
                    "commit",
                    "--quiet",
                    "--allow-empty",
                    "-m",
                    "candidate bytes",
                ],
                cwd=checkout,
                check=True,
                capture_output=True,
            )
            candidate = subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=checkout, text=True
            ).strip()
            subprocess.run(
                ["git", "switch", "--quiet", "-c", "main-integration-control", "HEAD^"],
                cwd=checkout,
                check=True,
                capture_output=True,
            )
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Offline Inventory Test",
                    "-c",
                    "user.email=test@example.invalid",
                    "merge",
                    "--quiet",
                    "--no-ff",
                    "-m",
                    "integrated candidate",
                    candidate,
                ],
                cwd=checkout,
                check=True,
                capture_output=True,
            )
            parents = subprocess.check_output(
                ["git", "rev-list", "--parents", "-n", "1", "HEAD"],
                cwd=checkout,
                text=True,
            ).split()
            self.assertEqual(len(parents), 3)
            inventory.validate(self.original, root=checkout, complete=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--foundry-dir", type=Path)
    args, remaining = parser.parse_known_args()
    FOUNDRY = args.foundry_dir
    unittest.main(argv=[__file__] + remaining)
