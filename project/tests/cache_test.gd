# cache_test.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

extends SceneTree

## Guarded parse-cache suite for one-shot store promotion and fail-closed loading.

const SuiteGuard = preload("res://tests/suite_guard.gd")

const FIXTURES := "res://tests/cache_fixtures"
const SCRIPT_A := "%s/script_a.barista" % FIXTURES
const SCRIPT_B := "%s/script_b.barista" % FIXTURES
const GOLDEN_STORE := "%s/golden_store.bin" % FIXTURES
const VERSION_MISMATCH_STORE := "%s/version_mismatch_store.bin" % FIXTURES
const TRUNCATED_STORE := "%s/truncated_store.bin" % FIXTURES
const CORRUPT_STORE := "%s/corrupt_store.bin" % FIXTURES
const BAD_MAGIC_STORE := "%s/bad_magic_store.bin" % FIXTURES
const DUPLICATE_KEY_STORE := "%s/duplicate_key_store.bin" % FIXTURES

const SCRATCH_ROOT := "user://cache_test_scratch"

# Distinct exercised cases. Keep in sync with the calls in _initialize.
const TEST_COUNT := 14

const COLD := 0
const VERSION_MISMATCH := 1
const DIGEST_MISMATCH := 2
const CORRUPT := 3

const FAULT_NONE := 0
const FAULT_BEFORE_WRITE := 1
const FAULT_AFTER_WRITE_BEFORE_RENAME := 2
const FAULT_TRUNCATE_TEMP_AFTER_WRITE := 3
const FAULT_REMOVE_TEMP_BEFORE_PROMOTION := 4


func _initialize() -> void:
	var failures: Array[String] = []
	DirAccess.make_dir_recursive_absolute(SCRATCH_ROOT)

	_test_first_create_replace_and_replay_identity(failures)
	_test_fault_four_preserves_old_store_and_memory(failures)
	_test_fault_four_without_old_store_leaves_destination_absent(failures)
	_test_invalid_directory_destination_is_rejected(failures)
	_test_path_shapes_publish_identically(failures)
	_test_malformed_paths_reject_before_temp_creation(failures)
	_test_faults_one_through_three_still_preserve_previous_store(failures)
	_test_golden_and_damaged_store_contracts(failures)

	_expect(failures, TEST_COUNT == 14, "TEST_COUNT must match the suite's declared coverage")
	quit(SuiteGuard.report("cache_test", failures))


func _test_first_create_replace_and_replay_identity(failures: Array[String]) -> void:
	var store := _scratch("create_replace_replay.bpc")
	_remove(store)
	var source_a := _read_text(SCRIPT_A)
	var source_b := _read_text(SCRIPT_B)
	var version := BaristaScriptParseCache.get_cache_format_version()

	var first := BaristaScriptParseCache.new()
	first.put(SCRIPT_A, source_a, _payload(source_a))
	_expect(failures, first.flush(store, FAULT_NONE, version) == OK, "first-create flush must succeed")
	_expect(failures, _temp_files_for(store).is_empty(), "first-create must leave no temp")

	var reader := BaristaScriptParseCache.new()
	_expect(failures, reader.load(store) == COLD, "first-create store must load cleanly")
	_expect(failures, reader.lookup(SCRIPT_A, source_a)["hit"], "first-create must serve A")
	_expect(failures, not reader.has_entry(SCRIPT_B), "first-create must not invent B")

	var bytes_after_a := FileAccess.get_file_as_bytes(store)
	var second := BaristaScriptParseCache.new()
	second.put(SCRIPT_B, source_b, _payload(source_b))
	_expect(failures, second.flush(store, FAULT_NONE, version) == OK, "replacement flush must succeed")
	_expect(failures, FileAccess.get_file_as_bytes(store) != bytes_after_a, "replacement must change store bytes")

	reader = BaristaScriptParseCache.new()
	_expect(failures, reader.load(store) == COLD, "replaced store must load cleanly")
	_expect(failures, reader.lookup(SCRIPT_B, source_b)["hit"], "replacement must serve B")
	_expect(failures, not reader.has_entry(SCRIPT_A), "replacement without A must drop A")

	var bytes_after_b := FileAccess.get_file_as_bytes(store)
	var replay := BaristaScriptParseCache.new()
	replay.put(SCRIPT_B, source_b, _payload(source_b))
	_expect(failures, replay.flush(store, FAULT_NONE, version) == OK, "replay flush must succeed")
	_expect(failures, FileAccess.get_file_as_bytes(store) == bytes_after_b, "identical entries must replay byte-identically")
	_expect(failures, _temp_files_for(store).is_empty(), "replay must leave no temp")


func _test_fault_four_preserves_old_store_and_memory(failures: Array[String]) -> void:
	var store := _scratch("fault4_preserve.bpc")
	_remove(store)
	var source_a := _read_text(SCRIPT_A)
	var source_b := _read_text(SCRIPT_B)
	var version := BaristaScriptParseCache.get_cache_format_version()

	var first := BaristaScriptParseCache.new()
	first.put(SCRIPT_A, source_a, _payload(source_a))
	_expect(failures, first.flush(store, FAULT_NONE, version) == OK, "baseline A flush must succeed")
	var previous := FileAccess.get_file_as_bytes(store)

	var second := BaristaScriptParseCache.new()
	second.put(SCRIPT_B, source_b, _payload(source_b))
	_expect(failures, second.has_entry(SCRIPT_B), "B must be buffered before fault 4")
	var err := second.flush(store, FAULT_REMOVE_TEMP_BEFORE_PROMOTION, version)
	_expect(failures, err == ERR_FILE_CANT_WRITE, "fault 4 must return ERR_FILE_CANT_WRITE, got %s" % err)
	_expect(failures, FileAccess.get_file_as_bytes(store) == previous, "fault 4 must leave previous loadable bytes unchanged")
	_expect(failures, second.has_entry(SCRIPT_B), "fault 4 must keep B available in memory")
	_expect(failures, _temp_files_for(store).is_empty(), "fault 4 must not leave its temp behind: %s" % [_temp_files_for(store)])

	var reader := BaristaScriptParseCache.new()
	_expect(failures, reader.load(store) == COLD, "previous store must still load")
	_expect(failures, reader.lookup(SCRIPT_A, source_a)["hit"], "previous A must still hit on disk")
	_expect(failures, not reader.has_entry(SCRIPT_B), "B must remain absent on disk after fault 4")


func _test_fault_four_without_old_store_leaves_destination_absent(failures: Array[String]) -> void:
	var store := _scratch("fault4_absent.bpc")
	_remove(store)
	var source_b := _read_text(SCRIPT_B)
	var cache := BaristaScriptParseCache.new()
	cache.put(SCRIPT_B, source_b, _payload(source_b))
	var err := cache.flush(store, FAULT_REMOVE_TEMP_BEFORE_PROMOTION, BaristaScriptParseCache.get_cache_format_version())
	_expect(failures, err != OK, "fault 4 without an old store must fail")
	_expect(failures, not FileAccess.file_exists(store), "fault 4 without an old store must leave destination absent")
	_expect(failures, cache.has_entry(SCRIPT_B), "in-memory B must survive fault 4")
	_expect(failures, _temp_files_for(store).is_empty(), "fault 4 absent-old case must leave no temp")


func _test_invalid_directory_destination_is_rejected(failures: Array[String]) -> void:
	var directory := _scratch("not_a_store_dir")
	DirAccess.make_dir_recursive_absolute(directory)
	var sentinel := "%s/sentinel.txt" % directory
	var sentinel_bytes := PackedByteArray([1, 2, 3, 4])
	_write_bytes(sentinel, sentinel_bytes)

	var cache := BaristaScriptParseCache.new()
	cache.put(SCRIPT_A, _read_text(SCRIPT_A), _payload(_read_text(SCRIPT_A)))
	var err := cache.flush(directory, FAULT_NONE, BaristaScriptParseCache.get_cache_format_version())
	_expect(failures, err != OK, "flush to a directory destination must fail")
	_expect(failures, DirAccess.dir_exists_absolute(directory), "directory destination must remain a directory")
	_expect(failures, FileAccess.get_file_as_bytes(sentinel) == sentinel_bytes, "sentinel inside the directory must be unchanged")
	_expect(failures, cache.has_entry(SCRIPT_A), "memory must stay usable after invalid destination")


func _test_path_shapes_publish_identically(failures: Array[String]) -> void:
	var source := _read_text(SCRIPT_A)
	var payload := _payload(source)
	var version := BaristaScriptParseCache.get_cache_format_version()

	var user_path := "user://cache_test_scratch/path with spaces/café_store.bpc"
	DirAccess.make_dir_recursive_absolute(user_path.get_base_dir())
	_remove(user_path)

	var writer := BaristaScriptParseCache.new()
	writer.put(SCRIPT_A, source, payload)
	_expect(failures, writer.flush(user_path, FAULT_NONE, version) == OK, "user:// path with spaces and café must flush")
	var user_bytes := FileAccess.get_file_as_bytes(user_path)

	var absolute := ProjectSettings.globalize_path(user_path)
	var abs_writer := BaristaScriptParseCache.new()
	abs_writer.put(SCRIPT_A, source, payload)
	_expect(failures, abs_writer.flush(absolute, FAULT_NONE, version) == OK, "absolute path flush must succeed")
	_expect(failures, FileAccess.get_file_as_bytes(absolute) == user_bytes, "absolute and user:// stores must match")

	var relative := "cache_test_relative_café.bpc"
	_remove(relative)
	var rel_writer := BaristaScriptParseCache.new()
	rel_writer.put(SCRIPT_A, source, payload)
	var rel_err := rel_writer.flush(relative, FAULT_NONE, version)
	_expect(failures, rel_err == OK, "relative path flush must succeed when CWD is writable")
	if rel_err == OK:
		_expect(failures, FileAccess.get_file_as_bytes(relative) == user_bytes, "relative flush must match user:// bytes")
		_remove(relative)

	var res_path := "res://tests/cache_test_scratch_res_café.bpc"
	_remove(res_path)
	var res_writer := BaristaScriptParseCache.new()
	res_writer.put(SCRIPT_A, source, payload)
	var res_err := res_writer.flush(res_path, FAULT_NONE, version)
	if res_err == OK:
		_expect(failures, FileAccess.get_file_as_bytes(res_path) == user_bytes, "writable res:// flush must match user:// bytes")
		_remove(res_path)
	# Read-only packaged res:// is an environment limit, not a promotion-contract failure.

	if OS.get_name() == "Windows":
		var long_dir := "user://cache_test_scratch/%s/%s/%s" % ["L".repeat(40), "M".repeat(40), "N".repeat(40)]
		DirAccess.make_dir_recursive_absolute(long_dir)
		var long_path := "%s/%s.bpc" % [long_dir, "O".repeat(40)]
		_expect(failures, ProjectSettings.globalize_path(long_path).length() > 260, "Windows long-path fixture must exceed 260 characters once globalized")
		var long_writer := BaristaScriptParseCache.new()
		long_writer.put(SCRIPT_A, source, payload)
		_expect(failures, long_writer.flush(long_path, FAULT_NONE, version) == OK, "Windows long path flush must succeed")
		_expect(failures, FileAccess.get_file_as_bytes(long_path) == user_bytes, "Windows long path bytes must match")


func _test_malformed_paths_reject_before_temp_creation(failures: Array[String]) -> void:
	var cache := BaristaScriptParseCache.new()
	cache.put(SCRIPT_A, _read_text(SCRIPT_A), _payload(_read_text(SCRIPT_A)))
	var version := BaristaScriptParseCache.get_cache_format_version()

	for path in ["", "uid://not-a-store", "http://example.invalid/store.bpc"]:
		var before_temps := _count_tmp_under(SCRATCH_ROOT)
		var err := cache.flush(path, FAULT_NONE, version)
		_expect(failures, err == ERR_INVALID_PARAMETER, "malformed path '%s' must return ERR_INVALID_PARAMETER, got %s" % [path, err])
		_expect(failures, _count_tmp_under(SCRATCH_ROOT) == before_temps, "malformed path '%s' must not create a temp" % path)

	# Godot's String layer replaces or truncates embedded NUL (chr(0) becomes U+FFFD;
	# get_string_from_utf8 stops at NUL), so GDScript cannot deliver a NUL path to the
	# extension. C++ `bs_validate_parse_cache_store_path` still rejects NUL; covered by the
	# native cache suite's path-validation case.
	_expect(failures, cache.has_entry(SCRIPT_A), "memory must remain usable after malformed path rejection")

func _test_faults_one_through_three_still_preserve_previous_store(failures: Array[String]) -> void:
	var source_a := _read_text(SCRIPT_A)
	var source_b := _read_text(SCRIPT_B)
	var version := BaristaScriptParseCache.get_cache_format_version()

	for fault in [FAULT_BEFORE_WRITE, FAULT_AFTER_WRITE_BEFORE_RENAME, FAULT_TRUNCATE_TEMP_AFTER_WRITE]:
		var store := _scratch("fault_%d.bpc" % fault)
		_remove(store)
		for leftover in _temp_files_for(store):
			DirAccess.remove_absolute(leftover)

		var first := BaristaScriptParseCache.new()
		first.put(SCRIPT_A, source_a, _payload(source_a))
		_expect(failures, first.flush(store, FAULT_NONE, version) == OK, "baseline before fault %d must succeed" % fault)
		var previous := FileAccess.get_file_as_bytes(store)

		var second := BaristaScriptParseCache.new()
		second.put(SCRIPT_B, source_b, _payload(source_b))
		_expect(failures, second.flush(store, fault, version) != OK, "fault %d must fail" % fault)
		_expect(failures, FileAccess.get_file_as_bytes(store) == previous, "fault %d must preserve previous bytes" % fault)
		_expect(failures, second.has_entry(SCRIPT_B), "fault %d must keep memory entries" % fault)

		if fault == FAULT_AFTER_WRITE_BEFORE_RENAME:
			_expect(failures, not _temp_files_for(store).is_empty(), "fault 2 must intentionally leave its complete temp")
			for leftover in _temp_files_for(store):
				DirAccess.remove_absolute(leftover)
		else:
			_expect(failures, _temp_files_for(store).is_empty(), "fault %d must not leave a temp" % fault)


func _test_golden_and_damaged_store_contracts(failures: Array[String]) -> void:
	var source_a := _read_text(SCRIPT_A)
	var source_b := _read_text(SCRIPT_B)

	var golden := BaristaScriptParseCache.new()
	_expect(failures, golden.load(GOLDEN_STORE) == COLD, "golden store must load without damage")
	_expect(failures, golden.get_entry_count() == 2, "golden store holds two entries")
	_expect(failures, golden.lookup(SCRIPT_A, source_a)["hit"], "golden must hit for script A")
	_expect(failures, golden.lookup(SCRIPT_B, source_b)["hit"], "golden must hit for script B")

	var rewritten := _scratch("golden_roundtrip.bpc")
	_expect(failures, golden.flush(rewritten, FAULT_NONE, BaristaScriptParseCache.get_cache_format_version()) == OK, "golden rewrite must succeed")
	_expect(failures, FileAccess.get_file_as_bytes(rewritten) == FileAccess.get_file_as_bytes(GOLDEN_STORE), "golden rewrite must be byte-identical")

	_expect_damaged(failures, TRUNCATED_STORE, "truncated")
	_expect_damaged(failures, CORRUPT_STORE, "corrupt")
	_expect_damaged(failures, BAD_MAGIC_STORE, "bad magic")
	_expect_damaged(failures, DUPLICATE_KEY_STORE, "duplicate key")

	var versioned := BaristaScriptParseCache.new()
	versioned.load(VERSION_MISMATCH_STORE)
	var mismatch := versioned.lookup(SCRIPT_A, source_a)
	_expect(failures, not mismatch["hit"], "version-mismatched entry must not hit")
	_expect(failures, mismatch["reason"] == VERSION_MISMATCH, "version-mismatched entry must report VERSION_MISMATCH")

	var stale := BaristaScriptParseCache.new()
	stale.load(GOLDEN_STORE)
	var digest := stale.lookup(SCRIPT_A, source_a + "\n# edited\n")
	_expect(failures, not digest["hit"], "digest mismatch must not hit")
	_expect(failures, digest["reason"] == DIGEST_MISMATCH, "digest mismatch must report DIGEST_MISMATCH")


func _expect_damaged(failures: Array[String], store: String, label: String) -> void:
	var cache := BaristaScriptParseCache.new()
	_expect(failures, cache.load(store) == CORRUPT, "%s store must load as CORRUPT" % label)
	_expect(failures, cache.get_entry_count() == 0, "%s store must yield no entries" % label)
	var result := cache.lookup(SCRIPT_A, _read_text(SCRIPT_A))
	_expect(failures, not result["hit"], "%s store must never serve an entry" % label)
	_expect(failures, result["reason"] == CORRUPT, "%s lookup must report CORRUPT rather than COLD" % label)


func _payload(source: String) -> PackedByteArray:
	var lines := source.split("\n")
	var summary := PackedStringArray()
	for index in lines.size():
		var line: String = lines[index].strip_edges()
		if line.is_empty() or line.begins_with("#"):
			continue
		summary.append("%d:%s" % [index, line])
	return "\n".join(summary).to_utf8_buffer()


func _read_text(path: String) -> String:
	return FileAccess.get_file_as_bytes(path).get_string_from_utf8()


func _scratch(name: String) -> String:
	DirAccess.make_dir_recursive_absolute(SCRATCH_ROOT)
	return "%s/%s" % [SCRATCH_ROOT, name]


func _write_bytes(path: String, bytes: PackedByteArray) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		push_error("could not write %s" % path)
		return
	file.store_buffer(bytes)
	file.close()


func _remove(path: String) -> void:
	if FileAccess.file_exists(path):
		DirAccess.remove_absolute(path)


func _temp_files_for(store: String) -> PackedStringArray:
	var found := PackedStringArray()
	var directory := store.get_base_dir()
	if directory.is_empty():
		directory = "."
	var listing := DirAccess.get_files_at(directory)
	var prefix := store.get_file()
	for name in listing:
		if name.begins_with(prefix) and name.ends_with(".tmp"):
			found.append("%s/%s" % [directory, name])
	return found


func _count_tmp_under(root: String) -> int:
	var count := 0
	var listing := DirAccess.get_files_at(root)
	for name in listing:
		if name.ends_with(".tmp"):
			count += 1
	return count


func _expect(failures: Array[String], condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
