/**************************************************************************/
/*  abstract_contract_analyzer_tests.cpp                                  */
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
void original(const String &name, const String &source, const String &expected, bool mixed) {
	StorageFixture storage;
	BSConformanceRegistry::ScopedCorpusState registry;
	WarningScope warnings;
	const String path = storage.path(name);
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
	BSAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
	diagnostics(parser);
	CHECK(public_block(source, path, parser) == expected);
	CHECK(parser.get_warnings().is_empty());
	const PackedStringArray lines = expected.split("\n");
	const int order[] = { 6, 0, 1, 2, 3, 4, 5, 7 };
	BS_TEST_REQUIRE(parser.get_errors().size() == (mixed ? 8 : 1));
	int index = 0;
	for (const auto &e : parser.get_errors()) {
		const String line = lines[mixed ? order[index] : 0];
		CHECK(vformat(">> ERROR at line %d: %s", e.line, e.message) == line);
		++index;
	}
	if (!mixed)
		error_at(parser, 0, lines[0].substr(lines[0].find(": ") + 2), parser.get_tree());
	if (mixed) {
		const int positions[][4] = { { 32, 56, 32, 63 }, { 11, 1, 12, 31 }, { 14, 1, 15, 10 }, { 17, 1, 18, 10 }, { 22, 9, 22, 16 }, { 25, 9, 25, 26 }, { 30, 9, 30, 14 }, { 33, 32, 33, 39 } };
		int position = 0;
		for (const auto &error : parser.get_errors()) {
			CHECK(error.line == positions[position][0]);
			CHECK(error.column == positions[position][1]);
			CHECK(error.end_line == positions[position][2]);
			CHECK(error.end_column == positions[position][3]);
			++position;
		}
	}
	CHECK(analyzer.get_highest_completed_phase() < BSAnalyzer::AnalyzerPhase::FLOW_FINALITY_INVARIANTS);
	const int previous = parser.get_errors().size();
	CHECK(analyzer.analyze() != OK);
	CHECK(parser.get_errors().size() == previous);
}
bool seed(StorageFixture &storage, const String &path, const String &source) {
	BSParser parser;
	if (parser.parse(source, path, false) != OK) {
		diagnostics(parser);
		return false;
	}
	BSCache::set_source_override(path, source);
	const auto record = BSDeclarationIndex::record_from_global_class(path, source, bs_resolve_global_class_from_source(source, path));
	return storage.index().commit_record(storage.index().claim_refresh(path), record);
}
} //namespace
TEST_SUITE("abstract_contract_analyzer") {
	TEST_CASE("original_abstract_method_in_non_abstract_head") { original("abstract_method_in_non_abstract_head.barista", "# Edge case: a file with neither `class_name` nor `extends` has no head\n# declaration that `abstract` can prefix, so a leading `abstract` here is an\n# ordinary declaration modifier on the function. The implicit head class stays\n# non-abstract, so declaring an abstract method in it is rejected. To make the\n# head abstract you must add an explicit `abstract extends ...` (or\n# `abstract class_name ...`).\nabstract func describe() -> String\n", ">> ERROR at line 1: Class \"abstract_method_in_non_abstract_head.barista\" is not abstract but contains abstract methods. Mark the class as \"abstract\" or remove \"abstract\" from all methods in this class.", false); }
	TEST_CASE("original_abstract_methods") { original("abstract_methods.barista", "abstract class AbstractClass:\n\tabstract func some_func()\n\nclass ImplementedClass extends AbstractClass:\n\tfunc some_func():\n\t\tpass\n\nabstract class AbstractClassAgain extends ImplementedClass:\n\tabstract func some_func()\n\nclass Test1:\n\tabstract func some_func()\n\nclass Test2 extends AbstractClass:\n\tpass\n\nclass Test3 extends AbstractClassAgain:\n\tpass\n\nclass Test4 extends AbstractClass:\n\tfunc some_func():\n\t\tsuper()\n\n\tfunc other_func():\n\t\tsuper.some_func()\n\nabstract class A:\n\t# An abstract function cannot have a body.\n\tabstract func abstract_bodyful():\n\t\tpass\n\nfunc holding_some_invalid_lambda(invalid_default_arg = func():):\n\tvar some_invalid_lambda = (func():)\n\nfunc test():\n\tpass\n", ">> ERROR at line 11: Class \"Test1\" is not abstract but contains abstract methods. Mark the class as \"abstract\" or remove \"abstract\" from all methods in this class.\n>> ERROR at line 14: Class \"Test2\" must implement \"AbstractClass.some_func()\" and other inherited abstract methods or be marked as \"abstract\".\n>> ERROR at line 17: Class \"Test3\" must implement \"AbstractClassAgain.some_func()\" and other inherited abstract methods or be marked as \"abstract\".\n>> ERROR at line 22: Cannot call the parent class' abstract function \"some_func()\" because it hasn't been defined.\n>> ERROR at line 25: Cannot call the parent class' abstract function \"some_func()\" because it hasn't been defined.\n>> ERROR at line 30: An abstract function cannot have a body.\n>> ERROR at line 32: A lambda function must have a \":\" followed by a body.\n>> ERROR at line 33: A lambda function must have a \":\" followed by a body.", true); }

	TEST_CASE("abstract_heads_and_concrete_implementations_are_valid") {
		for (const char *source : { "abstract extends RefCounted\nabstract func read() -> int\n", "abstract class Base:\n\tabstract func read() -> int\nabstract class Deferred extends Base:\n\tpass\nclass Concrete extends Base:\n\tfunc read() -> int:\n\t\treturn 1\nclass Inherited extends Concrete:\n\tpass\nfunc test(value: Base):\n\tvar result: int = value.read()\n" }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			const String path = storage.path("valid_classes.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			CHECK(parser.get_errors().is_empty());
			CHECK(parser.get_warnings().is_empty());
			CHECK(public_block(source, path, parser) == "BS_TEST_OK");
		}
	}
	TEST_CASE("super_uses_actual_parent_signature_and_named_parameters") {
		for (bool named : { false, true }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			const String source = String("class Base:\n\tfunc create(parent_value: int = 1) -> Node:\n\t\treturn null\nclass Child extends Base:\n\tfunc create(value: int = 2) -> Control:\n\t\tvar inherited: Node = ") + (named ? "super.create(parent_value = 3)" : "super(parent_value = 3)") + "\n\t\treturn null\n";
			const String path = storage.path("concrete_super.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			CHECK(parser.get_errors().is_empty());
			auto *child = parser.get_tree()->get_member("Child").m_class;
			BS_TEST_REQUIRE(child && child->has_function("create"));
			auto *variable = static_cast<BSParser::VariableNode *>(child->get_member("create").function->body->statements[0]);
			BS_TEST_REQUIRE(variable && variable->initializer);
			auto *call = static_cast<BSParser::CallNode *>(variable->initializer);
			CHECK(call->is_super);
			CHECK(call->get_datatype().native_type == SNAME("Node"));
			CHECK(call->resolved_parameter_types.size() == 1);
			CHECK(parser.get_warnings().is_empty());
			CHECK(public_block(source, path, parser) == "BS_TEST_OK");
		}
	}
	TEST_CASE("retained_external_abstract_parent_obligation_has_consumer_origin") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState registry;
		WarningScope warnings;
		const String provider = "res://tests/abstract_contract/base.barista";
		BS_TEST_REQUIRE(seed(storage, provider, "abstract class_name ExternalBase\nabstract func read() -> int\n"));
		const auto before = storage.index().get_refresh_revision(provider);
		const String source = "extends ExternalBase\n";
		const String path = storage.path("consumer.barista");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		BS_TEST_REQUIRE(parser.get_errors().size() == 1);
		error_at(parser, 0, "Class \"consumer.barista\" must implement \"ExternalBase.read()\" and other inherited abstract methods or be marked as \"abstract\".", parser.get_tree());
		CHECK(parser.get_depended_parsers().has(provider));
		CHECK(storage.index().get_refresh_revision(provider) == before);
		CHECK(parser.get_warnings().is_empty());
		public_block(source, path, parser);
	}
	TEST_CASE("empty_normal_and_lambda_bodies_use_function_origins") {
		for (bool lambda : { false, true }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			const String source = lambda ? "func test():\n\tvar callback = (func():)\n" : "func test()\n";
			const String path = storage.path("empty_body.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == 1);
			auto *function = parser.get_tree()->get_member("test").function;
			if (lambda)
				function = static_cast<BSParser::LambdaNode *>(static_cast<BSParser::VariableNode *>(function->body->statements[0])->initializer)->function;
			error_at(parser, 0, lambda ? R"(A lambda function must have a ":" followed by a body.)" : R"(A function must either have a ":" followed by a body, or be marked as "abstract".)", function);
			CHECK(function->resolved_body);
			public_block(source, path, parser);
			CHECK(analyzer.analyze() != OK);
			CHECK(parser.get_errors().size() == 1);
		}
	}
	TEST_CASE("bodyful_abstract_uses_suite_origin_and_valid_body_opposite") {
		for (bool abstract : { false, true }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			const String source = String("abstract extends RefCounted\n") + (abstract ? "abstract " : "") + "func read():\n\tpass\n";
			const String path = storage.path("body.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() != OK) == abstract);
			diagnostics(parser);
			CHECK(parser.get_errors().size() == (abstract ? 1 : 0));
			if (abstract)
				error_at(parser, 0, "An abstract function cannot have a body.", parser.get_tree()->get_member("read").function->body);
			public_block(source, path, parser);
		}
	}
	TEST_CASE("obligations_keep_nearest_name_and_each_abstract_base") {
		StorageFixture storage;
		WarningScope warnings;
		BSParser parser;
		const String source = "abstract class First:\n\tabstract func first()\n\tabstract func other()\nabstract class Second extends First:\n\tabstract func second()\nclass Missing extends Second:\n\tpass\nclass Own extends First:\n\tabstract func own()\n";
		const String path = storage.path("obligations.barista");
		BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		BS_TEST_REQUIRE(parser.get_errors().size() == 4);
		auto *missing = parser.get_tree()->get_member("Missing").m_class;
		auto *own = parser.get_tree()->get_member("Own").m_class;
		error_at(parser, 0, R"*(Class "Missing" must implement "Second.second()" and other inherited abstract methods or be marked as "abstract".)*", missing);
		error_at(parser, 1, R"*(Class "Missing" must implement "First.first()" and other inherited abstract methods or be marked as "abstract".)*", missing);
		error_at(parser, 2, R"*(Class "Own" is not abstract but contains abstract methods. Mark the class as "abstract" or remove "abstract" from all methods in this class.)*", own);
		error_at(parser, 3, R"*(Class "Own" must implement "First.first()" and other inherited abstract methods or be marked as "abstract".)*", own);
		public_block(source, path, parser);
		CHECK(analyzer.analyze() != OK);
		CHECK(parser.get_errors().size() == 4);
	}
	TEST_CASE("super_missing_parent_does_not_select_current_method") {
		for (bool named : { false, true }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			const String source = String("class Base:\n\tpass\nclass Child extends Base:\n\tfunc only_here():\n\t\t") + (named ? "super.only_here()" : "super()") + "\n";
			const String path = storage.path("missing_super.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == 1);
			auto *call = static_cast<BSParser::CallNode *>(parser.get_tree()->get_member("Child").m_class->get_member("only_here").function->body->statements[0]);
			error_at(parser, 0, R"*(Function "only_here()" not found in base Base.)*", call);
			CHECK(call->get_datatype().is_variant());
			public_block(source, path, parser);
		}
	}
	TEST_CASE("native_parent_virtual_refusal_and_concrete_method_opposite") {
		for (bool virtual_method : { false, true }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			const String source = virtual_method ? "extends Node\nfunc _ready():\n\tsuper()\n" : "extends Node\nfunc test():\n\tvar id: int = super.get_instance_id()\n";
			const String path = storage.path("native_super.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() != OK) == virtual_method);
			diagnostics(parser);
			CHECK(parser.get_errors().size() == (virtual_method ? 1 : 0));
			if (virtual_method)
				error_at(parser, 0, R"*(Cannot call the parent class' virtual function "_ready()" because it hasn't been defined.)*", parser.get_tree()->get_member("_ready").function->body->statements[0]);
			else {
				auto *value = static_cast<BSParser::VariableNode *>(parser.get_tree()->get_member("test").function->body->statements[0]);
				CHECK(value->initializer->get_datatype().builtin_type == Variant::INT);
			}
			public_block(source, path, parser);
		}
	}
	TEST_CASE("implicit_super_lambda_rejected_explicit_parent_allowed") {
		for (bool named : { false, true }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			const String source = String("class Base:\n\tfunc read():\n\t\tpass\nclass Child extends Base:\n\tfunc read():\n\t\tvar callback = func():\n\t\t\t") + (named ? "super.read()" : "super()") + "\n";
			const String path = storage.path("lambda_super.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == named);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (named ? 0 : 2));
			auto *variable = static_cast<BSParser::VariableNode *>(parser.get_tree()->get_member("Child").m_class->get_member("read").function->body->statements[0]);
			auto *call = static_cast<BSParser::LambdaNode *>(variable->initializer)->function->body->statements[0];
			if (!named) {
				error_at(parser, 0, "Cannot use `super()` inside a lambda.", call);
				error_at(parser, 1, R"*(Function "<anonymous>()" not found in base Base.)*", call);
			}
			public_block(source, path, parser);
		}
	}
	TEST_CASE("retained_external_concrete_parent_preserves_signature") {
		for (bool named : { false, true }) {
			StorageFixture storage;
			BSConformanceRegistry::ScopedCorpusState registry;
			WarningScope warnings;
			const String provider = "res://tests/abstract_contract/concrete.barista";
			BS_TEST_REQUIRE(seed(storage, provider, "class_name ExternalConcrete\nfunc read(parent_value: int = 7) -> int:\n\treturn parent_value\n"));
			const auto revision = storage.index().get_refresh_revision(provider);
			const String source = String("extends ExternalConcrete\nfunc read(child_value: int = 8) -> int:\n\treturn ") + (named ? "super.read(parent_value = 3)" : "super(parent_value = 3)") + "\n";
			const String path = storage.path("external_super.barista");
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			CHECK(parser.get_errors().is_empty());
			const auto retained = parser.get_depended_parser_for(provider);
			BS_TEST_REQUIRE(retained.is_valid());
			CHECK(retained->get_result_for_status(BSParserRef::PARSED) == OK);
			CHECK(retained->get_parser()->get_tree()->resolved_body);
			CHECK(storage.index().get_refresh_revision(provider) == revision);
			CHECK(public_block(source, path, parser) == "BS_TEST_OK");
		}
	}

	TEST_CASE("failed_abstract_owner_replays_once_without_advancing_completed_phase") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState registry;
		WarningScope warnings;
		const String provider = "res://tests/abstract_contract/broken.barista";
		BS_TEST_REQUIRE(seed(storage, provider, "abstract class_name Broken\nabstract func read():\n\tpass\n"));
		BSParser first, second;
		BSAnalyzer first_analyzer(&first), second_analyzer(&second);
		BS_TEST_REQUIRE(first.parse("extends Broken\n", storage.path("first.barista"), false) == OK);
		BS_TEST_REQUIRE(second.parse("extends Broken\n", storage.path("second.barista"), false) == OK);
		// Bind both consumers before the direct owner-body failure, as in provider replay coverage.
		BS_TEST_REQUIRE(first_analyzer.resolve_inheritance() == OK);
		BS_TEST_REQUIRE(second_analyzer.resolve_inheritance() == OK);
		BS_TEST_REQUIRE(first_analyzer.resolve_interface() == OK);
		BS_TEST_REQUIRE(second_analyzer.resolve_interface() == OK);
		const auto owner = first.get_depended_parser_for(provider);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK(second.get_depended_parser_for(provider) == owner);
		const auto status_before = owner->get_status();
		for (BSAnalyzer *analyzer : { &first_analyzer, &second_analyzer }) {
			BSParser &parser = analyzer == &first_analyzer ? first : second;
			const String name = analyzer == &first_analyzer ? "first.barista" : "second.barista";
			CHECK(analyzer->resolve_body() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == 2);
			error_at(parser, 0, R"*(Could not resolve class "Broken". The class is declared in "res://tests/abstract_contract/broken.barista", which has errors, the first at line 3: An abstract function cannot have a body.)*", parser.get_tree());
			error_at(parser, 1, vformat(R"*(Class "%s" must implement "Broken.read()" and other inherited abstract methods or be marked as "abstract".)*", name), parser.get_tree());
			CHECK(owner->get_status() == status_before);
			CHECK(owner->get_parser()->get_tree()->resolved_body);
			CHECK(owner->get_parser()->get_errors().size() == 1);
			CHECK(owner->get_analyzer()->get_highest_completed_phase() < BSAnalyzer::AnalyzerPhase::FLOW_FINALITY_INVARIANTS);
			CHECK(analyzer->get_highest_completed_phase() == BSAnalyzer::AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL);
			CHECK(analyzer->resolve_body() != OK);
			CHECK(parser.get_errors().size() == 2);
			CHECK(owner->get_parser()->get_errors().size() == 1);
		}
	}
	TEST_CASE("abstract_settings_and_index_scopes_restore") {
		CHECK(verify_case_isolation([]() {
			StorageFixture storage;
			BSConformanceRegistry::ScopedCorpusState registry;
			WarningScope warnings;
			BS_TEST_REQUIRE(seed(storage, "res://tests/abstract_contract/isolation.barista", "abstract class_name Isolated\nabstract func read()\n"));
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("extends Isolated\nfunc read():\n\tpass\n", storage.path("isolated.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			CHECK(parser.get_errors().is_empty());
		}));
	}
	TEST_CASE("parent_member_claim_blocks_current_method_and_callable_fallback") {
		for (bool callable : { false, true }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			const String source = String("class Base:\n\tvar read: ") + (callable ? "Callable" : "int") + "\nclass Child extends Base:\n\tfunc test():\n\t\tsuper.read()\n";
			const String path = storage.path("claimed_parent.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (callable ? 1 : 2));
			const auto *call = parser.get_tree()->get_member("Child").m_class->get_member("test").function->body->statements[0];
			if (!callable)
				error_at(parser, 0, R"(Member "read" is not a function.)", call);
			error_at(parser, callable ? 0 : 1, R"*(Function "read()" not found in base Base.)*", call);
			public_block(source, path, parser);
		}
	}
	TEST_CASE("only_actual_parent_trait_methods_supply_super") {
		for (bool parent_uses_trait : { false, true }) {
			StorageFixture storage;
			BSConformanceRegistry::ScopedCorpusState registry;
			WarningScope warnings;
			BSParser parser;
			const String source = String("trait Reader:\n\tfunc read() -> int:\n\t\treturn 7\nclass Base:\n\t") + (parent_uses_trait ? "uses Reader" : "pass") + "\nclass Child extends Base:\n\t" + (parent_uses_trait ? "pass" : "uses Reader") + "\n\tfunc test():\n\t\tvar result = super.read()\n";
			const String path = storage.path("trait_parent.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == parent_uses_trait);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (parent_uses_trait ? 0 : 1));
			const auto *value = static_cast<BSParser::VariableNode *>(parser.get_tree()->get_member("Child").m_class->get_member("test").function->body->statements[0]);
			if (parent_uses_trait)
				CHECK(value->initializer->get_datatype().builtin_type == Variant::INT);
			else
				error_at(parser, 0, R"*(Function "read()" not found in base Base.)*", value->initializer);
			public_block(source, path, parser);
		}
	}
	TEST_CASE("concrete_super_return_discard_keeps_call_warning_origin") {
		StorageFixture storage;
		WarningScope warnings;
		BSParser parser;
		ProjectSettings::get_singleton()->set_setting(BSWarning::get_setting_path_from_code(BSWarning::RETURN_VALUE_DISCARDED), BSWarning::WARN);
		BSParser::update_project_settings();
		const String source = "extends Node\nfunc test():\n\tsuper.get_instance_id()\n";
		BS_TEST_REQUIRE(parser.parse(source, storage.path("super_warning.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		CHECK(parser.get_errors().is_empty());
		BS_TEST_REQUIRE(parser.get_warnings().size() == 1);
		const auto &warning = parser.get_warnings().front()->get();
		const auto *call = parser.get_tree()->get_member("test").function->body->statements[0];
		CHECK(warning.code == BSWarning::RETURN_VALUE_DISCARDED);
		CHECK(warning.start_line == call->start_line);
		CHECK(warning.start_column == call->start_column);
		CHECK(warning.end_line == call->end_line);
		CHECK(warning.end_column == call->end_column);
		CHECK(warning.symbols.size() == 1);
		CHECK(warning.symbols[0] == "get_instance_id");
	}
	TEST_CASE("only_actual_parent_conformance_witness_supplies_super") {
		for (bool parent_witness : { false, true }) {
			StorageFixture storage;
			BSConformanceRegistry::ScopedCorpusState registry;
			WarningScope warnings;
			BSParser parser;
			const String source = String("trait Reader:\n\tabstract func read() -> int\nclass Base:\n\tpass\nclass Child extends Base:\n\tfunc test():\n\t\tvar result = super.read()\nextend ") + (parent_witness ? "Base" : "Child") + " uses Reader:\n\tfunc read() -> int:\n\t\treturn 7\n";
			const String path = storage.path("witness_parent.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == parent_witness);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (parent_witness ? 0 : 1));
			const auto *value = static_cast<BSParser::VariableNode *>(parser.get_tree()->get_member("Child").m_class->get_member("test").function->body->statements[0]);
			if (parent_witness)
				CHECK(value->initializer->get_datatype().builtin_type == Variant::INT);
			else
				error_at(parser, 0, R"*(Function "read()" not found in base Base.)*", value->initializer);
			public_block(source, path, parser);
		}
	}
	TEST_CASE("repair1_static_parent_context_matrix") {
		for (bool static_parent : { false, true })
			for (bool static_caller : { false, true }) {
				StorageFixture storage;
				WarningScope warnings;
				BSParser parser;
				const String source = String("class Base:\n\t") + (static_parent ? "static " : "") + "func read() -> int:\n\t\treturn 1\nclass Child extends Base:\n\t" + (static_caller ? "static " : "") + "func test():\n\t\tsuper.read()\n";
				const String path = storage.path("static_parent.barista");
				BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
				BSAnalyzer analyzer(&parser);
				const bool bad = static_caller && !static_parent;
				CHECK((analyzer.analyze() != OK) == bad);
				diagnostics(parser);
				BS_TEST_REQUIRE(parser.get_errors().size() == (bad ? 1 : 0));
				auto *call = parser.get_tree()->get_member("Child").m_class->get_member("test").function->body->statements[0];
				if (bad)
					error_at(parser, 0, R"*(Cannot call non-static function "read()" from the static function "test()".)*", call);
				public_block(source, path, parser);
			}
	}
	TEST_CASE("repair1_parent_lambda_capture_chain_and_static_noncapture") {
		for (bool static_parent : { false, true })
			for (bool static_caller : { false, true }) {
				StorageFixture storage;
				WarningScope warnings;
				BSParser parser;
				const String source = String("class Base:\n\t") + (static_parent ? "static " : "") + "func read():\n\t\tpass\nclass Child extends Base:\n\t" + (static_caller ? "static " : "") + "func test():\n\t\tvar outer = func():\n\t\t\tvar inner = func():\n\t\t\t\tsuper.read()\n";
				const String path = storage.path("capture_parent.barista");
				BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
				BSAnalyzer analyzer(&parser);
				const bool bad = static_caller && !static_parent;
				CHECK((analyzer.analyze() != OK) == bad);
				diagnostics(parser);
				auto *outer = static_cast<BSParser::LambdaNode *>(static_cast<BSParser::VariableNode *>(parser.get_tree()->get_member("Child").m_class->get_member("test").function->body->statements[0])->initializer);
				auto *inner = static_cast<BSParser::LambdaNode *>(static_cast<BSParser::VariableNode *>(outer->function->body->statements[0])->initializer);
				CHECK(inner->parent_lambda == outer);
				CHECK(outer->use_self == (!static_parent && !static_caller));
				CHECK(inner->use_self == (!static_parent && !static_caller));
				BS_TEST_REQUIRE(parser.get_errors().size() == (bad ? 1 : 0));
				if (bad)
					error_at(parser, 0, R"*(Cannot call non-static function "read()" from the static function "test()".)*", inner->function->body->statements[0]);
				public_block(source, path, parser);
			}
	}
	TEST_CASE("repair1_parent_static_initializer_and_parameter_default_contexts") {
		for (int mode : { 0, 1, 2, 3 }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			const String tail = mode == 0 ? "\tstatic var value = super.read()\n" : mode == 1 ? "\tstatic var callback = func():\n\t\tsuper.read()\n"
					: mode == 2																  ? "\tstatic func test(value = super.read()):\n\t\tpass\n"
																							  : "\tstatic func test(callback = func(): super.read()):\n\t\tpass\n";
			const String source = "class Base:\n\tfunc read() -> int:\n\t\treturn 1\nclass Child extends Base:\n" + tail;
			const String path = storage.path("declaration_context.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == 1);
			auto *child = parser.get_tree()->get_member("Child").m_class;
			const BSParser::Node *call = nullptr;
			BSParser::LambdaNode *lambda = nullptr;
			if (mode == 0)
				call = child->get_member("value").variable->initializer;
			else if (mode == 1)
				lambda = static_cast<BSParser::LambdaNode *>(child->get_member("callback").variable->initializer);
			else if (mode == 2)
				call = child->get_member("test").function->parameters[0]->initializer;
			else
				lambda = static_cast<BSParser::LambdaNode *>(child->get_member("test").function->parameters[0]->initializer);
			if (lambda) {
				call = lambda->function->body->statements[0];
				CHECK_FALSE(lambda->use_self);
			}
			// Pin surface1758-1765 drains variable initializer lambdas immediately under Child.
			// Parameter-default lambdas remain pending until Base's body (pin5344/3824);
			// the queue carries no creation-class field. Preserve that distinct mode3 result.
			const String expected = mode == 3 ? R"*(Function "read()" not found in base RefCounted.)*" : mode == 0 || mode == 1 ? R"*(Cannot call non-static function "read()" from a static variable initializer.)*"
																																: R"*(Cannot call non-static function "read()" from the static function "test()".)*";
			error_at(parser, 0, expected, call);
			public_block(source, path, parser);
		}
	}
	TEST_CASE("repair1_unresolved_parent_named_argument_order") {
		for (int claim : { 0, 1, 2 }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			const String source = String("class Base:\n\t") + (claim == 0 ? "pass" : claim == 1 ? "var missing: int"
																								: "var missing: Callable") +
					"\nclass Child extends Base:\n\tfunc test():\n\t\tsuper.missing(value = 1)\n";
			const String path = storage.path("named_parent.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (claim == 1 ? 3 : 2));
			auto *call = static_cast<BSParser::CallNode *>(parser.get_tree()->get_member("Child").m_class->get_member("test").function->body->statements[0]);
			int index = 0;
			if (claim == 1)
				error_at(parser, index++, R"(Member "missing" is not a function.)", call);
			error_at(parser, index++, "Named arguments require a statically known BaristaScript function.", call->arguments[0]);
			error_at(parser, index, R"*(Function "missing()" not found in base Base.)*", call);
			public_block(source, path, parser);
			CHECK(analyzer.analyze() != OK);
			CHECK(parser.get_errors().size() == (claim == 1 ? 3 : 2));
		}
	}
	TEST_CASE("repair1_parent_signal_payload_and_arity") {
		for (int mode : { 0, 1, 2, 3 }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			const String source = String("extends Node\nsignal ping(value: int)\nfunc test():\n\tsuper.emit_signal(\"ping\"") + (mode == 0 ? ", 1" : mode == 1 ? ", \"wrong\""
																																				: mode == 2	   ? ""
																																							   : ", 1, 2") +
					")\n";
			const String path = storage.path("signal_parent.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() != OK) == (mode != 0));
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (mode == 0 ? 0 : 1));
			auto *call = static_cast<BSParser::CallNode *>(parser.get_tree()->get_member("test").function->body->statements[0]);
			if (mode == 1)
				error_at(parser, 0, R"*(Invalid argument for "emit_signal()" function: argument 2 should be "int" but is "String".)*", call->arguments[1]);
			if (mode == 2)
				error_at(parser, 0, R"*(Too few arguments for "emit_signal()" call. Expected at least 2 but received 1.)*", call);
			if (mode == 3)
				error_at(parser, 0, R"*(Too many arguments for "emit_signal()" call. Expected at most 2 but received 3.)*", call->arguments[2]);
			CHECK(parser.get_tree()->get_member("ping").signal->usages == 1);
			public_block(source, path, parser);
		}
	}
	TEST_CASE("repair1_parent_signal_callable_validation") {
		for (const String &method : { String("connect"), String("disconnect"), String("is_connected") })
			for (bool bad : { false, true }) {
				StorageFixture storage;
				WarningScope warnings;
				BSParser parser;
				const String source = String("extends Node\nsignal ping(value: int)\nfunc handler(value: ") + (bad ? "String" : "int") + ") -> void:\n\tpass\nfunc test():\n\tsuper." + method + "(\"ping\", handler)\n";
				const String path = storage.path("callback_parent.barista");
				BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() != OK) == bad);
				diagnostics(parser);
				BS_TEST_REQUIRE(parser.get_errors().size() == (bad ? 1 : 0));
				const auto *call = static_cast<BSParser::CallNode *>(parser.get_tree()->get_member("test").function->body->statements[0]);
				if (bad)
					error_at(parser, 0, "Cannot " + (method == "is_connected" ? String("check connection for") : method) + R"*( signal "Signal[[int]]" to callable "Callable[[String], void]": signal argument 1 of type "int" cannot be passed to callable parameter of type "String".)*", call->arguments[1]);
				CHECK(parser.get_tree()->get_member("ping").signal->usages == 1);
				public_block(source, path, parser);
			}
	}
	TEST_CASE("repair1_parent_signal_use_suppresses_only_used_signal_warning") {
		for (const String &method : { String("emit_signal"), String("connect"), String("disconnect"), String("is_connected") }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			ProjectSettings::get_singleton()->set_setting(BSWarning::get_setting_path_from_code(BSWarning::UNUSED_SIGNAL), BSWarning::WARN);
			BSParser::update_project_settings();
			const String source = String("extends Node\nsignal ping(value: int)\nsignal unused(value: int)\nfunc handler(value: int) -> void:\n\tpass\nfunc test():\n\tsuper.") + method + "(\"ping\", " + (method == "emit_signal" ? String("1") : String("handler")) + ")\n";
			const String path = storage.path("signal_use_parent.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			CHECK(parser.get_errors().is_empty());
			BS_TEST_REQUIRE(parser.get_warnings().size() == 1);
			const auto *used = parser.get_tree()->get_member("ping").signal;
			const auto *unused = parser.get_tree()->get_member("unused").signal;
			CHECK(used->usages == 1);
			CHECK(unused->usages == 0);
			const auto &warning = parser.get_warnings().front()->get();
			CHECK(warning.code == BSWarning::UNUSED_SIGNAL);
			CHECK(warning.symbols.size() == 1);
			CHECK(warning.symbols[0] == "unused");
			CHECK(warning.start_line == unused->identifier->start_line);
			CHECK(warning.start_column == unused->identifier->start_column);
			CHECK(warning.end_line == unused->identifier->end_line);
			CHECK(warning.end_column == unused->identifier->end_column);
			const int warning_count = parser.get_warnings().size();
			CHECK(analyzer.analyze() == OK);
			CHECK(used->usages == 1);
			CHECK(parser.get_warnings().size() == warning_count);
		}
	}
	TEST_CASE("repair1_head_initializer_and_default_lambdas_retain_static_context") {
		for (bool static_context : { false, true })
			for (bool parameter : { false, true }) {
				StorageFixture storage;
				WarningScope warnings;
				BSParser parser;
				const String prefix = static_context ? "static " : "";
				const String source = "extends Node\n" + prefix + (parameter ? String("func test(callback = func(): super.get_instance_id()):\n\tpass\n") : String("var callback = func():\n\tsuper.get_instance_id()\n"));
				const String path = storage.path("head_context.barista");
				BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() != OK) == static_context);
				diagnostics(parser);
				BS_TEST_REQUIRE(parser.get_errors().size() == (static_context ? 1 : 0));
				auto *lambda = static_cast<BSParser::LambdaNode *>(parameter ? parser.get_tree()->get_member("test").function->parameters[0]->initializer : parser.get_tree()->get_member("callback").variable->initializer);
				CHECK(lambda->use_self == !static_context);
				CHECK(lambda->function->is_static == static_context);
				if (static_context)
					error_at(parser, 0, parameter ? R"*(Cannot call non-static function "get_instance_id()" from the static function "test()".)*" : R"*(Cannot call non-static function "get_instance_id()" from a static variable initializer.)*", lambda->function->body->statements[0]);
				public_block(source, path, parser);
			}
	}
	TEST_CASE("repair1_static_context_reaches_native_trait_and_witness_parents") {
		for (int provider : { 0, 1, 2 }) {
			StorageFixture storage;
			BSConformanceRegistry::ScopedCorpusState registry;
			WarningScope warnings;
			BSParser parser;
			const String source = provider == 0 ? "extends Node\nstatic func test():\n\tsuper.get_instance_id()\n" : provider == 1 ? "trait Reader:\n\tfunc read():\n\t\tpass\nclass Base:\n\tuses Reader\nclass Child extends Base:\n\tstatic func test():\n\t\tsuper.read()\n"
																																   : "trait Reader:\n\tabstract func read()\nclass Base:\n\tpass\nclass Child extends Base:\n\tstatic func test():\n\t\tsuper.read()\nextend Base uses Reader:\n\tfunc read():\n\t\tpass\n";
			const String path = storage.path("parent_context_provider.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == 1);
			const auto *owner = provider == 0 ? parser.get_tree() : parser.get_tree()->get_member("Child").m_class;
			error_at(parser, 0, vformat(R"*(Cannot call non-static function "%s()" from the static function "test()".)*", provider == 0 ? "get_instance_id" : "read"), owner->get_member("test").function->body->statements[0]);
			public_block(source, path, parser);
		}
	}
	TEST_CASE("repair1_parent_error_tail_keeps_abstract_arity_static_void_order") {
		StorageFixture storage;
		WarningScope warnings;
		BSParser parser;
		const String source = "abstract class Base:\n\tabstract func read(value: int) -> void\nabstract class Child extends Base:\n\tstatic func test():\n\t\tvar result = super.read()\n";
		const String path = storage.path("parent_order.barista");
		BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		BS_TEST_REQUIRE(parser.get_errors().size() == 4);
		auto *call = static_cast<BSParser::VariableNode *>(parser.get_tree()->get_member("Child").m_class->get_member("test").function->body->statements[0])->initializer;
		error_at(parser, 0, R"*(Cannot call the parent class' abstract function "read()" because it hasn't been defined.)*", call);
		error_at(parser, 1, R"*(Too few arguments for "read()" call. Expected at least 1 but received 0.)*", call);
		error_at(parser, 2, R"*(Cannot call non-static function "read()" from the static function "test()".)*", call);
		error_at(parser, 3, R"*(Cannot get return value of call to "read()" because it returns "void".)*", call);
		public_block(source, path, parser);
	}
	TEST_CASE("repair1_nested_parent_signal_uses_active_class_not_head") {
		for (bool bad : { false, true }) {
			StorageFixture storage;
			WarningScope warnings;
			BSParser parser;
			const String source = String("signal ping(value: String)\nclass Child extends Node:\n\tsignal ping(value: int)\n\tfunc test():\n\t\tsuper.emit_signal(\"ping\", ") + (bad ? "\"wrong\"" : "1") + ")\nfunc head():\n\temit_signal(\"ping\", \"okay\")\n";
			const String path = storage.path("nested_signal_parent.barista");
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() != OK) == bad);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (bad ? 1 : 0));
			auto *child = parser.get_tree()->get_member("Child").m_class;
			auto *call = static_cast<BSParser::CallNode *>(child->get_member("test").function->body->statements[0]);
			if (bad)
				error_at(parser, 0, R"*(Invalid argument for "emit_signal()" function: argument 2 should be "int" but is "String".)*", call->arguments[1]);
			CHECK(child->get_member("ping").signal->usages == 1);
			// The following ordinary head call reads the original parser context. A leaked
			// Child binding would reject its String payload against Child.ping(int).
			CHECK(parser.get_tree()->get_member("ping").signal->usages == 1);
			public_block(source, path, parser);
		}
	}
}
