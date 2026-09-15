/**************************************************************************/
/*  analyzer_operations_test.cpp                                          */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                          */
/*  This file is part of BaristaScript, a Godot GDExtension.                */
/*  SPDX-License-Identifier: MIT                                           */
/**************************************************************************/

#include "analyzer_helpers.h"
#include "bs_conformance_registry.h"
#include "storage_fixture.h"

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

// Sources and every condition migrated from analyzer_test.gd at 4b2439f (PR #199).
namespace {
void scenario_get_operation_type() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String unary_not = "class_name UnaryNotBoolResult extends Node\nfunc take(v: int) -> void:\n\tvar b: bool = not v\n";
	const auto unary_not_report = analyze_source(unary_not, "res://tests/unary_not_bool_result.barista");
	CHECK_MESSAGE(unary_not_report.valid() == true, "unary not on int types as bool");
	const String hard_invalid = "class_name HardInvalidBinaryAdd extends Node\nfunc test() -> void:\n\tvar _x = \"a\" + 1\n";
	const auto hard_invalid_report = analyze_source(hard_invalid, "res://tests/hard_invalid_binary_add.barista");
	CHECK_MESSAGE(hard_invalid_report.valid() == false, "String + int is invalid");
	bool saw_hard = false;
	for (const auto &error : hard_invalid_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Invalid operands") || message.contains("\"+\"")) {
			saw_hard = true;
			break;
		}
	}
	CHECK_MESSAGE(saw_hard, "String + int names the operator / operands");
	const String union_add = "class_name UnionSetWiseAddReject extends Node\nfunc take(v: int | String) -> void:\n\tvar _x = v + 1\n";
	const auto union_add_report = analyze_source(union_add, "res://tests/union_set_wise_add_reject.barista");
	CHECK_MESSAGE(union_add_report.valid() == false, "int|String + int rejected set-wise");
	bool saw_set = false;
	for (const auto &error : union_add_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("allow the combination") || message.contains("no result for")) {
			saw_set = true;
			break;
		}
	}
	CHECK_MESSAGE(saw_set, "set-wise rejection names the unsupported combination");
	const String union_eq = "class_name UnionEqualityOk extends Node\nfunc take(v: int | String) -> bool:\n\treturn v == 1\n";
	const auto union_eq_report = analyze_source(union_eq, "res://tests/union_equality_ok.barista");
	CHECK_MESSAGE(union_eq_report.valid() == true, "int|String == int is valid (equality not set-wise)");
	const String compound_union = "class_name CompoundUnionSetWiseReject extends Node\nfunc take(v: int | String) -> void:\n\tv += 1\n";
	const auto compound_union_report = analyze_source(compound_union, "res://tests/compound_union_set_wise_reject.barista");
	CHECK_MESSAGE(compound_union_report.valid() == false, "compound += on int|String without narrowing is invalid");
	const String compound_string = "class_name CompoundStringPlusIntReject extends Node\nfunc take(v: int | String) -> void:\n\tif v is String:\n\t\tv += 1\n";
	const auto compound_string_report = analyze_source(compound_string, "res://tests/compound_string_plus_int_reject.barista");
	CHECK_MESSAGE(compound_string_report.valid() == false, "compound += 1 after `is String` is invalid");
	const String compound_hard = "class_name CompoundHardInvalid extends Node\nfunc test() -> void:\n\tvar s: String = \"a\"\n\ts += 1\n";
	const auto compound_hard_report = analyze_source(compound_hard, "res://tests/compound_hard_invalid.barista");
	CHECK_MESSAGE(compound_hard_report.valid() == false, "String += int remains invalid");
	const String array_add_ok = "class_name TypedArrayAddOk extends Node\nfunc test() -> void:\n\tvar a: Array[int] = [1]\n\tvar b: Array[int] = [2]\n\tvar _c: Array[int] = a + b\n";
	const auto array_add_ok_report = analyze_source(array_add_ok, "res://tests/typed_array_add_ok.barista");
	CHECK_MESSAGE(array_add_ok_report.valid() == true, "Array[int] + Array[int] is valid");
	const String array_add_bad = "class_name TypedArrayAddMismatch extends Node\nfunc test() -> void:\n\tvar a: Array[int] = [1]\n\tvar b: Array[String] = [\"x\"]\n\tvar _c = a + b\n";
	const auto array_add_bad_report = analyze_source(array_add_bad, "res://tests/typed_array_add_mismatch.barista");
	CHECK_MESSAGE(array_add_bad_report.valid() == false, "Array[int] + Array[String] is invalid");
}
} // namespace

TEST_SUITE("analyzer_operations") {
	TEST_CASE("get_operation_type") { scenario_get_operation_type(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_get_operation_type });
	}
}
