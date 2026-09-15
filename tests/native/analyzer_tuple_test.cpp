/**************************************************************************/
/*  analyzer_tuple_test.cpp                                               */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "analyzer_helpers.h"
#include "analyzer_self_identity_access.h"
#include "barista_script.h"
#include "bs_conformance_registry.h"
#include "bs_declaration_index.h"
#include "storage_fixture.h"
#include "test_require.h"

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
struct SelfIdentityObservations {
	bool alpha_fixed_slot = false;
	bool strict_fixed_slot = false;
	bool strict_return_slot = false;
	bool strict_async = false;
	bool strict_rest = false;
	bool strict_nested = false;
	bool strict_union = false;
	bool strict_ignores_parser_wildcard = false;
	bool markers_match = false;
	bool markers_fixed_slot = false;
	bool markers_return_slot = false;
	bool markers_rest_slot = false;
	bool parameter_variadic_to_fixed = false;
	bool parameter_gradual_to_narrowed_rest = false;
	bool parameter_strict_return_mismatch = false;
	bool parameter_fixed_receiver_identity = false;
	bool parameter_return_receiver_identity = false;
	bool parameter_rest_receiver_identity = false;
};

SelfIdentityObservations observe_self_identity() {
	const auto builtin = [](Variant::Type p_type) {
		BSParser::DataType result;
		result.kind = BSParser::DataType::BUILTIN;
		result.builtin_type = p_type;
		result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		return result;
	};
	const BSParser::DataType int_type = builtin(Variant::INT);
	const BSParser::DataType string_type = builtin(Variant::STRING);
	const BSParser::DataType void_type = builtin(Variant::NIL);

	BSParser::DataType callable;
	callable.kind = BSParser::DataType::BUILTIN;
	callable.builtin_type = Variant::CALLABLE;
	callable.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	callable.has_method_signature = true;
	callable.method_parameter_types.push_back(int_type);
	callable.method_return_type.push_back(void_type);

	BSParser::DataType fixed_mismatch = callable;
	fixed_mismatch.method_parameter_types.write[0] = string_type;
	BSParser::DataType return_mismatch = callable;
	return_mismatch.method_return_type.write[0] = int_type;
	BSParser::DataType async_mismatch = callable;
	async_mismatch.signature_is_async = true;
	BSParser::DataType rest_mismatch = callable;
	rest_mismatch.method_info.flags |= METHOD_FLAG_VARARG;
	BSParser::DataType rest_array = builtin(Variant::ARRAY);
	rest_array.container_element_types.push_back(int_type);
	rest_mismatch.method_rest_parameter_type.push_back(rest_array);

	BSParser::DataType nested_a = builtin(Variant::ARRAY);
	nested_a.container_element_types.push_back(callable);
	BSParser::DataType nested_b = builtin(Variant::ARRAY);
	nested_b.container_element_types.push_back(return_mismatch);

	BSParser::DataType union_a;
	union_a.kind = BSParser::DataType::UNION;
	union_a.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	union_a.union_members.push_back(callable);
	union_a.union_members.push_back(string_type);
	BSParser::DataType union_b = union_a;
	union_b.union_members.write[0] = fixed_mismatch;

	BSParser::DataType self_type;
	self_type.kind = BSParser::DataType::TYPE_PARAMETER;
	self_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	self_type.type_parameter_name = SNAME("@Self");
	self_type.type_parameter_scope = BSParser::DataType::TYPE_PARAMETER_CLASS;
	self_type.type_parameter_bound.push_back(int_type);
	BSParser::DataType self_callable = callable;
	self_callable.method_parameter_types.write[0] = self_type;
	self_callable.method_return_type.write[0] = self_type;
	self_callable.method_info.flags |= METHOD_FLAG_VARARG;
	BSParser::DataType self_rest = builtin(Variant::ARRAY);
	self_rest.container_element_types.push_back(self_type);
	self_callable.method_rest_parameter_type.push_back(self_rest);
	BSParser::DataType self_container = builtin(Variant::ARRAY);
	self_container.container_element_types.push_back(self_callable);
	const BSParser::DataType marked = SelfIdentityTestAccess::substitute_self(self_container, true);
	BSParser::DataType missing_fixed_marker = marked;
	missing_fixed_marker.container_element_types.write[0].method_parameter_types.write[0].is_substituted_self = false;
	BSParser::DataType missing_return_marker = marked;
	missing_return_marker.container_element_types.write[0].method_return_type.write[0].is_substituted_self = false;
	BSParser::DataType missing_rest_marker = marked;
	missing_rest_marker.container_element_types.write[0].method_rest_parameter_type.write[0].container_element_types.write[0].is_substituted_self = false;

	BSParser::DataType receiver_self = self_type;
	receiver_self.is_receiver_self_contract = true;
	BSParser::DataType expected_fixed = callable;
	expected_fixed.method_parameter_types.write[0] = receiver_self;
	BSParser::DataType variadic_fixed = expected_fixed;
	variadic_fixed.method_parameter_types.write[0] = self_type;
	variadic_fixed.method_info.flags |= METHOD_FLAG_VARARG;
	BSParser::DataType gradual_rest = builtin(Variant::ARRAY);
	variadic_fixed.method_rest_parameter_type.push_back(gradual_rest);
	BSParser::DataType expected_rest = callable;
	expected_rest.method_parameter_types.clear();
	expected_rest.method_info.flags |= METHOD_FLAG_VARARG;
	BSParser::DataType receiver_rest = builtin(Variant::ARRAY);
	receiver_rest.container_element_types.push_back(receiver_self);
	expected_rest.method_rest_parameter_type.push_back(receiver_rest);
	BSParser::DataType gradual_callable = expected_rest;
	gradual_callable.method_rest_parameter_type.clear();
	BSParser::DataType expected_return = callable;
	expected_return.method_parameter_types.clear();
	expected_return.method_return_type.write[0] = receiver_self;
	BSParser::DataType argument_return = expected_return;
	argument_return.method_return_type.write[0] = self_type;
	BSParser::DataType argument_rest = expected_rest;
	argument_rest.method_rest_parameter_type.write[0].container_element_types.write[0] = self_type;
	BSParser::DataType mismatched_return = variadic_fixed;
	mismatched_return.method_info.flags &= ~METHOD_FLAG_VARARG;
	mismatched_return.clear_method_rest_parameter_type();
	mismatched_return.method_return_type.write[0] = int_type;
	BSParser::DataType matched_parameter;

	BSParser::DataType undetected;
	undetected.kind = BSParser::DataType::VARIANT;
	undetected.type_source = BSParser::DataType::UNDETECTED;

	SelfIdentityObservations result;
	result.alpha_fixed_slot = !SelfIdentityTestAccess::alpha_equal(callable, fixed_mismatch);
	result.strict_fixed_slot = !SelfIdentityTestAccess::strict_identity_equal(callable, fixed_mismatch);
	result.strict_return_slot = !SelfIdentityTestAccess::strict_identity_equal(callable, return_mismatch);
	result.strict_async = !SelfIdentityTestAccess::strict_identity_equal(callable, async_mismatch);
	result.strict_rest = !SelfIdentityTestAccess::strict_identity_equal(callable, rest_mismatch);
	result.strict_nested = !SelfIdentityTestAccess::strict_identity_equal(nested_a, nested_b);
	result.strict_union = !SelfIdentityTestAccess::strict_identity_equal(union_a, union_b);
	result.strict_ignores_parser_wildcard = !SelfIdentityTestAccess::strict_identity_equal(undetected, int_type);
	result.markers_match = SelfIdentityTestAccess::matches_substituted_self(self_container, marked);
	result.markers_fixed_slot = !SelfIdentityTestAccess::matches_substituted_self(self_container, missing_fixed_marker);
	result.markers_return_slot = !SelfIdentityTestAccess::matches_substituted_self(self_container, missing_return_marker);
	result.markers_rest_slot = !SelfIdentityTestAccess::matches_substituted_self(self_container, missing_rest_marker);

	result.parameter_variadic_to_fixed = SelfIdentityTestAccess::parameter_matches(expected_fixed, variadic_fixed, matched_parameter);
	result.parameter_gradual_to_narrowed_rest = SelfIdentityTestAccess::parameter_matches(expected_rest, gradual_callable, matched_parameter);
	result.parameter_strict_return_mismatch = !SelfIdentityTestAccess::parameter_matches(expected_fixed, mismatched_return, matched_parameter);
	result.parameter_fixed_receiver_identity = SelfIdentityTestAccess::needs_receiver_identity(expected_fixed, variadic_fixed);
	result.parameter_return_receiver_identity = SelfIdentityTestAccess::needs_receiver_identity(expected_return, argument_return);
	result.parameter_rest_receiver_identity = SelfIdentityTestAccess::needs_receiver_identity(expected_rest, argument_rest);
	return result;
}

void check_public(const String &source, const String &path, const std::vector<ExpectedError> &expected) {
	const Dictionary report = BaristaScriptLanguage::get_singleton()->_validate(source, path, true, true, false, false);
	CHECK(bool(report.get("valid", false)) == expected.empty());
	const Array errors = report.get("errors", Array());
	BS_TEST_REQUIRE(size_t(errors.size()) == expected.size());
	for (int i = 0; i < errors.size(); ++i) {
		const Dictionary actual = errors[i];
		CHECK(String(actual.get("message", "")) == expected[i].message.c_str());
		CHECK(int(actual.get("line", -1)) == expected[i].line);
		CHECK(int(actual.get("column", -1)) == expected[i].column);
	}
}

void check_same_errors(const AnalysisResult &first, const AnalysisResult &second) {
	BS_TEST_REQUIRE(first.parser != nullptr);
	BS_TEST_REQUIRE(second.parser != nullptr);
	const auto &a = first.parser->get_errors();
	const auto &b = second.parser->get_errors();
	BS_TEST_REQUIRE(a.size() == b.size());
	auto *right = b.front();
	for (const auto &left : a) {
		CHECK(left.message == right->get().message);
		CHECK(left.line == right->get().line);
		CHECK(left.column == right->get().column);
		right = right->next();
	}
}

void scenario_local_tuple_and_literal_consumers() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String tuple_ok = "class_name TupleLocalOk extends Node\ntuple Pair(first: int, second: String)\nfunc use() -> void:\n\tvar named: Pair = Pair(1, \"ok\")\n\tvar by_index: int = named.0\n\tvar by_field: String = named.second\n\tvar anonymous: (int, String) = (2, \"two\")\n\tvar anonymous_index: String = anonymous.1\n";
	const String tuple_bad = "class_name TupleLocalBad extends Node\ntuple Pair(first: int, second: String)\nfunc use() -> void:\n\tvar wrong_arity := Pair(1)\n\tvar wrong_field := Pair(1, 2)\n";
	const String self_tuple_ok = "class_name TupleSelfLiteralOk extends Node\nfunc take(value: (Self, int)) -> void:\n\tpass\nfunc use() -> void:\n\ttake((self, 1))\n";
	const String self_tuple_bad = "class_name TupleSelfLiteralBad extends Node\nfunc take(value: (Self, int)) -> void:\n\tpass\nfunc use(other: TupleSelfLiteralBad) -> void:\n\ttake((other, 1))\n";
	const String tuple_write_source = "func test():\n\tvar pair := (1, 2)\n\tpair.0 = 5\n";
	const String tuple_access_source = "tuple Vec2(x: float, y: float)\nfunc test():\n\tvar point := Vec2(1.0, 2.0)\n\tprint(point.z)\n\tprint((1, 2).2)\n";
	const String invalid_index_source = "func test():\n\t# Array indices must be integers.\n\tprint([0, 1][true])\n";
	const String indexed_read_source = "func use(values: Array[int], lookup: Dictionary[String, int], i: int, key: String) -> void:\n\tvar from_array: int = values[i]\n\tvar from_dictionary: int = lookup[key]\n\tvalues[i] = from_dictionary\n\tlookup[key] = from_array\n";
	const String dictionary_index_source = "func test(d: Dictionary[String, int]):\n\tprint(d[true])\n";
	const String dynamic_base_source = "func test(value: Variant):\n\tprint(value[0])\n";
	const String dynamic_index_source = "func test(values: Array[int], index: Variant):\n\tprint(values[index])\n";
	const String union_source = "class_name TupleUnionClaimants extends Node\nfunc raw_first(v: Array | Array[Self]) -> void:\n\tpass\nfunc typed_first(v: Array[Self] | Array) -> void:\n\tpass\nfunc self_choice(v: Array[Self] | Array[(Self, int)]) -> void:\n\tpass\nfunc ambiguous_first(v: Array[Self] | Array[(Self, int)]) -> void:\n\tpass\nfunc ambiguous_reordered(v: Array[(Self, int)] | Array[Self]) -> void:\n\tpass\nfunc test() -> void:\n\traw_first([self])\n\ttyped_first([self])\n\tself_choice([self])\n\tambiguous_first([])\n\tambiguous_reordered([])\n";
	const auto tuple_ok_report = analyze_source(tuple_ok, "res://tests/tuple_local_ok.barista");
	CHECK(tuple_ok_report.valid());
	const std::vector<ExpectedError> tuple_bad_expected = {
		{ "Tuple \"Pair\" expects 2 argument(s), but 1 were given.", 4, 24 },
		{ "Invalid argument 2 for tuple \"Pair\": should be \"String\" but is \"int\".", 5, 32 },
	};
	const auto tuple_bad_report = analyze_source(tuple_bad, "res://tests/tuple_local_bad.barista");
	CHECK_FALSE(tuple_bad_report.valid());
	check_public(tuple_bad, "res://tests/tuple_local_bad.barista", tuple_bad_expected);
	CHECK(analyze_source(self_tuple_ok, "res://tests/tuple_self_literal_ok.barista").valid());
	CHECK_FALSE(analyze_source(self_tuple_bad, "res://tests/tuple_self_literal_bad.barista").valid());
	check_public(self_tuple_bad, "res://tests/tuple_self_literal_bad.barista", {
																					   { "Invalid argument for \"take()\" function: argument 1 should be \"(Self, int)\" but is \"(TupleSelfLiteralBad, int)\".", 5, 10 },
																			   });
	check_public(tuple_write_source, "res://tests/tuple_write_bad.barista", {
																					{ "Cannot assign to an element of tuple \"(int, int)\"; tuples are immutable.", 3, 5 },
																			});
	check_public(tuple_access_source, "res://tests/tuple_access_bad.barista", {
																					  { "Tuple \"Vec2\" has no field named \"z\".", 4, 17 },
																					  { "Tuple index 2 is out of range for \"(int, int)\", which has 2 element(s).", 5, 18 },
																			  });
	check_public(invalid_index_source, "res://tests/invalid_array_index.barista", {
																						  { "Invalid index type \"bool\" for a base of type \"Array\".", 3, 18 },
																				  });
	CHECK(analyze_source(indexed_read_source, "res://tests/indexed_read_types.barista").valid());
	check_public(dictionary_index_source, "res://tests/dictionary_index_bad.barista", {
																							  { "Invalid index type \"bool\" for a base of type \"Dictionary[String, int]\".", 2, 13 },
																					  });
	CHECK(analyze_source(dynamic_base_source, "res://tests/dynamic_base_index.barista").valid());
	CHECK(analyze_source(dynamic_index_source, "res://tests/dynamic_index.barista").valid());
	settings.strict_dynamic(true);
	check_public(dynamic_base_source, "res://tests/dynamic_base_index.barista", {
																						{ "Cannot use subscript operator on Variant in strict dynamic mode.", 2, 11 },
																				});
	check_public(dynamic_index_source, "res://tests/dynamic_index.barista", {
																					{ "Cannot use dynamic index of type \"Variant\" for base of type \"Array[int]\" in strict dynamic mode.", 2, 18 },
																			});
	settings.strict_dynamic(false);
	const String union_positive = union_source.replace("\tambiguous_first([])\n", "").replace("\tambiguous_reordered([])\n", "");
	check_public(union_positive, "res://tests/tuple_union_positive.barista", {});
	check_public(union_source, "res://tests/tuple_union_claimants.barista", {
																					{ "Invalid argument for \"ambiguous_first()\" function: argument 1 should be \"Array[(Self, int)] | Array[Self]\" but is \"Array\".", 16, 21 },
																					{ "Invalid argument for \"ambiguous_reordered()\" function: argument 1 should be \"Array[(Self, int)] | Array[Self]\" but is \"Array\".", 17, 25 },
																			});
	const int index_before = fixture.index().get_record_count();
	const auto tuple_ok_repeat = analyze_source(tuple_ok, "res://tests/tuple_local_ok.barista");
	const auto tuple_bad_repeat = analyze_source(tuple_bad, "res://tests/tuple_local_bad.barista");
	check_same_errors(tuple_ok_report, tuple_ok_repeat);
	check_same_errors(tuple_bad_report, tuple_bad_repeat);
	check_public(tuple_ok, "res://tests/tuple_local_ok.barista", {});
	CHECK(source_analyzes(tuple_ok, "res://tests/tuple_local_ok.barista"));
	CHECK_FALSE(source_analyzes(tuple_bad, "res://tests/tuple_local_bad.barista"));
	Ref<BaristaScript> script;
	script.instantiate();
	script->_set_source_code(tuple_bad);
	script->set_path("res://tests/tuple_local_bad.barista");
	CHECK_FALSE(script->_is_valid());
	CHECK(fixture.index().get_record_count() == index_before);

	const auto identity = observe_self_identity();
	CHECK(identity.alpha_fixed_slot);
	CHECK(identity.strict_fixed_slot);
	CHECK(identity.strict_return_slot);
	CHECK(identity.strict_async);
	CHECK(identity.strict_rest);
	CHECK(identity.strict_nested);
	CHECK(identity.strict_union);
	CHECK(identity.strict_ignores_parser_wildcard);
	CHECK(identity.markers_match);
	CHECK(identity.markers_fixed_slot);
	CHECK(identity.markers_return_slot);
	CHECK(identity.markers_rest_slot);
	CHECK(identity.parameter_variadic_to_fixed);
	CHECK(identity.parameter_gradual_to_narrowed_rest);
	CHECK(identity.parameter_strict_return_mismatch);
	CHECK(identity.parameter_fixed_receiver_identity);
	CHECK(identity.parameter_return_receiver_identity);
	CHECK(identity.parameter_rest_receiver_identity);
}
} // namespace

TEST_SUITE("analyzer_tuple") {
	TEST_CASE("local_tuple_and_literal_consumers") { scenario_local_tuple_and_literal_consumers(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_local_tuple_and_literal_consumers });
	}
}
