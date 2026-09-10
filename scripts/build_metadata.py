#!/usr/bin/env python3
# build_metadata.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Deterministic compiled identity and inspection of actual linked artifact bytes."""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from build_config import (CONFIG_PATH, ROOT, VERSION, canonical_json, config_fingerprint, exact_keys,
                          load_config, match, parse_json, require_equal, validate_config, validate_selection)

ENVELOPE_PREFIX = b"BARISTASCRIPT_BUILD_INFO_V1:"
ENVELOPE_SUFFIX = b":END_BARISTASCRIPT_BUILD_INFO"
MAX_PAYLOAD = 65536
REVISION = r"(?:[0-9a-f]{40}|[0-9a-f]{64})"


def git(root, *arguments):
    try:
        return subprocess.run(["git", "-C", str(root), *arguments], check=True, capture_output=True,
                              env=dict(os.environ, GIT_OPTIONAL_LOCKS="0")).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        detail = error.stderr.decode("utf-8", errors="replace").strip() if isinstance(error, subprocess.CalledProcessError) else str(error)
        raise ValueError(f"cannot inspect Git identity at {root}: {detail}") from error


def repository_identity(root):
    """Archives never inherit a containing checkout's identity through Git's upward search."""
    root = Path(root).resolve()
    if not os.path.lexists(root / ".git"):
        return {"revision": "unknown", "state": "unknown"}
    discovered = Path(os.fsdecode(git(root, "rev-parse", "--show-toplevel")).strip()).resolve()
    if discovered != root:
        raise ValueError(f"Git repository root: expected {root}, actual {discovered}")
    revision = git(root, "rev-parse", "--verify", "HEAD").decode("ascii").strip()
    match("Git revision", revision, REVISION)
    # Include staged and tracked edits even in ignored paths, actual submodule changes,
    # and nonignored untracked inputs. Ignored generated/import/build outputs are noise.
    dirty = bool(git(root, "status", "--porcelain=v1", "-z", "--untracked-files=normal", "--ignore-submodules=none"))
    return {"revision": revision, "state": "dirty" if dirty else "clean"}


def selected_gitlink(root, source):
    if source["state"] == "unknown":
        return "unknown"
    entries = git(root, "ls-files", "--stage", "-z", "--", "godot-cpp").split(b"\0")
    entries = [entry for entry in entries if entry]
    if len(entries) != 1:
        raise ValueError("godot-cpp gitlink: expected one unconflicted selected entry, actual " + repr(entries))
    try:
        mode, revision, rest = entries[0].decode("ascii").split(" ", 2)
        stage, path = rest.split("\t", 1)
    except (ValueError, UnicodeError) as error:
        raise ValueError("godot-cpp gitlink: malformed index entry") from error
    require_equal("godot-cpp gitlink mode", "160000", mode)
    require_equal("godot-cpp gitlink stage", "0", stage)
    require_equal("godot-cpp gitlink path", "godot-cpp", path)
    match("godot-cpp gitlink revision", revision, REVISION)
    return revision


def create_metadata(root, config, selection, *, native_tests):
    validate_config(config)
    build = validate_selection(config, **selection)
    require_equal("native_tests type", bool, type(native_tests))
    source = repository_identity(root)
    dependency = repository_identity(Path(root) / "godot-cpp")
    dependency["selected_revision"] = selected_gitlink(root, source)
    value = dict(schema=1, extension_version=config["extension_version"], config_sha256=config_fingerprint(config),
                 godot_api=build.pop("api"), godot_runtime=config["godot_runtime"], source=source,
                 godot_cpp=dependency, build=dict(build, native_tests=native_tests))
    validate_metadata(value)
    return value


def validate_revision(value, field):
    if value != "unknown":
        match(field, value, REVISION)


def validate_metadata(value):
    exact_keys(value, ("schema", "extension_version", "config_sha256", "godot_api", "godot_runtime", "source", "godot_cpp", "build"), "metadata")
    require_equal("metadata schema", 1, value["schema"])
    match("extension_version", value["extension_version"], VERSION)
    match("config_sha256", value["config_sha256"], r"[0-9a-f]{64}")
    match("godot_api", value["godot_api"], r"4\.(?:0|[1-9][0-9]*)")
    match("godot_runtime", value["godot_runtime"], VERSION)
    require_equal("godot_runtime API", value["godot_api"], value["godot_runtime"].rsplit(".", 1)[0])
    for field, keys in (("source", ("revision", "state")), ("godot_cpp", ("revision", "selected_revision", "state"))):
        identity = value[field]
        exact_keys(identity, keys, field)
        if identity["state"] not in ("clean", "dirty", "unknown"):
            raise ValueError(f"{field}.state: expected clean/dirty/unknown, actual {identity['state']!r}")
        validate_revision(identity["revision"], field + ".revision")
        require_equal(field + ".unknown state/revision", identity["state"] == "unknown", identity["revision"] == "unknown")
    validate_revision(value["godot_cpp"]["selected_revision"], "godot_cpp.selected_revision")
    build = value["build"]
    exact_keys(build, ("platform", "architecture", "target", "precision", "native_tests"), "build")
    require_equal("build.native_tests type", bool, type(build["native_tests"]))
    require_equal("build.precision", "single", build["precision"])
    if build["native_tests"]:
        require_equal("native build.target", "template_debug", build["target"])
    validate_selection({"godot_api": value["godot_api"], "precision": build["precision"]},
                       **{key: build[key] for key in ("platform", "architecture", "target", "precision")}, api=value["godot_api"])
    return value


def verify_identity(actual, expected, *, release=False):
    """Compare loaded/inspected metadata with separately observed expected build inputs."""
    validate_metadata(actual)
    validate_metadata(expected)
    if release:
        for side, value in (("expected", expected), ("artifact", actual)):
            for field in ("source", "godot_cpp"):
                require_equal(side + "." + field + ".state", "clean", value[field]["state"])
            selected = value["godot_cpp"]["selected_revision"]
            if selected == "unknown":
                raise ValueError(side + ".godot_cpp.selected_revision: expected complete identity, actual unknown")
            require_equal(side + ".godot_cpp.revision", selected, value["godot_cpp"]["revision"])
    def compare(actual_value, expected_value, prefix=""):
        for key, expected_item in expected_value.items():
            field = prefix + key
            if type(expected_item) is dict:
                compare(actual_value[key], expected_item, field + ".")
            else:
                require_equal(field, expected_item, actual_value[key])
    compare(actual, expected)


def encode_envelope(value):
    payload = canonical_json(validate_metadata(value)).encode("ascii")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("compiled metadata exceeds payload limit")
    return ENVELOPE_PREFIX + f"{len(payload):08x}:".encode("ascii") + hashlib.sha256(payload).hexdigest().encode("ascii") + b":" + payload + ENVELOPE_SUFFIX


def inspect_bytes(content):
    """Inspect data, without loading foreign code; universal slices must agree exactly."""
    found = []
    position = 0
    while True:
        start = content.find(ENVELOPE_PREFIX, position)
        if start < 0:
            break
        framing = start + len(ENVELOPE_PREFIX)
        header = content[framing:framing + 74]
        if re.fullmatch(rb"[0-9a-f]{8}:[0-9a-f]{64}:", header) is None:
            raise ValueError("malformed compiled metadata envelope header")
        length = int(header[:8], 16)
        if not 0 < length <= MAX_PAYLOAD:
            raise ValueError("invalid compiled metadata payload length")
        payload_start = framing + len(header)
        payload = content[payload_start:payload_start + length]
        end = payload_start + length
        if content[end:end + len(ENVELOPE_SUFFIX)] != ENVELOPE_SUFFIX:
            raise ValueError("truncated or malformed compiled metadata envelope")
        require_equal("compiled metadata checksum", header[9:73].decode("ascii"), hashlib.sha256(payload).hexdigest())
        try:
            value = validate_metadata(parse_json(payload.decode("ascii")))
        except (UnicodeError, ValueError) as error:
            raise ValueError(f"invalid compiled metadata payload: {error}") from error
        require_equal("canonical compiled metadata payload", canonical_json(value).encode("ascii"), payload)
        if found and found[0] != value:
            raise ValueError("conflicting compiled metadata envelopes in artifact")
        found.append(value)
        position = end + len(ENVELOPE_SUFFIX)
    if not found:
        raise ValueError("compiled metadata envelope absent from artifact")
    return found[0]


def inspect_artifact(path):
    try:
        return inspect_bytes(Path(path).read_bytes())
    except OSError as error:
        raise ValueError(f"cannot inspect compiled artifact {path}: {error}") from error


def write_if_changed(path, content):
    path = Path(path)
    if path.exists() and path.read_bytes() == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(content)
            stream.close()
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    return True


def compile_guards(build):
    architectures = {
        "x86_64": "defined(__x86_64__) || defined(_M_X64)",
        "x86_32": "defined(__i386__) || defined(_M_IX86)",
        "arm64": "defined(__aarch64__) || defined(_M_ARM64)",
        "arm32": "defined(__arm__) || defined(_M_ARM)",
        "wasm32": "defined(__wasm32__)",
        "universal": "defined(__x86_64__) || defined(__aarch64__)",
    }
    platforms = {
        "linux": "defined(__linux__) && !defined(__ANDROID__)",
        "windows": "defined(_WIN32)",
        "macos": "defined(__APPLE__) && !TARGET_OS_IPHONE",
        "android": "defined(__ANDROID__)",
        "ios": "defined(__APPLE__) && TARGET_OS_IPHONE",
        "web": "defined(__EMSCRIPTEN__)",
    }
    lines = ['#if defined(__APPLE__)', '#include <TargetConditionals.h>', '#endif',
             '#if defined(REAL_T_IS_DOUBLE)', '#error "compiled build metadata precision disagrees with compiler"', '#endif']
    conditions = {
        "architecture": architectures[build["architecture"]],
        "platform": platforms[build["platform"]],
        "target": ("" if build["target"] == "template_debug" else "!") + "defined(DEBUG_ENABLED)",
        "native_tests": ("" if build["native_tests"] else "!") + "defined(BARISTA_TESTS)",
    }
    for field, condition in conditions.items():
        lines.extend(("#if !(" + condition + ")", '#error "compiled build metadata ' + field + ' disagrees with compiler"', '#endif'))
    return "\n".join(lines) + "\n"


def write_header(path, value):
    envelope = encode_envelope(value)
    payload = canonical_json(value).encode("ascii")
    offset = len(ENVELOPE_PREFIX) + 74
    content = ("// Generated by scripts/build_metadata.py; do not edit.\n#pragma once\n"
               + compile_guards(value["build"])
               + "static constexpr char BS_BUILD_INFO_ENVELOPE[] = " + json.dumps(envelope.decode("ascii")) + ";\n"
               + f"static constexpr int BS_BUILD_INFO_OFFSET = {offset};\n"
               + f"static constexpr int BS_BUILD_INFO_LENGTH = {len(payload)};\n")
    return write_if_changed(path, content.encode("ascii"))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--platform")
    parser.add_argument("--architecture")
    parser.add_argument("--target")
    parser.add_argument("--api")
    parser.add_argument("--precision")
    parser.add_argument("--native-tests", action="store_true")
    parser.add_argument("--header", type=Path)
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--release", action="store_true", help="require clean, complete expected and artifact identity")
    args = parser.parse_args(argv)
    try:
        if args.release and not args.verify:
            raise ValueError("--release requires --verify against an actual artifact")
        if args.artifact and not args.verify:
            if args.release or args.header:
                raise ValueError("--release/--header require metadata generation or verification, not inspection alone")
            print(canonical_json(inspect_artifact(args.artifact)))
            return 0
        config = load_config(args.config or args.root / CONFIG_PATH.name)
        value = create_metadata(args.root, config, {key: getattr(args, key) for key in ("platform", "architecture", "target", "api", "precision")},
                                native_tests=args.native_tests)
        if args.verify:
            if not args.artifact:
                raise ValueError("--verify requires an actual --artifact")
            verify_identity(inspect_artifact(args.artifact), value, release=args.release)
        elif args.header:
            write_header(args.header, value)
        else:
            print(canonical_json(value))
        return 0
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
