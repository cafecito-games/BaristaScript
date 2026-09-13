# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Validate the byte-faithful pinned engine producer and rejected schema mutations."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import runpy
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = SimpleNamespace(**runpy.run_path(str(ROOT / "scripts/generate_global_api.py")))


class GlobalAPITest(unittest.TestCase):
    def setUp(self):
        # Real producer: Foundry core/extension/extension_api_dump.cpp:499-619 @ c9d5e35.
        # Use the checked-in pinned submodule output byte-for-byte, not a hand-written facsimile.
        self.path = ROOT / "godot-cpp/gdextension/extension_api-4-7.json"
        with self.path.open("rb") as stream:
            self.source = stream.read()
        self.api = json.loads(self.source.decode("utf-8"))

    def test_real_producer_and_reproducibility(self):
        first = GENERATOR.generate(self.source)
        self.assertEqual(first, GENERATOR.generate(self.source))
        self.assertEqual(hashlib.sha256(first.encode("utf-8")).hexdigest(),
                         "089d93d652db165948c5ec01258d80c6e7d42f1bae2f4b6f394b2b56556d7eb6")
        constants = list(self.api["global_constants"])
        for enum in self.api["global_enums"]:
            for value in enum["values"]:
                constants.append(dict(value, enum=enum["name"], is_bitfield=enum["is_bitfield"]))
        self.assertEqual(first.count("r.add_constant("), len(constants))
        for value in constants:
            number = "(-9223372036854775807LL - 1)" if value["value"] == -(2 ** 63) else str(value["value"]) + "LL"
            self.assertIn('r.add_constant(%s, %s, %s, %s);' % (json.dumps(value["name"]), number, json.dumps(value.get("enum", "")), str(value["is_bitfield"]).lower()), first)
        self.assertEqual(first.count("r.add_utility("), len(self.api["utility_functions"]))
        for function in self.api["utility_functions"]:
            arguments = ", ".join("property(%s, %s)" % (json.dumps(a["type"]), json.dumps(a["name"])) for a in function.get("arguments", []))
            self.assertIn('r.add_utility(%s, property(%s, ""), %s, %sLL, {%s});' % (json.dumps(function["name"]), json.dumps(function.get("return_type", "")), str(function["is_vararg"]).lower(), function["hash"], arguments), first)

    def test_unknown_and_malformed_input_rejected(self):
        mutations = [
            lambda a: a.pop("utility_functions"),
            lambda a: a.update(global_constants=None),
            lambda a: a["global_constants"][0].update(value=True),
            lambda a: a["global_enums"][0].update(is_bitfield="false"),
            lambda a: a["global_enums"][0]["values"][0].update(value="0"),
            lambda a: a["utility_functions"][0].update(return_type="UnknownCarrier"),
            lambda a: a["utility_functions"][0].update(category="unknown"),
            lambda a: a["utility_functions"][0].update(is_vararg=1),
            lambda a: a["utility_functions"][0].update(extra_status="unknown"),
            lambda a: a["utility_functions"][0]["arguments"][0].update(type="unknown"),
            lambda a: a["global_constants"].append(a["global_constants"][0]),
            lambda a: a["header"].update(version_minor=8),
            lambda a: a["global_constants"][0].update(name="bad\x00name"),
            lambda a: a["global_constants"][0].update(value=2 ** 63),
            lambda a: a["utility_functions"][0].update(hash=-1),
            lambda a: a["utility_functions"][0].update(return_type=[]),
        ]
        for mutation in mutations:
            with self.subTest(mutation=mutations.index(mutation)):
                api = copy.deepcopy(self.api)
                mutation(api)
                with self.assertRaises(ValueError):
                    GENERATOR.generate(json.dumps(api).encode("utf-8"))

    def test_idempotent_write(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "global_api.h"
            GENERATOR.write_header(self.path, target)
            before = target.stat().st_mtime_ns
            GENERATOR.write_header(self.path, target)
            self.assertEqual(before, target.stat().st_mtime_ns)

    def test_byte_level_producer_schema_mutations(self):
        # Mutate the actual producer bytes: dictionary round-tripping would erase duplicate keys.
        def replace(source, old, new, section=None):
            start = source.index(('"' + section + '":').encode("ascii")) if section else 0
            self.assertIn(old, source[start:])
            return source[:start] + source[start:].replace(old, new, 1)

        mutations = []
        for section, empty in [("header", b"{}"), ("global_constants", b"[]"),
                               ("global_enums", b"[]"), ("builtin_classes", b"[]"),
                               ("utility_functions", b"[]")]:
            mutations.append(("duplicate section " + section,
                              self.source.replace(b"{", b'{"' + section.encode("ascii") + b'":' + empty + b",", 1)))
        for field, original in [("version_major", b"4"), ("version_minor", b"7"), ("version_patch", b"0")]:
            token = ('"' + field + '": ').encode("ascii")
            for value in [original + b".0", b"true", b"false", b"-1", b"2147483648", b"null", b'"7"', b"[]",
                          b"NaN", b"Infinity", b"-Infinity"]:
                mutations.append((field + " " + value.decode("ascii"), replace(self.source, token + original, token + value)))
            mutations.append(("duplicate " + field, replace(self.source, token + original, token + original + b", " + token + original)))
        for old, new in [(b'"precision": "single"', b'"precision": null'),
                         (b'"precision": "single"', b'"precision": "double"'),
                         (b'"precision": "single"', b'"precision": "single", "unknown": true'),
                         (b'"version_patch": 0,', b''),
                         (b'"version_status": "stable"', b'"version_status": []'),
                         (b'"version_build": "official"', b'"version_build": false'),
                         (b'"version_full_name": "Godot Engine v4.7.stable.official"', b'"version_full_name": ""')]:
            mutations.append(("header shape " + old.decode("ascii"), replace(self.source, old, new)))
        mutations.append(("duplicate utility name", replace(self.source, b'"name": "sin"', b'"name": "sin", "name": "sin"', "utility_functions")))
        mutations.append(("duplicate constant value", replace(self.source, b'"value": 255', b'"value": 255, "value": 255', "global_constants")))
        for old, new in [(b'"name": "Nil"', b'"name": "UnknownCarrier"'),
                         (b'"name": "Nil"', b'"name": "bool"'),
                         (b'"name": "Nil"', b'"name": "nil"'),
                         (b'"name": "Nil"', b'"name": "Nil", "unexpected": true'),
                         (b'"name": "Nil"', b'"name": "Nil", "name": "Nil"'),
                         (b'"is_keyed": false', b'"is_keyed": 0'),
                         (b'"is_keyed": false,', b''),
                         (b'"has_destructor": false', b'"has_destructor": null'),
                         (b'"indexing_return_type": "float"', b'"indexing_return_type": "UnknownCarrier"')]:
            mutations.append(("builtin shape " + new.decode("ascii"), replace(self.source, old, new, "builtin_classes")))
        for field in ("operators", "constructors", "members", "constants", "enums", "methods"):
            token = ('"' + field + '": [').encode("ascii")
            mutations.append(("non-object builtin " + field, replace(self.source, token, token + b"null,", "builtin_classes")))
            token = ('"' + field + '":').encode("ascii")
            # Adding a malformed optional field to Nil also exercises an exact non-array shape.
            if field in ("members", "constants", "enums", "methods"):
                mutations.append(("non-array builtin " + field,
                                  replace(self.source, b'"name": "Nil"', b'"name": "Nil", ' + token + b"null", "builtin_classes")))
        invented = replace(self.source, b'"builtin_classes": [', b'"builtin_classes": [{"name":"UnknownCarrier"},')
        invented = replace(invented, b'"return_type": "float"', b'"return_type": "UnknownCarrier"', "utility_functions")
        mutations.append(("invented carrier authorizes utility", invented))
        renamed = replace(self.source, b'"name": "Nil"', b'"name": "UnknownCarrier"', "builtin_classes")
        renamed = replace(renamed, b'"return_type": "float"', b'"return_type": "UnknownCarrier"', "utility_functions")
        mutations.append(("renamed complete builtin authorizes utility", renamed))
        for label, source in mutations:
            with self.subTest(mutation=label):
                # These are schema mutations, not accidentally invalid JSON syntax. Python's
                # permissive decoder deliberately permits duplicate keys/non-finite literals.
                json.loads(source.decode("utf-8"))
                with self.assertRaises(ValueError):
                    GENERATOR.generate(source)

    def test_carrier_fingerprint_is_semantic_and_reports_evidence(self):
        api = copy.deepcopy(self.api)
        api["builtin_classes"].reverse()
        self.assertEqual(GENERATOR.validate_builtin_carriers(self.api["builtin_classes"]), GENERATOR.validate_builtin_carriers(api["builtin_classes"]))
        with self.assertRaisesRegex(ValueError, "builtin metadata projection mismatch"):
            GENERATOR.generate(json.dumps(api).encode("utf-8"))
        api["builtin_classes"][0]["name"] = "UnknownCarrier"
        with self.assertRaisesRegex(ValueError, "expected [0-9a-f]{64}, actual [0-9a-f]{64}"):
            GENERATOR.generate(json.dumps(api).encode("utf-8"))

    def test_every_closed_carrier_in_both_utility_positions(self):
        api = copy.deepcopy(self.api)
        carriers = sorted(GENERATOR.validate_builtin_carriers(api["builtin_classes"]))
        api["utility_functions"] = [dict(name="carrier_" + name, category="general", is_vararg=False,
                                         hash=1, return_type=name, arguments=[dict(name="value", type=name)])
                                    for name in carriers]
        output = GENERATOR.generate(json.dumps(api).encode("utf-8"))
        for carrier in carriers:
            with self.subTest(carrier=carrier):
                self.assertIn('property("%s", "")' % carrier, output)
                self.assertIn('property("%s", "value")' % carrier, output)

    def test_complete_builtin_projection_and_native_oracle(self):
        fields = ("constructors", "methods", "members", "constants", "enums")
        projection = [{key: builtin[key] for key in ("name",) + fields if key in builtin}
                      for builtin in self.api["builtin_classes"]]
        encoded = json.dumps(projection, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
        self.assertEqual(hashlib.sha256(self.source).hexdigest(), "53d37f85be32b6d10fb2266ca51f6ef0c3a55728acdb7c8301b1458a93c00943")
        self.assertEqual(hashlib.sha256(encoded.encode("ascii")).hexdigest(),
                         "8c4b3d220359541f1a73c9465b008aa3afa4c31ace0e04d99e34bf084b22fa70")
        self.assertEqual([sum(len(b.get(f, [])) for b in projection) for f in fields], [156, 999, 62, 210, 7])
        output = GENERATOR.generate(self.source)
        native = output.split("#ifdef BARISTA_TESTS\n", 1)[1].split("#endif", 1)[0]
        retained = native.split("BS_NATIVE_BUILTIN_ORACLE\n", 1)[1].split(')BSAPI";', 1)[0]
        self.assertEqual(retained, encoded)
        self.assertNotIn("BS_NATIVE_BUILTIN_ORACLE", output.split("#ifdef BARISTA_TESTS\n", 1)[0])
        self.assertEqual(output.count("r.add_constructor("), 156)
        self.assertEqual(output.count("r.add_builtin_method("), 999)
        self.assertEqual(output.count("].members.push_back("), 62)
        self.assertEqual(output.count("r.add_builtin_constant("), 210)
        self.assertEqual(output.count("r.add_builtin_enum("), 7)
        defaults = [(a["type"], a["default_value"]) for b in projection
                    for m in b["constructors"] + b.get("methods", [])
                    for a in m.get("arguments", []) if "default_value" in a]
        self.assertEqual(len(defaults), 150)
        self.assertEqual(len(set(defaults)), 19)

    def test_builtin_schema_is_decoded_before_fingerprint(self):
        def builtin(api, name="String"):
            return next(b for b in api["builtin_classes"] if b["name"] == name)
        def method(api):
            return builtin(api)["methods"][0]
        def default(api):
            return next(a for m in builtin(api)["methods"] for a in m.get("arguments", []) if "default_value" in a)
        mutations = [
            (lambda a: builtin(a).pop("constructors"), "schema"),
            (lambda a: builtin(a)["constructors"][0].update(index=True), "constructor index"),
            (lambda a: builtin(a)["constructors"][0].update(index=-1), "constructor index"),
            (lambda a: builtin(a)["constructors"].append(builtin(a)["constructors"][0]), "duplicate constructor"),
            (lambda a: builtin(a)["methods"].append(method(a)), "duplicate name"),
            (lambda a: method(a).update(is_const=1), "method flag"),
            (lambda a: method(a).update(is_static=None), "method flag"),
            (lambda a: method(a).update(is_vararg="false"), "method flag"),
            (lambda a: method(a).update(hash=True), "method hash"),
            (lambda a: method(a).update(hash=2**63), "method hash"),
            (lambda a: method(a).update(hash_compatibility=[False]), "compatibility hash"),
            (lambda a: method(a).update(hash_compatibility=[-1]), "compatibility hash"),
            (lambda a: method(a).update(hash_compatibility=1), "compatibility hashes"),
            (lambda a: method(a).update(return_type="invented"), "unknown builtin carrier"),
            (lambda a: method(a).update(arguments=[dict(name="x", type="int"), dict(name="x", type="int")]), "duplicate name"),
            (lambda a: method(a).update(arguments=[dict(name="x", type="int", default_value="0"), dict(name="y", type="int")]), "non-trailing"),
            (lambda a: default(a).update(default_value="danger()"), "literal|encoding|default"),
            (lambda a: default(a).update(type="invented"), "unknown builtin carrier"),
            (lambda a: builtin(a,"Vector2")["members"].append(builtin(a,"Vector2")["members"][0]), "duplicate name"),
            (lambda a: builtin(a,"Vector2")["members"][0].update(type="invented"), "unknown builtin carrier"),
            (lambda a: builtin(a,"Vector2")["constants"][0].update(value="Vector2(1)"), "component count"),
            (lambda a: builtin(a,"Vector2")["constants"][0].update(value="Vector2(1e400, 0)"), "non-finite"),
            (lambda a: builtin(a,"Vector2i")["constants"][0].update(value="Vector2i(2147483648, 0)"), "int32"),
            (lambda a: builtin(a,"Vector2i")["constants"][0].update(value="Vector2i(1.5, 0)"), "integer component"),
            (lambda a: builtin(a,"Vector2")["enums"][0].update(is_bitfield=False), "schema"),
            (lambda a: builtin(a,"Vector2")["enums"][0]["values"][0].update(value=True), "int64"),
            (lambda a: builtin(a,"Vector2")["enums"][0]["values"][0].update(value=2**63), "int64"),
            (lambda a: builtin(a,"Vector2")["enums"][0]["values"].append(builtin(a,"Vector2")["enums"][0]["values"][0]), "duplicate name"),
        ]
        for mutate, message in mutations:
            with self.subTest(mutation=mutations.index((mutate,message))):
                api=copy.deepcopy(self.api); mutate(api)
                with self.assertRaisesRegex(ValueError,message):
                    GENERATOR.generate(json.dumps(api).encode("utf-8"))
        # Real byte mutations preserve duplicate keys that a dict roundtrip would erase.
        for token in (b'"index": 0', b'"is_const": true', b'"is_static": false', b'"default_value": "-1"'):
            start=self.source.index(b'"builtin_classes":')
            self.assertIn(token,self.source[start:])
            source=self.source[:start]+self.source[start:].replace(token,token+b", "+token,1)
            with self.assertRaisesRegex(ValueError,"duplicate JSON object member"):
                GENERATOR.generate(source)

    def test_numeric_components_are_bounded_and_keep_matrix_order(self):
        # Asymmetric values expose row/column permutation hidden by identity constants.
        # VariantParser and godot-cpp use this scalar order for each admitted constructor.
        for carrier, count in (("Transform2D", 6), ("Basis", 9), ("Transform3D", 12), ("Projection", 16)):
            text = carrier + "(" + ", ".join(str(i) for i in range(1, count + 1)) + ")"
            expected = "Variant(" + carrier + "(" + ", ".join(str(i) + ".0" for i in range(1, count + 1)) + "))"
            self.assertEqual(GENERATOR.decoded_value(carrier, text), expected)
            with self.assertRaisesRegex(ValueError, "component count"):
                GENERATOR.decoded_value(carrier, carrier + "(0)")
        for text in ("Vector2(1e-9999, 0)", "Vector2(1e-46, 0)", "Vector2(3.5e38, 0)"):
            with self.assertRaisesRegex(ValueError, "single precision"):
                GENERATOR.decoded_value("Vector2", text)
        for text in ("Vector2(nan, 0)", "Vector2(-inf, 0)", "Vector2(user(), 0)"):
            with self.assertRaisesRegex(ValueError, "numeric component"):
                GENERATOR.decoded_value("Vector2", text)

    def test_atomic_publication_preserves_previous_output_on_every_failure(self):
        from unittest import mock
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/"api.json"; source.write_bytes(self.source)
            target=Path(directory)/"api.h"; target.write_bytes(b"previous output")
            before=target.stat().st_mtime_ns
            source.write_bytes(self.source.replace(b'"index": 0',b'"index": true',1))
            with self.assertRaises(ValueError): GENERATOR.write_header(source,target)
            self.assertEqual(target.read_bytes(),b"previous output"); self.assertEqual(target.stat().st_mtime_ns,before)
            source.write_bytes(self.source)
            with mock.patch.object(GENERATOR.os,"replace",side_effect=OSError("injected publication failure")):
                with self.assertRaises(OSError): GENERATOR.write_header(source,target)
            self.assertEqual(target.read_bytes(),b"previous output"); self.assertEqual(target.stat().st_mtime_ns,before)
            with mock.patch.object(GENERATOR.os,"fsync",side_effect=OSError("injected write failure")):
                with self.assertRaises(OSError): GENERATOR.write_header(source,target)
            self.assertEqual(target.read_bytes(),b"previous output"); self.assertEqual(target.stat().st_mtime_ns,before)
            self.assertEqual(sorted(p.name for p in Path(directory).iterdir()),["api.h","api.json"])
            GENERATOR.write_header(source,target); content=target.read_bytes(); before=target.stat().st_mtime_ns
            GENERATOR.write_header(source,target); self.assertEqual(target.read_bytes(),content); self.assertEqual(target.stat().st_mtime_ns,before)

    def test_cpp_string_escaping(self):
        api = copy.deepcopy(self.api)
        api["global_constants"][0]["name"] = 'quote"backslash\\'
        self.assertIn('quote\\"backslash\\\\', GENERATOR.generate(json.dumps(api).encode("utf-8")))


if __name__ == "__main__":
    unittest.main()
