/**************************************************************************/
/*  analyzer_resolution_test.cpp                                          */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                          */
/*  This file is part of BaristaScript, a Godot GDExtension.                */
/*  SPDX-License-Identifier: MIT                                           */
/**************************************************************************/

#include "analyzer_helpers.h"
#include "bs_build_info.h"
#include "bs_conformance_registry.h"
#include "bs_utility_functions.h"
#include "storage_fixture.h"
#include "test_require.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>

#include <cmath>
#include <limits>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

// Complete scenario sources and predicates from merged main 4c9c561 (PR #201).
namespace {
bool contains_analyzer_utility(const Variant &value, int depth = 0) {
	if (depth > 64) {
		return true;
	}
	if (value.get_type() == Variant::CALLABLE) {
		return BSUtilityFunctions::is_analyzer_callable(value);
	}
	if (value.get_type() == Variant::ARRAY) {
		for (const Variant &child : Array(value)) {
			if (contains_analyzer_utility(child, depth + 1)) {
				return true;
			}
		}
	} else if (value.get_type() == Variant::DICTIONARY) {
		const Dictionary dictionary = value;
		return contains_analyzer_utility(dictionary.keys(), depth + 1) || contains_analyzer_utility(dictionary.values(), depth + 1);
	}
	return false;
}

void scenario_language_utility_registry() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	struct UtilityCase {
		const char *name;
		const char *expression;
		const char *result_type;
	};
	const std::vector<UtilityCase> calls = {
		{ "type_exists", "type_exists(\"Node\")", "bool" },
		{ "char", "char(65)", "String" },
		{ "ord", "ord(\"A\")", "int" },
		{ "range", "range(1, 4, 1)", "Array" },
		{ "load", "load(\"res://does_not_exist.barista\")", "Resource" },
		{ "print_debug", "print_debug(\"test\", 1)", "void" },
		{ "print_stack", "print_stack()", "void" },
		{ "get_stack", "get_stack()", "Array" },
		{ "len", "len(\"abc\")", "int" },
		{ "is_instance_of", "is_instance_of(1, TYPE_INT)", "bool" },
		{ "create_proxy_dynamic", "create_proxy_dynamic(Object, Callable())", "Object" },
	};
	for (const char *source : { "char", "[char]", "Callable(char)", "{\"callback\": char}", "{char: \"callback\"}" }) {
		const auto result = analyze_source(String("var probe_expression = ") + source + String("\n"), "res://tests/fold_probe.barista");
		const auto *expression = find_expression(result);
		// Same exportability predicate as the old fold observer, without its Dictionary carrier.
		const bool exportable = result.valid() && expression != nullptr && expression->is_constant &&
				!contains_analyzer_utility(expression->reduced_value);
		CHECK_FALSE(exportable);
	}
	const List<MethodInfo> source = BSUtilityFunctions::get_function_list();
	const List<MethodInfo> roundtrip = BaristaScriptLanguage::get_public_function_list();
	CHECK(source.size() == roundtrip.size());
	for (const MethodInfo &info : source) {
		bool found = false;
		for (const MethodInfo &copy : roundtrip) {
			if (copy.name == info.name) {
				// Compare the actual public MethodInfo serialization, preserving every field.
				found = Dictionary(copy) == Dictionary(info);
				break;
			}
		}
		CHECK(found);
		const Callable identity = BSUtilityFunctions::make_analyzer_callable(info.name);
		CHECK_FALSE(identity.is_valid());
		CHECK(BSUtilityFunctions::is_analyzer_callable(identity));
		for (const MethodInfo &other : source) {
			const Callable other_identity = BSUtilityFunctions::make_analyzer_callable(other.name);
			CHECK((identity == other_identity) == (info.name == other.name));
		}
	}
	const TypedArray<Dictionary> functions = BaristaScriptLanguage::get_singleton()->_get_public_functions();
	CHECK(size_t(functions.size()) == calls.size());
	for (const Dictionary &info : functions) {
		const String name = info["name"];
		bool known = false;
		for (const auto &call : calls) {
			known = known || name == call.name;
		}
		CHECK(known);
		CHECK(info.has("args"));
		CHECK(info.has("return"));
		CHECK(info.has("flags"));
		CHECK(info.has("default_args"));
		const bool constant = name == "type_exists" || name == "char" || name == "ord" || name == "len" || name == "is_instance_of";
		CHECK(BSUtilityFunctions::is_function_constant(name) == constant);
	}
	struct FoldCase {
		const char *expression;
		Variant expected;
	};
	const std::vector<FoldCase> folded = {
		{ "len(\"abc\")", Variant(3) },
		{ "char(65)", Variant("A") },
		{ "ord(\"A\")", Variant(65) },
		{ "type_exists(\"Node\")", Variant(true) },
		{ "is_instance_of(1, TYPE_INT)", Variant(true) },
		{ "is_instance_of(1, TYPE_STRING)", Variant(false) },
		{ "len([1, 2])", Variant(2) },
		{ "len({\"x\": 1})", Variant(1) },
		{ "len({})", Variant(0) },
		{ "len({\"x\": {\"y\": 1}, \"z\": 2})", Variant(2) },
	};
	for (const auto &sample : folded) {
		const auto result = analyze_source(String("var probe_expression = ") + sample.expression + String("\n"), "res://tests/fold_probe.barista");
		CHECK(result.valid());
		const auto *expression = find_expression(result);
		BS_TEST_REQUIRE(expression != nullptr);
		CHECK(expression->is_constant);
		CHECK(expression->reduced_value == sample.expected);
	}
	for (const auto &call : calls) {
		INFO(call.name);
		std::vector<String> contexts = {
			vformat("func test():\n\t%s\n", call.expression),
			vformat("func test():\n\tvar utility: Callable = %s\n", call.name),
			vformat("const UTILITY = %s\n", call.name),
		};
		if (String(call.result_type) != "void") {
			contexts.push_back(vformat("func test():\n\tvar value: %s = %s\n", call.result_type, call.expression));
			contexts.push_back(vformat("func test() -> %s:\n\treturn %s\n", call.result_type, call.expression));
			contexts.push_back(vformat("func test():\n\tprint(%s)\n", call.expression));
		}
		for (const String &context : contexts) {
			CHECK(analyze_source(context, "res://tests/language_utility.barista").valid());
		}
	}
	struct InvalidCase {
		const char *label;
		const char *expression;
		const char *diagnostic;
	};
	const InvalidCase invalid[] = {
		{ "type_exists_arity", "type_exists()", "Too few arguments" },
		{ "type_exists_type", "type_exists(1)", "Invalid argument" },
		{ "char_arity", "char(65, 66)", "Too many arguments" },
		{ "char_type", "char(\"A\")", "Invalid argument" },
		{ "ord_arity", "ord()", "Too few arguments" },
		{ "ord_type", "ord(65)", "Invalid argument" },
		{ "load_arity", "load()", "Too few arguments" },
		{ "load_type", "load(1)", "Invalid argument" },
		{ "print_stack_arity", "print_stack(1)", "Too many arguments" },
		{ "get_stack_arity", "get_stack(1)", "Too many arguments" },
		{ "len_arity", "len()", "Too few arguments" },
		{ "len_constant_type", "len(1)", "Invalid argument" },
		{ "instance_arity", "is_instance_of(1)", "Too few arguments" },
		{ "instance_constant_type", "is_instance_of(1, \"int\")", "Invalid argument" },
		{ "proxy_arity", "create_proxy_dynamic(Object)", "Too few arguments" },
		{ "proxy_type", "create_proxy_dynamic(1, Callable())", "Invalid argument" },
		{ "proxy_handler", "create_proxy_dynamic(Object, 1)", "Invalid argument" },
		{ "named", "char(code = 65)", "Named arguments" },
	};
	for (const auto &sample : invalid) {
		INFO(sample.label);
		const auto result = analyze_source(vformat("func test():\n\t%s\n", sample.expression), "res://tests/language_utility_bad.barista");
		CHECK_FALSE(result.valid());
		BS_TEST_REQUIRE(result.parser != nullptr);
		BS_TEST_REQUIRE(result.parser->get_errors().size() == 1);
		CHECK(result.parser->get_errors().front()->get().message.contains(sample.diagnostic));
	}
	struct SourceCase {
		const char *label;
		const char *source;
	};
	const SourceCase positives[] = {
		{ "typed_value", "func test():\n\tvar cb: Callable[[int], String] = char\n\tvar text: String = cb.call(65)\n" },
		{ "inferred_value", "func test():\n\tvar cb := ord\n\tvar code: int = cb.call(\"A\")\n" },
		{ "bound_value", "func test():\n\tvar cb := char.bind(65)\n\tvar text: String = cb.call()\n" },
		{ "callv", "func test():\n\tvar cb := ord\n\tvar code: int = cb.callv([\"A\"])\n" },
		{ "local_shadow", "func test(len: Callable[[String], String]) -> String:\n\treturn len(\"abc\")\n" },
		{ "local_over_member", "func len(value: int) -> int:\n\treturn value\nfunc test(len: Callable[[String], String]) -> String:\n\treturn len(\"abc\")\n" },
		{ "member_shadow", "func len(value: String) -> String:\n\treturn value\nfunc test() -> String:\n\treturn len(\"abc\")\n" },
		{ "member_value_shadow", "var len: Callable[[String], String]\nfunc test() -> String:\n\treturn len.call(\"abc\")\n" },
		{ "constants", "const COUNT = len(\"abc\")\nconst CHARACTER = char(65)\nconst CODE = ord(CHARACTER)\nconst EXISTS = type_exists(\"Node\")\nconst MATCHES = is_instance_of(CODE, TYPE_INT)\n" },
		{ "annotation", "annotation number(value: int = len(\"abc\")) targets METHOD\n@number(ord(\"A\"))\nfunc test(value: String = char(65)):\n\tpass\n" },
		{ "utility_annotation", "annotation callback(value: Callable = len) targets METHOD\n@callback(ord)\nfunc test(cb: Callable = char):\n\tpass\n" },
		{ "range_loop", "func test():\n\tfor value in range(3):\n\t\tvar number: int = value\n" },
		{ "shadowed_range_loop", "func test(range: Callable[[], Array]):\n\tfor value in range():\n\t\tpass\n" },
		{ "runtime_range", "func test():\n\tvar values: Array = range()\n\tvar other: Array = range(\"runtime check\")\n" },
		{ "runtime_len", "func test(value):\n\tvar count: int = len(value)\n" },
		{ "runtime_dictionary_len", "func test(value):\n\tvar count: int = len({\"x\": value})\n" },
		{ "constant_dictionary_len", "const N = len({\"x\": 1})\n" },
		{ "runtime_instance", "func test(value, type):\n\tvar matches: bool = is_instance_of(value, type)\n" },
		{ "debug_vararg", "func test():\n\tprint_debug()\n\tprint_debug(1, \"x\", null)\n" },
	};
	for (const auto &sample : positives) {
		INFO(sample.label);
		CHECK(analyze_source(sample.source, "res://tests/language_utility_context.barista").valid());
	}
	const SourceCase negatives[] = {
		{ "callable_arity", "func test():\n\tvar cb := char\n\tcb.call()\n" },
		{ "callable_type", "func test():\n\tvar cb := ord\n\tcb.call(65)\n" },
		{ "bound_type", "func test():\n\tvar cb := char.bind(\"A\")\n" },
		{ "callv_type", "func test():\n\tvar cb := ord\n\tcb.callv([65])\n" },
		{ "void_initializer", "func test():\n\tvar value = print_stack()\n" },
		{ "void_nested", "func test():\n\tprint(print_debug())\n" },
		{ "char_domain", "const BAD = char(-1)\n" },
		{ "ord_domain", "const BAD = ord(\"abc\")\n" },
		{ "instance_domain", "const BAD = is_instance_of(1, -1)\n" },
		{ "range_loop_arity", "func test():\n\tfor value in range():\n\t\tpass\n" },
		{ "range_loop_many", "func test():\n\tfor value in range(1, 2, 3, 4):\n\t\tpass\n" },
		{ "range_iterator_type", "func test():\n\tfor value in range(3):\n\t\tvar text: String = value\n" },
		{ "nonconstant_range", "const BAD = range(3)\n" },
		{ "nonconstant_load", "const BAD = load(\"res://does_not_exist.barista\")\n" },
		{ "nonconstant_stack", "const BAD = get_stack()\n" },
		{ "nonconstant_proxy", "const BAD = create_proxy_dynamic(Object, Callable())\n" },
		{ "nonconstant_dictionary_len", "var value = 1\nconst BAD = len({\"x\": value})\n" },
	};
	for (const auto &sample : negatives) {
		INFO(sample.label);
		const auto result = analyze_source(sample.source, "res://tests/language_utility_negative.barista");
		CHECK_FALSE(result.valid());
		BS_TEST_REQUIRE(result.parser != nullptr);
		for (const auto &error : result.parser->get_errors()) {
			CHECK_FALSE(error.message.contains("not declared"));
		}
	}
	for (const char *expression : { "missing_language_utility()", "len(missing_language_utility())" }) {
		const auto result = analyze_source(vformat("func test():\n\t%s\n", expression), "res://tests/language_utility_unknown.barista");
		BS_TEST_REQUIRE(result.parser != nullptr);
		BS_TEST_REQUIRE(result.parser->get_errors().size() == 1);
		CHECK(result.parser->get_errors().front()->get().message == "Identifier \"missing_language_utility\" not declared in the current scope.");
	}
}

void scenario_pinned_global_api_lookup() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	// Build configuration selects the file only. Expected symbols and values come from
	// the raw pinned producer JSON, not generated analyzer/implementation metadata.
	const String api_version = bs_get_build_info()["godot_api"];
	const String path = String("res://../godot-cpp/gdextension/extension_api-") + api_version.replace(".", "-") + String(".json");
	const String raw = FileAccess::get_file_as_string(path);
	BS_TEST_REQUIRE(!raw.is_empty());
	const Variant parsed = JSON::parse_string(raw);
	BS_TEST_REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	const Dictionary api = parsed;
	BS_TEST_REQUIRE(api.has("global_constants") && api.has("global_enums") && api.has("utility_functions"));
	Array constants = Array(api["global_constants"]).duplicate();
	for (const Dictionary &enumeration : Array(api["global_enums"])) {
		constants.append_array(enumeration["values"]);
	}
	BS_TEST_REQUIRE(!constants.is_empty());
	for (const Dictionary &entry : constants) {
		const String name = entry["name"];
		INFO(std::string(name.utf8().get_data()));
		const auto result = analyze_source(String("var probe_expression = ") + name + String("\n"), "res://tests/fold_probe.barista");
		CHECK(result.valid());
		const auto *value = find_expression(result);
		BS_TEST_REQUIRE(value != nullptr);
		CHECK(value->is_constant);
		CHECK(value->reduced_value.get_type() == Variant::INT);
		// Godot JSON numbers use double; the legacy suite separately checks int64 limits.
		const double expected = entry["value"];
		if (std::abs(expected) < 9007199254740992.0) {
			CHECK(value->reduced_value == Variant(int64_t(expected)));
		}
	}
	for (const auto &limit : { std::pair<const char *, int64_t>{ "INT64_MAX", std::numeric_limits<int64_t>::max() },
				 std::pair<const char *, int64_t>{ "INT64_MIN", std::numeric_limits<int64_t>::min() } }) {
		const auto result = analyze_source(String("var probe_expression = ") + limit.first + String("\n"), "res://tests/fold_probe.barista");
		const auto *value = find_expression(result);
		BS_TEST_REQUIRE(value != nullptr);
		CHECK(value->is_constant);
		CHECK(value->reduced_value == Variant(limit.second));
	}
	const Array functions = api["utility_functions"];
	BS_TEST_REQUIRE(!functions.is_empty());
	for (const Dictionary &function : functions) {
		const String source = String("func test():\n\tvar utility: Callable = ") + String(function["name"]) + String("\n");
		CHECK(analyze_source(source, "res://tests/review_utility.barista").valid());
	}
	BSDeclarationIndex &index = fixture.index();
	index.clear();
	const String indexed_source = "class_name ReviewIndexed extends RefCounted\n";
	const String imported_source = "namespace review_scope\nclass_name ReviewImported extends RefCounted\n";
	auto *language = BaristaScriptLanguage::get_singleton();
	CHECK(language->synchronize_declaration_path_from_source("res://tests/review_indexed.barista", indexed_source) == OK);
	CHECK(language->synchronize_declaration_path_from_source("res://tests/review_imported.barista", imported_source) == OK);
	BSCache::set_source_override("res://tests/review_indexed.barista", indexed_source);
	BSCache::set_source_override("res://tests/review_imported.barista", imported_source);
	for (const char *source : { "func test():\n\tvar handle = ReviewIndexed\n", "import review_scope\nfunc test():\n\tvar handle = ReviewImported\n" }) {
		CHECK(analyze_source(source, "res://tests/review_handle.barista").valid());
	}
	BSCache::clear_source_override("res://tests/review_indexed.barista");
	BSCache::clear_source_override("res://tests/review_imported.barista");
	index.clear();
}

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
	TEST_CASE("language_utility_registry") { scenario_language_utility_registry(); }
	TEST_CASE("pinned_global_api_lookup") { scenario_pinned_global_api_lookup(); }
	TEST_CASE("review_resolution_regressions") { scenario_review_resolution_regressions(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_review_resolution_regressions, scenario_pinned_global_api_lookup, scenario_language_utility_registry });
	}
}
