#!/usr/bin/env python3
# test_import_runtime_corpus.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Pinned miniature runtime transport, transcript projection and fail-closed staging.

The pinned Foundry checkout is required, not optional: a run without it would report a
pass for work it never did, which is the exact shape of the M3 handoff lesson this
corpus exists to avoid. ``--foundry`` is therefore a required argument and every class
below runs.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))

FOUNDRY = None
REVISION = 'c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6'
FIXTURE = ROOT / 'tests/fixtures/runtime_import'
URI = 'res://tests/corpus/runtime'

# What the miniature carries, so a fixture edit that quietly drops a case is a failure
# rather than a smaller run.
FIXTURE_CENSUS = {'sources': 11, 'helpers': 4, 'cases': 7, 'outputs': 8}
FIXTURE_CASES = ('errors/checked_integer_shift_count.barista', 'errors/division_by_zero.barista',
                 'features/final_class_extend_rejected.barista',
                 'features/generic_tagged_union_global.barista',
                 'features/match_plain_enum_open_domain.barista',
                 'features/retroactive_conformance_loaded_declaring_file.barista',
                 'features/type_self_static_var_self_container.barista')


class RuntimeImport(unittest.TestCase):
    """The importer against a byte-pinned miniature of its real input."""

    def setUp(self):
        import import_runtime_corpus
        self.m = import_runtime_corpus
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'input'
        shutil.copytree(FIXTURE / 'scripts', self.source)
        self.policy = self.fixture_policy()

    def fixture_policy(self):
        policy = self.m.default_policy()
        policy['counts'] = None
        cases = set(FIXTURE_CASES)
        for field in ('excluded', 'deferred', 'rewritten', 'expectation_overrides', 'owners'):
            policy[field] = {path: value for path, value in policy[field].items() if path in cases}
        policy['expectation_edits'] = {
            path: value for path, value in policy['expectation_edits'].items()
            if (self.source / path).is_file()}
        policy['source_edits'] = {
            path: value for path, value in policy['source_edits'].items() if (self.source / path).is_file()}
        return policy

    def inventory(self, policy=None, census=None):
        return self.m.inventory_sources(self.source, policy or self.policy, URI,
                                        project_root=ROOT / 'project',
                                        census=census or FIXTURE_CENSUS)

    def record(self, inventory, upstream):
        return next(item for item in inventory['sources'] if item['upstream_path'] == upstream)

    def expect_failure(self, pattern, mutate):
        policy = copy.deepcopy(self.policy)
        mutate(policy)
        with self.assertRaisesRegex(ValueError, pattern):
            self.inventory(policy)

    def test_the_fixture_is_the_pinned_upstream_bytes(self):
        provenance = json.loads((FIXTURE / 'provenance.json').read_text())
        self.assertEqual(provenance['revision'], REVISION)
        for path, digest in provenance['files'].items():
            self.assertEqual(hashlib.sha256((self.source / path).read_bytes()).hexdigest(), digest, path)
        self.assertEqual(sorted(provenance['files']),
                         sorted(str(path.relative_to(self.source)).replace(os.sep, '/')
                                for path in self.source.rglob('*') if path.is_file()))

    def test_census_counts_and_categories_are_enforced(self):
        inventory = self.inventory()
        counts = inventory['counts']
        self.assertEqual({key: counts[key] for key in FIXTURE_CENSUS}, FIXTURE_CENSUS)
        self.assertEqual(counts['statuses'],
                         {'FS_TEST_ANALYZER_ERROR': 1, 'FS_TEST_OK': 3, 'FS_TEST_RUNTIME_ERROR': 3})
        for key in FIXTURE_CENSUS:
            drifted = dict(FIXTURE_CENSUS, **{key: FIXTURE_CENSUS[key] + 1})
            with self.subTest(count=key), self.assertRaisesRegex(ValueError, 'census drift'):
                self.inventory(census=drifted)

    def test_helpers_are_imported_but_never_become_cases(self):
        inventory = self.inventory()
        helpers = [item for item in inventory['sources'] if item['role'] == 'helper']
        self.assertEqual(len(helpers), FIXTURE_CENSUS['helpers'])
        for helper in helpers:
            self.assertTrue(helper['imported_path'].endswith('.notest.barista'))
            self.assertNotIn('status', helper)
            self.assertNotIn(helper['imported_path'], inventory['ledger']['expected_failures'])
        stage = self.root / 'stage'
        self.m.generate(inventory, self.source, stage)
        stages = json.loads((stage / 'case_stages.json').read_text())['cases']
        self.assertTrue(all(not path.endswith('.notest.barista') for path in stages))
        self.assertEqual(set(stages.values()), {'runtime'})

    def test_a_helper_carrying_its_own_output_is_refused(self):
        (self.source / 'runtime/features/final_base.notest.out').write_text('FS_TEST_OK\n')
        with self.assertRaisesRegex(ValueError, 'helper has paired output'):
            self.inventory()

    def test_the_orphan_expectation_is_recorded_and_never_a_case(self):
        inventory = self.inventory()
        orphans = inventory['orphan_expectations']
        self.assertEqual(len(orphans), 1)
        orphan = orphans[0]
        self.assertTrue(orphan['expectation_identity'].endswith('runtime/errors/reload_suspended_function.out'))
        self.assertTrue(orphan['provider_identity'].endswith('reload_suspended_function.notest.fs'))
        self.assertIn('#248', orphan['reason'])
        self.assertEqual(orphan['status'], 'FS_TEST_RUNTIME_ERROR')
        self.assertIn('Canceling suspended execution', orphan['expected_block'])
        self.assertEqual(hashlib.sha256(
            (self.source / 'runtime/errors/reload_suspended_function.out').read_bytes()).hexdigest(),
            orphan['expectation_sha256'])
        self.assertNotIn('errors/reload_suspended_function.barista',
                         {item['imported_path'] for item in inventory['sources']})
        stage = self.root / 'stage'
        self.m.generate(inventory, self.source, stage)
        self.assertFalse((stage / 'errors/reload_suspended_function.out').exists())
        self.assertFalse((stage / 'errors/reload_suspended_function.barista').exists())
        self.assertIn('Canceling suspended execution', (stage / 'inventory.json').read_text())

    def test_an_undeclared_orphan_output_is_refused(self):
        (self.source / 'runtime/features/invented.out').write_text('FS_TEST_OK\n')
        with self.assertRaisesRegex(ValueError, 'orphan expectations differ'):
            self.inventory()
        self.expect_failure('exactly the orphan expectation',
                            lambda policy: policy['orphan_expectations'].clear())

    def test_every_status_projects_its_complete_transcript(self):
        inventory = self.inventory()
        for upstream, status in (('runtime/features/match_plain_enum_open_domain.fs', 'FS_TEST_OK'),
                                 ('runtime/features/final_class_extend_rejected.fs', 'FS_TEST_RUNTIME_ERROR'),
                                 ('runtime/features/type_self_static_var_self_container.fs',
                                  'FS_TEST_ANALYZER_ERROR')):
            with self.subTest(status=status):
                record = self.record(inventory, upstream)
                self.assertEqual(record['status'], status)
                self.assertEqual(record['stage'], 'runtime')
                original = (self.source / upstream).with_suffix('.out').read_text()
                self.assertEqual(record['expected_block'].split('\n')[0], status)
                self.assertEqual(len(record['expected_block'].split('\n')), len(original.split('\n')) - 1)
                self.assertNotIn('.fs', record['expected_block'])
        interleaved = self.record(inventory, 'runtime/features/final_class_extend_rejected.fs')
        lines = interleaved['expected_block'].split('\n')
        self.assertTrue(lines[1].startswith('>> SCRIPT ERROR at res://tests/corpus/runtime/features/'))
        self.assertTrue(lines[2].startswith('>> ERROR: Failed to load script "res://tests/corpus/runtime/'))
        self.assertEqual(lines[3], 'derived loaded: true')
        warned = self.record(inventory, 'runtime/features/match_plain_enum_open_domain.fs')
        body = warned['expected_block'].split('\n')
        self.assertTrue(body[1].startswith('~~ WARNING at line 27:'))
        self.assertIn('match_plain_enum_open_domain.barista.Level', body[1])
        self.assertEqual(body[3:], ['low undeclared', 'declared undeclared'])

    def test_an_expectation_the_producer_could_not_have_written_is_refused(self):
        output = self.source / 'runtime/errors/division_by_zero.out'
        for value, pattern in ((b'FS_TEST_PARSER_ERROR\nboom\n', 'unrecognized upstream runtime status'),
                               (b'FS_TEST_OK\n\n', 'exactly one final LF'),
                               (b'\xff\n', 'invalid UTF-8'),
                               (b'FS_TEST_OK\r\nx\n', 'without CR or NUL')):
            output.write_bytes(value)
            with self.subTest(expectation=value[:12]), self.assertRaisesRegex(ValueError, pattern):
                self.inventory()

    def test_a_case_without_its_expectation_is_refused(self):
        (self.source / 'runtime/errors/division_by_zero.out').unlink()
        with self.assertRaisesRegex(ValueError, 'missing paired output'):
            self.inventory()

    def test_relocation_keeps_its_preimages_and_rejects_an_unknown_identity(self):
        inventory = self.inventory()
        absolute = self.record(inventory, 'runtime/features/final_class_extend_rejected.fs')
        self.assertEqual(absolute['references'][0]['literal'], 'res://runtime/features/final_derived.notest.fs')
        self.assertEqual(absolute['references'][0]['relocated'],
                         'res://tests/corpus/runtime/features/final_derived.notest.barista')
        cross = self.record(inventory, 'runtime/features/retroactive_conformance_loaded_declaring_file.fs')
        self.assertEqual(cross['references'][0]['target'], 'analyzer/errors/rtc_scoped_conformance.notest.fs')
        self.assertEqual(cross['references'][0]['relocated'], '../../analyzer/errors/rtc_scoped_conformance.notest.barista')
        for rule in ('resource-literal', 'transcript-resource-identity'):
            with self.subTest(rule=rule):
                self.assertTrue(any(item['rule'] == rule for record in inventory['sources']
                                    for item in (record['transformations']
                                                 + record.get('expectation_transformations', []))))
        output = self.source / 'runtime/errors/checked_integer_shift_count.out'
        original = output.read_bytes()
        output.write_bytes(original.replace(b'runtime/errors/checked_integer_shift_count.fs',
                                            b'runtime/errors/never_imported.fs'))
        with self.assertRaisesRegex(ValueError, 'unknown transcript source identity'):
            self.inventory()
        output.write_bytes(original)
        rejected = self.source / 'runtime/features/final_class_extend_rejected.out'
        rejected.write_bytes(rejected.read_bytes().replace(b'res://runtime/features/final_derived.notest.fs',
                                                           b'res://runtime/features/never_imported.fs'))
        with self.assertRaisesRegex(ValueError, 'unknown transcript resource identity'):
            self.inventory()

    def test_an_unknown_transcript_filename_fragment_is_refused(self):
        output = self.source / 'runtime/features/match_plain_enum_open_domain.out'
        output.write_bytes(output.read_bytes().replace(b'match_plain_enum_open_domain.fs.Level',
                                                       b'never_imported.fs.Level'))
        with self.assertRaisesRegex(ValueError, 'unknown transcript filename'):
            self.inventory()

    def test_a_missing_external_delivery_is_refused(self):
        with tempfile.TemporaryDirectory() as empty:
            with self.assertRaisesRegex(ValueError, 'analyzer corpus delivery is missing'):
                self.m.inventory_sources(self.source, self.policy, URI, project_root=Path(empty),
                                         census=FIXTURE_CENSUS)

    def test_a_wording_override_needs_its_disposition_and_a_valid_preimage(self):
        inventory = self.inventory()
        carrier = self.record(inventory, 'runtime/errors/division_by_zero.fs')
        self.assertEqual(carrier['disposition'], 'expectation_overrides')
        self.assertIn('The "/" operator on "int" has a divisor of zero.', carrier['expected_block'])
        self.assertEqual(len(carrier['expectation_edits']), 1)
        self.expect_failure('requires its disjoint override disposition',
                            lambda policy: policy['expectation_overrides'].clear())
        self.expect_failure('expectation edit hash preimage mismatch',
                            lambda policy: policy['expectation_edits'][
                                'runtime/errors/division_by_zero.out'].__setitem__('sha256', '0' * 64))
        self.expect_failure('patch preimage mismatch',
                            lambda policy: policy['expectation_edits'][
                                'runtime/errors/division_by_zero.out']['patches'][0].__setitem__('before', 'wrong'))
        self.expect_failure('stale expectation edit policy',
                            lambda policy: policy['expectation_edits'].__setitem__(
                                'runtime/errors/absent.out', {'sha256': '0' * 64, 'patches': []}))

    def test_every_included_case_is_pinned_with_an_owner_and_a_reason(self):
        inventory = self.inventory()
        ledger = inventory['ledger']
        included = {record['imported_path'] for record in inventory['sources']
                    if record['role'] == 'case' and record['disposition'] not in ('excluded', 'deferred')}
        self.assertEqual(ledger['expected_failures'], sorted(included))
        self.assertEqual(ledger['total'], len(included))
        owners = self.policy['owners']
        for case in ledger['expected_failures']:
            owner = owners[case]
            self.assertEqual(owner['review_state'], 'pinned_awaiting_runtime')
            self.assertTrue(owner['reason'].startswith(f'#{owner["primary_issue"]}: no runtime yet — '))
            self.assertIn(owner['family'], owner['reason'])
        self.expect_failure('needs a reason naming its owning child',
                            lambda policy: policy['owners'][FIXTURE_CASES[1]].__setitem__('reason', 'because'))
        self.expect_failure('invalid runtime owner record',
                            lambda policy: policy['owners'][FIXTURE_CASES[1]].__setitem__('primary_issue', 1))
        self.expect_failure('every runnable case requires exactly one owner record',
                            lambda policy: policy['owners'].pop(FIXTURE_CASES[1]))

    def test_a_reviewed_disposition_must_agree_with_its_owner(self):
        inventory = self.inventory()
        triage = inventory['ledger']['triage']
        self.assertEqual(set(triage['excluded']), {'errors/checked_integer_shift_count.barista'})
        self.assertEqual(set(triage['deferred']), {'features/generic_tagged_union_global.barista'})
        self.assertEqual(inventory['ledger']['upstream_total'],
                         inventory['ledger']['total'] + len(triage['excluded']) + len(triage['deferred']))
        self.expect_failure('owner review state must match its disposition',
                            lambda policy: policy['excluded'].pop('errors/checked_integer_shift_count.barista'))
        self.expect_failure('reviewed disposition rationale mismatch',
                            lambda policy: policy['excluded'].__setitem__(
                                'errors/checked_integer_shift_count.barista', 'a different rationale'))
        self.expect_failure('overlapping policy',
                            lambda policy: policy['deferred'].__setitem__(
                                'errors/checked_integer_shift_count.barista', 'both at once'))
        self.expect_failure('stale policy case',
                            lambda policy: policy['excluded'].__setitem__('errors/absent.barista', 'stale'))

    def test_excluded_and_deferred_cases_leave_the_tree(self):
        inventory = self.inventory()
        stage = self.root / 'stage'
        self.m.generate(inventory, self.source, stage)
        emitted = {str(path.relative_to(stage)) for path in stage.rglob('*') if path.is_file()}
        self.assertNotIn('errors/checked_integer_shift_count.barista', emitted)
        self.assertNotIn('features/generic_tagged_union_global.barista', emitted)
        self.assertEqual(sum(name.endswith('.barista') for name in emitted),
                         inventory['ledger']['total'] + inventory['ledger']['skipped'])
        self.assertEqual(emitted - {'inventory.json', 'case_stages.json', 'README.md'},
                         {name for name in emitted if name.endswith(('.barista', '.out'))})
        self.assertFalse(any(name.endswith(('.fsignore', '.baristaignore')) for name in emitted))

    def test_a_regeneration_is_byte_identical_and_drift_is_detected(self):
        inventory = self.inventory()
        first, second = self.root / 'one', self.root / 'two'
        self.m.generate(inventory, self.source, first)
        self.m.generate(inventory, self.source, second)
        self.m.check_tree(inventory, self.source, first)
        for path in sorted(first.rglob('*')):
            if path.is_file():
                self.assertEqual(path.read_bytes(), (second / path.relative_to(first)).read_bytes())
        case = first / 'errors/division_by_zero.barista'
        case.write_bytes(case.read_bytes().replace(b'integer', b'integes'))
        with self.assertRaisesRegex(ValueError, 'imported byte drift'):
            self.m.check_tree(inventory, self.source, first)
        case.unlink()
        with self.assertRaisesRegex(ValueError, 'population/tree drift'):
            self.m.check_tree(inventory, self.source, first)

    def test_an_atomic_failure_preserves_the_previous_tree(self):
        inventory = self.inventory()
        destination = self.root / 'destination'
        self.m.write_tree(inventory, self.source, destination)
        before = {str(path.relative_to(destination)): path.read_bytes()
                  for path in destination.rglob('*') if path.is_file()}
        broken = copy.deepcopy(inventory)
        emitted = next(record for record in broken['sources']
                       if record.get('disposition') not in ('excluded', 'deferred'))
        emitted['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'source drift after inventory'):
            self.m.write_tree(broken, self.source, destination)
        self.assertEqual({str(path.relative_to(destination)): path.read_bytes()
                          for path in destination.rglob('*') if path.is_file()}, before)

    def test_unsafe_sources_and_symlinks_are_refused(self):
        unsafe = self.source / 'runtime/features/bad name.fs'
        unsafe.write_text('func test():\n\tpass\n')
        unsafe.with_suffix('.out').write_text('FS_TEST_OK\n')
        with self.assertRaisesRegex(ValueError, 'unsafe source path'):
            self.inventory(census=dict(FIXTURE_CENSUS, sources=FIXTURE_CENSUS['sources'] + 1,
                                       cases=FIXTURE_CENSUS['cases'] + 1,
                                       outputs=FIXTURE_CENSUS['outputs'] + 1))
        unsafe.unlink()
        unsafe.with_suffix('.out').unlink()
        link = self.source / 'runtime/features/linked.notest.fs'
        link.symlink_to(self.source / 'runtime/features/final_base.notest.fs')
        with self.assertRaisesRegex(ValueError, 'unsupported symlink'):
            self.inventory()

    def test_an_unknown_category_is_refused(self):
        invented = self.source / 'runtime/invented/case.fs'
        invented.parent.mkdir()
        invented.write_text('func test():\n\tpass\n')
        invented.with_suffix('.out').write_text('FS_TEST_OK\n')
        with self.assertRaisesRegex(ValueError, 'unknown runtime category'):
            self.inventory(census=dict(FIXTURE_CENSUS, sources=FIXTURE_CENSUS['sources'] + 1,
                                       cases=FIXTURE_CENSUS['cases'] + 1,
                                       outputs=FIXTURE_CENSUS['outputs'] + 1))

    def test_the_auxiliary_reconciliation_names_one_mechanism_per_identity(self):
        import corpus_registry
        registry = corpus_registry.load_registry(ROOT)
        self.m.validate_auxiliary_reconciliation(registry)
        self.assertNotIn(self.m.AUXILIARY_IN_CORPUS, registry['auxiliary_sources'])
        self.assertIn(self.m.AUXILIARY_REGISTERED, registry['auxiliary_sources'])
        self.assertIn('runtime corpus source root',
                      self.policy['auxiliary_reconciliation'][self.m.AUXILIARY_IN_CORPUS])
        broken = copy.deepcopy(registry)
        broken['auxiliary_sources'] = sorted(broken['auxiliary_sources'] + [self.m.AUXILIARY_IN_CORPUS])
        with self.assertRaisesRegex(ValueError, 'verified by that root'):
            self.m.validate_auxiliary_reconciliation(broken)
        self.expect_failure('reconcile exactly the two registered auxiliary helper identities',
                            lambda policy: policy['auxiliary_reconciliation'].pop(self.m.AUXILIARY_REGISTERED))


class PinEstablishment(unittest.TestCase):
    """How the first pin comes about, and what may change it afterwards."""

    def setUp(self):
        import import_runtime_corpus
        self.m = import_runtime_corpus
        self.policy = {'owners': {case: {'reason': 'owned'} for case in ('a.barista', 'b.barista', 'c.barista')}}
        self.included = {'a.barista', 'b.barista', 'c.barista'}
        self.triage = {'excluded': {}, 'deferred': {}}

    def establish(self, report=None, pinned=None):
        return self.m.established_expected_failures(self.policy, report, pinned, self.included, self.triage)

    def test_the_first_pin_is_the_whole_included_population(self):
        self.assertEqual(self.establish(), ['a.barista', 'b.barista', 'c.barista'])

    def test_a_committed_pin_is_carried_forward_and_may_shrink(self):
        self.assertEqual(self.establish(pinned=['a.barista']), ['a.barista'])
        self.assertEqual(self.establish(pinned=[]), [])

    def test_a_pin_entry_that_is_not_an_included_case_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'not an imported case'):
            self.establish(pinned=['a.barista', 'd.barista'])

    def test_a_pin_entry_without_an_owner_reason_is_refused(self):
        self.policy['owners']['b.barista'] = {'reason': '   '}
        with self.assertRaisesRegex(ValueError, 'non-empty reason'):
            self.establish(pinned=['b.barista'])
        self.policy['owners'].pop('b.barista')
        with self.assertRaisesRegex(ValueError, 'no semantic owner'):
            self.establish(pinned=['b.barista'])

    def test_a_pin_entry_carrying_a_non_import_disposition_is_refused(self):
        for key in ('excluded', 'deferred'):
            with self.subTest(disposition=key):
                self.triage = {'excluded': {}, 'deferred': {}}
                self.triage[key]['b.barista'] = 'reviewed'
                with self.assertRaisesRegex(ValueError, f'{key} disposition'):
                    self.establish(pinned=['b.barista'])

    def test_a_run_and_a_commit_may_not_both_supply_the_pin(self):
        with self.assertRaisesRegex(ValueError, 'never both'):
            self.establish(report={}, pinned=[])

    def test_a_partial_run_may_not_shrink_the_pin(self):
        report = {'completed': False, 'complete_population': True, 'selected_cases': [], 'results': [],
                  'stopped_cases': []}
        with self.assertRaisesRegex(ValueError, 'did not complete'):
            self.establish(report=report)

    def test_a_completed_run_derives_the_failing_set(self):
        report = {'completed': True, 'complete_population': True, 'infrastructure_error': None,
                  'selected_cases': sorted(self.included), 'stopped_cases': [],
                  'results': [{'case': 'a.barista', 'passed': True}, {'case': 'b.barista', 'passed': False},
                              {'case': 'c.barista', 'passed': False}]}
        self.assertEqual(self.establish(report=report), ['b.barista', 'c.barista'])


class CommittedCorpus(unittest.TestCase):
    """The committed corpus, its ledger and the corpora it must leave alone."""

    def run_importer(self, *arguments):
        return subprocess.run([sys.executable, str(ROOT / 'scripts/import_runtime_corpus.py'),
                               '--foundry', str(FOUNDRY), '--revision', REVISION, *arguments],
                              capture_output=True, text=True, check=False)

    def test_check_is_clean_and_writes_nothing(self):
        before = {str(path.relative_to(ROOT)): path.stat().st_mtime_ns
                  for path in (ROOT / 'project/tests/corpus/runtime').rglob('*') if path.is_file()}
        completed = self.run_importer('--check')
        self.assertEqual(completed.returncode, 0, completed.stderr)
        # The pin is the population minus whatever already passes, so it is asserted against
        # the committed baseline rather than against the import-time number, which was only
        # the whole population while nothing could execute a case.
        pinned = len(json.loads((ROOT / 'tests/corpus_baseline.json').read_text())['corpora']['runtime']['expected_failures'])
        self.assertIn(f'{pinned} pinned residual failures', completed.stdout)
        self.assertEqual({str(path.relative_to(ROOT)): path.stat().st_mtime_ns
                          for path in (ROOT / 'project/tests/corpus/runtime').rglob('*') if path.is_file()},
                         before)

    def test_a_wrong_revision_fails_rather_than_skipping(self):
        completed = subprocess.run([sys.executable, str(ROOT / 'scripts/import_runtime_corpus.py'),
                                    '--foundry', str(FOUNDRY), '--revision', '0' * 40, '--check'],
                                   capture_output=True, text=True, check=False)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn('not the pinned revision', completed.stderr)

    def test_one_changed_byte_in_an_imported_case_fails_the_check(self):
        case = ROOT / 'project/tests/corpus/runtime/errors/division_by_zero.barista'
        original = case.read_bytes()
        try:
            case.write_bytes(original.replace(b'integer', b'integes'))
            completed = self.run_importer('--check')
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn('byte drift', completed.stderr)
        finally:
            case.write_bytes(original)
        self.assertEqual(self.run_importer('--check').returncode, 0)

    def test_the_committed_ledger_states_the_verified_census(self):
        baseline = json.loads((ROOT / 'tests/corpus_baseline.json').read_text())['corpora']['runtime']
        self.assertEqual(baseline['root'], URI)
        self.assertEqual(baseline['upstream_total'], 757)
        self.assertEqual(baseline['upstream_helpers'], 82)
        self.assertEqual(baseline['upstream_sources'], 839)
        self.assertEqual(baseline['foundry_revision'], REVISION)
        self.assertEqual(baseline['upstream_total'],
                         baseline['total'] + len(baseline['triage']['excluded'])
                         + len(baseline['triage']['deferred']))
        self.assertEqual(baseline['expected_failures'], sorted(set(baseline['expected_failures'])))
        # A subset of the population, not the whole of it: the corpus executes now, and the pin
        # shrinks as families land. Every entry still has to be an imported case.
        self.assertLessEqual(len(baseline['expected_failures']), baseline['total'])
        stages = json.loads((ROOT / 'project/tests/corpus/runtime/case_stages.json').read_text())['cases']
        self.assertLessEqual(set(baseline['expected_failures']), set(stages))
        inventory = json.loads((ROOT / 'project/tests/corpus/runtime/inventory.json').read_text())
        self.assertEqual(inventory['counts']['statuses'],
                         {'FS_TEST_ANALYZER_ERROR': 10, 'FS_TEST_OK': 482, 'FS_TEST_RUNTIME_ERROR': 265})
        self.assertEqual(inventory['counts']['outputs'], 758)
        self.assertEqual({name: counts['cases'] for name, counts in inventory['counts']['categories'].items()},
                         {'enum_host_functions_namespaced': 1, 'errors': 257, 'features': 491,
                          'namespaced_native': 7, 'namespaced_script': 1})

    def test_the_stale_upstream_case_count_is_not_reproduced(self):
        for path in (ROOT / 'scripts/import_runtime_corpus.py', ROOT / 'scripts/runtime_expectations.py',
                     ROOT / 'scripts/runtime_corpus_policy.json',
                     ROOT / 'project/tests/corpus/runtime/README.md'):
            self.assertNotIn('816', path.read_text(), path.name)

    def test_the_pin_reads_as_a_burndown_board(self):
        import import_runtime_corpus
        policy = import_runtime_corpus.default_policy()
        baseline = json.loads((ROOT / 'tests/corpus_baseline.json').read_text())['corpora']['runtime']
        owners = {case: policy['owners'][case] for case in baseline['expected_failures']}
        self.assertEqual(len(owners), len(baseline['expected_failures']))
        for case, owner in owners.items():
            self.assertEqual(owner['reason'], f'#{owner["primary_issue"]}: no runtime yet — {owner["family"]}', case)
        self.assertGreaterEqual(len({owner['primary_issue'] for owner in owners.values()}), 5)

    def test_the_parser_and_analyzer_corpora_are_untouched(self):
        for name, importer in (('parser', 'scripts/import_parser_corpus.py'),
                               ('analyzer', 'scripts/import_analyzer_corpus.py')):
            with self.subTest(corpus=name):
                completed = subprocess.run([sys.executable, str(ROOT / importer), '--foundry', str(FOUNDRY),
                                            '--revision', REVISION, '--check'],
                                           capture_output=True, text=True, check=False)
                self.assertEqual(completed.returncode, 0, completed.stderr)
        baseline = json.loads((ROOT / 'tests/corpus_baseline.json').read_text())['corpora']
        self.assertEqual(len(baseline['analyzer']['expected_failures']), 168)
        self.assertEqual(baseline['parser']['expected_failures'], [])

    def test_the_imported_tree_carries_no_ignore_file(self):
        tree = ROOT / 'project/tests/corpus/runtime'
        self.assertTrue((FOUNDRY / 'modules/foundry_script/tests/scripts/runtime/.fsignore').is_file())
        self.assertEqual([path.name for path in tree.rglob('*') if path.name.endswith('ignore')], [])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--foundry', type=Path, required=True,
                        help='clean checkout of the registered Foundry revision; without it this '
                             'suite would report a pass for work it never did')
    arguments, remaining = parser.parse_known_args()
    FOUNDRY = arguments.foundry.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
