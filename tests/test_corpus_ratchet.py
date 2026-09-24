#!/usr/bin/env python3
# test_corpus_ratchet.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""The residual-failure pin may only ever shrink, and must match what a run observes.

Growth is measured against every committed state this checkout can resolve: the newest
commit whose pin differs from the working tree, and the merge base with the upstream
default branch when one exists. References only ever add complaints, so a missing merge
base weakens nothing and the one-commit lookback is required rather than optional.

Reference selection compares the pin, never the baseline file: an unrelated edit to the
baseline must not move the comparison onto the very commit that grew the pin.

Shrinking is enforced by the triage run itself: a pinned case that passes is a failure,
which is what keeps a pin from outliving the bug it describes.
"""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import corpus_registry
import run_corpus_triage as triage

BASELINE = 'tests/corpus_baseline.json'
UPSTREAM_BRANCH = 'origin/main'


def expected_failures_by_corpus(document):
    """``corpus -> set(path)`` for any baseline document, present or historical."""
    corpora = document.get('corpora') if isinstance(document, dict) else None
    if not isinstance(corpora, dict):
        return {}
    return {name: set(entry.get('expected_failures') or []) for name, entry in corpora.items()}


def imported_corpora(document):
    """The corpora a document actually imports, and which therefore may hold a pin."""
    corpora = document.get('corpora') if isinstance(document, dict) else None
    if not isinstance(corpora, dict):
        return set()
    return {name for name, entry in corpora.items() if entry.get('imported', True) is not False}


def growth(previous_document, current_document):
    """Every pin entry present now and absent before, named with its corpus.

    A corpus the previous state did not import contributes nothing, which covers two cases:
    a corpus recorded as pending, whose pin corpus_ledger forbids outright until promotion,
    and a corpus absent from the previous baseline altogether, which a rename also produces.
    Either way its first pin is establishment rather than growth, and every entry after that
    is judged against it.

    What keeps that opening safe is not this function. Two independent guards refuse a
    committed pin that does not match the tree it describes:

    * ``scripts/corpus_registry.py`` (the triage branch of ``validate_registration``) requires
      the baseline entry to equal the ledger the importer wrote beside the cases in
      ``inventory.json``. ``tests/validate_ci.py`` runs it, with no upstream checkout.
    * ``import_analyzer_corpus.check_tree`` regenerates the whole tree from the committed pin
      and byte-compares it. The tree's README states the pin count, so a mis-sized pin shows
      up as byte drift. ``scripts/check_corpus_reproducibility.py --foundry`` runs it, which
      needs the pinned Foundry checkout.

    Both of those guard *tampering* with a committed pin. What constrains how a pin is
    *derived* is upstream of all three: ``derive_expected_failures`` extracts the failing set
    mechanically from one completed run, and ``validate_expected_failures`` requires every
    entry to be an imported case with a written owner reason and no excluded/deferred
    disposition. growth() only enforces monotonicity on an already-established pin.
    """
    previous = expected_failures_by_corpus(previous_document)
    current = expected_failures_by_corpus(current_document)
    established = imported_corpora(previous_document)
    complaints = []
    for name in sorted(current):
        if name not in established:
            continue
        for path in sorted(current[name] - previous.get(name, set())):
            complaints.append(f'corpus {name!r}: {path} was added to expected_failures; '
                              'the residual-failure pin may only ever shrink')
    return complaints


def working_baseline(repository):
    return json.loads((repository / BASELINE).read_bytes())


def committed_baseline(repository, reference):
    """The baseline document at ``reference``, or None when it carries none."""
    try:
        return json.loads(corpus_registry.run_git(repository, 'show', f'{reference}:{BASELINE}'))
    except (ValueError, json.JSONDecodeError):
        return None


def resolved_commit(repository, reference):
    try:
        return corpus_registry.run_git(repository, 'rev-parse', '--verify', f'{reference}^{{commit}}')
    except ValueError:
        return None


def merge_base_commit(repository, reference=UPSTREAM_BRANCH):
    if resolved_commit(repository, reference) is None:
        return None
    try:
        return corpus_registry.run_git(repository, 'merge-base', 'HEAD', reference)
    except ValueError:
        return None


def previous_references(repository):
    """Every committed state the working tree's pin must not have grown against.

    The first reference is the newest commit whose *pin* differs from the working tree: an
    uncommitted pin change makes that HEAD, and an unchanged pin means the change, if any,
    is in the tip commit, so the comparison has to reach one commit further back. Comparing
    the baseline file instead would let any unrelated edit snap the comparison onto HEAD and
    hide growth that HEAD already carries.
    """
    current = expected_failures_by_corpus(working_baseline(repository))
    head = committed_baseline(repository, 'HEAD')
    candidates = ['HEAD' if head is None or current != expected_failures_by_corpus(head) else 'HEAD~1']
    candidates.append(merge_base_commit(repository))
    references = []
    seen = set()
    for candidate in candidates:
        if candidate is None:
            continue
        commit = resolved_commit(repository, candidate)
        document = committed_baseline(repository, candidate) if commit is not None else None
        if commit is None or document is None or commit in seen:
            continue
        seen.add(commit)
        references.append((candidate, document))
    return references


def repository_complaints(repository):
    references = previous_references(repository)
    current = working_baseline(repository)
    complaints = []
    for _, document in references:
        for complaint in growth(document, current):
            if complaint not in complaints:
                complaints.append(complaint)
    return references, complaints


def document(corpora):
    """A minimal baseline document: ``name -> (expected_failures, imported)``."""
    return {'corpora': {name: {'expected_failures': list(failures), 'imported': imported}
                        for name, (failures, imported) in corpora.items()}}


class GrowthDetection(unittest.TestCase):
    def test_growth_names_each_added_entry(self):
        complaints = growth(document({'analyzer': (['a.barista'], True)}),
                            document({'analyzer': (['a.barista', 'b.barista', 'c.barista'], True)}))
        self.assertEqual(len(complaints), 2)
        self.assertIn('b.barista', complaints[0])
        self.assertIn('c.barista', complaints[1])
        self.assertTrue(all('may only ever shrink' in complaint for complaint in complaints))

    def test_growth_accepts_a_shrinking_pin(self):
        self.assertEqual(growth(document({'analyzer': (['a.barista', 'b.barista'], True)}),
                                document({'analyzer': (['a.barista'], True)})), [])

    def test_growth_accepts_an_unchanged_pin(self):
        self.assertEqual(growth(document({'analyzer': (['a.barista'], True)}),
                                document({'analyzer': (['a.barista'], True)})), [])

    def test_a_first_pin_on_a_newly_imported_corpus_is_establishment(self):
        # corpus_ledger forbids a pending corpus any pin at all, so promotion is the one
        # moment a pin may appear; every entry after that is judged against it.
        promoted = growth(document({'analyzer': ([], False)}),
                          document({'analyzer': (['a.barista', 'b.barista'], True)}))
        self.assertEqual(promoted, [])
        after = growth(document({'analyzer': (['a.barista', 'b.barista'], True)}),
                       document({'analyzer': (['a.barista', 'b.barista', 'c.barista'], True)}))
        self.assertEqual(len(after), 1)
        self.assertIn('c.barista', after[0])

    def test_a_corpus_absent_from_the_previous_baseline_is_also_establishment(self):
        # A rename reaches the carve-out by this second entrance. What refuses a pin of the
        # wrong size is the baseline/ledger agreement in corpus_registry and the tree
        # byte-compare in import_analyzer_corpus.check_tree, not this function.
        self.assertEqual(growth(document({'parser': ([], True)}),
                                document({'parser': ([], True), 'analyzer': (['a.barista'], True)})), [])

    def test_an_imported_corpus_that_was_imported_before_is_judged(self):
        complaints = growth(document({'analyzer': ([], True)}), document({'analyzer': (['a.barista'], True)}))
        self.assertEqual(len(complaints), 1)
        self.assertIn("corpus 'analyzer'", complaints[0])

    def test_expected_failures_are_read_per_corpus(self):
        parsed = expected_failures_by_corpus({'corpora': {'parser': {'expected_failures': []},
                                                          'analyzer': {'expected_failures': ['x.barista']}}})
        self.assertEqual(parsed, {'parser': set(), 'analyzer': {'x.barista'}})


class TemporaryRepository:
    """A throwaway git repository holding only a baseline, driven through run_git."""

    def __init__(self, stack):
        self.root = Path(stack.enter_context(tempfile.TemporaryDirectory()))
        (self.root / 'tests').mkdir()
        self.git('init', '-q')
        self.git('symbolic-ref', 'HEAD', 'refs/heads/main')

    def git(self, *arguments):
        return corpus_registry.run_git(self.root, '-c', 'user.email=ratchet@example.invalid',
                                       '-c', 'user.name=Ratchet', '-c', 'commit.gpgsign=false', *arguments)

    def write(self, corpora, **extra):
        payload = document(corpora)
        for name, entry in payload['corpora'].items():
            entry.update(extra.get(name, {}))
        (self.root / BASELINE).write_text(json.dumps(payload, indent=2) + '\n')

    def commit(self, message):
        self.git('add', '-A')
        self.git('commit', '-q', '-m', message)
        return self.git('rev-parse', 'HEAD')


class ReferenceSelection(unittest.TestCase):
    def setUp(self):
        from contextlib import ExitStack
        stack = ExitStack()
        self.addCleanup(stack.close)
        self.repository = TemporaryRepository(stack)

    def test_an_uncommitted_pin_addition_is_caught_against_head(self):
        self.repository.write({'analyzer': (['a.barista'], True)})
        self.repository.commit('base')
        self.repository.write({'analyzer': (['a.barista', 'b.barista'], True)})
        references, complaints = repository_complaints(self.repository.root)
        self.assertEqual([label for label, _ in references], ['HEAD'])
        self.assertEqual(len(complaints), 1)
        self.assertIn('b.barista', complaints[0])

    def test_a_committed_pin_addition_is_caught_against_the_grandparent(self):
        self.repository.write({'analyzer': (['a.barista'], True)})
        self.repository.commit('base')
        self.repository.write({'analyzer': (['a.barista', 'b.barista'], True)})
        self.repository.commit('grow the pin')
        references, complaints = repository_complaints(self.repository.root)
        self.assertEqual([label for label, _ in references], ['HEAD~1'])
        self.assertEqual(len(complaints), 1)
        self.assertIn('b.barista', complaints[0])

    def test_an_unrelated_dirty_baseline_edit_does_not_hide_committed_growth(self):
        # The escape this test exists for: selecting the reference by comparing the baseline
        # file rather than the pin snapped the comparison onto HEAD, which already carried
        # the added entry, so the addition compared equal to itself and vanished.
        self.repository.write({'analyzer': (['a.barista'], True)})
        self.repository.commit('base')
        self.repository.write({'analyzer': (['a.barista', 'b.barista'], True)})
        self.repository.commit('grow the pin')
        self.repository.write({'analyzer': (['a.barista', 'b.barista'], True)}, analyzer={'total': 1078})
        self.assertNotEqual((self.repository.root / BASELINE).read_text(),
                            self.repository.git('show', f'HEAD:{BASELINE}') + '\n')
        references, complaints = repository_complaints(self.repository.root)
        self.assertEqual([label for label, _ in references], ['HEAD~1'])
        self.assertEqual(len(complaints), 1)
        self.assertIn('b.barista', complaints[0])

    def test_growth_in_an_earlier_branch_commit_is_caught_against_the_merge_base(self):
        # A one-commit lookback alone cannot see this: HEAD~1 already carries the addition.
        self.repository.write({'analyzer': (['a.barista'], True)})
        base = self.repository.commit('base')
        self.repository.git('update-ref', f'refs/remotes/{UPSTREAM_BRANCH}', base)
        self.repository.write({'analyzer': (['a.barista', 'b.barista'], True)})
        self.repository.commit('grow the pin')
        self.repository.write({'analyzer': (['a.barista', 'b.barista'], True)}, analyzer={'total': 7})
        self.repository.commit('unrelated tip commit')
        self.assertEqual(growth(committed_baseline(self.repository.root, 'HEAD~1'),
                                working_baseline(self.repository.root)), [])
        references, complaints = repository_complaints(self.repository.root)
        self.assertIn(base, [self.repository.git('rev-parse', label) for label, _ in references])
        self.assertEqual(len(complaints), 1)
        self.assertIn('b.barista', complaints[0])

    def test_a_missing_upstream_branch_leaves_the_one_commit_lookback(self):
        self.repository.write({'analyzer': (['a.barista'], True)})
        self.repository.commit('base')
        self.repository.write({'analyzer': (['a.barista', 'b.barista'], True)})
        self.repository.commit('grow the pin')
        self.assertIsNone(merge_base_commit(self.repository.root))
        references, complaints = repository_complaints(self.repository.root)
        self.assertEqual([label for label, _ in references], ['HEAD~1'])
        self.assertEqual(len(complaints), 1)

    def test_an_orphan_first_commit_resolves_no_reference(self):
        self.repository.write({'analyzer': ([], True)})
        self.repository.commit('only commit')
        self.assertEqual(previous_references(self.repository.root), [])

    def test_duplicate_references_are_compared_once(self):
        self.repository.write({'analyzer': (['a.barista'], True)})
        base = self.repository.commit('base')
        self.repository.write({'analyzer': (['a.barista', 'b.barista'], True)})
        self.repository.commit('grow the pin')
        self.repository.git('update-ref', f'refs/remotes/{UPSTREAM_BRANCH}', base)
        references, complaints = repository_complaints(self.repository.root)
        self.assertEqual(len(references), 1)
        self.assertEqual(len(complaints), 1)


class GitAccess(unittest.TestCase):
    def test_the_previous_baseline_is_read_through_the_shared_git_helper(self):
        with patch.object(corpus_registry, 'run_git', return_value='{"corpora": {}}') as helper:
            self.assertEqual(committed_baseline(ROOT, 'HEAD~1'), {'corpora': {}})
        helper.assert_called_once_with(ROOT, 'show', f'HEAD~1:{BASELINE}')

    def test_a_reference_without_the_baseline_reads_as_no_document(self):
        with patch.object(corpus_registry, 'run_git', side_effect=ValueError('no such path')):
            self.assertIsNone(committed_baseline(ROOT, 'HEAD~1'))


class RepositoryRatchet(unittest.TestCase):
    def test_expected_failures_did_not_grow_since_the_previous_commit(self):
        current = expected_failures_by_corpus(working_baseline(ROOT))
        self.assertIn('parser', current, 'the baseline under test was not parsed')
        references, complaints = repository_complaints(ROOT)
        self.assertTrue(references, 'no committed baseline to compare against; the ratchet needs the full history')
        self.assertEqual(complaints, [], '\n'.join(complaints))


class RuntimeRatchet(unittest.TestCase):
    """The runtime pin is established once at 100% and is monotone from then on.

    Establishment is the whole included population, because the corpus lands before any
    virtual machine exists. That makes growth the only direction that can hide a
    regression, and it is refused here exactly as it is for the analyzer corpus.
    """

    def setUp(self):
        import import_runtime_corpus
        self.importer = import_runtime_corpus
        self.included = {'a.barista', 'b.barista'}
        self.policy = {'owners': {case: {'reason': '#244: no runtime yet — expressions'}
                                  for case in self.included}}
        self.triage = {'excluded': {}, 'deferred': {}}

    def test_a_grown_runtime_pin_is_refused(self):
        complaints = growth(document({'runtime': (['a.barista'], True)}),
                            document({'runtime': (['a.barista', 'b.barista'], True)}))
        self.assertEqual(len(complaints), 1)
        self.assertIn("corpus 'runtime'", complaints[0])
        self.assertIn('b.barista', complaints[0])

    def test_a_shrinking_runtime_pin_is_accepted(self):
        self.assertEqual(growth(document({'runtime': (['a.barista', 'b.barista'], True)}),
                                document({'runtime': (['a.barista'], True)})), [])

    def test_the_committed_runtime_pin_is_a_sorted_subset_of_the_population(self):
        # At import the pin was the whole population, because nothing could execute a case.
        # Now that the corpus runs, the pin is whatever is left: a sorted, duplicate-free
        # subset of the imported cases that shrinks as families land. It may reach zero, and
        # the monotone direction is what the growth cases above enforce.
        baseline = working_baseline(ROOT)['corpora']['runtime']
        pin = baseline['expected_failures']
        self.assertEqual(pin, sorted(set(pin)))
        self.assertLessEqual(len(pin), baseline['total'])
        stages = json.loads((ROOT / 'project/tests/corpus/runtime/case_stages.json').read_text())
        self.assertLessEqual(set(pin), set(stages['cases']))

    def test_a_runtime_pin_entry_without_an_owner_or_a_reason_is_refused(self):
        self.assertEqual(self.importer.established_expected_failures(
            self.policy, None, None, self.included, self.triage), sorted(self.included))
        self.policy['owners']['b.barista'] = {'reason': '  '}
        with self.assertRaisesRegex(ValueError, 'non-empty reason'):
            self.importer.established_expected_failures(self.policy, None, ['b.barista'],
                                                        self.included, self.triage)
        self.policy['owners'].pop('b.barista')
        with self.assertRaisesRegex(ValueError, 'no semantic owner'):
            self.importer.established_expected_failures(self.policy, None, ['b.barista'],
                                                        self.included, self.triage)

    def test_a_runtime_pin_entry_that_is_not_an_included_case_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'not an imported case'):
            self.importer.established_expected_failures(self.policy, None, ['c.barista'],
                                                        self.included, self.triage)


class PinEnforcement(unittest.TestCase):
    """The shrinking half of the ratchet, enforced by the triage run against real outcomes."""

    def results(self, outcomes, *, owned=()):
        owned = set(owned) or {case for case in outcomes if not outcomes[case]}
        # A failing record carries the terminal that produced it. Only 'mismatch' -- the case
        # ran and its transcript diverged -- is a failure a pin may absorb, so these fixtures
        # have to say which failure they are rather than leaving it to a default.
        return [{'case': case, 'passed': passed, 'terminal': 'passed' if passed else 'mismatch',
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
        results = [{'case': 'b.barista', 'passed': False, 'terminal': 'mismatch',
                    'semantic_owner': {'reason': '  '}}]
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
