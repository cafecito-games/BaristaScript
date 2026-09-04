# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

extends SceneTree

const SuiteGuard := preload("res://tests/suite_guard.gd")
const FIXTURE_DIR := "res://tests/declaration_index_fixtures"
const WORK_DIR := "user://declaration_index_test"

func _init() -> void:
	var failures: PackedStringArray = []
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(WORK_DIR))
	_test_cold_and_round_trip(failures)
	_test_kinds_and_views(failures)
	_test_lifecycle_tokens(failures)
	_test_commit_rejects_noncanonical(failures)
	_test_corrupt_fixtures(failures)
	_test_atomic_write_faults(failures)
	_test_host_conformance(failures)
	quit(SuiteGuard.report("declaration_index_test", failures))


func _probe() -> BaristaScriptDeclarationIndexProbe:
	return BaristaScriptDeclarationIndexProbe.new()


func _expect(failures: PackedStringArray, condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)


func _test_cold_and_round_trip(failures: PackedStringArray) -> void:
	var probe := _probe()
	probe.clear()
	var missing := WORK_DIR.path_join("absent.bsi")
	_expect(failures, probe.load(missing) == 1, "absent index must be COLD")
	_expect(failures, probe.get_record_count() == 0, "cold load exposes no entries")

	var token_a := probe.claim_refresh("res://tests/declaration_index_fixtures/a.barista")
	_expect(failures, probe.commit_record(token_a, {
		"path": "res://tests/declaration_index_fixtures/a.barista",
		"source_digest": BaristaScriptDeclarationIndexProbe.compute_source_digest("class_name Alpha extends Node"),
		"namespace_name": "game",
		"qualified_name": "game.Alpha",
		"kind": 1,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(["game.Mark"]),
		"declares_retroactive_conformances": false,
	}), "class record must commit")

	var token_b := probe.claim_refresh("res://tests/declaration_index_fixtures/conform.barista")
	_expect(failures, probe.commit_record(token_b, {
		"path": "res://tests/declaration_index_fixtures/conform.barista",
		"source_digest": 42,
		"namespace_name": "game",
		"qualified_name": "",
		"kind": 0,
		"base_type": "",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": true,
	}), "conformance-only record must commit")

	var store := WORK_DIR.path_join("round_trip.bsi")
	_expect(failures, probe.flush(store, 0) == OK, "flush must succeed")
	var first := FileAccess.get_file_as_bytes(store)
	_expect(failures, probe.flush(store, 0) == OK, "second flush must succeed")
	var second := FileAccess.get_file_as_bytes(store)
	_expect(failures, first == second, "identical logical state must be byte-identical")

	# Persist golden fixture for the suite and docs.
	var golden_path := FIXTURE_DIR.path_join("golden_index.bsi")
	_expect(failures, probe.flush(golden_path, 0) == OK, "golden fixture flush must succeed")

	probe.clear()
	_expect(failures, probe.load(store) == 0, "round-trip load must be OK")
	_expect(failures, probe.get_record_count() == 2, "round-trip must restore both records")
	var conformances := probe.get_conformance_files_in_namespace("game")
	_expect(failures, conformances.size() == 1 and conformances[0].ends_with("conform.barista"),
		"conformance view must list the declaration-only file")
	var annotations := probe.get_annotation_declaring_paths("game.Mark")
	_expect(failures, annotations.size() == 1, "annotation view must list declaring path")


func _test_kinds_and_views(failures: PackedStringArray) -> void:
	var probe := _probe()
	probe.clear()
	var kinds := [
		{"path": "res://tests/k_class.barista", "kind": 1, "name": "KClass", "ns": ""},
		{"path": "res://tests/k_generic.barista", "kind": 2, "name": "pkg.Box", "ns": "pkg"},
		{"path": "res://tests/k_trait.barista", "kind": 3, "name": "pkg.Trait", "ns": "pkg"},
		{"path": "res://tests/k_enum.barista", "kind": 4, "name": "pkg.Enum", "ns": "pkg"},
		{"path": "res://tests/k_tuple.barista", "kind": 5, "name": "pkg.Tup", "ns": "pkg"},
	]
	for entry in kinds:
		var token := probe.claim_refresh(entry.path)
		_expect(failures, probe.commit_record(token, {
			"path": entry.path,
			"source_digest": 1,
			"namespace_name": entry.ns,
			"qualified_name": entry.name,
			"kind": entry.kind,
			"base_type": "RefCounted" if entry.kind == 1 or entry.kind == 2 else "",
			"is_abstract": entry.kind != 1,
			"is_tool": false,
			"icon_path": "",
			"global_annotations": PackedStringArray(),
			"declares_retroactive_conformances": false,
		}), "kind %s must commit" % entry.kind)
	_expect(failures, probe.get_record_count() == 5, "every declaration kind must round-trip in memory")


func _test_lifecycle_tokens(failures: PackedStringArray) -> void:
	var probe := _probe()
	probe.clear()
	var path := "res://tests/token.barista"
	var old_token := probe.claim_refresh(path)
	var new_token := probe.claim_refresh(path)
	_expect(failures, not probe.commit_record(old_token, {
		"path": path,
		"source_digest": 1,
		"namespace_name": "",
		"qualified_name": "Old",
		"kind": 1,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "stale generation token must not commit")
	_expect(failures, probe.commit_record(new_token, {
		"path": path,
		"source_digest": 2,
		"namespace_name": "",
		"qualified_name": "New",
		"kind": 1,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "current generation token must commit")
	var failed := probe.claim_refresh(path)
	_expect(failures, probe.remove_path(path, failed), "failed analysis removes prior record")
	_expect(failures, probe.get_record_count() == 0, "removed path must leave no record")


func _test_commit_rejects_noncanonical(failures: PackedStringArray) -> void:
	var probe := _probe()
	probe.clear()
	var evil_path := "user://evil.barista"
	var evil_token := probe.claim_refresh(evil_path)
	_expect(failures, not probe.commit_record(evil_token, {
		"path": evil_path,
		"source_digest": 1,
		"namespace_name": "evil",
		"qualified_name": "",
		"kind": 0,
		"base_type": "",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": true,
	}), "non-res:// path must not commit")
	_expect(failures, probe.get_record_count() == 0, "rejected non-res:// commit must leave no record")
	_expect(failures, probe.get_conformance_files_in_namespace("evil").is_empty(), "rejected path must not appear in conformance view")

	var path := "res://tests/bad_kind.barista"
	var token := probe.claim_refresh(path)
	_expect(failures, not probe.commit_record(token, {
		"path": path,
		"source_digest": 2,
		"namespace_name": "",
		"qualified_name": "BadKind",
		"kind": 99,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "out-of-range kind must not commit")
	_expect(failures, probe.get_record_count() == 0, "rejected kind must leave no record")


func _mutate_and_expect(failures: PackedStringArray, source: PackedByteArray, mutator: Callable, expected_status: int, label: String) -> void:
	var bytes := source.duplicate()
	mutator.call(bytes)
	var path := WORK_DIR.path_join(label + ".bsi")
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_buffer(bytes)
	file.close()
	var probe := _probe()
	probe.clear()
	_expect(failures, probe.load(path) == expected_status, "%s must reject as status %s" % [label, expected_status])
	_expect(failures, probe.get_record_count() == 0, "%s must expose no partial entries" % label)


func _test_corrupt_fixtures(failures: PackedStringArray) -> void:
	var golden := FileAccess.get_file_as_bytes(FIXTURE_DIR.path_join("golden_index.bsi"))
	_expect(failures, golden.size() > 20, "golden fixture must exist")

	# BAD_MAGIC = 2
	_mutate_and_expect(failures, golden, func(b: PackedByteArray) -> void: b[0] = "X".unicode_at(0), 2, "bad_magic")
	# UNSUPPORTED_VERSION = 3
	_mutate_and_expect(failures, golden, func(b: PackedByteArray) -> void:
		b[4] = 2
		# Invalidate file checksum deliberately by flipping last byte after version change without recompute.
		b[b.size() - 1] = (b[b.size() - 1] + 1) & 0xFF
	, 3, "old_version")
	# Truncate below header size so load reports TRUNCATED rather than a checksum mismatch.
	_mutate_and_expect(failures, golden, func(b: PackedByteArray) -> void: b.resize(8), 4, "truncated")
	# BAD_CHECKSUM = 6
	_mutate_and_expect(failures, golden, func(b: PackedByteArray) -> void: b[12] = (b[12] + 1) & 0xFF, 6, "bad_checksum")

	# Persist negative fixtures for docs/README.
	for name in ["bad_magic", "old_version", "truncated", "bad_checksum"]:
		var src := FileAccess.get_file_as_bytes(WORK_DIR.path_join(name + ".bsi"))
		var out := FileAccess.open(FIXTURE_DIR.path_join(name + ".bsi"), FileAccess.WRITE)
		out.store_buffer(src)
		out.close()


func _test_atomic_write_faults(failures: PackedStringArray) -> void:
	var probe := _probe()
	probe.clear()
	var token := probe.claim_refresh("res://tests/fault.barista")
	probe.commit_record(token, {
		"path": "res://tests/fault.barista",
		"source_digest": 7,
		"namespace_name": "",
		"qualified_name": "Fault",
		"kind": 1,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	})
	var store := WORK_DIR.path_join("atomic.bsi")
	_expect(failures, probe.flush(store, 0) == OK, "baseline flush")
	var before := FileAccess.get_file_as_bytes(store)
	_expect(failures, probe.flush(store, 1) != OK, "BEFORE_WRITE fault must fail")
	_expect(failures, FileAccess.get_file_as_bytes(store) == before, "BEFORE_WRITE must leave previous file")
	_expect(failures, probe.flush(store, 2) != OK, "AFTER_WRITE_BEFORE_RENAME fault must fail")
	_expect(failures, FileAccess.get_file_as_bytes(store) == before, "rename fault must leave previous file")
	_expect(failures, probe.flush(store, 3) != OK, "TRUNCATE fault must fail")
	_expect(failures, FileAccess.get_file_as_bytes(store) == before, "truncate fault must leave previous file")


func _test_host_conformance(failures: PackedStringArray) -> void:
	var probe := _probe()
	probe.clear()
	var token := probe.claim_refresh("res://tests/host_conform.barista")
	probe.commit_record(token, {
		"path": "res://tests/host_conform.barista",
		"source_digest": 9,
		"namespace_name": "hostns",
		"qualified_name": "",
		"kind": 0,
		"base_type": "",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": true,
	})
	var files := probe.host_conformance_files_in_namespace("hostns")
	_expect(failures, files.size() == 1, "installed host must read index conformance files")
	_expect(failures, probe.host_conformance_files_in_namespace("").is_empty(), "global namespace has no implicit conformances")

	probe.set_bootstrap_root("res://tests/")
	_expect(failures, probe.host_is_bootstrap_path_allowed("res://tests/host_conform.barista"), "in-root path allowed")
	_expect(failures, not probe.host_is_bootstrap_path_allowed("res://outside/x.barista"), "out-of-root path filtered")
	probe.set_bootstrap_root("")
