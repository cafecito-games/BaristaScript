#!/usr/bin/env python3
# test_run_corpus_triage.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Corpus execution retains same-process build identity and durable partial reports."""
import argparse
from contextlib import ExitStack, contextmanager, redirect_stderr
import io
import json
from pathlib import Path
import os
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_config
import build_metadata
import run_corpus_triage as triage
import run_native_suites as native

GODOT = LIBRARY = None


def metadata():
    config = build_config.load_config()
    return dict(schema=1, extension_version=config['extension_version'], config_sha256=build_config.config_fingerprint(config),
                godot_api=config['godot_api'], godot_runtime=config['godot_runtime'],
                source=dict(revision='1' * 40, state='clean'),
                godot_cpp=dict(revision='2' * 40, selected_revision='2' * 40, state='clean'),
                build=dict(platform='macos', architecture='arm64', target='template_debug', precision=config['precision'], native_tests=True))


BUILD_ID = 'b' * 64
# The triage-supervised corpus destinations the registry declares.
ANALYZER_ROOT = 'res://tests/corpus/analyzer'
RUNTIME_ROOT = 'res://tests/corpus/runtime'
ANALYZER_PROFILE = triage.corpus_profile(ANALYZER_ROOT)


def native_completion_line(nonce, *, cases=1, assertions=3, failed_cases=0, failed_assertions=0,
                           suite=ANALYZER_PROFILE['suite'], build_id=BUILD_ID, case=triage.NATIVE_CORPUS_CASE):
    return native.RESULT_PREFIX + json.dumps(dict(
        protocol=native.PROTOCOL_VERSION, suite=suite, case=case, nonce=nonce,
        build_id=build_id, build_info=metadata(), cases=cases, assertions=assertions,
        failed_cases=failed_cases, failed_assertions=failed_assertions))


class IdentityTests(unittest.TestCase):
    def test_selection_requires_an_unambiguous_host_native_test_artifact(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(triage.sys, 'platform', 'darwin'):
            root = Path(temporary)
            library = root / 'bin' / 'lib.template_debug.dylib'
            library.parent.mkdir()
            library.write_bytes(build_metadata.encode_envelope(metadata()))
            other = library.with_name('second.template_debug.dylib')
            other.write_bytes(library.read_bytes())
            self.assertEqual(triage.select_library(library, [library, other])[0], library.resolve())
            self.assertEqual(triage.select_library(None, [library])[0], library.resolve())
            with self.assertRaisesRegex(ValueError, 'unambiguous'):
                triage.select_library(None, [library, other])
            with self.assertRaisesRegex(ValueError, 'unambiguous'):
                triage.select_library(None, [])
            # analyzer_corpus is absent from an ordinary debug build; that is an error, not a fallback.
            ordinary = metadata(); ordinary['build']['native_tests'] = False
            library.write_bytes(build_metadata.encode_envelope(ordinary))
            with self.assertRaisesRegex(ValueError, 'barista_tests'):
                triage.select_library(library, [library])

    def test_only_triage_supervised_roots_may_carry_a_stage_manifest(self):
        # The GDScript-supervised parser destination is excluded too: this supervisor has no
        # business restaging a corpus whose cases the corpus runner already pins as a whole.
        roots = triage.permitted_corpus_roots()
        self.assertEqual(roots, {'res://tests/corpus/analyzer', 'res://tests/corpus/runtime'})
        for planted in ('res://tests/corpus', 'res://tests/planted/analyzer',
                        'res://tests/corpus/parser', 'res://tests/corpus_staging/analyzer',
                        'res://tests/corpus/analyzer/errors', 'user://analyzer'):
            self.assertNotIn(planted, roots)

    def test_completion_record_never_turns_a_failed_case_into_a_pass(self):
        info = metadata()
        output = native_completion_line('n')
        for terminal in ('passed', 'mismatch', 'crash', 'timeout'):
            record = dict(terminal=terminal, passed=terminal == 'passed', output=output)
            completion = triage.native_completion(record['output'], 'n', info, BUILD_ID, ANALYZER_PROFILE['suite'])
            triage.attach_build_info(record, completion, None)
            self.assertEqual(record['terminal'], terminal)
            self.assertEqual(record['passed'], terminal == 'passed')
            self.assertEqual(record['build_info'], info)
        for text in ('', output + '\n' + output, output.replace('"n"', '"wrong"'),
                     output.replace('1' * 40, '3' * 40),
                     native_completion_line('n', suite='runner_failure'),
                     # A library built from other sources cannot attest to the sources this
                     # report hashes, however well its artifact hash matches a descriptor.
                     native_completion_line('n', build_id='c' * 64)):
            record = dict(terminal='passed', passed=True, output=text)
            with self.assertRaises(ValueError):
                triage.native_completion(text, 'n', info, BUILD_ID, ANALYZER_PROFILE['suite'])
            triage.attach_build_info(record, None, 'rejected')
            self.assertFalse(record['passed'])
            self.assertEqual(record['terminal'], 'build_info_error')
            self.assertIsNone(record['build_info'])
            self.assertTrue(record['build_info_error'])

    def test_a_completion_record_that_disagrees_with_the_payload_is_not_a_pass(self):
        case, corpus = 'errors/case.barista', ANALYZER_ROOT
        payload = dict(path=corpus + '/' + case, passed=True, expected='BS_TEST_OK', actual='BS_TEST_OK')
        process = dict(output='BS_CASE_RESULT ' + json.dumps(payload) + '\nBS_CASE_RAN ' + case,
                       exit_code=0, timed_out=False, duration_seconds=0.1)
        agreeing = dict(cases=1, assertions=3, failed_cases=0, failed_assertions=0, build_info=metadata())
        self.assertEqual(triage.result_record(process, case, corpus, 'BS_TEST_OK', agreeing)['terminal'], 'passed')
        for disagreeing in (dict(agreeing, cases=2), dict(agreeing, assertions=0),
                            dict(agreeing, failed_assertions=1),
                            dict(agreeing, failed_cases=1, failed_assertions=1)):
            self.assertEqual(triage.result_record(process, case, corpus, 'BS_TEST_OK', disagreeing)['terminal'],
                             'inconsistent_completion')
        # A failing payload equally requires the suite to have reported the failure.
        failing = dict(payload, passed=False, actual='wrong')
        failed_process = dict(process, exit_code=1,
                              output='BS_CASE_RESULT ' + json.dumps(failing) + '\nBS_CASE_RAN ' + case)
        self.assertEqual(triage.result_record(failed_process, case, corpus, 'BS_TEST_OK', agreeing)['terminal'],
                         'inconsistent_completion')
        reported = dict(agreeing, failed_cases=1, failed_assertions=1)
        self.assertEqual(triage.result_record(failed_process, case, corpus, 'BS_TEST_OK', reported)['terminal'],
                         'mismatch')

    def test_interruption_kills_and_reaps_the_actual_child(self):
        original_popen = subprocess.Popen
        children = []

        def start(*args, **kwargs):
            child = original_popen(*args, **kwargs)
            children.append(child)
            communicate = child.communicate
            first = True

            def interrupted(*args, **kwargs):
                nonlocal first
                if first:
                    first = False
                    raise KeyboardInterrupt()
                return communicate(*args, **kwargs)

            child.communicate = interrupted
            return child

        with patch.object(triage.subprocess, 'Popen', side_effect=start):
            self.assertRaises(KeyboardInterrupt, triage.supervise,
                              [sys.executable, '-c', 'import time; time.sleep(20)'], 30)
        self.assertEqual(len(children), 1)
        self.assertIsNotNone(children[0].poll())
        self.assertLess(children[0].returncode, 0)


class ReportTests(unittest.TestCase):
    @contextmanager
    def fixture(self, case_limit=2):
        import import_analyzer_corpus as importer
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            root = base / 'checkout'
            source = base / 'input'
            shutil.copytree(ROOT / 'tests/fixtures/analyzer_import/scripts', source)
            policy = importer.default_policy()
            fixture_cases = {
                path.relative_to(source / 'analyzer').as_posix().removesuffix('.fs') + '.barista'
                for path in (source / 'analyzer').rglob('*.fs') if not path.name.endswith('.notest.fs')
            }
            policy['deferred'] = {
                path: reason for path, reason in policy['deferred'].items() if path in fixture_cases
            }
            policy['excluded'] = {
                path: reason for path, reason in policy['excluded'].items() if path in fixture_cases
            }
            policy.update(counts=None,
                          owners={path: owner for path, owner in policy['owners'].items()
                                  if path in policy['deferred'] or path in policy['excluded']},
                          expectation_edits={}, expectation_overrides={})
            policy['rewritten'] = {
                path: reason for path, reason in policy['rewritten'].items()
                if path in fixture_cases
            }
            fixture_sources = {
                path.relative_to(source).as_posix()
                for path in source.rglob('*.fs')
            }
            policy['source_edits'] = {
                path: edits for path, edits in policy['source_edits'].items()
                if path in fixture_sources
            }
            self.assertLessEqual(set(policy['rewritten']), fixture_cases)
            self.assertLessEqual(set(policy['source_edits']), fixture_sources)
            uri = 'res://tests/corpus/analyzer'
            policy['counts'] = importer.inventory_sources(source, policy, uri)['counts']
            inventory = importer.inventory_sources(source, policy, uri)
            corpus = root / 'project/tests/corpus/analyzer'
            # The native suite lives in the compiled library, so no GDScript corpus runner
            # is staged here: the fixture proves the run no longer depends on one.
            files = ('project/project.godot', 'project/.godot/extension_list.cfg',
                     'project/tests/corpus_support/parser/utils.notest.barista',
                     'project/tests/corpus_support/parser/source_map.json',
                     'scripts/corpus_sources.json', 'scripts/run_corpus_triage.py',
                     'scripts/import_parser_corpus.py', 'scripts/import_analyzer_corpus.py',
                     'scripts/import_runtime_corpus.py',
                     'src/bs_corpus_sentinels.h', 'src/bs_corpus_evaluation.cpp', 'src/bs_corpus_evaluation.h',
                     'tests/native/corpus_helpers.cpp', 'tests/native/corpus_helpers.h',
                     'tests/native/analyzer_corpus_test.cpp',
                     'tests/native/native_test_runner.cpp', 'tests/native/native_test_runner.h',
                     'tests/native/native_corpus_arguments.cpp', 'tests/native/native_corpus_arguments.h')
            for name in files:
                destination = root / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / name, destination)
            api_source = build_config.api_path(build_config.load_config(), ROOT)
            api_relative = api_source.relative_to(ROOT)
            api_destination = root / api_relative
            api_destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(api_source, api_destination)
            self.assertEqual(api_source.read_bytes(), api_destination.read_bytes())
            importer.write_tree(inventory, source, corpus, project_root=root / 'project')
            (root / 'scripts/analyzer_corpus_policy.json').write_text(json.dumps(policy))
            library = root / triage.NATIVE_BUILD_DIRECTORY / 'bin/libbarista_script.macos.template_debug.dylib'
            library.parent.mkdir(parents=True)
            library.write_bytes(build_metadata.encode_envelope(metadata()))
            other = library.with_name('other.template_debug.dll')
            other.write_bytes(b'other candidate remains hashed')
            (root / triage.NATIVE_BUILD_DIRECTORY / 'native-artifact.json').write_text(
                json.dumps(dict(library=str(library), sha256=triage.digest(library), build_id=BUILD_ID)))
            for command in (['init', '-q'], ['add', '.'], ['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                                                        'commit', '-qm', 'fixture']):
                subprocess.run(['git', '-C', str(root), *command], check=True, capture_output=True)
            cases = sorted(triage.validate_imported_tree(corpus, inventory, ANALYZER_PROFILE, project_root=root / 'project'))[:case_limit]
            report = base / 'report.json'
            checkout = dict(source=dict(revision='c' * 40, state='dirty'), config_sha256='d' * 64)
            real_run = subprocess.run

            def command_run(command, *args, **kwargs):
                if command == ['fake-godot', '--version']:
                    return subprocess.CompletedProcess(command, 0, 'actual-host-version\n', '')
                return real_run(command, *args, **kwargs)

            with ExitStack() as stack:
                for target, name, value in ((triage, 'ROOT', root), (native, 'ROOT', root),
                                            (triage.sys, 'platform', 'darwin')):
                    stack.enter_context(patch.object(target, name, value))
                stack.enter_context(patch.object(triage, 'build_identity', return_value=BUILD_ID))
                stack.enter_context(patch.object(importer, 'default_policy', return_value=policy))
                stack.enter_context(patch.object(triage, 'checkout_info', return_value=checkout))
                stack.enter_context(patch.object(native, 'godot_data_path', return_value=base / 'userdata'))
                stack.enter_context(patch.object(triage.subprocess, 'run', side_effect=command_run))
                yield root, library, report, inventory, cases, checkout

    def exercise(self, mode, *, jobs=1, case_limit=2, staggered=False, explicit_library=True):
        with self.fixture(case_limit) as (root, library, report_path, inventory, cases, checkout):
            records = {r['imported_path']: r for r in inventory['sources'] if r['role'] == 'case'}
            original_hashes = {str(p.relative_to(root)): triage.digest(p) for p in root.rglob('*') if p.is_file() and '.git' not in p.parts}
            projects = []
            started_cases = []

            def case_process(command, timeout, **process_hooks):
                project = Path(command[command.index('--path') + 1])
                projects.append(project)
                self.assertNotEqual(project, root / 'project')
                selected_api = build_config.api_path(build_config.load_config(), root)
                staged_api = project.parent / selected_api.relative_to(root)
                self.assertEqual(selected_api.read_bytes(), staged_api.read_bytes())
                self.assertIn('debug = "res://bin/native.dylib"', (project / 'bin/barista_script.gdextension').read_text())
                self.assertIn('release = "res://bin/native.dylib"', (project / 'bin/barista_script.gdextension').read_text())
                self.assertEqual(list((project / 'bin').glob('*.dylib')), [project / 'bin/native.dylib'])
                self.assertEqual(command[command.index('--main-loop') + 1], 'BaristaNativeTestRunner')
                self.assertIn(f'--native-suite={ANALYZER_PROFILE["suite"]}', command)
                self.assertIn(f'--native-case={triage.NATIVE_CORPUS_CASE}', command)
                self.assertIn(f'--corpus-root={inventory["root"]}', command)
                self.assertNotIn('res://tests/corpus_runner.gd', command)
                case = next(argument.removeprefix('--corpus-case=') for argument in command
                            if argument.startswith('--corpus-case='))
                nonce = next(argument.removeprefix('--native-nonce=') for argument in command
                             if argument.startswith('--native-nonce='))
                started_cases.append(case)
                if mode == 'fail':
                    # Every worker is dispatched before the failure reports, and the cases
                    # already in flight when it does outlive it and finish cleanly.
                    time.sleep(0.15 if case == cases[1] else 0.6)
                    if case == cases[1]:
                        raise ValueError('case process failure')
                if staggered:
                    # Reversed delays make completion order disagree with case order.
                    time.sleep(0.2 * (len(cases) - cases.index(case)))
                if mode == 'interrupt' and case == cases[1]:
                    raise KeyboardInterrupt()
                if mode in ('crash', 'timeout') and case == cases[0]:
                    # A host that died or was killed leaves no guarded record behind, which is
                    # exactly why neither can be adjudicated against an expectation.
                    return dict(output='', exit_code=-signal.SIGSEGV if mode == 'crash' else 0,
                                timed_out=mode == 'timeout', duration_seconds=0.1)
                expected = records[case]['expected_block']
                payload = dict(path=inventory['root'] + '/' + case, passed=True, expected=expected, actual=expected)
                output = 'BS_CASE_RESULT ' + json.dumps(payload) + '\nBS_CASE_RAN ' + case + '\n'
                if mode != 'missing' or case != cases[0]:
                    output += native_completion_line(nonce) + '\n'
                if mode == 'replace' and case == cases[0]:
                    (project / 'bin/native.dylib').write_bytes(b'replaced')
                return dict(output=output, exit_code=0, timed_out=False, duration_seconds=0.001)

            arguments = ['--godot', 'fake-godot', '--corpus', inventory['root'],
                         '--report', str(report_path), '--execution', 'isolated', '--jobs', str(jobs)]
            if explicit_library:
                arguments += ['--library', str(library)]
            for case in cases:
                arguments += ['--case', case]
            snapshots = []
            real_atomic_report = triage.atomic_report

            def recording_atomic_report(path, document):
                snapshots.append([record['case'] for record in document['results']])
                real_atomic_report(path, document)

            with patch.object(triage, 'supervise', side_effect=case_process), \
                    patch.object(triage, 'atomic_report', side_effect=recording_atomic_report):
                if mode == 'interrupt':
                    self.assertRaises(KeyboardInterrupt, triage.main, arguments)
                    result = None
                else:
                    result = triage.main(arguments)
            report = json.loads(report_path.read_text())
            self.assertTrue(projects)
            self.assertTrue(all(not project.parent.exists() for project in projects))
            self.assertEqual(report['selected_cases'], cases)
            self.assertEqual(report['execution'], 'isolated')
            self.assertEqual(report['library_bracket_scope'], 'per_case')
            self.assertFalse(report['complete_population'])
            self.assertEqual(report['inventory_sha256'], triage.digest(root / 'project/tests/corpus/analyzer/inventory.json'))
            self.assertEqual(report['checkout_info'], checkout)
            self.assertEqual(report['build_info'], metadata())
            self.assertNotEqual(report['build_info']['source'], report['checkout_info']['source'])
            self.assertEqual(len(report['build_artifacts']), 2)
            self.assertEqual(set(report['execution_files']),
                             {'scripts/run_corpus_triage.py', 'scripts/analyzer_corpus_policy.json',
                              'src/bs_corpus_evaluation.cpp', 'src/bs_corpus_evaluation.h',
                              'src/bs_corpus_sentinels.h',
                              'tests/native/corpus_helpers.cpp', 'tests/native/corpus_helpers.h',
                              'tests/native/analyzer_corpus_test.cpp',
                              'tests/native/native_test_runner.cpp', 'tests/native/native_test_runner.h',
                              'tests/native/native_corpus_arguments.cpp', 'tests/native/native_corpus_arguments.h'})
            self.assertEqual(report['native_build_id'], BUILD_ID)
            self.assertEqual(report['godot_version'], 'actual-host-version')
            for record in report['results']:
                source = records[record['case']]
                for field, source_field in (('identity', 'identity'), ('source_sha256', 'sha256'),
                                            ('staged_source_sha256', 'imported_sha256'), ('expectation_sha256', 'expectation_sha256'),
                                            ('semantic_owner', 'semantic_owner'), ('candidate_observations', 'candidate_observations')):
                    self.assertEqual(record[field], source[source_field])
            self.assertEqual(original_hashes, {str(p.relative_to(root)): triage.digest(p) for p in root.rglob('*') if p.is_file() and '.git' not in p.parts})
            self.staged_projects = list(projects)
            self.started_cases = list(started_cases)
            self.report_snapshots = snapshots
            return result, report

    def test_complete_report_preserves_population_hashes_and_stale_loaded_identity(self):
        result, report = self.exercise('complete')
        self.assertEqual(result, 0)
        self.assertTrue(report['completed'])
        self.assertEqual(report['summary'], {'passed': 2})
        self.assertTrue(all(r['library_sha256_before'] == r['library_sha256_after'] == report['selected_artifact']['sha256'] for r in report['results']))

    def test_interrupt_keeps_completed_case_durable_and_cleans_staging(self):
        _, report = self.exercise('interrupt')
        self.assertFalse(report['completed'])
        self.assertEqual(len(report['results']), 1)
        self.assertTrue(report['results'][0]['passed'])

    def test_missing_metadata_cannot_pass_and_does_not_abort_other_cases(self):
        result, report = self.exercise('missing')
        self.assertEqual(result, 1)
        self.assertTrue(report['completed'])
        self.assertEqual(report['summary'], {'build_info_error': 1, 'passed': 1})

    def test_replaced_library_is_recorded_then_stops_further_execution(self):
        result, report = self.exercise('replace')
        self.assertEqual(result, 2)
        self.assertFalse(report['completed'])
        self.assertEqual(len(report['results']), 1)
        self.assertEqual(report['results'][0]['terminal'], 'artifact_changed')
        self.assertIn('staged library before case', report['infrastructure_error'])

    @staticmethod
    def comparable(report):
        """Reduce a report to the recorded outcomes, dropping per-process timing and paths."""
        volatile = ('duration_seconds', 'command', 'output')
        outcomes = [{key: value for key, value in record.items() if key not in volatile}
                    for record in report['results']]
        return dict(completed=report['completed'], summary=report['summary'], results=outcomes,
                    selected_cases=report['selected_cases'], unowned_failures=report['unowned_failures'],
                    build_info=report['build_info'])

    def test_concurrent_jobs_reproduce_serial_outcomes_in_case_order(self):
        serial_result, serial = self.exercise('complete', case_limit=6)
        serial_projects = set(self.staged_projects)
        parallel_result, parallel = self.exercise('complete', jobs=4, case_limit=6, staggered=True)
        parallel_projects, snapshots = set(self.staged_projects), self.report_snapshots
        self.assertEqual(serial_result, 0)
        self.assertEqual(parallel_result, 0)
        self.assertEqual(len(serial['results']), 6)
        self.assertEqual([record['case'] for record in parallel['results']], parallel['selected_cases'])
        self.assertEqual(self.comparable(serial), self.comparable(parallel))
        self.assertEqual(parallel['jobs'], 4)
        self.assertEqual(len(serial_projects), 1)
        self.assertEqual(len(parallel_projects), 4, 'each concurrent worker needs its own staged project')
        # Every case completion writes the report (the last write is the completed summary),
        # and completion order is genuinely not case order.
        populated = [snapshot for snapshot in snapshots if snapshot]
        self.assertEqual([len(snapshot) for snapshot in populated], [1, 2, 3, 4, 5, 6, 6])
        self.assertTrue(all(snapshot == sorted(snapshot) for snapshot in populated))
        self.assertNotEqual(populated[0], parallel['selected_cases'][:1],
                            'staggered cases did not actually complete out of order')

    def test_a_failing_case_stops_the_run_before_the_next_case_starts(self):
        # Default jobs must start and record exactly what a serial supervisor did.
        result, report = self.exercise('fail', case_limit=6)
        self.assertEqual(result, 2)
        self.assertEqual(self.started_cases, report['selected_cases'][:2])
        self.assertEqual([record['case'] for record in report['results']], report['selected_cases'][:1])
        self.assertEqual(report['stopped_cases'], [])
        self.assertIn('case process failure', report['infrastructure_error'])
        for jobs in (4, 8):
            result, report = self.exercise('fail', jobs=jobs, case_limit=12)
            self.assertEqual(result, 2)
            dispatched = report['selected_cases'][:jobs]
            # Cases are dispatched in order, so the failure stops the run within one window.
            self.assertEqual(sorted(self.started_cases), dispatched)
            # A sibling's failure must not discard a case that had already finished cleanly.
            self.assertEqual([record['case'] for record in report['results']],
                             [case for case in dispatched if case != report['selected_cases'][1]])
            self.assertEqual(report['stopped_cases'], [])

    def test_main_thread_interrupt_kills_case_processes_without_waiting_for_the_timeout(self):
        with self.fixture(case_limit=4) as (root, library, report_path, inventory, cases, checkout):
            children = []
            original_popen = subprocess.Popen

            def start(command, *positional, **keyword):
                if isinstance(command, list) and '--headless' in command:
                    command = [sys.executable, '-c', 'import time; time.sleep(30)']
                    child = original_popen(command, *positional, **keyword)
                    children.append(child)
                    return child
                return original_popen(command, *positional, **keyword)

            interrupt_sent = threading.Event()

            def interrupt_when_all_cases_are_running():
                # If the run aborts before dispatching every case, send nothing: the assertions
                # below then report that instead of this thread spinning forever.
                deadline = time.monotonic() + 30
                while len(children) < len(cases) and time.monotonic() < deadline:
                    time.sleep(0.01)
                if len(children) == len(cases):
                    interrupt_sent.set()
                    # A real Ctrl-C is a signal to this process, handled on the main thread.
                    os.kill(os.getpid(), signal.SIGINT)

            arguments = ['--godot', 'fake-godot', '--library', str(library), '--corpus', inventory['root'],
                         '--report', str(report_path), '--execution', 'isolated',
                         '--jobs', str(len(cases)), '--timeout', '30']
            for case in cases:
                arguments += ['--case', case]
            interrupter = threading.Thread(target=interrupt_when_all_cases_are_running)
            try:
                with patch.object(triage.subprocess, 'Popen', side_effect=start):
                    interrupter.start()
                    began = time.monotonic()
                    self.assertRaises(KeyboardInterrupt, triage.main, arguments)
                    elapsed = time.monotonic() - began
            finally:
                interrupter.join(45)
                for child in children:
                    if child.poll() is None:
                        os.killpg(child.pid, signal.SIGKILL)
                    child.communicate()
            self.assertFalse(interrupter.is_alive())
            self.assertTrue(interrupt_sent.is_set(), 'the run never dispatched every case')
            self.assertEqual(len(children), len(cases))
            for child in children:
                self.assertIsNotNone(child.poll(), 'a case process outlived the interrupted run')
                self.assertLess(child.returncode, 0)
            self.assertLess(elapsed, 10, 'the interrupted run waited on the per-case timeout')
            report = json.loads(report_path.read_text())
            self.assertFalse(report['results'])
            # Killed mid-flight is a distinct state from never dispatched.
            self.assertEqual(report['stopped_cases'], sorted(cases))

    def test_non_positive_jobs_is_rejected(self):
        with self.fixture() as (root, library, report_path, inventory, cases, checkout):
            arguments = ['--godot', 'fake-godot', '--library', str(library), '--corpus', inventory['root'],
                         '--report', str(report_path)]
            for value in ('0', '-3'):
                stream = io.StringIO()
                with redirect_stderr(stream):
                    self.assertEqual(triage.main(arguments + ['--jobs', value]), 2)
                self.assertIn('jobs', stream.getvalue())
                self.assertFalse(report_path.exists())

    def test_an_unregistered_root_is_refused_before_any_case_runs(self):
        with self.fixture() as (root, library, report_path, inventory, cases, checkout):
            planted = root / 'project/tests/planted/analyzer'
            shutil.copytree(root / 'project/tests/corpus/analyzer', planted)
            # A planted manifest in an unregistered tree can restage a case at a stage it was
            # never adjudicated for, so the root itself has to be refused.
            arguments = ['--godot', 'fake-godot', '--library', str(library), '--report', str(report_path),
                         '--corpus', 'res://tests/planted/analyzer']
            stream = io.StringIO()
            with patch.object(triage, 'supervise', side_effect=AssertionError('no case may run')), \
                    redirect_stderr(stream):
                self.assertEqual(triage.main(arguments), 2)
            self.assertIn('registered corpus destination', stream.getvalue())
            self.assertFalse(report_path.exists())

    def test_the_recorded_native_artifact_selects_the_library(self):
        result, report = self.exercise('complete', explicit_library=False)
        self.assertEqual(result, 0)
        self.assertEqual(report['selected_artifact']['build_info']['build']['native_tests'], True)

    def test_a_malformed_native_artifact_descriptor_is_an_infrastructure_error(self):
        with self.fixture() as (root, library, report_path, inventory, cases, checkout):
            descriptor = root / triage.NATIVE_BUILD_DIRECTORY / 'native-artifact.json'
            arguments = ['--godot', 'fake-godot', '--corpus', inventory['root'], '--report', str(report_path)]
            for document in (dict(library=str(library)), dict(library=str(library), sha256=triage.digest(library)),
                             dict(library=str(library), sha256=triage.digest(library), build_id=None)):
                descriptor.write_text(json.dumps(document))
                stream = io.StringIO()
                with patch.object(triage, 'supervise', side_effect=AssertionError('no case may run')), \
                        redirect_stderr(stream):
                    # Exit 2 is an infrastructure error; exit 1 would claim the corpus itself failed.
                    self.assertEqual(triage.main(arguments), 2)
                self.assertIn('descriptor is missing', stream.getvalue())

    def test_a_library_the_native_artifact_descriptor_does_not_name_is_refused(self):
        with self.fixture() as (root, library, report_path, inventory, cases, checkout):
            descriptor = root / triage.NATIVE_BUILD_DIRECTORY / 'native-artifact.json'
            descriptor.write_text(json.dumps(dict(library=str(library), sha256='f' * 64, build_id=BUILD_ID)))
            arguments = ['--godot', 'fake-godot', '--corpus', inventory['root'], '--report', str(report_path)]
            stream = io.StringIO()
            with patch.object(triage, 'supervise', side_effect=AssertionError('no case may run')), \
                    redirect_stderr(stream):
                self.assertEqual(triage.main(arguments), 2)
            self.assertIn('native test artifact', stream.getvalue())

    def test_a_library_older_than_the_sources_the_report_hashes_is_refused(self):
        with self.fixture() as (root, library, report_path, inventory, cases, checkout):
            # Editing a native source without rebuilding leaves the artifact hash matching the
            # descriptor while execution_files would attest to sources that never ran.
            descriptor = root / triage.NATIVE_BUILD_DIRECTORY / 'native-artifact.json'
            descriptor.write_text(json.dumps(dict(library=str(library), sha256=triage.digest(library),
                                                  build_id='a' * 64)))
            arguments = ['--godot', 'fake-godot', '--corpus', inventory['root'], '--report', str(report_path)]
            stream = io.StringIO()
            with patch.object(triage, 'supervise', side_effect=AssertionError('no case may run')), \
                    redirect_stderr(stream):
                self.assertEqual(triage.main(arguments), 2)
            self.assertIn('native build id', stream.getvalue())

    def test_a_crashed_or_timed_out_case_fails_the_run_and_never_reaches_the_pin(self):
        """E4, end to end: neither terminal is a corpus result, so neither is pinnable."""
        for mode in ('crash', 'timeout'):
            result, report = self.exercise(mode)
            self.assertEqual(result, 1, (mode, report['summary']))
            crashed = report['results'][0]
            self.assertEqual(crashed['terminal'], mode)
            self.assertFalse(crashed['passed'])
            self.assertEqual(report['unadjudicated_failures'], [crashed['case']])
            self.assertTrue(any(complaint.startswith(crashed['case'] + ':')
                                and 'is not an adjudicated corpus result' in complaint
                                for complaint in report['expected_failure_complaints']),
                            report['expected_failure_complaints'])
            # The remedy the complaint names is never "pin it", so the pin must not be able to
            # silence it. Re-adjudicating the same outcomes with the case pinned still complains.
            self.assertEqual(
                len(triage.expected_failure_complaints(report['results'], [crashed['case']], [crashed['case']])), 1)

class FastPathTests(unittest.TestCase):
    """The whole-corpus path and, above all, its refusal to report a partial run as a whole one."""

    fixture = ReportTests.fixture

    @staticmethod
    def slice_bounds(total, shard, shards):
        """The same contiguous split corpus_shard_slice performs in C++."""
        base, remainder = divmod(total, shards)
        begin = shard * base + min(shard, remainder)
        return begin, begin + base + (1 if shard < remainder else 0)

    def shard_output(self, inventory, records, *, nonce, shard, shards, order='ascending',
                     planned=None, emitted=None, completed=None, announce_completion=True,
                     root=None, total=None, start=None, failing=()):
        pinned = set(inventory['ledger']['expected_failures']) | set(failing)
        population = sorted(records)
        if order == 'reverse':
            population.reverse()
        begin, finish = self.slice_bounds(len(population), shard, shards)
        cases = population[begin:finish]
        lines = [triage.CORPUS_PLAN_PREFIX + json.dumps(dict(
            planned=len(cases) if planned is None else planned, order=order,
            root=inventory['root'] if root is None else root, shard=shard, shards=shards,
            total=len(population) if total is None else total,
            start=begin if start is None else start))]
        for case in cases[:len(cases) if emitted is None else emitted]:
            expected = records[case]['expected_block']
            if case in pinned:
                payload = dict(passed=False, reason=4, path=inventory['root'] + '/' + case,
                               expectation_path=inventory['root'] + '/' + case.removesuffix('.barista') + '.out',
                               message='output mismatch', expected=expected, actual='diverged block',
                               fixture_index={}, analysis_ran=True)
            else:
                payload = dict(passed=True, path=inventory['root'] + '/' + case, expected=expected,
                               actual=expected, analysis_ran=True, fixture_index={})
            lines.append(triage.CASE_RESULT_PREFIX + json.dumps(payload))
            lines.append(triage.CASE_GUARD_PREFIX + case)
        if announce_completion:
            lines.append(triage.CORPUS_COMPLETE_PREFIX + json.dumps(dict(
                planned=len(cases) if planned is None else planned,
                completed=len(cases) if completed is None else completed, shard=shard, shards=shards)))
        lines.append(native_completion_line(nonce, case=triage.NATIVE_WHOLE_CORPUS_CASE))
        return '\n'.join(lines) + '\n'

    @contextmanager
    def fast_run(self, *, shards=1, order='ascending', extra_arguments=(), damaged_shard=None,
                 exit_code=0, **damage):
        with self.fixture() as (root, library, report_path, inventory, cases, checkout):
            records = {record['imported_path']: record for record in inventory['sources']
                       if record['role'] == 'case' and record['disposition'] not in ('excluded', 'deferred')}
            commands = []

            def shard_process(command, timeout, **hooks):
                commands.append(command)
                nonce = next(argument.removeprefix('--native-nonce=') for argument in command
                             if argument.startswith('--native-nonce='))
                shard = int(next(argument.removeprefix('--corpus-shard=') for argument in command
                                 if argument.startswith('--corpus-shard=')))
                declared = int(next(argument.removeprefix('--corpus-shards=') for argument in command
                                    if argument.startswith('--corpus-shards=')))
                applied = damage if damaged_shard in (shard, 'every') else {}
                return dict(output=self.shard_output(inventory, records, nonce=nonce, shard=shard,
                                                     shards=declared, order=order, **applied),
                            exit_code=exit_code if damaged_shard in (shard, 'every') else 0,
                            timed_out=False, duration_seconds=1.5 + shard)

            arguments = ['--godot', 'fake-godot', '--library', str(library), '--corpus', inventory['root'],
                         '--report', str(report_path), '--shards', str(shards), *extra_arguments]
            stderr = io.StringIO()
            with patch.object(triage, 'supervise', side_effect=shard_process), redirect_stderr(stderr):
                result = triage.main(arguments)
            yield result, json.loads(report_path.read_text()), stderr.getvalue(), commands, inventory, records

    def test_an_unsharded_run_is_one_process_and_adjudicates_the_pin(self):
        with self.fast_run() as (result, report, stderr, commands, inventory, records):
            self.assertEqual(result, 0, stderr)
            self.assertEqual(len(commands), 1, 'an unsharded fast run spawns exactly one Godot process')
            self.assertIn(f'--native-case={triage.NATIVE_WHOLE_CORPUS_CASE}', commands[0])
            self.assertIn(f'--corpus-root={inventory["root"]}', commands[0])
            self.assertIn('--corpus-order=ascending', commands[0])
            self.assertIn('--corpus-shard=0', commands[0])
            self.assertIn('--corpus-shards=1', commands[0])
            self.assertFalse(any(argument.startswith('--corpus-case=') for argument in commands[0]))
            self.assertTrue(report['completed'])
            self.assertTrue(report['complete_population'])
            self.assertEqual(report['execution'], 'fast')
            self.assertEqual(report['shards'], 1)
            self.assertEqual(report['library_bracket_scope'], 'per_shard')
            self.assertEqual([record['case'] for record in report['results']], sorted(records))
            self.assertEqual(report['expected_failure_complaints'], [])
            self.assertEqual(report['unowned_failures'], [])
            pinned = set(inventory['ledger']['expected_failures'])
            self.assertEqual({record['case'] for record in report['results'] if not record['passed']}, pinned)
            self.assertEqual(report['build_info'], metadata())

    def test_shards_partition_the_population_across_concurrent_processes(self):
        for shards in (2, 3, 6):
            with self.subTest(shards=shards), self.fast_run(shards=shards) as (
                    result, report, stderr, commands, inventory, records):
                self.assertEqual(result, 0, stderr)
                self.assertEqual(len(commands), shards)
                self.assertEqual(sorted(int(argument.removeprefix('--corpus-shard='))
                                        for command in commands for argument in command
                                        if argument.startswith('--corpus-shard=')), list(range(shards)))
                self.assertEqual(report['shards'], shards)
                # Records stay in case order however the slices were distributed.
                self.assertEqual([record['case'] for record in report['results']], sorted(records))
                self.assertEqual(report['expected_failure_complaints'], [])
                # Every case is attributed to exactly one shard, and every shard carries some.
                attribution = {record['case']: record['shard'] for record in report['results']}
                self.assertEqual(sorted(set(attribution.values())), list(range(shards)))
                self.assertEqual(sum(plan['planned'] for plan in report['shard_plans']), len(records))

    def test_a_reversed_run_orders_the_population_before_slicing_it(self):
        with self.fast_run(shards=2, order='reverse', extra_arguments=('--order', 'reverse')) as (
                result, report, stderr, commands, inventory, records):
            self.assertEqual(result, 0, stderr)
            self.assertTrue(all('--corpus-order=reverse' in command for command in commands))
            self.assertEqual(report['order'], 'reverse')
            self.assertEqual([record['case'] for record in report['results']], sorted(records))
            # Reversing moves cases between shards, so shard 0 holds the tail of the ascending order.
            first = [record['case'] for record in report['results'] if record['shard'] == 0]
            self.assertEqual(sorted(first), sorted(sorted(records)[len(records) - len(first):]))
        # A shard that ran a different order than the one requested is not the requested run.
        with self.fast_run(shards=2, order='ascending', extra_arguments=('--order', 'reverse')) as (
                result, report, stderr, commands, inventory, records):
            self.assertEqual(result, 2)
            self.assertIn('not the requested order', stderr)
            self.assertFalse(report['completed'])

    def test_a_shard_that_stops_before_its_last_case_is_refused(self):
        """The fast path's whole reason to be trustworthy: absent evidence is never a pass.

        The disguised exit is the one that matters. A shard killed at case 400 of 1078 still
        printed 399 well-formed records, and with several shards the run around it finished
        cleanly, so nothing but the missing completion line distinguishes it from a short run
        that worked.
        """
        truncations = (
            dict(announce_completion=False),
            dict(announce_completion=False, emitted=1),
            dict(completed=1),
            dict(emitted=1),
        )
        for shards in (1, 4):
            for damaged in range(shards):
                for truncation in truncations:
                    for exit_code in (0, -9):
                        with self.subTest(shards=shards, damaged=damaged, exit_code=exit_code,
                                          truncation=sorted(truncation)), \
                                self.fast_run(shards=shards, damaged_shard=damaged,
                                              exit_code=exit_code, **truncation) as (
                                result, report, stderr, commands, inventory, records):
                            self.assertEqual(result, 2, stderr)
                            self.assertFalse(report['completed'])
                            self.assertFalse(report['results'])
                            self.assertIn('infrastructure_error', report)
                            self.assertNotIn('summary', report)

    def test_a_shard_that_misreports_its_slice_is_refused(self):
        misreports = (
            (dict(start=0), 'gap or an overlap'),
            (dict(planned=0, emitted=0, completed=0), 'the shards cover'),
            (dict(total=3), 'the imported ledger declares'),
            (dict(root='res://tests/corpus/parser'), 'not the requested root'),
        )
        for damage, expected in misreports:
            with self.subTest(damage=sorted(damage)), self.fast_run(
                    shards=3, damaged_shard=2, **damage) as (result, report, stderr, *_):
                self.assertEqual(result, 2)
                self.assertIn(expected, stderr)
                self.assertFalse(report['completed'])

    def test_a_duplicated_slice_cannot_pass_as_coverage(self):
        """Two shards claiming the same cases keep the count right and the population wrong."""
        with self.fixture() as (root, library, report_path, inventory, cases, checkout):
            records = {record['imported_path']: record for record in inventory['sources']
                       if record['role'] == 'case' and record['disposition'] not in ('excluded', 'deferred')}

            def shard_process(command, timeout, **hooks):
                nonce = next(argument.removeprefix('--native-nonce=') for argument in command
                             if argument.startswith('--native-nonce='))
                shard = int(next(argument.removeprefix('--corpus-shard=') for argument in command
                                 if argument.startswith('--corpus-shard=')))
                own = self.shard_output(inventory, records, nonce=nonce, shard=shard, shards=2)
                if shard == 0:
                    return dict(output=own, exit_code=0, timed_out=False, duration_seconds=1.0)
                # Shard 1 re-emits as many of shard 0's cases as its own slice holds, while still
                # declaring its own boundaries and count. Every structural check therefore passes
                # and only the case names disagree: the population is short by shard 1's slice.
                kept = [line for line in own.splitlines()
                        if not line.startswith((triage.CASE_RESULT_PREFIX, triage.CASE_GUARD_PREFIX))]
                borrowed = [line for line in
                            self.shard_output(inventory, records, nonce=nonce, shard=0, shards=2).splitlines()
                            if line.startswith((triage.CASE_RESULT_PREFIX, triage.CASE_GUARD_PREFIX))]
                planned = json.loads(kept[0].removeprefix(triage.CORPUS_PLAN_PREFIX))['planned']
                return dict(output='\n'.join([kept[0], *borrowed[:2 * planned], *kept[1:]]) + '\n',
                            exit_code=0, timed_out=False, duration_seconds=1.0)

            stderr = io.StringIO()
            with patch.object(triage, 'supervise', side_effect=shard_process), redirect_stderr(stderr):
                result = triage.main(['--godot', 'fake-godot', '--library', str(library), '--corpus',
                                      inventory['root'], '--report', str(report_path), '--shards', '2'])
            self.assertEqual(result, 2)
            self.assertIn('did not evaluate exactly the declared population', stderr.getvalue())

    def test_an_undeclared_failure_complains_and_names_the_isolated_rerun(self):
        """The pin still bites on the fast path, and the complaint says how to pinpoint it."""
        with self.fixture() as (root, library, report_path, inventory, cases, checkout):
            records = {record['imported_path']: record for record in inventory['sources']
                       if record['role'] == 'case' and record['disposition'] not in ('excluded', 'deferred')}
            undeclared = sorted(records)[0]
            self.assertNotIn(undeclared, inventory['ledger']['expected_failures'])

            def shard_process(command, timeout, **hooks):
                nonce = next(argument.removeprefix('--native-nonce=') for argument in command
                             if argument.startswith('--native-nonce='))
                shard = int(next(argument.removeprefix('--corpus-shard=') for argument in command
                                 if argument.startswith('--corpus-shard=')))
                return dict(output=self.shard_output(inventory, records, nonce=nonce, shard=shard,
                                                     shards=2, failing=(undeclared,)),
                            exit_code=0, timed_out=False, duration_seconds=1.0)

            stderr = io.StringIO()
            with patch.object(triage, 'supervise', side_effect=shard_process), redirect_stderr(stderr):
                result = triage.main(['--godot', 'fake-godot', '--library', str(library), '--corpus',
                                      inventory['root'], '--report', str(report_path), '--shards', '2'])
            self.assertEqual(result, 1)
            report = json.loads(report_path.read_text())
            self.assertEqual(report['summary'], {'mismatch': 1, 'passed': len(records) - 1})
            self.assertTrue(any(complaint.startswith(undeclared + ':') and 'not declared in expected_failures' in complaint
                                for complaint in report['expected_failure_complaints']),
                            report['expected_failure_complaints'])
            self.assertIn('--execution isolated --case ' + shlex.quote(undeclared), stderr.getvalue())

    def test_narrowing_parallelism_and_slicing_belong_to_their_own_paths(self):
        with self.fixture() as (root, library, report_path, inventory, cases, checkout):
            population = len(triage.validate_imported_tree(
                root / 'project/tests/corpus/analyzer', inventory, ANALYZER_PROFILE, project_root=root / 'project'))
            base = ['--godot', 'fake-godot', '--library', str(library), '--corpus', inventory['root'],
                    '--report', str(report_path)]
            refusals = (
                (['--case', cases[0]], 'isolated'),
                (['--jobs', '4'], 'isolated'),
                (['--execution', 'isolated', '--order', 'reverse'], '--order'),
                (['--execution', 'isolated', '--shards', '4'], '--shards'),
                (['--shards', '0'], 'shards must be a positive integer'),
                # More shards than cases would leave shards proving they evaluated nothing.
                ([f'--shards', str(population + 1)], 'must not exceed'),
            )
            for extra, expected in refusals:
                with self.subTest(extra=extra):
                    stream = io.StringIO()
                    with patch.object(triage, 'supervise', side_effect=AssertionError('no process may run')), \
                            redirect_stderr(stream):
                        self.assertEqual(triage.main(base + extra), 2)
                    self.assertIn(expected, stream.getvalue())


@unittest.skipUnless(GODOT and LIBRARY, 'pass --godot and --library for actual same-process corpus transport')
class RuntimeTests(unittest.TestCase):
    def test_optional_transport_uses_the_actual_case_process(self):
        corpus = ANALYZER_ROOT
        corpus_root = ROOT / 'project' / corpus.removeprefix('res://')
        if not (corpus_root / 'inventory.json').is_file():
            self.skipTest('regenerate the analyzer corpus tree for the actual case transport')
        library, info, sha = triage.select_library(LIBRARY, [])
        from run_native_suites import staged_project
        inventory = triage.read_json(corpus_root / 'inventory.json')
        records = triage.validate_imported_tree(corpus_root, inventory, ANALYZER_PROFILE)
        case = sorted(records)[0]
        expected = records[case]['expected_block']
        with staged_project({'library': str(library)}) as project:
            triage.prepare_triage_project(project)
            nonce = 'same-process'
            command = [str(GODOT), '--headless', '--path', str(project), '--main-loop', 'BaristaNativeTestRunner',
                       '--', f'--native-suite={ANALYZER_PROFILE["suite"]}', f'--native-case={triage.NATIVE_CORPUS_CASE}',
                       f'--native-nonce={nonce}', f'--corpus-root={corpus}', f'--corpus-case={case}']
            process = triage.supervise(command, 120)
            completion = triage.native_completion(process['output'], nonce, info, triage.build_identity(),
                                                  ANALYZER_PROFILE['suite'])
            record = triage.result_record(process, case, corpus, expected, completion)
            triage.attach_build_info(record, completion, None)
            self.assertTrue(record['passed'], record)
            self.assertEqual(record['build_info'], info)
            self.assertEqual(triage.digest(project / 'bin' / ('native' + library.suffix)), sha)
        self.assertFalse(project.parent.exists())


class AdjudicationTests(unittest.TestCase):
    """What a residual-failure pin may and may not absorb, and which roots may be triaged."""

    @staticmethod
    def outcome(case, terminal, *, owner='#244: no runtime yet — expressions'):
        return dict(case=case, terminal=terminal, passed=terminal == 'passed',
                    semantic_owner=dict(reason=owner) if owner else None)

    def test_a_transcript_mismatch_is_the_only_failure_the_pin_absorbs(self):
        case = 'errors/case.barista'
        mismatch = [self.outcome(case, 'mismatch')]
        self.assertEqual(triage.expected_failure_complaints(mismatch, [case], [case]), [])
        # E4. Each of these is a failure the pin must refuse even though the case is pinned and
        # its owner carries a reason, because none of them is an adjudicated corpus result.
        for terminal in ('crash', 'timeout', 'missing_guard', 'malformed_result',
                         'inconsistent_completion', 'infrastructure_error', 'artifact_changed',
                         'build_info_error'):
            complaints = triage.expected_failure_complaints([self.outcome(case, terminal)], [case], [case])
            self.assertEqual(len(complaints), 1, (terminal, complaints))
            self.assertIn('is not an adjudicated corpus result', complaints[0])
            self.assertIn(terminal, complaints[0])
            self.assertTrue(triage.unadjudicated_failure(self.outcome(case, terminal)))
        self.assertFalse(triage.unadjudicated_failure(self.outcome(case, 'mismatch')))
        self.assertFalse(triage.unadjudicated_failure(self.outcome(case, 'passed')))

    def test_a_hard_failure_is_refused_whether_or_not_the_case_is_pinned(self):
        case = 'errors/case.barista'
        for pin in ([case], []):
            complaints = triage.expected_failure_complaints([self.outcome(case, 'crash')], pin, [case])
            # Exactly one complaint either way: an unpinned crash must not also be reported as
            # undeclared breakage, which would suggest pinning it is the remedy.
            self.assertEqual(len(complaints), 1, complaints)
            self.assertIn('is not an adjudicated corpus result', complaints[0])

    def test_a_pinned_case_that_passes_and_an_undeclared_failure_both_complain(self):
        passing, undeclared = 'errors/fixed.barista', 'errors/new.barista'
        complaints = triage.expected_failure_complaints(
            [self.outcome(passing, 'passed'), self.outcome(undeclared, 'mismatch')],
            [passing], [passing, undeclared])
        self.assertEqual(len(complaints), 2, complaints)
        self.assertIn('pinned expected failure now passes', complaints[0])
        self.assertIn('not declared in expected_failures', complaints[1])

    def test_a_record_with_no_terminal_complains_rather_than_raising(self):
        # The classifier fails closed on a record that does not say how it failed, so the
        # complaint has to be able to describe one. Reading the absent key directly would turn
        # the refusal into a crash and lose the durable report along with it.
        case = 'errors/case.barista'
        record = dict(case=case, passed=False, semantic_owner=dict(reason='#244: no runtime yet'))
        self.assertTrue(triage.unadjudicated_failure(record))
        complaints = triage.expected_failure_complaints([record], [case], [case])
        self.assertEqual(len(complaints), 1, complaints)
        self.assertIn('is not an adjudicated corpus result', complaints[0])
        self.assertIn('no terminal', complaints[0])

    def test_an_unowned_mismatch_is_refused(self):
        case = 'errors/case.barista'
        complaints = triage.expected_failure_complaints(
            [self.outcome(case, 'mismatch', owner=None)], [case], [case])
        self.assertEqual(len(complaints), 1, complaints)
        self.assertIn('no semantic owner', complaints[0])

    def test_both_registered_triage_roots_are_permitted_and_profiled(self):
        roots = triage.permitted_corpus_roots()
        self.assertEqual(roots, {ANALYZER_ROOT, RUNTIME_ROOT})
        self.assertEqual(triage.corpus_profile(ANALYZER_ROOT)['suite'], 'analyzer_corpus')
        runtime = triage.corpus_profile(RUNTIME_ROOT)
        self.assertEqual(runtime['name'], 'runtime')
        self.assertEqual(runtime['suite'], 'runtime_corpus')
        self.assertEqual(runtime['importer'], 'import_runtime_corpus')
        self.assertEqual(runtime['policy'], ROOT / 'scripts/runtime_corpus_policy.json')
        self.assertEqual(runtime['suite_source'], ROOT / 'tests/native/runtime_corpus_test.cpp')
        # The named suite has to exist as a declared native suite, or a triage run would ask
        # the runner for a filter that selects nothing and call the empty result a corpus.
        declared = json.loads((ROOT / 'tests/native_suites.json').read_text())['suites']
        for root in roots:
            self.assertIn(triage.corpus_profile(root)['suite'], declared)
            self.assertTrue(triage.corpus_profile(root)['policy'].is_file())
            self.assertTrue(triage.corpus_profile(root)['suite_source'].is_file())

    def test_an_unregistered_root_has_no_profile(self):
        for planted in ('res://tests/corpus/parser', 'res://tests/corpus/runtime/errors',
                        'user://runtime', 'res://tests/corpus/runtime/', ''):
            self.assertNotIn(planted, triage.permitted_corpus_roots())
            with self.assertRaises(ValueError):
                triage.corpus_profile(planted)

    def test_the_runtime_stage_is_an_admitted_case_stage(self):
        from corpus_stages import STAGES, validate_stages
        self.assertIn('runtime', STAGES)
        revision = triage.load_registry(ROOT)['revision']
        document = json.loads((ROOT / 'project/tests/corpus/runtime/case_stages.json').read_text())
        stages = validate_stages(document, set(document['cases']), set(), revision)
        self.assertTrue(stages)
        self.assertEqual(set(stages.values()), {'runtime'})


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--godot', type=Path)
    parser.add_argument('--library', type=Path)
    args, remaining = parser.parse_known_args()
    GODOT, LIBRARY = args.godot, args.library
    if bool(GODOT) != bool(LIBRARY):
        parser.error('--godot and --library must be supplied together')
    RuntimeTests.__unittest_skip__ = not bool(GODOT and LIBRARY)
    unittest.main(argv=[sys.argv[0], *remaining])
