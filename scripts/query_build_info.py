#!/usr/bin/env python3
# query_build_info.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Query the actual loaded extension in an isolated project; never substitute checkout identity."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import uuid

from build_config import ROOT, canonical_json, config_fingerprint, load_config, parse_json, require_equal
from build_metadata import create_metadata, inspect_artifact_bytes, repository_identity, selected_gitlink, verify_identity
sys.path.insert(0, str(ROOT / 'tests'))
from run_native_suites import staged_project, supervise

RESULT_PREFIX = 'BS_BUILD_INFO '


def evaluate(completed, nonce, expected):
    if completed.returncode:
        raise ValueError(f'build-info query process exited {completed.returncode}: {completed.stdout}')
    return parse_record(completed.stdout, nonce, expected)


def parse_record(output, nonce, expected):
    records = [line[len(RESULT_PREFIX):] for line in output.splitlines() if line.startswith(RESULT_PREFIX)]
    if len(records) != 1:
        raise ValueError(f'expected one loaded build-info record, found {len(records)}: {output}')
    record = parse_json(records[0])
    if type(record) is not dict or set(record) != {'nonce', 'build_info', 'godot_version'}:
        raise ValueError('loaded build-info record fields do not match the protocol')
    require_equal('build-info nonce', nonce, record['nonce'])
    if type(record['godot_version']) is not dict or not record['godot_version']:
        raise ValueError('missing actual Godot version')
    verify_identity(record['build_info'], expected)
    return record


def query(godot, library, *, timeout=120, expected=None, project=None):
    library = Path(library)
    content = library.read_bytes()
    actual = inspect_artifact_bytes(content)
    sha = hashlib.sha256(content).hexdigest()
    if expected is not None:
        verify_identity(actual, expected)

    def invoke(staged):
        copied = staged / 'bin' / ('native' + library.suffix)
        require_equal('staged library SHA256', sha, hashlib.sha256(copied.read_bytes()).hexdigest())
        shutil.copy2(ROOT / 'scripts/build_info_query.gd', staged / 'build_info_query.gd')
        nonce = uuid.uuid4().hex
        completed = supervise([str(godot), '--headless', '--path', str(staged), '--script', 'res://build_info_query.gd', '--', nonce], timeout)
        record = evaluate(completed, nonce, actual)
        return dict(build_info=record['build_info'], artifact_sha256=sha, godot_version=record['godot_version'])

    if project is not None:
        return invoke(Path(project))
    with staged_project({'library': str(library)}) as staged:
        return invoke(staged)


def checkout_info():
    source = repository_identity(ROOT)
    dependency = repository_identity(ROOT / 'godot-cpp')
    dependency['selected_revision'] = selected_gitlink(ROOT, source)
    return dict(source=source, godot_cpp=dependency, config_sha256=config_fingerprint(load_config()))


def require_current(actual, root=ROOT, config=None):
    config = config or load_config(Path(root) / "build_versions.json")
    build = actual["build"]
    selection = {key: build[key] for key in ("platform", "architecture", "target")}
    selection.update(api=config["godot_api"], precision=config["precision"])
    expected = create_metadata(root, config, selection, native_tests=build["native_tests"])
    # Revision+dirty/unknown state cannot distinguish two different dirty/archive contents.
    # Diagnostic queries still report them; a current-artifact claim requires complete clean identity.
    verify_identity(actual, expected, release=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--require-current', action='store_true', help='also require clean, complete current source/configuration identity')
    args = parser.parse_args(argv)
    try:
        value = query(args.godot, args.library)
        if args.require_current:
            require_current(value['build_info'])
        value['checkout_info'] = checkout_info()
        print(canonical_json(value))
        return 0
    except (OSError, ValueError) as error:
        print(f'build-info query: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
