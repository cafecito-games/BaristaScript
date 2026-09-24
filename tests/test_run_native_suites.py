#!/usr/bin/env python3
# test_run_native_suites.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Failure propagation tests; --godot additionally exercises actual native subprocesses."""
import argparse
import json
import hashlib
import re
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

import run_native_suites as runner

GODOT = None
BUILD_DIR = runner.DEFAULT_BUILD_DIR


class AnalyzerParityInventoryTests(unittest.TestCase):
    def test_manifest_is_exact_unique_additive_56_suite_union(self):
        suites = json.loads((runner.ROOT / "tests/native_suites.json").read_text())["suites"]
        expected = [
            "tokenizer", "parser", "warnings", "platform", "cache", "declaration_index",
            "global_class", "cross_file_analyzer", "provider_analyzer", "preload_analyzer",
            "refresh_analyzer", "analyzer_flow", "analyzer_cache", "analyzer_dependency",
            "analyzer_diagnostics", "analyzer_calls", "analyzer_finality",
            "analyzer_declarations", "analyzer_operations", "analyzer_conformance",
            "analyzer_members", "analyzer_enum", "analyzer_constants",
            "analyzer_type_compatibility", "source_analyzer", "autoload_analyzer",
            "trait_surface_analyzer", "witness_scope_analyzer", "variadic_callable_analyzer",
            "builtin_metadata_analyzer", "get_node_analyzer", "numeric_consumer_analyzer",
            "warning_producer_analyzer", "concrete_type_analyzer", "value_consumer_analyzer",
            "expression_consumer_analyzer", "abstract_contract_analyzer",
            "parent_contract_analyzer", "provider_phase_analyzer", "namespace_annotation_analyzer",
            "declaration_context_analyzer", "enum_call_dispatch_analyzer",
            "call_admission_analyzer", "analyzer_resolution", "declaration_cycle_analyzer",
            "analyzer_tuple", "provider_failure_replay_analyzer", "base_outer_analyzer",
            "external_enum_constant_analyzer", "top_level_enum_analyzer",
            "corpus_evaluation", "analyzer_corpus", "runtime_corpus", "runtime",
            "runtime_expressions", "runtime_scene",
        ]
        self.assertEqual(expected, suites)
        self.assertEqual(len(expected), len(set(suites)))

    def test_current_analyzer_inventory_maps_all_91_legacy_scenarios(self):
        legacy = (runner.ROOT / "project/tests/analyzer_test.gd").read_text()
        init = legacy.split("func _init() -> void:", 1)[1].split("\n\nfunc ", 1)[0]
        invoked = re.findall(r"^\s*(_test_[a-z0-9_]+)\(failures\)\s*$", init, re.MULTILINE)
        parity = (runner.ROOT / "tests/native/analyzer-parity.md").read_text()
        rows = re.findall(
            r"^\|\s*\d+\s*\|\s*`(_test_[a-z0-9_]+)`\s*\|[^|]*\|\s*([^|]+?)\s*\|$",
            parity,
            re.MULTILINE,
        )
        documented = [function for function, _status in rows]

        self.assertEqual(91, len(invoked))
        self.assertEqual(91, len(set(invoked)))
        self.assertEqual(invoked, documented)

        registrations = {}
        for source_path in (runner.ROOT / "tests/native").glob("*.cpp"):
            source = source_path.read_text()
            suites = re.findall(r'TEST_SUITE\("([a-z0-9_]+)"\)', source)
            if len(suites) == 1:
                registrations.setdefault(suites[0], set()).update(
                    re.findall(r'^\s*TEST_CASE\("([a-z0-9_]+)"\)', source, re.MULTILINE)
                )

        typed_container_cases = {
            "type_union_in_typed_container_reports_exact_child_diagnostics",
            "self_union_in_typed_container_reports_exact_child_diagnostics",
            "typed_container_union_rejection_preserves_neighboring_type_forms",
            "malformed_typed_container_arity_reports_once_and_recovers",
        }
        concrete_source = (runner.ROOT / "tests/native/concrete_type_analyzer_tests.cpp").read_text()
        typed_container_contracts = (
            'CHECK_FALSE(bool(first_public_report.get("valid", true)))',
            '_validate(source, path, true, true, true, false) == first_public_report',
            'CHECK_FALSE(source_analyzes(source, path))',
            'A type handle cannot represent the type union "String | int"',
            'Typed arrays require exactly one collection element type.',
            'Typed dictionaries require exactly two collection element types.',
            '{ "Typed arrays require exactly one collection element type.", 2, 23 }',
            '{ "Typed dictionaries require exactly two collection element types.", 2, 28 }',
            '{ "Typed dictionaries require exactly two collection element types.", 2, 27 }',
            '{ "Typed arrays require exactly one collection element type.", 3, 20 }',
            '{ "Typed dictionaries require exactly two collection element types.", 4, 25 }',
            '{ "Typed dictionaries require exactly two collection element types.", 5, 30 }',
        )
        for contract in typed_container_contracts:
            self.assertIn(contract, concrete_source)
        for function, status in rows:
            if function == "_test_typed_container_union_rejection":
                self.assertEqual("native `concrete_type_analyzer`", status)
                self.assertTrue(typed_container_cases <= registrations["concrete_type_analyzer"])
                continue
            if status == "native #139":
                suite = "analyzer_flow"
            else:
                match = re.fullmatch(r"native\s+`?([a-z0-9_]+)`?", status)
                self.assertIsNotNone(match, f"unrecognized parity status for {function}: {status}")
                suite = match.group(1)
            case = function.removeprefix("_test_")
            self.assertIn(case, registrations.get(suite, set()), f"{function} is not registered in {suite}")


class StagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "checkout"
        self.data = Path(self.temporary.name) / "data"
        self.data.mkdir()
        project = self.root / "project"
        (project / ".godot").mkdir(parents=True)
        (project / "project.godot").write_text("[application]\nconfig/name=\"fixture\"\n")
        (project / ".godot/extension_list.cfg").write_text("res://bin/barista_script.gdextension\n")
        self.library = self.root / "native.so"
        self.library.write_bytes(b"selected test artifact")
        self.api_dir = self.root / "godot-cpp/gdextension"
        self.api_dir.mkdir(parents=True)
        self.api_bytes = {"4.6": b'{"producer":"pinned-4.6"}', "4.7": b'{"producer":"pinned-4.7"}'}
        for version, content in self.api_bytes.items():
            (self.api_dir / ("extension_api-" + version.replace(".", "-") + ".json")).write_bytes(content)
        self.enterContext(mock.patch.object(runner, "ROOT", self.root))
        self.enterContext(mock.patch.object(runner, "godot_data_path", return_value=self.data))
        self.config = self.enterContext(mock.patch.object(runner, "load_config", return_value={"godot_api": "4.7"}))

    def test_only_selected_pinned_api_is_copied_and_cleaned(self):
        for version, content in self.api_bytes.items():
            with self.subTest(version=version):
                self.config.return_value = {"godot_api": version}
                with runner.staged_project({"library": str(self.library)}) as project:
                    api_dir = project.parent / "godot-cpp/gdextension"
                    expected = api_dir / ("extension_api-" + version.replace(".", "-") + ".json")
                    self.assertEqual(content, expected.read_bytes())
                    self.assertEqual([expected], list(api_dir.iterdir()))
                    self.assertEqual(b"selected test artifact", (project / "bin/native.so").read_bytes())
                self.assertFalse(project.parent.exists())
                self.assertEqual([], list(self.data.iterdir()))
        for version, content in self.api_bytes.items():
            self.assertEqual(content, (self.api_dir / ("extension_api-" + version.replace(".", "-") + ".json")).read_bytes())

    def test_missing_selected_api_fails_without_fallback_and_cleans(self):
        (self.api_dir / "extension_api-4-7.json").unlink()
        with self.assertRaises(FileNotFoundError):
            with runner.staged_project({"library": str(self.library)}):
                pass
        self.assertEqual([], list(self.data.iterdir()))
        self.assertTrue((self.api_dir / "extension_api-4-6.json").is_file())

    def test_api_copy_failure_cleans_disposable_project(self):
        copy2 = runner.shutil.copy2

        def fail_api_copy(source, destination, *args, **kwargs):
            if Path(source) == self.api_dir / "extension_api-4-7.json":
                raise OSError("selected API copy failed")
            return copy2(source, destination, *args, **kwargs)

        with mock.patch.object(runner.shutil, "copy2", side_effect=fail_api_copy):
            with self.assertRaisesRegex(OSError, "selected API copy failed"):
                with runner.staged_project({"library": str(self.library)}):
                    pass
        self.assertEqual([], list(self.data.iterdir()))


class ResultTests(unittest.TestCase):
    def setUp(self):
        config = runner.load_config()
        self.info = runner.create_metadata(runner.ROOT, config, dict(platform="linux", architecture="x86_64",
                                           target="template_debug", api=config["godot_api"], precision=config["precision"]), native_tests=True)

    def record(self, **overrides):
        record = dict(protocol=runner.PROTOCOL_VERSION, suite="tokenizer", case="", nonce="run-1",
                      build_id="build-1", cases=13, assertions=600, failed_cases=0,
                      failed_assertions=0, build_info=self.info)
        record.update(overrides)
        return runner.RESULT_PREFIX + json.dumps(record)

    def evaluate(self, output, code=0):
        return runner.evaluate(code, output, "tokenizer", "", "run-1", "build-1", self.info)

    def test_only_matching_executed_completion_is_success(self):
        self.assertEqual([], self.evaluate(self.record()))

    def test_exit_zero_empty_and_query_are_not_evidence(self):
        for output in ("", "[doctest] listing test cases", "[doctest] all tests passed"):
            with self.subTest(output=output):
                self.assertTrue(self.evaluate(output))

    def test_crash_and_assertion_failure(self):
        for code in (-11, 1):
            self.assertTrue(self.evaluate(self.record(), code))
        for field in ("failed_cases", "failed_assertions"):
            self.assertTrue(self.evaluate(self.record(**{field: 1})))

    def test_empty_unknown_and_mismatched_results(self):
        for key, value in dict(protocol=99, suite="other", case="other", nonce="old",
                               build_id="wrong", build_info={}, cases=0, assertions=0).items():
            with self.subTest(key=key):
                self.assertTrue(self.evaluate(self.record(**{key: value})))
        self.assertTrue(self.evaluate(self.record() + "\n" + self.record()))
        self.assertTrue(self.evaluate(runner.RESULT_PREFIX + "not json"))
        self.assertTrue(self.evaluate(self.record(cases=True)))
        self.assertTrue(self.evaluate(self.record(protocol=True)))
        self.assertTrue(self.evaluate(self.record(extra=1)))

    def test_timeout_is_failure_with_diagnostics(self):
        result = runner.supervise([sys.executable, "-c", "import time; time.sleep(5)"], 0.05)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("timeout", result.stdout)

    def test_ci_requires_unsuppressed_whole_manifest_execution(self):
        import validate_ci
        header = "jobs:\n  build:\n    steps:\n"
        good = header + '      - name: Native tests\n        run: python3 tests/run_native_suites.py --godot "$godot_binary"\n'
        self.assertIsNone(validate_ci.check_native_suite_wiring(good))
        for bad in (good.replace("--godot", "--list --godot"),
                    good.replace("--godot", "--suite tokenizer --godot"),
                    good.replace("--godot", "--case some_case --godot"),
                    good.replace('"$godot_binary"', '"$godot_binary" || true'),
                    good.replace("        run:", "        continue-on-error: true\n        run:"), ""):
            self.assertIsNotNone(validate_ci.check_native_suite_wiring(bad))

    def test_ci_requires_a_potentially_reachable_native_runner(self):
        import validate_ci
        header = "jobs:\n  build:\n    steps:\n"
        command = '        run: python3 tests/run_native_suites.py --godot "$godot_binary"\n'
        for condition in ("false", "'false'", '\"false\"', "'  false  '", "${{ false }}",
                          "'${{ false }}'", '\"${{   false   }}\"',
                          '\"  ${{ false }}  \"', "false # disabled"):
            with self.subTest(rejected=condition):
                workflow = header + "      - if: %s\n" % condition + command
                self.assertIsNotNone(validate_ci.check_native_suite_wiring(workflow))
        for condition in (None, "true", "${{ true }}", "${{ matrix.enabled }}",
                          "${{ false || matrix.enabled }}", "${{ 'false' }}"):
            with self.subTest(accepted=condition):
                if_line = "" if condition is None else "      - if: %s\n" % condition
                run_line = command if condition is not None else "      - run:" + command.split("run:", 1)[1]
                workflow = header + if_line + run_line
                self.assertIsNone(validate_ci.check_native_suite_wiring(workflow))

    def test_ci_requires_a_potentially_reachable_native_runner_job(self):
        import validate_ci
        command = '    steps:\n      - run: python3 tests/run_native_suites.py --godot "$godot_binary"\n'
        for condition in ("false", "'false'", '\"false\"', "'  false  '", "${{ false }}",
                          "'${{ false }}'", '\"${{   false   }}\"',
                          '\"  ${{ false }}  \"', "false # disabled"):
            with self.subTest(rejected=condition):
                workflow = "jobs:\n  build:\n    if: %s\n" % condition + command
                self.assertIsNotNone(validate_ci.check_native_suite_wiring(workflow))
        for condition in (None, "true", "${{ true }}", "${{ matrix.enabled }}",
                          "${{ false || matrix.enabled }}", "${{ 'false' }}"):
            with self.subTest(accepted=condition):
                if_line = "" if condition is None else "    if: %s\n" % condition
                workflow = "jobs:\n  build:\n" + if_line + command
                self.assertIsNone(validate_ci.check_native_suite_wiring(workflow))

    def test_unknown_manifest_suite_is_rejected(self):
        self.assertRaises(ValueError, runner.select_suites, ["missing"])

    def test_disabled_build_has_no_native_framework(self):
        import verify_native_surface
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assertTrue(verify_native_surface.verify_surface(root, "template_debug"))
            binary = root / "library.template_debug.so"
            binary.write_bytes(b"ordinary extension")
            self.assertEqual([], verify_native_surface.verify_surface(root, "template_debug"))
            for marker in verify_native_surface.NATIVE_MARKERS + verify_native_surface.RETIRED_ADAPTER_MARKERS:
                binary.write_bytes(b"ordinary extension" + marker)
                self.assertTrue(verify_native_surface.verify_surface(root, "template_debug"))

    def test_wrong_or_stale_artifact_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "native.so"
            from build_metadata import encode_envelope
            library.write_bytes(encode_envelope(self.info))
            artifact = dict(library=str(library), sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
                            build_id=runner.build_identity(), build_info=self.info)
            metadata = root / "native-artifact.json"
            metadata.write_text(json.dumps(artifact))
            self.assertEqual(artifact, runner.read_artifact(root))
            library.write_bytes(b"wrong artifact")
            self.assertRaises(ValueError, runner.read_artifact, root)
            library.write_bytes(encode_envelope(self.info))
            artifact["build_id"] = "previous source revision"
            metadata.write_text(json.dumps(artifact))
            self.assertRaises(ValueError, runner.read_artifact, root)

    def test_missing_library_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            self.assertRaises((ValueError, OSError), runner.read_artifact, Path(temporary))


@unittest.skipUnless(GODOT, "pass --godot to run real native subprocess checks")
class RuntimeTests(unittest.TestCase):
    def test_runtime_state_is_disposable(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            state = project.parent
            completed = runner.invoke(GODOT, project, "tokenizer", "integer_range_is_exact", "state-check", 60)
            self.assertEqual([], runner.evaluate(completed.returncode, completed.stdout, "tokenizer",
                              "integer_range_is_exact", "state-check", artifact["build_id"], artifact["build_info"]), completed.stdout)
            self.assertTrue((state / "logs/godot.log").is_file(), "Godot user:// logs must stay in disposable state")
        self.assertFalse(state.exists())

    def test_real_assertion_failure(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            completed = runner.invoke(GODOT, project, "runner_failure", "intentional failing assertion", "failure-check", 60)
        reasons = runner.evaluate(completed.returncode, completed.stdout, "runner_failure", "intentional failing assertion",
                                  "failure-check", artifact["build_id"], artifact["build_info"])
        self.assertTrue(reasons, completed.stdout)
        self.assertNotEqual(completed.returncode, 0, completed.stdout)
        self.assertIn('"failed_assertions":1', completed.stdout)

    def test_warning_settings_restore_after_real_failed_assertions(self):
        artifact = runner.read_artifact(BUILD_DIR)
        case = "warning settings restore after failed assertions"
        with runner.staged_project(artifact) as project:
            completed = runner.invoke(GODOT, project, "runner_failure", case, "settings-cleanup", 60)
        self.assertNotEqual(completed.returncode, 0, completed.stdout)
        records = [json.loads(line[len(runner.RESULT_PREFIX):]) for line in completed.stdout.splitlines()
                   if line.startswith(runner.RESULT_PREFIX)]
        self.assertEqual(len(records), 1, completed.stdout)
        record = records[0]
        self.assertEqual(record["cases"], 1, completed.stdout)
        self.assertEqual(record["failed_cases"], 1, completed.stdout)
        self.assertEqual(record["failed_assertions"], 2, completed.stdout)
        self.assertGreater(record["assertions"], 2, completed.stdout)
        self.assertIn("settings restoration checked after both failed assertions", completed.stdout)

    def test_missing_library_subprocess(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            for library in (project / "bin").glob("native.*"):
                library.unlink()
            completed = runner.invoke(GODOT, project, "tokenizer", "", "missing-check", 5)
        self.assertTrue(runner.evaluate(completed.returncode, completed.stdout, "tokenizer", "",
                                        "missing-check", artifact["build_id"], artifact["build_info"]))
        self.assertNotIn(runner.RESULT_PREFIX, completed.stdout)

    def test_wrong_library_cannot_supply_runner(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            for library in (project / "bin").glob("native.*"):
                library.write_bytes(b"not a Godot extension")
            completed = runner.invoke(GODOT, project, "tokenizer", "", "wrong-library-check", 5)
        self.assertTrue(runner.evaluate(completed.returncode, completed.stdout, "tokenizer", "",
                                        "wrong-library-check", artifact["build_id"], artifact["build_info"]))
        self.assertNotIn(runner.RESULT_PREFIX, completed.stdout)

    def test_complete_corpus_argument_pair_is_accepted(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            completed = runner.invoke(GODOT, project, "tokenizer", "integer_range_is_exact",
                                      "corpus-accept", 60, corpus_root=str(project),
                                      corpus_case="cases/example.barista")
        self.assertEqual([], runner.evaluate(completed.returncode, completed.stdout, "tokenizer",
                          "integer_range_is_exact", "corpus-accept", artifact["build_id"],
                          artifact["build_info"]), completed.stdout)

    def invoke_runner_arguments(self, artifact, corpus_arguments):
        """Runs the native runner with corpus arguments `invoke` cannot express."""
        with runner.staged_project(artifact) as project:
            command = [str(GODOT), "--headless", "--path", str(project), "--main-loop",
                       "BaristaNativeTestRunner", "--", "--native-suite=tokenizer",
                       "--native-case=", "--native-nonce=corpus-argument-check", *corpus_arguments]
            return runner.supervise(command, 60)

    def test_a_corpus_case_without_a_root_is_rejected(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            completed = runner.invoke(GODOT, project, "tokenizer", "", "corpus-pair-check", 60,
                                      corpus_case="cases/example.barista")
        self.assertEqual(2, completed.returncode, completed.stdout)
        self.assertIn("--corpus-case= requires --corpus-root=", completed.stdout)

    def test_a_corpus_root_alone_selects_the_whole_corpus(self):
        # A root with no case names the whole corpus; it is not an incomplete pair.
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            completed = runner.invoke(GODOT, project, "tokenizer", "integer_range_is_exact",
                                      "corpus-root-alone", 60, corpus_root=str(project))
        self.assertEqual([], runner.evaluate(completed.returncode, completed.stdout, "tokenizer",
                          "integer_range_is_exact", "corpus-root-alone", artifact["build_id"],
                          artifact["build_info"]), completed.stdout)

    def test_whole_corpus_arguments_are_rejected_for_a_single_case(self):
        artifact = runner.read_artifact(BUILD_DIR)
        single_case = ["--corpus-root=/tmp/corpus", "--corpus-case=cases/example.barista"]
        for extra, message in (
                (["--corpus-order=reverse"], "only for a whole-corpus run"),
                (["--corpus-shard=0", "--corpus-shards=2"], "only for a whole-corpus run")):
            with self.subTest(extra=extra):
                completed = self.invoke_runner_arguments(artifact, single_case + extra)
                self.assertEqual(2, completed.returncode, completed.stdout)
                self.assertIn(message, completed.stdout)

    def test_malformed_whole_corpus_arguments_are_rejected(self):
        artifact = runner.read_artifact(BUILD_DIR)
        whole_corpus = ["--corpus-root=/tmp/corpus"]
        for extra, message in (
                (["--corpus-order=sideways"], "accepts ascending or reverse"),
                (["--corpus-shard=0"], "required together"),
                (["--corpus-shards=2"], "required together"),
                (["--corpus-shard=first", "--corpus-shards=2"], "are integers"),
                (["--corpus-shard=2", "--corpus-shards=2"], "must name one of"),
                (["--corpus-shard=-1", "--corpus-shards=2"], "must name one of"),
                (["--corpus-shard=0", "--corpus-shards=0"], "must name one of")):
            with self.subTest(extra=extra):
                completed = self.invoke_runner_arguments(artifact, whole_corpus + extra)
                self.assertEqual(2, completed.returncode, completed.stdout)
                self.assertIn(message, completed.stdout)

    def test_unknown_argument_is_still_rejected(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            # `invoke` cannot emit an unrecognized flag, so the command line is built directly.
            command = [str(GODOT), "--headless", "--path", str(project), "--main-loop",
                       "BaristaNativeTestRunner", "--", "--native-suite=tokenizer",
                       "--native-case=", "--native-nonce=corpus-unknown-check", "--corpus-bogus=1"]
            completed = runner.supervise(command, 60)
        self.assertEqual(2, completed.returncode, completed.stdout)
        self.assertIn("Unknown native runner argument", completed.stdout)

    def test_ordinary_invocation_without_corpus_arguments_still_runs(self):
        artifact = runner.read_artifact(BUILD_DIR)
        with runner.staged_project(artifact) as project:
            completed = runner.invoke(GODOT, project, "tokenizer", "integer_range_is_exact",
                                      "corpus-regression", 60)
        self.assertEqual([], runner.evaluate(completed.returncode, completed.stdout, "tokenizer",
                          "integer_range_is_exact", "corpus-regression", artifact["build_id"],
                          artifact["build_info"]), completed.stdout)

    def test_unknown_case_and_query_emit_no_execution_evidence(self):
        artifact = runner.read_artifact(BUILD_DIR)
        for case, listing in (("no such case", False), ("INTEGER_RANGE_IS_EXACT", False), ("", True)):
            with runner.staged_project(artifact) as project:
                completed = runner.invoke(GODOT, project, "tokenizer", case, "query-check", 60,
                                          listing=listing)
            self.assertTrue(runner.evaluate(completed.returncode, completed.stdout, "tokenizer", case,
                                            "query-check", artifact["build_id"], artifact["build_info"]))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--godot")
    parser.add_argument("--build-dir", type=Path, default=BUILD_DIR)
    args, remaining = parser.parse_known_args()
    GODOT, BUILD_DIR = args.godot, args.build_dir
    RuntimeTests.__unittest_skip__ = not GODOT
    unittest.main(argv=[sys.argv[0], *remaining])
