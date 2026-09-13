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
} // namespace

TEST_SUITE("analyzer_constants") {
	TEST_CASE("dictionary_literal_constant_parity") { scenario_dictionary_literal_constant_parity(); }
	TEST_CASE("constant_dictionary_key_conversion") { scenario_constant_dictionary_key_conversion(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_constant_dictionary_key_conversion, scenario_dictionary_literal_constant_parity });
	}
}
