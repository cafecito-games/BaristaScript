#!/usr/bin/env python3
# import_analyzer_corpus.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Import the pinned analyzer corpus into its registered destination.

The inventory retains every source before policy subtraction. Dispositions come
from the pinned policy, never from an observed run; a run only supplies the
residual-failure pin, and every entry in it needs a written owner reason.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import sys
import tempfile

from corpus_expectations import decode_expectation, extract_static_block
from corpus_ledger import barista_path, empty_triage, validate_triage_ledger
from corpus_registry import ROOT, local_path, read_json, tree_entries, validate_registration, verify_checkout
from corpus_stages import write_stages

SCRIPTS = 'modules/foundry_script/tests/scripts'
DESTINATION = 'project/tests/corpus/analyzer'
BASELINE = ROOT / 'tests/corpus_baseline.json'
POLICY = ROOT / 'scripts/analyzer_corpus_policy.json'
STATUS = {'FS_TEST_OK', 'FS_TEST_ANALYZER_ERROR', 'FS_TEST_PARSER_ERROR', 'FS_TEST_COMPILER_ERROR', 'FS_TEST_RUNTIME_ERROR'}
LATER = {
    'errors/enum_name_bare_payload_case_in_own_body.barista': 'FS_TEST_COMPILER_ERROR',
    'errors/generic_trait_use_missing_type_arguments_dependency.barista': 'FS_TEST_COMPILER_ERROR',
    'features/typed_callable_bind_gapped_arity_rpc_id.barista': 'FS_TEST_RUNTIME_ERROR',
    'features/typed_callable_unbind_gapped_arity_rpc_id.barista': 'FS_TEST_RUNTIME_ERROR',
}
SUPPORT = ['utils.notest.fs', 'runtime/features/rtc_runtime_self_adoptable.notest.fs']
SHARED_SUPPORT_ROOTS = {'utils.notest.fs': 'res://tests/corpus_support/parser'}
MISSING = ('analyzer/errors/preload_missing_relative_path.fs', './preload_missing_relative_target.notest.fs',
           'analyzer/errors/preload_missing_relative_target.notest.fs')


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def encoded(value) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False, sort_keys=True) + '\n').encode('utf-8')


def default_policy():
    return read_json(POLICY)


def checked_text(data: bytes, path: str) -> str:
    try:
        text = data.decode('utf-8')
    except UnicodeError as error:
        raise ValueError(f'{path}: invalid UTF-8') from error
    if '\0' in text:
        raise ValueError(f'{path}: NUL in source')
    return text


# A lexical scanner, not a language parser: comments/other quoted strings are
# consumed as units. Only load/preload argument and extends string tokens count.
TOKEN = re.compile(rb'#[^\n]*|"""[\s\S]*?"""|\'\'\'[\s\S]*?\'\'\'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_][A-Za-z_0-9]*|[^\s]', re.MULTILINE)


def resource_spans(data: bytes, path: str):
    tokens = [m for m in TOKEN.finditer(data) if not m[0].startswith(b'#')]
    for index, token in enumerate(tokens):
        raw = token[0]
        if raw[:1] not in (b'"', b"'"):
            continue
        prior = [t[0] for t in tokens[max(0, index - 2):index]]
        kind = 'extends' if prior and prior[-1] == b'extends' else None
        if len(prior) == 2 and prior[0] in (b'load', b'preload') and prior[1] == b'(':
            kind = prior[0].decode()
        if kind is None:
            continue
        literal = raw[1:-1].decode('utf-8')
        if '.fs' not in literal:
            continue
        if b'\\' in raw or raw[:3] in (b'"""', b"'''") or not literal.endswith('.fs'):
            raise ValueError(f'{path}: unsupported resource literal {literal!r}')
        yield token.start() + 1, token.end() - 1, literal, kind


def _patch(data: bytes, changes: list[dict], path: str, allow_newline_changes: bool) -> bytes:
    end = 0
    required = {'start', 'end', 'line', 'before', 'after', 'rule', 'occurrences'}
    for item in changes:
        if (not isinstance(item, dict) or not required <= item.keys()
                or item.keys() - required - {'target'}
                or any(not isinstance(item[k], str) or '\0' in item[k] for k in ('before', 'after', 'rule'))
                or not item['rule'].strip()
                or type(item['line']) is not int
                or type(item['occurrences']) is not int or item['occurrences'] != 1
                or type(item['start']) is not int or type(item['end']) is not int
                or (not allow_newline_changes
                    and item['before'].count('\n') != item['after'].count('\n'))):
            raise ValueError(f'{path}: invalid patch provenance')
    for change in sorted(changes, key=lambda item: item['start']):
        start, stop = change['start'], change['end']
        if type(start) is not int or type(stop) is not int or start < end or stop <= start or stop > len(data):
            raise ValueError(f'{path}: overlapping/invalid patch span')
        if data[start:stop] != change['before'].encode('utf-8'):
            raise ValueError(f'{path}: patch preimage mismatch at {start}')
        if change['line'] != data[:start].count(b'\n') + 1:
            raise ValueError(f'{path}: patch line preimage mismatch at {start}')
        end = stop
    for change in sorted(changes, key=lambda item: item['start'], reverse=True):
        data = data[:change['start']] + change['after'].encode('utf-8') + data[change['end']:]
    return data


def patch(data: bytes, changes: list[dict], path: str) -> bytes:
    """Apply a source/resource patch while preserving its exact line structure."""
    return _patch(data, changes, path, False)


def expectation_patch(data: bytes, changes: list[dict], path: str) -> bytes:
    """Apply a pinned expectation-only patch, permitting complete-block line changes."""
    checked_text(data, path)
    return _patch(data, changes, path, True)


def change(data, start, end, after, rule, **extra):
    return {'start': start, 'end': end, 'line': data[:start].count(b'\n') + 1,
            'before': data[start:end].decode('utf-8'), 'after': after,
            'rule': rule, 'occurrences': 1, **extra}


def source_policy_changes(data: bytes, path: str, policy: dict) -> list[dict]:
    """Shared exact-preimage adapter for imported cases and auxiliary sources."""
    if path not in policy['source_edits']:
        return []
    edits = policy['source_edits'][path]
    if (not isinstance(edits, dict)
            or set(edits) not in ({'sha256', 'patches'},
                                  {'sha256', 'patches', 'projected_helper_clone'})
            or edits.get('sha256') != sha(data)):
        raise ValueError(f'{path}: source edit hash preimage mismatch')
    patch(data, edits['patches'], path)
    return edits['patches']


def projected_helper_clone(data: bytes, path: str, edits: dict, occupied: set[str]) -> dict | None:
    """Validate one helper-only raw-source clone nested under its pinned provider."""
    clone = edits.get('projected_helper_clone')
    if clone is None:
        return None
    if not path.startswith('analyzer/') or not path.endswith('.notest.fs'):
        raise ValueError(f'{path}: projected helper clone requires an analyzer helper provider')
    if (not isinstance(clone, dict)
            or set(clone) != {'target', 'transformed_sha256', 'patches'}
            or not isinstance(clone.get('patches'), list) or not clone['patches']):
        raise ValueError(f'{path}: invalid projected helper clone schema')
    target = clone.get('target')
    if (not isinstance(target, str) or not target.endswith('.notest.barista')
            or target.startswith('/') or '\\' in target or str(PurePosixPath(target)) != target
            or any(part in ('', '.', '..') for part in target.split('/'))
            or target.split('/')[0] not in ('errors', 'features', 'warnings')):
        raise ValueError(f'{path}: invalid projected helper clone target')
    if target in occupied:
        raise ValueError(f'{path}: projected helper clone target collision: {target}')
    transformed = patch(data, clone['patches'], f'{path} projected helper clone')
    if (not isinstance(clone.get('transformed_sha256'), str)
            or not re.fullmatch(r'[0-9a-f]{64}', clone['transformed_sha256'])
            or sha(transformed) != clone['transformed_sha256']):
        raise ValueError(f'{path}: projected helper clone transformed hash mismatch')
    identity = f'{SCRIPTS}/{path}#projected-helper-clone:{target}'
    return {'target': target, 'identity': identity, 'patches': clone['patches'],
            'transformed_sha256': clone['transformed_sha256']}


def source_record(data, path, relative, role, changes, references):
    transformed = patch(data, changes, path)
    return {'upstream_path': path, 'identity': SCRIPTS + '/' + path,
            'sha256': sha(data), 'git_blob': hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest(),
            'role': role, 'imported_path': relative, 'imported_sha256': sha(transformed),
            'transformations': sorted(changes, key=lambda p: p['start']), 'references': references}


def path_map(files: dict[str, bytes], uri: str):
    mapping = {}
    for path in sorted(files):
        if not path.endswith('.fs'):
            continue
        if path.startswith('analyzer/'):
            mapping[path] = uri + '/' + barista_path(path.removeprefix('analyzer/'))
        elif path.startswith('parser/'):
            mapping[path] = 'res://tests/corpus/parser/' + barista_path(path.removeprefix('parser/'))
        elif path in SHARED_SUPPORT_ROOTS:
            mapping[path] = SHARED_SUPPORT_ROOTS[path] + '/' + barista_path(path)
        elif path in SUPPORT:
            mapping[path] = uri + '/_support/' + barista_path(path)
    # Only this case asserts absence. It is not a source and never enumerates as one.
    mapping[MISSING[2]] = uri + '/' + barista_path(MISSING[2].removeprefix('analyzer/'))
    return mapping


def expected_projection(block: str, mapping: dict, path: str):
    data = block.encode('utf-8')
    changes = []
    # Identity fragments only. A duplicate basename projects its extension, never
    # chooses a provider; full paths preserve that distinction.
    pattern = rb'res://[A-Za-z0-9_./-]+\.fs|[A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+)*\.fs'
    basenames = {PurePosixPath(p).name for p in mapping}
    for match in re.finditer(pattern, data):
        before = match[0].decode()
        if before.startswith('res://'):
            target = before.removeprefix('res://')
            if target not in mapping:
                raise ValueError(f'{path}: unknown diagnostic source identity {before}')
            after = mapping[target]
        else:
            if before not in basenames:
                raise ValueError(f'{path}: unknown diagnostic filename {before}')
            after = barista_path(before)
        changes.append(change(data, match.start(), match.end(), after, 'diagnostic-resource-identity'))
    return patch(data, changes, path).decode(), changes


def owner_candidates(source: str, expected: str, references: list):
    """Construct observations select finite review slices, never dispositions."""
    signals = []
    if references or re.search(r'^\s*(import|namespace)\b', source, re.M):
        signals.append({'issue': 140, 'family': 'X1-X6', 'evidence': 'resource edge or namespace/import declaration'})
    if re.search(r'\b(Self|tuple|tuple_name)\b|\b(Array|Dictionary)\s*\[|\bas\b|\bis\b', source):
        signals.append({'issue': 138, 'family': 'R03/R05/R25/R27', 'evidence': 'concrete type/container/receiver reduction'})
    if re.search(r'\b(match|while|for|assert|final)\b|@noreturn', source):
        signals.append({'issue': 139, 'family': 'R26', 'evidence': 'flow/finality/iteration/assertion construct'})
    if re.search(r'\b(uses|trait|trait_name|extend)\b|@|\b(Callable|Signal)\b|\.\w+\s*\(', source):
        signals.append({'issue': 141, 'family': 'S1-S6', 'evidence': 'annotation/conformance/call/member surface'})
    if not signals:
        signals.append({'issue': 138, 'family': 'R27', 'evidence': 'local expression/declaration and value compatibility; requires execution review'})
    return signals


def _recorded_outcomes(report, included_cases):
    """Return ``case -> passed`` for a run that may be used as a pin basis.

    Absence of a result is only interpretable once the run is known to have finished.
    A stopped case ran but produced unusable output, and a case that was never
    dispatched produced nothing at all; neither may be read as a pass.
    """
    if not isinstance(report, dict):
        raise ValueError('execution report must be a JSON object')
    if report.get('infrastructure_error') is not None:
        raise ValueError(f'execution report records an infrastructure error: {report["infrastructure_error"]}')
    if report.get('completed') is not True:
        raise ValueError('execution report did not complete; a partial run is not a pin basis')
    if report.get('complete_population') is not True:
        raise ValueError('execution report covers a case selection, not the complete population')
    selected = report.get('selected_cases')
    results = report.get('results')
    stopped = report.get('stopped_cases')
    if not isinstance(selected, list) or not isinstance(results, list) or not isinstance(stopped, list):
        raise ValueError('execution report selected_cases, results and stopped_cases must be lists')
    outcomes = {}
    for record in results:
        if not isinstance(record, dict) or not isinstance(record.get('case'), str) or type(record.get('passed')) is not bool:
            raise ValueError('execution report result records must carry a case name and a boolean outcome')
        if record['case'] in outcomes:
            raise ValueError(f'execution report holds duplicate result records for {record["case"]}')
        outcomes[record['case']] = record['passed']
    if set(selected) != set(included_cases):
        raise ValueError('execution report population differs from the included case set: '
                         + str(sorted(set(selected) ^ set(included_cases))[:8]))
    unselected = sorted(set(outcomes) - set(selected))
    if unselected:
        raise ValueError(f'execution report records outcomes for cases it did not select: {", ".join(unselected)}')
    killed = sorted(case for case in stopped if case in selected)
    if killed:
        raise ValueError(f'execution report stopped these cases mid-flight: {", ".join(killed)}')
    undispatched = sorted(case for case in selected if case not in outcomes)
    if undispatched:
        raise ValueError(f'execution report has no recorded outcome for: {", ".join(undispatched)}')
    return outcomes


def validate_expected_failures(policy: dict, failures, included_cases, triage: dict) -> list[str]:
    """Check one residual-failure pin against policy ownership and the included cases.

    Every pinned failure must be an included case that carries a semantic owner with a
    non-empty reason and no excluded/deferred disposition, so a failure cannot enter the
    pin without someone having written down why it fails. Both the derivation from a run
    and the committed pin read back by ``--check`` go through here, so a hand-edited
    baseline is held to exactly what an execution-derived one had to satisfy.
    """
    if (not isinstance(failures, list) or not all(isinstance(case, str) for case in failures)
            or sorted(set(failures)) != failures):
        raise ValueError('expected failures must be a sorted list of unique case paths')
    owners = policy.get('owners')
    if not isinstance(owners, dict):
        raise ValueError('policy owners must be an object of path -> owner')
    excluded = triage.get('excluded', {})
    deferred = triage.get('deferred', {})
    for case in failures:
        if case not in included_cases:
            raise ValueError(f'{case}: pinned expected failure is not an imported case')
        if case in excluded:
            raise ValueError(f'{case}: failing case carries an excluded disposition; a residual failure '
                             'must be declared, not absorbed into a non-import disposition')
        if case in deferred:
            raise ValueError(f'{case}: failing case carries a deferred disposition; a residual failure '
                             'must be declared, not absorbed into a non-import disposition')
        owner = owners.get(case)
        if owner is None:
            raise ValueError(f'{case}: failing case has no semantic owner; every pinned failure needs a '
                             'path-specific written reason')
        if not isinstance(owner, dict) or not isinstance(owner.get('reason'), str) or not owner['reason'].strip():
            raise ValueError(f'{case}: semantic owner must carry a non-empty reason')
    return list(failures)


def derive_expected_failures(policy: dict, execution_report: dict, included_cases, triage: dict) -> list[str]:
    """Compute the pinned residual failure set from policy ownership plus one run."""
    outcomes = _recorded_outcomes(execution_report, included_cases)
    failures = sorted(case for case, passed in outcomes.items() if not passed)
    return validate_expected_failures(policy, failures, included_cases, triage)


def resolved_expected_failures(policy, execution_report, pinned_failures, included, triage) -> list[str]:
    """The ledger pin: derived from a completed run, read back from a commit, or empty."""
    if execution_report is not None and pinned_failures is not None:
        raise ValueError('a pinned ledger comes from one source: a run or a commit, never both')
    if execution_report is not None:
        return derive_expected_failures(policy, execution_report, included, triage)
    if pinned_failures is not None:
        return validate_expected_failures(policy, pinned_failures, included, triage)
    return []


def inventory_sources(scripts: Path, policy: dict, uri: str, execution_report: dict | None = None,
                      pinned_failures: list[str] | None = None) -> dict:
    if set(policy) != {'schema_version', 'foundry_revision', 'counts', 'excluded', 'rewritten',
                       'expectation_overrides', 'deferred', 'source_edits', 'expectation_edits', 'owners'} or type(policy['schema_version']) is not int or policy['schema_version'] != 2:
        raise ValueError('invalid analyzer policy schema')
    for field in ('excluded', 'rewritten', 'expectation_overrides', 'deferred', 'source_edits', 'expectation_edits', 'owners'):
        if not isinstance(policy[field], dict):
            raise ValueError(f'invalid analyzer policy map: {field}')
    if not isinstance(policy['foundry_revision'], str) or not re.fullmatch(r'[0-9a-f]{40}', policy['foundry_revision']):
        raise ValueError('invalid analyzer policy revision')
    for path, owner in policy['owners'].items():
        if (not isinstance(owner, dict) or owner.get('primary_issue') not in (45, 60, 138, 139, 140, 141)
                or not isinstance(owner.get('family'), str) or not owner['family']
                or not isinstance(owner.get('reason'), str) or not owner['reason'].strip()
                or not isinstance(owner.get('prerequisites'), list)
                or any(type(n) is not int or n not in (42, 45, 60, 138, 139, 140, 141) for n in owner['prerequisites'])
                or len(set(owner['prerequisites'])) != len(owner['prerequisites'])
                or owner['primary_issue'] in owner['prerequisites']
                or owner.get('review_state') not in ('source_slice_pending_execution', 'execution_reviewed',
                                                     'source_reviewed_deferred', 'source_reviewed_excluded')):
            raise ValueError(f'invalid bounded owner record: {path}')
        review = owner.get('source_review')
        expected_identity = f'{SCRIPTS}/analyzer/{path.removesuffix(".barista")}.fs'
        if (not isinstance(review, dict)
                or not isinstance(review.get('advisory_report'), str)
                or Path(review['advisory_report']).name != review['advisory_report']
                or not isinstance(review.get('advisory_report_sha256'), str)
                or not re.fullmatch(r'[0-9a-f]{64}', review['advisory_report_sha256'])
                or not isinstance(review.get('assertion'), str) or not review['assertion'].strip()
                or not isinstance(review.get('final_disposition'), str) or not review['final_disposition'].strip()
                or review.get('identity') != expected_identity
                or not isinstance(review.get('scope_observation'), str) or not review['scope_observation'].strip()):
            raise ValueError(f'invalid owner source review: {path}')
        requires_execution = (owner['review_state'] in ('execution_reviewed', 'source_reviewed_excluded')
                              or (owner['review_state'] == 'source_reviewed_deferred' and path not in LATER))
        if requires_execution or 'execution_evidence' in owner:
            evidence = owner.get('execution_evidence')
            hashes = ('report_sha256', 'source_sha256', 'staged_source_sha256',
                      'expectation_sha256', 'expected_block_sha256', 'actual_block_sha256')
            if (not isinstance(evidence, dict)
                    or any(not isinstance(evidence.get(key), str)
                           or not re.fullmatch(r'[0-9a-f]{64}', evidence[key]) for key in hashes)
                    or not isinstance(evidence.get('revision'), str)
                    or not re.fullmatch(r'[0-9a-f]{40}', evidence['revision'])
                    or not isinstance(evidence.get('report'), str)
                    or Path(evidence['report']).name != evidence['report']
                    or evidence.get('terminal') not in ('passed', 'mismatch', 'crash', 'timeout', 'malformed_result', 'missing_guard', 'inconsistent_completion', 'infrastructure_error')
                    or type(evidence.get('guard')) is not bool
                    or (evidence['terminal'] in ('passed', 'mismatch') and not evidence['guard'])
                    or not isinstance(owner.get('code_symbols'), list) or not owner['code_symbols']):
                raise ValueError(f'invalid reviewed owner execution evidence: {path}')
    files = {}
    for scope in ['analyzer', 'parser']:
        root = scripts / scope
        for relative, kind in tree_entries(root).items():
            if kind == 'file':
                files[scope + '/' + relative] = (root / relative).read_bytes()
    for relative in SUPPORT:
        path = local_path(scripts, relative)
        if not path.is_file():
            raise ValueError(f'required support helper missing: {relative}')
        files[relative] = path.read_bytes()
    sources = sorted(path for path in files if path.startswith('analyzer/') and path.endswith('.fs'))
    if not sources:
        raise ValueError('empty analyzer inventory')
    mapping = path_map(files, uri)
    cases = set()
    helpers = set()
    category = {name: Counter() for name in ('errors', 'features', 'warnings')}
    statuses = Counter()
    for path in sources:
        relative = path.removeprefix('analyzer/')
        if not re.fullmatch(r'[A-Za-z0-9_./-]+', relative) or posixpath.normpath(relative) != relative:
            raise ValueError(f'unsafe source path: {path}')
        cat = relative.split('/')[0]
        if cat not in category:
            raise ValueError(f'unknown analyzer category: {path}')
        checked_text(files[path], path)
        category[cat]['sources'] += 1
        output = path[:-3] + '.out'
        if path.endswith('.notest.fs'):
            if output in files:
                raise ValueError(f'helper has paired output: {path}')
            helpers.add(barista_path(relative))
            category[cat]['helpers'] += 1
        else:
            if output not in files:
                raise ValueError(f'missing paired output: {path}')
            status = checked_text(files[output], output).split('\n')[0]
            if status not in STATUS:
                raise ValueError(f'unknown status {status}: {output}')
            if status in ('FS_TEST_COMPILER_ERROR', 'FS_TEST_RUNTIME_ERROR'):
                decode_expectation(files[output], output)
                if LATER.get(barista_path(relative)) != status or barista_path(relative) not in policy['deferred']:
                    raise ValueError(f'unreviewed later-stage output: {output}')
            else:
                extract_static_block(files[output], output)
            statuses[status] += 1
            cases.add(barista_path(relative))
            category[cat]['cases'] += 1
    for path in files:
        if path.startswith('analyzer/') and path.endswith('.out') and path[:-4] + '.fs' not in files:
            raise ValueError(f'orphan output: {path}')
    seen = set()
    triage = empty_triage()
    for disposition in triage:
        table = policy[disposition]
        if not isinstance(table, dict):
            raise ValueError(f'policy {disposition} must be a reason map')
        for path, reason in table.items():
            if path not in cases:
                raise ValueError(f'stale policy case: {path}')
            if path in seen:
                raise ValueError(f'overlapping policy: {path}')
            if not isinstance(reason, str) or not reason.strip():
                raise ValueError(f'empty policy reason: {path}')
            seen.add(path)
        triage[disposition] = table
    for path, owner in policy['owners'].items():
        expected_state = ('source_reviewed_deferred' if path in triage['deferred'] else
                          'source_reviewed_excluded' if path in triage['excluded'] else None)
        if expected_state is not None and owner['review_state'] != expected_state:
            raise ValueError(f'{expected_state.removeprefix("source_reviewed_")} policy requires matching owner state: {path}')
        if (expected_state is not None
                and (owner['reason'] != triage[expected_state.removeprefix('source_reviewed_')][path]
                     or owner['source_review']['final_disposition'] != triage[expected_state.removeprefix('source_reviewed_')][path])):
            raise ValueError(f'{expected_state.removeprefix("source_reviewed_")} disposition rationale mismatch: {path}')
        if expected_state is None and owner['review_state'] in ('source_reviewed_deferred', 'source_reviewed_excluded'):
            raise ValueError(f'orphan reviewed disposition owner: {path}')
    for disposition in ('deferred', 'excluded'):
        missing = set(triage[disposition]) - set(policy['owners'])
        if missing:
            raise ValueError(f'{disposition} policy requires owner records: {sorted(missing)[0]}')
    if set(policy['owners']) - cases:
        raise ValueError('stale owner policy')
    counts = {'sources': len(sources), 'cases': len(cases), 'helpers': len(helpers),
              'outputs': sum(statuses.values()), 'support_helpers': len(SUPPORT),
              'categories': {k: dict(v) for k, v in sorted(category.items())}, 'statuses': dict(sorted(statuses.items()))}
    if policy['counts'] is not None and counts != policy['counts']:
        raise ValueError(f'pinned inventory count drift: expected {policy["counts"]}, actual {counts}')
    if set(policy['source_edits']) - set(sources + SUPPORT):
        raise ValueError('stale source edit policy')
    imported_paths = {
        barista_path(path) if path in SHARED_SUPPORT_ROOTS else
        ('_support/' + barista_path(path) if path in SUPPORT else
         barista_path(path.removeprefix('analyzer/')))
        for path in sources + SUPPORT
    }
    clone_specs = {}
    for path in policy['source_edits']:
        data = files[path]
        source_policy_changes(data, path, policy)
        clone = projected_helper_clone(data, path, policy['source_edits'][path], imported_paths)
        if clone is not None:
            if clone['identity'] in {item['identity'] for item in clone_specs.values()}:
                raise ValueError(f'{path}: duplicate projected helper clone identity')
            clone_specs[path] = clone
            imported_paths.add(clone['target'])
        key = barista_path(path.removeprefix('analyzer/'))
        if key in cases and key not in triage['rewritten'] and key not in triage['expectation_overrides']:
            raise ValueError(f'{path}: source edit requires its disjoint rewrite or override disposition')
    if set(policy['expectation_edits']) - {p[:-3] + '.out' for p in sources if not p.endswith('.notest.fs')}:
        raise ValueError('stale expectation edit policy')
    for path in policy['expectation_edits']:
        key = barista_path(path.removeprefix('analyzer/')[:-4] + '.fs')
        if key not in triage['expectation_overrides']:
            raise ValueError(f'{path}: expectation edit requires its disjoint override disposition')
    records = []
    for path in sources + SUPPORT:
        data = files[path]
        source = checked_text(data, path)
        support = path in SUPPORT
        relative = barista_path(path) if path in SHARED_SUPPORT_ROOTS else ('_support/' + barista_path(path) if support else barista_path(path.removeprefix('analyzer/')))
        role = 'support_helper' if support else ('helper' if path.endswith('.notest.fs') else 'case')
        changes = []
        references = []
        for start, end, literal, kind in resource_spans(data, path):
            target = posixpath.normpath(literal.removeprefix('res://') if literal.startswith('res://')
                                       else posixpath.join(posixpath.dirname(path), literal))
            intentional = (path, literal, target) == MISSING
            if target not in mapping or (target not in files and not intentional):
                raise ValueError(f'{path}: required resource target missing: {target}')
            if intentional and target in files:
                raise ValueError(f'{path}: intentional missing target unexpectedly exists')
            if literal.startswith('res://'):
                after = mapping[target]
            elif target in SHARED_SUPPORT_ROOTS:
                after = mapping[target]
            else:
                # Keep relative spelling and dot segments exactly; only suffix changes.
                after = barista_path(literal)
                resolved = posixpath.normpath(posixpath.join(posixpath.dirname(mapping[path].removeprefix('res://')), after))
                if 'res://' + resolved != mapping[target]:
                    raise ValueError(f'{path}: relative relocation escaped mapped target: {literal}')
            references.append({'kind': kind, 'literal': literal, 'target': target,
                               'relocated': after, 'intentional_missing': intentional})
            changes.append(change(data, start, end, after, 'resource-literal', target=target))
        relocation_changes = list(changes)
        changes.extend(source_policy_changes(data, path, policy))
        record = source_record(data, path, relative, role, changes, references)
        if path in SHARED_SUPPORT_ROOTS:
            record['root'] = SHARED_SUPPORT_ROOTS[path]
        if role == 'case':
            output = path[:-3] + '.out'
            status = files[output].split(b'\n')[0].decode()
            disposition = next((key for key in triage if relative in triage[key]), 'imported')
            expectation_data = files[output]
            expectation_edits = []
            if output in policy['expectation_edits']:
                edit = policy['expectation_edits'][output]
                if set(edit) != {'sha256', 'patches'} or sha(expectation_data) != edit['sha256']:
                    raise ValueError(f'{output}: expectation edit hash preimage mismatch')
                expectation_edits = edit['patches']
                expectation_data = expectation_patch(expectation_data, expectation_edits, output)
            block, projections = ('', []) if status in ('FS_TEST_COMPILER_ERROR', 'FS_TEST_RUNTIME_ERROR') else expected_projection(extract_static_block(expectation_data, output), mapping, output)
            record.update({'expectation_identity': SCRIPTS + '/' + output, 'expectation_sha256': sha(files[output]),
                           'status': status, 'stage': 'analyzer', 'disposition': disposition,
                           'reason': triage.get(disposition, {}).get(relative, 'Discovery inclusion; final semantic eligibility remains unreviewed.'),
                           'expected_block': block, 'expectation_edits': expectation_edits, 'expectation_transformations': projections,
                           'candidate_observations': {'generic_syntax': bool(re.search(r'\b(?:class_name|trait_name|enum_name|class|func)\s+\w+\[', source)),
                                                      'owners': owner_candidates(source, block, references)},
                           'semantic_owner': policy['owners'].get(relative)})
        records.append(record)
        clone = clone_specs.get(path)
        if clone is not None:
            clone_changes = relocation_changes
            clone_changes.extend(clone['patches'])
            clone_record = source_record(data, path, clone['target'], 'helper', clone_changes, references)
            clone_record.update({
                'identity': clone['identity'],
                'projection': {
                    'kind': 'projected_helper_clone',
                    'provider_identity': SCRIPTS + '/' + path,
                    'provider_sha256': sha(data),
                    'transformed_sha256': clone['transformed_sha256'],
                },
            })
            records.append(clone_record)
    included = cases - set(triage['excluded']) - set(triage['deferred'])
    for record in records:
        if record.get('disposition') in ('excluded', 'deferred'):
            continue
        for reference in record['references']:
            target = reference['target']
            if (target.startswith('analyzer/') and not reference['intentional_missing']
                    and not target.endswith('.notest.fs')
                    and barista_path(target.removeprefix('analyzer/')) not in included):
                raise ValueError(f'{record["identity"]}: required paired dependency removed by policy: {target}')
    ledger = {'root': uri, 'foundry_revision': policy['foundry_revision'], 'upstream_total': len(cases),
              'upstream_helpers': len(helpers) + len(SUPPORT), 'upstream_sources': len(sources) + len(SUPPORT),
              'total': len(included),
              'skipped': len(helpers) + len(SUPPORT) - len(SHARED_SUPPORT_ROOTS) + len(clone_specs),
              'expected_failures': resolved_expected_failures(policy, execution_report, pinned_failures,
                                                              included, triage),
              'triage': triage}
    complaint = validate_triage_ledger('analyzer', ledger, disk_cases=included,
                                      disk_helpers=helpers | {'_support/' + barista_path(p) for p in SUPPORT if p not in SHARED_SUPPORT_ROOTS})
    if complaint:
        raise ValueError(complaint)
    return {'schema_version': 1, 'checkpoint': 'imported', 'imported': True,
            'foundry_revision': policy['foundry_revision'], 'root': uri, 'counts': counts,
            'policy_sha256': sha(encoded(policy)), 'ledger': ledger, 'sources': sorted(records, key=lambda r: r['identity'])}


def shared_source_path(record: dict, project_root: Path) -> Path | None:
    """Resolve only the admitted real shared helper; all other records stay corpus-local."""
    root = SHARED_SUPPORT_ROOTS.get(record['upstream_path'])
    if root is None:
        if 'root' in record:
            raise ValueError('unregistered external source root')
        return None
    if (record.get('root') != root or record.get('role') != 'support_helper'
            or record.get('imported_path') != barista_path(record['upstream_path'])):
        raise ValueError('invalid shared support identity/root/path')
    path = local_path(project_root, root.removeprefix('res://') + '/' + record['imported_path'])
    if not path.is_file() or sha(path.read_bytes()) != record['imported_sha256']:
        raise ValueError(f'shared support delivery missing or byte drift: {path}')
    return path


def generate(inv: dict, source: Path, destination: Path, *, project_root: Path | None = None):
    project_root = project_root or ROOT / 'project'
    cases, helpers = [], []
    for record in inv['sources']:
        if record.get('disposition') in ('deferred', 'excluded'):
            continue
        shared = shared_source_path(record, project_root)
        path = destination / record['imported_path']
        data = (source / record['upstream_path']).read_bytes()
        if sha(data) != record['sha256']:
            raise ValueError(f'source drift after inventory: {record["identity"]}')
        transformed = patch(data, record['transformations'], record['identity'])
        if sha(transformed) != record['imported_sha256']:
            raise ValueError(f'provenance drift: {record["identity"]}')
        if shared is not None:
            if shared.read_bytes() != transformed:
                raise ValueError(f'shared support differs from pinned source projection: {shared}')
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(transformed)
        if record['role'] == 'case':
            cases.append(record['imported_path'])
            path.with_suffix('.out').write_bytes((record['expected_block'] + '\n').encode())
        else:
            helpers.append(record['imported_path'])
    write_stages(destination, cases, helpers, inv['foundry_revision'], 'analyzer')
    (destination / 'inventory.json').write_bytes(encoded(inv))
    (destination / 'README.md').write_text(f'# Analyzer conformance corpus\n\nImported from Foundry. {len(cases)} static cases, {len(helpers)} helpers.\n'
        f'Foundry `{inv["foundry_revision"]}`. Full upstream inventory: {inv["counts"]["cases"]} cases, '
        f'{inv["counts"]["helpers"]} analyzer helpers; {inv["counts"]["support_helpers"]} support identities, '
        f'{len(SHARED_SUPPORT_ROOTS)} supplied by the existing external parser-owned delivery.\n'
        f'Generated helper projections: {sum("projection" in record for record in inv["sources"])}.\n'
        f'Residual failures pinned in `tests/corpus_baseline.json`: {len(inv["ledger"]["expected_failures"])}.\n'
        'Cases are supervised case-by-case by `scripts/run_corpus_triage.py`; every included dependency\n'
        'remains available. No script body executes.\n')


def write_tree(inv: dict, source: Path, destination: Path, *, project_root: Path | None = None):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        tree_entries(destination)
    with tempfile.TemporaryDirectory(prefix='.analyzer-import-', dir=destination.parent) as temporary:
        candidate = Path(temporary) / 'candidate'
        candidate.mkdir()
        generate(inv, source, candidate, project_root=project_root)
        previous = Path(temporary) / 'previous'
        if destination.exists():
            destination.rename(previous)
        try:
            candidate.rename(destination)
        except OSError:
            if previous.exists():
                previous.rename(destination)
            raise


def check_tree(inv: dict, source: Path, destination: Path, *, project_root: Path | None = None):
    with tempfile.TemporaryDirectory(prefix='analyzer-check-') as temporary:
        fresh = Path(temporary)
        generate(inv, source, fresh, project_root=project_root)
        expected, actual = tree_entries(fresh), tree_entries(destination)
        if expected != actual:
            raise ValueError('imported population/tree drift')
        for path, kind in expected.items():
            if kind == 'file' and (fresh / path).read_bytes() != (destination / path).read_bytes():
                raise ValueError(f'imported byte drift: {path}')


def import_destination(root: Path, value: str) -> Path:
    if value != DESTINATION:
        raise ValueError(f'only the registered destination {DESTINATION} is allowed')
    destination = local_path(root, value)
    if destination.exists():
        tree_entries(destination)
    return destination


def baseline_entry(ledger: dict) -> dict:
    """The committed ledger entry for the imported analyzer corpus."""
    return {'imported': True, 'root': ledger['root'], 'total': ledger['total'], 'skipped': ledger['skipped'],
            'expected_failures': ledger['expected_failures'], 'foundry_revision': ledger['foundry_revision'],
            'upstream_total': ledger['upstream_total'], 'upstream_helpers': ledger['upstream_helpers'],
            'upstream_sources': ledger['upstream_sources'], 'triage': ledger['triage']}


def committed_expected_failures(baseline_path: Path) -> list[str]:
    """The pin as committed, so --check regenerates the very tree the repository claims."""
    corpora = read_json(baseline_path).get('corpora')
    if not isinstance(corpora, dict) or not isinstance(corpora.get('analyzer'), dict):
        raise ValueError(f'{baseline_path}: missing analyzer ledger entry')
    failures = corpora['analyzer'].get('expected_failures')
    if not isinstance(failures, list):
        raise ValueError(f'{baseline_path}: analyzer expected_failures must be a list')
    return failures


def check_ledger(ledger: dict, baseline_path: Path) -> None:
    """The committed entry must be exactly the ledger a regeneration produces."""
    corpora = read_json(baseline_path).get('corpora')
    committed = corpora['analyzer'] if isinstance(corpora, dict) else None
    expected = baseline_entry(ledger)
    if committed != expected:
        differing = sorted(key for key in set(expected) | set(committed or {})
                           if (committed or {}).get(key) != expected.get(key))
        raise ValueError(f'{baseline_path}: committed analyzer ledger differs from the regenerated one: '
                         + ', '.join(differing))


def publish_baseline(ledger: dict, baseline_path: Path) -> None:
    document = read_json(baseline_path)
    document['corpora']['analyzer'] = baseline_entry(ledger)
    temporary = baseline_path.with_name(baseline_path.name + '.candidate')
    # Same encoding as scripts/import_parser_corpus.py writes, so either importer
    # rewriting this file leaves the other's entry byte-identical.
    temporary.write_text(json.dumps(document, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, baseline_path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--foundry', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument('--inventory', type=Path)
    modes.add_argument('--write-tree')
    modes.add_argument('--check', action='store_true')
    parser.add_argument('--execution-report', type=Path,
                        help='completed triage report whose failures become the ledger expected_failures pin')
    args = parser.parse_args(argv)
    try:
        # Writing the tree or an external inventory is the one moment the destination may
        # legitimately be absent or stale; every other mode validates it as committed.
        rebuilding = 'analyzer' if args.write_tree or args.inventory else None
        registry = validate_registration(ROOT, rebuilding=rebuilding)
        verify_checkout(args.foundry, registry, args.revision)
        if registry.get('auxiliary_sources') != sorted(SCRIPTS + '/' + p for p in SUPPORT):
            raise ValueError('registered auxiliary sources do not match the analyzer support requirement')
        policy = default_policy()
        if policy['counts'] is None:
            raise ValueError('production policy must retain complete pinned inventory counts')
        if policy['foundry_revision'] != registry['revision']:
            raise ValueError('policy revision differs from shared registry')
        destination = import_destination(ROOT, args.write_tree or DESTINATION)
        uri = 'res://' + DESTINATION.removeprefix('project/')
        execution_report = None
        pinned_failures = None
        if args.execution_report is not None:
            if args.check:
                raise ValueError('--check reads the committed pin; it may not derive a new one from a run')
            execution_report = read_json(args.execution_report)
        else:
            # Only a completed run may change the pin. Without one, both checking and
            # regenerating carry the committed pin forward: --check can then byte-compare
            # it along with the case files, and a rebuild cannot silently drop it.
            pinned_failures = committed_expected_failures(BASELINE)
        inv = inventory_sources(args.foundry / SCRIPTS, policy, uri, execution_report, pinned_failures)
        if args.inventory:
            # Inventory is metadata-only; forbid a caller using it to corrupt
            # registered generated inputs or any repository-owned source file.
            if args.inventory.resolve().is_relative_to(ROOT) or args.inventory.resolve().is_relative_to(args.foundry.resolve()):
                raise ValueError('inventory report must be outside repository and source checkout')
            args.inventory.write_bytes(encoded(inv))
        elif args.write_tree:
            write_tree(inv, args.foundry / SCRIPTS, destination)
            publish_baseline(inv['ledger'], BASELINE)
        else:
            # An absent destination is drift, not a vacuous pass: the registry records this
            # corpus as imported, so there is a tree to compare against or the check failed.
            if not destination.is_dir():
                raise ValueError(f'imported corpus tree is missing: {destination}')
            check_tree(inv, args.foundry / SCRIPTS, destination)
            check_ledger(inv['ledger'], BASELINE)
        print(f'analyzer corpus: {inv["counts"]["sources"]} sources, {inv["ledger"]["total"]} imported cases, '
              f'{inv["ledger"]["skipped"]} helpers, {len(inv["ledger"]["expected_failures"])} pinned residual failures')
        return 0
    except (ValueError, OSError) as error:
        print(f'analyzer corpus: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
