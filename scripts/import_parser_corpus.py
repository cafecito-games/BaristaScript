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
  * A `.out` is rewritten to the one line the harness compares. Upstream's first
    line is a status word and the rest is the *runtime* transcript of a language
    BaristaScript cannot yet run; keeping either would be asserting something no
    M2 build can produce. `FS_TEST_OK` becomes the success sentinel, and
    `FS_TEST_PARSER_ERROR` becomes upstream's own message line, which is what
    `BSParser` is a hard fork of and must therefore reproduce verbatim.
  * `FS_TEST_ANALYZER_ERROR` cases parse cleanly and fail analysis, and there is
    no analyzer until M3. Their expectation is the parse outcome, and every one
    of them is listed in the baseline under `analyzer_deferred` with the
    upstream diagnostic it will be restored to, so M3 has an exact list rather
    than a memory.
  * Upstream's `.fsignore` is not copied. It is an empty file that opts the
    parser scripts out of Foundry's *runtime* suite; imported into a harness
    that honours it, it would silently skip all 340 cases at exit code 0. The
    harness's marker is `.baristaignore` for the same reason.
  * The sixteen files the D1 delta and the fork rename touch are triaged by
    DELETIONS, REPLACEMENT_*, EDITS and EXPECTATION_OVERRIDES below, which are
    the single copy of that table.

Nothing here normalizes whitespace or line endings: the harness compares
exactly, so the import must preserve exactly.
"""

from __future__ import annotations

import argparse
import filecmp
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT / "scripts") not in sys.path:
    sys.path.insert(0, str(ROOT / "scripts"))
from corpus_ledger import barista_path, build_triage_from_maps, validate_triage_ledger  # noqa: E402

# The Foundry revision this import is pinned to. Foundry moves; an import that
# ran against a different tree would produce a corpus whose provenance is a
# guess, so the script refuses rather than warns.
FOUNDRY_REVISION = "c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6"

# Upstream runnable-case count at FOUNDRY_REVISION for modules/.../parser
# (helpers are counted separately and never contribute to this total).
PARSER_UPSTREAM_TOTAL = 344

CORPUS_SUBPATH = Path("modules/foundry_script/tests/scripts/parser")
DESTINATION = ROOT / "project" / "tests" / "corpus" / "parser"
BASELINE_PATH = ROOT / "tests" / "corpus_baseline.json"
SENTINEL_HEADER = ROOT / "src" / "bs_corpus_sentinels.h"

CATEGORIES = ("features", "errors", "warnings")

UPSTREAM_OK = "FS_TEST_OK"
UPSTREAM_PARSER_ERROR = "FS_TEST_PARSER_ERROR"
UPSTREAM_ANALYZER_ERROR = "FS_TEST_ANALYZER_ERROR"


def success_sentinel() -> str:
    """The expectation text, read from the compiled extension's own header.

    Restating the literal here would put a second spelling of it in a third
    language, and a corpus whose 340 expectations disagree with the harness by
    one character fails in a way that looks like a port defect.
    """
    match = re.search(
        r'SUCCESS_SENTINEL\s*=\s*"([^"]+)"', SENTINEL_HEADER.read_text(encoding="utf-8")
    )
    if match is None:
        raise SystemExit(
            f"could not read SUCCESS_SENTINEL from {SENTINEL_HEADER}; the corpus expectations "
            "have no definition to be written from"
        )
    return match.group(1)


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
# This is the only copy of the table. Issue #10's body documents it; it does not
# duplicate it.
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
        "root": "res://tests/corpus/analyzer",
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


def run_git(repository: Path, *arguments: str) -> str:
    completed = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        raise SystemExit(
            f"git {' '.join(arguments)} failed in {repository}: {completed.stderr.strip()}"
        )
    return completed.stdout.strip()


def verify_revision(foundry: Path, requested: str) -> None:
    """Refuse to import from anything but the pinned revision.

    A corpus imported from an unrecorded tree cannot be re-derived, and the
    revision is cited case by case in the PR that lands it.
    """
    if requested != FOUNDRY_REVISION:
        raise SystemExit(
            f"--revision {requested} is not the pinned revision {FOUNDRY_REVISION}; this import "
            "reproduces one corpus, and a different revision would produce a different one"
        )
    head = run_git(foundry, "rev-parse", "HEAD")
    if head != FOUNDRY_REVISION:
        raise SystemExit(
            f"{foundry} is at {head}, not the pinned revision {FOUNDRY_REVISION}; check it out "
            "before importing"
        )
    dirty = run_git(foundry, "status", "--porcelain", "--", str(CORPUS_SUBPATH))
    if dirty:
        raise SystemExit(
            f"{foundry / CORPUS_SUBPATH} has uncommitted changes; the import would record a tree "
            f"that is not {FOUNDRY_REVISION}:\n{dirty}"
        )


def read_bytes(path: Path) -> bytes:
    return path.read_bytes()


def expectation_line(upstream_out: bytes, path: str, sentinel: str) -> tuple[str, str | None]:
    """The one expectation line for `path`, and the upstream text it defers.

    Returns (expectation, deferred_upstream_diagnostic). The second is non-None
    only for an analyzer case, whose real expectation M3 has to restore.
    """
    text = upstream_out.decode("utf-8")
    lines = text.split("\n")
    status = lines[0]
    if status == UPSTREAM_OK:
        return sentinel, None
    if status == UPSTREAM_PARSER_ERROR:
        if len(lines) < 2 or not lines[1]:
            raise SystemExit(f"{path}: upstream parser-error expectation has no message line")
        return lines[1], None
    if status == UPSTREAM_ANALYZER_ERROR:
        deferred = "\n".join(line for line in lines[1:] if line)
        return sentinel, deferred
    raise SystemExit(
        f"{path}: unrecognized upstream status {status!r}. The import refuses to guess what a "
        "status word it has never seen means for a parser-only evaluation"
    )


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


def import_corpus(foundry: Path, destination: Path) -> dict:
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
    analyzer_deferred: dict[str, str] = {}

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
                deferred = None
                replacement_applied = True
            else:
                source_bytes = read_bytes(source_path)
                if relative in edits_by_path:
                    source_bytes = apply_edits(source_bytes, relative, edits_by_path[relative])
                    applied_edits.add(relative)
                override = overrides_by_path.get(relative)
                if override is not None:
                    source_bytes = apply_edits(source_bytes, relative, override[0])
                    applied_overrides.add(relative)
                target.write_bytes(source_bytes)
                expectation, deferred = expectation_line(
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
            if deferred is not None:
                analyzer_deferred[case] = deferred

    missing_deletions = sorted(set(DELETIONS) - applied_deletions)
    missing_edits = sorted(set(edits_by_path) - applied_edits)
    missing_overrides = sorted(set(overrides_by_path) - applied_overrides)
    if missing_deletions or missing_edits or missing_overrides or not replacement_applied:
        raise SystemExit(
            "the triage table names files the upstream corpus does not contain: "
            f"deletions {missing_deletions}, edits {missing_edits}, "
            f"overrides {missing_overrides}, replacement applied {replacement_applied}"
        )

    return {
        "cases": sorted(cases),
        "helpers": sorted(helpers),
        "analyzer_deferred": analyzer_deferred,
    }


def write_readme(destination: Path, summary: dict) -> None:
    sentinel = success_sentinel()
    counts = {category: 0 for category in CATEGORIES}
    for case in summary["cases"]:
        counts[case.split("/", 1)[0]] += 1
    deferred = "\n".join(f"- `{case}`" for case in sorted(summary["analyzer_deferred"]))
    (destination / "README.md").write_text(
        f"""# Parser conformance corpus

Imported from Foundry `cafecito-games/Foundry` @ `{FOUNDRY_REVISION}`,
`{CORPUS_SUBPATH.as_posix()}`, by `scripts/import_parser_corpus.py`. Do not edit these files by
hand: the script is the single copy of the D1 triage table and `--check` proves this tree is what it
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

Each `.out` holds one line: the success sentinel, or the exact diagnostic the front end must
produce, compared byte for byte. Upstream's `.out` files carry a status word and then the *runtime*
transcript of the case; neither survives the import, because M2 has no runtime to produce a
transcript with and the status word is not a diagnostic.

## What these cases assert at M2

The harness evaluates a case through the tokenizer and the parser. So a `{sentinel}` expectation
here means **"this source parses without a diagnostic"**, not "this source behaves correctly" --
the value it printed upstream is not checked, and neither are the warnings the `warnings/` cases are
named for. That is not a gap this milestone can close: the analyzer does not exist until M3, and
inventing warning expectations now would be inventing the analyzer's output. The 29 `warnings/`
cases earn their place regardless: they are 29 more real sources the parser has to accept.

## Cases whose expectation M3 must restore

Upstream marks {len(summary["analyzer_deferred"])} of these `FS_TEST_ANALYZER_ERROR`: the parser
accepts them and the *analyzer* rejects them. With no analyzer their honest M2 expectation is the
parse outcome, so each is listed in `tests/corpus_baseline.json` under `analyzer_deferred` together
with the upstream diagnostic it owes:

{deferred}
""",
        encoding="utf-8",
    )


def parser_baseline_entry(summary: dict) -> dict:
    """The parser corpus ledger entry, including triage derived from the tables."""
    triage = parser_triage_ledger()
    entry = {
        "root": "res://tests/corpus/parser",
        "total": len(summary["cases"]),
        "skipped": len(summary["helpers"]),
        "expected_failures": [],
        "foundry_revision": FOUNDRY_REVISION,
        "upstream_total": PARSER_UPSTREAM_TOTAL,
        "triage": triage,
        "analyzer_deferred": dict(sorted(summary["analyzer_deferred"].items())),
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


def write_baseline(summary: dict) -> None:
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
    BASELINE_PATH.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def compare_trees(left: Path, right: Path) -> list[str]:
    differences: list[str] = []

    def walk(directory: Path, root: Path) -> set[str]:
        return {path.relative_to(root).as_posix() for path in directory.rglob("*") if path.is_file()}

    left_files = walk(left, left)
    right_files = walk(right, right)
    for name in sorted(left_files - right_files):
        differences.append(f"only in the committed tree: {name}")
    for name in sorted(right_files - left_files):
        differences.append(f"only in a fresh import: {name}")
    for name in sorted(left_files & right_files):
        if not filecmp.cmp(left / name, right / name, shallow=False):
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
    verify_revision(foundry, arguments.revision)

    if arguments.check:
        with tempfile.TemporaryDirectory() as temporary:
            fresh = Path(temporary) / "parser"
            summary = import_corpus(foundry, fresh)
            write_readme(fresh, summary)
            differences = compare_trees(DESTINATION, fresh)
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
        # Provenance fields the importer owns must match what a fresh import emits.
        for field in (
            "total",
            "skipped",
            "foundry_revision",
            "upstream_total",
            "triage",
            "analyzer_deferred",
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

    summary = import_corpus(foundry, DESTINATION)
    write_readme(DESTINATION, summary)
    write_baseline(summary)
    print(
        f"imported {len(summary['cases'])} cases and {len(summary['helpers'])} helpers "
        f"from {FOUNDRY_REVISION}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
