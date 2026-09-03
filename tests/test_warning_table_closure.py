#!/usr/bin/env python3
# test_warning_table_closure.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""The warning registry has one table, and every consumer covers all of it.

`project/tests/warning_registry_test.gd` proves the registry answers correctly for every code, but
it can only see what the compiled binary exposes. This checks the source-level property that makes
that answer trustworthy: the `Code` enumeration, the default-level table, the name table and the
message `switch` are four views of one vocabulary, listing the same enumerators in the same order,
with nothing that quietly absorbs a code none of them knows.

Two of these are also checked by `static_assert` at compile time. That is deliberate overlap: the
asserts compare lengths, this compares the identifiers, and a table whose entries are permuted has
the right length.

Usage:
    python3 tests/test_warning_table_closure.py
"""

import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "bs_warning.h"
SOURCE = ROOT / "src" / "bs_warning.cpp"

ENUM_PATTERN = re.compile(r"\tenum Code \{\n(.*?)\n\t\};", re.DOTALL)
DEFAULTS_PATTERN = re.compile(r"constexpr static WarnLevel default_warning_levels\[\] = \{\n(.*?)\n\t\};", re.DOTALL)
NAMES_PATTERN = re.compile(r"static const char \*names\[\] = \{\n(.*?)\n\t\};", re.DOTALL)
SWITCH_PATTERN = re.compile(r"\tswitch \(code\) \{\n(.*?)\n\t\}\n", re.DOTALL)

ENUMERATOR_PATTERN = re.compile(r"^\t\t([A-Z][A-Z0-9_]*),")
DEFAULT_ENTRY_PATTERN = re.compile(r"^\t\t(IGNORE|WARN|ERROR), // ([A-Z][A-Z0-9_]*)")
NAME_ENTRY_PATTERN = re.compile(r'^\t\t"([A-Z][A-Z0-9_]*)",')
CASE_PATTERN = re.compile(r"^\t\tcase ([A-Z][A-Z0-9_]*):")

SENTINEL = "WARNING_MAX"


def section(pattern, text, what):
    match = pattern.search(text)
    if match is None:
        raise AssertionError("cannot find the {} in the registry source".format(what))
    return match.group(1)


def entries(pattern, body, group=1):
    found = []
    for line in body.splitlines():
        match = pattern.match(line)
        if match:
            found.append(match.group(group))
    return found


class WarningTableClosureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.enumerators = entries(ENUMERATOR_PATTERN, section(ENUM_PATTERN, cls.header, "Code enumeration"))
        cls.defaults = entries(
            DEFAULT_ENTRY_PATTERN, section(DEFAULTS_PATTERN, cls.header, "default-level table"), group=2
        )
        cls.names = entries(NAME_ENTRY_PATTERN, section(NAMES_PATTERN, cls.source, "name table"))
        cls.switch_body = section(SWITCH_PATTERN, cls.source, "message switch")
        cls.cases = entries(CASE_PATTERN, cls.switch_body)

    def test_the_enumeration_is_non_empty_and_ends_with_the_sentinel(self):
        self.assertGreater(len(self.enumerators), 1, "the Code enumeration was not parsed")
        self.assertEqual(self.enumerators[-1], SENTINEL)

    def test_the_default_level_table_matches_the_enumeration(self):
        """Same codes, same order -- a permuted table has the right length and the wrong defaults."""
        self.assertEqual(self.defaults, self.enumerators[:-1])

    def test_the_name_table_matches_the_enumeration(self):
        self.assertEqual(self.names, self.enumerators[:-1])

    def test_the_message_switch_covers_every_code_exactly_once(self):
        self.assertEqual(self.cases, self.enumerators)
        self.assertEqual(len(set(self.cases)), len(self.cases), "a code has more than one case label")

    def test_the_message_switch_has_no_default_label(self):
        """A `default:` would absorb a code no table knows, turning a build error into a silent one."""
        self.assertNotIn("default:", self.switch_body)

    def test_no_consumer_invents_a_name_for_an_unknown_code(self):
        """Out-of-range codes yield an empty string, never a plausible-looking diagnostic."""
        self.assertNotIn("Unknown warning", self.source)
        self.assertNotIn("unknown warning", self.source)

    def test_the_setting_prefix_is_written_once(self):
        self.assertEqual(self.source.count('"debug/barista_script/warnings/"'), 1)
        self.assertNotIn("foundry_script/warnings", self.source)

    def test_no_second_table_keyed_by_code(self):
        """One name table and one default table; a parallel array is what this forbids."""
        self.assertEqual(len(NAMES_PATTERN.findall(self.source)), 1)
        self.assertEqual(len(DEFAULTS_PATTERN.findall(self.header)), 1)
        self.assertEqual(len(SWITCH_PATTERN.findall(self.source)), 1)

    def test_both_tables_are_length_checked_at_compile_time(self):
        self.assertIn("static_assert(sizeof(default_warning_levels)", self.header)
        self.assertIn("static_assert(sizeof(names)", self.source)


if __name__ == "__main__":
    result = unittest.main(argv=[sys.argv[0], "-v"], exit=False).result
    raise SystemExit(0 if result.wasSuccessful() else 1)
