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

#include "bs_analyzer.h"
#include "bs_builtin_sources.h"
#include "bs_parser.h"

#include <cstring>

#include <godot_cpp/core/object.hpp>

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
 * FNV-1a over arbitrary bytes with a caller-chosen basis. 64 bits, not 32: both hashes below are
 * the sole guard on a correctness decision -- one decides whether a stored payload still describes
 * the file on disk, the other whether a record survived the trip to disk intact -- and a 32-bit
 * width leaves a birthday collision within reach of an ordinary project's worth of sources. Two
 * distinct bases are used so an entry checksum can never collide with a source digest by
 * construction. The exact function is part of the on-disk contract: changing it changes every
 * digest at once, which is a CACHE_FORMAT_VERSION bump, not a silent rewrite.
 */
static uint64_t bs_hash_bytes(const uint8_t *p_data, uint64_t p_length, uint64_t p_basis) {
	uint64_t hash = p_basis;
	for (uint64_t i = 0; i < p_length; i++) {
		hash ^= (uint64_t)p_data[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

// Bases for the two distinct hashes the store depends on. Named constants, not inline literals,
// because the on-disk format is defined once. BS_SOURCE_DIGEST_BASIS is FNV-1a's standard 64-bit
// offset basis; the checksum's is a different arbitrary odd constant.
static constexpr uint64_t BS_SOURCE_DIGEST_BASIS = 14695981039346656037ULL;
static constexpr uint64_t BS_ENTRY_CHECKSUM_BASIS = 1469598103934665403ULL;

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

static void bs_put_u64(Vector<uint8_t> &p_destination, uint64_t p_value) {
	for (int shift = 0; shift < 64; shift += 8) {
		p_destination.push_back((uint8_t)((p_value >> shift) & 0xFF));
	}
}

static uint64_t bs_get_u64(const uint8_t *p_source, uint64_t p_offset) {
	uint64_t value = 0;
	for (int index = 0; index < 8; index++) {
		value |= (uint64_t)p_source[p_offset + index] << (index * 8);
	}
	return value;
}

/**
 * Removes a temp store that will never be promoted. A failed flush must not leave a half-written
 * file behind for the next flush to append to or a reader to trip over; the real store is
 * untouched either way.
 */
static void bs_discard_temp_store(const String &p_temp_path) {
	if (FileAccess::file_exists(p_temp_path)) {
		DirAccess::remove_absolute(p_temp_path);
	}
}

/**
 * Shortens a file to p_length bytes. Only WriteFault::TRUNCATE_TEMP_AFTER_WRITE uses this, to stand
 * in for the deferred write failure -- a full volume surfacing at flush or close -- that no test
 * can provoke honestly.
 */
static void bs_truncate_for_test(const String &p_path, int64_t p_length) {
	const PackedByteArray whole = FileAccess::get_file_as_bytes(p_path);
	const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	ERR_FAIL_COND_MSG(file.is_null(), "bs_truncate_for_test: could not reopen '" + p_path + "'.");
	file->store_buffer(whole.slice(0, p_length));
	file->close();
}

uint64_t BSParseCache::compute_source_digest(const String &p_source) {
	// The digest is semantic: the version tag, then the source's byte length, then its bytes. No
	// path and no timestamp, so the same source under the same language version digests identically
	// in any checkout. The length is part of the digested input as well as implied by it, so two
	// sources must collide on both a 64-bit hash and their length to be confused for each other.
	const PackedByteArray utf8 = p_source.to_utf8_buffer();
	Vector<uint8_t> bytes;
	bs_put_u32(bytes, CACHE_FORMAT_VERSION);
	bs_put_u64(bytes, (uint64_t)utf8.size());
	const int prefix = bytes.size();
	bytes.resize(prefix + utf8.size());
	if (utf8.size() > 0) {
		memcpy(bytes.ptrw() + prefix, utf8.ptr(), utf8.size());
	}
	return bs_hash_bytes(bytes.ptr(), bytes.size(), BS_SOURCE_DIGEST_BASIS);
}

uint64_t BSParseCache::compute_entry_checksum(const Vector<uint8_t> &p_record_bytes) {
	return bs_hash_bytes(p_record_bytes.ptr(), (uint64_t)p_record_bytes.size(), BS_ENTRY_CHECKSUM_BASIS);
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
			// The key must survive a full byte -> String -> byte round trip. Decoding alone is not
			// enough: String::utf8 stops at an embedded NUL and hands back the non-empty prefix, so
			// a malformed record would otherwise be filed under a shorter path that really exists
			// and served as a hit for it. Re-encoding and comparing is what rejects that, along
			// with invalid sequences and any other lossy decode.
			key = String::utf8((const char *)(data + key_offset), (int64_t)key_length);
			if (key.is_empty()) {
				return RecordStatus::CORRUPT_RECORD;
			}
			const PackedByteArray key_round_trip = key.to_utf8_buffer();
			if ((uint64_t)key_round_trip.size() != (uint64_t)key_length ||
					memcmp(key_round_trip.ptr(), data + key_offset, key_length) != 0) {
				return RecordStatus::CORRUPT_RECORD;
			}

			if (!require(8)) {
				return RecordStatus::CORRUPT_RECORD;
			}
			const uint64_t source_digest = bs_get_u64(data, key_offset + key_length);

			if (!require(4)) {
				return RecordStatus::CORRUPT_RECORD;
			}
			const uint32_t payload_length = bs_get_u32(data, key_offset + key_length + 8);
			if (!require(payload_length) || !require(8)) {
				return RecordStatus::CORRUPT_RECORD;
			}
			const uint64_t payload_offset = key_offset + key_length + 12;
			const uint64_t stored_entry_digest = bs_get_u64(data, offset - 8);

			// The checksum covers the whole record from the version tag through the payload, so a
			// flipped bit anywhere in it -- including in the version tag itself -- is corruption.
			const uint64_t computed_entry_digest = bs_hash_bytes(
					data + entry_start, payload_offset + payload_length - entry_start, BS_ENTRY_CHECKSUM_BASIS);
			if (computed_entry_digest != stored_entry_digest) {
				return RecordStatus::CORRUPT_RECORD;
			}

			// A key claimed twice means the writer is broken, whatever the second claim's version
			// tag says. This is checked before the version tag so a duplicate cannot hide behind a
			// stale record: a store carrying one is discarded, never quietly deduplicated.
			if (entries.has(key) || rejected.has(key)) {
				return RecordStatus::DUPLICATE_KEY;
			}

			if (version_tag != CACHE_FORMAT_VERSION) {
				return RecordStatus::VERSION_REJECTED;
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
				// Never partially deserialize: a store damaged anywhere is not trustworthy
				// anywhere, so the records that happened to parse before the damage go too. Serving
				// a prefix would make a truncated store indistinguishable from a smaller healthy
				// one, which is the "malformed is never absent" rule failing quietly.
				const String line = "cache store '" + p_store_path + "' is truncated or corrupt at byte " +
						String::num((int64_t)entry_start) + " of " + String::num((int64_t)size) +
						"; discarding the whole store";
				clear();
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
				" entries; discarding the whole store";
		clear();
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

		const uint64_t digest = compute_source_digest(p_source);
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
		bs_put_u64(store, entry.source_digest);
		bs_put_u32(store, (uint32_t)entry.payload.size());
		for (int64_t i = 0; i < entry.payload.size(); i++) {
			store.push_back(entry.payload.ptr()[i]);
		}
		bs_put_u64(store, bs_hash_bytes(store.ptr() + entry_start, store.size() - entry_start, BS_ENTRY_CHECKSUM_BASIS));
	}

	// Atomic write: everything lands in a temp file first, so a crash mid-write leaves either the
	// previous store or no store -- never a half-written one under the real name.
	//
	// The temp file's name is unique per flush rather than a fixed "<store>.tmp". Two editors open
	// on one project share a user:// directory, and a fixed name lets one writer truncate the file
	// the other is about to promote. The name folds in a process-local counter and a hash of the
	// bytes about to be written: two flushes carrying different content can never pick the same
	// name, and two carrying identical content produce identical files, so sharing one is harmless.
	// The read-back below is what makes that last case safe rather than merely likely.
	static std::atomic<uint64_t> flush_counter{ 0 };
	const String temp_path = p_store_path + String(".") +
			String::num_uint64(bs_hash_bytes(store.ptr(), (uint64_t)store.size(), BS_ENTRY_CHECKSUM_BASIS), 16) +
			String(".") + String::num_uint64(flush_counter.fetch_add(1)) + String(".tmp");
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
	const bool buffered = file->store_buffer(store_bytes);
	file->flush();
	const Error write_error = file->get_error();
	file->close();
	if (!buffered || (write_error != Error::OK && write_error != Error::ERR_FILE_EOF)) {
		const String line = "cache flush to '" + p_store_path + "' failed while writing '" + temp_path +
				"' (error " + String::num((int64_t)write_error) + "); parsing continues without a cache";
		ERR_PRINT(line);
		bs_discard_temp_store(temp_path);
		return Error::ERR_FILE_CANT_WRITE;
	}

	if (p_fault == WriteFault::TRUNCATE_TEMP_AFTER_WRITE) {
		bs_truncate_for_test(temp_path, store_bytes.size() / 2);
	}

	// Read the temp file back before promoting it. store_buffer reports what the buffer accepted,
	// not what reached the disk: a full or failing volume surfaces at flush or close, and on some
	// platforms not until then. Promoting a short file would rename an unloadable store over a
	// perfectly good one, which is exactly what the atomic-write contract exists to prevent.
	const PackedByteArray written_back = FileAccess::get_file_as_bytes(temp_path);
	if (FileAccess::get_open_error() != Error::OK || written_back != store_bytes) {
		const String line = "cache flush to '" + p_store_path + "' wrote '" + temp_path +
				"' incompletely (" + String::num((int64_t)written_back.size()) + " of " +
				String::num((int64_t)store_bytes.size()) +
				" bytes read back); the previous store is left in place";
		ERR_PRINT(line);
		bs_discard_temp_store(temp_path);
		return Error::ERR_FILE_CANT_WRITE;
	}

	if (p_fault == WriteFault::AFTER_WRITE_BEFORE_RENAME) {
		// The temp file is complete but the rename never happens, so the previous store must still
		// be the one a loader sees. The temp file is left in place; it is not under the real name.
		const String line = "cache flush to '" + p_store_path +
				"' failed between write and rename (simulated); the previous store is untouched";
		ERR_PRINT(line);
		return Error::ERR_FILE_CANT_WRITE;
	}

	// On platforms whose rename cannot replace an existing file atomically, Godot's DirAccess
	// removes the destination first, so a crash in that window leaves no store rather than the
	// previous one. That is within the contract -- either the old entry or none, never a partial
	// one -- and costs a cold parse on the next run, so it is not worked around here.
	const Error rename_error = DirAccess::rename_absolute(temp_path, p_store_path);
	if (rename_error != Error::OK) {
		const String line = "cache flush to '" + p_store_path + "' failed to rename '" + temp_path +
				"' (error " + String::num((int64_t)rename_error) + "); parsing continues without a cache";
		ERR_PRINT(line);
		bs_discard_temp_store(temp_path);
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

BSCache *BSCache::get_singleton() {
	// Created lazily and deliberately never destroyed: the cache holds engine-backed Strings, and
	// a destructor run during static destruction would call into the GDExtension interface after
	// it has been unloaded -- the same hazard the seam documents for SNAME. One leaked singleton
	// outlives the process by moments and keeps teardown order from mattering.
	//
	// The function-local static is what makes the creation itself safe: Godot loads and reloads
	// scripts from worker threads, so a plain `if (pointer == nullptr)` could construct two caches
	// and leave two callers locking different mutexes while mutating the same maps. C++11
	// guarantees this initializer runs exactly once, however many threads arrive together.
	static BSCache *instance = memnew(BSCache);
	return instance;
}

BSCache::BSCache() {}

String BSCache::get_source_code(const String &p_path) {
	{
		BSCache *cache = get_singleton();
		std::lock_guard<std::mutex> lock(cache->mutex);
		if (HashMap<String, String>::ConstIterator override = cache->source_overrides.find(p_path)) {
			return override->value;
		}
	}

	{
		String builtin_source;
		if (barista_script::BSBuiltinSources::get_source(p_path, builtin_source)) {
			return builtin_source;
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
	BSCache *cache = get_singleton();
	if (cache == nullptr) {
		return;
	}

	{
		std::lock_guard<std::mutex> lock(cache->mutex);
		if (cache->cleared) {
			return;
		}
		if (HashMap<String, Vector<uint64_t>>::Iterator abandoned = cache->abandoned_parser_map.find(p_path)) {
			for (uint64_t parser_ref_id : abandoned->value) {
				Object *object = ObjectDB::get_instance(parser_ref_id);
				BSParserRef *parser_ref = object != nullptr ? Object::cast_to<BSParserRef>(object) : nullptr;
				if (parser_ref != nullptr) {
					parser_ref->clear();
				}
			}
		}
		cache->abandoned_parser_map.erase(p_path);
		if (cache->parser_map.has(p_path) && cache->parser_map[p_path].is_valid()) {
			cache->parser_map[p_path]->clear();
		}
	}

	remove_parser(p_path);

	std::lock_guard<std::mutex> lock(cache->mutex);
	clear_dependency_edges(cache, p_path);
	cache->source_overrides.erase(p_path);
}

void BSCache::move_script(const String &p_from, const String &p_to) {
	if (p_from == p_to || p_from.is_empty()) {
		return;
	}
	// The moved script keeps nothing of its old dependency edges; whoever reloads it records them
	// again against the new path, which is why p_to is not consulted here.
	remove_parser(p_from);
	BSCache *cache = get_singleton();
	std::lock_guard<std::mutex> lock(cache->mutex);
	clear_dependency_edges(cache, p_from);
	cache->source_overrides.erase(p_from);
}

void BSCache::clear() {
	BSCache *cache = get_singleton();
	if (cache == nullptr) {
		return;
	}
	std::lock_guard<std::mutex> lock(cache->mutex);
	cache->cleared = true;
	for (KeyValue<String, Ref<BSParserRef>> &entry : cache->parser_map) {
		if (entry.value.is_valid()) {
			entry.value->abandoned = true;
			entry.value->clear();
		}
	}
	cache->parser_map.clear();
	cache->parser_dependencies.clear();
	cache->parser_inverse_dependencies.clear();
	cache->abandoned_parser_map.clear();
	cache->source_overrides.clear();
	cache->dependencies.clear();
	cache->inverse_dependencies.clear();
	cache->cleared = false;
}

void BSCache::clear_parser_dependency_edges(BSCache *p_cache, const String &p_path) {
	if (HashMap<String, HashSet<String>>::Iterator forward = p_cache->parser_dependencies.find(p_path)) {
		for (const String &dep : forward->value) {
			if (HashMap<String, HashSet<String>>::Iterator inverse = p_cache->parser_inverse_dependencies.find(dep)) {
				inverse->value.erase(p_path);
				if (inverse->value.is_empty()) {
					p_cache->parser_inverse_dependencies.erase(dep);
				}
			}
		}
		p_cache->parser_dependencies.erase(p_path);
	}
}

void BSCache::update_parser_dependencies(const String &p_path, const barista_script::BSParser *p_parser) {
	BSCache *cache = get_singleton();
	if (cache == nullptr || p_parser == nullptr) {
		return;
	}
	std::lock_guard<std::mutex> lock(cache->mutex);
	clear_parser_dependency_edges(cache, p_path);
	HashSet<String> deps;
	for (const String &dep : p_parser->get_dependencies()) {
		deps.insert(dep);
		cache->parser_inverse_dependencies[dep].insert(p_path);
	}
	if (!deps.is_empty()) {
		cache->parser_dependencies[p_path] = deps;
	}
}

BSParserRef::Status BSParserRef::get_status() const {
	return status;
}

String BSParserRef::get_path() const {
	return path;
}

uint32_t BSParserRef::get_source_hash() const {
	return source_hash;
}

barista_script::BSParser *BSParserRef::get_parser() {
	if (parser == nullptr && status == EMPTY) {
		parser = memnew(barista_script::BSParser);
	}
	return parser;
}

barista_script::BSAnalyzer *BSParserRef::get_analyzer() {
	if (analyzer == nullptr) {
		analyzer = memnew(barista_script::BSAnalyzer(get_parser()));
	}
	return analyzer;
}

Error BSParserRef::raise_status(Status p_new_status) {
	std::lock_guard<std::mutex> raise_lock(raise_mutex);
	ERR_FAIL_COND_V(clearing, ERR_BUG);
	ERR_FAIL_COND_V(parser == nullptr && status != EMPTY, ERR_BUG);

	while (result == OK && p_new_status > status) {
		switch (status) {
			case EMPTY: {
				// Allocate the parser while status is still EMPTY (get_parser() only constructs then).
				barista_script::BSParser *p = get_parser();
				status = PARSED;
				const String source = BSCache::get_source_code(path);
				source_hash = (uint32_t)source.hash();
				result = p->parse(source, path, false);
				if (result == OK) {
					BSCache::update_parser_dependencies(path, p);
				}
			} break;
			case PARSED: {
				status = INHERITANCE_SOLVED;
				result = get_analyzer()->resolve_inheritance();
			} break;
			case INHERITANCE_SOLVED: {
				status = INTERFACE_SOLVED;
				result = get_analyzer()->resolve_interface();
			} break;
			case INTERFACE_SOLVED: {
				status = FULLY_SOLVED;
				result = get_analyzer()->resolve_body();
			} break;
			case FULLY_SOLVED: {
				return result;
			}
		}
	}

	return result;
}

void BSParserRef::clear() {
	if (clearing) {
		return;
	}
	clearing = true;

	barista_script::BSParser *lparser = parser;
	barista_script::BSAnalyzer *lanalyzer = analyzer;

	parser = nullptr;
	analyzer = nullptr;
	status = EMPTY;
	result = OK;
	source_hash = 0;

	clearing = false;

	if (lanalyzer != nullptr) {
		memdelete(lanalyzer);
	}
	if (lparser != nullptr) {
		memdelete(lparser);
	}
}

BSParserRef::~BSParserRef() {
	clear();

	if (!abandoned) {
		BSCache *cache = BSCache::get_singleton();
		if (cache != nullptr) {
			std::lock_guard<std::mutex> lock(cache->mutex);
			cache->parser_map.erase(path);
		}
	}
}

Ref<BSParserRef> BSCache::get_parser(const String &p_path, BSParserRef::Status p_status, Error &r_error, const String &p_owner) {
	r_error = OK;
	Ref<BSParserRef> ref;
	BSCache *cache = get_singleton();
	if (cache == nullptr || p_path.is_empty()) {
		r_error = ERR_INVALID_PARAMETER;
		return ref;
	}

	const String path = p_path.simplify_path();
	const String owner = p_owner.simplify_path();

	{
		std::lock_guard<std::mutex> lock(cache->mutex);
		if (cache->cleared) {
			r_error = ERR_BUSY;
			return ref;
		}
		if (!owner.is_empty() && path != owner) {
			cache->dependencies[owner].insert(path);
			cache->parser_inverse_dependencies[path].insert(owner);
		}

		if (cache->parser_map.has(path)) {
			ref = cache->parser_map[path];
			if (ref.is_null()) {
				r_error = ERR_INVALID_DATA;
				return ref;
			}
		} else {
			// Missing files must not invent cache entries (unless an in-memory override or builtin
			// source supplies the text). Checked under the cache lock so we do not re-enter.
			String builtin_source;
			const bool has_builtin = barista_script::BSBuiltinSources::get_source(path, builtin_source);
			if (!cache->source_overrides.has(path) && !has_builtin && !FileAccess::file_exists(path)) {
				r_error = ERR_FILE_NOT_FOUND;
				return ref;
			}
			ref.instantiate();
			ref->path = path;
			cache->parser_map[path] = ref;
		}
	}

	// Parse/analyze outside the cache mutex so a dependency cycle cannot deadlock.
	r_error = ref->raise_status(p_status);
	return ref;
}

bool BSCache::has_parser(const String &p_path) {
	std::lock_guard<std::mutex> lock(get_singleton()->mutex);
	return get_singleton()->parser_map.has(p_path.simplify_path());
}

HashSet<String> BSCache::collect_parser_invalidation_closure(const String &p_path) {
	HashSet<String> closure;
	BSCache *cache = get_singleton();
	if (cache == nullptr || p_path.is_empty()) {
		return closure;
	}
	std::lock_guard<std::mutex> lock(cache->mutex);
	List<String> frontier;
	frontier.push_back(p_path.simplify_path());
	while (!frontier.is_empty()) {
		const String path = frontier.front()->get();
		frontier.pop_front();
		if (closure.has(path)) {
			continue;
		}
		closure.insert(path);
		if (HashMap<String, HashSet<String>>::ConstIterator inverse = cache->parser_inverse_dependencies.find(path)) {
			for (const String &dependent : inverse->value) {
				frontier.push_back(dependent);
			}
		}
	}
	return closure;
}

Vector<String> BSCache::collect_parsers_reaching_namespace(const String &p_namespace) {
	Vector<String> members;
	BSCache *cache = get_singleton();
	if (cache == nullptr || p_namespace.is_empty()) {
		return members;
	}
	std::lock_guard<std::mutex> lock(cache->mutex);
	for (const KeyValue<String, Ref<BSParserRef>> &entry : cache->parser_map) {
		const Ref<BSParserRef> &parser_ref = entry.value;
		if (parser_ref.is_null() || parser_ref->status == BSParserRef::EMPTY) {
			continue;
		}
		const barista_script::BSParser *parser = parser_ref->parser;
		if (parser == nullptr) {
			continue;
		}
		const barista_script::BSParser::ClassNode *head = parser->get_tree();
		if (head == nullptr) {
			continue;
		}
		if (head->namespace_name == p_namespace || head->imports.has(p_namespace)) {
			members.push_back(entry.key);
		}
	}
	return members;
}

void BSCache::remove_parser(const String &p_path) {
	BSCache *cache = get_singleton();
	if (cache == nullptr) {
		return;
	}
	const String path = p_path.simplify_path();
	HashSet<String> ideps;
	{
		std::lock_guard<std::mutex> lock(cache->mutex);
		clear_parser_dependency_edges(cache, path);
		if (cache->parser_map.has(path)) {
			Ref<BSParserRef> parser_ref = cache->parser_map[path];
			if (parser_ref.is_valid()) {
				parser_ref->abandoned = true;
				cache->abandoned_parser_map[path].push_back(parser_ref->get_instance_id());
			}
			cache->parser_map.erase(path);
		}
		if (HashMap<String, HashSet<String>>::Iterator inverse = cache->parser_inverse_dependencies.find(path)) {
			ideps = inverse->value;
			cache->parser_inverse_dependencies.erase(path);
		}
	}
	for (const String &idep_path : ideps) {
		remove_parser(idep_path);
	}
}

void BSCache::invalidate_analysis() {
	BSCache *cache = get_singleton();
	if (cache == nullptr) {
		return;
	}
	Vector<String> parser_paths;
	{
		std::lock_guard<std::mutex> lock(cache->mutex);
		for (const KeyValue<String, Ref<BSParserRef>> &entry : cache->parser_map) {
			parser_paths.push_back(entry.key);
		}
	}
	for (const String &path : parser_paths) {
		remove_parser(path);
	}
}
