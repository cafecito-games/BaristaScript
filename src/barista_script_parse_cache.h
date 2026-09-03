/**************************************************************************/
/*  barista_script_parse_cache.h                                          */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_cache.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace barista_script {

/**
 * The engine-facing handle over the parse cache (BSParseCache in bs_cache.h).
 *
 * The cache itself is ported C++ with no engine base class, and the ported types are
 * engine-backed, so the only place its behaviour can be exercised is inside a running Godot --
 * exactly where the test runners live. This handle forwards each method to BSParseCache or to the
 * miss-reason vocabulary in bs_miss and adds no policy of its own; the cached payload stays opaque
 * bytes end to end.
 */
class BaristaScriptParseCache final : public godot::RefCounted {
	GDCLASS(BaristaScriptParseCache, godot::RefCounted)

	BSParseCache cache;

protected:
	static void _bind_methods();

public:
	/**
	 * Loads a store file into this handle, returning the store-level verdict as a BSMissReason
	 * index (see get_miss_reason_names()). COLD means no file existed; CORRUPT means one existed
	 * and was rejected.
	 */
	int load(const godot::String &p_store_path);

	/**
	 * Looks up one script path against the source now on disk, returning
	 * {"hit": bool, "reason": int, "reason_name": String, "payload": PackedByteArray}.
	 */
	godot::Dictionary lookup(const godot::String &p_script_path, const godot::String &p_source);

	/** Buffers a payload for the path; flush() persists it. */
	void put(const godot::String &p_script_path, const godot::String &p_source,
			const godot::PackedByteArray &p_payload);

	/**
	 * Writes the buffered entries atomically. p_fault is a BSParseCache::WriteFault value (0 NONE,
	 * 1 BEFORE_WRITE, 2 AFTER_WRITE_BEFORE_RENAME) injected for the atomic-write tests;
	 * p_version_tag is the entry version tag, CACHE_FORMAT_VERSION unless a test is deliberately
	 * writing under another one. Returns a Godot Error code.
	 */
	int flush(const godot::String &p_store_path, int p_fault, int p_version_tag);

	/** Evicts entries whose source file no longer exists, returning the evicted paths. */
	godot::PackedStringArray evict_entries_with_missing_files();

	bool has_entry(const godot::String &p_script_path) const;
	int get_entry_count() const;
	godot::PackedStringArray get_load_report() const;
	void clear();

	// The miss-reason vocabulary of bs_miss, exposed by index so the enum has exactly one
	// definition. get_miss_reason_names() is ordered: index i names reason i.
	static godot::PackedStringArray get_miss_reason_names();
	static godot::String get_miss_reason_name(int p_index);
	static godot::String get_miss_reason_log_line(int p_index, const godot::String &p_script_path);

	static int get_cache_format_version();
	static godot::String get_default_store_path();
	/** The 64-bit source digest, reinterpreted as a signed GDScript int. */
	static int64_t compute_source_digest(const godot::String &p_source);

	// The in-memory source-override and dependency layer (BSCache), same forwarding rule.
	static godot::String get_source_code(const godot::String &p_path);
	static void set_source_override(const godot::String &p_path, const godot::String &p_source);
	static bool has_source_override(const godot::String &p_path);
	static void clear_source_override(const godot::String &p_path);
	static void clear_source_overrides();
	static void record_dependency(const godot::String &p_path, const godot::String &p_owner);
	static godot::PackedStringArray get_inverse_dependencies(const godot::String &p_path);
	static void remove_script(const godot::String &p_path);
	static void move_script(const godot::String &p_from, const godot::String &p_to);
	static void clear_script_cache();
};

} // namespace barista_script
