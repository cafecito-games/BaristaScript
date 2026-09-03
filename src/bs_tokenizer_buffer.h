/**************************************************************************/
/*  bs_tokenizer_buffer.h                                                 */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_tokenizer.h"

namespace barista_script {

/**
 * Hard fork of Foundry's token buffer (`modules/foundry_script/fs_tokenizer_buffer.h` @
 * c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6). It replays a tokenized file from a compiled byte
 * buffer instead of from source, which is how the parser is fed when only compiled code shipped.
 *
 * Three divergences, each forced and each narrow:
 *
 *   - D1 deletes the trailing numeric descriptor byte every literal carried upstream
 *     (fs_tokenizer_buffer.h:52, fs_tokenizer_buffer.cpp:71-77). With one integer type a literal's
 *     Variant carrier is the whole of its type, so there is nothing left to describe. The
 *     descriptor's validation on read (fs_tokenizer_buffer.cpp:112-120) goes with it.
 *   - Constants are length-prefixed. Upstream's `decode_variant` reports how many bytes one value
 *     consumed; the godot-cpp serializer the seam reaches (`BSMarshalls::decode_variant`) does not,
 *     so the framing the reader needs is written down instead of inferred.
 *   - D2 stores a token's whole span -- start line, start column, end line, end column -- where
 *     upstream stores only its start line (fs_tokenizer_buffer.cpp:79). Upstream's `token_lines` /
 *     `token_columns` maps hold one line per token and a column only for the first token of each
 *     line, which is all the INDENT/DEDENT regeneration needs but leaves a replayed tree with no
 *     columns and no end positions. Those are the anchors every diagnostic the analyzer and the
 *     LSP emit, so a cache-warm parse could differ from a cold one in exactly the dimension no
 *     test could see. The maps are kept as they are, for the layout regeneration they exist for;
 *     the span is carried on the token record beside them.
 *
 * The magic and version therefore identify BaristaScript's own format: it is not Foundry's, and a
 * Foundry buffer must be rejected rather than misread.
 */
class BSTokenizerBuffer : public BSTokenizer {
public:
	enum CompressMode {
		COMPRESS_NONE,
		COMPRESS_ZSTD,
	};

	// Layout version of the token buffer only. It is deliberately independent of any compiled
	// bytecode format version: the two formats change for different reasons and are read by
	// different loaders. Increment it whenever `Token::Type` or the layout below changes.
	//
	// 3: token records carry the whole span (D2) instead of the start line alone. A version-2
	//    record is 5 or 8 bytes where a version-3 record is 17 or 20, so a version-2 buffer read
	//    as version 3 would resolve nonsense positions rather than fail; the version check is what
	//    stops it, and `set_code_buffer` refuses any tag but this one.
	static constexpr uint32_t TOKENIZER_VERSION = 3;
	static constexpr uint32_t TOKEN_BYTE_MASK = 0x80;
	static constexpr uint32_t TOKEN_BITS = 8;
	static constexpr uint32_t TOKEN_MASK = (1 << (TOKEN_BITS - 1)) - 1;
	// The span trailer every token record carries after its type field: start line, start column,
	// end line, end column, each a uint32.
	static constexpr int TOKEN_SPAN_SIZE = 16;
	// The two token record sizes: a one-byte type field for a type that needs no pool index, a
	// four-byte one for a type that does. Both are followed by the span trailer.
	static constexpr int TOKEN_SHORT_SIZE = 1 + TOKEN_SPAN_SIZE;
	static constexpr int TOKEN_LONG_SIZE = 4 + TOKEN_SPAN_SIZE;
	// One line-start record: the token index, the line and column the replay resumes at, and the
	// span of the NEWLINE regenerated before that token.
	static constexpr int LINE_MARKER_SIZE = 3 * 4 + TOKEN_SPAN_SIZE;
	// The two spans that follow the line-start records: the file's final NEWLINE and its EOF.
	static constexpr int TRAILING_SPANS_SIZE = 2 * TOKEN_SPAN_SIZE;
	// Token types are indexed into the low bits of the record, so the enum has to stay inside the
	// mask; a wider enum would silently alias two types onto one encoding.
	static_assert(Token::TK_MAX <= TOKEN_MASK, "Token::Type no longer fits in the buffer's token field.");

	/**
	 * A position pair, for the layout tokens the replay regenerates.
	 *
	 * A NEWLINE, INDENT or DEDENT is never stored as a token record -- the buffer omits layout
	 * tokens and `scan()` builds them back from the indentation bookkeeping -- so its position has
	 * to come from somewhere. An INDENT's and a DEDENT's follow from the token they precede
	 * (`bs_tokenizer.cpp`, `BSTokenizerText::scan`, the `pending_indents` block), but a NEWLINE's
	 * does not: it sits at the end of a line, past any trailing whitespace or comment, so it is
	 * written down.
	 */
	struct TokenSpan {
		int start_line = 0;
		int start_column = 0;
		int end_line = 0;
		int end_column = 0;
	};

	Vector<StringName> identifiers;
	Vector<Variant> constants;
	Vector<int> continuation_lines;
	HashMap<int, int> token_lines;
	HashMap<int, int> token_columns;
	// The NEWLINE that precedes the token at this index, keyed exactly as `token_lines` is.
	HashMap<int, TokenSpan> token_newlines;
	// The NEWLINE that follows the last token, and the end-of-file token. Both are regenerated by
	// `scan()` after the stored tokens run out, so neither has a token to derive a position from.
	TokenSpan final_newline;
	TokenSpan eof_span;
	Vector<Token> tokens;
	int current = 0;
	uint32_t current_line = 1;

	bool multiline_mode = false;
	List<int> indent_stack;
	List<List<int>> indent_stack_stack; // For lambdas, which require manipulating the indentation point.
	int pending_indents = 0;
	bool last_token_was_newline = false;

#ifdef TOOLS_ENABLED
	HashMap<int, CommentData> dummy;
#endif // TOOLS_ENABLED

	// Pool key for literal constants. Plain Variant equality and hashing are cross-carrier, so `1`
	// and `1.0` would otherwise share one pool entry and the second literal encoded would read back
	// with the first one's carrier. Pooling per carrier keeps every literal exactly as it was
	// written.
	struct ConstantKey {
		Variant value;

		bool operator==(const ConstantKey &p_other) const {
			return value.get_type() == p_other.value.get_type() && value == p_other.value;
		}
	};

	struct ConstantKeyHasher {
		static _FORCE_INLINE_ uint32_t hash(const ConstantKey &p_key) {
			return hash_murmur3_one_32(uint32_t(p_key.value.get_type()), p_key.value.hash());
		}
	};

	using ConstantMap = HashMap<ConstantKey, uint32_t, ConstantKeyHasher>;

	static int _token_to_binary(const Token &p_token, PackedByteArray &r_buffer, int p_start, HashMap<StringName, uint32_t> &r_identifiers_map, ConstantMap &r_constants_map);
	Token _binary_to_token(const uint8_t *p_buffer);

public:
	Error set_code_buffer(const PackedByteArray &p_buffer);
	static PackedByteArray parse_code_string(const String &p_code, CompressMode p_compress_mode);

	virtual int get_cursor_line() const override;
	virtual int get_cursor_column() const override;
	virtual void set_cursor_position(int p_line, int p_column) override;
	virtual void set_multiline_mode(bool p_state) override;
	virtual bool is_past_cursor() const override;
	virtual void push_expression_indented_block() override; // For lambdas, or blocks inside expressions.
	virtual void pop_expression_indented_block() override; // For lambdas, or blocks inside expressions.
	virtual bool is_text() override { return false; }

#ifdef TOOLS_ENABLED
	virtual const HashMap<int, CommentData> &get_comments() const override {
		return dummy;
	}
#endif // TOOLS_ENABLED

	virtual Token scan() override;
};

} // namespace barista_script
