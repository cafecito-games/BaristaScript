/**************************************************************************/
/*  bs_declaration_index.h                                                */
/*                                                                        */
/*  Private BaristaScript declaration index (issue #44 / epic #42).       */
/*  Owns records, derived views, BSGI persistence, and generation tokens. */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_global_class.h"
#include "bs_platform.h"

#include <atomic>
#include <mutex>

namespace barista_script {

/**
 * One path's contribution to the private declaration index.
 *
 * A declaration-only conformance/annotation file has an empty `qualified_name` and kind `NONE`
 * while still carrying namespace / conformance / annotation metadata. Godot's global class cache
 * never sees private kinds; only eligible non-generic `class_name` records correspond to engine
 * registrations (`docs/GRAMMAR.md` D6).
 */
struct BSDeclarationRecord {
	String path;
	uint64_t source_digest = 0;
	String namespace_name;
	String qualified_name;
	BSDeclarationKind kind = BSDeclarationKind::NONE;
	String base_type;
	bool is_abstract = false;
	bool is_tool = false;
	String icon_path;
	Vector<String> global_annotations;
	bool declares_retroactive_conformances = false;

	bool has_head_declaration() const { return kind != BSDeclarationKind::NONE && !qualified_name.is_empty(); }
};

/**
 * Why a BSGI load rejected an on-disk index. Every consumer must handle every enumerator; COLD is
 * the only value that may be silently absent.
 */
enum class BSDeclarationIndexLoadStatus {
	OK,
	COLD,
	BAD_MAGIC,
	UNSUPPORTED_VERSION,
	TRUNCATED,
	TRAILING_BYTES,
	BAD_CHECKSUM,
	INVALID_KIND,
	INVALID_PATH,
	INVALID_UTF8,
	DUPLICATE_PATH,
	DUPLICATE_NAME,
	UNSORTED,
};

String bs_declaration_index_load_status_name(BSDeclarationIndexLoadStatus p_status);

/**
 * The BaristaScript-owned declaration index.
 *
 * Ownership boundary (epic #42): this type owns data and persistence. #27's concrete parser host
 * queries conformance membership here and must not keep a second declaration map. #43 commits
 * analyzer-produced records through the claim/commit APIs after successful analysis.
 *
 * Lock order: `generation_mutex` is always taken *outside* `mutex`. Analysis and filesystem IO never
 * run while either mutex is held.
 */
class BSDeclarationIndex {
public:
	static const uint32_t FORMAT_VERSION = 1;
	static const char *const STORE_MAGIC; // "BSGI"

	enum class WriteFault {
		NONE,
		BEFORE_WRITE,
		AFTER_WRITE_BEFORE_RENAME,
		TRUNCATE_TEMP_AFTER_WRITE,
	};

	static String get_default_store_path();
	static uint64_t compute_source_digest(const String &p_source);
	static uint64_t compute_record_checksum(const Vector<uint8_t> &p_record_bytes);
	static uint64_t compute_file_checksum(const Vector<uint8_t> &p_file_bytes);

	BSDeclarationIndexLoadStatus load(const String &p_store_path);
	Error flush(const String &p_store_path, WriteFault p_fault = WriteFault::NONE);

	/** Snapshot of every path record, sorted by canonical path. */
	Vector<BSDeclarationRecord> get_records() const;
	bool has_path(const String &p_path) const;
	bool try_get_by_path(const String &p_path, BSDeclarationRecord &r_record) const;
	bool try_get_by_qualified_name(const String &p_name, BSDeclarationRecord &r_record) const;
	Vector<String> get_conformance_files_in_namespace(const String &p_namespace) const;
	Vector<String> get_annotation_declaring_paths(const String &p_qualified_annotation) const;
	int get_record_count() const;
	const Vector<String> &get_load_report() const { return load_report; }
	BSDeclarationIndexLoadStatus get_last_load_status() const { return last_load_status; }

	/**
	 * Claims a generation token for a refresh of `p_path`. An older concurrent analysis cannot
	 * commit after a newer claim for the same path.
	 */
	uint64_t claim_refresh(const String &p_path);
	void claim_rename_refresh(const String &p_from, const String &p_to, uint64_t &r_from_token, uint64_t &r_to_token);
	void invalidate_claims(const String &p_path);
	void invalidate_all_claims();

	/**
	 * Commits a successful analysis/declaration result when `p_token` is still current. Returns
	 * false when a newer refresh superseded this one (no mutation), or when the record fails the
	 * same canonical-path / kind gates `load` enforces. On commit, rebuilds derived views and
	 * reports namespaces whose conformance sets changed via `r_changed_namespaces`.
	 */
	bool commit_record(uint64_t p_token, const BSDeclarationRecord &p_record, Vector<String> *r_changed_namespaces = nullptr);

	/**
	 * Removes the path when `p_token` is current. Used for failed analysis (drop stale metadata)
	 * and for delete/rename of the old path.
	 */
	bool remove_path(const String &p_path, uint64_t p_token, Vector<String> *r_changed_namespaces = nullptr);

	/**
	 * Unconditional remove used when synchronizing the live path set (absent files). Bumps the
	 * path's generation so in-flight commits for that path cannot resurrect it.
	 */
	void remove_path_unconditional(const String &p_path, Vector<String> *r_changed_namespaces = nullptr);

	void clear();

	/**
	 * Builds a record from the declaration-only global-class surface already available without the
	 * full analyzer (#43). Conformance/annotation metadata defaults empty; #43 replaces this with
	 * analyzer-produced records via `commit_record`.
	 */
	static BSDeclarationRecord record_from_global_class(const String &p_path, const String &p_source, const BSGlobalClass &p_resolved);

private:
	HashMap<String, BSDeclarationRecord> by_path;
	HashMap<String, String> path_by_qualified_name;
	HashMap<String, Vector<String>> conformance_files_by_namespace;
	HashMap<String, Vector<String>> paths_by_annotation;

	mutable std::mutex mutex;
	std::mutex generation_mutex;
	uint64_t generation_counter = 0;
	uint64_t generation_floor = 0;
	HashMap<String, uint64_t> generations;

	Vector<String> load_report;
	BSDeclarationIndexLoadStatus last_load_status = BSDeclarationIndexLoadStatus::COLD;

	bool _is_token_current_unlocked(const String &p_path, uint64_t p_token) const;
	uint64_t _claim_unlocked(const String &p_path);
	void _erase_path_unlocked(const String &p_path, Vector<String> *r_changed_namespaces);
	void _insert_path_unlocked(const BSDeclarationRecord &p_record, Vector<String> *r_changed_namespaces);
	void _rebuild_views_unlocked();
	static void _sort_unique(Vector<String> &p_values);
	static bool _encode_record(const BSDeclarationRecord &p_record, Vector<uint8_t> &r_bytes, String &r_error);
	static bool _decode_record(const uint8_t *p_data, uint64_t p_size, uint64_t &r_offset, BSDeclarationRecord &r_record, String &r_error);
};
} // namespace barista_script
