/**************************************************************************/
/*  corpus_helpers.h                                                      */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_corpus_evaluation.h"

#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace barista_script::native_tests {

/** A corpus case is a `.barista` source paired with a `.out` expectation of the same stem. */
inline constexpr const char *CORPUS_CASE_EXTENSION = ".barista";
inline constexpr const char *CORPUS_HELPER_SUFFIX = ".notest.barista";
inline constexpr const char *CORPUS_EXPECTATION_EXTENSION = ".out";

/**
 * The marker that opts a directory, and every descendant, out of collection.
 *
 * Deliberately not Foundry's `.fsignore`: the upstream corpus root carries one of its own,
 * so honoring that name would silently skip every case while the run still exited zero.
 */
inline constexpr const char *CORPUS_IGNORE_MARKER = ".baristaignore";

/** The corpus root whose cases are evaluated against the shared analyzer fixture sources. */
inline constexpr const char *CORPUS_ANALYZER_STAGING_ROOT = "res://tests/corpus_staging/analyzer";

/**
 * Ordinals are serialized into `BS_CASE_RESULT` and read back by
 * `scripts/run_corpus_triage.py`, so they are pinned to the declaration order of
 * `FailureReason` in `project/tests/corpus_harness.gd`.
 */
enum CorpusFailureReason {
	CORPUS_MISSING_EXPECTATION = 0,
	CORPUS_ORPHANED_EXPECTATION = 1,
	CORPUS_INVALID_EXPECTATION = 2,
	CORPUS_UNREADABLE_SOURCE = 3,
	CORPUS_OUTPUT_MISMATCH = 4,
	CORPUS_INVALID_RESULT = 5,
};

struct CorpusCase {
	godot::String path;
	godot::String expectation_path;
	godot::String stage;
};

struct CorpusDiscovery {
	godot::Vector<CorpusCase> cases;
	godot::PackedStringArray orphaned_expectations;
	godot::PackedStringArray unreadable_directories;
	godot::PackedStringArray discovery_errors;
	int skipped_count = 0;
};

/**
 * Walk `p_root`, pairing `.barista` sources with `.out` expectations.
 *
 * Everything is path-sorted, so a result never depends on directory iteration order.
 * A symlinked entry is recorded as a discovery error and is never traversed or classified.
 */
CorpusDiscovery discover_corpus(const godot::String &p_root);

struct CorpusExpectationGate {
	bool valid = false;
	/** The comparison block: the decoded text with exactly one trailing LF removed. */
	godot::String expected;
};

/**
 * Apply the expectation validity gate. An expectation must decode as round-trip-exact UTF-8,
 * carry no NUL and no CR, and end with exactly one LF over a nonempty block.
 */
CorpusExpectationGate check_expectation_bytes(const godot::PackedByteArray &p_bytes);

struct CorpusOutcome {
	bool passed = false;
	int reason = CORPUS_INVALID_RESULT;
	godot::String path;
	godot::String expectation_path;
	godot::String message;
	godot::String expected;
	godot::String actual;
	bool analysis_ran = false;
	godot::Dictionary fixture_index;
};

/**
 * Presentation-only escaping for mismatch diagnostics. Remaining ASCII C0 and DEL render as
 * lowercase `\xNN`; comparison always stays on the raw values.
 */
godot::String escape_mismatch_value(const godot::String &p_value);

/** The consistency complaint about an evaluation result, or the empty string when it is sound. */
godot::String corpus_result_error(const CorpusResult &p_result, const godot::String &p_stage);

/** Compare an already-validated expectation block against an already-checked evaluation. */
CorpusOutcome compare_corpus_result(const CorpusCase &p_case, const godot::String &p_expected,
		const CorpusResult &p_result);

/** Gate the expectation, read and evaluate the source, then compare. */
CorpusOutcome run_corpus_case(const CorpusCase &p_case, const godot::PackedStringArray &p_fixture_paths);

/**
 * The `BS_CASE_RESULT` payload. The key set is deliberately not uniform: `fixture_index` and
 * `analysis_ran` accompany a pass and an output mismatch only.
 */
godot::Dictionary corpus_case_result_dictionary(const CorpusOutcome &p_outcome);

/** Whether `p_path` is a relative, traversal-free, non-helper `.barista` case path. */
bool valid_case_relative(const godot::String &p_path);

/**
 * Reject the JSON extensions Godot's parser accepts -- trailing commas, literal control
 * characters in strings, duplicate decoded keys, a non-integer `schema_version`. Returns the
 * complaint, or the empty string when the text is strict JSON.
 */
godot::String validate_strict_json(const godot::String &p_text);

struct CorpusJsonDocument {
	godot::String error;
	godot::Dictionary document;
};

/** Read a JSON object that must be valid UTF-8 and strict JSON before its data is used. */
CorpusJsonDocument read_unique_json(const godot::String &p_path);

struct CorpusStageManifest {
	godot::String error;
	/** Absolute `res://` case path to its `parser` or `analyzer` stage. */
	godot::Dictionary stages;
};

/** Validate a `case_stages.json` document against the cases actually present under `p_root`. */
CorpusStageManifest validate_stage_manifest(const godot::Dictionary &p_stages, const godot::String &p_root,
		const godot::String &p_revision);

/** Every `.barista` source under `p_root`, recursively, sorted ascending. */
godot::PackedStringArray fixture_source_paths(const godot::String &p_root);

/** The fixture sources an analyzer-staging run makes available, strictly ascending. */
godot::PackedStringArray staging_fixture_paths();

/** Whether `p_path` is `p_root` or lies beneath it. */
bool corpus_path_under(const godot::String &p_path, const godot::String &p_root);

/** Normalize a `res://`, `user://` or absolute filesystem corpus root to a resource path. */
godot::String normalize_corpus_root(const godot::String &p_root, godot::String *r_error);

struct CorpusModeReport {
	/** False when no `--corpus-case=` was given, which is every ordinary suite run. */
	bool selected = false;
	godot::String error;
	godot::String relative_case;
	CorpusOutcome outcome;
};

/** Discover, stage, evaluate and compare the case named by `--corpus-root=`/`--corpus-case=`. */
CorpusModeReport run_selected_corpus_case();

/** Print `BS_CASE_RESULT` and then `BS_CASE_RAN`, in that order. */
void emit_corpus_guards(const CorpusModeReport &p_report);

} //namespace barista_script::native_tests
