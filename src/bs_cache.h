/**************************************************************************/
/*  bs_cache.h                                                            */
/*                                                                        */
/*  Ported from Foundry modules/foundry_script/fs_cache.h @ c9d5e35.      */
/*  Hard fork: renamed fs_* -> bs_* and FS* -> BS*, diverging from day    */
/*  one.                                                                  */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_platform.h"

#include <atomic>
#include <mutex>

/**
 * The BaristaScript parse cache.
 *
 * Two layers, as in Foundry:
 *
 * - `BSCache` is the in-memory cache ported from Foundry's `FSCache` (fs_cache.h:90). Foundry keys
 *   it by full script path (fs_cache.h:91) and nothing in it persists to disk: the only on-disk
 *   artifacts it touches are compiled `.fsb`/`.fsc` binaries (fs_cache.cpp:84, fs_cache.cpp:95),
 *   which are the bytecode milestone (M4), not this one. The staged `FSParserRef` half is restored
 *   here for M3; compiled-script halves remain M4. Source overrides and dependency edges stay.
 *
 * - `BSParseCache` is the on-disk parse-result store. Foundry has no counterpart to port -- reading
 *   fs_cache.cpp at the pinned revision is what establishes that -- so its record layout is new
 *   design, written to the fail-closed contract of issue #8: malformed is never silently absent.
 */

/**
 * Every reason a cache lookup can miss. A single enum, and every consumer handles every value; the
 * parametrized test in project/tests/cache_test.gd iterates `BSMissReason::get_names()` so a new
 * enumerator without a consumer is a test failure, not a silent fall-through.
 *
 * Only COLD may pass unlogged: a cache file that does not exist is a cold cache. Every other value
 * means bytes existed and were rejected, which is logged distinctly from a cold miss.
 */
enum class BSMissReason {
	COLD,
	VERSION_MISMATCH,
	DIGEST_MISMATCH,
	CORRUPT,
	EVICTED,
};

/**
 * The one definition of the miss-reason vocabulary: enumerator <-> name, and the distinct
 * log/report line for each. Kept beside the enum so a consumer cannot invent a second spelling.
 */
namespace bs_miss {

BSMissReason from_index(int p_index);
String get_name(BSMissReason p_reason);
String get_log_line(BSMissReason p_reason, const String &p_script_path);
Vector<String> get_names();

} // namespace bs_miss

/**
 * The on-disk parse cache. One store file holds many entries keyed by script path (the project-
 * relative `res://` path, never an absolute filesystem path, so two checkouts of one project share
 * cache identity). An entry is valid only when its version tag equals this build's
 * `CACHE_FORMAT_VERSION` and its source digest matches the file on disk.
 *
 * The cached payload is opaque bytes. The parser milestone owns their serialization; this class
 * stores and returns them without interpretation, which is the narrow interface issue #8 leaves
 * for it.
 *
 * On-disk record layout (the single definition; little-endian, host-independent):
 *
 *	store header:
 *	  0..4    magic "BSPC"
 *	  4..8    entry_count (uint32)
 *	then entry_count entries, sorted by key so consecutive flushes of one entry set are
 *	byte-identical:
 *	  version_tag   (uint32)   must equal CACHE_FORMAT_VERSION
 *	  key_length    (uint32)
 *	  key           (key_length bytes, UTF-8)
 *	  source_digest (uint64)
 *	  payload_len   (uint32)
 *	  payload       (payload_len bytes)
 *	  entry_digest  (uint64)   hash over this entry's bytes from version_tag through payload
 *
 * Both digests are 64 bits because each is the sole guard on a correctness decision -- one on
 * whether a payload still describes the source, the other on whether a record survived the trip to
 * disk -- and a 32-bit width puts a birthday collision within reach of an ordinary project.
 */
class BSParseCache {
public:
	/**
	 * The one version constant. Bumped by any change to the record layout or to anything the
	 * cached payload means; a record written under any other value is rejected as
	 * VERSION_MISMATCH, never upgraded.
	 */
	static const uint32_t CACHE_FORMAT_VERSION = 2;

	static const char *const STORE_MAGIC;

	/** The default on-disk location, under user:// so it survives editor restarts. */
	static String get_default_store_path();

	/**
	 * The semantic source digest: it covers the version tag and the source's UTF-8 bytes, and
	 * excludes absolute paths and timestamps, so the same source under the same language version
	 * digests identically in any checkout. Foundry keys parser freshness by `source.hash()`
	 * (fs_cache.cpp:101); this is the same idea made content-addressed.
	 */
	static uint64_t compute_source_digest(const String &p_source);

	/**
	 * The entry checksum over one record's bytes, from its version tag through its payload. The
	 * single definition, exposed so a test can build a record the writer would never emit -- a key
	 * carrying an embedded NUL, say -- with a genuine checksum, rather than reimplementing the hash
	 * beside it and letting the two drift.
	 */
	static uint64_t compute_entry_checksum(const Vector<uint8_t> &p_record_bytes);

	/** Fault points a flush can be asked to simulate, so the atomic-write contract is testable. */
	enum class WriteFault {
		NONE,
		BEFORE_WRITE,
		AFTER_WRITE_BEFORE_RENAME,
		// Shortens the temp file after it is closed, standing in for a write failure that only
		// surfaces at flush or close -- a full volume, say. The read-back check before the rename
		// must catch it and leave the previous store alone.
		TRUNCATE_TEMP_AFTER_WRITE,
	};

	struct Entry {
		uint64_t source_digest = 0;
		Vector<uint8_t> payload;
	};

	struct Lookup {
		bool hit = false;
		BSMissReason reason = BSMissReason::COLD;
		Vector<uint8_t> payload;
	};

private:
	HashMap<String, Entry> entries;
	// Paths whose stored entry was read but rejected, with the reason. A rejected entry must not
	// read back as a cold miss: the bytes existed, so the lookup reports why they were refused.
	HashMap<String, BSMissReason> rejected;
	// Set when the store file exists but is not structurally trustworthy (bad magic, truncated
	// mid-entry, checksum failure, duplicate key). While set, a lookup for an absent path reports
	// CORRUPT rather than COLD -- malformed is never absent.
	bool store_corrupt = false;
	// Human-readable log lines accumulated by load(); empty exactly when the load was cold.
	Vector<String> load_report;

public:
	/**
	 * Loads a store file. Returns the store-level verdict:
	 *   COLD            no file -- the only condition that may be silently absent
	 *   CORRUPT         file present but truncated, checksum-failing, wrong magic, or carrying a
	 *                   duplicate key (the writer is broken; reported loudly via ERR_PRINT)
	 *   VERSION_MISMATCH is never returned here; the version tag is per entry, so it is reported
	 *                   by lookup() for the entries it actually affects
	 * COLD is also the verdict for a store that read cleanly: nothing was rejected at the store
	 * level. Structural damage anywhere discards the whole store -- never a partial deserialization
	 * -- and every later lookup against it reports CORRUPT rather than a cold miss.
	 */
	BSMissReason load(const String &p_store_path);

	/**
	 * Looks up one script path against the source now on disk. A hit returns the stored payload;
	 * every miss returns its distinct reason and discards the stale entry where one exists.
	 */
	Lookup lookup(const String &p_script_path, const String &p_source);

	/** Buffers a payload for the path. Overwriting a dirty buffer for the same path is a reparse. */
	void put(const String &p_script_path, const String &p_source, Vector<uint8_t> p_payload);

	/**
	 * Writes the buffered entries atomically: the full store is written to `<path>.tmp`, flushed,
	 * closed, read back and compared, and only then renamed over the store. A crash at any point
	 * leaves either the previous store or none; a partially written store is never visible under
	 * the real name. The read-back is what makes that true for a write failure that surfaces only
	 * at flush or close, which store_buffer's return value cannot report. Write failures are
	 * returned and logged, never fatal to the caller -- parsing succeeds without a cache.
	 */
	Error flush(const String &p_store_path, WriteFault p_fault = WriteFault::NONE,
			uint32_t p_version_tag = CACHE_FORMAT_VERSION);

	/**
	 * Evicts every entry whose source file no longer exists, returning the evicted paths. Foundry
	 * evicts lazily through remove_script()/remove_parser() (fs_cache.cpp:202); a persistent store
	 * needs the sweep so deleted scripts do not accumulate entries forever.
	 */
	Vector<String> evict_entries_with_missing_files();

	bool has_entry(const String &p_script_path) const;
	int get_entry_count() const;
	const Vector<String> &get_load_report() const { return load_report; }
	void clear();
};

namespace barista_script {
class BSAnalyzer;
class BSParser;
} // namespace barista_script

/**
 * One cached parser/analyzer entry per canonical script path.
 *
 * Ported from Foundry's `FSParserRef` (fs_cache.h:45 @ c9d5e35). Status rises monotonically through
 * M3 phases. A latched `result` fails later callers without re-running. Raise never holds the cache
 * mutex (issue #27): callers snapshot the ref under the lock, then raise outside it.
 */
class BSParserRef : public RefCounted {
	GDCLASS(BSParserRef, RefCounted)

public:
	enum Status {
		EMPTY,
		PARSED,
		INHERITANCE_SOLVED,
		INTERFACE_SOLVED,
		FULLY_SOLVED,
	};

private:
	barista_script::BSParser *parser = nullptr;
	barista_script::BSAnalyzer *analyzer = nullptr;
	Status status = EMPTY;
	Error result = OK;
	String path;
	uint32_t source_hash = 0;
	bool clearing = false;
	bool abandoned = false;
	std::mutex raise_mutex;

	friend class BSCache;

protected:
	static void _bind_methods() {}

public:
	Status get_status() const;
	String get_path() const;
	uint32_t get_source_hash() const;
	Error get_result() const { return result; }
	barista_script::BSParser *get_parser();
	barista_script::BSAnalyzer *get_analyzer();
	Error raise_status(Status p_new_status);
	void clear();

	BSParserRef() {}
	~BSParserRef() override;
};

/**
 * The in-memory script cache, ported from Foundry's FSCache (fs_cache.h:90). Holds source overrides,
 * dependency edges, and the staged parser/analyzer map restored by issue #27.
 */
class BSCache {
	HashMap<String, String> source_overrides;
	HashMap<String, HashSet<String>> dependencies;
	HashMap<String, HashSet<String>> inverse_dependencies;
	HashMap<String, Ref<BSParserRef>> parser_map;
	HashMap<String, HashSet<String>> parser_dependencies;
	HashMap<String, HashSet<String>> parser_inverse_dependencies;
	HashMap<String, Vector<uint64_t>> abandoned_parser_map;
	bool cleared = false;

	std::mutex mutex;

	friend class BSParserRef;

	/**
	 * Drops every dependency edge naming p_path, in both directions. Ported from
	 * FSCache::clear_parser_dependency_edges (fs_cache.cpp:279) over the two maps this port has.
	 * The caller holds the mutex.
	 */
	static void clear_dependency_edges(BSCache *p_cache, const String &p_path);
	static void clear_parser_dependency_edges(BSCache *p_cache, const String &p_path);
	static void update_parser_dependencies(const String &p_path, const barista_script::BSParser *p_parser);

public:
	static BSCache *get_singleton();

	/**
	 * The script source for a path: the in-memory override when one is set (an edited-but-unsaved
	 * buffer), otherwise the file on disk. Ported from fs_cache.cpp:392; Foundry's builtin-source
	 * branch (fs_cache.cpp:394) has no BaristaScript counterpart yet and is dropped rather than
	 * stubbed.
	 */
	static String get_source_code(const String &p_path);

	// In-memory source-override map, ported from fs_cache.h:105-108 and fs_cache.cpp:423-453.
	// Setting or clearing an override does not by itself invalidate anything already built from the
	// old source; callers invalidate the affected paths around a change for it to take effect.
	static void set_source_override(const String &p_path, const String &p_source);
	static bool has_source_override(const String &p_path);
	static void clear_source_override(const String &p_path);
	static void clear_source_overrides();

	/**
	 * Records that p_owner loaded p_path. Ported from the dependency bookkeeping inside
	 * FSCache::get_parser()/get_shallow_script() (fs_cache.cpp:242-245, fs_cache.cpp:525-527);
	 * hoisted to its own method because the loaders themselves are not ported yet.
	 */
	static void record_dependency(const String &p_path, const String &p_owner);

	/**
	 * The files that directly depend on p_path, as recorded by record_dependency(). Snapshot by
	 * value so callers are safe against concurrent cache mutation. Ported from fs_cache.cpp:455.
	 */
	static HashSet<String> get_inverse_dependencies(const String &p_path);

	/**
	 * Returns a cached parser raised to at least `p_status`. Owner edges are recorded under the
	 * lock; parsing/analysis run after the lock is released so dependency cycles cannot deadlock.
	 * Self-dependencies and missing files create no invented entries.
	 */
	static Ref<BSParserRef> get_parser(const String &p_path, BSParserRef::Status p_status, Error &r_error, const String &p_owner = String());
	static bool has_parser(const String &p_path);
	static void remove_parser(const String &p_path);
	static HashSet<String> collect_parser_invalidation_closure(const String &p_path);
	static Vector<String> collect_parsers_reaching_namespace(const String &p_namespace);
	/** Drops every parsed/analyzed artifact; preserves source overrides. */
	static void invalidate_analysis();

	/** Drops every trace of a path: override, dependency edges, parser/analyzer state. */
	static void remove_script(const String &p_path);

	/** Removes old-path parser state; the new path rebuilds on next access. */
	static void move_script(const String &p_from, const String &p_to);

	static void clear();

	BSCache();
};

/**
 * RAII guard that installs a set of source overrides on construction and clears exactly those
 * paths on destruction, restoring the cache's override state. Ported verbatim from
 * fs_cache.h:239 (FSCacheSourceOverrideGuard).
 */
class BSCacheSourceOverrideGuard {
	Vector<String> paths;

public:
	explicit BSCacheSourceOverrideGuard(const HashMap<String, String> &p_overrides) {
		for (const KeyValue<String, String> &entry : p_overrides) {
			BSCache::set_source_override(entry.key, entry.value);
			paths.push_back(entry.key);
		}
	}

	~BSCacheSourceOverrideGuard() {
		for (const String &path : paths) {
			BSCache::clear_source_override(path);
		}
	}

	BSCacheSourceOverrideGuard(const BSCacheSourceOverrideGuard &) = delete;
	BSCacheSourceOverrideGuard &operator=(const BSCacheSourceOverrideGuard &) = delete;
};
