# corpus_expectations.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Exact static blocks from the pinned Foundry test producer; no runtime output."""
from pathlib import Path
import re


def success_sentinel() -> str:
    header = Path(__file__).resolve().parents[1] / 'src/bs_corpus_sentinels.h'
    match = re.search(r'SUCCESS_SENTINEL\s*=\s*"([^"]+)"', header.read_text(encoding='utf-8'))
    if match is None:
        raise ValueError(f'{header}: missing success sentinel')
    return match[1]


def decode_expectation(data: bytes, path: str) -> str:
    try:
        text = data.decode('utf-8', errors='strict')
    except UnicodeError as error:
        raise ValueError(f'{path}: invalid UTF-8 expectation') from error
    if not text.endswith('\n') or text == '\n' or text.endswith('\n\n') or '\r' in text or '\0' in text:
        raise ValueError(f'{path}: expectation requires a nonempty block and exactly one final LF, without CR or NUL')
    return text[:-1]


ERROR = re.compile(r'>> ERROR at line [1-9][0-9]*: .+')
WARNING = re.compile(r'~~ WARNING at line [1-9][0-9]*: \([A-Z][A-Z0-9_]*\) .+')


def extract_static_block(data: bytes, path: str) -> str:
    try:
        text = data.decode('utf-8', errors='strict')
    except UnicodeError as error:
        raise ValueError(f'{path}: invalid UTF-8 producer output') from error
    if not text.endswith('\n'):
        raise ValueError(f'{path}: producer output requires final LF')
    lines = text[:-1].split('\n')
    status, body = lines[0], lines[1:]
    if status != 'FS_TEST_OK':
        decode_expectation(data, path)
    if status == 'FS_TEST_PARSER_ERROR':
        if len(body) != 1 or not body[0]:
            raise ValueError(f'{path}: parser-error status requires one diagnostic')
        return body[0]
    if status == 'FS_TEST_ANALYZER_ERROR':
        if not body or any(not ERROR.fullmatch(line) for line in body):
            raise ValueError(f'{path}: malformed analyzer-error block')
        return '\n'.join(body)
    if status == 'FS_TEST_OK':
        warnings = []
        for line in body:
            if not line.startswith('~~ WARNING'):
                break
            if not WARNING.fullmatch(line) or '\r' in line or '\0' in line:
                raise ValueError(f'{path}: malformed initial warning block')
            warnings.append(line)
        return '\n'.join(warnings) or success_sentinel()
    raise ValueError(f'{path}: unrecognized upstream status {status!r}')
