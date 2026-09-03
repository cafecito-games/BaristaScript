/**************************************************************************/
/*  bs_cache.cpp                                                          */
/*                                                                        */
/*  Ported from Foundry modules/foundry_script/fs_cache.cpp @ c9d5e35.    */
/*  Hard fork: renamed fs_* -> bs_* and FS* -> BS*, diverging from day    */
/*  one.                                                                  */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_cache.h"

// ---------------------------------------------------------------------------
// Miss-reason vocabulary. One definition of every name and every log line.
// ---------------------------------------------------------------------------

namespace bs_miss {

BSMissReason from_index(int p_index) {
	ERR_FAIL_COND_V_MSG(p_index < 0 || p_index >= 5, BSMissReason::CORRUPT,
			"bs_miss::from_index: no BSMissReason has index " + String::num(p_index) + ".");
	switch (p_index) {
		case 0:
			return BSMissReason::COLD;
		case 1:
			return BSMissReason::VERSION_MISMATCH;
		case 2:
			return BSMissReason::DIGEST_MISMATCH;
		case 3:
			return BSMissReason::CORRUPT;
		case 4:
			return BSMissReason::EVICTED;
	}
	ERR_FAIL_V_MSG(BSMissReason::CORRUPT, "bs_miss::from_index: unhandled index.");
}

String get_name(BSMissReason p_reason) {
	switch (p_reason) {
		case BSMissReason::COLD:
			return "COLD";
		case BSMissReason::VERSION_MISMATCH:
			return "VERSION_MISMATCH";
		case BSMissReason::DIGEST_MISMATCH:
			return "DIGEST_MISMATCH";
		case BSMissReason::CORRUPT:
			return "CORRUPT";
		case BSMissReason::EVICTED:
			return "EVICTED";
	}
	ERR_FAIL_V_MSG(String(), "bs_miss::get_name: unhandled BSMissReason.");
}

String get_log_line(BSMissReason p_reason, const String &p_script_path) {
	// COLD is the only value that may pass unlogged; it still has a line so the parametrized test
	// can prove every consumer handles every enumerator without special-casing it.
	switch (p_reason) {
		case BSMissReason::COLD:
			return "cold cache: no entry for '" + p_script_path + "'";
		case BSMissReason::VERSION_MISMATCH:
			return "discarding cache entry for '" + p_script_path +
					"': its version tag does not match this language version, and no upgrade is attempted";
		case BSMissReason::DIGEST_MISMATCH:
			return "discarding cache entry for '" + p_script_path +
					"': the source on disk does not match the digest the entry was written for";
		case BSMissReason::CORRUPT:
			return "discarding corrupt cache data naming '" + p_script_path +
					"': the store is truncated, fails its checksum, or is not a BaristaScript cache";
		case BSMissReason::EVICTED:
			return "evicting cache entry for '" + p_script_path + "': the script file no longer exists";
	}
	ERR_FAIL_V_MSG(String(), "bs_miss::get_log_line: unhandled BSMissReason.");
}

Vector<String> get_names() {
	return { "COLD", "VERSION_MISMATCH", "DIGEST_MISMATCH", "CORRUPT", "EVICTED" };
}

} // namespace bs_miss

// ---------------------------------------------------------------------------
// BSParseCache
// ---------------------------------------------------------------------------

const char *const BSParseCache::STORE_MAGIC = "BSPC";

String BSParseCache::get_default_store_path() {
	return "user://barista_parse_cache.bin";
}

/**
 * djb2 over arbitrary bytes with a caller-chosen seed. Two seeds are used below so an entry
 * checksum can never collide with a source digest by construction. The exact function is part of
 * the on-disk contract: changing it changes every digest at once, which is a CACHE_FORMAT_VERSION
 * bump, not a silent rewrite.
 */
static uint32_t bs_hash_bytes(const uint8_t *p_data, uint64_t p_length, uint32_t p_seed) {
	uint32_t hash = p_seed;
	for (uint64_t i = 0; i < p_length; i++) {
		hash = (hash * 33) ^ p_data[i];
	}
	return hash;
}

// Seeds for the two distinct hashes the store depends on. Named constants, not inline literals,
// because the on-disk format is defined once.
static constexpr uint32_t BS_SOURCE_DIGEST_SEED = 5381;
static constexpr uint32_t BS_ENTRY_CHECKSUM_SEED = 19779;

static void bs_put_u32(Vector<uint8_t> &p_destination, uint32_t p_value) {
	p_destination.push_back((uint8_t)(p_value & 0xFF));
	p_destination.push_back((uint8_t)((p_value >> 8) & 0xFF));
	p_destination.push_back((uint8_t)((p_value >> 16) & 0xFF));
	p_destination.push_back((uint8_t)((p_value >> 24) & 0xFF));
}

static uint32_t bs_get_u32(const uint8_t *p_source, uint64_t p_offset) {
	return (uint32_t)p_source[p_offset] | ((uint32_t)p_source[p_offset + 1] << 8) |
			((uint32_t)p_source[p_offset + 2] << 16) | ((uint32_t)p_source[p_offset + 3] << 24);
}

uint32_t BSParseCache::compute_source_digest(const String &p_source) {
	// The digest is semantic: version tag bytes then source bytes. No path, no timestamp -- the
	// same source under the same language version digests identically in any checkout.
	const PackedByteArray utf8 = p_source.to_utf8_buffer();
	Vector<uint8_t> bytes;
	bytes.resize(4 + utf8.size());
	bytes.write[0] = (uint8_t)(CACHE_FORMAT_VERSION & 0xFF);
	bytes.write[1] = (uint8_t)((CACHE_FORMAT_VERSION >> 8) & 0xFF);
	bytes.write[2] = (uint8_t)((CACHE_FORMAT_VERSION >> 16) & 0xFF);
	bytes.write[3] = (uint8_t)((CACHE_FORMAT_VERSION >> 24) & 0xFF);
	if (utf8.size() > 0) {
		memcpy(bytes.ptrw() + 4, utf8.ptr(), utf8.size());
	}
	return bs_hash_bytes(bytes.ptr(), bytes.size(), BS_SOURCE_DIGEST_SEED);
}

void BSParseCache::clear() {
	entries.clear();
	rejected.clear();
	store_corrupt = false;
	load_report.clear();
}

bool BSParseCache::has_entry(const String &p_script_path) const {
	return entries.has(p_script_path);
}

int BSParseCache::get_entry_count() const {
	return entries.size();
}

/**
 * The verdict on one on-disk record, returned by the decoder in load(). Kept separate from
 * BSMissReason because DUPLICATE_KEY is a writer bug rather than a lookup outcome.
 */
enum class RecordStatus {
	OK,
	VERSION_REJECTED,
	DUPLICATE_KEY,
	CORRUPT_RECORD,
};

BSMissReason BSParseCache::load(const String &p_store_path) {
	clear();

	const PackedByteArray bytes = FileAccess::get_file_as_bytes(p_store_path);
	const Error open_error = FileAccess::get_open_error();
	if (open_error == Error::ERR_FILE_NOT_FOUND) {
		// The one condition that may be silently absent: no file, no report, cold cache.
		return BSMissReason::COLD;
	}
	if (open_error != Error::OK) {
		store_corrupt = true;
		const String line = "cache store '" + p_store_path + "' exists but cannot be read (error " +
				String::num((int64_t)open_error) + ")";
		load_report.push_back(line);
		ERR_PRINT(line);
		return BSMissReason::CORRUPT;
	}

	const uint8_t *data = bytes.ptr();
	const uint64_t size = bytes.size();

	// Anything shorter than a header, or with the wrong magic, is not a BaristaScript cache. That
	// is corruption, not absence: it is reported so a writer bug cannot hide behind a cold miss.
	if (size < 8 || memcmp(data, STORE_MAGIC, 4) != 0) {
		store_corrupt = true;
		const String line = bs_miss::get_log_line(BSMissReason::CORRUPT, p_store_path);
		load_report.push_back(line);
		ERR_PRINT(line);
		return BSMissReason::CORRUPT;
	}

	const uint32_t entry_count = bs_get_u32(data, 4);
	uint64_t offset = 8;

	for (uint32_t entry_index = 0; entry_index < entry_count; entry_index++) {
		const uint64_t entry_start = offset;
		String key;
		Entry entry;
		uint32_t version_tag = 0;

		// One record is decoded by a single expression whose every rejection path is a `return`, so
		// no branch can skip past the initialization of a field the branches below it read.
		const RecordStatus status = [&]() -> RecordStatus {
			// Every field is bounds-checked against what is actually on disk; a short read means
			// the store was truncated mid-entry and nothing after it can be trusted.
			auto require = [&](uint64_t p_count) -> bool {
				if (p_count > size || offset > size - p_count) {
					return false;
				}
				offset += p_count;
				return true;
			};

			if (!require(4)) {
				return RecordStatus::CORRUPT_RECORD;
			}
			version_tag = bs_get_u32(data, entry_start);

			if (!require(4)) {
				return RecordStatus::CORRUPT_RECORD;
			}
			const uint32_t key_length = bs_get_u32(data, entry_start + 4);
			if (key_length == 0 || !require(key_length)) {
				return RecordStatus::CORRUPT_RECORD;
			}
			const uint64_t key_offset = entry_start + 8;
			// The key is UTF-8; round-tripping it through String rejects embedded NULs and invalid
			// sequences the way a loader would.
			key = String::utf8((const char *)(data + key_offset), (int64_t)key_length);
			if (key.is_empty()) {
				return RecordStatus::CORRUPT_RECORD;
			}

			if (!require(4)) {
				return RecordStatus::CORRUPT_RECORD;
			}
			const uint32_t source_digest = bs_get_u32(data, key_offset + key_length);

			if (!require(4)) {
				return RecordStatus::CORRUPT_RECORD;
			}
			const uint32_t payload_length = bs_get_u32(data, key_offset + key_length + 4);
			if (!require(payload_length) || !require(4)) {
				return RecordStatus::CORRUPT_RECORD;
			}
			const uint64_t payload_offset = key_offset + key_length + 8;
			const uint32_t stored_entry_digest = bs_get_u32(data, offset - 4);

			// The checksum covers the whole record from the version tag through the payload, so a
			// flipped bit anywhere in it -- including in the version tag itself -- is corruption.
			const uint32_t computed_entry_digest = bs_hash_bytes(
					data + entry_start, payload_offset + payload_length - entry_start, BS_ENTRY_CHECKSUM_SEED);
			if (computed_entry_digest != stored_entry_digest) {
				return RecordStatus::CORRUPT_RECORD;
			}

			if (version_tag != CACHE_FORMAT_VERSION) {
				return RecordStatus::VERSION_REJECTED;
			}

			// A key claimed twice -- whether the earlier claim was accepted or version-rejected --
			// means the writer is broken.
			if (entries.has(key) || rejected.has(key)) {
				return RecordStatus::DUPLICATE_KEY;
			}

			entry.source_digest = source_digest;
			entry.payload.resize((int)payload_length);
			if (payload_length > 0) {
				memcpy(entry.payload.ptrw(), data + payload_offset, payload_length);
			}
			return RecordStatus::OK;
		}();

		switch (status) {
			case RecordStatus::OK: {
				entries[key] = entry;
			} break;

			case RecordStatus::VERSION_REJECTED: {
				// Keep the rejection as a tombstone: a later lookup of this path must report
				// VERSION_MISMATCH, not read back as a cold miss.
				const String line = bs_miss::get_log_line(BSMissReason::VERSION_MISMATCH, key);
				load_report.push_back(line);
				ERR_PRINT(line);
				rejected[key] = BSMissReason::VERSION_MISMATCH;
			} break;

			case RecordStatus::DUPLICATE_KEY: {
				// Two records claiming one key means the writer is broken, so nothing in the store
				// is trustworthy: fail loudly and drop everything rather than pick a winner.
				const String line = "duplicate cache key '" + key + "' in '" + p_store_path +
						"': the cache writer is broken, discarding the whole store";
				clear();
				load_report.push_back(line);
				ERR_PRINT(line);
				store_corrupt = true;
				return BSMissReason::CORRUPT;
			}

			case RecordStatus::CORRUPT_RECORD: {
				const String line = "cache store '" + p_store_path + "' is truncated or corrupt at byte " +
						String::num((int64_t)entry_start) + " of " + String::num((int64_t)size) +
						"; discarding it and every entry after it";
				load_report.push_back(line);
				ERR_PRINT(line);
				store_corrupt = true;
				return BSMissReason::CORRUPT;
			}
		}
	}

	// A store whose declared entry count is not exhausted by its bytes, or that carries bytes after
	// its last record, was not written by this build's writer.
	if (offset != size) {
		const String line = "cache store '" + p_store_path + "' has " + String::num((int64_t)(size - offset)) +
				" trailing bytes after its declared " + String::num((int64_t)entry_count) +
				" entries; discarding it";
		load_report.push_back(line);
		ERR_PRINT(line);
		store_corrupt = true;
		return BSMissReason::CORRUPT;
	}

	return BSMissReason::COLD;
}

BSParseCache::Lookup BSParseCache::lookup(const String &p_script_path, const String &p_source) {
	Lookup result;
	result.reason = BSMissReason::COLD;

	if (HashMap<String, BSMissReason>::ConstIterator tombstone = rejected.find(p_script_path)) {
		result.reason = tombstone->value;
		return result;
	}

	if (HashMap<String, Entry>::Iterator entry = entries.find(p_script_path)) {
		if (!FileAccess::file_exists(p_script_path)) {
			// An entry for a file that no longer exists is evicted, not served and not silently
			// dropped: the lookup names the eviction.
			entries.erase(p_script_path);
			rejected[p_script_path] = BSMissReason::EVICTED;
			const String line = bs_miss::get_log_line(BSMissReason::EVICTED, p_script_path);
			load_report.push_back(line);
			ERR_PRINT(line);
			result.reason = BSMissReason::EVICTED;
			return result;
		}

		const uint32_t digest = compute_source_digest(p_source);
		if (digest != entry->value.source_digest) {
			entries.erase(p_script_path);
			rejected[p_script_path] = BSMissReason::DIGEST_MISMATCH;
			const String line = bs_miss::get_log_line(BSMissReason::DIGEST_MISMATCH, p_script_path);
			load_report.push_back(line);
			ERR_PRINT(line);
			result.reason = BSMissReason::DIGEST_MISMATCH;
			return result;
		}

		result.hit = true;
		result.payload = entry->value.payload;
		return result;
	}

	if (store_corrupt) {
		// The store exists but is not structurally trustworthy; a miss here is corruption made
		// visible, never a cold miss.
		result.reason = BSMissReason::CORRUPT;
		return result;
	}

	return result;
}

void BSParseCache::put(const String &p_script_path, const String &p_source, Vector<uint8_t> p_payload) {
	Entry entry;
	entry.source_digest = compute_source_digest(p_source);
	entry.payload = p_payload;
	entries[p_script_path] = entry;
	// A fresh put supersedes whatever rejection the path carried: the next flush writes an entry
	// this build stands behind.
	rejected.erase(p_script_path);
}

Error BSParseCache::flush(const String &p_store_path, WriteFault p_fault, uint32_t p_version_tag) {
	if (p_fault == WriteFault::BEFORE_WRITE) {
		const String line = "cache flush to '" + p_store_path + "' failed before writing (simulated)";
		ERR_PRINT(line);
		return Error::ERR_FILE_CANT_WRITE;
	}

	// Sorted keys, so one entry set always serializes to the same bytes and a rewritten store can
	// be compared byte-for-byte.
	Vector<String> keys;
	for (const KeyValue<String, Entry> &entry : entries) {
		keys.push_back(entry.key);
	}
	keys.sort();

	Vector<uint8_t> store;
	store.push_back((uint8_t)STORE_MAGIC[0]);
	store.push_back((uint8_t)STORE_MAGIC[1]);
	store.push_back((uint8_t)STORE_MAGIC[2]);
	store.push_back((uint8_t)STORE_MAGIC[3]);
	bs_put_u32(store, (uint32_t)keys.size());

	for (const String &key : keys) {
		const Entry &entry = entries[key];
		const PackedByteArray key_bytes = key.to_utf8_buffer();

		const uint64_t entry_start = store.size();

		bs_put_u32(store, p_version_tag);
		bs_put_u32(store, (uint32_t)key_bytes.size());
		for (int64_t i = 0; i < key_bytes.size(); i++) {
			store.push_back(key_bytes.ptr()[i]);
		}
		bs_put_u32(store, entry.source_digest);
		bs_put_u32(store, (uint32_t)entry.payload.size());
		for (int64_t i = 0; i < entry.payload.size(); i++) {
			store.push_back(entry.payload.ptr()[i]);
		}
		bs_put_u32(store, bs_hash_bytes(store.ptr() + entry_start, store.size() - entry_start, BS_ENTRY_CHECKSUM_SEED));
	}

	// Atomic write: everything lands in a temp file first, so a crash mid-write leaves either the
	// previous store or no store -- never a half-written one under the real name.
	const String temp_path = p_store_path + String(".tmp");
	const Ref<FileAccess> file = FileAccess::open(temp_path, FileAccess::WRITE);
	if (file.is_null()) {
		const String line = "cache flush to '" + p_store_path + "' failed: cannot open '" + temp_path +
				"' for writing (error " + String::num((int64_t)FileAccess::get_open_error()) +
				"); parsing continues without a cache";
		ERR_PRINT(line);
		return Error::ERR_FILE_CANT_WRITE;
	}

	PackedByteArray store_bytes;
	store_bytes.resize(store.size());
	if (store.size() > 0) {
		memcpy(store_bytes.ptrw(), store.ptr(), store.size());
	}
	if (!file->store_buffer(store_bytes)) {
		const String line = "cache flush to '" + p_store_path + "' failed while writing '" + temp_path +
				"'; parsing continues without a cache";
		ERR_PRINT(line);
		return Error::ERR_FILE_CANT_WRITE;
	}
	file->flush();
	file->close();

	if (p_fault == WriteFault::AFTER_WRITE_BEFORE_RENAME) {
		// The temp file is complete but the rename never happens, so the previous store must still
		// be the one a loader sees. The temp file is left in place; it is not under the real name.
		const String line = "cache flush to '" + p_store_path +
				"' failed between write and rename (simulated); the previous store is untouched";
		ERR_PRINT(line);
		return Error::ERR_FILE_CANT_WRITE;
	}

	const Error rename_error = DirAccess::rename_absolute(temp_path, p_store_path);
	if (rename_error != Error::OK) {
		const String line = "cache flush to '" + p_store_path + "' failed to rename '" + temp_path +
				"' (error " + String::num((int64_t)rename_error) + "); parsing continues without a cache";
		ERR_PRINT(line);
		return rename_error;
	}

	return Error::OK;
}

Vector<String> BSParseCache::evict_entries_with_missing_files() {
	Vector<String> evicted;
	Vector<String> keys;
	for (const KeyValue<String, Entry> &entry : entries) {
		keys.push_back(entry.key);
	}
	// Copied out before mutating: erasing from the map being iterated would invalidate it.
	for (const String &key : keys) {
		if (!FileAccess::file_exists(key)) {
			entries.erase(key);
			rejected[key] = BSMissReason::EVICTED;
			evicted.push_back(key);
			const String line = bs_miss::get_log_line(BSMissReason::EVICTED, key);
			load_report.push_back(line);
			ERR_PRINT(line);
		}
	}
	return evicted;
}

// ---------------------------------------------------------------------------
// BSCache (the in-memory layer ported from Foundry's FSCache)
// ---------------------------------------------------------------------------

BSCache *BSCache::singleton = nullptr;

BSCache *BSCache::get_singleton() {
	// Created lazily and deliberately never destroyed: the cache holds engine-backed Strings, and
	// a destructor run during static destruction would call into the GDExtension interface after
	// it has been unloaded -- the same hazard the seam documents for SNAME. One leaked singleton
	// outlives the process by moments and keeps teardown order from mattering.
	if (singleton == nullptr) {
		singleton = memnew(BSCache);
	}
	return singleton;
}

BSCache::BSCache() {}

String BSCache::get_source_code(const String &p_path) {
	if (singleton != nullptr) {
		std::lock_guard<std::mutex> lock(singleton->mutex);
		if (HashMap<String, String>::ConstIterator override = singleton->source_overrides.find(p_path)) {
			return override->value;
		}
	}

	const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		ERR_FAIL_V_MSG(String(), "Script '" + p_path + "' could not be opened (error " + String::num((int64_t)FileAccess::get_open_error()) + ").");
	}

	const int64_t length = file->get_length();
	const PackedByteArray raw = file->get_buffer(length);
	if (raw.size() != length) {
		ERR_FAIL_V_MSG(String(), "Script '" + p_path + "' was truncated while being read.");
	}

	const String source = raw.get_string_from_utf8();
	if (length > 0 && source.is_empty()) {
		ERR_FAIL_V_MSG(String(), "Script '" + p_path + "' contains invalid unicode (UTF-8), so it was not loaded. Please ensure that scripts are saved in valid UTF-8 unicode.");
	}
	return source;
}

void BSCache::set_source_override(const String &p_path, const String &p_source) {
	std::lock_guard<std::mutex> lock(get_singleton()->mutex);
	get_singleton()->source_overrides[p_path] = p_source;
}

bool BSCache::has_source_override(const String &p_path) {
	std::lock_guard<std::mutex> lock(get_singleton()->mutex);
	return get_singleton()->source_overrides.has(p_path);
}

void BSCache::clear_source_override(const String &p_path) {
	std::lock_guard<std::mutex> lock(get_singleton()->mutex);
	get_singleton()->source_overrides.erase(p_path);
}

void BSCache::clear_source_overrides() {
	std::lock_guard<std::mutex> lock(get_singleton()->mutex);
	get_singleton()->source_overrides.clear();
}

void BSCache::record_dependency(const String &p_path, const String &p_owner) {
	if (p_owner.is_empty() || p_path == p_owner) {
		return;
	}
	std::lock_guard<std::mutex> lock(get_singleton()->mutex);
	get_singleton()->dependencies[p_owner].insert(p_path);
	get_singleton()->inverse_dependencies[p_path].insert(p_owner);
}

HashSet<String> BSCache::get_inverse_dependencies(const String &p_path) {
	std::lock_guard<std::mutex> lock(get_singleton()->mutex);
	HashSet<String> owners;
	if (HashMap<String, HashSet<String>>::ConstIterator found = get_singleton()->inverse_dependencies.find(p_path)) {
		owners = found->value;
	}
	return owners;
}

void BSCache::clear_dependency_edges(BSCache *p_cache, const String &p_path) {
	// Ported from FSCache::clear_parser_dependency_edges (fs_cache.cpp:279) over the two maps that
	// exist in this port.
	if (HashMap<String, HashSet<String>>::Iterator forward = p_cache->dependencies.find(p_path)) {
		for (const String &dep : forward->value) {
			if (HashMap<String, HashSet<String>>::Iterator inverse = p_cache->inverse_dependencies.find(dep)) {
				inverse->value.erase(p_path);
				if (inverse->value.is_empty()) {
					p_cache->inverse_dependencies.erase(dep);
				}
			}
		}
		p_cache->dependencies.erase(p_path);
	}
	p_cache->inverse_dependencies.erase(p_path);
}

void BSCache::remove_script(const String &p_path) {
	if (singleton == nullptr) {
		return;
	}
	std::lock_guard<std::mutex> lock(singleton->mutex);
	clear_dependency_edges(singleton, p_path);
	singleton->source_overrides.erase(p_path);
}

void BSCache::move_script(const String &p_from, const String &p_to) {
	if (singleton == nullptr || p_from == p_to || p_from.is_empty()) {
		return;
	}
	std::lock_guard<std::mutex> lock(singleton->mutex);
	clear_dependency_edges(singleton, p_from);
	singleton->source_overrides.erase(p_from);
	if (!p_to.is_empty()) {
		// The moved script keeps nothing of its old dependency edges; whoever reloads it records
		// them again against the new path.
	}
}

void BSCache::clear() {
	if (singleton == nullptr) {
		return;
	}
	std::lock_guard<std::mutex> lock(singleton->mutex);
	singleton->source_overrides.clear();
	singleton->dependencies.clear();
	singleton->inverse_dependencies.clear();
}
