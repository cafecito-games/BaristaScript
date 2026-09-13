/**************************************************************************/
/*  analyzer_resolution_test.cpp                                          */
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

// Complete scenario sources and predicates from merged main 4c9c561 (PR #201).
namespace {
void scenario_review_resolution_regressions() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	struct ValidCase {
		const char *label;
		const char *source;
	};
	const ValidCase valid_cases[] = {
		{ "global_constant", "func test():\n\tprint(OK)\n" },
		{ "utility_value", "func test():\n\tvar fn = print\n" },
		{ "native_property", "extends Node\nfunc test():\n\tvar value: StringName = name\n" },
		{ "native_method", "extends Node\nfunc test():\n\tvar fn: Callable = get_parent\n" },
		{ "native_signal", "extends Node\nfunc test():\n\tvar event: Signal = tree_entered\n" },
		{ "native_constant", "extends Node\nfunc test():\n\tvar mode: int = PROCESS_MODE_DISABLED\n" },
		{ "outer_member", "const VALUE = 1\nclass Inner:\n\tfunc test():\n\t\tvar value: int = VALUE\n" },
		{ "nested_class", "class Inner:\n\tpass\nfunc test():\n\tvar cls = Inner\n" },
		{ "packed_argument", "annotation items(values: PackedInt32Array) targets METHOD\n@items([1, 2])\nfunc test():\n\tpass\n" },
		{ "packed_default", "annotation items(values: PackedInt32Array = [1, 2]) targets METHOD\n@items\nfunc test():\n\tpass\n" },
		{ "empty_callable", "const EMPTY = Callable()\n" },
		{ "empty_signal", "const EMPTY = Signal()\n" },
		{ "copy_callable", "const EMPTY = Callable()\nconst COPY = Callable(EMPTY)\n" },
		{ "copy_signal", "const EMPTY = Signal()\nconst COPY = Signal(EMPTY)\n" },
		{ "carrier_annotation", "annotation empty(value: Callable, event: Signal = Signal()) targets METHOD\n@empty(Callable())\nfunc test():\n\tpass\n" },
		{ "carrier_default", "func test(value: Callable = Callable(), event: Signal = Signal()):\n\tpass\n" },
		{ "alias_owner", "class Inner:\n\ttype Scalar = String\n\tvar first: Value = 1.5\ntype Value = Scalar\ntype Scalar = float\nvar subsequent: Value = 2.5\n" },
		{ "alias_chain", "class Inner:\n\ttype Scalar = String\n\tvar first: Value = 1.5\ntype Value = Middle\ntype Middle = Scalar\ntype Scalar = float\nvar subsequent: Value = 2.5\n" },
		{ "alias_order", "type Scalar = float\ntype Value = Scalar\nclass Inner:\n\ttype Scalar = String\n\tvar first: Value = 1.5\nvar subsequent: Value = 2.5\n" },
	};
	for (const auto &sample : valid_cases) {
		INFO(sample.label);
		const auto result = analyze_source(sample.source, vformat("res://tests/review_%s.barista", sample.label));
		CHECK(result.valid());
	}
	const auto member = analyze_source("var len: Callable[[String], String]\nfunc test() -> String:\n\treturn len(\"abc\")\n", "res://tests/review_member_value_call.barista");
	CHECK_FALSE(member.valid());
	BS_TEST_REQUIRE(member.parser != nullptr);
	BS_TEST_REQUIRE(member.parser->get_errors().size() == 1);
	CHECK(member.parser->get_errors().front()->get().message == "Name \"len\" is a Callable. You can call it with \"len.call()\" instead.");

	for (const String &callee : { String("missing_ident"), String("Only") }) {
		for (const char *expression : { "%s()", "var value = %s()", "return %s()", "print(%s())" }) {
			INFO(expression);
			const String prefix = callee == "Only" ? String("type Only = int\n") : String();
			const String source = prefix + String("func test():\n\t") + vformat(expression, callee) + String("\n");
			const auto result = analyze_source(source, "res://tests/review_call.barista");
			CHECK_FALSE(result.valid());
			BS_TEST_REQUIRE(result.parser != nullptr);
			BS_TEST_REQUIRE(result.parser->get_errors().size() == 1);
			const String diagnostic = callee == "Only" ? "Type alias \"Only\" can only be used in a type position." : "Identifier \"missing_ident\" not declared in the current scope.";
			CHECK(result.parser->get_errors().front()->get().message.begins_with(diagnostic));
		}
	}
	CHECK_FALSE(analyze_source("func test():\n\tvar value = missing_value\n", "res://tests/review_missing_value.barista").valid());
	for (const char *source : {
				 "var outer_value: int\nclass Inner:\n\tfunc test():\n\t\tprint(outer_value)\n",
				 "signal outer_event\nclass Inner:\n\tfunc test():\n\t\tvar event = outer_event\n",
				 "static func outer() -> int:\n\treturn 1\nclass Inner:\n\tfunc test():\n\t\tvar value = outer\n",
				 "extends Node\nclass Inner:\n\tfunc test():\n\t\tvar value = name\n",
		 }) {
		CHECK_FALSE(analyze_source(source, "res://tests/review_outer_boundary.barista").valid());
	}
	for (const char *source : {
				 "const BAD = Callable(1)\n",
				 "const BAD = Signal(1)\n",
				 "extends Node\nconst BAD = Callable(self, \"get_parent\")\n",
				 "extends Node\nconst BAD = Signal(self, \"tree_entered\")\n",
		 }) {
		CHECK_FALSE(analyze_source(source, "res://tests/review_unsafe_constant.barista").valid());
	}
	for (const char *expression : { "Callable()", "Callable(Callable())", "Signal()", "Signal(Signal())" }) {
		const auto result = analyze_source(String("var probe_expression = ") + expression + String("\n"), "res://tests/fold_probe.barista");
		CHECK(result.valid());
		const auto *value = find_expression(result);
		BS_TEST_REQUIRE(value != nullptr);
		CHECK(value->is_constant);
		const Variant::Type expected = String(expression).begins_with("Callable") ? Variant::CALLABLE : Variant::SIGNAL;
		CHECK(value->reduced_value.get_type() == expected);
	}
	for (const String &carrier : { String("PackedByteArray"), String("PackedInt32Array"), String("PackedInt64Array"), String("PackedFloat32Array"), String("PackedFloat64Array"), String("PackedStringArray") }) {
		const String values = carrier == "PackedStringArray" ? "[\"one\", \"two\"]" : "[1, 2]";
		for (const bool use_default : { false, true }) {
			const String parameter = String("value: ") + carrier + (use_default ? String(" = ") + values : String());
			const String annotation = use_default ? String("@items") : String("@items(") + values + ")";
			const String source = String("annotation items(") + parameter + ") targets METHOD\n" + annotation + "\nfunc test():\n\tpass\n";
			CHECK(analyze_source(source, "res://tests/review_conversion.barista").valid());
		}
	}
}
} // namespace

TEST_SUITE("analyzer_resolution") {
	TEST_CASE("review_resolution_regressions") { scenario_review_resolution_regressions(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_review_resolution_regressions });
	}
}
