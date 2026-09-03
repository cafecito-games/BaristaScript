# cache_test.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT
extends SceneTree

## Fail-closed contract tests for the parse cache (src/bs_cache.{h,cpp}), driven
## through the BaristaScriptParseCache handle.
##
## The cache is engine-backed C++ -- its String and FileAccess dependencies only
## exist inside a loaded Godot runtime -- so this is the only place it can be
## exercised. Every row of issue #8's fail-closed table has a named test here,
## and every binary fixture under cache_fixtures/ was produced by this build's
## own writer rather than typed by hand.
##
## Regenerate the fixtures with:
##   godot --headless --path project --script res://tests/cache_test.gd -- --regenerate

const FIXTURES_ROOT := "res://tests/cache_fixtures"
const SCRIPT_A := "%s/script_a.barista" % FIXTURES_ROOT
const SCRIPT_B := "%s/script_b.barista" % FIXTURES_ROOT

const GOLDEN_STORE := "%s/golden_store.bin" % FIXTURES_ROOT
const VERSION_MISMATCH_STORE := "%s/version_mismatch_store.bin" % FIXTURES_ROOT
const TRUNCATED_STORE := "%s/truncated_store.bin" % FIXTURES_ROOT
const CORRUPT_STORE := "%s/corrupt_store.bin" % FIXTURES_ROOT
const BAD_MAGIC_STORE := "%s/bad_magic_store.bin" % FIXTURES_ROOT
const DUPLICATE_KEY_STORE := "%s/duplicate_key_store.bin" % FIXTURES_ROOT
const DUPLICATE_KEY_ACROSS_VERSIONS_STORE := "%s/duplicate_key_across_versions_store.bin" % FIXTURES_ROOT

const SCRATCH_ROOT := "user://cache_test"

## Printed only when every assertion passed. CI greps for it because a GDScript
## parse error exits 0 and would otherwise read as a pass.
const SUCCESS_SENTINEL := "BS_PARSE_CACHE_OK"
const TEST_COUNT := 24

# Mirrors the BSMissReason enum. get_miss_reason_names() is the single source of
# truth; these constants only give the assertions readable names, and
# _test_miss_reason_vocabulary_is_closed proves they still agree.
const COLD := 0
const VERSION_MISMATCH := 1
const DIGEST_MISMATCH := 2
const CORRUPT := 3
const EVICTED := 4

# A byte well inside the first record of a two-entry store: past the 8-byte
# header, the version tag and the key length, so flipping it damages the key or
# the digest either way and the record's checksum must reject it.
const CORRUPTION_OFFSET := 20


func _initialize() -> void:
	if OS.get_cmdline_user_args().has("--regenerate"):
		quit(_regenerate())
		return

	var failures: Array[String] = []

	# The store-level verdicts.
	_test_absent_store_is_a_silent_cold_cache(failures)
	_test_golden_store_from_this_build_loads(failures)
	_test_golden_store_round_trips_byte_identically(failures)
	_test_truncated_store_is_corrupt_never_absent(failures)
	_test_corrupt_store_is_corrupt_never_absent(failures)
	_test_bad_magic_store_is_corrupt_never_absent(failures)
	_test_trailing_bytes_are_corrupt_never_absent(failures)
	_test_duplicate_key_fails_loudly(failures)
	_test_duplicate_key_fails_loudly_across_versions(failures)
	_test_key_with_an_embedded_nul_is_corrupt(failures)

	# The per-entry verdicts.
	_test_version_mismatch_is_discarded_never_upgraded(failures)
	_test_digest_mismatch_is_discarded(failures)
	_test_entry_for_deleted_file_is_evicted(failures)
	_test_eviction_sweep_reports_every_deleted_file(failures)

	# Integrity and idempotency.
	_test_warm_lookup_equals_cold_parse(failures)
	_test_digest_is_semantic_and_path_independent(failures)
	_test_digest_ignores_file_modification_time(failures)
	_test_write_failure_is_logged_and_non_fatal(failures)
	_test_atomic_write_leaves_the_previous_store_intact(failures)
	_test_deferred_write_failure_never_replaces_the_previous_store(failures)

	# Vocabulary closure.
	_test_miss_reason_vocabulary_is_closed(failures)
	_test_every_miss_reason_has_a_distinct_log_line(failures)

	# The in-memory layer ported from Foundry's FSCache.
	_test_source_override_shadows_the_file_on_disk(failures)
	_test_dependency_edges_are_recorded_and_removed(failures)

	for failure in failures:
		push_error(failure)
	# A GDScript parse error makes SceneTree quit 0, so CI greps for this sentinel
	# rather than trusting the exit code: it can only be printed by a suite that
	# actually loaded and ran.
	if failures.is_empty():
		print("%s %d test groups passed" % [SUCCESS_SENTINEL, TEST_COUNT])
	quit(0 if failures.is_empty() else 1)


# ---------------------------------------------------------------------------
# Store-level verdicts
# ---------------------------------------------------------------------------

## "Cache file absent" is the only condition that may be silently absent: no
## report line, and a lookup that reads back as COLD rather than as damage.
func _test_absent_store_is_a_silent_cold_cache(failures: Array[String]) -> void:
	var store := _scratch_path("absent.bin")
	_remove(store)
	var cache := BaristaScriptParseCache.new()

	_expect(failures, cache.load(store) == COLD, "an absent store must load as COLD")
	_expect(failures, cache.get_load_report().is_empty(), "a cold load must report nothing: %s" % cache.get_load_report())
	_expect(failures, cache.get_entry_count() == 0, "a cold load must produce no entries")

	var result := cache.lookup(SCRIPT_A, _read_source(SCRIPT_A))
	_expect(failures, not result["hit"], "a cold cache must not hit")
	_expect(failures, result["reason"] == COLD, "a cold miss must read as COLD, got %s" % result["reason_name"])


## Acceptance criterion 2: a store this build's own writer produced is checked
## in, and the loader accepts it byte for byte.
func _test_golden_store_from_this_build_loads(failures: Array[String]) -> void:
	var cache := BaristaScriptParseCache.new()
	_expect(failures, cache.load(GOLDEN_STORE) == COLD, "the checked-in golden store must load without damage")
	_expect(failures, cache.get_load_report().is_empty(), "a healthy load must report nothing: %s" % cache.get_load_report())
	_expect(failures, cache.get_entry_count() == 2, "the golden store holds two entries, got %d" % cache.get_entry_count())

	for path in [SCRIPT_A, SCRIPT_B]:
		var source := _read_source(path)
		var result := cache.lookup(path, source)
		_expect(failures, result["hit"], "the golden store must hit for %s (%s)" % [path, result["reason_name"]])
		_expect(
			failures,
			result["payload"] == _parse(source),
			"the golden payload for %s must equal a cold parse of its source" % path
		)


## Idempotency: the same entry set flushed again is byte-identical to the
## checked-in store, so the record layout has exactly one encoding.
func _test_golden_store_round_trips_byte_identically(failures: Array[String]) -> void:
	var rewritten := _scratch_path("rewritten.bin")
	_remove(rewritten)
	var error := _populated_cache().flush(rewritten, 0, BaristaScriptParseCache.get_cache_format_version())
	_expect(failures, error == OK, "flushing the fixture entry set must succeed, got error %d" % error)

	var expected := FileAccess.get_file_as_bytes(GOLDEN_STORE)
	var actual := FileAccess.get_file_as_bytes(rewritten)
	_expect(
		failures,
		actual == expected,
		"a re-flush must reproduce the golden store byte for byte (%d vs %d bytes)" % [actual.size(), expected.size()]
	)


func _test_truncated_store_is_corrupt_never_absent(failures: Array[String]) -> void:
	_expect_damaged_store(failures, TRUNCATED_STORE, "truncated or corrupt at byte", "truncated store")


func _test_corrupt_store_is_corrupt_never_absent(failures: Array[String]) -> void:
	_expect_damaged_store(failures, CORRUPT_STORE, "truncated or corrupt at byte", "checksum-failing store")


func _test_bad_magic_store_is_corrupt_never_absent(failures: Array[String]) -> void:
	_expect_damaged_store(failures, BAD_MAGIC_STORE, "is not a BaristaScript cache", "store with a clobbered magic")


## A store whose bytes outrun its declared entry count was not written by this
## build's writer, so it is damage rather than a store with a healthy prefix.
func _test_trailing_bytes_are_corrupt_never_absent(failures: Array[String]) -> void:
	var store := _scratch_path("trailing.bin")
	var bytes := FileAccess.get_file_as_bytes(GOLDEN_STORE)
	bytes.append(0x7F)
	_write_bytes(store, bytes)
	_expect_damaged_store(failures, store, "trailing bytes after its declared", "store with trailing bytes")


## Two entries claiming one key means the writer is broken, so the whole store
## goes rather than one of them winning silently.
func _test_duplicate_key_fails_loudly(failures: Array[String]) -> void:
	var cache := BaristaScriptParseCache.new()
	_expect(failures, cache.load(DUPLICATE_KEY_STORE) == CORRUPT, "a duplicate key must make the store CORRUPT")
	_expect(failures, cache.get_entry_count() == 0, "a duplicate key must discard every entry, kept %d" % cache.get_entry_count())
	_expect(
		failures,
		_report_contains(cache, "duplicate cache key '%s'" % SCRIPT_A),
		"a duplicate key must be reported by name: %s" % cache.get_load_report()
	)
	_expect(
		failures,
		_report_contains(cache, "the cache writer is broken"),
		"a duplicate key must be reported as a writer bug: %s" % cache.get_load_report()
	)


## A duplicate must not be able to hide behind a stale version tag: the second
## record claiming the key is rejected as a writer bug before its version is
## even consulted, so the store is discarded rather than quietly deduplicated.
func _test_duplicate_key_fails_loudly_across_versions(failures: Array[String]) -> void:
	var cache := BaristaScriptParseCache.new()
	_expect(
		failures,
		cache.load(DUPLICATE_KEY_ACROSS_VERSIONS_STORE) == CORRUPT,
		"a duplicate key whose second record is version-stale must still make the store CORRUPT"
	)
	_expect(failures, cache.get_entry_count() == 0, "a duplicate key must discard every entry, kept %d" % cache.get_entry_count())
	_expect(
		failures,
		_report_contains(cache, "duplicate cache key '%s'" % SCRIPT_A),
		"the duplicate must be reported as a duplicate, not as a version mismatch: %s" % cache.get_load_report()
	)
	_expect(
		failures,
		cache.lookup(SCRIPT_A, _read_source(SCRIPT_A))["reason"] == CORRUPT,
		"a lookup against a duplicate-carrying store must report CORRUPT"
	)


## A key is only valid if its bytes survive a full round trip. String decoding
## stops at an embedded NUL and returns the prefix, so without the re-encode
## check this record would be filed under SCRIPT_A -- a path that really exists
## -- and served as a hit for it.
##
## The record is assembled here because the writer will never emit one, but its
## checksum comes from the cache's own compute_entry_checksum: the store is
## structurally perfect, and the loader has to reject the key on its own merits
## rather than on damage it can see in the digest.
func _test_key_with_an_embedded_nul_is_corrupt(failures: Array[String]) -> void:
	var store := _scratch_path("nul_key.bin")
	var source := _read_source(SCRIPT_A)
	var payload := _parse(source)

	var key := SCRIPT_A.to_utf8_buffer()
	key.append(0)
	key.append_array("/shadow".to_utf8_buffer())

	var record := _u32(BaristaScriptParseCache.get_cache_format_version())
	record.append_array(_u32(key.size()))
	record.append_array(key)
	record.append_array(_u64(BaristaScriptParseCache.compute_source_digest(source)))
	record.append_array(_u32(payload.size()))
	record.append_array(payload)
	record.append_array(_u64(BaristaScriptParseCache.compute_entry_checksum(record)))

	var bytes := "BSPC".to_utf8_buffer()
	bytes.append_array(_u32(1))
	bytes.append_array(record)
	_write_bytes(store, bytes)

	var cache := BaristaScriptParseCache.new()
	_expect(failures, cache.load(store) == CORRUPT, "a key with an embedded NUL must make the store CORRUPT")
	_expect(failures, cache.get_entry_count() == 0, "a NUL-truncated key must not become an entry")
	_expect(failures, not cache.has_entry(SCRIPT_A), "a NUL-truncated key must not be filed under the path it truncates to")
	_expect(
		failures,
		not cache.lookup(SCRIPT_A, source)["hit"],
		"a NUL-truncated key must never be served as a hit for the path it truncates to"
	)


# ---------------------------------------------------------------------------
# Per-entry verdicts
# ---------------------------------------------------------------------------

## A record written under another version tag is rejected outright. No
## best-effort upgrade, and the rejection is a tombstone so the path can never
## read back as a cold miss.
func _test_version_mismatch_is_discarded_never_upgraded(failures: Array[String]) -> void:
	var cache := BaristaScriptParseCache.new()
	cache.load(VERSION_MISMATCH_STORE)
	_expect(failures, cache.get_entry_count() == 0, "version-tagged-wrong records must not become entries")
	_expect(failures, not cache.has_entry(SCRIPT_A), "a version-rejected record must not be retrievable")

	var result := cache.lookup(SCRIPT_A, _read_source(SCRIPT_A))
	_expect(failures, not result["hit"], "a version-rejected record must never be served")
	_expect(
		failures,
		result["reason"] == VERSION_MISMATCH,
		"a version-rejected record must report VERSION_MISMATCH, got %s" % result["reason_name"]
	)
	_expect(
		failures,
		_report_contains(cache, "no upgrade is attempted"),
		"a version rejection must be logged: %s" % cache.get_load_report()
	)


## The digest is checked against the source now on disk, so an entry written for
## other bytes is discarded and the path stays tombstoned as a damaged entry.
func _test_digest_mismatch_is_discarded(failures: Array[String]) -> void:
	var cache := BaristaScriptParseCache.new()
	cache.load(GOLDEN_STORE)

	var result := cache.lookup(SCRIPT_A, "%s\n# edited since the entry was written\n" % _read_source(SCRIPT_A))
	_expect(failures, not result["hit"], "a stale entry must never be served")
	_expect(
		failures,
		result["reason"] == DIGEST_MISMATCH,
		"a stale entry must report DIGEST_MISMATCH, got %s" % result["reason_name"]
	)
	_expect(failures, not cache.has_entry(SCRIPT_A), "a stale entry must be discarded, not left in place")

	# The entry is gone, but the path is not therefore cold: the bytes existed
	# and were refused, and a second lookup must keep saying so.
	var again := cache.lookup(SCRIPT_A, _read_source(SCRIPT_A))
	_expect(
		failures,
		again["reason"] == DIGEST_MISMATCH,
		"a discarded entry must not read back as a cold miss, got %s" % again["reason_name"]
	)


func _test_entry_for_deleted_file_is_evicted(failures: Array[String]) -> void:
	var missing := _scratch_path("deleted.barista")
	var source := "func gone():\n\treturn 1\n"
	_write_bytes(missing, source.to_utf8_buffer())

	var cache := BaristaScriptParseCache.new()
	cache.put(missing, source, _parse(source))
	_expect(failures, cache.lookup(missing, source)["hit"], "the entry must hit while its file exists")

	_remove(missing)
	var result := cache.lookup(missing, source)
	_expect(failures, not result["hit"], "an entry for a deleted file must not be served")
	_expect(failures, result["reason"] == EVICTED, "a deleted file must report EVICTED, got %s" % result["reason_name"])
	_expect(failures, not cache.has_entry(missing), "an evicted entry must be gone")


func _test_eviction_sweep_reports_every_deleted_file(failures: Array[String]) -> void:
	var missing := _scratch_path("swept.barista")
	var source := "func swept():\n\treturn 2\n"
	_write_bytes(missing, source.to_utf8_buffer())

	var cache := _populated_cache()
	cache.put(missing, source, _parse(source))
	_remove(missing)

	var evicted := cache.evict_entries_with_missing_files()
	_expect(failures, evicted == PackedStringArray([missing]), "the sweep must name exactly the deleted file: %s" % evicted)
	_expect(failures, cache.get_entry_count() == 2, "the sweep must keep the entries whose files still exist")
	_expect(
		failures,
		cache.lookup(missing, source)["reason"] == EVICTED,
		"a swept path must stay tombstoned as EVICTED rather than reading cold"
	)


# ---------------------------------------------------------------------------
# Integrity and idempotency
# ---------------------------------------------------------------------------

## Acceptance criterion 4: the whole point of the cache. A warm lookup on the
## non-trivial fixture must produce exactly the bytes a cold parse produces.
func _test_warm_lookup_equals_cold_parse(failures: Array[String]) -> void:
	var source := _read_source(SCRIPT_B)
	var cold := _parse(source)

	var store := _scratch_path("warm.bin")
	_remove(store)
	var writer := BaristaScriptParseCache.new()
	writer.put(SCRIPT_B, source, cold)
	_expect(failures, writer.flush(store, 0, BaristaScriptParseCache.get_cache_format_version()) == OK, "the warm store must flush")

	var reader := BaristaScriptParseCache.new()
	reader.load(store)
	var warm := reader.lookup(SCRIPT_B, source)
	_expect(failures, warm["hit"], "a store this build wrote must hit on reload (%s)" % warm["reason_name"])
	_expect(failures, warm["payload"] == cold, "a warm payload must be byte-identical to a cold parse")
	_expect(failures, not cold.is_empty(), "the fixture must be non-trivial enough for the comparison to mean something")


## The digest covers source bytes and the version tag and nothing else, so the
## same source in two checkouts -- at two different paths -- digests the same.
func _test_digest_is_semantic_and_path_independent(failures: Array[String]) -> void:
	var source := _read_source(SCRIPT_B)
	var digest: int = BaristaScriptParseCache.compute_source_digest(source)

	_expect(failures, BaristaScriptParseCache.compute_source_digest(source) == digest, "the digest must be deterministic")
	_expect(
		failures,
		BaristaScriptParseCache.compute_source_digest("%s " % source) != digest,
		"a one-byte source change must change the digest"
	)

	# Two stores at different paths holding the same source: the entry written
	# by one is accepted by the other, which is only true if no absolute path
	# leaked into the digest.
	var checkout_one := _scratch_path("checkout_one.bin")
	var checkout_two := _scratch_path("checkout_two.bin")
	_remove(checkout_one)
	_remove(checkout_two)
	var writer := BaristaScriptParseCache.new()
	writer.put(SCRIPT_B, source, _parse(source))
	writer.flush(checkout_one, 0, BaristaScriptParseCache.get_cache_format_version())
	writer.flush(checkout_two, 0, BaristaScriptParseCache.get_cache_format_version())
	_expect(
		failures,
		FileAccess.get_file_as_bytes(checkout_one) == FileAccess.get_file_as_bytes(checkout_two),
		"the same entry set must serialize identically regardless of the store's own path"
	)


## mtime alone is not a correctness signal: rewriting a file with identical
## bytes must still hit, and changing its bytes must still miss.
func _test_digest_ignores_file_modification_time(failures: Array[String]) -> void:
	var script := _scratch_path("touched.barista")
	var source := "func touched():\n\treturn 3\n"
	_write_bytes(script, source.to_utf8_buffer())

	var cache := BaristaScriptParseCache.new()
	cache.put(script, source, _parse(source))

	# Rewritten byte-for-byte, so only the modification time moved.
	_write_bytes(script, source.to_utf8_buffer())
	_expect(failures, cache.lookup(script, source)["hit"], "a rewrite with identical bytes must still hit")

	var edited := "%s# edited\n" % source
	_write_bytes(script, edited.to_utf8_buffer())
	_expect(
		failures,
		cache.lookup(script, edited)["reason"] == DIGEST_MISMATCH,
		"changed bytes must miss even though the entry is otherwise current"
	)


## A failed write is reported and non-fatal: nothing on disk changes and the
## in-memory entries a caller just parsed are still usable.
func _test_write_failure_is_logged_and_non_fatal(failures: Array[String]) -> void:
	var store := _scratch_path("write_failure.bin")
	_remove(store)
	var cache := _populated_cache()
	_expect(failures, cache.flush(store, 0, BaristaScriptParseCache.get_cache_format_version()) == OK, "the baseline store must flush")
	var before := FileAccess.get_file_as_bytes(store)

	# 1 == BSParseCache::WriteFault::BEFORE_WRITE.
	var error := cache.flush(store, 1, BaristaScriptParseCache.get_cache_format_version())
	_expect(failures, error != OK, "a failed flush must return an error rather than reporting success")
	_expect(failures, FileAccess.get_file_as_bytes(store) == before, "a failed flush must leave the store untouched")
	_expect(
		failures,
		cache.lookup(SCRIPT_A, _read_source(SCRIPT_A))["hit"],
		"a failed flush must not cost the caller the entries it already has"
	)


## Acceptance criterion 5: a fault between write and rename leaves the previous
## store intact, never a half-written one under the real name.
func _test_atomic_write_leaves_the_previous_store_intact(failures: Array[String]) -> void:
	var store := _scratch_path("atomic.bin")
	_remove(store)
	_remove("%s.tmp" % store)

	var first_source := _read_source(SCRIPT_A)
	var first := BaristaScriptParseCache.new()
	first.put(SCRIPT_A, first_source, _parse(first_source))
	_expect(failures, first.flush(store, 0, BaristaScriptParseCache.get_cache_format_version()) == OK, "the first store must flush")
	var previous := FileAccess.get_file_as_bytes(store)

	var second_source := _read_source(SCRIPT_B)
	var second := BaristaScriptParseCache.new()
	second.put(SCRIPT_B, second_source, _parse(second_source))
	# 2 == BSParseCache::WriteFault::AFTER_WRITE_BEFORE_RENAME.
	_expect(
		failures,
		second.flush(store, 2, BaristaScriptParseCache.get_cache_format_version()) != OK,
		"a fault between write and rename must be reported"
	)
	_expect(failures, FileAccess.get_file_as_bytes(store) == previous, "the previous store must survive the interrupted write byte for byte")

	var reader := BaristaScriptParseCache.new()
	_expect(failures, reader.load(store) == COLD, "the surviving store must still load cleanly")
	_expect(failures, reader.lookup(SCRIPT_A, first_source)["hit"], "the previous entry must still be served")
	_expect(failures, not reader.has_entry(SCRIPT_B), "the interrupted write's entry must not be visible")


## store_buffer reports what the buffer accepted, not what reached the disk: a
## full volume surfaces at flush or close. The read-back before the rename is
## what keeps such a write from being promoted over a healthy store.
func _test_deferred_write_failure_never_replaces_the_previous_store(failures: Array[String]) -> void:
	var store := _scratch_path("deferred.bin")
	_remove(store)
	_remove("%s.tmp" % store)

	var first_source := _read_source(SCRIPT_A)
	var first := BaristaScriptParseCache.new()
	first.put(SCRIPT_A, first_source, _parse(first_source))
	_expect(failures, first.flush(store, 0, BaristaScriptParseCache.get_cache_format_version()) == OK, "the baseline store must flush")
	var previous := FileAccess.get_file_as_bytes(store)

	# 3 == BSParseCache::WriteFault::TRUNCATE_TEMP_AFTER_WRITE: the temp file is
	# shortened after close, exactly as a deferred I/O failure would leave it.
	var second_source := _read_source(SCRIPT_B)
	var second := BaristaScriptParseCache.new()
	second.put(SCRIPT_B, second_source, _parse(second_source))
	_expect(
		failures,
		second.flush(store, 3, BaristaScriptParseCache.get_cache_format_version()) != OK,
		"a short write must be reported rather than promoted"
	)
	_expect(failures, FileAccess.get_file_as_bytes(store) == previous, "a short write must leave the previous store byte for byte")
	_expect(failures, not FileAccess.file_exists("%s.tmp" % store), "a refused flush must not leave its temp file behind")

	var reader := BaristaScriptParseCache.new()
	_expect(failures, reader.load(store) == COLD, "the surviving store must still load cleanly")
	_expect(failures, reader.lookup(SCRIPT_A, first_source)["hit"], "the previous entry must still be served")


# ---------------------------------------------------------------------------
# Vocabulary closure
# ---------------------------------------------------------------------------

## Acceptance criterion 6: the parametrized test over every miss-reason
## enumerator. It iterates the vocabulary rather than a list written here, so a
## new enumerator without a consumer fails this test instead of falling through.
func _test_miss_reason_vocabulary_is_closed(failures: Array[String]) -> void:
	var names := BaristaScriptParseCache.get_miss_reason_names()
	_expect(
		failures,
		names == PackedStringArray(["COLD", "VERSION_MISMATCH", "DIGEST_MISMATCH", "CORRUPT", "EVICTED"]),
		"the miss-reason vocabulary changed without this test being updated: %s" % names
	)

	for index in names.size():
		_expect(
			failures,
			BaristaScriptParseCache.get_miss_reason_name(index) == names[index],
			"miss reason %d must round-trip through its index" % index
		)

	# Every enumerator this suite asserts against must be one the cache knows.
	for pair in [["COLD", COLD], ["VERSION_MISMATCH", VERSION_MISMATCH], ["DIGEST_MISMATCH", DIGEST_MISMATCH], ["CORRUPT", CORRUPT], ["EVICTED", EVICTED]]:
		_expect(
			failures,
			names[pair[1]] == pair[0],
			"this test's %s constant no longer names index %d" % [pair[0], pair[1]]
		)


## Every enumerator gets its own log line naming the path, so a rejection can
## never be mistaken for a cold miss in a log.
func _test_every_miss_reason_has_a_distinct_log_line(failures: Array[String]) -> void:
	var seen := {}
	for index in BaristaScriptParseCache.get_miss_reason_names().size():
		var line := BaristaScriptParseCache.get_miss_reason_log_line(index, SCRIPT_A)
		var name := BaristaScriptParseCache.get_miss_reason_name(index)
		_expect(failures, not line.is_empty(), "%s must have a log line" % name)
		_expect(failures, line.contains(SCRIPT_A), "%s's log line must name the script path: %s" % [name, line])
		_expect(failures, not seen.has(line), "%s reuses another reason's log line: %s" % [name, line])
		seen[line] = true

	# COLD is the only reason that describes absence; every other line must read
	# as damage so "malformed" never looks like "absent" in a log.
	_expect(
		failures,
		BaristaScriptParseCache.get_miss_reason_log_line(COLD, SCRIPT_A).begins_with("cold cache"),
		"COLD's log line must say the cache was cold"
	)
	for index in [VERSION_MISMATCH, DIGEST_MISMATCH, CORRUPT, EVICTED]:
		var line := BaristaScriptParseCache.get_miss_reason_log_line(index, SCRIPT_A)
		_expect(
			failures,
			not line.contains("cold"),
			"%s must not read as a cold miss: %s" % [BaristaScriptParseCache.get_miss_reason_name(index), line]
		)


# ---------------------------------------------------------------------------
# The in-memory layer (BSCache, ported from Foundry's FSCache)
# ---------------------------------------------------------------------------

func _test_source_override_shadows_the_file_on_disk(failures: Array[String]) -> void:
	BaristaScriptParseCache.clear_source_overrides()
	var on_disk := _read_source(SCRIPT_A)
	_expect(failures, BaristaScriptParseCache.get_source_code(SCRIPT_A) == on_disk, "with no override, the file on disk is the source")

	var edited := "%s# unsaved edit\n" % on_disk
	BaristaScriptParseCache.set_source_override(SCRIPT_A, edited)
	_expect(failures, BaristaScriptParseCache.has_source_override(SCRIPT_A), "the override must be recorded")
	_expect(failures, BaristaScriptParseCache.get_source_code(SCRIPT_A) == edited, "an override must shadow the file on disk")

	BaristaScriptParseCache.clear_source_override(SCRIPT_A)
	_expect(failures, not BaristaScriptParseCache.has_source_override(SCRIPT_A), "the override must clear")
	_expect(failures, BaristaScriptParseCache.get_source_code(SCRIPT_A) == on_disk, "clearing an override must restore the file on disk")


func _test_dependency_edges_are_recorded_and_removed(failures: Array[String]) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.record_dependency(SCRIPT_A, SCRIPT_B)
	_expect(
		failures,
		BaristaScriptParseCache.get_inverse_dependencies(SCRIPT_A) == PackedStringArray([SCRIPT_B]),
		"an owner must show up in its dependency's inverse edges: %s" % BaristaScriptParseCache.get_inverse_dependencies(SCRIPT_A)
	)

	# A self-edge is not a dependency and must not be recorded.
	BaristaScriptParseCache.record_dependency(SCRIPT_A, SCRIPT_A)
	_expect(
		failures,
		BaristaScriptParseCache.get_inverse_dependencies(SCRIPT_A) == PackedStringArray([SCRIPT_B]),
		"a self-dependency must be ignored"
	)

	BaristaScriptParseCache.remove_script(SCRIPT_B)
	_expect(
		failures,
		BaristaScriptParseCache.get_inverse_dependencies(SCRIPT_A).is_empty(),
		"removing the owner must drop the inverse edge it created"
	)

	BaristaScriptParseCache.record_dependency(SCRIPT_A, SCRIPT_B)
	BaristaScriptParseCache.move_script(SCRIPT_B, "%s/moved.barista" % FIXTURES_ROOT)
	_expect(
		failures,
		BaristaScriptParseCache.get_inverse_dependencies(SCRIPT_A).is_empty(),
		"a moved script must not leave edges under its old path"
	)
	BaristaScriptParseCache.clear_script_cache()


# ---------------------------------------------------------------------------
# Fixture regeneration
# ---------------------------------------------------------------------------

## Rewrites every binary fixture from this build's own writer. Nothing here is
## typed by hand: the damaged variants are byte edits of real writer output, so
## the fixtures cannot drift into describing a format the writer never emits.
func _regenerate() -> int:
	var version := BaristaScriptParseCache.get_cache_format_version()
	var scratch := _scratch_path("regenerate.bin")

	_remove(scratch)
	if _populated_cache().flush(scratch, 0, version) != OK:
		push_error("could not flush the golden store")
		return 1
	var golden := FileAccess.get_file_as_bytes(scratch)
	_write_bytes(GOLDEN_STORE, golden)

	_remove(scratch)
	if _populated_cache().flush(scratch, 0, version + 1) != OK:
		push_error("could not flush the version-mismatch store")
		return 1
	_write_bytes(VERSION_MISMATCH_STORE, FileAccess.get_file_as_bytes(scratch))

	_write_bytes(TRUNCATED_STORE, golden.slice(0, golden.size() - 6))

	var corrupted := golden.duplicate()
	corrupted[CORRUPTION_OFFSET] = corrupted[CORRUPTION_OFFSET] ^ 0xFF
	_write_bytes(CORRUPT_STORE, corrupted)

	var bad_magic := golden.duplicate()
	bad_magic[0] = "X".to_utf8_buffer()[0]
	_write_bytes(BAD_MAGIC_STORE, bad_magic)

	# A single-entry store, then its one record repeated with the header's count
	# raised to two: the only way to produce the duplicate key a correct writer
	# never emits.
	_remove(scratch)
	var single := BaristaScriptParseCache.new()
	var source := _read_source(SCRIPT_A)
	single.put(SCRIPT_A, source, _parse(source))
	if single.flush(scratch, 0, version) != OK:
		push_error("could not flush the single-entry store")
		return 1
	var one_entry := FileAccess.get_file_as_bytes(scratch)
	var record := one_entry.slice(8)
	var duplicated := one_entry.slice(0, 4)
	duplicated.append_array(_u32(2))
	duplicated.append_array(record)
	duplicated.append_array(record)
	_write_bytes(DUPLICATE_KEY_STORE, duplicated)

	# The same key twice, the second record tagged with a version this build does
	# not accept: a duplicate must still be a duplicate rather than a version
	# rejection that quietly leaves one record standing.
	_remove(scratch)
	if single.flush(scratch, 0, version + 1) != OK:
		push_error("could not flush the single-entry store under the next version")
		return 1
	var stale_record := FileAccess.get_file_as_bytes(scratch).slice(8)
	var across_versions := one_entry.slice(0, 4)
	across_versions.append_array(_u32(2))
	across_versions.append_array(record)
	across_versions.append_array(stale_record)
	_write_bytes(DUPLICATE_KEY_ACROSS_VERSIONS_STORE, across_versions)

	print("regenerated the parse cache fixtures under %s" % FIXTURES_ROOT)
	return 0


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

## The stand-in for a parse result. The parser milestone owns the real payload;
## what this needs to be is deterministic and derived only from the source, so
## "a warm payload equals a cold parse" is a real assertion rather than a
## comparison of two constants.
func _parse(source: String) -> PackedByteArray:
	var lines := source.split("\n")
	var summary := PackedStringArray()
	for index in lines.size():
		var line: String = lines[index].strip_edges()
		if line.is_empty() or line.begins_with("#"):
			continue
		summary.append("%d:%s" % [index, line])
	return "\n".join(summary).to_utf8_buffer()


func _populated_cache() -> BaristaScriptParseCache:
	var cache := BaristaScriptParseCache.new()
	for path in [SCRIPT_A, SCRIPT_B]:
		var source := _read_source(path)
		cache.put(path, source, _parse(source))
	return cache


func _read_source(path: String) -> String:
	return FileAccess.get_file_as_bytes(path).get_string_from_utf8()


## Asserts the whole "malformed is never absent" rule for one damaged store: the
## load is CORRUPT, it says why, nothing survives, and a later lookup for a path
## the store never mentioned still reports damage rather than a cold miss.
func _expect_damaged_store(failures: Array[String], store: String, expected_line: String, description: String) -> void:
	var cache := BaristaScriptParseCache.new()
	_expect(failures, cache.load(store) == CORRUPT, "a %s must load as CORRUPT" % description)
	_expect(failures, not cache.get_load_report().is_empty(), "a %s must be reported, not silently dropped" % description)
	_expect(
		failures,
		_report_contains(cache, expected_line),
		"a %s must be reported as \"%s\": %s" % [description, expected_line, cache.get_load_report()]
	)
	_expect(failures, cache.get_entry_count() == 0, "a %s must yield no entries" % description)

	var result := cache.lookup(SCRIPT_A, _read_source(SCRIPT_A))
	_expect(failures, not result["hit"], "a %s must never serve an entry" % description)
	_expect(
		failures,
		result["reason"] == CORRUPT,
		"a lookup against a %s must report CORRUPT, not a cold miss, got %s" % [description, result["reason_name"]]
	)


func _report_contains(cache: BaristaScriptParseCache, fragment: String) -> bool:
	for line in cache.get_load_report():
		if line.contains(fragment):
			return true
	return false


func _scratch_path(name: String) -> String:
	DirAccess.make_dir_recursive_absolute(SCRATCH_ROOT)
	return "%s/%s" % [SCRATCH_ROOT, name]


func _write_bytes(path: String, bytes: PackedByteArray) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		push_error("could not write %s (error %d)" % [path, FileAccess.get_open_error()])
		return
	file.store_buffer(bytes)
	file.close()


func _remove(path: String) -> void:
	if FileAccess.file_exists(path):
		DirAccess.remove_absolute(path)


func _u32(value: int) -> PackedByteArray:
	return PackedByteArray([value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF, (value >> 24) & 0xFF])


## Little-endian, matching bs_put_u64. The value arrives as a signed GDScript
## int reinterpreting a uint64, so the shifts are masked byte by byte.
func _u64(value: int) -> PackedByteArray:
	var bytes := PackedByteArray()
	for index in 8:
		bytes.append((value >> (index * 8)) & 0xFF)
	return bytes


func _expect(failures: Array[String], condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
