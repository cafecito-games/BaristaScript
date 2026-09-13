/**************************************************************************/
/*  analyzer_members_test.cpp                                             */
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

// Exact source/predicate migration of the 4b2439f legacy member-resolution cases.
namespace {
void scenario_surface_inheritance_member_depth() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String same_file_ok = "class_name InheritSameFileHost extends Node\nclass Parent extends Node:\n\tvar parent_value: int = 1\n\tfunc parent_add(a: int, b: int) -> int:\n\t\treturn a + b\nclass Child extends Parent:\n\tfunc use() -> int:\n\t\treturn parent_add(1, 2) + parent_value + self.parent_value\n";
	const auto same_file_ok_report = analyze_source(same_file_ok, "res://tests/inherit_same_file_ok.barista");
	CHECK_MESSAGE((same_file_ok_report.valid() == true), "same-file Child extends Parent inherits members");
	const String same_file_arity = "class_name InheritSameFileArity extends Node\nclass Parent extends Node:\n\tfunc parent_add(a: int, b: int) -> int:\n\t\treturn a + b\nclass Child extends Parent:\n\tfunc use() -> void:\n\t\tparent_add(1)\n";
	const auto same_file_arity_report = analyze_source(same_file_arity, "res://tests/inherit_same_file_arity.barista");
	CHECK_MESSAGE((same_file_arity_report.valid() == false), "inherited same-file method arity is validated");
	bool saw_same_file_arity = false;
	for (const auto &error : same_file_arity_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Too few arguments") && message.contains("parent_add")) {
			saw_same_file_arity = true;
		}
	}
	CHECK_MESSAGE((saw_same_file_arity), "same-file inherited call too-few-arguments diagnostic");
	const String same_file_type = "class_name InheritSameFileType extends Node\nclass Parent extends Node:\n\tfunc parent_add(a: int, b: int) -> int:\n\t\treturn a + b\nclass Child extends Parent:\n\tfunc use() -> void:\n\t\tparent_add(1, 1.5)\n";
	const auto same_file_type_report = analyze_source(same_file_type, "res://tests/inherit_same_file_type.barista");
	CHECK_MESSAGE((same_file_type_report.valid() == false), "inherited same-file method arg types are validated");
	const String nested_extends = "class_name InheritNestedHost extends Node\nclass Outer extends Node:\n\tclass Inner extends Node:\n\t\tfunc inner_add(a: int, b: int) -> int:\n\t\t\treturn a + b\nclass Child extends Outer.Inner:\n\tfunc use() -> int:\n\t\treturn inner_add(1, 2)\n";
	const auto nested_extends_report = analyze_source(nested_extends, "res://tests/inherit_nested_extends.barista");
	CHECK_MESSAGE((nested_extends_report.valid() == true), "extends Outer.Inner nested CLASS chain resolves");
	const String final_base = "class_name InheritFinalHost extends Node\nfinal class Sealed extends Node:\n\tpass\nclass Child extends Sealed:\n\tpass\n";
	const auto final_base_report = analyze_source(final_base, "res://tests/inherit_final_base.barista");
	CHECK_MESSAGE((final_base_report.valid() == false), "extending a final class is invalid");
	bool saw_final = false;
	for (const auto &error : final_base_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot extend final class")) {
			saw_final = true;
		}
	}
	CHECK_MESSAGE((saw_final), "final class extends diagnostic");
	const String trait_base = "class_name InheritTraitHost extends Node\ntrait SomeTrait:\n\tpass\nclass Child extends SomeTrait:\n\tpass\n";
	const auto trait_base_report = analyze_source(trait_base, "res://tests/inherit_trait_base.barista");
	CHECK_MESSAGE((trait_base_report.valid() == false), "extending a trait is invalid");
	bool saw_trait = false;
	for (const auto &error : trait_base_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("cannot extend trait") && message.contains("use \"uses")) {
			saw_trait = true;
		}
	}
	CHECK_MESSAGE((saw_trait), "trait-as-base Foundry diagnostic");
	const String cycle_member_a = "class_name InheritCycleMemberA extends \"res://tests/inherit_cycle_member_b.barista\"\nfunc use() -> int:\n\treturn base_add(1, 2) + base_value + self.base_value\n";
	BSCache::set_source_override("res://tests/inherit_cycle_member_a.barista", cycle_member_a);
	BSCache::set_source_override("res://tests/inherit_cycle_member_b.barista", "class_name InheritCycleMemberB extends \"res://tests/inherit_cycle_member_a.barista\"\nvar base_value: int = 1\nfunc base_add(a: int, b: int) -> int:\n\treturn a + b\n");
	const auto cycle_member_report = analyze_source(cycle_member_a, "res://tests/inherit_cycle_member_a.barista");
	CHECK_MESSAGE((cycle_member_report.parser != nullptr), "cyclic path-extends with member use terminates");
	BSCache::clear_source_override("res://tests/inherit_cycle_member_a.barista");
	BSCache::clear_source_override("res://tests/inherit_cycle_member_b.barista");
	const String self_extends = "class_name InheritSelfExtends extends Node\nclass Foo extends Foo:\n\tfunc use() -> void:\n\t\tuse()\n";
	const auto self_extends_report = analyze_source(self_extends, "res://tests/inherit_self_extends.barista");
	CHECK_MESSAGE((self_extends_report.valid() == false), "same-file self-extends is invalid");
	bool saw_self_cycle = false;
	for (const auto &error : self_extends_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cyclic reference")) {
			saw_self_cycle = true;
		}
	}
	CHECK_MESSAGE((saw_self_cycle), "same-file self-extends cyclic diagnostic");
	const String base_source = "class_name InheritCrossBase extends Node\nvar base_value: int = 7\nfunc base_add(a: int, b: int) -> int:\n\treturn a + b\n";
	BSCache::set_source_override("res://tests/inherit_cross_base.barista", base_source);
	const String derived_ok = "class_name InheritCrossDerived extends \"res://tests/inherit_cross_base.barista\"\nfunc use() -> int:\n\treturn base_add(1, 2) + base_value + self.base_value\n";
	const auto derived_ok_report = analyze_source(derived_ok, "res://tests/inherit_cross_derived_ok.barista");
	CHECK_MESSAGE((derived_ok_report.valid() == true), "cross-file extends path inherits base members");
	const String derived_arity = "class_name InheritCrossDerivedArity extends \"res://tests/inherit_cross_base.barista\"\nfunc use() -> void:\n\tbase_add(1)\n";
	const auto derived_arity_report = analyze_source(derived_arity, "res://tests/inherit_cross_derived_arity.barista");
	CHECK_MESSAGE((derived_arity_report.valid() == false), "cross-file inherited method arity is validated");
	bool saw_cross_arity = false;
	for (const auto &error : derived_arity_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Too few arguments") && message.contains("base_add")) {
			saw_cross_arity = true;
		}
	}
	CHECK_MESSAGE((saw_cross_arity), "cross-file inherited call too-few-arguments diagnostic");
	BSCache::clear_source_override("res://tests/inherit_cross_base.barista");
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(same_file_ok, "res://tests/inherit_same_file_validate.barista", true, true, true, false);
	CHECK_MESSAGE((bool(validate_report.get("valid", false)) == true), "same-file inheritance remains valid under validate()");
	CHECK_MESSAGE((source_analyzes(same_file_ok, "res://tests/inherit_same_file_is_valid.barista")), "same-file inheritance remains valid under is_semantically_valid()");
	CHECK_MESSAGE((index.get_record_count() == before), "analyze/validate/is_valid must not mutate declaration index for inheritance member depth");
}

void scenario_resolve_class_member_depth() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String later_const_ok = "class_name ResolveMemberLaterConstOk extends Node\nfunc use() -> int:\n\treturn later_const\nconst later_const: int = 7\n";
	const auto later_const_ok_report = analyze_source(later_const_ok, "res://tests/resolve_member_later_const_ok.barista");
	CHECK_MESSAGE((later_const_ok_report.valid() == true), "later-declared typed const is resolved for earlier use");
	const String later_const_bad = "class_name ResolveMemberLaterConstBad extends Node\nfunc use() -> void:\n\tvar s: String = later_const\nconst later_const: int = 7\n";
	const auto later_const_bad_report = analyze_source(later_const_bad, "res://tests/resolve_member_later_const_bad.barista");
	CHECK_MESSAGE((later_const_bad_report.valid() == false), "later-declared int const rejects String destination");
	bool saw_later_const_type = false;
	for (const auto &error : later_const_bad_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot assign a value of type") && message.contains("int") && message.contains("String")) {
			saw_later_const_type = true;
		}
	}
	CHECK_MESSAGE((saw_later_const_type), "later-declared const type mismatch diagnostic");
	const String later_fn_ok = "class_name ResolveMemberLaterFnOk extends Node\nfunc use() -> String:\n\treturn later_fn()\nfunc later_fn() -> String:\n\treturn \"ok\"\n";
	const auto later_fn_ok_report = analyze_source(later_fn_ok, "res://tests/resolve_member_later_fn_ok.barista");
	CHECK_MESSAGE((later_fn_ok_report.valid() == true), "later-declared function return type resolves for earlier call");
	const String later_fn_bad = "class_name ResolveMemberLaterFnBad extends Node\nfunc use() -> void:\n\tvar i: int = later_fn()\nfunc later_fn() -> String:\n\treturn \"ok\"\n";
	const auto later_fn_bad_report = analyze_source(later_fn_bad, "res://tests/resolve_member_later_fn_bad.barista");
	CHECK_MESSAGE((later_fn_bad_report.valid() == false), "later-declared String return rejects int destination");
	const String cyclic_const = "class_name ResolveMemberCyclicConst extends Node\nfunc use() -> void:\n\tprint(c1)\nconst c1 = c2\nconst c2 = c1\n";
	const auto cyclic_const_report = analyze_source(cyclic_const, "res://tests/resolve_member_cyclic_const.barista");
	CHECK_MESSAGE((cyclic_const_report.valid() == false), "cyclic const members are invalid");
	bool saw_cyclic_member = false;
	for (const auto &error : cyclic_const_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Could not resolve member") && message.contains("Cyclic reference")) {
			saw_cyclic_member = true;
		}
	}
	CHECK_MESSAGE((saw_cyclic_member), "cyclic const member diagnostic");
	const String cyclic_var = "class_name ResolveMemberCyclicVar extends Node\nfunc use() -> void:\n\tprint(v1)\nvar v1 := v2\nvar v2 := v1\n";
	const auto cyclic_var_report = analyze_source(cyclic_var, "res://tests/resolve_member_cyclic_var.barista");
	CHECK_MESSAGE((cyclic_var_report.valid() == false), "cyclic var members are invalid");
	bool saw_cyclic_var = false;
	for (const auto &error : cyclic_var_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Could not resolve member") && message.contains("Cyclic reference")) {
			saw_cyclic_var = true;
		}
	}
	CHECK_MESSAGE((saw_cyclic_var), "cyclic var member diagnostic");
	const String self_attr = "class_name ResolveMemberSelfAttr extends Node\nfunc use() -> int:\n\treturn self.later_const\nconst later_const: int = 3\n";
	const auto self_attr_report = analyze_source(self_attr, "res://tests/resolve_member_self_attr.barista");
	CHECK_MESSAGE((self_attr_report.valid() == true), "self.later_const attribute resolves later-declared const");
	const String base_source = "class_name ResolveMemberCrossBase extends Node\nconst BASE_CONST: int = 42\nfunc typed_return() -> String:\n\treturn \"ok\"\n";
	BSCache::set_source_override("res://tests/resolve_member_cross_base.barista", base_source);
	const String derived_ok = "class_name ResolveMemberCrossDerivedOk extends \"res://tests/resolve_member_cross_base.barista\"\nfunc use() -> void:\n\tvar s: String = typed_return()\n\tvar i: int = BASE_CONST\n";
	const auto derived_ok_report = analyze_source(derived_ok, "res://tests/resolve_member_cross_derived_ok.barista");
	CHECK_MESSAGE((derived_ok_report.valid() == true), "cross-file SCRIPT member types resolve beyond inheritance");
	const String derived_bad = "class_name ResolveMemberCrossDerivedBad extends \"res://tests/resolve_member_cross_base.barista\"\nfunc use() -> void:\n\tvar s: String = BASE_CONST\n";
	const auto derived_bad_report = analyze_source(derived_bad, "res://tests/resolve_member_cross_derived_bad.barista");
	CHECK_MESSAGE((derived_bad_report.valid() == false), "cross-file const type mismatch is validated");
	bool saw_cross_type = false;
	for (const auto &error : derived_bad_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot assign a value of type")) {
			saw_cross_type = true;
		}
	}
	CHECK_MESSAGE((saw_cross_type), "cross-file SCRIPT member type mismatch diagnostic");
	BSCache::clear_source_override("res://tests/resolve_member_cross_base.barista");
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(later_const_ok, "res://tests/resolve_member_validate.barista", true, true, true, false);
	CHECK_MESSAGE((bool(validate_report.get("valid", false)) == true), "resolve_class_member fixtures remain valid under validate()");
	CHECK_MESSAGE((index.get_record_count() == before), "analyze/validate must not mutate declaration index for resolve_class_member depth");
}

void scenario_foreign_member_failure_replay() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String owner_cyclic = "class_name ForeignFailOwner extends Node\nconst c1 = c2\nconst c2 = c1\n";
	BSCache::set_source_override("res://tests/foreign_fail_owner.barista", owner_cyclic);
	const String dependent_once = "class_name ForeignFailDependent extends \"res://tests/foreign_fail_owner.barista\"\nfunc use() -> void:\n\tvar a = c1\n\tvar b = c1\n";
	const auto dependent_once_report = analyze_source(dependent_once, "res://tests/foreign_fail_dependent.barista");
	CHECK_MESSAGE((dependent_once_report.valid() == false), "cross-file owner member failure invalidates dependent");
	int external_fail_count = 0;
	for (const auto &error : dependent_once_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Could not resolve external class member") && message.contains("c1")) {
			external_fail_count += 1;
		}
	}
	CHECK_MESSAGE((external_fail_count >= 1), "dependent surfaces external class member failure for c1");
	CHECK_MESSAGE((external_fail_count == 1), "single dependent does not spam duplicate external-member failures");
	const String dependent_two = "class_name ForeignFailDependentTwo extends \"res://tests/foreign_fail_owner.barista\"\nfunc use() -> void:\n\tvar x = c1\n\tvar y = c1\n";
	const auto dependent_two_report = analyze_source(dependent_two, "res://tests/foreign_fail_dependent_two.barista");
	CHECK_MESSAGE((dependent_two_report.valid() == false), "second dependent also sees owner member failure");
	int external_fail_count_two = 0;
	for (const auto &error : dependent_two_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Could not resolve external class member") && message.contains("c1")) {
			external_fail_count_two += 1;
		}
	}
	CHECK_MESSAGE((external_fail_count_two == 1), "second dependent replays external failure once (no intra-file spam)");
	const auto dependent_reanalyze = analyze_source(dependent_once, "res://tests/foreign_fail_dependent_reanalyze.barista");
	CHECK_MESSAGE((dependent_reanalyze.valid() == false), "re-analyze still invalid after owner member failure");
	int external_fail_reanalyze = 0;
	for (const auto &error : dependent_reanalyze.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Could not resolve external class member") && message.contains("c1")) {
			external_fail_reanalyze += 1;
		}
	}
	CHECK_MESSAGE((external_fail_reanalyze == 1), "re-analyze keeps single external-member failure diagnostic");
	BSCache::clear_source_override("res://tests/foreign_fail_owner.barista");
	BSCache::clear();
	BSCache::set_source_override("res://tests/cycle_a.barista", "class_name CycleA extends Node\n");
	BSCache::set_source_override("res://tests/cycle_b.barista", "class_name CycleB extends Node\n");
	Error a_error = ERR_BUG;
	const auto a = BSCache::get_parser("res://tests/cycle_a.barista", BSParserRef::EMPTY, a_error, "res://tests/cycle_b.barista");
	Error b_error = ERR_BUG;
	const auto b = BSCache::get_parser("res://tests/cycle_b.barista", BSParserRef::EMPTY, b_error, "res://tests/cycle_a.barista");
	CHECK_MESSAGE((a.is_valid() && b.is_valid()), "CycleA/CycleB edges still recordable at EMPTY after foreign failure replay");
	Error raised_a_error = ERR_BUG;
	const auto raised_a = BSCache::get_parser("res://tests/cycle_a.barista", BSParserRef::FULLY_SOLVED, raised_a_error, "");
	Error raised_b_error = ERR_BUG;
	const auto raised_b = BSCache::get_parser("res://tests/cycle_b.barista", BSParserRef::FULLY_SOLVED, raised_b_error, "");
	CHECK_MESSAGE((raised_a.is_valid() && raised_b.is_valid()), "CycleA/CycleB raise still completes without deadlock");
	BSCache::clear_source_overrides();
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const String ok_source = "class_name ForeignFailReplayValid extends Node\nconst ok: int = 1\nfunc use() -> int:\n\treturn ok\n";
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(ok_source, "res://tests/foreign_fail_replay_validate.barista", true, true, true, false);
	CHECK_MESSAGE((bool(validate_report.get("valid", false)) == true), "foreign failure replay suite remains valid under validate()");
	CHECK_MESSAGE((index.get_record_count() == before), "analyze/validate must not mutate declaration index for foreign failure replay");
}

void scenario_foreign_class_phase_failure_replay() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String iface_owner = "class_name ForeignIfaceFailOwner extends Node\nconst c1 = c2\nconst c2 = c1\n";
	BSCache::set_source_override("res://tests/foreign_iface_fail_owner.barista", iface_owner);
	const String iface_dependent = "class_name ForeignIfaceFailDependent extends \"res://tests/foreign_iface_fail_owner.barista\"\nfunc ok() -> void:\n\tpass\n";
	const auto iface_report = analyze_source(iface_dependent, "res://tests/foreign_iface_fail_dependent.barista");
	CHECK_MESSAGE((iface_report.valid() == false), "cross-file owner INTERFACE failure invalidates dependent");
	int iface_plain = 0;
	int iface_body_suffix = 0;
	for (const auto &error : iface_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Could not resolve class") && message.contains("ForeignIfaceFailOwner")) {
			if (message.contains("declared in")) {
				iface_body_suffix += 1;
			} else {
				iface_plain += 1;
			}
		}
	}
	CHECK_MESSAGE((iface_plain >= 1), "dependent surfaces class-phase INTERFACE failure for owner");
	CHECK_MESSAGE((iface_plain == 1), "single dependent does not spam duplicate INTERFACE class-phase failures");
	CHECK_MESSAGE((iface_body_suffix <= 1), "INTERFACE→BODY propagation replays BODY at most once");
	const String iface_dependent_two = "class_name ForeignIfaceFailDependentTwo extends \"res://tests/foreign_iface_fail_owner.barista\"\nfunc ok() -> void:\n\tpass\n";
	const auto iface_report_two = analyze_source(iface_dependent_two, "res://tests/foreign_iface_fail_dependent_two.barista");
	CHECK_MESSAGE((iface_report_two.valid() == false), "second INTERFACE dependent also sees owner failure");
	int iface_plain_two = 0;
	for (const auto &error : iface_report_two.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Could not resolve class") && message.contains("ForeignIfaceFailOwner") && !(message.contains("declared in"))) {
			iface_plain_two += 1;
		}
	}
	CHECK_MESSAGE((iface_plain_two == 1), "second INTERFACE dependent replays INTERFACE failure once");
	const auto iface_reanalyze = analyze_source(iface_dependent, "res://tests/foreign_iface_fail_dependent_reanalyze.barista");
	CHECK_MESSAGE((iface_reanalyze.valid() == false), "INTERFACE re-analyze still invalid");
	int iface_plain_reanalyze = 0;
	for (const auto &error : iface_reanalyze.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Could not resolve class") && message.contains("ForeignIfaceFailOwner") && !(message.contains("declared in"))) {
			iface_plain_reanalyze += 1;
		}
	}
	CHECK_MESSAGE((iface_plain_reanalyze == 1), "INTERFACE re-analyze keeps single INTERFACE class-phase failure");
	BSCache::clear_source_override("res://tests/foreign_iface_fail_owner.barista");
	BSCache::clear();
	const String body_owner = "class_name ForeignBodyFailOwner extends Node\nfunc bad() -> void:\n\tvar x: int = \"nope\"\n";
	BSCache::set_source_override("res://tests/foreign_body_fail_owner.barista", body_owner);
	const String body_dependent = "class_name ForeignBodyFailDependent extends \"res://tests/foreign_body_fail_owner.barista\"\nfunc ok() -> void:\n\tpass\n";
	const auto body_report = analyze_source(body_dependent, "res://tests/foreign_body_fail_dependent.barista");
	CHECK_MESSAGE((body_report.valid() == false), "cross-file owner BODY failure invalidates dependent");
	int body_fail_count = 0;
	int body_plain_iface = 0;
	for (const auto &error : body_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Could not resolve class") && message.contains("ForeignBodyFailOwner")) {
			if (message.contains("declared in")) {
				body_fail_count += 1;
			} else {
				body_plain_iface += 1;
			}
		}
	}
	CHECK_MESSAGE((body_fail_count >= 1), "dependent surfaces class-phase BODY failure for owner");
	CHECK_MESSAGE((body_fail_count == 1), "single dependent does not spam duplicate BODY class-phase failures");
	CHECK_MESSAGE((body_plain_iface == 0), "BODY-only owner failure does not emit INTERFACE class replay");
	const String body_dependent_two = "class_name ForeignBodyFailDependentTwo extends \"res://tests/foreign_body_fail_owner.barista\"\nfunc ok() -> void:\n\tpass\n";
	const auto body_report_two = analyze_source(body_dependent_two, "res://tests/foreign_body_fail_dependent_two.barista");
	CHECK_MESSAGE((body_report_two.valid() == false), "second BODY dependent also sees owner failure");
	int body_fail_count_two = 0;
	for (const auto &error : body_report_two.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Could not resolve class") && message.contains("ForeignBodyFailOwner") && message.contains("declared in")) {
			body_fail_count_two += 1;
		}
	}
	CHECK_MESSAGE((body_fail_count_two == 1), "second BODY dependent replays class failure once");
	BSCache::clear_source_override("res://tests/foreign_body_fail_owner.barista");
	BSCache::clear();
	BSCache::set_source_override("res://tests/cycle_a.barista", "class_name CycleA extends Node\n");
	BSCache::set_source_override("res://tests/cycle_b.barista", "class_name CycleB extends Node\n");
	Error a_error = ERR_BUG;
	const auto a = BSCache::get_parser("res://tests/cycle_a.barista", BSParserRef::EMPTY, a_error, "res://tests/cycle_b.barista");
	Error b_error = ERR_BUG;
	const auto b = BSCache::get_parser("res://tests/cycle_b.barista", BSParserRef::EMPTY, b_error, "res://tests/cycle_a.barista");
	CHECK_MESSAGE((a.is_valid() && b.is_valid()), "CycleA/CycleB edges still recordable at EMPTY after class-phase replay");
	Error raised_a_error = ERR_BUG;
	const auto raised_a = BSCache::get_parser("res://tests/cycle_a.barista", BSParserRef::FULLY_SOLVED, raised_a_error, "");
	Error raised_b_error = ERR_BUG;
	const auto raised_b = BSCache::get_parser("res://tests/cycle_b.barista", BSParserRef::FULLY_SOLVED, raised_b_error, "");
	CHECK_MESSAGE((raised_a.is_valid() && raised_b.is_valid()), "CycleA/CycleB raise still completes without deadlock after class-phase replay");
	BSCache::clear_source_overrides();
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const String ok_source = "class_name ForeignClassPhaseValid extends Node\nconst ok: int = 1\nfunc use() -> int:\n\treturn ok\n";
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(ok_source, "res://tests/foreign_class_phase_validate.barista", true, true, true, false);
	CHECK_MESSAGE((bool(validate_report.get("valid", false)) == true), "foreign class-phase replay suite remains valid under validate()");
	CHECK_MESSAGE((index.get_record_count() == before), "analyze/validate must not mutate declaration index for foreign class-phase replay");
}
} // namespace

TEST_SUITE("analyzer_members") {
	TEST_CASE("surface_inheritance_member_depth") { scenario_surface_inheritance_member_depth(); }
	TEST_CASE("resolve_class_member_depth") { scenario_resolve_class_member_depth(); }
	TEST_CASE("foreign_member_failure_replay") { scenario_foreign_member_failure_replay(); }
	TEST_CASE("foreign_class_phase_failure_replay") { scenario_foreign_class_phase_failure_replay(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_surface_inheritance_member_depth, scenario_resolve_class_member_depth, scenario_foreign_member_failure_replay, scenario_foreign_class_phase_failure_replay });
	}
}
