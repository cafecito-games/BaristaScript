#!/usr/bin/env python3
# import_analyzer_corpus.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Discover the pinned analyzer corpus; staging is never final promotion.

The inventory retains every source before policy subtraction. All semantic
scope observations are candidates until reviewed promotion; observed failures
never change expectations or dispositions.
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
STAGE = 'project/tests/corpus_staging/analyzer'
POLICY = ROOT / 'scripts/analyzer_corpus_policy.json'
STATUS = {'FS_TEST_OK', 'FS_TEST_ANALYZER_ERROR', 'FS_TEST_PARSER_ERROR', 'FS_TEST_COMPILER_ERROR', 'FS_TEST_RUNTIME_ERROR'}
LATER = {
    'errors/enum_name_bare_payload_case_in_own_body.barista': 'FS_TEST_COMPILER_ERROR',
    'errors/generic_trait_use_missing_type_arguments_dependency.barista': 'FS_TEST_COMPILER_ERROR',
    'features/typed_callable_bind_gapped_arity_rpc_id.barista': 'FS_TEST_RUNTIME_ERROR',
    'features/typed_callable_unbind_gapped_arity_rpc_id.barista': 'FS_TEST_RUNTIME_ERROR',
}
SUPPORT = ['utils.notest.fs', 'runtime/features/rtc_runtime_self_adoptable.notest.fs']
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


def patch(data: bytes, changes: list[dict], path: str) -> bytes:
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
                or item['before'].count('\n') != item['after'].count('\n')):
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


def change(data, start, end, after, rule, **extra):
    return {'start': start, 'end': end, 'line': data[:start].count(b'\n') + 1,
            'before': data[start:end].decode('utf-8'), 'after': after,
            'rule': rule, 'occurrences': 1, **extra}


def path_map(files: dict[str, bytes], uri: str):
    mapping = {}
    for path in sorted(files):
        if not path.endswith('.fs'):
            continue
        if path.startswith('analyzer/'):
            mapping[path] = uri + '/' + barista_path(path.removeprefix('analyzer/'))
        elif path.startswith('parser/'):
            mapping[path] = 'res://tests/corpus/parser/' + barista_path(path.removeprefix('parser/'))
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


def inventory_sources(scripts: Path, policy: dict, uri: str) -> dict:
    if set(policy) != {'schema_version', 'foundry_revision', 'counts', 'excluded', 'rewritten',
                       'expectation_overrides', 'deferred', 'source_edits', 'expectation_edits', 'owners'} or type(policy['schema_version']) is not int or policy['schema_version'] != 1:
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
                or owner.get('review_state') not in ('source_slice_pending_execution', 'execution_reviewed', 'source_reviewed_deferred')):
            raise ValueError(f'invalid bounded owner record: {path}')
        if owner['review_state'] == 'source_reviewed_deferred' and path not in policy['deferred']:
            raise ValueError(f'invalid deferred owner: {path}')
        if owner['review_state'] == 'execution_reviewed':
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
                    or evidence.get('terminal') not in ('passed', 'mismatch', 'crash', 'timeout', 'malformed_result', 'missing_guard', 'missing_summary', 'infrastructure_error')
                    or type(evidence.get('guard')) is not bool
                    or (evidence['terminal'] in ('passed', 'mismatch') and not evidence['guard'])
                    or not isinstance(owner.get('source_review'), dict)
                    or not owner['source_review'].get('assertion')
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
    if set(policy['owners']) - cases:
        raise ValueError('stale owner policy')
    counts = {'sources': len(sources), 'cases': len(cases), 'helpers': len(helpers),
              'outputs': sum(statuses.values()), 'support_helpers': len(SUPPORT),
              'categories': {k: dict(v) for k, v in sorted(category.items())}, 'statuses': dict(sorted(statuses.items()))}
    if policy['counts'] is not None and counts != policy['counts']:
        raise ValueError(f'pinned inventory count drift: expected {policy["counts"]}, actual {counts}')
    if set(policy['source_edits']) - set(sources + SUPPORT):
        raise ValueError('stale source edit policy')
    for path in policy['source_edits']:
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
        relative = '_support/' + barista_path(path) if support else barista_path(path.removeprefix('analyzer/'))
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
            else:
                # Keep relative spelling and dot segments exactly; only suffix changes.
                after = barista_path(literal)
                resolved = posixpath.normpath(posixpath.join(posixpath.dirname(mapping[path].removeprefix('res://')), after))
                if 'res://' + resolved != mapping[target]:
                    raise ValueError(f'{path}: relative relocation escaped mapped target: {literal}')
            references.append({'kind': kind, 'literal': literal, 'target': target,
                               'relocated': after, 'intentional_missing': intentional})
            changes.append(change(data, start, end, after, 'resource-literal', target=target))
        if path in policy['source_edits']:
            edits = policy['source_edits'][path]
            if set(edits) != {'sha256', 'patches'} or edits['sha256'] != sha(data):
                raise ValueError(f'{path}: source edit hash preimage mismatch')
            changes.extend(edits['patches'])
        transformed = patch(data, changes, path)
        record = {'upstream_path': path, 'identity': SCRIPTS + '/' + path,
                  'sha256': sha(data), 'git_blob': hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest(),
                  'role': role, 'imported_path': relative, 'imported_sha256': sha(transformed),
                  'transformations': sorted(changes, key=lambda p: p['start']), 'references': references}
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
                expectation_data = patch(expectation_data, expectation_edits, output)
            block, projections = ('', []) if status in ('FS_TEST_COMPILER_ERROR', 'FS_TEST_RUNTIME_ERROR') else expected_projection(extract_static_block(expectation_data, output), mapping, output)
            record.update({'expectation_identity': SCRIPTS + '/' + output, 'expectation_sha256': sha(files[output]),
                           'status': status, 'stage': 'analyzer', 'disposition': disposition,
                           'reason': triage.get(disposition, {}).get(relative, 'Discovery inclusion; final semantic eligibility remains unreviewed.'),
                           'expected_block': block, 'expectation_edits': expectation_edits, 'expectation_transformations': projections,
                           'candidate_observations': {'generic_syntax': bool(re.search(r'\b(?:class_name|trait_name|enum_name|class|func)\s+\w+\[', source)),
                                                      'owners': owner_candidates(source, block, references)},
                           'semantic_owner': policy['owners'].get(relative)})
        records.append(record)
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
              'total': len(included), 'skipped': len(helpers) + len(SUPPORT), 'expected_failures': [], 'triage': triage}
    complaint = validate_triage_ledger('analyzer staging', ledger, disk_cases=included,
                                      disk_helpers=helpers | {'_support/' + barista_path(p) for p in SUPPORT})
    if complaint:
        raise ValueError(complaint)
    return {'schema_version': 1, 'checkpoint': 'discovery', 'imported': False,
            'foundry_revision': policy['foundry_revision'], 'root': uri, 'counts': counts,
            'policy_sha256': sha(encoded(policy)), 'ledger': ledger, 'sources': sorted(records, key=lambda r: r['identity'])}


def generate(inv: dict, source: Path, destination: Path):
    cases, helpers = [], []
    for record in inv['sources']:
        if record.get('disposition') in ('deferred', 'excluded'):
            continue
        path = destination / record['imported_path']
        path.parent.mkdir(parents=True, exist_ok=True)
        data = (source / record['upstream_path']).read_bytes()
        if sha(data) != record['sha256']:
            raise ValueError(f'source drift after inventory: {record["identity"]}')
        transformed = patch(data, record['transformations'], record['identity'])
        if sha(transformed) != record['imported_sha256']:
            raise ValueError(f'provenance drift: {record["identity"]}')
        path.write_bytes(transformed)
        if record['role'] == 'case':
            cases.append(record['imported_path'])
            path.with_suffix('.out').write_bytes((record['expected_block'] + '\n').encode())
        else:
            helpers.append(record['imported_path'])
    write_stages(destination, cases, helpers, inv['foundry_revision'], 'analyzer')
    (destination / 'inventory.json').write_bytes(encoded(inv))
    (destination / 'README.md').write_text(f'# Analyzer discovery staging\n\nPending final promotion. {len(cases)} static cases, {len(helpers)} helpers.\n'
        f'Foundry `{inv["foundry_revision"]}`. Full upstream inventory: {inv["counts"]["cases"]} cases, '
        f'{inv["counts"]["helpers"]} analyzer helpers; {inv["counts"]["support_helpers"]} additional support helpers.\n'
        'Execution selects cases; every included dependency remains available. No script body executes.\n')


def write_stage(inv: dict, source: Path, destination: Path):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        tree_entries(destination)
    with tempfile.TemporaryDirectory(prefix='.analyzer-stage-', dir=destination.parent) as temporary:
        candidate = Path(temporary) / 'candidate'
        candidate.mkdir()
        generate(inv, source, candidate)
        previous = Path(temporary) / 'previous'
        if destination.exists():
            destination.rename(previous)
        try:
            candidate.rename(destination)
        except OSError:
            if previous.exists():
                previous.rename(destination)
            raise


def check_stage(inv: dict, source: Path, destination: Path):
    with tempfile.TemporaryDirectory(prefix='analyzer-check-') as temporary:
        fresh = Path(temporary)
        generate(inv, source, fresh)
        expected, actual = tree_entries(fresh), tree_entries(destination)
        if expected != actual:
            raise ValueError('staging population/tree drift')
        for path, kind in expected.items():
            if kind == 'file' and (fresh / path).read_bytes() != (destination / path).read_bytes():
                raise ValueError(f'staging byte drift: {path}')


def stage_destination(root: Path, value: str) -> Path:
    if value != STAGE:
        raise ValueError(f'only importer-owned staging destination {STAGE} is allowed')
    destination = local_path(root, value)
    if destination.exists():
        tree_entries(destination)
    return destination


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--foundry', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument('--inventory', type=Path)
    modes.add_argument('--stage')
    modes.add_argument('--check', action='store_true')
    args = parser.parse_args(argv)
    try:
        registry = validate_registration(ROOT)
        verify_checkout(args.foundry, registry, args.revision)
        if registry.get('auxiliary_sources') != sorted(SCRIPTS + '/' + p for p in SUPPORT):
            raise ValueError('registered auxiliary sources do not match the analyzer support requirement')
        policy = default_policy()
        if policy['counts'] is None:
            raise ValueError('production policy must retain complete pinned inventory counts')
        if policy['foundry_revision'] != registry['revision']:
            raise ValueError('policy revision differs from shared registry')
        destination = stage_destination(ROOT, args.stage or STAGE)
        uri = 'res://' + STAGE.removeprefix('project/')
        inv = inventory_sources(args.foundry / SCRIPTS, policy, uri)
        if args.inventory:
            # Inventory is metadata-only; forbid a caller using it to corrupt
            # registered generated inputs or any repository-owned source file.
            if args.inventory.resolve().is_relative_to(ROOT) or args.inventory.resolve().is_relative_to(args.foundry.resolve()):
                raise ValueError('inventory report must be outside repository and source checkout')
            args.inventory.write_bytes(encoded(inv))
        elif args.stage:
            write_stage(inv, args.foundry / SCRIPTS, destination)
        elif destination.exists():
            check_stage(inv, args.foundry / SCRIPTS, destination)
        print(f'analyzer discovery: {inv["counts"]["sources"]} sources, {inv["counts"]["cases"]} cases, '
              f'{inv["counts"]["helpers"]} helpers + {inv["counts"]["support_helpers"]} support; pending imported=false')
        return 0
    except (ValueError, OSError) as error:
        print(f'analyzer discovery: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
