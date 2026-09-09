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
import subprocess
import sys
import tempfile
import time

from corpus_registry import ROOT, local_path, read_json, run_git, tree_entries
from corpus_stages import validate_stages
from corpus_expectations import decode_expectation


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


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
    return {'exit_code': process.returncode, 'timed_out': timed_out,
            'duration_seconds': round(time.monotonic() - started, 4),
            'output': output.decode('utf-8', errors='replace')}


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
            result = json.loads(payloads[0])
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


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--corpus', required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--case', action='append', default=[])
    parser.add_argument('--timeout', type=float, default=30.0)
    args = parser.parse_args(argv)
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
        records = {record['imported_path']: record for record in inventory['sources']
                   if record['role'] == 'case' and record['disposition'] not in ('excluded', 'deferred')}
        entries = tree_entries(root)
        sources = {path for path, kind in entries.items() if kind == 'file' and path.endswith('.barista')}
        helpers = {path for path in sources if path.endswith('.notest.barista')}
        helper_records = {r['imported_path'] for r in inventory['sources'] if r['role'] in ('helper', 'support_helper')}
        if len(records) != inventory['ledger']['total'] or helpers != helper_records or sources - helpers != set(records):
            raise ValueError('staging inventory/population disagreement')
        validate_stages(read_json(root / 'case_stages.json'), set(records), helpers, inventory['foundry_revision'])
        for record in inventory['sources']:
            if record.get('disposition') in ('excluded', 'deferred'):
                continue
            path = root / record['imported_path']
            if digest(path) != record['imported_sha256']:
                raise ValueError(f'staged source hash drift: {path}')
            if record['role'] == 'case' and decode_expectation(path.with_suffix('.out').read_bytes(), str(path)) != record['expected_block']:
                raise ValueError(f'staged expectation drift: {path}')
        if len(set(args.case)) != len(args.case) or set(args.case) - set(records):
            raise ValueError('duplicate or unknown exact case selection')
        selected = args.case or sorted(records)
        version = subprocess.run([str(args.godot), '--version'], capture_output=True, text=True, check=True).stdout.strip()
        libraries = sorted((ROOT / 'project/bin').rglob('*template_debug*'))
        libraries = [path for path in libraries if path.is_file()]
        if not libraries:
            raise ValueError('debug extension build artifact is missing')
        report = {'schema_version': 1, 'checkpoint': 'discovery', 'corpus': args.corpus,
                  'source_revision': inventory['foundry_revision'], 'inventory_sha256': digest(root / 'inventory.json'),
                  'baristascript_revision': run_git(ROOT, 'rev-parse', 'HEAD'),
                  'baristascript_worktree_diff_sha256': hashlib.sha256(subprocess.run(['git', '-C', str(ROOT), 'diff', 'HEAD'], capture_output=True, check=True).stdout).hexdigest(),
                  'build_target': 'template_debug', 'build_artifacts': {str(p.relative_to(ROOT)): digest(p) for p in libraries},
                  'execution_files': {str(path.relative_to(ROOT)): digest(path) for path in
                                      sorted([ROOT / 'scripts/run_corpus_triage.py', ROOT / 'project/tests/corpus_runner.gd',
                                              ROOT / 'project/tests/corpus_harness.gd', ROOT / 'src/bs_analyzer_probe.cpp',
                                              ROOT / 'src/bs_analyzer_probe.h', ROOT / 'scripts/analyzer_corpus_policy.json'])},
                  'godot': str(args.godot), 'godot_version': version, 'timeout_seconds': args.timeout,
                  'complete_population': not bool(args.case), 'selected_cases': selected,
                  'deferred_cases': [r['identity'] for r in inventory['sources'] if r.get('disposition') == 'deferred'],
                  'completed': False, 'results': []}
        atomic_report(args.report, report)
        for index, case in enumerate(selected):
            command = [str(args.godot), '--headless', '--path', str(ROOT / 'project'), '--script',
                       'res://tests/corpus_runner.gd', '--', '--corpus', args.corpus, '--case', case]
            process = supervise(command, args.timeout)
            record = result_record(process, case, args.corpus, records[case]['expected_block'])
            record.update({'case': case, 'identity': records[case]['identity'], 'source_sha256': records[case]['sha256'],
                           'staged_source_sha256': records[case]['imported_sha256'],
                           'expectation_sha256': records[case]['expectation_sha256'],
                           'semantic_owner': records[case]['semantic_owner'],
                           'candidate_observations': records[case]['candidate_observations'], 'command': command})
            report['results'].append(record)
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
        print(f'corpus triage: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
