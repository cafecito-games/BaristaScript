#!/usr/bin/env python3
# test_corpus_reproducibility.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Offline registry, workflow, and real parser-producer regression tests."""

import argparse
from contextlib import redirect_stdout, redirect_stderr
import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests"))


class RegistryDelivery(unittest.TestCase):
    def test_registry_and_wrapper_are_delivered(self):
        for name in ("corpus_sources.json", "corpus_registry.py", "check_corpus_reproducibility.py"):
            self.assertTrue((ROOT / "scripts" / name).is_file(), name)


class RegistryContract(unittest.TestCase):
    def setUp(self):
        self.assertTrue((ROOT / "scripts/corpus_registry.py").is_file(), "shared validator missing")
        import corpus_registry
        self.registry_module = corpus_registry
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for directory in ("scripts", "tests", "src"):
            (self.root / directory).mkdir()
        for name in ("corpus_sources.json", "import_parser_corpus.py"):
            shutil.copy2(ROOT / "scripts" / name, self.root / "scripts" / name)
        for name in ("corpus_baseline.json", "gdscript_suites.json"):
            shutil.copy2(ROOT / "tests" / name, self.root / "tests" / name)
        shutil.copy2(ROOT / "src/bs_corpus_sentinels.h", self.root / "src/bs_corpus_sentinels.h")
        shutil.copytree(ROOT / "project/tests/corpus/parser", self.root / "project/tests/corpus/parser")
        self.registry = json.loads((self.root / "scripts/corpus_sources.json").read_text())
        self.baseline = json.loads((self.root / "tests/corpus_baseline.json").read_text())
        self.suites = json.loads((self.root / "tests/gdscript_suites.json").read_text())

    def check(self):
        for relative, document in (("scripts/corpus_sources.json", self.registry),
                                   ("tests/corpus_baseline.json", self.baseline),
                                   ("tests/gdscript_suites.json", self.suites)):
            (self.root / relative).write_text(json.dumps(document))
        return self.registry_module.validate_registration(self.root)

    def test_committed_and_legacy_parser_metadata(self):
        self.check()
        self.baseline["corpora"]["parser"].pop("imported", None)
        self.check()

    def test_malformed_registry_values(self):
        mutations = [
            ("repository", "someone/Foundry"), ("repository", "cafecito-games/Foundry\ninjected=true"),
            ("revision", "main"), ("revision", "a" * 39), ("revision", "A" * 40),
            ("revision", "a" * 40 + "\ninjected=true"), ("schema_version", 2),
            ("schema_version", True), ("corpora", {}), ("corpora", []),
        ]
        original = copy.deepcopy(self.registry)
        for key, value in mutations:
            with self.subTest(key=key, value=value):
                self.registry = copy.deepcopy(original)
                self.registry[key] = value
                with self.assertRaises(ValueError):
                    self.check()

    def test_zero_active_duplicate_roots_and_malformed_baselines(self):
        original = copy.deepcopy(self.registry)
        for key in ("source", "destination"):
            self.registry = copy.deepcopy(original)
            self.registry["corpora"]["analyzer"][key] = self.registry["corpora"]["parser"][key]
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "duplicate"):
                self.check()
        self.registry = copy.deepcopy(original)
        self.registry["corpora"]["parser"]["state"] = "pending"
        with self.assertRaisesRegex(ValueError, "zero active"):
            self.check()
        self.registry = original
        original_baseline = copy.deepcopy(self.baseline)
        for value in (None, [], "invalid"):
            self.baseline = copy.deepcopy(original_baseline)
            self.baseline["corpora"]["parser"] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.check()
        self.baseline = original_baseline
        self.baseline["corpora"]["analyzer"]["upstream_helpers"] = True
        with self.assertRaisesRegex(ValueError, "upstream_helpers"):
            self.check()

    def test_pending_registration_cannot_be_removed_from_both_documents(self):
        del self.registry["corpora"]["analyzer"]
        del self.baseline["corpora"]["analyzer"]
        with self.assertRaisesRegex(ValueError, "parser and analyzer"):
            self.check()

    def test_importer_symlink_and_missing_file_are_rejected(self):
        path = self.root / "scripts/import_parser_corpus.py"
        path.unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            self.check()
        target = self.root / "scripts/other.py"
        target.write_text("# never execute")
        path.symlink_to(target)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.check()

    def test_paths_and_importer_are_allowlisted(self):
        original = copy.deepcopy(self.registry)
        for key, values in {
            "source": ["../escape", "/tmp/parser", "modules//parser", "modules/parser\nx=y", "modules/parser/.."],
            "destination": ["../escape", "/tmp/parser", "project/tests/corpus/../parser"],
            "importer": ["scripts/evil.py", "scripts/../evil.py", "/tmp/evil.py", "scripts/import_parser_corpus.py --repair"],
            "state": ["disabled", False],
        }.items():
            for value in values:
                with self.subTest(key=key, value=value):
                    self.registry = copy.deepcopy(original)
                    self.registry["corpora"]["parser"][key] = value
                    with self.assertRaises(ValueError):
                        self.check()

    def test_duplicate_json_keys_and_missing_registry(self):
        path = self.root / "scripts/corpus_sources.json"
        path.write_text('{"schema_version": 1, "schema_version": 1}')
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.registry_module.validate_registration(self.root)
        path.unlink()
        with self.assertRaises(ValueError):
            self.registry_module.validate_registration(self.root)

    def test_duplicate_importer_identity(self):
        self.registry["corpora"]["analyzer"]["importer"] = self.registry["corpora"]["parser"]["importer"]
        with self.assertRaises(ValueError):
            self.check()

    def test_baseline_coverage_and_provenance(self):
        original = copy.deepcopy(self.baseline)
        for key, value in (("root", "res://tests/corpus/wrong"), ("foundry_revision", "0" * 40),
                           ("imported", "false"), ("imported", False), ("total", True)):
            with self.subTest(key=key):
                self.baseline = copy.deepcopy(original)
                self.baseline["corpora"]["parser"][key] = value
                with self.assertRaises(ValueError):
                    self.check()
        self.baseline = copy.deepcopy(original)
        self.baseline["corpora"]["unknown"] = copy.deepcopy(original["corpora"]["parser"])
        with self.assertRaisesRegex(ValueError, "registration"):
            self.check()
        self.baseline = {"corpora": {}}
        with self.assertRaises(ValueError):
            self.check()

    def test_imported_corpus_requires_tree_and_importer(self):
        shutil.rmtree(self.root / "project/tests/corpus/parser")
        with self.assertRaisesRegex(ValueError, "tree"):
            self.check()
        self.registry["corpora"]["parser"]["importer"] = None
        with self.assertRaisesRegex(ValueError, "importer"):
            self.check()

    def test_pending_cannot_hide_delivery_or_claim_success(self):
        original = copy.deepcopy(self.baseline)
        for key, value in (("total", 1), ("skipped", 1), ("expected_failures", ["x.barista"])):
            with self.subTest(key=key):
                self.baseline = copy.deepcopy(original)
                self.baseline["corpora"]["analyzer"][key] = value
                with self.assertRaises(ValueError):
                    self.check()
        self.baseline = original
        self.suites["extra_invocations"].append({"script": "res://tests/corpus_runner.gd",
            "args": ["--corpus", "res://tests/corpus/analyzer"], "expect": "^BS_CORPUS 0/0 skipped=0$"})
        with self.assertRaisesRegex(ValueError, "pending"):
            self.check()
        self.suites["extra_invocations"].pop()
        destination = self.root / "project/tests/corpus/analyzer"
        destination.mkdir()
        (destination / "README.md").write_text("hidden delivery")
        with self.assertRaisesRegex(ValueError, "pending"):
            self.check()

    def test_pending_importer_can_be_staged_but_not_counted(self):
        self.registry["corpora"]["analyzer"]["importer"] = "scripts/import_analyzer_corpus.py"
        (self.root / "scripts/import_analyzer_corpus.py").write_text("# staging only")
        self.check()

    def test_imported_requires_exact_runner_invocation(self):
        pin = self.suites["extra_invocations"][-1]
        for key, value in (("expect", "BS_CORPUS"), ("script", "res://tests/other.gd"),
                           ("args", ["--unused", "res://tests/corpus/parser"])):
            original = copy.deepcopy(pin)
            pin[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check()
            pin.clear()
            pin.update(original)


class WorkflowContract(unittest.TestCase):
    def setUp(self):
        import validate_ci
        import yaml
        self.audit = getattr(validate_ci, "check_corpus_reproducibility_wiring", None)
        self.assertIsNotNone(self.audit, "structural corpus job audit missing")
        self.yaml = yaml
        self.workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        self.document = yaml.load(self.workflow, Loader=yaml.BaseLoader)

    def test_current_workflow(self):
        self.assertIsNone(self.audit(self.workflow))

    def test_gate_omission_suppression_and_mutable_checkout(self):
        original = copy.deepcopy(self.document)
        mutations = [
            lambda doc: doc["jobs"].pop("corpus-reproducibility"),
            lambda doc: doc["jobs"]["corpus-reproducibility"].update({"if": "false"}),
            lambda doc: doc["jobs"]["corpus-reproducibility"].update({"continue-on-error": "true"}),
            lambda doc: doc["jobs"]["corpus-reproducibility"].update({"strategy": {"matrix": {"x": [1, 2]}}}),
            lambda doc: doc["on"].pop("merge_group"),
            lambda doc: doc["on"].update({"pull_request_target": {}}),
        ]
        for mutation in mutations:
            document = copy.deepcopy(original)
            mutation(document)
            with self.subTest(mutation=mutation):
                self.assertIsNotNone(self.audit(self.yaml.safe_dump(document)))
        steps = original["jobs"]["corpus-reproducibility"]["steps"]
        for index, step in enumerate(steps):
            for key, value in (("if", "false"), ("continue-on-error", "true"), ("shell", "bash {0}")):
                document = copy.deepcopy(original)
                document["jobs"]["corpus-reproducibility"]["steps"][index][key] = value
                with self.subTest(index=index, key=key):
                    self.assertIsNotNone(self.audit(self.yaml.safe_dump(document)))
            if "run" in step:
                for command in (step["run"] + " || true", "echo '" + step["run"] + "'", "# " + step["run"]):
                    document = copy.deepcopy(original)
                    document["jobs"]["corpus-reproducibility"]["steps"][index]["run"] = command
                    with self.subTest(index=index, command=command):
                        self.assertIsNotNone(self.audit(self.yaml.safe_dump(document)))
            if step.get("with", {}).get("path") == ".upstream-foundry":
                for key, value in (("ref", "main"), ("repository", "cafecito-games/Foundry"),
                                   ("persist-credentials", "true"), ("fetch-depth", "0"),
                                   ("submodules", "recursive"), ("sparse-checkout", "modules")):
                    document = copy.deepcopy(original)
                    document["jobs"]["corpus-reproducibility"]["steps"][index]["with"][key] = value
                    with self.subTest(key=key, value=value):
                        self.assertIsNotNone(self.audit(self.yaml.safe_dump(document)))

    def test_bad_yaml_duplicate_keys_and_unrelated_comment(self):
        for text in ("jobs: [", "jobs: {}\njobs: {}", "# corpus-reproducibility\njobs: {}"):
            self.assertIsNotNone(self.audit(text))


class WrapperContract(unittest.TestCase):
    setUp = RegistryContract.setUp
    check = RegistryContract.check
    def invoke(self, arguments):
        import check_corpus_reproducibility as wrapper
        with patch.object(wrapper, "ROOT", self.root):
            return wrapper.main(arguments)

    def test_output_mode_is_local_validated_and_fixed_key(self):
        output = self.root / "outputs"
        with patch("subprocess.run", side_effect=AssertionError("output mode executed a process")):
            self.assertEqual(self.invoke(["--github-output", str(output)]), 0)
        text = output.read_text()
        self.assertEqual(text, "repository=" + self.registry["repository"] + "\nrevision=" + self.registry["revision"]
                         + "\nsparse_paths<<CORPUS_PATHS_END\n"
                         + "\n".join(sorted(item["source"] for item in self.registry["corpora"].values()))
                         + "\nCORPUS_PATHS_END\n")
        self.registry["repository"] += "\ninjected=true"
        (self.root / "scripts/corpus_sources.json").write_text(json.dumps(self.registry))
        with patch("subprocess.run", side_effect=AssertionError("invalid registry executed a process")), redirect_stderr(io.StringIO()):
            self.assertNotEqual(self.invoke(["--github-output", str(output)]), 0)
        self.assertEqual(output.read_text(), text)

    def test_importer_exit_and_pending_reporting(self):
        import check_corpus_reproducibility as wrapper
        for status in (0, 3, -9):
            stdout, stderr = io.StringIO(), io.StringIO()
            with patch.object(wrapper, "verify_checkout"), patch.object(wrapper.subprocess, "run") as run, redirect_stdout(stdout), redirect_stderr(stderr):
                run.return_value.returncode = status
                result = self.invoke(["--foundry", str(self.root / "upstream")])
            self.assertEqual(result == 0, status == 0)
            self.assertIn("pending corpus analyzer", stdout.getvalue())
            self.assertEqual("checked corpus parser" in stdout.getvalue(), status == 0)
            self.assertEqual(run.call_args.args[0], [sys.executable, str(self.root / "scripts/import_parser_corpus.py"),
                "--foundry", str((self.root / "upstream").resolve()), "--revision", self.registry["revision"], "--check"])
            self.assertNotIn("shell", run.call_args.kwargs)
            if status:
                self.assertIn("scripts/import_parser_corpus.py failed", stderr.getvalue())

    def test_checkout_failure_stops_importers(self):
        import check_corpus_reproducibility as wrapper
        with patch.object(wrapper, "verify_checkout", side_effect=ValueError("missing source root")), patch.object(wrapper.subprocess, "run") as run, redirect_stderr(io.StringIO()):
            self.assertNotEqual(self.invoke(["--foundry", str(self.root)]), 0)
            run.assert_not_called()

    def test_future_analyzer_delivery_uses_same_wrapper_in_sorted_order(self):
        self.registry["corpora"]["analyzer"].update(state="active", importer="scripts/import_analyzer_corpus.py")
        (self.root / "scripts/import_analyzer_corpus.py").write_text("# registered staging importer; execution mocked here")
        destination = self.root / "project/tests/corpus/analyzer"
        destination.mkdir()
        (destination / "one.barista").write_text("func test(): pass")
        self.baseline["corpora"]["analyzer"].update(imported=True, total=1, upstream_total=1)
        self.suites["extra_invocations"].append({"script": "res://tests/corpus_runner.gd",
            "args": ["--corpus", "res://tests/corpus/analyzer"], "expect": "^BS_CORPUS 1/1 skipped=0$"})
        self.check()
        import check_corpus_reproducibility as wrapper
        with patch.object(wrapper, "verify_checkout"), patch.object(wrapper.subprocess, "run") as run, redirect_stdout(io.StringIO()) as stdout:
            run.return_value.returncode = 0
            self.assertEqual(self.invoke(["--foundry", str(self.root)]), 0)
        self.assertEqual([Path(call.args[0][1]).name for call in run.call_args_list],
                         ["import_analyzer_corpus.py", "import_parser_corpus.py"])
        self.assertNotIn("pending", stdout.getvalue())


class CheckoutContract(unittest.TestCase):
    def setUp(self):
        import corpus_registry
        self.module = corpus_registry
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.registry = copy.deepcopy(corpus_registry.load_registry(ROOT))
        self.git("init", "--quiet")
        self.git("remote", "add", "origin", "https://github.com/" + self.registry["repository"] + ".git")
        for record in self.registry["corpora"].values():
            path = self.root / record["source"]
            path.mkdir(parents=True)
            (path / "input.fs").write_text("data only")
        self.git("add", ".")
        self.git("-c", "user.name=Offline Test", "-c", "user.email=test@example.invalid", "commit", "--quiet", "-m", "fixture")
        self.registry["revision"] = self.git("rev-parse", "HEAD")

    def git(self, *arguments):
        result = subprocess.run(["git", "-C", str(self.root), *arguments], capture_output=True, text=True, check=True)
        return result.stdout.strip()

    def verify(self):
        self.module.verify_checkout(self.root, self.registry, self.registry["revision"])

    def test_clean_checkout_and_unrelated_parent_cache(self):
        self.verify()
        cache = self.root / "modules/foundry_script/tests/scripts/.foundry/autoload_index_cache.cfg"
        cache.parent.mkdir()
        cache.write_text("not consumed")
        self.verify()
        self.assertEqual(cache.read_text(), "not consumed")

    def test_wrong_head_requested_revision_and_origin(self):
        with self.assertRaisesRegex(ValueError, "requested revision"):
            self.module.verify_checkout(self.root, self.registry, "main")
        self.registry["revision"] = "0" * 40
        with self.assertRaisesRegex(ValueError, "HEAD"):
            self.verify()
        self.registry["revision"] = self.git("rev-parse", "HEAD")
        self.git("remote", "set-url", "origin", "https://github.com/other/Foundry.git")
        with self.assertRaisesRegex(ValueError, "origin"):
            self.verify()

    def test_dirty_untracked_ignored_and_missing_sparse_roots(self):
        for record in self.registry["corpora"].values():
            source = self.root / record["source"]
            path = source / "input.fs"
            path.write_text("dirty")
            with self.assertRaisesRegex(ValueError, "uncommitted"):
                self.verify()
            self.git("checkout", "--", str(path))
            extra = source / "extra.fs"
            extra.write_text("untracked")
            with self.assertRaisesRegex(ValueError, "extra.fs"):
                self.verify()
            extra.unlink()
            (self.root / ".git/info/exclude").write_text("ignored.fs\n")
            ignored = source / "ignored.fs"
            ignored.write_text("ignored but consumed")
            with self.assertRaisesRegex(ValueError, "ignored.fs"):
                self.verify()
            ignored.unlink()
        source.rename(source.with_name("missing"))
        with self.assertRaisesRegex(ValueError, "sparse source root missing"):
            self.verify()


class OfflineProducer(unittest.TestCase):
    """Run the real producer on exact upstream fixture bytes; no mocked producer."""
    def setUp(self):
        import import_parser_corpus
        self.importer = import_parser_corpus
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        fixtures = ROOT / "tests/fixtures/corpus_reproducibility"
        provenance = json.loads((fixtures / "provenance.json").read_text())
        self.assertEqual(provenance["revision"], self.importer.FOUNDRY_REVISION)
        source = self.root / self.importer.CORPUS_SUBPATH
        shutil.copytree(fixtures / "parser", source)
        self.assertEqual(set(provenance["files"]), {p.relative_to(source).as_posix() for p in source.rglob("*") if p.is_file()})
        for name, record in provenance["files"].items():
            self.assertEqual(hashlib.sha256((source / name).read_bytes()).hexdigest(), record["sha256"])
            self.assertEqual(record["source"], (self.importer.CORPUS_SUBPATH / name).as_posix())
        self.fresh = self.root / "fresh"
        summary = self.importer.import_corpus(self.root, self.fresh)
        self.importer.write_readme(self.fresh, summary)
        self.committed = self.root / "committed"
        shutil.copytree(self.fresh, self.committed)

    def test_real_producer_detects_same_count_bytes_additions_and_removals(self):
        self.assertEqual(self.importer.compare_trees(self.committed, self.fresh), [])
        paths = [next(self.committed.rglob("*.barista")), next(self.committed.rglob("*.out")), self.committed / "README.md"]
        for path in paths:
            original = path.read_bytes()
            path.write_bytes(bytes([original[0] ^ 1]) + original[1:])
            with self.subTest(path=path.name):
                self.assertIn("differs from a fresh import: " + path.relative_to(self.committed).as_posix(),
                              self.importer.compare_trees(self.committed, self.fresh))
            path.write_bytes(original)
        extra = self.committed / "case_stages.json"
        extra.write_text('{"future": "stage"}')
        self.assertIn("only in the committed tree: case_stages.json", self.importer.compare_trees(self.committed, self.fresh))
        # Generic comparison also covers the future manifest once a producer emits it.
        (self.fresh / extra.name).write_bytes(extra.read_bytes())
        extra.write_text('{"future": "drift"}')
        self.assertIn("differs from a fresh import: case_stages.json", self.importer.compare_trees(self.committed, self.fresh))
        extra.unlink()
        self.assertIn("only in a fresh import: case_stages.json", self.importer.compare_trees(self.committed, self.fresh))
        path = paths[0]
        path.unlink()
        self.assertIn("only in a fresh import: " + path.relative_to(self.committed).as_posix(),
                      self.importer.compare_trees(self.committed, self.fresh))

    def test_fixture_source_drift_changes_real_producer_bytes(self):
        source = next((self.root / self.importer.CORPUS_SUBPATH).rglob("*.notest.fs"))
        source.write_bytes(source.read_bytes() + b"\n# changed input\n")
        changed = self.root / "changed"
        summary = self.importer.import_corpus(self.root, changed)
        self.importer.write_readme(changed, summary)
        self.assertTrue(self.importer.compare_trees(self.fresh, changed))


FOUNDRY = None


class FullProducer(unittest.TestCase):
    """Real pinned-source CLI in disposable repository copies, when supplied."""
    def setUp(self):
        if FOUNDRY is None:
            self.skipTest("full pinned-source integration needs --foundry; miniature producer tests ran offline")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for directory in ("scripts", "tests", "src"):
            (self.root / directory).mkdir()
        # Importer dependencies and pending staged importers evolve together.
        # Copy the reviewed local producer tree, never code from Foundry.
        shutil.copytree(ROOT / "scripts", self.root / "scripts", dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        for name in ("corpus_baseline.json", "gdscript_suites.json"):
            shutil.copy2(ROOT / "tests" / name, self.root / "tests" / name)
        shutil.copy2(ROOT / "src/bs_corpus_sentinels.h", self.root / "src/bs_corpus_sentinels.h")
        registry = json.loads((self.root / "scripts/corpus_sources.json").read_text())
        for record in registry["corpora"].values():
            destination = record["destination"]
            if (ROOT / destination).is_dir():
                shutil.copytree(ROOT / destination, self.root / destination)
        self.revision = registry["revision"]

    def snapshot(self, root):
        return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in root.rglob("*") if p.is_file() and "__pycache__" not in p.parts}

    def run_importer(self):
        before = self.snapshot(self.root)
        result = subprocess.run([sys.executable, str(self.root / "scripts/import_parser_corpus.py"),
            "--foundry", str(FOUNDRY), "--revision", self.revision, "--check"], capture_output=True, text=True)
        self.assertEqual(self.snapshot(self.root), before, "--check modified its inputs")
        return result

    def test_real_pin_clean_repeat_and_mutated_bytes(self):
        original_workspace = self.snapshot(ROOT / "project/tests/corpus/parser")
        for _ in range(2):
            result = self.run_importer()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("340 cases", result.stdout)
        destination = self.root / "project/tests/corpus/parser"
        for path in (next(destination.rglob("*.barista")), next(destination.rglob("*.out")), destination / "README.md"):
            original = path.read_bytes()
            path.write_bytes(bytes([original[0] ^ 1]) + original[1:])
            result = self.run_importer()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("differs from a fresh import: " + path.relative_to(destination).as_posix(), result.stdout)
            print("REAL SAME-COUNT DRIFT (expected failure): " + result.stdout.strip(), flush=True)
            path.write_bytes(original)
        for relative in ("extra.txt", "case_stages.json"):
            extra = destination / relative
            extra.write_text("unexpected generated file")
            result = self.run_importer()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(relative, result.stdout)
            extra.unlink()
        missing = next(destination.rglob("*.out"))
        original = missing.read_bytes()
        missing.unlink()
        result = self.run_importer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(missing.name, result.stdout)
        missing.write_bytes(original)
        self.assertEqual(self.snapshot(ROOT / "project/tests/corpus/parser"), original_workspace)

    def test_explicit_regeneration_can_repair_a_missing_tree(self):
        shutil.rmtree(self.root / "project/tests/corpus/parser")
        result = subprocess.run([sys.executable, str(self.root / "scripts/import_parser_corpus.py"),
            "--foundry", str(FOUNDRY), "--revision", self.revision], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.run_importer().returncode, 0)

    def test_owned_metadata_mutations_fail_real_check(self):
        path = self.root / "tests/corpus_baseline.json"
        original = path.read_text()
        for field in ("foundry_revision", "analyzer_deferred", "triage"):
            baseline = json.loads(original)
            parser = baseline["corpora"]["parser"]
            if field == "foundry_revision":
                parser[field] = "0" * 40
            elif field == "analyzer_deferred":
                parser[field][next(iter(parser[field]))] += " drift"
            else:
                parser[field]["rewritten"][next(iter(parser[field]["rewritten"]))] += " drift"
            path.write_text(json.dumps(baseline))
            result = self.run_importer()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("provenance" if field == "foundry_revision" else field, result.stdout + result.stderr)
        path.write_text(original)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--foundry", type=Path)
    arguments, remaining = parser.parse_known_args()
    FOUNDRY = arguments.foundry.resolve() if arguments.foundry else None
    unittest.main(argv=[sys.argv[0], *remaining])
