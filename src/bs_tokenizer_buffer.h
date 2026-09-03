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
 * Two divergences, both forced and both narrow:
 *
 *   - D1 deletes the trailing numeric descriptor byte every literal carried upstream
 *     (fs_tokenizer_buffer.h:52, fs_tokenizer_buffer.cpp:71-77). With one integer type a literal's
 *     Variant carrier is the whole of its type, so there is nothing left to describe. The
 *     descriptor's validation on read (fs_tokenizer_buffer.cpp:112-120) goes with it.
 *   - Constants are length-prefixed. Upstream's `decode_variant` reports how many bytes one value
 *     consumed; the godot-cpp serializer the seam reaches (`BSMarshalls::decode_variant`) does not,
 *     so the framing the reader needs is written down instead of inferred.
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
	static constexpr uint32_t TOKENIZER_VERSION = 2;
	static constexpr uint32_t TOKEN_BYTE_MASK = 0x80;
	static constexpr uint32_t TOKEN_BITS = 8;
	static constexpr uint32_t TOKEN_MASK = (1 << (TOKEN_BITS - 1)) - 1;
	// Token types are indexed into the low bits of the record, so the enum has to stay inside the
	// mask; a wider enum would silently alias two types onto one encoding.
	static_assert(Token::TK_MAX <= TOKEN_MASK, "Token::Type no longer fits in the buffer's token field.");

	Vector<StringName> identifiers;
	Vector<Variant> constants;
	Vector<int> continuation_lines;
	HashMap<int, int> token_lines;
	HashMap<int, int> token_columns;
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
