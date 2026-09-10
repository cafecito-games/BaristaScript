# test_corpus_expectations.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Independent pinned producer and fail-closed static oracle tests."""
from pathlib import Path
import json
import hashlib
import sys
import unittest
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import corpus_expectations as oracle
from corpus_stages import validate_stages
from corpus_registry import load_registry, read_json
from corpus_expectations import success_sentinel

FIXTURES = ROOT / 'project/tests/oracle_fixtures/producer'

class ExpectationsTest(unittest.TestCase):
    def test_pinned_blocks(self):
        for source in FIXTURES.glob('*.upstream'):
            with self.subTest(source=source.name):
                expected = source.with_suffix('.block').read_bytes()
                self.assertEqual(oracle.extract_static_block(source.read_bytes(), str(source)), expected.decode('utf-8')[:-1])

    def test_fixture_provenance(self):
        metadata = json.loads((FIXTURES / 'provenance.json').read_text())
        self.assertEqual(metadata['foundry_revision'], load_registry()['revision'])
        for record in metadata['files']:
            self.assertEqual(hashlib.sha256((FIXTURES / (record['fixture'] + '.upstream')).read_bytes()).hexdigest(), record['sha256'])
            if 'source' in record:
                self.assertEqual(hashlib.sha256((FIXTURES / (record['fixture'] + '.source')).read_bytes()).hexdigest(), record['source']['sha256'])

    def test_runtime_boundary(self):
        warning = b'~~ WARNING at line 2: (UNUSED_VARIABLE) unused\n'
        for prefix in [b'', warning]:
            self.assertEqual(oracle.extract_static_block(b'FS_TEST_OK\n' + prefix + b'runtime\n' + warning + b'\n', 'fixture'), prefix.decode()[:-1] or success_sentinel())

    def test_bad_producer_blocks(self):
        for data in [b'', b'UNKNOWN\n', b'FS_TEST_ANALYZER_ERROR\n', b'FS_TEST_ANALYZER_ERROR\nwrong\n', b'FS_TEST_ANALYZER_ERROR\n>> ERROR at line x: bad\n', b'FS_TEST_ANALYZER_ERROR\n>> ERROR at line 1: \n', b'FS_TEST_OK\n~~ WARNING broken\n', b'FS_TEST_OK\n~~ WARNING at line 1: (CODE) bad\r\n', b'FS_TEST_OK\n~~ WARNING at line 1: (CODE) bad\x00\n', b'FS_TEST_OK\n~~ WARNING at line 0: (CODE) bad\n', b'FS_TEST_PARSER_ERROR\n', b'FS_TEST_PARSER_ERROR\nfirst\nsecond\n']:
            with self.subTest(data=data), self.assertRaises(ValueError):
                oracle.extract_static_block(data, 'fixture')

    def test_expectation_bytes(self):
        for data in [b'', b'\n', b'first', b'first\n\n', b'first\r\n', b'first\rsecond\n', b'first\x00\n', b'\xff\n']:
            with self.subTest(data=data), self.assertRaises(ValueError):
                oracle.decode_expectation(data, 'fixture')
        self.assertEqual(oracle.decode_expectation(b'first \nsecond\n', 'fixture'), 'first \nsecond')

    def test_stage_schema(self):
        revision = load_registry()['revision']
        good = {'schema_version': 1, 'foundry_revision': revision, 'cases': {'a.barista': 'parser'}}
        self.assertEqual(validate_stages(good, {'a.barista'}, {'h.notest.barista'}, revision), good['cases'])
        bad = [dict(good, schema_version=True), dict(good, schema_version=2), dict(good, foundry_revision='0'*40)]
        for cases in [{}, {'extra.barista': 'parser'}, {'a.barista': 'wrong'}, {'../a.barista': 'parser'}, {'a.barista': 'parser', 'h.notest.barista': 'analyzer'}]:
            bad.append(dict(good, cases=cases))
        for document in bad:
            with self.subTest(document=document), self.assertRaises(ValueError):
                validate_stages(document, {'a.barista'}, {'h.notest.barista'}, revision)

class PublicationTest(unittest.TestCase):
    def test_generation_failure_preserves_previous_output(self):
        import import_parser_corpus as importer
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination = root / 'parser'
            destination.mkdir()
            (destination / 'previous').write_bytes(b'valid old tree')
            with self.assertRaises(SystemExit):
                importer.import_corpus(root / 'missing-upstream', destination)
            self.assertEqual((destination / 'previous').read_bytes(), b'valid old tree')
            self.assertEqual(sorted(path.name for path in destination.iterdir()), ['previous'])

    def test_publication_failure_rolls_back_tree_and_baseline(self):
        import import_parser_corpus as importer
        baseline = json.loads((ROOT / 'tests/corpus_baseline.json').read_text())
        corpus = ROOT / 'project/tests/corpus/parser'
        summary = {'cases': sorted(path.relative_to(corpus).as_posix() for path in corpus.rglob('*.barista') if not path.name.endswith('.notest.barista')),
                   'helpers': sorted(path.relative_to(corpus).as_posix() for path in corpus.rglob('*.notest.barista')),
                   'stages': json.loads((corpus / 'case_stages.json').read_text())['cases']}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination = root / 'parser'
            destination.mkdir()
            (destination / 'previous').write_bytes(b'valid old tree')
            baseline_path = root / 'baseline.json'
            before = json.dumps(baseline).encode()
            baseline_path.write_bytes(before)
            def generate(_foundry, fresh):
                fresh.mkdir()
                (fresh / 'replacement').write_bytes(b'new tree')
                return summary
            replace = importer.os.replace
            def fail_baseline(source, target):
                if target == baseline_path and source.name == 'baseline.json':
                    raise OSError('injected publication failure')
                return replace(source, target)
            with patch.object(importer, 'BASELINE_PATH', baseline_path), patch.object(importer, '_generate_corpus', side_effect=generate), patch.object(importer, 'generate_support', side_effect=lambda _f, dest: dest.mkdir()), patch.object(importer.os, 'replace', side_effect=fail_baseline):
                with self.assertRaisesRegex(OSError, 'injected'):
                    importer.import_corpus(root, destination, publish_baseline=True)
            self.assertEqual((destination / 'previous').read_bytes(), b'valid old tree')
            self.assertFalse((destination / 'replacement').exists())
            self.assertEqual(baseline_path.read_bytes(), before)

    def test_shared_strict_json_serialized_controls(self):
        matrix = json.loads((FIXTURES.parent / 'json_contract.json').read_text())
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'case_stages.json'
            for valid in matrix['valid']:
                path.write_text(valid)
                self.assertIsInstance(read_json(path), dict)
            for invalid in matrix['invalid']:
                with self.subTest(invalid=invalid):
                    path.write_text(invalid)
                    with self.assertRaises(ValueError):
                        read_json(path)

    def test_duplicate_stage_keys_rejected_before_collapse(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'case_stages.json'
            for text in ['{"cases":{"a.barista":"parser","a.barista":"analyzer"}}', '{"schema_version":1,"schema_version":1}']:
                path.write_text(text)
                with self.assertRaisesRegex(ValueError, 'duplicate'):
                    read_json(path)

if __name__ == '__main__':
    unittest.main()
