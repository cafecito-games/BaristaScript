/**************************************************************************/
/*  analyzer_declarations_test.cpp                                        */
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

// Sources and every condition migrated from analyzer_test.gd at 4b2439f (PR #199).
namespace {
void scenario_local_enum_value_cycles() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	struct Sample {
		const char *source;
		const char *path;
		const char *member;
		int line;
	};
	for (const auto &sample : std::vector<Sample>{
				 { "func test():\n\tprint(E1.V)\n\nenum E1:\n\tV = E2.V\nenum E2:\n\tV = E1.V\n", "res://tests/cyclic_ref_enum.barista", "E1", 7 },
				 { "enum Bad:\n\tA = Bad.B\n\tB = 1\n\nfunc test():\n\tprint(Bad.A)\n", "res://tests/enum_int_backed_self_referential_value.barista", "Bad", 2 },
		 }) {
		const auto result = analyze_source(sample.source, sample.path);
		BS_TEST_REQUIRE(result.parser->get_errors().size() == 2);
		const auto &first = result.parser->get_errors().front()->get();
		const auto &second = result.parser->get_errors().front()->next()->get();
		CHECK(first.message == vformat("Could not resolve member \"%s\": Cyclic reference.", sample.member));
		CHECK(first.line == sample.line);
		CHECK(first.column == 9);
		CHECK(second.message == "Enum values must be constant.");
		CHECK(second.line == sample.line);
		CHECK(second.column == 9);
	}
	CHECK(analyze_source("class_name LegalRecursiveTagged extends Node\nenum Chain:\n\tEnd\n\tLink(next: Chain)\nfunc make() -> Chain:\n\treturn Chain.Link(Chain.End)\n", "res://tests/legal_recursive_tagged.barista").valid());
}

void scenario_member_name_conflicts() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String builtin_shadow = "class_name BuiltinMemberShadow extends Node\nvar Vector2\n";
	const auto builtin_report = analyze_source(builtin_shadow, "res://tests/builtin_member_shadow.barista");
	bool saw_builtin_shadow = false;
	for (const auto &error : builtin_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Vector2") && message.contains("builtin type")) {
			saw_builtin_shadow = true;
		}
	}
	CHECK_MESSAGE(saw_builtin_shadow, "member cannot shadow builtin type");
	const String async_shadow = "class_name AsyncCallableMemberShadow extends Node\nvar AsyncCallable\n";
	const auto async_report = analyze_source(async_shadow, "res://tests/async_callable_member_shadow.barista");
	bool saw_async_shadow = false;
	for (const auto &error : async_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("AsyncCallable") && message.contains("builtin type")) {
			saw_async_shadow = true;
		}
	}
	CHECK_MESSAGE(saw_async_shadow, "member cannot shadow AsyncCallable");
	const String number_shadow = "class_name NumberMemberShadow extends Node\nclass Number:\n\tpass\n";
	const auto number_report = analyze_source(number_shadow, "res://tests/number_member_shadow.barista");
	bool saw_number_shadow = false;
	for (const auto &error : number_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Number") && message.contains("compiler-provided type")) {
			saw_number_shadow = true;
		}
	}
	CHECK_MESSAGE(saw_number_shadow, "member cannot shadow compiler-provided Number");
	const String native_shadow = "class_name NativeMemberShadow extends Node\nvar name: String\n";
	const auto native_report = analyze_source(native_shadow, "res://tests/native_member_shadow.barista");
	bool saw_native_shadow = false;
	for (const auto &error : native_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("name") && message.contains("native class")) {
			saw_native_shadow = true;
		}
	}
	CHECK_MESSAGE(saw_native_shadow, "member cannot redefine inherited native property");
	const String parent_shadow = "class_name ParentMemberShadowHost extends Node\nclass Parent extends Node:\n\tfunc ping() -> void:\n\t\tpass\nclass Child extends Parent:\n\tvar ping: int\n";
	const auto parent_report = analyze_source(parent_shadow, "res://tests/parent_member_shadow.barista");
	bool saw_parent_shadow = false;
	for (const auto &error : parent_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("ping") && message.contains("parent class")) {
			saw_parent_shadow = true;
		}
	}
	CHECK_MESSAGE(saw_parent_shadow, "non-function member cannot shadow parent method");
	const String outer_shadow = "class_name OuterMemberShadowHost extends Node\nconst TOKEN := 1\nclass Inner extends Node:\n\tvar TOKEN: int\n";
	const auto outer_report = analyze_source(outer_shadow, "res://tests/outer_member_shadow.barista");
	bool saw_outer_shadow = false;
	for (const auto &error : outer_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("TOKEN") && message.contains("outer class")) {
			saw_outer_shadow = true;
		}
	}
	CHECK_MESSAGE(saw_outer_shadow, "nested member cannot shadow visible outer constant");
	const String function_builtin_name = "class_name FunctionBuiltinName extends Node\nfunc Vector2() -> void:\n\tpass\n";
	const auto function_builtin_report = analyze_source(function_builtin_name, "res://tests/function_builtin_name.barista");
	CHECK_MESSAGE(function_builtin_report.valid() == true, "function names do not run non-function native/builtin conflict checks");
}

void scenario_builtin_annotation_resolve() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String string_name_ok = "class_name BuiltinStringNameAnnot extends Node\nfunc take(n: StringName) -> void:\n\tvar _x: StringName = n\n";
	const auto string_name_report = analyze_source(string_name_ok, "res://tests/builtin_string_name_annot.barista");
	CHECK_MESSAGE(string_name_report.valid() == true, "StringName annotation resolves as builtin");
	const String node_path_ok = "class_name BuiltinNodePathAnnot extends Node\nfunc take(p: NodePath) -> void:\n\tvar _x: NodePath = p\n";
	const auto node_path_report = analyze_source(node_path_ok, "res://tests/builtin_node_path_annot.barista");
	CHECK_MESSAGE(node_path_report.valid() == true, "NodePath annotation resolves as builtin");
	const String bare_array_ok = "class_name BuiltinBareArrayAnnot extends Node\nfunc take(a: Array) -> void:\n\tvar _x: Array = a\n";
	const auto bare_array_report = analyze_source(bare_array_ok, "res://tests/builtin_bare_array_annot.barista");
	CHECK_MESSAGE(bare_array_report.valid() == true, "bare Array annotation resolves as builtin");
	const String callable_ok = "class_name BuiltinCallableAnnot extends Node\nfunc take(c: Callable) -> void:\n\tvar _x: Callable = c\n";
	const auto callable_report = analyze_source(callable_ok, "res://tests/builtin_callable_annot.barista");
	CHECK_MESSAGE(callable_report.valid() == true, "bare Callable annotation resolves as builtin");
	const String callable_sig_ok = "class_name BuiltinCallableSigAnnot extends Node\nfunc take(c: Callable[[int], void]) -> void:\n\tvar _x: Callable[[int], void] = c\n";
	const auto callable_sig_report = analyze_source(callable_sig_ok, "res://tests/builtin_callable_sig_annot.barista");
	CHECK_MESSAGE(callable_sig_report.valid() == true, "Callable[[int], void] signature annotation resolves");
	const String signal_ok = "class_name BuiltinSignalAnnot extends Node\nfunc take(s: Signal) -> void:\n\tvar _x: Signal = s\n";
	const auto signal_report = analyze_source(signal_ok, "res://tests/builtin_signal_annot.barista");
	CHECK_MESSAGE(signal_report.valid() == true, "bare Signal annotation resolves as builtin");
	const String number_ok = "class_name BuiltinNumberAnnot extends Node\nfunc take(n: Number) -> void:\n\tvar _x: Number = 1\n";
	const auto number_report = analyze_source(number_ok, "res://tests/builtin_number_annot.barista");
	CHECK_MESSAGE(number_report.valid() == true, "Number annotation resolves as int|float union");
	const String unknown = "class_name BuiltinUnknownAnnot extends Node\nfunc take(x: NotARealType) -> void:\n\tpass\n";
	const auto unknown_report = analyze_source(unknown, "res://tests/builtin_unknown_annot.barista");
	CHECK_MESSAGE(unknown_report.valid() == false, "unknown type annotation remains invalid");
	bool saw_unknown = false;
	for (const auto &error : unknown_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Could not find type \"NotARealType\"")) {
			saw_unknown = true;
		}
	}
	CHECK_MESSAGE(saw_unknown, "unknown type keeps Could not find type diagnostic");
	const String mismatch = "class_name BuiltinStringNameMismatch extends Node\nfunc take() -> void:\n\tvar _n: StringName = 123\n";
	const auto mismatch_report = analyze_source(mismatch, "res://tests/builtin_string_name_mismatch.barista");
	CHECK_MESSAGE(mismatch_report.valid() == false, "int → StringName annotation assign remains invalid");
}
void scenario_type_alias_surface() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String transparent_source = "type Meters = float\n\nfunc measure(distance: Meters) -> Meters:\n\treturn distance * 2.0\n\nfunc test():\n\tvar distance: Meters = 1.5\n\tprint(measure(distance))\n";
	const auto transparent_report = analyze_source(transparent_source, "res://tests/type_alias_resolution.barista");
	CHECK_MESSAGE((transparent_report.valid() == true), "type alias expands transparently in signatures and locals");
	const String cycle_source = "type Left = Right\ntype Right = Left\ntype SelfReferential = SelfReferential | int\n\n\nfunc test():\n\tvar value: Left = 1\n\tprint(value)\n";
	const auto cycle_report = analyze_source(cycle_source, "res://tests/type_alias_cycle.barista");
	String cycle_errors;
	for (const auto &error : cycle_report.parser->get_errors()) {
		cycle_errors += error.message + String("\n");
	}
	CHECK_MESSAGE((cycle_report.valid() == false), "cyclic type aliases invalidate analysis");
	CHECK_MESSAGE((cycle_errors.contains("Type alias \"Left\" -> \"Right\" -> \"Left\" expands to itself")), "mutual type alias cycle names its chain");
	CHECK_MESSAGE((cycle_errors.contains("Type alias \"SelfReferential\" -> \"SelfReferential\" expands to itself")), "self type alias cycle is diagnosed");
	const String unknown_source = "type Mixed = int | NotAType\n\n\nfunc test():\n\tvar value: Mixed = 1\n\tprint(value)\n";
	const auto unknown_report = analyze_source(unknown_source, "res://tests/type_alias_unknown_member.barista");
	String unknown_errors;
	for (const auto &error : unknown_report.parser->get_errors()) {
		unknown_errors += error.message + String("\n");
	}
	CHECK_MESSAGE((unknown_report.valid() == false), "unresolvable type alias invalidates analysis");
	CHECK_MESSAGE((unknown_errors.contains("Type alias \"Mixed\" has no expansion")), "unresolvable alias reports declaration failure");
	CHECK_MESSAGE((unknown_errors.contains("Could not find type \"NotAType\"")), "unresolvable alias reports missing member");
	const String expression_source = "class Holder:\n\tvar label: String = \"holder\"\n\n\ntype Only = Holder\ntype Scalar = int | String\n\n\nfunc test():\n\tvar read = Scalar\n\tvar called = Scalar()\n\tvar made = Only.new()\n\tprints(read, called, made)\n";
	const auto expression_report = analyze_source(expression_source, "res://tests/type_alias_not_an_expression.barista");
	String expression_errors;
	for (const auto &error : expression_report.parser->get_errors()) {
		expression_errors += error.message + String("\n");
	}
	CHECK_MESSAGE((expression_errors.contains("Type alias \"Scalar\" can only be used in a type position")), "type alias has no expression value");
	CHECK_MESSAGE((expression_errors.contains("Type alias \"Only\" can only be used in a type position")), "class alias has no constructor handle");
	const String conflict_source = "type int = String\ntype Label = float\n\n\nfunc test():\n\tprint(\"unreachable\")\n";
	const auto conflict_report = analyze_source(conflict_source, "res://tests/type_alias_hides_existing_type.barista");
	String conflict_errors;
	for (const auto &error : conflict_report.parser->get_errors()) {
		conflict_errors += error.message + String("\n");
	}
	CHECK_MESSAGE((conflict_errors.contains("Type alias \"int\" hides a built-in type")), "type alias cannot hide builtin type");
	CHECK_MESSAGE((conflict_errors.contains("Type alias \"Label\" hides a native class")), "type alias cannot hide native class");
}

} // namespace

TEST_SUITE("analyzer_declarations") {
	TEST_CASE("local_enum_value_cycles") { scenario_local_enum_value_cycles(); }
	TEST_CASE("type_alias_surface") { scenario_type_alias_surface(); }
	TEST_CASE("member_name_conflicts") { scenario_member_name_conflicts(); }
	TEST_CASE("builtin_annotation_resolve") { scenario_builtin_annotation_resolve(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_member_name_conflicts, scenario_builtin_annotation_resolve, scenario_type_alias_surface, scenario_local_enum_value_cycles });
	}
}
