#!/usr/bin/env python3
# test_corpus_ratchet.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""The residual-failure pin may only ever shrink, and must match what a run observes.

Growth is measured against the newest committed baseline that differs from the working
tree, so an uncommitted addition is caught before it is committed and a committed one is
caught afterwards. Shrinking is enforced by the triage run itself: a pinned case that
passes is a failure, which is what keeps a pin from outliving the bug it describes.
"""
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import corpus_registry
import run_corpus_triage as triage

BASELINE = 'tests/corpus_baseline.json'


def expected_failures_by_corpus(document):
    """``corpus -> set(path)`` for any baseline document, present or historical."""
    corpora = document.get('corpora') if isinstance(document, dict) else None
    if not isinstance(corpora, dict):
        return {}
    return {name: set(entry.get('expected_failures') or []) for name, entry in corpora.items()}


def committed_baseline(repository, reference):
    """The baseline document at ``reference``, or an empty document when it has none."""
    if reference is None:
        return {}
    try:
        return json.loads(corpus_registry.run_git(repository, 'show', f'{reference}:{BASELINE}'))
    except (ValueError, json.JSONDecodeError):
        return {}


def previous_reference(repository):
    """The newest committed state that differs from the baseline being checked.

    With an uncommitted edit that is HEAD itself; with a clean tree the edit, if any, is in
    the tip commit, so the comparison has to reach one commit further back.
    """
    current = (repository / BASELINE).read_bytes()
    try:
        head = corpus_registry.run_git(repository, 'show', f'HEAD:{BASELINE}')
    except ValueError:
        return None
    if head.strip() != current.decode('utf-8').strip():
        return 'HEAD'
    try:
        corpus_registry.run_git(repository, 'rev-parse', '--verify', 'HEAD~1')
    except ValueError:
        return None
    return 'HEAD~1'


def growth(previous, current):
    """Every pin entry present now and absent before, named with its corpus."""
    complaints = []
    for name in sorted(current):
        for path in sorted(current[name] - previous.get(name, set())):
            complaints.append(f'corpus {name!r}: {path} was added to expected_failures; '
                              'the residual-failure pin may only ever shrink')
    return complaints


class GrowthDetection(unittest.TestCase):
    def test_growth_names_each_added_entry(self):
        complaints = growth({'analyzer': {'a.barista'}}, {'analyzer': {'a.barista', 'b.barista', 'c.barista'}})
        self.assertEqual(len(complaints), 2)
        self.assertIn('b.barista', complaints[0])
        self.assertIn('c.barista', complaints[1])
        self.assertTrue(all('may only ever shrink' in complaint for complaint in complaints))

    def test_growth_accepts_a_shrinking_pin(self):
        self.assertEqual(growth({'analyzer': {'a.barista', 'b.barista'}}, {'analyzer': {'a.barista'}}), [])

    def test_growth_accepts_an_unchanged_pin(self):
        self.assertEqual(growth({'analyzer': {'a.barista'}}, {'analyzer': {'a.barista'}}), [])

    def test_growth_judges_a_corpus_the_previous_baseline_did_not_have(self):
        complaints = growth({'parser': set()}, {'parser': set(), 'analyzer': {'a.barista'}})
        self.assertEqual(len(complaints), 1)
        self.assertIn("corpus 'analyzer'", complaints[0])

    def test_expected_failures_are_read_per_corpus(self):
        document = {'corpora': {'parser': {'expected_failures': []},
                                'analyzer': {'expected_failures': ['x.barista']}}}
        self.assertEqual(expected_failures_by_corpus(document), {'parser': set(), 'analyzer': {'x.barista'}})


class PreviousCommitLookup(unittest.TestCase):
    def test_the_previous_baseline_is_read_through_the_shared_git_helper(self):
        with patch.object(corpus_registry, 'run_git', return_value='{"corpora": {}}') as helper:
            self.assertEqual(committed_baseline(ROOT, 'HEAD~1'), {'corpora': {}})
        helper.assert_called_once_with(ROOT, 'show', f'HEAD~1:{BASELINE}')

    def test_a_reference_without_the_baseline_reads_as_an_empty_document(self):
        with patch.object(corpus_registry, 'run_git', side_effect=ValueError('no such path')):
            self.assertEqual(committed_baseline(ROOT, 'HEAD~1'), {})

    def test_the_repository_resolves_a_previous_reference(self):
        self.assertIn(previous_reference(ROOT), ('HEAD', 'HEAD~1'))


class RepositoryRatchet(unittest.TestCase):
    def test_expected_failures_did_not_grow_since_the_previous_commit(self):
        reference = previous_reference(ROOT)
        current = expected_failures_by_corpus(json.loads((ROOT / BASELINE).read_bytes()))
        self.assertIn('parser', current, 'the baseline under test was not parsed')
        self.assertIsNotNone(reference, 'no committed baseline to compare against; the ratchet needs the full history')
        complaints = growth(expected_failures_by_corpus(committed_baseline(ROOT, reference)), current)
        self.assertEqual(complaints, [], '\n'.join(complaints))


class PinEnforcement(unittest.TestCase):
    """The shrinking half of the ratchet, enforced by the triage run against real outcomes."""

    def results(self, outcomes, *, owned=()):
        owned = set(owned) or {case for case in outcomes if not outcomes[case]}
        return [{'case': case, 'passed': passed,
                 'semantic_owner': {'reason': 'Contextual unions are unimplemented.'} if case in owned else None}
                for case, passed in outcomes.items()]

    def test_a_pin_matching_the_observed_failures_is_accepted(self):
        results = self.results({'a.barista': True, 'b.barista': False})
        self.assertEqual(triage.expected_failure_complaints(results, ['b.barista'], ['a.barista', 'b.barista']), [])

    def test_a_pinned_case_that_passes_is_reported(self):
        results = self.results({'a.barista': True, 'b.barista': False})
        complaints = triage.expected_failure_complaints(results, ['a.barista', 'b.barista'], ['a.barista', 'b.barista'])
        self.assertEqual(len(complaints), 1)
        self.assertIn('a.barista', complaints[0])
        self.assertIn('remove it from expected_failures', complaints[0])

    def test_an_undeclared_failure_is_reported(self):
        results = self.results({'a.barista': True, 'b.barista': False})
        complaints = triage.expected_failure_complaints(results, [], ['a.barista', 'b.barista'])
        self.assertEqual(len(complaints), 1)
        self.assertIn('b.barista', complaints[0])
        self.assertIn('not declared in expected_failures', complaints[0])

    def test_a_failure_without_an_owner_is_reported(self):
        results = self.results({'b.barista': False}, owned={'nothing.barista'})
        complaints = triage.expected_failure_complaints(results, ['b.barista'], ['b.barista'])
        self.assertEqual(len(complaints), 1)
        self.assertIn('no semantic owner', complaints[0])

    def test_an_owner_with_a_blank_reason_does_not_count_as_ownership(self):
        results = [{'case': 'b.barista', 'passed': False, 'semantic_owner': {'reason': '  '}}]
        complaints = triage.expected_failure_complaints(results, ['b.barista'], ['b.barista'])
        self.assertEqual(len(complaints), 1)
        self.assertIn('no semantic owner', complaints[0])

    def test_pinned_cases_outside_the_selection_are_not_judged(self):
        results = self.results({'b.barista': False})
        self.assertEqual(triage.expected_failure_complaints(results, ['a.barista', 'b.barista'], ['b.barista']), [])

    def test_a_selected_case_without_an_outcome_is_reported(self):
        complaints = triage.expected_failure_complaints([], ['a.barista'], ['a.barista'])
        self.assertEqual(len(complaints), 1)
        self.assertIn('no recorded outcome', complaints[0])


if __name__ == '__main__':
    unittest.main()
