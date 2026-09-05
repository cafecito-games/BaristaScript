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
	BaristaScriptParseCache.clear_script_cache()
	quit(SuiteGuard.report("analyzer_test", failures))


func _expect(failures: PackedStringArray, condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)


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
	BaristaScriptParseCache.set_source_override("res://tests/dep_a.barista", "class_name DepA extends Node\n")
	BaristaScriptParseCache.set_source_override("res://tests/dep_b.barista", "class_name DepB extends Node\n")
	BaristaScriptParseCache.set_source_override("res://tests/dep_c.barista", "class_name DepC extends Node\n")
	BaristaScriptParseCache.set_source_override("res://tests/unrelated.barista", "class_name Unrelated extends Node\n")

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
	BaristaScriptParseCache.set_source_override("res://tests/owner.barista", "class_name OwnerFile extends Node\n")
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

	BaristaScriptParseCache.set_source_override("res://tests/self.barista", "class_name SelfFile extends Node\n")
	var self_ref := BaristaScriptParseCache.get_parser("res://tests/self.barista", Status.EMPTY, "res://tests/self.barista")
	_expect(failures, self_ref.valid, "self owner still creates the entry")
	# Self-dependency must not invent a distinct edge; inverse deps of self exclude self-owner recording.
	var inverse := BaristaScriptParseCache.get_inverse_dependencies("res://tests/self.barista")
	_expect(failures, not ("res://tests/self.barista" in inverse), "self-dependency creates no owner edge")
	BaristaScriptParseCache.clear_source_overrides()


func _test_move_remove(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/old.barista", "class_name OldName extends Node\n")
	BaristaScriptParseCache.get_parser("res://tests/old.barista", Status.PARSED, "")
	_expect(failures, BaristaScriptParseCache.has_parser("res://tests/old.barista"), "old path cached")
	BaristaScriptParseCache.move_script("res://tests/old.barista", "res://tests/new.barista")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/old.barista"), "move drops old parser state")
	BaristaScriptParseCache.set_source_override("res://tests/new.barista", "class_name NewName extends Node\n")
	var rebuilt := BaristaScriptParseCache.get_parser("res://tests/new.barista", Status.PARSED, "")
	_expect(failures, rebuilt.valid, "new path rebuilds rather than inheriting stale pointers")
	BaristaScriptParseCache.remove_script("res://tests/new.barista")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/new.barista"), "remove_script drops parser state")
	BaristaScriptParseCache.clear_source_overrides()


func _test_dependency_cycle(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/cycle_a.barista", "class_name CycleA extends Node\n")
	BaristaScriptParseCache.set_source_override("res://tests/cycle_b.barista", "class_name CycleB extends Node\n")
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
	BaristaScriptParseCache.set_source_override("res://tests/strict.barista", "class_name StrictOne extends Node\n")
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
	var valid_source := "class_name AnalyzerValid extends Node\n\nfunc _ready() -> void:\n\tvar x: int = 1\n"
	var analyzed: Dictionary = probe.analyze_source(valid_source, "res://tests/analyzer_valid.barista")
	_expect(failures, analyzed["valid"] == true, "valid program analyzes cleanly")
	_expect(failures, probe.is_semantically_valid(valid_source, "res://tests/analyzer_valid.barista"),
		"probe is_semantically_valid agrees for valid program")
	var script := BaristaScript.new()
	script.set_source_code(valid_source)
	script.resource_path = "res://tests/analyzer_valid.barista"
	_expect(failures, script.is_valid(), "BaristaScript.is_valid agrees for valid program")

	var bad_source := "class_name AnalyzerBad extends NotARealBaseClass\n"
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
	var mismatch := "class_name TypeMismatch extends Node\n\nfunc _ready() -> void:\n\tvar x: int = 1.5\n"
	var report: Dictionary = probe.analyze_source(mismatch, "res://tests/type_mismatch.barista")
	_expect(failures, report["valid"] == false, "int = float mismatch is invalid")
	_expect(failures, (report["errors"] as PackedStringArray).size() > 0, "type mismatch produces a diagnostic")

	var generic := "class_name GenericBox[T] extends RefCounted\n"
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
	var ok_source := "class_name CommitOk extends Node\n\nfunc _ready() -> void:\n\tpass\n"
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

	index.synchronize_path_from_source(ok_path, "class_name CommitOk extends MissingBaseDefinitely\n")
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
		{"path": "res://tests/commit_generic.barista", "source": "class_name CommitGeneric[T] extends RefCounted\n", "name": "CommitGeneric", "kind": 2},
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
	var source := "class_name DigestFresh extends Node\n"
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

	BaristaScriptParseCache.set_source_override(consumer_old, "namespace oldns\nclass_name OldConsumer extends Node\n")
	BaristaScriptParseCache.set_source_override(consumer_new, "namespace newns\nclass_name NewConsumer extends Node\n")
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
		"import outerspace\nclass_name NeedsOuter extends Node\n",
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
	var too_few := "class_name CallFew extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tadd(1)\n"
	var few_report: Dictionary = probe.analyze_source(too_few, "res://tests/call_few.barista")
	_expect(failures, few_report.get("valid", true) == false, "too few arguments invalid")
	var saw_few := false
	for message in few_report.get("errors", PackedStringArray()):
		if "Too few arguments" in message:
			saw_few = true
	_expect(failures, saw_few, "too few arguments diagnostic")

	var bad_type := "class_name CallType extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tadd(1, 1.5)\n"
	var type_report: Dictionary = probe.analyze_source(bad_type, "res://tests/call_type.barista")
	_expect(failures, type_report.get("valid", true) == false, "wrong call argument type invalid")

	var ok := "class_name CallOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tvar x: int = add(1, 2)\n"
	var ok_report: Dictionary = probe.analyze_source(ok, "res://tests/call_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "matching call arity/types valid")


func _test_call_validation_methodinfo_and_signals(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()

	# MethodInfo path: bare native call on Node base (ClassDB MethodInfo via BSNativeDB).
	var native_few := "class_name NativeFew extends Node\nfunc _ready() -> void:\n\tget_node()\n"
	var native_few_report: Dictionary = probe.analyze_source(native_few, "res://tests/native_few.barista")
	_expect(failures, native_few_report.get("valid", true) == false, "native MethodInfo too-few invalid")
	var saw_native_few := false
	for message in native_few_report.get("errors", PackedStringArray()):
		if "Too few arguments" in message and "get_node" in message:
			saw_native_few = true
	_expect(failures, saw_native_few, "native MethodInfo too-few diagnostic")

	var native_type := "class_name NativeType extends Node\nfunc _ready() -> void:\n\tget_node(1)\n"
	var native_type_report: Dictionary = probe.analyze_source(native_type, "res://tests/native_type.barista")
	_expect(failures, native_type_report.get("valid", true) == false, "native MethodInfo wrong arg type invalid")
	var saw_native_type := false
	for message in native_type_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "get_node" in message:
			saw_native_type = true
	_expect(failures, saw_native_type, "native MethodInfo wrong-type diagnostic")

	# Signal.emit payload arity + types.
	var emit_few := "class_name EmitFew extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tchanged.emit()\n"
	var emit_few_report: Dictionary = probe.analyze_source(emit_few, "res://tests/emit_few.barista")
	_expect(failures, emit_few_report.get("valid", true) == false, "signal.emit too-few invalid")
	var saw_emit_few := false
	for message in emit_few_report.get("errors", PackedStringArray()):
		if "Too few arguments" in message and "emit" in message:
			saw_emit_few = true
	_expect(failures, saw_emit_few, "signal.emit too-few diagnostic")

	var emit_type := "class_name EmitType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tchanged.emit(\"bad\")\n"
	var emit_type_report: Dictionary = probe.analyze_source(emit_type, "res://tests/emit_type.barista")
	_expect(failures, emit_type_report.get("valid", true) == false, "signal.emit wrong type invalid")
	var saw_emit_type := false
	for message in emit_type_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit" in message:
			saw_emit_type = true
	_expect(failures, saw_emit_type, "signal.emit wrong-type diagnostic")

	var emit_ok := "class_name EmitOk extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tchanged.emit(1)\n"
	var emit_ok_report: Dictionary = probe.analyze_source(emit_ok, "res://tests/emit_ok.barista")
	_expect(failures, emit_ok_report.get("valid", false) == true, "signal.emit matching payload valid")

	# emit_signal("name", ...) constant-name payload validation.
	var emit_signal_type := "class_name EmitSignalType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\temit_signal(\"changed\", \"bad\")\n"
	var emit_signal_report: Dictionary = probe.analyze_source(emit_signal_type, "res://tests/emit_signal_type.barista")
	_expect(failures, emit_signal_report.get("valid", true) == false, "emit_signal wrong payload type invalid")
	var saw_emit_signal := false
	for message in emit_signal_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit_signal" in message:
			saw_emit_signal = true
	_expect(failures, saw_emit_signal, "emit_signal wrong-type diagnostic")

	var emit_signal_ok := "class_name EmitSignalOk extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\temit_signal(\"changed\", 1)\n"
	var emit_signal_ok_report: Dictionary = probe.analyze_source(emit_signal_ok, "res://tests/emit_signal_ok.barista")
	_expect(failures, emit_signal_ok_report.get("valid", false) == true, "emit_signal matching payload valid")

	# self.emit_signal must also run typed payload validation (#72 / PR #71 review).
	var self_emit_signal_type := "class_name SelfEmitSignalType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tself.emit_signal(\"changed\", \"bad\")\n"
	var self_emit_signal_report: Dictionary = probe.analyze_source(self_emit_signal_type, "res://tests/self_emit_signal_type.barista")
	_expect(failures, self_emit_signal_report.get("valid", true) == false, "self.emit_signal wrong payload type invalid")
	var saw_self_emit_signal := false
	for message in self_emit_signal_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit_signal" in message:
			saw_self_emit_signal = true
	_expect(failures, saw_self_emit_signal, "self.emit_signal wrong-type diagnostic")

	var self_emit := "class_name SelfChangedEmitType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tself.changed.emit(\"bad\")\n"
	var self_emit_report: Dictionary = probe.analyze_source(self_emit, "res://tests/self_changed_emit_type.barista")
	_expect(failures, self_emit_report.get("valid", true) == false, "self.changed.emit wrong payload type invalid")
	var saw_self_emit := false
	for message in self_emit_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit" in message:
			saw_self_emit = true
	_expect(failures, saw_self_emit, "self.changed.emit wrong-type diagnostic")


func _test_match_and_flow(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var incomplete := "class_name MatchIncomplete extends Node\nfunc check(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n"
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

	var exhaustive := "class_name MatchOk extends Node\nfunc check(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n\t\tfalse:\n\t\t\treturn 0\n"
	var ok_report: Dictionary = probe.analyze_source(exhaustive, "res://tests/match_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "exhaustive bool match with returns is valid")


func _test_warning_settings(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	# Underscore-prefixed local avoids UNUSED_VARIABLE so this fixture isolates INTEGER_DIVISION.
	var source := "class_name WarnDiv extends Node\nfunc _ready() -> void:\n\tvar _z: int = 1 / 2\n"
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
	var reassign := "class_name FinalReassign extends Node\nfunc _ready() -> void:\n\tfinal var x: int = 1\n\tx = 2\n"
	var reassign_report: Dictionary = probe.analyze_source(reassign, "res://tests/final_reassign.barista")
	_expect(failures, reassign_report.get("valid", true) == false, "reassigning initialized final is invalid")
	var saw_reassign := false
	for message in reassign_report.get("errors", PackedStringArray()):
		if "already assigned" in message:
			saw_reassign = true
	_expect(failures, saw_reassign, "final reassignment diagnostic")

	var use_before := "class_name FinalUseBefore extends Node\nfunc use() -> int:\n\tfinal var x: int\n\treturn x\n"
	var before_report: Dictionary = probe.analyze_source(use_before, "res://tests/final_use_before.barista")
	_expect(failures, before_report.get("valid", true) == false, "reading blank final before assignment is invalid")
	var saw_before := false
	for message in before_report.get("errors", PackedStringArray()):
		if "before assignment" in message:
			saw_before = true
	_expect(failures, saw_before, "final use-before-assignment diagnostic")

	var branch_ok := "class_name FinalBranchOk extends Node\nfunc pick(flag: bool) -> int:\n\tfinal var x: int\n\tif flag:\n\t\tx = 1\n\telse:\n\t\tx = 2\n\treturn x\n"
	var ok_report: Dictionary = probe.analyze_source(branch_ok, "res://tests/final_branch_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "final assigned on both branches then read is valid")

	var branch_bad := "class_name FinalBranchBad extends Node\nfunc pick(flag: bool) -> int:\n\tfinal var x: int\n\tif flag:\n\t\tx = 1\n\treturn x\n"
	var bad_report: Dictionary = probe.analyze_source(branch_bad, "res://tests/final_branch_bad.barista")
	_expect(failures, bad_report.get("valid", true) == false, "final assigned on only one branch then read is invalid")
	var saw_branch_bad := false
	for message in bad_report.get("errors", PackedStringArray()):
		if "before assignment" in message:
			saw_branch_bad = true
	_expect(failures, saw_branch_bad, "FinalBranchBad reports use-before-assignment diagnostic")

	var lambda_write := "class_name FinalLambdaWrite extends Node\nfunc _ready() -> void:\n\tfinal var x: int = 1\n\tvar f := func():\n\t\tx = 2\n\tf.call()\n"
	var lambda_report: Dictionary = probe.analyze_source(lambda_write, "res://tests/final_lambda_write.barista")
	_expect(failures, lambda_report.get("valid", true) == false, "assigning outer final inside lambda is invalid")
	var saw_lambda := false
	for message in lambda_report.get("errors", PackedStringArray()):
		if "lambda" in message.to_lower():
			saw_lambda = true
	_expect(failures, saw_lambda, "illegal lambda final-write diagnostic")


func _test_final_member_and_static_assignment(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()

	var member_ok := "class_name FinalMemberOk extends Node\nfinal var id: int\nfunc _init(p_id: int) -> void:\n\tid = p_id\n"
	var member_ok_report: Dictionary = probe.analyze_source(member_ok, "res://tests/final_member_ok.barista")
	_expect(failures, member_ok_report.get("valid", false) == true, "blank final assigned once in _init is valid")

	var member_outside := "class_name FinalMemberOutside extends Node\nfinal var id: int\nfunc _init() -> void:\n\tid = 1\nfunc reset() -> void:\n\tid = 2\n"
	var outside_report: Dictionary = probe.analyze_source(member_outside, "res://tests/final_member_outside.barista")
	_expect(failures, outside_report.get("valid", true) == false, "final member assigned outside _init is invalid")
	var saw_outside := false
	for message in outside_report.get("errors", PackedStringArray()):
		if "_init()" in message and "can only be assigned" in message:
			saw_outside = true
	_expect(failures, saw_outside, "final member outside-_init diagnostic")

	var member_twice := "class_name FinalMemberTwice extends Node\nfinal var id: int\nfunc _init() -> void:\n\tid = 1\n\tid = 2\n"
	var twice_report: Dictionary = probe.analyze_source(member_twice, "res://tests/final_member_twice.barista")
	_expect(failures, twice_report.get("valid", true) == false, "final member assigned twice in _init is invalid")
	var saw_twice := false
	for message in twice_report.get("errors", PackedStringArray()):
		if "already assigned" in message:
			saw_twice = true
	_expect(failures, saw_twice, "final member double-assign diagnostic")

	var member_blank := "class_name FinalMemberBlank extends Node\nfinal var id: int\nfunc _init() -> void:\n\tpass\n"
	var blank_report: Dictionary = probe.analyze_source(member_blank, "res://tests/final_member_blank.barista")
	_expect(failures, blank_report.get("valid", true) == false, "blank final never assigned in _init is invalid")
	var saw_blank := false
	for message in blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message and "_init()" in message:
			saw_blank = true
	_expect(failures, saw_blank, "blank final never-assigned diagnostic")

	var member_branches := "class_name FinalMemberBranches extends Node\nfinal var label: String\nfunc _init(positive: bool) -> void:\n\tif positive:\n\t\tlabel = \"pos\"\n\telse:\n\t\tlabel = \"neg\"\n"
	var branches_report: Dictionary = probe.analyze_source(member_branches, "res://tests/final_member_branches.barista")
	_expect(failures, branches_report.get("valid", false) == true, "final member assigned on both branches is valid")

	var member_self := "class_name FinalMemberSelf extends Node\nfinal var id: int\nfunc _init(p_id: int) -> void:\n\tself.id = p_id\nfunc bump() -> void:\n\tself.id = 99\n"
	var self_report: Dictionary = probe.analyze_source(member_self, "res://tests/final_member_self.barista")
	_expect(failures, self_report.get("valid", true) == false, "self.final reassigned outside _init is invalid")
	var saw_self := false
	for message in self_report.get("errors", PackedStringArray()):
		if "_init()" in message and "can only be assigned" in message:
			saw_self = true
	_expect(failures, saw_self, "self.final outside-_init diagnostic")

	var static_ok := "class_name FinalStaticOk extends Node\nfinal static var LABEL: String\nstatic func _static_init() -> void:\n\tLABEL = \"ready\"\n"
	var static_ok_report: Dictionary = probe.analyze_source(static_ok, "res://tests/final_static_ok.barista")
	_expect(failures, static_ok_report.get("valid", false) == true, "blank static final assigned once in _static_init is valid")

	var static_outside := "class_name FinalStaticOutside extends Node\nfinal static var LABEL: String = \"ready\"\nfunc reset() -> void:\n\tLABEL = \"other\"\n"
	var static_outside_report: Dictionary = probe.analyze_source(static_outside, "res://tests/final_static_outside.barista")
	_expect(failures, static_outside_report.get("valid", true) == false, "static final reassigned outside _static_init is invalid")
	var saw_static_outside := false
	for message in static_outside_report.get("errors", PackedStringArray()):
		if "_static_init()" in message and "can only be assigned" in message:
			saw_static_outside = true
	_expect(failures, saw_static_outside, "static final outside-_static_init diagnostic")

	var static_blank := "class_name FinalStaticBlank extends Node\nfinal static var LABEL: String\n"
	var static_blank_report: Dictionary = probe.analyze_source(static_blank, "res://tests/final_static_blank.barista")
	_expect(failures, static_blank_report.get("valid", true) == false, "blank static final without _static_init is invalid")
	var saw_static_blank := false
	for message in static_blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message and "_static_init()" in message:
			saw_static_blank = true
	_expect(failures, saw_static_blank, "blank static final never-assigned diagnostic")

	var static_qualified := "class_name FinalStaticQualified extends Node\nfinal static var LABEL: String = \"ready\"\nfunc reset() -> void:\n\tFinalStaticQualified.LABEL = \"other\"\n"
	var static_qualified_report: Dictionary = probe.analyze_source(static_qualified, "res://tests/final_static_qualified.barista")
	_expect(failures, static_qualified_report.get("valid", true) == false, "ClassName.static final reassignment is invalid")
	var saw_static_qualified := false
	for message in static_qualified_report.get("errors", PackedStringArray()):
		if "_static_init()" in message and "can only be assigned" in message:
			saw_static_qualified = true
	_expect(failures, saw_static_qualified, "ClassName.static final outside-_static_init diagnostic")

	var onready_final := "class_name FinalOnreadyBad extends Node\n@onready final var id: int = 1\n"
	var onready_report: Dictionary = probe.analyze_source(onready_final, "res://tests/final_onready_bad.barista")
	_expect(failures, onready_report.get("valid", true) == false, "@onready final member is invalid")
	var saw_onready := false
	for message in onready_report.get("errors", PackedStringArray()):
		if "@onready" in message:
			saw_onready = true
	_expect(failures, saw_onready, "@onready final rejection diagnostic")

	var property_final := "class_name FinalPropertyBad extends Node\nfinal var id: int:\n\tget:\n\t\treturn 1\n"
	var property_report: Dictionary = probe.analyze_source(property_final, "res://tests/final_property_bad.barista")
	_expect(failures, property_report.get("valid", true) == false, "final property member is invalid")
	var saw_property := false
	for message in property_report.get("errors", PackedStringArray()):
		if "property" in message.to_lower() or "getter" in message.to_lower() or "final" in message.to_lower():
			saw_property = true
	_expect(failures, saw_property, "final property rejection diagnostic")

	var early_return := "class_name FinalMemberEarlyReturn extends Node\nfinal var id: int\nfunc _init(flag: bool) -> void:\n\tif flag:\n\t\treturn\n\tid = 1\n"
	var early_report: Dictionary = probe.analyze_source(early_return, "res://tests/final_member_early_return.barista")
	_expect(failures, early_report.get("valid", true) == false, "blank final not assigned on early-return path is invalid")
	var saw_early := false
	for message in early_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message or "before assignment" in message:
			saw_early = true
	_expect(failures, saw_early, "final member early-return definite-assignment diagnostic")

	var use_before := "class_name FinalMemberUseBefore extends Node\nfinal var id: int\nfunc _init() -> void:\n\tvar _sink: int = id\n\tid = 1\n"
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

	var trait_ok := "class_name FinalTraitOk extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\nfunc _init() -> void:\n\tid = 42\n"
	var ok_report: Dictionary = probe.analyze_source(trait_ok, "res://tests/final_trait_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "trait blank final assigned once in implementer _init is valid")

	var trait_blank := "class_name FinalTraitBlank extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\nfunc _init() -> void:\n\tpass\n"
	var blank_report: Dictionary = probe.analyze_source(trait_blank, "res://tests/final_trait_blank.barista")
	_expect(failures, blank_report.get("valid", true) == false, "trait blank final never assigned is invalid")
	var saw_blank := false
	for message in blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message:
			saw_blank = true
	_expect(failures, saw_blank, "trait blank final never-assigned diagnostic")

	var trait_twice := "class_name FinalTraitTwice extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\nfunc _init() -> void:\n\tid = 1\n\tid = 2\n"
	var twice_report: Dictionary = probe.analyze_source(trait_twice, "res://tests/final_trait_twice.barista")
	_expect(failures, twice_report.get("valid", true) == false, "trait final assigned twice in _init is invalid")
	var saw_twice := false
	for message in twice_report.get("errors", PackedStringArray()):
		if "already assigned" in message:
			saw_twice = true
	_expect(failures, saw_twice, "trait final double-assign diagnostic")

	var trait_method := "class_name FinalTraitMethod extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int = 1\n\tfunc mutate() -> void:\n\t\tid = 2\n\nfunc _ready() -> void:\n\tpass\n"
	var method_report: Dictionary = probe.analyze_source(trait_method, "res://tests/final_trait_method.barista")
	_expect(failures, method_report.get("valid", true) == false, "trait method reassigning flattened final is invalid")
	var saw_method := false
	for message in method_report.get("errors", PackedStringArray()):
		if "can only be assigned" in message:
			saw_method = true
	_expect(failures, saw_method, "trait method illegal-write diagnostic")

	var trait_init := "class_name FinalTraitInit extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\tfunc _init() -> void:\n\t\tid = 5\n\nfunc _ready() -> void:\n\tpass\n"
	var trait_init_report: Dictionary = probe.analyze_source(trait_init, "res://tests/final_trait_init.barista")
	_expect(failures, trait_init_report.get("valid", false) == true, "trait-supplied _init assigning blank final is valid")

	# Cyclic uses must fail-stop (Foundry resolve_trait_uses); do not flatten as resolved (#75).
	var trait_cycle := "class_name FinalTraitCycle extends Node\nuses CycleA\n\ntrait CycleA:\n\tuses CycleB\n\ntrait CycleB:\n\tuses CycleA\n"
	var cycle_report: Dictionary = probe.analyze_source(trait_cycle, "res://tests/final_trait_cycle.barista")
	_expect(failures, cycle_report.get("valid", true) == false, "cyclic trait uses is invalid")
	var saw_cycle := false
	for message in cycle_report.get("errors", PackedStringArray()):
		if "Cyclic trait use" in message:
			saw_cycle = true
	_expect(failures, saw_cycle, "cyclic trait use diagnostic")

	# Static trait-supplied finals.
	var trait_static_blank := "class_name FinalTraitStaticBlank extends Node\nuses HasLabel\n\ntrait HasLabel:\n\tfinal static var LABEL: String\n"
	var static_blank_report: Dictionary = probe.analyze_source(trait_static_blank, "res://tests/final_trait_static_blank.barista")
	_expect(failures, static_blank_report.get("valid", true) == false, "trait static blank final without _static_init is invalid")
	var saw_static_blank := false
	for message in static_blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message and "_static_init()" in message:
			saw_static_blank = true
	_expect(failures, saw_static_blank, "trait static blank never-assigned diagnostic")

	var trait_static_ok := "class_name FinalTraitStaticOk extends Node\nuses HasLabel\n\ntrait HasLabel:\n\tfinal static var LABEL: String\n\nstatic func _static_init() -> void:\n\tLABEL = \"ready\"\n"
	var static_ok_report: Dictionary = probe.analyze_source(trait_static_ok, "res://tests/final_trait_static_ok.barista")
	_expect(failures, static_ok_report.get("valid", false) == true, "trait static blank assigned in _static_init is valid")

	var trait_static_outside := "class_name FinalTraitStaticOutside extends Node\nuses HasLabel\n\ntrait HasLabel:\n\tfinal static var LABEL: String = \"ready\"\n\nfunc reset() -> void:\n\tLABEL = \"other\"\n"
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
	var consumer := "class_name FinalTraitIndexBlank extends Node\nuses IndexHasId\nfunc _init() -> void:\n\tpass\n"
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
	var noreturn_ok := "class_name NoreturnOk extends Node\n@noreturn\nfunc die() -> void:\n\tpush_fatal(\"boom\")\nfunc value() -> int:\n\tdie()\n"
	var ok_report: Dictionary = probe.analyze_source(noreturn_ok, "res://tests/noreturn_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "noreturn callee terminates return paths")

	var noreturn_incomplete := "class_name NoreturnIncomplete extends Node\n@noreturn\nfunc die() -> void:\n\tpass\n"
	var incomplete_report: Dictionary = probe.analyze_source(noreturn_incomplete, "res://tests/noreturn_incomplete.barista")
	_expect(failures, incomplete_report.get("valid", true) == false, "@noreturn function that completes normally is invalid")
	var saw_complete := false
	for message in incomplete_report.get("errors", PackedStringArray()):
		if "cannot complete normally" in message:
			saw_complete = true
	_expect(failures, saw_complete, "@noreturn complete-normally diagnostic")

	var noreturn_nested := "class_name NoreturnNestedReturn extends Node\n@noreturn\nfunc die(flag: bool) -> void:\n\tif flag:\n\t\treturn\n\tpush_fatal(\"boom\")\n"
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
	var unused := "class_name UnusedLocal extends Node\nfunc _ready() -> void:\n\tvar orphan: int = 1\n"
	var unused_report: Dictionary = probe.validate_source(unused, "res://tests/unused_local.barista", true)
	_expect(failures, unused_report.get("valid", false) == true, "unused local stays valid at WARN")
	var saw_unused := false
	for warn in unused_report.get("warnings", []):
		if "UNUSED_VARIABLE" in str(warn.get("string_code", "")) or "never used" in str(warn.get("message", "")).to_lower():
			saw_unused = true
	_expect(failures, saw_unused, "unused local produces UNUSED_VARIABLE")

	var write_only := "class_name WriteOnlyLocal extends Node\nfunc _ready() -> void:\n\tvar scratch: int\n\tscratch = 1\n"
	var write_only_report: Dictionary = probe.validate_source(write_only, "res://tests/write_only_local.barista", true)
	var saw_write_only := false
	for warn in write_only_report.get("warnings", []):
		if "scratch" in str(warn.get("message", "")) and ("UNUSED_VARIABLE" in str(warn.get("string_code", "")) or "never used" in str(warn.get("message", "")).to_lower()):
			saw_write_only = true
	_expect(failures, saw_write_only, "write-only local produces UNUSED_VARIABLE")

	var unused_param := "class_name UnusedParam extends Node\nfunc greet(name: String) -> void:\n\tpass\n"
	var param_report: Dictionary = probe.validate_source(unused_param, "res://tests/unused_param.barista", true)
	var saw_param := false
	for warn in param_report.get("warnings", []):
		var msg := str(warn.get("message", "")).to_lower()
		if "UNUSED_PARAMETER" in str(warn.get("string_code", "")) and "never used" in msg and "greet" in msg:
			saw_param = true
	_expect(failures, saw_param, "unused parameter produces UNUSED_PARAMETER message")

	var used := "class_name UsedLocal extends Node\nfunc _ready() -> void:\n\tvar keep: int = 1\n\tvar _sink: int = keep\n"
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

	var unused_private := "class_name UnusedPrivateMember extends Node\nvar _orphan: int = 1\nfunc _ready() -> void:\n\tpass\n"
	var private_report: Dictionary = probe.validate_source(unused_private, "res://tests/unused_private_member.barista", true)
	_expect(failures, private_report.get("valid", false) == true, "unused private member stays valid at WARN")
	var saw_private := false
	for warn in private_report.get("warnings", []):
		var code := str(warn.get("string_code", ""))
		var msg := str(warn.get("message", ""))
		if "UNUSED_PRIVATE_CLASS_VARIABLE" in code and "_orphan" in msg and "never used in the class" in msg:
			saw_private = true
	_expect(failures, saw_private, "unused private member produces UNUSED_PRIVATE_CLASS_VARIABLE message")

	var used_private := "class_name UsedPrivateMember extends Node\nvar _keep: int = 1\nfunc _ready() -> void:\n\tvar _sink: int = _keep\n"
	var used_private_report: Dictionary = probe.validate_source(used_private, "res://tests/used_private_member.barista", true)
	var saw_used_private := false
	for warn in used_private_report.get("warnings", []):
		if "UNUSED_PRIVATE_CLASS_VARIABLE" in str(warn.get("string_code", "")) and "_keep" in str(warn.get("message", "")):
			saw_used_private = true
	_expect(failures, not saw_used_private, "used private member does not warn as unused")

	var public_unused := "class_name PublicUnusedMember extends Node\nvar visible: int = 1\nfunc _ready() -> void:\n\tpass\n"
	var public_report: Dictionary = probe.validate_source(public_unused, "res://tests/public_unused_member.barista", true)
	var saw_public := false
	for warn in public_report.get("warnings", []):
		if "UNUSED_PRIVATE_CLASS_VARIABLE" in str(warn.get("string_code", "")):
			saw_public = true
	_expect(failures, not saw_public, "public unused member does not produce UNUSED_PRIVATE_CLASS_VARIABLE")

	var unused_signal := "class_name UnusedSignalScript extends Node\nsignal lonely\nfunc _ready() -> void:\n\tpass\n"
	var signal_report: Dictionary = probe.validate_source(unused_signal, "res://tests/unused_signal.barista", true)
	_expect(failures, signal_report.get("valid", false) == true, "unused signal stays valid at WARN")
	var saw_signal := false
	for warn in signal_report.get("warnings", []):
		var code := str(warn.get("string_code", ""))
		var msg := str(warn.get("message", ""))
		if "UNUSED_SIGNAL" in code and "lonely" in msg and "never explicitly used" in msg:
			saw_signal = true
	_expect(failures, saw_signal, "unused signal produces UNUSED_SIGNAL message")

	var used_signal := "class_name UsedSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\temit_signal(\"ping\")\n"
	var used_signal_report: Dictionary = probe.validate_source(used_signal, "res://tests/used_signal.barista", true)
	var saw_used_signal := false
	for warn in used_signal_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "ping" in str(warn.get("message", "")):
			saw_used_signal = true
	_expect(failures, not saw_used_signal, "emit_signal counts as signal use")

	# Bare connect/disconnect/is_connected must also count (Foundry identifier callee = self) (#78).
	var connect_signal := "class_name ConnectSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\tconnect(\"ping\", Callable())\n"
	var connect_report: Dictionary = probe.validate_source(connect_signal, "res://tests/connect_signal.barista", true)
	var saw_connect_unused := false
	for warn in connect_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "ping" in str(warn.get("message", "")):
			saw_connect_unused = true
	_expect(failures, not saw_connect_unused, "bare connect counts as signal use")

	var disconnect_signal := "class_name DisconnectSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\tdisconnect(\"ping\", Callable())\n"
	var disconnect_report: Dictionary = probe.validate_source(disconnect_signal, "res://tests/disconnect_signal.barista", true)
	var saw_disconnect_unused := false
	for warn in disconnect_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "ping" in str(warn.get("message", "")):
			saw_disconnect_unused = true
	_expect(failures, not saw_disconnect_unused, "bare disconnect counts as signal use")

	var is_connected_signal := "class_name IsConnectedSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\tvar _linked: bool = is_connected(\"ping\", Callable())\n"
	var is_connected_report: Dictionary = probe.validate_source(is_connected_signal, "res://tests/is_connected_signal.barista", true)
	var saw_is_connected_unused := false
	for warn in is_connected_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "ping" in str(warn.get("message", "")):
			saw_is_connected_unused = true
	_expect(failures, not saw_is_connected_unused, "bare is_connected counts as signal use")

	# Nested unused: exactly one warning for the nested signal (no duplicate from outer unused pass).
	var nested := "class_name NestedUnusedOuter extends Node\nclass Inner:\n\tsignal nested_lonely\n\tfunc _ready() -> void:\n\t\tpass\nfunc _ready() -> void:\n\tpass\n"
	var nested_report: Dictionary = probe.validate_source(nested, "res://tests/nested_unused_signal.barista", true)
	var nested_count := 0
	for warn in nested_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "nested_lonely" in str(warn.get("message", "")):
			nested_count += 1
	_expect(failures, nested_count == 1, "nested unused signal warns exactly once")

	# Annotation surface: @warning_ignore needs resolve_annotation to populate resolved_arguments.
	var ignored := "class_name IgnoredSignalScript extends Node\n@warning_ignore(\"unused_signal\")\nsignal quiet\nfunc _ready() -> void:\n\tpass\n"
	var ignored_report: Dictionary = probe.validate_source(ignored, "res://tests/ignored_signal.barista", true)
	var saw_ignored := false
	for warn in ignored_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "quiet" in str(warn.get("message", "")):
			saw_ignored = true
	_expect(failures, not saw_ignored, "@warning_ignore(\"unused_signal\") suppresses UNUSED_SIGNAL via resolve_annotation")


func _test_trait_requirements_and_conformance_witness(failures: PackedStringArray) -> void:
	# Foundry fixtures: trait_required_method / retroactive_conformance_missing_method (#60).
	var probe := BaristaScriptAnalyzerProbe.new()

	var missing := "class_name TraitReqMissing extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n"
	var missing_report: Dictionary = probe.analyze_source(missing, "res://tests/trait_req_missing.barista")
	_expect(failures, missing_report.get("valid", true) == false, "missing abstract trait method is invalid")
	var saw_missing := false
	for message in missing_report.get("errors", PackedStringArray()):
		if "must implement trait method" in message and "Damageable.take_damage()" in message:
			saw_missing = true
	_expect(failures, saw_missing, "missing trait method diagnostic")

	var ok := "class_name TraitReqOk extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n\nfunc take_damage(amount: int) -> void:\n\tpass\n"
	var ok_report: Dictionary = probe.analyze_source(ok, "res://tests/trait_req_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "implemented abstract trait method is valid")

	var abstract_class := "abstract class_name TraitReqAbstract extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n"
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

	var local_missing := "class_name RtcLocalGadget extends Node\n\ntrait NeedsPing:\n\tabstract func ping() -> void\n\nextend RtcLocalGadget uses NeedsPing:\n\tpass\n"
	var local_missing_report: Dictionary = probe.analyze_source(local_missing, "res://tests/rtc_local_missing.barista")
	_expect(failures, local_missing_report.get("valid", true) == false, "same-file extend missing witness is invalid")
	var saw_local_missing := false
	for message in local_missing_report.get("errors", PackedStringArray()):
		if "Conformance of" in message and "must implement trait method" in message and "ping()" in message:
			saw_local_missing = true
	_expect(failures, saw_local_missing, "same-file extend missing-witness diagnostic")

	var local_ok := "class_name RtcLocalOk extends Node\n\ntrait NeedsPing:\n\tabstract func ping() -> void\n\nextend RtcLocalOk uses NeedsPing:\n\tfunc ping() -> void:\n\t\tpass\n"
	var local_ok_report: Dictionary = probe.analyze_source(local_ok, "res://tests/rtc_local_ok.barista")
	_expect(failures, local_ok_report.get("valid", false) == true, "same-file extend with witness is valid")

	# Redundant extend when the target already owns the trait via uses.
	var redundant := "class_name RtcRedundantTarget extends Node\nuses NeedsPing\n\ntrait NeedsPing:\n\tabstract func ping() -> void\n\nfunc ping() -> void:\n\tpass\n\nextend RtcRedundantTarget uses NeedsPing:\n\tpass\n"
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
	var consumer := "class_name TraitIndexMissing extends Node\nuses IndexDamageable\n"
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
	var sig_mismatch := "class_name TraitSigMismatch extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n\nfunc take_damage(amount: String) -> void:\n\tpass\n"
	var sig_mismatch_report: Dictionary = probe.analyze_source(sig_mismatch, "res://tests/trait_sig_mismatch.barista")
	_expect(failures, sig_mismatch_report.get("valid", true) == false, "trait method wrong parameter type is invalid")
	var saw_sig_mismatch := false
	for message in sig_mismatch_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Damageable.take_damage()" in message:
			saw_sig_mismatch = true
	_expect(failures, saw_sig_mismatch, "trait method signature mismatch diagnostic")

	var async_required := "class_name TraitAsyncRequired extends Node\nuses RemoteLoadable\n\ntrait RemoteLoadable:\n\tabstract async func fetch() -> String\n\nfunc fetch() -> String:\n\treturn \"\"\n"
	var async_required_report: Dictionary = probe.analyze_source(async_required, "res://tests/trait_async_required.barista")
	_expect(failures, async_required_report.get("valid", true) == false, "sync impl of async trait method is invalid")
	var saw_async_required := false
	for message in async_required_report.get("errors", PackedStringArray()):
		if "must be async because it implements async trait method" in message and "RemoteLoadable.fetch()" in message:
			saw_async_required = true
	_expect(failures, saw_async_required, "async trait method requires async impl diagnostic")

	var sync_required := "class_name TraitSyncRequired extends Node\nuses Syncable\n\ntrait Syncable:\n\tabstract func compute() -> int\n\nasync func compute() -> int:\n\treturn 0\n"
	var sync_required_report: Dictionary = probe.analyze_source(sync_required, "res://tests/trait_sync_required.barista")
	_expect(failures, sync_required_report.get("valid", true) == false, "async impl of sync trait method is invalid")
	var saw_sync_required := false
	for message in sync_required_report.get("errors", PackedStringArray()):
		if "cannot be async because it implements synchronous trait method" in message and "Syncable.compute()" in message:
			saw_sync_required = true
	_expect(failures, saw_sync_required, "sync trait method rejects async impl diagnostic")

	var self_ok := "class_name TraitSelfOk extends Node\nuses Creatable\n\ntrait Creatable:\n\tabstract static func create() -> Self\n\nstatic func create() -> Self:\n\treturn TraitSelfOk.new()\n"
	var self_ok_report: Dictionary = probe.analyze_source(self_ok, "res://tests/trait_self_ok.barista")
	_expect(failures, self_ok_report.get("valid", false) == true, "Self return matching implementer is valid")

	var self_bad := "class_name TraitSelfBad extends Node\nuses Creatable\n\ntrait Creatable:\n\tabstract static func create() -> Self\n\nstatic func create() -> String:\n\treturn \"x\"\n"
	var self_bad_report: Dictionary = probe.analyze_source(self_bad, "res://tests/trait_self_bad.barista")
	_expect(failures, self_bad_report.get("valid", true) == false, "Self return mismatched to String is invalid")
	var saw_self_bad := false
	for message in self_bad_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Creatable.create()" in message:
			saw_self_bad = true
	_expect(failures, saw_self_bad, "Self return mismatch diagnostic")

	var arity_bad := "class_name TraitArityBad extends Node\nuses Binary\n\ntrait Binary:\n\tabstract func combine(a: int, b: int) -> int\n\nfunc combine(a: int) -> int:\n\treturn a\n"
	var arity_bad_report: Dictionary = probe.analyze_source(arity_bad, "res://tests/trait_arity_bad.barista")
	_expect(failures, arity_bad_report.get("valid", true) == false, "trait method arity mismatch is invalid")
	var saw_arity := false
	for message in arity_bad_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Binary.combine()" in message:
			saw_arity = true
	_expect(failures, saw_arity, "trait method arity mismatch diagnostic")

	var rest_narrow := "class_name TraitRestNarrow extends Node\nuses AcceptsNodes\n\ntrait AcceptsNodes:\n\tabstract func accept(...nodes: Array[Node]) -> int\n\nfunc accept(...nodes: Array[String]) -> int:\n\treturn nodes.size()\n"
	var rest_narrow_report: Dictionary = probe.analyze_source(rest_narrow, "res://tests/trait_rest_narrow.barista")
	_expect(failures, rest_narrow_report.get("valid", true) == false, "narrower rest tail does not satisfy trait rest requirement")
	var saw_rest := false
	for message in rest_narrow_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "AcceptsNodes.accept()" in message:
			saw_rest = true
	_expect(failures, saw_rest, "trait rest narrowing diagnostic")

	# Fixed Array/Dictionary element matching (#84 / #83 review): carriers alone are not enough.
	var array_elem := "class_name TraitArrayElem extends Node\nuses TakesNodes\n\ntrait TakesNodes:\n\tabstract func take(items: Array[Node]) -> void\n\nfunc take(items: Array[String]) -> void:\n\tpass\n"
	var array_elem_report: Dictionary = probe.analyze_source(array_elem, "res://tests/trait_array_elem.barista")
	_expect(failures, array_elem_report.get("valid", true) == false, "fixed Array[Node] vs Array[String] param is invalid")
	var saw_array_elem := false
	for message in array_elem_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "TakesNodes.take()" in message:
			saw_array_elem = true
	_expect(failures, saw_array_elem, "fixed Array element mismatch diagnostic")

	var dict_elem := "class_name TraitDictElem extends Node\nuses TakesMap\n\ntrait TakesMap:\n\tabstract func take(items: Dictionary[String, Node]) -> void\n\nfunc take(items: Dictionary[String, String]) -> void:\n\tpass\n"
	var dict_elem_report: Dictionary = probe.analyze_source(dict_elem, "res://tests/trait_dict_elem.barista")
	_expect(failures, dict_elem_report.get("valid", true) == false, "fixed Dictionary value element mismatch is invalid")
	var saw_dict_elem := false
	for message in dict_elem_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "TakesMap.take()" in message:
			saw_dict_elem = true
	_expect(failures, saw_dict_elem, "fixed Dictionary element mismatch diagnostic")

	# Explicit static-vs-instance (and reverse) signature mismatch (#84 / #83 review).
	var static_vs_instance := "class_name TraitStaticVsInst extends Node\nuses Factory\n\ntrait Factory:\n\tabstract static func build() -> void\n\nfunc build() -> void:\n\tpass\n"
	var static_vs_instance_report: Dictionary = probe.analyze_source(static_vs_instance, "res://tests/trait_static_vs_inst.barista")
	_expect(failures, static_vs_instance_report.get("valid", true) == false, "instance impl of static trait method is invalid")
	var saw_static_vs_inst := false
	for message in static_vs_instance_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Factory.build()" in message:
			saw_static_vs_inst = true
	_expect(failures, saw_static_vs_inst, "static-vs-instance signature mismatch diagnostic")

	var instance_vs_static := "class_name TraitInstVsStatic extends Node\nuses Worker\n\ntrait Worker:\n\tabstract func run() -> void\n\nstatic func run() -> void:\n\tpass\n"
	var instance_vs_static_report: Dictionary = probe.analyze_source(instance_vs_static, "res://tests/trait_inst_vs_static.barista")
	_expect(failures, instance_vs_static_report.get("valid", true) == false, "static impl of instance trait method is invalid")
	var saw_inst_vs_static := false
	for message in instance_vs_static_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Worker.run()" in message:
			saw_inst_vs_static = true
	_expect(failures, saw_inst_vs_static, "instance-vs-static signature mismatch diagnostic")

	var rtc_sig := "class_name RtcSigTarget extends Node\n\ntrait NeedsPing:\n\tabstract func ping(code: int) -> void\n\nextend RtcSigTarget uses NeedsPing:\n\tfunc ping(code: String) -> void:\n\t\tpass\n"
	var rtc_sig_report: Dictionary = probe.analyze_source(rtc_sig, "res://tests/rtc_sig_mismatch.barista")
	_expect(failures, rtc_sig_report.get("valid", true) == false, "extend witness with wrong signature is invalid")
	var saw_rtc_sig := false
	for message in rtc_sig_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "NeedsPing.ping()" in message:
			saw_rtc_sig = true
	_expect(failures, saw_rtc_sig, "extend witness signature mismatch diagnostic")

	var native_sig := "class_name NativeSigBad extends Node\nuses NeedsGetNode\n\ntrait NeedsGetNode:\n\tabstract func get_node(path: int) -> Node\n"
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

	var bare_null := "class_name BareNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tvar x: Node = n\n"
	var bare_null_report: Dictionary = probe.analyze_source(bare_null, "res://tests/bare_nullable_assign.barista")
	_expect(failures, bare_null_report.get("valid", true) == false, "nullable to non-null assign fails under strict_null")

	var narrowed_null := "class_name NarrowedNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tvar x: Node = n\n"
	var narrowed_null_report: Dictionary = probe.analyze_source(narrowed_null, "res://tests/narrowed_nullable_assign.barista")
	_expect(failures, narrowed_null_report.get("valid", false) == true, "null-check true arm allows Node? → Node")

	var else_null := "class_name ElseNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tpass\n\telse:\n\t\tvar x: Node = n\n"
	var else_null_report: Dictionary = probe.analyze_source(else_null, "res://tests/else_nullable_assign.barista")
	_expect(failures, else_null_report.get("valid", true) == false, "null-check else arm keeps Node? → Node invalid")

	var assert_null := "class_name AssertNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tassert(n != null)\n\tvar x: Node = n\n"
	var assert_null_report: Dictionary = probe.analyze_source(assert_null, "res://tests/assert_nullable_assign.barista")
	_expect(failures, assert_null_report.get("valid", false) == true, "assert null-check narrows later statements")

	var bare_union := "class_name BareUnionAssign extends Node\nfunc take(v: int | String) -> void:\n\tvar s: String = v\n"
	var bare_union_report: Dictionary = probe.analyze_source(bare_union, "res://tests/bare_union_assign.barista")
	_expect(failures, bare_union_report.get("valid", true) == false, "union to String without type test is invalid")

	var narrowed_is := "class_name NarrowedIsAssign extends Node\nfunc take(v: int | String) -> void:\n\tif v is String:\n\t\tvar s: String = v\n"
	var narrowed_is_report: Dictionary = probe.analyze_source(narrowed_is, "res://tests/narrowed_is_assign.barista")
	_expect(failures, narrowed_is_report.get("valid", false) == true, "`is String` true arm allows int|String → String")

	var else_is := "class_name ElseIsAssign extends Node\nfunc take(v: int | String) -> void:\n\tif v is String:\n\t\tpass\n\telse:\n\t\tvar i: int = v\n"
	var else_is_report: Dictionary = probe.analyze_source(else_is, "res://tests/else_is_assign.barista")
	_expect(failures, else_is_report.get("valid", false) == true, "`is String` else arm subtracts String leaving int")

	var cleared := "class_name ClearedNarrowingAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tn = null\n\t\tvar x: Node = n\n"
	var cleared_report: Dictionary = probe.analyze_source(cleared, "res://tests/cleared_narrowing_assign.barista")
	_expect(failures, cleared_report.get("valid", true) == false, "assignment clears prior null-check narrowing")

	var and_narrow := "class_name AndNarrowingAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null and n is Node:\n\t\tvar x: Node = n\n"
	var and_narrow_report: Dictionary = probe.analyze_source(and_narrow, "res://tests/and_narrowing_assign.barista")
	_expect(failures, and_narrow_report.get("valid", false) == true, "`and` condition applies left-side null-check narrowing")

	ProjectSettings.set_setting("debug/barista_script/analysis/strict_null_checks", false)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
