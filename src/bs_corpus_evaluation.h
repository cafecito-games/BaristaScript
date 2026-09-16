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
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

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

} // namespace barista_script

#endif // DEBUG_ENABLED
