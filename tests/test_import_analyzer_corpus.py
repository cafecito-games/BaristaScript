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
        self.m.write_stage(inv, self.source, destination)
        self.m.check_stage(inv, self.source, destination)
        self.assertEqual((destination / original['imported_path']).read_bytes(),
                         self.m.patch(data, standard, original['identity']))
        self.assertEqual((destination / clone['imported_path']).read_bytes(), clone_bytes)
        first = {path.relative_to(destination): path.read_bytes()
                 for path in destination.rglob('*') if path.is_file()}
        self.m.write_stage(inv, self.source, destination)
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

D1_SELECTED_CASES = ('errors/assign_narrowed_match_bind.barista',
 'errors/named_call_argument_rest_parameter.barista',
 'errors/type_alias_not_an_expression.barista',
 'errors/type_union_as_type_handle.barista',
 'errors/type_union_export_rejected.barista',
 'errors/type_union_in_typed_container.barista',
 'errors/type_union_runtime_type_operations.barista',
 'errors/typed_rest_parameter_external_argument.barista',
 'errors/typed_rest_parameter_override_narrowing.barista',
 'features/tuple_destructure_typing.norun.barista',
 'features/type_alias_union_export_single_member_retains_type.barista',
 'features/type_self_callable_matching_width_parameter.barista',
 'features/typed_rest_parameter_concrete.barista',
 'features/typed_rest_parameter_external.barista',
 'features/typed_rest_parameter_override_variance.barista')
D1_REWRITTEN_CASES = frozenset({'errors/assign_narrowed_match_bind.barista',
           'errors/named_call_argument_rest_parameter.barista',
           'errors/type_alias_not_an_expression.barista',
           'errors/typed_rest_parameter_external_argument.barista',
           'errors/typed_rest_parameter_override_narrowing.barista',
           'features/tuple_destructure_typing.norun.barista',
           'features/type_self_callable_matching_width_parameter.barista',
           'features/typed_rest_parameter_concrete.barista',
           'features/typed_rest_parameter_external.barista',
           'features/typed_rest_parameter_override_variance.barista'})
D1_EXPECTATION_OVERRIDE_CASES = frozenset({'errors/type_union_as_type_handle.barista',
           'errors/type_union_export_rejected.barista',
           'errors/type_union_in_typed_container.barista',
           'errors/type_union_runtime_type_operations.barista',
           'features/type_alias_union_export_single_member_retains_type.barista'})
D1_SOURCE_EDIT_PATHS = frozenset({'analyzer/errors/assign_narrowed_match_bind.fs',
           'analyzer/errors/named_call_argument_rest_parameter.fs',
           'analyzer/errors/type_alias_not_an_expression.fs',
           'analyzer/errors/type_union_as_type_handle.fs',
           'analyzer/errors/type_union_export_rejected.fs',
           'analyzer/errors/type_union_in_typed_container.fs',
           'analyzer/errors/type_union_runtime_type_operations.fs',
           'analyzer/errors/typed_rest_parameter_override_narrowing.fs',
           'analyzer/features/tuple_destructure_typing.norun.fs',
           'analyzer/features/type_alias_union_export_single_member_retains_type.fs',
           'analyzer/features/type_self_callable_matching_width_parameter.fs',
           'analyzer/features/typed_rest_parameter_concrete.fs',
           'analyzer/features/typed_rest_parameter_external.fs',
           'analyzer/features/typed_rest_parameter_external_provider.notest.fs',
           'analyzer/features/typed_rest_parameter_override_variance.fs'})
D1_EXPECTATION_EDIT_PATHS = frozenset({'analyzer/errors/type_union_as_type_handle.out',
           'analyzer/errors/type_union_export_rejected.out',
           'analyzer/errors/type_union_in_typed_container.out',
           'analyzer/errors/type_union_runtime_type_operations.out',
           'analyzer/features/type_alias_union_export_single_member_retains_type.out'})


class FullPinned(unittest.TestCase):
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

        source_specs = {'analyzer/errors/assign_narrowed_match_bind.fs': ('c5fdeb008ada9bbb8459a9e4c338cd8a7d7159b58f1588a9a9691e5e669e0a2f',
                                                   '9f1146b824a9e443ac350e24e3db438bd50295a03772057534d0d2ab28b14217',
                                                   'd9e6b3c1bbcedcfc8af1c10ed5d2488aced86ce8d76cad0fc675ee80c4806312'),
 'analyzer/errors/named_call_argument_rest_parameter.fs': ('736e4ebec421306204015f21491e83a195576023cf4aa561433b0c5e8ff40a55',
                                                           '596b25172a04c718338bce6c21ed0380a66ed441c32c2511c503a9a089cd8058',
                                                           'c10cb978dd121308a0467c039108d40b07e484c450b2a1f606653eb578cc8f1f'),
 'analyzer/errors/type_alias_not_an_expression.fs': ('1afdc244ccb192446c046f0d7f9dea7afff19361839d58594527bf0056f5a9b4',
                                                     '38562ffee2911c2c00e250c186233af400d004799d649360ffc31ef60e8546d4',
                                                     'd9d1d3c440d66036d78afc26651171e8369ea0b3582125bf01f32493b194d3b3'),
 'analyzer/errors/type_union_as_type_handle.fs': ('d58551c3def3d0925758cd725de200b1344c1f60bb4a181310249594c4871a3b',
                                                  '16fa805325a0de8664525bea65224d8ef73213d3e46fc0e06fbd5e27e94f906a',
                                                  '1182da14b603ba118c1ba937a4a28db2f3f93708b12caaa6f822ce1fcd7d1ac0'),
 'analyzer/errors/type_union_export_rejected.fs': ('06164c91a858163ad02ab2915d2c452b1ff442f011b8f9989f0add5b0fe1dab7',
                                                   '6573bb24530dd562583fb8a8e111f8dd2c8e9e40c06c9b231eb7b1576243e7dd',
                                                   'bd04e439d2c54a700d9d6008ae87d257e387bde3b30a877cd3ed7c361d16a658'),
 'analyzer/errors/type_union_in_typed_container.fs': ('8c49b6868e5679acaf9f208ee4f923365783dbbaefe83cc0b7b71bd52c2c883c',
                                                      '5e3ec8468cc9a43cac2e8611334e00b84fe3b9c93e8cc5e90db208321b694b98',
                                                      'c4aa3e17d857c4e740f1abb0490d3de89068d9b2a03f3c09af14af809d69a6ec'),
 'analyzer/errors/type_union_runtime_type_operations.fs': ('77c308045f0fd5beedfbb1f0793bb1965ce8c525762cdecc63788fd98da2c171',
                                                           'a71d2a46f32d8d4bf05d4618360a06484c7c1444e4443541a04503f925d8dfa5',
                                                           'aa7b28dd5c4fd3f1dc20d104ef95de434c2a9f91b424a7674598558b83889c24'),
 'analyzer/errors/typed_rest_parameter_override_narrowing.fs': ('029bdd50f72763dfa0ca74a044006873ef91e9f53dcc77dddb8d3d93c5d9fec2',
                                                                '83825f64f880cf7077b9a104b186bda40a8c1a042f6132dc76185397a1e48e83',
                                                                '232c4db678fc16e2a73503332bdc49fd48e958a55fedab240517b9566906e313'),
 'analyzer/features/tuple_destructure_typing.norun.fs': ('6f19dbd75d376c02ad54d5bbdcf6e68b72269a276c5ae79fc5f81cae94f64ce2',
                                                         'dc01d4d4e2d4bf93880f7a9569aee8e5c24783b12ba87848204c0c7154cf13c0',
                                                         '6313ca04cca9b1436eb0d7cf396ef56b0813ea418b15e454f6f70206f113acf8'),
 'analyzer/features/type_alias_union_export_single_member_retains_type.fs': ('c5b8a244789d0c4b8bd3d5e277ed4059705776023bcc142190444912b28c6c72',
                                                                             '4e6abeee1b4c5cc8963d29202e1ff4e539f8ab5ebb03844312e04d3feb044a3d',
                                                                             '7099936c1baaf791b674fddf9f580aebe085feddbad3d28f8c5bb72a250fb1d1'),
 'analyzer/features/type_self_callable_matching_width_parameter.fs': ('23b5ed3867a8726940a4023ed6dcf02d0fc7674f7cf74e184a6678d510bbb296',
                                                                      '25dfacf995b96562d3d6d238e2c39e6b31d8948b48a840c3a9d96f565823c28d',
                                                                      '2a7f3179b261922677761e23df076e70cfff21e097945e2ed55e43f8a19a175e'),
 'analyzer/features/typed_rest_parameter_concrete.fs': ('47ca2cba4c9e35eb0a6d71f228778951d1882256c33009787945626ce166f52a',
                                                        '7556627e8ff2347209c6b6c736dd4a15ef3e296bb7156d43258091daea73c48b',
                                                        '23af8478a42c015abff6436e663addef60bffc4a31f9d4211c07047a933ee330'),
 'analyzer/features/typed_rest_parameter_external.fs': ('7edaa5f5e3769473db32fb6139e099635f6f9c3ee874247dc4e62a03f6a91106',
                                                        'e2d479b2388ea9669edd5c79094401965eee9fb9476dee25a3b61690aa29f559',
                                                        'cc6c154f02163c6f9c180873e5f223894cda1fa2bff06ac959992c048b6e8d28'),
 'analyzer/features/typed_rest_parameter_override_variance.fs': ('5f36c8bddb2b14e18019ad8b1586a777e217c534ef07f4f48460564c5d9c7268',
                                                                 'fb55c56da2ab77c5cbd2ff5a3fc45ddc028fce26d5755fcedd069e373afeba03',
                                                                 'f4199aa95b0914e3ad32fc963704c52349d4a8346e0c655f671c318b3325a815')}
        expectation_specs = {'analyzer/errors/type_union_as_type_handle.out': ('28fe10479a8e18b14b0a0eee9c74af52ce9f129e9caf939dc46194dcded27080',
                                                   '1dd350196faf904f14eb5d052351f23d21743d3f5bffde627a476752234cf575',
                                                   '43fdfe70f9fc9eafa1bfea115392ee9fd6f263b2866d5e0b727d586fc27bd5a0'),
 'analyzer/errors/type_union_export_rejected.out': ('f2f6a75e327cc59d82604b9776699b5265029bc32137f97610d78128b781694c',
                                                    'b568e51aa92cd7d2ff8f3c1ddb4369c83b37480fc312028b99422fdd820a6d68',
                                                    '5652db3b2eee843398255ac5e098658d6070ca7d87d6d8112524a66c09371f77'),
 'analyzer/errors/type_union_in_typed_container.out': ('897a4b4fda981c7902c275757b7299f560ef112859e67d36620d79461c75ad29',
                                                       '5b5f73698e432e3f7b13fcac8b083a64fe2dad4ff0bab52b86bc506d8d71b42c',
                                                       'a567e5ae6cc82010bcd2ea80c37ce264f1137272ce025657ba6930d4f7f87aad'),
 'analyzer/errors/type_union_runtime_type_operations.out': ('3986eb71e035597714a72c77993e4fdf2ec8a56b52a18878587a341ade4da4b0',
                                                            '85c1d496b0e2279865582aa5aaf4ab0759cebaf2177c9526f529c3b7b9c07667',
                                                            '2fd2388d1388313c9d80256126168f242a1393d8c8fcaad194c788ec51f9c423'),
 'analyzer/features/type_alias_union_export_single_member_retains_type.out': ('6d6689ed1a411f79e719d0a29a9cf8d76c9753a4260836c7ffb79e9535aa129b',
                                                                              '09e65a45537df2baf76eb986b8fe0cf945fe3efd3db9a0a756f48b55b5a67251',
                                                                              '3e259e6c42f9d7ff916d732f5d10e892e3bd06b1780ad9724d1d546d279ffcd6')}
        case_specs = {'errors/assign_narrowed_match_bind.barista': ('rewritten',
                                               'analyzer/errors/assign_narrowed_match_bind.fs',
                                               '9f1146b824a9e443ac350e24e3db438bd50295a03772057534d0d2ab28b14217',
                                               'd9e6b3c1bbcedcfc8af1c10ed5d2488aced86ce8d76cad0fc675ee80c4806312',
                                               'analyzer/errors/assign_narrowed_match_bind.out',
                                               '80539efbf1a78e1775257824bb370a9b50c7fc72536d4e5adc5088ae95b974df',
                                               '35455f5dfaee07ee3673bedc2e3f6fb4ab44d8204b0db6407e9b748f4f484a68',
                                               2,
                                               0),
 'errors/named_call_argument_rest_parameter.barista': ('rewritten',
                                                       'analyzer/errors/named_call_argument_rest_parameter.fs',
                                                       '596b25172a04c718338bce6c21ed0380a66ed441c32c2511c503a9a089cd8058',
                                                       'c10cb978dd121308a0467c039108d40b07e484c450b2a1f606653eb578cc8f1f',
                                                       'analyzer/errors/named_call_argument_rest_parameter.out',
                                                       'a5fa42855fe98467627246ff3d39abf6b6d5574171b562ef5d60c3b181d7e13e',
                                                       '04babc5c5964886fb7fa352c400c5544177e335508ef8d2bd71bce1c16b8609a',
                                                       1,
                                                       0),
 'errors/type_alias_not_an_expression.barista': ('rewritten',
                                                 'analyzer/errors/type_alias_not_an_expression.fs',
                                                 '38562ffee2911c2c00e250c186233af400d004799d649360ffc31ef60e8546d4',
                                                 'd9d1d3c440d66036d78afc26651171e8369ea0b3582125bf01f32493b194d3b3',
                                                 'analyzer/errors/type_alias_not_an_expression.out',
                                                 '35d29f71a91e593e24891282d1db66bf88efcb042a2739551b7364dc607207d9',
                                                 'ac0b01bfe2bcab6ffff8dd297ef7347229250a5a2acbc27d9bacf8ff01db4c01',
                                                 1,
                                                 0),
 'errors/type_union_as_type_handle.barista': ('expectation_overrides',
                                              'analyzer/errors/type_union_as_type_handle.fs',
                                              '16fa805325a0de8664525bea65224d8ef73213d3e46fc0e06fbd5e27e94f906a',
                                              '1182da14b603ba118c1ba937a4a28db2f3f93708b12caaa6f822ce1fcd7d1ac0',
                                              'analyzer/errors/type_union_as_type_handle.out',
                                              '43fdfe70f9fc9eafa1bfea115392ee9fd6f263b2866d5e0b727d586fc27bd5a0',
                                              '142271725698a78b4d9e1c8b3b130068ca443395e1a567289ed286cd2ac312d7',
                                              1,
                                              1),
 'errors/type_union_export_rejected.barista': ('expectation_overrides',
                                               'analyzer/errors/type_union_export_rejected.fs',
                                               '6573bb24530dd562583fb8a8e111f8dd2c8e9e40c06c9b231eb7b1576243e7dd',
                                               'bd04e439d2c54a700d9d6008ae87d257e387bde3b30a877cd3ed7c361d16a658',
                                               'analyzer/errors/type_union_export_rejected.out',
                                               '5652db3b2eee843398255ac5e098658d6070ca7d87d6d8112524a66c09371f77',
                                               '6b996254183dd3e121d396f5137beebc6ab39335069b8574140ae5d36d2a0f44',
                                               1,
                                               1),
 'errors/type_union_in_typed_container.barista': ('expectation_overrides',
                                                  'analyzer/errors/type_union_in_typed_container.fs',
                                                  '5e3ec8468cc9a43cac2e8611334e00b84fe3b9c93e8cc5e90db208321b694b98',
                                                  'c4aa3e17d857c4e740f1abb0490d3de89068d9b2a03f3c09af14af809d69a6ec',
                                                  'analyzer/errors/type_union_in_typed_container.out',
                                                  'a567e5ae6cc82010bcd2ea80c37ce264f1137272ce025657ba6930d4f7f87aad',
                                                  'cefbe3bb0f52fff8531f9cbed50a1403a070b5c5f8dd5b2b96a652a0a10fbde4',
                                                  2,
                                                  4),
 'errors/type_union_runtime_type_operations.barista': ('expectation_overrides',
                                                       'analyzer/errors/type_union_runtime_type_operations.fs',
                                                       'a71d2a46f32d8d4bf05d4618360a06484c7c1444e4443541a04503f925d8dfa5',
                                                       'aa7b28dd5c4fd3f1dc20d104ef95de434c2a9f91b424a7674598558b83889c24',
                                                       'analyzer/errors/type_union_runtime_type_operations.out',
                                                       '2fd2388d1388313c9d80256126168f242a1393d8c8fcaad194c788ec51f9c423',
                                                       '037b534e3f4aba7a0f349bea81f8621c0932253c8f15f4009b4e67aaab7bf92a',
                                                       1,
                                                       2),
 'errors/typed_rest_parameter_external_argument.barista': ('rewritten',
                                                           'analyzer/errors/typed_rest_parameter_external_argument.fs',
                                                           'f8f13b3c8720621801be93094310a99c809ab56a9f9d0a7c7e8d337a2c4ade2f',
                                                           '3c46acd0f9dfe8469561c9ac667c853ca39b60a95ddfd6f1a1189405c411916e',
                                                           'analyzer/errors/typed_rest_parameter_external_argument.out',
                                                           '26e92a367b345a049d69cc07ad2e3867153ddc9c506a098830ee311bded51b28',
                                                           '24bb0076caa70ec3cc14f4500147d5a3f72e20af5047238e41cbd86e24fee2d2',
                                                           1,
                                                           0),
 'errors/typed_rest_parameter_override_narrowing.barista': ('rewritten',
                                                            'analyzer/errors/typed_rest_parameter_override_narrowing.fs',
                                                            '83825f64f880cf7077b9a104b186bda40a8c1a042f6132dc76185397a1e48e83',
                                                            '232c4db678fc16e2a73503332bdc49fd48e958a55fedab240517b9566906e313',
                                                            'analyzer/errors/typed_rest_parameter_override_narrowing.out',
                                                            'de958416daad622de169bd4c705336df602c595aa41c38caa9eac00aad6fc147',
                                                            '09b752c033a8d5b5a66d9c4c63317cb24f2fa5b733e9635b4ad1e9c7b9a577e7',
                                                            4,
                                                            0),
 'features/tuple_destructure_typing.norun.barista': ('rewritten',
                                                     'analyzer/features/tuple_destructure_typing.norun.fs',
                                                     'dc01d4d4e2d4bf93880f7a9569aee8e5c24783b12ba87848204c0c7154cf13c0',
                                                     '6313ca04cca9b1436eb0d7cf396ef56b0813ea418b15e454f6f70206f113acf8',
                                                     'analyzer/features/tuple_destructure_typing.norun.out',
                                                     'a79326b2c94dcb1410e12f6b774c756388a4eedb3f9066b078a3b45c5574cb1d',
                                                     '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
                                                     1,
                                                     0),
 'features/type_alias_union_export_single_member_retains_type.barista': ('expectation_overrides',
                                                                         'analyzer/features/type_alias_union_export_single_member_retains_type.fs',
                                                                         '4e6abeee1b4c5cc8963d29202e1ff4e539f8ab5ebb03844312e04d3feb044a3d',
                                                                         '7099936c1baaf791b674fddf9f580aebe085feddbad3d28f8c5bb72a250fb1d1',
                                                                         'analyzer/features/type_alias_union_export_single_member_retains_type.out',
                                                                         '3e259e6c42f9d7ff916d732f5d10e892e3bd06b1780ad9724d1d546d279ffcd6',
                                                                         '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
                                                                         3,
                                                                         1),
 'features/type_self_callable_matching_width_parameter.barista': ('rewritten',
                                                                  'analyzer/features/type_self_callable_matching_width_parameter.fs',
                                                                  '25dfacf995b96562d3d6d238e2c39e6b31d8948b48a840c3a9d96f565823c28d',
                                                                  '2a7f3179b261922677761e23df076e70cfff21e097945e2ed55e43f8a19a175e',
                                                                  'analyzer/features/type_self_callable_matching_width_parameter.out',
                                                                  '2027f455f04398fbb0f446cb1b655f2cf6f4086fbfcae752254480fb76bfe560',
                                                                  '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
                                                                  3,
                                                                  0),
 'features/typed_rest_parameter_concrete.barista': ('rewritten',
                                                    'analyzer/features/typed_rest_parameter_concrete.fs',
                                                    '7556627e8ff2347209c6b6c736dd4a15ef3e296bb7156d43258091daea73c48b',
                                                    '23af8478a42c015abff6436e663addef60bffc4a31f9d4211c07047a933ee330',
                                                    'analyzer/features/typed_rest_parameter_concrete.out',
                                                    'f27a1eced2676eb49ec04e2c4f21a43b39a68a632687185ffa136b1d1c737b5c',
                                                    '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
                                                    6,
                                                    0),
 'features/typed_rest_parameter_external.barista': ('rewritten',
                                                    'analyzer/features/typed_rest_parameter_external.fs',
                                                    'e2d479b2388ea9669edd5c79094401965eee9fb9476dee25a3b61690aa29f559',
                                                    'd30a67ade150c58950e7ae8eba258e680cd922fa994f73309afe6510446b4683',
                                                    'analyzer/features/typed_rest_parameter_external.out',
                                                    'e8bd154eafdb88f63d8dede48d0c8e3377dbb84598de0e4315e25f7b0fb84ef5',
                                                    '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
                                                    2,
                                                    0),
 'features/typed_rest_parameter_override_variance.barista': ('rewritten',
                                                             'analyzer/features/typed_rest_parameter_override_variance.fs',
                                                             'fb55c56da2ab77c5cbd2ff5a3fc45ddc028fce26d5755fcedd069e373afeba03',
                                                             'f4199aa95b0914e3ad32fc963704c52349d4a8346e0c655f671c318b3325a815',
                                                             'analyzer/features/typed_rest_parameter_override_variance.out',
                                                             '062c22dc15e1e66f38b91ca393653042427d804d39cd29b8e78ed9a5d233a5da',
                                                             '369e7ec368d13a45f0fb1b13f89d6bb20829f1cf8901065dfba26bfc99b89735',
                                                             6,
                                                             0)}
        provider_path = 'analyzer/features/typed_rest_parameter_external_provider.notest.fs'
        provider_spec = ('938b3f53dd393bb68583fb6d8ff97a9a72402f3cf94dce16bb2d5f2b31b39c1f',
                         '63ec633ac970fa3690990531f325038e9dfaf73ba147c5b61efb8aa59060aebe',
                         '34c1547378a704872f079c57e06f953ceff0ab75d35bb265fa3f2a449ffadc62')
        policy = importer.default_policy()
        source = FOUNDRY / importer.SCRIPTS
        uri = 'res://tests/corpus_staging/analyzer'

        self.assertEqual(policy['schema_version'], 2)
        self.assertEqual(hashlib.sha256(importer.POLICY.read_bytes()).hexdigest(),
                         'a772d9bba4b98cae462a249bf20a44fdfa61ce66695edb12a808b3e1bf5c5674')
        self.assertEqual((len(D1_SELECTED_CASES), case_list_sha(D1_SELECTED_CASES)),
                         (15, 'a7e7e701f3a9bc5cedc396ea8b949e97635c09cd49f7f13bf588ec9529bc330c'))
        self.assertEqual(set(case_specs), set(D1_SELECTED_CASES))
        self.assertEqual(set(source_specs) | {provider_path}, D1_SOURCE_EDIT_PATHS)
        self.assertEqual(len(source_specs), 14)
        self.assertNotIn(provider_path, source_specs)
        self.assertEqual(set(expectation_specs), D1_EXPECTATION_EDIT_PATHS)
        self.assertEqual({case for case in D1_SELECTED_CASES if case in policy['rewritten']},
                         D1_REWRITTEN_CASES)
        self.assertEqual({case for case in D1_SELECTED_CASES
                          if case in policy['expectation_overrides']},
                         D1_EXPECTATION_OVERRIDE_CASES)
        self.assertEqual((len(D1_REWRITTEN_CASES), len(D1_EXPECTATION_OVERRIDE_CASES)), (10, 5))
        self.assertEqual(sum(len(policy['source_edits'][path]['patches']) for path in source_specs), 33)
        self.assertEqual(len(policy['source_edits'][provider_path]['patches']), 2)
        self.assertEqual(sum(len(policy['expectation_edits'][path]['patches'])
                             for path in expectation_specs), 9)

        for path, (entry_sha, raw_sha, transformed_sha) in source_specs.items():
            with self.subTest(source=path):
                entry = policy['source_edits'][path]
                self.assertEqual(canonical_sha(entry), entry_sha)
                data = (source / path).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), raw_sha)
                changes = importer.source_policy_changes(data, path, policy)
                self.assertEqual(hashlib.sha256(importer.patch(data, changes, path)).hexdigest(),
                                 transformed_sha)
        for path, (entry_sha, raw_sha, projected_sha) in expectation_specs.items():
            with self.subTest(expectation=path):
                entry = policy['expectation_edits'][path]
                self.assertEqual(canonical_sha(entry), entry_sha)
                data = (source / path).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), raw_sha)
                self.assertEqual(hashlib.sha256(importer.patch(data, entry['patches'], path)).hexdigest(),
                                 projected_sha)

        first = importer.inventory_sources(source, policy, uri)
        self.assertEqual(first['schema_version'], 1)
        self.assertEqual(hashlib.sha256(importer.encoded(first)).hexdigest(),
                         'c008aed9e496a7d1029d1ad132e4d7af19be32dc784e979a9a0a5c2a15f0efa3')
        self.assertEqual((first['counts']['sources'], first['counts']['cases'],
                          first['counts']['helpers'], first['counts']['support_helpers']),
                         (1596, 1346, 250, 2))
        self.assertEqual((first['ledger']['total'], first['ledger']['skipped']), (1078, 252))
        records = {record['imported_path']: record for record in first['sources']
                   if record.get('role') == 'case'}
        for case, expected in case_specs.items():
            with self.subTest(case=case):
                (disposition, upstream_path, raw_sha, imported_sha, output_path,
                 expectation_sha, expected_block_sha, transformations, expectation_edits) = expected
                record = records[case]
                self.assertEqual(record['disposition'], disposition)
                self.assertEqual(record['upstream_path'], upstream_path)
                self.assertEqual(record['sha256'], raw_sha)
                self.assertEqual(record['imported_sha256'], imported_sha)
                self.assertEqual(record['expectation_identity'], importer.SCRIPTS + '/' + output_path)
                raw_expectation_sha = (expectation_specs[output_path][1]
                                       if output_path in expectation_specs else expectation_sha)
                self.assertEqual(record['expectation_sha256'], raw_expectation_sha)
                self.assertEqual(hashlib.sha256(record['expected_block'].encode()).hexdigest(),
                                 expected_block_sha)
                self.assertEqual(len(record['transformations']), transformations)
                self.assertEqual(len(record['expectation_edits']), expectation_edits)

        provider = next(record for record in first['sources']
                        if record['upstream_path'] == provider_path)
        provider_entry_sha, provider_raw_sha, provider_transformed_sha = provider_spec
        self.assertEqual(canonical_sha(policy['source_edits'][provider_path]), provider_entry_sha)
        self.assertEqual(provider['role'], 'helper')
        self.assertEqual(provider['sha256'], provider_raw_sha)
        self.assertEqual(provider['imported_sha256'], provider_transformed_sha)
        self.assertNotIn('#projected-helper-clone:', provider['identity'])
        self.assertNotIn('projected_helper_clone', policy['source_edits'][provider_path])
        self.assertEqual(sum(record['imported_path'] == provider['imported_path']
                             for record in first['sources']), 1)
        consumers = {case: records[case]['references'] for case in (
            'errors/typed_rest_parameter_external_argument.barista',
            'features/typed_rest_parameter_external.barista',
        )}
        self.assertEqual(consumers, {
            'errors/typed_rest_parameter_external_argument.barista': [{
                'intentional_missing': False,
                'kind': 'preload',
                'literal': '../features/typed_rest_parameter_external_provider.notest.fs',
                'relocated': '../features/typed_rest_parameter_external_provider.notest.barista',
                'target': provider_path,
            }],
            'features/typed_rest_parameter_external.barista': [{
                'intentional_missing': False,
                'kind': 'preload',
                'literal': 'typed_rest_parameter_external_provider.notest.fs',
                'relocated': 'typed_rest_parameter_external_provider.notest.barista',
                'target': provider_path,
            }],
        })

        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary) / 'stage'
            importer.write_stage(first, source, stage)
            importer.check_stage(first, source, stage)
            before = {path.relative_to(stage): path.read_bytes()
                      for path in stage.rglob('*') if path.is_file()}
            importer.write_stage(first, source, stage)
            self.assertEqual(before, {path.relative_to(stage): path.read_bytes()
                                      for path in stage.rglob('*') if path.is_file()})
            self.assertEqual(len(before), 2411)
            self.assertEqual(hashlib.sha256((stage / 'inventory.json').read_bytes()).hexdigest(),
                             'c008aed9e496a7d1029d1ad132e4d7af19be32dc784e979a9a0a5c2a15f0efa3')
            self.assertEqual(hashlib.sha256((stage / 'case_stages.json').read_bytes()).hexdigest(),
                             '3ad070abe3df618bcb3e01bc6c5cdc753884b9e30819c606eae5a2e515ff9202')
            tree = hashlib.sha256()
            for path in sorted(candidate for candidate in stage.rglob('*') if candidate.is_file()):
                tree.update(hashlib.sha256(path.read_bytes()).hexdigest().encode())
                tree.update(b'  ./')
                tree.update(path.relative_to(stage).as_posix().encode())
                tree.update(b'\n')
            self.assertEqual(tree.hexdigest(),
                             'b552302628d22b4e45c73445ae45db64f1d6304ab1b984bb64a2bd48607bb674')

        def expect_inventory_failure(mutator, pattern):
            candidate = copy.deepcopy(policy)
            mutator(candidate)
            with self.assertRaisesRegex(ValueError, pattern):
                importer.inventory_sources(source, candidate, uri)

        source_case = 'errors/assign_narrowed_match_bind.barista'
        source_path = 'analyzer/errors/assign_narrowed_match_bind.fs'
        expectation_case = 'errors/type_union_as_type_handle.barista'
        expectation_path = 'analyzer/errors/type_union_as_type_handle.out'
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
                copy.deepcopy(candidate['source_edits'][source_path]['patches'][0])),
            'overlapping')
        expect_inventory_failure(
            lambda candidate: candidate['rewritten'].pop(source_case),
            'disposition')
        expect_inventory_failure(
            lambda candidate: candidate['expectation_edits'][expectation_path].__setitem__(
                'sha256', '0' * 64), 'expectation edit hash preimage mismatch')
        expect_inventory_failure(
            lambda candidate: candidate['expectation_overrides'].pop(expectation_case),
            'override disposition')
        expect_inventory_failure(
            lambda candidate: candidate['source_edits'][provider_path].__setitem__('sha256', '0' * 64),
            'source edit hash preimage mismatch')

        missing = copy.deepcopy(policy['source_edits'][source_path])
        missing['patches'].pop()
        with self.assertRaises(AssertionError):
            self.assertEqual(canonical_sha(missing), source_specs[source_path][0])

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
                         set(reviewed_cases) | D1_REWRITTEN_CASES | {
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
                         set(projected_sources) | D1_SOURCE_EDIT_PATHS | {
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
