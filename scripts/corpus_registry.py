#!/usr/bin/env python3
# corpus_registry.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Validate the immutable corpus source registry and its delivery evidence.

No upstream code is imported. Registry validation and GitHub output preparation
are entirely local; checkout validation invokes only Git with argument lists.
"""

from __future__ import annotations

import hashlib
import json
import os
import stat
from pathlib import Path, PurePosixPath
import re
import subprocess

from corpus_ledger import validate_triage_ledger
from corpus_stages import validate_stages
from corpus_expectations import decode_expectation

ROOT = Path(__file__).resolve().parents[1]
TRUSTED_REPOSITORY = "cafecito-games/Foundry"
# Extending the command surface requires a reviewed local code change. Pending
# analyzer work may stage this file without claiming an imported corpus.
ALLOWED_IMPORTERS = {
    "parser": "scripts/import_parser_corpus.py",
    "analyzer": "scripts/import_analyzer_corpus.py",
}


def read_json(path: Path) -> dict:
    def unique_pairs(pairs):
        document = {}
        for key, value in pairs:
            if key in document:
                raise ValueError(f"{path}: duplicate JSON key {key!r}")
            document[key] = value
        return document

    try:
        document = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_pairs)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"{path}: {error}") from error
    if not isinstance(document, dict):
        raise ValueError(f"{path}: expected a JSON object")
    return document


def normalized_path(value: object, prefix: str) -> str:
    if (not isinstance(value, str) or not value.startswith(prefix)
            or not re.fullmatch(r"[A-Za-z0-9_./-]+", value)
            or str(PurePosixPath(value)) != value
            or any(part in (".", "..") for part in value.split("/"))):
        raise ValueError(f"invalid normalized path beneath {prefix}: {value!r}")
    return value


def local_path(root: Path, relative: str) -> Path:
    path = root / relative
    # Reject symlink components even if they resolve back inside the checkout.
    # A registry path must identify the reviewed file/tree itself.
    if any((root / Path(*Path(relative).parts[:index])).is_symlink()
           for index in range(1, len(Path(relative).parts) + 1)):
        raise ValueError(f"registry path must not traverse a symlink: {relative}")
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"registry path escapes checkout: {relative}")
    return path


def tree_entries(root: Path) -> dict[str, str]:
    """Inventory regular files/directories without following any symlink.

    Path.is_file() erases dangling links and follows live links outside the
    corpus. Reject every unsupported type before consumers read file bytes.
    """
    if not stat.S_ISDIR(root.lstat().st_mode):
        raise ValueError(f"corpus tree is not a regular directory: {root}")
    entries = {}

    def visit(directory: Path) -> None:
        with os.scandir(directory) as children:
            ordered = sorted(children, key=lambda child: child.name)
        for child in ordered:
            path = directory / child.name
            relative = path.relative_to(root).as_posix()
            mode = child.stat(follow_symlinks=False).st_mode
            if stat.S_ISDIR(mode):
                entries[relative] = "directory"
                visit(path)
            elif stat.S_ISREG(mode):
                entries[relative] = "file"
            else:
                kind = "symlink" if stat.S_ISLNK(mode) else "special file"
                raise ValueError(f"unsupported {kind} in corpus tree: {path}")

    visit(root)
    return entries


def load_registry(root: Path = ROOT) -> dict:
    registry = read_json(root / "scripts/corpus_sources.json")
    if type(registry.get("schema_version")) is not int or registry["schema_version"] != 1:
        raise ValueError("corpus registry requires schema_version 1")
    if registry.get("repository") != TRUSTED_REPOSITORY:
        raise ValueError(f"corpus registry repository must be {TRUSTED_REPOSITORY}")
    if not isinstance(registry.get("revision"), str) or not re.fullmatch(r"[0-9a-f]{40}", registry["revision"]):
        raise ValueError("corpus registry revision must be one full lowercase 40-character SHA")
    corpora = registry.get("corpora")
    if not isinstance(corpora, dict) or not corpora:
        raise ValueError("corpus registry must contain nonempty corpora")
    if set(corpora) != set(ALLOWED_IMPORTERS):
        raise ValueError("corpus registry must retain parser and analyzer registrations")
    seen = {key: set() for key in ("source", "destination", "importer")}
    for name, record in sorted(corpora.items()):
        if name not in ALLOWED_IMPORTERS or not isinstance(record, dict):
            raise ValueError(f"unregistered corpus identity {name!r}")
        if record.get("state") not in ("active", "pending"):
            raise ValueError(f"corpus {name!r}: state must be active or pending")
        normalized_path(record.get("source"), "modules/foundry_script/tests/scripts/")
        normalized_path(record.get("destination"), "project/tests/corpus/")
        local_path(root, record["destination"])
        importer = record.get("importer")
        if importer is not None:
            if importer != ALLOWED_IMPORTERS[name]:
                raise ValueError(f"corpus {name!r}: importer {importer!r} is not allowlisted")
            if not local_path(root, importer).is_file():
                raise ValueError(f"corpus {name!r}: importer {importer!r} is missing")
        elif record["state"] == "active":
            raise ValueError(f"active corpus {name!r} requires an importer")
        for key in seen:
            value = record.get(key)
            if value is not None:
                if value in seen[key]:
                    raise ValueError(f"duplicate corpus {key}: {value}")
                seen[key].add(value)
    if not any(record["state"] == "active" for record in corpora.values()):
        raise ValueError("corpus registry has zero active corpora")
    auxiliary = registry.get("auxiliary_sources", [])
    if not isinstance(auxiliary, list) or not all(isinstance(path, str) for path in auxiliary) or auxiliary != sorted(set(auxiliary)):
        raise ValueError("auxiliary sources must be sorted unique paths")
    for path in auxiliary:
        normalized_path(path, "modules/foundry_script/tests/scripts/")
        if not path.endswith(".notest.fs") or any(path == record["source"] or path.startswith(record["source"] + "/") for record in corpora.values()):
            raise ValueError(f"invalid auxiliary helper source: {path}")
    analyzer = registry.get("analyzer_sources", [])
    if (not isinstance(analyzer, list) or not all(isinstance(path, str) for path in analyzer)
            or analyzer != sorted(set(analyzer))):
        raise ValueError("analyzer sources must be sorted unique paths")
    for path in analyzer:
        normalized_path(path, "modules/foundry_script/")
        if not path.endswith((".cpp", ".h")) or "/" in path[len("modules/foundry_script/"):]:
            raise ValueError(f"invalid analyzer source: {path}")
    return registry


def source_paths(registry: dict) -> list[str]:
    return sorted([record["source"] for record in registry["corpora"].values()] + registry.get("auxiliary_sources", []) + registry.get("analyzer_sources", []))


def sparse_patterns(registry: dict) -> list[str]:
    directories = {record["source"] for record in registry["corpora"].values()}
    return ["/" + path + ("/" if path in directories else "") for path in source_paths(registry)]


def validate_registration(root: Path = ROOT, *, baseline_path: Path | None = None,
                          suites_path: Path | None = None, rebuilding: str | None = None) -> dict:
    registry = load_registry(root)
    if rebuilding is not None and (rebuilding not in registry["corpora"]
                                   or registry["corpora"][rebuilding]["state"] != "active"):
        raise ValueError("only a registered active importer may rebuild its destination")
    baseline = read_json(baseline_path or root / "tests/corpus_baseline.json")
    suites = read_json(suites_path or root / "tests/gdscript_suites.json")
    corpora = baseline.get("corpora")
    if not isinstance(corpora, dict) or not corpora or set(corpora) != set(registry["corpora"]):
        raise ValueError("baseline corpus registration must exactly match the nonempty registry")
    invocations = suites.get("extra_invocations")
    overrides = suites.get("overrides", {})
    if not isinstance(invocations, list) or not isinstance(overrides, dict):
        raise ValueError("suite manifest requires extra_invocations and overrides")
    all_invocations = invocations + list(overrides.values())
    for invocation in all_invocations:
        if (not isinstance(invocation, dict) or not isinstance(invocation.get("args", []), list)
                or not all(isinstance(arg, str) for arg in invocation.get("args", []))
                or not isinstance(invocation.get("script", ""), str)
                or not isinstance(invocation.get("expect", ""), str)):
            raise ValueError("malformed corpus suite invocation")
    match = re.search(r'SUMMARY_PREFIX\s*=\s*"([^"]+)"',
                      (root / "src/bs_corpus_sentinels.h").read_text(encoding="utf-8"))
    if match is None:
        raise ValueError("could not read corpus SUMMARY_PREFIX")
    for name, record in sorted(registry["corpora"].items()):
        corpus = corpora[name]
        if not isinstance(corpus, dict):
            raise ValueError(f"corpus {name!r}: baseline must be an object")
        if "analyzer_deferred" in corpus:
            raise ValueError("obsolete analyzer_deferred field is forbidden")
        imported = corpus.get("imported", True)  # compatibility with the initial parser ledger
        if type(imported) is not bool or imported != (record["state"] == "active"):
            raise ValueError(f"corpus {name!r}: imported boolean must agree with registry state")
        root_uri = "res://" + record["destination"][len("project/"):]
        if corpus.get("root") != root_uri or corpus.get("foundry_revision") != registry["revision"]:
            raise ValueError(f"corpus {name!r}: root/revision provenance differs from registry")
        for field in ("total", "skipped", "upstream_total", "upstream_helpers", "upstream_sources"):
            if field in ("upstream_helpers", "upstream_sources") and field not in corpus:
                continue
            if type(corpus.get(field)) is not int or corpus[field] < 0:
                raise ValueError(f"corpus {name!r}: {field} must be a nonnegative integer")
        failures = corpus.get("expected_failures")
        if (not isinstance(failures, list) or not all(isinstance(item, str) for item in failures)
                or len(set(failures)) != len(failures)):
            raise ValueError(f"corpus {name!r}: expected_failures must be unique paths")
        destination = local_path(root, record["destination"])
        mentions = [invocation for invocation in all_invocations
                    if root_uri in invocation.get("args", []) or root_uri == invocation.get("script")]
        if not imported:
            if (destination.exists() or corpus["total"] or corpus["skipped"] or failures or mentions):
                raise ValueError(f"pending corpus {name!r}: generated tree, nonzero totals or claimed invocation")
            complaint = validate_triage_ledger(name, corpus)
            if complaint:
                raise ValueError(complaint)
            continue
        # Explicit non-check regeneration may replace its own missing/drifted
        # destination. Registry identity, ledger schema, runner pin and all other
        # corpora remain validated; wrapper/output mode never uses this option.
        if rebuilding == name:
            complaint = validate_triage_ledger(name, corpus)
            if complaint:
                raise ValueError(complaint)
        else:
            if not destination.is_dir():
                raise ValueError(f"imported corpus {name!r}: generated tree is missing or empty: {destination}")
            entries = tree_entries(destination)
            if "file" not in entries.values():
                raise ValueError(f"imported corpus {name!r}: generated tree is empty: {destination}")
            sources = {path for path, kind in entries.items() if kind == "file" and path.endswith(".barista")}
            helpers = {path for path in sources if path.endswith(".notest.barista")}
            cases = sources - helpers
            if len(cases) != corpus["total"] or len(helpers) != corpus["skipped"]:
                raise ValueError(f"corpus {name!r} holds {len(cases)} cases and {len(helpers)} skipped helpers, "
                                 f"but baseline records {corpus['total']} and {corpus['skipped']}")
            complaint = validate_triage_ledger(name, corpus, disk_cases=cases, disk_helpers=helpers)
            if complaint:
                raise ValueError(complaint)
            validate_stages(read_json(destination / "case_stages.json"), cases, helpers, registry["revision"])
            for case in cases:
                decode_expectation((destination / case).with_suffix(".out").read_bytes(), case)
            unknown = sorted(set(failures) - cases)
            if unknown:
                raise ValueError(f"corpus {name!r} records expected failures that are not cases: {', '.join(unknown)}")
        required = (f"^{re.escape(match.group(1))} {corpus['total'] - len(failures)}/{corpus['total']} "
                    f"skipped={corpus['skipped']}$")
        pins = [entry for entry in invocations if entry.get("script") == "res://tests/corpus_runner.gd"
                and entry.get("args") == ["--corpus", root_uri]]
        if len(pins) != 1:
            raise ValueError(f"corpus {name!r} requires exactly one explicit runner invocation; an unrun corpus is not a baseline")
        if pins[0].get("expect") != required:
            raise ValueError(f"corpus {name!r} baseline requires {required!r}; the pin may not be a loose match")
    return registry


def run_git(repository: Path, *arguments: str) -> str:
    completed = subprocess.run(["git", "--no-optional-locks", "-C", str(repository), *arguments],
                               capture_output=True, text=True, check=False)
    if completed.returncode:
        raise ValueError(f"git {' '.join(arguments)} failed in {repository}: {completed.stderr.strip()}")
    return completed.stdout.strip()


def verify_checkout(foundry: Path, registry: dict, requested: str) -> None:
    revision = registry["revision"]
    if requested != revision:
        raise ValueError(f"requested revision {requested} is not the pinned revision {revision}")
    origin = run_git(foundry, "remote", "get-url", "origin")
    repository = registry["repository"]
    allowed = {f"https://github.com/{repository}", f"https://github.com/{repository}.git",
               f"git@github.com:{repository}.git", f"ssh://git@github.com/{repository}.git"}
    if origin not in allowed:
        raise ValueError(f"{foundry}: origin {origin!r} is not trusted repository {repository}")
    head = run_git(foundry, "rev-parse", "HEAD")
    if head != revision:
        raise ValueError(f"{foundry}: HEAD {head} is not the pinned revision {revision}")
    inputs = [(name, record, False) for name, record in sorted(registry["corpora"].items())]
    inputs += [("support_helper", {"source": path}, True) for path in registry.get("auxiliary_sources", [])]
    inputs += [("analyzer_source", {"source": path}, True) for path in registry.get("analyzer_sources", [])]
    for name, record, single_file in inputs:
        source = local_path(foundry, record["source"])
        if single_file:
            if not source.is_file():
                raise ValueError(f"required auxiliary source missing: {source}")
            dirty = run_git(foundry, "diff-index", "--cached", "--name-status", "--no-renames", revision, "--", record["source"])
            if dirty:
                raise ValueError(f"{source}: uncommitted changes at pinned revision {revision}:\n{dirty}")
            listing = run_git(foundry, "ls-tree", revision, "--", record["source"])
            if not listing:
                raise ValueError(f"auxiliary source absent from pin: {source}")
            metadata, relative = listing.split("\t", 1)
            mode, kind, object_id = metadata.split()
            contents = source.read_bytes()
            actual_id = hashlib.sha1(f"blob {len(contents)}\0".encode("ascii") + contents).hexdigest()
            if mode not in ("100644", "100755") or kind != "blob" or relative != record["source"] or actual_id != object_id:
                raise ValueError(f"auxiliary source bytes/type differ from pinned revision {revision}: {source}")
            if bool(source.stat().st_mode & stat.S_IXUSR) != (mode == "100755"):
                raise ValueError(f"auxiliary source executable mode differs from pinned revision: {source}")
            continue
        if not source.is_dir():
            raise ValueError(f"corpus {name!r}: required sparse source root missing: {source}")
        entries = tree_entries(source)
        dirty = run_git(foundry, "diff-index", "--cached", "--name-status", "--no-renames", revision, "--", record["source"])
        if dirty:
            raise ValueError(f"{source}: uncommitted changes at pinned revision {revision}:\n{dirty}")

        # Cached index comparison requires no ignore/attribute blobs outside
        # the registered sparse inputs. Complete filesystem enumeration below
        # catches ignored/untracked files too, without git status's lazy fetch.
        # Git status trusts index hints such as assume-unchanged and
        # skip-worktree. Compare all consumed bytes to immutable blob IDs,
        # independent of those hints, without refreshing/changing the index.
        pinned = {}
        listing = run_git(foundry, "ls-tree", "-rz", revision, "--", record["source"])
        for item in listing.split("\0"):
            if not item:
                continue
            metadata, relative = item.split("\t", 1)
            mode, kind, object_id = metadata.split()
            if mode not in ("100644", "100755") or kind != "blob":
                raise ValueError(f"unsupported pinned source entry type {mode}: {relative}")
            if not relative.startswith(record["source"] + "/"):
                raise ValueError(f"pinned source path escapes its registered root: {relative}")
            pinned[relative[len(record["source"]) + 1:]] = (object_id, mode)
        actual_files = {path for path, kind in entries.items() if kind == "file"}
        for missing in sorted(pinned.keys() - actual_files):
            raise ValueError(f"pinned source file missing: {source / missing}")
        for extra in sorted(actual_files - pinned.keys()):
            raise ValueError(f"source file absent from pinned revision: {source / extra}")
        for relative, (object_id, mode) in sorted(pinned.items()):
            if bool((source / relative).stat().st_mode & stat.S_IXUSR) != (mode == "100755"):
                raise ValueError(f"source executable mode differs from pinned revision: {source / relative}")
            contents = (source / relative).read_bytes()
            # The registry's full 40-character pin selects Git's SHA-1 object
            # format. Hash raw bytes, with no attribute filters or upstream code.
            actual_id = hashlib.sha1(f"blob {len(contents)}\0".encode("ascii") + contents).hexdigest()
            if actual_id != object_id:
                raise ValueError(f"uncommitted source bytes differ from pinned revision {revision}: {source / relative}")
