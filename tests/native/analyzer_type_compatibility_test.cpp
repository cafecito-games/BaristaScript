/**************************************************************************/
/*  analyzer_type_compatibility_test.cpp                                  */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "analyzer_helpers.h"
#include "doctest.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <string>
#include <vector>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
using ParserError = BSParser::ParserError;

struct ExactError {
	const char *message;
	int line;
	int column;
};

String named_class(const char *body) {
	return "class_name " + String(body);
}

AnalysisResult inspect_source(const String &source, const String &path) {
	AnalysisResult result = analyze_source(source, path);
	CHECK(result.parser != nullptr);
	if (result.parser != nullptr) {
		CHECK(result.parser->get_warnings().is_empty());
	}
	return result;
}

bool valid_source(const String &source, const String &path) {
	const AnalysisResult result = inspect_source(source, path);
	if (result.parser == nullptr) {
		return false;
	}
	const Vector<const ParserError *> errors = result.parser->get_errors_in_source_order();
	CHECK(result.parser->get_errors().size() == errors.size());
	CHECK(result.valid() == errors.is_empty());
	return result.valid();
}

bool has_error_containing(const AnalysisResult &result, const String &fragment) {
	if (result.parser == nullptr) {
		return false;
	}
	for (const ParserError *error : result.parser->get_errors_in_source_order()) {
		if (error != nullptr && error->message.contains(fragment)) {
			return true;
		}
	}
	return false;
}

int count_errors_containing(const AnalysisResult &result, const String &fragment) {
	if (result.parser == nullptr) {
		return 0;
	}
	int count = 0;
	for (const ParserError *error : result.parser->get_errors_in_source_order()) {
		if (error != nullptr && error->message.contains(fragment)) {
			++count;
		}
	}
	return count;
}

bool errors_are_exact(const AnalysisResult &result, const std::vector<ExactError> &expected) {
	if (result.parser == nullptr) {
		return false;
	}
	const Vector<const ParserError *> errors = result.parser->get_errors_in_source_order();
	if (size_t(errors.size()) != expected.size()) {
		return false;
	}
	for (int i = 0; i < errors.size(); ++i) {
		const ParserError *actual = errors[i];
		if (actual == nullptr || actual->message != expected[size_t(i)].message || actual->line != expected[size_t(i)].line || actual->column != expected[size_t(i)].column) {
			return false;
		}
	}
	return true;
}

const BSParser::VariableNode *local_variable(const AnalysisResult &result, const StringName &function_name, int statement) {
	const BSParser::FunctionNode *function = find_function(result, function_name);
	if (function == nullptr || function->body == nullptr || statement < 0 || statement >= function->body->statements.size()) {
		return nullptr;
	}
	const BSParser::Node *node = function->body->statements[statement];
	return node != nullptr && node->type == BSParser::Node::VARIABLE ? static_cast<const BSParser::VariableNode *>(node) : nullptr;
}

void scenario_union_union_assignability() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	settings.warnings_enabled(false);
	const String number_self = named_class("NumberToNumberAssign extends Node\nfunc take(n: Number) -> void:\n\tvar _x: Number = n\n");
	const AnalysisResult number_result = inspect_source(number_self, "res://tests/native/analyzer_type_compatibility/number_to_number_assign.barista");
	CHECK(number_result.valid());
	const BSParser::VariableNode *number_local = local_variable(number_result, SNAME("take"), 0);
	BS_TEST_REQUIRE(number_local != nullptr && number_local->initializer != nullptr);
	CHECK(number_local->get_datatype().is_union());
	CHECK(number_local->get_datatype().union_members.size() == 2);
	CHECK(number_local->initializer->get_datatype().is_union());
	CHECK(valid_source(named_class("WrittenUnionSelfAssign extends Node\nfunc take(v: int | String) -> void:\n\tvar _x: int | String = v\n"), "res://tests/native/analyzer_type_compatibility/written_union_self_assign.barista"));
	CHECK(valid_source(named_class("WrittenUnionReorderAssign extends Node\nfunc take(v: String | int) -> void:\n\tvar _x: int | String = v\n"), "res://tests/native/analyzer_type_compatibility/written_union_reorder_assign.barista"));
	CHECK(valid_source(named_class("WrittenToNumberAssign extends Node\nfunc take(v: int | float) -> void:\n\tvar _x: Number = v\n"), "res://tests/native/analyzer_type_compatibility/written_to_number_assign.barista"));
	CHECK_FALSE(valid_source(named_class("UnionPartialAssign extends Node\nfunc take(v: int | String) -> void:\n\tvar _s: String = v\n"), "res://tests/native/analyzer_type_compatibility/union_partial_assign.barista"));
	CHECK_FALSE(valid_source(named_class("NumberToFloatAssign extends Node\nfunc take(n: Number) -> void:\n\tvar _f: float = n\n"), "res://tests/native/analyzer_type_compatibility/number_to_float_assign.barista"));
	CHECK(valid_source(named_class("IntToNumberAssign extends Node\nfunc take(n: int) -> void:\n\tvar _x: Number = n\n"), "res://tests/native/analyzer_type_compatibility/int_to_number_assign.barista"));
	CHECK(source_analyzes(number_self, "res://tests/native/analyzer_type_compatibility/number_to_number_validate.barista"));
	CHECK(source_analyzes(number_self, "res://tests/native/analyzer_type_compatibility/number_to_number_is_valid.barista"));
	CHECK(fixture.index().get_record_count() == 0);
}

void scenario_union_store_carrier_select() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	settings.warnings_enabled(false);
	const String exact = named_class("UnionExactPrefersInt extends Node\nfunc take(n: int) -> void:\n\tvar _x: int | float = n\n");
	const AnalysisResult exact_result = inspect_source(exact, "res://tests/native/analyzer_type_compatibility/union_exact_prefers_int.barista");
	CHECK(exact_result.valid());
	const BSParser::VariableNode *exact_local = local_variable(exact_result, SNAME("take"), 0);
	BS_TEST_REQUIRE(exact_local != nullptr && exact_local->initializer != nullptr);
	CHECK(exact_local->get_datatype().is_union());
	CHECK(exact_local->initializer->get_datatype().builtin_type == Variant::INT);
	CHECK_FALSE(exact_local->use_conversion_assign);
	CHECK(valid_source(named_class("UnionNumericStoreWiden extends Node\nfunc take(n: int) -> void:\n\tvar _x: float | String = n\n"), "res://tests/native/analyzer_type_compatibility/union_numeric_store_widen.barista"));
	CHECK(valid_source(named_class("PlainStringToStringName extends Node\nfunc take() -> void:\n\tvar _n: StringName = \"ready\"\n"), "res://tests/native/analyzer_type_compatibility/plain_string_to_string_name.barista"));
	CHECK_FALSE(valid_source(named_class("UnionRejectsStringNameBridge extends Node\nfunc take() -> void:\n\tvar _x: StringName | int = \"ready\"\n"), "res://tests/native/analyzer_type_compatibility/union_rejects_string_name_bridge.barista"));
	CHECK_FALSE(valid_source(named_class("UnionRejectsStringParamBridge extends Node\nfunc take(s: String) -> void:\n\tvar _x: StringName | Node = s\n"), "res://tests/native/analyzer_type_compatibility/union_rejects_string_param_bridge.barista"));
	CHECK(source_analyzes(exact, "res://tests/native/analyzer_type_compatibility/union_store_carrier_validate.barista"));
	CHECK(source_analyzes(exact, "res://tests/native/analyzer_type_compatibility/union_store_carrier_is_valid.barista"));
	CHECK(fixture.index().get_record_count() == 0);
}

void scenario_enum_self_payload_field_leg() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	settings.warnings_enabled(false);
	CHECK(valid_source(named_class("EnumSelfSameReceiver extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_all() -> void:\n\tvar made := Message.Attach(1, self)\n\tvar qualified := self.Message.Attach(2, self)\n\tvar shorthand: Message = .Attach(3, self)\n"), "res://tests/native/analyzer_type_compatibility/enum_self_same_receiver.barista"));
	const AnalysisResult foreign = inspect_source(named_class("EnumSelfForeign extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_foreign(other: EnumSelfForeign) -> void:\n\tvar bad := Message.Attach(1, other)\n"), "res://tests/native/analyzer_type_compatibility/enum_self_foreign.barista");
	CHECK_FALSE(foreign.valid());
	CHECK(has_error_containing(foreign, "Invalid argument 2 for enum case \"Message.Attach\""));
	CHECK(valid_source(named_class("EnumSelfStaticOk extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nstatic func construct_static(value: EnumSelfStaticOk) -> void:\n\tvar made := Message.Attach(3, value)\n"), "res://tests/native/analyzer_type_compatibility/enum_self_static_ok.barista"));
	CHECK(valid_source(named_class("EnumSelfHandleOk extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_handle(value: EnumSelfHandleOk) -> void:\n\tvar made := EnumSelfHandleOk.Message.Attach(4, value)\n"), "res://tests/native/analyzer_type_compatibility/enum_self_handle_ok.barista"));
	CHECK_FALSE(valid_source(named_class("EnumSelfHandleBad extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_handle() -> void:\n\tvar bad := EnumSelfHandleBad.Message.Attach(1, \"nope\")\n"), "res://tests/native/analyzer_type_compatibility/enum_self_handle_bad.barista"));
	CHECK(valid_source(named_class("EnumSelfBaseOk extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_via_base(receiver: EnumSelfBaseOk) -> void:\n\tvar made := receiver.Message.Attach(8, receiver)\n"), "res://tests/native/analyzer_type_compatibility/enum_self_base_ok.barista"));
	CHECK_FALSE(valid_source(named_class("EnumSelfBaseBad extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_via_base(receiver: EnumSelfBaseBad, other: EnumSelfBaseBad) -> void:\n\tvar first := receiver.Message.Attach(1, other)\n\tvar second := receiver.Message.Attach(2, self)\n"), "res://tests/native/analyzer_type_compatibility/enum_self_base_bad.barista"));
	CHECK(valid_source(named_class("EnumSelfLiteralStatic extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nstatic func construct_static(value: Self) -> void:\n\tvar made := Self.Message.Attach(1, value)\n"), "res://tests/native/analyzer_type_compatibility/enum_self_literal_static.barista"));
	const AnalysisResult self_handle = inspect_source(named_class("EnumSelfHandleFrame extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_via_self_handle(other: EnumSelfHandleFrame) -> void:\n\tvar good := Self.Message.Attach(1, self)\n\tvar bad := Self.Message.Attach(2, other)\n"), "res://tests/native/analyzer_type_compatibility/enum_self_handle_frame.barista");
	CHECK_FALSE(self_handle.valid());
	CHECK(count_errors_containing(self_handle, "Invalid argument 2 for enum case \"Message.Attach\"") == 1);
	CHECK(valid_source("final class_name EnumSelfFinalSolo extends Node\nenum Note:\n\tTag(owner: Self)\nfunc construct_with_value(value: EnumSelfFinalSolo) -> void:\n\tvar made := Note.Tag(value)\n", "res://tests/native/analyzer_type_compatibility/enum_self_final_solo.barista"));
	CHECK(fixture.index().get_record_count() == 0);
}

void scenario_self_contract_assign_return() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	settings.warnings_enabled(false);
	const AnalysisResult roundtrip = inspect_source(named_class("SelfContractRoundtrip extends Node\nfunc echo(value: Self) -> Self:\n\tvar tmp: Self = value\n\ttmp = self\n\treturn tmp\n"), "res://tests/native/analyzer_type_compatibility/self_contract_roundtrip.barista");
	CHECK(roundtrip.valid());
	const BSParser::VariableNode *tmp = local_variable(roundtrip, SNAME("echo"), 0);
	BS_TEST_REQUIRE(tmp != nullptr && tmp->initializer != nullptr);
	CHECK(tmp->get_datatype().kind == BSParser::DataType::TYPE_PARAMETER);
	CHECK(tmp->get_datatype().to_string() == "Self");
	const AnalysisResult foreign = inspect_source(named_class("SelfContractForeign extends Node\nfunc bad(other: SelfContractForeign) -> Self:\n\tvar tmp: Self = other\n\ttmp = other\n\treturn other\n"), "res://tests/native/analyzer_type_compatibility/self_contract_foreign.barista");
	CHECK_FALSE(foreign.valid());
	const bool saw_assign = has_error_containing(foreign, "cannot be assigned to a variable of type \"Self\"") || has_error_containing(foreign, "Cannot assign a value of type");
	CHECK(saw_assign);
	CHECK(has_error_containing(foreign, "Cannot return value of type"));
	CHECK(valid_source(named_class("SelfContractMemberOk extends Node\nvar holder: Self\nfunc store(value: Self) -> void:\n\tholder = value\n\tholder = self\n"), "res://tests/native/analyzer_type_compatibility/self_contract_member_ok.barista"));
	CHECK_FALSE(valid_source(named_class("SelfContractMemberBad extends Node\nvar holder: Self\nfunc store(other: SelfContractMemberBad) -> void:\n\tholder = other\n"), "res://tests/native/analyzer_type_compatibility/self_contract_member_bad.barista"));
	CHECK(valid_source(named_class("SelfContractCallOk extends Node\nfunc take(value: Self) -> void:\n\tpass\nfunc use() -> void:\n\ttake(self)\n\tself.take(self)\n"), "res://tests/native/analyzer_type_compatibility/self_contract_call_ok.barista"));
	CHECK_FALSE(valid_source(named_class("SelfContractCallBad extends Node\nfunc take(value: Self) -> void:\n\tpass\nfunc use(other: SelfContractCallBad) -> void:\n\ttake(other)\n"), "res://tests/native/analyzer_type_compatibility/self_contract_call_bad.barista"));
	CHECK(valid_source("final class_name SelfContractFinal extends Node\nfunc echo(value: SelfContractFinal) -> Self:\n\tvar tmp: Self = value\n\treturn value\n", "res://tests/native/analyzer_type_compatibility/self_contract_final.barista"));
	CHECK(valid_source(named_class("SelfContractEnumStillOk extends Node\nenum Message:\n\tAttach(owner: Self)\nfunc construct() -> void:\n\tvar made := Message.Attach(self)\n"), "res://tests/native/analyzer_type_compatibility/self_contract_enum_still_ok.barista"));
	CHECK(fixture.index().get_record_count() == 0);
}

void scenario_self_contract_gradual_union() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	settings.warnings_enabled(false);
	const String soft_self = named_class("GradualSelfUnionSoft extends Node\nfunc drive() -> void:\n\tvar dynamic: Variant = 5\n\tvar link: int | (int, Self) = dynamic\n");
	const String soft_plain = named_class("GradualPlainUnionSoft extends Node\nfunc drive() -> void:\n\tvar dynamic: Variant = 5\n\tvar link: int | String = dynamic\n");
	settings.strict_dynamic(false);
	CHECK(valid_source(soft_self, "res://tests/native/analyzer_type_compatibility/gradual_self_union_soft.barista"));
	CHECK(valid_source(soft_plain, "res://tests/native/analyzer_type_compatibility/gradual_plain_union_soft.barista"));
	settings.strict_dynamic(true);
	CHECK_FALSE(valid_source(soft_self, "res://tests/native/analyzer_type_compatibility/gradual_self_union_strict.barista"));
	CHECK_FALSE(valid_source(soft_plain, "res://tests/native/analyzer_type_compatibility/gradual_plain_union_strict.barista"));
	settings.strict_dynamic(false);
	CHECK(valid_source(named_class("SelfFreeUnionAdmit extends Node\nfunc drive() -> void:\n\tvar link: int | (int, Self) = 5\n\tlink = 7\n"), "res://tests/native/analyzer_type_compatibility/self_free_union_admit.barista"));
	CHECK(valid_source(named_class("SelfBearingUnionOk extends Node\nfunc drive() -> void:\n\tvar pair: (int, Self) = (1, self)\n\tvar link: int | (int, Self) = pair\n"), "res://tests/native/analyzer_type_compatibility/self_bearing_union_ok.barista"));
	CHECK_FALSE(valid_source(named_class("SelfBearingUnionBad extends Node\nfunc drive(other: SelfBearingUnionBad) -> void:\n\tvar pair: (int, SelfBearingUnionBad) = (1, other)\n\tvar link: int | (int, Self) = pair\n"), "res://tests/native/analyzer_type_compatibility/self_bearing_union_bad.barista"));
	CHECK(fixture.index().get_record_count() == 0);
}

void scenario_ordinary_assignment_and_return_consumers() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	settings.warnings_enabled(false);
	const AnalysisResult assignment = inspect_source("func f(v: int):\n\tvar x: String = \"ok\"\n\tx = v\n", "res://tests/native/analyzer_type_compatibility/ordinary_assignment_bad.barista");
	CHECK_FALSE(assignment.valid());
	CHECK(errors_are_exact(assignment, { { "Value of type \"int\" cannot be assigned to a variable of type \"String\".", 3, 9 } }));
	const AnalysisResult returned = inspect_source("func f(v: String) -> int:\n\treturn v\n", "res://tests/native/analyzer_type_compatibility/ordinary_return_bad.barista");
	CHECK_FALSE(returned.valid());
	CHECK(errors_are_exact(returned, { { "Cannot return value of type \"String\" because the function return type is \"int\".", 2, 5 } }));
	const AnalysisResult constants = inspect_source("const TEXT: Variant = \"hello\"\n\nfunc take_int(v: int) -> int:\n\treturn v\n\nfunc give_int() -> int:\n\treturn TEXT\n\nfunc test():\n\ttake_int(TEXT)\n\tvar initialized: int = TEXT\n\tvar assigned: int = 0\n\tassigned = TEXT\n\tprint(initialized, assigned, give_int())\n", "res://tests/native/analyzer_type_compatibility/known_constant_consumers.barista");
	CHECK(errors_are_exact(constants, {
											  { "Cannot return a value of type \"String\" as \"int\".", 7, 12 },
											  { "Cannot pass a value of type \"String\" as \"int\".", 10, 14 },
											  { "Cannot assign a value of type \"String\" as \"int\".", 11, 28 },
											  { "Cannot assign a value of type \"String\" as \"int\".", 13, 16 },
									  }));
	const AnalysisResult return_shape = inspect_source("func void_bad() -> void:\n\treturn 1\nfunc bare_bad() -> int:\n\treturn\n", "res://tests/native/analyzer_type_compatibility/return_shape_bad.barista");
	CHECK(errors_are_exact(return_shape, {
												 { "A void function cannot return a value.", 2, 5 },
												 { "Cannot return without a value because the function return type is \"int\".", 4, 5 },
										 }));
	const AnalysisResult direct_constant = inspect_source("func test():\n\tconst TEST = 25\n\tTEST = 50\n", "res://tests/native/analyzer_type_compatibility/direct_constant_write.barista");
	CHECK(errors_are_exact(direct_constant, { { "Cannot assign a new value to a constant.", 3, 5 } }));
	const AnalysisResult match_bind = inspect_source("enum Box:\n\tValue(v: Variant)\nfunc inspect(box: Box) -> void:\n\tmatch box:\n\t\tBox.Value(b):\n\t\t\tif b is int:\n\t\t\t\tb = 5\n\t\t_:\n\t\t\tpass\n", "res://tests/native/analyzer_type_compatibility/match_bind_write.barista");
	CHECK(errors_are_exact(match_bind, { { "Cannot assign a new value to a constant.", 7, 17 } }));
	CHECK(fixture.index().get_record_count() == 0);
}
} // namespace

TEST_SUITE("analyzer_type_compatibility") {
	TEST_CASE("union_union_assignability") { scenario_union_union_assignability(); }
	TEST_CASE("union_store_carrier_select") { scenario_union_store_carrier_select(); }
	TEST_CASE("enum_self_payload_field_leg") { scenario_enum_self_payload_field_leg(); }
	TEST_CASE("self_contract_assign_return") { scenario_self_contract_assign_return(); }
	TEST_CASE("self_contract_gradual_union") { scenario_self_contract_gradual_union(); }
	TEST_CASE("ordinary_assignment_and_return_consumers") { scenario_ordinary_assignment_and_return_consumers(); }

	TEST_CASE("repeated_and_reversed_cases_restore_ambient_state") {
		void (*scenarios[])() = {
			scenario_union_union_assignability,
			scenario_union_store_carrier_select,
			scenario_enum_self_payload_field_leg,
			scenario_self_contract_assign_return,
			scenario_self_contract_gradual_union,
			scenario_ordinary_assignment_and_return_consumers,
		};
		const int count = int(sizeof(scenarios) / sizeof(scenarios[0]));
		for (int pass = 0; pass < 2; ++pass) {
			for (int i = 0; i < count; ++i) {
				CHECK(verify_case_isolation(scenarios[i]));
			}
			for (int i = count - 1; i >= 0; --i) {
				CHECK(verify_case_isolation(scenarios[i]));
			}
		}
	}
}
