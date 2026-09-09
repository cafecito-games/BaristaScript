#!/usr/bin/env python3
# test_import_analyzer_corpus.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Pinned miniature analyzer transport and fail-closed staging regressions."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))


class AnalyzerImport(unittest.TestCase):
    def setUp(self):
        self.assertTrue((ROOT / 'scripts/import_analyzer_corpus.py').is_file(),
                        'discovery importer is missing')
        import import_analyzer_corpus
        self.m = import_analyzer_corpus
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'input'
        shutil.copytree(ROOT / 'tests/fixtures/analyzer_import/scripts', self.source)
        self.policy = self.m.default_policy()
        self.policy['counts'] = None
        self.policy['owners'] = {}
        self.policy['expectation_edits'] = {}
        self.policy['expectation_overrides'] = {}

    def inventory(self):
        return self.m.inventory_sources(self.source, self.policy, 'res://tests/corpus_staging/analyzer')

    def test_reviewed_owner_requires_execution_and_source_evidence(self):
        path = 'errors/preload_missing_relative_path.barista'
        owner = dict(self.m.default_policy()['owners'][path])
        self.policy['owners'][path] = owner
        self.inventory()
        owner.pop('execution_evidence')
        with self.assertRaisesRegex(ValueError, 'execution evidence'):
            self.inventory()
        owner.update(self.m.default_policy()['owners'][path])
        owner['execution_evidence'] = dict(owner['execution_evidence'], guard=False)
        with self.assertRaisesRegex(ValueError, 'execution evidence'):
            self.inventory()
        owner.update(self.m.default_policy()['owners'][path])
        owner['review_state'] = 'source_reviewed_deferred'
        with self.assertRaisesRegex(ValueError, 'deferred owner'):
            self.inventory()

    def test_only_missing_metadata_producers_are_prerequisites(self):
        owners = self.m.default_policy()['owners']
        local_consumers = (
            'variant_constant_known_value_rejected',
            'union_constant_known_alternative_rejected',
            'type_self_tuple_argument_sibling_element_mismatch',
            'type_self_tuple_parameter_wrong_arity',
            'tuple_construction_argument_type',
            'typed_array_pass_differently_to_typed',
            'explicit_callable_nullable_parameter_shape',
            'trait_meta_type_assignment',
        )
        for case in local_consumers:
            with self.subTest(case=case):
                owner = owners[f'errors/{case}.barista']
                self.assertEqual(owner['primary_issue'], 138)
                self.assertNotIn(141, owner['prerequisites'])
        other_available_producers = (
            'errors/argument_byte_width_diagnostic.barista',
            'errors/external_callable_signature_container_param.barista',
            'errors/final_static_var_reassigned_qualified.barista',
            'errors/issue_70_namespace_ambiguous_import.barista',
            'errors/retroactive_conformance_hidden_cold_registry.barista',
            'features/use_preload_script_as_type.barista',
        )
        for case in other_available_producers:
            with self.subTest(case=case):
                self.assertNotIn(141, owners[case]['prerequisites'])
        missing_producers = (
            'errors/abstract_method_in_non_abstract_head.barista',
            'errors/external_signal_nested_callable_enum_mismatch.barista',
            'errors/signal_value_connect_lambda_mismatch.barista',
            'errors/typed_container_mutation_methods.barista',
            'features/assymetric_assignment_good.barista',
            'features/type_alias_in_conformance_witness.barista',
        )
        for case in missing_producers:
            with self.subTest(case=case):
                self.assertIn(141, owners[case]['prerequisites'])

    def test_pinned_bytes_full_blocks_and_dependency_identities(self):
        provenance = json.loads((ROOT / 'tests/fixtures/analyzer_import/provenance.json').read_text())
        for path, digest in provenance['files'].items():
            self.assertEqual(hashlib.sha256((self.source / path).read_bytes()).hexdigest(), digest)
        inv = self.inventory()
        records = {r['upstream_path']: r for r in inv['sources']}
        case = records['analyzer/errors/enum_same_name_same_basename/dictionary_enum_across_same_named_helpers.fs']
        self.assertEqual(case['expected_block'].count('>> ERROR'), 4)
        self.assertNotIn('.fs', case['expected_block'])
        negative = records['analyzer/errors/preload_missing_relative_path.fs']
        self.assertEqual(len(negative['references']), 1)
        self.assertTrue(negative['references'][0]['intentional_missing'])
        self.assertIn('res://tests/corpus_staging/analyzer/', negative['expected_block'])
        cross = records['analyzer/errors/extends_preloaded_tuple_name_head.fs']
        self.assertEqual(cross['references'][0]['target'], 'parser/features/tuple_name_declaration.norun.fs')
        self.assertEqual(inv['counts']['support_helpers'], 2)
        for path, status in self.m.LATER.items():
            record = records['analyzer/' + path.removesuffix('.barista') + '.fs']
            self.assertEqual((record['status'], record['disposition']), (status, 'deferred'))
        actual = ROOT / 'project/tests/oracle_fixtures/declarations/annotation_duplicate_in_imported_lib.notest.barista'
        self.assertEqual(actual.read_bytes(), (self.source / 'analyzer/errors/annotation_duplicate_in_imported_lib.notest.fs').read_bytes())

    def test_missing_orphan_helper_outputs_unknown_status_and_utf8_fail(self):
        source = self.source / 'analyzer/errors/preload_missing_relative_path.fs'
        out = source.with_suffix('.out')
        original = out.read_bytes()
        mutations = [None, b'FS_TEST_UNKNOWN\n', b'FS_TEST_ANALYZER_ERROR\ninvalid\n', b'\xff\n']
        for value in mutations:
            if value is None:
                out.unlink()
            else:
                out.write_bytes(value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.inventory()
            out.write_bytes(original)
        orphan = source.with_name('orphan.out')
        orphan.write_bytes(original)
        with self.assertRaisesRegex(ValueError, 'orphan'):
            self.inventory()
        orphan.unlink()
        helper = next((self.source / 'analyzer').rglob('*.notest.fs'))
        helper.with_suffix('.out').write_bytes(b'FS_TEST_OK\n')
        with self.assertRaisesRegex(ValueError, 'helper'):
            self.inventory()

    def test_required_target_removal_and_symlinks_fail(self):
        path = self.source / 'parser/features/tuple_name_declaration.norun.fs'
        original = path.read_bytes()
        path.unlink()
        with self.assertRaisesRegex(ValueError, 'required.*target'):
            self.inventory()
        path.symlink_to(self.root / 'missing')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            self.inventory()
        path.unlink()
        path.write_bytes(original)

    def test_required_paired_dependency_cannot_be_removed_by_policy(self):
        consumer = self.source / 'analyzer/features/paired_consumer.fs'
        provider = consumer.with_name('paired_provider.fs')
        consumer.parent.mkdir(parents=True, exist_ok=True)
        consumer.write_bytes(b'const Provider = preload("paired_provider.fs")\n')
        provider.write_bytes(b'func test():\n\tpass\n')
        for path in (consumer, provider):
            path.with_suffix('.out').write_bytes(b'FS_TEST_OK\n')
        self.inventory()
        self.policy['deferred']['features/paired_provider.barista'] = 'M5 reviewed fixture control'
        with self.assertRaisesRegex(ValueError, 'required.*dependency.*removed'):
            self.inventory()

    def test_case_source_edits_require_disjoint_disposition_and_valid_provenance(self):
        path = 'analyzer/errors/preload_missing_relative_path.fs'
        data = (self.source / path).read_bytes()
        edit = self.m.change(data, 0, 1, data[:1].decode(), 'exact-control')
        self.policy['source_edits'][path] = {'sha256': self.m.sha(data), 'patches': [edit]}
        with self.assertRaisesRegex(ValueError, 'source edit requires'):
            self.inventory()
        self.policy['rewritten']['errors/preload_missing_relative_path.barista'] = 'Identity-preserving source control.'
        self.inventory()
        for key, value in [('line', 500), ('rule', ''), ('occurrences', True), ('after', '\0'), ('after', 'x\n')]:
            old = edit[key]
            edit[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                self.inventory()
            edit[key] = old

    def test_staging_validation_rejects_unlisted_files(self):
        import run_corpus_triage as triage
        inv = self.inventory()
        destination = self.root / 'stage'
        self.m.write_stage(inv, self.source, destination)
        triage.validate_staging(destination, inv)
        for extra in ['.baristaignore', '_support/.baristaignore', 'orphan.out']:
            path = destination / extra
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'')
            with self.subTest(extra=extra), self.assertRaisesRegex(ValueError, 'population'):
                triage.validate_staging(destination, inv)
            path.unlink()

    def test_staging_removed_case_cannot_shrink_its_declared_population(self):
        import run_corpus_triage as triage
        inv = self.inventory()
        destination = self.root / 'stage'
        self.m.write_stage(inv, self.source, destination)
        record = next(r for r in inv['sources'] if r['role'] == 'case' and r['disposition'] == 'imported')
        inv['sources'].remove(record)
        inv['ledger']['total'] -= 1
        case = destination / record['imported_path']
        case.unlink()
        case.with_suffix('.out').unlink()
        stages = self.m.read_json(destination / 'case_stages.json')
        stages['cases'].pop(record['imported_path'])
        (destination / 'case_stages.json').write_bytes(self.m.encoded(stages))
        with self.assertRaisesRegex(ValueError, 'population|accounting'):
            triage.validate_staging(destination, inv)

    def test_stage_replacement_is_deterministic_and_detects_byte_drift(self):
        inv = self.inventory()
        destination = self.root / 'stage'
        self.m.write_stage(inv, self.source, destination)
        before = {p.relative_to(destination): p.read_bytes() for p in destination.rglob('*') if p.is_file()}
        self.m.write_stage(inv, self.source, destination)
        self.assertEqual(before, {p.relative_to(destination): p.read_bytes() for p in destination.rglob('*') if p.is_file()})
        path = next(destination.rglob('*.barista'))
        path.write_bytes(path.read_bytes() + b'# drift\n')
        with self.assertRaisesRegex(ValueError, 'drift'):
            self.m.check_stage(inv, self.source, destination)
        path.unlink()
        with self.assertRaises(ValueError):
            self.m.check_stage(inv, self.source, destination)

    def test_policy_stale_overlap_preimage_and_unsafe_destination(self):
        self.policy['deferred']['errors/nonexistent.barista'] = 'M5: fixture'
        with self.assertRaisesRegex(ValueError, 'stale'):
            self.inventory()
        for path in ['project/tests/corpus', 'project/tests/corpus/analyzer', 'project',
                     'project/tests/corpus_staging/analyzer/../escape']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.m.stage_destination(ROOT, path)

    def test_warning_runtime_boundary_and_nonresource_strings(self):
        source = self.source / 'analyzer/features/transport.fs'
        source.parent.mkdir(parents=True, exist_ok=True)
        data = b'# preload("missing.fs")\nfunc test():\n\tprint("arbitrary.fs")\n'
        source.write_bytes(data)
        source.with_suffix('.out').write_bytes(b'FS_TEST_OK\n~~ WARNING at line 2: (UNUSED_VARIABLE) first\n~~ WARNING at line 3: (UNUSED_VARIABLE) second\nruntime.fs\n>> ERROR misleading runtime text\n')
        record = next(r for r in self.inventory()['sources'] if r['upstream_path'].endswith('/transport.fs'))
        self.assertEqual(record['references'], [])
        self.assertEqual(record['transformations'], [])
        self.assertEqual(record['expected_block'].count('~~ WARNING'), 2)
        self.assertNotIn('runtime', record['expected_block'])

    def test_registered_language_resource_identity_patches(self):
        policy = self.m.default_policy()
        output = 'analyzer/errors/retroactive_conformance_enum_target.out'
        case = 'errors/retroactive_conformance_enum_target.barista'
        self.policy['expectation_edits'][output] = policy['expectation_edits'][output]
        self.policy['expectation_overrides'][case] = policy['expectation_overrides'][case]
        records = {r['upstream_path']: r for r in self.inventory()['sources']}
        self.assertIn('only BaristaScript class', records[output.replace('.out', '.fs')]['expected_block'])
        source = 'analyzer/features/use_preload_script_as_type.fs'
        record = records[source]
        data = self.m.patch((self.source / source).read_bytes(), record['transformations'], source)
        self.assertIn(b'const preloaded: BaristaScript = preload("fs_to_preload.notest.barista")', data)

    def test_d1_helper_patch_stale_hash_and_overlapping_spans(self):
        inv = self.inventory()
        record = next(r for r in inv['sources'] if r['upstream_path'] == 'utils.notest.fs')
        self.assertEqual(len(record['transformations']), 4)
        data = (self.source / 'utils.notest.fs').read_bytes()
        changed = self.m.patch(data, record['transformations'], 'utils')
        self.assertIn(b'METHOD_FLAG_ASYNC', changed)
        self.assertIn(b'utils.notest.fs', changed)
        self.policy['source_edits']['utils.notest.fs']['patches'].append(record['transformations'][0])
        with self.assertRaisesRegex(ValueError, 'overlapping'):
            self.inventory()
        self.policy['source_edits']['utils.notest.fs']['patches'].pop()
        (self.source / 'utils.notest.fs').write_bytes(data.replace(b'Utils', b'Utill', 1))
        with self.assertRaisesRegex(ValueError, 'preimage'):
            self.inventory()

    def test_disjoint_generic_defer_and_expected_override(self):
        source = self.source / 'analyzer/features/generic_fixture.fs'
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_bytes(b'class_name Box[T]\nvar item: T\n')
        source.with_suffix('.out').write_bytes(b'FS_TEST_OK\n')
        self.policy['deferred']['features/generic_fixture.barista'] = 'M5: the assertion requires free T substitution in a user generic class.'
        inv = self.inventory()
        self.assertEqual(inv['ledger']['upstream_total'], inv['ledger']['total'] + len(self.policy['deferred']))
        self.policy['rewritten']['features/generic_fixture.barista'] = 'duplicate disposition'
        with self.assertRaisesRegex(ValueError, 'overlapping'):
            self.inventory()
        self.policy['rewritten'].pop('features/generic_fixture.barista')
        source = self.source / 'analyzer/errors/preload_missing_relative_path.out'
        data = source.read_bytes()
        start = data.index(b'Could not') if b'Could not' in data else data.index(b'preload')
        edit = self.m.change(data, start, start + 1, data[start:start + 1].decode(), 'transport-preimage-control')
        self.policy['expectation_edits']['analyzer/errors/preload_missing_relative_path.out'] = {'sha256': self.m.sha(data), 'patches': [edit]}
        with self.assertRaisesRegex(ValueError, 'override disposition'):
            self.inventory()
        self.policy['expectation_overrides']['errors/preload_missing_relative_path.barista'] = 'Exact byte-span transport control; identity unchanged.'
        self.inventory()
        self.policy['expectation_edits']['analyzer/errors/preload_missing_relative_path.out']['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'preimage'):
            self.inventory()

    def test_atomic_failure_preserves_previous_stage(self):
        from unittest.mock import patch
        inv = self.inventory()
        destination = self.root / 'stage'
        self.m.write_stage(inv, self.source, destination)
        before = (destination / 'inventory.json').read_bytes()
        rename = Path.rename
        def fail_candidate(path, target):
            if path.name == 'candidate':
                raise OSError('injected publication failure')
            return rename(path, target)
        with patch.object(Path, 'rename', fail_candidate), self.assertRaisesRegex(OSError, 'publication'):
            self.m.write_stage(inv, self.source, destination)
        self.assertEqual(before, (destination / 'inventory.json').read_bytes())

    def test_duplicate_json_keys_and_count_drift_fail(self):
        path = self.root / 'policy.json'
        path.write_text('{"schema_version":1,"schema_version":1}')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.m.read_json(path)
        self.policy['counts'] = self.inventory()['counts']
        self.policy['counts']['cases'] -= 1
        with self.assertRaisesRegex(ValueError, 'count drift'):
            self.inventory()


class TriageGuard(unittest.TestCase):
    def test_duplicate_result_fields_are_malformed(self):
        import run_corpus_triage as triage
        payload = '{"path":"res://fixture/case.barista","passed":false,"passed":true,"expected":"BS_TEST_OK","actual":"BS_TEST_OK"}'
        process = {'output': 'BS_CASE_RESULT ' + payload + '\nBS_CASE_RAN case.barista\nBS_CORPUS 1/1 skipped=2\n', 'exit_code': 0, 'timed_out': False}
        self.assertEqual(triage.result_record(process, 'case.barista', 'res://fixture', 'BS_TEST_OK')['terminal'], 'malformed_result')

    def test_exit_zero_without_execution_is_failure(self):
        import run_corpus_triage as triage
        process = {'output': '', 'exit_code': 0, 'timed_out': False}
        self.assertEqual(triage.result_record(process, 'case.barista', 'res://fixture', 'BS_TEST_OK')['terminal'], 'missing_guard')
        payload = {'path': 'res://fixture/case.barista', 'passed': True, 'expected': 'BS_TEST_OK', 'actual': 'BS_TEST_OK'}
        process['output'] = 'BS_CASE_RESULT ' + json.dumps(payload) + '\nBS_CASE_RAN case.barista\nBS_CORPUS 1/1 skipped=2\n'
        self.assertTrue(triage.result_record(process, 'case.barista', 'res://fixture', 'BS_TEST_OK')['passed'])
        for change in ['BS_CASE_RAN other.barista', 'BS_CORPUS 0/0 skipped=0']:
            broken = dict(process)
            broken['output'] = process['output'].replace('BS_CASE_RAN case.barista' if change.startswith('BS_CASE') else 'BS_CORPUS 1/1 skipped=2', change)
            self.assertFalse(triage.result_record(broken, 'case.barista', 'res://fixture', 'BS_TEST_OK')['passed'])
        process['timed_out'] = True
        self.assertEqual(triage.result_record(process, 'case.barista', 'res://fixture', 'BS_TEST_OK')['terminal'], 'timeout')

    def test_timeout_supervises_real_process_and_preserves_output(self):
        import run_corpus_triage as triage
        result = triage.supervise([sys.executable, '-c', 'import time; print("started", flush=True); time.sleep(10)'], 0.2)
        self.assertTrue(result['timed_out'])
        self.assertLess(result['duration_seconds'], 3)
        self.assertIn('started', result['output'])


FOUNDRY = None


class FullPinned(unittest.TestCase):
    def test_complete_producer_inventory_and_staging(self):
        if FOUNDRY is None:
            self.skipTest('full producer requires --foundry; byte-faithful miniature tests ran')
        import import_analyzer_corpus as importer
        import corpus_registry
        registry = corpus_registry.validate_registration(ROOT)
        corpus_registry.verify_checkout(FOUNDRY, registry, registry['revision'])
        policy = importer.default_policy()
        source = FOUNDRY / importer.SCRIPTS
        uri = 'res://tests/corpus_staging/analyzer'
        first = importer.inventory_sources(source, policy, uri)
        self.assertEqual(first, importer.inventory_sources(source, policy, uri))
        self.assertEqual((first['counts']['sources'], first['counts']['cases'], first['counts']['helpers']), (1596, 1346, 250))
        self.assertEqual(sum(len(r['references']) for r in first['sources']), 139)
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary) / 'stage'
            importer.write_stage(first, source, stage)
            importer.check_stage(first, source, stage)
            original = next(stage.rglob('*.barista'))
            data = original.read_bytes()
            original.write_bytes(bytes([data[0] ^ 1]) + data[1:])
            with self.assertRaisesRegex(ValueError, 'byte drift'):
                importer.check_stage(first, source, stage)
            original.write_bytes(data)
            original.unlink()
            with self.assertRaisesRegex(ValueError, 'population'):
                importer.check_stage(first, source, stage)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--foundry', type=Path)
    args, remaining = parser.parse_known_args()
    FOUNDRY = args.foundry
    unittest.main(argv=[sys.argv[0], *remaining])
