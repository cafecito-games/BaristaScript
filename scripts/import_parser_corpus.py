#!/usr/bin/env python3
# import_parser_corpus.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Import Foundry's parser conformance corpus into project/tests/corpus/parser.

The corpus is the exit criterion of the M2 port, so how it got here has to be
reproducible rather than remembered. This script is that record: point it at a
Foundry checkout at the pinned revision and it rebuilds the imported tree from
scratch, triage edits included, byte for byte.

    python3 scripts/import_parser_corpus.py --foundry ../Foundry \\
        --revision c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6
    python3 scripts/import_parser_corpus.py --foundry ../Foundry \\
        --revision c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6 --check

`--check` rebuilds into a temporary directory and diffs, so CI or a reviewer can
prove the committed tree is what this script produces without touching it.

What the import does, and why each part of it is not a judgement call:

  * `.fs` becomes `.barista`; `.notest.fs` becomes `.notest.barista` and stays
    uncounted, because it is a helper source the runner imports, not a case
    (`modules/foundry_script/tests/fs_test_runner.cpp:539-540`).
  * The shared static extractor restores all four analyzer-error blocks and 29
    warning cases at analyzer stage; the remaining cases stay parser stage.
    Original full blocks and source hashes remain in generated source provenance.
  * Upstream's `.fsignore` is not copied. It is an empty file that opts the
    parser scripts out of Foundry's *runtime* suite; imported into a harness
    that honours it, it would silently skip all 340 cases at exit code 0. The
    harness's marker is `.baristaignore` for the same reason.
  * The twenty dispositions the D1 delta and the fork rename touch are triaged by
    DELETIONS, REPLACEMENT_*, EDITS and EXPECTATION_OVERRIDES below, which are
    the single copy of that table.

Nothing here normalizes whitespace or line endings: the harness compares
exactly, so the import must preserve exactly.
"""

from __future__ import annotations

import argparse
import filecmp
import os
import json
import shutil
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT / "scripts") not in sys.path:
    sys.path.insert(0, str(ROOT / "scripts"))
from corpus_expectations import success_sentinel, extract_static_block  # noqa: E402
from import_analyzer_corpus import (source_policy_changes, source_record, default_policy, patch, change, sha, encoded, SHARED_SUPPORT_ROOTS)
from corpus_stages import write_stages  # noqa: E402
from corpus_ledger import barista_path, build_triage_from_maps, validate_triage_ledger  # noqa: E402

from corpus_registry import load_registry, local_path, tree_entries, validate_registration, verify_checkout  # noqa: E402

REGISTRY = load_registry(ROOT)
FOUNDRY_REVISION = REGISTRY["revision"]

# Upstream runnable-case count at FOUNDRY_REVISION for modules/.../parser
# (helpers are counted separately and never contribute to this total).
PARSER_UPSTREAM_TOTAL = 344

CORPUS_SUBPATH = Path(REGISTRY["corpora"]["parser"]["source"])
DESTINATION = ROOT / REGISTRY["corpora"]["parser"]["destination"]
BASELINE_PATH = ROOT / "tests" / "corpus_baseline.json"

CATEGORIES = ("features", "errors", "warnings")
SUPPORT_DESTINATION = ROOT / "project" / SHARED_SUPPORT_ROOTS["utils.notest.fs"].removeprefix("res://")
SUPPORT_URI = SHARED_SUPPORT_ROOTS["utils.notest.fs"] + "/utils.notest.barista"
ANALYZER_ERRORS = {
    "errors/export_enum_wrong_array_type.fs", "errors/export_enum_wrong_type.fs",
    "errors/export_tool_button_requires_tool_mode.fs",
    "features/contextual_tagged_union_shorthand.norun.fs",
}


def case_stage(path):
    return "analyzer" if path in ANALYZER_ERRORS or path.startswith("warnings/") else "parser"


UPSTREAM_OK = "FS_TEST_OK"
UPSTREAM_PARSER_ERROR = "FS_TEST_PARSER_ERROR"
UPSTREAM_ANALYZER_ERROR = "FS_TEST_ANALYZER_ERROR"

# --------------------------------------------------------------------------
# The D1 triage table.
#
# D1 (docs/GRAMMAR.md section 0.2) removes the integer tower, so a case either
# tests a feature that no longer exists, or happens to spell a removed type
# while testing something else.
#
# Issue #10 derives the affected set from
# `grep -rlE '\b(uint|ulong)\b|as!|[0-9](UL|U|L)\b' --include='*.fs'`, which
# returns nine files. That grep under-approximates, and running the corpus is
# what showed by how much: it does not match bare `long`, it does not match a
# lowercase or misordered suffix (`1u`, `1uL`, `1LU`), and no grep at all can
# match `errors/fixed_width_integer_unsuffixed_overflow.fs`, whose whole subject
# is an unsuffixed decimal above the signed range that upstream answers by
# naming `ulong`. Seven further cases land here for those reasons. The set below
# is the one derived from what the corpus actually does, not from the grep; the
# grep survives as the AC-4 gate, which is a regression check on the spellings
# it does cover and not a discovery tool.
#
# One further case, `errors/identifier_similar_to_keyword.fs`, is triaged for
# the hard fork rather than for a language delta: its diagnostic names the
# language, and this one is BaristaScript.
#
# This is the only copy of the disposition *reasons*. Issue #10's body documents
# it; it does not duplicate it. `parser_triage_ledger()` projects these tables
# into `tests/corpus_baseline.json` without restating any reason text.
# --------------------------------------------------------------------------

# Cases whose whole subject is the removed integer tower. They test range
# checking and suffix selection across four integer types, none of which exist.
DELETIONS = {
    "errors/fixed_width_integer_long_below_min.fs":
        'Asserts `Integer literal is out of range for "long".` for -9223372036854775809L. '
        '"long" is removed and the suffix is reserved, so the diagnostic it asserts cannot be '
        "produced and the case has no BaristaScript meaning.",
    "errors/fixed_width_integer_negative_unsigned.fs":
        "Asserts that a `UL` suffix does not reinterpret a negative magnitude. There is no "
        "unsigned type to reinterpret into.",
    "errors/fixed_width_integer_suffix_out_of_range.fs":
        'Asserts `Integer literal is out of range for "uint".` for 4294967296U. Same: the type '
        "and the suffix are both gone.",
    "errors/fixed_width_integer_ulong_above_max.fs":
        'Asserts `Integer literal is out of range for "ulong".` for a literal above 2^64-1. Same.',
}

# The one case replaced rather than deleted: its subject survives D1 inverted.
# Upstream asserted that every fixed-width literal form parses; BaristaScript
# asserts that the first of them is rejected, by the single definition in
# BSTokenizer::removed_type_name_diagnostic()'s sibling suffix diagnostic. It
# keeps the upstream path so the removal is findable where the feature was, and
# it is the one file the AC-4 grep gate is allowed to return.
REPLACEMENT_PATH = "features/fixed_width_integer_literals.fs"
REPLACEMENT_SOURCE = '''\
# D1 (docs/GRAMMAR.md section 0.2) removes Foundry's four-type integer tower. The literal suffixes
# are reserved rather than freed (section 2.5), so ported Foundry Script fails loudly here instead
# of silently changing meaning, and reintroducing a numeric tower later stays additive.
#
# Upstream's features/fixed_width_integer_literals.fs @ c9d5e35 asserted that all four widths and
# every suffix form parsed and printed. This case is that assertion inverted: the first suffix in
# the file is rejected, with the wording GRAMMAR section 2.5 specifies.
func test():
	print(42U)
	print(42L)
	print(42UL)
	print(0xFFFFU)
	print(0b1010UL)
	print(1_000_000L)
'''
REPLACEMENT_EXPECTATION = (
    'The "U" integer literal suffix is reserved. BaristaScript has one integer type, "int"; '
    'write "42".'
)
REPLACEMENT_REASON = (
    "Upstream asserted that `uint`/`long`/`ulong` literals parse; D1 removes them. Kept as the "
    "removal's own golden file rather than deleted, so the corpus proves the rejection happens "
    "where the feature used to be tested."
)

# Cases that test something D1 leaves untouched but happen to spell a removed
# type while doing it. Each substitution is the smallest that keeps the case's
# actual subject intact, and none of them changes the expectation: an edit that
# needed the .out rewritten would be a different case, not this one.
#
# Each entry is (upstream path, [(exact old text, new text)], reason).
EDITS = [
    (
        "features/contextual_tagged_union_shorthand.norun.fs",
        [("enum Result[T, E]", "enum Result"), ("enum Option[T]", "enum Option")],
        "M3 removes only unused declaration parameter lists; concrete payload types, line layout "
        "and all six original contextual shorthand diagnostics remain unchanged.",
    ),
    *[("warnings/" + name + ".fs", [("../../utils.notest.fs", SUPPORT_URI)],
       "Relocate the original preload to the real adapted auxiliary .barista helper outside "
       "aggregate corpus discovery; preserve the line layout and original warning block.")
      for name in ("return_value_discarded", "standalone_expression")],
    (
        "errors/type_alias_duplicate_name.fs",
        [("type Unsigned = uint | ulong", "type Unsigned = String | bool")],
        "The case asserts that a type alias may not reuse a constant's name; its union members are "
        "incidental. Two non-numeric members keep the union arity and the alias name the "
        "expectation quotes, so the .out is untouched.",
    ),
    (
        "errors/type_alias_union_missing_member.fs",
        [("type Unsigned = uint |", "type Unsigned = String |")],
        'The case asserts `Expected a type after "|".` for a union with a trailing bar. Only the '
        "member before the bar has to be a legal type; substituting it changes nothing the case "
        "tests, and the .out is untouched.",
    ),
    (
        "features/type_alias_declarations.fs",
        [("type Unsigned = uint | ulong", "type Unsigned = String | bool")],
        "The case asserts that `type` is a contextual keyword across file, class and expression "
        "scope. Line 3 is one of five alias declarations and the only one spelling a removed type; "
        "the other four (`float`, `String? | int`, `int | float`) already survive D1 unchanged.",
    ),
    (
        "warnings/static_called_on_instance.fs",
        [("8589934592UL", "8589934592")],
        "The case asserts the STATIC_CALLED_ON_INSTANCE warning. The `UL` suffix on lines 11 and 13 "
        "is reserved under D1; the literal fits the one signed 64-bit carrier, so dropping the "
        "suffix preserves the value exactly and the call sites the warning is about are unchanged.",
    ),
    (
        "features/enum_host_functions.norun.fs",
        [("static func parse(p_name: String) -> long:", "static func parse(p_name: String) -> int:")],
        "The case asserts that an enum may host methods, static methods, an async static method and "
        "a nested enum. One of the five host functions returns `long`; the body returns "
        "`p_name.length()`, which is an `int` in Foundry too, so `int` is the return type the case "
        "already had in everything but spelling.",
    ),
    (
        "features/enum_name_host_functions.norun.fs",
        [("static func parse(p_name: String) -> long:", "static func parse(p_name: String) -> int:")],
        "The `enum_name` sibling of the case above, with the same single `long` return type and the "
        "same `p_name.length()` body.",
    ),
]

# Cases whose *source* survives D1 intact but whose upstream expectation quotes
# something BaristaScript does not have. The source is imported unchanged except
# where a comment describes the removed rule; only the expectation moves.
#
# These are the deviations from upstream to scrutinise hardest, so each carries
# the upstream text it replaces alongside the delta that justifies it, and each
# is a diagnostic BaristaScript already pins by fixture in
# project/tests/corpus_fixtures/tokenizer/.
#
# Each entry is (upstream path, [(exact old text, new text)] for the source,
# upstream expectation, new expectation, reason).
EXPECTATION_OVERRIDES = [
    (
        "warnings/narrowing_conversion.fs", [],
        "~~ WARNING at line 5: (NARROWING_CONVERSION) Narrowing conversion (float is converted to int and loses precision).",
        '>> ERROR at line 5: Invalid argument for "i_accept_ints_only()" function: argument 1 should be "int" but is "float".',
        "D1 (docs/GRAMMAR.md implicit numeric conversions) requires an explicit cast for the "
        "fractional 12.345 argument. The positional call validator rejects it at argument line 5; "
        "preserve the pinned warning block in source provenance instead of relaxing hard conversion.",
    ),
    (
        "errors/fixed_width_integer_suffix_lowercase.fs",
        [("\t# Integer suffixes are uppercase only.\n",
          "\t# D1 reserves every integer literal suffix, in any case.\n")],
        'Invalid integer suffix "u". Integer suffixes are uppercase "U", "L", or "UL"; write "1U".',
        'The "u" integer literal suffix is reserved. BaristaScript has one integer type, "int"; '
        'write "1".',
        "Upstream rejects `1u` for its *case*, and its diagnostic tells the reader to write `1U` -- "
        "a spelling D1 removes outright. BaristaScript has no suffix in any case, so the "
        "reservation diagnostic of GRAMMAR section 2.5 is the only honest answer. Pinned by "
        "project/tests/corpus_fixtures/tokenizer/suffix_lowercase_misordered.",
    ),
    (
        "errors/fixed_width_integer_suffix_mixed_case.fs",
        [("\t# A mixed-case suffix names its canonical uppercase replacement.\n",
          "\t# D1 reserves the suffix whatever its case; the diagnostic quotes it as written.\n")],
        'Invalid integer suffix "uL". Integer suffixes are uppercase "U", "L", or "UL"; write "1UL".',
        'The "uL" integer literal suffix is reserved. BaristaScript has one integer type, "int"; '
        'write "1".',
        "Same delta as the case above: upstream's replacement text `1UL` is itself removed by D1.",
    ),
    (
        "errors/fixed_width_integer_suffix_order.fs",
        [('\t# "LU" is not a suffix; the canonical order is "UL".\n',
          "\t# D1 reserves any suffix letters on an integer literal, in any order.\n")],
        'Invalid integer suffix "LU". Integer suffixes are uppercase "U", "L", or "UL"; write "1UL".',
        'The "LU" integer literal suffix is reserved. BaristaScript has one integer type, "int"; '
        'write "1".',
        "Same delta again: there is no canonical order to name when no suffix exists.",
    ),
    (
        "errors/fixed_width_integer_unsuffixed_overflow.fs",
        [('\t# Above the signed range an unsuffixed literal names the "UL" suffix.\n',
          "\t# D1 leaves one signed 64-bit carrier, so above its range there is no wider type.\n")],
        'Integer literal is out of range for "long"; add the "UL" suffix to write a "ulong".',
        'Integer literal is out of range for "int", the only integer type; BaristaScript stores '
        "every integer on one signed 64-bit carrier.",
        "The one D1 case no grep finds: the source is the plain decimal 9223372036854775808. "
        "Upstream's diagnostic offers `ulong` as the type that would hold it; D1 removes both that "
        "type and the suffix that would select it, so the range is final. Pinned by "
        "project/tests/corpus_fixtures/tokenizer/integer_above_range.",
    ),
    (
        "errors/identifier_similar_to_keyword.fs",
        [],
        'Identifier "\u0430s" is visually similar to the FoundryScript keyword "as" and thus not '
        "allowed.",
        'Identifier "\u0430s" is visually similar to the BaristaScript keyword "as" and thus not '
        "allowed.",
        "Not a language delta: the diagnostic names the language, and the hard fork renamed it. "
        "The confusable-identifier rule, the Cyrillic source and the keyword it collides with are "
        "all unchanged.",
    ),
]


def parser_triage_ledger() -> dict[str, dict[str, str]]:
    """Ledger records derived from the in-code disposition tables.

    Reasons live only in DELETIONS / REPLACEMENT_REASON / EDITS /
    EXPECTATION_OVERRIDES; this function maps those tables into the shared
    schema without restating any reason text.
    """
    excluded = {barista_path(path): reason for path, reason in DELETIONS.items()}
    rewritten = {barista_path(REPLACEMENT_PATH): REPLACEMENT_REASON}
    rewritten.update({barista_path(path): reason for path, _edits, reason in EDITS})
    overrides = {
        barista_path(path): reason
        for path, _edits, _upstream, _new, reason in EXPECTATION_OVERRIDES
    }
    return build_triage_from_maps(
        excluded=excluded,
        rewritten=rewritten,
        expectation_overrides=overrides,
        deferred={},
    )


def analyzer_scaffold_entry() -> dict:
    """Pending analyzer ledger slot for #45.

    Records the pinned upstream enumeration only. Final imported/skipped totals
    and path-specific dispositions are filled by execution-driven triage, not
    guessed here.
    """
    return {
        "imported": False,
        "root": "res://" + REGISTRY["corpora"]["analyzer"]["destination"][len("project/"):],
        "foundry_revision": FOUNDRY_REVISION,
        "upstream_total": 1346,
        "upstream_helpers": 250,
        "upstream_sources": 1596,
        "upstream_status_distribution": {
            "analyzer_error": 844,
            "ok": 480,
            "parser_error": 18,
            "compiler_error": 2,
            "runtime_error": 2,
        },
        "total": 0,
        "skipped": 0,
        "expected_failures": [],
        "triage": build_triage_from_maps(),
        "comment": (
            "Schema scaffold only. #45 imports the analyzer corpus, classifies "
            "every residual with a path-specific disposition, and writes the "
            "execution-derived totals. Grep hit counts are never the population."
        ),
    }


def verify_revision(foundry: Path, requested: str, *, rebuilding: bool = False) -> None:
    """Validate shared delivery registration and the consumed upstream checkout."""
    try:
        registry = validate_registration(ROOT, baseline_path=BASELINE_PATH,
                                         rebuilding="parser" if rebuilding else None)
        local_path(ROOT, SUPPORT_DESTINATION.relative_to(ROOT).as_posix())
        verify_checkout(foundry, registry, requested)
    except (ValueError, OSError) as error:
        raise SystemExit(str(error)) from error


def read_bytes(path: Path) -> bytes:
    return path.read_bytes()


def expectation_line(upstream_out: bytes, path: str, sentinel: str) -> tuple[str, str]:
    """Select the pinned complete static block at the explicit owned stage."""
    try:
        block = extract_static_block(upstream_out, path)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    status = upstream_out.split(b"\n", 1)[0].decode("utf-8")
    stage = case_stage(path)
    if (status == UPSTREAM_ANALYZER_ERROR) != (path in ANALYZER_ERRORS):
        raise SystemExit(f"{path}: analyzer-error stage policy differs from pinned status")
    return (block if stage == "analyzer" or status == UPSTREAM_PARSER_ERROR else sentinel), stage


def source_changes(before, after, reason, edits):
    changes = []
    for old, new in edits:
        offset = 0
        needle = old.encode()
        while (start := before.find(needle, offset)) >= 0:
            changes.append(change(before, start, start + len(needle), new, reason))
            offset = start + len(needle)
    if patch(before, changes, "parser source adaptation") != after:
        raise ValueError("parser source adaptation differs from its exact patch provenance")
    return changes


def generate_support(foundry, destination):
    path = "utils.notest.fs"
    data = (foundry / CORPUS_SUBPATH.parent / path).read_bytes()
    policy = default_policy()
    changes = source_policy_changes(data, path, policy)
    record = source_record(data, path, "utils.notest.barista", "support_helper", changes, [])
    destination.mkdir(parents=True)
    (destination / record['imported_path']).write_bytes(patch(data, changes, path))
    (destination / "source_map.json").write_bytes(encoded({
        "schema_version": 1, "foundry_revision": FOUNDRY_REVISION,
        "root": SUPPORT_URI.rsplit('/', 1)[0], "sources": [record]}))
    return record


def apply_edits(source: bytes, path: str, edits: list[tuple[str, str]]) -> bytes:
    text = source.decode("utf-8")
    for old, new in edits:
        if old not in text:
            raise SystemExit(
                f"{path}: triage edit {old!r} does not appear in the upstream source. The corpus "
                "changed under the pinned revision, or the triage table is wrong; either way the "
                "import stops rather than producing a tree nobody described"
            )
        text = text.replace(old, new)
    return text.encode("utf-8")


def _generate_corpus(foundry: Path, destination: Path) -> dict:
    sentinel = success_sentinel()
    corpus_root = foundry / CORPUS_SUBPATH
    if not corpus_root.is_dir():
        raise SystemExit(f"{corpus_root} is not a directory")

    edits_by_path = {path: edits for path, edits, _reason in EDITS}
    overrides_by_path = {
        path: (edits, upstream, replacement)
        for path, edits, upstream, replacement, _reason in EXPECTATION_OVERRIDES
    }
    applied_edits: set[str] = set()
    applied_overrides: set[str] = set()
    applied_deletions: set[str] = set()
    replacement_applied = False

    if destination.exists():
        shutil.rmtree(destination)

    cases: list[str] = []
    helpers: list[str] = []
    stages = {}
    records = []

    for category in CATEGORIES:
        category_root = corpus_root / category
        if not category_root.is_dir():
            raise SystemExit(f"{category_root} is not a directory")
        for source_path in sorted(category_root.rglob("*.fs")):
            relative = source_path.relative_to(corpus_root).as_posix()

            if relative in DELETIONS:
                applied_deletions.add(relative)
                continue

            target = destination / relative
            target = target.with_suffix(".barista")
            target.parent.mkdir(parents=True, exist_ok=True)

            if relative.endswith(".notest.fs"):
                # A helper the runner imports, never a case: it has no .out and
                # must stay uncounted rather than become an unpaired failure.
                target.write_bytes(read_bytes(source_path))
                helpers.append(target.relative_to(destination).as_posix())
                continue

            upstream_out = source_path.with_suffix(".out")
            if not upstream_out.is_file():
                raise SystemExit(
                    f"{relative}: no upstream .out. Only .notest.fs helpers may lack one, and this "
                    "is not one"
                )

            if relative == REPLACEMENT_PATH:
                target.write_bytes(REPLACEMENT_SOURCE.encode("utf-8"))
                expectation = REPLACEMENT_EXPECTATION
                stage = "parser"
                replacement_applied = True
            else:
                source_bytes = read_bytes(source_path)
                if relative == "warnings/narrowing_conversion.fs" and sha(source_bytes) != "d41e732170651e7e7fd4fbf88c952be4415dfecfc6e7ba3052644150465bbde4":
                    raise SystemExit(f"{relative}: D1 fractional argument source preimage mismatch")
                if relative in edits_by_path:
                    source_bytes = apply_edits(source_bytes, relative, edits_by_path[relative])
                    applied_edits.add(relative)
                override = overrides_by_path.get(relative)
                if override is not None:
                    source_bytes = apply_edits(source_bytes, relative, override[0])
                    applied_overrides.add(relative)
                target.write_bytes(source_bytes)
                expectation, stage = expectation_line(
                    read_bytes(upstream_out), relative, sentinel
                )
                if override is not None:
                    # The override states the text it replaces, so an upstream
                    # rewording cannot be absorbed silently: if the expectation
                    # this import is overriding is not the one upstream holds,
                    # the justification recorded for it is about a different
                    # diagnostic and the import stops.
                    if expectation != override[1]:
                        raise SystemExit(
                            f"{relative}: the expectation override replaces\n  {override[1]!r}\n"
                            f"but upstream now holds\n  {expectation!r}"
                        )
                    expectation = override[2]

            expectation_target = target.with_suffix(".out")
            expectation_target.write_bytes((expectation + "\n").encode("utf-8"))
            case = target.relative_to(destination).as_posix()
            cases.append(case)
            stages[case] = stage
            if stage != "analyzer":
                continue
            original = read_bytes(source_path)
            upstream_bytes = read_bytes(upstream_out)
            reason = parser_triage_ledger()
            disposition = next((key for key, paths in reason.items() if case in paths), "imported")
            description = reason.get(disposition, {}).get(case, "Pinned static frontend expectation.")
            changes = source_changes(original, target.read_bytes(), description, edits_by_path.get(relative, []))
            record = source_record(original, "parser/" + relative, case, "case", changes, [])
            if relative in ("warnings/return_value_discarded.fs", "warnings/standalone_expression.fs"):
                record['references'] = [{'kind': 'preload', 'literal': '../../utils.notest.fs',
                                         'target': 'utils.notest.fs', 'relocated': SUPPORT_URI,
                                         'intentional_missing': False}]
            record.update({'expectation_identity': str(CORPUS_SUBPATH / relative[:-3]) + '.out',
                           'expectation_sha256': sha(upstream_bytes),
                           'status': upstream_bytes.split(b"\n", 1)[0].decode(),
                           'stage': stage, 'disposition': disposition, 'reason': description,
                           'original_expected_block': extract_static_block(upstream_bytes, relative),
                           'expected_block': expectation})
            records.append(record)

    missing_deletions = sorted(set(DELETIONS) - applied_deletions)
    missing_edits = sorted(set(edits_by_path) - applied_edits)
    missing_overrides = sorted(set(overrides_by_path) - applied_overrides)
    if missing_deletions or missing_edits or missing_overrides or not replacement_applied:
        raise SystemExit(
            "the triage table names files the upstream corpus does not contain: "
            f"deletions {missing_deletions}, edits {missing_edits}, "
            f"overrides {missing_overrides}, replacement applied {replacement_applied}"
        )

    selected = {path for path, stage in stages.items() if stage == "analyzer"}
    if len(selected) != 33 or len(selected & {barista_path(p) for p in ANALYZER_ERRORS}) != 4:
        raise SystemExit("parser restoration requires exactly four analyzer errors and 29 warning cases")
    write_stages(destination, cases, helpers, FOUNDRY_REVISION, stages)
    (destination / "source_map.json").write_bytes(encoded({
        "schema_version": 1, "foundry_revision": FOUNDRY_REVISION,
        "root": "res://tests/corpus/parser", "sources": records}))
    return {
        "cases": sorted(cases),
        "helpers": sorted(helpers),
        "stages": stages,
    }


def import_corpus(foundry: Path, destination: Path, *, publish_baseline: bool = False,
                  support_destination: Path | None = None) -> dict:
    """Validate and publish corpus, auxiliary support and baseline in one rollback scope."""
    support_destination = support_destination or destination.with_name(destination.name + "_support")
    destination.parent.mkdir(parents=True, exist_ok=True)
    support_destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".parser-import-", dir=destination.parent) as temporary:
        staging = Path(temporary)
        fresh = staging / "fresh"
        summary = _generate_corpus(foundry, fresh)
        generate_support(foundry, staging / "support")
        write_readme(fresh, summary)
        candidate_baseline = staging / "baseline.json"
        if publish_baseline:
            write_baseline(summary, candidate_baseline)
        outputs = [(fresh, destination), (staging / "support", support_destination)]
        if publish_baseline:
            outputs.append((candidate_baseline, BASELINE_PATH))
        moved, published = [], []
        try:
            for index, (source, target) in enumerate(outputs):
                backup = staging / ("previous-" + str(index))
                if target.exists():
                    os.replace(target, backup)
                    moved.append((backup, target))
                os.replace(source, target)
                published.append(target)
        except BaseException:
            for target in reversed(published):
                if target.is_dir():
                    shutil.rmtree(target)
                else:
                    target.unlink()
            for backup, target in reversed(moved):
                os.replace(backup, target)
            raise
        return summary


def write_readme(destination: Path, summary: dict) -> None:
    sentinel = success_sentinel()
    counts = {category: 0 for category in CATEGORIES}
    for case in summary["cases"]:
        counts[case.split("/", 1)[0]] += 1
    (destination / "README.md").write_text(
        f"""# Parser conformance corpus

Imported from Foundry `cafecito-games/Foundry` @ `{FOUNDRY_REVISION}`,
`{CORPUS_SUBPATH.as_posix()}`, by `scripts/import_parser_corpus.py`. Do not edit these files by
hand: the script is the single copy of the D1 triage *reasons* (projected into
`tests/corpus_baseline.json` without duplication) and `--check` proves this tree is what it
produces.

| Category | Cases |
|---|---|
| `features/` | {counts["features"]} |
| `errors/` | {counts["errors"]} |
| `warnings/` | {counts["warnings"]} |
| **total** | **{len(summary["cases"])}** |

Plus {len(summary["helpers"])} `.notest.barista` helper sources, which are skipped and never counted.
Upstream at this pin holds {PARSER_UPSTREAM_TOTAL} runnable parser cases; the triage ledger in
`tests/corpus_baseline.json` accounts for every non-import disposition so
`upstream_total == imported + excluded + deferred`.

Each `.out` is a complete static diagnostic block followed by exactly one LF. The generated
`case_stages.json` assigns each runnable case its explicit frontend stage; helpers have no entries.
Do not use `--update-expectations` here: the importer owns sources, expectations and stages.

## Static frontend coverage

Exactly 33 cases run through the analyzer (the four original analyzer-error cases and all
29 warning cases); the remaining {len(summary["cases"]) - 33} assert parser coverage.
`{sentinel}` means acceptance at that case's explicit stage. The deprecated-operators control
asserts analyzer acceptance with zero warnings. No runtime transcript or case function executes.

The original blocks contain 53 warnings and 9 errors. The single documented D1 fractional
float-to-int expectation override projects these to 52 warnings and 10 errors. `source_map.json`
binds each selected original source/output hash, full original block, adapted bytes and final expectation.

The real `utils.notest.barista` auxiliary lives at `res://tests/corpus_support/parser/`, outside
aggregate discovery. Its source map records the pinned bytes and four exact D1 annotation patches.
The parser importer owns that support tree transactionally alongside this corpus and its baseline.
""",
        encoding="utf-8",
    )


def parser_baseline_entry(summary: dict) -> dict:
    """The parser corpus ledger entry, including triage derived from the tables."""
    triage = parser_triage_ledger()
    entry = {
        "imported": True,
        "root": "res://" + REGISTRY["corpora"]["parser"]["destination"][len("project/"):],
        "total": len(summary["cases"]),
        "skipped": len(summary["helpers"]),
        "expected_failures": [],
        "foundry_revision": FOUNDRY_REVISION,
        "upstream_total": PARSER_UPSTREAM_TOTAL,
        "triage": triage,
    }
    complaint = validate_triage_ledger(
        "parser",
        entry,
        disk_cases=set(summary["cases"]),
        disk_helpers=set(summary["helpers"]),
    )
    if complaint is not None:
        raise SystemExit(complaint)
    return entry


def write_baseline(summary: dict, destination: Path | None = None) -> None:
    """Write the baseline, preserving any non-parser corpus entries already present."""
    existing: dict = {}
    if BASELINE_PATH.is_file():
        existing = json.loads(BASELINE_PATH.read_text(encoding="utf-8"))

    corpora = dict(existing.get("corpora", {}))
    corpora["parser"] = parser_baseline_entry(summary)
    if "analyzer" not in corpora:
        corpora["analyzer"] = analyzer_scaffold_entry()

    document = {
        "comment": [
            "The committed conformance baseline and triage ledger.",
            "tests/validate_ci.py requires tests/gdscript_suites.json to pin the exact",
            "summary line each imported corpus implies, and proves every upstream",
            "runnable case is imported or has exactly one path-specific disposition.",
            "Regenerated by scripts/import_parser_corpus.py for the parser entry;",
            "analyzer dispositions are filled by #45. Do not hand-edit reasons here",
            "when an importer owns them — edit the importer tables instead.",
        ],
        "corpora": corpora,
    }
    (destination or BASELINE_PATH).write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def compare_trees(left: Path, right: Path) -> list[str]:
    differences: list[str] = []

    try:
        left_entries = tree_entries(left)
        right_entries = tree_entries(right)
    except (ValueError, OSError) as error:
        return [str(error)]
    for name in sorted(left_entries.keys() - right_entries.keys()):
        differences.append(f"only in the committed tree: {name}")
    for name in sorted(right_entries.keys() - left_entries.keys()):
        differences.append(f"only in a fresh import: {name}")
    for name in sorted(left_entries.keys() & right_entries.keys()):
        if left_entries[name] != right_entries[name]:
            differences.append(f"entry type differs from a fresh import: {name}")
        elif left_entries[name] == "file" and not filecmp.cmp(left / name, right / name, shallow=False):
            differences.append(f"differs from a fresh import: {name}")
    return differences


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--foundry", required=True, help="path to a Foundry checkout")
    parser.add_argument("--revision", required=True, help=f"must be {FOUNDRY_REVISION}")
    parser.add_argument(
        "--check",
        action="store_true",
        help="import into a temporary directory and diff against the committed tree",
    )
    arguments = parser.parse_args(argv)

    foundry = Path(arguments.foundry).resolve()
    verify_revision(foundry, arguments.revision, rebuilding=not arguments.check)

    if arguments.check:
        with tempfile.TemporaryDirectory() as temporary:
            fresh = Path(temporary) / "parser"
            support = Path(temporary) / "support"
            summary = import_corpus(foundry, fresh, support_destination=support)
            write_readme(fresh, summary)
            differences = compare_trees(DESTINATION, fresh)
            differences += ["auxiliary support: " + d for d in compare_trees(SUPPORT_DESTINATION, support)]
            expected_entry = parser_baseline_entry(summary)
        if differences:
            print("the committed corpus is not what the importer produces:")
            for difference in differences:
                print(f"  {difference}")
            return 1
        committed = json.loads(BASELINE_PATH.read_text(encoding="utf-8"))
        committed_parser = committed.get("corpora", {}).get("parser")
        if committed_parser is None:
            print("tests/corpus_baseline.json has no parser corpus entry")
            return 1
        if "analyzer_deferred" in committed_parser:
            print("obsolete analyzer_deferred field is forbidden")
            return 1
        # Provenance fields the importer owns must match what a fresh import emits.
        for field in (
            "total",
            "skipped",
            "foundry_revision",
            "upstream_total",
            "triage",
            "expected_failures",
        ):
            if committed_parser.get(field) != expected_entry.get(field):
                print(
                    f"committed parser ledger field {field!r} does not match a fresh import"
                )
                return 1
        print(
            f"corpus and triage ledger match a fresh import of {FOUNDRY_REVISION}: "
            f"{len(summary['cases'])} cases"
        )
        return 0

    summary = import_corpus(foundry, DESTINATION, publish_baseline=True, support_destination=SUPPORT_DESTINATION)
    print(
        f"imported {len(summary['cases'])} cases and {len(summary['helpers'])} helpers "
        f"from {FOUNDRY_REVISION}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
