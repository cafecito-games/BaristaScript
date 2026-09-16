#!/usr/bin/env python3
# run_corpus_triage.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Supervise static corpus cases in isolated Godot processes (30s per case).

Each case is evaluated by the native analyzer_corpus suite hosted by stock Godot, so the
library must be a barista_tests build. Cases run one at a time by default; --jobs runs
several at once, each in its own staged project. Concurrency changes only timing, never a
recorded outcome or its ordering.

Exit zero requires each selected case's JSON result, exact-case execution guard, the
native completion record of the same process, full expected/actual block agreement and a
successful process. Nonzero reports are discovery evidence, never passing corpus baselines.
"""
from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from contextlib import ExitStack
import hashlib
import json
import math
import os
from pathlib import Path
import queue
import signal
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import uuid

from corpus_registry import ROOT, load_registry, local_path, read_json, run_git, tree_entries
from corpus_stages import validate_stages
from corpus_expectations import decode_expectation
from corpus_ledger import validate_triage_ledger
from build_config import parse_json, require_equal
from build_metadata import inspect_artifact_bytes, verify_identity
from native_test_build import build_identity
from query_build_info import checkout_info
from run_native_suites import PROTOCOL_VERSION, RESULT_FIELDS, RESULT_PREFIX, staged_project

NATIVE_SUITE = 'analyzer_corpus'
# The one registered analyzer_corpus case that evaluates a single externally selected case.
NATIVE_CORPUS_CASE = 'selected_corpus_case_emits_guards'
NATIVE_BUILD_DIRECTORY = 'build/native-scons'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def permitted_corpus_roots():
    """The roots trusted to carry a case_stages.json manifest.

    The native suite reads case_stages.json from whatever root it is handed, so a root
    outside this set could carry a planted manifest that evaluates a case at a stage it
    was never adjudicated for and silently turn a failing case into a passing one. Only a
    registered destination whose record declares triage supervision is trusted to carry
    that manifest, and this is now the sole restriction: it both admits the promoted
    analyzer corpus and refuses the GDScript-supervised parser destination, which this
    supervisor has no business restaging.
    """
    return {'res://' + record['destination'].removeprefix('project/')
            for record in load_registry(ROOT)['corpora'].values()
            if record['execution'] == 'triage'}


def select_library(explicit, candidates):
    """Resolve the barista_tests debug library that hosts the native analyzer_corpus suite."""
    hosts = {'darwin': ('macos', '.dylib'), 'win32': ('windows', '.dll'), 'linux': ('linux', '.so')}
    platform, suffix = hosts.get(sys.platform, (None, None))
    if platform is None:
        raise ValueError(f'unsupported corpus triage host: {sys.platform}')
    if explicit is None:
        matching = [path for path in candidates if path.suffix == suffix]
        if len(matching) != 1:
            raise ValueError('select --library explicitly; an unambiguous current-host native-test debug artifact is required')
        explicit = matching[0]
    library = Path(explicit).resolve()
    content = library.read_bytes()
    info = inspect_artifact_bytes(content)
    # analyzer_corpus is compiled only into a barista_tests build, so an ordinary debug
    # library cannot evaluate a single case at all. Say so instead of running it.
    if not info['build']['native_tests'] or info['build']['target'] != 'template_debug':
        raise ValueError('corpus triage requires the barista_tests template_debug library that hosts analyzer_corpus')
    require_equal('triage library platform', platform, info['build']['platform'])
    return library, info, hashlib.sha256(content).hexdigest()


def prepare_triage_project(project):
    # The native corpus suite reads this registry adjacent to its project. It needs no API fixture.
    scripts = project.parent / 'scripts'
    scripts.mkdir()
    shutil.copy2(ROOT / 'scripts/corpus_sources.json', scripts / 'corpus_sources.json')


def native_completion(output, nonce, expected, expected_build_id):
    """Parse the one native completion record the case process emits about its own run.

    The recorded build id is a content fingerprint of the native sources, so pinning it binds
    this case process to the very sources the report attests to rather than to a stale library
    that merely happens to still carry the artifact hash the descriptor recorded.
    """
    records = [line.removeprefix(RESULT_PREFIX) for line in output.splitlines() if line.startswith(RESULT_PREFIX)]
    if len(records) != 1:
        raise ValueError(f'expected one native completion record, found {len(records)}')
    record = parse_json(records[0])
    if type(record) is not dict or set(record) != RESULT_FIELDS:
        raise ValueError('native completion fields do not match the shared protocol')
    for key, expectation in (('protocol', PROTOCOL_VERSION), ('suite', NATIVE_SUITE),
                             ('case', NATIVE_CORPUS_CASE), ('nonce', nonce),
                             ('build_id', expected_build_id)):
        if type(record[key]) is not type(expectation) or record[key] != expectation:
            raise ValueError(f'native completion {key} does not match {expectation!r}')
    for key in ('cases', 'assertions', 'failed_cases', 'failed_assertions'):
        if type(record[key]) is not int or record[key] < 0:
            raise ValueError(f'invalid native completion {key}')
    verify_identity(record['build_info'], expected)
    return record


def attach_build_info(record, completion, error):
    # Corpus mismatch/crash/timeout status remains authoritative. Metadata the case process
    # emits about itself can still identify it without pretending it exited zero. The native
    # completion protocol carries build identity only; the host engine version is recorded
    # once per report from the selected executable.
    record.update(build_info=None, godot_version=None, build_info_error=error)
    if completion is None:
        record['passed'] = False
        if record['terminal'] == 'passed':
            record['terminal'] = 'build_info_error'
        return
    record['build_info'] = completion['build_info']


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


# Returned in place of a record by a case whose process a stop request killed. Its output is
# truncated garbage, so it is not an outcome, but it did run and the report has to say so.
KILLED_BY_STOP = object()


def kill_process_group(process):
    if process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass


def supervise(command: list[str], timeout: float, started=None, finished=None):
    """Run one isolated case process. The optional hooks expose it while it is alive.

    A supervisor running cases concurrently receives an interruption on its own thread, never
    inside this call, so it needs the live process to kill the group it created.
    """
    begun = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               start_new_session=True)
    timed_out = False
    try:
        if started is not None:
            started(process)
        try:
            output, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            kill_process_group(process)
            output, _ = process.communicate()
        except BaseException:
            # A user interruption must not leave the isolated case process running after cleanup.
            kill_process_group(process)
            process.communicate()
            raise
    finally:
        if finished is not None:
            finished(process)
    return {'exit_code': process.returncode, 'timed_out': timed_out,
            'duration_seconds': round(time.monotonic() - begun, 4),
            'output': output.decode('utf-8', errors='replace')}


def unique_result_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate result field: {key}')
        result[key] = value
    return result


def completion_agrees(completion, result):
    """Whether the native completion record describes this payload's own run.

    The suite runs exactly one case and asserts the very outcome the payload reports, so the
    record must show one executed case, at least one assertion, and a failure reported when
    and only when the payload did not pass. Anything else describes a different run.
    """
    suite_reported_failure = bool(completion['failed_cases'] or completion['failed_assertions'])
    payload_passed = bool(result.get('passed'))
    return completion['cases'] == 1 and completion['assertions'] >= 1 and suite_reported_failure != payload_passed


def result_record(process, case, corpus, expected, completion):
    lines = process['output'].splitlines()
    payloads = [line.removeprefix('BS_CASE_RESULT ') for line in lines if line.startswith('BS_CASE_RESULT ')]
    guards = [line for line in lines if line.startswith('BS_CASE_RAN ')]
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
    elif completion is not None and not completion_agrees(completion, result):
        terminal = 'inconsistent_completion'
    elif result.get('passed') and actual == expected and process['exit_code'] == 0:
        terminal = 'passed'
    elif process['exit_code'] == 2 or result.get('reason') in (3, 5):
        terminal = 'infrastructure_error'
    else:
        terminal = 'mismatch'
    return {**process, 'terminal': terminal, 'guard': ran, 'expected_block': expected,
            'actual_block': actual, 'frontend_result': result, 'passed': terminal == 'passed'}


def owned_failure(record):
    """Whether a failing case names a semantic owner carrying a written reason."""
    owner = record.get('semantic_owner')
    return isinstance(owner, dict) and isinstance(owner.get('reason'), str) and bool(owner['reason'].strip())


def expected_failure_complaints(results, expected_failures, selected):
    """Enforce the residual-failure pin in both directions over the cases this run covered.

    A failure missing from the pin would let new breakage land unnoticed, and a pinned case
    that passes would let the pin outlive the bug it describes. Cases outside this run's
    selection are not judged, so an exact-case run never reads a whole-population pin.
    """
    ordered = list(dict.fromkeys(selected))
    outcomes = {}
    complaints = []
    for record in results:
        # derive_expected_failures refuses a report holding two records for one case; the two
        # readers of this structure must not disagree about what a well-formed run looks like.
        if record['case'] in outcomes:
            complaints.append(f'{record["case"]}: the run recorded more than one outcome for this case')
            continue
        outcomes[record['case']] = record
    pinned = set(expected_failures) & set(ordered)
    for case in ordered:
        record = outcomes.get(case)
        if record is None:
            complaints.append(f'{case}: selected case has no recorded outcome; the run cannot be judged against the pin')
            continue
        if record['passed']:
            if case in pinned:
                complaints.append(f'{case}: pinned expected failure now passes; remove it from expected_failures')
            continue
        if not owned_failure(record):
            complaints.append(f'{case}: failing case has no semantic owner with a written reason')
        if case not in pinned:
            complaints.append(f'{case}: failing case is not declared in expected_failures; new breakage must be '
                              'declared with an owner, never silently absorbed')
    return complaints


def validate_imported_tree(root, inventory, *, project_root=None):
    from import_analyzer_corpus import shared_source_path
    project_root = project_root or ROOT / "project"
    if (len({r['identity'] for r in inventory['sources']}) != len(inventory['sources'])
            or len({r['imported_path'] for r in inventory['sources']}) != len(inventory['sources'])
            or sum(r['role'] == 'case' for r in inventory['sources']) != inventory['counts']['cases']):
        raise ValueError('imported source identity/population disagreement')
    records = {record['imported_path']: record for record in inventory['sources']
               if record['role'] == 'case' and record['disposition'] not in ('excluded', 'deferred')}
    entries = tree_entries(root)
    sources = {path for path, kind in entries.items() if kind == 'file' and path.endswith('.barista')}
    helpers = {path for path in sources if path.endswith('.notest.barista')}
    helper_records = {r['imported_path'] for r in inventory['sources'] if r['role'] in ('helper', 'support_helper') and 'root' not in r}
    if len(records) != inventory['ledger']['total'] or helpers != helper_records or sources - helpers != set(records):
        raise ValueError('imported inventory/population disagreement')
    complaint = validate_triage_ledger('analyzer', inventory['ledger'], disk_cases=set(records), disk_helpers=helpers)
    if complaint:
        raise ValueError(f'imported population accounting: {complaint}')
    validate_stages(read_json(root / 'case_stages.json'), set(records), helpers, inventory['foundry_revision'])
    for record in inventory['sources']:
        if record.get('disposition') in ('excluded', 'deferred'):
            continue
        shared = shared_source_path(record, project_root)
        if shared is not None:
            continue
        path = root / record['imported_path']
        if digest(path) != record['imported_sha256']:
            raise ValueError(f'imported source hash drift: {path}')
        if record['role'] == 'case' and decode_expectation(path.with_suffix('.out').read_bytes(), str(path)) != record['expected_block']:
            raise ValueError(f'imported expectation drift: {path}')
    expected_files = {'inventory.json', 'case_stages.json', 'README.md'} | sources
    expected_files |= {str(Path(case).with_suffix('.out')) for case in records}
    if {path for path, kind in entries.items() if kind == 'file'} != expected_files:
        raise ValueError('imported inventory/file population disagreement')
    return records


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--library', type=Path, help='explicit native-test debug library; otherwise require one current-host candidate')
    parser.add_argument('--corpus', required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--case', action='append', default=[])
    parser.add_argument('--timeout', type=float, default=30.0)
    parser.add_argument('--jobs', type=int, default=1, help='number of cases to execute concurrently')
    args = parser.parse_args(argv)
    report = None
    try:
        if not math.isfinite(args.timeout) or args.timeout <= 0:
            raise ValueError('timeout must be finite and positive')
        if args.jobs < 1:
            raise ValueError('jobs must be a positive integer')
        if args.corpus not in permitted_corpus_roots():
            raise ValueError('corpus root must be a triage-supervised registered corpus destination')
        root = local_path(ROOT, 'project/' + args.corpus.removeprefix('res://'))
        if args.report.resolve().is_relative_to(ROOT):
            raise ValueError('execution report must be outside the repository')
        inventory = read_json(root / 'inventory.json')
        if inventory.get('checkpoint') != 'imported' or inventory.get('imported') is not True or inventory.get('root') != args.corpus:
            raise ValueError('not an imported corpus inventory')
        from import_analyzer_corpus import default_policy, encoded, sha
        policy = default_policy()
        if inventory['counts'] != policy['counts'] or inventory['policy_sha256'] != sha(encoded(policy)):
            raise ValueError('imported inventory differs from the current pinned policy; regenerate the tree')
        records = validate_imported_tree(root, inventory)
        if len(set(args.case)) != len(args.case) or set(args.case) - set(records):
            raise ValueError('duplicate or unknown exact case selection')
        selected = args.case or sorted(records)
        version = subprocess.run([str(args.godot), '--version'], capture_output=True, text=True, check=True).stdout.strip()
        build_directory = ROOT / NATIVE_BUILD_DIRECTORY
        libraries = sorted((build_directory / 'bin').rglob('*template_debug*'))
        libraries = [path for path in libraries if path.is_file()]
        library, inspected_info, artifact_sha = select_library(args.library, libraries)
        expected_build_id = build_identity()
        if args.library is None:
            # Without an operator-pinned library, the build that recorded the descriptor is
            # the only one this run may host: a stale sibling artifact is an error, not a
            # fallback. An explicit --library pins a build the operator already chose, and
            # carries no descriptor to check here; its build id is still pinned, but only
            # once the first case process reports it, which costs a run to discover.
            descriptor = read_json(build_directory / 'native-artifact.json')
            for key in ('sha256', 'build_id'):
                if not isinstance(descriptor.get(key), str):
                    raise ValueError(f'native test artifact descriptor is missing {key}')
            require_equal('native test artifact', descriptor['sha256'], artifact_sha)
            require_equal('native build id', expected_build_id, descriptor['build_id'])
        report = {'schema_version': 1, 'checkpoint': 'imported', 'corpus': args.corpus,
                  'source_revision': inventory['foundry_revision'], 'inventory_sha256': digest(root / 'inventory.json'),
                  'baristascript_revision': run_git(ROOT, 'rev-parse', 'HEAD'),
                  'baristascript_worktree_diff_sha256': hashlib.sha256(subprocess.run(['git', '-C', str(ROOT), 'diff', 'HEAD'], capture_output=True, check=True).stdout).hexdigest(),
                  'build_target': 'template_debug', 'build_artifacts': {str(p.relative_to(ROOT)): digest(p) for p in libraries},
                  'selected_artifact': {'path': str(library), 'sha256': artifact_sha, 'build_info': inspected_info},
                  'build_info': None, 'checkout_info': checkout_info(),
                  'execution_files': {str(path.relative_to(ROOT)): digest(path) for path in
                                      sorted([ROOT / 'scripts/run_corpus_triage.py', ROOT / 'scripts/analyzer_corpus_policy.json',
                                              ROOT / 'tests/native/corpus_helpers.cpp', ROOT / 'tests/native/corpus_helpers.h',
                                              ROOT / 'tests/native/analyzer_corpus_test.cpp',
                                              ROOT / 'tests/native/native_test_runner.cpp', ROOT / 'tests/native/native_test_runner.h',
                                              ROOT / 'tests/native/native_corpus_arguments.cpp', ROOT / 'tests/native/native_corpus_arguments.h',
                                              ROOT / 'src/bs_corpus_evaluation.cpp', ROOT / 'src/bs_corpus_evaluation.h',
                                              ROOT / 'src/bs_corpus_sentinels.h'])},
                  'native_build_id': expected_build_id,
                  'godot': str(args.godot), 'godot_version': version, 'timeout_seconds': args.timeout,
                  'jobs': args.jobs,
                  'complete_population': not bool(args.case), 'selected_cases': selected,
                  'deferred_cases': [r['identity'] for r in inventory['sources'] if r.get('disposition') == 'deferred'],
                  'completed': False, 'results': [], 'stopped_cases': []}
        atomic_report(args.report, report)
        # Concurrent Godot processes must not share a staged project: each one owns the writable
        # engine state, descriptor and library copy that the artifact bracket below hashes.
        worker_count = max(1, min(args.jobs, len(selected)))
        idle_projects = queue.SimpleQueue()
        # A worker cannot see an interruption raised on the supervising thread, so a stop is
        # published here: no further case starts, and any case process still alive is killed.
        stop_requested = threading.Event()
        live_processes = set()
        killed_processes = set()
        live_lock = threading.Lock()

        def kill_and_record(process):
            killed_processes.add(process)
            kill_process_group(process)

        def note_process(process):
            with live_lock:
                live_processes.add(process)
                if stop_requested.is_set():
                    kill_and_record(process)

        def forget_process(process):
            with live_lock:
                live_processes.discard(process)

        def was_killed(process):
            with live_lock:
                return process in killed_processes

        def request_stop():
            with live_lock:
                # Setting the event under the same lock note_process holds closes the window in
                # which a process registers after the sweep below has already read the set.
                stop_requested.set()
                for process in list(live_processes):
                    kill_and_record(process)

        def run_case(case):
            if stop_requested.is_set():
                return None
            project = idle_projects.get()
            case_processes = []

            def remember_process(process):
                case_processes.append(process)
                note_process(process)

            try:
                copied = project / 'bin' / ('native' + library.suffix)
                before = digest(copied)
                require_equal('staged library before case', artifact_sha, before)
                nonce = uuid.uuid4().hex
                command = [str(args.godot), '--headless', '--path', str(project), '--main-loop',
                           'BaristaNativeTestRunner', '--', f'--native-suite={NATIVE_SUITE}',
                           f'--native-case={NATIVE_CORPUS_CASE}', f'--native-nonce={nonce}',
                           f'--corpus-root={args.corpus}', f'--corpus-case={case}']
                process = supervise(command, args.timeout, started=remember_process, finished=forget_process)
                if any(was_killed(child) for child in case_processes):
                    # Only a process the stop actually killed produces unusable output. A case
                    # that finished on its own keeps its result even if a sibling then failed.
                    return KILLED_BY_STOP
                try:
                    completion = native_completion(process['output'], nonce, inspected_info, expected_build_id)
                    completion_error = None
                except ValueError as error:
                    completion, completion_error = None, str(error)
                record = result_record(process, case, args.corpus, records[case]['expected_block'], completion)
                attach_build_info(record, completion, completion_error)
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
                return record
            except BaseException:
                # This set must stay here rather than move into the supervising thread's handler:
                # that thread only observes the exception after the pool has already handed this
                # worker the next case, so the worker itself is the last point still ahead of the
                # next dequeue. Setting it outside live_lock is safe because request_stop sets it
                # again under the lock before sweeping live_processes.
                stop_requested.set()
                raise
            finally:
                idle_projects.put(project)

        with ExitStack() as stack:
            for _ in range(worker_count):
                project = stack.enter_context(staged_project({'library': str(library)}))
                prepare_triage_project(project)
                validate_imported_tree(project / args.corpus.removeprefix('res://'), inventory, project_root=project)
                idle_projects.put(project)
            # Only this thread touches the report, so every write stays a consistent snapshot.
            finished = {}
            stopped = set()

            def absorb(case, outcome):
                if outcome is KILLED_BY_STOP:
                    stopped.add(case)
                elif outcome is not None:
                    finished[case] = outcome

            def publish_finished_records():
                # Case order, not completion order, and the newest loaded identity that survives it.
                report['results'] = [finished[name] for name in selected if name in finished]
                # Selected cases absent from both lists were never dispatched, which a consumer
                # cannot otherwise tell apart from a case this run killed mid-flight.
                report['stopped_cases'] = sorted(stopped)
                for candidate in report['results']:
                    if candidate['build_info'] is not None:
                        report['build_info'] = candidate['build_info']
                atomic_report(args.report, report)

            pool = stack.enter_context(ThreadPoolExecutor(max_workers=worker_count))
            futures = {pool.submit(run_case, case): case for case in selected}
            try:
                for future in as_completed(futures):
                    case = futures[future]
                    outcome = future.result()
                    absorb(case, outcome)
                    if case not in finished:
                        continue
                    # Each completed record is durable even if the supervisor is interrupted.
                    publish_finished_records()
                    print(f'{len(finished)}/{len(selected)} {outcome["terminal"]} {case}', flush=True)
            except BaseException:
                # A failing or interrupted run must not keep spawning further case processes,
                # and must not leave one behind; the cases that did finish stay recorded.
                request_stop()
                pool.shutdown(wait=True, cancel_futures=True)
                for future, case in futures.items():
                    if case in finished or case in stopped or future.cancelled() or not future.done():
                        continue
                    if future.exception() is None:
                        absorb(case, future.result())
                publish_finished_records()
                raise
        report['completed'] = True
        report['summary'] = dict(sorted(Counter(r['terminal'] for r in report['results']).items()))
        report['unowned_failures'] = [r['case'] for r in report['results'] if not r['passed'] and not owned_failure(r)]
        complaints = expected_failure_complaints(report['results'], inventory['ledger']['expected_failures'], selected)
        report['expected_failure_complaints'] = complaints
        atomic_report(args.report, report)
        print(json.dumps(report['summary'], sort_keys=True))
        for complaint in complaints:
            print(f'corpus triage: {complaint}', file=sys.stderr)
        return 1 if complaints else 0
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        if report is not None:
            report['infrastructure_error'] = str(error)
            atomic_report(args.report, report)
        print(f'corpus triage: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
