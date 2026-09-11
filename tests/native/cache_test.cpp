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
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#if defined(WINDOWS_ENABLED)
#include <windows.h>
#endif

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

	static void scenario_remove_temp_before_promotion_preserves_previous_store() {
		StorageFixture fixture;
		const String store = fixture.path("fault4.bin");
		BSParseCache first;
		first.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(first.flush(store) == OK);
		const auto previous = read_bytes(store);
		BSParseCache second;
		second.put(SCRIPT_B, source(SCRIPT_B), payload(source(SCRIPT_B)));
		CHECK(second.flush(store, Fault::REMOVE_TEMP_BEFORE_PROMOTION) == ERR_FILE_CANT_WRITE);
		CHECK(read_bytes(store) == previous);
		CHECK(second.has_entry(SCRIPT_B));
		CHECK(temporary_files(store).is_empty());
		BSParseCache reader;
		CHECK(reader.load(store) == Reason::COLD);
		CHECK(reader.lookup(SCRIPT_A, source(SCRIPT_A)).hit);
		CHECK_FALSE(reader.has_entry(SCRIPT_B));
	}
	TEST_CASE("remove_temp_before_promotion_preserves_previous_store") { scenario_remove_temp_before_promotion_preserves_previous_store(); }

	static void scenario_malformed_store_paths_reject_before_temp_creation() {
		StorageFixture fixture;
		BSParseCache cache;
		cache.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		// Snapshot temps under the fixture scratch (mirror GDScript `_count_tmp_under`), not an
		// unrelated basename that would never match temps created for these paths.
		const String paths[] = { String(), String("uid://not-a-store"), String("http://example.invalid/store.bin") };
		for (const String &path : paths) {
			const int before_temps = count_tmp_under(fixture.root);
			CHECK(cache.flush(path) == ERR_INVALID_PARAMETER);
			CHECK(count_tmp_under(fixture.root) == before_temps);
		}
		CHECK(cache.has_entry(SCRIPT_A));
	}
	TEST_CASE("malformed_store_paths_reject_before_temp_creation") { scenario_malformed_store_paths_reject_before_temp_creation(); }

	static void scenario_nul_embedded_store_path_rejects_before_temp_creation() {
		StorageFixture fixture;
		BSParseCache cache;
		cache.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));

		// Observation only: constructors still cannot carry an embedded NUL through godot-cpp String.
		const String via_chr = String("a") + String::chr(0) + String("b");
		CHECK_MESSAGE(!(via_chr.length() == 3 && via_chr[1] == 0),
				"String::chr(0) concatenation unexpectedly preserved an embedded NUL");
		const char raw[3] = { 'a', '\0', 'b' };
		const String via_utf8 = String::utf8(raw, 3);
		CHECK_MESSAGE(!(via_utf8.length() == 3 && via_utf8[1] == 0),
				"length-bounded String::utf8 unexpectedly preserved an embedded NUL");

		// Production path: mutate an owned basename via writable operator[] so a truncated view of
		// the path still names a leaf under the fixture root (not a prefix outside it).
		const String leaf = String("nul_store.bin");
		String store = fixture.path(leaf);
		CHECK(DirAccess::make_dir_recursive_absolute(store.get_base_dir()) == OK);
		const int basename_start = store.length() - leaf.length();
		CHECK(basename_start > 0);
		const int nul_at = basename_start + 1; // second codepoint of the owned leaf
		CHECK(nul_at > basename_start);
		CHECK(nul_at < store.length() - 1);
		CHECK(store[nul_at] != 0);
		const int length_before = store.length();
		store[nul_at] = 0;
		CHECK(store.length() == length_before);
		CHECK(store[nul_at] == 0);
		CHECK(store.substr(0, basename_start).begins_with(fixture.root));

		const int before_temps = count_tmp_under(fixture.root);
		CHECK(bs_validate_parse_cache_store_path(store) == ERR_INVALID_PARAMETER);
		CHECK(cache.flush(store) == ERR_INVALID_PARAMETER);
		CHECK(count_tmp_under(fixture.root) == before_temps);
		CHECK(cache.has_entry(SCRIPT_A));
		// GDScript still cannot deliver NUL; production C++ scan is covered here. No raw-byte public API.
	}
	TEST_CASE("nul_embedded_store_path_rejects_before_temp_creation") { scenario_nul_embedded_store_path_rejects_before_temp_creation(); }

	static void scenario_first_create_replace_and_replay_byte_identity() {
		StorageFixture fixture;
		const String store = fixture.path("create_replace_replay.bin");
		BSParseCache first;
		first.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(first.flush(store) == OK);
		CHECK(temporary_files(store).is_empty());
		BSParseCache reader;
		CHECK(reader.load(store) == Reason::COLD);
		CHECK(reader.lookup(SCRIPT_A, source(SCRIPT_A)).hit);
		CHECK_FALSE(reader.has_entry(SCRIPT_B));
		const auto bytes_after_a = read_bytes(store);

		BSParseCache second;
		second.put(SCRIPT_B, source(SCRIPT_B), payload(source(SCRIPT_B)));
		CHECK(second.flush(store) == OK);
		CHECK(read_bytes(store) != bytes_after_a);
		CHECK(reader.load(store) == Reason::COLD);
		CHECK(reader.lookup(SCRIPT_B, source(SCRIPT_B)).hit);
		CHECK_FALSE(reader.has_entry(SCRIPT_A));
		const auto bytes_after_b = read_bytes(store);

		BSParseCache replay;
		replay.put(SCRIPT_B, source(SCRIPT_B), payload(source(SCRIPT_B)));
		CHECK(replay.flush(store) == OK);
		CHECK(read_bytes(store) == bytes_after_b);
		CHECK(temporary_files(store).is_empty());
	}
	TEST_CASE("first_create_replace_and_replay_byte_identity") { scenario_first_create_replace_and_replay_byte_identity(); }

	static void scenario_remove_temp_before_promotion_without_old_store_leaves_destination_absent() {
		StorageFixture fixture;
		const String store = fixture.path("fault4_absent.bin");
		BSParseCache cache;
		cache.put(SCRIPT_B, source(SCRIPT_B), payload(source(SCRIPT_B)));
		CHECK(cache.flush(store, Fault::REMOVE_TEMP_BEFORE_PROMOTION) != OK);
		CHECK_FALSE(FileAccess::file_exists(store));
		CHECK(cache.has_entry(SCRIPT_B));
		CHECK(temporary_files(store).is_empty());
	}
	TEST_CASE("remove_temp_before_promotion_without_old_store_leaves_destination_absent") {
		scenario_remove_temp_before_promotion_without_old_store_leaves_destination_absent();
	}

	static void scenario_invalid_directory_destination_keeps_sentinel_and_cleans_temp() {
		StorageFixture fixture;
		const String directory = fixture.path("not_a_store_dir");
		CHECK(DirAccess::make_dir_recursive_absolute(directory) == OK);
		const String sentinel = directory.path_join("sentinel.txt");
		const auto sentinel_bytes = bytes(String("sentinel-bytes"));
		CHECK(write_bytes(sentinel, sentinel_bytes));
		BSParseCache cache;
		cache.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(cache.flush(directory) != OK);
		CHECK(DirAccess::dir_exists_absolute(directory));
		CHECK(read_bytes(sentinel) == sentinel_bytes);
		CHECK(temporary_files(directory).is_empty());
		CHECK(cache.has_entry(SCRIPT_A));
	}
	TEST_CASE("invalid_directory_destination_keeps_sentinel_and_cleans_temp") {
		scenario_invalid_directory_destination_keeps_sentinel_and_cleans_temp();
	}

	static void scenario_path_shapes_publish_with_explicit_guards() {
		StorageFixture fixture;
		const auto expected = payload(source(SCRIPT_A));
		const String spaced = fixture.path("path with spaces/café_store.bin");
		CHECK(DirAccess::make_dir_recursive_absolute(spaced.get_base_dir()) == OK);
		BSParseCache writer;
		writer.put(SCRIPT_A, source(SCRIPT_A), expected);
		CHECK(writer.flush(spaced) == OK);
		const auto spaced_bytes = read_bytes(spaced);

		const String absolute = ProjectSettings::get_singleton()->globalize_path(spaced);
		BSParseCache abs_writer;
		abs_writer.put(SCRIPT_A, source(SCRIPT_A), expected);
		CHECK(abs_writer.flush(absolute) == OK);
		CHECK(read_bytes(absolute) == spaced_bytes);

		const String user_leaf = fixture.path("user_leaf_café.bin");
		BSParseCache user_writer;
		user_writer.put(SCRIPT_A, source(SCRIPT_A), expected);
		CHECK(user_writer.flush(user_leaf) == OK);
		CHECK(read_bytes(user_leaf) == spaced_bytes);

		// Relative and res:// controls own unique scratch under captured cwd / project res and
		// require flush OK. Soft-skipping on failure would let a regression pass.
		const Ref<DirAccess> cwd_access = DirAccess::open(".");
		CHECK(cwd_access.is_valid());
		const String cwd = cwd_access->get_current_dir();
		static uint64_t path_shape_serial = 0;
		const String rel_dir_name = String("bs_cache_rel_") + String::num_uint64((uint64_t)OS::get_singleton()->get_process_id()) +
				String("_") + String::num_uint64(++path_shape_serial);
		const String abs_rel_dir = cwd.path_join(rel_dir_name);
		CHECK(DirAccess::make_dir_recursive_absolute(abs_rel_dir) == OK);
		CHECK(DirAccess::open(abs_rel_dir).is_valid());
		const String relative = String(rel_dir_name).path_join("store_café.bin");
		const String abs_relative = abs_rel_dir.path_join("store_café.bin");
		BSParseCache rel_writer;
		rel_writer.put(SCRIPT_A, source(SCRIPT_A), expected);
		CHECK(rel_writer.flush(relative) == OK);
		CHECK(read_bytes(abs_relative) == spaced_bytes);
		CHECK(DirAccess::remove_absolute(abs_relative) == OK);
		CHECK(DirAccess::remove_absolute(abs_rel_dir) == OK);

		const String res_dir_name = String("tests/bs_cache_res_") + String::num_uint64((uint64_t)OS::get_singleton()->get_process_id()) +
				String("_") + String::num_uint64(++path_shape_serial);
		const String res_dir = String("res://").path_join(res_dir_name);
		CHECK(DirAccess::make_dir_recursive_absolute(res_dir) == OK);
		CHECK(DirAccess::open(res_dir).is_valid());
		const String res_store = res_dir.path_join("store_café.bin");
		BSParseCache res_writer;
		res_writer.put(SCRIPT_A, source(SCRIPT_A), expected);
		CHECK(res_writer.flush(res_store) == OK);
		CHECK(read_bytes(res_store) == spaced_bytes);
		CHECK(DirAccess::remove_absolute(res_store) == OK);
		CHECK(DirAccess::remove_absolute(res_dir) == OK);
	}
	TEST_CASE("path_shapes_publish_with_explicit_guards") { scenario_path_shapes_publish_with_explicit_guards(); }

	static void scenario_same_cache_retry_after_remove_temp_before_promotion_succeeds() {
		StorageFixture fixture;
		const String store = fixture.path("fault4_retry.bin");
		BSParseCache first;
		first.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(first.flush(store) == OK);
		const auto previous = read_bytes(store);
		BSParseCache second;
		const auto memory_payload = payload(source(SCRIPT_B));
		second.put(SCRIPT_B, source(SCRIPT_B), memory_payload);
		CHECK(second.flush(store, Fault::REMOVE_TEMP_BEFORE_PROMOTION) == ERR_FILE_CANT_WRITE);
		CHECK(read_bytes(store) == previous);
		CHECK(second.has_entry(SCRIPT_B));
		CHECK(second.lookup(SCRIPT_B, source(SCRIPT_B)).payload == memory_payload);
		CHECK(second.flush(store) == OK);
		BSParseCache reader;
		CHECK(reader.load(store) == Reason::COLD);
		CHECK(reader.lookup(SCRIPT_B, source(SCRIPT_B)).hit);
		CHECK_FALSE(reader.has_entry(SCRIPT_A));
		CHECK(temporary_files(store).is_empty());
	}
	TEST_CASE("same_cache_retry_after_remove_temp_before_promotion_succeeds") {
		scenario_same_cache_retry_after_remove_temp_before_promotion_succeeds();
	}

	static void scenario_failed_first_create_leaves_destination_absent() {
		StorageFixture fixture;
		const String store = fixture.path("failed_first_create.bin");
		BSParseCache cache;
		cache.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(cache.flush(store, Fault::REMOVE_TEMP_BEFORE_PROMOTION) != OK);
		CHECK_FALSE(FileAccess::file_exists(store));
		CHECK(cache.has_entry(SCRIPT_A));
		CHECK(temporary_files(store).is_empty());
	}
	TEST_CASE("failed_first_create_leaves_destination_absent") { scenario_failed_first_create_leaves_destination_absent(); }

	static void scenario_exact_in_memory_payload_survives_failed_promotion() {
		StorageFixture fixture;
		const String store = fixture.path("memory_payload.bin");
		BSParseCache first;
		first.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(first.flush(store) == OK);
		BSParseCache second;
		const auto exact = payload(source(SCRIPT_B));
		second.put(SCRIPT_B, source(SCRIPT_B), exact);
		CHECK(second.lookup(SCRIPT_B, source(SCRIPT_B)).payload == exact);
		CHECK(second.flush(store, Fault::REMOVE_TEMP_BEFORE_PROMOTION) == ERR_FILE_CANT_WRITE);
		CHECK(second.lookup(SCRIPT_B, source(SCRIPT_B)).hit);
		CHECK(second.lookup(SCRIPT_B, source(SCRIPT_B)).payload == exact);
	}
	TEST_CASE("exact_in_memory_payload_survives_failed_promotion") { scenario_exact_in_memory_payload_survives_failed_promotion(); }

	static void scenario_unrelated_temp_preserved_while_owned_temp_cleaned_on_failed_promotion() {
		StorageFixture fixture;
		const String store = fixture.path("owned_promote.bin");
		BSParseCache first;
		first.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(first.flush(store) == OK);
		const String unrelated = fixture.path("unrelated_keep.tmp");
		CHECK(write_bytes(unrelated, bytes(String("keep-me"))));
		BSParseCache second;
		second.put(SCRIPT_B, source(SCRIPT_B), payload(source(SCRIPT_B)));
		CHECK(second.flush(store, Fault::REMOVE_TEMP_BEFORE_PROMOTION) == ERR_FILE_CANT_WRITE);
		CHECK(temporary_files(store).is_empty());
		CHECK(FileAccess::file_exists(unrelated));
		CHECK(read_bytes(unrelated) == bytes(String("keep-me")));
	}
	TEST_CASE("unrelated_temp_preserved_while_owned_temp_cleaned_on_failed_promotion") {
		scenario_unrelated_temp_preserved_while_owned_temp_cleaned_on_failed_promotion();
	}

#if defined(UNIX_ENABLED)
	static void scenario_posix_backslash_store_path_flush_round_trips() {
		StorageFixture fixture;
		const String parent = fixture.path("backslash_dir");
		CHECK(DirAccess::make_dir_recursive_absolute(parent) == OK);
		const String store = parent + String("\\leaf.bin");
		const String unrelated = parent.path_join("unrelated_keep.tmp");
		CHECK(write_bytes(unrelated, bytes(String("aside"))));
		BSParseCache cache;
		cache.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(cache.flush(store) == OK);
		CHECK(FileAccess::file_exists(parent.path_join("leaf.bin")));
		BSParseCache reader;
		CHECK(reader.load(parent.path_join("leaf.bin")) == Reason::COLD);
		CHECK(reader.lookup(SCRIPT_A, source(SCRIPT_A)).hit);
		CHECK(temporary_files(parent.path_join("leaf.bin")).is_empty());
		CHECK(count_tmp_under(parent) == 1); // only the unrelated keeper
		CHECK(FileAccess::file_exists(unrelated));
	}
	TEST_CASE("posix_backslash_store_path_flush_round_trips") { scenario_posix_backslash_store_path_flush_round_trips(); }

	static void scenario_posix_slash_and_backslash_store_paths_are_equivalent() {
		StorageFixture fixture;
		const String parent = fixture.path("equiv_dir");
		CHECK(DirAccess::make_dir_recursive_absolute(parent) == OK);
		const String slash_store = parent.path_join("equiv.bin");
		const String backslash_store = parent + String("\\equiv.bin");
		BSParseCache first;
		first.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(first.flush(slash_store) == OK);
		const auto via_slash = read_bytes(slash_store);
		BSParseCache second;
		second.put(SCRIPT_B, source(SCRIPT_B), payload(source(SCRIPT_B)));
		CHECK(second.flush(backslash_store) == OK);
		CHECK(read_bytes(slash_store) == read_bytes(backslash_store));
		CHECK(read_bytes(slash_store) != via_slash);
	}
	TEST_CASE("posix_slash_and_backslash_store_paths_are_equivalent") { scenario_posix_slash_and_backslash_store_paths_are_equivalent(); }

	static void scenario_posix_backslash_fault4_cleanup_preserves_store_and_drops_temp() {
		// R1: FileAccess normalizes `\` on write; discard must use the same spelling so fault 4
		// cannot leave a promotable temp that bs_replace_file would publish over A.
		StorageFixture fixture;
		const String parent = fixture.path("fault4_backslash_dir");
		CHECK(DirAccess::make_dir_recursive_absolute(parent) == OK);
		const String slash_store = parent.path_join("leaf.bin");
		const String backslash_store = parent + String("\\leaf.bin");
		const String keeper = parent.path_join("keeper.tmp");
		CHECK(write_bytes(keeper, bytes(String("keeper"))));

		BSParseCache first;
		first.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(first.flush(slash_store) == OK);
		const auto previous = read_bytes(slash_store);

		BSParseCache second;
		const auto memory_payload = payload(source(SCRIPT_B));
		second.put(SCRIPT_B, source(SCRIPT_B), memory_payload);
		CHECK(second.flush(backslash_store, Fault::REMOVE_TEMP_BEFORE_PROMOTION) == ERR_FILE_CANT_WRITE);
		CHECK(read_bytes(slash_store) == previous);
		CHECK(second.has_entry(SCRIPT_B));
		CHECK(second.lookup(SCRIPT_B, source(SCRIPT_B)).payload == memory_payload);
		CHECK(count_tmp_under(parent) == 1); // only the unrelated keeper
		CHECK(FileAccess::file_exists(keeper));
		CHECK(temporary_files(slash_store).is_empty());

		CHECK(second.flush(backslash_store, Fault::TRUNCATE_TEMP_AFTER_WRITE) == ERR_FILE_CANT_WRITE);
		CHECK(read_bytes(slash_store) == previous);
		CHECK(count_tmp_under(parent) == 1);
		CHECK(second.flush(backslash_store) == OK);
		CHECK(read_bytes(slash_store) != previous);
		CHECK(temporary_files(slash_store).is_empty());
		CHECK(FileAccess::file_exists(keeper));
	}
	TEST_CASE("posix_backslash_fault4_cleanup_preserves_store_and_drops_temp") {
		scenario_posix_backslash_fault4_cleanup_preserves_store_and_drops_temp();
	}
#else
	TEST_CASE("posix_backslash_store_path_flush_round_trips") {
		WARN_PRINT("POSIX backslash store-path cases require UNIX_ENABLED; skipped on this host");
	}
	TEST_CASE("posix_slash_and_backslash_store_paths_are_equivalent") {
		WARN_PRINT("POSIX backslash equivalence case requires UNIX_ENABLED; skipped on this host");
	}
	TEST_CASE("posix_backslash_fault4_cleanup_preserves_store_and_drops_temp") {
		WARN_PRINT("POSIX backslash fault-4 cleanup case requires UNIX_ENABLED; skipped on this host");
	}
#endif

#if defined(WINDOWS_ENABLED)
	static bool windows_long_paths_enabled_opt_in() {
		DWORD value = 0;
		DWORD value_size = sizeof(value);
		const LONG status = RegGetValueW(HKEY_LOCAL_MACHINE,
				L"SYSTEM\\CurrentControlSet\\Control\\FileSystem",
				L"LongPathsEnabled", RRF_RT_REG_DWORD, nullptr, &value, &value_size);
		return status == ERROR_SUCCESS && value != 0;
	}

	static void scenario_windows_ascii_long_path_flush_round_trips() {
		StorageFixture fixture;
		// G1: these cases establish the disabled-host long-path profile explicitly. Ambient
		// LongPathsEnabled=1 would not prove extended-prep behavior without the opt-in.
		CHECK_FALSE(windows_long_paths_enabled_opt_in());
		const String long_dir = fixture.path(String("L").repeat(40).path_join(String("M").repeat(40)).path_join(String("N").repeat(40)));
		CHECK(DirAccess::make_dir_recursive_absolute(long_dir) == OK);
		const String store = long_dir.path_join(String("O").repeat(40) + String(".bin"));
		String absolute = ProjectSettings::get_singleton()->globalize_path(store);
		const Char16String utf16 = absolute.utf16();
		CHECK(utf16.length() > 260);
		BSParseCache cache;
		cache.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(cache.flush(store) == OK);
		BSParseCache control;
		control.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		const String control_store = fixture.path("ascii_long_control.bin");
		CHECK(control.flush(control_store) == OK);
		CHECK(read_bytes(store) == read_bytes(control_store));
		CHECK(temporary_files(store).is_empty());
	}
	TEST_CASE("windows_ascii_long_path_flush_round_trips") { scenario_windows_ascii_long_path_flush_round_trips(); }

	static void scenario_windows_supplementary_unicode_long_path_flush_succeeds() {
		StorageFixture fixture;
		CHECK_FALSE(windows_long_paths_enabled_opt_in());
		String component;
		while (component.utf16().length() < 130) {
			component += String::chr(0x1F600);
		}
		CHECK(component.utf16().length() < 255);
		const String parent = fixture.path(component).path_join(component);
		CHECK(DirAccess::make_dir_recursive_absolute(parent) == OK);
		const String store = parent.path_join(String("store.bin"));
		String absolute = ProjectSettings::get_singleton()->globalize_path(store);
		CHECK(absolute.utf16().length() > 260);
		CHECK(absolute.length() < 260);
		BSParseCache cache;
		cache.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(cache.flush(store) == OK);
		BSParseCache reader;
		CHECK(reader.load(store) == Reason::COLD);
		CHECK(reader.lookup(SCRIPT_A, source(SCRIPT_A)).hit);
		CHECK(temporary_files(store).is_empty());
	}
	TEST_CASE("windows_supplementary_unicode_long_path_flush_succeeds") {
		scenario_windows_supplementary_unicode_long_path_flush_succeeds();
	}

	static String windows_localhost_admin_unc(const String &absolute_path) {
		String normalized = absolute_path.replace("/", "\\");
		if (normalized.length() < 3 || normalized[1] != ':' || normalized[2] != '\\') {
			return String();
		}
		return String("\\\\localhost\\") + normalized.substr(0, 1) + String("$") + normalized.substr(2);
	}

	static void scenario_windows_unc_store_path_flush_round_trips_with_long_paths_disabled() {
		StorageFixture fixture;
		CHECK_FALSE(windows_long_paths_enabled_opt_in());
		const String local_store = fixture.path("unc_leaf.bin");
		const String absolute = ProjectSettings::get_singleton()->globalize_path(local_store);
		const String unc_store = windows_localhost_admin_unc(absolute);
		CHECK_FALSE(unc_store.is_empty());
		CHECK(unc_store.begins_with("\\\\localhost\\"));
		BSParseCache cache;
		cache.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		CHECK(cache.flush(unc_store) == OK);
		BSParseCache reader;
		CHECK(reader.load(local_store) == Reason::COLD);
		CHECK(reader.lookup(SCRIPT_A, source(SCRIPT_A)).hit);
		CHECK(temporary_files(local_store).is_empty());
		BSParseCache control;
		control.put(SCRIPT_A, source(SCRIPT_A), payload(source(SCRIPT_A)));
		const String control_store = fixture.path("unc_control.bin");
		CHECK(control.flush(control_store) == OK);
		CHECK(read_bytes(local_store) == read_bytes(control_store));
	}
	TEST_CASE("windows_unc_store_path_flush_round_trips_with_long_paths_disabled") {
		scenario_windows_unc_store_path_flush_round_trips_with_long_paths_disabled();
	}
#else
	TEST_CASE("windows_ascii_long_path_flush_round_trips") {
		WARN_PRINT("Windows long-path ASCII case requires WINDOWS_ENABLED; skipped on this host");
	}
	TEST_CASE("windows_supplementary_unicode_long_path_flush_succeeds") {
		WARN_PRINT("Windows supplementary-Unicode long-path case requires WINDOWS_ENABLED; skipped on this host");
	}
	TEST_CASE("windows_unc_store_path_flush_round_trips_with_long_paths_disabled") {
		WARN_PRINT("Windows UNC store-path case requires WINDOWS_ENABLED; skipped on this host");
	}
#endif

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
			scenario_remove_temp_before_promotion_preserves_previous_store,
			scenario_malformed_store_paths_reject_before_temp_creation,
			scenario_nul_embedded_store_path_rejects_before_temp_creation,
			scenario_first_create_replace_and_replay_byte_identity,
			scenario_remove_temp_before_promotion_without_old_store_leaves_destination_absent,
			scenario_invalid_directory_destination_keeps_sentinel_and_cleans_temp,
			scenario_path_shapes_publish_with_explicit_guards,
			scenario_same_cache_retry_after_remove_temp_before_promotion_succeeds,
			scenario_failed_first_create_leaves_destination_absent,
			scenario_exact_in_memory_payload_survives_failed_promotion,
			scenario_unrelated_temp_preserved_while_owned_temp_cleaned_on_failed_promotion,
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
