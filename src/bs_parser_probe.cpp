/**************************************************************************/
/*  bs_parser_probe.cpp                                                   */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_parser_probe.h"

#ifdef DEBUG_ENABLED

#include "bs_parser.h"
#include "bs_tokenizer.h"
#include "bs_tokenizer_buffer.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

using namespace godot;

namespace barista_script {

namespace {

// Fields are tab-separated for the reason the tokenizer probe's are: the punctuation a parser
// reports about is exactly the punctuation a delimiter would collide with. No node name and no
// rendered position contains a tab.
const char *FIELD_SEPARATOR = "\t";

String render_span(int p_start_line, int p_start_column, int p_end_line, int p_end_column) {
	// The tokenizer's convention, carried through unchanged: 1-based line and column, start
	// inclusive, end exclusive.
	return vformat("%d:%d-%d:%d", p_start_line, p_start_column, p_end_line, p_end_column);
}

PackedStringArray render_diagnostics(const BSParser &p_parser) {
	PackedStringArray rendered;
	// `get_errors_in_source_order()` is the parser's own presentation order; `errors` keeps emission
	// order, which is not source order. A test that compared emission order would be asserting the
	// order the sweeps happen to run in.
	for (const BSParser::ParserError *error : p_parser.get_errors_in_source_order()) {
		rendered.push_back(vformat("%s%s%s",
				render_span(error->line, error->column, error->end_line, error->end_column),
				FIELD_SEPARATOR, error->message));
	}
	return rendered;
}

// `p_node_list` is `BSParser::list`, which only a friend of `BSParser` may read; the probe's
// members are friends and this helper is not, so the head is passed in rather than reached for.
Dictionary render_report(const BSParser &p_parser, const BSParser::Node *p_node_list, Error p_error) {
	Dictionary report;
	report["error"] = (int)p_error;
	// A run that never got as far as reporting -- a token buffer this build cannot read, say -- has
	// an empty diagnostic list and no tree. Calling that "complete" is exactly the partial-tree-as-
	// complete failure the contract forbids, so the returned Error counts too.
	report["complete"] = p_error == OK && p_parser.get_errors().is_empty();
	report["has_tree"] = p_parser.get_tree() != nullptr;
	report["tokenizer_failed"] = p_parser.has_tokenizer_failure();
	report["diagnostics"] = render_diagnostics(p_parser);

	PackedStringArray nodes;
	PackedStringArray node_types;
	HashSet<int> seen_types;
	for (const BSParser::Node *node = p_node_list; node != nullptr; node = node->next) {
		const String type_name = BSParser::get_node_type_name(node->type);
		nodes.push_back(vformat("%s%s%s", type_name, FIELD_SEPARATOR,
				render_span(node->start_line, node->start_column, node->end_line, node->end_column)));
		if (!seen_types.has((int)node->type)) {
			seen_types.insert((int)node->type);
			node_types.push_back(type_name);
		}
	}
	node_types.sort();
	report["nodes"] = nodes;
	report["node_types"] = node_types;

#ifdef DEBUG_ENABLED
	if (p_parser.get_tree() != nullptr) {
		BSParser::TreePrinter printer;
		report["tree"] = printer.render_tree(p_parser);
	} else {
		report["tree"] = String();
	}
#else
	// `TreePrinter` only exists in debug builds, like the diagnostics it renders.
	report["tree"] = String();
#endif // DEBUG_ENABLED

	return report;
}

} // namespace

void BaristaScriptParserProbe::_bind_methods() {
	ClassDB::bind_method(D_METHOD("parse_text", "source_utf8", "script_path"), &BaristaScriptParserProbe::parse_text);
	ClassDB::bind_method(D_METHOD("parse_token_buffer", "token_buffer", "script_path"), &BaristaScriptParserProbe::parse_token_buffer);
	ClassDB::bind_method(D_METHOD("reused_parse_reports", "source_utf8", "token_buffer", "script_path"), &BaristaScriptParserProbe::reused_parse_reports);
	ClassDB::bind_method(D_METHOD("tokenize_to_buffer", "source_utf8", "compress"), &BaristaScriptParserProbe::tokenize_to_buffer);
	ClassDB::bind_method(D_METHOD("node_type_names"), &BaristaScriptParserProbe::node_type_names);
	ClassDB::bind_method(D_METHOD("removed_type_name_diagnostic", "spelling"), &BaristaScriptParserProbe::removed_type_name_diagnostic);
	ClassDB::bind_method(D_METHOD("nested_source", "prefix", "open", "close", "suffix", "depth"), &BaristaScriptParserProbe::nested_source);
	ClassDB::bind_method(D_METHOD("max_nesting_depth"), &BaristaScriptParserProbe::max_nesting_depth);
}

Dictionary BaristaScriptParserProbe::parse_text(const PackedByteArray &p_source_utf8, const String &p_script_path) const {
	String source;
	String decode_error;
	if (!BSTokenizer::decode_source(p_source_utf8, &source, &decode_error)) {
		// The source never reached the parser, so there is no parser report to render. Saying so
		// explicitly is the point: a decode failure must not arrive as an empty but "complete" tree.
		Dictionary report;
		report["error"] = (int)ERR_INVALID_DATA;
		report["complete"] = false;
		report["has_tree"] = false;
		PackedStringArray diagnostics;
		diagnostics.push_back(vformat("0:0-0:0%s%s", FIELD_SEPARATOR, decode_error));
		report["diagnostics"] = diagnostics;
		report["nodes"] = PackedStringArray();
		report["node_types"] = PackedStringArray();
		report["tree"] = String();
		return report;
	}

	BSParser parser;
	const Error error = parser.parse(source, p_script_path, false);
	return render_report(parser, parser.list, error);
}

Dictionary BaristaScriptParserProbe::parse_token_buffer(const PackedByteArray &p_token_buffer, const String &p_script_path) const {
	BSParser parser;
	const Error error = parser.parse_binary(p_token_buffer, p_script_path);
	return render_report(parser, parser.list, error);
}

Array BaristaScriptParserProbe::reused_parse_reports(const PackedByteArray &p_source_utf8, const PackedByteArray &p_token_buffer, const String &p_script_path) const {
	Array reports;
	BSParser parser;

	String source;
	String decode_error;
	if (BSTokenizer::decode_source(p_source_utf8, &source, &decode_error)) {
		const Error first_error = parser.parse(source, p_script_path, false);
		reports.push_back(render_report(parser, parser.list, first_error));
	}

	const Error second_error = parser.parse_binary(p_token_buffer, p_script_path);
	reports.push_back(render_report(parser, parser.list, second_error));
	return reports;
}

PackedByteArray BaristaScriptParserProbe::tokenize_to_buffer(const PackedByteArray &p_source_utf8, bool p_compress) const {
	String source;
	String decode_error;
	if (!BSTokenizer::decode_source(p_source_utf8, &source, &decode_error)) {
		return PackedByteArray();
	}
	return BSTokenizerBuffer::parse_code_string(source,
			p_compress ? BSTokenizerBuffer::COMPRESS_ZSTD : BSTokenizerBuffer::COMPRESS_NONE);
}

PackedStringArray BaristaScriptParserProbe::node_type_names() const {
	PackedStringArray names;
	for (int i = 0; i < BSParser::Node::NODE_TYPE_MAX; i++) {
		names.push_back(BSParser::get_node_type_name((BSParser::Node::Type)i));
	}
	return names;
}

String BaristaScriptParserProbe::removed_type_name_diagnostic(const String &p_spelling) const {
	return BSTokenizer::removed_type_name_diagnostic(p_spelling);
}

PackedByteArray BaristaScriptParserProbe::nested_source(const String &p_prefix, const String &p_open, const String &p_close, const String &p_suffix, int p_depth) const {
	String built = p_prefix;
	for (int i = 0; i < p_depth; i++) {
		built += p_open;
	}
	for (int i = 0; i < p_depth; i++) {
		built += p_close;
	}
	built += p_suffix;
	return built.to_utf8_buffer();
}

int BaristaScriptParserProbe::max_nesting_depth() const {
	return BSParser::MAX_NESTING_DEPTH;
}

} // namespace barista_script

#endif // DEBUG_ENABLED
