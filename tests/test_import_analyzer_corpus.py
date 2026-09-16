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

EXECUTION_REPORT = None


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
        return self.m.inventory_sources(self.source, self.policy, 'res://tests/corpus/analyzer')

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
        self.m.write_tree(inv, self.source, stage)
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
            self.m.write_tree(inv, self.source, stage, project_root=project)
        self.assertEqual((stage / 'previous').read_bytes(), b'previous valid stage')
        original = (self.source / 'utils.notest.fs').read_bytes()
        projected = self.m.patch(original, record['transformations'], record['identity'])
        support.write_bytes(projected)
        self.m.write_tree(inv, self.source, stage, project_root=project)
        run_corpus_triage.validate_imported_tree(stage, inv, project_root=project)
        support.write_bytes(projected + b'drift')
        with self.assertRaisesRegex(ValueError, 'shared support'):
            self.m.check_tree(inv, self.source, stage, project_root=project)
        with self.assertRaisesRegex(ValueError, 'shared support'):
            run_corpus_triage.validate_imported_tree(stage, inv, project_root=project)
        support.write_bytes(projected)
        for root in ('res://outside', 'res://tests/corpus_support/parser/../parser'):
            changed = copy.deepcopy(inv)
            next(r for r in changed['sources'] if r['upstream_path'] == 'utils.notest.fs')['root'] = root
            with self.assertRaisesRegex(ValueError, 'identity/root/path'):
                run_corpus_triage.validate_imported_tree(stage, changed, project_root=project)
        support.unlink()
        support.symlink_to(ROOT / 'project/tests/corpus_support/parser/utils.notest.barista')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            self.m.check_tree(inv, self.source, stage, project_root=project)

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
        self.assertIn('res://tests/corpus/analyzer/', negative['expected_block'])
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

    def test_projected_helper_clone_is_raw_bound_helper_only_and_deterministic(self):
        before = self.inventory()
        provider = self.source / 'analyzer/features/projected_provider.notest.fs'
        provider.parent.mkdir(parents=True, exist_ok=True)
        data = b'namespace projected_fixture\n\nenum_name RawResult[T, E]:\n\tOk(value: T)\n\tErr(error: E)\n'
        provider.write_bytes(data)
        name_start = data.index(b'RawResult[T, E]')
        value_start = data.index(b'T)', name_start)
        error_start = data.index(b'E)', value_start)
        standard = [
            self.m.change(data, name_start, name_start + len(b'RawResult[T, E]'), 'StandardResult', 'standard-provider'),
            self.m.change(data, value_start, value_start + 1, 'int', 'standard-provider'),
            self.m.change(data, error_start, error_start + 1, 'String', 'standard-provider'),
        ]
        clone_patches = [
            self.m.change(data, name_start, name_start + len(b'RawResult[T, E]'), 'ProjectedResult', 'projected-provider'),
            self.m.change(data, value_start, value_start + 1, 'String', 'projected-provider'),
            self.m.change(data, error_start, error_start + 1, 'int', 'projected-provider'),
        ]
        clone_bytes = self.m.patch(data, clone_patches, 'fixture clone')
        self.policy['source_edits']['analyzer/features/projected_provider.notest.fs'] = {
            'sha256': self.m.sha(data),
            'patches': standard,
            'projected_helper_clone': {
                'target': 'features/projected_flipped.notest.barista',
                'transformed_sha256': self.m.sha(clone_bytes),
                'patches': clone_patches,
            },
        }
        inv = self.inventory()
        records = {record['imported_path']: record for record in inv['sources']}
        original = records['features/projected_provider.notest.barista']
        clone = records['features/projected_flipped.notest.barista']
        self.assertEqual(self.m.patch(data, original['transformations'], original['identity']),
                         data.replace(b'RawResult[T, E]', b'StandardResult').replace(b'T)', b'int)').replace(b'E)', b'String)'))
        self.assertEqual(self.m.patch(data, clone['transformations'], clone['identity']), clone_bytes)
        self.assertEqual(clone['role'], 'helper')
        self.assertEqual(clone['projection'], {
            'kind': 'projected_helper_clone',
            'provider_identity': self.m.SCRIPTS + '/analyzer/features/projected_provider.notest.fs',
            'provider_sha256': self.m.sha(data),
            'transformed_sha256': self.m.sha(clone_bytes),
        })
        self.assertEqual(len({record['identity'] for record in inv['sources']}), len(inv['sources']))
        self.assertEqual(inv['counts']['cases'], before['counts']['cases'])
        self.assertEqual(inv['ledger']['total'], before['ledger']['total'])
        self.assertEqual(inv['ledger']['skipped'], before['ledger']['skipped'] + 2)
        destination = self.root / 'projected-stage'
        self.m.write_tree(inv, self.source, destination)
        self.m.check_tree(inv, self.source, destination)
        self.assertEqual((destination / original['imported_path']).read_bytes(),
                         self.m.patch(data, standard, original['identity']))
        self.assertEqual((destination / clone['imported_path']).read_bytes(), clone_bytes)
        first = {path.relative_to(destination): path.read_bytes()
                 for path in destination.rglob('*') if path.is_file()}
        self.m.write_tree(inv, self.source, destination)
        self.assertEqual(first, {path.relative_to(destination): path.read_bytes()
                                 for path in destination.rglob('*') if path.is_file()})
        self.assertEqual(provider.read_bytes(), data)

    def test_projected_helper_clone_schema_and_provenance_fail_closed(self):
        provider = self.source / 'analyzer/features/clone_guard.notest.fs'
        provider.parent.mkdir(parents=True, exist_ok=True)
        data = b'enum_name Guard[T]:\n\tValue(value: T)\n'
        provider.write_bytes(data)
        start = data.index(b'Guard[T]')
        patches = [self.m.change(data, start, start + len(b'Guard[T]'), 'GuardInt', 'clone-guard')]
        valid = {
            'sha256': self.m.sha(data),
            'patches': [],
            'projected_helper_clone': {
                'target': 'features/clone_guard_projected.notest.barista',
                'transformed_sha256': self.m.sha(self.m.patch(data, patches, 'clone guard')),
                'patches': patches,
            },
        }
        key = 'analyzer/features/clone_guard.notest.fs'
        self.policy['source_edits'][key] = valid
        self.inventory()
        mutations = (
            ('unknown clone field', lambda item: item['projected_helper_clone'].__setitem__('extra', True), 'clone schema'),
            ('missing clone field', lambda item: item['projected_helper_clone'].pop('patches'), 'clone schema'),
            ('empty clone patches', lambda item: item['projected_helper_clone'].__setitem__('patches', []), 'clone schema'),
            ('absolute target', lambda item: item['projected_helper_clone'].__setitem__('target', '/escape.notest.barista'), 'clone target'),
            ('dot target', lambda item: item['projected_helper_clone'].__setitem__('target', 'features/../escape.notest.barista'), 'clone target'),
            ('case target', lambda item: item['projected_helper_clone'].__setitem__('target', 'features/escape.barista'), 'clone target'),
            ('wrong transformed hash', lambda item: item['projected_helper_clone'].__setitem__('transformed_sha256', '0' * 64), 'transformed hash'),
            ('stale clone patch', lambda item: item['projected_helper_clone']['patches'][0].__setitem__('before', 'Wrong[T]'), 'preimage'),
            ('stale provider hash', lambda item: item.__setitem__('sha256', '0' * 64), 'preimage'),
        )
        for label, mutate, complaint in mutations:
            with self.subTest(label=label):
                candidate = copy.deepcopy(valid)
                mutate(candidate)
                self.policy['source_edits'][key] = candidate
                with self.assertRaisesRegex(ValueError, complaint):
                    self.inventory()
        self.policy['source_edits'][key] = copy.deepcopy(valid)
        occupied = copy.deepcopy(valid)
        occupied['projected_helper_clone']['target'] = 'errors/annotation_duplicate_in_imported_lib.notest.barista'
        self.policy['source_edits'][key] = occupied
        with self.assertRaisesRegex(ValueError, 'target collision'):
            self.inventory()
        case_path = 'analyzer/errors/preload_missing_relative_path.fs'
        case_data = (self.source / case_path).read_bytes()
        self.policy['source_edits'].pop(key)
        self.policy['source_edits'][case_path] = {
            'sha256': self.m.sha(case_data), 'patches': [],
            'projected_helper_clone': copy.deepcopy(valid['projected_helper_clone']),
        }
        self.policy['rewritten']['errors/preload_missing_relative_path.barista'] = 'Clone role guard fixture.'
        with self.assertRaisesRegex(ValueError, 'helper provider'):
            self.inventory()

    def test_imported_tree_validation_rejects_unlisted_files(self):
        import run_corpus_triage as triage
        inv = self.inventory()
        destination = self.root / 'stage'
        self.m.write_tree(inv, self.source, destination)
        triage.validate_imported_tree(destination, inv)
        for extra in ['.baristaignore', '_support/.baristaignore', 'orphan.out']:
            path = destination / extra
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'')
            with self.subTest(extra=extra), self.assertRaisesRegex(ValueError, 'population'):
                triage.validate_imported_tree(destination, inv)
            path.unlink()

    def test_removed_case_cannot_shrink_its_declared_population(self):
        import run_corpus_triage as triage
        inv = self.inventory()
        destination = self.root / 'stage'
        self.m.write_tree(inv, self.source, destination)
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
            triage.validate_imported_tree(destination, inv)

    def test_tree_replacement_is_deterministic_and_detects_byte_drift(self):
        inv = self.inventory()
        destination = self.root / 'stage'
        self.m.write_tree(inv, self.source, destination)
        before = {p.relative_to(destination): p.read_bytes() for p in destination.rglob('*') if p.is_file()}
        self.m.write_tree(inv, self.source, destination)
        self.assertEqual(before, {p.relative_to(destination): p.read_bytes() for p in destination.rglob('*') if p.is_file()})
        path = next(destination.rglob('*.barista'))
        path.write_bytes(path.read_bytes() + b'# drift\n')
        with self.assertRaisesRegex(ValueError, 'drift'):
            self.m.check_tree(inv, self.source, destination)
        path.unlink()
        with self.assertRaises(ValueError):
            self.m.check_tree(inv, self.source, destination)

    def test_policy_stale_overlap_preimage_and_unsafe_destination(self):
        self.policy['deferred']['errors/nonexistent.barista'] = 'M5: fixture'
        with self.assertRaisesRegex(ValueError, 'stale'):
            self.inventory()
        for path in ['project/tests/corpus', 'project/tests/corpus/parser', 'project',
                     'project/tests/corpus_staging/analyzer',
                     'project/tests/corpus/analyzer/../escape']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.m.import_destination(ROOT, path)

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

    def test_expectation_patch_is_line_flexible_but_fail_closed_and_isolated(self):
        output = 'analyzer/errors/preload_missing_relative_path.out'
        case = 'errors/preload_missing_relative_path.barista'
        path = self.source / output
        data = path.read_bytes()
        before = data[data.index(b'>> ERROR'):].decode().rstrip('\n')
        after = (before + '\n'
                 '>> ERROR at line 4: Exact expectation-only second diagnostic.')
        edit = self.m.change(data, data.index(b'>> ERROR'), len(data) - 1, after,
                             'expectation-line-count-control')

        with self.assertRaisesRegex(ValueError, 'invalid patch provenance'):
            self.m.patch(data, [edit], output)
        projected = self.m.expectation_patch(data, [edit], output)
        self.assertEqual(projected.count(b'>> ERROR'), 2)
        self.policy['expectation_edits'][output] = {
            'sha256': self.m.sha(data),
            'patches': [edit],
        }
        self.policy['expectation_overrides'][case] = (
            'Exact expectation-only line-count transport control.')
        record = next(record for record in self.inventory()['sources']
                      if record['upstream_path'] == output.removesuffix('.out') + '.fs')
        self.assertEqual(record['expected_block'].count('>> ERROR'), 2)
        self.assertIn('Exact expectation-only second diagnostic.', record['expected_block'])
        self.assertIn('res://tests/corpus/analyzer/', record['expected_block'])
        self.assertEqual(record['expectation_edits'], [edit])

        def expect_failure(mutator, pattern):
            candidate = copy.deepcopy(self.policy)
            mutator(candidate['expectation_edits'][output]['patches'])
            original = self.policy
            self.policy = candidate
            try:
                with self.assertRaisesRegex(ValueError, pattern):
                    self.inventory()
            finally:
                self.policy = original

        expect_failure(lambda patches: patches[0].pop('rule'), 'invalid patch provenance')
        expect_failure(lambda patches: patches[0].__setitem__('start', patches[0]['start'] + 1),
                       'preimage|span')
        expect_failure(lambda patches: patches[0].__setitem__('line', patches[0]['line'] + 1),
                       'line preimage mismatch')
        expect_failure(lambda patches: patches[0].__setitem__('before', 'wrong'),
                       'preimage mismatch')
        expect_failure(lambda patches: patches.append(copy.deepcopy(patches[0])), 'overlapping')

        self.policy['expectation_overrides'].pop(case)
        with self.assertRaisesRegex(ValueError, 'override disposition'):
            self.inventory()
        self.policy['expectation_overrides'][case] = 'Exact expectation-only control.'
        self.policy['expectation_edits']['analyzer/errors/unlisted.out'] = copy.deepcopy(
            self.policy['expectation_edits'][output])
        with self.assertRaisesRegex(ValueError, 'stale expectation edit policy'):
            self.inventory()
        self.policy['expectation_edits'].pop('analyzer/errors/unlisted.out')

        for malformed, pattern in (
                (b'FS_TEST_ANALYZER_ERROR\nnot a diagnostic\n', 'malformed'),
                (data.replace(b'Preload', b'Pre\rload'), 'CR'),
                (data.replace(b'Preload', b'Pre\0load'), 'NUL'),
                (data.rstrip(b'\n'), 'final LF')):
            with self.subTest(malformed=pattern):
                path.write_bytes(malformed)
                self.policy['expectation_edits'][output]['sha256'] = self.m.sha(malformed)
                with self.assertRaisesRegex(ValueError, pattern):
                    self.inventory()
        path.write_bytes(data)

    def test_atomic_failure_preserves_previous_stage(self):
        from unittest.mock import patch
        inv = self.inventory()
        destination = self.root / 'stage'
        self.m.write_tree(inv, self.source, destination)
        before = (destination / 'inventory.json').read_bytes()
        rename = Path.rename
        def fail_candidate(path, target):
            if path.name == 'candidate':
                raise OSError('injected publication failure')
            return rename(path, target)
        with patch.object(Path, 'rename', fail_candidate), self.assertRaisesRegex(OSError, 'publication'):
            self.m.write_tree(inv, self.source, destination)
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
    COMPLETION = {'cases': 1, 'assertions': 4, 'failed_cases': 0, 'failed_assertions': 0}

    def test_duplicate_result_fields_are_malformed(self):
        import run_corpus_triage as triage
        payload = '{"path":"res://fixture/case.barista","passed":false,"passed":true,"expected":"BS_TEST_OK","actual":"BS_TEST_OK"}'
        process = {'output': 'BS_CASE_RESULT ' + payload + '\nBS_CASE_RAN case.barista\n', 'exit_code': 0, 'timed_out': False}
        self.assertEqual(triage.result_record(process, 'case.barista', 'res://fixture', 'BS_TEST_OK', self.COMPLETION)['terminal'],
                         'malformed_result')

    def test_exit_zero_without_execution_is_failure(self):
        import run_corpus_triage as triage
        process = {'output': '', 'exit_code': 0, 'timed_out': False}
        self.assertEqual(triage.result_record(process, 'case.barista', 'res://fixture', 'BS_TEST_OK', self.COMPLETION)['terminal'],
                         'missing_guard')
        payload = {'path': 'res://fixture/case.barista', 'passed': True, 'expected': 'BS_TEST_OK', 'actual': 'BS_TEST_OK'}
        process['output'] = 'BS_CASE_RESULT ' + json.dumps(payload) + '\nBS_CASE_RAN case.barista\n'
        self.assertTrue(triage.result_record(process, 'case.barista', 'res://fixture', 'BS_TEST_OK', self.COMPLETION)['passed'])
        broken = dict(process, output=process['output'].replace('BS_CASE_RAN case.barista', 'BS_CASE_RAN other.barista'))
        self.assertFalse(triage.result_record(broken, 'case.barista', 'res://fixture', 'BS_TEST_OK', self.COMPLETION)['passed'])
        # A completion record that did not run exactly this one case cannot report a pass either.
        for completion in (dict(self.COMPLETION, cases=0), dict(self.COMPLETION, assertions=0),
                           dict(self.COMPLETION, failed_cases=1)):
            self.assertFalse(triage.result_record(process, 'case.barista', 'res://fixture', 'BS_TEST_OK', completion)['passed'])
        process['timed_out'] = True
        self.assertEqual(triage.result_record(process, 'case.barista', 'res://fixture', 'BS_TEST_OK', self.COMPLETION)['terminal'],
                         'timeout')

    def test_timeout_supervises_real_process_and_preserves_output(self):
        import run_corpus_triage as triage
        result = triage.supervise([sys.executable, '-c', 'import time; print("started", flush=True); time.sleep(10)'], 0.2)
        self.assertTrue(result['timed_out'])
        self.assertLess(result['duration_seconds'], 3)
        self.assertIn('started', result['output'])


FOUNDRY = None

D1_SELECTED_CASES = (
    'errors/assign_narrowed_match_bind.barista',
    'errors/named_call_argument_rest_parameter.barista',
    'errors/type_alias_not_an_expression.barista',
    'errors/type_union_as_type_handle.barista',
    'errors/type_union_export_rejected.barista',
    'errors/type_union_runtime_type_operations.barista',
    'errors/typed_rest_parameter_external_argument.barista',
    'errors/typed_rest_parameter_override_narrowing.barista',
    'features/tuple_destructure_typing.norun.barista',
    'features/type_alias_union_export_single_member_retains_type.barista',
    'features/type_self_callable_matching_width_parameter.barista',
    'features/typed_rest_parameter_concrete.barista',
    'features/typed_rest_parameter_external.barista',
    'features/typed_rest_parameter_override_variance.barista',
)
D1_REWRITTEN_CASES = frozenset({
    'errors/assign_narrowed_match_bind.barista',
    'errors/named_call_argument_rest_parameter.barista',
    'errors/typed_rest_parameter_external_argument.barista',
    'errors/typed_rest_parameter_override_narrowing.barista',
    'features/tuple_destructure_typing.norun.barista',
    'features/type_self_callable_matching_width_parameter.barista',
    'features/typed_rest_parameter_concrete.barista',
    'features/typed_rest_parameter_external.barista',
    'features/typed_rest_parameter_override_variance.barista',
})
D1_EXPECTATION_OVERRIDE_CASES = frozenset({
    'errors/type_alias_not_an_expression.barista',
    'errors/type_union_as_type_handle.barista',
    'errors/type_union_export_rejected.barista',
    'errors/type_union_runtime_type_operations.barista',
    'features/type_alias_union_export_single_member_retains_type.barista',
})
D1_SOURCE_EDIT_PATHS = frozenset({
    'analyzer/errors/assign_narrowed_match_bind.fs',
    'analyzer/errors/named_call_argument_rest_parameter.fs',
    'analyzer/errors/type_alias_not_an_expression.fs',
    'analyzer/errors/type_union_as_type_handle.fs',
    'analyzer/errors/type_union_export_rejected.fs',
    'analyzer/errors/type_union_runtime_type_operations.fs',
    'analyzer/errors/typed_rest_parameter_override_narrowing.fs',
    'analyzer/features/tuple_destructure_typing.norun.fs',
    'analyzer/features/type_alias_union_export_single_member_retains_type.fs',
    'analyzer/features/type_self_callable_matching_width_parameter.fs',
    'analyzer/features/typed_rest_parameter_concrete.fs',
    'analyzer/features/typed_rest_parameter_external.fs',
    'analyzer/features/typed_rest_parameter_external_provider.notest.fs',
    'analyzer/features/typed_rest_parameter_override_variance.fs',
})
D1_EXPECTATION_EDIT_PATHS = frozenset({
    'analyzer/errors/type_alias_not_an_expression.out',
    'analyzer/errors/type_union_as_type_handle.out',
    'analyzer/errors/type_union_export_rejected.out',
    'analyzer/errors/type_union_runtime_type_operations.out',
    'analyzer/features/type_alias_union_export_single_member_retains_type.out',
})
TYPED_CONTAINER_CASE = 'errors/type_union_in_typed_container.barista'
TYPED_CONTAINER_SOURCE_PATH = 'analyzer/errors/type_union_in_typed_container.fs'
TYPED_CONTAINER_EXPECTATION_PATH = 'analyzer/errors/type_union_in_typed_container.out'



# Reviewed safe subset of #45's contextual tagged-union packet. Each tuple pins
# real Foundry source, projected source, raw output, and complete static block.
CONTEXTUAL_SPECS = {
    'errors/contextual_tagged_union_is_non_union_operand.barista': (
        '75f5d2e9ae3879da8ac2ac6ba103e875d85af73ec513f3d91cb9810fde82d289',
        'b99845e1400c03a1f1d62cbd587484e1e1c1e2dc7a24b01349f729a65b2aa38b',
        '252ec3eabd35b9858f8188ca8f0fd7340f40ff9ec5684e0810a96603a1215045',
        '58dacf93313bd06e504523b559aa38e9e33cfa475d3bb7eb48bf6e86e31f4ba2',
    ),
    'errors/contextual_tagged_union_is_undeclared_operand.barista': (
        '7c7823d8b58ae76b05c023c8fa2fa44fc3390ea59220488082da2dd82d30e856',
        '094b97340bb25c0dc436853d2d397d88220c12c8a6faeecc2ba4ecedc2169499',
        '799c10905793243562fe470754a5c04db5ab130c3db068cdd0f3b533d7ffcad4',
        '3ab0d287fbacf1b7c80c7bbc542cd090f6b18695234bb45804d17db0628abb49',
    ),
    'errors/contextual_tagged_union_is_unknown_case.barista': (
        '74ac2263970bf1e3210ea7d6fbd539c126b0d18a576f866f559d2315795ccc6f',
        '080c341cc9bfaf803014b41325800d3cc030550456b124044dc40cf35bfcc4be',
        '4dfa7a584d5611577bff0456d0b2df858d96fc926c01a1fc3a5880100dd61d2e',
        'eb4b6c7864f8c6fbb90ffca6071b5f7cd034321189a206a4053f03210586e390',
    ),
    'errors/contextual_tagged_union_pattern_non_union_subject.barista': (
        '9356cf3cd1129f9f8874e3b9319fafedbf23280aca64b40aac1d92ffa849a039',
        '66f851525dfc71675d4122f844a5392031a715ed6ff26d1d1990ebc4dc8ed666',
        'ff054cd93971b026dad7febc182e59d3b23ad9e5ff90e86898989d8e3ec18fb8',
        'ab9db825c7a13d6772a3f2405df5c428e343578c7faf128b560f9f4ab6cd73c8',
    ),
    'errors/contextual_tagged_union_pattern_payload_form.barista': (
        '6809bfb8f6da2f08b3094e60d50e381252f22a5a41636ad8217700efbdd64d2d',
        '4eaca933f5504565eadaebce84e9f1acaa285873e3307228a31c1ed6de2ba15c',
        '5fc5cdb631b572d795254883b03ba9a97b558bee3aada67e6ac7bb3b74216f36',
        'e74e770033097a3adb00eb25fa8bb122dcc275b3d815f351f515204b44813d4c',
    ),
    'errors/contextual_tagged_union_pattern_undeclared_subject.barista': (
        '8a3c5c1ecfc0897bbd3f26b33bc8423beeb30774e4943ebfc6fdad9eeba6218c',
        '1df18100952301ea43cd23077ae274b250773273c4378e514a4bc4692538c4a5',
        '11d029d61dac75296c7efe037ed7caa3603e0442b470cfe76023ca58fd6695a5',
        'd5db9aa9119b75091401e405e4e737bbddf73008535fa47d856cf073f61281de',
    ),
    'errors/contextual_tagged_union_pattern_unknown_case.barista': (
        '4818185ce73dee22de534353acde099f13777d3c5b7c3d294a3ea5f52f4d37e2',
        '4f285c1019d50d30b3abff6cf4c489c5d9492332f1c682c56126e17ff6a11a8e',
        '02f62ccc265505bdcbef26a700dd1787f57e251587ce2a501d23256bf8a348ac',
        '7bc7435cfffaa3dceff670f7f401af1839195302af8b81961ecaec284d7a9626',
    ),
    'errors/contextual_tagged_union_pattern_variant_subject.barista': (
        '11e11d2a0c4c1f191f7d97da51c59ef5fadfe8eca75862ab0d780f82720c1298',
        'ab93c893003c4c8412c1ed154c0c4bec557b6a2d1609abaf40f93d23dd2d3108',
        '6f57bf32aa92793521b3f23e8a3e9563cb0c13912911d12c1a6db5e671dc0ed1',
        'c1a17682ffadec1c4142be481f502b5293383a05e114130ab62d7a3bdea81459',
    ),
    'errors/contextual_tagged_union_payload_arity.barista': (
        '088f5dbeeb040096923dfe28fe55e02b969192ea61fd1dbc7d82109b51d9dd99',
        'b04ba2a1705cd03398d6c9c33b0a5b84716ca5b3bc496f4e7ecf18f3fddd53a7',
        'f577b127b970efcfd73597b323309ff7be41cc6b63c7fa6a3dd89cbe1531e394',
        'ec9bed201c43881aedfbf9b61ce61c05a18f4e108529861189756fe5fce47b25',
    ),
    'errors/contextual_tagged_union_payload_form_mismatch.barista': (
        '33c78a6f4b5cae96ea5b144faddb62fe359c943c087c0d963d7bb4e1f6828b2c',
        '4780a097cb7a4161275bf8b7e0fe7585049925d0610b69e7ef990818f8c3f06d',
        '4cc56da1b30bf08849a7a72370ab42dc382806b85a5a05e0b6ad99154e794f9e',
        'bb97e5f89897be44512920d4ca5d3c7a61aecdb67484ebaebe36f8e1664e1c9c',
    ),
    'errors/contextual_tagged_union_unknown_case.barista': (
        'ff8134e0e5ba8cfae9e679ea6d30ed6cf377f99f3428374af6b08ce6868ea0eb',
        '54d675584eabc982b8cb57026c4a7e98267e3cb7d94ea1d6f87eb25962fdb327',
        '4dfa7a584d5611577bff0456d0b2df858d96fc926c01a1fc3a5880100dd61d2e',
        'eb4b6c7864f8c6fbb90ffca6071b5f7cd034321189a206a4053f03210586e390',
    ),
    'features/contextual_tagged_union_argument.barista': (
        '7ce252d5ec68062c95518ba7af3491054a9dd8dac567529f602659923e666874',
        '95dd800f552ee4262ff38688dc1263ffc35299ee2e6ca737724921f9c9b1656b',
        '6e81fd7cf7f28493f472c633b76d170f2db78f409bd8ccf0539de8c02b3dd43f',
        '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
    ),
    'features/contextual_tagged_union_assignment.barista': (
        '2abdc5ee879692f54155880972da028391114f9655f6833d9c03b4c1e109f1ea',
        'e28680d48437ff093de226fcc03dac9fe4394841e99456ff7441b2e0b288ec05',
        'a63c87e7b8a074fdfa8595bb115d867178e617370f67ce57aaf635db14968ce1',
        '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
    ),
    'features/contextual_tagged_union_default_value.barista': (
        '0543bcfe745c23a0d90d6509173143510ee0fac9668bd9d9da1fad4a7c36305a',
        'fe0fb73cadfc00681350e85a5d2f5190152eb40e59dd6e53d0d8bba809daf908',
        '0afa3b1585224f8084e7d5c9537f2f1890cbd1ab55443d0188bb9b50e84eedb9',
        '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
    ),
    'features/contextual_tagged_union_dictionary_entry.barista': (
        '6658562981880b644775fa591fb47f943108286d94ae09660f3bce77a068ef89',
        '4521609a437a46b5654978b5c59830f6a7e751293f20ddf8761715f5dae5b8d1',
        'cfd3ad2cf78c00fbfd19381cfb4d089e0c6c9cad291aed8bfad2574992a3e78d',
        '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
    ),
    'features/contextual_tagged_union_exhaustive_match.barista': (
        'd7bcf22de6e9e5eca62773d6e0aaa8c5e44e9018167861adae1b8c19644c0429',
        '4dcbbd6ce21cdd7faf01aa92e6ff159d5c6dfb72e77ec8a6b4f0161c78825054',
        '5ba0c0b0f12b4ba818123482aaa982616ce6b2d5130dc1cc37d3a8f2d6d8807c',
        '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
    ),
    'features/contextual_tagged_union_is_pattern.barista': (
        'e0c97e4f4d8765ed186475cd2016ff2ac2efd48b1f444a2576fc37d3367204d8',
        'a1fe8e20280a8733cb1c5338fed96b1e0b157ab4573750a50fa0a010e9c61790',
        '62746eca3913bf58b11f2c3ae4a0267819b766e985097d38bdab5489092044cf',
        '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
    ),
    'features/contextual_tagged_union_match_pattern.barista': (
        'b98e1824e748df998c16138484e1d4de7cbd89e267cc9317a83d652812c57020',
        '831b419d2e6fc2712ebcf2d09483e07312cb14f139edff52e74df60120296173',
        'c05daf06623348791a9d2329341c3f0659f6b01c6d67479bd9d914a94bb1787d',
        '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
    ),
    'features/contextual_tagged_union_payloadless.barista': (
        '4003173820815399c2533ed614218ff88e2ce2192dd49b2a50c88fba49e24f2e',
        '8f990d5c498138cf33e5b1025b27da8122755049d3e45b5a2481bc19f5fb865a',
        '11aa67f4ac23894f0f83cbdc7231337bd54c9b5f3549440f15d6a50c9effd5e8',
        '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
    ),
    'features/contextual_tagged_union_return.barista': (
        '6e686c8fdcb1477bc032bd3c9c3bd60832851a0f434945a4e84fb907ee35e0c5',
        '880381bcbe0e9de1684e4954e1d89631bd18b4ca0c913e21aa777683b8f328ea',
        '647d0d6c863f92f6bd3a59aa1798da339a76e2a633c0b6f5b2a92057abb6f1fc',
        '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
    ),
    'features/contextual_tagged_union_ternary.barista': (
        'a64e4c656304f32e60fb6ef26e0938803146f65fcc1c21662d2acfba901885da',
        'a25fca307d5772ca4b97f963bafb250a9d1c2ca20bde63d2eccbab59b5b583a9',
        '28e8ba98d810dd69214efada7b35694bce15c0fbc4ab247063ddadf7abdc27a2',
        '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
    ),
    'warnings/contextual_tagged_union_match_nullable_subject.barista': (
        'ef5c69ab5e53fa7ad5ea24bbaf95ba5e211811396445751f8d97a6b1ff1327bc',
        '9f0f409a41872b82f16e782a78d8556c21b6ea87a2646b4c5db6faf0189aec18',
        '2d3da5c5fe7b4a3a8b9f0c8ab12bc3a9a6126c4e709066fa3c7ac0948b960200',
        '9862246ee82c032ae1f421995d4d99eb99342758a2635f0f1e91fa0e28e81fa4',
    ),
}
CONTEXTUAL_SELECTED_CASES = tuple(CONTEXTUAL_SPECS)
CONTEXTUAL_SOURCE_PATHS = frozenset(
    'analyzer/' + case.removesuffix('.barista') + '.fs'
    for case in CONTEXTUAL_SELECTED_CASES
)


class FullPinned(unittest.TestCase):
    def assert_safe_contextual_policy(self, policy):
        import import_analyzer_corpus as importer

        selected = set(CONTEXTUAL_SELECTED_CASES)
        self.assertTrue(selected <= set(policy['rewritten']))
        self.assertTrue(CONTEXTUAL_SOURCE_PATHS <= set(policy['source_edits']))
        edits = {path: policy['source_edits'][path]
                 for path in sorted(CONTEXTUAL_SOURCE_PATHS)}
        self.assertEqual(hashlib.sha256(json.dumps(
            edits, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()).hexdigest(),
            '12209f98420de0259041d308fe91137464e68eddc75b84194dfabc6a3b38fe8a')
        self.assertEqual(sum(len(entry['patches']) for entry in edits.values()), 137)
        for case in selected:
            self.assertEqual(policy['rewritten'][case], policy['owners'][case]['reason'])

        # Removing exactly this packet recovers every prior table byte-for-byte:
        # owner/source review, expectations, helper/provider, and dispositions.
        predecessor = copy.deepcopy(policy)
        for case in selected:
            predecessor['rewritten'].pop(case)
        for path in CONTEXTUAL_SOURCE_PATHS:
            predecessor['source_edits'].pop(path)
        self.assertEqual(hashlib.sha256(importer.encoded(predecessor)).hexdigest(),
                         '01e2364afa4ccca921aa16911c128b45aa5284d4a2e8e80a0b028ee675af272b')

    def test_safe_contextual_policy_contract_and_negative_controls(self):
        import import_analyzer_corpus as importer

        policy = importer.default_policy()
        self.assert_safe_contextual_policy(policy)
        case_list_sha = lambda paths: hashlib.sha256(('\n'.join(paths) + '\n').encode()).hexdigest()
        self.assertEqual(CONTEXTUAL_SELECTED_CASES, tuple(sorted(CONTEXTUAL_SELECTED_CASES)))
        self.assertEqual((len(CONTEXTUAL_SELECTED_CASES), case_list_sha(CONTEXTUAL_SELECTED_CASES)),
                         (22, 'b3fa2ad2bb9be82c3fa46bda49b605679a0195b1a7b37cc9c4fdb57b4ca558fb'))
        self.assertEqual(case_list_sha(sorted(CONTEXTUAL_SOURCE_PATHS)),
                         '727c4e5f82ab1333891b7bbe91e2c21c20b774132834177530bb1f6aba7608ab')
        self.assertEqual(collections.Counter(case.split('/')[0] for case in CONTEXTUAL_SELECTED_CASES),
                         {'errors': 11, 'features': 10, 'warnings': 1})

        case = CONTEXTUAL_SELECTED_CASES[0]
        path = 'analyzer/' + case.removesuffix('.barista') + '.fs'
        # Valid schema alone cannot authorize a different semantic projection.
        mutations = [
            ('source hash', lambda p: p['source_edits'][path].__setitem__('sha256', '0' * 64)),
            ('missing source', lambda p: p['source_edits'].pop(path)),
            ('missing patch', lambda p: p['source_edits'][path]['patches'].pop()),
            ('overlap/extra patch', lambda p: p['source_edits'][path]['patches'].append(
                copy.deepcopy(p['source_edits'][path]['patches'][0]))),
            ('missing disposition', lambda p: p['rewritten'].pop(case)),
            ('wrong disposition', lambda p: p['rewritten'].__setitem__(case, 'different assertion')),
            ('expectation edit', lambda p: p['expectation_edits'].__setitem__(
                path.removesuffix('.fs') + '.out', {'sha256': '0' * 64, 'patches': []})),
            ('expectation override', lambda p: p['expectation_overrides'].__setitem__(case, 'override')),
            ('owner', lambda p: p['owners'][case].__setitem__('primary_issue', 139)),
            ('provider/helper', lambda p: p['source_edits'].__setitem__(
                'analyzer/features/unreviewed.notest.fs', {'sha256': '0' * 64, 'patches': []})),
            ('helper clone', lambda p: p['source_edits'][path].__setitem__(
                'projected_helper_clone', {})),
            ('diagnostic-sensitive boundary', lambda p: p['rewritten'].__setitem__(
                'errors/contextual_tagged_union_no_expected_type.barista', 'unsafe projection')),
            ('remaining contextual boundary', lambda p: p['rewritten'].__setitem__(
                'features/contextual_tagged_union_nested_generic.barista', 'out of packet')),
            ('near-name boundary', lambda p: p['rewritten'].__setitem__(
                'errors/named_tuple_contextual_case_non_union_field.barista', 'out of packet')),
        ]
        for field, value in (('start', 0), ('end', 1), ('line', 1), ('before', 'wrong'),
                             ('after', 'enum Different:'), ('rule', 'unreviewed-rule'),
                             ('occurrences', 2)):
            mutations.append((field, lambda p, field=field, value=value:
                              p['source_edits'][path]['patches'][0].__setitem__(field, value)))
        for label, mutate in mutations:
            with self.subTest(mutation=label):
                candidate = copy.deepcopy(policy)
                mutate(candidate)
                with self.assertRaises(AssertionError):
                    self.assert_safe_contextual_policy(candidate)

    def test_safe_contextual_real_producer_projection_and_preimages(self):
        if FOUNDRY is None:
            self.skipTest('full producer requires --foundry; policy lock negative controls ran')
        import import_analyzer_corpus as importer
        import corpus_registry

        registry = corpus_registry.validate_registration(ROOT)
        corpus_registry.verify_checkout(FOUNDRY, registry, registry['revision'])
        policy = importer.default_policy()
        self.assert_safe_contextual_policy(policy)
        source = FOUNDRY / importer.SCRIPTS
        inventory = importer.inventory_sources(source, policy, 'res://tests/corpus/analyzer')
        records = {record['imported_path']: record for record in inventory['sources']
                   if record['role'] == 'case'}
        for case, (raw_sha, projected_sha, output_sha, block_sha) in CONTEXTUAL_SPECS.items():
            with self.subTest(case=case):
                path = 'analyzer/' + case.removesuffix('.barista') + '.fs'
                data = (source / path).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), raw_sha)
                entry = policy['source_edits'][path]
                self.assertEqual(set(entry), {'sha256', 'patches'})
                changes = importer.source_policy_changes(data, path, policy)
                projected = importer.patch(data, changes, path)
                self.assertEqual(hashlib.sha256(projected).hexdigest(), projected_sha)
                self.assertEqual(data.count(b'\n'), projected.count(b'\n'))
                # These local declarations contain no quoted '#'; all comments,
                # including examples of generic syntax, remain exact.
                comments = lambda text: [line.partition(b'#')[2] for line in text.splitlines()]
                self.assertEqual(comments(data), comments(projected))
                executable = b'\n'.join(line.partition(b'#')[0] for line in projected.splitlines())
                self.assertNotRegex(executable, rb'(?:Result|Option|Nested)\s*\[')
                for change in changes:
                    prefix = data[:change['start']].rsplit(b'\n', 1)[-1]
                    self.assertNotIn(b'#', prefix)
                output = (source / (path.removesuffix('.fs') + '.out')).read_bytes()
                self.assertEqual(hashlib.sha256(output).hexdigest(), output_sha)
                record = records[case]
                self.assertEqual((record['disposition'], record['stage']), ('rewritten', 'analyzer'))
                self.assertEqual(record['sha256'], raw_sha)
                self.assertEqual(record['imported_sha256'], projected_sha)
                self.assertEqual(record['expectation_sha256'], output_sha)
                self.assertEqual(hashlib.sha256(record['expected_block'].encode()).hexdigest(), block_sha)
                self.assertNotRegex(record['expected_block'], r'(?:Result|Option|Nested)\s*\[')
                self.assertEqual(record['references'], [])
                self.assertEqual(record['expectation_edits'], [])
                self.assertEqual(record['transformations'], entry['patches'])
                self.assertEqual(record['semantic_owner'], policy['owners'][case])

        path = 'analyzer/errors/contextual_tagged_union_is_non_union_operand.fs'
        data = (source / path).read_bytes()
        for field, value, pattern in (
                ('start', 0, 'preimage'), ('end', 1, 'span'),
                ('line', 1, 'line preimage'), ('before', 'wrong', 'preimage'),
                ('rule', '', 'invalid patch provenance'),
                ('occurrences', 2, 'invalid patch provenance')):
            with self.subTest(importer_rejection=field):
                candidate = copy.deepcopy(policy)
                candidate['source_edits'][path]['patches'][0][field] = value
                with self.assertRaisesRegex(ValueError, pattern):
                    importer.source_policy_changes(data, path, candidate)
        # An exact span in a comment is mechanically legal but outside the
        # reviewed patch contract; prove the packet lock rejects that addition.
        comment_start = data.index(b'#')
        comment = importer.change(data, comment_start, comment_start + 1, '#', 'comment-only')
        candidate = copy.deepcopy(policy)
        candidate['source_edits'][path]['patches'].append(comment)
        with self.assertRaises(AssertionError):
            self.assert_safe_contextual_policy(candidate)

    def test_explicit_d1_projection_contract_and_fail_closed(self):
        if FOUNDRY is None:
            self.skipTest('full producer requires --foundry; byte-faithful miniature tests ran')
        import import_analyzer_corpus as importer

        def canonical_sha(value):
            payload = json.dumps(value, sort_keys=True, separators=(',', ':'),
                                 ensure_ascii=False).encode()
            return hashlib.sha256(payload).hexdigest()

        def case_list_sha(paths):
            return hashlib.sha256(('\n'.join(paths) + '\n').encode()).hexdigest()

        policy = importer.default_policy()
        source = FOUNDRY / importer.SCRIPTS
        uri = 'res://tests/corpus/analyzer'
        provider_path = 'analyzer/features/typed_rest_parameter_external_provider.notest.fs'
        reintroduced_case = TYPED_CONTAINER_CASE
        reintroduced_source = TYPED_CONTAINER_SOURCE_PATH
        reintroduced_expectation = TYPED_CONTAINER_EXPECTATION_PATH

        self.assertEqual(policy['schema_version'], 2)
        self.assertEqual(hashlib.sha256(importer.POLICY.read_bytes()).hexdigest(),
                         '81dec9f94b89918de026b8860da127495e02546ebb0290242c0084c9b10718c1')
        self.assertEqual((len(D1_SELECTED_CASES), case_list_sha(D1_SELECTED_CASES)),
                         (14, 'e9fdfd464a5bd5f0dcf4e78c5c05210ca14429442e332b4bbed3201b488ca798'))
        self.assertEqual((len(D1_REWRITTEN_CASES), case_list_sha(sorted(D1_REWRITTEN_CASES))),
                         (9, 'a5480469575b6c04fccaae5fd433447af909440c16db52179837eec4fd03915b'))
        self.assertEqual((len(D1_EXPECTATION_OVERRIDE_CASES),
                          case_list_sha(sorted(D1_EXPECTATION_OVERRIDE_CASES))),
                         (5, '367404a4be34a2fc52b0f6c9137a6226546f5c97cb929d6e748e58536471ef27'))
        self.assertEqual((len(D1_SOURCE_EDIT_PATHS), case_list_sha(sorted(D1_SOURCE_EDIT_PATHS))),
                         (14, 'cb9bc11fb8163f3df77c09659b45ab2ed94b35f35a6502f17c698d8ed04627bd'))
        self.assertEqual((len(D1_EXPECTATION_EDIT_PATHS),
                          case_list_sha(sorted(D1_EXPECTATION_EDIT_PATHS))),
                         (5, '8972a7f89b9f467886e15558693f261887e2a2af7522fb52735c69b4d1431644'))
        self.assertEqual({case for case in D1_SELECTED_CASES if case in policy['rewritten']},
                         D1_REWRITTEN_CASES)
        self.assertEqual({case for case in D1_SELECTED_CASES
                          if case in policy['expectation_overrides']},
                         D1_EXPECTATION_OVERRIDE_CASES)
        self.assertTrue(D1_SOURCE_EDIT_PATHS <= set(policy['source_edits']))
        self.assertTrue(D1_EXPECTATION_EDIT_PATHS <= set(policy['expectation_edits']))
        direct_sources = D1_SOURCE_EDIT_PATHS - {provider_path}
        self.assertEqual(sum(len(policy['source_edits'][path]['patches'])
                             for path in direct_sources), 31)
        self.assertEqual(len(policy['source_edits'][provider_path]['patches']), 2)
        self.assertEqual(sum(len(policy['expectation_edits'][path]['patches'])
                             for path in D1_EXPECTATION_EDIT_PATHS), 7)

        self.assertNotIn(reintroduced_case, policy['rewritten'])
        self.assertEqual(policy['expectation_overrides'][reintroduced_case],
                         'All four diagnostics reject a multi-alternative union in Array element or Dictionary key/value positions. Replacing uint with String preserves those four positions; Barista canonicalizes the concrete union diagnostic as String | int.')
        self.assertEqual(canonical_sha(policy['owners'][reintroduced_case]),
                         '2ad342f04b877be404406b0d1e9c04b8f8a0fed1d167d8b8b61974b156f75154')
        source_entry = policy['source_edits'][reintroduced_source]
        self.assertEqual(canonical_sha(source_entry),
                         '8c49b6868e5679acaf9f208ee4f923365783dbbaefe83cc0b7b71bd52c2c883c')
        source_data = (source / reintroduced_source).read_bytes()
        self.assertEqual(hashlib.sha256(source_data).hexdigest(),
                         '5e3ec8468cc9a43cac2e8611334e00b84fe3b9c93e8cc5e90db208321b694b98')
        self.assertEqual(hashlib.sha256(importer.patch(
            source_data, importer.source_policy_changes(source_data, reintroduced_source, policy),
            reintroduced_source)).hexdigest(),
            'c4aa3e17d857c4e740f1abb0490d3de89068d9b2a03f3c09af14af809d69a6ec')
        expectation_entry = policy['expectation_edits'][reintroduced_expectation]
        self.assertEqual(canonical_sha(expectation_entry),
                         '08924e7803c7f29e751ccb410dc8d0de541399550d64c9f68ffe81f4dd1f64ea')
        expectation_data = (source / reintroduced_expectation).read_bytes()
        self.assertEqual(hashlib.sha256(expectation_data).hexdigest(),
                         '5b5f73698e432e3f7b13fcac8b083a64fe2dad4ff0bab52b86bc506d8d71b42c')
        self.assertEqual(hashlib.sha256(importer.expectation_patch(
            expectation_data, expectation_entry['patches'], reintroduced_expectation)).hexdigest(),
            '3360d3bb3175069e23f40081fce6d7508a8b39c2fbfe5975716e3a798dc34509')
        self.assertEqual(canonical_sha(policy['owners']['errors/type_alias_not_an_expression.barista']),
                         'f5acf00fc7eb000b622ac6607b6bb15376556d4d8f865a3e794549fbb80a5e07')
        self.assertEqual(canonical_sha(policy['owners']['errors/type_union_as_type_handle.barista']),
                         'f1e42ab31f07a3917f648538689332c668cdf7d7be22e40c0addb3f0f2057e96')

        expectation_specs = {
            'analyzer/errors/type_alias_not_an_expression.out':
                ('c4e0feeae2cb17aad0e8b262b1600a059a724c33bbd71a38706fe429b34dc853',
                 '35d29f71a91e593e24891282d1db66bf88efcb042a2739551b7364dc607207d9',
                 'd7a665b89214ff82f53b05d8c428f7ace39996d4389222b654aa5e84e1840d44'),
            'analyzer/errors/type_union_as_type_handle.out':
                ('d8646e1ad2680961c49f4c4c495f79fb55545f6acd972d700eb1e3ecd09267e7',
                 '1dd350196faf904f14eb5d052351f23d21743d3f5bffde627a476752234cf575',
                 'fa0229bfd6e37ecbf464e3612b8bb973ec6ed6b9470098fd99fb52e872a9e86c'),
            'analyzer/errors/type_union_export_rejected.out':
                ('a6bb75d9dad1eed2a73892e465e689072577d780670014995c9dc006999d6078',
                 'b568e51aa92cd7d2ff8f3c1ddb4369c83b37480fc312028b99422fdd820a6d68',
                 'e72a241a3294a2ef646e36f6c3ec832089ae2ab1d9e7313ec0ebd3d9bb00a093'),
            'analyzer/errors/type_union_runtime_type_operations.out':
                ('e396f768df411cf1fa57f589500087ac45318d182d27f1228565d0022255032c',
                 '85c1d496b0e2279865582aa5aaf4ab0759cebaf2177c9526f529c3b7b9c07667',
                 'ff15a50e9a5d86b13fe6e9406291e0900265567505d4ec586d0ff6697297f52e'),
            'analyzer/features/type_alias_union_export_single_member_retains_type.out':
                ('6d6689ed1a411f79e719d0a29a9cf8d76c9753a4260836c7ffb79e9535aa129b',
                 '09e65a45537df2baf76eb986b8fe0cf945fe3efd3db9a0a756f48b55b5a67251',
                 '3e259e6c42f9d7ff916d732f5d10e892e3bd06b1780ad9724d1d546d279ffcd6'),
        }
        line_changing = set()
        for path, (entry_sha, raw_sha, projected_sha) in expectation_specs.items():
            with self.subTest(expectation=path):
                entry = policy['expectation_edits'][path]
                self.assertEqual(canonical_sha(entry), entry_sha)
                data = (source / path).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), raw_sha)
                projected = importer.expectation_patch(data, entry['patches'], path)
                self.assertEqual(hashlib.sha256(projected).hexdigest(), projected_sha)
                if any(change['before'].count('\n') != change['after'].count('\n')
                       for change in entry['patches']):
                    line_changing.add(path)
                    with self.assertRaisesRegex(ValueError, 'invalid patch provenance'):
                        importer.patch(data, entry['patches'], path)
        self.assertEqual(line_changing, {
            'analyzer/errors/type_alias_not_an_expression.out',
            'analyzer/errors/type_union_as_type_handle.out',
        })

        first = importer.inventory_sources(source, policy, uri)
        self.assertEqual(first, importer.inventory_sources(source, policy, uri))
        self.assertEqual(first['schema_version'], 1)
        self.assertEqual(hashlib.sha256(importer.encoded(first)).hexdigest(),
                         'afe887e96f179b58c9b784a5249f258742b42b8148224e5fa112c201f8a424e7')
        self.assertEqual((first['counts']['sources'], first['counts']['cases'],
                          first['counts']['helpers'], first['counts']['support_helpers']),
                         (1596, 1346, 250, 2))
        self.assertEqual((first['ledger']['total'], first['ledger']['skipped']), (1078, 252))
        records = {record['imported_path']: record for record in first['sources']
                   if record.get('role') == 'case'}
        for case in D1_SELECTED_CASES:
            with self.subTest(case=case):
                record = records[case]
                expected_disposition = ('rewritten' if case in D1_REWRITTEN_CASES
                                        else 'expectation_overrides')
                self.assertEqual(record['disposition'], expected_disposition)
                self.assertEqual(record['upstream_path'],
                                 'analyzer/' + case.removesuffix('.barista') + '.fs')
                self.assertTrue(record['expected_block'])
        reintroduced = records[reintroduced_case]
        self.assertEqual(reintroduced['disposition'], 'expectation_overrides')
        self.assertEqual(reintroduced['sha256'],
                         '5e3ec8468cc9a43cac2e8611334e00b84fe3b9c93e8cc5e90db208321b694b98')
        self.assertEqual(reintroduced['imported_sha256'],
                         'c4aa3e17d857c4e740f1abb0490d3de89068d9b2a03f3c09af14af809d69a6ec')
        self.assertEqual(reintroduced['expectation_sha256'],
                         '5b5f73698e432e3f7b13fcac8b083a64fe2dad4ff0bab52b86bc506d8d71b42c')
        self.assertEqual(hashlib.sha256(reintroduced['expected_block'].encode()).hexdigest(),
                         '8cba00062f01b2751e8e9bc996dde2fc4000d3b0dfb21303eea5c1efb5fabd73')
        self.assertEqual((len(reintroduced['transformations']),
                          len(reintroduced['expectation_edits'])), (2, 4))

        provider = next(record for record in first['sources']
                        if record['upstream_path'] == provider_path)
        self.assertEqual((provider['role'], provider['sha256'], provider['imported_sha256']),
                         ('helper',
                          '63ec633ac970fa3690990531f325038e9dfaf73ba147c5b61efb8aa59060aebe',
                          '34c1547378a704872f079c57e06f953ceff0ab75d35bb265fa3f2a449ffadc62'))
        self.assertNotIn('projected_helper_clone', policy['source_edits'][provider_path])
        self.assertEqual(sum(record['imported_path'] == provider['imported_path']
                             for record in first['sources']), 1)
        self.assertEqual(sum(provider_path == reference.get('target')
                             for record in first['sources']
                             for reference in record.get('references', [])), 2)

        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary) / 'stage'
            importer.write_tree(first, source, stage)
            importer.check_tree(first, source, stage)
            before = {path.relative_to(stage): path.read_bytes()
                      for path in stage.rglob('*') if path.is_file()}
            importer.write_tree(first, source, stage)
            self.assertEqual(before, {path.relative_to(stage): path.read_bytes()
                                      for path in stage.rglob('*') if path.is_file()})
            self.assertEqual(len(before), 2411)
            self.assertEqual(hashlib.sha256((stage / 'inventory.json').read_bytes()).hexdigest(),
                             'afe887e96f179b58c9b784a5249f258742b42b8148224e5fa112c201f8a424e7')
            self.assertEqual(hashlib.sha256((stage / 'case_stages.json').read_bytes()).hexdigest(),
                             '3ad070abe3df618bcb3e01bc6c5cdc753884b9e30819c606eae5a2e515ff9202')
            tree = hashlib.sha256()
            for path in sorted(candidate for candidate in stage.rglob('*') if candidate.is_file()):
                tree.update(hashlib.sha256(path.read_bytes()).hexdigest().encode())
                tree.update(b'  ./')
                tree.update(path.relative_to(stage).as_posix().encode())
                tree.update(b'\n')
            self.assertEqual(tree.hexdigest(),
                             'f8933bcd2cb6b4f43734c19eb313a1ca19bb5caf8340d3930a855277bb95c919')

        def expect_inventory_failure(mutator, pattern):
            candidate = copy.deepcopy(policy)
            mutator(candidate)
            with self.assertRaisesRegex(ValueError, pattern):
                importer.inventory_sources(source, candidate, uri)

        source_case = 'errors/assign_narrowed_match_bind.barista'
        source_path = 'analyzer/errors/assign_narrowed_match_bind.fs'
        expectation_case = 'errors/type_alias_not_an_expression.barista'
        expectation_path = 'analyzer/errors/type_alias_not_an_expression.out'
        expect_inventory_failure(
            lambda candidate: candidate['source_edits'][source_path].__setitem__('sha256', '0' * 64),
            'source edit hash preimage mismatch')
        expect_inventory_failure(
            lambda candidate: candidate['source_edits'][source_path]['patches'][0].__setitem__(
                'start', candidate['source_edits'][source_path]['patches'][0]['start'] + 1),
            'preimage|span')
        expect_inventory_failure(
            lambda candidate: candidate['source_edits'][source_path]['patches'][0].__setitem__(
                'line', candidate['source_edits'][source_path]['patches'][0]['line'] + 1),
            'line preimage mismatch')
        expect_inventory_failure(
            lambda candidate: candidate['source_edits'][source_path]['patches'][0].__setitem__(
                'before', 'wrong'), 'preimage mismatch')
        expect_inventory_failure(
            lambda candidate: candidate['source_edits'][source_path]['patches'].append(
                copy.deepcopy(candidate['source_edits'][source_path]['patches'][0])), 'overlapping')
        expect_inventory_failure(lambda candidate: candidate['rewritten'].pop(source_case),
                                 'disposition')
        expect_inventory_failure(
            lambda candidate: candidate['expectation_edits'][expectation_path].__setitem__(
                'sha256', '0' * 64), 'expectation edit hash preimage mismatch')
        expect_inventory_failure(
            lambda candidate: candidate['expectation_edits'][expectation_path]['patches'][0].__setitem__(
                'line', candidate['expectation_edits'][expectation_path]['patches'][0]['line'] + 1),
            'line preimage mismatch')
        expect_inventory_failure(
            lambda candidate: candidate['expectation_edits'][expectation_path]['patches'][0].__setitem__(
                'before', 'wrong'), 'preimage mismatch')
        expect_inventory_failure(
            lambda candidate: candidate['expectation_edits'][expectation_path]['patches'].append(
                copy.deepcopy(candidate['expectation_edits'][expectation_path]['patches'][0])),
            'overlapping')
        expect_inventory_failure(
            lambda candidate: candidate['expectation_overrides'].pop(expectation_case),
            'override disposition')
        expect_inventory_failure(
            lambda candidate: candidate['source_edits'][provider_path].__setitem__('sha256', '0' * 64),
            'source edit hash preimage mismatch')
        expect_inventory_failure(
            lambda candidate: candidate['source_edits'][reintroduced_source].__setitem__(
                'sha256', '0' * 64), 'source edit hash preimage mismatch')
        expect_inventory_failure(
            lambda candidate: candidate['expectation_edits'][reintroduced_expectation].__setitem__(
                'sha256', '0' * 64), 'expectation edit hash preimage mismatch')
        expect_inventory_failure(
            lambda candidate: candidate['expectation_overrides'].pop(reintroduced_case),
            'override disposition')

    def test_complete_producer_inventory_and_staging(self):
        if FOUNDRY is None:
            self.skipTest('full producer requires --foundry; byte-faithful miniature tests ran')
        import import_analyzer_corpus as importer
        import corpus_registry
        registry = corpus_registry.validate_registration(ROOT)
        corpus_registry.verify_checkout(FOUNDRY, registry, registry['revision'])
        policy = importer.default_policy()
        source = FOUNDRY / importer.SCRIPTS
        uri = 'res://tests/corpus/analyzer'
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
                         set(reviewed_cases) | D1_REWRITTEN_CASES | set(CONTEXTUAL_SELECTED_CASES) | {
                             'features/generic_tagged_union_global.barista',
                             'features/lookup_class.barista',
                         })
        projected_sources = {
            'analyzer/errors/type_alias_not_inherited.fs':
                ('d703f76f1f801aae337447301266d1648a9ad804bc83d4d84b7f540262215978',
                 'eab13c8d7dc11a9750706bfd4e60dbcceff9b7631a3139fe992040309fc8bfc6'),
            'analyzer/features/generic_tagged_union_global_values.notest.fs':
                ('092dba1d8f5db2282c5f363be89218bd11a72ebb5b270b73bbc897bb6116d317',
                 '207ca64f8be05b2cc2228b12d58cece03bc8b262dd6e5dd163e4433fcc037cb7'),
            'analyzer/features/generic_tagged_union_global.fs':
                ('b8c30ff7fbeeca9412e2e2f6bb5692dd62f10f3e935609d33e6aa0db4f8d17b5',
                 '52c8053dbae0faa33c415211dab555149a69eaf40172ffad68bd9d15c7c40fd0'),
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
        self.assertEqual(set(policy['source_edits']),
                         set(projected_sources) | D1_SOURCE_EDIT_PATHS | CONTEXTUAL_SOURCE_PATHS | {
            TYPED_CONTAINER_SOURCE_PATH,
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
        global_case = records_by_case['features/generic_tagged_union_global.barista']
        self.assertEqual(global_case['disposition'], 'rewritten')
        self.assertEqual(global_case['expected_block'], 'BS_TEST_OK')
        self.assertEqual(global_case['expectation_sha256'],
                         '92fdfe7e37090831fdf953924a39ba8464cbf361c1294c9879f4b2d03d06b6a2')
        self.assertEqual(global_case['imported_sha256'],
                         '52b470071de8c8d7504b901f015be11cf7737aa3db16b446ad04fffbb677dc47')
        clone = next(record for record in first['sources']
                     if record['imported_path'] == 'features/generic_tagged_union_global_flipped_values.notest.barista')
        self.assertEqual(clone['role'], 'helper')
        self.assertEqual(clone['sha256'],
                         '092dba1d8f5db2282c5f363be89218bd11a72ebb5b270b73bbc897bb6116d317')
        self.assertEqual(clone['imported_sha256'],
                         'd418b676e9c1d2dc83b2b957ec40c1475c4d6f15bccb6a4916d01ad559c253e5')
        self.assertIn('#projected-helper-clone:', clone['identity'])
        self.assertEqual(first['ledger']['skipped'], 252)
        cache_isolation = 'features/generic_tagged_union_cache_isolation.barista'
        self.assertIn(cache_isolation, policy['deferred'])
        self.assertNotIn(cache_isolation, {record['imported_path'] for record in first['sources']
                                           if record.get('disposition') not in ('deferred', 'excluded')})
        self.assertIn('Retirement:', policy['deferred'][cache_isolation])

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
            importer.write_tree(first, source, stage)
            importer.check_tree(first, source, stage)
            original = next(stage.rglob('*.barista'))
            data = original.read_bytes()
            original.write_bytes(bytes([data[0] ^ 1]) + data[1:])
            with self.assertRaisesRegex(ValueError, 'byte drift'):
                importer.check_tree(first, source, stage)
            original.write_bytes(data)
            original.unlink()
            with self.assertRaisesRegex(ValueError, 'population'):
                importer.check_tree(first, source, stage)


class ExpectedFailureDerivation(unittest.TestCase):
    """The pinned residual set is computed from policy owners plus one completed run."""

    def setUp(self):
        import import_analyzer_corpus
        self.importer = import_analyzer_corpus
        self.included = {'errors/alpha.barista', 'errors/beta.barista', 'features/gamma.barista'}
        self.triage = {'excluded': {}, 'rewritten': {}, 'expectation_overrides': {}, 'deferred': {}}
        self.policy = {'owners': {
            'errors/alpha.barista': {'reason': 'Contextual union inference is unimplemented.'},
            'errors/beta.barista': {'reason': 'Trait resolution order differs from upstream.'},
            'features/gamma.barista': {'reason': 'Typed callable binding is unimplemented.'},
        }}

    def report(self, failing, **overrides):
        cases = sorted(self.included)
        document = {
            'completed': True,
            'complete_population': True,
            'selected_cases': cases,
            'stopped_cases': [],
            'results': [{'case': case, 'passed': case not in failing,
                         'terminal': 'passed' if case not in failing else 'mismatch'}
                        for case in cases],
        }
        document.update(overrides)
        return document

    def derive(self, report):
        return self.importer.derive_expected_failures(self.policy, report, self.included, self.triage)

    def test_derives_the_sorted_failing_cases(self):
        derived = self.derive(self.report({'features/gamma.barista', 'errors/alpha.barista'}))
        self.assertEqual(derived, ['errors/alpha.barista', 'features/gamma.barista'])

    def test_derivation_is_stable_across_result_ordering(self):
        failing = {'features/gamma.barista', 'errors/alpha.barista'}
        forward = self.report(failing)
        reversed_report = dict(forward, results=list(reversed(forward['results'])))
        self.assertEqual(self.derive(forward), self.derive(reversed_report))
        self.assertEqual(self.derive(forward), sorted(self.derive(forward)))

    def test_a_fully_passing_run_derives_an_empty_pin(self):
        self.assertEqual(self.derive(self.report(set())), [])

    def test_a_failing_case_without_an_owner_is_named(self):
        del self.policy['owners']['errors/beta.barista']
        with self.assertRaisesRegex(ValueError, 'errors/beta.barista.*no semantic owner'):
            self.derive(self.report({'errors/beta.barista'}))

    def test_a_failing_case_with_a_blank_owner_reason_is_named(self):
        self.policy['owners']['errors/beta.barista'] = {'reason': '   '}
        with self.assertRaisesRegex(ValueError, 'errors/beta.barista.*non-empty reason'):
            self.derive(self.report({'errors/beta.barista'}))

    def test_a_result_for_a_case_the_run_did_not_select_is_refused(self):
        # A record outside the selection is refused whatever its outcome: a passing one would
        # otherwise be silently dropped, and a failing one judged against the wrong contract.
        for passed in (True, False):
            with self.subTest(passed=passed):
                report = self.report(set())
                report['results'].append({'case': 'errors/ghost.barista', 'passed': passed, 'terminal': 'mismatch'})
                with self.assertRaises(ValueError) as caught:
                    self.derive(report)
                self.assertEqual(str(caught.exception),
                                 'execution report records outcomes for cases it did not select: errors/ghost.barista')

    def test_a_failing_case_carrying_a_deferred_disposition_is_named(self):
        self.triage['deferred'] = {'errors/beta.barista': 'Deferred to a later milestone.'}
        with self.assertRaisesRegex(ValueError, 'errors/beta.barista.*deferred'):
            self.derive(self.report({'errors/beta.barista'}))

    def test_a_failing_case_carrying_an_excluded_disposition_is_named(self):
        self.triage['excluded'] = {'errors/beta.barista': 'Excluded from the imported tree.'}
        with self.assertRaisesRegex(ValueError, 'errors/beta.barista.*excluded'):
            self.derive(self.report({'errors/beta.barista'}))

    def test_an_incomplete_run_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'did not complete'):
            self.derive(self.report(set(), completed=False))

    def test_a_run_carrying_an_infrastructure_error_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'infrastructure error'):
            self.derive(self.report(set(), infrastructure_error='staged library changed'))

    def test_a_partial_population_run_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'complete population'):
            self.derive(self.report(set(), complete_population=False))

    def test_a_run_with_stopped_cases_is_refused(self):
        report = self.report(set())
        report['stopped_cases'] = ['errors/beta.barista']
        report['results'] = [r for r in report['results'] if r['case'] != 'errors/beta.barista']
        with self.assertRaisesRegex(ValueError, 'stopped these cases mid-flight: errors/beta.barista'):
            self.derive(report)

    def test_a_never_dispatched_case_is_refused(self):
        report = self.report(set())
        report['results'] = [r for r in report['results'] if r['case'] != 'errors/beta.barista']
        with self.assertRaisesRegex(ValueError, 'no recorded outcome for: errors/beta.barista'):
            self.derive(report)

    def test_a_run_over_a_different_population_is_refused(self):
        report = self.report(set())
        report['selected_cases'] = [c for c in report['selected_cases'] if c != 'errors/beta.barista']
        report['results'] = [r for r in report['results'] if r['case'] != 'errors/beta.barista']
        with self.assertRaises(ValueError) as caught:
            self.derive(report)
        self.assertEqual(str(caught.exception), 'execution report population differs from the included '
                                                "case set: ['errors/beta.barista']")

    def test_duplicate_result_records_are_refused(self):
        report = self.report({'errors/alpha.barista'})
        report['results'].append(dict(report['results'][0]))
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.derive(report)



class ArchivedExecutionDerivation(unittest.TestCase):
    """Derivation against a real completed run, supplied by --execution-report.

    Without the argument there is nothing to derive from and the class does not run. With it,
    a missing report or staging tree is a failure rather than a skip, so a caller that asked
    for this evidence is never handed a silent pass instead.
    """

    def test_a_real_execution_report_derives_its_own_failing_set(self):
        import import_analyzer_corpus
        self.assertTrue(EXECUTION_REPORT.is_file(), f'execution report not found: {EXECUTION_REPORT}')
        staging = ROOT / 'project/tests/corpus/analyzer/inventory.json'
        self.assertTrue(staging.is_file(), f'analyzer staging inventory not found: {staging}; stage the corpus first')
        report = json.loads(EXECUTION_REPORT.read_text())
        inventory = json.loads(staging.read_text())
        included = {record['imported_path'] for record in inventory['sources']
                    if record['role'] == 'case' and record['disposition'] not in ('excluded', 'deferred')}
        policy = import_analyzer_corpus.default_policy()
        triage = inventory['ledger']['triage']
        derived = import_analyzer_corpus.derive_expected_failures(policy, report, included, triage)
        self.assertEqual(derived, sorted(derived))
        self.assertEqual(derived, import_analyzer_corpus.derive_expected_failures(policy, report, included, triage))
        self.assertEqual(set(derived), {r['case'] for r in report['results'] if not r['passed']})
        # Cross-check the size against a field the derivation never reads, so a report whose
        # results and summary disagree cannot quietly define its own expectation.
        self.assertEqual(len(derived), sum(count for terminal, count in report['summary'].items()
                                           if terminal != 'passed'))
        self.assertTrue(derived, 'a fully passing report proves nothing about the ownership contract')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--foundry', type=Path)
    parser.add_argument('--execution-report', type=Path,
                        help='completed corpus triage report to derive a residual-failure pin from')
    args, remaining = parser.parse_known_args()
    FOUNDRY = args.foundry
    EXECUTION_REPORT = args.execution_report
    ArchivedExecutionDerivation.__unittest_skip__ = EXECUTION_REPORT is None
    unittest.main(argv=[sys.argv[0], *remaining])
