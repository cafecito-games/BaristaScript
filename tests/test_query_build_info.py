#!/usr/bin/env python3
# test_query_build_info.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Reject missing, malformed, duplicate and mismatched loaded-identity records."""
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_config
import build_metadata
import query_build_info


class QueryTests(unittest.TestCase):
    def test_completion_requires_actual_matching_identity_and_nonce(self):
        config = build_config.load_config()
        selected = dict(platform='macos', architecture='arm64', target='template_debug', api=config['godot_api'], precision=config['precision'])
        info = build_metadata.create_metadata(ROOT, config, selected, native_tests=False)
        record = dict(nonce='nonce', build_info=info, godot_version={'major': 4, 'minor': 7, 'patch': 2})
        output = query_build_info.RESULT_PREFIX + json.dumps(record) + '\n'
        result = subprocess.CompletedProcess([], 0, output)
        self.assertEqual(query_build_info.evaluate(result, 'nonce', info), record)
        for code, text, nonce in ((1, output, 'nonce'), (0, '', 'nonce'), (0, output * 2, 'nonce'),
                                  (0, output, 'other'), (0, query_build_info.RESULT_PREFIX + '{}', 'nonce'),
                                  (0, query_build_info.RESULT_PREFIX + '{invalid}', 'nonce')):
            with self.subTest(code=code, text=text[:30], nonce=nonce), self.assertRaises(ValueError):
                query_build_info.evaluate(subprocess.CompletedProcess([], code, text), nonce, info)
        wrong = dict(info, extension_version='0.1.1')
        with self.assertRaisesRegex(ValueError, 'extension_version'):
            query_build_info.evaluate(result, 'nonce', wrong)


if __name__ == '__main__':
    unittest.main()
