/**************************************************************************/
/*  numeric_consumer_analyzer_tests.cpp                                    */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                         */
/*  This file is part of BaristaScript, a Godot GDExtension.               */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_conformance_registry.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <string>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
// Keep the production warning profile isolated, including previously absent settings.
struct WarningProfile {
	Dictionary saved;
	void set(const String &key, const Variant &value) {
		auto *settings = ProjectSettings::get_singleton();
		if (!saved.has(key))
			saved[key] = settings->has_setting(key) ? settings->get_setting(key) : Variant();
		settings->set_setting(key, value);
	}
	WarningProfile() {
		set("debug/barista_script/warnings/enable", true);
		for (int i = 0; i < BSWarning::WARNING_MAX; ++i)
			set(BSWarning::get_setting_path_from_code(BSWarning::Code(i)), BSWarning::IGNORE);
		level(BSWarning::INT_AS_ENUM_WITHOUT_CAST, BSWarning::WARN);
		level(BSWarning::INTEGER_DIVISION, BSWarning::WARN);
	}
	void level(BSWarning::Code code, BSWarning::WarnLevel value) {
		set(BSWarning::get_setting_path_from_code(code), value);
		BSParser::update_project_settings();
	}
	~WarningProfile() {
		const Array keys = saved.keys();
		for (int i = 0; i < keys.size(); ++i)
			ProjectSettings::get_singleton()->set_setting(keys[i], saved[keys[i]]);
		BSParser::update_project_settings();
		// Resolving defaults declares absent settings; preserve their original absence too.
		for (int i = 0; i < keys.size(); ++i)
			if (Variant(saved[keys[i]]).get_type() == Variant::NIL && ProjectSettings::get_singleton()->has_setting(keys[i]))
				ProjectSettings::get_singleton()->clear(keys[i]);
	}
};
void diagnostics(const BSParser &parser) {
	for (const auto &e : parser.get_errors())
		MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column);
	for (const auto &w : parser.get_warnings())
		MESSAGE(std::string(w.get_message().utf8().get_data()), " at ", w.start_line, ":", w.start_column, "-", w.end_line, ":", w.end_column);
}
void warning(const BSParser &parser, int index, BSWarning::Code code, const char *message, int line, int column, int end_column) {
	BS_TEST_REQUIRE(index < parser.get_warnings().size());
	auto *item = parser.get_warnings().front();
	for (int i = 0; i < index; ++i)
		item = item->next();
	const auto &w = item->get();
	CHECK(w.code == code);
	CHECK(w.get_message() == message);
	CHECK(w.start_line == line);
	CHECK(w.start_column == column);
	CHECK(w.end_line == line);
	CHECK(w.end_column == end_column);
}
constexpr const char *ENUM_WARNING = "Integer used when an enum value is expected. If this is intended, cast the integer to the enum type using the \"as\" keyword.";
const BSParser::VariableNode *local(const BSParser &parser, const StringName &name) {
	auto member = parser.get_tree()->get_member("test");
	if (member.type != BSParser::ClassNode::Member::FUNCTION || !member.function || !member.function->body)
		return nullptr;
	for (auto *statement : member.function->body->statements) {
		if (statement && statement->type == BSParser::Node::VARIABLE) {
			const auto *v = static_cast<const BSParser::VariableNode *>(statement);
			if (v->identifier && v->identifier->name == name)
				return v;
		}
	}
	return nullptr;
}
} //namespace

TEST_SUITE("numeric_consumer_analyzer") {
	TEST_CASE("original_enum_integer_consumers_preserve_all_four_warning_spans") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		WarningProfile profile;
		// Byte-faithful c9d5e35 parser/warnings/enum_assign_int_without_casting.fs.
		const String path = "res://tests/corpus/parser/warnings/enum_assign_int_without_casting.barista";
		const String source = FileAccess::get_file_as_string(path);
		BS_TEST_REQUIRE(!source.is_empty());
		for (int repeat = 0; repeat < 2; ++repeat) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			CHECK(parser.get_errors().is_empty());
			CHECK(parser.get_warnings().size() == 4);
			// Pin fs_analyzer.cpp:18400 reports at the supplied initializer/RHS expression.
			warning(parser, 0, BSWarning::INT_AS_ENUM_WITHOUT_CAST, ENUM_WARNING, 7, 25, 26);
			warning(parser, 1, BSWarning::INT_AS_ENUM_WITHOUT_CAST, ENUM_WARNING, 11, 17, 18);
			warning(parser, 2, BSWarning::INT_AS_ENUM_WITHOUT_CAST, ENUM_WARNING, 14, 29, 30);
			warning(parser, 3, BSWarning::INT_AS_ENUM_WITHOUT_CAST, ENUM_WARNING, 16, 17, 18);
		}
	}
	TEST_CASE("original_integer_division_keeps_warning_and_downgrades_weak_destination") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		WarningProfile profile;
		const String path = "res://tests/corpus/parser/warnings/integer_division.barista";
		const String source = FileAccess::get_file_as_string(path);
		BS_TEST_REQUIRE(!source.is_empty());
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().size() == 1);
		warning(parser, 0, BSWarning::INTEGER_DIVISION, "Integer division. Decimal part will be discarded.", 3, 14, 19);
		const auto *variable = local(parser, "__");
		BS_TEST_REQUIRE(variable != nullptr);
		CHECK(variable->get_datatype().is_variant());
		CHECK(variable->get_datatype().type_source == BSParser::DataType::UNDETECTED);
	}
	TEST_CASE("ordinary_enum_call_and_return_report_at_pinned_consumers") {
		StorageFixture storage;
		WarningProfile profile;
		const String source = "enum E:\n\tA = 0\nfunc take(_e: E):\n\tpass\nfunc give(v: int) -> E:\n\treturn v\nfunc test(v: int):\n\ttake(v)\n\ttake(0)\n";
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, "res://tests/numeric_call.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().size() == 2);
		warning(parser, 0, BSWarning::INT_AS_ENUM_WITHOUT_CAST, ENUM_WARNING, 6, 5, 13);
		// Pin call_validation:1119 passes no warning origin for nonconstant arguments.
		warning(parser, 1, BSWarning::INT_AS_ENUM_WITHOUT_CAST, ENUM_WARNING, 9, 10, 11);
	}
	TEST_CASE("explicit_enum_casts_and_named_values_do_not_warn") {
		StorageFixture storage;
		WarningProfile profile;
		const String source = "enum E:\n\tA = 0\nfunc give() -> E:\n\treturn E.A\nfunc test():\n\tvar value: E = E.A\n\tvalue = 0 as E\n\tvalue = give()\n\tvar handle = E\n\tprint(handle)\n";
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, "res://tests/numeric_explicit.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("weak_assignment_changes_carrier_but_hard_and_strong_destinations_refuse_fractional_values") {
		StorageFixture storage;
		WarningProfile profile;
		for (const char *declaration : { "var value = 1", "var value := 1", "var value: int = 1" }) {
			const bool weak = String(declaration) == "var value = 1";
			BSParser parser;
			const String source = String("func test():\n\t") + declaration + "\n\tvalue = 12.345\n\tvar after = value\n";
			BS_TEST_REQUIRE(parser.parse(source, "res://tests/numeric_weak.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == weak);
			diagnostics(parser);
			CHECK(parser.get_warnings().is_empty());
			if (weak) {
				CHECK(parser.get_errors().is_empty());
				const auto *value = local(parser, "value"), *after = local(parser, "after");
				BS_TEST_REQUIRE(value && after);
				CHECK(value->get_datatype().is_variant());
				CHECK(after->get_datatype().is_variant());
			} else {
				BS_TEST_REQUIRE(parser.get_errors().size() == 1);
				const auto &e = parser.get_errors().front()->get();
				CHECK(e.message == "Value of type \"float\" cannot be assigned to a variable of type \"int\".");
				CHECK(e.line == 3);
				CHECK(e.column == 13);
			}
		}
	}
	TEST_CASE("member_nonconstant_initializer_uses_the_same_enum_warning_boundary") {
		StorageFixture storage;
		WarningProfile profile;
		const String source = "enum E:\n\tA = 0\nfunc produce() -> int:\n\treturn 0\nvar member: E = produce()\nfunc test():\n\tprint(member)\n";
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, "res://tests/numeric_member.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().size() == 1);
		warning(parser, 0, BSWarning::INT_AS_ENUM_WITHOUT_CAST, ENUM_WARNING, 5, 17, 26);
	}
	TEST_CASE("enum_warning_severity_suppression_and_settings_restore") {
		StorageFixture storage;
		WarningProfile original;
		auto *settings = ProjectSettings::get_singleton();
		const String path = BSWarning::get_setting_path_from_code(BSWarning::INT_AS_ENUM_WITHOUT_CAST);
		const String source = "enum E:\n\tA = 0\nvar member: E = 0\n";
		for (bool present : { false, true }) {
			original.set(path, present ? Variant(BSWarning::IGNORE) : Variant());
			for (auto level : { BSWarning::WARN, BSWarning::ERROR, BSWarning::IGNORE, BSWarning::WARN }) {
				{
					WarningProfile profile;
					profile.level(BSWarning::INT_AS_ENUM_WITHOUT_CAST, level);
					for (bool suppressed : { false, true }) {
						BSParser parser;
						const String code = suppressed ? source.replace("var member", "@warning_ignore(\"int_as_enum_without_cast\")\nvar member") : source;
						BS_TEST_REQUIRE(parser.parse(code, "res://tests/numeric_settings.barista", false) == OK);
						BSAnalyzer analyzer(&parser);
						const bool promoted = !suppressed && level == BSWarning::ERROR;
						CHECK((analyzer.analyze() == OK) == !promoted);
						CHECK(parser.get_warnings().size() == int(!suppressed && level == BSWarning::WARN));
						CHECK(parser.get_errors().size() == int(promoted));
						if (promoted && !parser.get_errors().is_empty()) {
							const auto &e = parser.get_errors().front()->get();
							CHECK(e.message == String(ENUM_WARNING) + " (Warning treated as error.)");
							CHECK(e.line == 3);
							CHECK(e.column == 17);
							CHECK(e.end_line == 3);
							CHECK(e.end_column == 18);
						}
					}
				}
				CHECK(settings->has_setting(path) == present);
				if (present)
					CHECK(int(settings->get_setting(path)) == BSWarning::IGNORE);
			}
		}
	}
	TEST_CASE("tagged_union_and_enum_metatype_refusals_never_recommend_numeric_casts") {
		StorageFixture storage;
		WarningProfile profile;
		for (const char *source : {
					 "enum E:\n\tA(value: int)\nvar rejected: E = 0\n",
					 "enum E:\n\tA = 0\nvar rejected: Type[E] = 0\n" }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, "res://tests/numeric_exclusion.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			CHECK(!parser.get_errors().is_empty());
			CHECK(parser.get_warnings().is_empty());
		}
	}
	TEST_CASE("weak_downgrade_preserves_compound_operands_and_later_flow_reads") {
		StorageFixture storage;
		WarningProfile profile;
		for (const char *source : {
					 "func test():\n\tvar value = 1\n\tvalue += 0.5\n\tvar after = value\n",
					 "func test(v: Variant):\n\tvar value = 1\n\tvalue = v\n\tvar after = value\n",
					 "func test(flag: bool):\n\tvar value = 1\n\tif flag:\n\t\tvalue = 0.5\n\tvar after = value\n" }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, "res://tests/numeric_flow.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			CHECK(parser.get_errors().is_empty());
			CHECK(parser.get_warnings().is_empty());
			const auto *value = local(parser, "value"), *after = local(parser, "after");
			BS_TEST_REQUIRE(value && after);
			CHECK(value->get_datatype().is_variant());
			CHECK(after->get_datatype().is_variant());
		}
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func test():\n\tvar value := 1\n\tvalue += \"text\"\n", "res://tests/numeric_compound.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		CHECK(!parser.get_errors().is_empty());
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("hard_fractional_call_and_annotated_store_remain_explicit_only") {
		StorageFixture storage;
		WarningProfile profile;
		for (const char *source : {
					 "func take(_v: int):\n\tpass\nfunc test():\n\ttake(12.345)\n",
					 "func test():\n\tvar hard: int = 0\n\thard = 12.345\n" }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, "res://tests/numeric_hard.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == 1);
			const auto &e = parser.get_errors().front()->get();
			const bool call = String(source).begins_with("func take");
			CHECK(e.message == (call ? "Invalid argument for \"take()\" function: argument 1 should be \"int\" but is \"float\"." : "Value of type \"float\" cannot be assigned to a variable of type \"int\"."));
			CHECK(e.line == (call ? 4 : 3));
			CHECK(e.column == (call ? 10 : 12));
			CHECK(parser.get_warnings().is_empty());
		}
	}
	TEST_CASE("variant_carried_integer_constant_retypes_once_at_enum_boundary") {
		StorageFixture storage;
		WarningProfile profile;
		const String source = "enum E:\n\tA = 0\nconst BOX: Variant = 0\nvar member: E = BOX\n";
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, "res://tests/numeric_box.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().size() == 1);
		warning(parser, 0, BSWarning::INT_AS_ENUM_WITHOUT_CAST, ENUM_WARNING, 4, 17, 20);
	}
}
