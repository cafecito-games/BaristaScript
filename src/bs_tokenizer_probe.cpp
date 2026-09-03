/**************************************************************************/
/*  bs_tokenizer_probe.cpp                                                */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_tokenizer_probe.h"

#ifdef DEBUG_ENABLED

#include "bs_tokenizer.h"
#include "bs_tokenizer_buffer.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace barista_script {

namespace {

// A tokenizer that never reaches end of file would hang the test runner rather than fail it. No
// legal source produces more tokens than it has code points plus its layout tokens, so a cap
// several times that is unreachable in practice and turns a hang into a visible defect.
constexpr int MAX_SCANNED_TOKENS = 4000000;

// Fields are tab-separated, not `|`-separated: `|`, `||` and `|=` are themselves token names, so a
// `|` delimiter would make a rendered line ambiguous exactly where the operator vocabulary is
// densest. No token name contains a tab.
const char *FIELD_SEPARATOR = "\t";

String render_literal(const Variant &p_literal) {
	return vformat("%s:%s", Variant::get_type_name(p_literal.get_type()), String(p_literal).c_escape());
}

bool is_layout_token(BSTokenizer::Token::Type p_type) {
	return p_type == BSTokenizer::Token::NEWLINE || p_type == BSTokenizer::Token::INDENT || p_type == BSTokenizer::Token::DEDENT;
}

// The token kinds whose `literal` the buffer format actually stores: an identifier pool entry, a
// constant pool entry, or a diagnostic message. Every other kind carries no literal from the text
// tokenizer, and `BSTokenizerBuffer::_binary_to_token` deliberately fills one in from the token
// name on replay, so comparing those would compare the reconstruction rather than the round trip.
bool carries_literal(BSTokenizer::Token::Type p_type) {
	return p_type == BSTokenizer::Token::ANNOTATION || p_type == BSTokenizer::Token::IDENTIFIER ||
			p_type == BSTokenizer::Token::RESERVED_TYPE_NAME || p_type == BSTokenizer::Token::LITERAL ||
			p_type == BSTokenizer::Token::ERROR;
}

String render_significant_token(const BSTokenizer::Token &p_token) {
	if (!carries_literal(p_token.type)) {
		return BSTokenizer::get_token_name(p_token.type);
	}
	return vformat("%s%s%s", BSTokenizer::get_token_name(p_token.type), FIELD_SEPARATOR, render_literal(p_token.literal));
}

} // namespace

void BaristaScriptTokenizerProbe::_bind_methods() {
	ClassDB::bind_method(D_METHOD("first_diagnostic", "source_utf8"), &BaristaScriptTokenizerProbe::first_diagnostic);
	ClassDB::bind_method(D_METHOD("dump_tokens", "source_utf8"), &BaristaScriptTokenizerProbe::dump_tokens);
	ClassDB::bind_method(D_METHOD("dump_significant_tokens", "source_utf8"), &BaristaScriptTokenizerProbe::dump_significant_tokens);
	ClassDB::bind_method(D_METHOD("dump_buffer_significant_tokens", "source_utf8", "compress"), &BaristaScriptTokenizerProbe::dump_buffer_significant_tokens);
	ClassDB::bind_method(D_METHOD("token_type_names"), &BaristaScriptTokenizerProbe::token_type_names);
	ClassDB::bind_method(D_METHOD("keyword_spellings"), &BaristaScriptTokenizerProbe::keyword_spellings);
	ClassDB::bind_method(D_METHOD("reserved_spellings"), &BaristaScriptTokenizerProbe::reserved_spellings);
}

String BaristaScriptTokenizerProbe::first_diagnostic(const PackedByteArray &p_source_utf8) const {
	String source;
	String decode_error;
	if (!BSTokenizer::decode_source(p_source_utf8, &source, &decode_error)) {
		return decode_error;
	}

	BSTokenizerText tokenizer;
	tokenizer.set_source_code(source);
	for (int scanned = 0; scanned < MAX_SCANNED_TOKENS; scanned++) {
		const BSTokenizer::Token token = tokenizer.scan();
		if (token.type == BSTokenizer::Token::ERROR) {
			return token.literal;
		}
		if (token.type == BSTokenizer::Token::TK_EOF) {
			return String();
		}
	}
	return "Tokenizer did not reach end of file.";
}

PackedStringArray BaristaScriptTokenizerProbe::dump_tokens(const PackedByteArray &p_source_utf8) const {
	PackedStringArray lines;
	String source;
	String decode_error;
	if (!BSTokenizer::decode_source(p_source_utf8, &source, &decode_error)) {
		lines.push_back(decode_error);
		return lines;
	}

	BSTokenizerText tokenizer;
	tokenizer.set_source_code(source);
	for (int scanned = 0; scanned < MAX_SCANNED_TOKENS; scanned++) {
		const BSTokenizer::Token token = tokenizer.scan();
		lines.push_back(vformat("%s%s%d:%d-%d:%d%s%s",
				BSTokenizer::get_token_name(token.type), FIELD_SEPARATOR,
				token.start_line, token.start_column, token.end_line, token.end_column,
				FIELD_SEPARATOR, render_literal(token.literal)));
		if (token.type == BSTokenizer::Token::TK_EOF) {
			break;
		}
	}
	return lines;
}

PackedStringArray BaristaScriptTokenizerProbe::dump_significant_tokens(const PackedByteArray &p_source_utf8) const {
	PackedStringArray lines;
	String source;
	String decode_error;
	if (!BSTokenizer::decode_source(p_source_utf8, &source, &decode_error)) {
		lines.push_back(decode_error);
		return lines;
	}

	BSTokenizerText tokenizer;
	tokenizer.set_source_code(source);
	for (int scanned = 0; scanned < MAX_SCANNED_TOKENS; scanned++) {
		const BSTokenizer::Token token = tokenizer.scan();
		if (token.type == BSTokenizer::Token::TK_EOF) {
			break;
		}
		if (is_layout_token(token.type)) {
			continue;
		}
		lines.push_back(render_significant_token(token));
	}
	return lines;
}

PackedStringArray BaristaScriptTokenizerProbe::dump_buffer_significant_tokens(const PackedByteArray &p_source_utf8, bool p_compress) const {
	PackedStringArray lines;
	String source;
	String decode_error;
	if (!BSTokenizer::decode_source(p_source_utf8, &source, &decode_error)) {
		lines.push_back(decode_error);
		return lines;
	}

	const PackedByteArray buffer = BSTokenizerBuffer::parse_code_string(
			source, p_compress ? BSTokenizerBuffer::COMPRESS_ZSTD : BSTokenizerBuffer::COMPRESS_NONE);

	BSTokenizerBuffer tokenizer;
	const Error error = tokenizer.set_code_buffer(buffer);
	if (error != OK) {
		lines.push_back(vformat("buffer rejected (error %d)", int(error)));
		return lines;
	}

	for (int scanned = 0; scanned < MAX_SCANNED_TOKENS; scanned++) {
		const BSTokenizer::Token token = tokenizer.scan();
		if (token.type == BSTokenizer::Token::TK_EOF) {
			break;
		}
		if (is_layout_token(token.type)) {
			continue;
		}
		lines.push_back(render_significant_token(token));
	}
	return lines;
}

PackedStringArray BaristaScriptTokenizerProbe::token_type_names() const {
	PackedStringArray names;
	for (int type = 0; type < BSTokenizer::Token::TK_MAX; type++) {
		names.push_back(BSTokenizer::get_token_name((BSTokenizer::Token::Type)type));
	}
	return names;
}

PackedStringArray BaristaScriptTokenizerProbe::keyword_spellings() const {
	PackedStringArray spellings;
	for (const String &spelling : BSTokenizer::get_keyword_spellings()) {
		spellings.push_back(spelling);
	}
	return spellings;
}

PackedStringArray BaristaScriptTokenizerProbe::reserved_spellings() const {
	PackedStringArray spellings;
	for (const String &spelling : BSTokenizer::get_reserved_spellings()) {
		spellings.push_back(spelling);
	}
	return spellings;
}

} // namespace barista_script

#endif // DEBUG_ENABLED
