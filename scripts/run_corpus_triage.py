#!/usr/bin/env python3
# run_corpus_triage.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Supervise static corpus cases in isolated Godot processes (30s per case).

Exit zero requires each selected case's JSON result, exact-case execution guard,
aggregate guard, full expected/actual block agreement and a successful process.
Nonzero reports are discovery evidence, never passing corpus baselines.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

from corpus_registry import ROOT, local_path, read_json, run_git, tree_entries
from corpus_stages import validate_stages
from corpus_expectations import decode_expectation
from corpus_ledger import validate_triage_ledger
from build_config import require_equal
from build_metadata import inspect_artifact_bytes
from query_build_info import checkout_info, parse_record
from run_native_suites import staged_project


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def select_library(explicit, candidates):
    hosts = {'darwin': ('macos', '.dylib'), 'win32': ('windows', '.dll'), 'linux': ('linux', '.so')}
    platform, suffix = hosts.get(sys.platform, (None, None))
    if platform is None:
        raise ValueError(f'unsupported corpus triage host: {sys.platform}')
    if explicit is None:
        matching = [path for path in candidates if path.parent.name == platform and path.suffix == suffix]
        if len(matching) != 1:
            raise ValueError('select --library explicitly; an unambiguous current-host debug artifact is required')
        explicit = matching[0]
    library = Path(explicit).resolve()
    content = library.read_bytes()
    info = inspect_artifact_bytes(content)
    if info['build']['native_tests'] or info['build']['target'] != 'template_debug':
        raise ValueError('corpus triage requires an ordinary template_debug library with analyzer bindings')
    require_equal('triage library platform', platform, info['build']['platform'])
    return library, info, hashlib.sha256(content).hexdigest()


def prepare_triage_project(project):
    # The corpus harness reads this registry adjacent to its project. It needs no API fixture.
    scripts = project.parent / 'scripts'
    scripts.mkdir()
    shutil.copy2(ROOT / 'scripts/corpus_sources.json', scripts / 'corpus_sources.json')


def attach_build_info(record, nonce, expected):
    # Corpus mismatch/crash/timeout status remains authoritative. Metadata emitted before
    # evaluation can still identify that same process without pretending it exited zero.
    record.update(build_info=None, godot_version=None, build_info_error=None)
    try:
        loaded = parse_record(record['output'], nonce, expected)
        record.update(build_info=loaded['build_info'], godot_version=loaded['godot_version'])
    except ValueError as error:
        record['build_info_error'] = str(error)
        record['passed'] = False
        if record['terminal'] == 'passed':
            record['terminal'] = 'build_info_error'


def atomic_report(path, document):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as stream:
            json.dump(document, stream, indent=2, ensure_ascii=False)
            stream.write('\n')
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def supervise(command: list[str], timeout: float):
    started = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               start_new_session=True)
    timed_out = False
    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(process.pid, signal.SIGKILL)
        output, _ = process.communicate()
    except BaseException:
        # A user interruption must not leave the isolated case process running after cleanup.
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        process.communicate()
        raise
    return {'exit_code': process.returncode, 'timed_out': timed_out,
            'duration_seconds': round(time.monotonic() - started, 4),
            'output': output.decode('utf-8', errors='replace')}


def unique_result_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate result field: {key}')
        result[key] = value
    return result


def result_record(process, case, corpus, expected):
    lines = process['output'].splitlines()
    payloads = [line.removeprefix('BS_CASE_RESULT ') for line in lines if line.startswith('BS_CASE_RESULT ')]
    guards = [line for line in lines if line.startswith('BS_CASE_RAN ')]
    header = (ROOT / 'src/bs_corpus_sentinels.h').read_text()
    prefix = re.search(r'SUMMARY_PREFIX\s*=\s*"([^"]+)"', header)[1]
    summaries = [line for line in lines if line.startswith(prefix + ' ')]
    result = None
    malformed = False
    if len(payloads) == 1:
        try:
            result = json.loads(payloads[0], object_pairs_hook=unique_result_pairs)
            if (not isinstance(result, dict) or result.get('path') != corpus + '/' + case
                    or type(result.get('passed')) is not bool
                    or result.get('expected') != expected or not isinstance(result.get('actual'), str)):
                malformed = True
        except ValueError:
            malformed = True
    ran = guards == ['BS_CASE_RAN ' + case] and len(payloads) == 1 and not malformed
    actual = result.get('actual') if isinstance(result, dict) else None
    if process['timed_out']:
        terminal = 'timeout'
    elif process['exit_code'] < 0:
        terminal = 'crash'
    elif not ran:
        terminal = 'malformed_result' if malformed else 'missing_guard'
    elif len(summaries) != 1 or not re.fullmatch(re.escape(prefix) + r' [01]/1 skipped=[0-9]+', summaries[0]):
        terminal = 'missing_summary'
    elif result.get('passed') and actual == expected and process['exit_code'] == 0 and summaries[0].startswith(prefix + ' 1/1 '):
        terminal = 'passed'
    elif process['exit_code'] == 2 or result.get('reason') in (3, 5):
        terminal = 'infrastructure_error'
    else:
        terminal = 'mismatch'
    return {**process, 'terminal': terminal, 'guard': ran, 'expected_block': expected,
            'actual_block': actual, 'frontend_result': result, 'passed': terminal == 'passed'}


def validate_staging(root, inventory, *, project_root=None):
    from import_analyzer_corpus import shared_source_path
    project_root = project_root or ROOT / "project"
    if (len({r['identity'] for r in inventory['sources']}) != len(inventory['sources'])
            or len({r['imported_path'] for r in inventory['sources']}) != len(inventory['sources'])
            or sum(r['role'] == 'case' for r in inventory['sources']) != inventory['counts']['cases']):
        raise ValueError('staging source identity/population disagreement')
    records = {record['imported_path']: record for record in inventory['sources']
               if record['role'] == 'case' and record['disposition'] not in ('excluded', 'deferred')}
    entries = tree_entries(root)
    sources = {path for path, kind in entries.items() if kind == 'file' and path.endswith('.barista')}
    helpers = {path for path in sources if path.endswith('.notest.barista')}
    helper_records = {r['imported_path'] for r in inventory['sources'] if r['role'] in ('helper', 'support_helper') and 'root' not in r}
    if len(records) != inventory['ledger']['total'] or helpers != helper_records or sources - helpers != set(records):
        raise ValueError('staging inventory/population disagreement')
    complaint = validate_triage_ledger('analyzer staging', inventory['ledger'], disk_cases=set(records), disk_helpers=helpers)
    if complaint:
        raise ValueError(f'staging population accounting: {complaint}')
    validate_stages(read_json(root / 'case_stages.json'), set(records), helpers, inventory['foundry_revision'])
    for record in inventory['sources']:
        if record.get('disposition') in ('excluded', 'deferred'):
            continue
        shared = shared_source_path(record, project_root)
        if shared is not None:
            continue
        path = root / record['imported_path']
        if digest(path) != record['imported_sha256']:
            raise ValueError(f'staged source hash drift: {path}')
        if record['role'] == 'case' and decode_expectation(path.with_suffix('.out').read_bytes(), str(path)) != record['expected_block']:
            raise ValueError(f'staged expectation drift: {path}')
    expected_files = {'inventory.json', 'case_stages.json', 'README.md'} | sources
    expected_files |= {str(Path(case).with_suffix('.out')) for case in records}
    if {path for path, kind in entries.items() if kind == 'file'} != expected_files:
        raise ValueError('staging inventory/file population disagreement')
    return records


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--library', type=Path, help='explicit ordinary debug library; otherwise require one current-host candidate')
    parser.add_argument('--corpus', required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--case', action='append', default=[])
    parser.add_argument('--timeout', type=float, default=30.0)
    args = parser.parse_args(argv)
    report = None
    try:
        if not math.isfinite(args.timeout) or args.timeout <= 0:
            raise ValueError('timeout must be finite and positive')
        if args.corpus != 'res://tests/corpus_staging/analyzer':
            raise ValueError('discovery triage requires the importer-owned analyzer staging root')
        root = local_path(ROOT, 'project/' + args.corpus.removeprefix('res://'))
        if args.report.resolve().is_relative_to(ROOT):
            raise ValueError('execution report must be outside the repository')
        inventory = read_json(root / 'inventory.json')
        if inventory.get('checkpoint') != 'discovery' or inventory.get('imported') is not False or inventory.get('root') != args.corpus:
            raise ValueError('not a pending discovery inventory')
        from import_analyzer_corpus import default_policy, encoded, sha
        policy = default_policy()
        if inventory['counts'] != policy['counts'] or inventory['policy_sha256'] != sha(encoded(policy)):
            raise ValueError('staging inventory differs from the current pinned policy; regenerate staging')
        records = validate_staging(root, inventory)
        if len(set(args.case)) != len(args.case) or set(args.case) - set(records):
            raise ValueError('duplicate or unknown exact case selection')
        selected = args.case or sorted(records)
        version = subprocess.run([str(args.godot), '--version'], capture_output=True, text=True, check=True).stdout.strip()
        libraries = sorted((ROOT / 'project/bin').rglob('*template_debug*'))
        libraries = [path for path in libraries if path.is_file()]
        library, inspected_info, artifact_sha = select_library(args.library, libraries)
        report = {'schema_version': 1, 'checkpoint': 'discovery', 'corpus': args.corpus,
                  'source_revision': inventory['foundry_revision'], 'inventory_sha256': digest(root / 'inventory.json'),
                  'baristascript_revision': run_git(ROOT, 'rev-parse', 'HEAD'),
                  'baristascript_worktree_diff_sha256': hashlib.sha256(subprocess.run(['git', '-C', str(ROOT), 'diff', 'HEAD'], capture_output=True, check=True).stdout).hexdigest(),
                  'build_target': 'template_debug', 'build_artifacts': {str(p.relative_to(ROOT)): digest(p) for p in libraries},
                  'selected_artifact': {'path': str(library), 'sha256': artifact_sha, 'build_info': inspected_info},
                  'build_info': None, 'checkout_info': checkout_info(),
                  'execution_files': {str(path.relative_to(ROOT)): digest(path) for path in
                                      sorted([ROOT / 'scripts/run_corpus_triage.py', ROOT / 'project/tests/corpus_runner.gd',
                                              ROOT / 'project/tests/corpus_harness.gd', ROOT / 'src/bs_analyzer_probe.cpp',
                                              ROOT / 'src/bs_analyzer_probe.h', ROOT / 'scripts/analyzer_corpus_policy.json'])},
                  'godot': str(args.godot), 'godot_version': version, 'timeout_seconds': args.timeout,
                  'complete_population': not bool(args.case), 'selected_cases': selected,
                  'deferred_cases': [r['identity'] for r in inventory['sources'] if r.get('disposition') == 'deferred'],
                  'completed': False, 'results': []}
        atomic_report(args.report, report)
        with staged_project({'library': str(library)}) as project:
            prepare_triage_project(project)
            copied = project / 'bin' / ('native' + library.suffix)
            validate_staging(project / args.corpus.removeprefix('res://'), inventory, project_root=project)
            for name in ('corpus_runner.gd', 'corpus_harness.gd'):
                require_equal('staged execution script ' + name, report['execution_files']['project/tests/' + name],
                              digest(project / 'tests' / name))
            for index, case in enumerate(selected):
                before = digest(copied)
                require_equal('staged library before case', artifact_sha, before)
                nonce = uuid.uuid4().hex
                command = [str(args.godot), '--headless', '--path', str(project), '--script',
                           'res://tests/corpus_runner.gd', '--', '--corpus', args.corpus, '--case', case,
                           '--build-info-nonce', nonce]
                process = supervise(command, args.timeout)
                record = result_record(process, case, args.corpus, records[case]['expected_block'])
                attach_build_info(record, nonce, inspected_info)
                after = digest(copied) if copied.is_file() else None
                record.update(library_sha256_before=before, library_sha256_after=after)
                if after != artifact_sha:
                    record['artifact_error'] = 'staged library changed during case execution'
                    record['passed'] = False
                    if record['terminal'] == 'passed':
                        record['terminal'] = 'artifact_changed'
                record.update({'case': case, 'identity': records[case]['identity'], 'source_sha256': records[case]['sha256'],
                               'staged_source_sha256': records[case]['imported_sha256'],
                               'expectation_sha256': records[case]['expectation_sha256'],
                               'semantic_owner': records[case]['semantic_owner'],
                               'candidate_observations': records[case]['candidate_observations'], 'command': command})
                report['results'].append(record)
                if record['build_info'] is not None:
                    report['build_info'] = record['build_info']
                # Each completed record is durable even if the supervisor is interrupted.
                atomic_report(args.report, report)
                print(f'{index + 1}/{len(selected)} {record["terminal"]} {case}', flush=True)
        report['completed'] = True
        report['summary'] = dict(sorted(Counter(r['terminal'] for r in report['results']).items()))
        report['unowned_failures'] = [r['case'] for r in report['results'] if not r['passed'] and not r['semantic_owner']]
        atomic_report(args.report, report)
        print(json.dumps(report['summary'], sort_keys=True))
        return 0 if all(r['passed'] for r in report['results']) else 1
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        if report is not None:
            report['infrastructure_error'] = str(error)
            atomic_report(args.report, report)
        print(f'corpus triage: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
