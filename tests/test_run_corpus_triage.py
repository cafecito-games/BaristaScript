#!/usr/bin/env python3
# test_run_corpus_triage.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Corpus execution retains same-process build identity and durable partial reports."""
import argparse
from contextlib import ExitStack, contextmanager
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
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
    def fixture(self):
        import import_analyzer_corpus as importer
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            root = base / 'checkout'
            source = base / 'input'
            shutil.copytree(ROOT / 'tests/fixtures/analyzer_import/scripts', source)
            policy = importer.default_policy()
            policy.update(counts=None, owners={}, expectation_edits={}, expectation_overrides={})
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
            cases = sorted(triage.validate_staging(corpus, inventory, project_root=root / 'project'))[:2]
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

    def exercise(self, mode):
        with self.fixture() as (root, library, report_path, inventory, cases, checkout):
            records = {r['imported_path']: r for r in inventory['sources'] if r['role'] == 'case'}
            original_hashes = {str(p.relative_to(root)): triage.digest(p) for p in root.rglob('*') if p.is_file() and '.git' not in p.parts}
            projects = []

            def case_process(command, timeout):
                project = Path(command[command.index('--path') + 1])
                projects.append(project)
                self.assertNotEqual(project, root / 'project')
                self.assertIn('debug = "res://bin/native.dylib"', (project / 'bin/barista_script.gdextension').read_text())
                self.assertIn('release = "res://bin/native.dylib"', (project / 'bin/barista_script.gdextension').read_text())
                self.assertEqual(list((project / 'bin').glob('*.dylib')), [project / 'bin/native.dylib'])
                case = command[command.index('--case') + 1]
                nonce = command[command.index('--build-info-nonce') + 1]
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

            arguments = ['--godot', 'fake-godot', '--library', str(library), '--corpus', inventory['root'], '--report', str(report_path)]
            for case in cases:
                arguments += ['--case', case]
            with patch.object(triage, 'supervise', side_effect=case_process):
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
