/**************************************************************************/
/*  analyzer_finality_test.cpp                                            */
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
void scenario_final_local_assignment() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String reassign = "class_name FinalReassign extends Node\nfunc _ready() -> void:\n\tfinal var x: int = 1\n\tx = 2\n";
	const auto reassign_report = analyze_source(reassign, "res://tests/final_reassign.barista");
	CHECK_MESSAGE(reassign_report.valid() == false, "reassigning initialized final is invalid");
	bool saw_reassign = false;
	for (const auto &error : reassign_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("already assigned")) {
			saw_reassign = true;
		}
	}
	CHECK_MESSAGE(saw_reassign, "final reassignment diagnostic");
	const String use_before = "class_name FinalUseBefore extends Node\nfunc use() -> int:\n\tfinal var x: int\n\treturn x\n";
	const auto before_report = analyze_source(use_before, "res://tests/final_use_before.barista");
	CHECK_MESSAGE(before_report.valid() == false, "reading blank final before assignment is invalid");
	bool saw_before = false;
	for (const auto &error : before_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("before assignment")) {
			saw_before = true;
		}
	}
	CHECK_MESSAGE(saw_before, "final use-before-assignment diagnostic");
	const String branch_ok = "class_name FinalBranchOk extends Node\nfunc pick(flag: bool) -> int:\n\tfinal var x: int\n\tif flag:\n\t\tx = 1\n\telse:\n\t\tx = 2\n\treturn x\n";
	const auto ok_report = analyze_source(branch_ok, "res://tests/final_branch_ok.barista");
	CHECK_MESSAGE(ok_report.valid() == true, "final assigned on both branches then read is valid");
	const String branch_bad = "class_name FinalBranchBad extends Node\nfunc pick(flag: bool) -> int:\n\tfinal var x: int\n\tif flag:\n\t\tx = 1\n\treturn x\n";
	const auto bad_report = analyze_source(branch_bad, "res://tests/final_branch_bad.barista");
	CHECK_MESSAGE(bad_report.valid() == false, "final assigned on only one branch then read is invalid");
	bool saw_branch_bad = false;
	for (const auto &error : bad_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("before assignment")) {
			saw_branch_bad = true;
		}
	}
	CHECK_MESSAGE(saw_branch_bad, "FinalBranchBad reports use-before-assignment diagnostic");
	const String lambda_write = "class_name FinalLambdaWrite extends Node\nfunc _ready() -> void:\n\tfinal var x: int = 1\n\tvar f := func():\n\t\tx = 2\n\tf.call()\n";
	const auto lambda_report = analyze_source(lambda_write, "res://tests/final_lambda_write.barista");
	CHECK_MESSAGE(lambda_report.valid() == false, "assigning outer final inside lambda is invalid");
	bool saw_lambda = false;
	for (const auto &error : lambda_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.to_lower().contains("lambda")) {
			saw_lambda = true;
		}
	}
	CHECK_MESSAGE(saw_lambda, "illegal lambda final-write diagnostic");
}

void scenario_final_member_and_static_assignment() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String member_ok = "class_name FinalMemberOk extends Node\nfinal var id: int\nfunc _init(p_id: int) -> void:\n\tid = p_id\n";
	const auto member_ok_report = analyze_source(member_ok, "res://tests/final_member_ok.barista");
	CHECK_MESSAGE(member_ok_report.valid() == true, "blank final assigned once in _init is valid");
	const String member_outside = "class_name FinalMemberOutside extends Node\nfinal var id: int\nfunc _init() -> void:\n\tid = 1\nfunc reset() -> void:\n\tid = 2\n";
	const auto outside_report = analyze_source(member_outside, "res://tests/final_member_outside.barista");
	CHECK_MESSAGE(outside_report.valid() == false, "final member assigned outside _init is invalid");
	bool saw_outside = false;
	for (const auto &error : outside_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("_init()") && message.contains("can only be assigned")) {
			saw_outside = true;
		}
	}
	CHECK_MESSAGE(saw_outside, "final member outside-_init diagnostic");
	const String member_twice = "class_name FinalMemberTwice extends Node\nfinal var id: int\nfunc _init() -> void:\n\tid = 1\n\tid = 2\n";
	const auto twice_report = analyze_source(member_twice, "res://tests/final_member_twice.barista");
	CHECK_MESSAGE(twice_report.valid() == false, "final member assigned twice in _init is invalid");
	bool saw_twice = false;
	for (const auto &error : twice_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("already assigned")) {
			saw_twice = true;
		}
	}
	CHECK_MESSAGE(saw_twice, "final member double-assign diagnostic");
	const String member_blank = "class_name FinalMemberBlank extends Node\nfinal var id: int\nfunc _init() -> void:\n\tpass\n";
	const auto blank_report = analyze_source(member_blank, "res://tests/final_member_blank.barista");
	CHECK_MESSAGE(blank_report.valid() == false, "blank final never assigned in _init is invalid");
	bool saw_blank = false;
	for (const auto &error : blank_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("must be definitely assigned") && message.contains("_init()")) {
			saw_blank = true;
		}
	}
	CHECK_MESSAGE(saw_blank, "blank final never-assigned diagnostic");
	const String member_branches = "class_name FinalMemberBranches extends Node\nfinal var label: String\nfunc _init(positive: bool) -> void:\n\tif positive:\n\t\tlabel = \"pos\"\n\telse:\n\t\tlabel = \"neg\"\n";
	const auto branches_report = analyze_source(member_branches, "res://tests/final_member_branches.barista");
	CHECK_MESSAGE(branches_report.valid() == true, "final member assigned on both branches is valid");
	const String member_self = "class_name FinalMemberSelf extends Node\nfinal var id: int\nfunc _init(p_id: int) -> void:\n\tself.id = p_id\nfunc bump() -> void:\n\tself.id = 99\n";
	const auto self_report = analyze_source(member_self, "res://tests/final_member_self.barista");
	CHECK_MESSAGE(self_report.valid() == false, "self.final reassigned outside _init is invalid");
	bool saw_self = false;
	for (const auto &error : self_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("_init()") && message.contains("can only be assigned")) {
			saw_self = true;
		}
	}
	CHECK_MESSAGE(saw_self, "self.final outside-_init diagnostic");
	const String static_ok = "class_name FinalStaticOk extends Node\nfinal static var LABEL: String\nstatic func _static_init() -> void:\n\tLABEL = \"ready\"\n";
	const auto static_ok_report = analyze_source(static_ok, "res://tests/final_static_ok.barista");
	CHECK_MESSAGE(static_ok_report.valid() == true, "blank static final assigned once in _static_init is valid");
	const String static_outside = "class_name FinalStaticOutside extends Node\nfinal static var LABEL: String = \"ready\"\nfunc reset() -> void:\n\tLABEL = \"other\"\n";
	const auto static_outside_report = analyze_source(static_outside, "res://tests/final_static_outside.barista");
	CHECK_MESSAGE(static_outside_report.valid() == false, "static final reassigned outside _static_init is invalid");
	bool saw_static_outside = false;
	for (const auto &error : static_outside_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("_static_init()") && message.contains("can only be assigned")) {
			saw_static_outside = true;
		}
	}
	CHECK_MESSAGE(saw_static_outside, "static final outside-_static_init diagnostic");
	const String static_blank = "class_name FinalStaticBlank extends Node\nfinal static var LABEL: String\n";
	const auto static_blank_report = analyze_source(static_blank, "res://tests/final_static_blank.barista");
	CHECK_MESSAGE(static_blank_report.valid() == false, "blank static final without _static_init is invalid");
	bool saw_static_blank = false;
	for (const auto &error : static_blank_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("must be definitely assigned") && message.contains("_static_init()")) {
			saw_static_blank = true;
		}
	}
	CHECK_MESSAGE(saw_static_blank, "blank static final never-assigned diagnostic");
	const String static_qualified = "class_name FinalStaticQualified extends Node\nfinal static var LABEL: String = \"ready\"\nfunc reset() -> void:\n\tFinalStaticQualified.LABEL = \"other\"\n";
	const auto static_qualified_report = analyze_source(static_qualified, "res://tests/final_static_qualified.barista");
	CHECK_MESSAGE(static_qualified_report.valid() == false, "ClassName.static final reassignment is invalid");
	bool saw_static_qualified = false;
	for (const auto &error : static_qualified_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("_static_init()") && message.contains("can only be assigned")) {
			saw_static_qualified = true;
		}
	}
	CHECK_MESSAGE(saw_static_qualified, "ClassName.static final outside-_static_init diagnostic");
	const String onready_final = "class_name FinalOnreadyBad extends Node\n@onready final var id: int = 1\n";
	const auto onready_report = analyze_source(onready_final, "res://tests/final_onready_bad.barista");
	CHECK_MESSAGE(onready_report.valid() == false, "@onready final member is invalid");
	bool saw_onready = false;
	for (const auto &error : onready_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("@onready")) {
			saw_onready = true;
		}
	}
	CHECK_MESSAGE(saw_onready, "@onready final rejection diagnostic");
	const String property_final = "class_name FinalPropertyBad extends Node\nfinal var id: int:\n\tget:\n\t\treturn 1\n";
	const auto property_report = analyze_source(property_final, "res://tests/final_property_bad.barista");
	CHECK_MESSAGE(property_report.valid() == false, "final property member is invalid");
	bool saw_property = false;
	for (const auto &error : property_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.to_lower().contains("property") || message.to_lower().contains("getter") || message.to_lower().contains("final")) {
			saw_property = true;
		}
	}
	CHECK_MESSAGE(saw_property, "final property rejection diagnostic");
	const String early_return = "class_name FinalMemberEarlyReturn extends Node\nfinal var id: int\nfunc _init(flag: bool) -> void:\n\tif flag:\n\t\treturn\n\tid = 1\n";
	const auto early_report = analyze_source(early_return, "res://tests/final_member_early_return.barista");
	CHECK_MESSAGE(early_report.valid() == false, "blank final not assigned on early-return path is invalid");
	bool saw_early = false;
	for (const auto &error : early_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("must be definitely assigned") || message.contains("before assignment")) {
			saw_early = true;
		}
	}
	CHECK_MESSAGE(saw_early, "final member early-return definite-assignment diagnostic");
	const String use_before = "class_name FinalMemberUseBefore extends Node\nfinal var id: int\nfunc _init() -> void:\n\tvar _sink: int = id\n\tid = 1\n";
	const auto use_before_report = analyze_source(use_before, "res://tests/final_member_use_before.barista");
	CHECK_MESSAGE(use_before_report.valid() == false, "reading blank final member before assignment is invalid");
	bool saw_use_before = false;
	for (const auto &error : use_before_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("before assignment")) {
			saw_use_before = true;
		}
	}
	CHECK_MESSAGE(saw_use_before, "final member use-before-assignment diagnostic");
}

void scenario_flow_narrowing() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	settings.strict_null(true);
	const String bare_null = "class_name BareNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tvar x: Node = n\n";
	const auto bare_null_report = analyze_source(bare_null, "res://tests/bare_nullable_assign.barista");
	CHECK_MESSAGE(bare_null_report.valid() == false, "nullable to non-null assign fails under strict_null");
	const String narrowed_null = "class_name NarrowedNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tvar x: Node = n\n";
	const auto narrowed_null_report = analyze_source(narrowed_null, "res://tests/narrowed_nullable_assign.barista");
	CHECK_MESSAGE(narrowed_null_report.valid() == true, "null-check true arm allows Node? → Node");
	const String else_null = "class_name ElseNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tpass\n\telse:\n\t\tvar x: Node = n\n";
	const auto else_null_report = analyze_source(else_null, "res://tests/else_nullable_assign.barista");
	CHECK_MESSAGE(else_null_report.valid() == false, "null-check else arm keeps Node? → Node invalid");
	const String assert_null = "class_name AssertNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tassert(n != null)\n\tvar x: Node = n\n";
	const auto assert_null_report = analyze_source(assert_null, "res://tests/assert_nullable_assign.barista");
	CHECK_MESSAGE(assert_null_report.valid() == true, "assert null-check narrows later statements");
	const String bare_union = "class_name BareUnionAssign extends Node\nfunc take(v: int | String) -> void:\n\tvar s: String = v\n";
	const auto bare_union_report = analyze_source(bare_union, "res://tests/bare_union_assign.barista");
	CHECK_MESSAGE(bare_union_report.valid() == false, "union to String without type test is invalid");
	const String narrowed_is = "class_name NarrowedIsAssign extends Node\nfunc take(v: int | String) -> void:\n\tif v is String:\n\t\tvar s: String = v\n";
	const auto narrowed_is_report = analyze_source(narrowed_is, "res://tests/narrowed_is_assign.barista");
	CHECK_MESSAGE(narrowed_is_report.valid() == true, "`is String` true arm allows int|String → String");
	const String else_is = "class_name ElseIsAssign extends Node\nfunc take(v: int | String) -> void:\n\tif v is String:\n\t\tpass\n\telse:\n\t\tvar i: int = v\n";
	const auto else_is_report = analyze_source(else_is, "res://tests/else_is_assign.barista");
	CHECK_MESSAGE(else_is_report.valid() == true, "`is String` else arm subtracts String leaving int");
	const String cleared = "class_name ClearedNarrowingAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tn = null\n\t\tvar x: Node = n\n";
	const auto cleared_report = analyze_source(cleared, "res://tests/cleared_narrowing_assign.barista");
	CHECK_MESSAGE(cleared_report.valid() == false, "assignment clears prior null-check narrowing");
	const String and_narrow = "class_name AndNarrowingAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null and n is Node:\n\t\tvar x: Node = n\n";
	const auto and_narrow_report = analyze_source(and_narrow, "res://tests/and_narrowing_assign.barista");
	CHECK_MESSAGE(and_narrow_report.valid() == true, "`and` condition applies left-side null-check narrowing");
	const String match_null_stripped = "class_name MatchNullStrippedAssign extends Node\nfunc take(n: Node?) -> void:\n\tmatch n:\n\t\tnull:\n\t\t\tpass\n\t\t1:\n\t\t\tvar x: Node = n\n\t\t_:\n\t\t\tpass\n";
	const auto match_null_stripped_report = analyze_source(match_null_stripped, "res://tests/match_null_stripped_assign.barista");
	CHECK_MESSAGE(match_null_stripped_report.valid() == true, "match non-null literal arm strips Node? → Node");
	const String match_wildcard_keeps_null = "class_name MatchWildcardKeepsNull extends Node\nfunc take(n: Node?) -> void:\n\tmatch n:\n\t\t_:\n\t\t\tvar x: Node = n\n";
	const auto match_wildcard_keeps_null_report = analyze_source(match_wildcard_keeps_null, "res://tests/match_wildcard_keeps_null.barista");
	CHECK_MESSAGE(match_wildcard_keeps_null_report.valid() == false, "match wildcard arm does not strip nullability");
	const String match_is_type = "class_name MatchIsTypeAssign extends Node\nfunc take(v: int | String) -> void:\n\tmatch v:\n\t\tv is String:\n\t\t\tvar s: String = v\n\t\t_:\n\t\t\tpass\n";
	const auto match_is_type_report = analyze_source(match_is_type, "res://tests/match_is_type_assign.barista");
	CHECK_MESSAGE(match_is_type_report.valid() == true, "match `v is String` arm allows int|String → String");
	const String match_native_type = "class_name MatchNativeTypeAssign extends Node\nfunc take(v: Object?) -> void:\n\tmatch v:\n\t\tnull:\n\t\t\tpass\n\t\tNode:\n\t\t\tvar x: Node = v\n\t\t_:\n\t\t\tpass\n";
	const auto match_native_type_report = analyze_source(match_native_type, "res://tests/match_native_type_assign.barista");
	CHECK_MESSAGE(match_native_type_report.valid() == true, "match bare Node type pattern narrows Object? → Node");
	const String match_shadowed_classdb = "class_name MatchShadowedClassDBName extends Node\nfunc take(v: int | String) -> void:\n\tconst Node := 1\n\tmatch v:\n\t\tNode:\n\t\t\tvar x: Node = v\n\t\t_:\n\t\t\tpass\n";
	const auto match_shadowed_classdb_report = analyze_source(match_shadowed_classdb, "res://tests/match_shadowed_classdb_name.barista");
	CHECK_MESSAGE(match_shadowed_classdb_report.valid() == false, "match local Node shadow stays value pattern (no ClassDB type overlay)");
	settings.strict_null(false);
}
void scenario_final_trait_flattening() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String trait_ok = "class_name FinalTraitOk extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\nfunc _init() -> void:\n\tid = 42\n";
	const auto ok_report = analyze_source(trait_ok, "res://tests/final_trait_ok.barista");
	CHECK_MESSAGE((ok_report.valid() == true), "trait blank final assigned once in implementer _init is valid");
	const String trait_blank = "class_name FinalTraitBlank extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\nfunc _init() -> void:\n\tpass\n";
	const auto blank_report = analyze_source(trait_blank, "res://tests/final_trait_blank.barista");
	CHECK_MESSAGE((blank_report.valid() == false), "trait blank final never assigned is invalid");
	bool saw_blank = false;
	for (const auto &error : blank_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("must be definitely assigned")) {
			saw_blank = true;
		}
	}
	CHECK_MESSAGE((saw_blank), "trait blank final never-assigned diagnostic");
	const String trait_twice = "class_name FinalTraitTwice extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\nfunc _init() -> void:\n\tid = 1\n\tid = 2\n";
	const auto twice_report = analyze_source(trait_twice, "res://tests/final_trait_twice.barista");
	CHECK_MESSAGE((twice_report.valid() == false), "trait final assigned twice in _init is invalid");
	bool saw_twice = false;
	for (const auto &error : twice_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("already assigned")) {
			saw_twice = true;
		}
	}
	CHECK_MESSAGE((saw_twice), "trait final double-assign diagnostic");
	const String trait_method = "class_name FinalTraitMethod extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int = 1\n\tfunc mutate() -> void:\n\t\tid = 2\n\nfunc _ready() -> void:\n\tpass\n";
	const auto method_report = analyze_source(trait_method, "res://tests/final_trait_method.barista");
	CHECK_MESSAGE((method_report.valid() == false), "trait method reassigning flattened final is invalid");
	bool saw_method = false;
	for (const auto &error : method_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("can only be assigned")) {
			saw_method = true;
		}
	}
	CHECK_MESSAGE((saw_method), "trait method illegal-write diagnostic");
	const String trait_init = "class_name FinalTraitInit extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\tfunc _init() -> void:\n\t\tid = 5\n\nfunc _ready() -> void:\n\tpass\n";
	const auto trait_init_report = analyze_source(trait_init, "res://tests/final_trait_init.barista");
	CHECK_MESSAGE((trait_init_report.valid() == true), "trait-supplied _init assigning blank final is valid");
	const String trait_cycle = "class_name FinalTraitCycle extends Node\nuses CycleA\n\ntrait CycleA:\n\tuses CycleB\n\ntrait CycleB:\n\tuses CycleA\n";
	const auto cycle_report = analyze_source(trait_cycle, "res://tests/final_trait_cycle.barista");
	CHECK_MESSAGE((cycle_report.valid() == false), "cyclic trait uses is invalid");
	bool saw_cycle = false;
	for (const auto &error : cycle_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cyclic trait use")) {
			saw_cycle = true;
		}
	}
	CHECK_MESSAGE((saw_cycle), "cyclic trait use diagnostic");
	const String trait_static_blank = "class_name FinalTraitStaticBlank extends Node\nuses HasLabel\n\ntrait HasLabel:\n\tfinal static var LABEL: String\n";
	const auto static_blank_report = analyze_source(trait_static_blank, "res://tests/final_trait_static_blank.barista");
	CHECK_MESSAGE((static_blank_report.valid() == false), "trait static blank final without _static_init is invalid");
	bool saw_static_blank = false;
	for (const auto &error : static_blank_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("must be definitely assigned") && message.contains("_static_init()")) {
			saw_static_blank = true;
		}
	}
	CHECK_MESSAGE((saw_static_blank), "trait static blank never-assigned diagnostic");
	const String trait_static_ok = "class_name FinalTraitStaticOk extends Node\nuses HasLabel\n\ntrait HasLabel:\n\tfinal static var LABEL: String\n\nstatic func _static_init() -> void:\n\tLABEL = \"ready\"\n";
	const auto static_ok_report = analyze_source(trait_static_ok, "res://tests/final_trait_static_ok.barista");
	CHECK_MESSAGE((static_ok_report.valid() == true), "trait static blank assigned in _static_init is valid");
	const String trait_static_outside = "class_name FinalTraitStaticOutside extends Node\nuses HasLabel\n\ntrait HasLabel:\n\tfinal static var LABEL: String = \"ready\"\n\nfunc reset() -> void:\n\tLABEL = \"other\"\n";
	const auto static_outside_report = analyze_source(trait_static_outside, "res://tests/final_trait_static_outside.barista");
	CHECK_MESSAGE((static_outside_report.valid() == false), "trait static final reassigned outside _static_init is invalid");
	bool saw_static_outside = false;
	for (const auto &error : static_outside_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("_static_init()") && message.contains("can only be assigned")) {
			saw_static_outside = true;
		}
	}
	CHECK_MESSAGE((saw_static_outside), "trait static outside-_static_init diagnostic");
	BSDeclarationIndex &index = fixture.index();
	index.clear();
	const String trait_path = "res://tests/index_has_id.barista";
	const String trait_source = "trait_name IndexHasId\nfinal var id: int\n";
	BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(trait_path, trait_source);
	BSCache::set_source_override(trait_path, trait_source);
	const String consumer = "class_name FinalTraitIndexBlank extends Node\nuses IndexHasId\nfunc _init() -> void:\n\tpass\n";
	const auto index_blank_report = analyze_source(consumer, "res://tests/final_trait_index_blank.barista");
	CHECK_MESSAGE((index_blank_report.valid() == false), "index-backed trait blank final never assigned is invalid");
	bool saw_index_blank = false;
	for (const auto &error : index_blank_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("must be definitely assigned")) {
			saw_index_blank = true;
		}
	}
	CHECK_MESSAGE((saw_index_blank), "index-backed trait blank never-assigned diagnostic");
	BSCache::clear_source_override(trait_path);
	index.clear();
}

void scenario_lambda_capture_and_compound_narrowing() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	settings.strict_null(true);
	const String capture_clears = "class_name LambdaCaptureClearsNarrowing extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tvar f := func():\n\t\t\tvar _used = n\n\t\tf.call()\n\t\tvar x: Node = n\n";
	const auto capture_clears_report = analyze_source(capture_clears, "res://tests/lambda_capture_clears_narrowing.barista");
	CHECK_MESSAGE((capture_clears_report.valid() == false), "captured null-narrowing cleared after call makes Node? → Node invalid");
	const String capture_no_call = "class_name LambdaCaptureNoCallKeepsNarrowing extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tvar f := func():\n\t\t\tvar _used = n\n\t\tvar x: Node = n\n";
	const auto capture_no_call_report = analyze_source(capture_no_call, "res://tests/lambda_capture_no_call_keeps_narrowing.barista");
	CHECK_MESSAGE((capture_no_call_report.valid() == true), "captured narrowing stays until a call clears it");
	const String member_no_capture = "class_name LambdaMemberSkipsCapture extends Node\nvar member_n: Node?\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tvar f := func():\n\t\t\tvar _m = member_n\n\t\tf.call()\n\t\tvar x: Node = n\n";
	const auto member_no_capture_report = analyze_source(member_no_capture, "res://tests/lambda_member_skips_capture.barista");
	CHECK_MESSAGE((member_no_capture_report.valid() == true), "member read in lambda does not capture / clear local narrowing");
	const String is_capture_clears = "class_name LambdaIsCaptureClearsNarrowing extends Node\nfunc take(v: int | String) -> void:\n\tif v is String:\n\t\tvar f := func():\n\t\t\tvar _used = v\n\t\tf.call()\n\t\tvar s: String = v\n";
	const auto is_capture_clears_report = analyze_source(is_capture_clears, "res://tests/lambda_is_capture_clears_narrowing.barista");
	CHECK_MESSAGE((is_capture_clears_report.valid() == false), "captured `is` narrowing cleared after call");
	const String compound_clears = "class_name CompoundAssignClearsNarrowing extends Node\nfunc take(v: int | String) -> void:\n\tif v is int:\n\t\tv += 1\n\t\tvar i: int = v\n";
	const auto compound_clears_report = analyze_source(compound_clears, "res://tests/compound_assign_clears_narrowing.barista");
	CHECK_MESSAGE((compound_clears_report.valid() == false), "compound assignment clears prior `is` narrowing");
	const String compound_narrow_ok = "class_name CompoundAssignNarrowedReadOk extends Node\nfunc take(v: int | String) -> void:\n\tif v is int:\n\t\tv += 1\n";
	const auto compound_narrow_ok_report = analyze_source(compound_narrow_ok, "res://tests/compound_assign_narrowed_read_ok.barista");
	CHECK_MESSAGE((compound_narrow_ok_report.valid() == true), "compound += inside `is int` arm is valid: %s");
	for (int iteration = 0; iteration < 64; ++iteration) {
		INFO(iteration);
		const auto repeated = analyze_source(compound_narrow_ok, vformat("res://tests/compound_parameter_%d.barista", iteration));
		CHECK_MESSAGE((repeated.valid()), "parameter assignment preserves AST on iteration %d: %s");
	}
	settings.strict_null(false);
}

} // namespace

TEST_SUITE("analyzer_finality") {
	TEST_CASE("lambda_capture_and_compound_narrowing") { scenario_lambda_capture_and_compound_narrowing(); }
	TEST_CASE("final_trait_flattening") { scenario_final_trait_flattening(); }
	TEST_CASE("final_local_assignment") { scenario_final_local_assignment(); }
	TEST_CASE("final_member_and_static_assignment") { scenario_final_member_and_static_assignment(); }
	TEST_CASE("flow_narrowing") { scenario_flow_narrowing(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_final_local_assignment, scenario_final_member_and_static_assignment, scenario_flow_narrowing, scenario_final_trait_flattening, scenario_lambda_capture_and_compound_narrowing });
	}
}
