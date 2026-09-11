/**************************************************************************/
/*  parent_contract_analyzer_tests.cpp                                  */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/
#include "barista_script.h"
#include "bs_analyzer_probe.h"
#include "bs_conformance_registry.h"
#include "bs_global_class.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <godot_cpp/classes/project_settings.hpp>
#include <string>
using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;
namespace {
struct WarningScope {
	Dictionary saved;
	WarningScope() {
		auto *settings = ProjectSettings::get_singleton();
		for (int i = 0; i < BSWarning::WARNING_MAX; ++i) {
			const String path = BSWarning::get_setting_path_from_code(BSWarning::Code(i));
			saved[path] = settings->has_setting(path) ? settings->get_setting(path) : Variant();
			settings->set_setting(path, BSWarning::IGNORE);
		}
		BSParser::update_project_settings();
	}
	~WarningScope() {
		auto *settings = ProjectSettings::get_singleton();
		const Array keys = saved.keys();
		for (int i = 0; i < keys.size(); ++i)
			settings->set_setting(keys[i], saved[keys[i]]);
		BSParser::update_project_settings();
		for (int i = 0; i < keys.size(); ++i)
			if (Variant(saved[keys[i]]).get_type() == Variant::NIL && settings->has_setting(keys[i]))
				settings->clear(keys[i]);
	}
};
void diagnostics(const BSParser &parser) {
	for (const auto &e : parser.get_errors())
		MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column, "-", e.end_line, ":", e.end_column);
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
String public_block(const String &source, const String &path, const BSParser &parser) {
	Ref<BaristaScriptAnalyzerProbe> probe;
	probe.instantiate();
	const Dictionary result = probe->validate_source(source, path, true);
	const bool valid = parser.get_errors().is_empty();
	CHECK(bool(result.get("valid", !valid)) == valid);
	CHECK(bool(probe->analyze_source(source, path).get("valid", !valid)) == valid);
	Ref<BaristaScript> script;
	script.instantiate();
	script->set_path(path);
	script->_set_source_code(source);
	CHECK(script->_is_valid() == valid);
	const Array errors = result.get("errors", Array());
	CHECK(errors.size() == parser.get_errors().size());
	String block;
	for (int i = 0; i < errors.size(); ++i) {
		const Dictionary row = errors[i];
		if (i)
			block += "\n";
		block += vformat(">> ERROR at line %d: %s", int(row.get("line", 0)), String(row.get("message", "")));
		bool found = false;
		for (const auto &e : parser.get_errors())
			if (e.message == String(row.get("message", "")) && e.line == int(row.get("line", 0)) && e.column == int(row.get("column", 0)))
				found = true;
		CHECK(found);
	}
	CHECK(Array(result.get("warnings", Array())).is_empty());
	return block.is_empty() ? String("BS_TEST_OK") : block;
}
void original(const String &name, const String &source, const String &expected, int line, const String &owner, const String &method, bool external = false) {
	StorageFixture storage;
	BSConformanceRegistry::ScopedCorpusState registry;
	WarningScope warnings;
	const String path = storage.path(name);
	String actual_source = source;
	if (external) {
		const String provider = storage.path("final_method_base.notest.barista");
		BS_TEST_REQUIRE(write_bytes(provider, bytes("extends RefCounted\n\nfinal func locked() -> int:\n\treturn 1\n")));
		actual_source = source.replace("res://tests/corpus_staging/analyzer/errors/final_method_base.notest.barista", provider);
	}
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(actual_source, path, false) == OK);
	BSAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
	diagnostics(parser);
	CHECK(public_block(actual_source, path, parser) == expected);
	BS_TEST_REQUIRE(parser.get_errors().size() == 1);
	BSParser::ClassNode *child = owner.is_empty() ? parser.get_tree() : parser.get_tree()->get_member(owner).m_class;
	BS_TEST_REQUIRE(child && child->has_function(method));
	auto *function = child->get_member(method).function;
	CHECK(function->start_line == line);
	CHECK(function->start_column == (owner.is_empty() ? 1 : function->is_static ? 12
																				: 5));
	error_at(parser, 0, expected.substr(expected.find(": ") + 2), function);
	CHECK(parser.get_warnings().is_empty());
	const int before = parser.get_errors().size();
	CHECK(analyzer.analyze() != OK);
	CHECK(parser.get_errors().size() == before);
}

struct Expected {
	const char *message;
	bool rest = false;
};
void contract(const String &source, std::initializer_list<Expected> expected = {}, bool strict_null = false, bool strict_dynamic = false) {
	StorageFixture storage;
	BSConformanceRegistry::ScopedCorpusState registry;
	WarningScope warnings;
	auto *settings = ProjectSettings::get_singleton();
	const String null_key = "debug/barista_script/analysis/strict_null_checks";
	const String dynamic_key = "debug/barista_script/analysis/strict_dynamic_checks";
	const Variant old_null = settings->get_setting(null_key, false), old_dynamic = settings->get_setting(dynamic_key, false);
	settings->set_setting(null_key, strict_null);
	settings->set_setting(dynamic_key, strict_dynamic);
	struct Restore {
		String null_key, dynamic_key;
		Variant null_value, dynamic_value;
		~Restore() {
			auto *settings = ProjectSettings::get_singleton();
			settings->set_setting(null_key, null_value);
			settings->set_setting(dynamic_key, dynamic_value);
		}
	} restore{ null_key, dynamic_key, old_null, old_dynamic };
	const String path = storage.path("contract.barista");
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
	BSAnalyzer analyzer(&parser);
	CHECK((analyzer.analyze() == OK) == (expected.size() == 0));
	diagnostics(parser);
	BS_TEST_REQUIRE(parser.get_errors().size() == int(expected.size()));
	auto *child = parser.get_tree()->get_member("Child").m_class;
	BS_TEST_REQUIRE(child && child->has_function("f"));
	auto *function = child->get_member("f").function;
	CHECK(function->resolved_signature);
	CHECK(function->info.arguments.size() == function->parameters.size());
	CHECK(function->info.default_arguments.size() == function->default_arg_values.size());
	String block;
	int i = 0;
	for (const auto &error : expected) {
		const BSParser::Node *origin = error.rest ? static_cast<const BSParser::Node *>(function->rest_parameter) : function;
		error_at(parser, i++, error.message, origin);
		if (!block.is_empty())
			block += "\n";
		block += vformat(">> ERROR at line %d: %s", origin->start_line, String(error.message));
	}
	CHECK(public_block(source, path, parser) == (block.is_empty() ? String("BS_TEST_OK") : block));
	CHECK(parser.get_warnings().is_empty());
	const int before = parser.get_errors().size();
	CHECK((analyzer.analyze() == OK) == (expected.size() == 0));
	CHECK(parser.get_errors().size() == before);
}
} // namespace
TEST_SUITE("parent_contract_analyzer") {
	TEST_CASE("original_function_dont_match_parent_signature_parameter_count_less") { original("function_dont_match_parent_signature_parameter_count_less.barista", "func test():\n\tprint(\"Shouldn't reach this\")\n\nclass Parent:\n\tfunc my_function(_par1: int) -> int:\n\t\treturn 0\n\nclass Child extends Parent:\n\tfunc my_function() -> int:\n\t\treturn 0\n", ">> ERROR at line 9: The function signature doesn't match the parent. Parent signature is \"my_function(int) -> int\".", 9, "Child", "my_function"); }
	TEST_CASE("original_function_dont_match_parent_signature_parameter_count_more") { original("function_dont_match_parent_signature_parameter_count_more.barista", "func test():\n\tprint(\"Shouldn't reach this\")\n\nclass Parent:\n\tfunc my_function(_par1: int) -> int:\n\t\treturn 0\n\nclass Child extends Parent:\n\tfunc my_function(_pary1: int, _par2: int) -> int:\n\t\treturn 0\n", ">> ERROR at line 9: The function signature doesn't match the parent. Parent signature is \"my_function(int) -> int\".", 9, "Child", "my_function"); }
	TEST_CASE("original_function_dont_match_parent_signature_parameter_default_values") { original("function_dont_match_parent_signature_parameter_default_values.barista", "func test():\n\tprint(\"Shouldn't reach this\")\n\nclass Parent:\n\tfunc my_function(_par1: int = 0) -> int:\n\t\treturn 0\n\nclass Child extends Parent:\n\tfunc my_function(_par1: int) -> int:\n\t\treturn 0\n", ">> ERROR at line 9: The function signature doesn't match the parent. Parent signature is \"my_function(int = <default>) -> int\".", 9, "Child", "my_function"); }
	TEST_CASE("original_function_dont_match_parent_signature_parameter_type") { original("function_dont_match_parent_signature_parameter_type.barista", "func test():\n\tprint(\"Shouldn't reach this\")\n\nclass Parent:\n\tfunc my_function(_par1: int) -> int:\n\t\treturn 0\n\nclass Child extends Parent:\n\tfunc my_function(_par1: Vector2) -> int:\n\t\treturn 0\n", ">> ERROR at line 9: The function signature doesn't match the parent. Parent signature is \"my_function(int) -> int\".", 9, "Child", "my_function"); }
	TEST_CASE("original_function_dont_match_parent_signature_return_type") { original("function_dont_match_parent_signature_return_type.barista", "func test():\n\tprint(\"Shouldn't reach this\")\n\nclass Parent:\n\tfunc my_function() -> int:\n\t\treturn 0\n\nclass Child extends Parent:\n\tfunc my_function() -> Vector2:\n\t\treturn Vector2()\n", ">> ERROR at line 9: The function signature doesn't match the parent. Parent signature is \"my_function() -> int\".", 9, "Child", "my_function"); }
	TEST_CASE("original_function_param_type_invalid_contravariance_1") { original("function_param_type_invalid_contravariance_1.barista", "class A:\n\tfunc f(_p: Object):\n\t\tpass\n\nclass B extends A:\n\tfunc f(_p: Node):\n\t\tpass\n\nfunc test():\n\tpass\n", ">> ERROR at line 6: The function signature doesn't match the parent. Parent signature is \"f(Object) -> Variant\".", 6, "B", "f"); }
	TEST_CASE("original_function_param_type_invalid_contravariance_2") { original("function_param_type_invalid_contravariance_2.barista", "class A:\n\tfunc f(_p: Variant):\n\t\tpass\n\nclass B extends A:\n\tfunc f(_p: Node): # No `is_type_compatible()` misuse.\n\t\tpass\n\nfunc test():\n\tpass\n", ">> ERROR at line 6: The function signature doesn't match the parent. Parent signature is \"f(Variant) -> Variant\".", 6, "B", "f"); }
	TEST_CASE("original_function_param_type_invalid_contravariance_3") { original("function_param_type_invalid_contravariance_3.barista", "class A:\n\tfunc f(_p: int):\n\t\tpass\n\nclass B extends A:\n\tfunc f(_p: float): # No implicit conversion.\n\t\tpass\n\nfunc test():\n\tpass\n", ">> ERROR at line 6: The function signature doesn't match the parent. Parent signature is \"f(int) -> Variant\".", 6, "B", "f"); }
	TEST_CASE("original_function_return_type_invalid_covariance_1") { original("function_return_type_invalid_covariance_1.barista", "class A:\n\tfunc f() -> Node:\n\t\treturn null\n\nclass B extends A:\n\tfunc f() -> Object:\n\t\treturn null\n\nfunc test():\n\tpass\n", ">> ERROR at line 6: The function signature doesn't match the parent. Parent signature is \"f() -> Node\".", 6, "B", "f"); }
	TEST_CASE("original_function_return_type_invalid_covariance_2") { original("function_return_type_invalid_covariance_2.barista", "class A:\n\tfunc f() -> Node:\n\t\treturn null\n\nclass B extends A:\n\tfunc f() -> Variant: # No `is_type_compatible()` misuse.\n\t\treturn null\n\nfunc test():\n\tpass\n", ">> ERROR at line 6: The function signature doesn't match the parent. Parent signature is \"f() -> Node\".", 6, "B", "f"); }
	TEST_CASE("original_function_return_type_invalid_covariance_3") { original("function_return_type_invalid_covariance_3.barista", "class A:\n\tfunc f() -> Node:\n\t\treturn null\n\nclass B extends A:\n\tfunc f() -> void: # No `is_type_compatible()` misuse.\n\t\treturn\n\nfunc test():\n\tpass\n", ">> ERROR at line 6: The function signature doesn't match the parent. Parent signature is \"f() -> Node\".", 6, "B", "f"); }
	TEST_CASE("original_function_return_type_invalid_covariance_4") { original("function_return_type_invalid_covariance_4.barista", "class A:\n\tfunc f() -> float:\n\t\treturn 0.0\n\nclass B extends A:\n\tfunc f() -> int: # No implicit conversion.\n\t\treturn 0\n\nfunc test():\n\tpass\n", ">> ERROR at line 6: The function signature doesn't match the parent. Parent signature is \"f() -> float\".", 6, "B", "f"); }
	TEST_CASE("original_override_final_method") { original("override_final_method.barista", "class Base:\n\tfinal func greet() -> String:\n\t\treturn \"base\"\n\nclass Derived extends Base:\n\tfunc greet() -> String:\n\t\treturn \"derived\"\n\nfunc test():\n\tpass\n", ">> ERROR at line 6: Cannot override final function \"greet()\" declared in \"Base\".", 6, "Derived", "greet"); }
	TEST_CASE("original_override_final_method_crossfile") { original("override_final_method_crossfile.barista", "extends \"res://tests/corpus_staging/analyzer/errors/final_method_base.notest.barista\"\n\nfunc locked() -> int:\n\treturn 2\n\nfunc test():\n\tpass\n", ">> ERROR at line 3: Cannot override final function \"locked()\" declared in \"final_method_base.notest.barista\".", 3, "", "locked", true); }
	TEST_CASE("original_override_final_static_method") { original("override_final_static_method.barista", "class Base:\n\tfinal static func make() -> int:\n\t\treturn 1\n\nclass Derived extends Base:\n\tstatic func make() -> int:\n\t\treturn 2\n\nfunc test():\n\tpass\n", ">> ERROR at line 6: Cannot override final function \"make()\" declared in \"Base\".", 6, "Derived", "make"); }
	TEST_CASE("original_abstract_async_method_implementation") { original("abstract_async_method_implementation.barista", "abstract class AbstractAsync:\n\tabstract async func fetch() -> String\n\nclass SyncImplementation extends AbstractAsync:\n\tfunc fetch() -> String:\n\t\treturn \"sync\"\n\nfunc test():\n\tpass\n", ">> ERROR at line 5: The function \"fetch()\" must be async because it overrides an async parent function.", 5, "SyncImplementation", "fetch"); }

	TEST_CASE("same_nonfinal") { contract("class Parent:\n\tfunc f(_x: int) -> int:\n\t\treturn 1\nclass Child extends Parent:\n\tfunc f(_x: int) -> int:\n\t\treturn 2\n", {}, false); }

	TEST_CASE("same_async") { contract("class Parent:\n\tasync func f() -> int:\n\t\treturn 1\nclass Child extends Parent:\n\tasync func f() -> int:\n\t\treturn 2\n", {}, false); }

	TEST_CASE("same_static") { contract("class Parent:\n\tstatic func f() -> int:\n\t\treturn 1\nclass Child extends Parent:\n\tstatic func f() -> int:\n\t\treturn 2\n", {}, false); }

	TEST_CASE("extra_optional") { contract("class Parent:\n\tfunc f(_x: int) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(_x: int, _y: String = \"\") -> void:\n\t\tpass\n", {}, false); }

	TEST_CASE("adds_default") { contract("class Parent:\n\tfunc f(_x: int) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(_x: int = 1) -> void:\n\t\tpass\n", {}, false); }

	TEST_CASE("parameter_broadens") { contract("class Parent:\n\tfunc f(_x: Node) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(_x: Object) -> void:\n\t\tpass\n", {}, false); }

	TEST_CASE("return_narrows") { contract("class Parent:\n\tfunc f() -> Object:\n\t\treturn null\nclass Child extends Parent:\n\tfunc f() -> Node:\n\t\treturn null\n", {}, false); }

	TEST_CASE("untyped_child_skips_return") { contract("class Parent:\n\tfunc f() -> Node:\n\t\treturn null\nclass Child extends Parent:\n\tfunc f():\n\t\treturn 1\n", {}, false); }

	TEST_CASE("void_for_soft_parent") { contract("class Parent:\n\tfunc f():\n\t\tpass\nclass Child extends Parent:\n\tfunc f() -> void:\n\t\tpass\n", {}, false); }

	TEST_CASE("void_for_void") { contract("class Parent:\n\tfunc f() -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f() -> void:\n\t\tpass\n", {}, false); }

	TEST_CASE("rest_broadens") { contract("class Parent:\n\tfunc f(..._x: Array[Node]) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(..._x: Array[Object]) -> void:\n\t\tpass\n", {}, false); }

	TEST_CASE("rest_gradual_child") { contract("class Parent:\n\tfunc f(..._x: Array[int]) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(..._x) -> void:\n\t\tpass\n", {}, false); }

	TEST_CASE("rest_absorbs_fixed") { contract("class Parent:\n\tfunc f(_x: int, _y: int) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(..._x: Array[int]) -> void:\n\t\tpass\n", {}, false); }

	TEST_CASE("optional_prefix_rest") { contract("class Parent:\n\tfunc f(..._x: Array[int]) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(_x: int = 0, ..._y: Array[int]) -> void:\n\t\tpass\n", {}, false); }

	TEST_CASE("concrete_self") { contract("class Parent:\n\tfunc f(_x: Self) -> Self:\n\t\treturn self\nclass Child extends Parent:\n\tfunc f(_x: Child) -> Child:\n\t\treturn self\n", {}, false); }

	TEST_CASE("async_child_sync_parent") { contract("class Parent:\n\tfunc f() -> int:\n\t\treturn 1\nclass Child extends Parent:\n\tasync func f() -> int:\n\t\treturn 2\n", { { "The function \"f()\" cannot be async because it overrides a synchronous parent function." } }, false); }

	TEST_CASE("instance_child_static_parent") { contract("class Parent:\n\tstatic func f() -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f() -> void:\n\t\tpass\n", { { "The function signature doesn't match the parent. Parent signature is \"f() -> void\"." } }, false); }

	TEST_CASE("static_child_instance_parent") { contract("class Parent:\n\tfunc f() -> void:\n\t\tpass\nclass Child extends Parent:\n\tstatic func f() -> void:\n\t\tpass\n", { { "The function signature doesn't match the parent. Parent signature is \"f() -> void\"." } }, false); }

	TEST_CASE("void_for_hard_variant") { contract("class Parent:\n\tfunc f() -> Variant:\n\t\tpass\nclass Child extends Parent:\n\tfunc f() -> void:\n\t\tpass\n", { { "The function signature doesn't match the parent. Parent signature is \"f() -> Variant\"." } }, false); }

	TEST_CASE("rest_general_precedes_tail") { contract("class Parent:\n\tfunc f(_x: int, ..._y: Array[String]) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(_x: String, ..._y: Array[int]) -> void:\n\t\tpass\n", { { "The function signature doesn't match the parent. Parent signature is \"f(int, ...: Array[String]) -> void\"." } }, false); }

	TEST_CASE("missing_rest_general") { contract("class Parent:\n\tfunc f(..._x: Array[int]) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f() -> void:\n\t\tpass\n", { { "The function signature doesn't match the parent. Parent signature is \"f(...: Array[int]) -> void\"." } }, false); }

	TEST_CASE("rest_narrowing_refused") { contract("class Parent:\n\tfunc f(..._x: Array[Object]) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(..._x: Array[Node]) -> void:\n\t\tpass\n", { { "The rest parameter type \"Array[Node]\" does not accept every trailing argument allowed by the parent rest parameter type \"Array[Object]\".", true } }, false); }

	TEST_CASE("rest_cannot_narrow_gradual") { contract("class Parent:\n\tfunc f(..._x) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(..._x: Array[int]) -> void:\n\t\tpass\n", { { "The rest parameter type \"Array[int]\" does not accept every trailing argument allowed by the parent rest parameter type \"Array\".", true } }, false); }

	TEST_CASE("rest_absorbed_first_refusal") { contract("class Parent:\n\tfunc f(_x: int, _y: String, _z: bool) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(..._x: Array[int]) -> void:\n\t\tpass\n", { { "The rest parameter type \"Array[int]\" does not accept the parent parameter of type \"String\".", true } }, false); }

	TEST_CASE("rest_precedes_absorbed") { contract("class Parent:\n\tfunc f(_x: String, ..._y: Array[String]) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(..._x: Array[int]) -> void:\n\t\tpass\n", { { "The rest parameter type \"Array[int]\" does not accept every trailing argument allowed by the parent rest parameter type \"Array[String]\".", true } }, false); }

	TEST_CASE("final_before_general") { contract("class Parent:\n\tfinal func f(_x: int) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f() -> void:\n\t\tpass\n", { { "Cannot override final function \"f()\" declared in \"Parent\"." }, { "The function signature doesn't match the parent. Parent signature is \"f(int) -> void\"." } }, false); }

	TEST_CASE("final_before_async") { contract("class Parent:\n\tfinal func f(_x: int) -> void:\n\t\tpass\nclass Child extends Parent:\n\tasync func f() -> void:\n\t\tpass\n", { { "Cannot override final function \"f()\" declared in \"Parent\"." }, { "The function \"f()\" cannot be async because it overrides a synchronous parent function." } }, false); }

	TEST_CASE("strict_nullable_tail_False") { contract("class Parent:\n\tfunc f(..._x: Array[Node?]) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(..._x: Array[Node]) -> void:\n\t\tpass\n", { { "The rest parameter type \"Array[Node]\" does not accept every trailing argument allowed by the parent rest parameter type \"Array[Node?]\".", true } }, true); }

	TEST_CASE("strict_nullable_tail_True") { contract("class Parent:\n\tfunc f(..._x: Array[Node?]) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(..._x: Array[Node?]) -> void:\n\t\tpass\n", {}, true); }
	TEST_CASE("recursive_array_parent_domain_and_assignment_opposite") {
		for (bool broad : { false, true }) {
			const String parent = broad ? "Node" : "Object", child = broad ? "Object" : "Node";
			const String source = "class Parent:\n\tfunc f(_x: Array[" + parent + "]) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(_x: Array[" + child + "]) -> void:\n\t\tpass\nfunc assignment(value: Object):\n\tvar narrowed: Node = value\n";
			if (broad)
				contract(source);
			else
				contract(source, { { "The function signature doesn't match the parent. Parent signature is \"f(Array[Object]) -> void\"." } });
		}
	}
	TEST_CASE("nearest_ordinary_claims_before_traits_and_native") {
		contract("class Grand:\n\tfunc f(_x: Variant) -> void:\n\t\tpass\ntrait Reader:\n\tfunc f(_x: int) -> void:\n\t\tpass\nclass Parent extends Grand:\n\tuses Reader\nclass Child extends Parent:\n\tfunc f(_x: int) -> void:\n\t\tpass\n", { { "The function signature doesn't match the parent. Parent signature is \"f(Variant) -> void\"." } });
		contract("class Parent:\n\tvar f: Callable\nclass Child extends Parent:\n\tfunc f(_x: int) -> void:\n\t\tpass\n");
		contract("class Parent:\n\tvar f: int\nclass Child extends Parent:\n\tfunc f() -> void:\n\t\tpass\n", { { "Member \"f\" is not a function." } });
		contract("class Parent:\n\ttype f = int\nclass Child extends Parent:\n\tfunc f() -> void:\n\t\tpass\n", { { "Type alias \"f\" cannot be called. It names a type without declaring one, so it has no constructor; call the aliased type instead." } });
	}
	TEST_CASE("applied_trait_parent_signature_is_selected") {
		contract("trait Reader:\n\tfunc f(_x: int) -> void:\n\t\tpass\nclass Parent:\n\tuses Reader\nclass Child extends Parent:\n\tfunc f() -> void:\n\t\tpass\n", { { "The function signature doesn't match the parent. Parent signature is \"f(int) -> void\"." } });
	}
	TEST_CASE("constructor_lambda_enum_exemptions_keep_ordinary_opposite") {
		contract("class Parent:\n\tfunc _init(_x: int):\n\t\tpass\n\tstatic func _static_init():\n\t\tpass\n\tfunc f(_x: int) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc _init():\n\t\tpass\n\tstatic func _static_init():\n\t\tpass\n\tfunc f(_x: int) -> void:\n\t\tvar callback = func f(): pass\n\tenum E:\n\t\tA = 0\n\t\tfunc f():\n\t\t\tpass\n");
	}
	TEST_CASE("native_virtual_contract_and_actual_nonvirtual_warning_owner") {
		for (int mode : { 0, 1, 2 }) {
			StorageFixture storage;
			WarningScope warnings;
			ProjectSettings::get_singleton()->set_setting(BSWarning::get_setting_path_from_code(BSWarning::NATIVE_METHOD_OVERRIDE), BSWarning::WARN);
			BSParser::update_project_settings();
			const String source = mode == 0 ? "extends Node\nfunc _process(_delta: float) -> void:\n\tpass\n" : mode == 1 ? "extends Node\nasync func _process(_delta: float) -> void:\n\tpass\n"
																														  : "extends Node\nfunc get_instance_id() -> int:\n\treturn 7\n";
			const String method = mode == 2 ? "get_instance_id" : "_process";
			const String path = storage.path("native_parent.barista");
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == (mode != 1));
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (mode == 1 ? 1 : 0));
			auto *function = parser.get_tree()->get_member(method).function;
			if (mode == 1)
				error_at(parser, 0, "The function \"_process()\" cannot be async because it overrides a synchronous parent function.", function);
			BS_TEST_REQUIRE(parser.get_warnings().size() == (mode == 2 ? 1 : 0));
			if (mode == 2) {
				const auto &warning = parser.get_warnings().front()->get();
				CHECK(warning.code == BSWarning::NATIVE_METHOD_OVERRIDE);
				CHECK(warning.get_message() == "The method \"get_instance_id()\" overrides a method from native class \"Object\". This won't be called by the engine and may not work as expected.");
				CHECK(warning.start_line == function->start_line);
				CHECK(warning.start_column == function->start_column);
				CHECK(warning.end_line == function->end_line);
				CHECK(warning.end_column == function->end_column);
			}
			Ref<BaristaScriptAnalyzerProbe> probe;
			probe.instantiate();
			const Dictionary result = probe->validate_source(source, path, true);
			CHECK(bool(result["valid"]) == (mode != 1));
			CHECK(Array(result["warnings"]).size() == parser.get_warnings().size());
			const int count = parser.get_errors().size();
			analyzer.analyze();
			CHECK(parser.get_errors().size() == count);
			CHECK(parser.get_warnings().size() == (mode == 2 ? 1 : 0));
		}
	}
	TEST_CASE("deep_external_parent_identity_and_repeated_failure") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState registry;
		WarningScope warnings;
		const String provider = storage.path("declaring.notest.barista");
		BS_TEST_REQUIRE(write_bytes(provider, bytes("extends RefCounted\nfinal func f(_x: int = 1) -> int:\n\treturn 1\n")));
		const String source = "class Parent extends \"" + provider + "\":\n\tpass\nclass Child extends Parent:\n\tfunc f() -> int:\n\t\treturn 2\n";
		const String path = storage.path("external.barista");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		BS_TEST_REQUIRE(parser.get_errors().size() == 2);
		auto *function = parser.get_tree()->get_member("Child").m_class->get_member("f").function;
		error_at(parser, 0, "Cannot override final function \"f()\" declared in \"declaring.notest.barista\".", function);
		error_at(parser, 1, "The function signature doesn't match the parent. Parent signature is \"f(int = <default>) -> int\".", function);
		const auto retained = parser.get_depended_parser_for(provider);
		BS_TEST_REQUIRE(retained.is_valid());
		CHECK(retained->get_parser()->get_errors().is_empty());
		const auto status = retained->get_status();
		CHECK(analyzer.analyze() != OK);
		CHECK(parser.get_errors().size() == 2);
		CHECK(retained->get_status() == status);
		CHECK(public_block(source, path, parser) == ">> ERROR at line 4: Cannot override final function \"f()\" declared in \"declaring.notest.barista\".\n>> ERROR at line 4: The function signature doesn't match the parent. Parent signature is \"f(int = <default>) -> int\".");
	}
	TEST_CASE("strict_dynamic_parent_parameter_profile") {
		const String source = "class Parent:\n\tfunc f(_x) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f(_x: Node) -> void:\n\t\tpass\n";
		contract(source);
		// Pin skips unset parameter types during signature resolution; body inference is later.
		contract(source, {}, false, true);
	}
	TEST_CASE("parent_state_isolation") {
		CHECK(verify_case_isolation([]() {
			contract("class Parent:\n\tfunc f(_x: int) -> void:\n\t\tpass\nclass Child extends Parent:\n\tfunc f() -> void:\n\t\tpass\n", { { "The function signature doesn't match the parent. Parent signature is \"f(int) -> void\"." } });
		}));
	}
	TEST_CASE("retained_broken_parent_reports_once_per_consumer") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState registry;
		WarningScope warnings;
		const String provider = storage.path("broken.notest.barista");
		BS_TEST_REQUIRE(write_bytes(provider, bytes("extends RefCounted\nfunc f(_x: int = \"bad\") -> int:\n\treturn 1\n")));
		const String source = "extends \"" + provider + "\"\nfunc f() -> int:\n\treturn 2\n";
		BSParser first, second;
		BSAnalyzer a(&first), b(&second);
		BS_TEST_REQUIRE(first.parse(source, storage.path("first.barista"), false) == OK);
		BS_TEST_REQUIRE(second.parse(source, storage.path("second.barista"), false) == OK);
		BS_TEST_REQUIRE(a.resolve_inheritance() == OK);
		BS_TEST_REQUIRE(b.resolve_inheritance() == OK);
		for (BSAnalyzer *analyzer : { &a, &b }) {
			auto &parser = analyzer == &a ? first : second;
			CHECK(analyzer->resolve_interface() != OK);
			diagnostics(parser);
			const auto retained = parser.get_depended_parser_for(provider);
			BS_TEST_REQUIRE(retained.is_valid());
			const auto *owner = retained->get_parser();
			BS_TEST_REQUIRE(owner->get_errors().size() == 1);
			error_at(*owner, 0, "Cannot assign a value of type String to parameter \"_x\" with specified type int.", owner->get_tree()->get_member("f").function->parameters[0]->initializer);
			const int count = parser.get_errors().size();
			BS_TEST_REQUIRE(count == 2);
			error_at(parser, 0, "Could not resolve class \"broken.notest.barista\".", parser.get_tree());
			error_at(parser, 1, "Could not resolve external class member \"f\".", parser.get_tree()->get_member("f").function);
			for (const auto &error : parser.get_errors())
				CHECK_FALSE(error.message.begins_with("The function signature"));
			CHECK(analyzer->resolve_interface() != OK);
			CHECK(parser.get_errors().size() == count);
			CHECK(owner->get_errors().size() == 1);
		}
		CHECK(first.get_depended_parser_for(provider) == second.get_depended_parser_for(provider));
	}
	TEST_CASE("visible_witness_parent_and_hidden_opposite") {
		for (bool visible : { false, true }) {
			StorageFixture storage;
			BSConformanceRegistry::ScopedCorpusState registry;
			WarningScope warnings;
			const String provider = storage.path("witness.notest.barista");
			BS_TEST_REQUIRE(write_bytes(provider, bytes("trait Reader:\n\tabstract func f(_x: int) -> int\nextend RefCounted uses Reader:\n\tfunc f(_x: int) -> int:\n\t\treturn 1\n")));
			Error err = OK;
			const auto registered = BSCache::get_parser(provider, BSParserRef::INTERFACE_SOLVED, err);
			BS_TEST_REQUIRE(err == OK && registered.is_valid());
			const String source = String("extends RefCounted\n") + (visible ? "const Provider = preload(\"" + provider + "\")\n" : "") + "func f() -> int:\n\treturn 2\n";
			const String path = storage.path("witness_consumer.barista");
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == !visible);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (visible ? 1 : 0));
			if (visible)
				error_at(parser, 0, "The function signature doesn't match the parent. Parent signature is \"f(int) -> int\".", parser.get_tree()->get_member("f").function);
			CHECK(parser.get_depended_parsers().has(provider) == visible);
			CHECK(registered->get_parser()->get_errors().is_empty());
		}
	}
}
