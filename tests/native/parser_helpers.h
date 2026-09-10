/**************************************************************************/
/*  parser_helpers.h                                                      */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_parser.h"
#include "bs_tokenizer_buffer.h"
#include <vector>

namespace barista_script::test {
struct SourceSpan {
	int start_line, start_column, end_line, end_column;
	bool operator==(const SourceSpan &other) const;
};
struct NodeSnapshot {
	BSParser::Node::Type type;
	SourceSpan span;
	bool operator==(const NodeSnapshot &other) const;
};
struct Diagnostic {
	SourceSpan span;
	godot::String message;
	bool operator==(const Diagnostic &other) const;
};
struct ParseSnapshot {
	godot::Error error;
	bool complete, has_tree, tokenizer_failed;
	std::vector<NodeSnapshot> nodes;
	std::vector<Diagnostic> diagnostics;
	// Retain the legacy printer identity assertion in addition to typed node checks.
	godot::String rendered_tree;
	std::vector<BSParser::Node::Type> kinds() const;
	bool contains(BSParser::Node::Type type) const;
	bool diagnostic_contains(const godot::String &text) const;
	bool diagnostic_is(const godot::String &text) const;
};
int max_nesting_depth();
ParseSnapshot snapshot(const BSParser &parser, godot::Error error);
ParseSnapshot parse_text(const godot::String &source);
ParseSnapshot parse_bytes(const godot::PackedByteArray &bytes);
ParseSnapshot parse_buffer(const godot::PackedByteArray &buffer);
godot::PackedByteArray tokenize(const godot::String &source, bool compress = false);
bool expressions_equal(const BSParser::ExpressionNode *left, const BSParser::ExpressionNode *right);
inline constexpr const char *PARSER_PATH = "res://tests/parser_fixture.barista";
} // namespace barista_script::test
