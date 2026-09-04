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
