# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Embed global metadata from the pinned engine API; compatible with Python 3.4.

Producer: core/extension/extension_api_dump.cpp:499-619 @ Foundry c9d5e35.
No runtime file reads, guessed names, or independent signature tables.
"""
import argparse
from decimal import Decimal
import hashlib
import json
import math
import os
import re
import tempfile
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


BUILTIN_METADATA_SHA256 = "8c4b3d220359541f1a73c9465b008aa3afa4c31ace0e04d99e34bf084b22fa70"
BUILTIN_SECTIONS = ("constructors", "methods", "members", "constants", "enums")


def quote(value):
    require(type(value) is str and all(32 <= ord(char) < 127 for char in value), "non-ASCII/control symbol")
    return json.dumps(value, ensure_ascii=True)


def integer(value, bits=64):
    require(type(value) is int and -(2 ** (bits - 1)) <= value < 2 ** (bits - 1), "constant outside int" + str(bits))
    return "(-9223372036854775807LL - 1)" if value == -(2 ** 63) else str(value) + "LL"


def decoded_value(carrier, text):
    """Closed typed producer literals, never evaluated as Python or pasted as C++ code."""
    require(type(text) is str, "invalid default/value text")
    if carrier == "Variant" and text == "null":
        return "Variant()"
    if carrier == "bool" and text in ("true", "false"):
        return "Variant(" + text + ")"
    if carrier == "String":
        require(text in ('""', '" "', '"{_}"'), "unsupported String default")
        return "Variant(String(" + quote(json.loads(text)) + "))"
    if carrier == "int":
        require(re.match(r"^-?(0|[1-9][0-9]*)$", text) is not None, "invalid integer literal")
        return "Variant(int64_t(" + integer(int(text)) + "))"
    def number(atom, integral=False):
        if integral:
            require(re.match(r"^-?(0|[1-9][0-9]*)$", atom) is not None, "invalid integer component")
            return "int32_t(" + integer(int(atom), 32) + ")"
        if atom == "inf":
            return "std::numeric_limits<real_t>::infinity()"
        require(re.match(r"^[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$", atom) is not None, "invalid numeric component")
        value = float(atom)
        require(math.isfinite(value), "non-finite numeric literal")
        require((value == 0 and Decimal(atom) == 0) or 1.401298464324817e-45 <= abs(value) <= 3.4028234663852886e38, "component outside single precision")
        return repr(value)
    if carrier == "float":
        return "Variant(double(" + number(text) + "))"
    counts = {"Vector2": 2, "Vector2i": 2, "Vector3": 3, "Vector3i": 3, "Vector4": 4,
              "Vector4i": 4, "Plane": 4, "Quaternion": 4, "Color": 4, "Transform2D": 6,
              "Basis": 9, "Transform3D": 12, "Projection": 16}
    require(carrier in counts and text.startswith(carrier + "(") and text.endswith(")"), "unsupported typed value encoding")
    atoms = text[len(carrier) + 1:-1].split(", ")
    require(len(atoms) == counts[carrier], "invalid constructor component count")
    values = [number(atom, carrier in ("Vector2i", "Vector3i", "Vector4i")) for atom in atoms]
    return "Variant(" + carrier + "(" + ", ".join(values) + "))"


def builtin_projection(builtins):
    return [dict((field, b[field]) for field in ("name",) + BUILTIN_SECTIONS if field in b) for b in builtins]


def validate_builtin_metadata(builtins, types):
    def carrier(value):
        require(type(value) is str and value in types, "unknown builtin carrier")
    def arguments(signature):
        defaults = False
        for arg in named_records(signature.get("arguments", [])):
            record(arg, ("name", "type"), ("default_value",))
            quote(arg["name"])
            carrier(arg["type"])
            if "default_value" in arg:
                decoded_value(arg["type"], arg["default_value"])
                defaults = True
            else:
                require(not defaults, "non-trailing default argument")
    for b in builtins:
        quote(b["name"])
        indices = set()
        for c in b["constructors"]:
            record(c, ("index",), ("arguments",))
            require(type(c["index"]) is int and 0 <= c["index"] < 2 ** 31, "invalid constructor index")
            require(c["index"] not in indices, "duplicate constructor index")
            indices.add(c["index"])
            arguments(c)
        for m in named_records(b.get("methods", [])):
            record(m, ("name", "is_const", "is_static", "is_vararg", "hash"), ("return_type", "arguments", "hash_compatibility"))
            quote(m["name"])
            for flag in ("is_const", "is_static", "is_vararg"):
                require(type(m[flag]) is bool, "invalid method flag")
            require(type(m["hash"]) is int and 0 <= m["hash"] < 2 ** 63, "invalid method hash")
            if "hash_compatibility" in m:
                require(type(m["hash_compatibility"]) is list, "invalid compatibility hashes")
                for value in m["hash_compatibility"]:
                    require(type(value) is int and 0 <= value < 2 ** 63, "invalid compatibility hash")
            if "return_type" in m:
                carrier(m["return_type"])
            arguments(m)
        for m in named_records(b.get("members", [])):
            record(m, ("name", "type")); quote(m["name"]); carrier(m["type"])
        for c in named_records(b.get("constants", [])):
            record(c, ("name", "type", "value")); quote(c["name"]); carrier(c["type"])
            decoded_value(c["type"], c["value"])
        for e in named_records(b.get("enums", [])):
            record(e, ("name", "values")); quote(e["name"])
            for v in named_records(e["values"]):
                record(v, ("name", "value")); quote(v["name"]); integer(v["value"])
    projection = json.dumps(builtin_projection(builtins), sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    digest = hashlib.sha256(projection).hexdigest()
    require(digest == BUILTIN_METADATA_SHA256, "builtin metadata projection mismatch: expected " + BUILTIN_METADATA_SHA256 + ", actual " + digest)


def builtin_lines(builtins):
    lines = []
    def args(m):
        return ", ".join("property(%s, %s)" % (quote(a["type"]), quote(a["name"])) for a in m.get("arguments", []))
    def defaults(m):
        return ", ".join(decoded_value(a["type"], a["default_value"]) for a in m.get("arguments", []) if "default_value" in a)
    for b in builtins:
        owner = quote(b["name"])
        mask = sum(1 << i for i, field in enumerate(BUILTIN_SECTIONS) if field in b)
        lines.append("r.add_builtin(%s, %d);" % (owner, mask))
        for c in b["constructors"]:
            lines.append("r.add_constructor(%s, %d, {%s}, {%s});" % (owner, c["index"], args(c), defaults(c)))
        for m in b.get("methods", []):
            flags = " | ".join(name for name, key in (("METHOD_FLAG_CONST", "is_const"), ("METHOD_FLAG_STATIC", "is_static"), ("METHOD_FLAG_VARARG", "is_vararg")) if m[key]) or "0"
            hashes = ", ".join(integer(h) for h in m.get("hash_compatibility", []))
            lines.append("r.add_builtin_method(%s, %s, property(%s, \"\"), %s, %s, {%s}, {%s}, {%s});" %
                         (owner, quote(m["name"]), quote(m.get("return_type", "")), flags, integer(m["hash"]), hashes, args(m), defaults(m)))
        for m in b.get("members", []):
            lines.append("r.builtins[%s].members.push_back(property(%s, %s));" % (owner, quote(m["type"]), quote(m["name"])))
        for c in b.get("constants", []):
            lines.append("r.add_builtin_constant(%s, %s, property(%s, \"\").type, []() -> Variant { return %s; });" % (owner, quote(c["name"]), quote(c["type"]), decoded_value(c["type"], c["value"])))
        for e in b.get("enums", []):
            lines.append("r.add_builtin_enum(%s, %s, {%s}, {%s});" % (owner, quote(e["name"]), ", ".join(quote(v["name"]) for v in e["values"]), ", ".join(integer(v["value"]) for v in e["values"])))
    return lines


def generate(source):
    api = json.loads(source.decode("utf-8"), object_pairs_hook=unique_object, parse_constant=reject_non_json_number)
    require(type(api) is dict and type(api.get("header")) is dict, "missing API header")
    validate_header(api["header"])
    sections = ("global_constants", "global_enums", "utility_functions")
    require(all(section in api for section in sections), "missing global metadata section")
    require("builtin_classes" in api, "missing builtin carrier vocabulary")
    types = validate_builtin_carriers(api["builtin_classes"])
    validate_builtin_metadata(api["builtin_classes"], types)
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
    lines.extend(builtin_lines(api["builtin_classes"]))
    lines.append("}")
    projection = json.dumps(builtin_projection(api["builtin_classes"]), sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    lines.extend(["#ifdef BARISTA_TESTS", "static const char *builtin_projection_for_tests() { return R\"BSAPI(BS_NATIVE_BUILTIN_ORACLE\n" + projection + ")BSAPI\"; }", "#endif"])
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
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="wb", dir=str(target.parent), prefix=target.name + ".", delete=False) as stream:
            temporary = stream.name
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, str(target))
        temporary = None
    finally:
        if temporary is not None:
            os.unlink(temporary)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source")
    parser.add_argument("target")
    args = parser.parse_args()
    write_header(args.source, args.target)
