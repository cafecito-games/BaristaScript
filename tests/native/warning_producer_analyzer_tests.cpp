/**************************************************************************/
/*  warning_producer_analyzer_tests.cpp                                    */
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
		for (auto code : { BSWarning::RETURN_VALUE_DISCARDED, BSWarning::STATIC_CALLED_ON_INSTANCE, BSWarning::UNASSIGNED_VARIABLE, BSWarning::UNASSIGNED_VARIABLE_OP_ASSIGN, BSWarning::UNUSED_VARIABLE, BSWarning::UNUSED_LOCAL_CONSTANT, BSWarning::SHADOWED_VARIABLE, BSWarning::SHADOWED_GLOBAL_IDENTIFIER })
			level(code, BSWarning::WARN);
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
String rendered(const BSParser &parser) {
	String result;
	for (const auto &e : parser.get_errors()) {
		if (!result.is_empty())
			result += "\n";
		result += vformat(">> ERROR at line %d: %s", e.line, e.message);
	}
	for (const auto &w : parser.get_warnings()) {
		if (!result.is_empty())
			result += "\n";
		result += vformat("~~ WARNING at line %d: (%s) %s", w.start_line, w.get_name(), w.get_message());
	}
	return result;
}
} //namespace
TEST_SUITE("warning_producer_analyzer") {
	TEST_CASE("original_export_tool_button_requires_tool_mode") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		WarningProfile profile;
		const String path = "res://tests/corpus/parser/errors/export_tool_button_requires_tool_mode.barista";
		const String source = FileAccess::get_file_as_string(path);
		BS_TEST_REQUIRE(!source.is_empty());
		for (int repeat = 0; repeat < 2; ++repeat) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == false);
			diagnostics(parser);
			CHECK(rendered(parser) == R"EXPECTED(>> ERROR at line 1: Tool buttons can only be used in tool scripts (add "@tool" to the top of the script).)EXPECTED");
			BS_TEST_REQUIRE(parser.get_errors().size() == 1);
			CHECK(parser.get_errors().front()->get().column == 1);
			CHECK(parser.get_warnings().is_empty());
		}
	}
	TEST_CASE("original_return_value_discarded_builtin_and_utility") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		WarningProfile profile;
		const String path = "res://tests/corpus/parser/warnings/return_value_discarded_builtin_and_utility.barista";
		const String source = FileAccess::get_file_as_string(path);
		BS_TEST_REQUIRE(!source.is_empty());
		for (int repeat = 0; repeat < 2; ++repeat) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == true);
			diagnostics(parser);
			CHECK(rendered(parser) == R"EXPECTED(~~ WARNING at line 2: (RETURN_VALUE_DISCARDED) The function "Vector2()" returns a value that will be discarded if not used.
~~ WARNING at line 3: (RETURN_VALUE_DISCARDED) The function "len()" returns a value that will be discarded if not used.
~~ WARNING at line 4: (RETURN_VALUE_DISCARDED) The function "sin()" returns a value that will be discarded if not used.)EXPECTED");
			warning(parser, 0, BSWarning::RETURN_VALUE_DISCARDED, "The function \"Vector2()\" returns a value that will be discarded if not used.", 2, 5, 18);
			warning(parser, 1, BSWarning::RETURN_VALUE_DISCARDED, "The function \"len()\" returns a value that will be discarded if not used.", 3, 5, 15);
			warning(parser, 2, BSWarning::RETURN_VALUE_DISCARDED, "The function \"sin()\" returns a value that will be discarded if not used.", 4, 5, 13);
		}
	}
	TEST_CASE("original_shadowed_constant") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		WarningProfile profile;
		const String path = "res://tests/corpus/parser/warnings/shadowed_constant.barista";
		const String source = FileAccess::get_file_as_string(path);
		BS_TEST_REQUIRE(!source.is_empty());
		for (int repeat = 0; repeat < 2; ++repeat) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == true);
			diagnostics(parser);
			CHECK(rendered(parser) == R"EXPECTED(~~ WARNING at line 8: (UNUSED_LOCAL_CONSTANT) The local constant "TEST" is declared but never used in the block. If this is intended, prefix it with an underscore: "_TEST".
~~ WARNING at line 8: (SHADOWED_VARIABLE) The local constant "TEST" is shadowing an already-declared constant at line 2 in the current class.)EXPECTED");
			warning(parser, 0, BSWarning::UNUSED_LOCAL_CONSTANT, "The local constant \"TEST\" is declared but never used in the block. If this is intended, prefix it with an underscore: \"_TEST\".", 8, 5, 20);
			warning(parser, 1, BSWarning::SHADOWED_VARIABLE, "The local constant \"TEST\" is shadowing an already-declared constant at line 2 in the current class.", 8, 11, 15);
		}
	}
	TEST_CASE("original_shadowed_global_identifier") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		WarningProfile profile;
		const String path = "res://tests/corpus/parser/warnings/shadowed_global_identifier.barista";
		const String source = FileAccess::get_file_as_string(path);
		BS_TEST_REQUIRE(!source.is_empty());
		for (int repeat = 0; repeat < 2; ++repeat) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == true);
			diagnostics(parser);
			CHECK(rendered(parser) == R"EXPECTED(~~ WARNING at line 2: (UNUSED_VARIABLE) The local variable "abs" is declared but never used in the block. If this is intended, prefix it with an underscore: "_abs".
~~ WARNING at line 2: (SHADOWED_GLOBAL_IDENTIFIER) The variable "abs" has the same name as a built-in function.)EXPECTED");
			warning(parser, 0, BSWarning::UNUSED_VARIABLE, "The local variable \"abs\" is declared but never used in the block. If this is intended, prefix it with an underscore: \"_abs\".", 2, 5, 74);
			warning(parser, 1, BSWarning::SHADOWED_GLOBAL_IDENTIFIER, "The variable \"abs\" has the same name as a built-in function.", 2, 9, 12);
		}
	}
	TEST_CASE("original_shadowed_variable_class") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		WarningProfile profile;
		const String path = "res://tests/corpus/parser/warnings/shadowed_variable_class.barista";
		const String source = FileAccess::get_file_as_string(path);
		BS_TEST_REQUIRE(!source.is_empty());
		for (int repeat = 0; repeat < 2; ++repeat) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == true);
			diagnostics(parser);
			CHECK(rendered(parser) == R"EXPECTED(~~ WARNING at line 8: (UNUSED_VARIABLE) The local variable "foo" is declared but never used in the block. If this is intended, prefix it with an underscore: "_foo".
~~ WARNING at line 8: (SHADOWED_VARIABLE) The local variable "foo" is shadowing an already-declared variable at line 1 in the current class.)EXPECTED");
			warning(parser, 0, BSWarning::UNUSED_VARIABLE, "The local variable \"foo\" is declared but never used in the block. If this is intended, prefix it with an underscore: \"_foo\".", 8, 5, 18);
			warning(parser, 1, BSWarning::SHADOWED_VARIABLE, "The local variable \"foo\" is shadowing an already-declared variable at line 1 in the current class.", 8, 9, 12);
		}
	}
	TEST_CASE("original_shadowed_variable_function") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		WarningProfile profile;
		const String path = "res://tests/corpus/parser/warnings/shadowed_variable_function.barista";
		const String source = FileAccess::get_file_as_string(path);
		BS_TEST_REQUIRE(!source.is_empty());
		for (int repeat = 0; repeat < 2; ++repeat) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == true);
			diagnostics(parser);
			CHECK(rendered(parser) == R"EXPECTED(~~ WARNING at line 2: (UNUSED_VARIABLE) The local variable "test" is declared but never used in the block. If this is intended, prefix it with an underscore: "_test".
~~ WARNING at line 2: (SHADOWED_VARIABLE) The local variable "test" is shadowing an already-declared function at line 1 in the current class.)EXPECTED");
			warning(parser, 0, BSWarning::UNUSED_VARIABLE, "The local variable \"test\" is declared but never used in the block. If this is intended, prefix it with an underscore: \"_test\".", 2, 5, 73);
			warning(parser, 1, BSWarning::SHADOWED_VARIABLE, "The local variable \"test\" is shadowing an already-declared function at line 1 in the current class.", 2, 9, 13);
		}
	}
	TEST_CASE("original_static_called_on_instance") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		WarningProfile profile;
		const String path = "res://tests/corpus/parser/warnings/static_called_on_instance.barista";
		const String source = FileAccess::get_file_as_string(path);
		BS_TEST_REQUIRE(!source.is_empty());
		for (int repeat = 0; repeat < 2; ++repeat) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == true);
			diagnostics(parser);
			CHECK(rendered(parser) == R"EXPECTED(~~ WARNING at line 13: (STATIC_CALLED_ON_INSTANCE) The function "num_uint64()" is a static function but was called from an instance. Instead, it should be directly called from the type: "String.num_uint64()".
~~ WARNING at line 19: (STATIC_CALLED_ON_INSTANCE) The function "static_func()" is a static function but was called from an instance. Instead, it should be directly called from the type: "TestStaticCalledOnInstance.static_func()".
~~ WARNING at line 23: (STATIC_CALLED_ON_INSTANCE) The function "static_func()" is a static function but was called from an instance. Instead, it should be directly called from the type: "Inner.static_func()".)EXPECTED");
			warning(parser, 0, BSWarning::STATIC_CALLED_ON_INSTANCE, "The function \"num_uint64()\" is a static function but was called from an instance. Instead, it should be directly called from the type: \"String.num_uint64()\".", 13, 11, 45);
			warning(parser, 1, BSWarning::STATIC_CALLED_ON_INSTANCE, "The function \"static_func()\" is a static function but was called from an instance. Instead, it should be directly called from the type: \"TestStaticCalledOnInstance.static_func()\".", 19, 5, 24);
			warning(parser, 2, BSWarning::STATIC_CALLED_ON_INSTANCE, "The function \"static_func()\" is a static function but was called from an instance. Instead, it should be directly called from the type: \"Inner.static_func()\".", 23, 5, 24);
		}
	}
	TEST_CASE("original_unassigned_variable") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		WarningProfile profile;
		const String path = "res://tests/corpus/parser/warnings/unassigned_variable.barista";
		const String source = FileAccess::get_file_as_string(path);
		BS_TEST_REQUIRE(!source.is_empty());
		for (int repeat = 0; repeat < 2; ++repeat) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == true);
			diagnostics(parser);
			CHECK(rendered(parser) == R"EXPECTED(~~ WARNING at line 3: (UNASSIGNED_VARIABLE) The variable "unassigned" is used before being assigned a value.
~~ WARNING at line 7: (UNASSIGNED_VARIABLE) The variable "a" is used before being assigned a value.
~~ WARNING at line 8: (UNASSIGNED_VARIABLE) The variable "a" is used before being assigned a value.)EXPECTED");
			warning(parser, 0, BSWarning::UNASSIGNED_VARIABLE, "The variable \"unassigned\" is used before being assigned a value.", 3, 11, 21);
			warning(parser, 1, BSWarning::UNASSIGNED_VARIABLE, "The variable \"a\" is used before being assigned a value.", 7, 11, 12);
			warning(parser, 2, BSWarning::UNASSIGNED_VARIABLE, "The variable \"a\" is used before being assigned a value.", 8, 8, 9);
		}
	}
	TEST_CASE("original_unassigned_variable_op_assign") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		WarningProfile profile;
		const String path = "res://tests/corpus/parser/warnings/unassigned_variable_op_assign.barista";
		const String source = FileAccess::get_file_as_string(path);
		BS_TEST_REQUIRE(!source.is_empty());
		for (int repeat = 0; repeat < 2; ++repeat) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == true);
			diagnostics(parser);
			CHECK(rendered(parser) == R"EXPECTED(~~ WARNING at line 4: (UNASSIGNED_VARIABLE_OP_ASSIGN) The variable "__" is modified with the compound-assignment operator "+=" but was not previously initialized.)EXPECTED");
			warning(parser, 0, BSWarning::UNASSIGNED_VARIABLE_OP_ASSIGN, "The variable \"__\" is modified with the compound-assignment operator \"+=\" but was not previously initialized.", 4, 5, 13);
		}
	}
	TEST_CASE("helper_blocked_user_call_producer_has_direct_same_file_control") {
		StorageFixture storage;
		WarningProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func value() -> int:\n\treturn 1\nfunc test():\n\tvalue()\n", "res://tests/direct_user.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().size() == 1);
		warning(parser, 0, BSWarning::RETURN_VALUE_DISCARDED, "The function \"value()\" returns a value that will be discarded if not used.", 4, 5, 12);
	}
	TEST_CASE("helper_blocked_standalone_constructor_utility_producers_have_direct_control") {
		StorageFixture storage;
		WarningProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func test():\n\tColor(1, 1, 1)\n\tfloat(125)\n\tabsi(4)\n", "res://tests/direct_standalone.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().size() == 3);
		warning(parser, 0, BSWarning::RETURN_VALUE_DISCARDED, "The function \"Color()\" returns a value that will be discarded if not used.", 2, 5, 19);
		warning(parser, 1, BSWarning::RETURN_VALUE_DISCARDED, "The function \"float()\" returns a value that will be discarded if not used.", 3, 5, 15);
		warning(parser, 2, BSWarning::RETURN_VALUE_DISCARDED, "The function \"absi()\" returns a value that will be discarded if not used.", 4, 5, 12);
	}

	TEST_CASE("discarded_calls_distinguish_consumed_void_variant_await_coroutine_and_preload") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		WarningProfile profile;
		profile.level(BSWarning::UNUSED_VARIABLE, BSWarning::IGNORE);
		profile.level(BSWarning::MISSING_AWAIT, BSWarning::WARN);
		const String source = "func value() -> int:\n\treturn 1\nfunc nothing() -> void:\n\tpass\nfunc dynamic() -> Variant:\n\treturn 1\nasync func task() -> int:\n\treturn 1\nasync func launch() -> void:\n\tpass\nfunc test():\n\tvar held = value()\n\tprint(value())\n\tnothing()\n\tdynamic()\n\tawait task()\n\tlaunch()\n\ttask()\n\tpreload(\"res://example.barista\")\n";
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, "res://tests/discard_neighbors.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().size() == 2);
		warning(parser, 0, BSWarning::MISSING_AWAIT, "The call returns a \"Coroutine[int]\" whose result is discarded. Use \"await\", or store or pass the handle if it is awaited elsewhere.", 18, 5, 11);
		warning(parser, 1, BSWarning::RETURN_VALUE_DISCARDED, "The function \"preload()\" returns a value that will be discarded if not used.", 19, 5, 37);
	}
	TEST_CASE("shadowing_uses_declaring_class_and_global_priority_even_when_local_is_used") {
		StorageFixture storage;
		WarningProfile profile;
		const String source = "var abs = 1\nvar item = 1\nclass Inner:\n\tconst item = 2\n\tfunc test():\n\t\tvar item = 3\n\t\tprint(item)\nfunc test():\n\tvar abs = 2\n\tprint(abs)\n\titem = 3\n\tvar distinct = 1\n\tvar capture = func():\n\t\tprint(distinct)\n\tprint(capture)\n";
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, "res://tests/shadow_neighbors.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().size() == 2);
		warning(parser, 0, BSWarning::SHADOWED_VARIABLE, "The local variable \"item\" is shadowing an already-declared constant at line 4 in the current class.", 6, 13, 17);
		warning(parser, 1, BSWarning::SHADOWED_GLOBAL_IDENTIFIER, "The variable \"abs\" has the same name as a built-in function.", 9, 9, 12);
	}
	TEST_CASE("unassigned_scalar_counter_keeps_defaults_captures_and_compound_rhs_order") {
		StorageFixture storage;
		WarningProfile profile;
		profile.level(BSWarning::UNUSED_VARIABLE, BSWarning::IGNORE);
		const String source = "func test(parameter: int):\n\tvar hard: int\n\tprint(hard)\n\tvar initialized = 1\n\tinitialized += 1\n\tparameter += 1\n\tvar captured\n\tvar closure = func():\n\t\tprint(captured)\n\tcaptured = 1\n\tprint(captured)\n\tvar rhs\n\tvar target: int\n\ttarget += rhs\n\ttarget += 1\n\tprint(closure)\n";
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, "res://tests/unassigned_neighbors.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().size() == 3);
		warning(parser, 0, BSWarning::UNASSIGNED_VARIABLE, "The variable \"captured\" is used before being assigned a value.", 9, 15, 23);
		warning(parser, 1, BSWarning::UNASSIGNED_VARIABLE, "The variable \"rhs\" is used before being assigned a value.", 14, 15, 18);
		warning(parser, 2, BSWarning::UNASSIGNED_VARIABLE_OP_ASSIGN, "The variable \"target\" is modified with the compound-assignment operator \"+=\" but was not previously initialized.", 14, 5, 18);
	}
	TEST_CASE("each_retained_warning_honors_ignore_warn_error_and_annotation_suppression") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		struct Consumer {
			BSWarning::Code code;
			const char *prefix;
			const char *statement;
			const char *message;
			int column;
			int end;
		};
		const Consumer cases[] = {
			{ BSWarning::RETURN_VALUE_DISCARDED, "", "sin(1.0)", "The function \"sin()\" returns a value that will be discarded if not used.", 5, 13 },
			{ BSWarning::STATIC_CALLED_ON_INSTANCE, "", "\"\".num_uint64(1)", "The function \"num_uint64()\" is a static function but was called from an instance. Instead, it should be directly called from the type: \"String.num_uint64()\".", 5, 21 },
			{ BSWarning::UNASSIGNED_VARIABLE, "\tvar value\n", "print(value)", "The variable \"value\" is used before being assigned a value.", 11, 16 },
			{ BSWarning::UNASSIGNED_VARIABLE_OP_ASSIGN, "\tvar value: int\n", "value += 1", "The variable \"value\" is modified with the compound-assignment operator \"+=\" but was not previously initialized.", 5, 15 },
			{ BSWarning::SHADOWED_GLOBAL_IDENTIFIER, "", "var abs = 1", "The variable \"abs\" has the same name as a built-in function.", 9, 12 },
			{ BSWarning::SHADOWED_VARIABLE, "", "var test = 1", "The local variable \"test\" is shadowing an already-declared function at line 1 in the current class.", 9, 13 },
		};
		for (const auto &c : cases) {
			for (int mode : { 0, 1, 2, 3, 1 }) {
				WarningProfile profile;
				for (int code = 0; code < BSWarning::WARNING_MAX; ++code)
					profile.level(BSWarning::Code(code), BSWarning::IGNORE);
				profile.level(c.code, mode == 0 ? BSWarning::IGNORE : mode == 2 ? BSWarning::ERROR
																				: BSWarning::WARN);
				String source = String("func test():\n") + c.prefix;
				if (mode == 3)
					source += String("\t@warning_ignore(\"") + BSWarning::get_name_from_code(c.code).to_lower() + "\")\n";
				source += String("\t") + c.statement + "\n";
				BSParser parser;
				if (parser.parse(source, "res://tests/severity.barista", false) != OK) {
					CHECK(false);
					diagnostics(parser);
					continue;
				}
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() == OK) == (mode != 2));
				diagnostics(parser);
				const int line = String(c.prefix).is_empty() ? 2 : 3;
				if (mode == 1) {
					CHECK(parser.get_errors().is_empty());
					CHECK(parser.get_warnings().size() == 1);
					warning(parser, 0, c.code, c.message, line, c.column, c.end);
				} else if (mode == 2) {
					CHECK(parser.get_errors().size() == 1);
					CHECK(parser.get_warnings().is_empty());
					if (parser.get_errors().size() == 1) {
						const auto &e = parser.get_errors().front()->get();
						CHECK(e.message == String(c.message) + " (Warning treated as error.)");
						CHECK(e.line == line);
						CHECK(e.column == c.column);
					}
				} else {
					CHECK(parser.get_errors().is_empty());
					CHECK(parser.get_warnings().is_empty());
				}
			}
		}
	}
	TEST_CASE("tool_button_static_validation_preserves_adjacent_admission_rules") {
		StorageFixture storage;
		WarningProfile profile;
		struct Control {
			const char *source;
			const char *message;
			int line;
		};
		const Control cases[] = {
			{ "@tool\n@export_tool_button(\"Run\") var action: Callable\n", "", 0 },
			{ "@tool\n@export_tool_button(\"Run\") static var action: Callable\n", "Annotation \"@export_tool_button\" cannot be applied to a static variable.", 2 },
			{ "@tool\n@export\n@export_tool_button(\"Run\") var action: Callable\n", "Annotation \"@export_tool_button\" cannot be used with another \"@export\" annotation.", 3 },
			{ "@tool\n@export_tool_button(\"Run\") var action: int\n", "\"@export_tool_button\" annotation requires a variable of type \"Callable\", but type \"int\" was given instead.", 2 },
			{ "@tool\n@export_tool_button(3) var action: Callable\n", "Invalid argument for annotation \"@export_tool_button\": argument 1 should be \"String\" but is \"int\".", 2 },
		};
		for (const auto &c : cases) {
			BSParser parser;
			if (parser.parse(c.source, "res://tests/tool_button.barista", false) != OK) {
				CHECK(false);
				diagnostics(parser);
				continue;
			}
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == (c.line == 0));
			diagnostics(parser);
			CHECK(parser.get_warnings().is_empty());
			CHECK(parser.get_errors().size() == (c.line == 0 ? 0 : 1));
			if (parser.get_errors().size() == 1) {
				const auto &e = parser.get_errors().front()->get();
				CHECK(e.message == c.message);
				CHECK(e.line == c.line);
				CHECK(e.column == (String(c.source).contains("button(3)") ? 21 : 1));
			}
		}
	}
}
