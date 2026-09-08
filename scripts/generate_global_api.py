# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Embed global metadata from the pinned engine API; compatible with Python 3.4.

Producer: core/extension/extension_api_dump.cpp:499-619 @ Foundry c9d5e35.
No runtime file reads, guessed names, or independent signature tables.
"""
import argparse
import json
from pathlib import Path


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


def generate(source):
    api = json.loads(source.decode("utf-8"))
    require(type(api) is dict and type(api.get("header")) is dict, "missing API header")
    require(api["header"].get("version_major") == 4 and api["header"].get("version_minor") == 7, "expected pinned Godot 4.7 API")
    sections = ("global_constants", "global_enums", "utility_functions")
    require(all(section in api for section in sections), "missing global metadata section")
    require("builtin_classes" in api, "missing builtin carrier vocabulary")
    types = set(value["name"] for value in named_records(api["builtin_classes"])) | {"Variant", "Object"}
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
