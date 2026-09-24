#!/usr/bin/env python3
# import_runtime_corpus.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Import the pinned runtime corpus into its registered destination.

The corpus lands before anything can run it. Nothing executes here, so no case can
pass yet and the ledger pins every included case as an expected failure, each owned by
the child that has to land before the case can leave the pin. The pin is monotone from
that first commit onward: it may shrink, never grow.

Dispositions come from the pinned policy, never from an observed run. The residual
pin comes either from a completed execution report or from the committed ledger, and
both routes go through the same ownership validation the analyzer importer uses, so a
hand-edited baseline is held to exactly what an execution-derived one had to satisfy.

A runtime expectation is the complete transcript the upstream producer emits, not a
projection of it; ``scripts/runtime_expectations.py`` is its single definition.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import posixpath
import re
import sys
import tempfile

from corpus_ledger import barista_path, empty_triage, validate_triage_ledger
from corpus_registry import ROOT, local_path, read_json, tree_entries, validate_registration, verify_checkout
from corpus_stages import write_stages
from runtime_expectations import extract_transcript, relocated_identity, transcript_identities

# Reused rather than reimplemented: provenance-checked patching, the source record
# shape and, most importantly, the pin's ownership validation and its derivation from a
# completed run. A looser second copy of those would let a runtime failure enter the pin
# without anyone having written down why it fails.
from import_analyzer_corpus import (
    change,
    checked_text,
    derive_expected_failures,
    encoded,
    expectation_patch,
    patch,
    resource_spans,
    sha,
    source_policy_changes,
    source_record,
    validate_expected_failures,
)

SCRIPTS = 'modules/foundry_script/tests/scripts'
SCOPE = 'runtime'
DESTINATION = 'project/tests/corpus/runtime'
BASELINE = ROOT / 'tests/corpus_baseline.json'
POLICY = ROOT / 'scripts/runtime_corpus_policy.json'

# Corpora whose imported trees a runtime case may reference. The analyzer importer owns
# every byte under its own destination; this importer only resolves identities into it
# and checks the delivery is there.
EXTERNAL_ROOTS = {'analyzer': 'res://tests/corpus/analyzer'}

# The 758th expectation belongs to a helper, not to a case. An upstream C++ suite drives
# it; #248 owns the reload-cancellation behaviour it asserts. It must never become a
# runnable case, and it may not be dropped without a written reason either, so it is
# declared here, required by the policy, and recorded in the generated inventory.
ORPHAN_EXPECTATION = 'runtime/errors/reload_suspended_function.out'
ORPHAN_HELPER = 'runtime/errors/reload_suspended_function.notest.fs'

# Counted at the pinned revision. Drift in any of the four aborts before any final
# output is touched.
CENSUS = {'sources': 839, 'helpers': 82, 'cases': 757, 'outputs': 758}

# Every top-level upstream directory a case may live in. Their individual case counts
# are part of the pinned inventory counts, so a directory losing cases is count drift.
CATEGORIES = ('enum_host_functions_namespaced', 'errors', 'features', 'namespaced_native',
              'namespaced_script')

# The M4 children plus the epic. A pin entry names the child that owns it; a
# disposition names the child or milestone issue that owns the removed behaviour.
OWNER_ISSUES = (38, 240, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254)
REVIEW_STATES = ('pinned_awaiting_runtime', 'source_reviewed_deferred', 'source_reviewed_excluded')

# The shape every pinned reason takes, so the pin reads as a burndown board: which child
# owns what is left is legible from the reason alone.
PIN_REASON = re.compile(r'#(\d+): no runtime yet — .+')

# scripts/corpus_sources.json listed two runtime-tree helper identities as auxiliary
# sources before this corpus existed. Registering the runtime source root subsumes one
# of them: the whole tree is now byte-verified against the pin rather than that single
# file. The other stays auxiliary because it sits outside every corpus root. The policy
# states which mechanism owns each, and this importer checks the registry agrees.
AUXILIARY_IN_CORPUS = SCRIPTS + '/runtime/features/rtc_runtime_self_adoptable.notest.fs'
AUXILIARY_REGISTERED = SCRIPTS + '/utils.notest.fs'

POLICY_KEYS = {'schema_version', 'foundry_revision', 'counts', 'excluded', 'rewritten',
               'expectation_overrides', 'deferred', 'source_edits', 'expectation_edits',
               'owners', 'orphan_expectations', 'auxiliary_reconciliation'}


def default_policy():
    return read_json(POLICY)


def validate_policy(policy: dict) -> None:
    """Schema-check the pinned policy before it can decide anything."""
    if set(policy) != POLICY_KEYS or type(policy['schema_version']) is not int or policy['schema_version'] != 1:
        raise ValueError('invalid runtime policy schema')
    for field in ('excluded', 'rewritten', 'expectation_overrides', 'deferred', 'source_edits',
                  'expectation_edits', 'owners', 'orphan_expectations', 'auxiliary_reconciliation'):
        if not isinstance(policy[field], dict):
            raise ValueError(f'invalid runtime policy map: {field}')
    if not isinstance(policy['foundry_revision'], str) or not re.fullmatch(r'[0-9a-f]{40}', policy['foundry_revision']):
        raise ValueError('invalid runtime policy revision')
    if set(policy['orphan_expectations']) != {ORPHAN_EXPECTATION}:
        raise ValueError(f'the policy must declare exactly the orphan expectation {ORPHAN_EXPECTATION}')
    if set(policy['auxiliary_reconciliation']) != {AUXILIARY_IN_CORPUS, AUXILIARY_REGISTERED}:
        raise ValueError('the policy must reconcile exactly the two registered auxiliary helper identities')
    for field in ('orphan_expectations', 'auxiliary_reconciliation'):
        for key, reason in policy[field].items():
            if not isinstance(reason, str) or not reason.strip():
                raise ValueError(f'empty runtime policy reason: {field}/{key}')


def validate_owner(path: str, owner, triage: dict) -> None:
    """One owner record: who owns this case, and on what evidence."""
    if (not isinstance(owner, dict)
            or set(owner) != {'primary_issue', 'family', 'reason', 'prerequisites', 'review_state', 'source_review'}
            or owner['primary_issue'] not in OWNER_ISSUES
            or not isinstance(owner.get('family'), str) or not owner['family'].strip()
            or not isinstance(owner.get('reason'), str) or not owner['reason'].strip()
            or not isinstance(owner.get('prerequisites'), list)
            or any(type(number) is not int or number not in OWNER_ISSUES for number in owner['prerequisites'])
            or len(set(owner['prerequisites'])) != len(owner['prerequisites'])
            or owner['primary_issue'] in owner['prerequisites']
            or owner['review_state'] not in REVIEW_STATES):
        raise ValueError(f'invalid runtime owner record: {path}')
    review = owner['source_review']
    identity = f'{SCRIPTS}/{SCOPE}/{path.removesuffix(".barista")}.fs'
    if (not isinstance(review, dict)
            or set(review) != {'identity', 'signals', 'assertion', 'final_disposition'}
            or review['identity'] != identity
            or not isinstance(review['signals'], list) or not review['signals']
            or any(not isinstance(signal, str) or not signal.strip() for signal in review['signals'])
            or not isinstance(review['assertion'], str) or not review['assertion'].strip()
            or not isinstance(review['final_disposition'], str) or not review['final_disposition'].strip()):
        raise ValueError(f'invalid runtime owner source review: {path}')
    expected_state = ('source_reviewed_deferred' if path in triage['deferred'] else
                      'source_reviewed_excluded' if path in triage['excluded'] else
                      'pinned_awaiting_runtime')
    if owner['review_state'] != expected_state:
        raise ValueError(f'owner review state must match its disposition: {path}')
    if expected_state == 'pinned_awaiting_runtime':
        match = PIN_REASON.fullmatch(owner['reason'])
        if match is None or int(match[1]) != owner['primary_issue']:
            raise ValueError(f'a pinned case needs a reason naming its owning child: {path}')
        if owner['family'] not in owner['reason']:
            raise ValueError(f'a pinned reason must name its family: {path}')
        return
    disposition = triage['deferred' if path in triage['deferred'] else 'excluded'][path]
    if owner['reason'] != disposition or review['final_disposition'] != disposition:
        raise ValueError(f'reviewed disposition rationale mismatch: {path}')


def path_map(files: dict[str, bytes], uri: str) -> dict[str, str]:
    """Upstream ``.fs`` identity -> imported resource URI, for every readable scope."""
    mapping = {}
    for path in sorted(files):
        if not path.endswith('.fs'):
            continue
        scope, _, relative = path.partition('/')
        if scope == SCOPE:
            mapping[path] = uri + '/' + barista_path(relative)
        elif scope in EXTERNAL_ROOTS:
            mapping[path] = EXTERNAL_ROOTS[scope] + '/' + barista_path(relative)
    return mapping


def transcript_projection(block: str, mapping: dict, basenames: set, path: str):
    """Relocate every resource identity in one transcript against a validated preimage.

    This is the only transformation an imported transcript undergoes. Wording that
    differs between the Foundry fork and stock 4.7 is not handled here: it goes through
    the policy's expectation_overrides channel with a path-specific reason, because a
    silent normalization here would agree with any text at all.
    """
    data = block.encode('utf-8')
    changes = []
    for start, end, identity in transcript_identities(block):
        after = relocated_identity(identity, mapping, basenames, path)
        changes.append(change(data, start, end, after, 'transcript-resource-identity'))
    return patch(data, changes, path).decode(), changes


def established_expected_failures(policy, execution_report, pinned_failures, included, triage) -> list[str]:
    """The ledger pin: derived from a completed run, read back from a commit, or established.

    Establishment happens exactly once, when the ledger carries no runtime entry yet. No
    virtual machine exists at that moment, so a completed run is impossible and every
    included case fails for the same reason; the pin is therefore the whole included
    population, each entry carrying the owner that has to land before it can leave. From
    then on the committed pin is what a regeneration reproduces, a completed run is the
    only thing that may change it, and it may only ever shrink.
    """
    if execution_report is not None and pinned_failures is not None:
        raise ValueError('a pinned ledger comes from one source: a run or a commit, never both')
    if execution_report is not None:
        return derive_expected_failures(policy, execution_report, included, triage)
    if pinned_failures is not None:
        return validate_expected_failures(policy, pinned_failures, included, triage)
    return validate_expected_failures(policy, sorted(included), included, triage)


def basename(path: str) -> str:
    return path.rsplit('/', 1)[-1]


def external_delivery(uri: str, project_root: Path) -> Path:
    """Resolve an imported resource another corpus owns, and require it be delivered."""
    for scope, root in EXTERNAL_ROOTS.items():
        if uri.startswith(root + '/'):
            path = local_path(project_root, uri.removeprefix('res://'))
            if not path.is_file():
                raise ValueError(f'required {scope} corpus delivery is missing: {path}')
            if not path.name.endswith('.notest.barista'):
                raise ValueError(f'a runtime case may only depend on an external helper: {path}')
            return path
    raise ValueError(f'unregistered external resource root: {uri}')


def read_sources(scripts: Path) -> dict[str, bytes]:
    """Every file of the runtime tree, plus the corpora its cases may reference."""
    files = {}
    for scope in [SCOPE, *sorted(EXTERNAL_ROOTS)]:
        root = local_path(scripts, scope)
        if not root.is_dir():
            raise ValueError(f'required source root missing: {root}')
        for relative, kind in tree_entries(root).items():
            if kind == 'file':
                files[scope + '/' + relative] = (root / relative).read_bytes()
    return files


def inventory_sources(scripts: Path, policy: dict, uri: str, execution_report: dict | None = None,
                      pinned_failures: list[str] | None = None, *, project_root: Path | None = None,
                      census: dict | None = None) -> dict:
    project_root = project_root or ROOT / 'project'
    census = census or CENSUS
    validate_policy(policy)
    files = read_sources(scripts)
    sources = sorted(path for path in files if path.startswith(SCOPE + '/') and path.endswith('.fs'))
    if not sources:
        raise ValueError('empty runtime inventory')
    mapping = path_map(files, uri)
    basenames = {basename(path) for path in mapping}
    cases: set[str] = set()
    helpers: set[str] = set()
    category = {name: Counter() for name in CATEGORIES}
    statuses = Counter()
    for path in sources:
        relative = path.removeprefix(SCOPE + '/')
        if not re.fullmatch(r'[A-Za-z0-9_./-]+', relative) or posixpath.normpath(relative) != relative:
            raise ValueError(f'unsafe source path: {path}')
        name = relative.split('/')[0]
        if name not in category:
            raise ValueError(f'unknown runtime category: {path}')
        checked_text(files[path], path)
        category[name]['sources'] += 1
        output = path[:-3] + '.out'
        if path.endswith('.notest.fs'):
            if output in files:
                raise ValueError(f'helper has paired output: {path}')
            helpers.add(barista_path(relative))
            category[name]['helpers'] += 1
        else:
            if output not in files:
                raise ValueError(f'missing paired output: {path}')
            status, _ = extract_transcript(files[output], output)
            statuses[status] += 1
            cases.add(barista_path(relative))
            category[name]['cases'] += 1
    orphans = sorted(path for path in files
                     if path.startswith(SCOPE + '/') and path.endswith('.out') and path[:-4] + '.fs' not in files)
    if orphans != sorted(policy['orphan_expectations']):
        raise ValueError(f'orphan expectations differ from the reviewed declaration: {orphans}')
    if ORPHAN_HELPER not in files:
        raise ValueError(f'the declared orphan expectation has no helper provider: {ORPHAN_HELPER}')
    counts = {'sources': len(sources), 'cases': len(cases), 'helpers': len(helpers),
              'outputs': sum(statuses.values()) + len(orphans),
              'categories': {name: dict(counter) for name, counter in sorted(category.items())},
              'statuses': dict(sorted(statuses.items()))}
    observed = {'sources': counts['sources'], 'helpers': counts['helpers'],
                'cases': counts['cases'], 'outputs': counts['outputs']}
    if observed != census:
        raise ValueError(f'upstream census drift: expected {census}, actual {observed}')
    if policy['counts'] is not None and counts != policy['counts']:
        raise ValueError(f'pinned inventory count drift: expected {policy["counts"]}, actual {counts}')
    seen = set()
    triage = empty_triage()
    for disposition in triage:
        table = policy[disposition]
        for path, reason in table.items():
            if path not in cases:
                raise ValueError(f'stale policy case: {path}')
            if path in seen:
                raise ValueError(f'overlapping policy: {path}')
            if not isinstance(reason, str) or not reason.strip():
                raise ValueError(f'empty policy reason: {path}')
            seen.add(path)
        triage[disposition] = table
    if set(policy['owners']) != cases:
        raise ValueError('every runnable case requires exactly one owner record')
    for path in sorted(policy['owners']):
        validate_owner(path, policy['owners'][path], triage)
    if set(policy['source_edits']) - set(sources):
        raise ValueError('stale source edit policy')
    for path in policy['source_edits']:
        key = barista_path(path.removeprefix(SCOPE + '/'))
        if key in cases and key not in triage['rewritten'] and key not in triage['expectation_overrides']:
            raise ValueError(f'{path}: source edit requires its disjoint rewrite or override disposition')
    if set(policy['expectation_edits']) - {path[:-3] + '.out' for path in sources if not path.endswith('.notest.fs')}:
        raise ValueError('stale expectation edit policy')
    for path in policy['expectation_edits']:
        key = barista_path(path.removeprefix(SCOPE + '/')[:-4] + '.fs')
        if key not in triage['expectation_overrides']:
            raise ValueError(f'{path}: expectation edit requires its disjoint override disposition')
    records = []
    for path in sources:
        data = files[path]
        relative = barista_path(path.removeprefix(SCOPE + '/'))
        role = 'helper' if path.endswith('.notest.fs') else 'case'
        changes = []
        references = []
        for start, end, literal, kind in resource_spans(data, path):
            target = posixpath.normpath(literal.removeprefix('res://') if literal.startswith('res://')
                                        else posixpath.join(posixpath.dirname(path), literal))
            if target not in mapping or target not in files:
                raise ValueError(f'{path}: required resource target missing: {target}')
            if literal.startswith('res://'):
                after = mapping[target]
            else:
                # Keep relative spelling and dot segments exactly; only the suffix changes.
                after = barista_path(literal)
                resolved = posixpath.normpath(posixpath.join(
                    posixpath.dirname(mapping[path].removeprefix('res://')), after))
                if 'res://' + resolved != mapping[target]:
                    raise ValueError(f'{path}: relative relocation escaped mapped target: {literal}')
            if not target.startswith(SCOPE + '/'):
                external_delivery(mapping[target], project_root)
            references.append({'kind': kind, 'literal': literal, 'target': target, 'relocated': after})
            changes.append(change(data, start, end, after, 'resource-literal', target=target))
        changes.extend(source_policy_changes(data, path, policy))
        record = source_record(data, path, relative, role, changes, references)
        if role == 'case':
            output = path[:-3] + '.out'
            expectation_data = files[output]
            expectation_edits = []
            if output in policy['expectation_edits']:
                edit = policy['expectation_edits'][output]
                if set(edit) != {'sha256', 'patches'} or sha(expectation_data) != edit['sha256']:
                    raise ValueError(f'{output}: expectation edit hash preimage mismatch')
                expectation_edits = edit['patches']
                expectation_data = expectation_patch(expectation_data, expectation_edits, output)
            status, transcript = extract_transcript(expectation_data, output)
            block, projections = transcript_projection(transcript, mapping, basenames, output)
            disposition = next((key for key in triage if relative in triage[key]), 'imported')
            record.update({'expectation_identity': SCRIPTS + '/' + output, 'expectation_sha256': sha(files[output]),
                           'status': status, 'stage': SCOPE, 'disposition': disposition,
                           'reason': triage.get(disposition, {}).get(relative, policy['owners'][relative]['reason']),
                           'expected_block': block, 'expectation_edits': expectation_edits,
                           'expectation_transformations': projections,
                           'semantic_owner': policy['owners'][relative]})
        records.append(record)
    included = cases - set(triage['excluded']) - set(triage['deferred'])
    for record in records:
        if record.get('disposition') in ('excluded', 'deferred'):
            continue
        for reference in record['references']:
            target = reference['target']
            if (target.startswith(SCOPE + '/') and not target.endswith('.notest.fs')
                    and barista_path(target.removeprefix(SCOPE + '/')) not in included):
                raise ValueError(f'{record["identity"]}: required paired dependency removed by policy: {target}')
    orphan_records = []
    for output in orphans:
        status, transcript = extract_transcript(files[output], output)
        block, projections = transcript_projection(transcript, mapping, basenames, output)
        orphan_records.append({'expectation_identity': SCRIPTS + '/' + output,
                               'expectation_sha256': sha(files[output]), 'status': status,
                               'provider_identity': SCRIPTS + '/' + ORPHAN_HELPER,
                               'reason': policy['orphan_expectations'][output],
                               'expected_block': block, 'expectation_transformations': projections})
    ledger = {'root': uri, 'foundry_revision': policy['foundry_revision'], 'upstream_total': len(cases),
              'upstream_helpers': len(helpers), 'upstream_sources': len(sources),
              'total': len(included), 'skipped': len(helpers),
              'expected_failures': established_expected_failures(policy, execution_report, pinned_failures,
                                                                 included, triage),
              'triage': triage}
    complaint = validate_triage_ledger(SCOPE, ledger, disk_cases=included, disk_helpers=helpers)
    if complaint:
        raise ValueError(complaint)
    return {'schema_version': 1, 'checkpoint': 'imported', 'imported': True,
            'foundry_revision': policy['foundry_revision'], 'root': uri, 'counts': counts,
            'policy_sha256': sha(encoded(policy)), 'ledger': ledger,
            'orphan_expectations': orphan_records,
            'auxiliary_reconciliation': policy['auxiliary_reconciliation'],
            'sources': sorted(records, key=lambda record: record['identity'])}


def generate(inventory: dict, source: Path, destination: Path):
    cases, helpers = [], []
    for record in inventory['sources']:
        if record.get('disposition') in ('deferred', 'excluded'):
            continue
        path = destination / record['imported_path']
        data = (source / record['upstream_path']).read_bytes()
        if sha(data) != record['sha256']:
            raise ValueError(f'source drift after inventory: {record["identity"]}')
        transformed = patch(data, record['transformations'], record['identity'])
        if sha(transformed) != record['imported_sha256']:
            raise ValueError(f'provenance drift: {record["identity"]}')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(transformed)
        if record['role'] == 'case':
            cases.append(record['imported_path'])
            path.with_suffix('.out').write_bytes((record['expected_block'] + '\n').encode())
        else:
            helpers.append(record['imported_path'])
    write_stages(destination, cases, helpers, inventory['foundry_revision'], SCOPE)
    (destination / 'inventory.json').write_bytes(encoded(inventory))
    ledger = inventory['ledger']
    counts = inventory['counts']
    (destination / 'README.md').write_text(
        f'# Runtime conformance corpus\n\nImported from Foundry. {len(cases)} runtime cases, {len(helpers)} helpers.\n'
        f'Foundry `{inventory["foundry_revision"]}`. Full upstream inventory: {counts["sources"]} sources, '
        f'{counts["cases"]} runnable cases, {counts["helpers"]} helpers and {counts["outputs"]} expectations; '
        f'{len(inventory["orphan_expectations"])} expectation belongs to a helper rather than a case and is '
        'recorded in `inventory.json` instead of becoming one.\n'
        f'Expected status over the {counts["cases"]} runnable cases: '
        + ', '.join(f'{status} {count}' for status, count in counts['statuses'].items()) + '.\n'
        f'Population: {ledger["upstream_total"]} = {ledger["total"]} imported + '
        f'{len(ledger["triage"]["excluded"])} excluded + {len(ledger["triage"]["deferred"])} deferred.\n'
        f'Residual failures pinned in `tests/corpus_baseline.json`: {len(ledger["expected_failures"])}.\n'
        'A case expectation is the complete upstream transcript, relocated onto this destination and\n'
        'otherwise byte-identical. `scripts/run_corpus_triage.py` compiles and runs each case against it;\n'
        'every case that still fails is pinned with a reason naming the child that owns it, and the pin\n'
        'shrinks only through this importer reading a completed execution report.\n')


def write_tree(inventory: dict, source: Path, destination: Path):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        tree_entries(destination)
    with tempfile.TemporaryDirectory(prefix='.runtime-import-', dir=destination.parent) as temporary:
        candidate = Path(temporary) / 'candidate'
        candidate.mkdir()
        generate(inventory, source, candidate)
        previous = Path(temporary) / 'previous'
        if destination.exists():
            destination.rename(previous)
        try:
            candidate.rename(destination)
        except OSError:
            if previous.exists():
                previous.rename(destination)
            raise


def check_tree(inventory: dict, source: Path, destination: Path):
    with tempfile.TemporaryDirectory(prefix='runtime-check-') as temporary:
        fresh = Path(temporary)
        generate(inventory, source, fresh)
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
    """The committed ledger entry for the imported runtime corpus."""
    return {'imported': True, 'root': ledger['root'], 'total': ledger['total'], 'skipped': ledger['skipped'],
            'expected_failures': ledger['expected_failures'], 'foundry_revision': ledger['foundry_revision'],
            'upstream_total': ledger['upstream_total'], 'upstream_helpers': ledger['upstream_helpers'],
            'upstream_sources': ledger['upstream_sources'], 'triage': ledger['triage']}


def committed_expected_failures(baseline_path: Path) -> list[str] | None:
    """The pin as committed, so --check regenerates the very tree the repository claims.

    ``None`` means the ledger carries no runtime entry at all, which is establishment
    rather than an empty pin; every other caller of this ledger refuses a missing entry,
    so establishment cannot recur behind a deleted one.
    """
    corpora = read_json(baseline_path).get('corpora')
    if not isinstance(corpora, dict):
        raise ValueError(f'{baseline_path}: missing corpora')
    if SCOPE not in corpora:
        return None
    if not isinstance(corpora[SCOPE], dict):
        raise ValueError(f'{baseline_path}: malformed runtime ledger entry')
    failures = corpora[SCOPE].get('expected_failures')
    if not isinstance(failures, list):
        raise ValueError(f'{baseline_path}: runtime expected_failures must be a list')
    return failures


def check_ledger(ledger: dict, baseline_path: Path) -> None:
    """The committed entry must be exactly the ledger a regeneration produces."""
    corpora = read_json(baseline_path).get('corpora')
    committed = corpora[SCOPE] if isinstance(corpora, dict) and SCOPE in corpora else None
    expected = baseline_entry(ledger)
    if committed != expected:
        differing = sorted(key for key in set(expected) | set(committed or {})
                           if (committed or {}).get(key) != expected.get(key))
        raise ValueError(f'{baseline_path}: committed runtime ledger differs from the regenerated one: '
                         + ', '.join(differing))


def publish_baseline(ledger: dict, baseline_path: Path) -> None:
    document = read_json(baseline_path)
    document['corpora'][SCOPE] = baseline_entry(ledger)
    temporary = baseline_path.with_name(baseline_path.name + '.candidate')
    # Same encoding the parser and analyzer importers write, so any importer rewriting
    # this file leaves the other entries byte-identical.
    temporary.write_text(json.dumps(document, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, baseline_path)


def validate_auxiliary_reconciliation(registry: dict) -> None:
    """The two runtime-tree helper identities keep exactly one owning mechanism each."""
    registered = registry.get('auxiliary_sources', [])
    roots = tuple(record['source'] for record in registry['corpora'].values())
    if AUXILIARY_REGISTERED not in registered:
        raise ValueError('the shared parser support helper must remain a registered auxiliary source')
    if AUXILIARY_IN_CORPUS in registered:
        raise ValueError('a helper inside the runtime corpus root is verified by that root, not as auxiliary')
    if not any(AUXILIARY_IN_CORPUS.startswith(root + '/') for root in roots):
        raise ValueError('no registered corpus root verifies the runtime support helper')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--foundry', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument('--inventory', type=Path)
    modes.add_argument('--stage', help='project-relative directory to write the imported tree into')
    modes.add_argument('--check', action='store_true')
    parser.add_argument('--execution-report', type=Path,
                        help='completed triage report whose failures become the ledger expected_failures pin')
    args = parser.parse_args(argv)
    try:
        # Writing the tree or an external inventory is the one moment the destination may
        # legitimately be absent or stale; every other mode validates it as committed.
        rebuilding = SCOPE if args.stage or args.inventory else None
        registry = validate_registration(ROOT, rebuilding=rebuilding)
        verify_checkout(args.foundry, registry, args.revision)
        validate_auxiliary_reconciliation(registry)
        policy = default_policy()
        if policy['counts'] is None:
            raise ValueError('production policy must retain complete pinned inventory counts')
        if policy['foundry_revision'] != registry['revision']:
            raise ValueError('policy revision differs from shared registry')
        destination = import_destination(ROOT, args.stage or DESTINATION)
        uri = 'res://' + DESTINATION.removeprefix('project/')
        execution_report = None
        pinned_failures = None
        if args.execution_report is not None:
            if args.check:
                raise ValueError('--check reads the committed pin; it may not derive a new one from a run')
            execution_report = read_json(args.execution_report)
        else:
            # Only a completed run may change the pin. Without one, both checking and
            # regenerating carry the committed pin forward.
            pinned_failures = committed_expected_failures(BASELINE)
        inventory = inventory_sources(args.foundry / SCRIPTS, policy, uri, execution_report, pinned_failures)
        if args.inventory:
            # Inventory is metadata-only; forbid a caller using it to corrupt registered
            # generated inputs or any repository-owned source file.
            if (args.inventory.resolve().is_relative_to(ROOT)
                    or args.inventory.resolve().is_relative_to(args.foundry.resolve())):
                raise ValueError('inventory report must be outside repository and source checkout')
            args.inventory.write_bytes(encoded(inventory))
        elif args.stage:
            write_tree(inventory, args.foundry / SCRIPTS, destination)
            publish_baseline(inventory['ledger'], BASELINE)
        else:
            # An absent destination is drift, not a vacuous pass: the registry records this
            # corpus as imported, so there is a tree to compare against or the check failed.
            if not destination.is_dir():
                raise ValueError(f'imported corpus tree is missing: {destination}')
            check_tree(inventory, args.foundry / SCRIPTS, destination)
            check_ledger(inventory['ledger'], BASELINE)
        print(f'runtime corpus: {inventory["counts"]["sources"]} sources, {inventory["ledger"]["total"]} imported '
              f'cases, {inventory["ledger"]["skipped"]} helpers, '
              f'{len(inventory["ledger"]["expected_failures"])} pinned residual failures')
        return 0
    except (ValueError, OSError) as error:
        print(f'runtime corpus: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
