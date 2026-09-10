/**************************************************************************/
/*  parser_helpers.cpp                                                    */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "parser_helpers.h"
#include "bs_tokenizer.h"
#include "doctest.h"
#include <algorithm>
#include <tuple>

using namespace godot;
namespace barista_script {
struct ParserTestAccess {
	static int max_nesting_depth() { return BSParser::MAX_NESTING_DEPTH; }
	static const BSParser::Node *nodes(const BSParser &parser) { return parser.list; }
};
namespace test {
int max_nesting_depth() { return ParserTestAccess::max_nesting_depth(); }
bool SourceSpan::operator==(const SourceSpan &other) const {
	return std::tie(start_line, start_column, end_line, end_column) == std::tie(other.start_line, other.start_column, other.end_line, other.end_column);
}
bool NodeSnapshot::operator==(const NodeSnapshot &other) const { return type == other.type && span == other.span; }
bool Diagnostic::operator==(const Diagnostic &other) const { return span == other.span && message == other.message; }
std::vector<BSParser::Node::Type> ParseSnapshot::kinds() const {
	std::vector<BSParser::Node::Type> result;
	for (const auto &node : nodes) {
		result.push_back(node.type);
	}
	return result;
}
bool ParseSnapshot::contains(BSParser::Node::Type type) const {
	return std::any_of(nodes.begin(), nodes.end(), [type](const NodeSnapshot &node) { return node.type == type; });
}
bool ParseSnapshot::diagnostic_contains(const String &text) const {
	return std::any_of(diagnostics.begin(), diagnostics.end(), [&text](const Diagnostic &diagnostic) { return diagnostic.message.contains(text); });
}
bool ParseSnapshot::diagnostic_is(const String &text) const {
	return std::any_of(diagnostics.begin(), diagnostics.end(), [&text](const Diagnostic &diagnostic) { return diagnostic.message == text; });
}
ParseSnapshot snapshot(const BSParser &parser, Error error) {
	ParseSnapshot result{ error, error == OK && parser.get_errors().is_empty(), parser.get_tree() != nullptr, parser.has_tokenizer_failure(), {}, {}, {} };
	for (const auto *node = ParserTestAccess::nodes(parser); node; node = node->next) {
		result.nodes.push_back({ node->type, { node->start_line, node->start_column, node->end_line, node->end_column } });
	}
	for (const auto *error : parser.get_errors_in_source_order()) {
		result.diagnostics.push_back({ { error->line, error->column, error->end_line, error->end_column }, error->message });
	}
	if (result.has_tree) {
		BSParser::TreePrinter printer;
		result.rendered_tree = printer.render_tree(parser);
	}
	return result;
}
ParseSnapshot parse_text(const String &source) {
	BSParser parser;
	return snapshot(parser, parser.parse(source, PARSER_PATH, false));
}
ParseSnapshot parse_bytes(const PackedByteArray &bytes) {
	String source, error;
	if (!BSTokenizer::decode_source(bytes, &source, &error)) {
		return { ERR_INVALID_DATA, false, false, true, {}, { { { 0, 0, 0, 0 }, error } }, {} };
	}
	return parse_text(source);
}
ParseSnapshot parse_buffer(const PackedByteArray &buffer) {
	BSParser parser;
	return snapshot(parser, parser.parse_binary(buffer, PARSER_PATH));
}
PackedByteArray tokenize(const String &source, bool compress) {
	return BSTokenizerBuffer::parse_code_string(source, compress ? BSTokenizerBuffer::COMPRESS_ZSTD : BSTokenizerBuffer::COMPRESS_NONE);
}

// Compare the actual expression graph, ignoring spans/grouping punctuation. This
// deliberately fails on a new kind instead of silently treating it as equal.
bool expressions_equal(const BSParser::ExpressionNode *left, const BSParser::ExpressionNode *right) {
	using P = BSParser;
	if (!left || !right) {
		return left == right;
	}
	if (left->type != right->type) {
		return false;
	}
	switch (left->type) {
		case P::Node::IDENTIFIER:
			return static_cast<const P::IdentifierNode *>(left)->name == static_cast<const P::IdentifierNode *>(right)->name;
		case P::Node::LITERAL:
			return static_cast<const P::LiteralNode *>(left)->value == static_cast<const P::LiteralNode *>(right)->value;
		case P::Node::BINARY_OPERATOR: {
			const auto *a = static_cast<const P::BinaryOpNode *>(left), *b = static_cast<const P::BinaryOpNode *>(right);
			return a->operation == b->operation && expressions_equal(a->left_operand, b->left_operand) && expressions_equal(a->right_operand, b->right_operand);
		}
		case P::Node::UNARY_OPERATOR: {
			const auto *a = static_cast<const P::UnaryOpNode *>(left), *b = static_cast<const P::UnaryOpNode *>(right);
			return a->operation == b->operation && expressions_equal(a->operand, b->operand);
		}
		case P::Node::TERNARY_OPERATOR: {
			const auto *a = static_cast<const P::TernaryOpNode *>(left), *b = static_cast<const P::TernaryOpNode *>(right);
			return expressions_equal(a->condition, b->condition) && expressions_equal(a->true_expr, b->true_expr) && expressions_equal(a->false_expr, b->false_expr);
		}
		case P::Node::TYPE_TEST: {
			const auto *a = static_cast<const P::TypeTestNode *>(left), *b = static_cast<const P::TypeTestNode *>(right);
			CHECK(a->test_type);
			CHECK(b->test_type);
			if (!a->test_type || !b->test_type) {
				return false;
			}
			// The only type-test fixture here is the simple built-in `int`.
			CHECK(a->test_type->type_chain.size() == 1);
			CHECK(b->test_type->type_chain.size() == 1);
			if (a->test_type->type_chain.size() != 1 || b->test_type->type_chain.size() != 1) {
				return false;
			}
			return a->test_type->type_chain[0]->name == b->test_type->type_chain[0]->name && expressions_equal(a->operand, b->operand);
		}
		case P::Node::SUBSCRIPT: {
			const auto *a = static_cast<const P::SubscriptNode *>(left), *b = static_cast<const P::SubscriptNode *>(right);
			return a->is_attribute == b->is_attribute && expressions_equal(a->base, b->base) && expressions_equal(a->index, b->index);
		}
		case P::Node::CALL: {
			const auto *a = static_cast<const P::CallNode *>(left), *b = static_cast<const P::CallNode *>(right);
			if (!expressions_equal(a->callee, b->callee) || a->arguments.size() != b->arguments.size()) {
				return false;
			}
			for (int i = 0; i < a->arguments.size(); ++i) {
				if (!expressions_equal(a->arguments[i], b->arguments[i])) {
					return false;
				}
			}
			return a->argument_names == b->argument_names;
		}
		default:
			FAIL_CHECK("unhandled expression kind in structural comparison");
			return false;
	}
}
} // namespace test
} // namespace barista_script
