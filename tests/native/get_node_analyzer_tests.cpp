/**************************************************************************/
/*  get_node_analyzer_tests.cpp                                   */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                         */
/*  This file is part of BaristaScript, a Godot GDExtension.               */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_builtin_sources.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <string>
using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;
namespace {
BSParser::ExpressionNode *returned(BSParser::FunctionNode *function) {
	if (!function || !function->body || function->body->statements.is_empty())
		return nullptr;
	auto *statement = function->body->statements[function->body->statements.size() - 1];
	return statement && statement->type == BSParser::Node::RETURN ? static_cast<BSParser::ReturnNode *>(statement)->return_value : nullptr;
}
void node_type(const BSParser::ExpressionNode *expression) {
	BS_TEST_REQUIRE(expression != nullptr);
	const auto type = expression->get_datatype();
	CHECK(type.kind == BSParser::DataType::NATIVE);
	CHECK(type.builtin_type == Variant::OBJECT);
	CHECK(type.native_type == StringName("Node"));
	CHECK(type.type_source == BSParser::DataType::ANNOTATED_EXPLICIT);
	CHECK_FALSE(type.is_meta_type);
	CHECK_FALSE(type.is_constant);
	CHECK_FALSE(type.is_nullable);
	CHECK_FALSE(expression->is_constant);
	CHECK_FALSE(expression->is_unmaterialized_constant);
}
void scenario(const String &base, bool is_static, const String &spelling) {
	StorageFixture fixture;
	BSParser parser;
	const String source = "extends " + base + "\n" + (is_static ? "static " : "") + "func test():\n\treturn " + spelling + "Child\n";
	BS_TEST_REQUIRE(parser.parse(source, fixture.path("node.barista"), false) == OK);
	auto *expression = returned(parser.get_tree()->get_member("test").function);
	BS_TEST_REQUIRE(expression && expression->type == BSParser::Node::GET_NODE);
	CHECK(expression->start_line == 3);
	CHECK(expression->start_column == 12);
	CHECK(expression->end_line == 3);
	CHECK(expression->end_column == 18);
	BSAnalyzer analyzer(&parser);
	const bool valid = base == "Node" && !is_static;
	CHECK((analyzer.analyze() == OK) == valid);
	if (valid) {
		CHECK(parser.get_errors().is_empty());
		node_type(expression);
	} else {
		BS_TEST_REQUIRE(parser.get_errors().size() == 1);
		const auto &e = parser.get_errors().front()->get();
		const String expected = "Cannot use shorthand \"get_node()\" notation (\"" + spelling + "\") " + (base == "Node" ? "in a static function." : "on a class that isn't a node.");
		CHECK(std::string(e.message.utf8().get_data()) == std::string(expected.utf8().get_data()));
		CHECK(e.line == 3);
		CHECK(e.column == 12);
		CHECK(e.end_line == 3);
		CHECK(e.end_column == 18);
		CHECK(expression->get_datatype().kind == BSParser::DataType::VARIANT);
	}
	CHECK(parser.get_warnings().is_empty());
}
} //namespace
TEST_SUITE("get_node_analyzer") {
	TEST_CASE("dollar_and_percent_publish_node_without_runtime_lookup") {
		scenario("Node", false, "$");
		scenario("Node", false, "%");
	}
	TEST_CASE("non_node_precedes_static_and_keeps_exact_get_node_span") {
		for (const char *s : { "$", "%" }) {
			scenario("RefCounted", false, s);
			scenario("RefCounted", true, s);
		}
	}
	TEST_CASE("static_node_context_is_rejected_at_both_spellings") {
		scenario("Node", true, "$");
		scenario("Node", true, "%");
	}
	TEST_CASE("parameter_defaults_use_the_declaring_static_context") {
		for (bool is_static : { false, true }) {
			for (const char *spelling : { "$", "%" }) {
				StorageFixture fixture;
				BSParser parser;
				const String source = String("extends Node\n") + (is_static ? "static " : "") + "func f(_arg = " + spelling + "Child):\n\tpass\n";
				BS_TEST_REQUIRE(parser.parse(source, fixture.path("default.barista"), false) == OK);
				auto *expression = parser.get_tree()->get_member("f").function->parameters[0]->initializer;
				BS_TEST_REQUIRE(expression && expression->type == BSParser::Node::GET_NODE);
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() == OK) == !is_static);
				if (is_static) {
					BS_TEST_REQUIRE(parser.get_errors().size() == 1);
					const auto &e = parser.get_errors().front()->get();
					CHECK(e.message == String("Cannot use shorthand \"get_node()\" notation (\"") + spelling + "\") in a static function.");
					CHECK(e.line == 2);
					CHECK(e.column == 22);
					CHECK(e.end_line == 2);
					CHECK(e.end_column == 28);
				} else {
					CHECK(parser.get_errors().is_empty());
					node_type(expression);
				}
				CHECK(parser.get_warnings().is_empty());
			}
		}
	}
	TEST_CASE("default_lambda_and_lazy_signature_restore_declaration_context") {
		for (bool lambda : { false, true }) {
			for (bool is_static : { false, true }) {
				StorageFixture fixture;
				BSParser parser;
				const String source = String("extends Node\nvar value = f()\n") + (is_static ? "static " : "") + "func f(_arg = " + (lambda ? "func(): return " : "") + "$Child):\n\treturn 1\nfunc after():\n\treturn %Child\n";
				BS_TEST_REQUIRE(parser.parse(source, fixture.path("lazy_default.barista"), false) == OK);
				auto *expression = parser.get_tree()->get_member("f").function->parameters[0]->initializer;
				BS_TEST_REQUIRE(expression != nullptr);
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() == OK) == !is_static);
				if (is_static) {
					BS_TEST_REQUIRE(parser.get_errors().size() == 1);
					const auto &e = parser.get_errors().front()->get();
					CHECK(e.message == "Cannot use shorthand \"get_node()\" notation (\"$\") in a static function.");
					CHECK(e.line == 3);
					CHECK(e.column == (lambda ? 37 : 22));
					CHECK(e.end_line == 3);
					CHECK(e.end_column == (lambda ? 43 : 28));
				} else {
					CHECK(parser.get_errors().is_empty());
				}
				if (lambda) {
					BS_TEST_REQUIRE(expression->type == BSParser::Node::LAMBDA);
					auto *node = static_cast<BSParser::LambdaNode *>(expression);
					CHECK(node->function->is_static == is_static);
					CHECK(node->use_self == !is_static);
					if (!is_static)
						node_type(returned(node->function));
				} else if (!is_static) {
					node_type(expression);
				}
				node_type(returned(parser.get_tree()->get_member("after").function));
				CHECK(parser.get_warnings().is_empty());
			}
		}
	}
	TEST_CASE("nested_lambdas_capture_every_parent_and_restore_context") {
		StorageFixture fixture;
		BSParser parser;
		const String source = "extends Node\nfunc test():\n\treturn func():\n\t\treturn func():\n\t\t\treturn %Child\nfunc other():\n\treturn func(): return 1\n";
		BS_TEST_REQUIRE(parser.parse(source, fixture.path("capture.barista"), false) == OK);
		auto *outer = returned(parser.get_tree()->get_member("test").function);
		BS_TEST_REQUIRE(outer && outer->type == BSParser::Node::LAMBDA);
		auto *a = static_cast<BSParser::LambdaNode *>(outer);
		auto *inner = returned(a->function);
		BS_TEST_REQUIRE(inner && inner->type == BSParser::Node::LAMBDA);
		auto *b = static_cast<BSParser::LambdaNode *>(inner);
		auto *other = returned(parser.get_tree()->get_member("other").function);
		BS_TEST_REQUIRE(other && other->type == BSParser::Node::LAMBDA);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		CHECK(parser.get_errors().is_empty());
		CHECK(a->use_self);
		CHECK(b->use_self);
		CHECK(b->parent_lambda == a);
		CHECK_FALSE(static_cast<BSParser::LambdaNode *>(other)->use_self);
		node_type(returned(b->function));
	}
	TEST_CASE("nested_static_lambda_retains_enclosing_static_rejection") {
		StorageFixture fixture;
		BSParser parser;
		const String source = "extends Node\nstatic func test():\n\treturn func():\n\t\treturn func():\n\t\t\treturn $Child\n";
		BS_TEST_REQUIRE(parser.parse(source, fixture.path("static_capture.barista"), false) == OK);
		auto *outer = returned(parser.get_tree()->get_member("test").function);
		BS_TEST_REQUIRE(outer && outer->type == BSParser::Node::LAMBDA);
		auto *a = static_cast<BSParser::LambdaNode *>(outer);
		auto *inner = returned(a->function);
		BS_TEST_REQUIRE(inner && inner->type == BSParser::Node::LAMBDA);
		auto *b = static_cast<BSParser::LambdaNode *>(inner);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		BS_TEST_REQUIRE(parser.get_errors().size() == 1);
		const auto &e = parser.get_errors().front()->get();
		CHECK(e.message == "Cannot use shorthand \"get_node()\" notation (\"$\") in a static function.");
		CHECK(e.line == 5);
		CHECK(e.column == 20);
		CHECK(e.end_line == 5);
		CHECK(e.end_column == 26);
		CHECK_FALSE(a->use_self);
		CHECK_FALSE(b->use_self);
	}
	TEST_CASE("static_member_initializer_and_lambda_preserve_and_restore_context") {
		for (const char *expression : { "$Child", "func(): return %Child" }) {
			StorageFixture fixture;
			BSParser parser;
			const String source = String("extends Node\nstatic var value = ") + expression + "\nvar other = func(): return $Child\n";
			BS_TEST_REQUIRE(parser.parse(source, fixture.path("member.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			BS_TEST_REQUIRE(parser.get_errors().size() == 1);
			const auto &e = parser.get_errors().front()->get();
			const bool lambda = String(expression).begins_with("func");
			CHECK(e.message == String("Cannot use shorthand \"get_node()\" notation (\"") + (lambda ? "%" : "$") + "\") in a static function.");
			CHECK(e.line == 2);
			CHECK(e.column == (lambda ? 35 : 20));
			CHECK(e.end_line == 2);
			CHECK(e.end_column == (lambda ? 41 : 26));
			auto *other = parser.get_tree()->get_member("other").variable->initializer;
			BS_TEST_REQUIRE(other && other->type == BSParser::Node::LAMBDA);
			auto *other_lambda = static_cast<BSParser::LambdaNode *>(other);
			CHECK(other_lambda->use_self);
			node_type(returned(other_lambda->function));
			if (lambda)
				CHECK_FALSE(static_cast<BSParser::LambdaNode *>(parser.get_tree()->get_member("value").variable->initializer)->use_self);
		}
	}
	TEST_CASE("inherited_node_and_nested_class_keep_receiver_and_restore_after_error") {
		StorageFixture fixture;
		BSParser parser;
		const String source = "class Base extends Node:\n\tfunc test(): return $Child\nclass Derived extends Base:\n\tfunc test(): return %Child\nclass Other:\n\tfunc test(): return $Child\nclass Last extends Node2D:\n\tfunc test(): return %Child\n";
		BS_TEST_REQUIRE(parser.parse(source, fixture.path("inherit.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		BS_TEST_REQUIRE(parser.get_errors().size() == 1);
		const auto &e = parser.get_errors().front()->get();
		CHECK(e.message == "Cannot use shorthand \"get_node()\" notation (\"$\") on a class that isn't a node.");
		CHECK(e.line == 6);
		CHECK(e.column == 25);
		CHECK(e.end_line == 6);
		CHECK(e.end_column == 31);
		for (const char *name : { "Base", "Derived", "Last" }) {
			auto *klass = parser.get_tree()->get_member(name).m_class;
			BS_TEST_REQUIRE(klass != nullptr);
			node_type(returned(klass->get_member("test").function));
		}
	}
	TEST_CASE("original_global_enum_test_no_exec_block_has_no_warnings") {
		StorageFixture fixture;
		BSParser parser;
		// Exact static test_no_exec block from global_builtin_and_native_enums.fs at c9d5e35.
		const String source = "extends Node\n\nfunc test_no_exec():\n\t# GH-99309\n\tvar sprite: Sprite3D = $Sprite3D\n\tsprite.axis = Vector3.AXIS_Y # No warning.\n\tsprite.set_axis(Vector3.AXIS_Y) # No warning.\n";
		BS_TEST_REQUIRE(parser.parse(source, fixture.path("original.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		for (const auto &e : parser.get_errors())
			MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column);
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().is_empty());
		auto *function = parser.get_tree()->get_member("test_no_exec").function;
		BS_TEST_REQUIRE(function && function->body && !function->body->statements.is_empty());
		auto *first = function->body->statements[0];
		BS_TEST_REQUIRE(first && first->type == BSParser::Node::VARIABLE);
		node_type(static_cast<BSParser::VariableNode *>(first)->initializer);
	}
	TEST_CASE("builtin_source_registry_preserves_static_path_identity") {
		StorageFixture fixture;
		const String path = "barista://builtin/s7_accounting.barista";
		String previous;
		const bool present = BSBuiltinSources::get_source(path, previous);
		struct Restore {
			String path;
			String source;
			bool present;
			~Restore() {
				if (present)
					BSBuiltinSources::register_source(path, source);
				else
					BSBuiltinSources::unregister_source(path);
			}
		} restore{ path, previous, present };
		BSBuiltinSources::register_source(path, "const answer = 42\n");
		String found;
		CHECK(BSBuiltinSources::is_builtin_path(path));
		CHECK_FALSE(BSBuiltinSources::is_builtin_path("res://s7_accounting.barista"));
		CHECK(BSBuiltinSources::get_source(path, found));
		CHECK(found == "const answer = 42\n");
		CHECK(BSBuiltinSources::get_exported_bytecode_path(path) == "res://.barista/builtin/s7_accounting.bsb");
	}
	TEST_CASE("original_anonymous_enum_is_published_before_for_body") {
		StorageFixture fixture;
		BSParser parser;
		const String source = "enum:\n\tenum_value = 1\n\nfunc test():\n\tfor x in enum_value:\n\t\tif x is String:\n\t\t\tpass\n";
		BS_TEST_REQUIRE(parser.parse(source, fixture.path("enum_for.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		for (const auto &e : parser.get_errors())
			MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column, "-", e.end_line, ":", e.end_column);
		BS_TEST_REQUIRE(parser.get_errors().size() == 1);
		const auto &e = parser.get_errors().front()->get();
		CHECK(e.message == "Expression is of type \"int\" so it can't be of type \"String\".");
		CHECK(e.line == 6);
		CHECK(e.column == 12);
		CHECK(e.end_line == 6);
		CHECK(e.end_column == 13);
	}
	TEST_CASE("original_unrelated_inner_enum_keeps_match_nonexhaustive") {
		for (bool incremental : { false, true }) {
			StorageFixture fixture;
			BSParser parser;
			const String source = R"(
class Inner:
	enum Level:
		LOW = 1
		HIGH = 2

enum Level:
	LOW = 1
	HIGH = 2

func describe() -> String:
	var value = Level.LOW
	match value:
		value is Inner.Level:
			return "inner"
)";
			BS_TEST_REQUIRE(parser.parse(source, fixture.path("enum_match.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			if (incremental) {
				CHECK(analyzer.resolve_inheritance() == OK);
				CHECK(analyzer.resolve_interface() != OK);
				CHECK(analyzer.resolve_body() != OK);
			} else {
				CHECK(analyzer.analyze() != OK);
			}
			for (const auto &e : parser.get_errors())
				MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column, "-", e.end_line, ":", e.end_column);
			BS_TEST_REQUIRE(parser.get_errors().size() == 2);
			const auto &name_error = parser.get_errors().front()->get();
			CHECK(name_error.message == "The member \"Level\" already exists in outer class enum_match.barista.");
			CHECK(name_error.line == 3);
			CHECK(name_error.column == 5);
			CHECK(name_error.end_line == 5);
			CHECK(name_error.end_column == 18);
			const auto &e = parser.get_errors().back()->get();
			CHECK(e.message == R"(Not all code paths return a value. The "match" over "Level" leaves the undeclared values of its integer carrier unhandled; add an unguarded "_" or bind branch.)");
			auto *function = parser.get_tree()->get_member("describe").function;
			BS_TEST_REQUIRE(function && function->body && function->body->statements.size() == 2);
			CHECK(e.line == 11);
			CHECK(e.column == 1);
			CHECK(e.end_line == 15);
			CHECK(e.end_column == 28);
			auto *statement = function->body->statements[1];
			BS_TEST_REQUIRE(statement && statement->type == BSParser::Node::MATCH);
			CHECK_FALSE(static_cast<BSParser::MatchNode *>(statement)->covers_subject_domain);
			CHECK(analyzer.get_highest_completed_phase() == BSAnalyzer::AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL);
			CHECK(analyzer.resolve_body() != OK);
			CHECK(analyzer.analyze() != OK);
			CHECK(parser.get_errors().size() == 2);
		}
	}
	TEST_CASE("flow_recovery_checks_failed_visited_bodies_but_not_unvisited_bodies") {
		StorageFixture fixture;
		for (bool inherited_error : { false, true }) {
			BSParser parser;
			const String source = inherited_error ? "extends MissingBase\nfunc value() -> int:\n\tpass\n" : "func value() -> int:\n\tunknown_name\n";
			BS_TEST_REQUIRE(parser.parse(source, fixture.path("recovery.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			for (const auto &e : parser.get_errors())
				MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column, "-", e.end_line, ":", e.end_column);
			auto *function = parser.get_tree()->get_member("value").function;
			BS_TEST_REQUIRE(function != nullptr);
			CHECK(function->resolved_body == !inherited_error);
			BS_TEST_REQUIRE(parser.get_errors().size() == (inherited_error ? 3 : 2));
			int index = 0;
			for (const auto &e : parser.get_errors()) {
				if (inherited_error) {
					CHECK(e.message == R"(Could not find base class "MissingBase".)");
					CHECK(e.line == 1);
					CHECK(e.column == 9);
					CHECK(e.end_line == 1);
					CHECK(e.end_column == 20);
				} else if (index == 0) {
					CHECK(e.message == R"(Identifier "unknown_name" not declared in the current scope.)");
					CHECK(e.line == 2);
					CHECK(e.column == 5);
					CHECK(e.end_line == 2);
					CHECK(e.end_column == 17);
				} else {
					CHECK(e.message == "Not all code paths return a value.");
					CHECK(e.line == 1);
					CHECK(e.column == 1);
					CHECK(e.end_line == 2);
					CHECK(e.end_column == 18);
				}
				++index;
			}
			if (!inherited_error) {
				CHECK(analyzer.resolve_body() != OK);
				CHECK(parser.get_errors().size() == 2);
			}
		}
	}
}
