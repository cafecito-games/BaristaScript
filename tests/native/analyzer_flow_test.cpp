/**************************************************************************/
/*  analyzer_flow_test.cpp                                                */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "analyzer_conformance_helpers.h"
#include "analyzer_helpers.h"
#include "bs_type.h"
#include "doctest.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
using Phase = BSAnalyzer::AnalyzerPhase;

struct SourceCase {
	const char *name;
	const char *source;
	std::vector<ExpectedError> errors;
	std::vector<ExpectedWarning> warnings;
	Phase phase = Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES;
	bool strict_null = false;
	bool strict_dynamic = false;
	bool check_warning_severity = false;
};

struct NodeTypeExpectation {
	const char *path;
	const char *type;
	int type_source = -1;
};

void check_node_type(const BSParser::FunctionNode *function, const NodeTypeExpectation &expected) {
	INFO(std::string(expected.path));
	const BSParser::Node *node = find_statement_node(function, expected.path);
	BS_TEST_REQUIRE(node != nullptr);
	CHECK(std::string(node->get_datatype().to_string().utf8().get_data()) == expected.type);
	if (expected.type_source >= 0) {
		CHECK(int(node->get_datatype().type_source) == expected.type_source);
	}
}

void check_case(StorageFixture &fixture, AnalyzerSettings &settings, const char *group, const SourceCase &test_case) {
	INFO(std::string(test_case.name));
	settings.strict_null(test_case.strict_null);
	settings.strict_dynamic(test_case.strict_dynamic);
	const String path = "res://tests/native/analyzer_flow/" + String(group) + String("_") + test_case.name + String(".barista");
	const AnalysisResult first = analyze_source(test_case.source, path);
	check_analysis(first, test_case.errors, test_case.warnings, test_case.phase);
	CHECK(source_analyzes(test_case.source, path) == test_case.errors.empty());
	const AnalysisResult repeated = analyze_source(test_case.source, path);
	check_analysis(repeated, test_case.errors, test_case.warnings, test_case.phase);
	if (test_case.check_warning_severity) {
		BS_TEST_REQUIRE(test_case.warnings.size() == 1);
		const ExpectedWarning &warning = test_case.warnings.front();
		settings.warning(warning.code, BSWarning::IGNORE);
		check_analysis(analyze_source(test_case.source, path), test_case.errors, {}, test_case.phase);

		settings.warning(warning.code, BSWarning::ERROR);
		std::vector<ExpectedError> errors = test_case.errors;
		errors.push_back({ warning.message + " (Warning treated as error.)", warning.start_line, warning.start_column });
		std::stable_sort(errors.begin(), errors.end(), [](const ExpectedError &left, const ExpectedError &right) {
			return left.line == right.line ? left.column < right.column : left.line < right.line;
		});
		check_analysis(analyze_source(test_case.source, path), errors, {}, test_case.phase);
		settings.warning(warning.code, BSWarning::WARN);
	}
	CHECK(fixture.index().get_record_count() == 0);
}

#define E(message, line, column) \
	ExpectedError { message, line, column }
#define W(code, message, start_line, start_column, end_line, end_column) \
	ExpectedWarning { BSWarning::code, message, start_line, start_column, end_line, end_column }
} // namespace

TEST_SUITE("analyzer_flow") {
	TEST_CASE("pinned_suite_exit_summary") {
		StorageFixture fixture;
		AnalyzerSettings settings;
		settings.warnings_enabled(true);
		settings.warning(BSWarning::UNREACHABLE_CODE, BSWarning::WARN);
		settings.warning(BSWarning::NON_EXHAUSTIVE_MATCH, BSWarning::WARN);
		settings.warning(BSWarning::OPEN_ENUM_MATCH_WITHOUT_DEFAULT, BSWarning::WARN);
		const SourceCase cases[] = {
			{ "while_true", "func f() -> int:\n\twhile true:\n\t\tpass\n", {}, {} },
			{ "while_truthy", "func f() -> int:\n\twhile 1:\n\t\tpass\n", {}, {} },
			{ "own_break", "func f() -> int:\n\twhile true:\n\t\tbreak\n", { E("Not all code paths return a value.", 1, 1) }, {} },
			{ "own_break_then_return", "func f() -> int:\n\twhile true:\n\t\tbreak\n\treturn 1\n", {}, {} },
			{ "conditional_break", "func f(flag: bool) -> int:\n\twhile true:\n\t\tif flag:\n\t\t\tbreak\n", { E("Not all code paths return a value.", 1, 1) }, {} },
			{ "nested_while_break", "func f() -> int:\n\twhile true:\n\t\twhile true:\n\t\t\tbreak\n", {}, {} },
			{ "nested_for_break", "func f() -> int:\n\twhile true:\n\t\tfor _i in range(1):\n\t\t\tbreak\n", {}, {} },
			{ "unreachable_break", "func f() -> int:\n\twhile true:\n\t\tpush_fatal(\"stop\")\n\t\tbreak\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 4, 9, 4, 14) } },
			{ "false_while", "func f() -> int:\n\twhile false:\n\t\tpass\n", { E("Not all code paths return a value.", 1, 1) }, {} },
			{ "unknown_while", "func f(flag: bool) -> int:\n\twhile flag:\n\t\tpass\n", { E("Not all code paths return a value.", 1, 1) }, {} },
			{ "for_no_guaranteed_exit", "func f() -> int:\n\tfor _i in range(1):\n\t\treturn 1\n", { E("Not all code paths return a value.", 1, 1) }, {} },
			{ "if_without_else", "func f(flag: bool) -> int:\n\tif flag:\n\t\treturn 1\n", { E("Not all code paths return a value.", 1, 1) }, {} },
			{ "generic_pass", "func f() -> int:\n\tpass\n", { E("Not all code paths return a value.", 1, 1) }, {} },
			{ "noreturn_pass", "@noreturn\nfunc f() -> void:\n\tpass\n", { E("A \"@noreturn\" function cannot complete normally.", 2, 1) }, {} },
			{ "noreturn_return", "@noreturn\nfunc f() -> void:\n\treturn\n", { E("A \"@noreturn\" function cannot return.", 2, 1) }, {} },
			{ "abort_unreachable_return", "@noreturn\nfunc f() -> void:\n\tpush_fatal(\"stop\")\n\treturn\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 4, 5, 4, 11) } },
			{ "for_reachable_return", "@noreturn\nfunc f() -> void:\n\tfor _i in range(1):\n\t\treturn\n\tpush_fatal(\"stop\")\n", { E("A \"@noreturn\" function cannot return.", 2, 1) }, {} },
			{ "for_unreachable_return", "@noreturn\nfunc f() -> void:\n\tfor _i in range(1):\n\t\tpush_fatal(\"stop\")\n\t\treturn\n\tpush_fatal(\"stop\")\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 5, 9, 5, 15) } },
			{ "nested_warning_if", "func f(flag: bool) -> void:\n\tif flag:\n\t\tpush_fatal(\"stop\")\n\t\tpass\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 4, 9, 4, 13) } },
			{ "nested_warning_while", "func f(flag: bool) -> void:\n\twhile flag:\n\t\tpush_fatal(\"stop\")\n\t\tpass\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 4, 9, 4, 13) } },
			{ "nested_warning_for", "func f() -> void:\n\tfor _i in range(1):\n\t\tpush_fatal(\"stop\")\n\t\tpass\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 4, 9, 4, 13) } },
			{ "nested_warning_match", "func f(flag: bool) -> void:\n\tmatch flag:\n\t\t_:\n\t\t\tpush_fatal(\"stop\")\n\t\t\tpass\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 5, 13, 5, 17) } },
			{ "two_functions", "func f() -> void:\n\tpush_fatal(\"stop\")\n\tpass\nfunc g() -> void:\n\tpush_fatal(\"stop\")\n\tpass\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 3, 5, 3, 9), W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"g()\".", 6, 5, 6, 9) } },
			{ "lambda_warning", "func f() -> void:\n\tvar _callback := func() -> void:\n\t\tpush_fatal(\"stop\")\n\t\tpass\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"<anonymous lambda>()\".", 4, 9, 4, 13) } },
			{ "nested_class_warning", "class C:\n\tfunc f() -> void:\n\t\tpush_fatal(\"stop\")\n\t\tpass\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 4, 9, 4, 13) } },
			{ "return_warning_once", "func f() -> void:\n\treturn\n\tpass\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 3, 5, 3, 9) } },
			{ "abort_return_then_pass", "func f() -> void:\n\tpush_fatal(\"stop\")\n\treturn\n\tpass\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 3, 5, 3, 11), W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 4, 5, 4, 9) } },
			{ "nested_terminator_warning", "func f(flag: bool) -> void:\n\tif flag:\n\t\treturn\n\telse:\n\t\tpush_fatal(\"stop\")\n\tpass\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 6, 5, 6, 9) } },
			{ "bool_gap", "func f(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n", { E("Not all code paths return a value. The \"match\" over \"bool\" does not cover: false.", 1, 1) }, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"bool\". Unhandled: false. Add the missing patterns or a \"_\" wildcard branch.", 2, 5, 4, 22) } },
			{ "bool_unrelated_fallthrough", "func f(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n\tpass\n", { E("Not all code paths return a value.", 1, 1) }, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"bool\". Unhandled: false. Add the missing patterns or a \"_\" wildcard branch.", 2, 5, 4, 22) } },
			{ "bool_branch_fallthrough", "func f(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\tpass\n", { E("Not all code paths return a value.", 1, 1) }, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"bool\". Unhandled: false. Add the missing patterns or a \"_\" wildcard branch.", 2, 5, 4, 18) } },
			{ "bool_complete", "func f(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n\t\tfalse:\n\t\t\treturn 0\n", {}, {} },
			{ "tagged_gap", "enum E:\n\tA\n\tB(value: int)\nfunc f(value: E) -> int:\n\tmatch value:\n\t\tE.A:\n\t\t\treturn 1\n", { E("Not all code paths return a value. The \"match\" over \"E\" does not cover: B.", 4, 1) }, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"E\". Unhandled: B. Add the missing patterns or a \"_\" wildcard branch.", 5, 5, 7, 22) } },
			{ "nullable_gap", "enum E:\n\tA\n\tB(value: int)\nfunc f(value: E?) -> int:\n\tmatch value:\n\t\tE.A:\n\t\t\treturn 1\n\t\tE.B(var _x):\n\t\t\treturn 2\n", { E("Not all code paths return a value. The \"match\" over \"E\" does not cover: null.", 4, 1) }, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"E\". Unhandled: null. Add the missing patterns or a \"_\" wildcard branch.", 5, 5, 9, 22) } },
			{ "enum_open", "enum E:\n\tA = 0\n\tB = 1\nfunc f(value: E) -> int:\n\tmatch value:\n\t\tE.A:\n\t\t\treturn 1\n\t\tE.B:\n\t\t\treturn 2\n", { E("Not all code paths return a value. The \"match\" over \"E\" leaves the undeclared values of its integer carrier unhandled; add an unguarded \"_\" or bind branch.", 4, 1) }, { W(OPEN_ENUM_MATCH_WITHOUT_DEFAULT, "The \"match\" over \"E\" has no unguarded \"_\" or bind branch. \"E\" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.", 5, 5, 9, 22) } },
			{ "original_noreturn_paths_norun", "@noreturn\nfunc abort_user() -> void:\n\tpush_fatal(\"abort\")\n\nfunc returns_from_push_fatal() -> int:\n\tpush_fatal(\"not implemented\")\n\nfunc returns_from_user_noreturn() -> int:\n\tabort_user()\n\nfunc returns_from_if(flag: bool) -> int:\n\tif flag:\n\t\treturn 1\n\telse:\n\t\tabort_user()\n\nfunc returns_from_all_if_noreturn(flag: bool) -> int:\n\tif flag:\n\t\tabort_user()\n\telse:\n\t\tpush_fatal(\"stop\")\n\nfunc returns_from_match(value: int) -> String:\n\tmatch value:\n\t\t0:\n\t\t\tabort_user()\n\t\t_:\n\t\t\tpush_fatal(\"stop\")\n\nfunc returns_from_while_true() -> int:\n\twhile true:\n\t\tabort_user()\n", {}, {} },
			{ "original_noreturn_function_return", "@noreturn\nfunc invalid_return() -> void:\n\treturn\n", { E("A \"@noreturn\" function cannot return.", 2, 1) }, {} },
			{ "original_noreturn_function_fallthrough", "@noreturn\nfunc invalid_fallthrough() -> void:\n\tprint(\"fallthrough\")\n", { E("A \"@noreturn\" function cannot complete normally.", 2, 1) }, {} },
			{ "original_noreturn_unreachable_norun", "@noreturn\nfunc abort_user() -> void:\n\tpush_fatal(\"abort\")\n\nfunc unreachable_after_noreturn() -> void:\n\tabort_user()\n\tprint(\"unreachable\")\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"unreachable_after_noreturn()\".", 7, 5, 7, 25) } },
			{ "body_unknown_call", "func f() -> int:\n\tunknown_abort()\n", { E("Not all code paths return a value.", 1, 1), E("Identifier \"unknown_abort\" not declared in the current scope.", 2, 5) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL },
			{ "body_failure_before_flow", "func f() -> int:\n\tmissing_name\n", { E("Not all code paths return a value.", 1, 1), E("Identifier \"missing_name\" not declared in the current scope.", 2, 5) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL },
			{ "parse_failure_before_flow", "func f(\n", { E("Expected closing \")\" after function parameters.", 1, 7) }, {}, Phase::NONE },
			{ "lambda_missing_return", "func f() -> void:\n\tvar _callback := func() -> int:\n\t\tpass\n", { E("Not all code paths return a value.", 2, 22) }, {} },
			{ "nested_class_missing_return", "class C:\n\tfunc f() -> int:\n\t\tpass\n", { E("Not all code paths return a value.", 2, 5) }, {} },
			{ "parser_overlap", "func f(flag: bool) -> void:\n\tif flag:\n\t\tpush_fatal(\"stop\")\n\t\treturn\n\telse:\n\t\treturn\n\tpass\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 4, 9, 4, 15), W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 7, 5, 7, 9) } },
			{ "match_break", "func f(flag: bool) -> int:\n\twhile true:\n\t\tmatch flag:\n\t\t\t_:\n\t\t\t\tbreak\n", { E("Not all code paths return a value.", 1, 1) }, {} },
			{ "unreachable_if_break", "func f(flag: bool) -> int:\n\twhile true:\n\t\tif flag:\n\t\t\treturn 1\n\t\telse:\n\t\t\tpush_fatal(\"stop\")\n\t\tbreak\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"f()\".", 7, 9, 7, 14) } },
			{ "tagged_gap_complete", "enum E:\n\tA\n\tB(value: int)\nfunc f(value: E) -> int:\n\tmatch value:\n\t\tE.A:\n\t\t\treturn 1\n\t\tE.B(var _x):\n\t\t\treturn 2\n", {}, {} },
			{ "nullable_gap_complete", "enum E:\n\tA\n\tB(value: int)\nfunc f(value: E?) -> int:\n\tmatch value:\n\t\tE.A:\n\t\t\treturn 1\n\t\tE.B(var _x):\n\t\t\treturn 2\n\t\tnull:\n\t\t\treturn 0\n", {}, {} },
			{ "enum_open_complete", "enum E:\n\tA = 0\n\tB = 1\nfunc f(value: E) -> int:\n\tmatch value:\n\t\tE.A:\n\t\t\treturn 1\n\t\tE.B:\n\t\t\treturn 2\n\t\t_:\n\t\t\treturn 0\n", {}, {} },
			{ "guarded_case", "enum E:\n\tA\n\tB(value: int)\nfunc f(value: E, flag: bool) -> int:\n\tmatch value:\n\t\tE.A:\n\t\t\treturn 1\n\t\tE.B(var _x) when flag:\n\t\t\treturn 2\n", { E("Not all code paths return a value. The \"match\" over \"E\" does not cover: B.", 4, 1) }, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"E\". Unhandled: B. Add the missing patterns or a \"_\" wildcard branch.", 5, 5, 9, 22) } },
			{ "refutable_case", "enum E:\n\tA\n\tB(value: int)\nfunc f(value: E) -> int:\n\tmatch value:\n\t\tE.A:\n\t\t\treturn 1\n\t\tE.B(1):\n\t\t\treturn 2\n", { E("Not all code paths return a value. The \"match\" over \"E\" does not cover: B.", 4, 1) }, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"E\". Unhandled: B. Add the missing patterns or a \"_\" wildcard branch.", 5, 5, 9, 22) } },
			{ "witness_missing_return", "trait T:\n\tabstract func witness_missing_return() -> int\nextend Node uses T:\n\tfunc witness_missing_return() -> int:\n\t\tpass\n", { E("Not all code paths return a value.", 4, 5) }, {} },
			{ "witness_warning", "trait T:\n\tabstract func witness_warning() -> void\nextend Node uses T:\n\tfunc witness_warning() -> void:\n\t\tpush_fatal(\"stop\")\n\t\tpass\n", {}, { W(UNREACHABLE_CODE, "Unreachable code (statement after return) in function \"witness_warning()\".", 6, 9, 6, 13) } },
			{ "witness_loop", "trait T:\n\tabstract func witness_loop() -> int\nextend Node uses T:\n\tfunc witness_loop() -> int:\n\t\twhile true:\n\t\t\tpass\n", {}, {} },
			{ "witness_body_failure", "trait T:\n\tabstract func witness_body_failure() -> int\nextend Node uses T:\n\tfunc witness_body_failure() -> int:\n\t\tmissing_name\n", { E("Identifier \"missing_name\" not declared in the current scope.", 5, 9) }, {}, Phase::CONFORMANCE_WITNESS_BODY },
		};
		const String generation_path = "res://tests/native/analyzer_flow/suite_exit_generation.barista";
		const uint64_t token = fixture.index().claim_refresh(generation_path);
		for (const SourceCase &test_case : cases) {
			check_case(fixture, settings, "suite_exit", test_case);
		}
		CHECK(fixture.index().claim_refresh(generation_path) == token + 1);
	}
	TEST_CASE("pinned_match_finality_domains") {
		StorageFixture fixture;
		AnalyzerSettings settings;
		settings.warnings_enabled(false);
		const SourceCase cases[] = {
			{ "finite_bool_DA_no_match", "func test(flag: bool) -> int:\n\tfinal var value: int\n\tmatch flag:\n\t\ttrue:\n\t\t\tvalue = 1\n\t\tfalse:\n\t\t\tvalue = 2\n\treturn value\n", { E("Final variable \"value\" may be used before assignment.", 8, 12) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "finite_bool_DA_type_cover", "func test(flag: bool) -> int:\n\tfinal var value: int\n\tmatch flag:\n\t\tflag is bool:\n\t\t\tvalue = 1\n\treturn value\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "concrete_union_no_exhaustion", "func test(value: int | String) -> int:\n\tif value is int:\n\t\treturn value\n\treturn 0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "nullable_union_no_exhaustion", "func test(value: int? | String) -> int:\n\tif value is int:\n\t\treturn value\n\treturn 0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "subclasses_no_exhaustion", "class First extends RefCounted:\n\tpass\nclass Second extends RefCounted:\n\tpass\nfunc test(value: First | Second) -> bool:\n\treturn value is RefCounted\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "enum_file_local_final", "enum_name Status:\n\tREADY = 1\n\n\tfunc invalid() -> int:\n\t\tfinal var value := 1\n\t\tvalue = 2\n\t\treturn value\n", { E("Cannot assign to final variable \"value\"; it is already assigned.", 6, 9) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_enum_host_function_final_local", "enum Status:\n\tREADY = 1\n\n\tfunc invalid() -> int:\n\t\tfinal var value := 1\n\t\tvalue = 2\n\t\treturn value\n", { E("Cannot assign to final variable \"value\"; it is already assigned.", 6, 9) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_local_var_lambda_assignment", "# A `final var` local captured by a nested lambda cannot be reassigned there; the\n# lambda is a separate scope outside the single-assignment slot.\nfunc test() -> void:\n\tfinal var x := 1\n\tvar reassign := func() -> void:\n\t\tx = 2\n\treassign.call()\n\tprint(x)\n", { E("Final variable \"x\" cannot be assigned inside a lambda.", 6, 9) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_local_var_in_member_initializer_lambda", "# A `final var` local inside a lambda used as a member initializer is enforced\n# like any other lambda body.\nvar callback := func() -> void:\n\tfinal var x := 1\n\tx = 2\n\tprint(x)\n\nfunc test() -> void:\n\tpass\n", { E("Cannot assign to final variable \"x\"; it is already assigned.", 5, 5) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_member_from_trait_read_before_assignment", "# A non-final trait variable initializer runs during construction, before the implementer's `_init()`\n# fills a trait-supplied blank final, so reading that final from such an initializer is a\n# use-before-assignment.\nextends RefCounted\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\tvar copy := id\n\nfunc _init() -> void:\n\tid = 1\n\nfunc test() -> void:\n\tpass\n", { E("Final variable \"id\" may be used before assignment.", 9, 17) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_member_match_guard_use_before_assignment", "# A `match` guard is evaluated before its branch body, so reading a blank final\n# from the guard is a use-before-assignment.\nfinal var id: int\n\nfunc _init(value: int) -> void:\n\tmatch value:\n\t\t_ when id > 0:\n\t\t\tid = 1\n\t\t_:\n\t\t\tid = 2\n\nfunc test() -> void:\n\tpass\n", { E("Final variable \"id\" may be used before assignment.", 7, 16) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_member_match_pattern_use_before_assignment", "# A `match` pattern expression can reference a final; reading a blank final from a\n# pattern is a use-before-assignment.\nfinal var id: int\n\nfunc _init(value: int) -> void:\n\tmatch value:\n\t\tself.id:\n\t\t\tid = 1\n\t\t_:\n\t\t\tid = 2\n\nfunc test() -> void:\n\tpass\n", { E("Expression in match pattern must be a constant expression, an identifier, or an attribute access (\"A.B\").", 7, 9), E("Final variable \"id\" may be used before assignment.", 7, 9) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL },
			{ "original_final_member_non_wildcard_match", "# A `match` without a wildcard branch leaves a no-match path open, so the blank\n# final is not definitely assigned.\nfinal var id: int\n\nfunc _init(value: int) -> void:\n\tmatch value:\n\t\t0:\n\t\t\tid = 10\n\t\t1:\n\t\t\tid = 20\n\nfunc test() -> void:\n\tpass\n", { E("Final variable \"id\" must be definitely assigned in its declaration or in \"_init()\".", 3, 7) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_member_init_default_arg_reads_blank", "# A `_init` parameter default is evaluated before the body assigns the blank\n# final, so reading it through an omitted default is use-before-assignment.\nfinal var id: int\n\nfunc _init(copy: int = id) -> void:\n\tid = copy\n\nfunc test() -> void:\n\tpass\n", { E("Final variable \"id\" may be used before assignment.", 5, 24) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_static_var_partial_assignment", "# Assigning a blank static final on only one arm of an `if` leaves it not\n# definitely assigned after `_static_init()`.\nfinal static var VALUE: int\n\nstatic func _flag() -> bool:\n\treturn false\n\nstatic func _static_init() -> void:\n\tif _flag():\n\t\tVALUE = 1\n\nfunc test() -> void:\n\tpass\n", { E("Final variable \"VALUE\" must be definitely assigned in its declaration or in \"_static_init()\".", 3, 14) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_static_var_early_return_unassigned", "# Returning early from `_static_init()` leaves a blank static final unassigned\n# for the lifetime of the class, so it must be assigned before every return.\nfinal static var MODE: int\n\nstatic func _flag() -> bool:\n\treturn false\n\nstatic func _static_init() -> void:\n\tif _flag():\n\t\tMODE = 1\n\telse:\n\t\treturn\n\nfunc test() -> void:\n\tpass\n", { E("Final variable \"MODE\" must be definitely assigned before returning from \"_static_init()\".", 12, 9) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_local_var_assigned_both_branches", "# A blank `final var` local assigned on every arm of an `if`/`else` is definitely\n# assigned at the join.\nfunc _flag() -> bool:\n\treturn false\n\nfunc test() -> void:\n\tfinal var size: int\n\tif _flag():\n\t\tsize = 1\n\telse:\n\t\tsize = 2\n\tprint(size)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_member_match_is_type_cover", "# A `match` whose only branch tests the subject against its whole domain always runs that branch,\n# so the blank final it assigns is definitely assigned afterwards.\nclass Categorized:\n\tfinal var kind: String\n\n\tfunc _init(value: bool) -> void:\n\t\tmatch value:\n\t\t\tvalue is bool:\n\t\t\t\tkind = \"flag:\" + str(value)\n\nfunc test() -> void:\n\tprint(Categorized.new(true).kind)\n\tprint(Categorized.new(false).kind)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_member_wildcard_match", "# A `match` with a wildcard branch that assigns on every branch makes the blank\n# final definitely assigned.\nclass Categorized:\n\tfinal var kind: String\n\n\tfunc _init(value: int) -> void:\n\t\tmatch value:\n\t\t\t0:\n\t\t\t\tkind = \"zero\"\n\t\t\t1:\n\t\t\t\tkind = \"one\"\n\t\t\t_:\n\t\t\t\tkind = \"many\"\n\nfunc test() -> void:\n\tprint(Categorized.new(0).kind)\n\tprint(Categorized.new(5).kind)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_member_from_trait_assigned_in_trait_init", "# A trait can carry both a blank `final var` and the `_init` that fills it. When the implementing\n# class adds no `_init` of its own, the trait's flattened `_init` is the assignment slot, so the blank\n# final is definitely assigned and the class compiles.\nextends RefCounted\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\tfunc _init() -> void:\n\t\tid = 5\n\nfunc test() -> void:\n\tprint(id)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_static_var_from_trait_assigned_in_trait_static_init", "# The static analog: a trait carries a blank `final static var` and the `_static_init` that fills\n# it; the flattened `_static_init` is the static slot on each implementer.\nextends RefCounted\nuses HasRegistry\n\ntrait HasRegistry:\n\tfinal static var COUNT: int\n\tstatic func _static_init() -> void:\n\t\tCOUNT = 3\n\nfunc test() -> void:\n\tprint(COUNT)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "original_final_static_var_assigned_both_branches", "# A blank static final assigned on every branch of an `if`/`else` in `_static_init()`\n# is definitely assigned at the join.\nfinal static var MODE: int\n\nstatic func _flag() -> bool:\n\treturn false\n\nstatic func _static_init() -> void:\n\tif _flag():\n\t\tMODE = 1\n\telse:\n\t\tMODE = 2\n\nfunc test() -> void:\n\tprint(MODE)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "return_bool", "func f(value: bool) -> int:\n\tmatch value:\n\t\tvalue is bool:\n\t\t\treturn 1\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "local_bool", "func f(value: bool) -> int:\n\tfinal var result: int\n\tmatch value:\n\t\tvalue is bool:\n\t\t\tresult = 1\n\treturn result\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "return_variant_nullable", "func f(value: bool?) -> int:\n\tmatch value:\n\t\tvalue is Variant:\n\t\t\treturn 1\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "local_variant_nullable", "func f(value: bool?) -> int:\n\tfinal var result: int\n\tmatch value:\n\t\tvalue is Variant:\n\t\t\tresult = 1\n\treturn result\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "return_variant_open", "func f(value: String) -> int:\n\tmatch value:\n\t\tvalue is Variant:\n\t\t\treturn 1\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "local_variant_open", "func f(value: String) -> int:\n\tfinal var result: int\n\tmatch value:\n\t\tvalue is Variant:\n\t\t\tresult = 1\n\treturn result\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "return_tagged", "enum E:\n\tA\n\tB(value: int)\nfunc f(value: E) -> int:\n\tmatch value:\n\t\tvalue is E:\n\t\t\treturn 1\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "local_tagged", "enum E:\n\tA\n\tB(value: int)\nfunc f(value: E) -> int:\n\tfinal var result: int\n\tmatch value:\n\t\tvalue is E:\n\t\t\tresult = 1\n\treturn result\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "return_enum_carrier", "enum E:\n\tA = 1\n\tB = 2\nfunc f(value: E) -> int:\n\tmatch value:\n\t\tvalue is int:\n\t\t\treturn 1\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "local_enum_carrier", "enum E:\n\tA = 1\n\tB = 2\nfunc f(value: E) -> int:\n\tfinal var result: int\n\tmatch value:\n\t\tvalue is int:\n\t\t\tresult = 1\n\treturn result\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "enum_host_typed_positive", "enum Status:\n\tREADY = 1\n\tfunc f(value: int) -> int:\n\t\tfinal var result: int = value\n\t\treturn result\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "enum_host_typed_negative", "enum Status:\n\tREADY = 1\n\tfunc f(value: int) -> String:\n\t\treturn value\n", { E("Cannot return value of type \"int\" because the function return type is \"String\".", 4, 9) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL },
			{ "enum_host_missing_return", "enum Status:\n\tREADY = 1\n\tfunc f() -> int:\n\t\tpass\n", { E("Not all code paths return a value.", 3, 5) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "enum_host_noreturn_positive", "enum Status:\n\tREADY = 1\n\t@noreturn\n\tfunc f() -> void:\n\t\twhile true:\n\t\t\tpass\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "enum_host_noreturn_negative", "enum Status:\n\tREADY = 1\n\t@noreturn\n\tfunc f() -> void:\n\t\tpass\n", { E("A \"@noreturn\" function cannot complete normally.", 4, 5) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "enum_file_typed_positive", "enum_name Status:\n\tREADY = 1\n\tfunc f(value: int) -> int:\n\t\tfinal var result: int = value\n\t\treturn result\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "enum_file_typed_negative", "enum_name Status:\n\tREADY = 1\n\tfunc f(value: int) -> String:\n\t\treturn value\n", { E("Cannot return value of type \"int\" because the function return type is \"String\".", 4, 9) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL },
			{ "enum_file_missing_return", "enum_name Status:\n\tREADY = 1\n\tfunc f() -> int:\n\t\tpass\n", { E("Not all code paths return a value.", 3, 5) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "enum_file_noreturn_positive", "enum_name Status:\n\tREADY = 1\n\t@noreturn\n\tfunc f() -> void:\n\t\twhile true:\n\t\t\tpass\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "enum_file_noreturn_negative", "enum_name Status:\n\tREADY = 1\n\t@noreturn\n\tfunc f() -> void:\n\t\tpass\n", { E("A \"@noreturn\" function cannot complete normally.", 4, 5) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "canonical_alias_collapse", "type Same = int | int\nfunc f(value: Same) -> bool:\n\treturn value is int\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "match_partial_then_ordinary", "func f(value: int | String) -> bool:\n\tmatch value:\n\t\tvalue is int:\n\t\t\tpass\n\treturn value is String\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "expression_identifier", "func f(value: int, other: int) -> void:\n\tmatch value:\n\t\tother:\n\t\t\tpass\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "expression_nonconstant", "func f(value: int, other: int) -> void:\n\tmatch value:\n\t\tother + 1:\n\t\t\tpass\n", { E("Expression in match pattern must be a constant expression, an identifier, or an attribute access (\"A.B\").", 3, 9) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL },
			{ "static_bool_false", "final static var RESULT: int\nstatic func _flag() -> bool:\n\treturn true\nstatic func _static_init() -> void:\n\tvar value: bool = _flag()\n\tmatch value:\n\t\ttrue:\n\t\t\tRESULT = 1\n\t\tfalse:\n\t\t\tRESULT = 2\n", { E("Final variable \"RESULT\" must be definitely assigned in its declaration or in \"_static_init()\".", 1, 14) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "static_bool_true", "final static var RESULT: int\nstatic func _flag() -> bool:\n\treturn true\nstatic func _static_init() -> void:\n\tvar value: bool = _flag()\n\tmatch value:\n\t\tvalue is bool:\n\t\t\tRESULT = 1\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "plain_carrier_E_return", "enum E:\n\tA = 1\nfunc f(value: E) -> int:\n\treturn value\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "plain_carrier_E_store", "enum E:\n\tA = 1\nfunc f(value: E) -> void:\n\tvar output: int = value\n\tprint(output)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "plain_carrier_E_call", "enum E:\n\tA = 1\nfunc accept(value: int) -> void:\n\tprint(value)\nfunc f(value: E) -> void:\n\taccept(value)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "plain_carrier_E_is", "enum E:\n\tA = 1\nfunc f(value: E) -> bool:\n\treturn value is int\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "plain_carrier_int_return", "enum E:\n\tA = 1\nfunc f(value: int) -> E:\n\treturn value\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "plain_carrier_int_store", "enum E:\n\tA = 1\nfunc f(value: int) -> void:\n\tvar output: E = value\n\tprint(output)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "plain_carrier_int_call", "enum E:\n\tA = 1\nfunc accept(value: E) -> void:\n\tprint(value)\nfunc f(value: int) -> void:\n\taccept(value)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
			{ "plain_carrier_int_is", "enum E:\n\tA = 1\nfunc f(value: int) -> bool:\n\treturn value is E\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES },
		};
		const String generation_path = "res://tests/native/analyzer_flow/pinned_match_finality_domains_generation.barista";
		const uint64_t token = fixture.index().claim_refresh(generation_path);
		for (const SourceCase &test_case : cases) {
			check_case(fixture, settings, "pinned_match_finality_domains", test_case);
		}
		CHECK(fixture.index().claim_refresh(generation_path) == token + 1);
	}
	TEST_CASE("pinned_match_domain_and_narrowing_audit") {
		StorageFixture fixture;
		AnalyzerSettings settings;
		settings.warnings_enabled(true);
		settings.warning(BSWarning::NON_EXHAUSTIVE_MATCH, BSWarning::WARN);
		settings.warning(BSWarning::OPEN_ENUM_MATCH_WITHOUT_DEFAULT, BSWarning::WARN);
		settings.warning(BSWarning::UNREACHABLE_PATTERN, BSWarning::WARN);
		const SourceCase cases[] = {
			{ "match_95_source", "\nenum Plain:\n\tOk(value: String)\n\tErr(error: int)\n\nfunc describe(v: Plain) -> String:\n\tmatch v:\n\t\tPlain.Ok(value):\n\t\t\treturn value\n\t\tPlain.Err(error):\n\t\t\treturn str(error)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_149_source", "\nenum Plain:\n\tOk(value: String)\n\tErr(error: int)\n\nfunc describe(v: Plain) -> String:\n\tmatch v:\n\t\tPlain.Ok(value):\n\t\t\treturn value\n", { E("Not all code paths return a value. The \"match\" over \"Plain\" does not cover: Err.", 6, 1) }, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"Plain\". Unhandled: Err. Add the missing patterns or a \"_\" wildcard branch.", 7, 5, 9, 26) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_169_source", "\nenum Plain:\n\tOk(value: String)\n\tErr(error: int)\n\nfunc describe(v: Plain) -> String:\n\tmatch v:\n\t\tPlain.Ok(value) when value.is_empty():\n\t\t\treturn \"empty\"\n\t\tPlain.Err(error) when error > 0:\n\t\t\treturn \"positive\"\n", { E("Not all code paths return a value. The \"match\" over \"Plain\" does not cover: Ok, Err.", 6, 1) }, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"Plain\". Unhandled: Ok, Err. Add the missing patterns or a \"_\" wildcard branch.", 7, 5, 11, 31) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_197_source", "\nenum Plain:\n\tOk(value: int)\n\tErr(error: int)\n\nfunc describe(v: Plain) -> String:\n\tmatch v:\n\t\tPlain.Ok(0):\n\t\t\treturn \"zero\"\n\t\tPlain.Err(error):\n\t\t\treturn str(error)\n", { E("Not all code paths return a value. The \"match\" over \"Plain\" does not cover: Ok.", 6, 1) }, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"Plain\". Unhandled: Ok. Add the missing patterns or a \"_\" wildcard branch.", 7, 5, 11, 31) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_218_uncovered_source", "\nenum Plain:\n\tOk(value: String)\n\tErr(error: int)\n\nfunc describe(v: Plain?) -> String:\n\tmatch v:\n\t\tPlain.Ok(value):\n\t\t\treturn value\n\t\tPlain.Err(error):\n\t\t\treturn str(error)\n", { E("Not all code paths return a value. The \"match\" over \"Plain\" does not cover: null.", 6, 1) }, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"Plain\". Unhandled: null. Add the missing patterns or a \"_\" wildcard branch.", 7, 5, 11, 31) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_218_covered_source", "\nenum Plain:\n\tOk(value: String)\n\tErr(error: int)\n\nfunc describe(v: Plain?) -> String:\n\tmatch v:\n\t\tPlain.Ok(value):\n\t\t\treturn value\n\t\tPlain.Err(error):\n\t\t\treturn str(error)\n\t\tnull:\n\t\t\treturn \"none\"\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_259_source", "\nfunc describe(flag: bool) -> String:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn \"yes\"\n\t\tfalse:\n\t\t\treturn \"no\"\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_279_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfunc describe(level: Level) -> String:\n\tmatch level:\n\t\tLevel.LOW:\n\t\t\treturn \"low\"\n\t\tLevel.HIGH:\n\t\t\treturn \"high\"\n", { E("Not all code paths return a value. The \"match\" over \"Level\" leaves the undeclared values of its integer carrier unhandled; add an unguarded \"_\" or bind branch.", 6, 1) }, { W(OPEN_ENUM_MATCH_WITHOUT_DEFAULT, "The \"match\" over \"Level\" has no unguarded \"_\" or bind branch. \"Level\" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.", 7, 5, 11, 27) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_308_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfunc describe(level: Level) -> String:\n\tmatch level:\n\t\tLevel.LOW:\n\t\t\treturn \"low\"\n\t\tLevel.HIGH:\n\t\t\treturn \"high\"\n\t\t99:\n\t\t\treturn \"ninety-nine\"\n", { E("Not all code paths return a value. The \"match\" over \"Level\" leaves the undeclared values of its integer carrier unhandled; add an unguarded \"_\" or bind branch.", 6, 1) }, { W(OPEN_ENUM_MATCH_WITHOUT_DEFAULT, "The \"match\" over \"Level\" has no unguarded \"_\" or bind branch. \"Level\" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.", 7, 5, 13, 34) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_337_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfunc describe(level: Level, allow: bool) -> String:\n\tmatch level:\n\t\tLevel.LOW:\n\t\t\treturn \"low\"\n\t\tLevel.HIGH:\n\t\t\treturn \"high\"\n\t\t_ when allow:\n\t\t\treturn \"other\"\n", { E("Not all code paths return a value. The \"match\" over \"Level\" leaves the undeclared values of its integer carrier unhandled; add an unguarded \"_\" or bind branch.", 6, 1) }, { W(OPEN_ENUM_MATCH_WITHOUT_DEFAULT, "The \"match\" over \"Level\" has no unguarded \"_\" or bind branch. \"Level\" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.", 7, 5, 13, 28) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_366_wildcard_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfunc describe(level: Level) -> String:\n\tmatch level:\n\t\tLevel.LOW:\n\t\t\treturn \"low\"\n\t\t_:\n\t\t\treturn \"other\"\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_366_bind_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfunc describe(level: Level) -> String:\n\tmatch level:\n\t\tLevel.LOW:\n\t\t\treturn \"low\"\n\t\tvar other:\n\t\t\treturn str(other)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_410_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfunc describe(level: Level) -> String:\n\tmatch level:\n\t\tlevel is Variant:\n\t\t\treturn str(level)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_437_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfinal var label: String\n\nfunc _init(level: Level) -> void:\n\tmatch level:\n\t\tlevel is Level:\n\t\t\tlabel = \"declared\"\n", { E("Final variable \"label\" must be definitely assigned in its declaration or in \"_init()\".", 6, 7) }, { W(OPEN_ENUM_MATCH_WITHOUT_DEFAULT, "The \"match\" over \"Level\" has no unguarded \"_\" or bind branch. \"Level\" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.", 9, 5, 11, 32) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_437_covered_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfinal var label: String\n\nfunc _init(level: Level) -> void:\n\tmatch level:\n\t\tlevel is Level:\n\t\t\tlabel = \"declared\"\n\t\t_:\n\t\t\tlabel = \"undeclared\"\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_480_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfunc describe(level: Level) -> String:\n\tmatch level:\n\t\tlevel is int:\n\t\t\treturn str(level)\n\nfunc record(level: Level) -> void:\n\tmatch level:\n\t\tlevel is int:\n\t\t\tprint(level)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_518_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfunc describe(level: Level) -> String:\n\tmatch level:\n\t\tlevel is Level:\n\t\t\treturn str(level)\n", { E("Not all code paths return a value. The \"match\" over \"Level\" leaves the undeclared values of its integer carrier unhandled; add an unguarded \"_\" or bind branch.", 6, 1) }, { W(OPEN_ENUM_MATCH_WITHOUT_DEFAULT, "The \"match\" over \"Level\" has no unguarded \"_\" or bind branch. \"Level\" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.", 7, 5, 9, 31) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_571_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfinal var label: String\n\nfunc _init(level: Level) -> void:\n\tmatch level:\n\t\tlevel is int:\n\t\t\tlabel = str(level)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_611_source", "\nenum Plain:\n\tOk(value: String)\n\tErr(error: int)\n\nfunc describe(v: Plain) -> String:\n\tmatch v:\n\t\tPlain.Ok(value):\n\t\t\treturn value\n\t\t_:\n\t\t\treturn \"other\"\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_633_source", "\nenum Plain:\n\tOk(value: String)\n\tErr(error: int)\n\nfunc describe(v: Plain) -> String:\n\tmatch v:\n\t\tPlain.Ok(value):\n\t\t\treturn value\n\t\tPlain.Err(error):\n\t\t\treturn str(error)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_676_source", "\nenum Plain:\n\tOk(value: String)\n\tErr(error: int)\n\nfunc from_bool(value: bool) -> String:\n\tmatch value:\n\t\tvalue is bool:\n\t\t\treturn str(value)\n\nfunc from_union(value: Plain) -> String:\n\tmatch value:\n\t\tvalue is Plain:\n\t\t\treturn \"plain\"\n\nfunc from_variant(value: int) -> String:\n\tmatch value:\n\t\tvalue is Variant:\n\t\t\treturn str(value)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_715_source", "\nenum Level:\n\tLOW = 1\n\tHIGH = 2\n\nfunc by_values(value: Level) -> String:\n\tmatch value:\n\t\tLevel.LOW:\n\t\t\treturn \"low\"\n\t\tLevel.HIGH:\n\t\t\treturn \"high\"\n\nfunc by_type(value: Level) -> String:\n\tmatch value:\n\t\tvalue is Level:\n\t\t\treturn \"type\"\n", { E("Not all code paths return a value. The \"match\" over \"Level\" leaves the undeclared values of its integer carrier unhandled; add an unguarded \"_\" or bind branch.", 6, 1), E("Not all code paths return a value. The \"match\" over \"Level\" leaves the undeclared values of its integer carrier unhandled; add an unguarded \"_\" or bind branch.", 13, 1) }, { W(OPEN_ENUM_MATCH_WITHOUT_DEFAULT, "The \"match\" over \"Level\" has no unguarded \"_\" or bind branch. \"Level\" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.", 7, 5, 11, 27), W(OPEN_ENUM_MATCH_WITHOUT_DEFAULT, "The \"match\" over \"Level\" has no unguarded \"_\" or bind branch. \"Level\" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.", 14, 5, 16, 27) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_756_source", "\ntype IntOrString = int | String\n\nenum Plain:\n\tOk(value: String)\n\tErr(error: int)\n\nfunc from_union(value: IntOrString) -> String:\n\tmatch value:\n\t\tvalue is int:\n\t\t\treturn \"int\"\n\nfunc from_case(value: Plain) -> String:\n\tmatch value:\n\t\tvalue is Plain.Ok:\n\t\t\treturn \"ok\"\n\nfunc from_nullable(value: bool?) -> String:\n\tmatch value:\n\t\tvalue is bool:\n\t\t\treturn str(value)\n\nfunc from_guard(value: bool) -> String:\n\tmatch value:\n\t\tvalue is bool when value:\n\t\t\treturn \"true\"\n", { E("Not all code paths return a value.", 8, 1), E("Not all code paths return a value.", 13, 1), E("Not all code paths return a value.", 18, 1), E("Not all code paths return a value. The \"match\" over \"bool\" does not cover: false, true.", 23, 1) }, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"bool\". Unhandled: false, true. Add the missing patterns or a \"_\" wildcard branch.", 24, 5, 26, 27) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_835_source", "\nfunc describe(value: String) -> String:\n\tmatch value:\n\t\tvalue is String:\n\t\t\treturn value\n", { E("Not all code paths return a value.", 2, 1) }, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_859_source", "\nfunc describe(value: bool) -> String:\n\tmatch value:\n\t\tvalue is bool:\n\t\t\treturn str(value)\n\t\t_:\n\t\t\treturn \"other\"\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_859_unreachable_source", "\nfunc describe(value: bool) -> String:\n\tmatch value:\n\t\t_:\n\t\t\treturn \"other\"\n\t\ttrue:\n\t\t\treturn \"true\"\n", {}, { W(UNREACHABLE_PATTERN, "Unreachable pattern (pattern after wildcard or bind).", 6, 9, 6, 13) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_915_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfunc handle(level: Level) -> void:\n\tmatch level:\n\t\tLevel.LOW:\n\t\t\tprint(\"low\")\n\t\tLevel.HIGH:\n\t\t\tprint(\"high\")\n", {}, { W(OPEN_ENUM_MATCH_WITHOUT_DEFAULT, "The \"match\" over \"Level\" has no unguarded \"_\" or bind branch. \"Level\" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.", 7, 5, 11, 27) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_944_source", "\nenum Level:\n\tLOW = 0\n\tMEDIUM = 1\n\tHIGH = 2\n\nfunc handle(level: Level) -> void:\n\tmatch level:\n\t\tLevel.MEDIUM:\n\t\t\tprint(\"medium\")\n", {}, { W(OPEN_ENUM_MATCH_WITHOUT_DEFAULT, "The \"match\" over \"Level\" does not handle: LOW, HIGH. \"Level\" is also carried by an integer that can hold values outside its declared members, so add an unguarded \"_\" or bind branch rather than the missing patterns alone.", 8, 5, 10, 29) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_969_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfunc handle(level: Level) -> void:\n\tmatch level:\n\t\tlevel is int:\n\t\t\tprint(\"carrier\")\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_969_open_source", "\nenum Level:\n\tLOW = 0\n\tHIGH = 1\n\nfunc handle(level: Level) -> void:\n\tmatch level:\n\t\tLevel.LOW:\n\t\t\tprint(\"low\")\n\t\tLevel.HIGH:\n\t\t\tprint(\"high\")\n", {}, { W(OPEN_ENUM_MATCH_WITHOUT_DEFAULT, "The \"match\" over \"Level\" has no unguarded \"_\" or bind branch. \"Level\" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.", 7, 5, 11, 27) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_1014_source", "\nfunc handle(flag: bool) -> void:\n\tmatch flag:\n\t\ttrue:\n\t\t\tprint(\"t\")\n", {}, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"bool\". Unhandled: false. Add the missing patterns or a \"_\" wildcard branch.", 3, 5, 5, 24) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "match_1033_source", "\nenum Message:\n\tMove(distance: int)\n\tStop\n\nfunc handle(message: Message) -> void:\n\tmatch message:\n\t\tMessage.Stop:\n\t\t\tprint(\"stop\")\n", {}, { W(NON_EXHAUSTIVE_MATCH, "The \"match\" statement does not cover all values of \"Message\". Unhandled: Move. Add the missing patterns or a \"_\" wildcard branch.", 7, 5, 9, 27) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "narrow_2908_if_not_null_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc test(node: Node?) -> void:\n\tif node != null:\n\t\taccept_node(node)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "narrow_2908_null_not_equal_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc test(node: Node?) -> void:\n\tif null != node:\n\t\taccept_node(node)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "narrow_2908_else_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc test(node: Node?) -> void:\n\tif node == null:\n\t\tpass\n\telse:\n\t\taccept_node(node)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "narrow_2908_outside_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc test(node: Node?) -> void:\n\tif node != null:\n\t\tpass\n\taccept_node(node)\n", { E("Cannot pass nullable value of type \"Node?\" as argument 1 of \"accept_node()\"; expected non-nullable \"Node\".", 6, 17) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, true, false },
			{ "narrow_2925_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc do_nothing() -> void:\n\tpass\nfunc test(node: Node?) -> void:\n\tif node != null:\n\t\tdo_nothing()\n\t\taccept_node(node)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "narrow_2931_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc do_nothing() -> void:\n\tpass\nfunc test(node: Node?) -> void:\n\tvar read_node := func() -> void:\n\t\tprint(node)\n\tif node != null:\n\t\tdo_nothing()\n\t\taccept_node(node)\n", { E("Cannot pass nullable value of type \"Node?\" as argument 1 of \"accept_node()\"; expected non-nullable \"Node\".", 10, 21) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, true, false },
			{ "narrow_2937_assert_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc test(node: Node?) -> void:\n\tassert(node != null)\n\taccept_node(node)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "narrow_2937_null_not_equal_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc test(node: Node?) -> void:\n\tassert(null != node)\n\taccept_node(node)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "narrow_2948_assignment_source", "func test(maybe: Node?) -> void:\n\tvar node: Node = maybe\n", { E("Cannot assign nullable value of type \"Node?\" to variable \"node\"; expected non-nullable \"Node\".", 2, 22) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, true, false },
			{ "narrow_2948_narrowed_assignment_source", "func test(maybe: Node?) -> void:\n\tif maybe != null:\n\t\tvar node: Node = maybe\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "narrow_2948_return_source", "func get_node(maybe: Node?) -> Node:\n\treturn maybe\n", { E("Cannot return value of type \"Node?\" because the function return type is \"Node\".", 2, 5) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, true, false },
			{ "narrow_2948_narrowed_return_source", "func get_node(maybe: Node?) -> Node:\n\tassert(maybe != null)\n\treturn maybe\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "narrow_2960_node_test_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc accept_button(button: Button) -> void:\n\tpass\nfunc test(value: Variant) -> void:\n\tif value is Node:\n\t\taccept_node(value)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, true },
			{ "narrow_2960_button_test_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc accept_button(button: Button) -> void:\n\tpass\nfunc test(value: Variant) -> void:\n\tif value is Button:\n\t\taccept_button(value)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, true },
			{ "narrow_2960_is_not_else_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc accept_button(button: Button) -> void:\n\tpass\nfunc test(value: Variant) -> void:\n\tif value is not Node:\n\t\tpass\n\telse:\n\t\taccept_node(value)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, true },
			{ "narrow_2960_outside_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc accept_button(button: Button) -> void:\n\tpass\nfunc test(value: Variant) -> void:\n\tif value is Node:\n\t\tpass\n\taccept_node(value)\n", { E("Cannot pass Variant value as argument 1 of \"accept_node()\" in strict dynamic mode; expected \"Node\".", 8, 17) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, true },
			{ "narrow_2960_reassigned_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc accept_button(button: Button) -> void:\n\tpass\nfunc test(value: Variant) -> void:\n\tif value is Node:\n\t\tvalue = 1\n\t\taccept_node(value)\n", { E("Cannot pass Variant value as argument 1 of \"accept_node()\" in strict dynamic mode; expected \"Node\".", 8, 21) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, true },
			{ "narrow_2975_array_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc test(nodes: Array[Node]) -> void:\n\tmatch nodes:\n\t\t[var node]:\n\t\t\taccept_node(node)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, true },
			{ "narrow_2975_dictionary_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc test(nodes: Dictionary[String, Node]) -> void:\n\tmatch nodes:\n\t\t{\"node\": var node}:\n\t\t\taccept_node(node)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, true },
			{ "narrow_2975_nested_array_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc test(nodes: Array[Array[Node]]) -> void:\n\tmatch nodes:\n\t\t[[var node]]:\n\t\t\taccept_node(node)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, true },
			{ "array_first_declaration", "func use() -> Array[String]:\n\tvar values: Array[String] = [1, 2, 3]\n\treturn values\n", { E("Cannot include a value of type \"int\" as \"String\".", 2, 34), E("Cannot have an element of type \"int\" in an array of type \"Array[String]\".", 2, 34) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "array_first_return", "func use() -> Array[String]:\n\treturn [1, 2, 3]\n", { E("Cannot include a value of type \"int\" as \"String\".", 2, 13), E("Cannot have an element of type \"int\" in an array of type \"Array[String]\".", 2, 13) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "array_first_call", "func accept(values: Array[String]) -> void:\n\tpass\nfunc use() -> void:\n\taccept([1, 2, 3])\n", { E("Cannot include a value of type \"int\" as \"String\".", 4, 13), E("Cannot have an element of type \"int\" in an array of type \"Array[String]\".", 4, 13) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "dictionary_first_key", "func use() -> void:\n\tvar values: Dictionary[String, String] = {1: 2, 3: 4}\n", { E("Cannot include a value of type \"int\" as \"String\".", 2, 47), E("Cannot have a key of type \"int\" in a dictionary of type \"Dictionary[String, String]\".", 2, 47) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "dictionary_first_value", "func use() -> void:\n\tvar values: Dictionary[String, String] = {\"a\": 1, \"b\": 2}\n", { E("Cannot include a value of type \"int\" as \"String\".", 2, 52), E("Cannot have a value of type \"int\" in a dictionary of type \"Dictionary[String, String]\".", 2, 52) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "null_Node_false", "func use() -> Node?:\n\tvar value: Node? = null\n\tvalue = null\n\treturn value\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "null_Node_true", "func use() -> Node?:\n\tvar value: Node? = null\n\tvalue = null\n\treturn value\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "null_int_false", "func use() -> int?:\n\tvar value: int? = null\n\tvalue = null\n\treturn value\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "null_int_true", "func use() -> int?:\n\tvar value: int? = null\n\tvalue = null\n\treturn value\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "null_E_false", "enum E:\n\tA = 0\nfunc use() -> E?:\n\tvar value: E? = null\n\tvalue = null\n\treturn value\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "null_E_true", "enum E:\n\tA = 0\nfunc use() -> E?:\n\tvar value: E? = null\n\tvalue = null\n\treturn value\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "null_Local_false", "class Local:\n\tpass\nfunc use() -> Local?:\n\tvar value: Local? = null\n\tvalue = null\n\treturn value\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "null_Local_true", "class Local:\n\tpass\nfunc use() -> Local?:\n\tvar value: Local? = null\n\tvalue = null\n\treturn value\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "null_call_false", "func accept(value: Node?) -> void:\n\tpass\nfunc use() -> void:\n\taccept(null)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "null_call_true", "func accept(value: Node?) -> void:\n\tpass\nfunc use() -> void:\n\taccept(null)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "narrow_2908_local_variable_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc test() -> void:\n\tvar node: Node? = null\n\tif node != null:\n\t\taccept_node(node)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "narrow_2908_reassigned_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc test(node: Node?) -> void:\n\tif node != null:\n\t\tnode = null\n\t\taccept_node(node)\n", { E("Cannot pass nullable value of type \"Node?\" as argument 1 of \"accept_node()\"; expected non-nullable \"Node\".", 6, 21) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, true, false },
			{ "narrow_2937_reassigned_source", "func accept_node(node: Node) -> void:\n\tpass\nfunc test(node: Node?) -> void:\n\tassert(node != null)\n\tnode = null\n\taccept_node(node)\n", { E("Cannot pass nullable value of type \"Node?\" as argument 1 of \"accept_node()\"; expected non-nullable \"Node\".", 6, 17) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, true, false },
		};
		for (const SourceCase &test_case : cases) {
			check_case(fixture, settings, "pinned_match_domain_and_narrowing_audit", test_case);
		}
	}
	TEST_CASE("pinned_for_assert_consumers") {
		StorageFixture fixture;
		AnalyzerSettings settings;
		settings.warnings_enabled(true);
		settings.warning(BSWarning::ASSERT_ALWAYS_TRUE, BSWarning::WARN);
		settings.warning(BSWarning::ASSERT_ALWAYS_FALSE, BSWarning::WARN);
		settings.warning(BSWarning::UNREACHABLE_CODE, BSWarning::WARN);
		settings.warning(BSWarning::NON_EXHAUSTIVE_MATCH, BSWarning::WARN);
		settings.warning(BSWarning::OPEN_ENUM_MATCH_WITHOUT_DEFAULT, BSWarning::WARN);
		const SourceCase cases[] = {
			{ "for_loop_wrong_specified_type", "func test():\n\tvar a: Array[Resource] = []\n\tfor node: Node in a:\n\t\tprint(node)\n", { E("Unable to iterate on value of type \"Array[Resource]\" with variable of type \"Node\".", 3, 13) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "for_loop_wrong_specified_type_with_literal_array", "# GH-82021\n\nfunc test():\n\tfor x: String in [1, 2, 3]:\n\t\tprint(x)\n", { E("Cannot include a value of type \"int\" as \"String\".", 4, 23), E("Cannot have an element of type \"int\" in an array of type \"Array[String]\".", 4, 23) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "for_loop_wrong_specified_type_with_literal_dictionary", "func test():\n\tfor key: int in { \"a\": 1 }:\n\t\tprint(key)\n", { E("Cannot include a value of type \"String\" as \"int\".", 2, 23), E("Cannot have a key of type \"String\" in a dictionary of type \"Dictionary[int, Variant]\".", 2, 23) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "range_shadow", "func range() -> Array[String]:\n\treturn []\nfunc use() -> String:\n\tfor value in range():\n\t\treturn value\n\treturn \"\"\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_array_int", "func use(values: Array[int]) -> int:\n\tfor value in values:\n\t\treturn value\n\treturn 0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_dictionary_string_int", "func use(values: Dictionary[String, int]) -> String:\n\tfor value in values:\n\t\treturn value\n\treturn \"\"\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_packedint32array", "func use(values: PackedInt32Array) -> int:\n\tfor value in values:\n\t\treturn value\n\treturn 0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_packedstringarray", "func use(values: PackedStringArray) -> String:\n\tfor value in values:\n\t\treturn value\n\treturn \"\"\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_int", "func use(values: int) -> int:\n\tfor value in values:\n\t\treturn value\n\treturn 0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_float", "func use(values: float) -> float:\n\tfor value in values:\n\t\treturn value\n\treturn 0.0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_string", "func use(values: String) -> String:\n\tfor value in values:\n\t\treturn value\n\treturn \"\"\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_vector2i", "func use(values: Vector2i) -> int:\n\tfor value in values:\n\t\treturn value\n\treturn 0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_vector3i", "func use(values: Vector3i) -> int:\n\tfor value in values:\n\t\treturn value\n\treturn 0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_vector2", "func use(values: Vector2) -> float:\n\tfor value in values:\n\t\treturn value\n\treturn 0.0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_vector3", "func use(values: Vector3) -> float:\n\tfor value in values:\n\t\treturn value\n\treturn 0.0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_array", "func use(values: Array) -> Variant:\n\tfor value in values:\n\t\treturn value\n\treturn null\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_dictionary", "func use(values: Dictionary) -> Variant:\n\tfor value in values:\n\t\treturn value\n\treturn null\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_parameter_variant", "func use(values: Variant) -> Variant:\n\tfor value in values:\n\t\treturn value\n\treturn null\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_wrong_return_array_int", "func use(values: Array[int]) -> String:\n\tfor value in values:\n\t\treturn value\n\treturn \"\"\n", { E("Cannot return value of type \"int\" because the function return type is \"String\".", 3, 9) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "for_wrong_return_packedint32array", "func use(values: PackedInt32Array) -> String:\n\tfor value in values:\n\t\treturn value\n\treturn \"\"\n", { E("Cannot return value of type \"int\" because the function return type is \"String\".", 3, 9) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "for_wrong_return_packedstringarray", "func use(values: PackedStringArray) -> int:\n\tfor value in values:\n\t\treturn value\n\treturn 0\n", { E("Cannot return value of type \"String\" because the function return type is \"int\".", 3, 9) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "for_samefile_iterator", "class Iter:\n\tfunc _iter_get(_state: Variant) -> int:\n\t\treturn 1\nfunc use(values: Iter) -> int:\n\tfor value in values:\n\t\treturn value\n\treturn 0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_inherited_iterator", "class Iter:\n\tfunc _iter_get(_state: Variant) -> int:\n\t\treturn 1\nclass Derived extends Iter:\n\tpass\nfunc use(values: Derived) -> int:\n\tfor value in values:\n\t\treturn value\n\treturn 0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_samefile_iterator_wrong_destination", "class Iter:\n\tfunc _iter_get(_state: Variant) -> int:\n\t\treturn 1\nfunc use(values: Iter) -> String:\n\tfor value in values:\n\t\treturn value\n\treturn \"\"\n", { E("Cannot return value of type \"int\" because the function return type is \"String\".", 6, 9) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "for_missing_object", "func use(values: Object) -> void:\n\tfor _value in values:\n\t\tpass\n", { E("Unable to iterate on object of type \"Object\".", 2, 19) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "for_missing_empty", "class Empty:\n\tpass\nfunc use(values: Empty) -> void:\n\tfor _value in values:\n\t\tpass\n", { E("Unable to iterate on object of type \"Empty\".", 4, 19) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "for_hard_bool", "func use() -> void:\n\tfor _value in true:\n\t\tpass\n", { E("Unable to iterate on value of type \"bool\".", 2, 19) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "for_wrong_annotation", "func use(values: Array[int]) -> void:\n\tfor value: String in values:\n\t\tpass\n", { E("Unable to iterate on value of type \"Array[int]\" with variable of type \"String\".", 2, 14) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "for_dictionary_key_literal", "func use() -> StringName:\n\tfor key: StringName in {\"a\": 1}:\n\t\treturn key\n\treturn &\"\"\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_array_float_literal", "func use() -> float:\n\tfor value: float in [1, 2]:\n\t\treturn value\n\treturn 0.0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "for_range_arity_0", "func use() -> void:\n\tfor _value in range():\n\t\tpass\n", { E("Invalid call for \"range()\" function. Expected at least 1 argument, none given.", 2, 19) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "for_range_arity_4", "func use() -> void:\n\tfor _value in range(1, 2, 3, 4):\n\t\tpass\n", { E("Invalid call for \"range()\" function. Expected at most 3 arguments, 4 given.", 2, 19) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "for_range_float_annotation", "func use() -> float:\n\tfor value: float in range(1, 3):\n\t\treturn value\n\treturn 0.0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "assert_true", "func use() -> void:\n\tassert(true)\n", {}, { W(ASSERT_ALWAYS_TRUE, "Assert statement is redundant because the expression is always true.", 2, 12, 2, 16) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false, true },
			{ "assert_false_numeric", "func use() -> void:\n\tassert(0)\n", {}, { W(ASSERT_ALWAYS_FALSE, "Assert statement will raise an error because the expression is always false.", 2, 12, 2, 13) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false, true },
			{ "assert_false_nonliteral", "func use() -> void:\n\tassert(not true)\n", {}, { W(ASSERT_ALWAYS_FALSE, "Assert statement will raise an error because the expression is always false.", 2, 12, 2, 20) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false, true },
			{ "assert_named_false", "const NEVER = false\nfunc use() -> void:\n\tassert(NEVER)\n", {}, { W(ASSERT_ALWAYS_FALSE, "Assert statement will raise an error because the expression is always false.", 3, 12, 3, 17) }, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false, true },
			{ "assert_literal_false", "func use() -> void:\n\tassert(false)\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "assert_literal_false_string_message", "func use() -> void:\n\tassert(false, \"message\")\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "assert_wrong_message", "func f(flag: bool) -> void:\n\tassert(flag, 1)\n", { E("Expected string for assert error message.", 2, 18) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "assert_string_message", "func f(flag: bool) -> void:\n\tassert(flag, \"message\")\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "assert_variant_message", "func f(flag: bool, message: Variant) -> void:\n\tassert(flag, message)\n", { E("Expected string for assert error message.", 2, 18) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, false, false },
			{ "assert_narrow_nonnull", "func get_node(maybe: Node?) -> Node:\n\tassert(maybe != null)\n\treturn maybe\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "for_narrowing_does_not_leak", "func use(values: Array[int], node: Node?) -> Node:\n\tfor _value in values:\n\t\tassert(node != null)\n\treturn node\n", { E("Cannot return value of type \"Node?\" because the function return type is \"Node\".", 4, 5) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, true, false },
			{ "for_preserves_incoming_narrowing", "func use(values: Array[int], node: Node?) -> Node:\n\tassert(node != null)\n\tfor _value in values:\n\t\tpass\n\treturn node\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false },
			{ "for_soft_scalar", "func use() -> Variant:\n\tvar values = 1\n\tfor value in values:\n\t\treturn value\n\treturn null\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "publication_return", "func use() -> int:\n\treturn 1\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "publication_for", "func use(values: Array[int]) -> int:\n\tfor value in values:\n\t\treturn value\n\treturn 0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "publication_if", "func use(flag: bool) -> int:\n\tif flag:\n\t\treturn 1\n\telse:\n\t\treturn 2\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
			{ "publication_while", "func use(flag: bool) -> int:\n\twhile flag:\n\t\treturn 1\n\treturn 0\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false },
		};
		for (const SourceCase &test_case : cases) {
			check_case(fixture, settings, "pinned_for_assert_consumers", test_case);
		}
	}
	TEST_CASE("pinned_suite_datatypes") {
		StorageFixture fixture;
		AnalyzerSettings settings;
		settings.warnings_enabled(false);
		struct DatatypeCase {
			const char *name;
			const char *source;
			std::vector<NodeTypeExpectation> nodes;
		};
		const DatatypeCase cases[] = {
			{ "array_conversion", "func use() -> float:\n\tfor value: float in [1, 2]:\n\t\treturn value\n\treturn 0.0\n", { { "body/0/list", "Array[float]", -1 }, { "body/0/list/0", "float", -1 }, { "body/0/list/1", "float", -1 }, { "body/0/iterator", "float", 2 } } },
			{ "dictionary_conversion", "func use() -> StringName:\n\tfor key: StringName in {\"a\": 1}:\n\t\treturn key\n\treturn &\"\"\n", { { "body/0/list", "Dictionary[StringName, Variant]", -1 }, { "body/0/list/key0", "StringName", -1 }, { "body/0/list/value0", "int", -1 }, { "body/0/iterator", "StringName", 2 } } },
			{ "soft_scalar", "func use() -> Variant:\n\tvar values = 1\n\tfor value in values:\n\t\treturn value\n\treturn null\n", { { "body/1/iterator", "int", 1 } } },
			{ "soft_object", "func use(source: Object) -> Variant:\n\tvar values = source\n\tfor value in values:\n\t\treturn value\n\treturn null\n", { { "body/1/iterator", "Variant", 0 } } },
			{ "range_float_conversion", "func use() -> float:\n\tfor value: float in range(1):\n\t\treturn value\n\treturn 0.0\n", { { "body/0/iterator", "float", 2 } } },
			{ "object_downcast", "func use(values: Array[Object]) -> Variant:\n\tfor value: Node in values:\n\t\treturn value\n\treturn null\n", { { "body/0/iterator", "Node", 2 } } },
			{ "soft_annotation", "func use(values: Variant) -> Variant:\n\tfor value: int in values:\n\t\treturn value\n\treturn null\n", { { "body/0/iterator", "int", 2 } } },
			{ "return", "func use() -> int:\n\treturn 1\n", { { "body", "int", 1 }, { "body/0", "int", -1 } } },
			{ "bare_return", "func use() -> void:\n\treturn\n", { { "body", "null", 1 }, { "body/0", "null", 2 } } },
			{ "while", "func use(flag: bool) -> int:\n\twhile flag:\n\t\treturn 1\n\treturn 0\n", { { "body", "int", 1 }, { "body/0", "int", 1 }, { "body/0/loop", "int", 1 } } },
			{ "if", "func use(flag: bool) -> int:\n\tif flag:\n\t\treturn 1\n\telse:\n\t\treturn 2\n", { { "body", "int", 1 }, { "body/0", "int", 1 }, { "body/0/true", "int", 1 }, { "body/0/false", "int", 1 } } },
			{ "if_plain_else_no_merge", "func use(flag: bool) -> Variant:\n\tif flag:\n\t\treturn 1\n\telse:\n\t\treturn \"s\"\n", { { "body/0", "int", 1 }, { "body/0/false", "String", 1 } } },
			{ "if_elif_mixed", "func use(first: bool, second: bool) -> Variant:\n\tif first:\n\t\treturn 1\n\telif second:\n\t\treturn \"s\"\n\treturn null\n", { { "body/0", "String", 1 } } },
			{ "match_no_transport", "func use(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n\t\tfalse:\n\t\t\treturn 2\n", { { "body/0", "<unresolved type>", 0 }, { "body/0/0", "<unresolved type>", 0 }, { "body/0/0/block", "int", 1 } } },
			{ "pattern_array", "func use(values: Array[int]) -> void:\n\tmatch values:\n\t\t[var _a, var _b]:\n\t\t\tpass\n", { { "body/0/0/pattern0", "int", 1 } } },
			{ "pattern_dictionary", "func use(values: Dictionary[String, int]) -> void:\n\tmatch values:\n\t\t{\"a\": var _a}:\n\t\t\tpass\n", { { "body/0/0/pattern0", "int", 1 } } },
			{ "pattern_tuple", "func use(values: (int, String)) -> void:\n\tmatch values:\n\t\t(var _a, var _b):\n\t\t\tpass\n", { { "body/0/0/pattern0", "String", 1 } } },
			{ "assert", "func use(flag: bool) -> void:\n\tassert(flag)\n", { { "body/0", "bool", 2 }, { "body/0/condition", "bool", 2 } } },
		};
		for (const DatatypeCase &test_case : cases) {
			INFO(test_case.name);
			const String path = String("res://tests/native/analyzer_flow/datatype_") + test_case.name + String(".barista");
			const AnalysisResult result = analyze_source(test_case.source, path);
			check_analysis(result, {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES);
			const BSParser::FunctionNode *function = find_function(result, SNAME("use"));
			BS_TEST_REQUIRE(function != nullptr);
			for (const NodeTypeExpectation &node : test_case.nodes) {
				check_node_type(function, node);
			}
			const BSParser::Node *loop_node = find_statement_node(function, "body/0");
			if (String(test_case.name) == "range_float_conversion" || String(test_case.name) == "object_downcast" || String(test_case.name) == "soft_annotation") {
				BS_TEST_REQUIRE(loop_node != nullptr && loop_node->type == BSParser::Node::FOR);
				CHECK(static_cast<const BSParser::ForNode *>(loop_node)->use_conversion_assign);
			}
			if (String(test_case.name) == "array_conversion") {
				for (const char *node_path : { "body/0/list/0", "body/0/list/1" }) {
					const BSParser::Node *node = find_statement_node(function, node_path);
					BS_TEST_REQUIRE(node != nullptr && node->type == BSParser::Node::LITERAL);
					CHECK(static_cast<const BSParser::LiteralNode *>(node)->reduced_value.get_type() == Variant::FLOAT);
				}
			}
			if (String(test_case.name) == "dictionary_conversion") {
				const BSParser::Node *key = find_statement_node(function, "body/0/list/key0");
				const BSParser::Node *value = find_statement_node(function, "body/0/list/value0");
				BS_TEST_REQUIRE(key != nullptr && key->type == BSParser::Node::LITERAL);
				BS_TEST_REQUIRE(value != nullptr && value->type == BSParser::Node::LITERAL);
				CHECK(static_cast<const BSParser::LiteralNode *>(key)->reduced_value.get_type() == Variant::STRING_NAME);
				CHECK(static_cast<const BSParser::LiteralNode *>(value)->reduced_value.get_type() == Variant::INT);
			}
		}
		const std::pair<const char *, const char *> iterator_types[] = {
			{ "Array[int]", "int" },
			{ "Dictionary[String, int]", "String" },
			{ "PackedByteArray", "int" },
			{ "PackedInt32Array", "int" },
			{ "PackedInt64Array", "int" },
			{ "PackedFloat32Array", "float" },
			{ "PackedFloat64Array", "float" },
			{ "PackedStringArray", "String" },
			{ "PackedVector2Array", "Vector2" },
			{ "PackedVector3Array", "Vector3" },
			{ "PackedVector4Array", "Vector4" },
			{ "PackedColorArray", "Color" },
			{ "int", "int" },
			{ "float", "float" },
			{ "String", "String" },
			{ "Vector2i", "int" },
			{ "Vector3i", "int" },
			{ "Vector2", "float" },
			{ "Vector3", "float" },
			{ "Array", "Variant" },
			{ "Dictionary", "Variant" },
			{ "Variant", "Variant" },
		};
		for (const auto &types : iterator_types) {
			INFO(types.first);
			const String source = vformat("func use(values: %s) -> Variant:\n\tfor value in values:\n\t\treturn value\n\treturn null\n", types.first);
			const AnalysisResult result = analyze_source(source, String("res://tests/native/analyzer_flow/iterator_") + String(types.first).validate_filename() + String(".barista"));
			check_analysis(result, {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES);
			const BSParser::FunctionNode *function = find_function(result, SNAME("use"));
			BS_TEST_REQUIRE(function != nullptr);
			for (const char *node_path : { "body/0/iterator", "body/0/loop/0/value", "body/0/loop/0", "body/0/loop", "body/0" }) {
				check_node_type(function, { node_path, types.second, -1 });
			}
			for (const char *node_path : { "body/0/loop", "body/0" }) {
				check_node_type(function, { node_path, types.second, 1 });
			}
		}
	}
	TEST_CASE("native_iterator_annotation_nullability") {
		StorageFixture fixture;
		AnalyzerSettings settings;
		settings.warnings_enabled(false);
		settings.strict_dynamic(false);
		struct IteratorCase {
			SourceCase source;
			const char *iterator_type;
			bool conversion;
		};
		const IteratorCase cases[] = {
			{ { "nullable_identity_soft", "func use(values: Array[Node?]) -> void:\n\tfor value: Node in values:\n\t\tpass\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false }, "Node", false },
			{ { "nullable_upcast_soft", "func use(values: Array[Node?]) -> void:\n\tfor value: Object in values:\n\t\tpass\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false }, "Object", false },
			{ { "identity_hard", "func use(values: Array[Node]) -> void:\n\tfor value: Node in values:\n\t\tpass\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false }, "Node", false },
			{ { "upcast_hard", "func use(values: Array[Node]) -> void:\n\tfor value: Object in values:\n\t\tpass\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false }, "Object", false },
			{ { "downcast_hard", "func use(values: Array[Object]) -> void:\n\tfor value: Node in values:\n\t\tpass\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false }, "Node", true },
			{ { "nullable_target", "func use(values: Array[Node?]) -> void:\n\tfor value: Node? in values:\n\t\tpass\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false }, "Node?", false },
			{ { "nullable_downcast_soft", "func use(values: Array[Object?]) -> void:\n\tfor value: Node in values:\n\t\tpass\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, false, false }, "Node", true },
			{ { "nullable_identity_strict", "func use(values: Array[Node?]) -> void:\n\tfor value: Node in values:\n\t\tpass\n", {}, {}, Phase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES, true, false }, "Node", true },
			{ { "nullable_upcast_strict", "func use(values: Array[Node?]) -> void:\n\tfor value: Object in values:\n\t\tpass\n", { E("Unable to iterate on value of type \"Array[Node?]\" with variable of type \"Object\".", 2, 14) }, {}, Phase::BODY_EXPRESSION_CALLABLE_SIGNAL, true, false }, "Object", false },
		};
		for (const IteratorCase &test_case : cases) {
			check_case(fixture, settings, "native_iterator", test_case.source);
			const String path = String("res://tests/native/analyzer_flow/native_iterator_observe_") + test_case.source.name + String(".barista");
			settings.strict_null(test_case.source.strict_null);
			const AnalysisResult result = analyze_source(test_case.source.source, path);
			const BSParser::FunctionNode *function = find_function(result, SNAME("use"));
			BS_TEST_REQUIRE(function != nullptr);
			const BSParser::Node *loop_node = find_statement_node(function, "body/0");
			BS_TEST_REQUIRE(loop_node != nullptr && loop_node->type == BSParser::Node::FOR);
			CHECK(static_cast<const BSParser::ForNode *>(loop_node)->use_conversion_assign == test_case.conversion);
			check_node_type(function, { "body/0/iterator", test_case.iterator_type, -1 });
		}
	}
	TEST_CASE("internal_type_test_exhaustion") {
		StorageFixture fixture;
		const auto observed = AnalyzerMigrationTestAccess::type_test_exhaustion_controls();
		const auto repeated = AnalyzerMigrationTestAccess::type_test_exhaustion_controls();
		BS_TEST_REQUIRE(observed.size() == 8);
		BS_TEST_REQUIRE(repeated.size() == observed.size());
		int position = 0;
		for (const char *name : { "duplicate", "singleton", "canonical_collapse", "nullable", "empty", "unset_test", "match_scope", "nested_match_scope" }) {
			INFO(name);
			const auto &result = observed[position];
			const auto &again = repeated[position++];
			CHECK(result.name == name);
			CHECK(again.name == result.name);
			CHECK(again.helper == result.helper);
			CHECK(again.alternative_set == result.alternative_set);
			CHECK(again.scope_restored == result.scope_restored);
			CHECK(again.subject_type_test == result.subject_type_test);
			BS_TEST_REQUIRE(again.errors.size() == result.errors.size());
			for (int i = 0; i < result.errors.size(); ++i) {
				CHECK(again.errors[i].message == result.errors[i].message);
				CHECK(again.errors[i].line == result.errors[i].line);
				CHECK(again.errors[i].column == result.errors[i].column);
			}
			const bool expected_helper = String(name) == "duplicate" || String(name) == "singleton" || String(name) == "match_scope" || String(name) == "nested_match_scope";
			const String expected_set = !expected_helper ? String() : String(name) == "singleton" ? String("int")
																								  : String("int | int");
			CHECK(result.helper == expected_helper);
			CHECK(result.alternative_set == expected_set);
			CHECK(result.scope_restored);
			const auto &errors = result.errors;
			const bool expects_error = String(name) == "duplicate" || String(name) == "singleton";
			BS_TEST_REQUIRE(errors.size() == (expects_error ? 1 : 0));
			if (expects_error) {
				const auto &error = errors[0];
				CHECK(error.message == vformat("Every alternative of \"%s\" passes \"is int\", so this test is always true and nothing reaches its false branch. Test the narrowest alternative first.", expected_set));
				CHECK(error.line == 11);
				CHECK(error.column == 7);
			}
			if (String(name) == "match_scope" || String(name) == "nested_match_scope") {
				CHECK(result.subject_type_test);
			}
		}

		using DataType = BSParser::DataType;
		DataType integer;
		integer.kind = DataType::BUILTIN;
		integer.type_source = DataType::ANNOTATED_EXPLICIT;
		integer.builtin_type = Variant::INT;
		DataType enumeration = integer;
		enumeration.kind = DataType::ENUM;
		enumeration.enum_type = SNAME("E");
		enumeration.native_type = SNAME("E");
		auto compatibility = [](const DataType &target, const DataType &source, bool conversion, bool strict_null) {
			BSTypeCompatibility::Options options;
			options.allow_implicit_conversion = conversion;
			options.strict_null = strict_null;
			return BSTypeCompatibility::check(target, source, options);
		};
		auto flags = [](const BSTypeCompatibility::Result &result, bool compatible, bool runtime, bool conversion) {
			CHECK(result.compatible == compatible);
			CHECK(result.requires_runtime_check == runtime);
			CHECK(result.uses_implicit_conversion == conversion);
		};
		flags(compatibility(integer, enumeration, false, false), true, false, false);
		flags(compatibility(integer, enumeration, true, false), true, false, true);
		flags(compatibility(enumeration, integer, false, false), true, false, false);
		flags(compatibility(enumeration, integer, true, false), true, false, false);
		DataType tagged = enumeration;
		tagged.is_tagged_union = true;
		tagged.builtin_type = Variant::ARRAY;
		flags(compatibility(integer, tagged, true, false), false, false, false);
		flags(compatibility(tagged, integer, true, false), false, false, false);
		DataType meta = enumeration;
		meta.is_meta_type = true;
		meta.builtin_type = Variant::DICTIONARY;
		flags(compatibility(integer, meta, true, false), false, false, false);
		meta.is_type_handle_annotation = true;
		flags(compatibility(meta, integer, true, false), false, false, false);
		DataType nullable_enum = enumeration;
		nullable_enum.is_nullable = true;
		DataType nullable_int = integer;
		nullable_int.is_nullable = true;
		flags(compatibility(integer, nullable_enum, false, true), false, false, false);
		flags(compatibility(integer, nullable_enum, false, false), true, true, false);
		flags(compatibility(enumeration, nullable_int, false, true), false, false, false);
		flags(compatibility(enumeration, nullable_int, false, false), true, true, false);
		flags(compatibility(nullable_enum, nullable_int, false, true), true, false, false);
		flags(compatibility(nullable_int, nullable_enum, false, true), true, false, false);
		DataType nil = integer;
		nil.builtin_type = Variant::NIL;
		flags(compatibility(nullable_int, nil, false, true), true, false, false);
		flags(compatibility(nullable_enum, nil, true, true), true, false, false);
		DataType nullable_native = nullable_int;
		nullable_native.kind = DataType::NATIVE;
		nullable_native.builtin_type = Variant::OBJECT;
		nullable_native.native_type = SNAME("Node");
		flags(compatibility(nullable_native, nil, false, true), true, false, false);
		DataType nullable_class = nullable_native;
		nullable_class.kind = DataType::CLASS;
		flags(compatibility(nullable_class, nil, false, true), true, false, false);
		flags(compatibility(integer, nil, false, false), false, false, false);
		nil.is_nullable = true;
		flags(compatibility(integer, nil, true, true), false, false, false);
		CHECK(fixture.index().get_record_count() == 0);
	}
}
