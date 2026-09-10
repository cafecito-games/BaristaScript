/**************************************************************************/
/*  variadic_callable_analyzer_tests.cpp                                  */
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
void diagnostics(const BSParser &parser) {
	for (const auto &e : parser.get_errors())
		MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column, "-", e.end_line, ":", e.end_column);
	for (const auto &w : parser.get_warnings())
		MESSAGE(std::string(w.get_name().utf8().get_data()), ": ", std::string(w.get_message().utf8().get_data()), " at ", w.start_line, ":", w.start_column, "-", w.end_line, ":", w.end_column);
}
void error_at(const BSParser &parser, int index, const String &message, const BSParser::Node *site) {
	BS_TEST_REQUIRE(index >= 0 && index < parser.get_errors().size() && site);
	auto *entry = parser.get_errors().front();
	for (int i = 0; i < index; i++)
		entry = entry->next();
	const auto &e = entry->get();
	CHECK(std::string(e.message.utf8().get_data()) == std::string(message.utf8().get_data()));
	CHECK(e.line == site->start_line);
	CHECK(e.column == site->start_column);
	CHECK(e.end_line == site->end_line);
	CHECK(e.end_column == site->end_column);
}
void exact_error(const BSParser &parser, const String &message, const BSParser::Node *site) {
	BS_TEST_REQUIRE(parser.get_errors().size() == 1);
	error_at(parser, 0, message, site);
}
const BSParser::VariableNode *local(const BSParser &parser, const StringName &name) {
	const auto member = parser.get_tree()->get_member("test");
	if (member.type != BSParser::ClassNode::Member::FUNCTION || !member.function || !member.function->body)
		return nullptr;
	for (auto *statement : member.function->body->statements)
		if (statement && statement->type == BSParser::Node::VARIABLE) {
			const auto *v = static_cast<const BSParser::VariableNode *>(statement);
			if (v->identifier && v->identifier->name == name)
				return v;
		}
	return nullptr;
}
Dictionary settings_snapshot() {
	Dictionary result;
	const TypedArray<Dictionary> properties = ProjectSettings::get_singleton()->get_property_list();
	for (int i = 0; i < properties.size(); i++) {
		const Dictionary item = properties[i];
		const String name = item["name"];
		if (ProjectSettings::get_singleton()->has_setting(name))
			result[name] = ProjectSettings::get_singleton()->get_setting(name);
	}
	return result;
}
struct SettingsRestore {
	Dictionary original = settings_snapshot();
	~SettingsRestore() {
		const Array current = settings_snapshot().keys();
		for (int i = 0; i < current.size(); i++)
			if (!original.has(current[i]))
				ProjectSettings::get_singleton()->set_setting(current[i], Variant());
		const Array keys = original.keys();
		for (int i = 0; i < keys.size(); i++)
			ProjectSettings::get_singleton()->set_setting(keys[i], original[keys[i]]);
	}
};
void public_agreement(const String &source, const String &path, bool valid) {
	// Public analysis intentionally publishes temporary conformance records. The shared
	// corpus guard isolates that documented effect while index/settings/source stay read-only.
	BSConformanceRegistry::ScopedCorpusState registry;
	Ref<BaristaScriptAnalyzerProbe> probe;
	probe.instantiate();
	CHECK(bool(probe->analyze_source(source, path).get("valid", !valid)) == valid);
	CHECK(bool(BaristaScriptLanguage::get_singleton()->_validate(source, path, true, true, true, true).get("valid", !valid)) == valid);
	Ref<BaristaScript> script;
	script.instantiate();
	script->_set_source_code(source);
	script->set_path(path);
	CHECK(script->_is_valid() == valid);
}
const BSParser::CallNode *last_call(const BSParser &parser) {
	const auto m = parser.get_tree()->get_member("test");
	if (m.type != BSParser::ClassNode::Member::FUNCTION || !m.function || !m.function->body || m.function->body->statements.is_empty())
		return nullptr;
	const auto *last = m.function->body->statements[m.function->body->statements.size() - 1];
	return last && last->type == BSParser::Node::CALL ? static_cast<const BSParser::CallNode *>(last) : nullptr;
}
} //namespace
TEST_SUITE("variadic_callable_analyzer") {
	TEST_CASE("pin_typed_variadic_callable_bind_arity") {
		StorageFixture fixture;
		const String source = R"SOURCE(func accept(index: int, ...names: Array[String]) -> bool:
	return index == names.size()

func test() -> void:
	var callback: Callable[[int, ...Array[String]], bool] = accept
	var bound := callback.bind(7)
	print(bound.call())
	var tail_bound := callback.bind("tail")
	print(tail_bound.call(2, "a"))
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/variadic_callable/pin_typed_variadic_callable_bind_arity.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		const auto *bound = local(parser, "bound");
		const auto *tail = local(parser, "tail_bound");
		BS_TEST_REQUIRE(bound && tail);
		CHECK(bound->get_datatype().has_explicit_method_signature);
		CHECK(bound->get_datatype().method_parameter_types.size() == 0);
		CHECK_FALSE((bound->get_datatype().method_info.flags & METHOD_FLAG_VARARG) != 0);
		CHECK(tail->get_datatype().has_explicit_method_signature);
		CHECK(tail->get_datatype().method_parameter_types.size() == 1);
		CHECK((tail->get_datatype().method_info.flags & METHOD_FLAG_VARARG) != 0);
		CHECK(tail->get_datatype().has_method_rest_parameter_type());
		BS_TEST_REQUIRE(tail->get_datatype().method_return_type.size() == 1);
		CHECK(tail->get_datatype().method_return_type[0].builtin_type == Variant::BOOL);
	}

	TEST_CASE("pin_typed_variadic_callable_bind_rest_tail") {
		StorageFixture fixture;
		const String source = R"SOURCE(func test() -> void:
	var callback: Callable[[int, ...Array[String]], bool]
	var bound := callback.bind(7)
	bound.call(1, "a")
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/variadic_callable/pin_typed_variadic_callable_bind_rest_tail.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		const auto *body = parser.get_tree()->get_member("test").function->body;
		BS_TEST_REQUIRE(body && body->statements.size() == 3 && body->statements[2]->type == BSParser::Node::CALL);
		const auto *call = static_cast<const BSParser::CallNode *>(body->statements[2]);
		BS_TEST_REQUIRE(call->arguments.size() == 2);
		exact_error(parser, "Too many arguments for \"call()\" call. Expected at most 0 but received 2.", call->arguments[0]);
	}
	TEST_CASE("inevitable_rest_tail_conflict_is_reported_at_bound_expression") {
		StorageFixture fixture;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func test() -> void:\n\tvar callback: Callable[[int, ...Array[String]], bool]\n\tvar bound = callback.bind(7, 1)\n\tprint(bound)\n", "res://tests/variadic_callable/inevitable.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		const auto *bound = local(parser, "bound");
		BS_TEST_REQUIRE(bound && bound->initializer && bound->initializer->type == BSParser::Node::CALL);
		const auto *call = static_cast<const BSParser::CallNode *>(bound->initializer);
		BS_TEST_REQUIRE(call->arguments.size() == 2);
		// Pin16199 reports inevitable surplus first;16249 then reports the earlier
		// rest conflict because no invocation arity survives.
		BS_TEST_REQUIRE(parser.get_errors().size() == 2);
		error_at(parser, 0, "Invalid argument for \"bind()\" function: argument 2 should be \"String\" but is \"int\".", call->arguments[1]);
		error_at(parser, 1, "Invalid argument for \"bind()\" function: argument 1 should be \"String\" but is \"int\".", call->arguments[0]);
	}
	TEST_CASE("separated_zero_two_arities_reject_one_through_call_and_callv") {
		for (bool callv : { false, true })
			for (int count : { 0, 1, 2 }) {
				StorageFixture fixture;
				BSParser parser;
				const String arguments = count == 0 ? "" : count == 1 ? "1"
																	  : "1, \"x\"";
				const String source = "func target(first: int, second: String = \"\", third: int = 0, ...tail: Array[String]) -> bool:\n\treturn first == third + second.length() + tail.size()\nfunc test() -> void:\n\tvar bound = target.bind(7)\n\tbound." + String(callv ? "callv([" : "call(") + arguments + String(callv ? "])\n" : ")\n");
				const Error parsed = parser.parse(source, "res://tests/variadic_callable/hole.barista", false);
				diagnostics(parser);
				BS_TEST_REQUIRE(parsed == OK);
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() == OK) == (count != 1));
				diagnostics(parser);
				if (count == 1) {
					const auto *call = last_call(parser);
					BS_TEST_REQUIRE(call && call->arguments.size() == 1);
					exact_error(parser, "Too few arguments for \"" + String(callv ? "callv" : "call") + "()\" call. Expected at least 2 but received 1.", callv ? static_cast<const BSParser::Node *>(call->arguments[0]) : call);
				}

				const auto *bound = local(parser, "bound");
				BS_TEST_REQUIRE(bound);
				const auto type = bound->get_datatype();
				CHECK(type.has_explicit_method_signature);
				CHECK(type.method_parameter_types.size() == 2);
				CHECK(type.method_info.default_arguments.is_empty());
				CHECK_FALSE((type.method_info.flags & METHOD_FLAG_VARARG) != 0);
				CHECK(type.method_extra_allowed_argument_counts.size() == 1);
				if (type.method_extra_allowed_argument_counts.size() == 1)
					CHECK(type.method_extra_allowed_argument_counts[0] == 0);
			}
	}
	TEST_CASE("literal_bindv_matches_bind_with_prefix_defaults_rest_and_holes") {
		for (bool bindv : { false, true })
			for (int mode : { 0, 1, 2 }) {
				StorageFixture fixture;
				BSParser parser;
				const String declaration = mode == 0 ? "func target(first: int, ...tail: Array[String]) -> bool:\n\treturn first == tail.size()\n" : "func target(first: int, second: String = \"\", third: int = 0, ...tail: Array[String]) -> bool:\n\treturn first == third + second.length() + tail.size()\n";
				const String values = mode == 0 ? "\"tail\"" : mode == 1 ? "7"
																		 : "";
				const String source = declaration + String("func test() -> void:\n\tvar bound = target.") + String(bindv ? "bindv([" : "bind(") + values + String(bindv ? "])\n" : ")\n") + "\tprint(bound)\n";
				const Error parsed = parser.parse(source, "res://tests/variadic_callable/parity.barista", false);
				diagnostics(parser);
				BS_TEST_REQUIRE(parsed == OK);
				BSAnalyzer analyzer(&parser);
				CHECK(analyzer.analyze() == OK);
				diagnostics(parser);
				const auto *bound = local(parser, "bound");
				BS_TEST_REQUIRE(bound);
				const auto t = bound->get_datatype();
				CHECK(t.has_explicit_method_signature);
				CHECK(t.method_parameter_types.size() == (mode == 0 ? 1 : mode == 1 ? 2
																					: 3));
				CHECK(t.method_info.default_arguments.size() == (mode == 2 ? 2 : 0));
				CHECK(((t.method_info.flags & METHOD_FLAG_VARARG) != 0) == (mode != 1));
				CHECK(t.has_method_rest_parameter_type() == (mode != 1));
				CHECK(t.method_extra_allowed_argument_counts.size() == (mode == 1 ? 1 : 0));
			}
	}
	TEST_CASE("chained_bind_and_unbind_preserve_separated_counts") {
		for (int mode : { 0, 1, 2 })
			for (int count : { 0, 1, 2, 3 }) {
				StorageFixture fixture;
				BSParser parser;
				const String chain = mode == 0 ? ".bind(\"x\")" : mode == 1 ? ".unbind(1)"
																			: ".unbind(1).bind(\"discard\")";
				const String arguments = count == 0 ? "" : count == 1 ? "1"
						: count == 2								  ? "1, \"x\""
																	  : "1, \"x\", false";
				const bool valid = mode == 0 ? count == 1 : mode == 1 ? (count == 1 || count == 3)
																	  : (count == 0 || count == 2);
				const String source = "func target(first: int, second: String = \"\", third: int = 0, ...tail: Array[String]) -> bool:\n\treturn first == third + second.length() + tail.size()\nfunc test() -> void:\n\tvar bound = target.bind(7)" + chain + "\n\tbound.call(" + arguments + ")\n";
				const Error parsed = parser.parse(source, "res://tests/variadic_callable/chained.barista", false);
				diagnostics(parser);
				BS_TEST_REQUIRE(parsed == OK);
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() == OK) == valid);
				diagnostics(parser);
				if (!valid) {
					const auto *call = last_call(parser);
					BS_TEST_REQUIRE(call);
					const int maximum = mode == 0 ? 1 : mode == 1 ? 3
																  : 2;
					const bool too_many = count > maximum;
					const BSParser::Node *site = call;
					if (too_many) {
						BS_TEST_REQUIRE(call->arguments.size() > maximum);
						site = call->arguments[maximum];
					}
					exact_error(parser, "Too " + String(too_many ? "many" : "few") + " arguments for \"call()\" call. Expected at " + String(too_many ? "most " : "least ") + itos(maximum) + " but received " + itos(count) + ".", site);
				}

				const auto *bound = local(parser, "bound");
				BS_TEST_REQUIRE(bound);
				const auto t = bound->get_datatype();
				CHECK(t.has_explicit_method_signature);
				CHECK(t.method_unbound_argument_count == (mode == 1 ? 1 : 0));
				CHECK(t.method_parameter_types.size() == (mode == 0 ? 1 : mode == 1 ? 3
																					: 2));
			}
	}
	TEST_CASE("unbind_then_bind_retains_only_remaining_discard_slots_and_typed_rest") {
		for (bool bindv : { false, true })
			for (int consumed : { 1, 2, 3 })
				for (bool valid : { false, true }) {
					StorageFixture fixture;
					BSParser parser;
					const String values = consumed == 1 ? "false" : consumed == 2 ? "false, 1"
																				  : "\"tail\", false, 1";
					const String arguments = valid ? (consumed == 1 ? "1, \"ok\", false" : "1, \"ok\"") : (consumed == 1 ? "1, false, \"discard\"" : "1, false");
					const String source = "func test() -> void:\n\tvar callback: Callable[[int, ...Array[String]], bool]\n\tvar bound = callback.unbind(2)." + String(bindv ? "bindv([" : "bind(") + values + String(bindv ? "])\n" : ")\n") + "\tbound.call(" + arguments + ")\n";
					const Error parsed = parser.parse(source, "res://tests/variadic_callable/discards.barista", false);
					diagnostics(parser);
					BS_TEST_REQUIRE(parsed == OK);
					BSAnalyzer analyzer(&parser);
					CHECK((analyzer.analyze() == OK) == valid);
					diagnostics(parser);
					if (!valid) {
						const auto *call = last_call(parser);
						BS_TEST_REQUIRE(call);
						const BSParser::Node *site = nullptr;
						if (call->function_name == SNAME("callv")) {
							BS_TEST_REQUIRE(call->arguments.size() == 1 && call->arguments[0]->type == BSParser::Node::ARRAY);
							const auto *array = static_cast<const BSParser::ArrayNode *>(call->arguments[0]);
							BS_TEST_REQUIRE(array->elements.size() > 1);
							site = array->elements[1];
						} else {
							BS_TEST_REQUIRE(call->arguments.size() > 1);
							site = call->arguments[1];
						}
						exact_error(parser, "Invalid argument for \"" + String(call->function_name) + "()\" function: argument 2 should be \"String\" but is \"bool\".", site);
					}
					const auto *bound = local(parser, "bound");
					BS_TEST_REQUIRE(bound);
					const auto t = bound->get_datatype();
					CHECK(t.has_explicit_method_signature);
					CHECK(t.method_unbound_argument_count == (consumed == 1 ? 1 : 0));
					CHECK(t.method_parameter_types.size() == (consumed == 1 ? 2 : 1));
					CHECK(t.method_info.default_arguments.is_empty());
					CHECK((t.method_info.flags & METHOD_FLAG_VARARG) != 0);
					CHECK(t.has_method_rest_parameter_type());
				}
	}
	TEST_CASE("bind_then_unbind_checks_reaching_rest_and_ignores_discarded_values") {
		for (bool callv : { false, true })
			for (bool valid : { false, true }) {
				StorageFixture fixture;
				BSParser parser;
				const String args = valid ? "1, \"ok\", false" : "1, false, \"discard\"";
				const String source = "func test() -> void:\n\tvar callback: Callable[[int, ...Array[String]], bool]\n\tvar bound = callback.bind(\"tail\").unbind(1)\n\tbound." + String(callv ? "callv([" : "call(") + args + String(callv ? "])\n" : ")\n");
				const Error parsed = parser.parse(source, "res://tests/variadic_callable/bind_unbind.barista", false);
				diagnostics(parser);
				BS_TEST_REQUIRE(parsed == OK);
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() == OK) == valid);
				diagnostics(parser);
				if (!valid) {
					const auto *call = last_call(parser);
					BS_TEST_REQUIRE(call);
					const BSParser::Node *site = nullptr;
					if (call->function_name == SNAME("callv")) {
						BS_TEST_REQUIRE(call->arguments.size() == 1 && call->arguments[0]->type == BSParser::Node::ARRAY);
						const auto *array = static_cast<const BSParser::ArrayNode *>(call->arguments[0]);
						BS_TEST_REQUIRE(array->elements.size() > 1);
						site = array->elements[1];
					} else {
						BS_TEST_REQUIRE(call->arguments.size() > 1);
						site = call->arguments[1];
					}
					exact_error(parser, "Invalid argument for \"" + String(call->function_name) + "()\" function: argument 2 should be \"String\" but is \"bool\".", site);
				}
				const auto *bound = local(parser, "bound");
				BS_TEST_REQUIRE(bound);
				CHECK(bound->get_datatype().method_unbound_argument_count == 1);
			}
	}
	TEST_CASE("unknown_bound_values_keep_possible_set_when_strict_set_is_empty") {
		StorageFixture fixture;
		BSParser parser;
		const String source = "func test(value: Variant) -> void:\n\tvar callback: Callable[[int, int, ...Array[String]], bool]\n\tvar bound = callback.bind(value, 7)\n\tbound.call()\n";
		const Error parsed = parser.parse(source, "res://tests/variadic_callable/unknown.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		const auto *bound = local(parser, "bound");
		BS_TEST_REQUIRE(bound);
		CHECK(bound->get_datatype().has_explicit_method_signature);
		CHECK(bound->get_datatype().method_parameter_types.is_empty());
		CHECK_FALSE((bound->get_datatype().method_info.flags & METHOD_FLAG_VARARG) != 0);
	}
	TEST_CASE("async_result_and_nonliteral_bindv_gradual_signature_are_preserved") {
		for (bool literal : { false, true })
			for (bool callv : { false, true }) {
				StorageFixture fixture;
				BSParser parser;
				const String source = "func test(values: Array) -> void:\n\tvar callback: AsyncCallable[[int, ...Array[String]], bool]\n\tvar bound = callback.bindv(" + String(literal ? "[\"tail\"]" : "values") + ")\n\tvar result = bound." + String(callv ? "callv([1])" : "call(1)") + "\n\tprint(result)\n";
				const Error parsed = parser.parse(source, "res://tests/variadic_callable/async.barista", false);
				diagnostics(parser);
				BS_TEST_REQUIRE(parsed == OK);
				BSAnalyzer analyzer(&parser);
				CHECK(analyzer.analyze() == OK);
				diagnostics(parser);
				const auto *bound = local(parser, "bound");
				const auto *result = local(parser, "result");
				BS_TEST_REQUIRE(bound && result);
				CHECK(bound->get_datatype().signature_is_async);
				CHECK(bound->get_datatype().has_explicit_method_signature == literal);
				CHECK(result->get_datatype().is_coroutine);
				BS_TEST_REQUIRE(result->get_datatype().container_element_types.size() == 1);
				CHECK(result->get_datatype().container_element_types[0].builtin_type == (literal ? Variant::BOOL : Variant::NIL));
				CHECK(result->get_datatype().container_element_types[0].is_variant() == !literal);
				CHECK(parser.get_warnings().size() == (literal ? 1 : 0));
				if (literal) {
					BS_TEST_REQUIRE(parser.get_warnings().size() == 1);
					const auto &w = parser.get_warnings().front()->get();
					const auto *parameter = parser.get_tree()->get_member("test").function->parameters[0];
					CHECK(w.code == BSWarning::UNUSED_PARAMETER);
					CHECK(w.get_message() == "The parameter \"values\" is never used in the function \"test()\". If this is intended, prefix it with an underscore: \"_values\".");
					CHECK(w.start_line == parameter->start_line);
					CHECK(w.start_column == parameter->start_column);
					CHECK(w.end_line == parameter->end_line);
					CHECK(w.end_column == parameter->end_column);
				}
			}
	}
	TEST_CASE("variadic_prior_extra_counts_survive_empty_bind_and_consumed_discard") {
		for (int mode : { 0, 1, 2 })
			for (int count : { 0, 1, 2, 3, 4 }) {
				StorageFixture fixture;
				BSParser parser;
				const String chain = mode == 0 ? ".bind()" : mode == 1 ? ".bindv([])"
																	   : ".unbind(1).bind(false)";
				const String args = count == 0 ? "" : count == 1 ? "1"
						: count == 2							 ? "1, \"x\""
						: count == 3							 ? "1, \"x\", 2"
																 : "1, \"x\", 2, 3";
				const String source = "func target(first: int, second: String = \"\", third: int = 0, ...tail: Array[int]) -> bool:\n\treturn first == third + second.length() + tail.size()\nfunc test() -> void:\n\tvar bound = target.bind(7)" + chain + "\n\tbound.call(" + args + ")\n";
				const Error parsed = parser.parse(source, "res://tests/variadic_callable/prior_extras.barista", false);
				diagnostics(parser);
				BS_TEST_REQUIRE(parsed == OK);
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() == OK) == (count != 1));
				diagnostics(parser);
				const auto *bound = local(parser, "bound");
				BS_TEST_REQUIRE(bound);
				const auto t = bound->get_datatype();
				CHECK(t.has_explicit_method_signature);
				CHECK(t.method_parameter_types.size() == 3);
				CHECK(t.method_info.default_arguments.size() == 1);
				CHECK(t.method_extra_allowed_argument_counts.size() == 1);
				if (t.method_extra_allowed_argument_counts.size() == 1)
					CHECK(t.method_extra_allowed_argument_counts[0] == 0);
				CHECK(t.method_unbound_argument_count == 0);
				CHECK((t.method_info.flags & METHOD_FLAG_VARARG) != 0);
				if (count == 1) {
					const auto *call = last_call(parser);
					BS_TEST_REQUIRE(call);
					exact_error(parser, "Too few arguments for \"call()\" call. Expected at least 2 but received 1.", call);
				}
			}
	}
	TEST_CASE("strict_dynamic_bound_value_reports_full_ordered_conflicts") {
		SettingsRestore restore;
		StorageFixture fixture;
		ProjectSettings::get_singleton()->set_setting("debug/barista_script/analysis/strict_dynamic_checks", true);
		BSParser parser;
		const String source = "func test(value: Variant) -> void:\n\tvar callback: Callable[[int, int, ...Array[String]], bool]\n\tvar bound = callback.bind(value, 7)\n\tprint(bound)\n";
		const Error parsed = parser.parse(source, "res://tests/variadic_callable/strict.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		const auto *bound = local(parser, "bound");
		BS_TEST_REQUIRE(bound && bound->initializer && bound->initializer->type == BSParser::Node::CALL);
		const auto *bind = static_cast<const BSParser::CallNode *>(bound->initializer);
		BS_TEST_REQUIRE(bind->arguments.size() == 2);
		BS_TEST_REQUIRE(parser.get_errors().size() == 2);
		error_at(parser, 0, "Cannot pass Variant value as argument 1 of \"bind()\" in strict dynamic mode; expected \"String\".", bind->arguments[0]);
		error_at(parser, 1, "Invalid argument for \"bind()\" function: argument 2 should be \"String\" but is \"int\".", bind->arguments[1]);
	}
	TEST_CASE("literal_bindv_and_zero_prefix_report_inevitable_rest_mismatches") {
		for (bool zero_prefix : { false, true }) {
			StorageFixture fixture;
			BSParser parser;
			const String source = "func test() -> void:\n\tvar callback: Callable[[" + String(zero_prefix ? "" : "int, ") + "...Array[String]], bool]\n\tvar bound = callback.bindv([" + String(zero_prefix ? "7" : "7, 1") + "])\n\tprint(bound)\n";
			const Error parsed = parser.parse(source, "res://tests/variadic_callable/bindv_error.barista", false);
			diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			const auto *bound = local(parser, "bound");
			BS_TEST_REQUIRE(bound && bound->initializer && bound->initializer->type == BSParser::Node::CALL);
			const auto *call = static_cast<const BSParser::CallNode *>(bound->initializer);
			BS_TEST_REQUIRE(call->arguments.size() == 1 && call->arguments[0]->type == BSParser::Node::ARRAY);
			const auto *array = static_cast<const BSParser::ArrayNode *>(call->arguments[0]);
			BS_TEST_REQUIRE(array->elements.size() == (zero_prefix ? 1 : 2));
			BS_TEST_REQUIRE(parser.get_errors().size() == (zero_prefix ? 1 : 2));
			if (!zero_prefix)
				error_at(parser, 0, "Invalid argument for \"bindv()\" function: argument 2 should be \"String\" but is \"int\".", array->elements[1]);
			error_at(parser, zero_prefix ? 0 : 1, "Invalid argument for \"bindv()\" function: argument 1 should be \"String\" but is \"int\".", array->elements[0]);
		}
	}
	TEST_CASE("public_variadic_callable_agreement_preserves_index_settings_and_overrides") {
		const Dictionary startup = settings_snapshot();
		{
			SettingsRestore restore;
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			const String path = "res://tests/variadic_callable/public.barista";
			const String indexed = "class_name Existing\n";
			const auto record = BSDeclarationIndex::record_from_global_class(path, indexed, bs_resolve_global_class_from_source(indexed, path));
			BS_TEST_REQUIRE(fixture.index().commit_record(fixture.index().claim_refresh(path), record));
			const Dictionary setup = settings_snapshot();
			const uint64_t revision = fixture.index().get_refresh_revision(path);
			const String store = fixture.path("index.bin");
			BS_TEST_REQUIRE(fixture.index().flush(store) == OK);
			const auto bytes = read_bytes(store);
			for (int state : { 0, 1, 2 }) {
				const String previous = state == 2 ? "class_name Unsaved\n" : "";
				if (state == 0)
					BSCache::clear_source_override(path);
				else
					BSCache::set_source_override(path, previous);
				for (bool valid : { false, true }) {
					const String source = "func test() -> void:\n\tvar callback: Callable[[int, ...Array[String]], bool]\n\tvar bound = callback.bind(7)\n\tbound.call(" + String(valid ? "" : "1, \"bad\"") + ")\n";
					public_agreement(source, path, valid);
					CHECK(settings_snapshot() == setup);
					CHECK(BSCache::has_source_override(path) == (state != 0));
					if (state != 0)
						CHECK(BSCache::get_source_code(path) == previous);
					CHECK(fixture.index().get_refresh_revision(path) == revision);
					CHECK(fixture.index().flush(store) == OK);
					CHECK(read_bytes(store) == bytes);
					CHECK(BSConformanceRegistry::get_singleton()->get_file_conformances(path).is_empty());
					CHECK(BSConformanceRegistry::get_singleton()->get_file_trait_bindings(path).is_empty());
				}
			}
		}
		CHECK(settings_snapshot() == startup);
	}
}
