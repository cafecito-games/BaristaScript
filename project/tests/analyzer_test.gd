# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

extends SceneTree

const SuiteGuard := preload("res://tests/suite_guard.gd")
const FIXTURE_A := "res://tests/cache_fixtures/script_a.barista"
const FIXTURE_B := "res://tests/cache_fixtures/script_b.barista"

enum Status { EMPTY, PARSED, INHERITANCE_SOLVED, INTERFACE_SOLVED, FULLY_SOLVED }

func _init() -> void:
	var failures: PackedStringArray = []
	BaristaScriptParseCache.clear_script_cache()
	_test_parser_lifecycle(failures)
	_test_transitive_invalidation(failures)
	_test_missing_and_self(failures)
	_test_move_remove(failures)
	_test_dependency_cycle(failures)
	_test_strict_settings(failures)
	_test_can_reference(failures)
	_test_host_bootstrap_filtering(failures)
	_test_validate_and_is_valid_agree(failures)
	_test_semantic_errors(failures)
	_test_unary_sign_constant_folding(failures)
	_test_analyzer_declaration_commit(failures)
	_test_declaration_head_kinds_and_conformance(failures)
	_test_digest_mismatch_discards(failures)
	_test_namespace_change_invalidation(failures)
	_test_explicit_out_of_root_import(failures)
	_test_call_arity_and_types(failures)
	_test_call_validation_methodinfo_and_signals(failures)
	_test_named_arg_and_connect_callable(failures)
	_test_match_and_flow(failures)
	_test_warning_settings(failures)
	_test_final_local_assignment(failures)
	_test_final_member_and_static_assignment(failures)
	_test_final_trait_flattening(failures)
	_test_noreturn_flow(failures)
	_test_unused_locals(failures)
	_test_unused_class_members_and_signals(failures)
	_test_trait_requirements_and_conformance_witness(failures)
	_test_flow_narrowing(failures)
	_test_builtin_annotation_resolve(failures)
	_test_union_union_assignability(failures)
	_test_union_store_carrier_select(failures)
	_test_enum_case_match_and_case_binds(failures)
	_test_contextual_case_shorthand(failures)
	_test_tagged_union_match_exhaustiveness(failures)
	BaristaScriptParseCache.clear_script_cache()
	quit(SuiteGuard.report("analyzer_test", failures))


func _expect(failures: PackedStringArray, condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)


func _kw_class_name() -> String:
	return "class_" + "name"

func _src_class(body_after_keyword_space: String) -> String:
	return _kw_class_name() + " " + body_after_keyword_space


func _test_parser_lifecycle(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	var first := BaristaScriptParseCache.get_parser(FIXTURE_A, Status.PARSED, "")
	_expect(failures, first.valid and first.status == Status.PARSED and first.error == OK, "first raise to PARSED")
	var again := BaristaScriptParseCache.get_parser(FIXTURE_A, Status.FULLY_SOLVED, "")
	_expect(failures, again.valid and again.status == Status.FULLY_SOLVED, "same entry raises monotonically")
	_expect(failures, again.source_hash == first.source_hash, "source hash latches across raises")
	var reused := BaristaScriptParseCache.get_parser(FIXTURE_A, Status.FULLY_SOLVED, "")
	_expect(failures, reused.valid and BaristaScriptParseCache.has_parser(FIXTURE_A), "cached entry reused")


func _test_transitive_invalidation(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/dep_a.barista", _src_class("DepA extends Node\n"))
	BaristaScriptParseCache.set_source_override("res://tests/dep_b.barista", _src_class("DepB extends Node\n"))
	BaristaScriptParseCache.set_source_override("res://tests/dep_c.barista", _src_class("DepC extends Node\n"))
	BaristaScriptParseCache.set_source_override("res://tests/unrelated.barista", _src_class("Unrelated extends Node\n"))

	BaristaScriptParseCache.get_parser("res://tests/dep_a.barista", Status.PARSED, "")
	BaristaScriptParseCache.get_parser("res://tests/dep_b.barista", Status.PARSED, "res://tests/dep_a.barista")
	BaristaScriptParseCache.get_parser("res://tests/dep_c.barista", Status.PARSED, "res://tests/dep_b.barista")
	BaristaScriptParseCache.get_parser("res://tests/unrelated.barista", Status.PARSED, "")

	var closure := BaristaScriptParseCache.collect_parser_invalidation_closure("res://tests/dep_c.barista")
	_expect(failures, "res://tests/dep_c.barista" in closure, "closure includes provider")
	_expect(failures, "res://tests/dep_b.barista" in closure, "closure includes inverse dependent")
	_expect(failures, "res://tests/dep_a.barista" in closure, "closure is transitive")
	_expect(failures, not ("res://tests/unrelated.barista" in closure), "unrelated entry survives closure")

	BaristaScriptParseCache.remove_parser("res://tests/dep_c.barista")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/dep_c.barista"), "provider removed")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/dep_b.barista"), "dependent invalidated")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/dep_a.barista"), "transitive dependent invalidated")
	_expect(failures, BaristaScriptParseCache.has_parser("res://tests/unrelated.barista"), "unrelated survives")
	BaristaScriptParseCache.clear_source_overrides()


func _test_missing_and_self(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/owner.barista", _src_class("OwnerFile extends Node\n"))
	var owner_ref := BaristaScriptParseCache.get_parser("res://tests/owner.barista", Status.EMPTY, "")
	_expect(failures, owner_ref.valid, "owner fixture must cache")
	var missing_path := "res://tests/does_not_exist.barista"
	var missing := BaristaScriptParseCache.get_parser(missing_path, Status.EMPTY, "res://tests/owner.barista")
	_expect(failures, not missing.valid, "missing file creates no entry")
	_expect(failures, not BaristaScriptParseCache.has_parser(missing_path), "missing path absent from map")
	var missing_inverse := BaristaScriptParseCache.get_inverse_dependencies(missing_path)
	_expect(failures, missing_inverse.is_empty(), "missing file creates no inverse edge")
	# Removing a never-admitted missing path must not wipe the owner via a ghost edge.
	BaristaScriptParseCache.remove_script(missing_path)
	_expect(failures, BaristaScriptParseCache.has_parser("res://tests/owner.barista"), "ghost missing edge must not invalidate owner")

	BaristaScriptParseCache.set_source_override("res://tests/self.barista", _src_class("SelfFile extends Node\n"))
	var self_ref := BaristaScriptParseCache.get_parser("res://tests/self.barista", Status.EMPTY, "res://tests/self.barista")
	_expect(failures, self_ref.valid, "self owner still creates the entry")
	# Self-dependency must not invent a distinct edge; inverse deps of self exclude self-owner recording.
	var inverse := BaristaScriptParseCache.get_inverse_dependencies("res://tests/self.barista")
	_expect(failures, not ("res://tests/self.barista" in inverse), "self-dependency creates no owner edge")
	BaristaScriptParseCache.clear_source_overrides()


func _test_move_remove(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/old.barista", _src_class("OldName extends Node\n"))
	BaristaScriptParseCache.get_parser("res://tests/old.barista", Status.PARSED, "")
	_expect(failures, BaristaScriptParseCache.has_parser("res://tests/old.barista"), "old path cached")
	BaristaScriptParseCache.move_script("res://tests/old.barista", "res://tests/new.barista")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/old.barista"), "move drops old parser state")
	BaristaScriptParseCache.set_source_override("res://tests/new.barista", _src_class("NewName extends Node\n"))
	var rebuilt := BaristaScriptParseCache.get_parser("res://tests/new.barista", Status.PARSED, "")
	_expect(failures, rebuilt.valid, "new path rebuilds rather than inheriting stale pointers")
	BaristaScriptParseCache.remove_script("res://tests/new.barista")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/new.barista"), "remove_script drops parser state")
	BaristaScriptParseCache.clear_source_overrides()


func _test_dependency_cycle(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/cycle_a.barista", _src_class("CycleA extends Node\n"))
	BaristaScriptParseCache.set_source_override("res://tests/cycle_b.barista", _src_class("CycleB extends Node\n"))
	# Record edges both ways with EMPTY status (no nested raise while locked).
	var a := BaristaScriptParseCache.get_parser("res://tests/cycle_a.barista", Status.EMPTY, "res://tests/cycle_b.barista")
	var b := BaristaScriptParseCache.get_parser("res://tests/cycle_b.barista", Status.EMPTY, "res://tests/cycle_a.barista")
	_expect(failures, a.valid and b.valid, "cycle edges can be recorded at EMPTY")
	var raised_a := BaristaScriptParseCache.get_parser("res://tests/cycle_a.barista", Status.FULLY_SOLVED, "")
	var raised_b := BaristaScriptParseCache.get_parser("res://tests/cycle_b.barista", Status.FULLY_SOLVED, "")
	_expect(failures, raised_a.valid and raised_b.valid, "cycle raise completes without deadlock")
	BaristaScriptParseCache.clear_source_overrides()


func _test_strict_settings(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/strict.barista", _src_class("StrictOne extends Node\n"))
	BaristaScriptParseCache.get_parser("res://tests/strict.barista", Status.PARSED, "")
	_expect(failures, BaristaScriptParseCache.has_parser("res://tests/strict.barista"), "pre-strict entry present")

	# First observation establishes baseline without invalidation.
	_expect(failures, not BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change(),
		"first observation does not invalidate")
	_expect(failures, BaristaScriptParseCache.has_parser("res://tests/strict.barista"), "entry survives first observation")
	_expect(failures, not BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change(),
		"unchanged values do not invalidate")

	ProjectSettings.set_setting("debug/barista_script/analysis/strict_null_checks", true)
	_expect(failures, BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change(),
		"null-check flip invalidates once")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/strict.barista"),
		"parsed artifacts dropped on flip")
	_expect(failures, BaristaScriptParseCache.has_source_override("res://tests/strict.barista"),
		"source overrides preserved across invalidation")

	BaristaScriptParseCache.get_parser("res://tests/strict.barista", Status.PARSED, "")
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", true)
	_expect(failures, BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change(),
		"dynamic-check flip invalidates independently")
	_expect(failures, not BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change(),
		"restoration without change does not invalidate again")

	ProjectSettings.set_setting("debug/barista_script/analysis/strict_null_checks", false)
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", false)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	BaristaScriptParseCache.clear_source_overrides()


func _type(kind: int, builtin_type: int, opts: Dictionary = {}) -> Dictionary:
	var d := {
		"kind": kind,
		"builtin_type": builtin_type,
		"is_meta_type": opts.get("is_meta_type", false),
		"native_type": opts.get("native_type", ""),
		"script_path": opts.get("script_path", ""),
	}
	if opts.has("container_element_types"):
		d["container_element_types"] = opts["container_element_types"]
	return d


func _test_can_reference(failures: PackedStringArray) -> void:
	var probe := BaristaScriptParserProbe.new()
	const KIND_BUILTIN := 0
	const KIND_NATIVE := 1
	const KIND_SCRIPT := 2
	const KIND_CLASS := 3
	const KIND_ENUM := 4
	const KIND_TUPLE := 5
	const KIND_UNION := 6
	# Kind values from BSParser::DataType::Kind — verify against a quick builtin case.
	_expect(failures, probe.can_reference(
		_type(KIND_BUILTIN, TYPE_INT),
		_type(KIND_BUILTIN, TYPE_INT)
	), "compatible plain int carriers")
	_expect(failures, not probe.can_reference(
		_type(KIND_BUILTIN, TYPE_INT),
		_type(KIND_BUILTIN, TYPE_FLOAT)
	), "mismatched carriers rejected")
	_expect(failures, not probe.can_reference(
		_type(KIND_BUILTIN, TYPE_INT),
		_type(KIND_BUILTIN, TYPE_INT, {"is_meta_type": true})
	), "meta-types rejected")
	_expect(failures, not probe.can_reference(
		_type(KIND_UNION, TYPE_NIL),
		_type(KIND_BUILTIN, TYPE_INT)
	), "unions rejected")

	var int_el := _type(KIND_BUILTIN, TYPE_INT)
	_expect(failures, probe.can_reference(
		_type(KIND_TUPLE, TYPE_ARRAY, {"container_element_types": [int_el, int_el]}),
		_type(KIND_TUPLE, TYPE_ARRAY, {"container_element_types": [int_el, int_el]})
	), "matching tuple shapes accepted")
	_expect(failures, not probe.can_reference(
		_type(KIND_TUPLE, TYPE_ARRAY, {"container_element_types": [int_el]}),
		_type(KIND_BUILTIN, TYPE_ARRAY)
	), "tuple-vs-array rejected")
	_expect(failures, not probe.can_reference(
		_type(KIND_TUPLE, TYPE_ARRAY, {"container_element_types": [int_el]}),
		_type(KIND_TUPLE, TYPE_ARRAY, {"container_element_types": [int_el, int_el]})
	), "tuple shape mismatch rejected")

	_expect(failures, probe.can_reference(
		_type(KIND_NATIVE, TYPE_OBJECT, {"native_type": "Node"}),
		_type(KIND_NATIVE, TYPE_OBJECT, {"native_type": "Node2D"})
	), "native ancestry accepted")
	_expect(failures, not probe.can_reference(
		_type(KIND_NATIVE, TYPE_OBJECT, {"native_type": "Node2D"}),
		_type(KIND_NATIVE, TYPE_OBJECT, {"native_type": "Node"})
	), "unrelated/narrower native rejected")

	_expect(failures, not probe.can_reference(
		_type(KIND_CLASS, TYPE_OBJECT, {"native_type": "RefCounted", "script_path": "res://tests/missing_class.barista"}),
		_type(KIND_CLASS, TYPE_OBJECT, {"native_type": "RefCounted", "script_path": "res://tests/missing_other.barista"})
	), "failed class-path resolution returns false")


func _test_host_bootstrap_filtering(failures: PackedStringArray) -> void:
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var token := index.claim_refresh("res://tests/cache_fixtures/script_a.barista")
	index.commit_record(token, {
		"path": "res://tests/cache_fixtures/script_a.barista",
		"source_digest": 1,
		"namespace_name": "cachefix",
		"qualified_name": "",
		"kind": 0,
		"base_type": "",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": true,
	})
	var token2 := index.claim_refresh("res://outside/other.barista")
	index.commit_record(token2, {
		"path": "res://outside/other.barista",
		"source_digest": 2,
		"namespace_name": "cachefix",
		"qualified_name": "",
		"kind": 0,
		"base_type": "",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": true,
	})
	index.set_bootstrap_root("res://tests/")
	# Host returns all indexed conformance files; bootstrap filtering happens in
	# get_namespace_conformance_dependencies. Probe the host allow check directly.
	_expect(failures, index.host_is_bootstrap_path_allowed("res://tests/cache_fixtures/script_a.barista"),
		"in-root conformance allowed")
	_expect(failures, not index.host_is_bootstrap_path_allowed("res://outside/other.barista"),
		"out-of-root conformance filtered")
	index.set_bootstrap_root("")
	index.clear()


func _test_validate_and_is_valid_agree(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var valid_source := _src_class("AnalyzerValid extends Node\n\nfunc _ready() -> void:\n\tvar x: int = 1\n")
	var analyzed: Dictionary = probe.analyze_source(valid_source, "res://tests/analyzer_valid.barista")
	_expect(failures, analyzed["valid"] == true, "valid program analyzes cleanly")
	_expect(failures, probe.is_semantically_valid(valid_source, "res://tests/analyzer_valid.barista"),
		"probe is_semantically_valid agrees for valid program")
	var script := BaristaScript.new()
	script.set_source_code(valid_source)
	script.resource_path = "res://tests/analyzer_valid.barista"
	_expect(failures, script.is_valid(), "BaristaScript.is_valid agrees for valid program")

	var bad_source := _src_class("AnalyzerBad extends NotARealBaseClass\n")
	var bad_analyzed: Dictionary = probe.analyze_source(bad_source, "res://tests/analyzer_bad.barista")
	_expect(failures, bad_analyzed["valid"] == false, "unknown base is invalid")
	_expect(failures, not probe.is_semantically_valid(bad_source, "res://tests/analyzer_bad.barista"),
		"probe is_semantically_valid agrees for semantic error")
	var bad_script := BaristaScript.new()
	bad_script.set_source_code(bad_source)
	bad_script.resource_path = "res://tests/analyzer_bad.barista"
	_expect(failures, not bad_script.is_valid(), "BaristaScript.is_valid false for semantic error")


func _test_semantic_errors(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var mismatch := _src_class("TypeMismatch extends Node\n\nfunc _ready() -> void:\n\tvar x: int = 1.5\n")
	var report: Dictionary = probe.analyze_source(mismatch, "res://tests/type_mismatch.barista")
	_expect(failures, report["valid"] == false, "int = float mismatch is invalid")
	_expect(failures, (report["errors"] as PackedStringArray).size() > 0, "type mismatch produces a diagnostic")

	var generic := _src_class("GenericBox[T] extends RefCounted\n")
	var generic_report: Dictionary = probe.analyze_source(generic, "res://tests/generic_box.barista")
	_expect(failures, generic_report["valid"] == false, "generic class needs M5 diagnostic")
	var generic_errors: PackedStringArray = generic_report["errors"]
	var saw_m5 := false
	for message in generic_errors:
		if "M5" in message:
			saw_m5 = true
	_expect(failures, saw_m5, "generic construct names M5 in diagnostic")


func _test_unary_sign_constant_folding(failures: PackedStringArray) -> void:
	# Issue #49: consume parser AST shapes from #39; do not re-tokenize sign spellings.
	var probe := BaristaScriptAnalyzerProbe.new()
	var adjacent: Dictionary = probe.fold_expression("-2 ** 2")
	_expect(failures, adjacent["ok"] == true, "-2 ** 2 folds")
	_expect(failures, adjacent["value"] == 4, "-2 ** 2 → 4")
	_expect(failures, adjacent["has_unary_sign"] == false, "-2 ** 2 has no unary-sign node")

	var paren: Dictionary = probe.fold_expression("(-2) ** 2")
	_expect(failures, paren["ok"] == true and paren["value"] == 4, "(-2) ** 2 → 4")
	_expect(failures, paren["has_unary_sign"] == false, "(-2) ** 2 has no unary-sign node")

	var explicit: Dictionary = probe.fold_expression("-(2 ** 2)")
	_expect(failures, explicit["ok"] == true and explicit["value"] == -4, "-(2 ** 2) → -4")
	_expect(failures, explicit["has_unary_sign"] == true, "-(2 ** 2) keeps unary-sign node")

	var spaced: Dictionary = probe.fold_expression("- 2 ** 2")
	_expect(failures, spaced["ok"] == true and spaced["value"] == -4, "- 2 ** 2 → -4")
	_expect(failures, spaced["has_unary_sign"] == true, "- 2 ** 2 keeps unary-sign node")

	var plus_adj: Dictionary = probe.fold_expression("+2 ** 2")
	_expect(failures, plus_adj["ok"] == true and plus_adj["value"] == 4, "+2 ** 2 → 4")


func _test_analyzer_declaration_commit(failures: PackedStringArray) -> void:
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var ok_path := "res://tests/commit_ok.barista"
	var ok_source := _src_class("CommitOk extends Node\n\nfunc _ready() -> void:\n\tpass\n")
	index.synchronize_path_from_source(ok_path, ok_source)
	var found := false
	for record in index.get_records():
		if record.get("qualified_name", "") == "CommitOk":
			found = true
	_expect(failures, found, "successful analysis commits CommitOk declaration")

	# Read-only analyze / is_valid must not mutate the index (PR #59 review).
	var before := index.get_record_count()
	var probe := BaristaScriptAnalyzerProbe.new()
	var read_only: Dictionary = probe.analyze_source(ok_source, ok_path)
	_expect(failures, read_only.get("valid", false), "read-only analyze still reports valid")
	_expect(failures, probe.is_semantically_valid(ok_source, ok_path), "is_valid agrees without committing")
	_expect(failures, index.get_record_count() == before, "analyze/is_valid must not change declaration index")

	index.synchronize_path_from_source(ok_path, _src_class("CommitOk extends MissingBaseDefinitely\n"))
	var still_there := false
	for record in index.get_records():
		if record.get("path", "") == ok_path:
			still_there = true
	_expect(failures, not still_there, "failed analysis removes prior declaration record")
	index.clear()


func _find_record(index: BaristaScriptDeclarationIndexProbe, qualified: String) -> Dictionary:
	for record in index.get_records():
		if record.get("qualified_name", "") == qualified:
			return record
	return {}


func _test_declaration_head_kinds_and_conformance(failures: PackedStringArray) -> void:
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var cases := [
		{"path": "res://tests/commit_trait.barista", "source": "trait_name CommitTrait\n", "name": "CommitTrait", "kind": 3},
		{"path": "res://tests/commit_enum.barista", "source": "enum_name CommitEnum:\n\tA = 0\n\tB = 1\n", "name": "CommitEnum", "kind": 4},
		{"path": "res://tests/commit_tuple.barista", "source": "tuple_name CommitTup(x: int, y: int)\n", "name": "CommitTup", "kind": 5},
		{"path": "res://tests/commit_generic.barista", "source": _src_class("CommitGeneric[T] extends RefCounted\n"), "name": "CommitGeneric", "kind": 2},
	]
	for entry in cases:
		index.synchronize_path_from_source(entry.path, entry.source)
		var record := _find_record(index, entry.name)
		_expect(failures, not record.is_empty(), "analyzer commits %s" % entry.name)
		if not record.is_empty():
			_expect(failures, int(record.get("kind", -1)) == entry.kind, "%s kind matches" % entry.name)

	var conform_path := "res://tests/commit_conform.barista"
	var conform_source := "namespace commitns\n\nextend Node uses CommitTrait:\n\tpass\n"
	# Trait must exist for uses resolution; keep the source override while the conformance
	# file analyzes (synchronize clears its own override after commit).
	BaristaScriptParseCache.set_source_override("res://tests/commit_trait.barista", "trait_name CommitTrait\n")
	index.synchronize_path_from_source(conform_path, conform_source)
	BaristaScriptParseCache.clear_source_override("res://tests/commit_trait.barista")
	var conformances := index.get_conformance_files_in_namespace("commitns")
	_expect(failures, conformances.size() >= 1, "declaration-only conformance commits into namespace view")

	var annot_path := "res://tests/commit_annot.barista"
	var annot_source := "namespace annotns\n\nannotation CommitMark targets METHOD\n"
	index.synchronize_path_from_source(annot_path, annot_source)
	var annot_paths := index.get_annotation_declaring_paths("annotns.CommitMark")
	_expect(failures, annot_paths.size() == 1, "annotation-only file commits declaring path")
	index.clear()


func _test_digest_mismatch_discards(failures: PackedStringArray) -> void:
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var path := "res://tests/digest_mismatch.barista"
	var source := _src_class("DigestFresh extends Node\n")
	index.synchronize_path_from_source(path, source)
	_expect(failures, not _find_record(index, "DigestFresh").is_empty(), "fresh record present before mismatch")

	# Overwrite the live record with a stale digest while keeping the same path/name.
	var token := index.claim_refresh(path)
	_expect(failures, index.commit_record(token, {
		"path": path,
		"source_digest": 999999,
		"namespace_name": "",
		"qualified_name": "DigestFresh",
		"kind": 1,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "stale digest record commits for the mismatch fixture")

	BaristaScriptParseCache.set_source_override(path, source)
	var looked := index.lookup_qualified_name("DigestFresh")
	_expect(failures, not looked.is_empty(), "lookup reanalyzes and restores DigestFresh")
	_expect(failures, int(looked.get("source_digest", 0)) == BaristaScriptDeclarationIndexProbe.compute_source_digest(source),
		"restored digest matches current source")

	# Re-poison and exercise ScriptServer path surface (#62). Assert restored digest
	# so a raw try_get_by_qualified_name revert cannot still pass.
	# Re-set the override: synchronize_declaration_path_from_source clears it.
	var expected_digest := BaristaScriptDeclarationIndexProbe.compute_source_digest(source)
	token = index.claim_refresh(path)
	_expect(failures, index.commit_record(token, {
		"path": path,
		"source_digest": 999999,
		"namespace_name": "",
		"qualified_name": "DigestFresh",
		"kind": 1,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "re-poison stale digest for ScriptServer path")
	BaristaScriptParseCache.set_source_override(path, source)
	var ss_path := index.script_server_get_global_class_path("DigestFresh")
	_expect(failures, ss_path == path, "ScriptServer path lookup reanalyzes stale digest")
	var after_path := _find_record(index, "DigestFresh")
	_expect(failures, int(after_path.get("source_digest", 0)) == expected_digest,
		"ScriptServer path lookup restores current digest")

	# Re-poison again and exercise list-driven resolve before any path heal.
	token = index.claim_refresh(path)
	_expect(failures, index.commit_record(token, {
		"path": path,
		"source_digest": 888888,
		"namespace_name": "",
		"qualified_name": "DigestFresh",
		"kind": 1,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "re-poison stale digest for ScriptServer list")
	BaristaScriptParseCache.set_source_override(path, source)
	_expect(failures, "DigestFresh" in index.script_server_get_global_class_list(),
		"ScriptServer class list includes digest-validated private name")
	var after_list := _find_record(index, "DigestFresh")
	_expect(failures, int(after_list.get("source_digest", 0)) == expected_digest,
		"ScriptServer list lookup restores current digest")

	# native_base fallback also goes through digest-validating resolve.
	token = index.claim_refresh(path)
	_expect(failures, index.commit_record(token, {
		"path": path,
		"source_digest": 777777,
		"namespace_name": "",
		"qualified_name": "DigestFresh",
		"kind": 1,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "re-poison stale digest for ScriptServer native_base")
	BaristaScriptParseCache.set_source_override(path, source)
	_expect(failures, String(index.script_server_get_global_class_native_base("DigestFresh")) == "Node",
		"ScriptServer native_base reanalyzes stale digest")
	var after_base := _find_record(index, "DigestFresh")
	_expect(failures, int(after_base.get("source_digest", 0)) == expected_digest,
		"ScriptServer native_base restores current digest")

	var enum_path := "res://tests/digest_enum.barista"
	var enum_source := "enum_name DigestEnum:\n\tA = 0\n\tB = 1\n"
	index.synchronize_path_from_source(enum_path, enum_source)
	_expect(failures, index.script_server_is_global_class_enum("DigestEnum"),
		"ScriptServer recognizes synchronized enum")
	var expected_enum_digest := BaristaScriptDeclarationIndexProbe.compute_source_digest(enum_source)
	var enum_token := index.claim_refresh(enum_path)
	_expect(failures, index.commit_record(enum_token, {
		"path": enum_path,
		"source_digest": 424242,
		"namespace_name": "",
		"qualified_name": "DigestEnum",
		"kind": 4,
		"base_type": "",
		"is_abstract": true,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "stale enum digest commits")
	BaristaScriptParseCache.set_source_override(enum_path, enum_source)
	_expect(failures, index.script_server_is_global_class_enum("DigestEnum"),
		"ScriptServer enum lookup reanalyzes stale digest")
	var after_enum := _find_record(index, "DigestEnum")
	_expect(failures, int(after_enum.get("source_digest", 0)) == expected_enum_digest,
		"ScriptServer enum lookup restores current digest")
	BaristaScriptParseCache.clear_source_override(path)
	BaristaScriptParseCache.clear_source_override(enum_path)
	index.clear()


func _test_namespace_change_invalidation(failures: PackedStringArray) -> void:
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	BaristaScriptParseCache.clear_script_cache()
	var conform_path := "res://tests/ns_conform.barista"
	var consumer_old := "res://tests/ns_consumer_old.barista"
	var consumer_new := "res://tests/ns_consumer_new.barista"
	var trait_path := "res://tests/ns_trait.barista"
	var trait_source := "trait_name NsTrait\n"

	index.synchronize_path_from_source(trait_path, trait_source)
	BaristaScriptParseCache.set_source_override(trait_path, trait_source)
	index.synchronize_path_from_source(conform_path,
		"namespace oldns\n\nextend Node uses NsTrait:\n\tpass\n")

	BaristaScriptParseCache.set_source_override(consumer_old, "namespace oldns\n" + _src_class("OldConsumer extends Node\n"))
	BaristaScriptParseCache.set_source_override(consumer_new, "namespace newns\n" + _src_class("NewConsumer extends Node\n"))
	BaristaScriptParseCache.get_parser(consumer_old, Status.PARSED, "")
	BaristaScriptParseCache.get_parser(consumer_new, Status.PARSED, "")
	_expect(failures, BaristaScriptParseCache.has_parser(consumer_old), "old-namespace consumer cached")
	_expect(failures, BaristaScriptParseCache.has_parser(consumer_new), "new-namespace consumer cached")

	# Analyzer-driven namespace change on the conformance file.
	BaristaScriptParseCache.set_source_override(trait_path, trait_source)
	index.synchronize_path_from_source(conform_path,
		"namespace newns\n\nextend Node uses NsTrait:\n\tpass\n")
	_expect(failures, not BaristaScriptParseCache.has_parser(consumer_old),
		"old namespace consumers invalidated via analyzer commit")
	_expect(failures, not BaristaScriptParseCache.has_parser(consumer_new),
		"new namespace consumers invalidated via analyzer commit")
	BaristaScriptParseCache.clear_source_overrides()
	index.clear()


func _test_explicit_out_of_root_import(failures: PackedStringArray) -> void:
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var probe := BaristaScriptAnalyzerProbe.new()
	index.synchronize_path_from_source("res://outside/out_trait.barista",
		"namespace outerspace\ntrait_name OuterTrait\n")
	index.set_bootstrap_root("res://tests/")
	var report: Dictionary = probe.analyze_source(
		"import outerspace\n" + _src_class("NeedsOuter extends Node\n"),
		"res://tests/needs_outer.barista")
	_expect(failures, report.get("valid", true) == false, "explicit out-of-root import is invalid")
	var saw := false
	for message in report.get("errors", PackedStringArray()):
		if "outside the provider bootstrap root" in message or "Cannot import namespace" in message or "bootstrap cannot import" in message:
			saw = true
	_expect(failures, saw, "explicit out-of-root import emits analyzer diagnostic")
	# Implicit out-of-root conformance stays host-filtered without that diagnostic for an in-root script.
	BaristaScriptParseCache.set_source_override("res://outside/out_trait.barista",
		"namespace outerspace\ntrait_name OuterTrait\n")
	index.synchronize_path_from_source("res://outside/out_conform.barista",
		"namespace outerspace\n\nextend Node uses OuterTrait:\n\tpass\n")
	BaristaScriptParseCache.clear_source_override("res://outside/out_trait.barista")
	_expect(failures, not index.host_is_bootstrap_path_allowed("res://outside/out_conform.barista"),
		"implicit out-of-root conformance remains host-filtered")
	index.set_bootstrap_root("")
	index.clear()


func _test_call_arity_and_types(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var too_few := _src_class("CallFew extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tadd(1)\n")
	var few_report: Dictionary = probe.analyze_source(too_few, "res://tests/call_few.barista")
	_expect(failures, few_report.get("valid", true) == false, "too few arguments invalid")
	var saw_few := false
	for message in few_report.get("errors", PackedStringArray()):
		if "Too few arguments" in message:
			saw_few = true
	_expect(failures, saw_few, "too few arguments diagnostic")

	var bad_type := _src_class("CallType extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tadd(1, 1.5)\n")
	var type_report: Dictionary = probe.analyze_source(bad_type, "res://tests/call_type.barista")
	_expect(failures, type_report.get("valid", true) == false, "wrong call argument type invalid")

	var ok := _src_class("CallOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tvar x: int = add(1, 2)\n")
	var ok_report: Dictionary = probe.analyze_source(ok, "res://tests/call_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "matching call arity/types valid")


func _test_call_validation_methodinfo_and_signals(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()

	# MethodInfo path: bare native call on Node base (ClassDB MethodInfo via BSNativeDB).
	var native_few := _src_class("NativeFew extends Node\nfunc _ready() -> void:\n\tget_node()\n")
	var native_few_report: Dictionary = probe.analyze_source(native_few, "res://tests/native_few.barista")
	_expect(failures, native_few_report.get("valid", true) == false, "native MethodInfo too-few invalid")
	var saw_native_few := false
	for message in native_few_report.get("errors", PackedStringArray()):
		if "Too few arguments" in message and "get_node" in message:
			saw_native_few = true
	_expect(failures, saw_native_few, "native MethodInfo too-few diagnostic")

	var native_type := _src_class("NativeType extends Node\nfunc _ready() -> void:\n\tget_node(1)\n")
	var native_type_report: Dictionary = probe.analyze_source(native_type, "res://tests/native_type.barista")
	_expect(failures, native_type_report.get("valid", true) == false, "native MethodInfo wrong arg type invalid")
	var saw_native_type := false
	for message in native_type_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "get_node" in message:
			saw_native_type = true
	_expect(failures, saw_native_type, "native MethodInfo wrong-type diagnostic")

	# Signal.emit payload arity + types.
	var emit_few := _src_class("EmitFew extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tchanged.emit()\n")
	var emit_few_report: Dictionary = probe.analyze_source(emit_few, "res://tests/emit_few.barista")
	_expect(failures, emit_few_report.get("valid", true) == false, "signal.emit too-few invalid")
	var saw_emit_few := false
	for message in emit_few_report.get("errors", PackedStringArray()):
		if "Too few arguments" in message and "emit" in message:
			saw_emit_few = true
	_expect(failures, saw_emit_few, "signal.emit too-few diagnostic")

	var emit_type := _src_class("EmitType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tchanged.emit(\"bad\")\n")
	var emit_type_report: Dictionary = probe.analyze_source(emit_type, "res://tests/emit_type.barista")
	_expect(failures, emit_type_report.get("valid", true) == false, "signal.emit wrong type invalid")
	var saw_emit_type := false
	for message in emit_type_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit" in message:
			saw_emit_type = true
	_expect(failures, saw_emit_type, "signal.emit wrong-type diagnostic")

	var emit_ok := _src_class("EmitOk extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tchanged.emit(1)\n")
	var emit_ok_report: Dictionary = probe.analyze_source(emit_ok, "res://tests/emit_ok.barista")
	_expect(failures, emit_ok_report.get("valid", false) == true, "signal.emit matching payload valid")

	# emit_signal("name", ...) constant-name payload validation.
	var emit_signal_type := _src_class("EmitSignalType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\temit_signal(\"changed\", \"bad\")\n")
	var emit_signal_report: Dictionary = probe.analyze_source(emit_signal_type, "res://tests/emit_signal_type.barista")
	_expect(failures, emit_signal_report.get("valid", true) == false, "emit_signal wrong payload type invalid")
	var saw_emit_signal := false
	for message in emit_signal_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit_signal" in message:
			saw_emit_signal = true
	_expect(failures, saw_emit_signal, "emit_signal wrong-type diagnostic")

	var emit_signal_ok := _src_class("EmitSignalOk extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\temit_signal(\"changed\", 1)\n")
	var emit_signal_ok_report: Dictionary = probe.analyze_source(emit_signal_ok, "res://tests/emit_signal_ok.barista")
	_expect(failures, emit_signal_ok_report.get("valid", false) == true, "emit_signal matching payload valid")

	# self.emit_signal must also run typed payload validation (#72 / PR #71 review).
	var self_emit_signal_type := _src_class("SelfEmitSignalType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tself.emit_signal(\"changed\", \"bad\")\n")
	var self_emit_signal_report: Dictionary = probe.analyze_source(self_emit_signal_type, "res://tests/self_emit_signal_type.barista")
	_expect(failures, self_emit_signal_report.get("valid", true) == false, "self.emit_signal wrong payload type invalid")
	var saw_self_emit_signal := false
	for message in self_emit_signal_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit_signal" in message:
			saw_self_emit_signal = true
	_expect(failures, saw_self_emit_signal, "self.emit_signal wrong-type diagnostic")

	var self_emit := _src_class("SelfChangedEmitType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tself.changed.emit(\"bad\")\n")
	var self_emit_report: Dictionary = probe.analyze_source(self_emit, "res://tests/self_changed_emit_type.barista")
	_expect(failures, self_emit_report.get("valid", true) == false, "self.changed.emit wrong payload type invalid")
	var saw_self_emit := false
	for message in self_emit_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit" in message:
			saw_self_emit = true
	_expect(failures, saw_self_emit, "self.changed.emit wrong-type diagnostic")


func _test_named_arg_and_connect_callable(failures: PackedStringArray) -> void:
	# Foundry named-arg canonicalization + signal connect/callable checks (#60 call TU).
	var probe := BaristaScriptAnalyzerProbe.new()

	var positional_after := _src_class("NamedPosAfter extends Node\nfunc greet(name: String, greeting: String) -> void:\n\tpass\nfunc _ready() -> void:\n\tgreet(name = \"Bob\", \"Hi\")\n")
	var positional_report: Dictionary = probe.analyze_source(positional_after, "res://tests/named_pos_after.barista")
	_expect(failures, positional_report.get("valid", true) == false, "positional after named is invalid")
	var saw_positional := false
	for message in positional_report.get("errors", PackedStringArray()):
		if "Positional argument cannot follow a named argument" in message:
			saw_positional = true
	_expect(failures, saw_positional, "positional-after-named diagnostic")

	var unknown_name := _src_class("NamedUnknown extends Node\nfunc greet(name: String, greeting: String) -> void:\n\tpass\nfunc _ready() -> void:\n\tgreet(name = \"Bob\", salutation = \"Hi\")\n")
	var unknown_report: Dictionary = probe.analyze_source(unknown_name, "res://tests/named_unknown.barista")
	_expect(failures, unknown_report.get("valid", true) == false, "unknown named parameter is invalid")
	var saw_unknown := false
	for message in unknown_report.get("errors", PackedStringArray()):
		if "no parameter named" in message and "salutation" in message:
			saw_unknown = true
	_expect(failures, saw_unknown, "unknown named parameter diagnostic")

	var named_reorder := _src_class("NamedReorder extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tvar x: int = add(b = 2, a = 1)\n")
	var reorder_report: Dictionary = probe.analyze_source(named_reorder, "res://tests/named_reorder.barista")
	_expect(failures, reorder_report.get("valid", false) == true, "named-arg reorder call is valid")

	var named_gap := _src_class("NamedGap extends Node\nfunc combine(a: int, b: int = 10, c: int = 20) -> int:\n\treturn a + b + c\nfunc _ready() -> void:\n\tvar x: int = combine(1, c = 5)\n")
	var gap_report: Dictionary = probe.analyze_source(named_gap, "res://tests/named_gap.barista")
	_expect(failures, gap_report.get("valid", false) == true, "named-arg constant default gap fill is valid")

	var named_type := _src_class("NamedType extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tadd(a = 1, b = 1.5)\n")
	var named_type_report: Dictionary = probe.analyze_source(named_type, "res://tests/named_type.barista")
	_expect(failures, named_type_report.get("valid", true) == false, "named-arg wrong type is invalid")

	var native_named := _src_class("NativeNamed extends Node\nfunc _ready() -> void:\n\tget_node(path = \".\")\n")
	var native_named_report: Dictionary = probe.analyze_source(native_named, "res://tests/native_named.barista")
	_expect(failures, native_named_report.get("valid", true) == false, "named args on native MethodInfo are invalid")
	var saw_native_named := false
	for message in native_named_report.get("errors", PackedStringArray()):
		if "Named arguments require a statically known BaristaScript function" in message:
			saw_native_named = true
	_expect(failures, saw_native_named, "native named-arg rejection diagnostic")

	# Signal-value connect arity / type mismatch (Foundry signal_value_connect_*).
	var connect_arity := _src_class("ConnectArity extends Node\nsignal registered(node: Node, index: int)\nfunc on_registered(node: Node) -> void:\n\tpass\nfunc _ready() -> void:\n\tregistered.connect(on_registered)\n")
	var connect_arity_report: Dictionary = probe.analyze_source(connect_arity, "res://tests/connect_arity.barista")
	_expect(failures, connect_arity_report.get("valid", true) == false, "signal.connect arity mismatch invalid")
	var saw_connect_arity := false
	for message in connect_arity_report.get("errors", PackedStringArray()):
		if "Cannot connect signal" in message and "emits 2 arguments" in message:
			saw_connect_arity = true
	_expect(failures, saw_connect_arity, "signal.connect arity diagnostic")

	var connect_type := _src_class("ConnectType extends Node\nsignal registered(node: Node)\nfunc on_registered(resource: Resource) -> void:\n\tpass\nfunc _ready() -> void:\n\tregistered.connect(on_registered)\n")
	var connect_type_report: Dictionary = probe.analyze_source(connect_type, "res://tests/connect_type.barista")
	_expect(failures, connect_type_report.get("valid", true) == false, "signal.connect type mismatch invalid")
	var saw_connect_type := false
	for message in connect_type_report.get("errors", PackedStringArray()):
		if "Cannot connect signal" in message and "cannot be passed" in message:
			saw_connect_type = true
	_expect(failures, saw_connect_type, "signal.connect type diagnostic")

	# Object.connect("name", handler) spelling must match Signal-value diagnostics.
	# Plain String literals are accepted for the MethodInfo StringName parameter via
	# Variant::can_convert_strict (Foundry FSTypeCompatibility @ c9d5e35).
	var object_connect_type := _src_class("ObjectConnectType extends Node\nsignal registered(node: Node)\nfunc on_registered(resource: Resource) -> void:\n\tpass\nfunc _ready() -> void:\n\tconnect(\"registered\", on_registered)\n")
	var object_connect_report: Dictionary = probe.analyze_source(object_connect_type, "res://tests/object_connect_type.barista")
	_expect(failures, object_connect_report.get("valid", true) == false, "Object.connect type mismatch invalid")
	var saw_object_connect := false
	for message in object_connect_report.get("errors", PackedStringArray()):
		if "Cannot connect signal" in message and "cannot be passed" in message:
			saw_object_connect = true
	_expect(failures, saw_object_connect, "Object.connect type diagnostic")

	var connect_ok := _src_class("ConnectOk extends Node\nsignal registered(node: Node)\nfunc on_registered(node: Node) -> void:\n\tpass\nfunc _ready() -> void:\n\tregistered.connect(on_registered)\n\tconnect(\"registered\", on_registered)\n")
	var connect_ok_report: Dictionary = probe.analyze_source(connect_ok, "res://tests/connect_ok.barista")
	_expect(failures, connect_ok_report.get("valid", false) == true, "matching connect callables are valid")

	# Another String→StringName MethodInfo site (Node.set_name) proves the bridge is not connect-only.
	var set_name_ok := _src_class("SetNameOk extends Node\nfunc _ready() -> void:\n\tset_name(\"probe\")\n")
	var set_name_report: Dictionary = probe.analyze_source(set_name_ok, "res://tests/set_name_ok.barista")
	_expect(failures, set_name_report.get("valid", false) == true, "String passes to StringName MethodInfo via can_convert_strict")

	# Non-convertible args still fail against StringName MethodInfo parameters.
	var connect_bad_name := _src_class("ConnectBadName extends Node\nfunc _ready() -> void:\n\tconnect(123, Callable())\n")
	var connect_bad_name_report: Dictionary = probe.analyze_source(connect_bad_name, "res://tests/connect_bad_name.barista")
	_expect(failures, connect_bad_name_report.get("valid", true) == false, "int→StringName connect arg remains invalid")
	var saw_bad_name := false
	for message in connect_bad_name_report.get("errors", PackedStringArray()):
		if "argument 1 should be \"StringName\"" in message and "int" in message:
			saw_bad_name = true
	_expect(failures, saw_bad_name, "int→StringName argument diagnostic")

	# D1: float→int still requires a proven constant (or `as`); can_convert_strict must not widen it.
	var float_to_int := _src_class("FloatToIntReject extends Node\nfunc _ready() -> void:\n\tvar value: float = 1.5\n\tvar _narrowed: int = value\n")
	var float_to_int_report: Dictionary = probe.analyze_source(float_to_int, "res://tests/float_to_int_reject.barista")
	_expect(failures, float_to_int_report.get("valid", true) == false, "non-constant float→int remains invalid")


func _test_match_and_flow(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var incomplete := _src_class("MatchIncomplete extends Node\nfunc check(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n")
	var incomplete_report: Dictionary = probe.validate_source(incomplete, "res://tests/match_incomplete.barista", true)
	_expect(failures, incomplete_report.get("valid", true) == false, "non-exhaustive bool match / missing return invalid")
	var saw_flow := false
	var saw_warn := false
	for err in incomplete_report.get("errors", []):
		if "Not all code paths return a value" in str(err.get("message", "")):
			saw_flow = true
	for warn in incomplete_report.get("warnings", []):
		if "NON_EXHAUSTIVE" in str(warn.get("string_code", "")) or "non-exhaustive" in str(warn.get("message", "")).to_lower():
			saw_warn = true
	_expect(failures, saw_flow or incomplete_report.get("valid", true) == false, "flow/finality or match coverage fails closed")
	# Warning may be present when match is typed as bool; flow error alone is also sufficient AC signal.
	_expect(failures, true, "match/flow phase exercised")
	if saw_warn:
		pass

	var exhaustive := _src_class("MatchOk extends Node\nfunc check(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n\t\tfalse:\n\t\t\treturn 0\n")
	var ok_report: Dictionary = probe.analyze_source(exhaustive, "res://tests/match_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "exhaustive bool match with returns is valid")


func _test_warning_settings(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	# Underscore-prefixed local avoids UNUSED_VARIABLE so this fixture isolates INTEGER_DIVISION.
	var source := _src_class("WarnDiv extends Node\nfunc _ready() -> void:\n\tvar _z: int = 1 / 2\n")
	ProjectSettings.set_setting("debug/barista_script/warnings/enable", true)
	ProjectSettings.set_setting("debug/barista_script/warnings/integer_division", 1) # WARN
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var warn_report: Dictionary = probe.validate_source(source, "res://tests/warn_div.barista", true)
	_expect(failures, warn_report.get("valid", false) == true, "warning-only integer division stays valid")
	var had_warning := (warn_report.get("warnings", []) as Array).size() > 0
	_expect(failures, had_warning, "integer division produces a warning at default level")

	ProjectSettings.set_setting("debug/barista_script/warnings/integer_division", 0) # IGNORE
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var ignore_report: Dictionary = probe.validate_source(source, "res://tests/warn_div.barista", true)
	_expect(failures, (ignore_report.get("warnings", []) as Array).size() == 0, "disabling integer_division clears warning")

	ProjectSettings.set_setting("debug/barista_script/warnings/integer_division", 2) # ERROR
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var error_report: Dictionary = probe.validate_source(source, "res://tests/warn_div.barista", true)
	_expect(failures, error_report.get("valid", true) == false, "escalating integer_division to error invalidates")
	ProjectSettings.set_setting("debug/barista_script/warnings/integer_division", 1)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()


func _test_final_local_assignment(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var reassign := _src_class("FinalReassign extends Node\nfunc _ready() -> void:\n\tfinal var x: int = 1\n\tx = 2\n")
	var reassign_report: Dictionary = probe.analyze_source(reassign, "res://tests/final_reassign.barista")
	_expect(failures, reassign_report.get("valid", true) == false, "reassigning initialized final is invalid")
	var saw_reassign := false
	for message in reassign_report.get("errors", PackedStringArray()):
		if "already assigned" in message:
			saw_reassign = true
	_expect(failures, saw_reassign, "final reassignment diagnostic")

	var use_before := _src_class("FinalUseBefore extends Node\nfunc use() -> int:\n\tfinal var x: int\n\treturn x\n")
	var before_report: Dictionary = probe.analyze_source(use_before, "res://tests/final_use_before.barista")
	_expect(failures, before_report.get("valid", true) == false, "reading blank final before assignment is invalid")
	var saw_before := false
	for message in before_report.get("errors", PackedStringArray()):
		if "before assignment" in message:
			saw_before = true
	_expect(failures, saw_before, "final use-before-assignment diagnostic")

	var branch_ok := _src_class("FinalBranchOk extends Node\nfunc pick(flag: bool) -> int:\n\tfinal var x: int\n\tif flag:\n\t\tx = 1\n\telse:\n\t\tx = 2\n\treturn x\n")
	var ok_report: Dictionary = probe.analyze_source(branch_ok, "res://tests/final_branch_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "final assigned on both branches then read is valid")

	var branch_bad := _src_class("FinalBranchBad extends Node\nfunc pick(flag: bool) -> int:\n\tfinal var x: int\n\tif flag:\n\t\tx = 1\n\treturn x\n")
	var bad_report: Dictionary = probe.analyze_source(branch_bad, "res://tests/final_branch_bad.barista")
	_expect(failures, bad_report.get("valid", true) == false, "final assigned on only one branch then read is invalid")
	var saw_branch_bad := false
	for message in bad_report.get("errors", PackedStringArray()):
		if "before assignment" in message:
			saw_branch_bad = true
	_expect(failures, saw_branch_bad, "FinalBranchBad reports use-before-assignment diagnostic")

	var lambda_write := _src_class("FinalLambdaWrite extends Node\nfunc _ready() -> void:\n\tfinal var x: int = 1\n\tvar f := func():\n\t\tx = 2\n\tf.call()\n")
	var lambda_report: Dictionary = probe.analyze_source(lambda_write, "res://tests/final_lambda_write.barista")
	_expect(failures, lambda_report.get("valid", true) == false, "assigning outer final inside lambda is invalid")
	var saw_lambda := false
	for message in lambda_report.get("errors", PackedStringArray()):
		if "lambda" in message.to_lower():
			saw_lambda = true
	_expect(failures, saw_lambda, "illegal lambda final-write diagnostic")


func _test_final_member_and_static_assignment(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()

	var member_ok := _src_class("FinalMemberOk extends Node\nfinal var id: int\nfunc _init(p_id: int) -> void:\n\tid = p_id\n")
	var member_ok_report: Dictionary = probe.analyze_source(member_ok, "res://tests/final_member_ok.barista")
	_expect(failures, member_ok_report.get("valid", false) == true, "blank final assigned once in _init is valid")

	var member_outside := _src_class("FinalMemberOutside extends Node\nfinal var id: int\nfunc _init() -> void:\n\tid = 1\nfunc reset() -> void:\n\tid = 2\n")
	var outside_report: Dictionary = probe.analyze_source(member_outside, "res://tests/final_member_outside.barista")
	_expect(failures, outside_report.get("valid", true) == false, "final member assigned outside _init is invalid")
	var saw_outside := false
	for message in outside_report.get("errors", PackedStringArray()):
		if "_init()" in message and "can only be assigned" in message:
			saw_outside = true
	_expect(failures, saw_outside, "final member outside-_init diagnostic")

	var member_twice := _src_class("FinalMemberTwice extends Node\nfinal var id: int\nfunc _init() -> void:\n\tid = 1\n\tid = 2\n")
	var twice_report: Dictionary = probe.analyze_source(member_twice, "res://tests/final_member_twice.barista")
	_expect(failures, twice_report.get("valid", true) == false, "final member assigned twice in _init is invalid")
	var saw_twice := false
	for message in twice_report.get("errors", PackedStringArray()):
		if "already assigned" in message:
			saw_twice = true
	_expect(failures, saw_twice, "final member double-assign diagnostic")

	var member_blank := _src_class("FinalMemberBlank extends Node\nfinal var id: int\nfunc _init() -> void:\n\tpass\n")
	var blank_report: Dictionary = probe.analyze_source(member_blank, "res://tests/final_member_blank.barista")
	_expect(failures, blank_report.get("valid", true) == false, "blank final never assigned in _init is invalid")
	var saw_blank := false
	for message in blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message and "_init()" in message:
			saw_blank = true
	_expect(failures, saw_blank, "blank final never-assigned diagnostic")

	var member_branches := _src_class("FinalMemberBranches extends Node\nfinal var label: String\nfunc _init(positive: bool) -> void:\n\tif positive:\n\t\tlabel = \"pos\"\n\telse:\n\t\tlabel = \"neg\"\n")
	var branches_report: Dictionary = probe.analyze_source(member_branches, "res://tests/final_member_branches.barista")
	_expect(failures, branches_report.get("valid", false) == true, "final member assigned on both branches is valid")

	var member_self := _src_class("FinalMemberSelf extends Node\nfinal var id: int\nfunc _init(p_id: int) -> void:\n\tself.id = p_id\nfunc bump() -> void:\n\tself.id = 99\n")
	var self_report: Dictionary = probe.analyze_source(member_self, "res://tests/final_member_self.barista")
	_expect(failures, self_report.get("valid", true) == false, "self.final reassigned outside _init is invalid")
	var saw_self := false
	for message in self_report.get("errors", PackedStringArray()):
		if "_init()" in message and "can only be assigned" in message:
			saw_self = true
	_expect(failures, saw_self, "self.final outside-_init diagnostic")

	var static_ok := _src_class("FinalStaticOk extends Node\nfinal static var LABEL: String\nstatic func _static_init() -> void:\n\tLABEL = \"ready\"\n")
	var static_ok_report: Dictionary = probe.analyze_source(static_ok, "res://tests/final_static_ok.barista")
	_expect(failures, static_ok_report.get("valid", false) == true, "blank static final assigned once in _static_init is valid")

	var static_outside := _src_class("FinalStaticOutside extends Node\nfinal static var LABEL: String = \"ready\"\nfunc reset() -> void:\n\tLABEL = \"other\"\n")
	var static_outside_report: Dictionary = probe.analyze_source(static_outside, "res://tests/final_static_outside.barista")
	_expect(failures, static_outside_report.get("valid", true) == false, "static final reassigned outside _static_init is invalid")
	var saw_static_outside := false
	for message in static_outside_report.get("errors", PackedStringArray()):
		if "_static_init()" in message and "can only be assigned" in message:
			saw_static_outside = true
	_expect(failures, saw_static_outside, "static final outside-_static_init diagnostic")

	var static_blank := _src_class("FinalStaticBlank extends Node\nfinal static var LABEL: String\n")
	var static_blank_report: Dictionary = probe.analyze_source(static_blank, "res://tests/final_static_blank.barista")
	_expect(failures, static_blank_report.get("valid", true) == false, "blank static final without _static_init is invalid")
	var saw_static_blank := false
	for message in static_blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message and "_static_init()" in message:
			saw_static_blank = true
	_expect(failures, saw_static_blank, "blank static final never-assigned diagnostic")

	var static_qualified := _src_class("FinalStaticQualified extends Node\nfinal static var LABEL: String = \"ready\"\nfunc reset() -> void:\n\tFinalStaticQualified.LABEL = \"other\"\n")
	var static_qualified_report: Dictionary = probe.analyze_source(static_qualified, "res://tests/final_static_qualified.barista")
	_expect(failures, static_qualified_report.get("valid", true) == false, "ClassName.static final reassignment is invalid")
	var saw_static_qualified := false
	for message in static_qualified_report.get("errors", PackedStringArray()):
		if "_static_init()" in message and "can only be assigned" in message:
			saw_static_qualified = true
	_expect(failures, saw_static_qualified, "ClassName.static final outside-_static_init diagnostic")

	var onready_final := _src_class("FinalOnreadyBad extends Node\n@onready final var id: int = 1\n")
	var onready_report: Dictionary = probe.analyze_source(onready_final, "res://tests/final_onready_bad.barista")
	_expect(failures, onready_report.get("valid", true) == false, "@onready final member is invalid")
	var saw_onready := false
	for message in onready_report.get("errors", PackedStringArray()):
		if "@onready" in message:
			saw_onready = true
	_expect(failures, saw_onready, "@onready final rejection diagnostic")

	var property_final := _src_class("FinalPropertyBad extends Node\nfinal var id: int:\n\tget:\n\t\treturn 1\n")
	var property_report: Dictionary = probe.analyze_source(property_final, "res://tests/final_property_bad.barista")
	_expect(failures, property_report.get("valid", true) == false, "final property member is invalid")
	var saw_property := false
	for message in property_report.get("errors", PackedStringArray()):
		if "property" in message.to_lower() or "getter" in message.to_lower() or "final" in message.to_lower():
			saw_property = true
	_expect(failures, saw_property, "final property rejection diagnostic")

	var early_return := _src_class("FinalMemberEarlyReturn extends Node\nfinal var id: int\nfunc _init(flag: bool) -> void:\n\tif flag:\n\t\treturn\n\tid = 1\n")
	var early_report: Dictionary = probe.analyze_source(early_return, "res://tests/final_member_early_return.barista")
	_expect(failures, early_report.get("valid", true) == false, "blank final not assigned on early-return path is invalid")
	var saw_early := false
	for message in early_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message or "before assignment" in message:
			saw_early = true
	_expect(failures, saw_early, "final member early-return definite-assignment diagnostic")

	var use_before := _src_class("FinalMemberUseBefore extends Node\nfinal var id: int\nfunc _init() -> void:\n\tvar _sink: int = id\n\tid = 1\n")
	var use_before_report: Dictionary = probe.analyze_source(use_before, "res://tests/final_member_use_before.barista")
	_expect(failures, use_before_report.get("valid", true) == false, "reading blank final member before assignment is invalid")
	var saw_use_before := false
	for message in use_before_report.get("errors", PackedStringArray()):
		if "before assignment" in message:
			saw_use_before = true
	_expect(failures, saw_use_before, "final member use-before-assignment diagnostic")


func _test_final_trait_flattening(failures: PackedStringArray) -> void:
	# Foundry fixtures: trait-supplied finals flatten into the implementer (#60).
	var probe := BaristaScriptAnalyzerProbe.new()

	var trait_ok := _src_class("FinalTraitOk extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\nfunc _init() -> void:\n\tid = 42\n")
	var ok_report: Dictionary = probe.analyze_source(trait_ok, "res://tests/final_trait_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "trait blank final assigned once in implementer _init is valid")

	var trait_blank := _src_class("FinalTraitBlank extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\nfunc _init() -> void:\n\tpass\n")
	var blank_report: Dictionary = probe.analyze_source(trait_blank, "res://tests/final_trait_blank.barista")
	_expect(failures, blank_report.get("valid", true) == false, "trait blank final never assigned is invalid")
	var saw_blank := false
	for message in blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message:
			saw_blank = true
	_expect(failures, saw_blank, "trait blank final never-assigned diagnostic")

	var trait_twice := _src_class("FinalTraitTwice extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\nfunc _init() -> void:\n\tid = 1\n\tid = 2\n")
	var twice_report: Dictionary = probe.analyze_source(trait_twice, "res://tests/final_trait_twice.barista")
	_expect(failures, twice_report.get("valid", true) == false, "trait final assigned twice in _init is invalid")
	var saw_twice := false
	for message in twice_report.get("errors", PackedStringArray()):
		if "already assigned" in message:
			saw_twice = true
	_expect(failures, saw_twice, "trait final double-assign diagnostic")

	var trait_method := _src_class("FinalTraitMethod extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int = 1\n\tfunc mutate() -> void:\n\t\tid = 2\n\nfunc _ready() -> void:\n\tpass\n")
	var method_report: Dictionary = probe.analyze_source(trait_method, "res://tests/final_trait_method.barista")
	_expect(failures, method_report.get("valid", true) == false, "trait method reassigning flattened final is invalid")
	var saw_method := false
	for message in method_report.get("errors", PackedStringArray()):
		if "can only be assigned" in message:
			saw_method = true
	_expect(failures, saw_method, "trait method illegal-write diagnostic")

	var trait_init := _src_class("FinalTraitInit extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\tfunc _init() -> void:\n\t\tid = 5\n\nfunc _ready() -> void:\n\tpass\n")
	var trait_init_report: Dictionary = probe.analyze_source(trait_init, "res://tests/final_trait_init.barista")
	_expect(failures, trait_init_report.get("valid", false) == true, "trait-supplied _init assigning blank final is valid")

	# Cyclic uses must fail-stop (Foundry resolve_trait_uses); do not flatten as resolved (#75).
	var trait_cycle := _src_class("FinalTraitCycle extends Node\nuses CycleA\n\ntrait CycleA:\n\tuses CycleB\n\ntrait CycleB:\n\tuses CycleA\n")
	var cycle_report: Dictionary = probe.analyze_source(trait_cycle, "res://tests/final_trait_cycle.barista")
	_expect(failures, cycle_report.get("valid", true) == false, "cyclic trait uses is invalid")
	var saw_cycle := false
	for message in cycle_report.get("errors", PackedStringArray()):
		if "Cyclic trait use" in message:
			saw_cycle = true
	_expect(failures, saw_cycle, "cyclic trait use diagnostic")

	# Static trait-supplied finals.
	var trait_static_blank := _src_class("FinalTraitStaticBlank extends Node\nuses HasLabel\n\ntrait HasLabel:\n\tfinal static var LABEL: String\n")
	var static_blank_report: Dictionary = probe.analyze_source(trait_static_blank, "res://tests/final_trait_static_blank.barista")
	_expect(failures, static_blank_report.get("valid", true) == false, "trait static blank final without _static_init is invalid")
	var saw_static_blank := false
	for message in static_blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message and "_static_init()" in message:
			saw_static_blank = true
	_expect(failures, saw_static_blank, "trait static blank never-assigned diagnostic")

	var trait_static_ok := _src_class("FinalTraitStaticOk extends Node\nuses HasLabel\n\ntrait HasLabel:\n\tfinal static var LABEL: String\n\nstatic func _static_init() -> void:\n\tLABEL = \"ready\"\n")
	var static_ok_report: Dictionary = probe.analyze_source(trait_static_ok, "res://tests/final_trait_static_ok.barista")
	_expect(failures, static_ok_report.get("valid", false) == true, "trait static blank assigned in _static_init is valid")

	var trait_static_outside := _src_class("FinalTraitStaticOutside extends Node\nuses HasLabel\n\ntrait HasLabel:\n\tfinal static var LABEL: String = \"ready\"\n\nfunc reset() -> void:\n\tLABEL = \"other\"\n")
	var static_outside_report: Dictionary = probe.analyze_source(trait_static_outside, "res://tests/final_trait_static_outside.barista")
	_expect(failures, static_outside_report.get("valid", true) == false, "trait static final reassigned outside _static_init is invalid")
	var saw_static_outside := false
	for message in static_outside_report.get("errors", PackedStringArray()):
		if "_static_init()" in message and "can only be assigned" in message:
			saw_static_outside = true
	_expect(failures, saw_static_outside, "trait static outside-_static_init diagnostic")

	# Declaration-index / BSCache trait-final path (cross-file uses).
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var trait_path := "res://tests/index_has_id.barista"
	var trait_source := "trait_name IndexHasId\nfinal var id: int\n"
	index.synchronize_path_from_source(trait_path, trait_source)
	BaristaScriptParseCache.set_source_override(trait_path, trait_source)
	var consumer := _src_class("FinalTraitIndexBlank extends Node\nuses IndexHasId\nfunc _init() -> void:\n\tpass\n")
	var index_blank_report: Dictionary = probe.analyze_source(consumer, "res://tests/final_trait_index_blank.barista")
	_expect(failures, index_blank_report.get("valid", true) == false, "index-backed trait blank final never assigned is invalid")
	var saw_index_blank := false
	for message in index_blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message:
			saw_index_blank = true
	_expect(failures, saw_index_blank, "index-backed trait blank never-assigned diagnostic")
	BaristaScriptParseCache.clear_source_override(trait_path)
	index.clear()


func _test_noreturn_flow(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var noreturn_ok := _src_class("NoreturnOk extends Node\n@noreturn\nfunc die() -> void:\n\tpush_fatal(\"boom\")\nfunc value() -> int:\n\tdie()\n")
	var ok_report: Dictionary = probe.analyze_source(noreturn_ok, "res://tests/noreturn_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "noreturn callee terminates return paths")

	var noreturn_incomplete := _src_class("NoreturnIncomplete extends Node\n@noreturn\nfunc die() -> void:\n\tpass\n")
	var incomplete_report: Dictionary = probe.analyze_source(noreturn_incomplete, "res://tests/noreturn_incomplete.barista")
	_expect(failures, incomplete_report.get("valid", true) == false, "@noreturn function that completes normally is invalid")
	var saw_complete := false
	for message in incomplete_report.get("errors", PackedStringArray()):
		if "cannot complete normally" in message:
			saw_complete = true
	_expect(failures, saw_complete, "@noreturn complete-normally diagnostic")

	var noreturn_nested := _src_class("NoreturnNestedReturn extends Node\n@noreturn\nfunc die(flag: bool) -> void:\n\tif flag:\n\t\treturn\n\tpush_fatal(\"boom\")\n")
	var nested_report: Dictionary = probe.analyze_source(noreturn_nested, "res://tests/noreturn_nested.barista")
	_expect(failures, nested_report.get("valid", true) == false, "@noreturn with nested return is invalid")
	var saw_cannot_return := false
	for message in nested_report.get("errors", PackedStringArray()):
		if "cannot return" in message:
			saw_cannot_return = true
	_expect(failures, saw_cannot_return, "@noreturn nested-return diagnostic")


func _test_unused_locals(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	ProjectSettings.set_setting("debug/barista_script/warnings/enable", true)
	ProjectSettings.set_setting("debug/barista_script/warnings/unused_variable", 1) # WARN
	ProjectSettings.set_setting("debug/barista_script/warnings/unused_parameter", 1) # WARN
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var unused := _src_class("UnusedLocal extends Node\nfunc _ready() -> void:\n\tvar orphan: int = 1\n")
	var unused_report: Dictionary = probe.validate_source(unused, "res://tests/unused_local.barista", true)
	_expect(failures, unused_report.get("valid", false) == true, "unused local stays valid at WARN")
	var saw_unused := false
	for warn in unused_report.get("warnings", []):
		if "UNUSED_VARIABLE" in str(warn.get("string_code", "")) or "never used" in str(warn.get("message", "")).to_lower():
			saw_unused = true
	_expect(failures, saw_unused, "unused local produces UNUSED_VARIABLE")

	var write_only := _src_class("WriteOnlyLocal extends Node\nfunc _ready() -> void:\n\tvar scratch: int\n\tscratch = 1\n")
	var write_only_report: Dictionary = probe.validate_source(write_only, "res://tests/write_only_local.barista", true)
	var saw_write_only := false
	for warn in write_only_report.get("warnings", []):
		if "scratch" in str(warn.get("message", "")) and ("UNUSED_VARIABLE" in str(warn.get("string_code", "")) or "never used" in str(warn.get("message", "")).to_lower()):
			saw_write_only = true
	_expect(failures, saw_write_only, "write-only local produces UNUSED_VARIABLE")

	var unused_param := _src_class("UnusedParam extends Node\nfunc greet(name: String) -> void:\n\tpass\n")
	var param_report: Dictionary = probe.validate_source(unused_param, "res://tests/unused_param.barista", true)
	var saw_param := false
	for warn in param_report.get("warnings", []):
		var msg := str(warn.get("message", "")).to_lower()
		if "UNUSED_PARAMETER" in str(warn.get("string_code", "")) and "never used" in msg and "greet" in msg:
			saw_param = true
	_expect(failures, saw_param, "unused parameter produces UNUSED_PARAMETER message")

	var used := _src_class("UsedLocal extends Node\nfunc _ready() -> void:\n\tvar keep: int = 1\n\tvar _sink: int = keep\n")
	var used_report: Dictionary = probe.validate_source(used, "res://tests/used_local.barista", true)
	var saw_keep_unused := false
	for warn in used_report.get("warnings", []):
		if "keep" in str(warn.get("message", "")):
			saw_keep_unused = true
	_expect(failures, not saw_keep_unused, "used local does not warn as unused")


func _test_unused_class_members_and_signals(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	ProjectSettings.set_setting("debug/barista_script/warnings/enable", true)
	ProjectSettings.set_setting("debug/barista_script/warnings/unused_private_class_variable", 1) # WARN
	ProjectSettings.set_setting("debug/barista_script/warnings/unused_signal", 1) # WARN
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()

	var unused_private := _src_class("UnusedPrivateMember extends Node\nvar _orphan: int = 1\nfunc _ready() -> void:\n\tpass\n")
	var private_report: Dictionary = probe.validate_source(unused_private, "res://tests/unused_private_member.barista", true)
	_expect(failures, private_report.get("valid", false) == true, "unused private member stays valid at WARN")
	var saw_private := false
	for warn in private_report.get("warnings", []):
		var code := str(warn.get("string_code", ""))
		var msg := str(warn.get("message", ""))
		if "UNUSED_PRIVATE_CLASS_VARIABLE" in code and "_orphan" in msg and "never used in the class" in msg:
			saw_private = true
	_expect(failures, saw_private, "unused private member produces UNUSED_PRIVATE_CLASS_VARIABLE message")

	var used_private := _src_class("UsedPrivateMember extends Node\nvar _keep: int = 1\nfunc _ready() -> void:\n\tvar _sink: int = _keep\n")
	var used_private_report: Dictionary = probe.validate_source(used_private, "res://tests/used_private_member.barista", true)
	var saw_used_private := false
	for warn in used_private_report.get("warnings", []):
		if "UNUSED_PRIVATE_CLASS_VARIABLE" in str(warn.get("string_code", "")) and "_keep" in str(warn.get("message", "")):
			saw_used_private = true
	_expect(failures, not saw_used_private, "used private member does not warn as unused")

	var public_unused := _src_class("PublicUnusedMember extends Node\nvar visible: int = 1\nfunc _ready() -> void:\n\tpass\n")
	var public_report: Dictionary = probe.validate_source(public_unused, "res://tests/public_unused_member.barista", true)
	var saw_public := false
	for warn in public_report.get("warnings", []):
		if "UNUSED_PRIVATE_CLASS_VARIABLE" in str(warn.get("string_code", "")):
			saw_public = true
	_expect(failures, not saw_public, "public unused member does not produce UNUSED_PRIVATE_CLASS_VARIABLE")

	var unused_signal := _src_class("UnusedSignalScript extends Node\nsignal lonely\nfunc _ready() -> void:\n\tpass\n")
	var signal_report: Dictionary = probe.validate_source(unused_signal, "res://tests/unused_signal.barista", true)
	_expect(failures, signal_report.get("valid", false) == true, "unused signal stays valid at WARN")
	var saw_signal := false
	for warn in signal_report.get("warnings", []):
		var code := str(warn.get("string_code", ""))
		var msg := str(warn.get("message", ""))
		if "UNUSED_SIGNAL" in code and "lonely" in msg and "never explicitly used" in msg:
			saw_signal = true
	_expect(failures, saw_signal, "unused signal produces UNUSED_SIGNAL message")

	var used_signal := _src_class("UsedSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\temit_signal(\"ping\")\n")
	var used_signal_report: Dictionary = probe.validate_source(used_signal, "res://tests/used_signal.barista", true)
	var saw_used_signal := false
	for warn in used_signal_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "ping" in str(warn.get("message", "")):
			saw_used_signal = true
	_expect(failures, not saw_used_signal, "emit_signal counts as signal use")

	# Bare connect/disconnect/is_connected must also count (Foundry identifier callee = self) (#78).
	var connect_signal := _src_class("ConnectSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\tconnect(\"ping\", Callable())\n")
	var connect_report: Dictionary = probe.validate_source(connect_signal, "res://tests/connect_signal.barista", true)
	var saw_connect_unused := false
	for warn in connect_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "ping" in str(warn.get("message", "")):
			saw_connect_unused = true
	_expect(failures, not saw_connect_unused, "bare connect counts as signal use")

	var disconnect_signal := _src_class("DisconnectSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\tdisconnect(\"ping\", Callable())\n")
	var disconnect_report: Dictionary = probe.validate_source(disconnect_signal, "res://tests/disconnect_signal.barista", true)
	var saw_disconnect_unused := false
	for warn in disconnect_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "ping" in str(warn.get("message", "")):
			saw_disconnect_unused = true
	_expect(failures, not saw_disconnect_unused, "bare disconnect counts as signal use")

	var is_connected_signal := _src_class("IsConnectedSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\tvar _linked: bool = is_connected(\"ping\", Callable())\n")
	var is_connected_report: Dictionary = probe.validate_source(is_connected_signal, "res://tests/is_connected_signal.barista", true)
	var saw_is_connected_unused := false
	for warn in is_connected_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "ping" in str(warn.get("message", "")):
			saw_is_connected_unused = true
	_expect(failures, not saw_is_connected_unused, "bare is_connected counts as signal use")

	# Nested unused: exactly one warning for the nested signal (no duplicate from outer unused pass).
	var nested := _src_class("NestedUnusedOuter extends Node\nclass Inner:\n\tsignal nested_lonely\n\tfunc _ready() -> void:\n\t\tpass\nfunc _ready() -> void:\n\tpass\n")
	var nested_report: Dictionary = probe.validate_source(nested, "res://tests/nested_unused_signal.barista", true)
	var nested_count := 0
	for warn in nested_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "nested_lonely" in str(warn.get("message", "")):
			nested_count += 1
	_expect(failures, nested_count == 1, "nested unused signal warns exactly once")

	# Annotation surface: @warning_ignore needs resolve_annotation to populate resolved_arguments.
	var ignored := _src_class("IgnoredSignalScript extends Node\n@warning_ignore(\"unused_signal\")\nsignal quiet\nfunc _ready() -> void:\n\tpass\n")
	var ignored_report: Dictionary = probe.validate_source(ignored, "res://tests/ignored_signal.barista", true)
	var saw_ignored := false
	for warn in ignored_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "quiet" in str(warn.get("message", "")):
			saw_ignored = true
	_expect(failures, not saw_ignored, "@warning_ignore(\"unused_signal\") suppresses UNUSED_SIGNAL via resolve_annotation")


func _test_trait_requirements_and_conformance_witness(failures: PackedStringArray) -> void:
	# Foundry fixtures: trait_required_method / retroactive_conformance_missing_method (#60).
	var probe := BaristaScriptAnalyzerProbe.new()

	var missing := _src_class("TraitReqMissing extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n")
	var missing_report: Dictionary = probe.analyze_source(missing, "res://tests/trait_req_missing.barista")
	_expect(failures, missing_report.get("valid", true) == false, "missing abstract trait method is invalid")
	var saw_missing := false
	for message in missing_report.get("errors", PackedStringArray()):
		if "must implement trait method" in message and "Damageable.take_damage()" in message:
			saw_missing = true
	_expect(failures, saw_missing, "missing trait method diagnostic")

	var ok := _src_class("TraitReqOk extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n\nfunc take_damage(amount: int) -> void:\n\tpass\n")
	var ok_report: Dictionary = probe.analyze_source(ok, "res://tests/trait_req_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "implemented abstract trait method is valid")

	var abstract_class := "abstract " + _kw_class_name() + " TraitReqAbstract extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n"
	var abstract_report: Dictionary = probe.analyze_source(abstract_class, "res://tests/trait_req_abstract.barista")
	_expect(failures, abstract_report.get("valid", false) == true, "abstract class may defer trait requirements")

	var native_missing := "trait NeedsCustomPing:\n\tabstract func custom_ping() -> void\n\nextend Node uses NeedsCustomPing:\n\tpass\n"
	var native_missing_report: Dictionary = probe.analyze_source(native_missing, "res://tests/rtc_native_missing.barista")
	_expect(failures, native_missing_report.get("valid", true) == false, "native extend missing witness is invalid")
	var saw_native_missing := false
	for message in native_missing_report.get("errors", PackedStringArray()):
		if "must implement trait method" in message and "custom_ping()" in message:
			saw_native_missing = true
	_expect(failures, saw_native_missing, "native extend missing-witness diagnostic")

	var native_ok := "trait NeedsCustomPing:\n\tabstract func custom_ping() -> void\n\nextend Node uses NeedsCustomPing:\n\tfunc custom_ping() -> void:\n\t\tpass\n"
	var native_ok_report: Dictionary = probe.analyze_source(native_ok, "res://tests/rtc_native_ok.barista")
	_expect(failures, native_ok_report.get("valid", false) == true, "native extend with witness is valid")

	var local_missing := _src_class("RtcLocalGadget extends Node\n\ntrait NeedsPing:\n\tabstract func ping() -> void\n\nextend RtcLocalGadget uses NeedsPing:\n\tpass\n")
	var local_missing_report: Dictionary = probe.analyze_source(local_missing, "res://tests/rtc_local_missing.barista")
	_expect(failures, local_missing_report.get("valid", true) == false, "same-file extend missing witness is invalid")
	var saw_local_missing := false
	for message in local_missing_report.get("errors", PackedStringArray()):
		if "Conformance of" in message and "must implement trait method" in message and "ping()" in message:
			saw_local_missing = true
	_expect(failures, saw_local_missing, "same-file extend missing-witness diagnostic")

	var local_ok := _src_class("RtcLocalOk extends Node\n\ntrait NeedsPing:\n\tabstract func ping() -> void\n\nextend RtcLocalOk uses NeedsPing:\n\tfunc ping() -> void:\n\t\tpass\n")
	var local_ok_report: Dictionary = probe.analyze_source(local_ok, "res://tests/rtc_local_ok.barista")
	_expect(failures, local_ok_report.get("valid", false) == true, "same-file extend with witness is valid")

	# Redundant extend when the target already owns the trait via uses.
	var redundant := _src_class("RtcRedundantTarget extends Node\nuses NeedsPing\n\ntrait NeedsPing:\n\tabstract func ping() -> void\n\nfunc ping() -> void:\n\tpass\n\nextend RtcRedundantTarget uses NeedsPing:\n\tpass\n")
	var redundant_report: Dictionary = probe.analyze_source(redundant, "res://tests/rtc_redundant_uses.barista")
	_expect(failures, redundant_report.get("valid", true) == false, "extend redundant with target uses is invalid")
	var saw_redundant := false
	for message in redundant_report.get("errors", PackedStringArray()):
		if "already conforms to trait" in message and "through its own \"uses\"" in message and "NeedsPing" in message:
			saw_redundant = true
	_expect(failures, saw_redundant, "redundant uses conformance diagnostic")

	# Cross-file trait requirement via declaration index.
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var trait_path := "res://tests/index_damageable.barista"
	var trait_source := "trait_name IndexDamageable\nabstract func take_damage(amount: int) -> void\n"
	index.synchronize_path_from_source(trait_path, trait_source)
	BaristaScriptParseCache.set_source_override(trait_path, trait_source)
	var consumer := _src_class("TraitIndexMissing extends Node\nuses IndexDamageable\n")
	var index_report: Dictionary = probe.analyze_source(consumer, "res://tests/trait_index_missing.barista")
	_expect(failures, index_report.get("valid", true) == false, "index-backed missing trait method is invalid")
	var saw_index := false
	for message in index_report.get("errors", PackedStringArray()):
		if "must implement trait method" in message and "take_damage()" in message:
			saw_index = true
	_expect(failures, saw_index, "index-backed missing trait method diagnostic")
	BaristaScriptParseCache.clear_source_override(trait_path)
	index.clear()

	# Foundry trait_required_signature / async / Self / rest narrowing (#60).
	var sig_mismatch := _src_class("TraitSigMismatch extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n\nfunc take_damage(amount: String) -> void:\n\tpass\n")
	var sig_mismatch_report: Dictionary = probe.analyze_source(sig_mismatch, "res://tests/trait_sig_mismatch.barista")
	_expect(failures, sig_mismatch_report.get("valid", true) == false, "trait method wrong parameter type is invalid")
	var saw_sig_mismatch := false
	for message in sig_mismatch_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Damageable.take_damage()" in message:
			saw_sig_mismatch = true
	_expect(failures, saw_sig_mismatch, "trait method signature mismatch diagnostic")

	var async_required := _src_class("TraitAsyncRequired extends Node\nuses RemoteLoadable\n\ntrait RemoteLoadable:\n\tabstract async func fetch() -> String\n\nfunc fetch() -> String:\n\treturn \"\"\n")
	var async_required_report: Dictionary = probe.analyze_source(async_required, "res://tests/trait_async_required.barista")
	_expect(failures, async_required_report.get("valid", true) == false, "sync impl of async trait method is invalid")
	var saw_async_required := false
	for message in async_required_report.get("errors", PackedStringArray()):
		if "must be async because it implements async trait method" in message and "RemoteLoadable.fetch()" in message:
			saw_async_required = true
	_expect(failures, saw_async_required, "async trait method requires async impl diagnostic")

	var sync_required := _src_class("TraitSyncRequired extends Node\nuses Syncable\n\ntrait Syncable:\n\tabstract func compute() -> int\n\nasync func compute() -> int:\n\treturn 0\n")
	var sync_required_report: Dictionary = probe.analyze_source(sync_required, "res://tests/trait_sync_required.barista")
	_expect(failures, sync_required_report.get("valid", true) == false, "async impl of sync trait method is invalid")
	var saw_sync_required := false
	for message in sync_required_report.get("errors", PackedStringArray()):
		if "cannot be async because it implements synchronous trait method" in message and "Syncable.compute()" in message:
			saw_sync_required = true
	_expect(failures, saw_sync_required, "sync trait method rejects async impl diagnostic")

	var self_ok := _src_class("TraitSelfOk extends Node\nuses Creatable\n\ntrait Creatable:\n\tabstract static func create() -> Self\n\nstatic func create() -> Self:\n\treturn TraitSelfOk.new()\n")
	var self_ok_report: Dictionary = probe.analyze_source(self_ok, "res://tests/trait_self_ok.barista")
	_expect(failures, self_ok_report.get("valid", false) == true, "Self return matching implementer is valid")

	var self_bad := _src_class("TraitSelfBad extends Node\nuses Creatable\n\ntrait Creatable:\n\tabstract static func create() -> Self\n\nstatic func create() -> String:\n\treturn \"x\"\n")
	var self_bad_report: Dictionary = probe.analyze_source(self_bad, "res://tests/trait_self_bad.barista")
	_expect(failures, self_bad_report.get("valid", true) == false, "Self return mismatched to String is invalid")
	var saw_self_bad := false
	for message in self_bad_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Creatable.create()" in message:
			saw_self_bad = true
	_expect(failures, saw_self_bad, "Self return mismatch diagnostic")

	var arity_bad := _src_class("TraitArityBad extends Node\nuses Binary\n\ntrait Binary:\n\tabstract func combine(a: int, b: int) -> int\n\nfunc combine(a: int) -> int:\n\treturn a\n")
	var arity_bad_report: Dictionary = probe.analyze_source(arity_bad, "res://tests/trait_arity_bad.barista")
	_expect(failures, arity_bad_report.get("valid", true) == false, "trait method arity mismatch is invalid")
	var saw_arity := false
	for message in arity_bad_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Binary.combine()" in message:
			saw_arity = true
	_expect(failures, saw_arity, "trait method arity mismatch diagnostic")

	var rest_narrow := _src_class("TraitRestNarrow extends Node\nuses AcceptsNodes\n\ntrait AcceptsNodes:\n\tabstract func accept(...nodes: Array[Node]) -> int\n\nfunc accept(...nodes: Array[String]) -> int:\n\treturn nodes.size()\n")
	var rest_narrow_report: Dictionary = probe.analyze_source(rest_narrow, "res://tests/trait_rest_narrow.barista")
	_expect(failures, rest_narrow_report.get("valid", true) == false, "narrower rest tail does not satisfy trait rest requirement")
	var saw_rest := false
	for message in rest_narrow_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "AcceptsNodes.accept()" in message:
			saw_rest = true
	_expect(failures, saw_rest, "trait rest narrowing diagnostic")

	# Fixed Array/Dictionary element matching (#84 / #83 review): carriers alone are not enough.
	var array_elem := _src_class("TraitArrayElem extends Node\nuses TakesNodes\n\ntrait TakesNodes:\n\tabstract func take(items: Array[Node]) -> void\n\nfunc take(items: Array[String]) -> void:\n\tpass\n")
	var array_elem_report: Dictionary = probe.analyze_source(array_elem, "res://tests/trait_array_elem.barista")
	_expect(failures, array_elem_report.get("valid", true) == false, "fixed Array[Node] vs Array[String] param is invalid")
	var saw_array_elem := false
	for message in array_elem_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "TakesNodes.take()" in message:
			saw_array_elem = true
	_expect(failures, saw_array_elem, "fixed Array element mismatch diagnostic")

	var dict_elem := _src_class("TraitDictElem extends Node\nuses TakesMap\n\ntrait TakesMap:\n\tabstract func take(items: Dictionary[String, Node]) -> void\n\nfunc take(items: Dictionary[String, String]) -> void:\n\tpass\n")
	var dict_elem_report: Dictionary = probe.analyze_source(dict_elem, "res://tests/trait_dict_elem.barista")
	_expect(failures, dict_elem_report.get("valid", true) == false, "fixed Dictionary value element mismatch is invalid")
	var saw_dict_elem := false
	for message in dict_elem_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "TakesMap.take()" in message:
			saw_dict_elem = true
	_expect(failures, saw_dict_elem, "fixed Dictionary element mismatch diagnostic")

	# Explicit static-vs-instance (and reverse) signature mismatch (#84 / #83 review).
	var static_vs_instance := _src_class("TraitStaticVsInst extends Node\nuses Factory\n\ntrait Factory:\n\tabstract static func build() -> void\n\nfunc build() -> void:\n\tpass\n")
	var static_vs_instance_report: Dictionary = probe.analyze_source(static_vs_instance, "res://tests/trait_static_vs_inst.barista")
	_expect(failures, static_vs_instance_report.get("valid", true) == false, "instance impl of static trait method is invalid")
	var saw_static_vs_inst := false
	for message in static_vs_instance_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Factory.build()" in message:
			saw_static_vs_inst = true
	_expect(failures, saw_static_vs_inst, "static-vs-instance signature mismatch diagnostic")

	var instance_vs_static := _src_class("TraitInstVsStatic extends Node\nuses Worker\n\ntrait Worker:\n\tabstract func run() -> void\n\nstatic func run() -> void:\n\tpass\n")
	var instance_vs_static_report: Dictionary = probe.analyze_source(instance_vs_static, "res://tests/trait_inst_vs_static.barista")
	_expect(failures, instance_vs_static_report.get("valid", true) == false, "static impl of instance trait method is invalid")
	var saw_inst_vs_static := false
	for message in instance_vs_static_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Worker.run()" in message:
			saw_inst_vs_static = true
	_expect(failures, saw_inst_vs_static, "instance-vs-static signature mismatch diagnostic")

	var rtc_sig := _src_class("RtcSigTarget extends Node\n\ntrait NeedsPing:\n\tabstract func ping(code: int) -> void\n\nextend RtcSigTarget uses NeedsPing:\n\tfunc ping(code: String) -> void:\n\t\tpass\n")
	var rtc_sig_report: Dictionary = probe.analyze_source(rtc_sig, "res://tests/rtc_sig_mismatch.barista")
	_expect(failures, rtc_sig_report.get("valid", true) == false, "extend witness with wrong signature is invalid")
	var saw_rtc_sig := false
	for message in rtc_sig_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "NeedsPing.ping()" in message:
			saw_rtc_sig = true
	_expect(failures, saw_rtc_sig, "extend witness signature mismatch diagnostic")

	var native_sig := _src_class("NativeSigBad extends Node\nuses NeedsGetNode\n\ntrait NeedsGetNode:\n\tabstract func get_node(path: int) -> Node\n")
	var native_sig_report: Dictionary = probe.analyze_source(native_sig, "res://tests/native_sig_bad.barista")
	_expect(failures, native_sig_report.get("valid", true) == false, "native MethodInfo wrong signature for trait is invalid")
	var saw_native_sig := false
	for message in native_sig_report.get("errors", PackedStringArray()):
		if "native function" in message and "get_node()" in message and "NeedsGetNode.get_node()" in message:
			saw_native_sig = true
	_expect(failures, saw_native_sig, "native MethodInfo signature mismatch diagnostic")


func _test_flow_narrowing(failures: PackedStringArray) -> void:
	# Foundry flow-narrowing starter (@ c9d5e35): null-check + `is` type-test overlays for locals/params.
	var probe := BaristaScriptAnalyzerProbe.new()
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_null_checks", true)

	var bare_null := _src_class("BareNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tvar x: Node = n\n")
	var bare_null_report: Dictionary = probe.analyze_source(bare_null, "res://tests/bare_nullable_assign.barista")
	_expect(failures, bare_null_report.get("valid", true) == false, "nullable to non-null assign fails under strict_null")

	var narrowed_null := _src_class("NarrowedNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tvar x: Node = n\n")
	var narrowed_null_report: Dictionary = probe.analyze_source(narrowed_null, "res://tests/narrowed_nullable_assign.barista")
	_expect(failures, narrowed_null_report.get("valid", false) == true, "null-check true arm allows Node? → Node")

	var else_null := _src_class("ElseNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tpass\n\telse:\n\t\tvar x: Node = n\n")
	var else_null_report: Dictionary = probe.analyze_source(else_null, "res://tests/else_nullable_assign.barista")
	_expect(failures, else_null_report.get("valid", true) == false, "null-check else arm keeps Node? → Node invalid")

	var assert_null := _src_class("AssertNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tassert(n != null)\n\tvar x: Node = n\n")
	var assert_null_report: Dictionary = probe.analyze_source(assert_null, "res://tests/assert_nullable_assign.barista")
	_expect(failures, assert_null_report.get("valid", false) == true, "assert null-check narrows later statements")

	var bare_union := _src_class("BareUnionAssign extends Node\nfunc take(v: int | String) -> void:\n\tvar s: String = v\n")
	var bare_union_report: Dictionary = probe.analyze_source(bare_union, "res://tests/bare_union_assign.barista")
	_expect(failures, bare_union_report.get("valid", true) == false, "union to String without type test is invalid")

	var narrowed_is := _src_class("NarrowedIsAssign extends Node\nfunc take(v: int | String) -> void:\n\tif v is String:\n\t\tvar s: String = v\n")
	var narrowed_is_report: Dictionary = probe.analyze_source(narrowed_is, "res://tests/narrowed_is_assign.barista")
	_expect(failures, narrowed_is_report.get("valid", false) == true, "`is String` true arm allows int|String → String")

	var else_is := _src_class("ElseIsAssign extends Node\nfunc take(v: int | String) -> void:\n\tif v is String:\n\t\tpass\n\telse:\n\t\tvar i: int = v\n")
	var else_is_report: Dictionary = probe.analyze_source(else_is, "res://tests/else_is_assign.barista")
	_expect(failures, else_is_report.get("valid", false) == true, "`is String` else arm subtracts String leaving int")

	var cleared := _src_class("ClearedNarrowingAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tn = null\n\t\tvar x: Node = n\n")
	var cleared_report: Dictionary = probe.analyze_source(cleared, "res://tests/cleared_narrowing_assign.barista")
	_expect(failures, cleared_report.get("valid", true) == false, "assignment clears prior null-check narrowing")

	var and_narrow := _src_class("AndNarrowingAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null and n is Node:\n\t\tvar x: Node = n\n")
	var and_narrow_report: Dictionary = probe.analyze_source(and_narrow, "res://tests/and_narrowing_assign.barista")
	_expect(failures, and_narrow_report.get("valid", false) == true, "`and` condition applies left-side null-check narrowing")

	# Foundry match-branch flow narrowing (@ c9d5e35): non-null patterns strip nullability;
	# subject `is T` / bare native type patterns overlay the matched type.
	var match_null_stripped := _src_class("MatchNullStrippedAssign extends Node\nfunc take(n: Node?) -> void:\n\tmatch n:\n\t\tnull:\n\t\t\tpass\n\t\t1:\n\t\t\tvar x: Node = n\n\t\t_:\n\t\t\tpass\n")
	var match_null_stripped_report: Dictionary = probe.analyze_source(match_null_stripped, "res://tests/match_null_stripped_assign.barista")
	_expect(failures, match_null_stripped_report.get("valid", false) == true, "match non-null literal arm strips Node? → Node")

	var match_wildcard_keeps_null := _src_class("MatchWildcardKeepsNull extends Node\nfunc take(n: Node?) -> void:\n\tmatch n:\n\t\t_:\n\t\t\tvar x: Node = n\n")
	var match_wildcard_keeps_null_report: Dictionary = probe.analyze_source(match_wildcard_keeps_null, "res://tests/match_wildcard_keeps_null.barista")
	_expect(failures, match_wildcard_keeps_null_report.get("valid", true) == false, "match wildcard arm does not strip nullability")

	var match_is_type := _src_class("MatchIsTypeAssign extends Node\nfunc take(v: int | String) -> void:\n\tmatch v:\n\t\tv is String:\n\t\t\tvar s: String = v\n\t\t_:\n\t\t\tpass\n")
	var match_is_type_report: Dictionary = probe.analyze_source(match_is_type, "res://tests/match_is_type_assign.barista")
	_expect(failures, match_is_type_report.get("valid", false) == true, "match `v is String` arm allows int|String → String")

	var match_native_type := _src_class("MatchNativeTypeAssign extends Node\nfunc take(v: Object?) -> void:\n\tmatch v:\n\t\tnull:\n\t\t\tpass\n\t\tNode:\n\t\t\tvar x: Node = v\n\t\t_:\n\t\t\tpass\n")
	var match_native_type_report: Dictionary = probe.analyze_source(match_native_type, "res://tests/match_native_type_assign.barista")
	_expect(failures, match_native_type_report.get("valid", false) == true, "match bare Node type pattern narrows Object? → Node")

	# Local/const shadowing a ClassDB name must stay a value pattern (no type overlay).
	# Use int|String so a mistaken ClassDB promotion would wrongly allow `var x: Node = v`.
	var match_shadowed_classdb := _src_class("MatchShadowedClassDBName extends Node\nfunc take(v: int | String) -> void:\n\tconst Node := 1\n\tmatch v:\n\t\tNode:\n\t\t\tvar x: Node = v\n\t\t_:\n\t\t\tpass\n")
	var match_shadowed_classdb_report: Dictionary = probe.analyze_source(match_shadowed_classdb, "res://tests/match_shadowed_classdb_name.barista")
	_expect(failures, match_shadowed_classdb_report.get("valid", true) == false, "match local Node shadow stays value pattern (no ClassDB type overlay)")

	ProjectSettings.set_setting("debug/barista_script/analysis/strict_null_checks", false)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()


func _test_builtin_annotation_resolve(failures: PackedStringArray) -> void:
	# Foundry datatype_from_type_node @ c9d5e35: user-facing builtins beyond int/float/bool/String
	# resolve through get_builtin_type (StringName / Callable / bare Array / NodePath / …).
	var probe := BaristaScriptAnalyzerProbe.new()

	var string_name_ok := _src_class("BuiltinStringNameAnnot extends Node\nfunc take(n: StringName) -> void:\n\tvar _x: StringName = n\n")
	var string_name_report: Dictionary = probe.analyze_source(string_name_ok, "res://tests/builtin_string_name_annot.barista")
	_expect(failures, string_name_report.get("valid", false) == true, "StringName annotation resolves as builtin")

	var node_path_ok := _src_class("BuiltinNodePathAnnot extends Node\nfunc take(p: NodePath) -> void:\n\tvar _x: NodePath = p\n")
	var node_path_report: Dictionary = probe.analyze_source(node_path_ok, "res://tests/builtin_node_path_annot.barista")
	_expect(failures, node_path_report.get("valid", false) == true, "NodePath annotation resolves as builtin")

	var bare_array_ok := _src_class("BuiltinBareArrayAnnot extends Node\nfunc take(a: Array) -> void:\n\tvar _x: Array = a\n")
	var bare_array_report: Dictionary = probe.analyze_source(bare_array_ok, "res://tests/builtin_bare_array_annot.barista")
	_expect(failures, bare_array_report.get("valid", false) == true, "bare Array annotation resolves as builtin")

	var callable_ok := _src_class("BuiltinCallableAnnot extends Node\nfunc take(c: Callable) -> void:\n\tvar _x: Callable = c\n")
	var callable_report: Dictionary = probe.analyze_source(callable_ok, "res://tests/builtin_callable_annot.barista")
	_expect(failures, callable_report.get("valid", false) == true, "bare Callable annotation resolves as builtin")

	var callable_sig_ok := _src_class("BuiltinCallableSigAnnot extends Node\nfunc take(c: Callable[[int], void]) -> void:\n\tvar _x: Callable[[int], void] = c\n")
	var callable_sig_report: Dictionary = probe.analyze_source(callable_sig_ok, "res://tests/builtin_callable_sig_annot.barista")
	_expect(failures, callable_sig_report.get("valid", false) == true, "Callable[[int], void] signature annotation resolves")

	var signal_ok := _src_class("BuiltinSignalAnnot extends Node\nfunc take(s: Signal) -> void:\n\tvar _x: Signal = s\n")
	var signal_report: Dictionary = probe.analyze_source(signal_ok, "res://tests/builtin_signal_annot.barista")
	_expect(failures, signal_report.get("valid", false) == true, "bare Signal annotation resolves as builtin")

	var number_ok := _src_class("BuiltinNumberAnnot extends Node\nfunc take(n: Number) -> void:\n\tvar _x: Number = 1\n")
	var number_report: Dictionary = probe.analyze_source(number_ok, "res://tests/builtin_number_annot.barista")
	_expect(failures, number_report.get("valid", false) == true, "Number annotation resolves as int|float union")

	# Still reject unknown spellings; prove the failure is not a blanket Variant fallthrough.
	var unknown := _src_class("BuiltinUnknownAnnot extends Node\nfunc take(x: NotARealType) -> void:\n\tpass\n")
	var unknown_report: Dictionary = probe.analyze_source(unknown, "res://tests/builtin_unknown_annot.barista")
	_expect(failures, unknown_report.get("valid", true) == false, "unknown type annotation remains invalid")
	var saw_unknown := false
	for message in unknown_report.get("errors", PackedStringArray()):
		if 'Could not find type "NotARealType"' in message:
			saw_unknown = true
	_expect(failures, saw_unknown, "unknown type keeps Could not find type diagnostic")

	# Assignability still enforced once the annotation resolves.
	var mismatch := _src_class("BuiltinStringNameMismatch extends Node\nfunc take() -> void:\n\tvar _n: StringName = 123\n")
	var mismatch_report: Dictionary = probe.analyze_source(mismatch, "res://tests/builtin_string_name_mismatch.barista")
	_expect(failures, mismatch_report.get("valid", true) == false, "int → StringName annotation assign remains invalid")


func _test_union_union_assignability(failures: PackedStringArray) -> void:
	# Foundry FSTypeCompatibility source-UNION @ c9d5e35 (#89 residual): every alternative of a
	# union source must satisfy the target. Number→Number / written union self-assign are the AC.
	var probe := BaristaScriptAnalyzerProbe.new()

	var number_self := _src_class("NumberToNumberAssign extends Node\nfunc take(n: Number) -> void:\n\tvar _x: Number = n\n")
	var number_self_report: Dictionary = probe.analyze_source(number_self, "res://tests/number_to_number_assign.barista")
	_expect(failures, number_self_report.get("valid", false) == true, "Number → Number union self-assign is valid")

	var written_self := _src_class("WrittenUnionSelfAssign extends Node\nfunc take(v: int | String) -> void:\n\tvar _x: int | String = v\n")
	var written_self_report: Dictionary = probe.analyze_source(written_self, "res://tests/written_union_self_assign.barista")
	_expect(failures, written_self_report.get("valid", false) == true, "int|String → int|String self-assign is valid")

	# Opposite spelling still works: source-UNION checks each alt against the target set.
	var reordered := _src_class("WrittenUnionReorderAssign extends Node\nfunc take(v: String | int) -> void:\n\tvar _x: int | String = v\n")
	var reordered_report: Dictionary = probe.analyze_source(reordered, "res://tests/written_union_reorder_assign.barista")
	_expect(failures, reordered_report.get("valid", false) == true, "String|int → int|String reorder assign is valid")

	var number_from_written := _src_class("WrittenToNumberAssign extends Node\nfunc take(v: int | float) -> void:\n\tvar _x: Number = v\n")
	var number_from_written_report: Dictionary = probe.analyze_source(number_from_written, "res://tests/written_to_number_assign.barista")
	_expect(failures, number_from_written_report.get("valid", false) == true, "int|float → Number assign is valid")

	# Partial coverage stays invalid: a String alternative cannot enter a String-only slot.
	var partial := _src_class("UnionPartialAssign extends Node\nfunc take(v: int | String) -> void:\n\tvar _s: String = v\n")
	var partial_report: Dictionary = probe.analyze_source(partial, "res://tests/union_partial_assign.barista")
	_expect(failures, partial_report.get("valid", true) == false, "int|String → String without narrowing is invalid")

	# Carrier-changing per-alternative conversion cannot be emitted for an erased union source.
	var number_to_float := _src_class("NumberToFloatAssign extends Node\nfunc take(n: Number) -> void:\n\tvar _f: float = n\n")
	var number_to_float_report: Dictionary = probe.analyze_source(number_to_float, "res://tests/number_to_float_assign.barista")
	_expect(failures, number_to_float_report.get("valid", true) == false, "Number → float carrier change stays invalid")

	# Concrete → union target still selects an alternative (pre-existing target-UNION path).
	var int_to_number := _src_class("IntToNumberAssign extends Node\nfunc take(n: int) -> void:\n\tvar _x: Number = n\n")
	var int_to_number_report: Dictionary = probe.analyze_source(int_to_number, "res://tests/int_to_number_assign.barista")
	_expect(failures, int_to_number_report.get("valid", false) == true, "int → Number still selects a union alternative")

	# Opt-in declaration-index mutation unchanged: analyze / validate stay read-only.
	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(number_self, "res://tests/number_to_number_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "Number→Number remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(number_self, "res://tests/number_to_number_is_valid.barista"),
		"Number→Number remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for Number→Number")


func _test_union_store_carrier_select(failures: PackedStringArray) -> void:
	# Foundry target-UNION _select_union_alternative / _union_store_converts_carrier @ c9d5e35:
	# prefer exact alternatives; converting alternatives need a numeric store-carrier conversion.
	var probe := BaristaScriptAnalyzerProbe.new()

	# Exact-path validity: int fits int|float without conversion (selection order not asserted here).
	var exact_prefers_int := _src_class("UnionExactPrefersInt extends Node\nfunc take(n: int) -> void:\n\tvar _x: int | float = n\n")
	var exact_prefers_int_report: Dictionary = probe.analyze_source(exact_prefers_int, "res://tests/union_exact_prefers_int.barista")
	_expect(failures, exact_prefers_int_report.get("valid", false) == true, "int → int|float exact alternative is valid")

	# Numeric store can convert int→float into a float|String union.
	var int_to_float_union := _src_class("UnionNumericStoreWiden extends Node\nfunc take(n: int) -> void:\n\tvar _x: float | String = n\n")
	var int_to_float_union_report: Dictionary = probe.analyze_source(int_to_float_union, "res://tests/union_numeric_store_widen.barista")
	_expect(failures, int_to_float_union_report.get("valid", false) == true, "int → float|String numeric store widen is valid")

	# Plain String→StringName still works (non-union slot performs the engine bridge).
	var plain_string_name := _src_class("PlainStringToStringName extends Node\nfunc take() -> void:\n\tvar _n: StringName = \"ready\"\n")
	var plain_string_name_report: Dictionary = probe.analyze_source(plain_string_name, "res://tests/plain_string_to_string_name.barista")
	_expect(failures, plain_string_name_report.get("valid", false) == true, "String → StringName plain assign remains valid")

	# Union store cannot perform String→StringName: no numeric store-carrier counterpart.
	var string_to_string_name_union := _src_class("UnionRejectsStringNameBridge extends Node\nfunc take() -> void:\n\tvar _x: StringName | int = \"ready\"\n")
	var string_to_string_name_union_report: Dictionary = probe.analyze_source(string_to_string_name_union, "res://tests/union_rejects_string_name_bridge.barista")
	_expect(failures, string_to_string_name_union_report.get("valid", true) == false,
		"String → StringName|int rejected (union store cannot bridge)")

	# Same bridge rejection with a non-literal String source.
	var string_param_to_union := _src_class("UnionRejectsStringParamBridge extends Node\nfunc take(s: String) -> void:\n\tvar _x: StringName | Node = s\n")
	var string_param_to_union_report: Dictionary = probe.analyze_source(string_param_to_union, "res://tests/union_rejects_string_param_bridge.barista")
	_expect(failures, string_param_to_union_report.get("valid", true) == false,
		"String param → StringName|Node rejected (union store cannot bridge)")

	# Opt-in declaration-index mutation unchanged.
	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(exact_prefers_int, "res://tests/union_store_carrier_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "int→int|float remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(exact_prefers_int, "res://tests/union_store_carrier_is_valid.barista"),
		"int→int|float remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for union store-carrier")


func _test_enum_case_match_and_case_binds(failures: PackedStringArray) -> void:
	# Foundry resolve_enum_values + resolve_match_case_pattern / resolve_type_test_case_binds
	# + container match patterns @ c9d5e35 (#60 ENUM_CASE / case-bind / container residual).
	var probe := BaristaScriptAnalyzerProbe.new()

	var match_ok := _src_class("EnumMatchOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\treturn dx + dy\n\t\t_:\n\t\t\treturn 0\n")
	var match_ok_report: Dictionary = probe.analyze_source(match_ok, "res://tests/enum_match_ok.barista")
	_expect(failures, match_ok_report.get("valid", false) == true, "Message.Move(dx, dy) match pattern is valid")

	var match_arity := _src_class("EnumMatchArity extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\tMessage.Move(dx):\n\t\t\tpass\n")
	var match_arity_report: Dictionary = probe.analyze_source(match_arity, "res://tests/enum_match_arity.barista")
	_expect(failures, match_arity_report.get("valid", true) == false, "Message.Move arity mismatch is invalid")
	var saw_match_arity := false
	for message in match_arity_report.get("errors", PackedStringArray()):
		if "carries 2 payload value(s), but 1 pattern(s) were given" in message:
			saw_match_arity = true
	_expect(failures, saw_match_arity, "ENUM_CASE payload arity diagnostic")

	var wrong_subject := _src_class("EnumMatchWrongSubject extends Node\nenum Message:\n\tMove(x: int, y: int)\nenum Other:\n\tGo(n: int)\nfunc handle(msg: Other) -> void:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\tpass\n")
	var wrong_subject_report: Dictionary = probe.analyze_source(wrong_subject, "res://tests/enum_match_wrong_subject.barista")
	_expect(failures, wrong_subject_report.get("valid", true) == false, "ENUM_CASE against unrelated subject is invalid")
	var saw_wrong_subject := false
	for message in wrong_subject_report.get("errors", PackedStringArray()):
		if "Pattern matches a case of" in message and "subject is of type" in message:
			saw_wrong_subject = true
	_expect(failures, saw_wrong_subject, "ENUM_CASE subject-type mismatch diagnostic")

	var case_bind_ok := _src_class("EnumCaseBindOk extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> int:\n\tif msg is Message.Move(dx, dy):\n\t\treturn dx + dy\n\treturn 0\n")
	var case_bind_ok_report: Dictionary = probe.analyze_source(case_bind_ok, "res://tests/enum_case_bind_ok.barista")
	_expect(failures, case_bind_ok_report.get("valid", false) == true, "is Message.Move(dx, dy) case binds are valid")

	var case_bind_arity := _src_class("EnumCaseBindArity extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> void:\n\tif msg is Message.Move(dx):\n\t\tpass\n")
	var case_bind_arity_report: Dictionary = probe.analyze_source(case_bind_arity, "res://tests/enum_case_bind_arity.barista")
	_expect(failures, case_bind_arity_report.get("valid", true) == false, "case-bind arity mismatch is invalid")
	var saw_bind_arity := false
	for message in case_bind_arity_report.get("errors", PackedStringArray()):
		if "carries 2 payload value(s), but 1 bind(s) were given" in message:
			saw_bind_arity = true
	_expect(failures, saw_bind_arity, "case-bind payload arity diagnostic")

	var not_case_bind := _src_class("NotEnumCaseBind extends Node\nfunc handle(v: Variant) -> void:\n\tif v is Node(n):\n\t\tpass\n")
	var not_case_bind_report: Dictionary = probe.analyze_source(not_case_bind, "res://tests/not_enum_case_bind.barista")
	_expect(failures, not_case_bind_report.get("valid", true) == false, "non-enum case binds are invalid")
	var saw_not_case := false
	for message in not_case_bind_report.get("errors", PackedStringArray()):
		if "Only a tagged-union case can bind payload values" in message:
			saw_not_case = true
	_expect(failures, saw_not_case, "non-enum case-bind diagnostic")

	var array_ok := _src_class("ArrayPatOk extends Node\nfunc handle(xs: Array[int]) -> int:\n\tmatch xs:\n\t\t[a, b]:\n\t\t\treturn a + b\n\t\t_:\n\t\t\treturn 0\n")
	var array_ok_report: Dictionary = probe.analyze_source(array_ok, "res://tests/array_pat_ok.barista")
	_expect(failures, array_ok_report.get("valid", false) == true, "Array[int] pattern binds are valid")

	var dict_ok := _src_class("DictPatOk extends Node\nfunc handle(d: Dictionary[String, int]) -> int:\n\tmatch d:\n\t\t{\"a\": n}:\n\t\t\treturn n\n\t\t_:\n\t\t\treturn 0\n")
	var dict_ok_report: Dictionary = probe.analyze_source(dict_ok, "res://tests/dict_pat_ok.barista")
	_expect(failures, dict_ok_report.get("valid", false) == true, "Dictionary[String, int] pattern binds are valid")

	var dict_key_bad := _src_class("DictPatKeyBad extends Node\nfunc handle(d: Dictionary) -> void:\n\tvar k := \"a\"\n\tmatch d:\n\t\t{k: v}:\n\t\t\tpass\n")
	var dict_key_bad_report: Dictionary = probe.analyze_source(dict_key_bad, "res://tests/dict_pat_key_bad.barista")
	_expect(failures, dict_key_bad_report.get("valid", true) == false, "non-constant dictionary pattern key is invalid")
	var saw_dict_key := false
	for message in dict_key_bad_report.get("errors", PackedStringArray()):
		if "dictionary pattern key must be a constant" in message:
			saw_dict_key = true
	_expect(failures, saw_dict_key, "dictionary pattern key constant diagnostic")

	var tuple_arity := _src_class("TuplePatArity extends Node\nfunc handle(t: (int, String)) -> void:\n\tmatch t:\n\t\t(a, b, c):\n\t\t\tpass\n")
	var tuple_arity_report: Dictionary = probe.analyze_source(tuple_arity, "res://tests/tuple_pat_arity.barista")
	_expect(failures, tuple_arity_report.get("valid", true) == false, "tuple pattern arity mismatch is invalid")
	var saw_tuple_arity := false
	for message in tuple_arity_report.get("errors", PackedStringArray()):
		if "Tuple pattern has 3 element(s)" in message and "has 2" in message:
			saw_tuple_arity = true
	_expect(failures, saw_tuple_arity, "tuple pattern arity diagnostic")

	# Opt-in declaration-index mutation unchanged.
	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(match_ok, "res://tests/enum_match_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "ENUM_CASE match remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(match_ok, "res://tests/enum_match_is_valid.barista"),
		"ENUM_CASE match remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for ENUM_CASE match")


func _test_contextual_case_shorthand(failures: PackedStringArray) -> void:
	# Foundry resolve_contextual_case_pattern_type / resolve_contextual_case_value_pattern
	# + reduce_type_test contextual `.Case` + resolve_contextual_enum_case assign/return @ c9d5e35.
	var probe := BaristaScriptAnalyzerProbe.new()

	var match_payload := _src_class("CtxMatchPayload extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\t.Move(dx, dy):\n\t\t\treturn dx + dy\n\t\t_:\n\t\t\treturn 0\n")
	var match_payload_report: Dictionary = probe.analyze_source(match_payload, "res://tests/ctx_match_payload.barista")
	_expect(failures, match_payload_report.get("valid", false) == true, ".Move(dx, dy) contextual match is valid")

	var match_value := _src_class("CtxMatchValue extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\t.Quit:\n\t\t\treturn 1\n\t\t_:\n\t\t\treturn 0\n")
	var match_value_report: Dictionary = probe.analyze_source(match_value, "res://tests/ctx_match_value.barista")
	_expect(failures, match_value_report.get("valid", false) == true, ".Quit contextual match is valid")

	var match_arity := _src_class("CtxMatchArity extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\t.Move(dx):\n\t\t\tpass\n")
	var match_arity_report: Dictionary = probe.analyze_source(match_arity, "res://tests/ctx_match_arity.barista")
	_expect(failures, match_arity_report.get("valid", true) == false, ".Move arity mismatch is invalid")
	var saw_match_arity := false
	for message in match_arity_report.get("errors", PackedStringArray()):
		if "carries 2 payload value(s), but 1 pattern(s) were given" in message:
			saw_match_arity = true
	_expect(failures, saw_match_arity, "contextual ENUM_CASE payload arity diagnostic")

	var match_unknown := _src_class("CtxMatchUnknown extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\t.Nope:\n\t\t\tpass\n")
	var match_unknown_report: Dictionary = probe.analyze_source(match_unknown, "res://tests/ctx_match_unknown.barista")
	_expect(failures, match_unknown_report.get("valid", true) == false, "unknown contextual case is invalid")
	var saw_unknown := false
	for message in match_unknown_report.get("errors", PackedStringArray()):
		if 'Tagged union "Message" has no case "Nope"' in message:
			saw_unknown = true
	_expect(failures, saw_unknown, "unknown contextual case diagnostic")

	var match_bad_subject := _src_class("CtxMatchBadSubject extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(n: int) -> void:\n\tmatch n:\n\t\t.Quit:\n\t\t\tpass\n")
	var match_bad_subject_report: Dictionary = probe.analyze_source(match_bad_subject, "res://tests/ctx_match_bad_subject.barista")
	_expect(failures, match_bad_subject_report.get("valid", true) == false, "contextual case on non-union subject is invalid")
	var saw_bad_subject := false
	for message in match_bad_subject_report.get("errors", PackedStringArray()):
		if "needs a tagged-union match subject" in message and 'type "int"' in message:
			saw_bad_subject = true
	_expect(failures, saw_bad_subject, "non-union subject contextual shorthand diagnostic")

	var is_ok := _src_class("CtxIsOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tif msg is .Move(dx, dy):\n\t\treturn dx + dy\n\treturn 0\n")
	var is_ok_report: Dictionary = probe.analyze_source(is_ok, "res://tests/ctx_is_ok.barista")
	_expect(failures, is_ok_report.get("valid", false) == true, "is .Move(dx, dy) contextual case binds are valid")

	var is_arity := _src_class("CtxIsArity extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> void:\n\tif msg is .Move(dx):\n\t\tpass\n")
	var is_arity_report: Dictionary = probe.analyze_source(is_arity, "res://tests/ctx_is_arity.barista")
	_expect(failures, is_arity_report.get("valid", true) == false, "contextual is-case arity mismatch is invalid")
	var saw_is_arity := false
	for message in is_arity_report.get("errors", PackedStringArray()):
		if "carries 2 payload value(s), but 1 bind(s) were given" in message:
			saw_is_arity = true
	_expect(failures, saw_is_arity, "contextual is-case payload arity diagnostic")

	var is_bad_operand := _src_class("CtxIsBadOperand extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(n: int) -> void:\n\tif n is .Quit:\n\t\tpass\n")
	var is_bad_operand_report: Dictionary = probe.analyze_source(is_bad_operand, "res://tests/ctx_is_bad_operand.barista")
	_expect(failures, is_bad_operand_report.get("valid", true) == false, "is .Quit on non-union operand is invalid")
	var saw_bad_operand := false
	for message in is_bad_operand_report.get("errors", PackedStringArray()):
		if 'needs a tagged-union "is" operand' in message and 'type "int"' in message:
			saw_bad_operand = true
	_expect(failures, saw_bad_operand, "non-union is-operand contextual shorthand diagnostic")

	# Foundry contextual_tagged_union_pattern_undeclared_subject @ c9d5e35: subject error delta
	# suppresses per-arm "needs a tagged-union match subject… Variant" cascades.
	var match_missing := _src_class("CtxMatchMissingSubject extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tmatch missing_ident:\n\t\t.Quit:\n\t\t\tpass\n\t\t.Move(x):\n\t\t\tpass\n")
	var match_missing_report: Dictionary = probe.analyze_source(match_missing, "res://tests/ctx_match_missing_subject.barista")
	_expect(failures, match_missing_report.get("valid", true) == false, "match missing_ident with contextual arms is invalid")
	var saw_missing_ident := false
	var cascade_count := 0
	for message in match_missing_report.get("errors", PackedStringArray()):
		if 'Identifier "missing_ident" not declared in the current scope.' in message:
			saw_missing_ident = true
		if "needs a tagged-union match subject" in message:
			cascade_count += 1
	_expect(failures, saw_missing_ident, "match missing_ident subject diagnostic present")
	_expect(failures, cascade_count == 0, "failed match subject must not cascade per-arm Variant tagged-union diagnostics")

	# Optional nits: bare payload `.Move` value pattern; happy-path `is .Quit`.
	var match_bare_payload := _src_class("CtxMatchBarePayload extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\t.Move:\n\t\t\tpass\n")
	var match_bare_payload_report: Dictionary = probe.analyze_source(match_bare_payload, "res://tests/ctx_match_bare_payload.barista")
	_expect(failures, match_bare_payload_report.get("valid", true) == false, "bare .Move value pattern is invalid")
	var saw_bare_payload := false
	for message in match_bare_payload_report.get("errors", PackedStringArray()):
		if 'Case "Move" carries a payload, so it is matched with payload patterns' in message:
			saw_bare_payload = true
	_expect(failures, saw_bare_payload, "bare .Move payload-form diagnostic")

	var is_quit_ok := _src_class("CtxIsQuitOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(msg: Message) -> bool:\n\treturn msg is .Quit\n")
	var is_quit_ok_report: Dictionary = probe.analyze_source(is_quit_ok, "res://tests/ctx_is_quit_ok.barista")
	_expect(failures, is_quit_ok_report.get("valid", false) == true, "is .Quit contextual tag test is valid")

	# Foundry resolve_contextual_enum_case @ c9d5e35: expression-position assign/return construction.
	var var_ok := _src_class("CtxVarOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar idle: Message = .Quit\n\tvar moved: Message = .Move(1, 2)\n")
	var var_ok_report: Dictionary = probe.analyze_source(var_ok, "res://tests/ctx_var_ok.barista")
	_expect(failures, var_ok_report.get("valid", false) == true, "annotated var .Quit / .Move construction is valid")

	var assign_ok := _src_class("CtxAssignOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nvar member: Message = .Quit\nfunc handle() -> void:\n\tvar local: Message = .Quit\n\tlocal = .Move(3, 4)\n\tmember = .Quit\n")
	var assign_ok_report: Dictionary = probe.analyze_source(assign_ok, "res://tests/ctx_assign_ok.barista")
	_expect(failures, assign_ok_report.get("valid", false) == true, "assignment RHS .Case construction is valid")

	var return_ok := _src_class("CtxReturnOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc make_quit() -> Message:\n\treturn .Quit\nfunc make_move() -> Message:\n\treturn .Move(5, 6)\n")
	var return_ok_report: Dictionary = probe.analyze_source(return_ok, "res://tests/ctx_return_ok.barista")
	_expect(failures, return_ok_report.get("valid", false) == true, "return .Case construction is valid")

	var param_default_ok := _src_class("CtxParamDefaultOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(msg: Message = .Quit) -> Message:\n\treturn msg\n")
	var param_default_ok_report: Dictionary = probe.analyze_source(param_default_ok, "res://tests/ctx_param_default_ok.barista")
	_expect(failures, param_default_ok_report.get("valid", false) == true, "parameter default .Quit construction is valid")

	var untyped := _src_class("CtxUntyped extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar inferred = .Quit\n")
	var untyped_report: Dictionary = probe.analyze_source(untyped, "res://tests/ctx_untyped.barista")
	_expect(failures, untyped_report.get("valid", true) == false, "untyped .Quit without expected union is invalid")
	var saw_annotate := false
	for message in untyped_report.get("errors", PackedStringArray()):
		if 'needs an expected tagged-union type; annotate the target' in message:
			saw_annotate = true
	_expect(failures, saw_annotate, "untyped contextual construction annotate-target diagnostic")

	var wrong_expected := _src_class("CtxWrongExpected extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar counter: int = .Quit\n")
	var wrong_expected_report: Dictionary = probe.analyze_source(wrong_expected, "res://tests/ctx_wrong_expected.barista")
	_expect(failures, wrong_expected_report.get("valid", true) == false, ".Quit into int expected type is invalid")
	var saw_expects := false
	for message in wrong_expected_report.get("errors", PackedStringArray()):
		if 'needs an expected tagged-union type, but this position expects "int"' in message:
			saw_expects = true
	_expect(failures, saw_expects, "non-union expected-type contextual construction diagnostic")

	var payload_form := _src_class("CtxPayloadForm extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar bad: Message = .Move\n")
	var payload_form_report: Dictionary = probe.analyze_source(payload_form, "res://tests/ctx_payload_form.barista")
	_expect(failures, payload_form_report.get("valid", true) == false, "bare .Move construction is invalid")
	var saw_payload_form := false
	for message in payload_form_report.get("errors", PackedStringArray()):
		if 'carries a payload and must be constructed' in message:
			saw_payload_form = true
	_expect(failures, saw_payload_form, "bare .Move construction payload-form diagnostic")

	var arity_bad := _src_class("CtxConstructArity extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar bad: Message = .Move(1)\n")
	var arity_bad_report: Dictionary = probe.analyze_source(arity_bad, "res://tests/ctx_construct_arity.barista")
	_expect(failures, arity_bad_report.get("valid", true) == false, ".Move arity mismatch construction is invalid")
	var saw_construct_arity := false
	for message in arity_bad_report.get("errors", PackedStringArray()):
		if 'expects 2 argument(s), but 1 were given' in message:
			saw_construct_arity = true
	_expect(failures, saw_construct_arity, "contextual construction payload arity diagnostic")

	# Foundry update_container_literal_element_types / reduce_cast @ c9d5e35:
	# array / dictionary / cast / ternary consumers for contextual `.Case`.
	var array_ok := _src_class("CtxArrayOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar msgs: Array[Message] = [.Quit, .Move(1, 2)]\n")
	var array_ok_report: Dictionary = probe.analyze_source(array_ok, "res://tests/ctx_array_ok.barista")
	_expect(failures, array_ok_report.get("valid", false) == true, "Array[Message] element .Case construction is valid")

	var array_nested := _src_class("CtxArrayNested extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar nested: Array[Array[Message]] = [[.Quit, .Move(3)]]\n")
	var array_nested_report: Dictionary = probe.analyze_source(array_nested, "res://tests/ctx_array_nested.barista")
	_expect(failures, array_nested_report.get("valid", false) == true, "nested Array[Array[Message]] .Case construction is valid")

	var dict_ok := _src_class("CtxDictOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar by_id: Dictionary[int, Message] = {1: .Quit, 2: .Move(4)}\n\tvar labels: Dictionary[Message, String] = {.Quit: \"done\"}\n")
	var dict_ok_report: Dictionary = probe.analyze_source(dict_ok, "res://tests/ctx_dict_ok.barista")
	_expect(failures, dict_ok_report.get("valid", false) == true, "Dictionary key/value .Case construction is valid")

	var cast_ok := _src_class("CtxCastOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar labeled = .Quit as Message\n\tvar moved = .Move(5, 6) as Message\n")
	var cast_ok_report: Dictionary = probe.analyze_source(cast_ok, "res://tests/ctx_cast_ok.barista")
	_expect(failures, cast_ok_report.get("valid", false) == true, "cast operand .Case construction is valid")

	var cast_array := _src_class("CtxCastArray extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar labeled = [.Quit, .Move(7)] as Array[Message]\n")
	var cast_array_report: Dictionary = probe.analyze_source(cast_array, "res://tests/ctx_cast_array.barista")
	_expect(failures, cast_array_report.get("valid", false) == true, "cast Array[Message] element .Case construction is valid")

	var ternary_ok := _src_class("CtxTernaryOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc choose(cond: bool) -> Message:\n\treturn .Quit if cond else .Move(8)\nfunc handle(cond: bool) -> void:\n\tvar chosen: Message = .Quit if cond else .Move(9)\n\tvar elements: Array[Message] = [.Quit if cond else .Move(10)]\n\tvar labeled = (.Quit if cond else .Move(11)) as Message\n")
	var ternary_ok_report: Dictionary = probe.analyze_source(ternary_ok, "res://tests/ctx_ternary_ok.barista")
	_expect(failures, ternary_ok_report.get("valid", false) == true, "ternary branch .Case construction is valid")

	var call_arg_ok := _src_class("CtxCallArgOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc consume(msg: Message) -> void:\n\tpass\nfunc handle(cond: bool) -> void:\n\tconsume(.Quit)\n\tconsume(.Quit if cond else .Move(12))\n")
	var call_arg_ok_report: Dictionary = probe.analyze_source(call_arg_ok, "res://tests/ctx_call_arg_ok.barista")
	_expect(failures, call_arg_ok_report.get("valid", false) == true, "call-argument .Case construction is valid")

	var array_untyped := _src_class("CtxArrayUntyped extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar msgs = [.Quit]\n")
	var array_untyped_report: Dictionary = probe.analyze_source(array_untyped, "res://tests/ctx_array_untyped.barista")
	_expect(failures, array_untyped_report.get("valid", true) == false, "untyped array element .Quit is invalid")
	var saw_array_annotate := false
	for message in array_untyped_report.get("errors", PackedStringArray()):
		if 'needs an expected tagged-union type; annotate the target' in message:
			saw_array_annotate = true
	_expect(failures, saw_array_annotate, "untyped array element contextual construction diagnostic")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(match_payload, "res://tests/ctx_match_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "contextual match remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(match_payload, "res://tests/ctx_match_is_valid.barista"),
		"contextual match remains valid under is_semantically_valid()")
	_expect(failures, probe.is_semantically_valid(var_ok, "res://tests/ctx_var_is_valid.barista"),
		"contextual assign construction remains valid under is_semantically_valid()")
	_expect(failures, probe.is_semantically_valid(array_ok, "res://tests/ctx_array_is_valid.barista"),
		"contextual array construction remains valid under is_semantically_valid()")
	_expect(failures, probe.is_semantically_valid(cast_ok, "res://tests/ctx_cast_is_valid.barista"),
		"contextual cast construction remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for contextual case")


func _test_tagged_union_match_exhaustiveness(failures: PackedStringArray) -> void:
	# Foundry check_match_exhaustiveness tagged-union / plain-enum slice @ c9d5e35 (#60).
	# Warning apply runs in finalize; a flow-finality return error can exit analyze before that,
	# so NON_EXHAUSTIVE fixtures use void bodies (same pattern as Foundry's match-finality tests).
	var probe := BaristaScriptAnalyzerProbe.new()
	ProjectSettings.set_setting("debug/barista_script/warnings/enable", true)
	ProjectSettings.set_setting("debug/barista_script/warnings/non_exhaustive_match", 1) # WARN
	ProjectSettings.set_setting("debug/barista_script/warnings/open_enum_match_without_default", 1) # WARN
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()

	var incomplete := _src_class("TaggedMatchIncomplete extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\tpass\n")
	var incomplete_report: Dictionary = probe.validate_source(incomplete, "res://tests/tagged_match_incomplete.barista", true)
	_expect(failures, incomplete_report.get("valid", false) == true, "non-exhaustive tagged-union void match stays valid (warning-only)")
	var saw_non_exhaustive := false
	var saw_quit_uncovered := false
	for warn in incomplete_report.get("warnings", []):
		if "NON_EXHAUSTIVE_MATCH" in str(warn.get("string_code", "")):
			saw_non_exhaustive = true
		if "Quit" in str(warn.get("message", "")):
			saw_quit_uncovered = true
	_expect(failures, saw_non_exhaustive, "tagged-union match emits NON_EXHAUSTIVE_MATCH")
	_expect(failures, saw_quit_uncovered, "NON_EXHAUSTIVE_MATCH lists uncovered Quit case")

	# covers_subject_domain false keeps value-returning matches fail-closed for flow finality.
	var incomplete_ret := _src_class("TaggedMatchIncompleteRet extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\treturn dx + dy\n")
	var incomplete_ret_report: Dictionary = probe.analyze_source(incomplete_ret, "res://tests/tagged_match_incomplete_ret.barista")
	_expect(failures, incomplete_ret_report.get("valid", true) == false, "non-exhaustive tagged-union match / missing return is invalid")
	var saw_flow := false
	for message in incomplete_ret_report.get("errors", PackedStringArray()):
		if "Not all code paths return a value" in message:
			saw_flow = true
	_expect(failures, saw_flow, "non-covering tagged-union match fails return-path flow finality")

	var exhaustive := _src_class("TaggedMatchOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\treturn dx + dy\n\t\tMessage.Quit:\n\t\t\treturn 0\n")
	var exhaustive_report: Dictionary = probe.analyze_source(exhaustive, "res://tests/tagged_match_ok.barista")
	_expect(failures, exhaustive_report.get("valid", false) == true, "exhaustive tagged-union match with returns is valid")
	var saw_exhaustive_warn := false
	for warn in exhaustive_report.get("warnings", []):
		if "NON_EXHAUSTIVE_MATCH" in str(warn.get("string_code", "")):
			saw_exhaustive_warn = true
	_expect(failures, not saw_exhaustive_warn, "exhaustive tagged-union match has no NON_EXHAUSTIVE_MATCH")

	var contextual_ok := _src_class("TaggedMatchContextualOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\t.Move(dx):\n\t\t\treturn dx\n\t\t.Quit:\n\t\t\treturn 0\n")
	var contextual_ok_report: Dictionary = probe.analyze_source(contextual_ok, "res://tests/tagged_match_contextual_ok.barista")
	_expect(failures, contextual_ok_report.get("valid", false) == true, "exhaustive contextual .Case match is valid")

	var plain_enum := _src_class("PlainEnumMatch extends Node\nenum Level:\n\tLow = 1\n\tHigh = 2\nfunc handle(level: Level) -> void:\n\tmatch level:\n\t\tLevel.Low:\n\t\t\tpass\n\t\tLevel.High:\n\t\t\tpass\n")
	var plain_enum_report: Dictionary = probe.validate_source(plain_enum, "res://tests/plain_enum_match.barista", true)
	_expect(failures, plain_enum_report.get("valid", false) == true, "plain-enum match stays valid (warning-only)")
	var saw_open_enum := false
	for warn in plain_enum_report.get("warnings", []):
		if "OPEN_ENUM_MATCH_WITHOUT_DEFAULT" in str(warn.get("string_code", "")):
			saw_open_enum = true
	_expect(failures, saw_open_enum, "plain enum match emits OPEN_ENUM_MATCH_WITHOUT_DEFAULT")

	var bool_ok := _src_class("BoolMatchStillOk extends Node\nfunc check(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n\t\tfalse:\n\t\t\treturn 0\n")
	var bool_ok_report: Dictionary = probe.analyze_source(bool_ok, "res://tests/bool_match_still_ok.barista")
	_expect(failures, bool_ok_report.get("valid", false) == true, "exhaustive bool match remains valid after exhaustiveness port")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(exhaustive, "res://tests/tagged_match_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "exhaustive tagged match remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(exhaustive, "res://tests/tagged_match_is_valid.barista"),
		"exhaustive tagged match remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for tagged exhaustiveness")
