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
inline constexpr const char *CORPUS_ANALYZER_ROOT = "res://tests/corpus/analyzer";

/**
 * The corpus root whose cases are compiled and executed rather than statically evaluated.
 *
 * It stages no fixture sources. An analyzer case names its dependencies through a fixture list
 * this harness publishes into a temporary declaration index; a runtime case `preload`s a
 * `.notest.barista` helper that sits beside it on disk, so the ordinary resource path already
 * resolves it and a staged copy would be a second, divergent one.
 */
inline constexpr const char *CORPUS_RUNTIME_ROOT = "res://tests/corpus/runtime";

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
 * Every returned array is sorted, so a result never depends on directory iteration order --
 * including `discovery_errors`, which reaches operators through joined diagnostics and would
 * otherwise emerge in the LIFO order of the traversal stack.
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

/** The one spelling of the discovery complaint about a symlinked corpus entry. */
godot::String symlink_error(const godot::String &p_path);

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
godot::PackedStringArray analyzer_fixture_paths();

/** Whether `p_path` is `p_root` or lies beneath it. */
bool corpus_path_under(const godot::String &p_path, const godot::String &p_root);

/**
 * Normalize a `res://`, `user://` or absolute filesystem corpus root to a resource path.
 *
 * A root outside the project, a missing one, and a symlinked one are all refused. Refusing a
 * symlinked root rather than resolving it is what lets the alias machinery of
 * `project/tests/corpus_harness.gd` stay unported: that machinery exists to stop an
 * expectation update writing through an alias, and a read-only run has no reason to follow one.
 */
godot::String normalize_corpus_root(const godot::String &p_root, godot::String *r_error);

/**
 * The index into `p_discovery.cases` of the single case at `p_root`/`p_relative`.
 *
 * Returns -1 with `r_error` set when the case was not discovered, and separately when the path
 * matched more than once: an ambiguous identity is refused rather than resolved to either
 * match, and says so rather than claiming the case was absent.
 */
int select_discovered_case(const CorpusDiscovery &p_discovery, const godot::String &p_root,
		const godot::String &p_relative, godot::String *r_error);

struct CorpusModeReport {
	/** False when no `--corpus-case=` was given, which is every ordinary suite run. */
	bool selected = false;
	/**
	 * An infrastructure complaint raised before, or instead of, a comparison. It is also
	 * copied into `outcome` so the emitted payload is self-describing for this failure class
	 * exactly as it is for every other one.
	 */
	godot::String error;
	godot::String relative_case;
	CorpusOutcome outcome;
};

/**
 * Fill `p_report.outcome` from an infrastructure `error`, so that failure class emits a
 * self-describing payload instead of a blank default the way every other class already does.
 * Does nothing when no case was selected or no error was raised.
 */
void describe_infrastructure_failure(CorpusModeReport &p_report);

/** Discover, stage, evaluate and compare the case named by `--corpus-root=`/`--corpus-case=`. */
CorpusModeReport run_selected_corpus_case();

/** Print `BS_CASE_RESULT` and then `BS_CASE_RAN`, in that order. */
void emit_corpus_guards(const CorpusModeReport &p_report);

/**
 * Everything a corpus root contributes to a case evaluation, established once.
 *
 * A single-case run and a whole-corpus run both build this the same way and then hand the
 * same discovery, stage manifest and fixture list to the same evaluation, so neither path
 * can drift into adjudicating a case differently from the other.
 */
struct CorpusEnvironment {
	/** A root-level complaint. Every other member is meaningless when this is set. */
	godot::String error;
	godot::String root;
	CorpusDiscovery discovery;
	/** Absolute `res://` case path to its validated stage. */
	godot::Dictionary stages;
	godot::PackedStringArray fixture_paths;
};

/** Normalize the root, discover its cases, and validate the registry and stage manifest. */
CorpusEnvironment prepare_corpus_environment(const godot::String &p_root_argument);

/** Evaluate `p_environment.discovery.cases[p_index]` at its manifest stage. */
CorpusModeReport evaluate_environment_case(const CorpusEnvironment &p_environment, int p_index);

/**
 * The completion evidence a whole-corpus run emits about itself.
 *
 * One process evaluating every case cannot rely on its own exit status the way a
 * one-case-per-process run can: a crash or a hang partway through still leaves every record
 * it already printed on stdout. `planned` and `completed` are what let a supervisor tell a
 * finished run from a truncated one, and they are the only reason the fast path is safe.
 */
struct CorpusWholeRunReport {
	godot::String error;
	int planned = 0;
	int completed = 0;
	/** Where this process's contiguous slice starts in the ordered population. */
	int start = 0;
	/** The whole population this process discovered, of which `planned` is its slice. */
	int total = 0;
};

/** The half-open `[start, end)` slice of `p_total` cases belonging to shard `p_shard`. */
struct CorpusShardSlice {
	int start = 0;
	int end = 0;
};

/**
 * Split `p_total` into `p_shard_count` contiguous slices and return the one at `p_shard`.
 *
 * Slices differ in length by at most one and the earlier shards take the remainder, so the
 * slices of a given population tile it exactly: no case is left out and none is claimed twice.
 */
CorpusShardSlice corpus_shard_slice(int p_total, int p_shard, int p_shard_count);

/**
 * Evaluate this process's slice of the cases under `--corpus-root=`, emitting the same guard
 * pair per case that a single-case run emits, bracketed by `BS_CORPUS_PLAN` and
 * `BS_CORPUS_COMPLETE`.
 *
 * `--corpus-order=` orders the whole population before it is sliced, so reversing changes both
 * the sequence within a shard and which cases share a process. `BS_CORPUS_COMPLETE` is printed
 * only after the last planned case has been emitted, so a shard that stops early is missing it
 * and cannot be mistaken for a whole one. A root-level failure returns before `BS_CORPUS_PLAN`
 * is printed and emits neither.
 */
CorpusWholeRunReport run_whole_corpus();

} //namespace barista_script::native_tests
