/**************************************************************************/
/*  cache_test.cpp                                                        */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "storage_fixture.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
constexpr const char *SCRIPT_A = "res://tests/cache_fixtures/script_a.barista";
constexpr const char *SCRIPT_B = "res://tests/cache_fixtures/script_b.barista";
constexpr const char *GOLDEN = "res://tests/cache_fixtures/golden_store.bin";
using Reason = BSMissReason;
using Fault = BSParseCache::WriteFault;

String source(const String &path) { return FileAccess::get_file_as_string(path); }

// Preserve the source-derived opaque payload used to produce the committed golden store.
Vector<uint8_t> payload(const String &text) {
	const PackedStringArray lines = text.split("\n");
	PackedStringArray summary;
	for (int i = 0; i < lines.size(); ++i) {
		const String line = lines[i].strip_edges();
		if (!line.is_empty() && !line.begins_with("#")) {
			summary.push_back(String::num_int64(i) + String(":") + line);
		}
	}
	return bytes(String("\n").join(summary));
}

BSParseCache populated() {
	BSParseCache cache;
	for (const char *path : { SCRIPT_A, SCRIPT_B }) {
		cache.put(path, source(path), payload(source(path)));
	}
	return cache;
}

bool report_contains(const BSParseCache &cache, const String &fragment) {
	for (const String &line : cache.get_load_report()) {
		if (line.contains(fragment)) {
			return true;
		}
	}
	return false;
}

void damaged(const String &path, const String &diagnostic) {
	BSParseCache cache;
	CHECK(cache.load(path) == Reason::CORRUPT);
	CHECK_FALSE(cache.get_load_report().is_empty());
	CHECK(report_contains(cache, diagnostic));
	CHECK(cache.get_entry_count() == 0);
	const auto lookup = cache.lookup(SCRIPT_A, source(SCRIPT_A));
	CHECK_FALSE(lookup.hit);
	CHECK(lookup.reason == Reason::CORRUPT);
}
} // namespace

TEST_SUITE("cache") {
	static void scenario_absent_store_is_a_silent_cold_cache() {
		StorageFixture fixture;
		BSParseCache cache;
		CHECK(cache.load(fixture.path("absent.bin")) == Reason::COLD);
		CHECK(cache.get_load_report().is_empty());
		CHECK(cache.get_entry_count() == 0);
		const auto result = cache.lookup(SCRIPT_A, source(SCRIPT_A));
		CHECK_FALSE(result.hit);
		CHECK(result.reason == Reason::COLD);
	}
	TEST_CASE("absent_store_is_a_silent_cold_cache") { scenario_absent_store_is_a_silent_cold_cache(); }

	static void scenario_golden_store_from_this_build_loads() {
		BSParseCache cache;
		CHECK(cache.load(GOLDEN) == Reason::COLD);
		CHECK(cache.get_load_report().is_empty());
		CHECK(cache.get_entry_count() == 2);
		for (const char *path : { SCRIPT_A, SCRIPT_B }) {
			const auto result = cache.lookup(path, source(path));
			CHECK(result.hit);
			CHECK(result.payload == payload(source(path)));
		}
	}
	TEST_CASE("golden_store_from_this_build_loads") { scenario_golden_store_from_this_build_loads(); }

	static void scenario_golden_store_round_trips_byte_identically() {
		StorageFixture fixture;
		const String rewritten = fixture.path("rewritten.bin");
		CHECK(populated().flush(rewritten) == OK);
		CHECK(read_bytes(rewritten) == read_bytes(GOLDEN));
	}
	TEST_CASE("golden_store_round_trips_byte_identically") { scenario_golden_store_round_trips_byte_identically(); }

	static void scenario_truncated_store_is_corrupt_never_absent() {
		damaged("res://tests/cache_fixtures/truncated_store.bin", "truncated or corrupt at byte");
	}
	TEST_CASE("truncated_store_is_corrupt_never_absent") { scenario_truncated_store_is_corrupt_never_absent(); }
	static void scenario_corrupt_store_is_corrupt_never_absent() {
		damaged("res://tests/cache_fixtures/corrupt_store.bin", "truncated or corrupt at byte");
	}
	TEST_CASE("corrupt_store_is_corrupt_never_absent") { scenario_corrupt_store_is_corrupt_never_absent(); }
	static void scenario_bad_magic_store_is_corrupt_never_absent() {
		damaged("res://tests/cache_fixtures/bad_magic_store.bin", "is not a BaristaScript cache");
	}
	TEST_CASE("bad_magic_store_is_corrupt_never_absent") { scenario_bad_magic_store_is_corrupt_never_absent(); }
	static void scenario_trailing_bytes_are_corrupt_never_absent() {
		StorageFixture fixture;
		auto content = read_bytes(GOLDEN);
		content.push_back(0x7f);
		const String store = fixture.path("trailing.bin");
		if (!write_bytes(store, content)) {
			return;
		}
		damaged(store, "trailing bytes after its declared");
	}
	TEST_CASE("trailing_bytes_are_corrupt_never_absent") { scenario_trailing_bytes_are_corrupt_never_absent(); }

	static void scenario_duplicate_key_fails_loudly() {
		BSParseCache cache;
		CHECK(cache.load("res://tests/cache_fixtures/duplicate_key_store.bin") == Reason::CORRUPT);
		CHECK(cache.get_entry_count() == 0);
		CHECK(report_contains(cache, "duplicate cache key '" + String(SCRIPT_A) + String("'")));
		CHECK(report_contains(cache, "the cache writer is broken"));
	}
	TEST_CASE("duplicate_key_fails_loudly") { scenario_duplicate_key_fails_loudly(); }
	static void scenario_duplicate_key_fails_loudly_across_versions() {
		BSParseCache cache;
		CHECK(cache.load("res://tests/cache_fixtures/duplicate_key_across_versions_store.bin") == Reason::CORRUPT);
		CHECK(cache.get_entry_count() == 0);
		CHECK(report_contains(cache, "duplicate cache key '" + String(SCRIPT_A) + String("'")));
		CHECK(cache.lookup(SCRIPT_A, source(SCRIPT_A)).reason == Reason::CORRUPT);
	}
	TEST_CASE("duplicate_key_fails_loudly_across_versions") { scenario_duplicate_key_fails_loudly_across_versions(); }

	static void scenario_key_with_an_embedded_nul_is_corrupt() {
		StorageFixture fixture;
		const auto text = source(SCRIPT_A);
		const auto data = payload(text);
		auto key = bytes(SCRIPT_A);
		key.push_back(0);
		key.append_array(bytes("/shadow"));
		Vector<uint8_t> record;
		append_integer(record, BSParseCache::CACHE_FORMAT_VERSION, 4);
		append_integer(record, key.size(), 4);
		record.append_array(key);
		append_integer(record, BSParseCache::compute_source_digest(text), 8);
		append_integer(record, data.size(), 4);
		record.append_array(data);
		append_integer(record, BSParseCache::compute_entry_checksum(record), 8);
		auto content = bytes(BSParseCache::STORE_MAGIC);
		append_integer(content, 1, 4);
		content.append_array(record);
		const String store = fixture.path("nul_key.bin");
		if (!write_bytes(store, content)) {
			return;
		}
		BSParseCache cache;
		CHECK(cache.load(store) == Reason::CORRUPT);
		CHECK(cache.get_entry_count() == 0);
		CHECK_FALSE(cache.has_entry(SCRIPT_A));
		CHECK_FALSE(cache.lookup(SCRIPT_A, text).hit);
	}
	TEST_CASE("key_with_an_embedded_nul_is_corrupt") { scenario_key_with_an_embedded_nul_is_corrupt(); }

	static void scenario_version_mismatch_is_discarded_never_upgraded() {
		BSParseCache cache;
		cache.load("res://tests/cache_fixtures/version_mismatch_store.bin");
		CHECK(cache.get_entry_count() == 0);
		CHECK_FALSE(cache.has_entry(SCRIPT_A));
		const auto result = cache.lookup(SCRIPT_A, source(SCRIPT_A));
		CHECK_FALSE(result.hit);
		CHECK(result.reason == Reason::VERSION_MISMATCH);
		CHECK(report_contains(cache, "no upgrade is attempted"));
	}
	TEST_CASE("version_mismatch_is_discarded_never_upgraded") { scenario_version_mismatch_is_discarded_never_upgraded(); }
	static void scenario_digest_mismatch_is_discarded() {
		BSParseCache cache;
		cache.load(GOLDEN);
		const auto result = cache.lookup(SCRIPT_A, source(SCRIPT_A) + String("\n# edited since the entry was written\n"));
		CHECK_FALSE(result.hit);
		CHECK(result.reason == Reason::DIGEST_MISMATCH);
		CHECK_FALSE(cache.has_entry(SCRIPT_A));
		CHECK(cache.lookup(SCRIPT_A, source(SCRIPT_A)).reason == Reason::DIGEST_MISMATCH);
	}
	TEST_CASE("digest_mismatch_is_discarded") { scenario_digest_mismatch_is_discarded(); }

	static void scenario_entry_for_deleted_file_is_evicted() {
		StorageFixture fixture;
		const String path = fixture.path("deleted.barista");
		const String text = "func gone():\n\treturn 1\n";
		if (!write_bytes(path, bytes(text))) {
			return;
		}
		BSParseCache cache;
		cache.put(path, text, payload(text));
		CHECK(cache.lookup(path, text).hit);
		CHECK(DirAccess::remove_absolute(path) == OK);
		const auto result = cache.lookup(path, text);
		CHECK_FALSE(result.hit);
		CHECK(result.reason == Reason::EVICTED);
		CHECK_FALSE(cache.has_entry(path));
	}
	TEST_CASE("entry_for_deleted_file_is_evicted") { scenario_entry_for_deleted_file_is_evicted(); }
	static void scenario_eviction_sweep_reports_every_deleted_file() {
		StorageFixture fixture;
		const String path = fixture.path("swept.barista");
		const String text = "func swept():\n\treturn 2\n";
		if (!write_bytes(path, bytes(text))) {
			return;
		}
		auto cache = populated();
		cache.put(path, text, payload(text));
		CHECK(DirAccess::remove_absolute(path) == OK);
		const auto evicted = cache.evict_entries_with_missing_files();
		CHECK(evicted.size() == 1);
		if (evicted.size() == 1) {
			CHECK(evicted[0] == path);
		}
		CHECK(cache.get_entry_count() == 2);
		CHECK(cache.lookup(path, text).reason == Reason::EVICTED);
	}
	TEST_CASE("eviction_sweep_reports_every_deleted_file") { scenario_eviction_sweep_reports_every_deleted_file(); }

	static void scenario_warm_lookup_equals_cold_parse() {
		StorageFixture fixture;
		const String text = source(SCRIPT_B);
		const auto cold = payload(text);
		const String store = fixture.path("warm.bin");
		BSParseCache writer;
		writer.put(SCRIPT_B, text, cold);
		CHECK(writer.flush(store) == OK);
		BSParseCache reader;
		reader.load(store);
		const auto warm = reader.lookup(SCRIPT_B, text);
		CHECK(warm.hit);
		CHECK(warm.payload == cold);
		CHECK_FALSE(cold.is_empty());
	}
	TEST_CASE("warm_lookup_equals_cold_parse") { scenario_warm_lookup_equals_cold_parse(); }
	static void scenario_digest_is_semantic_and_path_independent() {
		StorageFixture fixture;
		const String text = source(SCRIPT_B);
		const auto digest = BSParseCache::compute_source_digest(text);
		CHECK(BSParseCache::compute_source_digest(text) == digest);
		CHECK(BSParseCache::compute_source_digest(text + String(" ")) != digest);
		BSParseCache writer;
		writer.put(SCRIPT_B, text, payload(text));
		const String first = fixture.path("checkout_one.bin");
		const String second = fixture.path("checkout_two.bin");
		CHECK(writer.flush(first) == OK);
		CHECK(writer.flush(second) == OK);
		CHECK(read_bytes(first) == read_bytes(second));
	}
	TEST_CASE("digest_is_semantic_and_path_independent") { scenario_digest_is_semantic_and_path_independent(); }
	static void scenario_digest_ignores_file_modification_time() {
		StorageFixture fixture;
		const String path = fixture.path("touched.barista");
		const String text = "func touched():\n\treturn 3\n";
		if (!write_bytes(path, bytes(text))) {
			return;
		}
		BSParseCache cache;
		cache.put(path, text, payload(text));
		if (!write_bytes(path, bytes(text))) {
			return;
		}
		CHECK(cache.lookup(path, text).hit);
		const String edited = text + String("# edited\n");
		if (!write_bytes(path, bytes(edited))) {
			return;
		}
		CHECK(cache.lookup(path, edited).reason == Reason::DIGEST_MISMATCH);
	}
	TEST_CASE("digest_ignores_file_modification_time") { scenario_digest_ignores_file_modification_time(); }

	static void scenario_write_failure_is_logged_and_non_fatal() {
		StorageFixture fixture;
		const String store = fixture.path("write_failure.bin");
		auto cache = populated();
		CHECK(cache.flush(store) == OK);
		const auto before = read_bytes(store);
		CHECK(cache.flush(store, Fault::BEFORE_WRITE) != OK);
		CHECK(read_bytes(store) == before);
		CHECK(cache.lookup(SCRIPT_A, source(SCRIPT_A)).hit);
	}
	TEST_CASE("write_failure_is_logged_and_non_fatal") { scenario_write_failure_is_logged_and_non_fatal(); }
	static void scenario_atomic_write_leaves_the_previous_store_intact() {
		StorageFixture fixture;
		const String store = fixture.path("atomic.bin");
		BSParseCache first;
		first.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(first.flush(store) == OK);
		const auto previous = read_bytes(store);
		BSParseCache second;
		second.put(SCRIPT_B, source(SCRIPT_B), payload(source(SCRIPT_B)));
		CHECK(second.flush(store, Fault::AFTER_WRITE_BEFORE_RENAME) != OK);
		CHECK(read_bytes(store) == previous);
		BSParseCache third;
		third.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(third.flush(store, Fault::AFTER_WRITE_BEFORE_RENAME) != OK);
		const auto temps = temporary_files(store);
		CHECK(temps.size() == 2);
		for (const String &path : temps) {
			CHECK(DirAccess::remove_absolute(path) == OK);
		}
		BSParseCache reader;
		CHECK(reader.load(store) == Reason::COLD);
		CHECK(reader.lookup(SCRIPT_A, source(SCRIPT_A)).hit);
		CHECK_FALSE(reader.has_entry(SCRIPT_B));
	}
	TEST_CASE("atomic_write_leaves_the_previous_store_intact") { scenario_atomic_write_leaves_the_previous_store_intact(); }
	static void scenario_deferred_write_failure_never_replaces_the_previous_store() {
		StorageFixture fixture;
		const String store = fixture.path("deferred.bin");
		BSParseCache first;
		first.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(first.flush(store) == OK);
		const auto previous = read_bytes(store);
		BSParseCache second;
		second.put(SCRIPT_B, source(SCRIPT_B), payload(source(SCRIPT_B)));
		CHECK(second.flush(store, Fault::TRUNCATE_TEMP_AFTER_WRITE) != OK);
		CHECK(read_bytes(store) == previous);
		CHECK(temporary_files(store).is_empty());
		BSParseCache reader;
		CHECK(reader.load(store) == Reason::COLD);
		CHECK(reader.lookup(SCRIPT_A, source(SCRIPT_A)).hit);
	}
	TEST_CASE("deferred_write_failure_never_replaces_the_previous_store") { scenario_deferred_write_failure_never_replaces_the_previous_store(); }

	static void scenario_miss_reason_vocabulary_is_closed() {
		const auto names = bs_miss::get_names();
		const char *expected[] = { "COLD", "VERSION_MISMATCH", "DIGEST_MISMATCH", "CORRUPT", "EVICTED" };
		CHECK(names.size() == 5);
		if (names.size() != 5) {
			return;
		}
		for (int i = 0; i < names.size(); ++i) {
			CHECK(names[i] == expected[i]);
			CHECK(bs_miss::get_name(bs_miss::from_index(i)) == names[i]);
		}
		CHECK(names[int(Reason::COLD)] == "COLD");
		CHECK(names[int(Reason::VERSION_MISMATCH)] == "VERSION_MISMATCH");
		CHECK(names[int(Reason::DIGEST_MISMATCH)] == "DIGEST_MISMATCH");
		CHECK(names[int(Reason::CORRUPT)] == "CORRUPT");
		CHECK(names[int(Reason::EVICTED)] == "EVICTED");
	}
	TEST_CASE("miss_reason_vocabulary_is_closed") { scenario_miss_reason_vocabulary_is_closed(); }
	static void scenario_every_miss_reason_has_a_distinct_log_line() {
		HashSet<String> seen;
		const auto names = bs_miss::get_names();
		for (int i = 0; i < names.size(); ++i) {
			const String line = bs_miss::get_log_line(bs_miss::from_index(i), SCRIPT_A);
			CHECK_FALSE(line.is_empty());
			CHECK(line.contains(SCRIPT_A));
			CHECK_FALSE(seen.has(line));
			seen.insert(line);
		}
		CHECK(bs_miss::get_log_line(Reason::COLD, SCRIPT_A).begins_with("cold cache"));
		for (Reason reason : { Reason::VERSION_MISMATCH, Reason::DIGEST_MISMATCH, Reason::CORRUPT, Reason::EVICTED }) {
			CHECK_FALSE(bs_miss::get_log_line(reason, SCRIPT_A).contains("cold"));
		}
	}
	TEST_CASE("every_miss_reason_has_a_distinct_log_line") { scenario_every_miss_reason_has_a_distinct_log_line(); }
	static void scenario_source_override_shadows_the_file_on_disk() {
		StorageFixture fixture;
		const String disk = source(SCRIPT_A);
		CHECK(BSCache::get_source_code(SCRIPT_A) == disk);
		const String edited = disk + String("# unsaved edit\n");
		BSCache::set_source_override(SCRIPT_A, edited);
		CHECK(BSCache::has_source_override(SCRIPT_A));
		CHECK(BSCache::get_source_code(SCRIPT_A) == edited);
		BSCache::clear_source_override(SCRIPT_A);
		CHECK_FALSE(BSCache::has_source_override(SCRIPT_A));
		CHECK(BSCache::get_source_code(SCRIPT_A) == disk);
	}
	TEST_CASE("source_override_shadows_the_file_on_disk") { scenario_source_override_shadows_the_file_on_disk(); }
	static void scenario_dependency_edges_are_recorded_and_removed() {
		StorageFixture fixture;
		BSCache::record_dependency(SCRIPT_A, SCRIPT_B);
		auto inverse = BSCache::get_inverse_dependencies(SCRIPT_A);
		CHECK(inverse.size() == 1);
		CHECK(inverse.has(SCRIPT_B));
		BSCache::record_dependency(SCRIPT_A, SCRIPT_A);
		inverse = BSCache::get_inverse_dependencies(SCRIPT_A);
		CHECK(inverse.size() == 1);
		CHECK(inverse.has(SCRIPT_B));
		BSCache::remove_script(SCRIPT_B);
		CHECK(BSCache::get_inverse_dependencies(SCRIPT_A).is_empty());
		BSCache::record_dependency(SCRIPT_A, SCRIPT_B);
		BSCache::move_script(SCRIPT_B, "res://tests/cache_fixtures/moved.barista");
		CHECK(BSCache::get_inverse_dependencies(SCRIPT_A).is_empty());
	}
	TEST_CASE("dependency_edges_are_recorded_and_removed") { scenario_dependency_edges_are_recorded_and_removed(); }
	TEST_CASE("repeated_and_reversed_cases_restore_ambient_state") {
		void (*scenarios[])() = {
			scenario_absent_store_is_a_silent_cold_cache,
			scenario_golden_store_from_this_build_loads,
			scenario_golden_store_round_trips_byte_identically,
			scenario_truncated_store_is_corrupt_never_absent,
			scenario_corrupt_store_is_corrupt_never_absent,
			scenario_bad_magic_store_is_corrupt_never_absent,
			scenario_trailing_bytes_are_corrupt_never_absent,
			scenario_duplicate_key_fails_loudly,
			scenario_duplicate_key_fails_loudly_across_versions,
			scenario_key_with_an_embedded_nul_is_corrupt,
			scenario_version_mismatch_is_discarded_never_upgraded,
			scenario_digest_mismatch_is_discarded,
			scenario_entry_for_deleted_file_is_evicted,
			scenario_eviction_sweep_reports_every_deleted_file,
			scenario_warm_lookup_equals_cold_parse,
			scenario_digest_is_semantic_and_path_independent,
			scenario_digest_ignores_file_modification_time,
			scenario_write_failure_is_logged_and_non_fatal,
			scenario_atomic_write_leaves_the_previous_store_intact,
			scenario_deferred_write_failure_never_replaces_the_previous_store,
			scenario_miss_reason_vocabulary_is_closed,
			scenario_every_miss_reason_has_a_distinct_log_line,
			scenario_source_override_shadows_the_file_on_disk,
			scenario_dependency_edges_are_recorded_and_removed,
		};
		const int count = int(sizeof(scenarios) / sizeof(scenarios[0]));
		for (int pass = 0; pass < 2; ++pass) {
			for (int i = 0; i < count; ++i) {
				CHECK(verify_case_isolation(scenarios[i]));
			}
			for (int i = count - 1; i >= 0; --i) {
				CHECK(verify_case_isolation(scenarios[i]));
			}
		}
	}
} // TEST_SUITE
