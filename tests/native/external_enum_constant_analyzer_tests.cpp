/**************************************************************************/
/*  external_enum_constant_analyzer_tests.cpp                             */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script_language.h"
#include "bs_global_class.h"
#include "storage_fixture.h"
#include "test_require.h"

#include <string>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {

const char *const ROOT = "res://tests/corpus_staging/analyzer/features/";

String path(const String &p_name) {
	return String(ROOT) + p_name + ".barista";
}

void install(StorageFixture &p_storage, const String &p_path, const String &p_source) {
	BSParser syntax;
	BS_TEST_REQUIRE(syntax.parse(p_source, p_path, false) == OK);
	BSCache::set_source_override(p_path, p_source);
	auto record = BSDeclarationIndex::record_from_global_class(p_path, p_source, bs_resolve_global_class_from_source(p_source, p_path));
	record.namespace_name = syntax.get_tree()->namespace_name;
	BS_TEST_REQUIRE(p_storage.index().commit_record(p_storage.index().claim_refresh(p_path), record));
}

String error_block(const BSParser &p_parser) {
	String result;
	for (const auto &error : p_parser.get_errors()) {
		if (!result.is_empty()) {
			result += "\n";
		}
		result += vformat(">> ERROR at line %d: %s", error.line, error.message);
	}
	return result.is_empty() ? String("BS_TEST_OK") : result;
}

void no_errors(const BSParser &p_parser) {
	for (const auto &error : p_parser.get_errors()) {
		MESSAGE(std::string(error.message.utf8().get_data()));
	}
	CHECK(error_block(p_parser) == "BS_TEST_OK");
}

String exact_external_source() {
	return "const External = preload(\"external_enum_as_constant_external.notest.barista\")\n"
		   "const MyEnum = External.MyEnum\n\n"
		   "func test():\n"
		   "\tprint(MyEnum.WAITING == 0)\n"
		   "\tprint(MyEnum.GODOT == 1)\n";
}

String exact_local_source() {
	return "class InnerClass:\n"
		   "\tenum InnerEnum:\n"
		   "\t\tA = 2\n"
		   "\tconst INNER_CONST = \"INNER_CONST\"\n\n"
		   "enum Enum:\n"
		   "\tA = 1\n\n"
		   "const Other = preload(\"./local_const_as_type.notest.barista\")\n\n"
		   "func test():\n"
		   "\tconst IC = InnerClass\n"
		   "\tconst IE = IC.InnerEnum\n"
		   "\tconst E = Enum\n"
		   "\t# Doesn't work in CI, but works in the editor. Looks like an unrelated bug. TO"
		   "DO: Investigate it.\n"
		   "\t# Error: Invalid call. Nonexistent function 'new' in base 'FoundryScript'.\n"
		   "\tvar a1: IC = null # IC.new()\n"
		   "\tvar a2: IE = IE.A\n"
		   "\tvar a3: IC.InnerEnum = IE.A\n"
		   "\tvar a4: E = E.A\n"
		   "\tprint(a1.INNER_CONST)\n"
		   "\tprint(a2)\n"
		   "\tprint(a3)\n"
		   "\tprint(a4)\n\n"
		   "\tconst O = Other\n"
		   "\tconst OV: Variant = Other # Removes metatype.\n"
		   "\tconst OIC = O.InnerClass\n"
		   "\tconst OIE = OIC.InnerEnum\n"
		   "\tconst OE = O.Enum\n"
		   "\tvar b: O = O.new()\n"
		   "\t@warning_ignore(\"unsafe_method_access\")\n"
		   "\tvar bv: OV = OV.new()\n"
		   "\tvar b1: OIC = OIC.new()\n"
		   "\tvar b2: OIE = OIE.A\n"
		   "\tvar b3: O.InnerClass.InnerEnum = OIE.A\n"
		   "\tvar b4: OE = OE.A\n"
		   "\tprint(b.CONST)\n"
		   "\tprint(bv.CONST)\n"
		   "\tprint(b1.INNER_CONST)\n"
		   "\tprint(b2)\n"
		   "\tprint(b3)\n"
		   "\tprint(b4)\n";
}

String exact_local_helper() {
	return "class InnerClass:\n"
		   "\tenum InnerEnum:\n"
		   "\t\tA = 20\n"
		   "\tconst INNER_CONST = \"OTHER_INNER_CONST\"\n\n"
		   "enum Enum:\n"
		   "\tA = 10\n"
		   "const CONST = \"OTHER_CONST\"\n";
}

BSParser::ExpressionNode *constant_initializer(BSParser &p_parser, const StringName &p_name) {
	CHECK(p_parser.get_tree()->has_member(p_name));
	if (!p_parser.get_tree()->has_member(p_name)) {
		return nullptr;
	}
	const auto member = p_parser.get_tree()->get_member(p_name);
	CHECK(member.type == BSParser::ClassNode::Member::CONSTANT);
	return member.type == BSParser::ClassNode::Member::CONSTANT ? member.constant->initializer : nullptr;
}

void check_enum_alias(const BSParser::ExpressionNode *p_alias, const StringName &p_first, int64_t p_first_value, const StringName &p_second, int64_t p_second_value) {
	BS_TEST_REQUIRE(p_alias != nullptr);
	CHECK(p_alias->is_constant);
	CHECK_FALSE(BSAnalyzer::has_materialized_constant_value(p_alias));
	const auto type = p_alias->get_datatype();
	CHECK(type.kind == BSParser::DataType::ENUM);
	CHECK(type.is_meta_type);
	CHECK(type.enum_values.has(p_first));
	CHECK(type.enum_values.has(p_second));
	if (type.enum_values.has(p_first)) {
		CHECK(type.enum_values[p_first] == p_first_value);
	}
	if (type.enum_values.has(p_second)) {
		CHECK(type.enum_values[p_second] == p_second_value);
	}
}

} // namespace

TEST_SUITE("external_enum_constant_analyzer") {
	TEST_CASE("exact_external_enum_as_constant_packet_is_static") {
		StorageFixture storage;
		install(storage, path("external_enum_as_constant_external.notest"), "enum MyEnum:\n\tWAITING = 0\n\tGODOT = WAITING + 1\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(exact_external_source(), path("external_enum_as_constant"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		MESSAGE(std::string(error_block(parser).utf8().get_data()));
		no_errors(parser);
		check_enum_alias(constant_initializer(parser, "MyEnum"), "WAITING", 0, "GODOT", 1);
	}

	TEST_CASE("exact_local_const_as_type_packet_reduces_only_to_the_independent_OV_boundary") {
		StorageFixture storage;
		install(storage, path("local_const_as_type.notest"), exact_local_helper());
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(exact_local_source(), path("local_const_as_type"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		MESSAGE(std::string(error_block(parser).utf8().get_data()));
		CHECK(error_block(parser) == ">> ERROR at line 33: Local constant \"OV\" is not a valid type.");
	}

	TEST_CASE("local_and_external_nested_enum_aliases_preserve_identity_and_values") {
		StorageFixture storage;
		install(storage, path("enum_alias_provider.notest"),
				"class Inner:\n\tenum State:\n\t\tFIRST = 10\n\t\tSECOND = FIRST + 1\n"
				"enum Top:\n\tFIRST = 20\n\tSECOND = FIRST + 1\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(
								"class Local:\n\tenum State:\n\t\tFIRST = 30\n\t\tSECOND = FIRST + 1\n"
								"const P = preload(\"enum_alias_provider.notest.barista\")\n"
								"const LocalAlias = Local.State\n"
								"const NestedAlias = P.Inner.State\n"
								"const TopAlias = P.Top\n"
								"const LocalValue = LocalAlias.SECOND\n"
								"const NestedValue = NestedAlias.SECOND\n"
								"const TopValue = TopAlias.SECOND\n",
								path("enum_alias_consumer"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		no_errors(parser);
		check_enum_alias(constant_initializer(parser, "LocalAlias"), "FIRST", 30, "SECOND", 31);
		check_enum_alias(constant_initializer(parser, "NestedAlias"), "FIRST", 10, "SECOND", 11);
		check_enum_alias(constant_initializer(parser, "TopAlias"), "FIRST", 20, "SECOND", 21);
		CHECK(constant_initializer(parser, "LocalValue")->reduced_value == Variant(31));
		CHECK(constant_initializer(parser, "NestedValue")->reduced_value == Variant(11));
		CHECK(constant_initializer(parser, "TopValue")->reduced_value == Variant(21));
		CHECK(constant_initializer(parser, "NestedAlias")->get_datatype().class_type != constant_initializer(parser, "TopAlias")->get_datatype().class_type);
	}

	TEST_CASE("cached_and_refreshed_providers_keep_generation_specific_enum_aliases") {
		StorageFixture storage;
		const String provider_path = path("enum_refresh_provider.notest");
		const String consumer_source = "const P = preload(\"enum_refresh_provider.notest.barista\")\nconst Alias = P.State\nconst Value = Alias.NEXT\n";
		install(storage, provider_path, "enum State:\n\tFIRST = 1\n\tNEXT = FIRST + 1\n");
		const uint64_t old_generation = storage.index().get_refresh_revision(provider_path);

		BSParser old_consumer;
		BS_TEST_REQUIRE(old_consumer.parse(consumer_source, path("enum_refresh_old"), false) == OK);
		BSAnalyzer old_analyzer(&old_consumer);
		CHECK(old_analyzer.analyze() == OK);
		no_errors(old_consumer);
		check_enum_alias(constant_initializer(old_consumer, "Alias"), "FIRST", 1, "NEXT", 2);
		const Ref<BSParserRef> retained = old_consumer.get_depended_parsers()[provider_path];
		BS_TEST_REQUIRE(retained.is_valid());

		const String refreshed_source = "enum State:\n\tFIRST = 40\n\tNEXT = FIRST + 1\n";
		BSCache::set_source_override(provider_path, refreshed_source);
		BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(provider_path, refreshed_source) == OK);
		CHECK(storage.index().get_refresh_revision(provider_path) > old_generation);

		BSParser fresh_consumer;
		BS_TEST_REQUIRE(fresh_consumer.parse(consumer_source, path("enum_refresh_fresh"), false) == OK);
		BSAnalyzer fresh_analyzer(&fresh_consumer);
		CHECK(fresh_analyzer.analyze() == OK);
		no_errors(fresh_consumer);
		check_enum_alias(constant_initializer(fresh_consumer, "Alias"), "FIRST", 40, "NEXT", 41);
		CHECK(constant_initializer(old_consumer, "Value")->reduced_value == Variant(2));
		CHECK(constant_initializer(fresh_consumer, "Value")->reduced_value == Variant(41));
		CHECK(fresh_consumer.get_depended_parsers()[provider_path] != retained);
	}

	TEST_CASE("true_member_misses_and_failed_providers_do_not_materialize_enum_aliases") {
		StorageFixture storage;
		install(storage, path("enum_miss_provider.notest"), "enum State:\n\tREADY = 1\n");
		BSParser missing;
		BS_TEST_REQUIRE(missing.parse("const P = preload(\"enum_miss_provider.notest.barista\")\nconst Alias = P.Missing\n", path("enum_true_miss"), false) == OK);
		BSAnalyzer missing_analyzer(&missing);
		CHECK(missing_analyzer.analyze() != OK);
		CHECK_FALSE(constant_initializer(missing, "Alias")->is_constant);
		CHECK(error_block(missing).contains("Cannot find member \"Missing\""));

		install(storage, path("enum_failed_provider.notest"), "extends MissingBase\nenum State:\n\tREADY = 1\n");
		BSParser failed;
		BS_TEST_REQUIRE(failed.parse("const P = preload(\"enum_failed_provider.notest.barista\")\nconst Alias = P.State\n", path("enum_failed_consumer"), false) == OK);
		BSAnalyzer failed_analyzer(&failed);
		CHECK(failed_analyzer.analyze() != OK);
		CHECK_FALSE(constant_initializer(failed, "Alias")->is_constant);
		CHECK(error_block(failed).contains("Could not preload resource script"));
	}
}
