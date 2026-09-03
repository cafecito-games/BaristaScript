/**************************************************************************/
/*  bs_tokenizer.cpp                                                      */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_tokenizer.h"

// Upstream includes `core/error/error_macros.h` and `core/string/char_utils.h` here
// (fs_tokenizer.cpp:33-34); both are mapped by the seam, so `bs_platform.h` is the whole include
// block.
//
// Upstream also includes `servers/text/text_server.h` under `DEBUG_ENABLED` (fs_tokenizer.cpp:37)
// for the `TextServer::FEATURE_UNICODE_SECURITY` confusable-identifier check at
// fs_tokenizer.cpp:672. The platform seam guards that header out (see the
// `servers/text/text_server.h` entry in `src/bs_platform_manifest.json`), so the check is
// deliberately absent here; its call site is marked in `potential_identifier()` below.
//
// Upstream's `editor/settings/editor_settings.h` (fs_tokenizer.cpp:41) is guarded out for the same
// reason: a GDExtension reaches editor settings through `EditorInterface`, not that header, so the
// tab size stays at the language default until the editor-integration milestone.

namespace barista_script {

namespace {

// Upstream spells this `Variant::MAX_RECURSION_DEPTH` (fs_tokenizer.cpp:1616). godot-cpp does not
// expose that constant, so Godot's own value is written here rather than reached for.
constexpr int MAX_CONTINUATION_SCAN_DEPTH = 1024;

// The inclusive bounds of the one integer type (docs/GRAMMAR.md section 7.1). Held as an unsigned
// magnitude so that `-9223372036854775808`, whose magnitude is one past the positive maximum, is
// judged exactly rather than through a signed intermediate that cannot represent it.
constexpr uint64_t INTEGER_MAXIMUM_MAGNITUDE = 9223372036854775807ULL;
constexpr uint64_t INTEGER_MINIMUM_MAGNITUDE = 9223372036854775807ULL + 1ULL;

} // namespace

static const char *token_names[] = {
	"Empty", // EMPTY,
	// Basic
	"Annotation", // ANNOTATION
	"Identifier", // IDENTIFIER,
	"Reserved type name", // RESERVED_TYPE_NAME,
	"Literal", // LITERAL,
	// Comparison
	"<", // LESS,
	"<=", // LESS_EQUAL,
	">", // GREATER,
	">=", // GREATER_EQUAL,
	"==", // EQUAL_EQUAL,
	"!=", // BANG_EQUAL,
	// Logical
	"and", // AND,
	"or", // OR,
	"not", // NOT,
	"&&", // AMPERSAND_AMPERSAND,
	"||", // PIPE_PIPE,
	"!", // BANG,
	// Bitwise
	"&", // AMPERSAND,
	"|", // PIPE,
	"~", // TILDE,
	"^", // CARET,
	"<<", // LESS_LESS,
	">>", // GREATER_GREATER,
	// Math
	"+", // PLUS,
	"-", // MINUS,
	"*", // STAR,
	"**", // STAR_STAR,
	"/", // SLASH,
	"%", // PERCENT,
	// Assignment
	"=", // EQUAL,
	"+=", // PLUS_EQUAL,
	"-=", // MINUS_EQUAL,
	"*=", // STAR_EQUAL,
	"**=", // STAR_STAR_EQUAL,
	"/=", // SLASH_EQUAL,
	"%=", // PERCENT_EQUAL,
	"<<=", // LESS_LESS_EQUAL,
	">>=", // GREATER_GREATER_EQUAL,
	"&=", // AMPERSAND_EQUAL,
	"|=", // PIPE_EQUAL,
	"^=", // CARET_EQUAL,
	// Control flow
	"if", // IF,
	"elif", // ELIF,
	"else", // ELSE,
	"for", // FOR,
	"while", // WHILE,
	"break", // BREAK,
	"continue", // CONTINUE,
	"pass", // PASS,
	"return", // RETURN,
	"match", // MATCH,
	"when", // WHEN,
	// Keywords
	"abstract", // ABSTRACT,
	"as", // AS,
	"assert", // ASSERT,
	"await", // AWAIT,
	"breakpoint", // BREAKPOINT,
	"class", // CLASS,
	"class_name", // CLASS_NAME,
	"enum_name", // ENUM_NAME,
	"const", // TK_CONST,
	"enum", // ENUM,
	"extends", // EXTENDS,
	"final", // FINAL,
	"func", // FUNC,
	"import", // IMPORT,
	"in", // TK_IN,
	"is", // IS,
	"namespace", // NAMESPACE
	"preload", // PRELOAD,
	"self", // SELF,
	"signal", // SIGNAL,
	"static", // STATIC,
	"super", // SUPER,
	"trait", // TRAIT,
	"trait_name", // TRAIT_NAME,
	"tuple", // TUPLE,
	"tuple_name", // TUPLE_NAME,
	"uses", // USES,
	"var", // VAR,
	"void", // TK_VOID,
	"yield", // YIELD,
	// Punctuation
	"[", // BRACKET_OPEN,
	"]", // BRACKET_CLOSE,
	"{", // BRACE_OPEN,
	"}", // BRACE_CLOSE,
	"(", // PARENTHESIS_OPEN,
	")", // PARENTHESIS_CLOSE,
	",", // COMMA,
	";", // SEMICOLON,
	".", // PERIOD,
	"..", // PERIOD_PERIOD,
	"...", // PERIOD_PERIOD_PERIOD,
	":", // COLON,
	"$", // DOLLAR,
	"->", // FORWARD_ARROW,
	"_", // UNDERSCORE,
	// Whitespace
	"Newline", // NEWLINE,
	"Indent", // INDENT,
	"Dedent", // DEDENT,
	// Constants
	"PI", // CONST_PI,
	"TAU", // CONST_TAU,
	"INF", // CONST_INF,
	"NaN", // CONST_NAN,
	// Error message improvement
	"VCS conflict marker", // VCS_CONFLICT_MARKER,
	"`", // BACKTICK,
	"?", // QUESTION_MARK,
	// Special
	"Error", // ERROR,
	"End of file", // EOF,
};

// Avoid desync.
static_assert(sizeof(token_names) / sizeof(token_names[0]) == BSTokenizer::Token::TK_MAX, "Amount of token names don't match the amount of token types.");

const char *BSTokenizer::Token::get_name() const {
	ERR_FAIL_INDEX_V_MSG(type, TK_MAX, "<error>", "Using token type out of the enum.");
	return token_names[type];
}

String BSTokenizer::Token::get_debug_name() const {
	switch (type) {
		case IDENTIFIER:
			return vformat(R"(identifier "%s")", source);
		default:
			return vformat(R"("%s")", get_name());
	}
}

bool BSTokenizer::Token::can_precede_bin_op() const {
	switch (type) {
		case IDENTIFIER:
		// A removed type spelling used as an ordinary name is a value like any other identifier.
		case RESERVED_TYPE_NAME:
		case LITERAL:
		case SELF:
		case BRACKET_CLOSE:
		case BRACE_CLOSE:
		case PARENTHESIS_CLOSE:
		case CONST_PI:
		case CONST_TAU:
		case CONST_INF:
		case CONST_NAN:
		// `uses` is a keyword token that is still valid as an identifier (see `is_identifier()`),
		// so a value spelled `uses` can precede a binary operator too, exactly like any other
		// identifier. `uses` never leads a clause followed by a general expression (it is always
		// followed by a bare trait name), so this cannot misinterpret a signed-number clause.
		//
		// `match` and `when` are deliberately NOT included here even though they are also valid
		// identifiers: both are far more commonly used as clause-leading keywords immediately
		// followed by an arbitrary expression that may itself start with a unary `+`/`-`
		// (`match -2 ** 2:`, `pattern when -x > 0:`). Treating them as value tokens would flip
		// that leading sign from part of the number to a binary operator, changing which branch
		// is selected. The rare case of `match`/`when` used as a bare identifier immediately
		// followed by `+`/`-`/`.<digit>` keeps the pre-existing (unfixed) lexing.
		case USES:
		// `tuple` is accepted as an attribute name (`is_node_name()`), so `self.tuple` ends in a
		// raw `TUPLE` token even though the parser treats it as an identifier attribute; without
		// this, `self.tuple+1` and `self.tuple.0` would misdisambiguate the following `+`/`.`.
		// Unlike `match`/`when`, `tuple` never leads a clause followed by a general expression in
		// the current grammar (a `tuple Name(...)` declaration is always followed by an
		// identifier), so there is no equivalent conflict.
		case TUPLE:
			return true;
		default:
			return false;
	}
}

bool BSTokenizer::Token::is_identifier() const {
	// Note: Most keywords should not be recognized as identifiers.
	// These are only exceptions for stuff that already is on the engine's API.
	switch (type) {
		case IDENTIFIER:
		// (D1) `uint`, `ulong` and `long` are reserved type names, not keywords: outside a type
		// they stay ordinary identifiers, exactly as `int` is (docs/GRAMMAR.md section 2.5).
		case RESERVED_TYPE_NAME:
		case MATCH: // Used in String.match().
		case WHEN: // New keyword, avoid breaking existing code.
		case USES: // Contextual trait declaration keyword.
		// Allow constants to be treated as regular identifiers.
		case CONST_PI:
		case CONST_INF:
		case CONST_NAN:
		case CONST_TAU:
			return true;
		default:
			return false;
	}
}

bool BSTokenizer::Token::is_node_name() const {
	// This is meant to allow keywords with the $ notation, but not as general identifiers.
	switch (type) {
		case IDENTIFIER:
		case RESERVED_TYPE_NAME:
		case ABSTRACT:
		case AND:
		case AS:
		case ASSERT:
		case AWAIT:
		case BREAK:
		case BREAKPOINT:
		case CLASS_NAME:
		case CLASS:
		case ENUM_NAME:
		case TK_CONST:
		case CONST_PI:
		case CONST_INF:
		case CONST_NAN:
		case CONST_TAU:
		case CONTINUE:
		case ELIF:
		case ELSE:
		case ENUM:
		case EXTENDS:
		case FINAL:
		case FOR:
		case FUNC:
		case IF:
		case IMPORT:
		case TK_IN:
		case IS:
		case MATCH:
		case NAMESPACE:
		case NOT:
		case OR:
		case PASS:
		case PRELOAD:
		case RETURN:
		case SELF:
		case SIGNAL:
		case STATIC:
		case SUPER:
		case TRAIT:
		case TRAIT_NAME:
		case TUPLE:
		case TUPLE_NAME:
		case USES:
		case UNDERSCORE:
		case VAR:
		case TK_VOID:
		case WHILE:
		case WHEN:
		case YIELD:
			return true;
		default:
			return false;
	}
}

String BSTokenizer::get_token_name(Token::Type p_token_type) {
	ERR_FAIL_INDEX_V_MSG(p_token_type, Token::TK_MAX, "<error>", "Using token type out of the enum.");
	return token_names[p_token_type];
}

bool BSTokenizer::decode_source(const PackedByteArray &p_utf8, String *r_source, String *r_error) {
	ERR_FAIL_NULL_V(r_source, false);
	ERR_FAIL_NULL_V(r_error, false);

	const int64_t size = p_utf8.size();
	const uint8_t *bytes = p_utf8.ptr();
	int64_t index = 0;
	while (index < size) {
		const uint8_t lead = bytes[index];
		int64_t continuation_count = 0;
		uint32_t code_point = 0;
		if (lead < 0x80) {
			index++;
			continue;
		} else if ((lead & 0xE0) == 0xC0) {
			continuation_count = 1;
			code_point = lead & 0x1F;
		} else if ((lead & 0xF0) == 0xE0) {
			continuation_count = 2;
			code_point = lead & 0x0F;
		} else if ((lead & 0xF8) == 0xF0) {
			continuation_count = 3;
			code_point = lead & 0x07;
		} else {
			// A continuation byte with no lead, or a five/six-byte form no encoder may produce.
			*r_error = vformat(R"(Invalid UTF-8 in source at byte offset %d.)", index);
			return false;
		}

		if (index + continuation_count >= size) {
			*r_error = vformat(R"(Invalid UTF-8 in source at byte offset %d.)", index);
			return false;
		}
		for (int64_t offset = 1; offset <= continuation_count; offset++) {
			const uint8_t continuation = bytes[index + offset];
			if ((continuation & 0xC0) != 0x80) {
				*r_error = vformat(R"(Invalid UTF-8 in source at byte offset %d.)", index + offset);
				return false;
			}
			code_point = (code_point << 6) | (continuation & 0x3F);
		}

		// Overlong forms, surrogate halves and code points past U+10FFFF all decode to something
		// Godot would replace with U+FFFD, which is the silent substitution the contract forbids.
		const bool overlong = (continuation_count == 1 && code_point < 0x80) ||
				(continuation_count == 2 && code_point < 0x800) ||
				(continuation_count == 3 && code_point < 0x10000);
		const bool surrogate = code_point >= 0xD800 && code_point <= 0xDFFF;
		if (overlong || surrogate || code_point > 0x10FFFF) {
			*r_error = vformat(R"(Invalid UTF-8 in source at byte offset %d.)", index);
			return false;
		}
		index += continuation_count + 1;
	}

	*r_source = String::utf8(reinterpret_cast<const char *>(bytes), size);
	*r_error = String();
	return true;
}

void BSTokenizerText::set_source_code(const String &p_source_code) {
	source = p_source_code;
	source_utf32 = source.utf32();
	_source = source_utf32.get_data();
	_start = 0;
	line = 1;
	column = 1;
	length = source_utf32.length();
	position = 0;
}

void BSTokenizerText::set_cursor_position(int p_line, int p_column) {
	cursor_line = p_line;
	cursor_column = p_column;
}

void BSTokenizerText::set_multiline_mode(bool p_state) {
	multiline_mode = p_state;
}

void BSTokenizerText::push_expression_indented_block() {
	indent_stack_stack.push_back(indent_stack);
}

void BSTokenizerText::pop_expression_indented_block() {
	ERR_FAIL_COND(indent_stack_stack.is_empty());
	indent_stack = indent_stack_stack.back()->get();
	indent_stack_stack.pop_back();
}

int BSTokenizerText::get_cursor_line() const {
	return cursor_line;
}

int BSTokenizerText::get_cursor_column() const {
	return cursor_column;
}

bool BSTokenizerText::is_past_cursor() const {
	if (line < cursor_line) {
		return false;
	}
	if (line > cursor_line) {
		return true;
	}
	if (column < cursor_column) {
		return false;
	}
	return true;
}

char32_t BSTokenizerText::_advance() {
	if (unlikely(_is_at_end())) {
		return '\0';
	}
	column++;
	position++;
	if (unlikely(_is_at_end())) {
		// Add extra newline even if it's not there, to satisfy the parser.
		newline(true);
		// Also add needed unindent.
		check_indent();
	}
	return _peek(-1);
}

void BSTokenizerText::push_paren(char32_t p_char) {
	paren_stack.push_back(p_char);
}

bool BSTokenizerText::pop_paren(char32_t p_expected) {
	if (paren_stack.is_empty()) {
		return false;
	}
	char32_t actual = paren_stack.back()->get();
	paren_stack.pop_back();

	return actual == p_expected;
}

BSTokenizer::Token BSTokenizerText::pop_error() {
	Token error = error_stack.back()->get();
	error_stack.pop_back();
	return error;
}

BSTokenizer::Token BSTokenizerText::make_token(Token::Type p_type) {
	Token token(p_type);
	token.start_line = start_line;
	token.end_line = line;
	token.start_column = start_column;
	token.end_column = column;
	token.source = source.substr(_start, position - _start);

	if (p_type != Token::ERROR && cursor_line > -1) {
		// Also count whitespace after token.
		int offset = 0;
		while (_peek(offset) == ' ' || _peek(offset) == '\t') {
			offset++;
		}
		int last_column = column + offset;
		// Check cursor position in token.
		if (start_line == line) {
			// Single line token.
			if (cursor_line == start_line && cursor_column >= start_column && cursor_column <= last_column) {
				token.cursor_position = cursor_column - start_column;
				if (cursor_column == start_column) {
					token.cursor_place = CURSOR_BEGINNING;
				} else if (cursor_column < column) {
					token.cursor_place = CURSOR_MIDDLE;
				} else {
					token.cursor_place = CURSOR_END;
				}
			}
		} else {
			// Multi line token.
			if (cursor_line == start_line && cursor_column >= start_column) {
				// Is in first line.
				token.cursor_position = cursor_column - start_column;
				if (cursor_column == start_column) {
					token.cursor_place = CURSOR_BEGINNING;
				} else {
					token.cursor_place = CURSOR_MIDDLE;
				}
			} else if (cursor_line == line && cursor_column <= last_column) {
				// Is in last line.
				token.cursor_position = cursor_column - start_column;
				if (cursor_column < column) {
					token.cursor_place = CURSOR_MIDDLE;
				} else {
					token.cursor_place = CURSOR_END;
				}
			} else if (cursor_line > start_line && cursor_line < line) {
				// Is in middle line.
				token.cursor_position = CURSOR_MIDDLE;
			}
		}
	}

	// `p_type != Token::IDENTIFIER` excludes plain identifiers, which `Token::can_precede_bin_op()`
	// already recognizes directly; this only needs to cover the keyword tokens that
	// `Token::is_node_name()` accepts as attribute names but `can_precede_bin_op()` does not,
	// because the parser re-spells them to `IDENTIFIER` only when consuming the attribute, not at
	// tokenize time.
	last_token_is_keyword_attribute = p_type != Token::IDENTIFIER && token.is_node_name() && last_token.type == Token::PERIOD;
	last_token = token;
	return token;
}

BSTokenizer::Token BSTokenizerText::make_literal(const Variant &p_literal) {
	Token token = make_token(Token::LITERAL);
	token.literal = p_literal;
	return token;
}

BSTokenizer::Token BSTokenizerText::make_identifier(const StringName &p_identifier) {
	Token identifier = make_token(Token::IDENTIFIER);
	identifier.literal = p_identifier;
	return identifier;
}

bool BSTokenizerText::_is_type_position() const {
	switch (last_token.type) {
		case Token::FORWARD_ARROW:
		case Token::AS:
		case Token::IS:
			return true;
		case Token::COLON:
			// A Python-style dictionary entry is the one construct that spells a value after ":",
			// and it is always inside "{". Everywhere else a ":" followed by a name introduces a
			// type.
			return paren_stack.is_empty() || paren_stack.back()->get() != '{';
		default:
			return false;
	}
}

BSTokenizer::Token BSTokenizerText::make_error(const String &p_message) {
	Token error = make_token(Token::ERROR);
	error.literal = p_message;

	return error;
}

void BSTokenizerText::push_error(const String &p_message) {
	Token error = make_error(p_message);
	error_stack.push_back(error);
}

void BSTokenizerText::push_error(const Token &p_error) {
	error_stack.push_back(p_error);
}

BSTokenizer::Token BSTokenizerText::make_paren_error(char32_t p_paren) {
	if (paren_stack.is_empty()) {
		return make_error(vformat("Closing \"%c\" doesn't have an opening counterpart.", p_paren));
	}
	Token error = make_error(vformat("Closing \"%c\" doesn't match the opening \"%c\".", p_paren, paren_stack.back()->get()));
	paren_stack.pop_back(); // Remove opening one anyway.
	return error;
}

BSTokenizer::Token BSTokenizerText::check_vcs_marker(char32_t p_test, Token::Type p_double_type) {
	int offset = 1; // The character after the two already matched.
	int chars = 2; // Two already matched.

	// Test before consuming characters, since we don't want to consume more than needed.
	while (_peek(offset) == p_test) {
		chars++;
		offset++;
	}
	if (chars >= 7) {
		// It is a VCS conflict marker.
		while (chars > 1) {
			// Consume all characters (first was already consumed by scan()).
			_advance();
			chars--;
		}
		return make_token(Token::VCS_CONFLICT_MARKER);
	} else {
		// It is only a regular double character token, so we consume the second character.
		_advance();
		return make_token(p_double_type);
	}
}

BSTokenizer::Token BSTokenizerText::annotation() {
	if (is_unicode_identifier_start(_peek())) {
		_advance(); // Consume start character.
	} else {
		push_error("Expected annotation identifier after \"@\".");
	}
	while (is_unicode_identifier_continue(_peek())) {
		// Consume all identifier characters.
		_advance();
	}
	// Fully qualified annotation usage such as `@cafecito.test.timeout` keeps the whole dotted
	// path in a single token. Only a "." immediately followed by an identifier extends the name,
	// so a stray "." (or a "." that begins something else) is left to the regular tokenizer.
	while (_peek() == '.' && is_unicode_identifier_start(_peek(1))) {
		_advance(); // Consume ".".
		_advance(); // Consume the next segment's start character.
		while (is_unicode_identifier_continue(_peek())) {
			_advance();
		}
	}
	// During completion, a trailing "." sitting immediately before the cursor (e.g.
	// `@namespace.|`) is kept as part of the annotation token so qualified-name completion can
	// offer the declarations under that namespace. Outside completion (`cursor_line == -1`) the
	// stray "." is still left to the regular tokenizer.
	if (cursor_line == line && _peek() == '.' && cursor_column == column + 1) {
		_advance(); // Consume the trailing ".".
	}
	Token annotation = make_token(Token::ANNOTATION);
	annotation.literal = StringName(annotation.source);
	return annotation;
}

/**
 * The one table of words the tokenizer takes out of identifier space.
 *
 * `KEYWORD` names a spelling that has its own token type. `KEYWORD_REMOVED` names a spelling D1
 * removed from the language (docs/GRAMMAR.md sections 2.5 and 7.1): it has no token type, because
 * nothing downstream may consume it, and matching one is a diagnostic naming the removal. Keeping
 * the removed spellings in this table rather than in a second list is what makes it impossible for
 * a reserved word to quietly become an ordinary identifier again -- and it is the table
 * `BaristaScriptLanguage::_get_reserved_words()` is to be wired to, rather than a copy of it.
 */
#define KEYWORDS(KEYWORD_GROUP, KEYWORD, KEYWORD_REMOVED) \
	KEYWORD_GROUP('a')                                    \
	KEYWORD("abstract", Token::ABSTRACT)                  \
	KEYWORD("as", Token::AS)                              \
	KEYWORD("and", Token::AND)                            \
	KEYWORD("assert", Token::ASSERT)                      \
	KEYWORD("await", Token::AWAIT)                        \
	KEYWORD_GROUP('b')                                    \
	KEYWORD("break", Token::BREAK)                        \
	KEYWORD("breakpoint", Token::BREAKPOINT)              \
	KEYWORD_GROUP('c')                                    \
	KEYWORD("class", Token::CLASS)                        \
	KEYWORD("class_name", Token::CLASS_NAME)              \
	KEYWORD("const", Token::TK_CONST)                     \
	KEYWORD("continue", Token::CONTINUE)                  \
	KEYWORD_GROUP('e')                                    \
	KEYWORD("elif", Token::ELIF)                          \
	KEYWORD("else", Token::ELSE)                          \
	KEYWORD("enum", Token::ENUM)                          \
	KEYWORD("enum_name", Token::ENUM_NAME)                \
	KEYWORD("extends", Token::EXTENDS)                    \
	KEYWORD_GROUP('f')                                    \
	KEYWORD("final", Token::FINAL)                        \
	KEYWORD("for", Token::FOR)                            \
	KEYWORD("func", Token::FUNC)                          \
	KEYWORD_GROUP('i')                                    \
	KEYWORD("if", Token::IF)                              \
	KEYWORD("import", Token::IMPORT)                      \
	KEYWORD("in", Token::TK_IN)                           \
	KEYWORD("is", Token::IS)                              \
	KEYWORD_GROUP('l')                                    \
	KEYWORD_REMOVED("long")                               \
	KEYWORD_GROUP('m')                                    \
	KEYWORD("match", Token::MATCH)                        \
	KEYWORD_GROUP('n')                                    \
	KEYWORD("namespace", Token::NAMESPACE)                \
	KEYWORD("not", Token::NOT)                            \
	KEYWORD_GROUP('o')                                    \
	KEYWORD("or", Token::OR)                              \
	KEYWORD_GROUP('p')                                    \
	KEYWORD("pass", Token::PASS)                          \
	KEYWORD("preload", Token::PRELOAD)                    \
	KEYWORD_GROUP('r')                                    \
	KEYWORD("return", Token::RETURN)                      \
	KEYWORD_GROUP('s')                                    \
	KEYWORD("self", Token::SELF)                          \
	KEYWORD("signal", Token::SIGNAL)                      \
	KEYWORD("static", Token::STATIC)                      \
	KEYWORD("super", Token::SUPER)                        \
	KEYWORD_GROUP('t')                                    \
	KEYWORD("trait", Token::TRAIT)                        \
	KEYWORD("trait_name", Token::TRAIT_NAME)              \
	KEYWORD("tuple", Token::TUPLE)                        \
	KEYWORD("tuple_name", Token::TUPLE_NAME)              \
	KEYWORD_GROUP('u')                                    \
	KEYWORD_REMOVED("uint")                               \
	KEYWORD_REMOVED("ulong")                              \
	KEYWORD("uses", Token::USES)                          \
	KEYWORD_GROUP('v')                                    \
	KEYWORD("var", Token::VAR)                            \
	KEYWORD("void", Token::TK_VOID)                       \
	KEYWORD_GROUP('w')                                    \
	KEYWORD("while", Token::WHILE)                        \
	KEYWORD("when", Token::WHEN)                          \
	KEYWORD_GROUP('y')                                    \
	KEYWORD("yield", Token::YIELD)                        \
	KEYWORD_GROUP('I')                                    \
	KEYWORD("INF", Token::CONST_INF)                      \
	KEYWORD_GROUP('N')                                    \
	KEYWORD("NAN", Token::CONST_NAN)                      \
	KEYWORD_GROUP('P')                                    \
	KEYWORD("PI", Token::CONST_PI)                        \
	KEYWORD_GROUP('T')                                    \
	KEYWORD("TAU", Token::CONST_TAU)

#define MIN_KEYWORD_LENGTH 2
#define MAX_KEYWORD_LENGTH 10

String BSTokenizer::removed_type_name_diagnostic(const String &p_spelling) {
	// The wording docs/GRAMMAR.md section 2.5 specifies, in one place, so the tokenizer and the
	// type positions the parser owns cannot word the same rejection differently.
	return vformat(R"("%s" is reserved. BaristaScript stores every integer on one signed 64-bit carrier; write "int".)", p_spelling);
}

Vector<String> BSTokenizer::get_keyword_spellings() {
#define KEYWORD_GROUP_IGNORE(group)
#define KEYWORD_APPEND(keyword, token_type) spellings.push_back(keyword);
#define KEYWORD_REMOVED_IGNORE(keyword)
	Vector<String> spellings;
	KEYWORDS(KEYWORD_GROUP_IGNORE, KEYWORD_APPEND, KEYWORD_REMOVED_IGNORE)
	return spellings;
#undef KEYWORD_REMOVED_IGNORE
#undef KEYWORD_APPEND
#undef KEYWORD_GROUP_IGNORE
}

Vector<String> BSTokenizer::get_reserved_spellings() {
#define KEYWORD_GROUP_IGNORE(group)
#define KEYWORD_IGNORE(keyword, token_type)
#define KEYWORD_REMOVED_APPEND(keyword) spellings.push_back(keyword);
	Vector<String> spellings;
	KEYWORDS(KEYWORD_GROUP_IGNORE, KEYWORD_IGNORE, KEYWORD_REMOVED_APPEND)
	return spellings;
#undef KEYWORD_REMOVED_APPEND
#undef KEYWORD_IGNORE
#undef KEYWORD_GROUP_IGNORE
}

BSTokenizer::Token BSTokenizerText::potential_identifier() {
	bool only_ascii = _peek(-1) < 128;

	// Consume all identifier characters.
	while (is_unicode_identifier_continue(_peek())) {
		char32_t c = _advance();
		only_ascii = only_ascii && c < 128;
	}

	int len = position - _start;

	if (len == 1 && _peek(-1) == '_') {
		// Lone underscore.
		Token token = make_token(Token::UNDERSCORE);
		token.literal = "_";
		return token;
	}

	String name = source.substr(_start, len);
	if (len < MIN_KEYWORD_LENGTH || len > MAX_KEYWORD_LENGTH) {
		// Cannot be a keyword, as the length doesn't match any.
		return make_identifier(name);
	}

	if (!only_ascii) {
		// Upstream runs the `TextServer::FEATURE_UNICODE_SECURITY` confusable-identifier check here
		// under `DEBUG_ENABLED` (fs_tokenizer.cpp:668-677), rejecting a non-ASCII identifier that
		// is visually similar to a keyword. The platform seam guards out
		// `servers/text/text_server.h`, so the check is absent; reinstating it belongs with the
		// warning registry. Everything else about this branch is unchanged.

		// Cannot be a keyword, as keywords are ASCII only.
		return make_identifier(name);
	}

	// Define some helper macros for the switch case.
#define KEYWORD_GROUP_CASE(char) \
	break;                       \
	case char:
#define KEYWORD(keyword, token_type)                                                                                      \
	{                                                                                                                     \
		const int keyword_length = sizeof(keyword) - 1;                                                                   \
		static_assert(keyword_length <= MAX_KEYWORD_LENGTH, "There's a keyword longer than the defined maximum length");  \
		static_assert(keyword_length >= MIN_KEYWORD_LENGTH, "There's a keyword shorter than the defined minimum length"); \
		if (keyword_length == len && name == keyword) {                                                                   \
			Token kw = make_token(token_type);                                                                            \
			kw.literal = name;                                                                                            \
			return kw;                                                                                                    \
		}                                                                                                                 \
	}
#define KEYWORD_REMOVED(keyword)                                                                                          \
	{                                                                                                                     \
		const int keyword_length = sizeof(keyword) - 1;                                                                   \
		static_assert(keyword_length <= MAX_KEYWORD_LENGTH, "There's a keyword longer than the defined maximum length");  \
		static_assert(keyword_length >= MIN_KEYWORD_LENGTH, "There's a keyword shorter than the defined minimum length"); \
		if (keyword_length == len && name == keyword) {                                                                   \
			if (_is_type_position()) {                                                                                    \
				return make_error(removed_type_name_diagnostic(name));                                                    \
			}                                                                                                             \
			Token reserved = make_token(Token::RESERVED_TYPE_NAME);                                                       \
			reserved.literal = StringName(name);                                                                          \
			return reserved;                                                                                              \
		}                                                                                                                 \
	}

	// (D1) `as!` is Foundry's unchecked bit-reinterpret operator (fs_tokenizer.cpp:700-708). It
	// reinterprets between integer widths, of which BaristaScript has one, so the compound spelling
	// is reserved and rejected rather than lexed. The contiguity requirement is upstream's: `as !x`,
	// with whitespace, stays an ordinary cast of a negated expression and never reaches this.
	if (len == 2 && name == "as" && _peek() == '!') {
		_advance();
		return make_error(R"("as!" is reserved. It reinterprets between integer widths, which BaristaScript does not distinguish.)");
	}

	// Find if it's a keyword.
	switch (_source[_start]) {
		default:
			KEYWORDS(KEYWORD_GROUP_CASE, KEYWORD, KEYWORD_REMOVED)
			break;
	}

	// Check if it's a special literal
	if (len == 4) {
		if (name == "true") {
			return make_literal(true);
		} else if (name == "null") {
			return make_literal(Variant());
		}
	} else if (len == 5) {
		if (name == "false") {
			return make_literal(false);
		}
	}

	// Not a keyword, so must be an identifier.
	return make_identifier(name);

#undef KEYWORD_REMOVED
#undef KEYWORD_GROUP_CASE
#undef KEYWORD
}

#undef MAX_KEYWORD_LENGTH
#undef MIN_KEYWORD_LENGTH
#undef KEYWORDS

void BSTokenizerText::newline(bool p_make_token) {
	// Don't overwrite previous newline, nor create if we want a line continuation.
	if (p_make_token && !pending_newline && !line_continuation) {
		Token newline(Token::NEWLINE);
		newline.start_line = line;
		newline.end_line = line;
		newline.start_column = column - 1;
		newline.end_column = column;
		pending_newline = true;
		last_newline = newline;
		if (paren_stack.is_empty()) {
			// This newline is not nested inside an unclosed `(`/`[`/`{`, so it really does end a
			// statement/line and must reset `last_token` for the `+`/`-`/tuple-index
			// disambiguation that inspects the last real token.
			//
			// Deliberately keyed on `paren_stack`, not `multiline_mode`: the parser only sets
			// `multiline_mode` while inside brackets, but `BSTokenizerBuffer::parse_code_string`
			// forces `multiline_mode` on for an entire file to omit `NEWLINE` tokens from the
			// compiled buffer, even across ordinary statement boundaries. Gating on
			// `multiline_mode` there would leave `last_token` stuck on the previous statement's
			// last token forever. `paren_stack` reflects the tokenizer's own bracket nesting
			// regardless of why layout tokens are being suppressed, so it stays correct in both
			// the parser's per-bracket toggling and the buffer exporter's whole-file toggling.
			last_token = newline;
			last_token_is_keyword_attribute = false;
		}
	}

	// Increment line/column counters.
	line++;
	column = 1;
}

// (D1) Whether a run of letters after an integer body is one of the four suffix spellings Foundry
// accepted (`U`, `L`, `UL`, `LU`, in any case). BaristaScript has one integer type, so every one of
// them is reserved and rejected rather than parsed; anything else stays an ordinary "letter after a
// number" error, so `1abc` keeps reporting invalid notation.
static bool _is_reserved_integer_suffix(const String &p_suffix) {
	const String upper = p_suffix.to_upper();
	return upper == "U" || upper == "L" || upper == "UL" || upper == "LU";
}

static uint64_t _numeric_digit_value(char32_t p_character) {
	if (is_digit(p_character)) {
		return uint64_t(p_character - '0');
	}
	if (p_character >= 'a' && p_character <= 'f') {
		return uint64_t(p_character - 'a') + 10;
	}
	return uint64_t(p_character - 'A') + 10;
}

BSTokenizer::Token BSTokenizerText::number() {
	int base = 10;
	bool has_decimal = false;
	bool has_exponent = false;
	bool has_error = false;
	bool need_digits = false;
	bool (*digit_check_func)(char32_t) = is_digit;

	// A digit immediately following a `PERIOD` token is a tuple index (`t.0`), never a float
	// literal: no fractional part, exponent, non-decimal prefix, or type suffix is allowed.
	const bool is_tuple_index = last_token.type == Token::PERIOD;

	// Sign before hexadecimal or binary.
	if ((_peek(-1) == '+' || _peek(-1) == '-') && _peek() == '0') {
		_advance();
	}

	if (_peek(-1) == '.') {
		has_decimal = true;
	} else if (!is_tuple_index && _peek(-1) == '0') {
		if (_peek() == 'x' || _peek() == 'X') {
			// Hexadecimal.
			base = 16;
			digit_check_func = is_hex_digit;
			need_digits = true;
			_advance();
		} else if (_peek() == 'b' || _peek() == 'B') {
			// Binary.
			base = 2;
			digit_check_func = is_binary_digit;
			need_digits = true;
			_advance();
		}
	}

	if (base != 10 && is_underscore(_peek())) { // Disallow `0x_` and `0b_`.
		Token error = make_error(vformat(R"(Unexpected underscore after "0%c".)", _peek(-1)));
		error.start_column = column;
		error.end_column = column + 1;
		push_error(error);
		has_error = true;
	}
	bool previous_was_underscore = false; // Allow `_` to be used in a number, for readability.
	while (digit_check_func(_peek()) || is_underscore(_peek())) {
		if (is_underscore(_peek())) {
			if (previous_was_underscore) {
				Token error = make_error(R"(Multiple underscores cannot be adjacent in a numeric literal.)");
				error.start_column = column;
				error.end_column = column + 1;
				push_error(error);
			}
			previous_was_underscore = true;
		} else {
			need_digits = false;
			previous_was_underscore = false;
		}
		_advance();
	}

	// It might be a ".." token (instead of decimal point) so we check if it's not.
	// A tuple index never has a fractional part: `x.0.1` is nested member access, not `x` followed
	// by the float `0.1`.
	if (!is_tuple_index && _peek() == '.' && _peek(1) != '.') {
		if (base == 10 && !has_decimal) {
			has_decimal = true;
		} else if (base == 10) {
			Token error = make_error("Cannot use a decimal point twice in a number.");
			error.start_column = column;
			error.end_column = column + 1;
			push_error(error);
			has_error = true;
		} else if (base == 16) {
			Token error = make_error("Cannot use a decimal point in a hexadecimal number.");
			error.start_column = column;
			error.end_column = column + 1;
			push_error(error);
			has_error = true;
		} else {
			Token error = make_error("Cannot use a decimal point in a binary number.");
			error.start_column = column;
			error.end_column = column + 1;
			push_error(error);
			has_error = true;
		}
		if (!has_error) {
			_advance();

			// Consume decimal digits.
			if (is_underscore(_peek())) { // Disallow `10._`, but allow `10.`.
				Token error = make_error(R"(Unexpected underscore after decimal point.)");
				error.start_column = column;
				error.end_column = column + 1;
				push_error(error);
				has_error = true;
			}
			previous_was_underscore = false;
			while (is_digit(_peek()) || is_underscore(_peek())) {
				if (is_underscore(_peek())) {
					if (previous_was_underscore) {
						Token error = make_error(R"(Multiple underscores cannot be adjacent in a numeric literal.)");
						error.start_column = column;
						error.end_column = column + 1;
						push_error(error);
					}
					previous_was_underscore = true;
				} else {
					previous_was_underscore = false;
				}
				_advance();
			}
		}
	}
	if (base == 10 && !is_tuple_index) {
		if (_peek() == 'e' || _peek() == 'E') {
			has_exponent = true;
			_advance();
			if (_peek() == '+' || _peek() == '-') {
				// Exponent sign.
				_advance();
			}
			// Consume exponent digits.
			if (!is_digit(_peek())) {
				Token error = make_error(R"(Expected exponent value after "e".)");
				error.start_column = column;
				error.end_column = column + 1;
				push_error(error);
			}
			previous_was_underscore = false;
			while (is_digit(_peek()) || is_underscore(_peek())) {
				if (is_underscore(_peek())) {
					if (previous_was_underscore) {
						Token error = make_error(R"(Multiple underscores cannot be adjacent in a numeric literal.)");
						error.start_column = column;
						error.end_column = column + 1;
						push_error(error);
					}
					previous_was_underscore = true;
				} else {
					previous_was_underscore = false;
				}
				_advance();
			}
		}
	}

	if (need_digits) {
		// No digits in hex or bin literal.
		Token error = make_error(vformat(R"(Expected %s digit after "0%c".)", (base == 16 ? "hexadecimal" : "binary"), (base == 16 ? 'x' : 'b')));
		error.start_column = column;
		error.end_column = column + 1;
		return error;
	}

	// The numeric body ends here, so remember where it stops before looking at any trailing
	// letters: the literal's value and the replacement a diagnostic proposes both come from the
	// body text alone. A suffix is only meaningful on an integer, so a tuple index, a fractional
	// part, or an exponent leaves the letters to the "letter after a number" error below.
	const int body_end = position;
	if (!is_tuple_index && !has_decimal && !has_exponent && is_unicode_identifier_start(_peek())) {
		String suffix_text;
		int suffix_length = 0;
		while (is_unicode_identifier_continue(_peek(suffix_length))) {
			suffix_text += String::chr(_peek(suffix_length));
			suffix_length++;
		}
		if (_is_reserved_integer_suffix(suffix_text)) {
			// (D1) Consume the suffix so the diagnostic covers the whole literal as written, and
			// so the reserved spelling cannot come back as an identifier token.
			for (int i = 0; i < suffix_length; i++) {
				_advance();
			}
			const String body = source.substr(_start, body_end - _start);
			// The suffix is named as it was written, not canonicalized: a diagnostic that said "UL"
			// about a source that says "lu" would send the reader looking for text that is not
			// there. The replacement it proposes is the body alone, because there is no suffix that
			// would have been accepted.
			return make_error(vformat(R"(The "%s" integer literal suffix is reserved. BaristaScript has one integer type, "int"; write "%s".)", suffix_text, body));
		}
	}

	// Detect extra decimal point.
	if (is_tuple_index && (is_unicode_identifier_start(_peek()) || is_unicode_identifier_continue(_peek()))) {
		// A tuple index is a bare decimal integer; no exponent, prefix, or suffix is allowed.
		push_error(R"(Expected a tuple index after ".": only a decimal integer is allowed.)");
		has_error = true;
	} else if (!has_error && has_decimal && _peek() == '.' && _peek(1) != '.') {
		Token error = make_error("Cannot use a decimal point twice in a number.");
		error.start_column = column;
		error.end_column = column + 1;
		push_error(error);
		has_error = true;
	} else if (is_unicode_identifier_start(_peek()) || is_unicode_identifier_continue(_peek())) {
		// Letter at the end of the number.
		push_error("Invalid numeric notation.");
	}

	// Create a string with the numeric body.
	const String body_text = source.substr(_start, body_end - _start);
	String number = body_text.remove_char('_');

	if (has_decimal || has_exponent) {
		double value = number.to_float();
		return make_literal(value);
	}

	// Accumulate the magnitude in unsigned arithmetic rather than routing it through `int64_t`, so
	// that `-9223372036854775808` -- whose magnitude is one past the positive maximum -- is
	// representable while it is being judged, and an out-of-range literal is diagnosed instead of
	// silently wrapping or saturating into a value the source never wrote.
	bool is_negative = false;
	int64_t digit_index = 0;
	if (number[0] == '-') {
		is_negative = true;
		digit_index = 1;
	} else if (number[0] == '+') {
		digit_index = 1;
	}
	if (base != 10) {
		digit_index += 2; // Skip the `0x`/`0b` prefix.
	}

	uint64_t magnitude = 0;
	bool magnitude_overflowed = false;
	for (; digit_index < number.length(); digit_index++) {
		const uint64_t digit = _numeric_digit_value(number[digit_index]);
		if (magnitude > (UINT64_MAX - digit) / uint64_t(base)) {
			magnitude_overflowed = true;
			break;
		}
		magnitude = magnitude * uint64_t(base) + digit;
	}

	// (D1) There is one integer type and no wider carrier to promote to, so a magnitude outside the
	// signed 64-bit range is an error. It never wraps and it never reinterprets.
	const uint64_t magnitude_limit = is_negative ? INTEGER_MINIMUM_MAGNITUDE : INTEGER_MAXIMUM_MAGNITUDE;
	if (magnitude_overflowed || magnitude > magnitude_limit) {
		return make_error(R"(Integer literal is out of range for "int", the only integer type; BaristaScript stores every integer on one signed 64-bit carrier.)");
	}

	// Negating through the unsigned magnitude keeps the minimum representable, which negating the
	// signed value would not.
	const int64_t signed_value = is_negative ? int64_t(uint64_t(0) - magnitude) : int64_t(magnitude);
	return make_literal(signed_value);
}

BSTokenizer::Token BSTokenizerText::string() {
	enum StringType {
		STRING_REGULAR,
		STRING_NAME,
		STRING_NODEPATH,
	};

	bool is_raw = false;
	bool is_multiline = false;
	StringType type = STRING_REGULAR;

	if (_peek(-1) == 'r') {
		is_raw = true;
		_advance();
	} else if (_peek(-1) == '&') {
		type = STRING_NAME;
		_advance();
	} else if (_peek(-1) == '^') {
		type = STRING_NODEPATH;
		_advance();
	}

	char32_t quote_char = _peek(-1);

	if (_peek() == quote_char && _peek(1) == quote_char) {
		is_multiline = true;
		// Consume all quotes.
		_advance();
		_advance();
	}

	String result;
	char32_t prev = 0;
	int prev_pos = 0;

	for (;;) {
		// Consume actual string.
		if (_is_at_end()) {
			return make_error("Unterminated string.");
		}

		char32_t ch = _peek();

		if (ch == 0x200E || ch == 0x200F || (ch >= 0x202A && ch <= 0x202E) || (ch >= 0x2066 && ch <= 0x2069)) {
			Token error;
			if (is_raw) {
				error = make_error("Invisible text direction control character present in the string, use regular string literal instead of r-string.");
			} else {
				error = make_error("Invisible text direction control character present in the string, escape it (\"\\u" + String::num_int64(ch, 16) + "\") to avoid confusion.");
			}
			error.start_column = column;
			error.end_column = column + 1;
			push_error(error);
		}

		if (ch == '\\') {
			// Escape pattern.
			_advance();
			if (_is_at_end()) {
				return make_error("Unterminated string.");
			}

			if (is_raw) {
				if (_peek() == quote_char) {
					_advance();
					if (_is_at_end()) {
						return make_error("Unterminated string.");
					}
					result += '\\';
					result += quote_char;
				} else if (_peek() == '\\') { // For `\\\"`.
					_advance();
					if (_is_at_end()) {
						return make_error("Unterminated string.");
					}
					result += '\\';
					result += '\\';
				} else {
					result += '\\';
				}
			} else {
				// Grab escape character.
				char32_t code = _peek();
				_advance();
				if (_is_at_end()) {
					return make_error("Unterminated string.");
				}

				char32_t escaped = 0;
				bool valid_escape = true;

				switch (code) {
					case 'a':
						escaped = '\a';
						break;
					case 'b':
						escaped = '\b';
						break;
					case 'f':
						escaped = '\f';
						break;
					case 'n':
						escaped = '\n';
						break;
					case 'r':
						escaped = '\r';
						break;
					case 't':
						escaped = '\t';
						break;
					case 'v':
						escaped = '\v';
						break;
					case '\'':
						escaped = '\'';
						break;
					case '\"':
						escaped = '\"';
						break;
					case '\\':
						escaped = '\\';
						break;
					case 'U':
					case 'u': {
						// Hexadecimal sequence.
						int hex_len = (code == 'U') ? 6 : 4;
						for (int j = 0; j < hex_len; j++) {
							if (_is_at_end()) {
								return make_error("Unterminated string.");
							}

							char32_t digit = _peek();
							char32_t value = 0;
							if (is_digit(digit)) {
								value = digit - '0';
							} else if (digit >= 'a' && digit <= 'f') {
								value = digit - 'a';
								value += 10;
							} else if (digit >= 'A' && digit <= 'F') {
								value = digit - 'A';
								value += 10;
							} else {
								// Make error, but keep parsing the string.
								Token error = make_error("Invalid hexadecimal digit in unicode escape sequence.");
								error.start_column = column;
								error.end_column = column + 1;
								push_error(error);
								valid_escape = false;
								break;
							}

							escaped <<= 4;
							escaped |= value;

							_advance();
						}
					} break;
					case '\r':
						if (_peek() != '\n') {
							// Carriage return without newline in string. (???)
							// Just add it to the string and keep going.
							result += ch;
							_advance();
							break;
						}
						[[fallthrough]];
					case '\n':
						// Escaping newline.
						newline(false);
						valid_escape = false; // Don't add to the string.
						break;
					default:
						Token error = make_error("Invalid escape in string.");
						error.start_column = column - 2;
						push_error(error);
						valid_escape = false;
						break;
				}
				// Parse UTF-16 pair.
				if (valid_escape) {
					if ((escaped & 0xfffffc00) == 0xd800) {
						if (prev == 0) {
							prev = escaped;
							prev_pos = column - 2;
							continue;
						} else {
							Token error = make_error("Invalid UTF-16 sequence in string, unpaired lead surrogate.");
							error.start_column = column - 2;
							push_error(error);
							valid_escape = false;
							prev = 0;
						}
					} else if ((escaped & 0xfffffc00) == 0xdc00) {
						if (prev == 0) {
							Token error = make_error("Invalid UTF-16 sequence in string, unpaired trail surrogate.");
							error.start_column = column - 2;
							push_error(error);
							valid_escape = false;
						} else {
							escaped = (prev << 10UL) + escaped - ((0xd800 << 10UL) + 0xdc00 - 0x10000);
							prev = 0;
						}
					}
					if (prev != 0) {
						Token error = make_error("Invalid UTF-16 sequence in string, unpaired lead surrogate.");
						error.start_column = prev_pos;
						push_error(error);
						prev = 0;
					}
				}

				if (valid_escape) {
					result += escaped;
				}
			}
		} else if (ch == quote_char) {
			if (prev != 0) {
				Token error = make_error("Invalid UTF-16 sequence in string, unpaired lead surrogate");
				error.start_column = prev_pos;
				push_error(error);
				prev = 0;
			}
			_advance();
			if (is_multiline) {
				if (_peek() == quote_char && _peek(1) == quote_char) {
					// Ended the multiline string. Consume all quotes.
					_advance();
					_advance();
					break;
				} else {
					// Not a multiline string termination, add consumed quote.
					result += quote_char;
				}
			} else {
				// Ended single-line string.
				break;
			}
		} else {
			if (prev != 0) {
				Token error = make_error("Invalid UTF-16 sequence in string, unpaired lead surrogate");
				error.start_column = prev_pos;
				push_error(error);
				prev = 0;
			}
			result += ch;
			_advance();
			if (ch == '\n') {
				newline(false);
			}
		}
	}
	if (prev != 0) {
		Token error = make_error("Invalid UTF-16 sequence in string, unpaired lead surrogate");
		error.start_column = prev_pos;
		push_error(error);
		prev = 0;
	}

	// Make the literal.
	Variant string;
	switch (type) {
		case STRING_NAME:
			string = StringName(result);
			break;
		case STRING_NODEPATH:
			string = NodePath(result);
			break;
		case STRING_REGULAR:
			string = result;
			break;
	}

	return make_literal(string);
}

void BSTokenizerText::check_indent() {
	ERR_FAIL_COND_MSG(column != 1, "Checking tokenizer indentation in the middle of a line.");

	if (_is_at_end()) {
		// Send dedents for every indent level.
		pending_indents -= indent_level();
		indent_stack.clear();
		return;
	}

	for (;;) {
		char32_t current_indent_char = _peek();
		int indent_count = 0;

		if (current_indent_char != ' ' && current_indent_char != '\t' && current_indent_char != '\r' && current_indent_char != '\n' && current_indent_char != '#') {
			// First character of the line is not whitespace, so we clear all indentation levels.
			// Unless we are in a continuation or in multiline mode (inside expression).
			if (line_continuation || multiline_mode) {
				return;
			}
			pending_indents -= indent_level();
			indent_stack.clear();
			return;
		}

		if (_peek() == '\r') {
			_advance();
			if (_peek() != '\n') {
				push_error("Stray carriage return character in source code.");
			}
		}
		if (_peek() == '\n') {
			// Empty line, keep going.
			_advance();
			newline(false);
			continue;
		}

		// Check indent level.
		bool mixed = false;
		while (!_is_at_end()) {
			char32_t space = _peek();
			if (space == '\t') {
				// Consider individual tab columns.
				column += tab_size - 1;
				indent_count += tab_size;
			} else if (space == ' ') {
				indent_count += 1;
			} else {
				break;
			}
			mixed = mixed || space != current_indent_char;
			_advance();
		}

		if (_is_at_end()) {
			// Reached the end with an empty line, so just dedent as much as needed.
			pending_indents -= indent_level();
			indent_stack.clear();
			return;
		}

		if (_peek() == '\r') {
			_advance();
			if (_peek() != '\n') {
				push_error("Stray carriage return character in source code.");
			}
		}
		if (_peek() == '\n') {
			// Empty line, keep going.
			_advance();
			newline(false);
			continue;
		}
		if (_peek() == '#') {
			// Comment. Advance to the next line.
#ifdef TOOLS_ENABLED
			String comment;
			while (_peek() != '\n' && !_is_at_end()) {
				comment += _advance();
			}
			comments[line] = CommentData(comment, true);
#else
			while (_peek() != '\n' && !_is_at_end()) {
				_advance();
			}
#endif // TOOLS_ENABLED
			if (_is_at_end()) {
				// Reached the end with an empty line, so just dedent as much as needed.
				pending_indents -= indent_level();
				indent_stack.clear();
				return;
			}
			_advance(); // Consume '\n'.
			newline(false);
			continue;
		}

		if (mixed && !line_continuation && !multiline_mode) {
			Token error = make_error("Mixed use of tabs and spaces for indentation.");
			error.start_line = line;
			error.start_column = 1;
			push_error(error);
		}

		if (line_continuation || multiline_mode) {
			// We cleared up all the whitespace at the beginning of the line.
			// If this is a line continuation or we're in multiline mode then we don't want any indentation changes.
			return;
		}

		// Check if indentation character is consistent.
		if (indent_char == '\0') {
			// First time indenting, choose character now.
			indent_char = current_indent_char;
		} else if (current_indent_char != indent_char) {
			Token error = make_error(vformat("Used %s character for indentation instead of %s as used before in the file.",
					_get_indent_char_name(current_indent_char), _get_indent_char_name(indent_char)));
			error.start_line = line;
			error.start_column = 1;
			push_error(error);
		}

		// Now we can do actual indentation changes.

		// Check if indent or dedent.
		int previous_indent = 0;
		if (indent_level() > 0) {
			previous_indent = indent_stack.back()->get();
		}
		if (indent_count == previous_indent) {
			// No change in indentation.
			return;
		}
		if (indent_count > previous_indent) {
			// Indentation increased.
			indent_stack.push_back(indent_count);
			pending_indents++;
		} else {
			// Indentation decreased (dedent).
			if (indent_level() == 0) {
				push_error("Tokenizer bug: trying to dedent without previous indent.");
				return;
			}
			while (indent_level() > 0 && indent_stack.back()->get() > indent_count) {
				indent_stack.pop_back();
				pending_indents--;
			}
			if ((indent_level() > 0 && indent_stack.back()->get() != indent_count) || (indent_level() == 0 && indent_count != 0)) {
				// Mismatched indentation alignment.
				Token error = make_error("Unindent doesn't match the previous indentation level.");
				error.start_line = line;
				error.start_column = 1;
				error.end_column = column + 1;
				push_error(error);
				// Still, we'll be lenient and keep going, so keep this level in the stack.
				indent_stack.push_back(indent_count);
			}
		}
		break; // Get out of the loop in any case.
	}
}

String BSTokenizerText::_get_indent_char_name(char32_t ch) {
	ERR_FAIL_COND_V(ch != ' ' && ch != '\t', String::chr(ch).c_escape());

	return ch == ' ' ? "space" : "tab";
}

void BSTokenizerText::_skip_whitespace() {
	if (pending_indents != 0) {
		// Still have some indent/dedent tokens to give.
		return;
	}

	bool is_bol = column == 1; // Beginning of line.

	if (is_bol) {
		check_indent();
		return;
	}

	for (;;) {
		char32_t c = _peek();
		switch (c) {
			case ' ':
				_advance();
				break;
			case '\t':
				_advance();
				// Consider individual tab columns.
				column += tab_size - 1;
				break;
			case '\r':
				_advance(); // Consume either way.
				if (_peek() != '\n') {
					push_error("Stray carriage return character in source code.");
					return;
				}
				break;
			case '\n':
				_advance();
				newline(!is_bol); // Don't create new line token if line is empty.
				check_indent();
				break;
			case '#': {
				// Comment.
#ifdef TOOLS_ENABLED
				String comment;
				while (_peek() != '\n' && !_is_at_end()) {
					comment += _advance();
				}
				comments[line] = CommentData(comment, is_bol);
#else
				while (_peek() != '\n' && !_is_at_end()) {
					_advance();
				}
#endif // TOOLS_ENABLED
				if (_is_at_end()) {
					return;
				}
				_advance(); // Consume '\n'
				newline(!is_bol);
				check_indent();
			} break;
			default:
				return;
		}
	}
}

BSTokenizer::Token BSTokenizerText::scan() {
	if (has_error()) {
		return pop_error();
	}

	_skip_whitespace();

	if (pending_newline) {
		pending_newline = false;
		if (!multiline_mode) {
			// Don't return newline tokens on multiline mode.
			return last_newline;
		}
	}

	// Check for potential errors after skipping whitespace().
	if (has_error()) {
		return pop_error();
	}

	_start = position;
	start_line = line;
	start_column = column;

	if (pending_indents != 0) {
		// Adjust position for indent.
		if (start_column > 1) {
			while (_start > 0 && _source[_start - 1] != '\n' && _source[_start - 1] != '\r') {
				_start--;
			}
			start_column = 1;
		}
		if (pending_indents > 0) {
			// Indents.
			pending_indents--;
			return make_token(Token::INDENT);
		} else {
			// Dedents.
			pending_indents++;
			Token dedent = make_token(Token::DEDENT);
			dedent.end_column += 1;
			return dedent;
		}
	}

	if (_is_at_end()) {
		return make_token(Token::TK_EOF);
	}

	const char32_t c = _advance();

	if (c == '\\') {
		// Line continuation with backslash.
		if (_peek() == '\r') {
			if (_peek(1) != '\n') {
				return make_error("Unexpected carriage return character.");
			}
			_advance();
		}
		if (_peek() != '\n') {
			return make_error("Expected new line after \"\\\".");
		}
		_advance();
		newline(false);
		line_continuation = true;
		_skip_whitespace(); // Skip whitespace/comment lines after `\`. See GH-89403.
		continuation_lines.push_back(line);
		if (continuation_scan_depth >= MAX_CONTINUATION_SCAN_DEPTH) {
			return make_error("Too many line continuations.");
		}
		continuation_scan_depth++;
		Token token = scan();
		continuation_scan_depth--;
		return token;
	}

	line_continuation = false;

	if (is_digit(c)) {
		return number();
	} else if (c == 'r' && (_peek() == '"' || _peek() == '\'')) {
		// Raw string literals.
		return string();
	} else if (is_unicode_identifier_start(c)) {
		return potential_identifier();
	}

	switch (c) {
		// String literals.
		case '"':
		case '\'':
			return string();

		// Annotation.
		case '@':
			return annotation();

		// Single characters.
		case '~':
			return make_token(Token::TILDE);
		case ',':
			return make_token(Token::COMMA);
		case ':':
			return make_token(Token::COLON);
		case ';':
			return make_token(Token::SEMICOLON);
		case '$':
			return make_token(Token::DOLLAR);
		case '?':
			return make_token(Token::QUESTION_MARK);
		case '`':
			return make_token(Token::BACKTICK);

		// Parens.
		case '(':
			push_paren('(');
			return make_token(Token::PARENTHESIS_OPEN);
		case '[':
			push_paren('[');
			return make_token(Token::BRACKET_OPEN);
		case '{':
			push_paren('{');
			return make_token(Token::BRACE_OPEN);
		case ')':
			if (!pop_paren('(')) {
				return make_paren_error(c);
			}
			return make_token(Token::PARENTHESIS_CLOSE);
		case ']':
			if (!pop_paren('[')) {
				return make_paren_error(c);
			}
			return make_token(Token::BRACKET_CLOSE);
		case '}':
			if (!pop_paren('{')) {
				return make_paren_error(c);
			}
			return make_token(Token::BRACE_CLOSE);

		// Double characters.
		case '!':
			if (_peek() == '=') {
				_advance();
				return make_token(Token::BANG_EQUAL);
			} else {
				return make_token(Token::BANG);
			}
		case '.':
			if (_peek() == '.') {
				_advance();
				if (_peek() == '.') {
					_advance();
					return make_token(Token::PERIOD_PERIOD_PERIOD);
				}
				return make_token(Token::PERIOD_PERIOD);
			} else if (is_digit(_peek()) && !_last_token_precedes_bin_op()) {
				// Number starting with '.'.
				return number();
			} else {
				// After a value token (identifier, literal, `)`/`]`, etc.) a following digit is a
				// tuple index (`t.0`), not the start of a float literal.
				return make_token(Token::PERIOD);
			}
		case '+':
			if (_peek() == '=') {
				_advance();
				return make_token(Token::PLUS_EQUAL);
			} else if (is_digit(_peek()) && !_last_token_precedes_bin_op()) {
				// Number starting with '+'.
				return number();
			} else {
				return make_token(Token::PLUS);
			}
		case '-':
			if (_peek() == '=') {
				_advance();
				return make_token(Token::MINUS_EQUAL);
			} else if (is_digit(_peek()) && !_last_token_precedes_bin_op()) {
				// Number starting with '-'.
				return number();
			} else if (_peek() == '>') {
				_advance();
				return make_token(Token::FORWARD_ARROW);
			} else {
				return make_token(Token::MINUS);
			}
		case '*':
			if (_peek() == '=') {
				_advance();
				return make_token(Token::STAR_EQUAL);
			} else if (_peek() == '*') {
				if (_peek(1) == '=') {
					_advance();
					_advance(); // Advance both '*' and '='
					return make_token(Token::STAR_STAR_EQUAL);
				}
				_advance();
				return make_token(Token::STAR_STAR);
			} else {
				return make_token(Token::STAR);
			}
		case '/':
			if (_peek() == '=') {
				_advance();
				return make_token(Token::SLASH_EQUAL);
			} else {
				return make_token(Token::SLASH);
			}
		case '%':
			if (_peek() == '=') {
				_advance();
				return make_token(Token::PERCENT_EQUAL);
			} else {
				return make_token(Token::PERCENT);
			}
		case '^':
			if (_peek() == '=') {
				_advance();
				return make_token(Token::CARET_EQUAL);
			} else if (_peek() == '"' || _peek() == '\'') {
				// Node path
				return string();
			} else {
				return make_token(Token::CARET);
			}
		case '&':
			if (_peek() == '&') {
				_advance();
				return make_token(Token::AMPERSAND_AMPERSAND);
			} else if (_peek() == '=') {
				_advance();
				return make_token(Token::AMPERSAND_EQUAL);
			} else if (_peek() == '"' || _peek() == '\'') {
				// String Name
				return string();
			} else {
				return make_token(Token::AMPERSAND);
			}
		case '|':
			if (_peek() == '|') {
				_advance();
				return make_token(Token::PIPE_PIPE);
			} else if (_peek() == '=') {
				_advance();
				return make_token(Token::PIPE_EQUAL);
			} else {
				return make_token(Token::PIPE);
			}

		// Potential VCS conflict markers.
		case '=':
			if (_peek() == '=') {
				return check_vcs_marker('=', Token::EQUAL_EQUAL);
			} else {
				return make_token(Token::EQUAL);
			}
		case '<':
			if (_peek() == '=') {
				_advance();
				return make_token(Token::LESS_EQUAL);
			} else if (_peek() == '<') {
				if (_peek(1) == '=') {
					_advance();
					_advance(); // Advance both '<' and '='
					return make_token(Token::LESS_LESS_EQUAL);
				} else {
					return check_vcs_marker('<', Token::LESS_LESS);
				}
			} else {
				return make_token(Token::LESS);
			}
		case '>':
			if (_peek() == '=') {
				_advance();
				return make_token(Token::GREATER_EQUAL);
			} else if (_peek() == '>') {
				if (_peek(1) == '=') {
					_advance();
					_advance(); // Advance both '>' and '='
					return make_token(Token::GREATER_GREATER_EQUAL);
				} else {
					return check_vcs_marker('>', Token::GREATER_GREATER);
				}
			} else {
				return make_token(Token::GREATER);
			}

		default:
			if (is_whitespace(c)) {
				return make_error(vformat(R"(Invalid white space character U+%04X.)", static_cast<int32_t>(c)));
			} else {
				return make_error(vformat(R"(Invalid character "%c" (U+%04X).)", c, static_cast<int32_t>(c)));
			}
	}
}

BSTokenizerText::BSTokenizerText() {
	// Upstream reads the editor's indent size here (fs_tokenizer.cpp:1891-1895) through
	// `editor/settings/editor_settings.h`, which the seam guards out. The language default stands
	// until the editor-integration milestone supplies one through `EditorInterface`.
}

} // namespace barista_script
