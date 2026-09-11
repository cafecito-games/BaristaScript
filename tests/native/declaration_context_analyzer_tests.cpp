/**************************************************************************/
/*  declaration_context_analyzer_tests.cpp                                */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"
#include "bs_conformance_registry.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <functional>
#include <godot_cpp/classes/project_settings.hpp>
#include <string>
using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;
namespace {
struct TestParser : BSParser {
	using BSParser::get_allocated_nodes;
};
struct Expected {
	const char *message;
	int line;
	BSParser::Node::Type kind;
	const char *name = "";
};
struct WarningLevel {
	String key;
	Variant previous;
	WarningLevel(BSWarning::Code code) : key(BSWarning::get_setting_path_from_code(code)) {
		auto *settings = ProjectSettings::get_singleton();
		previous = settings->get_setting(key, Variant());
		settings->set_setting(key, BSWarning::WARN);
		BSParser::update_project_settings();
	}
	~WarningLevel() {
		auto *settings = ProjectSettings::get_singleton();
		settings->set_setting(key, previous);
		BSParser::update_project_settings();
		if (previous.get_type() == Variant::NIL && settings->has_setting(key))
			settings->clear(key);
	}
};
void check_warning(const BSParser &parser, BSWarning::Code code, const String &message, const BSParser::Node *node, int count) {
	int found = 0;
	for (const auto &warning : parser.get_warnings()) {
		if (warning.code != code)
			continue;
		++found;
		BS_TEST_REQUIRE(node != nullptr);
		CHECK(warning.get_message() == message);
		CHECK(warning.start_line == node->start_line);
		CHECK(warning.start_column == node->start_column);
		CHECK(warning.end_line == node->end_line);
		CHECK(warning.end_column == node->end_column);
	}
	CHECK(found == count);
}
const BSParser::Node *origin(const TestParser &parser, const Expected &expected) {
	const BSParser::Node *found = nullptr;
	for (auto *node = parser.get_allocated_nodes(); node; node = node->next) {
		if (node->type != expected.kind || node->start_line != expected.line)
			continue;
		if (expected.kind == BSParser::Node::IDENTIFIER && static_cast<const BSParser::IdentifierNode *>(node)->name != StringName(expected.name))
			continue;
		if (expected.kind == BSParser::Node::CALL && static_cast<const BSParser::CallNode *>(node)->function_name != StringName(expected.name))
			continue;
		if (expected.kind == BSParser::Node::PARAMETER && static_cast<const BSParser::ParameterNode *>(node)->identifier->name != StringName(expected.name))
			continue;
		CHECK(found == nullptr);
		found = node;
	}
	if (found && expected.kind == BSParser::Node::PARAMETER)
		return static_cast<const BSParser::ParameterNode *>(found)->initializer;
	return found;
}
void analyze(const String &source, std::initializer_list<Expected> expected = {}, const String &name = "context.barista", const std::function<void(const TestParser &)> &inspect = {}) {
	StorageFixture storage;
	BSConformanceRegistry::ScopedCorpusState registry;
	TestParser parser;
	BS_TEST_REQUIRE(parser.parse(source, storage.path(name), false) == OK);
	BSAnalyzer analyzer(&parser);
	CHECK((analyzer.analyze() == OK) == (expected.size() == 0));
	for (const auto &e : parser.get_errors())
		MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column, "-", e.end_line, ":", e.end_column);
	CHECK(parser.get_errors().size() == int(expected.size()));
	auto *entry = parser.get_errors().front();
	for (const auto &item : expected) {
		const auto *node = origin(parser, item);
		BS_TEST_REQUIRE(node != nullptr);
		if (!entry)
			continue;
		const auto &error = entry->get();
		CHECK(error.message == String(item.message));
		CHECK(error.line == item.line);
		CHECK(error.column == node->start_column);
		CHECK(error.end_line == node->end_line);
		CHECK(error.end_column == node->end_column);
		entry = entry->next();
	}
	if (inspect)
		inspect(parser);
	const int count = parser.get_errors().size();
	CHECK((analyzer.analyze() == OK) == (expected.size() == 0));
	CHECK(parser.get_errors().size() == count);
}
} // namespace
TEST_SUITE("declaration_context_analyzer") {
	TEST_CASE("original_inferring_with_weak_type_parameter") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/inferring_with_weak_type_parameter.fs; source SHA256 3bb73541fa942b32e7c22164b87ebc0a797d71cfd43a50d4e56564cf703f2c0e.
		analyze("func check(untyped = 1, inferred := untyped):\n\tpass\n\nfunc test():\n\tcheck()\n", { { "Cannot infer the type of \"inferred\" parameter because the value doesn't have a set type.", 1, BSParser::Node::PARAMETER, "inferred" } }, "inferring_with_weak_type_parameter.barista");
	}
	TEST_CASE("original_property_function_get_type_error") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/property_function_get_type_error.fs; source SHA256 5985922e2409cd2eb0fae02ee816718f4ee8198da2b4560f89777ae866342a0f.
		analyze("var _prop: int\n\n# Getter function has wrong return type.\nvar prop: String:\n\tget = get_prop\n\nfunc get_prop():\n\treturn _prop\n\nfunc test():\n\tpass\n", { { "Function with return type \"int\" cannot be used as getter for a property of type \"String\".", 4, BSParser::Node::VARIABLE, "" } }, "property_function_get_type_error.barista");
	}
	TEST_CASE("original_property_function_set_type_error") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/property_function_set_type_error.fs; source SHA256 aee91ec8305661ff88f0d3986787432e9bb5c64717091e49d6bf29ee0026ab04.
		analyze("var _prop: int\n\n# Setter function has wrong argument type.\nvar prop: String:\n\tset = set_prop\n\nfunc set_prop(value: int):\n\t_prop = value\n\nfunc test():\n\tpass\n", { { "Function with argument type \"int\" cannot be used as setter for a property of type \"String\".", 4, BSParser::Node::VARIABLE, "" } }, "property_function_set_type_error.barista");
	}
	TEST_CASE("original_property_inline_get_type_error") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/property_inline_get_type_error.fs; source SHA256 11e7ef8610b3aef81b0bf13445b923aa9c223423320eac19fe95fc7a47de3f6c.
		analyze("var _prop: int\n\n# Inline getter returns int instead of String.\nvar prop: String:\n\tget:\n\t\treturn _prop\n\nfunc test():\n\tpass\n", { { "Cannot return value of type \"int\" because the function return type is \"String\".", 6, BSParser::Node::RETURN, "" } }, "property_inline_get_type_error.barista");
	}
	TEST_CASE("original_property_inline_set_type_error") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/property_inline_set_type_error.fs; source SHA256 d642c5f293d6781417ef80bd45f625ff005559b41469792b67d57b9f86b7c555.
		analyze("var _prop: int\n\n# Inline setter assigns String to int.\nvar prop: String:\n\tset(value):\n\t\t_prop = value\n\nfunc test():\n\tpass\n", { { "Value of type \"String\" cannot be assigned to a variable of type \"int\".", 6, BSParser::Node::IDENTIFIER, "value" } }, "property_inline_set_type_error.barista");
	}
	TEST_CASE("original_named_tuple_export_rejected") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/named_tuple_export_rejected.fs; source SHA256 fd40928230efc04f2b0f2ec9a0eeb68b464b484f8ac0ba42eff13252a6a6e202.
		analyze("extends Node\n\ntuple Vec2(x: float, y: float)\n\n@export var position: Vec2 = Vec2(1.0, 2.0)\n\nfunc test():\n\tprint(position)\n", { { "Cannot export a tuple-typed property: \"position\" has type \"Vec2\".", 5, BSParser::Node::ANNOTATION, "" } }, "named_tuple_export_rejected.barista");
	}
	TEST_CASE("original_static_func_access_non_static") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/static_func_access_non_static.fs; source SHA256 db24a372ae8ec905d49ccf55b3c3105e972c82436252154154567b599722a20d.
		analyze("# GH-91403\n\nstatic func static_func():\n\tprint(non_static_func)\n\nfunc non_static_func():\n\tpass\n\nfunc test():\n\tpass\n", { { "Cannot access non-static function \"non_static_func\" from the static function \"static_func()\".", 4, BSParser::Node::IDENTIFIER, "non_static_func" } }, "static_func_access_non_static.barista");
	}
	TEST_CASE("original_static_func_access_non_static_in_lambda_param") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/static_func_access_non_static_in_lambda_param.fs; source SHA256 777a2723fa20499ed64f705be82ac767ed35697a7704f54ed332332f17ce0883.
		analyze("# GH-91403\n\nfunc non_static_func():\n\tpass\n\nstatic func static_func(\n\t\tf := func ():\n\t\t\tvar g := func ():\n\t\t\t\tprint(non_static_func)\n\t\t\tg.call()\n):\n\tf.call()\n\nfunc test():\n\tpass\n", { { "Cannot access non-static function \"non_static_func\" from the static function \"static_func()\".", 9, BSParser::Node::IDENTIFIER, "non_static_func" } }, "static_func_access_non_static_in_lambda_param.barista");
	}
	TEST_CASE("original_static_func_call_non_static") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/static_func_call_non_static.fs; source SHA256 8a2a68b42bb5cf82de0c920fe5b36be658265755718d5dbe96a119abd81a1e7a.
		analyze("static func static_func():\n\tnon_static_func()\n\nfunc non_static_func():\n\tpass\n\nfunc test():\n\tpass\n", { { "Cannot call non-static function \"non_static_func()\" from the static function \"static_func()\".", 2, BSParser::Node::CALL, "non_static_func" } }, "static_func_call_non_static.barista");
	}
	TEST_CASE("original_static_func_call_non_static_in_lambda") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/static_func_call_non_static_in_lambda.fs; source SHA256 b38575793ab17af92c0cdc599579857062004425a32348857eb5ed4b77338594.
		analyze("# GH-83468\n\nfunc non_static_func():\n\tpass\n\nstatic func static_func():\n\tvar f := func ():\n\t\tvar g := func ():\n\t\t\tnon_static_func()\n\t\tg.call()\n\tf.call()\n\nfunc test():\n\tpass\n", { { "Cannot call non-static function \"non_static_func()\" from the static function \"static_func()\".", 9, BSParser::Node::CALL, "non_static_func" } }, "static_func_call_non_static_in_lambda.barista");
	}
	TEST_CASE("original_static_func_call_non_static_in_lambda_param") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/static_func_call_non_static_in_lambda_param.fs; source SHA256 7c53e78dcd77365a0bbb3712fa0f5bfa4ddbdc77f35ce1c58316f5e9978a0cf8.
		analyze("# GH-83468\n\nfunc non_static_func():\n\tpass\n\nstatic func static_func(\n\t\tf := func ():\n\t\t\tvar g := func ():\n\t\t\t\tnon_static_func()\n\t\t\tg.call()\n):\n\tf.call()\n\nfunc test():\n\tpass\n", { { "Cannot call non-static function \"non_static_func()\" from the static function \"static_func()\".", 9, BSParser::Node::CALL, "non_static_func" } }, "static_func_call_non_static_in_lambda_param.barista");
	}
	TEST_CASE("original_static_var_init_access_non_static_in_lambda") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/static_var_init_access_non_static_in_lambda.fs; source SHA256 b0898b68b933773540bbe5d8b9a5674f67372460bf94b5060f11bd14b9851406.
		analyze("# GH-91403\n\nfunc non_static_func():\n\tpass\n\nstatic var static_var = func ():\n\tvar f := func ():\n\t\tvar g := func ():\n\t\t\tprint(non_static_func)\n\t\tg.call()\n\tf.call()\n\nfunc test():\n\tpass\n", { { "Cannot access non-static function \"non_static_func\" from a static variable initializer.", 9, BSParser::Node::IDENTIFIER, "non_static_func" } }, "static_var_init_access_non_static_in_lambda.barista");
	}
	TEST_CASE("original_static_var_init_access_non_static_in_lambda_setter") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/static_var_init_access_non_static_in_lambda_setter.fs; source SHA256 58068b1c041fca5cc91f7138f8234770b998ce3693affe170a93008df1259613.
		analyze("# GH-91403\n\nfunc non_static_func():\n\tpass\n\nstatic var static_var:\n\tset(_value):\n\t\tvar f := func ():\n\t\t\tvar g := func ():\n\t\t\t\tprint(non_static_func)\n\t\t\tg.call()\n\t\tf.call()\n\nfunc test():\n\tpass\n", { { "Cannot access non-static function \"non_static_func\" from the static function \"@static_var_setter()\".", 10, BSParser::Node::IDENTIFIER, "non_static_func" } }, "static_var_init_access_non_static_in_lambda_setter.barista");
	}
	TEST_CASE("original_static_var_init_call_non_static_in_lambda") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/static_var_init_call_non_static_in_lambda.fs; source SHA256 03481227aa78ef2f50a6609ed97e7662acf4e42ad64a6430501ba649401befb0.
		analyze("# GH-83468\n\nfunc non_static_func():\n\tpass\n\nstatic var static_var = func ():\n\tvar f := func ():\n\t\tvar g := func ():\n\t\t\tnon_static_func()\n\t\tg.call()\n\tf.call()\n\nfunc test():\n\tpass\n", { { "Cannot call non-static function \"non_static_func()\" from a static variable initializer.", 9, BSParser::Node::CALL, "non_static_func" } }, "static_var_init_call_non_static_in_lambda.barista");
	}
	TEST_CASE("original_static_var_init_call_non_static_in_lambda_setter") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/static_var_init_call_non_static_in_lambda_setter.fs; source SHA256 afaf1287a6a42fb57fe17d682ea810ff8180f90d13ae9eb3784c4e04a45efff6.
		analyze("# GH-83468\n\nfunc non_static_func():\n\tpass\n\nstatic var static_var:\n\tset(_value):\n\t\tvar f := func ():\n\t\t\tvar g := func ():\n\t\t\t\tnon_static_func()\n\t\t\tg.call()\n\t\tf.call()\n\nfunc test():\n\tpass\n", { { "Cannot call non-static function \"non_static_func()\" from the static function \"@static_var_setter()\".", 10, BSParser::Node::CALL, "non_static_func" } }, "static_var_init_call_non_static_in_lambda_setter.barista");
	}
	TEST_CASE("original_static_var_init_non_static_access") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/static_var_init_non_static_access.fs; source SHA256 266bf2a1ec3b2a2d286667e6dbbfc7e4ebd0384e02efdbca3ea5fe811d87cb6d.
		analyze("# GH-91403\n\n@static_unload\n\nfunc non_static():\n\treturn \"non static\"\n\nstatic var static_var = Callable(non_static)\n\nfunc test():\n\tprint(\"does not run\")\n", { { "Cannot access non-static function \"non_static\" from a static variable initializer.", 8, BSParser::Node::IDENTIFIER, "non_static" } }, "static_var_init_non_static_access.barista");
	}
	TEST_CASE("original_static_var_init_non_static_call") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/static_var_init_non_static_call.fs; source SHA256 993cdf799a1a057bb504959cbb31497245a9111994a7e89acefe87a61a0e84f4.
		analyze("@static_unload\n\nfunc non_static():\n\treturn \"non static\"\n\nstatic var static_var = non_static()\n\nfunc test():\n\tprint(\"does not run\")\n", { { "Cannot call non-static function \"non_static()\" from a static variable initializer.", 6, BSParser::Node::CALL, "non_static" } }, "static_var_init_non_static_call.barista");
	}
	TEST_CASE("original_tagged_union_export_rejected") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/tagged_union_export_rejected.fs; source SHA256 943d6780a116f048f3913dad1763ecf4383abf1768e94d759221d6928c8b0a89.
		analyze("extends Node\n\nenum Message:\n\tQuit\n\tMove(x: int, y: int)\n\n@export var last_message: Message\n\nfunc test():\n\tprint(last_message)\n", { { "Cannot export a tagged-union-typed property: \"last_message\" has type \"tagged_union_export_rejected.barista.Message\".", 7, BSParser::Node::ANNOTATION, "" } }, "tagged_union_export_rejected.barista");
	}
	TEST_CASE("original_tagged_union_export_rejected_in_array") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/tagged_union_export_rejected_in_array.fs; source SHA256 8574a579483bebb0629d3ab912ee2be4b561bfbff827edd5f7e559198866f3fa.
		analyze("extends Node\n\nenum Message:\n\tQuit\n\tMove(x: int, y: int)\n\n@export var history: Array[Message] = []\n\nfunc test():\n\tprint(history)\n", { { "Cannot export a tagged-union-typed property: \"history\" has type \"Array[tagged_union_export_rejected_in_array.barista.Message]\".", 7, BSParser::Node::ANNOTATION, "" } }, "tagged_union_export_rejected_in_array.barista");
	}
	TEST_CASE("original_tagged_union_export_rejected_via_export_custom") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/tagged_union_export_rejected_via_export_custom.fs; source SHA256 fa9644ac4c97ef80e1ae577884a2a62e578c4f7553a8b0a25f86f8d02042579f.
		analyze("extends Node\n\nenum Message:\n\tQuit\n\tMove(x: int, y: int)\n\n@export_custom(PROPERTY_HINT_NONE, \"\") var last_message: Message\n\nfunc test():\n\tprint(last_message)\n", { { "Cannot export a tagged-union-typed property: \"last_message\" has type \"tagged_union_export_rejected_via_export_custom.barista.Message\".", 7, BSParser::Node::ANNOTATION, "" } }, "tagged_union_export_rejected_via_export_custom.barista");
	}
	TEST_CASE("original_tagged_union_export_rejected_via_variant_initializer") {
		// Foundry c9d5e35: modules/foundry_script/tests/scripts/analyzer/errors/tagged_union_export_rejected_via_variant_initializer.fs; source SHA256 12feff03c13a8ec73c90293f513816641441971afa7e1ef03ac8a812506609ba.
		analyze("extends Node\n\nenum Message:\n\tQuit\n\tMove(x: int, y: int)\n\n@export var last_message = Message.Quit\n\nfunc test():\n\tprint(last_message)\n", { { "Cannot export a tagged-union-typed property: \"last_message\" has type \"tagged_union_export_rejected_via_variant_initializer.barista.Message\".", 7, BSParser::Node::ANNOTATION, "" } }, "tagged_union_export_rejected_via_variant_initializer.barista");
	}
	TEST_CASE("named_getter_missing") { analyze("var p: int:\n\tget = absent\n", { { "Getter \"absent\" not found.", 1, BSParser::Node::VARIABLE, "" } }); }
	TEST_CASE("named_setter_missing") { analyze("var p: int:\n\tset = absent\n", { { "Setter \"absent\" not found.", 1, BSParser::Node::VARIABLE, "" } }); }
	TEST_CASE("named_getter_argument_rejected") { analyze("var p: int:\n\tget = getter\nfunc getter(_value: int) -> int:\n\treturn 1\n", { { "Function \"getter\" cannot be used as getter because of its signature.", 1, BSParser::Node::VARIABLE, "" } }); }
	TEST_CASE("named_getter_void_rejected") { analyze("var p: int:\n\tget = getter\nfunc getter() -> void:\n\tpass\n", { { "Function \"getter\" cannot be used as getter because of its signature.", 1, BSParser::Node::VARIABLE, "" } }); }
	TEST_CASE("named_setter_arity_rejected") { analyze("var p: int:\n\tset = setter\nfunc setter():\n\tpass\n", { { "Function \"setter\" cannot be used as setter because of its signature.", 1, BSParser::Node::VARIABLE, "" } }); }
	TEST_CASE("named_compatible_accessors") { analyze("var p: int:\n\tget = getter, set = setter\nfunc getter() -> int:\n\treturn 1\nfunc setter(value: int):\n\tprint(value)\n", {}); }
	TEST_CASE("named_variant_pair_mismatch") { analyze("var p:\n\tget = getter, set = setter\nfunc getter():\n\treturn 1\nfunc setter(value: String):\n\tprint(value)\n", { { "Getter with type \"int\" cannot be used along with setter of type \"String\".", 1, BSParser::Node::VARIABLE, "" } }); }
	TEST_CASE("inline_typed_accessors") { analyze("var p: int:\n\tget:\n\t\treturn 1\n\tset(value):\n\t\tprint(value + 1)\n", {}); }
	TEST_CASE("inline_inferred_property_type") { analyze("var p := 1:\n\tset(value):\n\t\tvar text: String = value\n\t\tprint(text)\n", { { "Cannot assign a value of type int to variable \"text\" with specified type String.", 3, BSParser::Node::IDENTIFIER, "value" } }); }
	TEST_CASE("inline_getter_flow_once") { analyze("var p: int:\n\tget:\n\t\tpass\n", { { "Not all code paths return a value.", 2, BSParser::Node::FUNCTION, "" } }); }
	TEST_CASE("static_variable_and_signal_reads") { analyze("signal ping\nvar field: int\nstatic func f():\n\tprint(field)\n\tprint(ping)\n", { { "Cannot access non-static variable \"field\" from the static function \"f()\".", 4, BSParser::Node::IDENTIFIER, "field" }, { "Cannot access signal \"ping\" from the static function \"f()\".", 5, BSParser::Node::IDENTIFIER, "ping" } }); }
	TEST_CASE("static_default_direct_call") { analyze("func instance():\n\tpass\nstatic func f(value = instance()):\n\tprint(value)\n", { { "Cannot call non-static function \"instance()\" from the static function \"f()\".", 3, BSParser::Node::CALL, "instance" } }); }
	TEST_CASE("static_default_direct_read") { analyze("var field: int\nstatic func f(value = field):\n\tprint(value)\n", { { "Cannot access non-static variable \"field\" from the static function \"f()\".", 2, BSParser::Node::IDENTIFIER, "field" } }); }
	TEST_CASE("static_native_call_and_read") { analyze("extends Node\nstatic func f():\n\tget_name()\n\tprint(name)\n", { { "Cannot call non-static function \"get_name()\" from the static function \"f()\".", 3, BSParser::Node::CALL, "get_name" }, { "Cannot access non-static variable \"name\" from the static function \"f()\".", 4, BSParser::Node::IDENTIFIER, "name" } }); }
	TEST_CASE("static_controls_and_restoration") { analyze("var field: int\nstatic var count: int\nstatic func helper():\n\tpass\nstatic func f():\n\thelper()\n\tprint(count)\nfunc instance(value = field):\n\tprint(value)\n\tprint(field)\n", {}); }
	TEST_CASE("instance_lambda_capture") { analyze("var field: int\nfunc instance():\n\tpass\nfunc f():\n\tvar callback := func():\n\t\tinstance()\n\t\tprint(field)\n\tcallback.call()\n", {}); }
	TEST_CASE("weak_default_remains_weak") { analyze("func f(untyped = 1):\n\tprint(untyped)\n", {}); }
	TEST_CASE("hard_inferred_default") { analyze("func f(typed: int = 1, inferred := typed):\n\tprint(inferred)\n", {}); }
	TEST_CASE("literal_inferred_default") { analyze("func f(inferred := 1):\n\tprint(inferred)\n", {}); }
	TEST_CASE("export_tuple_array") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export var p: Array[Pair]\n", { { "Cannot export a tuple-typed property: \"p\" has type \"Array[Pair]\".", 6, BSParser::Node::ANNOTATION, "" } }); }
	TEST_CASE("export_tuple_key") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export var p: Dictionary[Pair, int]\n", { { "Cannot export a tuple-typed property: \"p\" has type \"Dictionary[Pair, int]\".", 6, BSParser::Node::ANNOTATION, "" } }); }
	TEST_CASE("export_tuple_value") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export var p: Dictionary[int, Pair]\n", { { "Cannot export a tuple-typed property: \"p\" has type \"Dictionary[int, Pair]\".", 6, BSParser::Node::ANNOTATION, "" } }); }
	TEST_CASE("export_tagged_key") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export var p: Dictionary[Message, int]\n", { { "Cannot export a tagged-union-typed property: \"p\" has type \"Dictionary[context.barista.Message, int]\".", 6, BSParser::Node::ANNOTATION, "" } }); }
	TEST_CASE("export_tagged_value") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export var p: Dictionary[int, Message]\n", { { "Cannot export a tagged-union-typed property: \"p\" has type \"Dictionary[int, context.barista.Message]\".", 6, BSParser::Node::ANNOTATION, "" } }); }
	TEST_CASE("export_nested") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export var p: Array[Dictionary[int, Pair]]\n", { { "Cannot export a tuple-typed property: \"p\" has type \"Array[Dictionary[int, Pair]]\".", 6, BSParser::Node::ANNOTATION, "" } }); }
	TEST_CASE("export_storage_tuple") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export_storage var p: Pair\n", {}); }
	TEST_CASE("export_storage_tagged") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export_storage var p: Message\n", {}); }
	TEST_CASE("export_storage_container") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export_storage var p: Array[Message]\n", {}); }
	TEST_CASE("export_custom_tuple") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export_custom(PROPERTY_HINT_NONE, \"\") var p: Pair\n", {}); }
	TEST_CASE("export_custom_array") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export_custom(PROPERTY_HINT_NONE, \"\") var p: Array[Message]\n", {}); }
	TEST_CASE("export_custom_dictionary") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export_custom(PROPERTY_HINT_NONE, \"\") var p: Dictionary[int, Pair]\n", {}); }
	TEST_CASE("export_variant_tuple") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export var p: Variant = Pair(1, 2)\n", { { "Cannot export a tuple-typed property: \"p\" has type \"Pair\".", 6, BSParser::Node::ANNOTATION, "" } }); }
	TEST_CASE("export_tagged_metatype") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export var p = Message\n", {}); }
	TEST_CASE("named_variant_pair_compatible") { analyze("var p:\n\tget = getter, set = setter\nfunc getter():\n\treturn 1\nfunc setter(value: int):\n\tprint(value)\n", {}, "context.barista"); }
	TEST_CASE("named_setter_two_parameters") { analyze("var p: int:\n\tset = setter\nfunc setter(value: int, extra: int = 1):\n\tprint(value, extra)\n", { { "Function \"setter\" cannot be used as setter because of its signature.", 1, BSParser::Node::VARIABLE, "" } }, "context.barista"); }
	TEST_CASE("setter_destination_accepts_subtype") { analyze("var p: Object:\n\tset = setter\nfunc setter(value: Node):\n\tprint(value)\n", {}, "context.barista"); }
	TEST_CASE("setter_native_runtime_downcast_permitted") { analyze("var p: Node:\n\tset = setter\nfunc setter(value: Object):\n\tprint(value)\n", {}, "context.barista"); }
	TEST_CASE("named_numeric_narrowing_uses_property_origin") {
		WarningLevel level(BSWarning::NARROWING_CONVERSION);
		for (bool getter : { false, true })
			for (bool narrowing : { false, true }) {
				const String property = (getter == narrowing) ? "int" : "float";
				const String other = property == "int" ? "float" : "int";
				const String source = getter ? "var p: " + property + ":\n\tget = getter\nfunc getter() -> " + other + ":\n\treturn " + (other == "int" ? "1" : "1.0") + "\n" : "var p: " + property + ":\n\tset = setter\nfunc setter(value: " + other + "):\n\tprint(value)\n";
				// GRAMMAR 6.1 D1: float-to-int requires a cast or proven constant; accessor signatures supply no value.
				// Preserve the pin's compatibility direction and its warning only after that shared admission gate.
				const String message = getter ? "Function with return type \"float\" cannot be used as getter for a property of type \"int\"." : "Function with argument type \"float\" cannot be used as setter for a property of type \"int\".";
				const auto inspect = [&](const TestParser &parser) {
					check_warning(parser, BSWarning::NARROWING_CONVERSION, "Narrowing conversion (float is converted to int and loses precision).", parser.get_tree()->get_member("p").variable, !getter && narrowing ? 1 : 0);
				};
				if (property == "int")
					analyze(source, { { message.utf8().get_data(), 1, BSParser::Node::VARIABLE, "" } }, "context.barista", inspect);
				else
					analyze(source, {}, "context.barista", inspect);
			}
	}
	TEST_CASE("static_explicit_instance_and_shadowed_sources") {
		analyze("var field: int\nfunc method():\n\tpass\nstatic func f(object: Object, field: int, method: Callable):\n\tobject.get_class()\n\tprint(field)\n\tmethod.call()\n", {}, "context.barista", [](const TestParser &parser) {
			auto *f = parser.get_tree()->get_member("f").function;
			CHECK(f->resolved_body);
			const auto *read = origin(parser, { "", 6, BSParser::Node::IDENTIFIER, "field" });
			BS_TEST_REQUIRE(read);
			CHECK(static_cast<const BSParser::IdentifierNode *>(read)->source == BSParser::IdentifierNode::FUNCTION_PARAMETER);
		});
	}
	TEST_CASE("static_native_signal_and_function_sources") { analyze("extends Node\nstatic func f():\n\tprint(ready)\n\tprint(get_name)\n", { { "Cannot access signal \"ready\" from the static function \"f()\".", 3, BSParser::Node::IDENTIFIER, "ready" }, { "Cannot access non-static function \"get_name\" from the static function \"f()\".", 4, BSParser::Node::IDENTIFIER, "get_name" } }, "context.barista"); }
	TEST_CASE("static_inherited_variable") { analyze("class Parent:\n\tvar field: int\nclass Child extends Parent:\n\tstatic func f():\n\t\tprint(field)\n", { { "Cannot access non-static variable \"field\" from the static function \"f()\".", 5, BSParser::Node::IDENTIFIER, "field" } }, "context.barista"); }
	TEST_CASE("instance_nested_lambda_capture_and_restore") {
		analyze("var field: int\nstatic var count: int\nfunc method():\n\tpass\nstatic func before():\n\tprint(count)\nfunc f():\n\tvar callback := func():\n\t\tvar inner := func():\n\t\t\tmethod()\n\t\t\tprint(field)\n\t\tinner.call()\n\tcallback.call()\n", {}, "context.barista", [](const TestParser &parser) {
			int lambdas = 0;
			for (auto *node = parser.get_allocated_nodes(); node; node = node->next)
				if (node->type == BSParser::Node::LAMBDA) {
					auto *lambda = static_cast<const BSParser::LambdaNode *>(node);
					CHECK(lambda->function->resolved_body);
					CHECK_FALSE(lambda->function->is_static);
					CHECK(lambda->use_self);
					++lambdas;
				}
			CHECK(lambdas == 2);
		});
	}
	TEST_CASE("reentrant_default_restores_static_initializer") { analyze("class_name Context\nvar field: int\nstatic var result = [Context.new().later(), field]\nfunc later(callback := func(): return field):\n\treturn callback.call()\n", { { "Cannot access non-static variable \"field\" from a static variable initializer.", 3, BSParser::Node::IDENTIFIER, "field" } }, "context.barista"); }
	TEST_CASE("reentrant_default_restores_static_function") { analyze("class_name Context\nvar field: int\nstatic func first(object: Context):\n\tobject.later()\n\tprint(field)\nfunc later(callback := func(): return field):\n\treturn callback.call()\nfunc after():\n\tprint(field)\n", { { "Cannot access non-static variable \"field\" from the static function \"first()\".", 5, BSParser::Node::IDENTIFIER, "field" } }, "context.barista"); }
	TEST_CASE("inferred_null_parameter") { analyze("func f(value := null):\n\tprint(value)\n", { { "Cannot infer the type of \"value\" parameter because the value is \"null\".", 1, BSParser::Node::PARAMETER, "value" } }, "context.barista"); }
	TEST_CASE("published_hard_and_weak_parameter_defaults") {
		analyze("var saved = f\nfunc f(weak = 1, hard := 2, next := hard):\n\tprint(weak, next)\n", {}, "context.barista", [](const TestParser &parser) {
			auto *f = parser.get_tree()->get_member("f").function;
			BS_TEST_REQUIRE(f && f->parameters.size() == 3 && f->info.arguments.size() == 3 && f->default_arg_values.size() == 3);
			CHECK(f->parameters[0]->get_datatype().type_source == BSParser::DataType::INFERRED);
			CHECK(f->parameters[1]->get_datatype().type_source == BSParser::DataType::ANNOTATED_INFERRED);
			CHECK(f->parameters[2]->get_datatype().type_source == BSParser::DataType::ANNOTATED_INFERRED);
			CHECK(f->info.arguments[1].type == Variant::INT);
			CHECK(f->info.arguments[2].type == Variant::INT);
			CHECK(f->default_arg_values[0] == Variant(1));
			CHECK(f->default_arg_values[1] == Variant(2));
			CHECK(f->default_arg_values[2].get_type() == Variant::NIL);
			CHECK(f->info.default_arguments.size() == 3);
			const auto callable = parser.get_tree()->get_member("saved").variable->get_datatype();
			CHECK(callable.has_method_signature);
			BS_TEST_REQUIRE(callable.method_parameter_types.size() == 3);
			CHECK(callable.method_parameter_types[0].type_source == BSParser::DataType::INFERRED);
			CHECK(callable.method_parameter_types[1].builtin_type == Variant::INT);
			CHECK(callable.method_parameter_types[1].is_hard_type());
			CHECK(callable.method_parameter_types[2].builtin_type == Variant::INT);
			CHECK(callable.method_parameter_types[2].is_hard_type());
		});
	}
	TEST_CASE("hard_variant_parameter_warns_without_weak_inference_error") {
		WarningLevel level(BSWarning::INFERENCE_ON_VARIANT);
		analyze("func f(hard: Variant = 1, inferred := hard):\n\tprint(inferred)\n", {}, "context.barista", [](const TestParser &parser) {
			auto *f = parser.get_tree()->get_member("f").function;
			BS_TEST_REQUIRE(f && f->parameters.size() == 2);
			auto *parameter = f->parameters[1];
			CHECK(parameter->get_datatype().is_variant());
			CHECK(parameter->get_datatype().is_hard_type());
			check_warning(parser, BSWarning::INFERENCE_ON_VARIANT, "The parameter type is being inferred from a Variant value, so it will be typed as Variant.", parameter, 1);
		});
	}
	TEST_CASE("export_first_leaf_tuple") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export var p: Dictionary[Pair, Message]\n", { { "Cannot export a tuple-typed property: \"p\" has type \"Dictionary[Pair, context.barista.Message]\".", 6, BSParser::Node::ANNOTATION, "" } }, "context.barista"); }
	TEST_CASE("export_first_leaf_tagged-union") { analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export var p: Dictionary[Message, Pair]\n", { { "Cannot export a tagged-union-typed property: \"p\" has type \"Dictionary[context.barista.Message, Pair]\".", 6, BSParser::Node::ANNOTATION, "" } }, "context.barista"); }
	TEST_CASE("export_custom_variant_tagged") {
		analyze("extends Node\ntuple Pair(x: int, y: int)\nenum Message:\n\tQuit\n\tMove(x: int)\n@export_custom(PROPERTY_HINT_NONE, \"\") var p: Variant = Message.Quit\n", { { "Cannot export a tagged-union-typed property: \"p\" has type \"context.barista.Message\".", 6, BSParser::Node::ANNOTATION, "" } }, "context.barista", [](const TestParser &parser) { CHECK_FALSE(parser.get_tree()->get_member("p").variable->exported); });
	}
	TEST_CASE("class_variable_inference_suppression_stays_scoped") {
		WarningLevel level(BSWarning::INFERENCE_ON_VARIANT);
		analyze("const VALUE: Variant = 1\n@warning_ignore(\"inference_on_variant\")\nvar ignored := VALUE\nvar inferred := VALUE\n", {}, "context.barista", [](const TestParser &parser) {
			check_warning(parser, BSWarning::INFERENCE_ON_VARIANT, "The variable type is being inferred from a Variant value, so it will be typed as Variant.", parser.get_tree()->get_member("inferred").variable, 1);
		});
	}
}
