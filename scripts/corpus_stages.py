# corpus_stages.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Shared imported-case evaluation-stage schema, independent of importer identity."""
import json
from pathlib import PurePosixPath


def validate_stages(document: dict, cases: set[str], helpers: set[str], revision: str) -> dict[str, str]:
    if (set(document) != {'schema_version', 'foundry_revision', 'cases'}
            or type(document.get('schema_version')) is not int or document['schema_version'] != 1
            or document.get('foundry_revision') != revision):
        raise ValueError('case stages require schema_version 1 and the registered revision')
    stages = document.get('cases')
    if not isinstance(stages, dict):
        raise ValueError('case stages cases must be an object')
    for path, stage in stages.items():
        if (not isinstance(path, str) or not path.endswith('.barista') or path.endswith('.notest.barista')
                or '\\' in path or path.startswith('/') or str(PurePosixPath(path)) != path
                or any(part in ('', '.', '..') for part in path.split('/')) or path in helpers
                or stage not in ('parser', 'analyzer')):
            raise ValueError(f'invalid case stage entry: {path!r}: {stage!r}')
    if set(stages) != cases:
        raise ValueError(f'case stages missing {sorted(cases - stages.keys())}, extra {sorted(stages.keys() - cases)}')
    return stages


def write_stages(destination, cases, helpers, revision, stage='parser'):
    document = {'schema_version': 1, 'foundry_revision': revision,
                'cases': dict(sorted(stage.items())) if isinstance(stage, dict) else {path: stage for path in sorted(cases)}}
    validate_stages(document, set(cases), set(helpers), revision)
    (destination / 'case_stages.json').write_text(json.dumps(document, indent=2) + '\n', encoding='utf-8')
