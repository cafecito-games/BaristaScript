/**************************************************************************/
/*  expression_consumer_analyzer_tests.cpp                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_analyzer_probe.h"
#include "bs_conformance_registry.h"
#include "bs_core_constants.h"
#include "bs_global_class.h"
#include "bs_native_db.h"
#include "bs_type.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <string>
#include <utility>
using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;
namespace {
struct TypeProfile {
	Dictionary saved;
	void set(const String &key, const Variant &value) {
		auto *settings = ProjectSettings::get_singleton();
		if (!saved.has(key))
			saved[key] = settings->has_setting(key) ? settings->get_setting(key) : Variant();
		settings->set_setting(key, value);
	}
	TypeProfile(bool strict_null = false) {
		set("debug/barista_script/analysis/strict_null_checks", strict_null);
		set("debug/barista_script/analysis/strict_dynamic_checks", false);
		for (int i = 0; i < BSWarning::WARNING_MAX; ++i)
			set(BSWarning::get_setting_path_from_code(BSWarning::Code(i)), BSWarning::IGNORE);
		BSParser::update_project_settings();
	}
	~TypeProfile() {
		const Array keys = saved.keys();
		for (int i = 0; i < keys.size(); ++i)
			ProjectSettings::get_singleton()->set_setting(keys[i], saved[keys[i]]);
		BSParser::update_project_settings();
		for (int i = 0; i < keys.size(); ++i)
			if (Variant(saved[keys[i]]).get_type() == Variant::NIL && ProjectSettings::get_singleton()->has_setting(keys[i]))
				ProjectSettings::get_singleton()->clear(keys[i]);
	}
};

void original(const char *name, const char *source, const char *expected) {
	StorageFixture storage;
	BSConformanceRegistry::ScopedCorpusState conformances;
	TypeProfile profile;
	profile.set(BSWarning::get_setting_path_from_code(BSWarning::UNSAFE_VOID_RETURN), BSWarning::WARN);
	BSParser::update_project_settings();
	const String path = storage.path(name);
	if (String(source).contains("HelperA")) {
		BS_TEST_REQUIRE(DirAccess::make_dir_recursive_absolute(storage.path("errors/enum_same_name_same_basename/a")) == OK);
		BS_TEST_REQUIRE(DirAccess::make_dir_recursive_absolute(storage.path("errors/enum_same_name_same_basename/b")) == OK);
		const String helper = "extends RefCounted\n\nenum Result:\n\tOK = 0\n\tFAIL = 1\n";
		BS_TEST_REQUIRE(write_bytes(storage.path("errors/enum_same_name_same_basename/a/helper.notest.barista"), bytes(helper)));
		BS_TEST_REQUIRE(write_bytes(storage.path("errors/enum_same_name_same_basename/b/helper.notest.barista"), bytes(helper)));
	}
	if (String(source).contains("Utils.")) {
		const String utils_path = "res://tests/corpus_support/parser/utils.notest.barista";
		const auto data = read_bytes(utils_path);
		BS_TEST_REQUIRE(!data.is_empty());
		const String utils = String::utf8(reinterpret_cast<const char *>(data.ptr()), data.size());
		const auto record = BSDeclarationIndex::record_from_global_class(utils_path, utils, bs_resolve_global_class_from_source(utils, utils_path));
		BS_TEST_REQUIRE(storage.index().commit_record(storage.index().claim_refresh(utils_path), record));
	}
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
	BSAnalyzer analyzer(&parser);
	const Error error = analyzer.analyze();
	String actual;
	for (const auto &e : parser.get_errors()) {
		if (!actual.is_empty())
			actual += "\n";
		actual += vformat(">> ERROR at line %d: %s", e.line, e.message);
		MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column);
	}
	if (actual.is_empty())
		for (const auto &w : parser.get_warnings()) {
			if (!actual.is_empty())
				actual += "\n";
			actual += vformat("~~ WARNING at line %d: (%s) %s", w.start_line, w.get_name(), w.get_message());
		}
	if (actual.is_empty())
		actual = "BS_TEST_OK";
	const String oracle = String(expected).replace("res://tests/corpus_staging/analyzer/", storage.root + "/");
	CHECK(std::string(actual.utf8().get_data()) == std::string(oracle.utf8().get_data()));
	CHECK((error == OK) == !oracle.begins_with(">> ERROR"));
}

void diagnostics(const BSParser &parser) {
	for (const auto &e : parser.get_errors())
		MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column);
}
void error_at(const BSParser &parser, int index, const String &message, const BSParser::Node *origin) {
	BS_TEST_REQUIRE(origin && index < parser.get_errors().size());
	auto *entry = parser.get_errors().front();
	for (int i = 0; i < index; ++i)
		entry = entry->next();
	const auto &e = entry->get();
	CHECK(e.message == message);
	CHECK(e.line == origin->start_line);
	CHECK(e.column == origin->start_column);
	CHECK(e.end_line == origin->end_line);
	CHECK(e.end_column == origin->end_column);
}
BSParser::VariableNode *local(BSParser &parser, const StringName &name) {
	auto *body = parser.get_tree()->get_member("test").function->body;
	for (auto *statement : body->statements)
		if (statement->type == BSParser::Node::VARIABLE) {
			auto *variable = static_cast<BSParser::VariableNode *>(statement);
			if (variable->identifier->name == name)
				return variable;
		}
	return nullptr;
}
void color_value(const BSParser::ExpressionNode *value, const Color &expected) {
	BS_TEST_REQUIRE(value);
	INFO("color origin ", value->start_line, ":", value->start_column);
	CHECK(BSAnalyzer::has_materialized_constant_value(value));
	CHECK(value->get_datatype().builtin_type == Variant::COLOR);
	CHECK(value->reduced_value.get_type() == Variant::COLOR);
	CHECK(Color(value->reduced_value) == expected);
}
void public_agrees(const String &source, const String &path, const BSParser &parser) {
	Ref<BaristaScriptAnalyzerProbe> probe;
	probe.instantiate();
	const Dictionary analyzed = probe->analyze_source(source, path);
	const Dictionary validated = probe->validate_source(source, path, true);
	CHECK(bool(analyzed.get("valid", false)) == parser.get_errors().is_empty());
	CHECK(bool(validated.get("valid", false)) == parser.get_errors().is_empty());
	const Array errors = validated.get("errors", Array());
	CHECK(errors.size() == parser.get_errors().size());
	for (const auto &e : parser.get_errors()) {
		bool found = false;
		for (int i = 0; i < errors.size(); ++i) {
			const Dictionary row = errors[i];
			if (String(row.get("message", "")) == e.message && int(row.get("line", -1)) == e.line && int(row.get("column", -1)) == e.column)
				found = true;
		}
		CHECK(found);
	}
	Ref<BaristaScript> script;
	script.instantiate();
	script->set_path(path);
	script->_set_source_code(source);
	CHECK(script->_is_valid() == parser.get_errors().is_empty());
}
} // namespace
TEST_SUITE("expression_consumer_analyzer") {
	TEST_CASE("original_errors_enum_same_name_same_basename_cast_enum_across_same_named_helpers") { original("errors/enum_same_name_same_basename/cast_enum_across_same_named_helpers.barista", "extends RefCounted\n\nconst HelperA = preload(\"./a/helper.notest.barista\")\nconst HelperB = preload(\"./b/helper.notest.barista\")\n\nfunc test():\n\tvar wrong := HelperA.Result as HelperB.Result\n\tprint(wrong)\n", ">> ERROR at line 7: Cannot cast a value of type \"helper.notest.barista.Result\" as \"helper.notest.barista.Result\". The value is declared in \"res://tests/corpus_staging/analyzer/errors/enum_same_name_same_basename/a/helper.notest.barista\"; the target type is declared in \"res://tests/corpus_staging/analyzer/errors/enum_same_name_same_basename/b/helper.notest.barista\"."); }
	TEST_CASE("original_features_enum_assign_other_enum_cast_to_same_enum") { original("features/enum_assign_other_enum_cast_to_same_enum.barista", "enum MyEnum:\n\tENUM_VALUE_1 = 0\n\tENUM_VALUE_2 = ENUM_VALUE_1 + 1\nenum MyOtherEnum:\n\tOTHER_ENUM_VALUE_1 = 0\n\tOTHER_ENUM_VALUE_2 = OTHER_ENUM_VALUE_1 + 1\n\nvar class_var: MyEnum = MyOtherEnum.OTHER_ENUM_VALUE_1 as MyEnum\n\nfunc test():\n\tprint(class_var)\n\tclass_var = MyOtherEnum.OTHER_ENUM_VALUE_2 as MyEnum\n\tprint(class_var)\n\n\tvar local_var: MyEnum = MyOtherEnum.OTHER_ENUM_VALUE_1 as MyEnum\n\tprint(local_var)\n\tlocal_var = MyOtherEnum.OTHER_ENUM_VALUE_2 as MyEnum\n\tprint(local_var)\n", "BS_TEST_OK"); }
	TEST_CASE("original_errors_use_value_of_void_function_custom_method") { original("errors/use_value_of_void_function_custom_method.barista", "func foo() -> void:\n\tpass\n\nfunc test():\n\tprint(foo()) # Custom method.\n", ">> ERROR at line 5: Cannot get return value of call to \"foo()\" because it returns \"void\"."); }
	TEST_CASE("original_errors_use_value_of_void_function_native_method") { original("errors/use_value_of_void_function_native_method.barista", "func test():\n\tvar obj := Node.new()\n\tprint(obj.free()) # Native type method.\n", ">> ERROR at line 3: Cannot get return value of call to \"free()\" because it returns \"void\"."); }
	TEST_CASE("original_errors_use_value_of_void_function_utility") { original("errors/use_value_of_void_function_utility.barista", "func test():\n\tprint(print()) # Built-in utility function.\n", ">> ERROR at line 2: Cannot get return value of call to \"print()\" because it returns \"void\"."); }
	TEST_CASE("original_features_allow_void_function_to_return_void") { original("features/allow_void_function_to_return_void.barista", "func test():\n\treturn_call()\n\treturn_nothing()\n\treturn_side_effect()\n\tvar r = return_side_effect.call() # Untyped call to check return value.\n\tprints(r, typeof(r) == TYPE_NIL)\n\tprint(\"end\")\n\nfunc side_effect(v):\n\tprint(\"effect\")\n\treturn v\n\nfunc return_call() -> void:\n\treturn print(\"hello\")\n\nfunc return_nothing() -> void:\n\treturn\n\nfunc return_side_effect() -> void:\n\treturn side_effect(\"x\")\n", "~~ WARNING at line 20: (UNSAFE_VOID_RETURN) The method \"return_side_effect()\" returns \"void\" but it's trying to return a call to \"side_effect()\" that can't be ensured to also be \"void\"."); }
	TEST_CASE("original_features_as") { original("features/as.barista", "func test():\n\tvar some_bool = 5 as bool\n\tvar some_int = 5 as int\n\tvar some_float = 5 as float\n\tprint(typeof(some_bool))\n\tprint(typeof(some_int))\n\tprint(typeof(some_float))\n\n\tprint()\n\n\tvar some_bool_typed := 5 as bool\n\tvar some_int_typed := 5 as int\n\tvar some_float_typed := 5 as float\n\tprint(typeof(some_bool_typed))\n\tprint(typeof(some_int_typed))\n\tprint(typeof(some_float_typed))\n", "BS_TEST_OK"); }
	TEST_CASE("original_errors_assymetric_assignment_bad") { original("errors/assymetric_assignment_bad.barista", "func test():\n\tvar var_color: String = Color.RED\n\tprint('not ok')\n", ">> ERROR at line 2: Cannot assign a value of type Color to variable \"var_color\" with specified type String."); }
	TEST_CASE("original_features_assymetric_assignment_good") { original("features/assymetric_assignment_good.barista", "const const_color: Color = 'red'\n\nfunc func_color(arg_color: Color = 'blue') -> bool:\n\treturn arg_color == Color.BLUE\n\nfunc test():\n\tUtils.check(const_color == Color.RED)\n\n\tUtils.check(func_color() == true)\n\tUtils.check(func_color('blue') == true)\n\n\tvar var_color: Color = 'green'\n\tUtils.check(var_color == Color.GREEN)\n\n\tprint('ok')\n", "BS_TEST_OK"); }
	TEST_CASE("original_errors_invalid_concatenation_dictionary") { original("errors/invalid_concatenation_dictionary.barista", "func test():\n\tprint({\"hello\": \"world\"} + {\"godot\": \"engine\"})\n", ">> ERROR at line 2: Invalid operands \"Dictionary\" and \"Dictionary\" for \"+\" operator."); }
	TEST_CASE("original_errors_invalid_concatenation_mixed") { original("errors/invalid_concatenation_mixed.barista", "func test():\n\tprint(\"hello\" + [\"world\"])\n", ">> ERROR at line 2: Invalid operands \"String\" and \"Array\" for \"+\" operator."); }
	TEST_CASE("original_errors_local_const_as_type_use_before_declared") { original("errors/local_const_as_type_use_before_declared.barista", "enum MyEnum:\n\tpass\n\nfunc test():\n\tvar e: E\n\tconst E = MyEnum\n", ">> ERROR at line 5: Local constant \"E\" is not resolved at this point."); }
	TEST_CASE("original_errors_local_const_as_type_use_not_const") { original("errors/local_const_as_type_use_not_const.barista", "enum MyEnum:\n\tpass\n\nfunc test():\n\tvar E = MyEnum\n\tvar e: E\n", ">> ERROR at line 6: Local variable \"E\" cannot be used as a type."); }
	TEST_CASE("original_errors_local_const_as_type_use_not_type") { original("errors/local_const_as_type_use_not_type.barista", "enum MyEnum:\n\tA = 0\n\nfunc test():\n\tconst E = MyEnum.A\n\tvar e: E\n", ">> ERROR at line 6: Local constant \"E\" is not a valid type."); }
}

TEST_SUITE("expression_consumer_analyzer") {
	TEST_CASE("enum_value_casts_preserve_payload_identity_and_nonconstant_values") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		const String source = "enum A:\n\tZERO = 0\nenum B:\n\tZERO = 0\nconst VALUE = A.ZERO as B\nfunc test(input: A):\n\tvar casted: B = input as B\n\tvar same: A = input as A\n";
		BS_TEST_REQUIRE(parser.parse(source, storage.path("enum_values.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		auto *value = parser.get_tree()->get_member("VALUE").constant->initializer;
		CHECK(BSAnalyzer::has_materialized_constant_value(value));
		CHECK(value->reduced_value.get_type() == Variant::INT);
		CHECK(int64_t(value->reduced_value) == 0);
		const auto expected = parser.get_tree()->get_member("B").get_datatype();
		CHECK(value->get_datatype().enum_type == expected.enum_type);
		CHECK_FALSE(value->get_datatype().is_meta_type);
		auto *casted = local(parser, "casted"), *same = local(parser, "same");
		BS_TEST_REQUIRE(casted && same);
		CHECK(casted->get_datatype().enum_type == expected.enum_type);
		CHECK_FALSE(casted->initializer->is_constant);
		CHECK(same->get_datatype().enum_type == parser.get_tree()->get_member("A").get_datatype().enum_type);
		CHECK(parser.get_warnings().is_empty());
		public_agrees(source, storage.path("enum_values.barista"), parser);
	}
	TEST_CASE("enum_handle_refusal_keeps_unmaterialized_operand_and_exact_origin") {
		for (const char *target : { "int", "E" }) {
			StorageFixture storage;
			TypeProfile profile;
			BSParser parser;
			const String source = String("enum E:\n\tZERO = 0\nfunc test():\n\tvar value = E as ") + target + "\n";
			const String path = storage.path("enum_handle.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			auto *v = local(parser, "value");
			BS_TEST_REQUIRE(v && v->initializer && v->initializer->type == BSParser::Node::CAST);
			auto *cast = static_cast<BSParser::CastNode *>(v->initializer);
			CHECK(parser.get_errors().size() == 1);
			error_at(parser, 0, vformat(R"(Cannot cast a value of type "%s" as "%s".)", cast->operand->get_datatype().to_string(), cast->get_datatype().to_string()), cast->operand);
			CHECK_FALSE(BSAnalyzer::has_materialized_constant_value(cast->operand));
			CHECK_FALSE(cast->is_constant);
			CHECK(cast->reduced_value.get_type() == Variant::NIL);
			CHECK(parser.get_warnings().is_empty());
			public_agrees(source, path, parser);
		}
	}
	TEST_CASE("integer_enum_cast_warning_is_at_operand_once") {
		StorageFixture storage;
		TypeProfile profile;
		profile.set(BSWarning::get_setting_path_from_code(BSWarning::INT_AS_ENUM_WITHOUT_MATCH), BSWarning::WARN);
		BSParser::update_project_settings();
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("enum E:\n\tZERO = 0\nfunc test():\n\tvar match = 0 as E\n\tvar miss = 7 as E\n", storage.path("enum_warning.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		CHECK(parser.get_warnings().size() == 1);
		auto *value = local(parser, "miss");
		BS_TEST_REQUIRE(value && value->initializer);
		auto *operand = static_cast<BSParser::CastNode *>(value->initializer)->operand;
		BS_TEST_REQUIRE(parser.get_warnings().size() == 1);
		const auto &w = parser.get_warnings().front()->get();
		CHECK(w.code == BSWarning::INT_AS_ENUM_WITHOUT_MATCH);
		CHECK(w.start_line == operand->start_line);
		CHECK(w.start_column == operand->start_column);
		CHECK(w.end_line == operand->end_line);
		CHECK(w.end_column == operand->end_column);
		CHECK(w.get_message() == "Cannot cast 7 as Enum \"enum_warning.barista.E\": no enum member has matching value.");
	}
	TEST_CASE("engine_utility_varargs_have_no_fixed_placeholder_and_keep_raw_native_metadata") {
		MethodInfo print, abs, free;
		BS_TEST_REQUIRE(BSCoreConstants::get_utility_function("print", print));
		CHECK((print.flags & METHOD_FLAG_VARARG) != 0);
		CHECK(print.arguments.is_empty());
		CHECK(print.return_val.type == Variant::NIL);
		CHECK((print.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == 0);
		BS_TEST_REQUIRE(BSCoreConstants::get_utility_function("abs", abs));
		CHECK((abs.flags & METHOD_FLAG_VARARG) == 0);
		CHECK(abs.arguments.size() == 1);
		BS_TEST_REQUIRE(BSNativeDB::get_method_info("Node", "free", &free));
		CHECK_FALSE(BSNativeDB::get_method_info("Node", "missing_expression_consumer_method", &free));
		CHECK(free.return_val.type == Variant::NIL);
		CHECK((free.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == 0);
	}
	TEST_CASE("void_value_calls_have_one_exact_call_origin_and_public_agreement") {
		struct Example {
			const char *prefix;
			const char *call;
			const char *name;
		};
		for (const auto &example : { Example{ "func target() -> void:\n\tpass\n", "target()", "target" }, Example{ "func target() -> void:\n\tpass\n", "self.target()", "target" }, Example{ "", "Node.new().free()", "free" }, Example{ "", "print()", "print" }, Example{ "", "[1].reverse()", "reverse" }, Example{ "signal ping\n", "ping.emit()", "emit" }, Example{ "func target() -> void:\n\tpass\nvar cb: Callable[[], void] = target\n", "cb.call()", "call" } }) {
			StorageFixture storage;
			TypeProfile profile;
			BSParser parser;
			const String source = String(example.prefix) + "func test():\n\tprint(" + example.call + ")\n";
			const String path = storage.path("void_values.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			auto *body = parser.get_tree()->get_member("test").function->body;
			BS_TEST_REQUIRE(body && body->statements.size() == 1);
			auto *outer = static_cast<BSParser::CallNode *>(body->statements[0]);
			BS_TEST_REQUIRE(outer->arguments.size() == 1);
			CHECK(parser.get_errors().size() == 1);
			error_at(parser, 0, vformat(R"*(Cannot get return value of call to "%s()" because it returns "void".)*", example.name), outer->arguments[0]);
			CHECK(parser.get_warnings().is_empty());
			public_agrees(source, path, parser);
		}
	}
	TEST_CASE("void_direct_return_discards_hard_nonvoid_but_rejects_literals") {
		for (const char *expression : { "integer()", "print()", "1", "null", "integer() + 1" }) {
			StorageFixture storage;
			TypeProfile profile;
			BSParser parser;
			const String source = String("func integer() -> int:\n\treturn 1\nfunc test() -> void:\n\treturn ") + expression + "\n";
			const String path = storage.path("void_return.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			const bool call = String(expression) == "integer()" || String(expression) == "print()";
			CHECK((analyzer.analyze() == OK) == call);
			diagnostics(parser);
			auto *ret = static_cast<BSParser::ReturnNode *>(parser.get_tree()->get_member("test").function->body->statements[0]);
			CHECK(ret->void_return);
			CHECK(ret->get_datatype().builtin_type == Variant::NIL);
			CHECK(ret->get_datatype().is_hard_type());
			CHECK(ret->get_datatype().is_constant);
			CHECK(parser.get_errors().size() == (call ? 0 : 1));
			if (!call)
				error_at(parser, 0, "A void function cannot return a value.", ret);
			CHECK(parser.get_warnings().is_empty());
			public_agrees(source, path, parser);
		}
	}
	TEST_CASE("converted_colors_publish_actual_values_across_existing_consumers") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		const String source = "const MEMBER: Color = 'red'\nvar member: Color = 'blue'\nfunc take(value: Color = 'red') -> Color:\n\treturn 'blue'\nfunc test():\n\tconst LOCAL: Color = 'red'\n\tvar local: Color = 'blue'\n\tlocal = 'red'\n\ttake('blue')\n\tvar casted = 'red' as Color\n\tvar colors: Array[Color] = ['blue']\n\tvar mapping: Dictionary[Color, Color] = {'red': 'blue'}\n\tvar nullable: Color? = null\n";
		BS_TEST_REQUIRE(parser.parse(source, storage.path("colors.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		color_value(parser.get_tree()->get_member("MEMBER").constant->initializer, Color(1, 0, 0));
		color_value(parser.get_tree()->get_member("member").variable->initializer, Color(0, 0, 1));
		auto *take = parser.get_tree()->get_member("take").function;
		BS_TEST_REQUIRE(take && take->parameters.size() == 1);
		color_value(take->parameters[0]->initializer, Color(1, 0, 0));
		BS_TEST_REQUIRE(take->default_arg_values.size() == 1);
		CHECK(take->default_arg_values[0].get_type() == Variant::COLOR);
		CHECK(Color(take->default_arg_values[0]) == Color(1, 0, 0));
		BS_TEST_REQUIRE(take->info.default_arguments.size() == 1);
		CHECK(take->info.default_arguments[0].get_type() == Variant::COLOR);
		CHECK(Color(take->info.default_arguments[0]) == Color(1, 0, 0));
		color_value(static_cast<BSParser::ReturnNode *>(take->body->statements[0])->return_value, Color(0, 0, 1));
		auto *body = parser.get_tree()->get_member("test").function->body;
		BS_TEST_REQUIRE(body && body->statements.size() == 8);
		color_value(static_cast<BSParser::ConstantNode *>(body->statements[0])->initializer, Color(1, 0, 0));
		color_value(local(parser, "local")->initializer, Color(0, 0, 1));
		color_value(static_cast<BSParser::AssignmentNode *>(body->statements[2])->assigned_value, Color(1, 0, 0));
		color_value(static_cast<BSParser::CallNode *>(body->statements[3])->arguments[0], Color(0, 0, 1));
		color_value(local(parser, "casted")->initializer, Color(1, 0, 0));
		auto *array = static_cast<BSParser::ArrayNode *>(local(parser, "colors")->initializer);
		BS_TEST_REQUIRE(array->elements.size() == 1);
		color_value(array->elements[0], Color(0, 0, 1));
		auto *mapping = local(parser, "mapping")->initializer;
		CHECK(mapping->reduced_value.get_type() == Variant::DICTIONARY);
		const Dictionary dict = mapping->reduced_value;
		CHECK(dict.has(Color(1, 0, 0)));
		CHECK(Color(dict.get(Color(1, 0, 0), Variant())) == Color(0, 0, 1));
		auto *nullable = local(parser, "nullable")->initializer;
		CHECK(nullable->reduced_value.get_type() == Variant::NIL);
		CHECK(nullable->get_datatype().is_nullable);
		CHECK(parser.get_warnings().is_empty());
		public_agrees(source, storage.path("colors.barista"), parser);
	}
	TEST_CASE("invalid_container_binary_and_scalar_evaluator_errors_remain_distinct") {
		for (const char *expression : { "{} + {}", "'x' + []", "true + false" }) {
			StorageFixture storage;
			TypeProfile profile;
			BSParser parser;
			const String source = String("func test():\n\tvar value = ") + expression + "\n";
			BS_TEST_REQUIRE(parser.parse(source, storage.path("binary.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			auto *v = local(parser, "value");
			BS_TEST_REQUIRE(v && v->initializer);
			CHECK(parser.get_errors().size() == 1);
			const String expected = String(expression) == "{} + {}" ? "Invalid operands \"Dictionary\" and \"Dictionary\" for \"+\" operator." : String(expression) == "'x' + []" ? "Invalid operands \"String\" and \"Array\" for \"+\" operator."
																																												  : "Invalid operands to operator +, bool and bool.";
			error_at(parser, 0, expected, v->initializer);
			CHECK(parser.get_warnings().is_empty());
		}
	}
	TEST_CASE("local_type_claims_shadow_native_and_preserve_alias_identity") {
		for (const char *declaration : { "const Node = 1", "var Node = 1", "" }) {
			StorageFixture storage;
			TypeProfile profile;
			BSParser parser;
			const bool parameter = String(declaration).is_empty();
			const String source = String(parameter ? "func test(Node: int):\n" : "func test():\n\t" + String(declaration) + "\n") + "\tvar value: Node\n";
			const String path = storage.path("local_claim.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			auto *v = local(parser, "value");
			BS_TEST_REQUIRE(v && v->datatype_specifier);
			CHECK(parser.get_errors().size() == 1);
			error_at(parser, 0, parameter ? "Local parameter \"Node\" cannot be used as a type." : String(declaration).begins_with("const") ? "Local constant \"Node\" is not a valid type."
																																			: "Local variable \"Node\" cannot be used as a type.",
					v->datatype_specifier->type_chain[0]);
			CHECK(parser.get_warnings().is_empty());
			public_agrees(source, path, parser);
		}
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		const String source = "class Owner:\n\tclass Child:\n\t\tpass\n\ttuple Pair(x: int, y: int)\nenum E:\n\tA = 0\nfunc test():\n\tconst Alias = Owner\n\tconst EnumAlias = E\n\tconst TupleAlias = Owner.Pair\n\tvar child: Alias.Child?\n\tvar value: EnumAlias = E.A\n\tvar pair: TupleAlias = Owner.Pair(1,2)\n";
		BS_TEST_REQUIRE(parser.parse(source, storage.path("local_alias.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		auto *body = parser.get_tree()->get_member("test").function->body;
		BS_TEST_REQUIRE(body && body->statements.size() == 6);
		auto *alias = static_cast<BSParser::ConstantNode *>(body->statements[2]);
		CHECK(parser.get_errors().size() == 1);
		error_at(parser, 0, "Assigned value for constant \"TupleAlias\" isn't a constant expression.", alias->initializer);
		CHECK(alias->get_datatype().kind == BSParser::DataType::TUPLE);
		CHECK(alias->get_datatype().is_meta_type);
		CHECK_FALSE(BSAnalyzer::has_materialized_constant_value(alias->initializer));
		auto *child = local(parser, "child"), *value = local(parser, "value"), *pair = local(parser, "pair");
		BS_TEST_REQUIRE(child && value && pair);
		CHECK(child->get_datatype().is_nullable);
		CHECK(child->get_datatype().class_type == parser.get_tree()->get_member("Owner").m_class->get_member("Child").m_class);
		CHECK(value->get_datatype().enum_type == parser.get_tree()->get_member("E").get_datatype().enum_type);
		CHECK(pair->get_datatype().kind == BSParser::DataType::TUPLE);
		CHECK(parser.get_warnings().is_empty());
	}
}

TEST_SUITE("expression_consumer_analyzer") {
	TEST_CASE("bare_and_explicit_callable_consumers_keep_distinct_result_contracts") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		const String source = "func target() -> int:\n\treturn 1\nfunc test():\n\tvar bare = target.call()\n\tvar qualified = self.target.call()\n\tvar authored: Callable[[], int] = target\n\tvar typed = authored.call()\n\tvar lambda = func() -> int: return 2\n\tvar inferred = lambda.call()\n\tvar bare_bound = target.bind()\n\tvar typed_bound = self.target.bind()\n";
		BS_TEST_REQUIRE(parser.parse(source, storage.path("callable_contracts.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		auto *bare = local(parser, "bare"), *qualified = local(parser, "qualified"), *typed = local(parser, "typed"), *inferred = local(parser, "inferred"), *lambda = local(parser, "lambda"), *bare_bound = local(parser, "bare_bound"), *typed_bound = local(parser, "typed_bound");
		BS_TEST_REQUIRE(bare && qualified && typed && inferred && lambda && bare_bound && typed_bound);
		CHECK(bare->get_datatype().is_variant());
		CHECK(inferred->get_datatype().is_variant());
		CHECK(qualified->get_datatype().builtin_type == Variant::INT);
		CHECK(typed->get_datatype().builtin_type == Variant::INT);
		CHECK(lambda->get_datatype().has_method_signature);
		CHECK_FALSE(lambda->get_datatype().has_explicit_method_signature);
		CHECK(lambda->get_datatype().method_return_type.size() == 1);
		CHECK_FALSE(bare_bound->get_datatype().has_explicit_method_signature);
		CHECK(typed_bound->get_datatype().has_explicit_method_signature);
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("void_statement_await_and_variant_result_remain_admitted") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		const String source = "func target() -> void:\n\tpass\nfunc unknown(value):\n\treturn value\nfunc test():\n\ttarget()\n\tprint()\n\tawait target()\n\tvar value = unknown(1)\n\tvar indirect = target.call()\n";
		BS_TEST_REQUIRE(parser.parse(source, storage.path("void_opposites.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(local(parser, "value")->get_datatype().is_variant());
		CHECK(local(parser, "indirect")->get_datatype().is_variant());
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("void_return_nested_call_is_still_a_value_context") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		const String source = "func target() -> void:\n\tpass\nfunc test() -> void:\n\treturn print(target())\n";
		BS_TEST_REQUIRE(parser.parse(source, storage.path("void_nested.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		CHECK(parser.get_errors().size() == 1);
		auto *ret = static_cast<BSParser::ReturnNode *>(parser.get_tree()->get_member("test").function->body->statements[0]);
		auto *outer = static_cast<BSParser::CallNode *>(ret->return_value);
		BS_TEST_REQUIRE(outer->arguments.size() == 1);
		error_at(parser, 0, "Cannot get return value of call to \"target()\" because it returns \"void\".", outer->arguments[0]);
		CHECK(ret->void_return);
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("unsafe_void_return_has_return_origin_and_exact_warning") {
		StorageFixture storage;
		TypeProfile profile;
		profile.set(BSWarning::get_setting_path_from_code(BSWarning::UNSAFE_VOID_RETURN), BSWarning::WARN);
		BSParser::update_project_settings();
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func unknown(value):\n\treturn value\nfunc test() -> void:\n\treturn unknown(1)\n", storage.path("void_warning.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		BS_TEST_REQUIRE(parser.get_warnings().size() == 1);
		const auto &w = parser.get_warnings().front()->get();
		auto *ret = static_cast<BSParser::ReturnNode *>(parser.get_tree()->get_member("test").function->body->statements[0]);
		CHECK(w.code == BSWarning::UNSAFE_VOID_RETURN);
		CHECK(w.start_line == ret->start_line);
		CHECK(w.start_column == ret->start_column);
		CHECK(w.end_line == ret->end_line);
		CHECK(w.end_column == ret->end_column);
		CHECK(w.get_message() == "The method \"test()\" returns \"void\" but it's trying to return a call to \"unknown()\" that can't be ensured to also be \"void\".");
		CHECK(ret->void_return);
		CHECK(ret->get_datatype().builtin_type == Variant::NIL);
	}
	TEST_CASE("successful_scalar_array_folds_keep_actual_readonly_carriers") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func test():\n\tconst scalar = 2 + 3\n\tconst values = [1] + [2]\n", storage.path("folds.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		auto *body = parser.get_tree()->get_member("test").function->body;
		BS_TEST_REQUIRE(body && body->statements.size() == 2);
		auto *scalar = static_cast<BSParser::ConstantNode *>(body->statements[0])->initializer;
		auto *values = static_cast<BSParser::ConstantNode *>(body->statements[1])->initializer;
		CHECK(BSAnalyzer::has_materialized_constant_value(scalar));
		CHECK(int64_t(scalar->reduced_value) == 5);
		CHECK(BSAnalyzer::has_materialized_constant_value(values));
		BS_TEST_REQUIRE(values->reduced_value.get_type() == Variant::ARRAY);
		const Array array = values->reduced_value;
		CHECK(array.is_read_only());
		BS_TEST_REQUIRE(array.size() == 2);
		CHECK(int64_t(array[0]) == 1);
		CHECK(int64_t(array[1]) == 2);
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("local_preload_alias_keeps_retained_owner_and_nested_nullable_type") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		const String helper = "extends RefCounted\nclass Child:\n\tpass\n";
		BS_TEST_REQUIRE(write_bytes(storage.path("helper.notest.barista"), bytes(helper)));
		const String source = "func test():\n\tconst Alias = preload('helper.notest.barista')\n\tvar child: Alias.Child?\n";
		BS_TEST_REQUIRE(parser.parse(source, storage.path("preload_alias.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		auto *child = local(parser, "child");
		BS_TEST_REQUIRE(child);
		CHECK(child->get_datatype().kind == BSParser::DataType::CLASS);
		CHECK(child->get_datatype().is_nullable);
		CHECK(child->get_datatype().script_path == storage.path("helper.notest.barista"));
		CHECK_FALSE(child->get_datatype().is_meta_type);
		CHECK(parser.get_warnings().is_empty());
	}
}

TEST_SUITE("expression_consumer_analyzer") {
	TEST_CASE("bare_async_call_and_callv_wrap_variant_after_outer_validation") {
		for (bool explicit_receiver : { false, true })
			for (bool callv : { false, true }) {
				StorageFixture storage;
				TypeProfile profile;
				BSParser parser;
				const String source = String("async func work() -> int:\n\treturn 1\nfunc test():\n\tvar result = ") + (explicit_receiver ? "self.work." : "work.") + (callv ? "callv([])" : "call()") + "\n";
				BS_TEST_REQUIRE(parser.parse(source, storage.path("async_results.barista"), false) == OK);
				BSAnalyzer analyzer(&parser);
				CHECK(analyzer.analyze() == OK);
				diagnostics(parser);
				auto *result = local(parser, "result");
				BS_TEST_REQUIRE(result && result->initializer);
				CHECK(result->get_datatype().is_coroutine);
				BS_TEST_REQUIRE(result->get_datatype().container_element_types.size() == 1);
				const auto payload = result->get_datatype().container_element_types[0];
				CHECK(payload.is_variant() == !explicit_receiver);
				if (explicit_receiver)
					CHECK(payload.builtin_type == Variant::INT);
				CHECK(parser.get_warnings().is_empty());
			}
		for (const char *method : { "callv", "bindv" }) {
			StorageFixture storage;
			TypeProfile profile;
			BSParser parser;
			const String source = String("async func work() -> int:\n\treturn 1\nfunc test():\n\tvar result = work.") + method + "(1)\n";
			BS_TEST_REQUIRE(parser.parse(source, storage.path("async_outer.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			auto *result = local(parser, "result");
			BS_TEST_REQUIRE(result && result->initializer);
			auto *call = static_cast<BSParser::CallNode *>(result->initializer);
			BS_TEST_REQUIRE(call->arguments.size() == 1);
			CHECK(parser.get_errors().size() == 1);
			error_at(parser, 0, vformat(R"*(Invalid argument for "%s()" function: argument 1 should be "Array" but is "int".)*", method), call->arguments[0]);
			CHECK(parser.get_warnings().is_empty());
		}
	}
	TEST_CASE("bare_callable_variant_result_obeys_strict_dynamic_destination") {
		StorageFixture storage;
		TypeProfile profile;
		profile.set("debug/barista_script/analysis/strict_dynamic_checks", true);
		BSParser::update_project_settings();
		BSParser parser;
		const String source = "func target() -> int:\n\treturn 1\nfunc test():\n\tvar value: int = target.call()\n\tvar typed: int = self.target.call()\n";
		BS_TEST_REQUIRE(parser.parse(source, storage.path("strict_callable.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		auto *value = local(parser, "value");
		BS_TEST_REQUIRE(value && value->initializer);
		CHECK(value->initializer->get_datatype().is_variant());
		CHECK(parser.get_errors().size() == 1);
		error_at(parser, 0, "Cannot assign Variant value to variable \"value\" in strict dynamic mode; expected \"int\".", value->initializer);
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("enum_cast_profiles_preserve_nullable_and_gradual_provenance") {
		for (bool gradual : { false, true }) {
			StorageFixture storage;
			TypeProfile profile(!gradual);
			profile.set("debug/barista_script/analysis/strict_dynamic_checks", gradual);
			BSParser::update_project_settings();
			BSParser parser;
			const String source = String("enum E:\n\tZERO = 0\nconst BOX: ") + (gradual ? "Variant" : "int?") + " = 0\nfunc test():\n\tvar value = BOX as E\n";
			BS_TEST_REQUIRE(parser.parse(source, storage.path("enum_profile.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() != OK) == gradual);
			diagnostics(parser);
			auto *value = local(parser, "value");
			BS_TEST_REQUIRE(value && value->initializer);
			auto *cast = static_cast<BSParser::CastNode *>(value->initializer);
			CHECK(parser.get_errors().size() == (gradual ? 1 : 0));
			if (gradual) {
				error_at(parser, 0, "Cannot cast a value of type \"Variant\" as \"enum_profile.barista.E\".", cast->operand);
				CHECK(cast->operand->get_datatype().is_variant());
				CHECK_FALSE(cast->is_constant);
			} else {
				// Pin's explicit int-backed cast is intentional narrowing, even from int?.
				CHECK(cast->is_constant);
				CHECK(cast->reduced_value.get_type() == Variant::INT);
				CHECK(int64_t(cast->reduced_value) == 0);
				CHECK(cast->get_datatype().kind == BSParser::DataType::ENUM);
			}
			CHECK(parser.get_warnings().is_empty());
		}
	}
}

TEST_SUITE("expression_consumer_analyzer") {
	TEST_CASE("qualified_callable_counterparts_retain_typed_argument_and_arity_rejections") {
		struct Example {
			const char *expression;
			const char *message;
		};
		for (const auto &example : {
					 Example{ "self.one.bind('bad')", "Invalid argument for \"bind()\" function: argument 1 should be \"int\" but is \"String\"." },
					 Example{ "self.add.bind(5).call()", "Too few arguments for \"call()\" call. Expected at least 1 but received 0." },
					 Example{ "self.one.unbind(0)", "Amount of \"unbind()\" arguments must be 1 or greater." },
					 Example{ "self.one.bindv(['bad'])", "Invalid argument for \"bindv()\" function: argument 1 should be \"int\" but is \"String\"." },
					 Example{ "self.one.callv(['bad'])", "Invalid argument for \"callv()\" function: argument 1 should be \"int\" but is \"String\"." },
					 Example{ "self.add.callv([1])", "Too few arguments for \"callv()\" call. Expected at least 2 but received 1." },
					 Example{ "self.one.call_deferred('bad')", "Invalid argument for \"call_deferred()\" function: argument 1 should be \"int\" but is \"String\"." },
					 Example{ "self.add.rpc_id(1, 2)", "Too few arguments for \"rpc_id()\" call. Expected at least 3 but received 2." },
					 Example{ "self.one.rpc_id()", "Too few arguments for \"rpc_id()\" call. Expected at least 2 but received 0." } }) {
			const String source = String("func one(v: int) -> int:\n\treturn v\nfunc add(a: int, b: int) -> int:\n\treturn a+b\nfunc test() -> void:\n\t") + example.expression + "\n";
			const String expected = String(">> ERROR at line 6: ") + example.message;
			original("qualified_counterpart.barista", source.utf8().get_data(), expected.utf8().get_data());
		}
		for (const char *invocation : { "call()", "callv([])", "call_deferred()", "rpc()", "rpc_id(1)" }) {
			const String source = String("func zero() -> int:\n\treturn 1\nfunc test() -> void:\n\tself.zero.bind(1).") + invocation + "\n";
			original("qualified_overbound.barista", source.utf8().get_data(), ">> ERROR at line 4: Cannot invoke this Callable: it was over-bound (more arguments were bound than its target accepts), so the call can never succeed.");
		}
	}
	TEST_CASE("constructor_warning_ownership_preserves_selected_metadata_and_nonconstant_path") {
		StorageFixture storage;
		TypeProfile profile;
		profile.set(BSWarning::get_setting_path_from_code(BSWarning::NARROWING_CONVERSION), BSWarning::WARN);
		BSParser::update_project_settings();
		BSParser parser;
		const String source = "func test(value: int):\n\tvar result = Vector2i(1.0, value)\n";
		BS_TEST_REQUIRE(parser.parse(source, storage.path("constructor_warning.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		auto *result = local(parser, "result");
		BS_TEST_REQUIRE(result && result->initializer);
		auto *call = static_cast<BSParser::CallNode *>(result->initializer);
		CHECK_FALSE(call->is_constant);
		CHECK(call->get_datatype().builtin_type == Variant::VECTOR2I);
		BS_TEST_REQUIRE(call->arguments.size() == 2 && call->resolved_parameter_types.size() == 2);
		CHECK(call->resolved_parameter_types[0].builtin_type == Variant::INT);
		CHECK(call->resolved_parameter_types[1].builtin_type == Variant::INT);
		CHECK(call->arguments[0]->reduced_value.get_type() == Variant::INT);
		CHECK(int64_t(call->arguments[0]->reduced_value) == 1);
		CHECK_FALSE(call->arguments[1]->is_constant);
		BS_TEST_REQUIRE(parser.get_warnings().size() == 2);
		int index = 0;
		for (const auto &warning : parser.get_warnings()) {
			const BSParser::Node *origin = index++ == 0 ? static_cast<BSParser::Node *>(call) : call->arguments[0];
			CHECK(warning.code == BSWarning::NARROWING_CONVERSION);
			CHECK(warning.get_message() == "Narrowing conversion (float is converted to int and loses precision).");
			CHECK(warning.start_line == origin->start_line);
			CHECK(warning.start_column == origin->start_column);
			CHECK(warning.end_line == origin->end_line);
			CHECK(warning.end_column == origin->end_column);
		}
	}
	TEST_CASE("nullable_enum_null_constant_preserves_actual_nil") {
		StorageFixture storage;
		TypeProfile profile(true);
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("enum E:\n\tZERO = 0\nconst BOX: int? = null\nfunc test():\n\tvar value = BOX as E?\n", storage.path("enum_null.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		auto *value = local(parser, "value");
		BS_TEST_REQUIRE(value && value->initializer);
		CHECK(value->initializer->is_constant);
		CHECK(value->initializer->reduced_value.get_type() == Variant::NIL);
		CHECK(value->initializer->get_datatype().kind == BSParser::DataType::ENUM);
		CHECK(value->initializer->get_datatype().is_nullable);
		CHECK(parser.get_warnings().is_empty());
	}
}

TEST_SUITE("expression_consumer_analyzer") {
	TEST_CASE("qualified_async_void_discard_is_exempt_while_bare_variant_discard_warns") {
		for (bool explicit_receiver : { false, true }) {
			StorageFixture storage;
			TypeProfile profile;
			profile.set(BSWarning::get_setting_path_from_code(BSWarning::MISSING_AWAIT), BSWarning::WARN);
			BSParser::update_project_settings();
			BSParser parser;
			const String source = String("async func fire() -> void:\n\tpass\nfunc test():\n\t") + (explicit_receiver ? "self.fire.call()" : "fire.call()") + "\n";
			BS_TEST_REQUIRE(parser.parse(source, storage.path("async_void_discard.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			auto *body = parser.get_tree()->get_member("test").function->body;
			BS_TEST_REQUIRE(body && body->statements.size() == 1);
			auto *call = static_cast<BSParser::CallNode *>(body->statements[0]);
			CHECK(call->get_datatype().is_coroutine);
			BS_TEST_REQUIRE(call->get_datatype().container_element_types.size() == 1);
			const auto payload = call->get_datatype().container_element_types[0];
			CHECK(payload.is_variant() == !explicit_receiver);
			if (explicit_receiver) {
				CHECK(payload.kind == BSParser::DataType::BUILTIN);
				CHECK(payload.builtin_type == Variant::NIL);
				CHECK(parser.get_warnings().is_empty());
			} else {
				BS_TEST_REQUIRE(parser.get_warnings().size() == 1);
				const auto &warning = parser.get_warnings().front()->get();
				CHECK(warning.code == BSWarning::MISSING_AWAIT);
				CHECK(warning.start_line == call->start_line);
				CHECK(warning.start_column == call->start_column);
				CHECK(warning.end_line == call->end_line);
				CHECK(warning.end_column == call->end_column);
			}
		}
	}
}

TEST_SUITE("expression_consumer_analyzer") {
	TEST_CASE("parameter_default_refusal_keeps_named_origin_and_recovery_slots") {
		for (bool numeric : { false, true }) {
			StorageFixture storage;
			TypeProfile profile;
			BSParser parser;
			const String source = String("func take(value: ") + (numeric ? "int = 1.5" : "String = Color.RED") + ", good: Color = 'red') -> void:\n\tpass\nfunc test():\n\ttake()\n";
			const String path = storage.path("default_refusal.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			auto *take = parser.get_tree()->get_member("take").function;
			BS_TEST_REQUIRE(take && take->parameters.size() == 2);
			auto *initializer = take->parameters[0]->initializer;
			BS_TEST_REQUIRE(parser.get_errors().size() == 1);
			error_at(parser, 0, numeric ? "Cannot assign a value of type float to parameter \"value\" with specified type int." : "Cannot assign a value of type Color to parameter \"value\" with specified type String.", initializer);
			CHECK(initializer->is_constant);
			CHECK(initializer->reduced_value.get_type() == (numeric ? Variant::FLOAT : Variant::COLOR));
			BS_TEST_REQUIRE(take->default_arg_values.size() == 2 && take->info.default_arguments.size() == 2);
			CHECK(take->default_arg_values[0].get_type() == Variant::NIL);
			CHECK(take->info.default_arguments[0].get_type() == Variant::NIL);
			CHECK(take->default_arg_values[1].get_type() == Variant::COLOR);
			CHECK(take->info.default_arguments[1].get_type() == Variant::COLOR);
			CHECK(Color(take->default_arg_values[1]) == Color(1, 0, 0));
			CHECK(Color(take->info.default_arguments[1]) == Color(1, 0, 0));
			color_value(take->parameters[1]->initializer, Color(1, 0, 0));
			CHECK(parser.get_warnings().is_empty());
			public_agrees(source, path, parser);
		}
	}
	TEST_CASE("utility_void_error_precedes_arity_while_method_order_is_preserved") {
		for (int kind = 0; kind < 3; ++kind) {
			StorageFixture storage;
			TypeProfile profile;
			BSParser parser;
			const String source = kind == 0 ? "func test():\n\tprint(seed())\n" : kind == 1 ? "func seed(value: int) -> void:\n\tpass\nfunc test():\n\tprint(seed())\n"
																							: "func test():\n\tprint(self.free(1))\n";
			const String path = storage.path("void_order.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			auto *outer = static_cast<BSParser::CallNode *>(parser.get_tree()->get_member("test").function->body->statements[0]);
			BS_TEST_REQUIRE(outer->arguments.size() == 1);
			auto *inner = outer->arguments[0];
			const String void_error = kind == 2 ? "Cannot get return value of call to \"free()\" because it returns \"void\"." : "Cannot get return value of call to \"seed()\" because it returns \"void\".";
			const String arity_error = kind == 2 ? "Too many arguments for \"free()\" call. Expected at most 0 but received 1." : "Too few arguments for \"seed()\" call. Expected at least 1 but received 0.";
			BS_TEST_REQUIRE(parser.get_errors().size() == 2);
			const BSParser::Node *arity_origin = kind == 2 ? static_cast<BSParser::CallNode *>(inner)->arguments[0] : inner;
			error_at(parser, 0, kind == 0 ? void_error : arity_error, kind == 0 ? inner : arity_origin);
			error_at(parser, 1, kind == 0 ? arity_error : void_error, inner);
			public_agrees(source, path, parser);
			Ref<BaristaScriptAnalyzerProbe> probe;
			probe.instantiate();
			const Dictionary validated = probe->validate_source(source, path, true);
			const Array errors = validated.get("errors", Array());
			BS_TEST_REQUIRE(errors.size() == 2);
			CHECK(String(Dictionary(errors[0]).get("message", "")) == (kind != 1 ? void_error : arity_error));
			CHECK(String(Dictionary(errors[1]).get("message", "")) == (kind != 1 ? arity_error : void_error));
			CHECK(parser.get_warnings().is_empty());
		}
	}
	TEST_CASE("true_nil_enum_cast_requires_nullable_destination_in_both_profiles") {
		for (bool strict : { false, true }) {
			for (bool nullable : { false, true }) {
				StorageFixture storage;
				TypeProfile profile(strict);
				profile.set(BSWarning::get_setting_path_from_code(BSWarning::INT_AS_ENUM_WITHOUT_MATCH), BSWarning::WARN);
				BSParser::update_project_settings();
				BSParser parser;
				const String source = String("enum E:\n\tZERO = 0\nconst BOX: int? = null\nfunc test():\n\tvar value = BOX as E") + (nullable ? "?" : "") + "\n";
				const String path = storage.path("enum_nil_opposite.barista");
				BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() == OK) == nullable);
				diagnostics(parser);
				auto *value = local(parser, "value");
				BS_TEST_REQUIRE(value && value->initializer);
				auto *cast = static_cast<BSParser::CastNode *>(value->initializer);
				CHECK(cast->operand->reduced_value.get_type() == Variant::NIL);
				CHECK(cast->is_constant == nullable);
				CHECK(parser.get_warnings().is_empty());
				if (nullable) {
					CHECK(parser.get_errors().is_empty());
					CHECK(cast->reduced_value.get_type() == Variant::NIL);
					CHECK(cast->get_datatype().is_nullable);
				} else {
					BS_TEST_REQUIRE(parser.get_errors().size() == 1);
					error_at(parser, 0, vformat("Failed to convert a value of type \"null\" to \"%s\".", cast->get_datatype().to_string()), cast->operand);
					CHECK(cast->operand->get_datatype().kind == BSParser::DataType::BUILTIN);
					CHECK(cast->operand->get_datatype().is_nullable);
				}
				public_agrees(source, path, parser);
			}
		}
	}
}

TEST_SUITE("expression_consumer_analyzer") {
	TEST_CASE("unavailable_and_nonconstant_defaults_keep_untrusted_slots_without_new_policy") {
		for (int kind = 0; kind < 3; ++kind) {
			StorageFixture storage;
			TypeProfile profile;
			BSParser parser;
			const String source = String("enum E:\n\tZERO = 0\nfunc produce() -> int:\n\treturn 1\nfunc take(value: Variant = ") + (kind == 0 ? "produce()" : kind == 1 ? "E"
																																										: "1 / 0") +
					", good: int = 2) -> void:\n\tpass\nfunc test():\n\ttake()\n";
			const String path = storage.path("default_recovery.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == (kind != 2));
			diagnostics(parser);
			auto *take = parser.get_tree()->get_member("take").function;
			BS_TEST_REQUIRE(take && take->parameters.size() == 2);
			auto *initializer = take->parameters[0]->initializer;
			if (kind != 2)
				CHECK_FALSE(BSAnalyzer::has_materialized_constant_value(initializer));
			BS_TEST_REQUIRE(take->default_arg_values.size() == 2 && take->info.default_arguments.size() == 2);
			CHECK(take->default_arg_values[0].get_type() == Variant::NIL);
			CHECK(take->info.default_arguments[0].get_type() == Variant::NIL);
			CHECK(take->default_arg_values[1].get_type() == Variant::INT);
			CHECK(int64_t(take->default_arg_values[1]) == 2);
			CHECK(take->info.default_arguments[1].get_type() == Variant::INT);
			CHECK(int64_t(take->info.default_arguments[1]) == 2);
			if (kind == 2) {
				BS_TEST_REQUIRE(parser.get_errors().size() == 1);
				error_at(parser, 0, "Invalid operands to operator /, int and int.", initializer);
			} else
				CHECK(parser.get_errors().is_empty());
			CHECK(parser.get_warnings().is_empty());
			public_agrees(source, path, parser);
		}
	}
	TEST_CASE("implicit_enum_nil_conversion_preserves_profile_reporter_and_nullable_payload") {
		for (bool strict : { false, true }) {
			for (bool nullable : { false, true }) {
				StorageFixture storage;
				TypeProfile profile(strict);
				profile.set(BSWarning::get_setting_path_from_code(BSWarning::INT_AS_ENUM_WITHOUT_MATCH), BSWarning::WARN);
				profile.set(BSWarning::get_setting_path_from_code(BSWarning::INT_AS_ENUM_WITHOUT_CAST), BSWarning::WARN);
				BSParser::update_project_settings();
				BSParser parser;
				const String source = String("enum E:\n\tZERO = 0\nconst BOX: int? = null\nfunc test():\n\tvar value: E") + (nullable ? "?" : "") + " = BOX\n";
				const String path = storage.path("enum_nil_implicit.barista");
				BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() == OK) == nullable);
				diagnostics(parser);
				auto *value = local(parser, "value");
				BS_TEST_REQUIRE(value && value->initializer);
				auto *initializer = value->initializer;
				CHECK(initializer->reduced_value.get_type() == Variant::NIL);
				CHECK(parser.get_warnings().is_empty());
				if (nullable) {
					CHECK(parser.get_errors().is_empty());
					CHECK(initializer->get_datatype().kind == BSParser::DataType::ENUM);
					CHECK(initializer->get_datatype().is_nullable);
				} else {
					BS_TEST_REQUIRE(parser.get_errors().size() == 1);
					error_at(parser, 0, strict ? vformat("Cannot assign nullable value of type \"int?\" to variable \"value\"; expected non-nullable \"%s\".", value->get_datatype().to_string()) : vformat("Failed to convert a value of type \"null\" to \"%s\".", value->get_datatype().to_string()), initializer);
					CHECK(initializer->get_datatype().kind == BSParser::DataType::BUILTIN);
				}
				public_agrees(source, path, parser);
			}
		}
	}
	TEST_CASE("utility_invalid_arity_root_and_await_do_not_add_void_value_error") {
		for (const char *expression : { "seed()", "await seed()" }) {
			original("utility_exempt.barista", (String("func test():\n\t") + expression + "\n").utf8().get_data(), ">> ERROR at line 2: Too few arguments for \"seed()\" call. Expected at least 1 but received 0.");
		}
	}
}

TEST_SUITE("expression_consumer_analyzer") {
	TEST_CASE("parameter_defaults_preserve_strict_profile_diagnostic_precedence") {
		for (bool dynamic : { false, true }) {
			for (bool strict : { false, true }) {
				StorageFixture storage;
				TypeProfile profile(!dynamic && strict);
				profile.set("debug/barista_script/analysis/strict_dynamic_checks", dynamic && strict);
				BSParser::update_project_settings();
				BSParser parser;
				const String source = String("const BOX: ") + (dynamic ? "Variant" : "String?") + " = 'red'\nfunc take(value: String = BOX) -> void:\n\tpass\nfunc test():\n\ttake()\n";
				const String path = storage.path("default_profile.barista");
				BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() == OK) == !strict);
				diagnostics(parser);
				auto *take = parser.get_tree()->get_member("take").function;
				BS_TEST_REQUIRE(take && take->parameters.size() == 1 && take->default_arg_values.size() == 1 && take->info.default_arguments.size() == 1);
				if (strict) {
					BS_TEST_REQUIRE(parser.get_errors().size() == 1);
					error_at(parser, 0, dynamic ? "Cannot assign Variant value to parameter \"value\" in strict dynamic mode; expected \"String\"." : "Cannot assign nullable value of type \"String?\" to parameter \"value\"; expected non-nullable \"String\".", take->parameters[0]->initializer);
					CHECK(take->default_arg_values[0].get_type() == Variant::NIL);
					CHECK(take->info.default_arguments[0].get_type() == Variant::NIL);
				} else {
					CHECK(parser.get_errors().is_empty());
					CHECK(take->default_arg_values[0].get_type() == Variant::STRING);
					CHECK(String(take->default_arg_values[0]) == "red");
					CHECK(take->info.default_arguments[0].get_type() == Variant::STRING);
					CHECK(String(take->info.default_arguments[0]) == "red");
				}
				CHECK(parser.get_warnings().is_empty());
				public_agrees(source, path, parser);
			}
		}
	}
}
