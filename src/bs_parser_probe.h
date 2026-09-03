/**************************************************************************/
/*  bs_parser_probe.h                                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace barista_script {

/**
 * The parser's test surface.
 *
 * godot-cpp's `String`, `StringName` and `Variant` are engine-backed -- they dispatch through the
 * GDExtension interface and do not work without a loaded Godot runtime -- so there is no standalone
 * C++ test binary in which to exercise a ported front-end. Everything runs inside headless Godot
 * instead, which means `BSParser` has to be reachable from GDScript to be testable at all. This
 * class is that reach: it owns no parsing behaviour and adds no policy, it only renders what one
 * `BSParser` run produced into values a GDScript suite can compare exactly.
 *
 * Source is taken as raw bytes rather than as a `String`, for the reason the tokenizer probe takes
 * bytes: a `String` has already been decoded, and a malformed byte sequence has already become
 * U+FFFD by then, which is exactly the failure the front-end must report rather than absorb.
 */
class BaristaScriptParserProbe final : public godot::RefCounted {
	GDCLASS(BaristaScriptParserProbe, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	/**
	 * Parses `p_source_utf8` from text and reports everything one run produced:
	 *
	 *   `error`        the `Error` code `BSParser::parse()` returned.
	 *   `complete`     false when the run reported any diagnostic. This is how a consumer tells a
	 *                  recovered tree from a clean one: the parser recovers and keeps building, so
	 *                  a tree is only ever complete when nothing was reported against it.
	 *   `has_tree`     whether a head class exists at all.
	 *   `tokenizer_failed`  whether the source failed to tokenize. A tree still exists when it did --
	 *                  the parser recovers, which is what an editor needs mid-edit -- so this is how
	 *                  a consumer refuses a tree built from source that never lexed.
	 *   `diagnostics`  one line per diagnostic, in source order, as
	 *                  `line:column-end_line:end_column<TAB>message`.
	 *   `nodes`        every node the run allocated, in allocation order, as
	 *                  `TYPE<TAB>start_line:start_column-end_line:end_column`. This is the tree's
	 *                  shape *and* its positions, which is what identity assertions compare.
	 *   `node_types`   the distinct node kinds present, sorted, for vocabulary-coverage assertions.
	 *   `tree`         `BSParser::TreePrinter`'s rendering, or "" when there is no tree.
	 */
	godot::Dictionary parse_text(const godot::PackedByteArray &p_source_utf8, const godot::String &p_script_path) const;

	/**
	 * The same report, from a token buffer instead of from text.
	 *
	 * This is the cache-warm path: a cached parse replays the compiled token buffer rather than
	 * re-reading source, so a cold report and this one must agree node for node and position for
	 * position. The buffer is produced from the same bytes by `tokenize_to_buffer()`.
	 */
	godot::Dictionary parse_token_buffer(const godot::PackedByteArray &p_token_buffer, const godot::String &p_script_path) const;

	/**
	 * Two reports from **one** `BSParser`: a text parse of `p_source_utf8`, then a buffer parse of
	 * `p_token_buffer` on the same instance.
	 *
	 * Every other method here builds a fresh parser per call, which is exactly what a reuse defect
	 * hides behind. `BSParser` is reused in the compiler, so "nothing from one run reaches the next"
	 * has to be assertable, and this is the only way to assert it from GDScript.
	 */
	godot::Array reused_parse_reports(const godot::PackedByteArray &p_source_utf8, const godot::PackedByteArray &p_token_buffer, const godot::String &p_script_path) const;

	/** The compiled token buffer for this source, which is what a cache-warm parse replays. */
	godot::PackedByteArray tokenize_to_buffer(const godot::PackedByteArray &p_source_utf8, bool p_compress) const;

	/** Every `Node::Type` name, in enum order, for vocabulary-closure assertions. */
	godot::PackedStringArray node_type_names() const;

	/** The one definition of the diagnostic a D1-removed type spelling reports. */
	godot::String removed_type_name_diagnostic(const godot::String &p_spelling) const;

	/**
	 * A source built by nesting `p_open`/`p_close` `p_depth` times inside `p_prefix`/`p_suffix`.
	 *
	 * The recursion-guard fixture needs a nesting depth past `BSParser::MAX_NESTING_DEPTH`, which is
	 * 1024; a checked-in fixture file at that depth would be unreadable and its point invisible, and
	 * building it in GDScript would put the bound in a second place. This builds it from the bound
	 * itself.
	 */
	godot::PackedByteArray nested_source(const godot::String &p_prefix, const godot::String &p_open, const godot::String &p_close, const godot::String &p_suffix, int p_depth) const;

	/** `BSParser::MAX_NESTING_DEPTH`, so a test cannot restate it. */
	int max_nesting_depth() const;
};

} // namespace barista_script
