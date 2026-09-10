#!/usr/bin/env python3
# test_build_consumers.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Keep build configuration and real packaging gates wired to their shared authority."""
import copy
from pathlib import Path
import sys
import unittest
import yaml
import validate_ci

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_config import validate_scons_consumer


class ConsumerTests(unittest.TestCase):
    def setUp(self):
        self.ci = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(), Loader=yaml.BaseLoader)
        self.package = yaml.load((ROOT / '.github/workflows/make_build.yml').read_text(), Loader=yaml.BaseLoader)
        self.action = yaml.load((ROOT / '.github/actions/setup-godot-cpp/action.yml').read_text(), Loader=yaml.BaseLoader)

    def check(self, ci=None, package=None, action=None):
        return validate_ci.check_build_version_wiring(ci or self.ci, package or self.package, action or self.action)

    def test_current_consumers_are_wired(self):
        self.assertIsNone(self.check())
        validate_scons_consumer((ROOT / 'SConstruct').read_text())

    def test_scons_missing_commented_or_literal_defaults_fail(self):
        text = (ROOT / 'SConstruct').read_text()
        for old, new in (('versions = load_config()', '# versions = load_config()'),
                         ('localEnv["api_version"] = versions["godot_api"]', 'localEnv["api_version"] = "4.6"'),
                         ('localEnv["precision"] = versions["precision"]', 'localEnv["precision"] = "double"')):
            with self.subTest(old=old), self.assertRaises(ValueError):
                validate_scons_consumer(text.replace(old, new))

    def test_version_resolution_and_actual_packaging_gate_cannot_be_removed_or_suppressed(self):
        for name, document in (('ci', self.ci), ('package', self.package)):
            for index, step in enumerate(document['jobs']['build']['steps']):
                if step.get('id') != 'versions' and 'verify_build_artifacts.py' not in step.get('run', ''):
                    continue
                for change in ('remove', 'condition', 'suppression', 'wrong_tree'):
                    altered = copy.deepcopy(document)
                    target = altered['jobs']['build']['steps'][index]
                    if change == 'remove':
                        del altered['jobs']['build']['steps'][index]
                    elif change == 'condition':
                        target['if'] = 'false'
                    elif change == 'suppression':
                        target['run'] += ' || true'
                    else:
                        target['run'] = target['run'].replace('--binary-dir bin', '--binary-dir project/bin') if 'verify_build_artifacts' in target['run'] else '# ' + target['run']
                    with self.subTest(name=name, change=change):
                        self.assertIsNotNone(self.check(**{name: altered}))

    def test_packaging_must_gate_upload_and_matrix_cannot_drift(self):
        altered = copy.deepcopy(self.package)
        steps = altered['jobs']['build']['steps']
        gate = next(step for step in steps if 'verify_build_artifacts.py' in step.get('run', ''))
        steps.remove(gate)
        steps.append(gate)
        self.assertIsNotNone(self.check(package=altered))
        for document, name in ((self.ci, 'ci'), (self.package, 'package')):
            altered = copy.deepcopy(document)
            matrix = altered['jobs']['build']['strategy']['matrix']
            if name == 'ci':
                matrix['include'].pop()
            else:
                matrix['target'].pop()
            self.assertIsNotNone(self.check(**{name: altered}))

    def test_setup_and_runtime_versions_cannot_gain_independent_defaults(self):
        action = copy.deepcopy(self.action)
        action['inputs']['em-version']['default'] = '9.9.9'
        self.assertIsNotNone(self.check(action=action))
        ci = copy.deepcopy(self.ci)
        for step in ci['jobs']['build']['steps']:
            if 'GODOT_VERSION' in step.get('env', {}):
                step['env']['GODOT_VERSION'] = '9.9.9'
        self.assertIsNotNone(self.check(ci=ci))


if __name__ == '__main__':
    unittest.main()
