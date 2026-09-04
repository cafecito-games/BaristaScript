#!/usr/bin/env python3
# corpus_ledger.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Shared triage-ledger schema and validation for imported Foundry corpora.

Every imported corpus records a machine-readable ledger: the pinned upstream
runnable-case count, the imported case/helper counts, and every non-verbatim
disposition (excluded, rewritten, expectation-overridden, or milestone-deferred)
with a path-specific non-empty reason.

Population equation (helpers never count as cases):

    upstream_total == total + len(excluded) + len(deferred)

Rewritten and expectation-overridden cases are still imported and therefore
still contribute to ``total``. Only excluded and deferred subtract from the
upstream runnable population. Static grep hit counts are candidate sets only;
they are never the population total.

Importers (parser today; analyzer later via #45) own their disposition tables
and emit ledger records through ``empty_triage`` / ``barista_path``. Validators
read the same shape from ``tests/corpus_baseline.json``.
"""

from __future__ import annotations

from typing import Any, Iterable, Mapping


DISPOSITION_KEYS = (
    "excluded",
    "rewritten",
    "expectation_overrides",
    "deferred",
)

# Disposition keys that remove a case from the imported tree.
NON_IMPORT_DISPOSITIONS = ("excluded", "deferred")

# Disposition keys that still import the case (with edits).
IMPORT_DISPOSITIONS = ("rewritten", "expectation_overrides")

REQUIRED_IMPORTED_FIELDS = (
    "root",
    "total",
    "skipped",
    "expected_failures",
    "foundry_revision",
    "upstream_total",
    "triage",
)

REQUIRED_PENDING_FIELDS = (
    "root",
    "foundry_revision",
    "upstream_total",
    "upstream_helpers",
    "triage",
    "imported",
)


def empty_triage() -> dict[str, dict[str, str]]:
    """A complete triage object with every disposition key present and empty."""
    return {key: {} for key in DISPOSITION_KEYS}


def barista_path(upstream_fs_path: str) -> str:
    """Map an upstream ``.fs`` / ``.notest.fs`` path to the imported ``.barista`` path."""
    if upstream_fs_path.endswith(".notest.fs"):
        return upstream_fs_path[: -len(".notest.fs")] + ".notest.barista"
    if upstream_fs_path.endswith(".fs"):
        return upstream_fs_path[: -len(".fs")] + ".barista"
    return upstream_fs_path


def normalize_relative_path(path: str) -> str | None:
    """Return a posix relative path, or None when ``path`` is not a safe ledger key."""
    if not isinstance(path, str) or not path:
        return None
    if path.startswith("/") or path.startswith("\\"):
        return None
    if "\\" in path:
        return None
    parts = path.split("/")
    if any(part in ("", ".", "..") for part in parts):
        return None
    return path


def _as_reason_map(value: Any, corpus: str, field: str) -> tuple[dict[str, str], str | None]:
    if not isinstance(value, dict):
        return {}, f"corpus {corpus!r} triage.{field} must be an object of path -> reason"
    reasons: dict[str, str] = {}
    for path, reason in value.items():
        normalized = normalize_relative_path(path)
        if normalized is None:
            return {}, (
                f"corpus {corpus!r} triage.{field} path {path!r} is not a normalized "
                "relative posix path"
            )
        if not isinstance(reason, str) or not reason.strip():
            return {}, (
                f"corpus {corpus!r} triage.{field} path {normalized!r} has an empty reason"
            )
        reasons[normalized] = reason
    return reasons, None


def validate_triage_ledger(
    corpus: str,
    entry: Mapping[str, Any],
    *,
    disk_cases: set[str] | None = None,
    disk_helpers: set[str] | None = None,
) -> str | None:
    """Validate one corpus ledger entry.

    When ``disk_cases`` is provided (imported corpora), also check that
    excluded/deferred paths are absent and rewritten/override paths are present.
    Pending (not-yet-imported) corpora validate schema and reasons only.
    """
    imported = bool(entry.get("imported", True))
    required = REQUIRED_IMPORTED_FIELDS if imported else REQUIRED_PENDING_FIELDS
    for field in required:
        if field not in entry:
            return f"corpus {corpus!r} is missing required field {field!r}"

    revision = entry.get("foundry_revision")
    if not isinstance(revision, str) or not revision.strip():
        return f"corpus {corpus!r} foundry_revision must be a non-empty string"

    upstream_total = entry.get("upstream_total")
    if not isinstance(upstream_total, int) or upstream_total < 0:
        return f"corpus {corpus!r} upstream_total must be a non-negative integer"

    triage = entry.get("triage")
    if not isinstance(triage, dict):
        return f"corpus {corpus!r} triage must be an object"

    missing_keys = [key for key in DISPOSITION_KEYS if key not in triage]
    if missing_keys:
        return (
            f"corpus {corpus!r} triage is missing disposition key(s): "
            + ", ".join(missing_keys)
        )

    unknown_keys = sorted(set(triage) - set(DISPOSITION_KEYS))
    if unknown_keys:
        return (
            f"corpus {corpus!r} triage has unknown disposition key(s): "
            + ", ".join(unknown_keys)
        )

    disposition_maps: dict[str, dict[str, str]] = {}
    for key in DISPOSITION_KEYS:
        reasons, error = _as_reason_map(triage[key], corpus, key)
        if error is not None:
            return error
        disposition_maps[key] = reasons

    seen: dict[str, str] = {}
    for key, reasons in disposition_maps.items():
        for path in reasons:
            if path in seen:
                return (
                    f"corpus {corpus!r} path {path!r} has overlapping dispositions "
                    f"{seen[path]!r} and {key!r}"
                )
            seen[path] = key

    if not imported:
        helpers = entry.get("upstream_helpers")
        if not isinstance(helpers, int) or helpers < 0:
            return f"corpus {corpus!r} upstream_helpers must be a non-negative integer"
        # Pending corpora must not claim imported totals or expected failures.
        if entry.get("total") not in (None, 0):
            return (
                f"corpus {corpus!r} is not imported; total must be 0 or omitted until "
                "execution-driven triage (#45) fills it"
            )
        if entry.get("skipped") not in (None, 0):
            return (
                f"corpus {corpus!r} is not imported; skipped must be 0 or omitted until "
                "execution-driven triage fills it"
            )
        if entry.get("expected_failures"):
            return (
                f"corpus {corpus!r} is not imported; expected_failures must stay empty "
                "until cases are imported with explicit dispositions"
            )
        return None

    total = entry.get("total")
    skipped = entry.get("skipped")
    if not isinstance(total, int) or total < 0:
        return f"corpus {corpus!r} total must be a non-negative integer"
    if not isinstance(skipped, int) or skipped < 0:
        return f"corpus {corpus!r} skipped must be a non-negative integer"

    excluded = disposition_maps["excluded"]
    deferred = disposition_maps["deferred"]
    accounted = total + len(excluded) + len(deferred)
    if accounted != upstream_total:
        return (
            f"corpus {corpus!r} population mismatch: upstream_total={upstream_total} but "
            f"total ({total}) + excluded ({len(excluded)}) + deferred ({len(deferred)}) "
            f"= {accounted}"
        )

    expected_failures = entry.get("expected_failures")
    if not isinstance(expected_failures, list):
        return f"corpus {corpus!r} expected_failures must be a list"

    if disk_cases is not None:
        helper_set = disk_helpers or set()
        for key in NON_IMPORT_DISPOSITIONS:
            for path in disposition_maps[key]:
                if path in disk_cases:
                    return (
                        f"corpus {corpus!r} triage.{key} path {path!r} is still imported; "
                        "excluded/deferred cases must be absent from the tree"
                    )
                if path in helper_set:
                    return (
                        f"corpus {corpus!r} triage.{key} path {path!r} names a helper; "
                        "helpers are never cases and must not carry dispositions"
                    )
        for key in IMPORT_DISPOSITIONS:
            for path in disposition_maps[key]:
                if path not in disk_cases:
                    return (
                        f"corpus {corpus!r} triage.{key} path {path!r} is not an imported "
                        "case"
                    )
                if path in helper_set:
                    return (
                        f"corpus {corpus!r} triage.{key} path {path!r} names a helper; "
                        "helpers are never cases and must not carry dispositions"
                    )
        for path in expected_failures:
            normalized = normalize_relative_path(path) if isinstance(path, str) else None
            if normalized is None or normalized not in disk_cases:
                return (
                    f"corpus {corpus!r} records expected failures that are not cases: "
                    + str(path)
                )
            if normalized in excluded or normalized in deferred:
                return (
                    f"corpus {corpus!r} expected_failures path {normalized!r} overlaps an "
                    "excluded/deferred disposition; unexplained residuals must not be "
                    "silently absorbed"
                )

    return None


def build_triage_from_maps(
    *,
    excluded: Mapping[str, str] | None = None,
    rewritten: Mapping[str, str] | None = None,
    expectation_overrides: Mapping[str, str] | None = None,
    deferred: Mapping[str, str] | None = None,
) -> dict[str, dict[str, str]]:
    """Assemble a triage object from path->reason maps keyed by imported paths."""
    triage = empty_triage()
    triage["excluded"] = dict(sorted((excluded or {}).items()))
    triage["rewritten"] = dict(sorted((rewritten or {}).items()))
    triage["expectation_overrides"] = dict(sorted((expectation_overrides or {}).items()))
    triage["deferred"] = dict(sorted((deferred or {}).items()))
    return triage


def iter_disposition_paths(triage: Mapping[str, Mapping[str, str]]) -> Iterable[tuple[str, str]]:
    """Yield ``(disposition, path)`` for every ledger entry."""
    for key in DISPOSITION_KEYS:
        for path in triage.get(key, {}):
            yield key, path
