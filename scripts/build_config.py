#!/usr/bin/env python3
# build_config.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Validated version authority shared by builds, generators, CI and diagnostics."""

import argparse
import ast
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = ROOT / "build_versions.json"
VERSION = r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
ARCHITECTURES = {
    "linux": {"x86_64", "x86_32", "arm64", "arm32"},
    "windows": {"x86_64", "x86_32", "arm64"},
    "macos": {"x86_64", "arm64", "universal"},
    "android": {"x86_64", "x86_32", "arm64", "arm32"},
    "ios": {"arm64", "x86_64"},
    "web": {"wasm32"},
}
TOOL_PATTERNS = {
    "scons": VERSION, "mingw": VERSION, "ndk": r"r[1-9][0-9]*[a-z]",
    "emscripten": VERSION, "clang_format": VERSION, "windows_compiler": r"mingw|msvc",
}


def require_equal(field, expected, actual):
    if type(expected) is not type(actual) or actual != expected:
        raise ValueError(f"{field}: expected {expected!r}, actual {actual!r}")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError(f"invalid JSON numeric token: {value}")


def parse_json(text):
    return json.loads(text, object_pairs_hook=unique_object, parse_constant=reject_constant)


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True, allow_nan=False)


def exact_keys(value, expected, field):
    if type(value) is not dict or set(value) != set(expected):
        actual = sorted(value) if type(value) is dict else value
        raise ValueError(f"{field} keys: expected {sorted(expected)!r}, actual {actual!r}")


def match(field, value, pattern):
    if type(value) is not str or re.fullmatch(pattern, value) is None:
        raise ValueError(f"{field}: expected pattern {pattern!r}, actual {value!r}")


def validate_config(config):
    exact_keys(config, ("schema", "extension_version", "godot_api", "godot_api_patch",
                        "godot_runtime", "precision", "toolchain", "platform_overrides"), "configuration")
    require_equal("schema", 1, config["schema"])
    match("extension_version", config["extension_version"], VERSION)
    match("godot_api", config["godot_api"], r"4\.(?:0|[1-9][0-9]*)")
    if type(config["godot_api_patch"]) is not int or not 0 <= config["godot_api_patch"] <= 2147483647:
        raise ValueError(f"godot_api_patch: expected nonnegative integer, actual {config['godot_api_patch']!r}")
    match("godot_runtime", config["godot_runtime"], VERSION)
    require_equal("godot_runtime API", config["godot_api"], config["godot_runtime"].rsplit(".", 1)[0])
    require_equal("precision", "single", config["precision"])
    exact_keys(config["toolchain"], TOOL_PATTERNS, "toolchain")
    for key, pattern in TOOL_PATTERNS.items():
        match("toolchain." + key, config["toolchain"][key], pattern)
    allowed_overrides = {"web": {"emscripten"}, "windows": {"windows_compiler"}}
    exact_keys(config["platform_overrides"], allowed_overrides, "platform override")
    for platform, keys in allowed_overrides.items():
        overrides = config["platform_overrides"][platform]
        exact_keys(overrides, keys, "platform override " + platform)
        for key, value in overrides.items():
            match("platform override " + platform + "." + key, value, TOOL_PATTERNS[key])
    return config


def load_config(path=CONFIG_PATH):
    try:
        return validate_config(parse_json(Path(path).read_text(encoding="utf-8")))
    except (OSError, UnicodeError, ValueError) as error:
        raise ValueError(f"invalid build configuration {path}: {error}") from error


def effective_tools(config, platform=None, supplied=None):
    if platform is not None and (type(platform) is not str or platform not in ARCHITECTURES):
        raise ValueError(f"platform: expected one of {sorted(ARCHITECTURES)}, actual {platform!r}")
    result = dict(config["toolchain"])
    result.update(config["platform_overrides"].get(platform, {}))
    for key, actual in (supplied or {}).items():
        if key not in result:
            raise ValueError(f"tool override: expected one of {sorted(result)}, actual {key!r}")
        require_equal(key, result[key], actual)
    return result


def setup_tools(config, platform, supplied):
    defaults = effective_tools(config)
    effective = effective_tools(config, platform)
    for key, value in supplied.items():
        if key not in defaults:
            raise ValueError(f"unknown setup tool: {key}")
        if not value:
            continue
        if value not in (defaults[key], effective[key]):
            raise ValueError(f"setup {key}: expected configured default/override, actual {value!r}")
        defaults[key] = value
    return defaults


def validate_selection(config, *, platform, architecture, target, api, precision):
    require_equal("Godot API", config["godot_api"], api)
    require_equal("precision", config["precision"], precision)
    if type(platform) is not str or platform not in ARCHITECTURES:
        raise ValueError(f"platform: expected one of {sorted(ARCHITECTURES)}, actual {platform!r}")
    if type(architecture) is not str or architecture not in ARCHITECTURES[platform]:
        raise ValueError(f"architecture for {platform}: expected one of {sorted(ARCHITECTURES[platform])}, actual {architecture!r}")
    if target not in ("template_debug", "template_release"):
        raise ValueError(f"target: expected template_debug or template_release, actual {target!r}")
    return dict(platform=platform, architecture=architecture, target=target, api=api, precision=precision)


def api_path(config, root=ROOT):
    return Path(root) / "godot-cpp" / "gdextension" / ("extension_api-" + config["godot_api"].replace(".", "-") + ".json")


def validate_api_file(config, actual, root=ROOT):
    expected = api_path(config, root)
    try:
        expected_bytes, actual_bytes = expected.read_bytes(), Path(actual).read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read selected API input: {error}") from error
    require_equal("actual binding API SHA256", hashlib.sha256(expected_bytes).hexdigest(), hashlib.sha256(actual_bytes).hexdigest())
    validate_api_header(config, parse_json(actual_bytes)["header"])


def expected_api_header(config):
    major, minor = map(int, config["godot_api"].split("."))
    return dict(version_major=major, version_minor=minor, version_patch=config["godot_api_patch"],
                version_status="stable", version_build="official",
                version_full_name=f"Godot Engine v{config['godot_api']}.stable.official", precision=config["precision"])


def validate_api_header(config, header):
    expected = expected_api_header(config)
    exact_keys(header, expected, "API header")
    for key, value in expected.items():
        require_equal("API header " + key, value, header[key])


def config_fingerprint(config):
    return hashlib.sha256(canonical_json(validate_config(config)).encode("ascii")).hexdigest()


def flat_config(config, platform=None):
    return {**{key: str(config[key]) for key in ("extension_version", "godot_api", "godot_api_patch", "godot_runtime", "precision")},
            **effective_tools(config, platform), "config_sha256": config_fingerprint(config)}


def validate_scons_consumer(text):
    try:
        statements = ast.parse(text).body
    except SyntaxError as error:
        raise ValueError(f"invalid SConstruct: {error}") from error
    required = ('versions = load_config()', 'localEnv["api_version"] = versions["godot_api"]',
                'localEnv["precision"] = versions["precision"]')
    for source in required:
        expected = ast.dump(ast.parse(source).body[0])
        if sum(ast.dump(node) == expected for node in statements) != 1:
            raise ValueError(f"SConstruct must consume shared configuration: {source}")
    for field in ("api_version", "precision"):
        target = ast.dump(ast.parse('localEnv["' + field + '"] = None').body[0].targets[0])
        assignments = [node for node in statements if isinstance(node, ast.Assign) and any(ast.dump(item) == target for item in node.targets)]
        if len(assignments) != 1:
            raise ValueError(f"SConstruct has conflicting {field} assignments")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=CONFIG_PATH)
    parser.add_argument("--platform", choices=sorted(ARCHITECTURES))
    parser.add_argument("--format", choices=("json", "cmake", "github", "format-requirement"), default="json")
    parser.add_argument("--get")
    parser.add_argument("--setup", action="store_true", help="resolve composite-action defaults and explicit configured overrides")
    parser.add_argument("--tool", action="append", default=[], metavar="NAME=VALUE")
    parser.add_argument("--expect-api-file", type=Path)
    parser.add_argument("--expect-api")
    parser.add_argument("--expect-precision")
    args = parser.parse_args(argv)
    try:
        config = load_config(args.config)
        for field, expected in (("godot_api", args.expect_api), ("precision", args.expect_precision)):
            if expected is not None:
                require_equal(field, config[field], expected)
        if args.expect_api_file:
            validate_api_file(config, args.expect_api_file)
        values = flat_config(config, args.platform)
        if args.setup:
            supplied = {}
            for item in args.tool:
                key, separator, value = item.partition("=")
                if not separator or key in supplied:
                    raise ValueError("setup tool inputs require unique NAME=VALUE entries")
                supplied[key] = value
            values.update(setup_tools(config, args.platform, supplied))
        elif args.tool:
            raise ValueError("--tool requires --setup")
        if args.get:
            if args.get not in values:
                raise ValueError(f"configuration key: expected one of {sorted(values)}, actual {args.get!r}")
            print(values[args.get])
        elif args.format == "format-requirement":
            print("clang-format==" + values["clang_format"])
        elif args.format == "cmake":
            for key, value in values.items():
                print(f'set(BARISTA_{key.upper()} "{value}")')
        elif args.format == "github":
            for key, value in values.items():
                print(f"{key}={value}")
        else:
            print(canonical_json(values))
        return 0
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
