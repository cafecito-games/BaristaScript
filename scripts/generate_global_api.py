# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Embed global metadata from the pinned engine API; compatible with Python 3.4.

Producer: core/extension/extension_api_dump.cpp:499-619 @ Foundry c9d5e35.
No runtime file reads, guessed names, or independent signature tables.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))

# Complete carrier names from pinned godot-cpp/gdextension/extension_api-4-7.json.
# Producer: core/extension/extension_api_dump.cpp:624-643,911 @ Foundry c9d5e35.
# Projection: ASCII JSON of the sorted builtin_classes[].name list, separators=(',', ':').
# It excludes paths, timestamps and unused builtin members. Exact names (including case) are
# pinned because the C++ consumer resolves them with Variant::get_type_name at API 4.7.
BUILTIN_CARRIER_SHA256 = "dd14ccfa9e8879d5ddf5de44d4b524bf0366e1c4d32cbf2284da49505f52e848"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def record(value, required, optional=()):
    require(type(value) is dict, "expected object")
    require(set(required) <= set(value) <= set(required) | set(optional), "unexpected object schema")


def named_records(values):
    require(type(values) is list, "expected array")
    names = set()
    for value in values:
        require(type(value) is dict and type(value.get("name")) is str and value["name"], "invalid name")
        require(value["name"] not in names, "duplicate name")
        names.add(value["name"])
    return values


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON object member: " + key)
        result[key] = value
    return result


def reject_non_json_number(value):
    raise ValueError("invalid JSON numeric token: " + value)


def validate_header(header):
    record(header, ("version_major", "version_minor", "version_patch", "version_status",
                    "version_build", "version_full_name", "precision"))
    for field in ("version_major", "version_minor", "version_patch"):
        require(type(header[field]) is int and 0 <= header[field] <= 2147483647,
                "invalid exact integer API header field: " + field)
    from build_config import load_config, validate_api_header
    validate_api_header(load_config(), header)


def validate_builtin_carriers(builtins):
    entries = named_records(builtins)
    names = sorted(entry["name"] for entry in entries)
    projection = json.dumps(names, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    digest = hashlib.sha256(projection).hexdigest()
    require(digest == BUILTIN_CARRIER_SHA256,
            "builtin carrier vocabulary mismatch: expected " + BUILTIN_CARRIER_SHA256 + ", actual " + digest)
    # Variant is the dynamic carrier, and Object is emitted in classes rather than builtin_classes.
    types = set(names) | {"Variant", "Object"}
    required = ("name", "is_keyed", "operators", "constructors", "has_destructor")
    optional = ("indexing_return_type", "members", "constants", "enums", "methods")
    for entry in entries:
        record(entry, required, optional)
        for flag in ("is_keyed", "has_destructor"):
            require(type(entry[flag]) is bool, "invalid builtin flag: " + flag)
        for field in ("operators", "constructors", "members", "constants", "enums", "methods"):
            if field in entry:
                require(type(entry[field]) is list and all(type(item) is dict for item in entry[field]),
                        "invalid builtin metadata array: " + field)
        if "indexing_return_type" in entry:
            require(type(entry["indexing_return_type"]) is str and entry["indexing_return_type"] in types,
                    "unknown builtin indexing carrier")
    return types


def generate(source):
    api = json.loads(source.decode("utf-8"), object_pairs_hook=unique_object, parse_constant=reject_non_json_number)
    require(type(api) is dict and type(api.get("header")) is dict, "missing API header")
    validate_header(api["header"])
    sections = ("global_constants", "global_enums", "utility_functions")
    require(all(section in api for section in sections), "missing global metadata section")
    require("builtin_classes" in api, "missing builtin carrier vocabulary")
    types = validate_builtin_carriers(api["builtin_classes"])
    constant_names = set()
    for value in named_records(api["global_constants"]):
        record(value, ("name", "value", "is_bitfield"))
        require(type(value["value"]) is int and type(value["is_bitfield"]) is bool, "invalid constant")
        constant_names.add(value["name"])
    for enum in named_records(api["global_enums"]):
        record(enum, ("name", "is_bitfield", "values"))
        require(type(enum["is_bitfield"]) is bool, "invalid enum bitfield flag")
        for value in named_records(enum["values"]):
            record(value, ("name", "value"))
            require(type(value["value"]) is int, "invalid enum constant")
            require(value["name"] not in constant_names, "duplicate global constant")
            constant_names.add(value["name"])
    for function in named_records(api["utility_functions"]):
        record(function, ("name", "category", "is_vararg", "hash"), ("return_type", "arguments"))
        require(function["category"] in ("math", "random", "general"), "unknown utility category")
        require(type(function["is_vararg"]) is bool and type(function["hash"]) is int and 0 <= function["hash"] < 2 ** 63, "invalid utility flags/hash")
        if "return_type" in function:
            require(type(function["return_type"]) is str and function["return_type"] in types, "unknown return carrier")
        for argument in named_records(function.get("arguments", [])):
            record(argument, ("name", "type"))
            require(type(argument["type"]) is str and argument["type"] in types, "unknown argument carrier")

    def quote(value):
        # API identifiers/types use printable ASCII. Reject control bytes and non-ASCII rather
        # than creating invalid C++ universal escapes or changing an engine symbol's identity.
        require(all(32 <= ord(char) < 127 for char in value), "non-ASCII/control symbol")
        return json.dumps(value, ensure_ascii=True)

    def integer(value):
        require(-(2 ** 63) <= value < 2 ** 63, "constant outside int64")
        return "(-9223372036854775807LL - 1)" if value == -(2 ** 63) else str(value) + "LL"

    lines = ["// Generated from pinned extension_api-4-7.json; do not edit.", "static void populate(Metadata &r) {"]
    for value in api["global_constants"]:
        lines.append("r.add_constant(%s, %s, \"\", %s);" % (quote(value["name"]), integer(value["value"]), str(value["is_bitfield"]).lower()))
    for enum in api["global_enums"]:
        lines.append("r.enums.insert(%s, HashMap<StringName, int64_t>());" % quote(enum["name"]))
        for value in enum["values"]:
            lines.append("r.add_constant(%s, %s, %s, %s);" % (quote(value["name"]), integer(value["value"]), quote(enum["name"]), str(enum["is_bitfield"]).lower()))
    for function in api["utility_functions"]:
        arguments = ", ".join("property(%s, %s)" % (quote(a["type"]), quote(a["name"])) for a in function.get("arguments", []))
        lines.append("r.add_utility(%s, property(%s, \"\"), %s, %sLL, {%s});" % (quote(function["name"]), quote(function.get("return_type", "")), str(function["is_vararg"]).lower(), function["hash"], arguments))
    lines.append("}")
    return "\n".join(lines) + "\n"


def write_header(source, target):
    with Path(source).open("rb") as stream:
        content = generate(stream.read()).encode("utf-8")
    target = Path(target)
    if target.exists():
        with target.open("rb") as stream:
            if stream.read() == content:
                return
    if not target.parent.exists():
        target.parent.mkdir(parents=True)
    with target.open("wb") as stream:
        stream.write(content)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source")
    parser.add_argument("target")
    args = parser.parse_args()
    write_header(args.source, args.target)
