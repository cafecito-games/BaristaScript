/**************************************************************************/
/*  declaration_cycle_analyzer_tests.cpp                                  */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_analyzer_probe.h"
#include "bs_conformance_registry.h"
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

const BSParser::ParserError *error_at(const BSParser &p_parser, int p_index) {
	auto *entry = p_parser.get_errors().front();
	for (int i = 0; entry != nullptr && i < p_index; ++i) {
		entry = entry->next();
	}
	return entry != nullptr ? &entry->get() : nullptr;
}

void exact_error(const BSParser &p_parser, int p_index, const String &p_message, const BSParser::Node *p_origin) {
	BS_TEST_REQUIRE(p_origin != nullptr);
	const auto *error = error_at(p_parser, p_index);
	BS_TEST_REQUIRE(error != nullptr);
	CHECK(error->message == p_message);
	CHECK(error->line == p_origin->start_line);
	CHECK(error->column == p_origin->start_column);
	CHECK(error->end_line == p_origin->end_line);
	CHECK(error->end_column == p_origin->end_column);
}

String error_block(const BSParser &p_parser) {
	String block;
	for (const auto &error : p_parser.get_errors()) {
		if (!block.is_empty()) {
			block += "\n";
		}
		block += vformat(">> ERROR at line %d: %s", error.line, error.message);
	}
	return block.is_empty() ? String("BS_TEST_OK") : block;
}

String public_block(const String &p_source, const String &p_path) {
	Ref<BaristaScriptAnalyzerProbe> probe;
	probe.instantiate();
	const Dictionary result = probe->validate_source(p_source, p_path, true);
	String block;
	for (const Variant &value : Array(result.get("errors", Array()))) {
		const Dictionary error = value;
		if (!block.is_empty()) {
			block += "\n";
		}
		block += vformat(">> ERROR at line %d: %s", int(error.get("line", 0)), String(error.get("message", "")));
	}
	CHECK(Array(result.get("warnings", Array())).is_empty());
	return block.is_empty() ? String("BS_TEST_OK") : block;
}

using Inspect = std::function<void(const BSParser &)>;

void original(const String &p_name, const String &p_source, const String &p_expected, const Inspect &p_inspect) {
	StorageFixture storage;
	BSConformanceRegistry::ScopedCorpusState registry;
	WarningScope warnings;
	const String path = storage.path(p_name);
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(p_source, path, false) == OK);
	BSAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
	for (const auto &error : parser.get_errors()) {
		MESSAGE(std::string(error.message.utf8().get_data()), " at ", error.line, ":", error.column, "-", error.end_line, ":", error.end_column);
	}
	CHECK(error_block(parser) == p_expected);
	CHECK(public_block(p_source, path) == p_expected);
	CHECK(parser.get_warnings().is_empty());
	p_inspect(parser);
	const int errors = parser.get_errors().size();
	CHECK(analyzer.analyze() != OK);
	CHECK(parser.get_errors().size() == errors);
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
	CHECK(parser.get_errors().is_empty());
	CHECK(parser.get_warnings().is_empty());
	CHECK(public_block(p_source, path) == "BS_TEST_OK");
}

} // namespace

TEST_SUITE("declaration_cycle_analyzer") {
	TEST_CASE("cyclic_inheritance_uses_the_pinned_diagnostic") {
		const String source = "func test():\n\tprint(InnerA.new())\n\nclass InnerA extends InnerB:\n\tpass\n\nclass InnerB extends InnerA:\n\tpass\n";
		original("cyclic_inheritance.barista", source, ">> ERROR at line 4: Cyclic inheritance.", [](const BSParser &parser) {
			const auto member = parser.get_tree()->get_member("InnerA");
			BS_TEST_REQUIRE(member.type == BSParser::ClassNode::Member::CLASS && member.m_class != nullptr);
			exact_error(parser, 0, "Cyclic inheritance.", member.m_class);
		});
	}

	TEST_CASE("direct_self_extends_keeps_the_established_cycle_diagnostic") {
		const String source = "class Foo extends Foo:\n\tpass\n";
		original("direct_self_extends.barista", source, ">> ERROR at line 1: Could not resolve class \"Foo\": Cyclic reference.", [](const BSParser &parser) {
			const auto member = parser.get_tree()->get_member("Foo");
			BS_TEST_REQUIRE(member.type == BSParser::ClassNode::Member::CLASS && member.m_class != nullptr);
			exact_error(parser, 0, "Could not resolve class \"Foo\": Cyclic reference.", member.m_class);
		});
	}

	TEST_CASE("cyclic_constants_keep_only_the_pinned_failures") {
		const String source = "func test():\n\tprint(c1)\n\nconst c1 = c2\nconst c2 = c1\n";
		original("cyclic_ref_const.barista", source,
				">> ERROR at line 5: Could not resolve member \"c1\": Cyclic reference.\n>> ERROR at line 5: Could not resolve type for constant \"c2\".",
				[](const BSParser &parser) {
					const auto member = parser.get_tree()->get_member("c2");
					BS_TEST_REQUIRE(member.type == BSParser::ClassNode::Member::CONSTANT && member.constant != nullptr);
					BS_TEST_REQUIRE(member.constant->initializer != nullptr && member.constant->initializer->type == BSParser::Node::IDENTIFIER);
					exact_error(parser, 0, "Could not resolve member \"c1\": Cyclic reference.", member.constant->initializer);
					exact_error(parser, 1, "Could not resolve type for constant \"c2\".", member.constant->initializer);
				});
	}

	TEST_CASE("cyclic_function_defaults_replay_the_member_cycle") {
		const String source = "func test():\n\tprint(f1())\n\tprint(f2())\n\nstatic func f1(p := f2()) -> int:\n\treturn 1\n\nstatic func f2(p := f1()) -> int:\n\treturn 2\n";
		original("cyclic_ref_func.barista", source,
				">> ERROR at line 8: Could not resolve member \"f1\": Cyclic reference.\n>> ERROR at line 8: Cannot infer the type of \"p\" parameter because the value doesn't have a set type.",
				[](const BSParser &parser) {
					const auto member = parser.get_tree()->get_member("f2");
					BS_TEST_REQUIRE(member.type == BSParser::ClassNode::Member::FUNCTION && member.function != nullptr);
					BS_TEST_REQUIRE(member.function->parameters.size() == 1 && member.function->parameters[0]->initializer != nullptr);
					auto *call = static_cast<BSParser::CallNode *>(member.function->parameters[0]->initializer);
					BS_TEST_REQUIRE(call->callee != nullptr);
					exact_error(parser, 0, "Could not resolve member \"f1\": Cyclic reference.", call);
					exact_error(parser, 1, "Cannot infer the type of \"p\" parameter because the value doesn't have a set type.", call);
				});
	}

	TEST_CASE("distinct_function_default_back_edges_each_replay_the_cycle") {
		const String source = "static func f1(a := f2(), b := f3()) -> int:\n\treturn 1\nstatic func f2(p := f1()) -> int:\n\treturn 2\nstatic func f3(p := f1()) -> int:\n\treturn 3\n";
		original("review_140_two_default_back_edges.barista", source,
				">> ERROR at line 3: Could not resolve member \"f1\": Cyclic reference.\n>> ERROR at line 3: Cannot infer the type of \"p\" parameter because the value doesn't have a set type.\n>> ERROR at line 5: Could not resolve member \"f1\": Cyclic reference.\n>> ERROR at line 5: Cannot infer the type of \"p\" parameter because the value doesn't have a set type.",
				[](const BSParser &parser) {
					const auto f2_member = parser.get_tree()->get_member("f2");
					const auto f3_member = parser.get_tree()->get_member("f3");
					BS_TEST_REQUIRE(f2_member.type == BSParser::ClassNode::Member::FUNCTION && f2_member.function != nullptr);
					BS_TEST_REQUIRE(f3_member.type == BSParser::ClassNode::Member::FUNCTION && f3_member.function != nullptr);
					BS_TEST_REQUIRE(f2_member.function->parameters.size() == 1 && f2_member.function->parameters[0]->initializer != nullptr);
					BS_TEST_REQUIRE(f3_member.function->parameters.size() == 1 && f3_member.function->parameters[0]->initializer != nullptr);
					auto *f2_call = static_cast<BSParser::CallNode *>(f2_member.function->parameters[0]->initializer);
					auto *f3_call = static_cast<BSParser::CallNode *>(f3_member.function->parameters[0]->initializer);
					exact_error(parser, 0, "Could not resolve member \"f1\": Cyclic reference.", f2_call);
					exact_error(parser, 1, "Cannot infer the type of \"p\" parameter because the value doesn't have a set type.", f2_call);
					exact_error(parser, 2, "Could not resolve member \"f1\": Cyclic reference.", f3_call);
					exact_error(parser, 3, "Cannot infer the type of \"p\" parameter because the value doesn't have a set type.", f3_call);
				});
	}

	TEST_CASE("cyclic_override_defaults_replay_the_member_cycle") {
		const String source = "func test():\n\tprint(v)\n\nvar v := InnerA.new().f()\n\nclass InnerA:\n\tfunc f(p := InnerB.new().f()) -> int:\n\t\treturn 1\n\nclass InnerB extends InnerA:\n\tfunc f(p := 1) -> int:\n\t\treturn super.f()\n";
		original("cyclic_ref_override.barista", source, ">> ERROR at line 11: Could not resolve member \"f\": Cyclic reference.", [](const BSParser &parser) {
			const auto child_member = parser.get_tree()->get_member("InnerB");
			BS_TEST_REQUIRE(child_member.type == BSParser::ClassNode::Member::CLASS && child_member.m_class != nullptr);
			const auto function_member = child_member.m_class->get_member("f");
			BS_TEST_REQUIRE(function_member.type == BSParser::ClassNode::Member::FUNCTION && function_member.function != nullptr);
			exact_error(parser, 0, "Could not resolve member \"f\": Cyclic reference.", function_member.function);
		});
	}

	TEST_CASE("noncyclic_declaration_controls") {
		legal("noncyclic_constants.barista", "const c1 = 1\nconst c2 = c1\nfunc test():\n\tprint(c2)\n");
		legal("noncyclic_defaults.barista", "static func f1(p := 1) -> int:\n\treturn p\nstatic func f2(p := f1()) -> int:\n\treturn p\nfunc test():\n\tprint(f2())\n");
		legal("noncyclic_override.barista", "class Base:\n\tfunc f(p := 1) -> int:\n\t\treturn p\nclass Child extends Base:\n\tfunc f(p := 2) -> int:\n\t\treturn super.f(p)\n");
	}
}
