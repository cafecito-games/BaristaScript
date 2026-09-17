# runtime_expectations.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Full runtime transcripts from the pinned Foundry test producer.

This is the runtime counterpart of ``scripts/corpus_expectations.py``. That module
deliberately projects runtime output away, because an analyzer case asserts static
diagnostics only. A runtime case asserts the whole transcript the producer emits, in
emission order: the status token line, then warnings, then ``print`` output, then
``>> SCRIPT ERROR at <path>:<line> on <func>(): ...`` and ``>> ERROR: ...`` lines
interleaved exactly as the engine emitted them.

Nothing here parses the transcript body. The producer assembles it from engine error
handler output (``tests/fs_test_runner.cpp:827-857``) and compares it after
``strip_edges`` (``:858-872``); any structure this module imposed on the body beyond
the status token would be a second, weaker definition of a runtime expectation. The
only transformation a transcript undergoes is resource-identity relocation, which
``relocate_transcript`` performs against a validated preimage map.

This module is the single definition of runtime expectation rendering. A second
spelling of it anywhere would be a corpus-wide silent false pass, exactly as
``src/bs_corpus_evaluation.h`` warns for the static block.
"""
from __future__ import annotations

import re

from corpus_expectations import decode_expectation
from corpus_ledger import barista_path

# The three status tokens a runtime expectation may carry. The producer writes exactly
# one of them as the first line (``tests/fs_test_runner.cpp:874-880``); the remaining
# tokens belong to earlier pipeline stages and cannot appear in this corpus.
STATUS = ('FS_TEST_OK', 'FS_TEST_RUNTIME_ERROR', 'FS_TEST_ANALYZER_ERROR')

# The three spellings an upstream resource identity takes inside a transcript: the
# engine resource path, the producer's corpus-base-relative path
# (``tests/fs_test_runner.cpp:846``, with the base established at ``:327-333``), and a
# bare filename fragment inside a quoted class or enum name. The first two name a file
# and are resolved through the preimage map; the third only projects its extension and
# therefore never chooses a provider, so a duplicate basename keeps its ambiguity.
IDENTITY = re.compile(rb'res://[A-Za-z0-9_./-]+\.fs'
                      rb'|(?:runtime|analyzer|parser)/[A-Za-z0-9_./-]+\.fs'
                      rb'|[A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+)*\.fs')


def extract_transcript(data: bytes, path: str) -> tuple[str, str]:
    """Return ``(status token, complete transcript)`` for one producer output.

    The transcript retains every byte the producer wrote apart from its single final
    line feed, including the status token line, so an imported expectation differs from
    its upstream original only by declared resource relocation.
    """
    text = decode_expectation(data, path)
    status = text.split('\n', 1)[0]
    if status not in STATUS:
        raise ValueError(f'{path}: unrecognized upstream runtime status {status!r}')
    return status, text


def transcript_identities(block: str):
    """Yield ``(start, end, identity)`` for every resource identity in one transcript."""
    data = block.encode('utf-8')
    for match in IDENTITY.finditer(data):
        yield match.start(), match.end(), match[0].decode('utf-8')


def relocated_identity(identity: str, mapping: dict[str, str], basenames: set[str], path: str) -> str:
    """Relocate one identity, or reject it when no preimage validates it."""
    if identity.startswith('res://'):
        target = identity.removeprefix('res://')
        if target not in mapping:
            raise ValueError(f'{path}: unknown transcript resource identity {identity}')
        return mapping[target]
    if '/' in identity:
        if identity not in mapping:
            raise ValueError(f'{path}: unknown transcript source identity {identity}')
        return mapping[identity]
    if identity not in basenames:
        raise ValueError(f'{path}: unknown transcript filename {identity}')
    return barista_path(identity)
