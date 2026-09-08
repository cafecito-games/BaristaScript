# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Validate the byte-faithful pinned engine producer and rejected schema mutations."""
import copy
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

    def test_cpp_string_escaping(self):
        api = copy.deepcopy(self.api)
        api["global_constants"][0]["name"] = 'quote"backslash\\'
        self.assertIn('quote\\"backslash\\\\', GENERATOR.generate(json.dumps(api).encode("utf-8")))


if __name__ == "__main__":
    unittest.main()
