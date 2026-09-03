/**************************************************************************/
/*  bs_tokenizer_buffer.cpp                                               */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_tokenizer_buffer.h"

// Upstream includes `core/io/compression.h` and `core/io/marshalls.h` here
// (fs_tokenizer_buffer.cpp:32-33). Neither exists in godot-cpp; both are shimmed by the platform
// seam as `BSCompression` and `BSMarshalls`, so `bs_tokenizer.h` -> `bs_platform.h` is the whole
// include block.

namespace barista_script {

namespace {

// The buffer's own magic. It is deliberately not Foundry's "GDSC": the layouts differ, so a
// Foundry buffer must be refused rather than misread as a BaristaScript one.
constexpr uint8_t BUFFER_MAGIC[4] = { 'B', 'S', 'T', 'B' };
constexpr int64_t BUFFER_HEADER_SIZE = 12;
constexpr int64_t CONTENTS_HEADER_SIZE = 16;

void append_uint32(PackedByteArray &r_buffer, uint32_t p_value) {
	const int64_t offset = r_buffer.size();
	r_buffer.resize(offset + 4);
	BSMarshalls::encode_uint32(p_value, r_buffer.ptrw() + offset);
}

} // namespace

int BSTokenizerBuffer::_token_to_binary(const Token &p_token, PackedByteArray &r_buffer, int p_start, HashMap<StringName, uint32_t> &r_identifiers_map, ConstantMap &r_constants_map) {
	int pos = p_start;

	uint32_t token_type = uint32_t(p_token.type) & TOKEN_MASK;

	switch (p_token.type) {
		case Token::ANNOTATION:
		case Token::IDENTIFIER: {
			// Add identifier to map.
			uint32_t identifier_pos;
			StringName id = p_token.get_identifier();
			if (r_identifiers_map.has(id)) {
				identifier_pos = r_identifiers_map[id];
			} else {
				identifier_pos = r_identifiers_map.size();
				r_identifiers_map[id] = identifier_pos;
			}
			token_type |= identifier_pos << TOKEN_BITS;
		} break;
		case Token::ERROR:
		case Token::LITERAL: {
			// Add literal to map.
			const ConstantKey constant_key = { p_token.literal };
			uint32_t constant_pos;
			if (r_constants_map.has(constant_key)) {
				constant_pos = r_constants_map[constant_key];
			} else {
				constant_pos = r_constants_map.size();
				r_constants_map[constant_key] = constant_pos;
			}
			token_type |= constant_pos << TOKEN_BITS;
		} break;
		default:
			break;
	}

	// Encode token. D1 removed the trailing numeric descriptor byte upstream wrote after every
	// literal (fs_tokenizer_buffer.cpp:71-77): with one integer type the pooled Variant carrier is
	// the whole of a literal's type, so there is nothing left for a descriptor to say.
	int token_len;
	if (token_type & TOKEN_MASK) {
		token_len = 8;
		r_buffer.resize(pos + token_len);
		BSMarshalls::encode_uint32(token_type | TOKEN_BYTE_MASK, r_buffer.ptrw() + pos);
		pos += 4;
	} else {
		token_len = 5;
		r_buffer.resize(pos + token_len);
		r_buffer.ptrw()[pos] = uint8_t(token_type);
		pos++;
	}
	BSMarshalls::encode_uint32(uint32_t(p_token.start_line), r_buffer.ptrw() + pos);
	return token_len;
}

BSTokenizer::Token BSTokenizerBuffer::_binary_to_token(const uint8_t *p_buffer) {
	Token token;
	const uint8_t *b = p_buffer;

	uint32_t token_type = BSMarshalls::decode_uint32(b);
	const uint32_t type_index = token_type & TOKEN_MASK;
	// The mask admits 128 values and the enum defines fewer, so a crafted buffer can name a type
	// that does not exist. Refuse it here rather than letting `get_name()` index past the table.
	if (unlikely(type_index >= uint32_t(Token::TK_MAX))) {
		Token error;
		error.type = Token::ERROR;
		error.literal = "Token type out of range.";
		return error;
	}
	token.type = (Token::Type)type_index;
	if (token_type & TOKEN_BYTE_MASK) {
		b += 4;
	} else {
		b++;
	}
	token.start_line = BSMarshalls::decode_uint32(b);
	token.end_line = token.start_line;

	token.literal = token.get_name();
	if (token.type == Token::CONST_NAN) {
		token.literal = String("NAN"); // Special case since name and notation are different.
	}

	switch (token.type) {
		case Token::ANNOTATION:
		case Token::IDENTIFIER: {
			// Get name from map.
			uint32_t identifier_pos = token_type >> TOKEN_BITS;
			if (unlikely(identifier_pos >= uint32_t(identifiers.size()))) {
				Token error;
				error.type = Token::ERROR;
				error.literal = "Identifier index out of bounds.";
				return error;
			}
			token.literal = identifiers[identifier_pos];
		} break;
		case Token::ERROR:
		case Token::LITERAL: {
			// Get literal from map.
			uint32_t constant_pos = token_type >> TOKEN_BITS;
			if (unlikely(constant_pos >= uint32_t(constants.size()))) {
				Token error;
				error.type = Token::ERROR;
				error.literal = "Constant index out of bounds.";
				return error;
			}
			token.literal = constants[constant_pos];
		} break;
		default:
			break;
	}

	return token;
}

Error BSTokenizerBuffer::set_code_buffer(const PackedByteArray &p_buffer) {
	ERR_FAIL_COND_V(p_buffer.size() < BUFFER_HEADER_SIZE, ERR_INVALID_DATA);
	const uint8_t *buf = p_buffer.ptr();
	ERR_FAIL_COND_V(buf[0] != BUFFER_MAGIC[0] || buf[1] != BUFFER_MAGIC[1] || buf[2] != BUFFER_MAGIC[2] || buf[3] != BUFFER_MAGIC[3], ERR_INVALID_DATA);

	const uint32_t version = BSMarshalls::decode_uint32(&buf[4]);
	ERR_FAIL_COND_V_MSG(version != TOKENIZER_VERSION, ERR_INVALID_DATA, "Binary BaristaScript is not compatible with this version of the extension.");

	const uint32_t decompressed_size = BSMarshalls::decode_uint32(&buf[8]);

	PackedByteArray contents;
	if (decompressed_size == 0) {
		contents = p_buffer.slice(BUFFER_HEADER_SIZE);
	} else {
		contents = BSCompression::decompress_zstd(p_buffer.slice(BUFFER_HEADER_SIZE), int64_t(decompressed_size));
		ERR_FAIL_COND_V_MSG(contents.size() != int64_t(decompressed_size), ERR_INVALID_DATA, "Error decompressing BaristaScript tokenizer buffer.");
	}

	int64_t total_len = contents.size();
	ERR_FAIL_COND_V(total_len < CONTENTS_HEADER_SIZE, ERR_INVALID_DATA);
	buf = contents.ptr();
	const uint32_t identifier_count = BSMarshalls::decode_uint32(&buf[0]);
	const uint32_t constant_count = BSMarshalls::decode_uint32(&buf[4]);
	const uint32_t token_line_count = BSMarshalls::decode_uint32(&buf[8]);
	const uint32_t token_count = BSMarshalls::decode_uint32(&buf[12]);

	int64_t cursor = CONTENTS_HEADER_SIZE;
	total_len -= CONTENTS_HEADER_SIZE;

	// Every record has a minimum encoded size, so a count larger than the remaining bytes could
	// pay for is malformed however the rest of the buffer reads. Checking here, before the
	// resizes below, keeps a crafted header from asking for an allocation the buffer could never
	// have contained.
	ERR_FAIL_COND_V(int64_t(identifier_count) * 4 > total_len, ERR_INVALID_DATA);
	ERR_FAIL_COND_V(int64_t(constant_count) * 4 > total_len, ERR_INVALID_DATA);
	ERR_FAIL_COND_V(int64_t(token_line_count) * 16 > total_len, ERR_INVALID_DATA);
	ERR_FAIL_COND_V(int64_t(token_count) * 5 > total_len, ERR_INVALID_DATA);

	identifiers.resize(identifier_count);
	for (uint32_t i = 0; i < identifier_count; i++) {
		ERR_FAIL_COND_V(total_len < 4, ERR_INVALID_DATA);
		const uint32_t len = BSMarshalls::decode_uint32(&buf[cursor]);
		cursor += 4;
		total_len -= 4;
		ERR_FAIL_COND_V(int64_t(len) * 4 > total_len, ERR_INVALID_DATA);
		String name;
		for (uint32_t j = 0; j < len; j++) {
			uint8_t tmp[4];
			for (uint32_t k = 0; k < 4; k++) {
				tmp[k] = buf[cursor + int64_t(j) * 4 + k] ^ 0xb6;
			}
			name += char32_t(BSMarshalls::decode_uint32(tmp));
		}
		cursor += int64_t(len) * 4;
		total_len -= int64_t(len) * 4;
		identifiers.write[i] = name;
	}

	constants.resize(constant_count);
	for (uint32_t i = 0; i < constant_count; i++) {
		ERR_FAIL_COND_V(total_len < 4, ERR_INVALID_DATA);
		const uint32_t len = BSMarshalls::decode_uint32(&buf[cursor]);
		cursor += 4;
		total_len -= 4;
		ERR_FAIL_COND_V(int64_t(len) > total_len, ERR_INVALID_DATA);
		constants.write[i] = BSMarshalls::decode_variant(contents.slice(cursor, cursor + int64_t(len)));
		cursor += int64_t(len);
		total_len -= int64_t(len);
	}

	for (uint32_t i = 0; i < token_line_count; i++) {
		ERR_FAIL_COND_V(total_len < 8, ERR_INVALID_DATA);
		const uint32_t token_index = BSMarshalls::decode_uint32(&buf[cursor]);
		const uint32_t line = BSMarshalls::decode_uint32(&buf[cursor + 4]);
		cursor += 8;
		total_len -= 8;
		token_lines[token_index] = line;
	}
	for (uint32_t i = 0; i < token_line_count; i++) {
		ERR_FAIL_COND_V(total_len < 8, ERR_INVALID_DATA);
		const uint32_t token_index = BSMarshalls::decode_uint32(&buf[cursor]);
		const uint32_t column = BSMarshalls::decode_uint32(&buf[cursor + 4]);
		cursor += 8;
		total_len -= 8;
		token_columns[token_index] = column;
	}

	tokens.resize(token_count);
	for (uint32_t i = 0; i < token_count; i++) {
		ERR_FAIL_COND_V(total_len < 1, ERR_INVALID_DATA);
		int token_len = 5;
		if (buf[cursor] & TOKEN_BYTE_MASK) {
			token_len = 8;
		}
		ERR_FAIL_COND_V(total_len < token_len, ERR_INVALID_DATA);
		Token token = _binary_to_token(&buf[cursor]);
		cursor += token_len;
		ERR_FAIL_INDEX_V(token.type, Token::TK_MAX, ERR_INVALID_DATA);
		tokens.write[i] = token;
		total_len -= token_len;
	}

	ERR_FAIL_COND_V(total_len > 0, ERR_INVALID_DATA);

	return OK;
}

PackedByteArray BSTokenizerBuffer::parse_code_string(const String &p_code, CompressMode p_compress_mode) {
	HashMap<StringName, uint32_t> identifier_map;
	ConstantMap constant_map;
	PackedByteArray token_buffer;
	HashMap<uint32_t, uint32_t> token_lines;
	HashMap<uint32_t, uint32_t> token_columns;

	BSTokenizerText tokenizer;
	tokenizer.set_source_code(p_code);
	tokenizer.set_multiline_mode(true); // Ignore whitespace tokens.
	Token current = tokenizer.scan();
	int token_pos = 0;
	int last_token_line = 0;
	uint32_t token_counter = 0;

	while (current.type != Token::TK_EOF) {
		int token_len = _token_to_binary(current, token_buffer, token_pos, identifier_map, constant_map);
		token_pos += token_len;
		if (token_counter > 0 && current.start_line > last_token_line) {
			token_lines[token_counter] = current.start_line;
			token_columns[token_counter] = current.start_column;
		}
		last_token_line = current.end_line;

		current = tokenizer.scan();
		token_counter++;
	}

	// Reverse maps.
	Vector<StringName> rev_identifier_map;
	rev_identifier_map.resize(identifier_map.size());
	for (const KeyValue<StringName, uint32_t> &E : identifier_map) {
		rev_identifier_map.write[E.value] = E.key;
	}
	Vector<Variant> rev_constant_map;
	rev_constant_map.resize(constant_map.size());
	for (const KeyValue<ConstantKey, uint32_t> &E : constant_map) {
		rev_constant_map.write[E.value] = E.key.value;
	}
	HashMap<uint32_t, uint32_t> rev_token_lines;
	for (const KeyValue<uint32_t, uint32_t> &E : token_lines) {
		rev_token_lines[E.value] = E.key;
	}

	// Remove continuation lines from map.
	for (int line : tokenizer.get_continuation_lines()) {
		if (rev_token_lines.has(uint32_t(line))) {
			token_lines.erase(rev_token_lines[uint32_t(line)]);
			token_columns.erase(rev_token_lines[uint32_t(line)]);
		}
	}

	PackedByteArray contents;
	append_uint32(contents, identifier_map.size());
	append_uint32(contents, constant_map.size());
	append_uint32(contents, token_lines.size());
	append_uint32(contents, token_counter);

	// Save identifiers.
	for (const StringName &id : rev_identifier_map) {
		const String s = String(id);
		const int64_t len = s.length();

		append_uint32(contents, uint32_t(len));
		const int64_t offset = contents.size();
		contents.resize(offset + len * 4);
		uint8_t *write = contents.ptrw();
		for (int64_t i = 0; i < len; i++) {
			uint8_t tmp[4];
			BSMarshalls::encode_uint32(uint32_t(s[i]), tmp);
			for (int b = 0; b < 4; b++) {
				write[offset + i * 4 + b] = tmp[b] ^ 0xb6;
			}
		}
	}

	// Save constants. Each is length-prefixed because the seam's `decode_variant` cannot report how
	// many bytes one value consumed; see the note in `bs_tokenizer_buffer.h`.
	for (const Variant &v : rev_constant_map) {
		const PackedByteArray encoded = BSMarshalls::encode_variant(v);
		ERR_FAIL_COND_V_MSG(encoded.is_empty(), PackedByteArray(), "Error when trying to encode Variant.");
		append_uint32(contents, uint32_t(encoded.size()));
		contents.append_array(encoded);
	}

	// Save lines and columns.
	for (const KeyValue<uint32_t, uint32_t> &e : token_lines) {
		append_uint32(contents, e.key);
		append_uint32(contents, e.value);
	}
	for (const KeyValue<uint32_t, uint32_t> &e : token_columns) {
		append_uint32(contents, e.key);
		append_uint32(contents, e.value);
	}

	// Store tokens.
	contents.append_array(token_buffer);

	PackedByteArray buf;

	// Save header.
	buf.resize(BUFFER_HEADER_SIZE);
	uint8_t *header = buf.ptrw();
	for (int i = 0; i < 4; i++) {
		header[i] = BUFFER_MAGIC[i];
	}
	BSMarshalls::encode_uint32(TOKENIZER_VERSION, header + 4);

	switch (p_compress_mode) {
		case COMPRESS_NONE:
			BSMarshalls::encode_uint32(0u, buf.ptrw() + 8);
			buf.append_array(contents);
			break;

		case COMPRESS_ZSTD: {
			BSMarshalls::encode_uint32(uint32_t(contents.size()), buf.ptrw() + 8);
			const PackedByteArray compressed = BSCompression::compress_zstd(contents);
			ERR_FAIL_COND_V_MSG(compressed.is_empty() && !contents.is_empty(), PackedByteArray(), "Error compressing BaristaScript tokenizer buffer.");
			buf.append_array(compressed);
		} break;
	}

	return buf;
}

int BSTokenizerBuffer::get_cursor_line() const {
	return 0;
}

int BSTokenizerBuffer::get_cursor_column() const {
	return 0;
}

void BSTokenizerBuffer::set_cursor_position(int p_line, int p_column) {
}

void BSTokenizerBuffer::set_multiline_mode(bool p_state) {
	multiline_mode = p_state;
}

bool BSTokenizerBuffer::is_past_cursor() const {
	return false;
}

void BSTokenizerBuffer::push_expression_indented_block() {
	indent_stack_stack.push_back(indent_stack);
}

void BSTokenizerBuffer::pop_expression_indented_block() {
	ERR_FAIL_COND(indent_stack_stack.is_empty());
	indent_stack = indent_stack_stack.back()->get();
	indent_stack_stack.pop_back();
}

BSTokenizer::Token BSTokenizerBuffer::scan() {
	// Add final newline.
	if (current >= tokens.size() && !last_token_was_newline) {
		Token newline;
		newline.type = Token::NEWLINE;
		newline.start_line = current_line;
		newline.end_line = current_line;
		last_token_was_newline = true;
		return newline;
	}

	// Resolve pending indentation change.
	if (pending_indents > 0) {
		pending_indents--;
		Token indent;
		indent.type = Token::INDENT;
		indent.start_line = current_line;
		indent.end_line = current_line;
		return indent;
	} else if (pending_indents < 0) {
		pending_indents++;
		Token dedent;
		dedent.type = Token::DEDENT;
		dedent.start_line = current_line;
		dedent.end_line = current_line;
		return dedent;
	}

	if (current >= tokens.size()) {
		if (!indent_stack.is_empty()) {
			pending_indents -= indent_stack.size();
			indent_stack.clear();
			return scan();
		}
		Token eof;
		eof.type = Token::TK_EOF;
		return eof;
	}

	if (!last_token_was_newline && token_lines.has(current)) {
		current_line = token_lines[current];
		uint32_t current_column = token_columns[current];

		// Check if there's a need to indent/dedent.
		if (!multiline_mode) {
			uint32_t previous_indent = 0;
			if (!indent_stack.is_empty()) {
				previous_indent = indent_stack.back()->get();
			}
			if (current_column - 1 > previous_indent) {
				pending_indents++;
				indent_stack.push_back(current_column - 1);
			} else {
				while (current_column - 1 < previous_indent) {
					pending_indents--;
					indent_stack.pop_back();
					if (indent_stack.is_empty()) {
						break;
					}
					previous_indent = indent_stack.back()->get();
				}
			}

			Token newline;
			newline.type = Token::NEWLINE;
			newline.start_line = current_line;
			newline.end_line = current_line;
			last_token_was_newline = true;

			return newline;
		}
	}

	last_token_was_newline = false;

	Token token = tokens[current++];
	return token;
}

} // namespace barista_script
