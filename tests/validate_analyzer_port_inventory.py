#!/usr/bin/env python3
# validate_analyzer_port_inventory.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Verify the reviewed finite analyzer ledger, not automatic semantic completeness.

Signatures are explicit reviewed snippets, never discovered by parsing C++. Historical
marker identities are immutable; current markers and local evidence are checked against
tracked bytes. Candidate evidence is provisional and is not a merge/ancestry proof.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = "c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6"
INTEGRATION = "ffdecf1e7e996bc6b59a7d58bc58b8b138a48795"
FILES = tuple(
    "modules/foundry_script/" + name
    for name in (
        "fs_analyzer.cpp",
        "fs_analyzer_call_validation.cpp",
        "fs_analyzer_conformance.cpp",
        "fs_analyzer_finalization.cpp",
        "fs_analyzer_flow_finality.cpp",
        "fs_analyzer_surface.cpp",
        "fs_analyzer.h",
        "fs_builtin_types.cpp",
        "fs_builtin_types.h",
        "fs_builtin_sources.cpp",
        "fs_builtin_sources.h",
        "fs_conformance_registry.cpp",
        "fs_conformance_registry.h",
        "fs_trait_utils.cpp",
        "fs_trait_utils.h",
        "fs_type.cpp",
        "fs_type.h",
        "fs_cache.cpp",
        "fs_cache.h",
    )
)
MARKER = re.compile(
    r"\bresiduals?\b|follow[- ]up|\bTODO\b|not available until M[4-7]|not ported|non-ports",
    re.I,
)
HISTORICAL_DIGEST = "ba10a3ee79ae539f684cd7b390e4c7f411902ed6c549cdc18538bfd7373d8bcf"
STATES = {"pending", "implemented", "equivalent", "deleted", "downstream"}
ROWS = (
    {f"R{i:02}" for i in range(1, 31)}
    | {f"S{i}" for i in range(1, 8)}
    | {f"X{i}" for i in range(1, 7)}
)


def normalized(text: str) -> str:
    return " ".join(text.split())


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def read_json(path: Path) -> dict:
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, f"duplicate JSON key: {key}")
            result[key] = value
        return result

    value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=pairs)
    require(type(value) is dict, "inventory must be an object")
    return value


def local_file(root: Path, path: str) -> Path:
    require(
        isinstance(path, str)
        and path
        and not PurePosixPath(path).is_absolute()
        and str(PurePosixPath(path)) == path
        and ".." not in PurePosixPath(path).parts,
        f"invalid local path: {path}",
    )
    result = root / path
    require(
        result.resolve().is_relative_to(root.resolve()) and result.is_file(),
        f"missing local source: {path}",
    )
    return result


def reference(root: Path, entry: dict) -> None:
    require(
        type(entry) is dict and set(entry) == {"path", "symbol"},
        "local reference requires path and symbol",
    )
    text = local_file(root, entry["path"]).read_text(encoding="utf-8")
    require(
        isinstance(entry["symbol"], str)
        and entry["symbol"]
        and entry["symbol"] in text,
        f"missing local symbol/test: {entry}",
    )


def scan_markers(root: Path) -> list[dict]:
    paths = (
        subprocess.check_output(["git", "ls-files", "-z"], cwd=root)
        .decode()
        .split("\0")
    )
    result = []
    for path in sorted(paths):
        if not path.endswith((".h", ".cpp")) or path.startswith(
            ("godot-cpp/", "thirdparty/", "build/", "src/gen/")
        ):
            continue
        ordinal = 0
        for line in local_file(root, path).read_text(encoding="utf-8").splitlines():
            if MARKER.search(line):
                ordinal += 1
                text = normalized(line)
                result.append(
                    {
                        "path": path,
                        "ordinal": ordinal,
                        "text": text,
                        "sha256": digest(text.encode()),
                    }
                )
    return result


def historical_markers(root: Path) -> list[dict]:
    listing = subprocess.run(
        ["git", "ls-tree", "-r", "--name-only", INTEGRATION],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    require(
        listing.returncode == 0,
        "missing integration history: inventory CI checkout requires fetch-depth 0",
    )
    result = []
    for path in listing.stdout.decode().splitlines():
        if not path.endswith((".h", ".cpp")) or path.startswith(
            ("godot-cpp/", "thirdparty/", "build/", "src/gen/")
        ):
            continue
        source = subprocess.check_output(
            ["git", "show", f"{INTEGRATION}:{path}"], cwd=root
        ).decode()
        ordinal = 0
        for line in source.splitlines():
            if MARKER.search(line):
                ordinal += 1
                text = normalized(line)
                result.append(
                    {
                        "path": path,
                        "ordinal": ordinal,
                        "text": text,
                        "sha256": digest(text.encode()),
                    }
                )
    return result


def marker_identity(marker: dict) -> dict:
    return {key: marker[key] for key in ("path", "ordinal", "text", "sha256")}


def validate(
    data: dict,
    root: Path = ROOT,
    foundry_dir: Path | None = None,
    complete: bool = False,
) -> dict:
    require(
        set(data)
        == {
            "schema_version",
            "upstream_revision",
            "integration_revision",
            "upstream_files",
            "behavior_families",
            "markers",
        },
        "unexpected root schema",
    )
    require(
        type(data["schema_version"]) is int and data["schema_version"] == 1,
        "schema_version must be integer 1",
    )
    require(
        data["upstream_revision"] == UPSTREAM
        and data["integration_revision"] == INTEGRATION,
        "stale pin/integration revision",
    )
    registry = read_json(local_file(root, "scripts/corpus_sources.json"))
    platform = read_json(local_file(root, "src/bs_platform_manifest.json"))
    require(
        registry.get("revision") == UPSTREAM
        and platform.get("upstream", {}).get("revision") == UPSTREAM
        and platform["upstream"].get("repository") == "cafecito-games/Foundry"
        and platform["upstream"].get("module_root") == "modules/foundry_script/",
        "platform/corpus pin mismatch",
    )
    require(
        registry.get("analyzer_sources") == sorted(FILES),
        "source registry must explicitly include all 19 analyzer files",
    )
    files = data["upstream_files"]
    require(
        type(files) is list
        and {f["path"] for f in files} == set(FILES)
        and len(files) == len(FILES),
        "missing or duplicate upstream source file",
    )
    functions = {}
    identities = set()
    for file in files:
        require(
            re.fullmatch(r"[0-9a-f]{64}", file.get("sha256", "")) is not None,
            "invalid upstream hash",
        )
        require(
            type(file.get("functions")) is list and file["functions"],
            "missing explicit reviewed signatures",
        )
        source = None
        if foundry_dir is not None:
            env = dict(
                os.environ,
                GIT_CONFIG_GLOBAL="/dev/null",
                GIT_CONFIG_NOSYSTEM="1",
                GIT_ALLOW_PROTOCOL="",
                GIT_NO_LAZY_FETCH="1",
                GIT_OPTIONAL_LOCKS="0",
            )
            command = [
                "git",
                "-C",
                str(foundry_dir),
                "show",
                f"{UPSTREAM}:{file['path']}",
            ]
            result = subprocess.run(
                command,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            require(result.returncode == 0, f"missing pin/source: {file['path']}")
            require(
                digest(result.stdout) == file["sha256"],
                f"stale upstream hash: {file['path']}",
            )
            source = normalized(result.stdout.decode())
        for function in file["functions"]:
            name = function["id"]
            require(name not in functions, f"duplicate function ID: {name}")
            snippet = function["signature"]
            require(
                isinstance(snippet, str) and snippet == normalized(snippet) and snippet,
                "signature must be normalized",
            )
            require(
                digest(snippet.encode()) == function["sha256"],
                f"stale signature hash: {name}",
            )
            require(
                type(function["ordinal"]) is int
                and type(function["occurrences"]) is int
                and 1 <= function["ordinal"] <= function["occurrences"],
                "invalid signature ordinal/count",
            )
            require(
                function["qualified_name"]
                and function["qualified_name"].split("::")[-1] in snippet,
                "signature lacks function name",
            )
            identity = (
                file["path"],
                function["qualified_name"],
                snippet,
                function["ordinal"],
            )
            require(identity not in identities, f"duplicate signature identity: {name}")
            identities.add(identity)
            functions[name] = function
            if source is not None:
                require(
                    source.count(snippet) == function["occurrences"],
                    f"signature occurrence mismatch: {name}",
                )
    families = {}
    rows = set()
    referenced_functions = set()
    merged_commits = set()
    for family in data["behavior_families"]:
        name = family["id"]
        require(name not in families, f"duplicate family ID: {name}")
        families[name] = family
        require(
            family["rows"] and set(family["rows"]) <= ROWS,
            f"invalid family rows: {name}",
        )
        rows.update(family["rows"])
        keys = family.get("branch_keys")
        require(
            type(keys) is list
            and keys
            and all(isinstance(key, str) and key.strip() == key and key for key in keys)
            and len(keys) == len(set(keys)),
            f"missing or duplicate branch keys: {name}",
        )
        state = family["disposition"]
        require(state in STATES, f"unknown disposition: {state}")
        require(
            family.get("boundary") and family.get("rationale") and family.get("owner"),
            f"missing boundary/owner/rationale: {name}",
        )
        require(family["owner"] != "#37", "unrelated owner #37")
        require(
            (family.get("upstream") or set(family["rows"]) <= {"R29", "S7"})
            and set(family["upstream"]) <= set(functions),
            f"unknown upstream function: {name}",
        )
        referenced_functions.update(family["upstream"])
        for key in ("symbols", "tests"):
            for ref in family.get(key, []):
                reference(root, ref)
        if state in ("implemented", "equivalent"):
            require(
                family.get("symbols") and family.get("tests"),
                f"missing code/test evidence: {name}",
            )
            evidence = family.get("evidence", {})
            if evidence.get("kind") == "merged":
                require(
                    re.fullmatch(r"[0-9a-f]{40}", evidence.get("commit", ""))
                    is not None
                    and re.fullmatch(
                        r"https://github.com/cafecito-games/BaristaScript/pull/[1-9][0-9]*",
                        evidence.get("pr", ""),
                    )
                    is not None,
                    f"invalid merged evidence: {name}",
                )
                merged_commits.add(evidence["commit"])
            elif evidence.get("kind") == "candidate":
                locator = evidence.get("pr", {})
                require(
                    locator
                    == {
                        "kind": "provisional",
                        "repository": "cafecito-games/BaristaScript",
                        "head_ref": "m3/issue-141-restoration-warnings",
                        "issue": 141,
                    },
                    f"invalid candidate PR locator: {name}",
                )
                require(
                    evidence.get("blobs") and evidence.get("results"),
                    f"missing candidate blobs/results: {name}",
                )
                blob_paths = set()
                for blob in evidence["blobs"]:
                    require(
                        blob["path"] != "tests/analyzer_port_inventory.json",
                        "circular candidate blob",
                    )
                    require(blob["path"] not in blob_paths, "duplicate candidate blob")
                    blob_paths.add(blob["path"])
                    require(
                        digest(local_file(root, blob["path"]).read_bytes())
                        == blob["sha256"],
                        f"stale candidate blob: {blob['path']}",
                    )
                require(
                    {r["path"] for r in family["symbols"] + family["tests"]}
                    <= blob_paths,
                    f"candidate evidence must bind its code/tests: {name}",
                )
                for result in evidence["results"]:
                    require(
                        type(result.get("exit")) is int
                        and result["exit"] == 0
                        and type(result.get("command")) is list
                        and result["command"]
                        and result.get("result")
                        and re.fullmatch(r"[0-9a-f]{64}", result.get("log_sha256", ""))
                        is not None,
                        f"invalid candidate result: {name}",
                    )
            else:
                raise ValueError(f"missing implementation evidence: {name}")
        elif state == "deleted":
            require(
                family.get("decision") in ("D1", "D7"),
                f"missing deletion decision: {name}",
            )
        elif state == "downstream":
            require(
                family.get("milestone")
                in (
                    "M4",
                    "M5",
                    "M6",
                    "M7",
                    "parser-maintenance",
                    "stock-engine-adaptation",
                ),
                f"missing downstream milestone/boundary: {name}",
            )
        elif complete:
            raise ValueError(f"pending M3 family: {name}")
    require(rows == ROWS, "unclassified R/S/X row")
    require(
        referenced_functions == set(functions), "unclassified listed upstream signature"
    )
    for commit in merged_commits:
        check = subprocess.run(
            ["git", "merge-base", "--is-ancestor", commit, "HEAD"],
            cwd=root,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        require(check.returncode == 0, f"missing merged ancestor/history: {commit}")
    markers = data["markers"]
    ids = set()
    history = []
    current = []
    for marker in markers:
        require(marker["id"] not in ids, "duplicate marker ID")
        ids.add(marker["id"])
        require(
            marker.get("families") and set(marker["families"]) <= set(families),
            "unclassified marker family",
        )
        text = marker["text"]
        require(
            text == normalized(text)
            and MARKER.search(text)
            and digest(text.encode()) == marker["sha256"],
            "invalid marker fingerprint",
        )
        require(
            type(marker["ordinal"]) is int and marker["ordinal"] > 0,
            "invalid marker ordinal",
        )
        require(marker["origin"] in ("integration", "current"), "unknown marker origin")
        if marker["origin"] == "integration":
            history.append(marker_identity(marker))
            require(
                marker.get("replacement", {}).get("symbols")
                and marker["replacement"].get("tests"),
                "historical marker needs replacement code/test evidence",
            )
            for key in ("symbols", "tests"):
                for ref in marker["replacement"][key]:
                    reference(root, ref)
        else:
            current.append(marker_identity(marker))
    require(
        len(history) == 58
        and digest(json.dumps(history, sort_keys=True, separators=(",", ":")).encode())
        == HISTORICAL_DIGEST,
        "historical 58-marker identity mismatch",
    )
    require(history == historical_markers(root), "historical marker source mismatch")
    require(current == scan_markers(root), "unclassified or renamed current marker")
    return {
        "files": len(files),
        "functions": len(functions),
        "families": len(families),
        "historical_markers": len(history),
        "current_markers": len(current),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--inventory", type=Path, default=ROOT / "tests/analyzer_port_inventory.json"
    )
    parser.add_argument("--foundry-dir", type=Path)
    parser.add_argument("--require-m3-complete", action="store_true")
    args = parser.parse_args(argv)
    try:
        print(
            json.dumps(
                validate(
                    read_json(args.inventory),
                    ROOT,
                    args.foundry_dir,
                    args.require_m3_complete,
                ),
                sort_keys=True,
            )
        )
        return 0
    except (ValueError, OSError, KeyError, TypeError, UnicodeError) as error:
        print(f"analyzer inventory: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
