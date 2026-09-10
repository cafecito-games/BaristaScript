/**************************************************************************/
/*  tokenizer_test.cpp                                                    */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_parser.h"
#include "doctest.h"
#include "tokenizer_helpers.h"
#include "tokenizer_vocabulary.h"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <string>

using namespace godot;
using barista_script::BSParser;
using namespace barista_script::test;

namespace {
PackedByteArray bytes(const String &source) { return source.to_utf8_buffer(); }
PackedByteArray raw(std::initializer_list<uint8_t> values) {
	PackedByteArray result;
	for (uint8_t value : values) {
		result.push_back(value);
	}
	return result;
}
PackedStringArray fixture_paths() {
	const String root = "res://tests/corpus_fixtures/tokenizer";
	const Ref<DirAccess> directory = DirAccess::open(root);
	REQUIRE(directory.is_valid());
	PackedStringArray paths;
	for (const String &entry : directory->get_files()) {
		if (entry.ends_with(".barista")) {
			paths.push_back(root.path_join(entry));
		}
	}
	paths.sort();
	REQUIRE_FALSE(paths.is_empty());
	return paths;
}
String large_source(int count) {
	PackedStringArray lines;
	for (int index = 0; lines.size() < count; ++index) {
		lines.push_back(vformat("func step_%d(value: int) -> int:", index));
		lines.push_back(vformat("\tvar total = value * %d + 0x%s", index, String::num_int64(index, 16)));
		lines.push_back(vformat("\tvar label = \"step %d\"", index));
		lines.push_back("\tif total > 0 and label != \"\":");
		lines.push_back("\t\ttotal -= 1");
		lines.push_back("\treturn total");
		lines.push_back("");
	}
	return String("\n").join(lines.slice(0, count)) + "\n";
}
String removal(const String &spelling) {
	return vformat("\"%s\" is reserved. BaristaScript stores every integer on one signed 64-bit carrier; write \"int\".", spelling);
}
} // namespace

TEST_SUITE("tokenizer") {
	TEST_CASE("positions_are_one_based_and_end_exclusive") {
		const auto dump = dump_tokens(bytes("var x = 1\n"));
		REQUIRE(dump.size() >= 4);
		CHECK(dump[0].begins_with("var\t1:1-1:4\t"));
		CHECK(dump[1].begins_with("Identifier\t1:5-1:6\t"));
		CHECK(dump[3].begins_with("Literal\t1:9-1:10\t"));
	}
	TEST_CASE("token_vocabulary_is_closed") {
		HashSet<String> produced;
		for (const String &line : dump_tokens(bytes(TOKENIZER_VOCABULARY))) {
			produced.insert(line.get_slice("\t", 0));
		}
		for (const String &path : fixture_paths()) {
			for (const String &line : dump_tokens(FileAccess::get_file_as_bytes(path))) {
				produced.insert(line.get_slice("\t", 0));
			}
		}
		// Empty is the default-constructed Token; neither tokenizer emits it.
		for (const String &name : token_type_names()) {
			INFO(std::string(name.utf8().get_data()));
			CHECK((produced.has(name) || name == "Empty"));
		}
		CHECK_FALSE(produced.has("Empty"));
	}
	TEST_CASE("buffer_round_trips_the_token_stream") {
		Vector<PackedByteArray> sources;
		sources.push_back(bytes(TOKENIZER_VOCABULARY));
		for (const String &path : fixture_paths()) {
			sources.push_back(FileAccess::get_file_as_bytes(path));
		}
		CHECK(dump_significant_tokens(sources[0]).size() > 200);
		for (const PackedByteArray &source : sources) {
			const auto text = dump_significant_tokens(source);
			for (bool compress : { false, true }) {
				INFO("compress=", compress);
				CHECK(text == dump_buffer_significant_tokens(source, compress));
			}
		}
	}
	TEST_CASE("tokenizing_twice_is_identical") {
		const auto source = bytes(large_source(5000));
		const auto first = dump_tokens(source);
		CHECK(first == dump_tokens(source));
		CHECK(first.size() > 5000);
	}
	TEST_CASE("tokenizers_do_not_interfere") {
		const auto first_source = bytes(TOKENIZER_VOCABULARY);
		const auto second_source = bytes(large_source(64));
		const auto baseline_first = dump_tokens(first_source);
		const auto baseline_second = dump_tokens(second_source);
		// Every utility call constructs a fresh tokenizer; no bound probe object is necessary.
		CHECK(baseline_first == dump_tokens(first_source));
		CHECK(baseline_second == dump_tokens(second_source));
		CHECK(baseline_first == dump_tokens(first_source));
	}
	TEST_CASE("reserved_table_is_the_only_source_of_truth") {
		auto reserved = reserved_spellings();
		reserved.sort();
		REQUIRE(reserved.size() == 3);
		CHECK(reserved[0] == "long");
		CHECK(reserved[1] == "uint");
		CHECK(reserved[2] == "ulong");
		HashSet<String> seen;
		for (const String &spelling : keyword_spellings()) {
			CHECK_FALSE(seen.has(spelling));
			seen.insert(spelling);
		}
		for (const String &spelling : reserved) {
			CHECK_FALSE(seen.has(spelling));
			CHECK(first_diagnostic(bytes(vformat("func f() -> %s:\n\tpass\n", spelling))) == removal(spelling));
		}
	}
	TEST_CASE("removed_spellings_are_rejected_in_type_positions") {
		for (const String &spelling : reserved_spellings()) {
			for (const char *source_template : {
						 "func f() -> %s:\n\tpass\n",
						 "func f(value: Variant) -> void:\n\tvar cast = value as %s\n",
						 "func f(value: Variant) -> void:\n\tvar flag = value is %s\n" }) {
				const String source = vformat(source_template, spelling);
				INFO(std::string(source.utf8().get_data()));
				CHECK(first_diagnostic(bytes(source)) == removal(spelling));
			}
			// A colon can introduce a single-line block; these positions belong to the parser.
			for (const char *source_template : {
						 "func f(value: %s) -> void:\n\tpass\n", "var declared: %s = 1\n",
						 "const DECLARED: %s = 1\n", "var declared: Array[%s] = []\n" }) {
				const String source = vformat(source_template, spelling);
				CHECK(first_diagnostic(bytes(source)).is_empty());
				BSParser parser;
				parser.parse(source, "res://tokenizer_test.barista", false);
				bool rejected = false;
				for (const BSParser::ParserError &diagnostic : parser.get_errors()) {
					if (diagnostic.message.ends_with(removal(spelling))) {
						rejected = true;
					}
				}
				INFO(std::string(source.utf8().get_data()));
				CHECK(rejected);
			}
		}
	}
	TEST_CASE("removed_spellings_stay_usable_as_names") {
		for (const String &spelling : reserved_spellings()) {
			for (const char *source_template : {
						 "var %s = 1\n", "func %s() -> void:\n\tpass\n",
						 "func f() -> void:\n\tvar value = %s + 1\n",
						 "func f(value: Variant) -> void:\n\tvar mapping = {\"key\": %s}\n",
						 "func f() -> void:\n\tcall(argument = %s)\n",
						 "func f() -> void:\n\tvar value = self.%s\n" }) {
				const String source = vformat(source_template, spelling);
				INFO(std::string(source.utf8().get_data()));
				CHECK(first_diagnostic(bytes(source)).is_empty());
				bool seen_reserved = false;
				for (const String &line : dump_tokens(bytes(source))) {
					CHECK_FALSE((line.begins_with("Identifier\t") && line.ends_with(":" + spelling)));
					if (line.begins_with("Reserved type name\t") && line.ends_with(":" + spelling)) {
						seen_reserved = true;
					}
				}
				CHECK(seen_reserved);
			}
		}
	}
	TEST_CASE("removal_diagnostics_name_the_spelling") {
		for (const String &spelling : reserved_spellings()) {
			CHECK(first_diagnostic(bytes(vformat("func f() -> %s:\n\tpass\n", spelling))).contains("\"" + spelling + "\""));
		}
		for (const char *suffix : { "U", "L", "UL", "u", "l", "ul", "lu", "LU", "Ul", "uL" }) {
			CHECK(first_diagnostic(bytes(vformat("var value = 123%s\n", suffix))) ==
					vformat("The \"%s\" integer literal suffix is reserved. BaristaScript has one integer type, \"int\"; write \"123\".", suffix));
		}
		CHECK(first_diagnostic(bytes("var bits = value as!other\n")).contains("\"as!\""));
		CHECK(first_diagnostic(bytes("var value = 123abc\n")) == "Invalid numeric notation.");
	}
	TEST_CASE("as_bang_is_not_as_followed_by_bang") {
		CHECK(first_diagnostic(bytes("var bits = value as!other\n")) ==
				"\"as!\" is reserved. It reinterprets between integer widths, which BaristaScript does not distinguish.");
		const auto spaced = dump_significant_tokens(bytes("var bits = value as !other\n"));
		CHECK(spaced.has("as"));
		CHECK(spaced.has("!"));
		CHECK(first_diagnostic(bytes("var bits = value as !other\n")).is_empty());
	}
	TEST_CASE("malformed_utf8_names_the_offending_byte") {
		CHECK(first_diagnostic(raw({ 0x61, 0x62, 0xC3, 0x28 })) == "Invalid UTF-8 in source at byte offset 3.");
		for (const auto &source : { raw({ 0x80 }), raw({ 0xC0, 0xAF }), raw({ 0xED, 0xA0, 0x80 }), raw({ 0xE2, 0x82 }) }) {
			CHECK(first_diagnostic(source) == "Invalid UTF-8 in source at byte offset 0.");
		}
		CHECK(first_diagnostic(bytes(String::utf8("var café = 1\n"))).is_empty());
	}
	TEST_CASE("integer_range_is_exact") {
		const char *accepted[][2] = { { "9223372036854775807", "int:9223372036854775807" },
			{ "-9223372036854775808", "int:-9223372036854775808" }, { "0x7fffffffffffffff", "int:9223372036854775807" }, { "0b1", "int:1" } };
		for (const auto &entry : accepted) {
			CHECK(dump_significant_tokens(bytes(vformat("var value = %s\n", entry[0]))).has(String("Literal\t") + entry[1]));
		}
		for (const char *spelling : { "9223372036854775808", "-9223372036854775809", "18446744073709551615", "- 9223372036854775808" }) {
			CHECK(first_diagnostic(bytes(vformat("var value = %s\n", spelling))).begins_with("Integer literal is out of range for \"int\""));
		}
	}
	TEST_CASE("signed_literal_folding_boundaries") {
		const auto negative = dump_significant_tokens(bytes("var value = -2\n"));
		CHECK(negative.has("Literal\tint:-2"));
		CHECK_FALSE(negative.has("-"));
		const auto positive = dump_significant_tokens(bytes("var value = +2\n"));
		CHECK(positive.has("Literal\tint:2"));
		CHECK_FALSE(positive.has("+"));
		for (const char *expression : { "- 2", "a-2", "a -2" }) {
			const auto tokens = dump_significant_tokens(bytes(vformat("var value = %s\n", expression)));
			CHECK(tokens.has("-"));
			CHECK(tokens.has("Literal\tint:2"));
		}
		const auto spaced_positive = dump_significant_tokens(bytes("var value = + 2\n"));
		CHECK(spaced_positive.has("+"));
		CHECK(spaced_positive.has("Literal\tint:2"));
	}
} // TEST_SUITE
