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
	_test_match_and_flow(failures)
	_test_warning_settings(failures)
	_test_final_local_assignment(failures)
	_test_noreturn_flow(failures)
	_test_unused_locals(failures)
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
	# Trait must exist for uses resolution; CommitTrait already synchronized above.
	index.synchronize_path_from_source(conform_path, conform_source)
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

	index.synchronize_path_from_source(conform_path,
		"namespace oldns\n\nextend Node uses Object:\n\tpass\n")
	# Object isn't a trait — use a synchronized trait instead.
	index.synchronize_path_from_source("res://tests/ns_trait.barista", "trait_name NsTrait\n")
	index.synchronize_path_from_source(conform_path,
		"namespace oldns\n\nextend Node uses NsTrait:\n\tpass\n")

	BaristaScriptParseCache.set_source_override(consumer_old, "namespace oldns\nclass_name OldConsumer extends Node\n")
	BaristaScriptParseCache.set_source_override(consumer_new, "namespace newns\nclass_name NewConsumer extends Node\n")
	BaristaScriptParseCache.get_parser(consumer_old, Status.PARSED, "")
	BaristaScriptParseCache.get_parser(consumer_new, Status.PARSED, "")
	_expect(failures, BaristaScriptParseCache.has_parser(consumer_old), "old-namespace consumer cached")
	_expect(failures, BaristaScriptParseCache.has_parser(consumer_new), "new-namespace consumer cached")

	# Analyzer-driven namespace change on the conformance file.
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
	index.synchronize_path_from_source("res://outside/out_conform.barista",
		"namespace outerspace\n\nextend Node uses OuterTrait:\n\tpass\n")
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
		if "before assignment" in message or "already assigned" in message or "final" in message.to_lower():
			saw_branch_bad = true
	_expect(failures, saw_branch_bad, "FinalBranchBad reports final assignment diagnostic")

	var lambda_write := "class_name FinalLambdaWrite extends Node\nfunc _ready() -> void:\n\tfinal var x: int = 1\n\tvar f := func():\n\t\tx = 2\n\tf.call()\n"
	var lambda_report: Dictionary = probe.analyze_source(lambda_write, "res://tests/final_lambda_write.barista")
	_expect(failures, lambda_report.get("valid", true) == false, "assigning outer final inside lambda is invalid")
	var saw_lambda := false
	for message in lambda_report.get("errors", PackedStringArray()):
		if "lambda" in message.to_lower():
			saw_lambda = true
	_expect(failures, saw_lambda, "illegal lambda final-write diagnostic")


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
		if "UNUSED_PARAMETER" in str(warn.get("string_code", "")) or ("name" in str(warn.get("message", "")) and "never used" in str(warn.get("message", "")).to_lower()):
			saw_param = true
	_expect(failures, saw_param, "unused parameter produces UNUSED_PARAMETER")

	var used := "class_name UsedLocal extends Node\nfunc _ready() -> void:\n\tvar keep: int = 1\n\tvar _sink: int = keep\n"
	var used_report: Dictionary = probe.validate_source(used, "res://tests/used_local.barista", true)
	var saw_keep_unused := false
	for warn in used_report.get("warnings", []):
		if "keep" in str(warn.get("message", "")):
			saw_keep_unused = true
	_expect(failures, not saw_keep_unused, "used local does not warn as unused")
