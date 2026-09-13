/**************************************************************************/
/*  call_admission_analyzer_tests.cpp                                     */
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

#include <functional>
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
		for (int i = 0; i < keys.size(); ++i) {
			settings->set_setting(keys[i], saved[keys[i]]);
		}
		BSParser::update_project_settings();
		for (int i = 0; i < keys.size(); ++i) {
			if (Variant(saved[keys[i]]).get_type() == Variant::NIL && settings->has_setting(keys[i])) {
				settings->clear(keys[i]);
			}
		}
	}
};

void diagnostics(const BSParser &p_parser) {
	for (const auto &error : p_parser.get_errors()) {
		MESSAGE(std::string(error.message.utf8().get_data()), " at ", error.line, ":", error.column, "-", error.end_line, ":", error.end_column);
	}
}

const BSParser::ParserError *error_at_index(const BSParser &p_parser, int p_index) {
	CHECK(p_index >= 0);
	CHECK(p_index < p_parser.get_errors().size());
	if (p_index < 0 || p_index >= p_parser.get_errors().size()) {
		return nullptr;
	}
	auto *entry = p_parser.get_errors().front();
	for (int i = 0; i < p_index; ++i) {
		entry = entry->next();
	}
	return &entry->get();
}

void exact_error(const BSParser &p_parser, int p_index, const String &p_message, const BSParser::Node *p_origin) {
	BS_TEST_REQUIRE(p_origin != nullptr);
	const auto *error = error_at_index(p_parser, p_index);
	BS_TEST_REQUIRE(error != nullptr);
	CHECK(error->message == p_message);
	CHECK(error->line == p_origin->start_line);
	CHECK(error->column == p_origin->start_column);
	CHECK(error->end_line == p_origin->end_line);
	CHECK(error->end_column == p_origin->end_column);
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

String public_block(const String &p_source, const String &p_path, const BSParser &p_parser) {
	Ref<BaristaScriptAnalyzerProbe> probe;
	probe.instantiate();
	const Dictionary result = probe->validate_source(p_source, p_path, true);
	const bool valid = p_parser.get_errors().is_empty();
	CHECK(bool(result.get("valid", !valid)) == valid);
	CHECK(bool(probe->analyze_source(p_source, p_path).get("valid", !valid)) == valid);
	Ref<BaristaScript> script;
	script.instantiate();
	script->set_path(p_path);
	script->_set_source_code(p_source);
	CHECK(script->_is_valid() == valid);
	const Array errors = result.get("errors", Array());
	CHECK(errors.size() == p_parser.get_errors().size());
	String block;
	for (int i = 0; i < errors.size(); ++i) {
		const Dictionary row = errors[i];
		if (i != 0) {
			block += "\n";
		}
		block += vformat(">> ERROR at line %d: %s", int(row.get("line", 0)), String(row.get("message", "")));
	}
	CHECK(Array(result.get("warnings", Array())).is_empty());
	return block.is_empty() ? String("BS_TEST_OK") : block;
}

using Inspect = std::function<void(const BSParser &)>;

void original(StorageFixture &p_storage, const String &p_name, const String &p_source, const String &p_expected, const Inspect &p_inspect) {
	BSConformanceRegistry::ScopedCorpusState registry;
	WarningScope warnings;
	const String path = p_storage.path(p_name);
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(p_source, path, false) == OK);
	BSAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
	diagnostics(parser);
	CHECK(error_block(parser) == p_expected);
	CHECK(public_block(p_source, path, parser) == p_expected);
	CHECK(parser.get_warnings().is_empty());
	p_inspect(parser);
	const int previous_errors = parser.get_errors().size();
	CHECK(analyzer.analyze() != OK);
	CHECK(parser.get_errors().size() == previous_errors);
}

void legal(const String &p_name, const String &p_source) {
	StorageFixture storage;
	BSConformanceRegistry::ScopedCorpusState registry;
	WarningScope warnings;
	const String path = storage.path(p_name);
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(p_source, path, false) == OK);
	BSAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() == OK);
	diagnostics(parser);
	CHECK(parser.get_errors().is_empty());
	CHECK(parser.get_warnings().is_empty());
	CHECK(public_block(p_source, path, parser) == "BS_TEST_OK");
}

BSParser::FunctionNode *test_function(const BSParser &p_parser) {
	const auto member = p_parser.get_tree()->get_member("test");
	CHECK(member.type == BSParser::ClassNode::Member::FUNCTION);
	CHECK(member.function != nullptr);
	if (member.type != BSParser::ClassNode::Member::FUNCTION || member.function == nullptr) {
		return nullptr;
	}
	return member.function;
}

} // namespace

TEST_SUITE("call_admission_analyzer") {
	TEST_CASE("original_call_on_subscript_value") {
		StorageFixture storage;
		const String source = R"BS(# Calling the result of an index expression is an error in every context. The parser
# now lets `name[...](...)` through so call reduction can interpret use-site type
# arguments; until generic-method application lands this stays a call on an expression,
# reported exactly once per call site (no duplicate parser + analyzer diagnostics).
func values():
	return [1]


func test():
	var a = [1]
	a[0]()
	var _b = a[0]()
	values()[0]()


func from_return():
	var a = [1]
	return a[0]()
)BS";
		const String expected = R"BS(>> ERROR at line 11: Cannot call on an expression. Use ".call()" if it's a Callable.
>> ERROR at line 12: Cannot call on an expression. Use ".call()" if it's a Callable.
>> ERROR at line 13: Cannot call on an expression. Use ".call()" if it's a Callable.
>> ERROR at line 18: Cannot call on an expression. Use ".call()" if it's a Callable.)BS";
		original(storage, "call_on_subscript_value.barista", source, expected, [](const BSParser &parser) {
			auto *test = test_function(parser);
			auto *from_return = parser.get_tree()->get_member("from_return").function;
			BS_TEST_REQUIRE(test != nullptr && test->body && test->body->statements.size() == 4 && from_return && from_return->body && from_return->body->statements.size() == 2);
			auto *first = static_cast<BSParser::CallNode *>(test->body->statements[1]);
			auto *initializer = static_cast<BSParser::VariableNode *>(test->body->statements[2]);
			auto *third = static_cast<BSParser::CallNode *>(test->body->statements[3]);
			auto *ret = static_cast<BSParser::ReturnNode *>(from_return->body->statements[1]);
			exact_error(parser, 0, R"BS(Cannot call on an expression. Use ".call()" if it's a Callable.)BS", first);
			exact_error(parser, 1, R"BS(Cannot call on an expression. Use ".call()" if it's a Callable.)BS", initializer->initializer);
			exact_error(parser, 2, R"BS(Cannot call on an expression. Use ".call()" if it's a Callable.)BS", third);
			exact_error(parser, 3, R"BS(Cannot call on an expression. Use ".call()" if it's a Callable.)BS", ret->return_value);
		});
	}

	TEST_CASE("original_callable_member_used_as_function") {
		StorageFixture storage;
		const String source = "var callback: Callable = func():\n\tpass\n\nfunc test():\n\tcallback()\n";
		original(storage, "callable_member_used_as_function.barista", source,
				R"BS(>> ERROR at line 5: Name "callback" is a Callable. You can call it with "callback.call()" instead.)BS",
				[](const BSParser &parser) {
					auto *function = test_function(parser);
					BS_TEST_REQUIRE(function != nullptr && function->body && function->body->statements.size() == 1);
					auto *call = static_cast<BSParser::CallNode *>(function->body->statements[0]);
					exact_error(parser, 0, R"BS(Name "callback" is a Callable. You can call it with "callback.call()" instead.)BS", call->callee);
				});
	}

	TEST_CASE("original_constant_used_as_function") {
		StorageFixture storage;
		const String source = "const CONSTANT = 25\n\n\nfunc test():\n\tCONSTANT(123)\n";
		original(storage, "constant_used_as_function.barista", source,
				R"BS(>> ERROR at line 5: Member "CONSTANT" is not a function.
>> ERROR at line 5: Name "CONSTANT" called as a function but is a "int".)BS",
				[](const BSParser &parser) {
					auto *function = test_function(parser);
					BS_TEST_REQUIRE(function != nullptr && function->body && function->body->statements.size() == 1);
					auto *call = static_cast<BSParser::CallNode *>(function->body->statements[0]);
					exact_error(parser, 0, R"BS(Member "CONSTANT" is not a function.)BS", call);
					exact_error(parser, 1, R"BS(Name "CONSTANT" called as a function but is a "int".)BS", call->callee);
				});
	}

	TEST_CASE("original_construct_abstract_class") {
		StorageFixture storage;
		BS_TEST_REQUIRE(write_bytes(storage.path("construct_abstract_script.notest.barista"), bytes("abstract class_name AbstractScript\n")));
		const String source = "extends RefCounted\n\nconst AbstractScript = preload(\"./construct_abstract_script.notest.barista\")\n\nabstract class AbstractClass:\n\tpass\n\nfunc test():\n\tvar _a := AbstractScript.new()\n\tvar _b := AbstractClass.new()\n";
		original(storage, "construct_abstract_class.barista", source,
				R"BS(>> ERROR at line 9: Cannot construct abstract class "AbstractScript".
>> ERROR at line 10: Cannot construct abstract class "AbstractClass".)BS",
				[](const BSParser &parser) {
					auto *function = test_function(parser);
					BS_TEST_REQUIRE(function != nullptr && function->body && function->body->statements.size() == 2);
					auto *first = static_cast<BSParser::VariableNode *>(function->body->statements[0]);
					auto *second = static_cast<BSParser::VariableNode *>(function->body->statements[1]);
					exact_error(parser, 0, R"BS(Cannot construct abstract class "AbstractScript".)BS", first->initializer);
					exact_error(parser, 1, R"BS(Cannot construct abstract class "AbstractClass".)BS", second->initializer);
				});
	}

	TEST_CASE("original_engine_singleton_instantiate") {
		StorageFixture storage;
		const String source = "func test():\n\tTime.new()\n";
		original(storage, "engine_singleton_instantiate.barista", source,
				R"BS(>> ERROR at line 2: Cannot construct native class "Time" because it is an engine singleton.)BS",
				[](const BSParser &parser) {
					auto *function = test_function(parser);
					BS_TEST_REQUIRE(function != nullptr && function->body && function->body->statements.size() == 1);
					auto *call = static_cast<BSParser::CallNode *>(function->body->statements[0]);
					exact_error(parser, 0, R"BS(Cannot construct native class "Time" because it is an engine singleton.)BS", call);
				});
	}

	TEST_CASE("original_extend_engine_singleton") {
		StorageFixture storage;
		const String source = "# GH-82081\n\nextends Time\n\nfunc test():\n\tpass\n";
		original(storage, "extend_engine_singleton.barista", source,
				R"BS(>> ERROR at line 3: Cannot inherit native class "Time" because it is an engine singleton.)BS",
				[](const BSParser &parser) {
					BS_TEST_REQUIRE(parser.get_tree()->extends.size() == 1);
					exact_error(parser, 0, R"BS(Cannot inherit native class "Time" because it is an engine singleton.)BS", parser.get_tree()->extends[0]);
				});
	}

	TEST_CASE("original_engine_enum_type_handles_are_not_values") {
		const struct {
			const char *name;
			const char *source;
			const char *message;
		} cases[] = {
			{ "enum_builtin_access.barista", "func test():\n\tprint(Vector3.Axis)\n", R"BS(Type "Axis" in base "Vector3" cannot be used on its own.)BS" },
			{ "enum_global_access.barista", "func test():\n\tprint(Variant.Operator)\n", R"BS(Type "Operator" in base "Variant" cannot be used on its own.)BS" },
			{ "enum_native_access.barista", "func test():\n\tprint(Node.ProcessMode)\n", R"BS(Type "ProcessMode" in base "Node" cannot be used on its own.)BS" },
		};
		for (const auto &entry : cases) {
			StorageFixture storage;
			CAPTURE(std::string(entry.name));
			original(storage, entry.name, entry.source, String(">> ERROR at line 2: ") + entry.message,
					[&](const BSParser &parser) {
						auto *function = test_function(parser);
						BS_TEST_REQUIRE(function != nullptr && function->body && function->body->statements.size() == 1);
						auto *call = static_cast<BSParser::CallNode *>(function->body->statements[0]);
						BS_TEST_REQUIRE(call->arguments.size() == 1 && call->arguments[0]->type == BSParser::Node::SUBSCRIPT);
						auto *subscript = static_cast<BSParser::SubscriptNode *>(call->arguments[0]);
						exact_error(parser, 0, entry.message, subscript->attribute);
					});
		}
	}

	TEST_CASE("original_nested_class_names_use_class_diagnostic") {
		const struct {
			const char *name;
			const char *path;
		} cases[] = {
			{ "AsyncCallable", "class_name_shadows_async_callable.barista" },
			{ "Vector2", "class_name_shadows_builtin_type.barista" },
		};
		for (const auto &entry : cases) {
			StorageFixture storage;
			const String source = String("class ") + entry.name + ":\n\tpass\n\nfunc test():\n\tpass\n";
			const String message = vformat(R"BS(Class "%s" hides a built-in type.)BS", entry.name);
			CAPTURE(std::string(entry.name));
			original(storage, entry.path, source,
					String(">> ERROR at line 1: ") + message,
					[&](const BSParser &parser) {
						auto *nested = parser.get_tree()->get_member(entry.name).m_class;
						BS_TEST_REQUIRE(nested && nested->identifier);
						exact_error(parser, 0, message, nested->identifier);
					});
		}
	}

	TEST_CASE("original_final_receiver_unresolved_method") {
		StorageFixture storage;
		const String source = "# A `final` class has no subtypes, so a method the class does not declare can never be supplied by\n# something the analyzer cannot see. The call below used to pass analysis in silence — the\n# `UNSAFE_METHOD_ACCESS` warning that covered it is ignored by default — and failed only when the\n# line ran.\nfinal class FrmMessage:\n\tfunc send() -> void:\n\t\tpass\n\n\nfunc test() -> void:\n\tvar message := FrmMessage.new()\n\tmessage.deliver()\n";
		const String message = R"BS(Cannot call "deliver()" on "FrmMessage": the method does not exist, and the class is final, so no subtype can supply it.)BS";
		original(storage, "final_receiver_unresolved_method.barista", source, String(">> ERROR at line 12: ") + message,
				[&](const BSParser &parser) {
					auto *function = test_function(parser);
					BS_TEST_REQUIRE(function != nullptr && function->body && function->body->statements.size() == 2);
					auto *call = static_cast<BSParser::CallNode *>(function->body->statements[1]);
					exact_error(parser, 0, message, call->callee);
				});
	}

	TEST_CASE("legal_adjacent_call_and_admission_controls") {
		legal("call_controls.barista", "var callback: Callable = func():\n\tpass\nfunc test():\n\tcallback.call()\n\tvar callbacks: Array[Callable] = [callback]\n\tcallbacks[0].call()\n");
		legal("construction_controls.barista", "class Concrete:\n\tpass\nfunc test():\n\tvar value := Concrete.new()\n");
		legal("inheritance_control.barista", "extends Node\nfunc test():\n\tpass\n");
		legal("enum_member_controls.barista", "func test():\n\tvar a: int = Vector3.Axis.AXIS_X\n\tvar b: int = Variant.Operator.OP_ADD\n\tvar c: int = Node.ProcessMode.PROCESS_MODE_INHERIT\n");
		legal("class_name_control.barista", "class Custom:\n\tpass\nfunc test():\n\tpass\n");
		legal("final_method_control.barista", "final class Message:\n\tfunc deliver():\n\t\tpass\nfunc test():\n\tvar message := Message.new()\n\tmessage.deliver()\n");
		legal("open_receiver_control.barista", "class Message:\n\tpass\nfunc test():\n\tvar message := Message.new()\n\tmessage.deliver()\n");
	}
}
