/**************************************************************************/
/*  bs_tokenizer.h                                                        */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_platform.h"

namespace barista_script {

/**
 * Hard fork of Foundry's tokenizer (`modules/foundry_script/fs_tokenizer.h` @
 * c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6), renamed `FS*` -> `BS*` and reached through the
 * platform seam instead of Godot's `core/` headers.
 *
 * The one behavioural divergence is D1 (docs/GRAMMAR.md section 0.2): BaristaScript has a single
 * integer type on the signed 64-bit `Variant::INT` carrier, so the whole numeric tower is deleted
 * rather than ported. Concretely, relative to upstream:
 *
 *   - The two width descriptors upstream hangs on every token (fs_tokenizer.h:182,187) are gone,
 *     together with `make_numeric_literal` (fs_tokenizer.h:312). A literal's Variant carrier is the
 *     whole of its type.
 *   - `Token::AS_BANG` (fs_tokenizer.h:112) is gone. `as!` is reserved and rejected, never lexed.
 *   - `uint`, `ulong` and `long` are reserved spellings in the one keyword table and are rejected
 *     where upstream produced ordinary identifiers.
 *
 * None of that is optional. The seam deletes the upstream header the tower lived in and poisons its
 * type name, so a surviving reference anywhere in the port is a compile error that names the
 * decision rather than a stub that lets the tower grow back.
 */
class BSTokenizer {
public:
	enum CursorPlace {
		CURSOR_NONE,
		CURSOR_BEGINNING,
		CURSOR_MIDDLE,
		CURSOR_END,
	};

	struct Token {
		// If this enum changes, please increment the TOKENIZER_VERSION in bs_tokenizer_buffer.h
		enum Type {
			EMPTY,
			// Basic
			ANNOTATION,
			IDENTIFIER,
			// A spelling D1 removed from the type system (`uint`, `ulong`, `long`). It is not a
			// keyword: `Token::is_identifier()` accepts it, so it stays usable as an ordinary
			// name exactly as `int` is (docs/GRAMMAR.md section 2.5). Giving it its own type is
			// what lets a type position recognize it and reject it, here or in the parser,
			// instead of it arriving as an anonymous identifier that silently becomes a user type.
			RESERVED_TYPE_NAME,
			LITERAL,
			// Comparison
			LESS,
			LESS_EQUAL,
			GREATER,
			GREATER_EQUAL,
			EQUAL_EQUAL,
			BANG_EQUAL,
			// Logical
			AND,
			OR,
			NOT,
			AMPERSAND_AMPERSAND,
			PIPE_PIPE,
			BANG,
			// Bitwise
			AMPERSAND,
			PIPE,
			TILDE,
			CARET,
			LESS_LESS,
			GREATER_GREATER,
			// Math
			PLUS,
			MINUS,
			STAR,
			STAR_STAR,
			SLASH,
			PERCENT,
			// Assignment
			EQUAL,
			PLUS_EQUAL,
			MINUS_EQUAL,
			STAR_EQUAL,
			STAR_STAR_EQUAL,
			SLASH_EQUAL,
			PERCENT_EQUAL,
			LESS_LESS_EQUAL,
			GREATER_GREATER_EQUAL,
			AMPERSAND_EQUAL,
			PIPE_EQUAL,
			CARET_EQUAL,
			// Control flow
			IF,
			ELIF,
			ELSE,
			FOR,
			WHILE,
			BREAK,
			CONTINUE,
			PASS,
			RETURN,
			MATCH,
			WHEN,
			// Keywords
			ABSTRACT,
			AS,
			ASSERT,
			AWAIT,
			BREAKPOINT,
			CLASS,
			CLASS_NAME,
			ENUM_NAME,
			TK_CONST, // Conflict with WinAPI.
			ENUM,
			EXTENDS,
			FINAL,
			FUNC,
			IMPORT,
			TK_IN, // Conflict with WinAPI.
			IS,
			NAMESPACE,
			PRELOAD,
			SELF,
			SIGNAL,
			STATIC,
			SUPER,
			TRAIT,
			TRAIT_NAME,
			TUPLE,
			TUPLE_NAME,
			USES,
			VAR,
			TK_VOID, // Conflict with WinAPI.
			YIELD,
			// Punctuation
			BRACKET_OPEN,
			BRACKET_CLOSE,
			BRACE_OPEN,
			BRACE_CLOSE,
			PARENTHESIS_OPEN,
			PARENTHESIS_CLOSE,
			COMMA,
			SEMICOLON,
			PERIOD,
			PERIOD_PERIOD,
			PERIOD_PERIOD_PERIOD,
			COLON,
			DOLLAR,
			FORWARD_ARROW,
			UNDERSCORE,
			// Whitespace
			NEWLINE,
			INDENT,
			DEDENT,
			// Constants
			CONST_PI,
			CONST_TAU,
			CONST_INF,
			CONST_NAN,
			// Error message improvement
			VCS_CONFLICT_MARKER,
			BACKTICK,
			QUESTION_MARK,
			// Special
			ERROR,
			TK_EOF, // "EOF" is reserved
			TK_MAX
		};

		Type type = EMPTY;
		Variant literal;
		int start_line = 0, end_line = 0, start_column = 0, end_column = 0;
		int cursor_position = -1;
		CursorPlace cursor_place = CURSOR_NONE;
		String source;

		const char *get_name() const;
		String get_debug_name() const;
		bool can_precede_bin_op() const;
		bool is_identifier() const;
		bool is_node_name() const;
		StringName get_identifier() const { return literal; }

		Token(Type p_type) {
			type = p_type;
		}

		Token() {}
	};

#ifdef TOOLS_ENABLED
	struct CommentData {
		String comment;
		// true: Comment starts at beginning of line or after indentation.
		// false: Inline comment (starts after some code).
		bool new_line = false;
		CommentData() {}
		CommentData(const String &p_comment, bool p_new_line) {
			comment = p_comment;
			new_line = p_new_line;
		}
	};
	virtual const HashMap<int, CommentData> &get_comments() const = 0;
#endif // TOOLS_ENABLED

	static String get_token_name(Token::Type p_token_type);

	/**
	 * The keyword spellings, and separately the spellings D1 reserved as type names.
	 *
	 * This is a view onto the single keyword table in `bs_tokenizer.cpp`, not a second copy, so
	 * `BaristaScriptLanguage::_get_reserved_words()` can be wired to it later without the two
	 * drifting apart.
	 */
	static Vector<String> get_keyword_spellings();
	static Vector<String> get_reserved_spellings();

	/**
	 * The diagnostic a removed type spelling reports, defined once so the tokenizer and the type
	 * positions the parser owns cannot word it differently.
	 */
	static String removed_type_name_diagnostic(const String &p_spelling);

	/**
	 * The keyword a non-ASCII identifier is visually confusable with, or an empty `String` when it
	 * is confusable with none.
	 *
	 * Unicode confusables are how a keyword gets impersonated: `clаss` with a Cyrillic `а` reads as
	 * `class` and lexes as an identifier. Foundry rejects such an identifier outright
	 * (`fs_tokenizer.cpp:668-677` @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6), which is a parse
	 * error rather than the `CONFUSABLE_IDENTIFIER` warning `BSWarning` owns; the two are different
	 * questions asked of the same TextServer feature.
	 *
	 * The dictionary is `get_keyword_spellings()`, the one keyword table, so the set of names that
	 * may not be impersonated is by construction the set of names that are keywords.
	 *
	 * Fail-open by construction, like the warning-side predicate: no TextServer, or a build of it
	 * without the ICU-backed `FEATURE_UNICODE_SECURITY`, means no rejection. Refusing to tokenize
	 * would make whether a source file compiles depend on how the host was built.
	 */
	static String confusable_keyword(const String &p_identifier);

	/**
	 * The diagnostic a keyword-confusable identifier reports, defined once for the same reason
	 * `removed_type_name_diagnostic()` is.
	 */
	static String confusable_keyword_diagnostic(const String &p_identifier, const String &p_keyword);

	/**
	 * Decodes UTF-8 source bytes, refusing malformed input instead of substituting U+FFFD.
	 *
	 * Godot's `String::parse_utf8` reports failure but not where, and its recovery path writes a
	 * replacement character -- which is exactly the silent corruption the fail-closed contract
	 * forbids. The scan below therefore validates first and names the offending byte offset.
	 */
	static bool decode_source(const PackedByteArray &p_utf8, String *r_source, String *r_error);

#ifdef TOOLS_ENABLED
	// This is a temporary solution, as Tokens are not able to store their position, only lines and columns.
	virtual int get_current_position() const { return 0; }
	virtual String get_source_code() const { return ""; }
#endif // TOOLS_ENABLED

	virtual int get_cursor_line() const = 0;
	virtual int get_cursor_column() const = 0;
	virtual void set_cursor_position(int p_line, int p_column) = 0;
	virtual void set_multiline_mode(bool p_state) = 0;
	virtual bool is_past_cursor() const = 0;
	virtual void push_expression_indented_block() = 0; // For lambdas, or blocks inside expressions.
	virtual void pop_expression_indented_block() = 0; // For lambdas, or blocks inside expressions.
	virtual bool is_text() = 0;

	virtual Token scan() = 0;

	virtual ~BSTokenizer() {}
};

class BSTokenizerText : public BSTokenizer {
	String source;
	// Upstream keeps `const char32_t *` cursors into `String::get_data()`. godot-cpp's `String` is
	// engine-backed and exposes no such stable buffer, so the decoded code points are held here and
	// every cursor upstream spells as a pointer is spelled as an index into it.
	Char32String source_utf32;
	const char32_t *_source = nullptr;
	int line = -1, column = -1;
	int cursor_line = -1, cursor_column = -1;
	int tab_size = 4;

	// Keep track of multichar tokens.
	int _start = 0;
	int start_line = 0, start_column = 0;

	// Info cache.
	bool line_continuation = false; // Whether this line is a continuation of the previous, like when using '\'.
	bool multiline_mode = false;
	List<Token> error_stack;
	bool pending_newline = false;
	Token last_token;
	Token last_newline;
	int pending_indents = 0;
	// Whether `last_token` is a keyword token (e.g. `class`, `trait`, `return`) emitted in
	// attribute position, i.e. immediately after a `PERIOD`. `BSParser::parse_attribute` re-spells
	// any `is_node_name()`-accepted token to `IDENTIFIER` when consuming an attribute name, so such
	// a token behaves like an ordinary identifier value for the purposes of the `+`/`-`/`.<digit>`
	// disambiguation, even though `Token::can_precede_bin_op()` does not recognize its raw type.
	bool last_token_is_keyword_attribute = false;
	List<int> indent_stack;
	List<List<int>> indent_stack_stack; // For lambdas, which require manipulating the indentation point.
	List<char32_t> paren_stack;
	char32_t indent_char = '\0';
	int position = 0;
	int length = 0;
	Vector<int> continuation_lines;
	int continuation_scan_depth = 0;

#ifdef TOOLS_ENABLED
	HashMap<int, CommentData> comments;
#endif // TOOLS_ENABLED

	_FORCE_INLINE_ bool _is_at_end() { return position >= length; }
	_FORCE_INLINE_ char32_t _peek(int p_offset = 0) { return position + p_offset >= 0 && position + p_offset < length ? _source[position + p_offset] : '\0'; }
	int indent_level() const { return indent_stack.size(); }
	// Whether the last emitted token can precede a binary operator (or `.<digit>` tuple index)
	// rather than the start of a signed number or float literal. Combines
	// `Token::can_precede_bin_op()` with `last_token_is_keyword_attribute` so that keyword tokens
	// spelled in attribute position (`self.class`, `self.trait`, ...) are treated as value tokens
	// too, matching how the parser re-spells them to `IDENTIFIER`.
	bool _last_token_precedes_bin_op() const { return last_token.can_precede_bin_op() || last_token_is_keyword_attribute; }
	bool has_error() const { return !error_stack.is_empty(); }
	Token pop_error();
	char32_t _advance();
	String _get_indent_char_name(char32_t ch);
	void _skip_whitespace();
	void check_indent();

	Token make_error(const String &p_message);
	void push_error(const String &p_message);
	void push_error(const Token &p_error);
	Token make_paren_error(char32_t p_paren);
	Token make_token(Token::Type p_type);
	Token make_literal(const Variant &p_literal);
	Token make_identifier(const StringName &p_identifier);
	/**
	 * Whether the token about to be produced stands in a type position.
	 *
	 * The tokenizer cannot parse, so it recognizes only the positions the token stream itself
	 * settles: after `->`, `as` or `is`, where nothing but a type may follow, and after a `:` that
	 * is not inside a `{`. That last one is exact rather than a guess -- `block` begins with a
	 * NEWLINE (docs/GRAMMAR.md section 6), so a `:` followed by a name is never a block header; a
	 * named call argument is spelled `name = value`, not `name: value` (section 5.5), so a `:`
	 * inside `(` is a parameter or payload-field annotation; and the one construct that does spell
	 * a value after `:` is the Python-style dictionary entry, which is always inside `{`.
	 *
	 * Type positions the token stream does not settle -- a type argument (`Array[uint]`), a `type`
	 * alias -- are left to the parser, which knows what it is parsing. Being incomplete here costs
	 * a diagnostic the parser will produce anyway; guessing here would cost a rejected value.
	 */
	bool _is_type_position() const;
	Token check_vcs_marker(char32_t p_test, Token::Type p_double_type);
	void push_paren(char32_t p_char);
	bool pop_paren(char32_t p_expected);

	void newline(bool p_make_token);
	Token number();
	Token potential_identifier();
	Token string();
	Token annotation();

public:
	void set_source_code(const String &p_source_code);

	const Vector<int> &get_continuation_lines() const { return continuation_lines; }

#ifdef TOOLS_ENABLED
	virtual int get_current_position() const override { return position; }
	virtual String get_source_code() const override { return source; }
#endif // TOOLS_ENABLED

	virtual int get_cursor_line() const override;
	virtual int get_cursor_column() const override;
	virtual void set_cursor_position(int p_line, int p_column) override;
	virtual void set_multiline_mode(bool p_state) override;
	virtual bool is_past_cursor() const override;
	virtual void push_expression_indented_block() override; // For lambdas, or blocks inside expressions.
	virtual void pop_expression_indented_block() override; // For lambdas, or blocks inside expressions.
	virtual bool is_text() override { return true; }

#ifdef TOOLS_ENABLED
	virtual const HashMap<int, CommentData> &get_comments() const override {
		return comments;
	}
#endif // TOOLS_ENABLED

	virtual Token scan() override;

	BSTokenizerText();
};

} // namespace barista_script
