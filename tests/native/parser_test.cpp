/**************************************************************************/
/*  parser_test.cpp                                                       */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_tokenizer.h"
#include "doctest.h"
#include "parser_fixtures.h"
#include "parser_helpers.h"
#include "test_require.h"
#include <algorithm>
#include <godot_cpp/classes/file_access.hpp>
#include <set>

using namespace godot;
using namespace barista_script;
using namespace barista_script::test;
using N = BSParser::Node;

namespace {
void rejected_buffer(const PackedByteArray &buffer) {
	const auto report = parse_buffer(buffer);
	CHECK(report.error != OK);
	CHECK_FALSE(report.complete);
	CHECK(report.tokenizer_failed);
	CHECK(report.diagnostics.empty());
	CHECK_FALSE(report.has_tree);
	CHECK(report.nodes.empty());
}
void grouping(const char *plain, const char *same, const char *different = nullptr) {
	INFO(plain);
	const String prefix = "var a := 1\nvar b := 2\nvar c := 3\nvar d := 4\nvar e := 5\nvar probe_expression = ";
	BSParser a, b, c;
	BS_TEST_REQUIRE(a.parse(prefix + String(plain) + "\n", PARSER_PATH, false) == OK);
	BS_TEST_REQUIRE(b.parse(prefix + String(same) + "\n", PARSER_PATH, false) == OK);
	const auto *left = a.get_tree()->get_member("probe_expression").variable->initializer;
	const auto *right = b.get_tree()->get_member("probe_expression").variable->initializer;
	BS_TEST_REQUIRE(left);
	BS_TEST_REQUIRE(right);
	CHECK(expressions_equal(left, right));
	if (different && c.parse(prefix + String(different) + "\n", PARSER_PATH, false) == OK) {
		CHECK_FALSE(expressions_equal(left, c.get_tree()->get_member("probe_expression").variable->initializer));
	}
}
} //namespace

TEST_SUITE("parser") {
	TEST_CASE("undecodable_source_produces_no_tree") {
		PackedByteArray bytes;
		bytes.push_back(0xc3);
		bytes.push_back(0x28);
		const auto report = parse_bytes(bytes);
		CHECK_FALSE(report.complete);
		CHECK_FALSE(report.has_tree);
		CHECK(report.error == ERR_INVALID_DATA);
		BS_TEST_REQUIRE(report.diagnostics.size() == 1);
		CHECK(report.nodes.empty());
		String source, error;
		CHECK_FALSE(BSTokenizer::decode_source(bytes, &source, &error));
		CHECK(report.diagnostics[0].message == error);
	}
	TEST_CASE("tokenizer_diagnostic_reaches_the_parser") {
		for (const char *source : { "var name = \"unterminated\n", "var count = 1L\n", "var count = 1UL\n", "var count = 99999999999999999999\n", "func f() -> ulong:\n\tpass\n", "var cast = 1 as long\n", "var tested = 1 is uint\n" }) {
			INFO(source);
			const auto report = parse_text(source);
			CHECK_FALSE(report.complete);
			CHECK(report.error != OK);
			CHECK(report.tokenizer_failed);
			CHECK(report.diagnostics.size() == 1);
		}
		for (const char *source : { "func a( -> void:\n\tpass\n", "var value: uint = 0\n", "var typed: Array[ulong] = []\n", "type Alias = long\n" }) {
			INFO(source);
			const auto report = parse_text(source);
			CHECK_FALSE(report.complete);
			CHECK_FALSE(report.tokenizer_failed);
		}
	}
	TEST_CASE("a_leading_tokenizer_diagnostic_has_a_real_position") {
		const auto report = parse_text("\"unterminated");
		CHECK_FALSE(report.complete);
		BS_TEST_REQUIRE(!report.diagnostics.empty());
		for (const auto &diagnostic : report.diagnostics) {
			CHECK(diagnostic.span.start_line >= 1);
		}
	}
	TEST_CASE("a_colon_without_a_type_is_rejected") {
		for (const char *source : { "var declared: = 1\n", "const DECLARED: = 1\n", "func f(value:= 1) -> void:\n\tpass\n" }) {
			INFO(source);
			CHECK(parse_text(source).complete);
		}
		for (const char *source : { "var declared:;\n", "const DECLARED:\n", "func f(value:) -> void:\n\tpass\n" }) {
			INFO(source);
			const auto report = parse_text(source);
			CHECK_FALSE(report.complete);
			CHECK(report.diagnostic_contains("Expected type"));
		}
	}
	TEST_CASE("a_default_value_marker_without_an_expression_is_rejected") {
		for (const char *source : { "func f(value =) -> void:\n\tpass\n", "signal changed(value =)\n", "func f(value: int =) -> void:\n\tpass\n" }) {
			INFO(source);
			const auto report = parse_text(source);
			CHECK_FALSE(report.complete);
			CHECK(report.diagnostic_contains("Expected expression"));
		}
		for (const char *source : { "func f(value = 1) -> void:\n\tpass\n", "func f(value: int = 1) -> void:\n\tpass\n" }) {
			CHECK(parse_text(source).complete);
		}
	}
	TEST_CASE("syntax_error_recovers_and_marks_the_tree_incomplete") {
		CHECK(parse_text("func a() -> void:\n\tpass\n\nfunc b() -> void:\n\tpass\n").complete);
		const auto report = parse_text("func a( -> void:\n\tpass\n\nfunc b() -> void:\n\tpass\n");
		CHECK_FALSE(report.complete);
		CHECK(report.has_tree);
		CHECK(report.contains(N::FUNCTION));
	}
	TEST_CASE("import_before_namespace_names_the_rule") {
		CHECK(parse_text("namespace game\nimport engine\n").complete);
		const auto report = parse_text("import engine\nnamespace game\n");
		CHECK_FALSE(report.complete);
		CHECK(report.diagnostic_contains("import"));
	}
	TEST_CASE("namespace_used_twice_is_rejected") { CHECK_FALSE(parse_text("namespace game\nnamespace other\n").complete); }
	TEST_CASE("global_name_kinds_are_mutually_exclusive") {
		for (const char *source : { "class_name Alpha\ntrait_name Beta\n", "class_name Alpha\nenum_name Beta\n", "trait_name Alpha\ntuple_name Beta(x: int)\n", "enum_name Alpha\ntuple_name Beta(x: int)\n" }) {
			INFO(source);
			CHECK_FALSE(parse_text(source).complete);
		}
	}
	TEST_CASE("final_trait_is_rejected") {
		const auto report = parse_text("final trait_name Alpha\n");
		CHECK_FALSE(report.complete);
		CHECK(report.diagnostic_contains("final"));
	}
	TEST_CASE("removed_type_spelling_reports_once_from_one_definition") {
		const auto report = parse_text("var value = 1 as uint\n");
		BS_TEST_REQUIRE(report.diagnostics.size() == 1);
		CHECK(report.diagnostic_is(BSTokenizer::removed_type_name_diagnostic("uint")));
	}
	TEST_CASE("removed_type_spelling_in_every_parser_owned_type_position") {
		const char *positions[][2] = {
			{ "var declared: uint = 1\n", "uint" }, { "const DECLARED: ulong = 1\n", "ulong" }, { "func f(value: long) -> void:\n\tpass\n", "long" }, { "signal changed(value: uint)\n", "uint" },
			{ "var a: Array[uint] = []\n", "uint" }, { "var b: Dictionary[String, ulong] = {}\n", "ulong" }, { "type Alias = long\n", "long" }, { "type uint = int\n", "uint" },
			{ "class_name uint\n", "uint" }, { "trait_name ulong\n", "ulong" }, { "class Inner extends uint:\n\tpass\n", "uint" }, { "class Box[uint]:\n\tpass\n", "uint" }, { "tuple Pair(x: int, y: int)\nclass long:\n\tpass\n", "long" }
		};
		for (const auto &position : positions) {
			INFO(position[0]);
			const auto report = parse_text(position[0]);
			CHECK_FALSE(report.complete);
			CHECK(report.diagnostic_is(BSTokenizer::removed_type_name_diagnostic(position[1])));
		}
	}
	TEST_CASE("a_valid_current_version_token_buffer_parses") {
		const auto buffer = tokenize("func a() -> void:\n\tpass\n");
		BS_TEST_REQUIRE(buffer.size() > 8);
		const auto report = parse_buffer(buffer);
		CHECK(report.error == OK);
		CHECK_FALSE(report.tokenizer_failed);
	}
	TEST_CASE("token_buffer_lexical_and_parser_failures_are_distinct") {
		const auto syntax = parse_buffer(tokenize("func a( -> void:\n\tpass\n"));
		CHECK(syntax.error != OK);
		CHECK_FALSE(syntax.tokenizer_failed);
		CHECK(parse_buffer(tokenize("var x = 1L\n")).tokenizer_failed);
	}
	TEST_CASE("token_buffer_from_another_format_is_refused") {
		const auto buffer = tokenize("func a() -> void:\n\tpass\n");
		BS_TEST_REQUIRE(buffer.size() > 8);
		auto corrupted = buffer.duplicate();
		corrupted.set(4, (corrupted[4] + 1) & 0xff);
		rejected_buffer(corrupted);
		rejected_buffer(buffer.slice(0, buffer.size() / 2));
		rejected_buffer(PackedByteArray());
	}
	TEST_CASE("a_previous_format_version_buffer_is_refused") {
		for (const char *fixture : { "legacy_v2_token_buffer.bin", "legacy_v2_token_buffer_zstd.bin" }) {
			INFO(fixture);
			const auto bytes = FileAccess::get_file_as_bytes(String("res://tests/buffer_fixtures/") + fixture);
			BS_TEST_REQUIRE(bytes.size() > 12);
			CHECK(bytes.slice(0, 4) == String("BSTB").to_ascii_buffer());
			CHECK(bytes.decode_u32(4) == BSTokenizerBuffer::TOKENIZER_VERSION - 1);
			rejected_buffer(bytes);
		}
	}
	TEST_CASE("a_reused_parser_clears_tokenizer_failure") {
		const auto valid = tokenize("func a() -> void:\n\tpass\n");
		BS_TEST_REQUIRE(valid.size() > 8);
		auto rejected = valid.duplicate();
		rejected.set(4, (rejected[4] + 1) & 0xff);
		BSParser parser;
		const auto first = snapshot(parser, parser.parse_binary(rejected, PARSER_PATH));
		const auto second = snapshot(parser, parser.parse_binary(valid, PARSER_PATH));
		CHECK(first.error != OK);
		CHECK(first.tokenizer_failed);
		CHECK(second.error == OK);
		CHECK_FALSE(second.tokenizer_failed);
		CHECK(second.kinds() == parse_buffer(valid).kinds());
	}
	TEST_CASE("deep_nesting_is_a_diagnostic_not_a_crash") {
		const int depth = max_nesting_depth() + 8;
		CHECK_FALSE(parse_text(String("var deep = ") + String("(").repeat(depth) + String(")").repeat(depth) + "1\n").complete);
		CHECK_FALSE(parse_text(String("var deep: Array[") + String("Array[").repeat(depth) + String("]").repeat(depth) + "int]\n").complete);
		String source = "func f() -> void:\n";
		for (int level = 0; level < depth; ++level) {
			source += String("\t").repeat(level + 1) + "if true:\n";
		}
		source += String("\t").repeat(depth + 1) + "pass\n";
		const auto report = parse_text(source);
		CHECK_FALSE(report.complete);
		CHECK(report.diagnostic_contains("too deep"));
	}
	TEST_CASE("parsing_twice_is_identical") {
		const auto first = parse_text(rich_source()), second = parse_text(rich_source());
		CHECK(first.nodes == second.nodes);
		CHECK(first.diagnostics == second.diagnostics);
		CHECK(first.rendered_tree == second.rendered_tree);
	}
	TEST_CASE("cold_and_cache_warm_parses_agree") {
		const auto cold = parse_text(rich_source());
		BS_TEST_REQUIRE(cold.complete);
		const auto kinds = cold.kinds();
		CHECK(std::set<N::Type>(kinds.begin(), kinds.end()).size() >= 20);
		for (bool compress : { false, true }) {
			const auto warm = parse_buffer(tokenize(rich_source(), compress));
			CHECK(warm.complete);
			CHECK(cold.kinds() == warm.kinds());
			CHECK(cold.nodes == warm.nodes);
		}
	}
	TEST_CASE("full_span_agreement_is_sensitive_to_a_perturbed_span") {
		const auto cold = parse_text("var x = 1\n");
		BS_TEST_REQUIRE(cold.complete);
		auto buffer = tokenize("var x = 1\n");
		BS_TEST_REQUIRE(buffer.size() > 8);
		CHECK(cold.nodes == parse_buffer(buffer).nodes);
		const auto offset = buffer.size() - 4;
		buffer.set(offset, (buffer[offset] + 1) & 0xff);
		const auto warm = parse_buffer(buffer);
		BS_TEST_REQUIRE(warm.complete);
		CHECK(cold.kinds() == warm.kinds());
		CHECK_FALSE(cold.nodes == warm.nodes);
	}
	TEST_CASE("two_parsers_do_not_interfere") {
		const auto before = parse_text(rich_source());
		parse_text("class_name Other\nvar x := 1\n");
		parse_text("func ( bad\n");
		const auto after = parse_text(rich_source());
		CHECK(before.nodes == after.nodes);
		CHECK(before.diagnostics == after.diagnostics);
	}
	TEST_CASE("a_reused_parser_carries_nothing_over") {
		BSParser parser;
		const auto first = snapshot(parser, parser.parse("func a( -> void:\n\tpass\n", PARSER_PATH, false));
		const auto buffer = tokenize("func a() -> void:\n\tpass\n");
		const auto second = snapshot(parser, parser.parse_binary(buffer, PARSER_PATH));
		CHECK_FALSE(first.complete);
		CHECK(second.complete);
		CHECK_FALSE(second.tokenizer_failed);
		CHECK(second.kinds() == parse_buffer(buffer).kinds());
	}
	TEST_CASE("positions_are_one_based_and_end_exclusive") {
		const auto report = parse_text("var x = 1\n");
		bool variable = false, literal = false;
		for (const auto &node : report.nodes) {
			if (node.type == N::VARIABLE) {
				variable = true;
				CHECK(node.span == SourceSpan{ 1, 1, 1, 10 });
			}
			if (node.type == N::LITERAL) {
				literal = true;
				CHECK(node.span == SourceSpan{ 1, 9, 1, 10 });
			}
			if (node.type != N::CLASS) {
				CHECK(node.span.start_line >= 1);
				CHECK(node.span.start_column >= 1);
				CHECK(node.span.end_line >= 1);
				CHECK(node.span.end_column >= 1);
			}
		}
		CHECK(variable);
		CHECK(literal);
	}
	TEST_CASE("precedence_and_associativity") {
		const char *cases[][3] = {
			{ "a or b and c", "a or (b and c)", "(a or b) and c" }, { "a and b in c", "a and (b in c)", "(a and b) in c" }, { "a in b == c", "a in (b == c)", "(a in b) == c" },
			{ "a == b | c", "a == (b | c)", "(a == b) | c" }, { "a | b ^ c", "a | (b ^ c)", "(a | b) ^ c" }, { "a ^ b & c", "a ^ (b & c)", "(a ^ b) & c" },
			{ "a & b << c", "a & (b << c)", "(a & b) << c" }, { "a << b + c", "a << (b + c)", "(a << b) + c" }, { "a + b * c", "a + (b * c)", "(a + b) * c" },
			{ "a * b ** c", "a * (b ** c)", "(a * b) ** c" }, { "a ** b is int", "a ** (b is int)", "(a ** b) is int" }, { "-a ** b", "-(a ** b)", "(-a) ** b" },
			{ "~a * b", "(~a) * b", "~(a * b)" }, { "not a == b", "not (a == b)", "(not a) == b" }, { "a.b(c)", "(a.b)(c)", "a.(b(c))" },
			{ "a - b - c", "(a - b) - c", "a - (b - c)" }, { "a / b / c", "(a / b) / c", "a / (b / c)" }, { "a << b << c", "(a << b) << c", "a << (b << c)" }, { "a ** b ** c", "(a ** b) ** c", "a ** (b ** c)" },
			{ "-2 ** 2", "(-2) ** 2", "-(2 ** 2)" }, { "+2 ** 2", "(+2) ** 2", "+(2 ** 2)" }, { "- 2 ** 2", "-(2 ** 2)", "(-2) ** 2" }, { "+ 2 ** 2", "+(2 ** 2)", "(+2) ** 2" },
			{ "a-2 ** 2", "a - (2 ** 2)", "(a - 2) ** 2" }, { "a -2 ** 2", "a - (2 ** 2)", "(a - 2) ** 2" }
		};
		for (const auto &row : cases) {
			grouping(row[0], row[1], row[2]);
		}
		grouping("a if b else c if d else e", "a if b else (c if d else e)");
	}
	TEST_CASE("bracketed_types_span_lines") {
		for (const char *form : { "var values: Array[\n\tint\n] = []\n", "var table: Dictionary[\n\tString,\n\tint\n] = {}\n", "var call: Callable[[\n\tint\n], void]\n", "var sig: Signal[[\n\tint\n]]\n", "var work: Coroutine[\n\tint\n]\n", "var handle: Type[\n\tNode\n]\n", "var nested: Array[\n\tArray[\n\t\tint\n\t]\n]\n" }) {
			INFO(form);
			const auto report = parse_text(String(form) + "var after = 1\n");
			CHECK(report.complete);
			CHECK(std::count_if(report.nodes.begin(), report.nodes.end(), [](const NodeSnapshot &node) { return node.type == N::VARIABLE; }) == 2);
		}
		CHECK(parse_text("var values: Array[int] = []\nvar after = 1\n").complete);
	}
	TEST_CASE("node_type_vocabulary_is_closed") {
		std::set<N::Type> produced;
		for (const char *source : { rich_source(), "class_name WithTrait\ntrait Marker:\n\tpass\n", "trait_name Shape\nfunc area() -> int:\n\treturn 0\n", "enum_name Direction:\n\tNORTH = 0\n\tSOUTH = 1\n", "tuple_name Point(x: int, y: int)\n", "annotation deprecated(reason: String) targets METHOD\n", "func f() -> void:\n\twhile true:\n\t\tbreakpoint\n\t\tbreak\n", "func f(a: int, b: int) -> void:\n\tvar (x, y) = (a, b)\n\tpass\n", "class Box[T]:\n\tvar value: T\n", "func f(value: Variant) -> void:\n\tmatch value:\n\t\t[1, 2]:\n\t\t\tpass\n\t\t{\"k\": 1}:\n\t\t\tpass\n\t\tvar bound:\n\t\t\tpass\n" }) {
			INFO(source);
			const auto report = parse_text(source);
			CHECK(report.complete);
			for (const auto &node : report.nodes) {
				produced.insert(node.type);
			}
		}
		// NONE is a default value; CONFORMANCE is covered by the retained corpus.
		// NODE_TYPE_MAX is the iteration bound and cannot be allocated.
		for (int i = 0; i < N::NODE_TYPE_MAX; ++i) {
			const auto type = static_cast<N::Type>(i);
			INFO(BSParser::get_node_type_name(type));
			const bool excused = type == N::NONE || type == N::CONFORMANCE;
			CHECK((produced.count(type) > 0) != excused);
		}
	}
	TEST_CASE("node_type_names_are_distinct") {
		std::set<String> names;
		for (int i = 0; i < N::NODE_TYPE_MAX; ++i) {
			const auto name = BSParser::get_node_type_name(static_cast<N::Type>(i));
			CHECK_FALSE(name.is_empty());
			CHECK(names.insert(name).second);
		}
		CHECK_FALSE(names.empty());
	}
}
