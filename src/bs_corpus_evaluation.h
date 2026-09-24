/**************************************************************************/
/*  bs_corpus_evaluation.h                                                */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

// Corpus evaluation reads the warning subsystem (`BSParser::get_warnings`,
// `BSWarning`, `BSParser::update_project_settings`) and the scoped corpus state of the
// cache, declaration index and conformance registry. All of those exist only in debug
// builds, so this surface follows them rather than duplicating any of them.
#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/script_backtrace.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace barista_script {

/**
 * The outcome of statically evaluating one corpus case.
 *
 * `output` is the exact block a `.out` expectation is compared against: one
 * `>> ERROR at line N: message` per parser/analyzer error in source order, or one
 * `~~ WARNING at line N: (CODE) message` per warning in emission order, or the bare
 * success sentinel. Errors suppress warnings. The block is `\n`-joined with no
 * trailing newline and never contains `\r`.
 */
struct CorpusResult {
	godot::String output;
	bool ok = false;
	bool analysis_ran = false;
	bool infrastructure_error = false;
	/**
	 * Whether the fixture counts below are reported at all. They are only reported for a case
	 * that named fixtures and reached the front-end run; a case with no fixtures, and any case
	 * rejected before that point -- including one rejected partway through fixture discovery --
	 * reports no counts rather than zeroed ones.
	 */
	bool fixture_index_reported = false;
	int fixture_sources = 0;
	int fixture_heads = 0;
	int fixture_annotation_providers = 0;
	int fixture_conformance_providers = 0;
};

class BSParser;

/**
 * Reads back everything the engine published while one runtime case ran.
 *
 * A runtime transcript is not returned by anything: `print` output goes to the engine's message
 * channel and a runtime error goes to its error channel, so the only honest way to render one is
 * to read both channels in the order the engine wrote them. Upstream installs engine-internal
 * print and error handlers (`tests/fs_test_runner.cpp:812-857`); a GDExtension gets neither, and
 * a `Logger` registered with `OS::add_logger` is the stock surface that carries both.
 *
 * One `lines` array holds both channels precisely so emission order survives: a case that prints,
 * fails, and prints again must render in that order, and two separate arrays could not say so.
 */
class BaristaScriptCorpusTranscript final : public godot::Logger {
	GDCLASS(BaristaScriptCorpusTranscript, godot::Logger)

protected:
	static void _bind_methods() {}

public:
	godot::PackedStringArray lines;
	/** Whether any error line was published, which is what selects the runtime-error status. */
	bool saw_error = false;

	BaristaScriptCorpusTranscript() { live_count++; }
	~BaristaScriptCorpusTranscript() override { live_count--; }

	/**
	 * How many transcript readers are alive right now.
	 *
	 * A logger left registered with `OS` after its case finished is the worst failure this
	 * surface has: every later case in the same process inherits its lines, so hundreds of
	 * results turn false at once while the run still exits zero. `OS` holds the only other
	 * reference, so a reader that outlives its case is exactly a reader `OS` did not release,
	 * and this counter is how a case proves the removal happened rather than assuming it.
	 */
	static int live_instances() { return live_count; }

	void _log_error(const godot::String &p_function, const godot::String &p_file, int32_t p_line,
			const godot::String &p_code, const godot::String &p_rationale, bool p_editor_notify,
			int32_t p_error_type, const godot::TypedArray<godot::Ref<godot::ScriptBacktrace>> &p_backtraces) override;
	void _log_message(const godot::String &p_message, bool p_error) override;

private:
	static int live_count;
};

/**
 * Render the diagnostics a completed front-end run left on `p_parser` as the corpus
 * expectation block. This is the single definition of that rendering; a second spelling
 * of it anywhere would be a silent corpus-wide false pass.
 */
CorpusResult format_corpus_result(const BSParser &p_parser, godot::Error p_error, bool p_analysis_ran);

/**
 * Statically evaluate one case. Never compiles or executes script code.
 *
 * `p_stage` must be "parser" or "analyzer"; `p_path` must be a simplified `res://`
 * path ending in `.barista`; `p_fixture_paths` must be strictly ascending. Any
 * violation returns an infrastructure error rather than a diagnostic.
 */
CorpusResult evaluate_corpus_case(const godot::PackedByteArray &p_bytes, const godot::String &p_path,
		const godot::String &p_stage, const godot::PackedStringArray &p_fixture_paths);

/**
 * The three status tokens a runtime transcript may open with.
 *
 * They keep the upstream `FS_` spelling because they are the first line of an imported
 * expectation, not a BaristaScript sentinel: renaming them here would mean rewriting 394
 * pinned `.out` files to say something the pinned producer never wrote.
 */
inline constexpr const char *RUNTIME_STATUS_OK = "FS_TEST_OK";
inline constexpr const char *RUNTIME_STATUS_RUNTIME_ERROR = "FS_TEST_RUNTIME_ERROR";
inline constexpr const char *RUNTIME_STATUS_ANALYZER_ERROR = "FS_TEST_ANALYZER_ERROR";

/**
 * The line a case that never reached the virtual machine emits.
 *
 * A script the front-end accepts and the compiler refuses has no upstream transcript shape --
 * the producer's engine compiled every case it ran. Rendering that refusal as one of the
 * upstream line shapes would let a half-implemented family coincide with a real expectation
 * and read as a pass, so it gets a prefix that appears in no imported `.out` file and
 * therefore can never compare equal to one.
 */
inline constexpr const char *RUNTIME_COMPILE_ERROR_PREFIX = ">> COMPILE ERROR: ";

/**
 * Execute one case and render the whole transcript it emits.
 *
 * Unlike `evaluate_corpus_case()` above, this compiles the case, attaches it to a fresh
 * instance of its declared base type, calls `test()`, and returns everything the engine
 * published while it ran: the status token line, then the analyzer warnings, then `print`
 * output and error lines interleaved in emission order.
 *
 * `p_stage` must be "runtime"; `p_path` must be a simplified `res://` path ending in
 * `.barista`; `p_fixture_paths` must be strictly ascending. Any violation returns an
 * infrastructure error rather than a transcript. This is the single definition of runtime
 * transcript rendering, for the same reason `format_corpus_result` is the single definition
 * of the static one.
 */
CorpusResult evaluate_runtime_case(const godot::PackedByteArray &p_bytes, const godot::String &p_path,
		const godot::String &p_stage, const godot::PackedStringArray &p_fixture_paths);

} // namespace barista_script

#endif // DEBUG_ENABLED
