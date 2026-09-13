/**************************************************************************/
/*  analyzer_constants_test.cpp                                          */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                          */
/*  This file is part of BaristaScript, a Godot GDExtension.                */
/*  SPDX-License-Identifier: MIT                                           */
/**************************************************************************/

#include "analyzer_helpers.h"
#include "bs_conformance_registry.h"
#include "storage_fixture.h"
#include "test_require.h"

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
Array array_value(std::initializer_list<Variant> values) {
	Array result;
	for (const Variant &value : values) {
		result.push_back(value);
	}
	return result;
}

// This is the real public language result, not a private analyzer serializer.
Dictionary public_validate(const String &source, const String &path, bool warnings = true) {
	return BaristaScriptLanguage::get_singleton()->_validate(source, path, true, true, warnings, false);
}

AnalysisResult fold_source(const String &expression) {
	return analyze_source(String("var probe_expression = ") + expression + String("\n"), "res://tests/fold_probe.barista");
}

void check_folded_value(const String &source, const Variant &expected) {
	const auto result = fold_source(source);
	CHECK(result.valid());
	const auto *expression = find_expression(result);
	BS_TEST_REQUIRE(expression != nullptr);
	CHECK(expression->is_constant);
	CHECK(expression->reduced_value == expected);
}

void check_public_errors(const Dictionary &report, const std::vector<ExpectedError> &expected) {
	const Array errors = report.get("errors", Array());
	BS_TEST_REQUIRE(size_t(errors.size()) == expected.size());
	for (int i = 0; i < errors.size(); ++i) {
		const Dictionary actual = errors[i];
		CHECK(String(actual.get("message", "")) == expected[i].message.c_str());
		CHECK(int(actual.get("line", -1)) == expected[i].line);
		CHECK(int(actual.get("column", -1)) == expected[i].column);
	}
}

void check_public_success(const Dictionary &report) {
	CHECK(bool(report.get("valid", false)));
	check_public_errors(report, {});
	CHECK(Array(report.get("warnings", Array())).is_empty());
}

void scenario_constant_dictionary_key_conversion() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	for (const char *source : {
				 "const BASE: Dictionary[float, int] = {1.0: 2}\nvar probe_expression = BASE[1]\n",
				 "const BASE: Dictionary[int, int] = {1: 2}\nvar probe_expression = BASE[1.5]\n",
				 "const BASE: Dictionary[int, int] = {1: 2}\nvar probe_expression = BASE[true]\n",
				 "const BASE: Dictionary[bool, int] = {true: 2}\nvar probe_expression = BASE[1]\n",
				 "const BASE: Dictionary[String, int] = {\"x\": 2}\nvar probe_expression = BASE[&\"x\"]\n",
				 "const BASE: Dictionary[StringName, int] = {&\"x\": 2}\nvar probe_expression = BASE[\"x\"]\n",
		 }) {
		const String path = "res://tests/review_builtin_dictionary_key.barista";
		const auto result = analyze_source(source, path);
		const auto *expression = find_expression(result);
		BS_TEST_REQUIRE(expression != nullptr);
		CHECK(result.valid());
		CHECK(expression->is_constant);
		CHECK(expression->get_datatype().to_string() == "int");
		CHECK(expression->reduced_value == Variant(2));
		const Dictionary report = BaristaScriptLanguage::get_singleton()->_validate(source, path, true, true, true, false);
		CHECK(bool(report.get("valid", false)));
		check_public_errors(report, {});
		CHECK(Array(report.get("warnings", Array())).is_empty());
	}
	struct Rejection {
		const char *source;
		const char *message;
	};
	const std::vector<Rejection> rejected = {
		{ "const BASE = {1.0: 2}\nvar probe_expression = BASE[1]\n", "Cannot get index \"1\" from \"{ 1.0: 2 }\"." },
		{ "const BASE: Dictionary[float, int] = {1.0: 2}\nvar probe_expression = BASE[\"missing\"]\n", "Cannot get index \"missing\" from \"{ 1.0: 2 }\"." },
		{ "const BASE: Dictionary[float, int] = {1.0: 2}\nvar probe_expression = BASE[3]\n", "Cannot get index \"3\" from \"{ 1.0: 2 }\"." },
		{ "const BASE: Dictionary[int, int] = {1: 2}\nvar probe_expression = BASE[1e309]\n", "Cannot get index \"inf\" from \"{ 1: 2 }\"." },
	};
	for (const auto &sample : rejected) {
		const String path = "res://tests/review_rejected_dictionary_key.barista";
		const auto result = analyze_source(sample.source, path);
		const auto *expression = find_expression(result);
		BS_TEST_REQUIRE(expression != nullptr);
		CHECK_FALSE(result.valid());
		CHECK_FALSE(expression->is_constant);
		// The legacy observer exports a value only from a constant expression.
		const Variant observed_value = expression->is_constant ? expression->reduced_value : Variant();
		CHECK(observed_value.get_type() == Variant::NIL);
		const Dictionary report = BaristaScriptLanguage::get_singleton()->_validate(sample.source, path, true, true, false, false);
		check_public_errors(report, { { sample.message, 2, 29 } });
	}
}
void scenario_dictionary_literal_constant_parity() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	Dictionary named_inner;
	named_inner[StringName("inner")] = 2;
	Dictionary string_inner;
	string_inner[String("inner")] = 2;
	struct Sample {
		const char *source;
		Variant key;
		Variant value;
	};
	const std::vector<Sample> samples = {
		{ "{\"key\": 1}", "key", 1 },
		{ "{ key = 1 }", "key", 1 },
		{ "{\"outer\": { inner = 2 }}", "outer", named_inner },
		{ "{ outer = {\"inner\": 2} }", StringName("outer"), string_inner },
	};
	for (const auto &sample : samples) {
		const auto result = fold_source(sample.source);
		CHECK(result.valid());
		const auto *expression = find_expression(result);
		BS_TEST_REQUIRE(expression != nullptr);
		CHECK(expression->is_constant);
		BS_TEST_REQUIRE(expression->reduced_value.get_type() == Variant::DICTIONARY);
		const Dictionary value = expression->reduced_value;
		CHECK(value.get(sample.key, Variant()) == sample.value);
	}
	check_folded_value("len({\"key\": 1})", 1);
	check_folded_value("len({ key = 1 })", 1);
	check_folded_value("len({})", 0);
	check_folded_value("{}", Dictionary());
	CHECK(analyze_source("func test(value: int):\n\tvar python := {\"key\": value}\n\tvar lua := { key = value }\n\tvar python_count: int = len(python)\n\tvar lua_count: int = len(lua)\n", "res://tests/dictionary_nonconstant.barista").valid());
	struct Duplicate {
		const char *source;
		int first_line;
		int line;
		int column;
	};
	const std::vector<Duplicate> duplicates = {
		{ "const DUPLICATE = {\"key\": 1, \"key\": 2}\n", 1, 1, 30 },
		{ "const DUPLICATE = { key = 1, key = 2 }\n", 1, 1, 30 },
		{ "const KEY = &\"key\"\nconst DUPLICATE = {KEY: 1, \"key\": 2}\n", 2, 2, 28 },
		{ "const DUPLICATE = { key = 1, \"key\" = 2 }\n", 1, 1, 30 },
	};
	for (const auto &sample : duplicates) {
		const String path = "res://tests/dictionary_duplicate.barista";
		const auto result = analyze_source(sample.source, path);
		CHECK_FALSE(result.valid());
		BS_TEST_REQUIRE(result.parser->get_errors().size() == 1);
		const String expected = vformat("Key \"key\" was already used in this dictionary (at line %d).", sample.first_line);
		CHECK(result.parser->get_errors().front()->get().message == expected);
		const Dictionary report = BaristaScriptLanguage::get_singleton()->_validate(sample.source, path, true, true, false, false);
		const Array errors = report.get("errors", Array());
		BS_TEST_REQUIRE(errors.size() == 1);
		const Dictionary error = errors[0];
		CHECK(int(error.get("line", -1)) == sample.line);
		CHECK(int(error.get("column", -1)) == sample.column);
	}
	for (const char *source : {
				 "var value = 1\nconst BAD = len({\"key\": value})\n",
				 "var value = 1\nconst BAD = len({ key = value })\n",
		 }) {
		const auto result = analyze_source(source, "res://tests/dictionary_nonconstant_const.barista");
		CHECK_FALSE(result.valid());
		CHECK_FALSE(errors_contain(result, "not declared"));
	}
}
void scenario_pure_constant_review_regressions() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	for (const char *source : {
				 "var probe_expression = ([1, 2] as PackedInt32Array) as Array\n",
				 "var probe_expression = [([1, 2] as PackedInt32Array) as Array]\n",
				 "const VALUE = ([1, 2] as PackedInt32Array) as Array\nvar probe_expression = [VALUE]\n",
				 "const PACKED: Variant = [1, 2] as PackedInt32Array\nconst VALUE: Array = PACKED\nvar probe_expression = [VALUE]\n",
		 }) {
		const String path = "res://tests/review_converted_readonly.barista";
		const auto result = analyze_source(source, path);
		const auto *expression = find_expression(result);
		BS_TEST_REQUIRE(expression != nullptr);
		CHECK(result.valid());
		CHECK(expression->is_constant);
		check_public_success(public_validate(source, path));
		BS_TEST_REQUIRE(expression->reduced_value.get_type() == Variant::ARRAY);
		const Array values = expression->reduced_value;
		CHECK(values.is_read_only());
		BS_TEST_REQUIRE(!values.is_empty());
		if (Variant(values[0]).get_type() == Variant::ARRAY) {
			CHECK(Array(values[0]).is_read_only());
		}
	}
	struct Selection {
		const char *source;
		const char *datatype;
		Variant value;
	};
	const std::vector<Selection> selections = {
		{ "const BASE: Array[Variant] = [0]\nvar probe_expression = BASE[0]\n", "int", 0 },
		{ "const BASE: Array[int | String] = [0]\nvar probe_expression = BASE[0]\n", "int", 0 },
		{ "const BASE: Dictionary[String, int] = {\"x\": 1}\nvar probe_expression = BASE[&\"x\"]\n", "int", 1 },
		{ "const BASE: Array[Array[int]] = [[0]]\nvar probe_expression = BASE[0]\n", "Array[int]", array_value({ 0 }) },
		{ "const BASE: Array[(int, String)] = [(0, \"x\")]\nvar probe_expression = BASE[0]\n", "(int, String)", array_value({ 0, "x" }) },
		{ "const BASE: Dictionary[String, Array[int]] = {\"x\": [0]}\nvar probe_expression = BASE[\"x\"]\n", "Array[int]", array_value({ 0 }) },
		{ "const BASE: Variant = 0\nvar probe_expression = BASE\n", "Variant", 0 },
		{ "const BASE = [null]\nvar probe_expression = BASE[0]\n", "null", Variant() },
	};
	for (const auto &sample : selections) {
		const auto result = analyze_source(sample.source, "res://tests/review_constant_selection.barista");
		const auto *expression = find_expression(result);
		BS_TEST_REQUIRE(expression != nullptr);
		CHECK(result.valid());
		CHECK(expression->is_constant);
		CHECK(expression->get_datatype().to_string() == sample.datatype);
		CHECK(expression->reduced_value == sample.value);
	}
	struct Consumer {
		const char *source;
		int line;
	};
	const std::vector<Consumer> consumers = {
		{ "const BASE: Array[Variant] = [0]\nfunc test():\n\t@warning_ignore(\"inference_on_variant\")\n\tvar sub := BASE[0]\n\tif sub is String: pass\n", 5 },
		{ "const BASE: Array[int | String] = [0]\nfunc test():\n\tvar sub := BASE[0]\n\tif sub is String: pass\n", 4 },
		{ "const BASE: Dictionary[String, Variant] = {\"x\": 0}\nfunc test():\n\t@warning_ignore(\"inference_on_variant\")\n\tvar sub := BASE[\"x\"]\n\tif sub is String: pass\n", 5 },
	};
	for (const auto &sample : consumers) {
		const Dictionary report = public_validate(sample.source, "res://tests/review_selected_consumer.barista");
		CHECK_FALSE(bool(report.get("valid", true)));
		check_public_errors(report, { { "Expression is of type \"int\" so it can't be of type \"String\".", sample.line, 8 } });
		CHECK(Array(report.get("warnings", Array())).is_empty());
	}
	struct Rejected {
		const char *source;
		std::vector<ExpectedError> errors;
	};
	const std::vector<Rejected> rejected = {
		{ "const BASE = [1]\nvar probe_expression = BASE[true]\n", { { "Cannot get index \"true\" from \"[1]\".", 2, 29 } } },
		{ "var probe_expression = [1][true]\n", { { "Invalid index type \"bool\" for a base of type \"Array\".", 1, 28 } } },
		{ "const VALUE = (1, 2)[-1]\nvar probe_expression = VALUE\n", { { "Assigned value for constant \"VALUE\" isn't a constant expression.", 1, 15 }, { "Tuple index -1 is out of range for \"(int, int)\", which has 2 element(s).", 1, 22 } } },
		{ "const VALUE = (1, 2)[0.0]\nvar probe_expression = VALUE\n", { { "Assigned value for constant \"VALUE\" isn't a constant expression.", 1, 15 }, { "Only an integer can index tuple \"(int, int)\", but received \"float\".", 1, 22 } } },
		{ "var probe_expression = [(1, 2)[-1]]\n", { { "Tuple index -1 is out of range for \"(int, int)\", which has 2 element(s).", 1, 32 } } },
	};
	for (const auto &sample : rejected) {
		const String path = "res://tests/review_rejected_subscript.barista";
		const auto result = analyze_source(sample.source, path);
		const auto *expression = find_expression(result);
		BS_TEST_REQUIRE(expression != nullptr);
		CHECK_FALSE(result.valid());
		CHECK_FALSE(expression->is_constant);
		CHECK((expression->is_constant ? expression->reduced_value : Variant()).get_type() == Variant::NIL);
		const Dictionary report = public_validate(sample.source, path, false);
		CHECK_FALSE(bool(report.get("valid", true)));
		check_public_errors(report, sample.errors);
	}
	struct Packed {
		const char *source;
		const char *datatype;
		Variant key;
	};
	const std::vector<Packed> packed = {
		{ "const VALUE: Array[PackedInt32Array] = [[1, 2]]\nvar probe_expression = VALUE\n", "Array[PackedInt32Array]", 0 },
		{ "const VALUE: (PackedInt32Array, int) = ([1, 2], 3)\nvar probe_expression = VALUE\n", "(PackedInt32Array, int)", 0 },
		{ "const VALUE: Dictionary[String, PackedInt32Array] = {\"x\": [1, 2]}\nvar probe_expression = VALUE\n", "Dictionary[String, PackedInt32Array]", "x" },
		{ "const VALUE: (PackedInt32Array, int) = ([1, 2], 3)\nvar probe_expression = VALUE.0\n", "PackedInt32Array", Variant() },
		{ "var probe_expression = [1, 2] as PackedInt32Array\n", "PackedInt32Array", Variant() },
	};
	PackedInt32Array expected;
	expected.push_back(1);
	expected.push_back(2);
	for (const auto &sample : packed) {
		const String path = "res://tests/review_packed_carrier.barista";
		const auto result = analyze_source(sample.source, path);
		const auto *expression = find_expression(result);
		BS_TEST_REQUIRE(expression != nullptr);
		CHECK(result.valid());
		CHECK(expression->is_constant);
		CHECK(expression->get_datatype().to_string() == sample.datatype);
		check_public_success(public_validate(sample.source, path));
		Variant selected = expression->reduced_value;
		if (sample.key.get_type() != Variant::NIL) {
			if (selected.get_type() == Variant::ARRAY) {
				const Array values = selected;
				CHECK(values.is_read_only());
				const int key = sample.key;
				BS_TEST_REQUIRE(key >= 0 && key < values.size());
				selected = values[key];
			} else {
				BS_TEST_REQUIRE(selected.get_type() == Variant::DICTIONARY);
				const Dictionary values = selected;
				CHECK(values.is_read_only());
				selected = values.get(sample.key, Variant());
			}
		}
		CHECK(selected.get_type() == Variant::PACKED_INT32_ARRAY);
		CHECK(selected == Variant(expected));
	}
	const auto runtime = analyze_source("func source_value() -> Array:\n\treturn [1, 2]\nvar probe_expression = [source_value()]\n", "res://tests/review_runtime_child.barista");
	const auto *runtime_expression = find_expression(runtime);
	BS_TEST_REQUIRE(runtime_expression != nullptr);
	CHECK(runtime.valid());
	CHECK_FALSE(runtime_expression->is_constant);
	CHECK((runtime_expression->is_constant ? runtime_expression->reduced_value : Variant()).get_type() == Variant::NIL);
	check_public_success(public_validate("func test():\n\tconst PACKED: Variant = [1, 2] as PackedInt32Array\n\tconst VALUE: Array = PACKED\n\tconst NESTED: Array[PackedInt32Array] = [[1, 2]]\n\tprint(VALUE, NESTED)\n", "res://tests/review_local_conversion.barista"));
}

Dictionary dictionary_value(std::initializer_list<std::pair<Variant, Variant>> entries) {
	Dictionary result;
	for (const auto &entry : entries) {
		result[entry.first] = entry.second;
	}
	return result;
}

// Compare the legacy recursive carrier contract directly, without serializing AST observations.
void check_readonly_carrier(const Variant &actual, const Variant &expected, int depth = 0) {
	BS_TEST_REQUIRE(depth < 32);
	BS_TEST_REQUIRE(actual.get_type() == expected.get_type());
	if (actual.get_type() == Variant::ARRAY) {
		const Array values = actual;
		const Array wanted = expected;
		CHECK(values.is_read_only());
		BS_TEST_REQUIRE(values.size() == wanted.size());
		for (int i = 0; i < values.size(); ++i) {
			check_readonly_carrier(values[i], wanted[i], depth + 1);
		}
	} else if (actual.get_type() == Variant::DICTIONARY) {
		const Dictionary values = actual;
		const Dictionary wanted = expected;
		CHECK(values.is_read_only());
		BS_TEST_REQUIRE(values.size() == wanted.size());
		const Array keys = values.keys();
		const Array wanted_keys = wanted.keys();
		for (int i = 0; i < keys.size(); ++i) {
			check_readonly_carrier(keys[i], wanted_keys[i], depth + 1);
			check_readonly_carrier(values[keys[i]], wanted[wanted_keys[i]], depth + 1);
		}
	} else {
		CHECK(actual == expected);
	}
}

void scenario_constant_producer_child_evidence() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	settings.warnings_enabled(true);
	settings.warning(BSWarning::REDUNDANT_AWAIT, BSWarning::WARN);
	struct ProducerCase {
		const char *name;
		const char *source;
		const char *datatype;
		bool valid;
		bool constant;
		bool hard;
		Variant carrier;
		std::vector<ExpectedError> errors;
		std::vector<ExpectedWarning> warnings;
	};
	const std::vector<ProducerCase> cases = {
		{ "nested", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = [Message.Data(KEYS)]\nvar probe_expression = BOX[0][1][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "direct", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = Message.Data(KEYS)\nvar probe_expression = BOX[1][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "selected", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = [Message.Data(KEYS)]\nvar probe_expression = BOX[0][1]\n", "Dictionary[float?, int?]", true, true, true, dictionary_value({ { Variant(), Variant() }, { Variant(double(1)), Variant(int64_t(2)) } }), {}, {} },
		{ "null", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = [Message.Data(KEYS)]\nvar probe_expression = BOX[0][1][null]\n", "null", true, true, true, Variant(), {}, {} },
		{ "variant_alias", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX: Variant = Message.Data(KEYS)\nvar probe_expression = BOX[1][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "direct_variant", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX: Variant = Message.Data(KEYS)\nvar probe_expression = BOX\n", "Variant", true, true, true, array_value({ Variant(int64_t(0)), dictionary_value({ { Variant(), Variant() }, { Variant(double(1)), Variant(int64_t(2)) } }) }), {}, {} },
		{ "array_selected", "enum Message:\n\tData(value: Variant)\nconst VALUES: Array[int?] = [null, 1]\nconst BOX = [Message.Data(VALUES)]\nvar probe_expression = BOX[0][1]\n", "Array[int?]", true, true, true, array_value({ Variant(), Variant(int64_t(1)) }), {}, {} },
		{ "array_null", "enum Message:\n\tData(value: Variant)\nconst VALUES: Array[int?] = [null, 1]\nconst BOX = [Message.Data(VALUES)]\nvar probe_expression = BOX[0][1][0]\n", "null", true, true, true, Variant(), {}, {} },
		{ "array_integer", "enum Message:\n\tData(value: Variant)\nconst VALUES: Array[int?] = [null, 1]\nconst BOX = [Message.Data(VALUES)]\nvar probe_expression = BOX[0][1][1]\n", "int", true, true, true, Variant(int64_t(1)), {}, {} },
		{ "nested_enum", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = [Message.Data(Message.Data(KEYS))]\nvar probe_expression = BOX[0][1][1][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "enum_in_tuple", "enum Message:\n\tData(value: Variant)\ntuple Pair(value: Variant, count: int)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = [Pair(Message.Data(KEYS), 0)]\nvar probe_expression = BOX[0][0][1][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "tuple_in_enum", "enum Message:\n\tData(value: Variant)\ntuple Pair(value: Variant, count: int)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = [Message.Data(Pair(KEYS, 0))]\nvar probe_expression = BOX[0][1][0][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "dict_value_path", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = {\"items\": [Message.Data({\"inner\": KEYS})]}\nvar probe_expression = BOX[\"items\"][0][1][\"inner\"][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "dict_key_value_path", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = [Message.Data({KEYS: KEYS})]\nvar probe_expression = BOX[0][1][KEYS][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "concat", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = [Message.Data(KEYS)] + []\nvar probe_expression = BOX[0][1][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "ternary", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = [Message.Data(KEYS)] if true else []\nvar probe_expression = BOX[0][1][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "cast", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = [Message.Data(KEYS)] as Array[Variant]\nvar probe_expression = BOX[0][1][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "raw_before_typed", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst RAW: Dictionary = KEYS\nconst BOX = [Message.Data([RAW, KEYS])]\nvar probe_expression = BOX[0][1][0][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "equal_raw", "enum Message:\n\tData(known: Variant, raw: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst RAW = {null: null, 1.0: 2}\nconst BOX = [Message.Data(KEYS, RAW)]\nvar probe_expression = BOX[0][2][1]\n", "Variant", false, false, false, Variant(), {
																																																																											  { "Cannot get index \"1\" from \"{ <null>: <null>, 1.0: 2 }\".", 6, 34 },
																																																																									  },
				{} },
		{ "invalid_payload", "enum Message:\n\tData(value: int)\nvar probe_expression = Message.Data(\"bad\")\n", "repair4_invalid_payload.barista.Message", false, false, true, Variant(), {
																																																	{ "Invalid argument 1 for enum case \"Message.Data\": should be \"int\" but is \"String\".", 3, 37 },
																																															},
				{} },
		{ "failed_conversion", "enum Message:\n\tData(value: Variant)\nvar probe_expression = Message.Data([1, \"bad\"] as Array[int])\n", "repair4_failed_conversion.barista.Message", false, false, true, Variant(), {
																																																							   { "Cannot include a value of type \"String\" as \"int\".", 3, 41 },
																																																							   { "Cannot have an element of type \"String\" in an array of type \"Array[int]\".", 3, 41 },
																																																					   },
				{} },
		{ "nonconstant_payload", "enum Message:\n\tData(value: Variant)\nvar value: int = 1\nvar probe_expression = Message.Data(value)\n", "repair4_nonconstant_payload.barista.Message", true, false, true, Variant(), {}, {} },
		{ "typed_payload_nonbakeable", "enum Message:\n\tData(value: Dictionary[float?, int?])\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nvar probe_expression = Message.Data(KEYS)\n", "repair4_typed_payload_nonbakeable.barista.Message", true, false, true, Variant(), {}, {} },
		{ "actual_null", "enum Message:\n\tData(value: Variant)\nvar probe_expression = Message.Data(null)\n", "repair4_actual_null.barista.Message", true, true, true, array_value({ Variant(int64_t(0)), Variant() }), {}, {} },
		{ "partial_payload", "enum Message:\n\tData(value: Variant)\nvar probe_expression = Message.Data(\n", "<unresolved type>", false, false, false, Variant(), {
																																										   { "Expected expression as the function argument.", 3, 36 },
																																										   { "Expected closing \")\" after call arguments.", 3, 36 },
																																								   },
				{} },
		{ "wrong_arity", "enum Message:\n\tData(value: Variant)\nvar probe_expression = Message.Data()\n", "repair4_wrong_arity.barista.Message", false, false, true, Variant(), {
																																														 { "Enum case \"Message.Data\" expects 1 argument(s), but 0 were given.", 3, 24 },
																																												 },
				{} },
		{ "await_container", "const KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\n@warning_ignore(\"redundant_await\")\nconst BOX = await [KEYS]\nvar probe_expression = BOX[0][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "await_enum", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\n@warning_ignore(\"redundant_await\")\nconst BOX = await Message.Data(KEYS)\nvar probe_expression = BOX[1][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "class_attribute", "class Holder:\n\tconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = [Holder.KEYS]\nvar probe_expression = BOX[0][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "class_attribute_alias", "class Holder:\n\tconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\n\tconst VALUE: Variant = [KEYS]\nconst BOX = Holder.VALUE\nvar probe_expression = BOX[0][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "self_attribute", "const KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst VALUE: Variant = [KEYS]\nconst BOX = self.VALUE\nvar probe_expression = BOX[0][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "utility_scalar", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nvar probe_expression = len(Message.Data(KEYS))\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "singleton", "enum Message:\n\tEmpty\n\tData(value: Variant)\nvar probe_expression = Message.Empty\n", "repair4_singleton.barista.Message", true, true, true, array_value({ Variant(int64_t(0)) }), {}, {} },
		{ "contextual_enum", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX: Message = .Data(KEYS)\nvar probe_expression = BOX[1][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "await_warning", "const KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = await [KEYS]\nvar probe_expression = BOX[0][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {
																																																				  { BSWarning::REDUNDANT_AWAIT, "\"await\" keyword is unnecessary because the expression isn't a coroutine nor a signal.", 2, 13, 2, 25 },
																																																		  } },
		{ "await_dynamic", "var value: Variant = [1]\nvar probe_expression = await value\n", "Variant", true, false, true, Variant(), {}, {} },
		{ "await_coroutine", "var value: Coroutine[Dictionary[float?, int?]]\nvar probe_expression = await value\n", "Dictionary[float?, int?]", true, false, true, Variant(), {}, {} },
		{ "attribute_direct_variant", "class Holder:\n\tconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\n\tconst VALUE: Variant = [KEYS]\nvar probe_expression = Holder.VALUE\n", "Variant", true, true, true, array_value({ dictionary_value({ { Variant(), Variant() }, { Variant(double(1)), Variant(int64_t(2)) } }) }), {}, {} },
		{ "attribute_equal_raw", "class Holder:\n\tconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\n\tconst RAW = {null: null, 1.0: 2}\n\tconst VALUE: Variant = [KEYS, RAW]\nconst BOX = Holder.VALUE\nvar probe_expression = BOX[1][1]\n", "Variant", false, false, false, Variant(), {
																																																																											{ "Cannot get index \"1\" from \"{ <null>: <null>, 1.0: 2 }\".", 6, 31 },
																																																																									},
				{} },
		{ "dictionary_key_child", "enum Message:\n\tData(value: Variant)\nconst KEYS: Dictionary[float?, int?] = {null: null, 1.0: 2}\nconst BOX = {KEYS: Message.Data(KEYS)}\nvar probe_expression = BOX[KEYS][1][1]\n", "int", true, true, true, Variant(int64_t(2)), {}, {} },
		{ "await_scalar_warning", "var probe_expression = await 1\n", "int", true, true, true, Variant(int64_t(1)), {}, {
																																{ BSWarning::REDUNDANT_AWAIT, "\"await\" keyword is unnecessary because the expression isn't a coroutine nor a signal.", 1, 24, 1, 31 },
																														} },
	};
	CHECK(int(BSWarning::REDUNDANT_AWAIT) == 27);
	for (const auto &sample : cases) {
		INFO(sample.name);
		const String path = vformat("res://tests/repair4_%s.barista", sample.name);
		const auto observed = analyze_source(sample.source, path);
		const auto *expression = find_expression(observed);
		BS_TEST_REQUIRE(expression != nullptr);
		CHECK(observed.valid() == sample.valid);
		CHECK(expression->get_datatype().to_string() == sample.datatype);
		CHECK(expression->get_datatype().is_hard_type() == sample.hard);
		CHECK(expression->is_constant == sample.constant);
		check_readonly_carrier(expression->is_constant ? expression->reduced_value : Variant(), sample.carrier);
		const Dictionary report = public_validate(sample.source, path);
		CHECK(bool(report.get("valid", false)) == sample.valid);
		check_public_errors(report, sample.errors);
		const Array errors = report.get("errors", Array());
		for (const Dictionary &error : errors) {
			CHECK(error.size() == 4);
			CHECK(String(error.get("path", "")) == path);
		}
		const Array warnings = report.get("warnings", Array());
		BS_TEST_REQUIRE(size_t(warnings.size()) == sample.warnings.size());
		for (int i = 0; i < warnings.size(); ++i) {
			const Dictionary warning = warnings[i];
			const auto &expected = sample.warnings[i];
			CHECK(warning.size() == 7);
			CHECK(int(warning.get("code", -1)) == int(expected.code));
			CHECK(String(warning.get("string_code", "")) == "REDUNDANT_AWAIT");
			CHECK(String(warning.get("message", "")) == expected.message.c_str());
			CHECK(int(warning.get("start_line", -1)) == expected.start_line);
			CHECK(int(warning.get("start_column", -1)) == expected.start_column);
			CHECK(int(warning.get("end_line", -1)) == expected.end_line);
			CHECK(int(warning.get("end_column", -1)) == expected.end_column);
		}
		CHECK(source_analyzes(sample.source, path) == sample.valid);
		CHECK(public_validate(sample.source, path) == report);
	}
}

} // namespace

TEST_SUITE("analyzer_constants") {
	TEST_CASE("constant_producer_child_evidence") { scenario_constant_producer_child_evidence(); }
	TEST_CASE("pure_constant_review_regressions") { scenario_pure_constant_review_regressions(); }
	TEST_CASE("dictionary_literal_constant_parity") { scenario_dictionary_literal_constant_parity(); }
	TEST_CASE("constant_dictionary_key_conversion") { scenario_constant_dictionary_key_conversion(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_constant_dictionary_key_conversion, scenario_dictionary_literal_constant_parity, scenario_pure_constant_review_regressions, scenario_constant_producer_child_evidence });
	}
}
