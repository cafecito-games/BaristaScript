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
import query_build_info
import run_corpus_triage as triage
import run_native_suites as native

GODOT = LIBRARY = None


def metadata():
    config = build_config.load_config()
    return dict(schema=1, extension_version=config['extension_version'], config_sha256=build_config.config_fingerprint(config),
                godot_api=config['godot_api'], godot_runtime=config['godot_runtime'],
                source=dict(revision='1' * 40, state='clean'),
                godot_cpp=dict(revision='2' * 40, selected_revision='2' * 40, state='clean'),
                build=dict(platform='macos', architecture='arm64', target='template_debug', precision=config['precision'], native_tests=False))


class IdentityTests(unittest.TestCase):
    def test_selection_requires_unambiguous_host_debug_artifact(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(triage.sys, 'platform', 'darwin'):
            root = Path(temporary)
            library = root / 'macos' / 'lib.template_debug.dylib'
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
            bad = metadata(); bad['build']['native_tests'] = True
            library.write_bytes(build_metadata.encode_envelope(bad))
            with self.assertRaisesRegex(ValueError, 'ordinary'):
                triage.select_library(library, [library])

    def test_metadata_never_turns_a_failed_case_into_a_pass(self):
        info = metadata()
        output = query_build_info.RESULT_PREFIX + json.dumps(dict(nonce='n', build_info=info, godot_version={'major': 4}))
        for terminal in ('passed', 'mismatch', 'crash', 'timeout'):
            record = dict(terminal=terminal, passed=terminal == 'passed', output=output)
            triage.attach_build_info(record, 'n', info)
            self.assertEqual(record['terminal'], terminal)
            self.assertEqual(record['passed'], terminal == 'passed')
            self.assertEqual(record['build_info'], info)
        for text in ('', output * 2, output.replace('"n"', '"wrong"'), output.replace('1' * 40, '3' * 40)):
            record = dict(terminal='passed', passed=True, output=text)
            triage.attach_build_info(record, 'n', info)
            self.assertFalse(record['passed'])
            self.assertEqual(record['terminal'], 'build_info_error')
            self.assertIsNone(record['build_info'])
            self.assertTrue(record['build_info_error'])

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
            uri = 'res://tests/corpus_staging/analyzer'
            policy['counts'] = importer.inventory_sources(source, policy, uri)['counts']
            inventory = importer.inventory_sources(source, policy, uri)
            corpus = root / 'project/tests/corpus_staging/analyzer'
            files = ('project/project.godot', 'project/.godot/extension_list.cfg',
                     'project/tests/corpus_runner.gd', 'project/tests/corpus_harness.gd',
                     'project/tests/corpus_support/parser/utils.notest.barista',
                     'project/tests/corpus_support/parser/source_map.json',
                     'scripts/corpus_sources.json', 'scripts/run_corpus_triage.py',
                     'src/bs_corpus_sentinels.h', 'src/bs_analyzer_probe.cpp', 'src/bs_analyzer_probe.h')
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
            importer.write_stage(inventory, source, corpus, project_root=root / 'project')
            (root / 'scripts/analyzer_corpus_policy.json').write_text(json.dumps(policy))
            library = root / 'project/bin/macos/lib.template_debug.dylib'
            library.parent.mkdir(parents=True)
            library.write_bytes(build_metadata.encode_envelope(metadata()))
            other = root / 'project/bin/windows/other.template_debug.dll'
            other.parent.mkdir()
            other.write_bytes(b'other candidate remains hashed')
            for command in (['init', '-q'], ['add', '.'], ['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                                                        'commit', '-qm', 'fixture']):
                subprocess.run(['git', '-C', str(root), *command], check=True, capture_output=True)
            cases = sorted(triage.validate_staging(corpus, inventory, project_root=root / 'project'))[:case_limit]
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
                stack.enter_context(patch.object(importer, 'default_policy', return_value=policy))
                stack.enter_context(patch.object(triage, 'checkout_info', return_value=checkout))
                stack.enter_context(patch.object(native, 'godot_data_path', return_value=base / 'userdata'))
                stack.enter_context(patch.object(triage.subprocess, 'run', side_effect=command_run))
                yield root, library, report, inventory, cases, checkout

    def exercise(self, mode, *, jobs=1, case_limit=2, staggered=False):
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
                case = command[command.index('--case') + 1]
                nonce = command[command.index('--build-info-nonce') + 1]
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
                expected = records[case]['expected_block']
                payload = dict(path=inventory['root'] + '/' + case, passed=True, expected=expected, actual=expected)
                output = 'BS_CASE_RESULT ' + json.dumps(payload) + '\nBS_CASE_RAN ' + case + '\nBS_CORPUS 1/1 skipped=0\n'
                if mode != 'missing' or case != cases[0]:
                    output += query_build_info.RESULT_PREFIX + json.dumps(dict(nonce=nonce, build_info=metadata(), godot_version={'major': 4})) + '\n'
                if mode == 'replace' and case == cases[0]:
                    (project / 'bin/native.dylib').write_bytes(b'replaced')
                return dict(output=output, exit_code=0, timed_out=False, duration_seconds=0.001)

            arguments = ['--godot', 'fake-godot', '--library', str(library), '--corpus', inventory['root'],
                         '--report', str(report_path), '--jobs', str(jobs)]
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
            self.assertFalse(report['complete_population'])
            self.assertEqual(report['inventory_sha256'], triage.digest(root / 'project/tests/corpus_staging/analyzer/inventory.json'))
            self.assertEqual(report['checkout_info'], checkout)
            self.assertEqual(report['build_info'], metadata())
            self.assertNotEqual(report['build_info']['source'], report['checkout_info']['source'])
            self.assertEqual(len(report['build_artifacts']), 2)
            self.assertEqual(len(report['execution_files']), 6)
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
                         '--report', str(report_path), '--jobs', str(len(cases)), '--timeout', '30']
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


@unittest.skipUnless(GODOT and LIBRARY, 'pass --godot and --library for actual same-process corpus transport')
class RuntimeTests(unittest.TestCase):
    def test_optional_transport_uses_the_actual_case_process(self):
        library, info, sha = triage.select_library(LIBRARY, [])
        from run_native_suites import staged_project
        with staged_project({'library': str(library)}) as project:
            triage.prepare_triage_project(project)
            corpus = 'res://tests/corpus_fixtures/tokenizer'
            case = 'comment_at_eof.barista'
            expected = triage.decode_expectation((project / 'tests/corpus_fixtures/tokenizer/comment_at_eof.out').read_bytes(), case)
            command = [str(GODOT), '--headless', '--path', str(project), '--script', 'res://tests/corpus_runner.gd',
                       '--', '--corpus', corpus, '--case', case, '--stage', 'parser']
            ordinary = triage.supervise(command, 60)
            self.assertTrue(triage.result_record(ordinary, case, corpus, expected)['passed'], ordinary['output'])
            self.assertNotIn(query_build_info.RESULT_PREFIX, ordinary['output'])
            process = triage.supervise(command + ['--build-info-nonce', 'same-process'], 60)
            record = triage.result_record(process, case, corpus, expected)
            triage.attach_build_info(record, 'same-process', info)
            self.assertTrue(record['passed'], record)
            self.assertEqual(record['build_info'], info)
            self.assertEqual(triage.digest(project / 'bin' / ('native' + library.suffix)), sha)
        self.assertFalse(project.parent.exists())


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
