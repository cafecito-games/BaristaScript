#!/usr/bin/env python3
# test_import_analyzer_corpus.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Pinned miniature analyzer transport and fail-closed staging regressions."""
import argparse
import collections
import copy
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
        fixture_cases = {
            path.relative_to(self.source / 'analyzer').as_posix().removesuffix('.fs') + '.barista'
            for path in (self.source / 'analyzer').rglob('*.fs') if not path.name.endswith('.notest.fs')
        }
        self.policy['deferred'] = {
            path: reason for path, reason in self.policy['deferred'].items() if path in fixture_cases
        }
        self.policy['excluded'] = {
            path: reason for path, reason in self.policy['excluded'].items() if path in fixture_cases
        }
        self.policy['owners'] = {
            path: owner for path, owner in self.policy['owners'].items()
            if path in self.policy['deferred'] or path in self.policy['excluded']
        }
        self.policy['expectation_edits'] = {}
        self.policy['expectation_overrides'] = {}
        self.policy['rewritten'] = {
            path: reason for path, reason in self.policy['rewritten'].items()
            if path in fixture_cases
        }
        fixture_sources = {
            path.relative_to(self.source).as_posix()
            for path in self.source.rglob('*.fs')
        }
        self.policy['source_edits'] = {
            path: edits for path, edits in self.policy['source_edits'].items()
            if path in fixture_sources
        }

    def inventory(self):
        return self.m.inventory_sources(self.source, self.policy, 'res://tests/corpus_staging/analyzer')

    def disposition_owner(self, path, state, reason):
        owner = copy.deepcopy(self.m.default_policy()['owners']['errors/preload_missing_relative_path.barista'])
        owner['primary_issue'] = 138
        owner['prerequisites'] = []
        owner['review_state'] = state
        owner['reason'] = reason
        owner['source_review'] = dict(owner['source_review'],
                                     identity=self.m.SCRIPTS + '/analyzer/' + path.removesuffix('.barista') + '.fs',
                                     assertion='The fixture has a source-reviewed disposition.',
                                     final_disposition=reason)
        return owner

    def test_shared_utils_has_one_real_provider(self):
        inv = self.inventory()
        record = next(r for r in inv['sources'] if r['upstream_path'] == 'utils.notest.fs')
        self.assertEqual(record.get('root'), 'res://tests/corpus_support/parser')
        self.assertEqual(record['imported_path'], 'utils.notest.barista')
        self.assertEqual(inv['ledger']['skipped'], inv['counts']['helpers'] + 1)
        stage = self.root / 'stage'
        self.m.write_stage(inv, self.source, stage)
        self.assertFalse((stage / '_support/utils.notest.barista').exists())
        self.assertEqual(sum(p.name == 'utils.notest.barista' for p in stage.rglob('*.barista')), 0)
        self.assertEqual(inv['counts']['support_helpers'], 2)

    def test_shared_support_actual_bytes_and_root_cannot_drift(self):
        import copy
        import run_corpus_triage
        inv = self.inventory()
        record = next(r for r in inv['sources'] if r['upstream_path'] == 'utils.notest.fs')
        project = self.root / 'project'
        support = project / 'tests/corpus_support/parser/utils.notest.barista'
        support.parent.mkdir(parents=True)
        stage = self.root / 'stage'
        stage.mkdir()
        (stage / 'previous').write_bytes(b'previous valid stage')
        with self.assertRaisesRegex(ValueError, 'shared support'):
            self.m.write_stage(inv, self.source, stage, project_root=project)
        self.assertEqual((stage / 'previous').read_bytes(), b'previous valid stage')
        original = (self.source / 'utils.notest.fs').read_bytes()
        projected = self.m.patch(original, record['transformations'], record['identity'])
        support.write_bytes(projected)
        self.m.write_stage(inv, self.source, stage, project_root=project)
        run_corpus_triage.validate_staging(stage, inv, project_root=project)
        support.write_bytes(projected + b'drift')
        with self.assertRaisesRegex(ValueError, 'shared support'):
            self.m.check_stage(inv, self.source, stage, project_root=project)
        with self.assertRaisesRegex(ValueError, 'shared support'):
            run_corpus_triage.validate_staging(stage, inv, project_root=project)
        support.write_bytes(projected)
        for root in ('res://outside', 'res://tests/corpus_support/parser/../parser'):
            changed = copy.deepcopy(inv)
            next(r for r in changed['sources'] if r['upstream_path'] == 'utils.notest.fs')['root'] = root
            with self.assertRaisesRegex(ValueError, 'identity/root/path'):
                run_corpus_triage.validate_staging(stage, changed, project_root=project)
        support.unlink()
        support.symlink_to(ROOT / 'project/tests/corpus_support/parser/utils.notest.barista')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            self.m.check_stage(inv, self.source, stage, project_root=project)

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
        with self.assertRaisesRegex(ValueError, 'orphan reviewed disposition owner'):
            self.inventory()

    def test_reviewed_dispositions_require_exact_bidirectional_owner_state(self):
        path = 'errors/preload_missing_relative_path.barista'
        owner = copy.deepcopy(self.m.default_policy()['owners'][path])
        self.policy['owners'][path] = owner

        self.policy['excluded'][path] = 'Root-reviewed removed premise fixture.'
        with self.assertRaisesRegex(ValueError, 'excluded policy requires matching owner state'):
            self.inventory()
        owner['review_state'] = 'source_reviewed_excluded'
        owner['reason'] = self.policy['excluded'][path]
        owner['source_review']['final_disposition'] = self.policy['excluded'][path]
        self.inventory()

        self.policy['excluded'].pop(path)
        with self.assertRaisesRegex(ValueError, 'orphan reviewed disposition owner'):
            self.inventory()
        owner['review_state'] = 'source_reviewed_deferred'
        self.policy['deferred'][path] = 'M5-reviewed fixture.'
        owner['reason'] = self.policy['deferred'][path]
        owner['source_review']['final_disposition'] = self.policy['deferred'][path]
        self.inventory()

        owner['source_review'] = dict(owner['source_review'], identity='wrong.fs')
        with self.assertRaisesRegex(ValueError, 'owner source review'):
            self.inventory()
        owner['source_review'] = dict(self.m.default_policy()['owners'][path]['source_review'])
        owner.pop('execution_evidence')
        with self.assertRaisesRegex(ValueError, 'execution evidence'):
            self.inventory()

    def test_disposition_without_owner_fails_closed(self):
        path = 'errors/preload_missing_relative_path.barista'
        self.policy['excluded'][path] = 'Root-reviewed removed premise fixture.'
        with self.assertRaisesRegex(ValueError, 'excluded policy requires owner records'):
            self.inventory()

    def test_reviewed_disposition_rationale_must_match_both_owner_fields(self):
        path = 'errors/preload_missing_relative_path.barista'
        for disposition in ('deferred', 'excluded'):
            state = 'source_reviewed_' + disposition
            reason = f'{disposition} source-reviewed fixture rationale.'
            with self.subTest(disposition=disposition):
                self.policy[disposition][path] = reason
                owner = self.disposition_owner(path, state, reason)
                self.policy['owners'][path] = owner
                self.inventory()
                for field in ('reason', 'final_disposition'):
                    with self.subTest(disposition=disposition, field=field):
                        if field == 'reason':
                            owner[field] = 'contradictory owner rationale'
                        else:
                            owner['source_review'][field] = 'contradictory final disposition'
                        with self.assertRaisesRegex(ValueError, 'disposition rationale mismatch'):
                            self.inventory()
                        owner['reason'] = reason
                        owner['source_review']['final_disposition'] = reason
                self.policy[disposition].pop(path)
                self.policy['owners'].pop(path)

    def test_authored_policy_uses_canonical_importer_encoding(self):
        self.assertEqual(self.m.POLICY.read_bytes(), self.m.encoded(self.m.default_policy()))

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
        noreturn_paths = owners['features/noreturn_paths.norun.barista']
        self.assertEqual(noreturn_paths['primary_issue'], 139)
        self.assertNotIn(141, noreturn_paths['prerequisites'])
        self.assertEqual(noreturn_paths['code_symbols'], [
            'src/bs_analyzer.cpp:BSAnalyzer::node_terminates',
            'src/bs_analyzer.cpp:BSAnalyzer::suite_has_return',
            'src/bs_analyzer.cpp:BSAnalyzer::check_function_flow_finality',
        ])
        self.assertIn('constant-true WHILE exit-summary gap', noreturn_paths['reason'])
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
        self.policy['owners']['features/paired_provider.barista'] = self.disposition_owner(
            'features/paired_provider.barista', 'source_reviewed_deferred',
            self.policy['deferred']['features/paired_provider.barista'])
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
        self.policy['owners']['features/generic_fixture.barista'] = self.disposition_owner(
            'features/generic_fixture.barista', 'source_reviewed_deferred',
            self.policy['deferred']['features/generic_fixture.barista'])
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
        self.assertEqual(first['counts']['support_helpers'], 2)
        self.assertEqual(sum(len(r['references']) for r in first['sources']), 139)

        m5 = sorted(set(policy['deferred']) - set(importer.LATER))
        excluded = sorted(policy['excluded'])
        combined = sorted(set(m5) | set(excluded))
        case_list_sha = lambda paths: hashlib.sha256(('\n'.join(paths) + '\n').encode()).hexdigest()
        self.assertEqual((len(m5), case_list_sha(m5)),
                         (231, '1d348c41a1b9b68aefe9fbc0bea393310cf64d89c78037a6b18385ccdcbf368b'))
        self.assertEqual((len(excluded), case_list_sha(excluded)),
                         (33, '07aee540c42b8282e3b52ee461709d3e1a4e7a8f7e9dbcdc51872a95cf7174ee'))
        self.assertEqual((len(combined), case_list_sha(combined)),
                         (264, '9d8000dd6cb16cc4521c243c870a0805c97095c1613bc7801cb56476497bfcaa'))
        self.assertEqual((first['ledger']['upstream_total'], first['ledger']['total'],
                          len(policy['excluded']), len(policy['deferred'])),
                         (1346, 1078, 33, 235))
        included = [record for record in first['sources']
                    if record.get('role') == 'case' and record['disposition'] not in ('excluded', 'deferred')]
        self.assertEqual(collections.Counter(record['imported_path'].split('/')[0] for record in included),
                         {'errors': 662, 'features': 354, 'warnings': 62})
        self.assertEqual(collections.Counter(record['status'] for record in included),
                         {'FS_TEST_ANALYZER_ERROR': 646, 'FS_TEST_OK': 414, 'FS_TEST_PARSER_ERROR': 18})
        for path in m5:
            self.assertEqual(policy['owners'][path]['review_state'], 'source_reviewed_deferred')
        for path in excluded:
            self.assertEqual(policy['owners'][path]['review_state'], 'source_reviewed_excluded')

        projected_cases = [
            'errors/type_alias_not_inherited.barista',
            'features/generic_tagged_union_namespaced.barista',
            'features/retroactive_conformance_basic.norun.barista',
            'features/retroactive_conformance_native.norun.barista',
            'features/type_alias_explicit_type_argument_nearer_class_shadow.barista',
            'features/type_alias_explicit_type_argument_witness.barista',
        ]
        reviewed_cases = sorted(projected_cases + ['features/use_preload_script_as_type.barista'])
        self.assertEqual((len(projected_cases), case_list_sha(projected_cases)),
                         (6, 'f4ae7b56a09a61a5b575ec96076598c1ef2ba184408ad794701a5eef78deb8ee'))
        self.assertEqual((len(reviewed_cases), case_list_sha(reviewed_cases)),
                         (7, 'c6a3ee1e94d74a0fd14915f597776dc6c83a1af426e262e1de8e3eb678a3945c'))
        self.assertEqual(set(policy['rewritten']),
                         set(reviewed_cases) | {'features/lookup_class.barista'})
        projected_sources = {
            'analyzer/errors/type_alias_not_inherited.fs':
                ('d703f76f1f801aae337447301266d1648a9ad804bc83d4d84b7f540262215978',
                 'eab13c8d7dc11a9750706bfd4e60dbcceff9b7631a3139fe992040309fc8bfc6'),
            'analyzer/features/generic_tagged_union_global_values.notest.fs':
                ('092dba1d8f5db2282c5f363be89218bd11a72ebb5b270b73bbc897bb6116d317',
                 '207ca64f8be05b2cc2228b12d58cece03bc8b262dd6e5dd163e4433fcc037cb7'),
            'analyzer/features/generic_tagged_union_namespaced.fs':
                ('d14d1fc596ec4a0f4a1a68cfb06526deea272878cb318c062436e348638c22c0',
                 'ee120028ef679c15c7334e4e829b5854630a023091eea06748169238492f3912'),
            'analyzer/features/retroactive_conformance_basic.norun.fs':
                ('0a91316704f9a48d2ac1c55daf8c705eb8d54a954433273ad5337f57bfd3b458',
                 '59f4cb8d230655be0d4735b35138f285f3a41e38b6d939f1d33ec6b056c07dc7'),
            'analyzer/features/retroactive_conformance_native.norun.fs':
                ('aafb5eae9774e87f1791def9f493ad2a4ffa0507b4f68e2871785822ff3f1ac3',
                 'e24347e4bcd0d66aee8f00ec11134bf72dfb37b41d6cc298ca5d1b66c74c90c6'),
            'analyzer/features/type_alias_explicit_type_argument_nearer_class_shadow.fs':
                ('36673ac7706595f8727b27c61ede9957b968db6b0f41c7da2adfcffa571036c0',
                 'ce5c185e21cdc5e84527e1f9fa6cb7522b1ea40ec9df72afd13890aab1715cc9'),
            'analyzer/features/type_alias_explicit_type_argument_witness_conformance.notest.fs':
                ('adc16ce3f5816a865b006d8358667dec39495b697b20d736542cf335f07af722',
                 '9f267e188fd4ac74b8ba8d7126672e36af1be1c70e9c672e3f1e202403741b1a'),
        }
        self.assertEqual(set(policy['source_edits']), set(projected_sources) | {
            'analyzer/features/lookup_class.fs',
            'analyzer/features/use_preload_script_as_type.fs',
            'utils.notest.fs',
        })
        for path, (preimage_sha, projected_sha) in projected_sources.items():
            data = (source / path).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), preimage_sha)
            changes = importer.source_policy_changes(data, path, policy)
            self.assertEqual(hashlib.sha256(importer.patch(data, changes, path)).hexdigest(),
                             projected_sha)

        records_by_case = {record['imported_path']: record for record in first['sources']
                           if record['role'] == 'case'}
        expected_outputs = {
            'errors/type_alias_not_inherited.barista':
                '56d11e99f733f3bb863795192e346a9340da4c8c6ed5f31809f31f128a5a4f7d',
            'features/generic_tagged_union_namespaced.barista':
                'a0c2235fe56e9b53017a36aac7ab8f7ca1e305aa6a52c8ef75eb11c11b554027',
            'features/retroactive_conformance_basic.norun.barista':
                'a79326b2c94dcb1410e12f6b774c756388a4eedb3f9066b078a3b45c5574cb1d',
            'features/retroactive_conformance_native.norun.barista':
                'a79326b2c94dcb1410e12f6b774c756388a4eedb3f9066b078a3b45c5574cb1d',
            'features/type_alias_explicit_type_argument_nearer_class_shadow.barista':
                'db661f52fcdac393885cad2edea733e09c5fc919e4b7c36649909b01490bbd03',
            'features/type_alias_explicit_type_argument_witness.barista':
                '3d24d399e21ef4bf3cbe74083192674f466680569a941c4de38e8c869ba53198',
            'features/use_preload_script_as_type.barista':
                '0ee9742d50b10ce2aa6a67702cf1626fb0c0afcb331baba163b0a943fd7bc78b',
        }
        for path in reviewed_cases:
            record = records_by_case[path]
            self.assertEqual(record['disposition'], 'rewritten')
            self.assertEqual(record['expectation_sha256'], expected_outputs[path])
            self.assertEqual(record['expectation_edits'], [])
            if path.startswith('features/'):
                self.assertEqual(record['expected_block'], 'BS_TEST_OK')
        error_block = records_by_case['errors/type_alias_not_inherited.barista']['expected_block']
        self.assertEqual(error_block.count('>> ERROR'), 2)
        self.assertIn('Type alias "Element" is not in scope here.', error_block)
        self.assertIn('Type alias "Meters" is not in scope here.', error_block)
        namespaced = records_by_case['features/generic_tagged_union_namespaced.barista']
        self.assertEqual(namespaced['imported_sha256'],
                         'a797b55059066bda14a2c849e5338b289d97b5e37bdff16d1d9fc47b0c42ea8a')

        lookup_path = 'analyzer/features/lookup_class.fs'
        lookup_case = 'features/lookup_class.barista'
        lookup_data = (source / lookup_path).read_bytes()
        lookup_edit = policy['source_edits'][lookup_path]
        self.assertIn('strict order-independent lexical-outer conflicts',
                      policy['rewritten'][lookup_case])
        self.assertEqual(hashlib.sha256(lookup_data).hexdigest(),
                         'df86ab03fb9a399ab518538b0382a6bb69e62143531fed246d48a00d7a8c012b')
        self.assertEqual(lookup_edit, {
            'sha256': 'df86ab03fb9a399ab518538b0382a6bb69e62143531fed246d48a00d7a8c012b',
            'patches': [
                {'start': 532, 'end': 533, 'line': 32, 'before': 'E', 'after': 'G',
                 'rule': 'strict-outer-conflict-incidental-rename', 'occurrences': 1},
                {'start': 793, 'end': 794, 'line': 49, 'before': 'E', 'after': 'G',
                 'rule': 'strict-outer-conflict-incidental-rename', 'occurrences': 1},
            ],
        })
        lookup = next(record for record in first['sources']
                      if record['upstream_path'] == lookup_path)
        self.assertEqual(lookup['disposition'], 'rewritten')
        self.assertEqual(lookup['expected_block'], 'BS_TEST_OK')
        self.assertEqual(lookup['expectation_edits'], [])
        self.assertEqual(lookup['imported_sha256'],
                         '005b619a501e0d163c00f0929ab42106e3674a6c6034cce7a08d819535f13230')
        transformed = importer.patch(lookup_data, lookup['transformations'], lookup_path)
        self.assertIn(b'class G extends D:', transformed)
        self.assertIn(b'var f: = G.F.new()', transformed)
        self.assertIn(b'class E extends External.E:', transformed)
        self.assertNotIn(lookup_path[:-3] + '.out', policy['expectation_edits'])

        stale = copy.deepcopy(policy)
        stale['source_edits'][lookup_path]['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'source edit hash preimage mismatch'):
            importer.inventory_sources(source, stale, uri)
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
