/**************************************************************************/
/*  bs_tokenizer_probe.h                                                  */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace barista_script {

/**
 * The tokenizer's test surface.
 *
 * godot-cpp's `String`, `StringName` and `Variant` are engine-backed -- they dispatch through the
 * GDExtension interface and do not work without a loaded Godot runtime -- so there is no standalone
 * C++ test binary in which to exercise a ported frontend. Everything runs inside headless Godot
 * instead, which means the tokenizer has to be reachable from GDScript to be testable at all. This
 * class is that reach: it owns no behaviour, it only renders what `BSTokenizerText` and
 * `BSTokenizerBuffer` produce into strings a golden-file corpus can compare exactly.
 *
 * Source is taken as raw bytes rather than as a `String` on purpose. A `String` has already been
 * decoded, and a malformed byte sequence has already become U+FFFD by then -- which is precisely
 * the failure the tokenizer must report rather than absorb.
 */
class BaristaScriptTokenizerProbe final : public godot::RefCounted {
	GDCLASS(BaristaScriptTokenizerProbe, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	/**
	 * The first diagnostic the tokenizer reports for this source, or the empty string when it
	 * reports none. Scanning runs to end of file, so a diagnostic anywhere in the source is found.
	 */
	godot::String first_diagnostic(const godot::PackedByteArray &p_source_utf8) const;

	/**
	 * Every token, one line each, as `type|start_line:start_column-end_line:end_column|literal`.
	 * Positions are the tokenizer's own: 1-based line, 1-based column, start inclusive and end
	 * exclusive.
	 */
	godot::PackedStringArray dump_tokens(const godot::PackedByteArray &p_source_utf8) const;

	/**
	 * Every token that is not layout (`NEWLINE`, `INDENT`, `DEDENT`), as `type|literal`.
	 *
	 * This is the shape the token buffer round-trips: `parse_code_string` tokenizes in multiline
	 * mode, so layout tokens are never written and are regenerated from stored line and column
	 * numbers on replay. Comparing the significant tokens is therefore the exact claim the buffer
	 * makes, rather than a weaker one.
	 */
	godot::PackedStringArray dump_significant_tokens(const godot::PackedByteArray &p_source_utf8) const;

	/** The same rendering, taken from a token buffer written and read back. */
	godot::PackedStringArray dump_buffer_significant_tokens(const godot::PackedByteArray &p_source_utf8, bool p_compress) const;

	/** Every `Token::Type` name, in enum order, for vocabulary-closure assertions. */
	godot::PackedStringArray token_type_names() const;

	/** The keyword spellings, and the spellings D1 reserved without giving them a token type. */
	godot::PackedStringArray keyword_spellings() const;
	godot::PackedStringArray reserved_spellings() const;
};

} // namespace barista_script
