/**************************************************************************/
/*  builtin_metadata_analyzer_tests.cpp                                   */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                         */
/*  This file is part of BaristaScript, a Godot GDExtension.               */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_analyzer_probe.h"
#include "bs_conformance_registry.h"
#include "bs_core_constants.h"
#include "bs_global_class.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <string>
using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;
namespace barista_script {
struct BSCoreConstantsTestAccess {
	static String projection() { return BSCoreConstants::builtin_projection_for_tests(); }
};
} //namespace barista_script
namespace {
void diagnostics(const BSParser &parser) {
	for (const auto &e : parser.get_errors())
		MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column, "-", e.end_line, ":", e.end_column);
	for (const auto &w : parser.get_warnings())
		MESSAGE(std::string(w.get_name().utf8().get_data()), ": ", std::string(w.get_message().utf8().get_data()), " at ", w.start_line, ":", w.start_column, "-", w.end_line, ":", w.end_column);
}
void error_tuple(const BSParser &parser, int index, const String &message, int line, int column, int end_line, int end_column) {
	BS_TEST_REQUIRE(index >= 0 && index < parser.get_errors().size());
	auto *entry = parser.get_errors().front();
	for (int i = 0; i < index; ++i)
		entry = entry->next();
	const auto &error = entry->get();
	CHECK(std::string(error.message.utf8().get_data()) == std::string(message.utf8().get_data()));
	CHECK(error.line == line);
	CHECK(error.column == column);
	CHECK(error.end_line == end_line);
	CHECK(error.end_column == end_column);
}
void warning_tuple(const BSParser &parser, int index, const String &name, const String &message, int line, int column, int end_line, int end_column) {
	BS_TEST_REQUIRE(index >= 0 && index < parser.get_warnings().size());
	auto *entry = parser.get_warnings().front();
	for (int i = 0; i < index; ++i)
		entry = entry->next();
	const auto &warning = entry->get();
	CHECK(warning.get_name() == name);
	CHECK(warning.get_message() == message);
	CHECK(warning.start_line == line);
	CHECK(warning.start_column == column);
	CHECK(warning.end_line == end_line);
	CHECK(warning.end_column == end_column);
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
Variant::Type carrier(const String &name) {
	if (name == "Variant" || name.is_empty())
		return Variant::NIL;
	for (int i = 0; i < Variant::VARIANT_MAX; ++i)
		if (Variant::get_type_name(Variant::Type(i)) == name)
			return Variant::Type(i);
	return Variant::VARIANT_MAX;
}
void property_matches(const PropertyInfo &actual, const String &type, const String &name) {
	CHECK(actual.type == carrier(type));
	CHECK(actual.name == StringName(name));
	CHECK(bool(actual.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == (type == "Variant"));
}
void signature_matches(const MethodInfo &actual, const Dictionary &row, const String &constructor = String()) {
	property_matches(actual.return_val, constructor.is_empty() ? String(row.get("return_type", "")) : constructor, "");
	const Array arguments = row.get("arguments", Array());
	BS_TEST_REQUIRE(actual.arguments.size() == arguments.size());
	int defaults = 0, index = 0;
	for (const auto &argument : actual.arguments) {
		const Dictionary expected = arguments[index++];
		property_matches(argument, expected["type"], expected["name"]);
		defaults += expected.has("default_value");
	}
	BS_TEST_REQUIRE(actual.default_arguments.size() == defaults);
	for (int i = 0; i < defaults; ++i) {
		const Dictionary argument = arguments[arguments.size() - defaults + i];
		// Inputs are only the pin-validated 19 default spellings, never user text.
		const Variant expected = UtilityFunctions::str_to_var(argument["default_value"]);
		CHECK(actual.default_arguments[i].get_type() == carrier(argument["type"]));
		CHECK(actual.default_arguments[i] == expected);
	}
}
} // namespace
TEST_SUITE("builtin_metadata_analyzer") {
	TEST_CASE("every_builtin_producer_row_and_typed_value_matches_immutable_query") {
		const String oracle = BSCoreConstantsTestAccess::projection();
		BS_TEST_REQUIRE(oracle.begins_with("BS_NATIVE_BUILTIN_ORACLE\n"));
		const Variant parsed = JSON::parse_string(oracle.substr(String("BS_NATIVE_BUILTIN_ORACLE\n").length()));
		BS_TEST_REQUIRE(parsed.get_type() == Variant::ARRAY);
		const Array rows = parsed;
		BS_TEST_REQUIRE(rows.size() == BSCoreConstants::get_builtin_order().size());
		int constructor_count = 0, method_count = 0, member_count = 0, constant_count = 0, enum_count = 0, default_count = 0;
		HashSet<String> default_pairs;
		const char *sections[] = { "constructors", "methods", "members", "constants", "enums" };
		for (int row_index = 0; row_index < rows.size(); ++row_index) {
			const Dictionary row = rows[row_index];
			const String name = row["name"];
			CAPTURE(std::string(name.utf8().get_data()));
			CHECK(BSCoreConstants::get_builtin_order()[row_index] == StringName(name));
			const auto type = carrier(name);
			const auto *builtin = BSCoreConstants::get_builtin(type);
			BS_TEST_REQUIRE(builtin);
			CHECK(builtin->name == StringName(name));
			uint32_t mask = 0;
			for (int i = 0; i < 5; ++i)
				if (row.has(sections[i]))
					mask |= 1 << i;
			CHECK(builtin->present_sections == mask);
			const Array constructors = row["constructors"], methods = row.get("methods", Array()), members = row.get("members", Array()), constants = row.get("constants", Array()), enums = row.get("enums", Array());
			BS_TEST_REQUIRE(builtin->constructors.size() == constructors.size());
			BS_TEST_REQUIRE(builtin->methods.size() == methods.size());
			BS_TEST_REQUIRE(builtin->members.size() == members.size());
			BS_TEST_REQUIRE(builtin->constants.size() == constants.size());
			BS_TEST_REQUIRE(builtin->enums.size() == enums.size());
			constructor_count += constructors.size();
			method_count += methods.size();
			member_count += members.size();
			constant_count += constants.size();
			enum_count += enums.size();
			for (int i = 0; i < constructors.size(); ++i) {
				const Dictionary expected = constructors[i];
				CHECK(builtin->constructors[i].index == int(expected["index"]));
				CHECK(builtin->constructors[i].info.name == StringName(name));
				signature_matches(builtin->constructors[i].info, expected, name);
			}
			for (int i = 0; i < methods.size(); ++i) {
				const Dictionary expected = methods[i];
				const auto &actual = builtin->methods[i];
				CAPTURE(std::string(String(expected["name"]).utf8().get_data()));
				CHECK(actual.info.name == StringName(expected["name"]));
				CHECK(actual.hash == int64_t(expected["hash"]));
				CHECK(bool(actual.info.flags & METHOD_FLAG_CONST) == bool(expected["is_const"]));
				CHECK(bool(actual.info.flags & METHOD_FLAG_STATIC) == bool(expected["is_static"]));
				CHECK(bool(actual.info.flags & METHOD_FLAG_VARARG) == bool(expected["is_vararg"]));
				const Array hashes = expected.get("hash_compatibility", Array());
				BS_TEST_REQUIRE(actual.compatibility_hashes.size() == hashes.size());
				for (int h = 0; h < hashes.size(); ++h)
					CHECK(actual.compatibility_hashes[h] == int64_t(hashes[h]));
				signature_matches(actual.info, expected);
				CHECK(BSCoreConstants::get_builtin_method(type, expected["name"]) == &actual);
				const Array arguments = expected.get("arguments", Array());
				for (int a = 0; a < arguments.size(); ++a) {
					const Dictionary argument = arguments[a];
					if (!argument.has("default_value"))
						continue;
					++default_count;
					default_pairs.insert(String(argument["type"]) + String(":") + String(argument["default_value"]));
				}
			}
			for (int i = 0; i < members.size(); ++i) {
				const Dictionary expected = members[i];
				property_matches(builtin->members[i], expected["type"], expected["name"]);
				CHECK(BSCoreConstants::get_builtin_member(type, expected["name"]) == &builtin->members[i]);
			}
			for (int i = 0; i < constants.size(); ++i) {
				const Dictionary expected = constants[i];
				const auto &actual = builtin->constants[i];
				CAPTURE(std::string(String(expected["name"]).utf8().get_data()));
				CHECK(actual.name == StringName(expected["name"]));
				CHECK(actual.type == carrier(expected["type"]));
				BS_TEST_REQUIRE(actual.get_value);
				const Variant value = actual.get_value();
				// Independent engine text decoding of the validated API constructor text, not the generator factory.
				const Variant decoded = UtilityFunctions::str_to_var(expected["value"]);
				CHECK(value.get_type() == actual.type);
				CHECK(value == decoded);
				CHECK(BSCoreConstants::get_builtin_constant(type, expected["name"]) == &actual);
			}
			for (int i = 0; i < enums.size(); ++i) {
				const Dictionary expected = enums[i];
				const auto &actual = builtin->enums[i];
				CHECK(actual.name == StringName(expected["name"]));
				const Array values = expected["values"];
				BS_TEST_REQUIRE(actual.names.size() == values.size() && actual.values.size() == values.size());
				for (int v = 0; v < values.size(); ++v) {
					const Dictionary value = values[v];
					CHECK(actual.names[v] == StringName(value["name"]));
					CHECK(actual.values[v] == int64_t(value["value"]));
				}
				CHECK(BSCoreConstants::get_builtin_enum(type, expected["name"]) == &actual);
			}
		}
		CHECK(constructor_count == 156);
		CHECK(method_count == 999);
		CHECK(member_count == 62);
		CHECK(constant_count == 210);
		CHECK(enum_count == 7);
		CHECK(default_count == 150);
		CHECK(default_pairs.size() == 19);
		CHECK(BSCoreConstants::get_builtin_method(Variant::STRING, "absent") == nullptr);
		CHECK(BSCoreConstants::get_builtin_member(Variant::VECTOR2, "absent") == nullptr);
		CHECK(BSCoreConstants::get_builtin_constant(Variant::VECTOR3, "absent") == nullptr);
		CHECK(BSCoreConstants::get_builtin_enum(Variant::VECTOR3, "absent") == nullptr);
	}

	TEST_CASE("asymmetric_matrix_components_match_the_pinned_text_constructor_order") {
		CHECK(Variant(Transform2D(1, 2, 3, 4, 5, 6)) == UtilityFunctions::str_to_var("Transform2D(1, 2, 3, 4, 5, 6)"));
		CHECK(Variant(Basis(1, 2, 3, 4, 5, 6, 7, 8, 9)) == UtilityFunctions::str_to_var("Basis(1, 2, 3, 4, 5, 6, 7, 8, 9)"));
		CHECK(Variant(Transform3D(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)) == UtilityFunctions::str_to_var("Transform3D(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)"));
		CHECK(Variant(Projection(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16)) == UtilityFunctions::str_to_var("Projection(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16)"));
	}
	TEST_CASE("builtin_method_return_and_constructor_are_precise") {
		for (const auto &item : std::initializer_list<std::pair<const char *, Variant::Type>>{
					 { "\"abc\".length()", Variant::INT }, { "\"abc\".substr(1)", Variant::STRING }, { "Vector2(1, 2)", Variant::VECTOR2 } }) {
			BSParser parser;
			String source = String("func test():\n\tvar result = ") + item.first + "\n\tprint(result)\n";
			BS_TEST_REQUIRE(parser.parse(source, "res://builtin_metadata.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			const auto *v = local(parser, "result");
			BS_TEST_REQUIRE(v && v->initializer);
			CHECK(v->initializer->get_datatype().kind == BSParser::DataType::BUILTIN);
			CHECK(v->initializer->get_datatype().builtin_type == item.second);
		}
	}
	TEST_CASE("known_missing_and_invalid_overload_are_rejected") {
		int mode = 0;
		for (const char *expression : { "\"abc\".missing()", "\"abc\".substr(Vector2())", "Vector2(false, false, false)" }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(String("func test():\n\t") + expression + "\n", "res://builtin_negative.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == 1);
			if (mode == 0)
				error_tuple(parser, 0, "Function \"missing()\" not found in base String.", 2, 5, 2, 18);
			else if (mode == 1)
				error_tuple(parser, 0, "Invalid argument for \"substr()\" function: argument 1 should be \"int\" but is \"Vector2\".", 2, 18, 2, 27);
			else
				error_tuple(parser, 0, "No constructor of \"Vector2\" matches the signature \"Vector2(bool, bool, bool)\".", 2, 5, 2, 33);
			CHECK(parser.get_warnings().is_empty());
			++mode;
		}
	}
	TEST_CASE("pin_void_builtin_method_result") {
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func test():\n\tvar builtin := []\n\tprint(builtin.reverse()) # Built-in type method.\n", "res://use_value_of_void_function_builtin_method.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		BS_TEST_REQUIRE(parser.get_errors().size() == 1);
		error_tuple(parser, 0, "Cannot get return value of call to \"reverse()\" because it returns \"void\".", 3, 11, 3, 28);
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("array_dictionary_constructor_constants") {
		for (const char *expression : { "Array()", "Array([1, 2])", "Dictionary()", "Dictionary({\"x\": 1})" }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(String("const VALUE = ") + expression + "\n", "res://builtin_fold.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			const auto member = parser.get_tree()->get_member("VALUE");
			BS_TEST_REQUIRE(member.type == BSParser::ClassNode::Member::CONSTANT && member.constant && member.constant->initializer);
			CHECK(member.constant->initializer->is_constant);
			CHECK_FALSE(member.constant->initializer->is_unmaterialized_constant);
			const Variant value = member.constant->initializer->reduced_value;
			if (String(expression).begins_with("Array")) {
				BS_TEST_REQUIRE(value.get_type() == Variant::ARRAY);
				const Array array = value;
				CHECK(array.is_read_only());
				CHECK(array.size() == (String(expression) == "Array()" ? 0 : 2));
				if (!array.is_empty()) {
					CHECK(array[0] == Variant(1));
					CHECK(array[1] == Variant(2));
				}
			} else {
				BS_TEST_REQUIRE(value.get_type() == Variant::DICTIONARY);
				const Dictionary dictionary = value;
				CHECK(dictionary.is_read_only());
				CHECK(dictionary.size() == (String(expression) == "Dictionary()" ? 0 : 1));
				if (!dictionary.is_empty())
					CHECK(dictionary["x"] == Variant(1));
			}
			CHECK(parser.get_warnings().is_empty());
		}
	}
	TEST_CASE("builtin_properties_and_captured_method_signatures") {
		const String source = "func test(value: Vector2):\n\tvar coordinate = value.x\n\tvar method: Callable[[], int] = \"abc\".length\n\tvar count = method.call()\n\tprint(coordinate, count)\n";
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, "res://metadata_properties.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		const auto *coordinate = local(parser, "coordinate"), *method = local(parser, "method"), *count = local(parser, "count");
		BS_TEST_REQUIRE(coordinate && method && count);
		CHECK(coordinate->get_datatype().builtin_type == Variant::FLOAT);
		CHECK(method->get_datatype().has_explicit_method_signature);
		CHECK(count->get_datatype().builtin_type == Variant::INT);
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("pin_global_builtin_native_enum_static_declarations") {
		const String source = R"SOURCE(extends Node

@export var test_type_1 := TYPE_BOOL
@export var test_type_2 := Variant.Type.TYPE_BOOL
@export var test_type_3: Variant.Type

@export var test_side_1 := SIDE_RIGHT
@export var test_side_2 := Side.SIDE_RIGHT
@export var test_side_3: Side

@export var test_axis_1 := Vector3.AXIS_Y
@export var test_axis_2 := Vector3.Axis.AXIS_Y
@export var test_axis_3: Vector3.Axis

@export var test_mode_1 := Node.PROCESS_MODE_ALWAYS
@export var test_mode_2 := Node.ProcessMode.PROCESS_MODE_ALWAYS
@export var test_mode_3: Node.ProcessMode
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://global_builtin_and_native_enums.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		int group = 0;
		const char *identities[] = { "Variant.Type", "Side", "Vector3.Axis", "Node.ProcessMode" };
		const int64_t ordinals[] = { 1, 2, 1, 3 };
		for (const char *prefix : { "type", "side", "axis", "mode" }) {
			for (int i = 1; i <= 3; ++i) {
				const auto member = parser.get_tree()->get_member(String("test_") + prefix + String("_") + itos(i));
				BS_TEST_REQUIRE(member.type == BSParser::ClassNode::Member::VARIABLE && member.variable);
				const auto type = member.variable->get_datatype();
				CHECK(type.kind == BSParser::DataType::ENUM);
				CHECK(type.builtin_type == Variant::INT);
				CHECK_FALSE(type.is_meta_type);
				CHECK(type.native_type == StringName(identities[group]));
				CHECK(member.variable->exported);
				CHECK(member.variable->export_info.type == Variant::INT);
				CHECK(member.variable->export_info.hint == PROPERTY_HINT_ENUM);
				CHECK(member.variable->export_info.class_name == StringName(identities[group]));
				CHECK(bool(member.variable->export_info.usage & PROPERTY_USAGE_CLASS_IS_ENUM));
				String expected_hint;
				for (const KeyValue<StringName, int64_t> &entry : type.enum_values) {
					if (!expected_hint.is_empty())
						expected_hint += ",";
					expected_hint += String(entry.key).capitalize().xml_escape() + ":" + itos(entry.value);
				}
				CHECK(member.variable->export_info.hint_string == expected_hint);
				if (i < 3) {
					BS_TEST_REQUIRE(member.variable->initializer);
					CHECK(member.variable->initializer->is_constant);
					CHECK(member.variable->initializer->reduced_value == Variant(ordinals[group]));
				}
				for (const auto *annotation : member.variable->annotations) {
					BS_TEST_REQUIRE(annotation);
					CHECK(annotation->is_applied);
				}
			}
			++group;
		}
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().is_empty());
		// Interface and body reuse must not reapply the export callback or duplicate diagnostics.
		CHECK(analyzer.analyze() == OK);
		CHECK(parser.get_errors().is_empty());
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("builtin_enum_owner_identity_and_constant_values") {
		BSParser parser;
		const String source = "const AXIS = Vector3.Axis.AXIS_Y\nconst VECTOR = Vector3.ONE\nfunc test():\n\tvar axis: Vector2.Axis = Vector3.AXIS_Y\n\tprint(axis)\n";
		BS_TEST_REQUIRE(parser.parse(source, "res://metadata_enum_identity.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		const auto axis = parser.get_tree()->get_member("AXIS"), vector = parser.get_tree()->get_member("VECTOR");
		BS_TEST_REQUIRE(axis.constant && vector.constant && axis.constant->initializer && vector.constant->initializer);
		CHECK(axis.constant->initializer->reduced_value == Variant(int64_t(1)));
		CHECK(vector.constant->initializer->reduced_value == Variant(Vector3(1, 1, 1)));
		CHECK(axis.constant->get_datatype().native_type == SNAME("Vector3.Axis"));
		BS_TEST_REQUIRE(parser.get_errors().size() == 1);
		error_tuple(parser, 0, "Cannot assign a value of type \"Vector3.Axis\" to a variable of type \"Vector2.Axis\".", 4, 30, 4, 44);
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("builtin_trait_implementation_accepts_matching_and_rejects_mismatch") {
		for (bool valid : { true, false }) {
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			const String source = String("trait Sized:\n\tabstract func length() -> ") + (valid ? "int" : "String") + "\nextend String uses Sized:\n\tpass\n";
			BSParser parser;
			const Error parsed = parser.parse(source, "res://metadata_trait.barista", false);
			diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == valid);
			diagnostics(parser);
			if (valid)
				CHECK(parser.get_errors().is_empty());
			else {
				BS_TEST_REQUIRE(parser.get_errors().size() == 1);
				error_tuple(parser, 0, "The native function \"length()\" signature does not match required trait method \"Sized.length()\". Implementation comes from builtin type \"String\".", 2, 14, 2, 38);
			}
			CHECK(parser.get_warnings().is_empty());
		}
	}
	TEST_CASE("native_property_mismatch_remains_typed") {
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func test(node: Node2D):\n\tnode.position = \"wrong\"\n", "res://metadata_native_property.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		BS_TEST_REQUIRE(parser.get_errors().size() == 1);
		error_tuple(parser, 0, "Value of type \"String\" cannot be assigned to a variable of type \"Vector2\".", 2, 21, 2, 28);
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("signal_metadata_flags_result_and_payload_are_checked_together") {
		for (int mode = 0; mode < 5; ++mode) {
			const String source = String("signal changed(value: int)\nfunc handler(value: ") + (mode == 4 ? "String" : "int") + "):\n\tprint(value)\nfunc test():\n\tvar status = changed.connect(handler" + (mode == 1 ? String(", 1") : mode == 2 ? String(", Vector2()")
																																																													: String()) +
					")\n\tchanged.emit(" + (mode == 3 ? String("Vector2()") : String("1")) + ")\n\tprint(status, changed.is_connected(handler))\n\tchanged.disconnect(handler)\n";
			BSParser parser;
			const Error parsed = parser.parse(source, "res://metadata_signal.barista", false);
			diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == (mode < 2));
			diagnostics(parser);
			const auto *status = local(parser, "status");
			BS_TEST_REQUIRE(status && status->initializer);
			CHECK(status->initializer->get_datatype().builtin_type == Variant::INT);
			BS_TEST_REQUIRE(parser.get_errors().size() == (mode < 2 ? 0 : mode == 4 ? 3
																					: 1));
			if (mode == 2)
				error_tuple(parser, 0, "Invalid argument for \"connect()\" function: argument 2 should be \"int\" but is \"Vector2\".", 5, 43, 5, 52);
			if (mode == 3)
				error_tuple(parser, 0, "Invalid argument for \"emit()\" function: argument 1 should be \"int\" but is \"Vector2\".", 6, 18, 6, 27);
			if (mode == 4) {
				const String detail = " signal \"Signal[[int]]\" to callable \"Callable[[String], Variant]\": signal argument 1 of type \"int\" cannot be passed to callable parameter of type \"String\".";
				error_tuple(parser, 0, "Cannot connect" + detail, 5, 34, 5, 41);
				error_tuple(parser, 1, "Cannot check connection for" + detail, 7, 40, 7, 47);
				error_tuple(parser, 2, "Cannot disconnect" + detail, 8, 24, 8, 31);
			}
			CHECK(parser.get_warnings().is_empty());
		}
	}
	TEST_CASE("constructor_admission_retypes_only_selected_candidate_and_keeps_unknown_gradual") {
		SettingsRestore restore;
		ProjectSettings::get_singleton()->set_setting("debug/barista_script/warnings/unsafe_call_argument", 1);
		for (int mode = 0; mode < 3; ++mode) {
			const String expression = mode == 0 ? "Vector2(1, 2)" : mode == 1 ? "Vector2i(1.0, 2.0)"
																			  : "Vector2(value)";
			BSParser parser;
			const String source = String("func test(") + (mode == 2 ? "value: Variant" : "") + "):\n\tvar result = " + expression + "\n\tprint(result)\n";
			BS_TEST_REQUIRE(parser.parse(source, "res://metadata_constructor_selection.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			const auto *result = local(parser, "result");
			BS_TEST_REQUIRE(result && result->initializer && result->initializer->type == BSParser::Node::CALL);
			const auto *call = static_cast<const BSParser::CallNode *>(result->initializer);
			CHECK_FALSE(call->is_constant); // Arbitrary math constructors are typed, not evaluated here.
			if (mode < 2) {
				BS_TEST_REQUIRE(call->arguments.size() == 2);
				for (const auto *argument : call->arguments)
					CHECK(argument->reduced_value.get_type() == (mode == 0 ? Variant::FLOAT : Variant::INT));
			}
			BS_TEST_REQUIRE(parser.get_warnings().size() == (mode == 0 ? 0 : mode == 1 ? 2
																					   : 1));
			if (mode == 1)
				for (int i = 0; i < 2; ++i)
					warning_tuple(parser, i, "NARROWING_CONVERSION", "Narrowing conversion (float is converted to int and loses precision).", 2, 18, 2, 36);
			if (mode == 2)
				warning_tuple(parser, 0, "UNSAFE_CALL_ARGUMENT", "The argument 1 of the constructor \"Vector2()\" requires the subtype \"Vector2\" but the supertype \"Variant\" was provided.", 2, 26, 2, 31);
		}
	}
	TEST_CASE("builtin_static_form_missing_members_and_void_append") {
		int mode = 0;
		for (const char *expression : { "String.length()", "Vector2.ABSENT", "[].append(1)" }) {
			const String source = String("func test():\n\tvar result = ") + expression + "\n\tprint(result)\n";
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, "res://metadata_static.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == 1);
			if (mode == 0)
				error_tuple(parser, 0, "Cannot call non-static function \"length()\" on the class \"String\" directly. Make an instance instead.", 2, 18, 2, 33);
			if (mode == 1)
				error_tuple(parser, 0, "Cannot find member \"ABSENT\" in base \"Vector2\".", 2, 26, 2, 32);
			if (mode == 2)
				error_tuple(parser, 0, "Cannot get return value of call to \"append()\" because it returns \"void\".", 2, 18, 2, 30);
			CHECK(parser.get_warnings().is_empty());
			++mode;
		}
	}
	TEST_CASE("container_call_folding_reaches_nested_pure_children_and_defers_unknown") {
		for (int mode = 0; mode < 4; ++mode) {
			const String source = mode == 0 ? "const VALUE = Array([[1, 2]][0])\n" : mode == 1 ? "const VALUE = Dictionary({\"x\": (1, 2)})\n"
					: mode == 2																   ? "func test(value: Array):\n\tvar result = Array(value)\n\tprint(result)\n"
																							   : "const VALUE = Array(1)\n";
			BSParser parser;
			const Error parsed = parser.parse(source, "res://metadata_fold_children.barista", false);
			diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == (mode < 3));
			diagnostics(parser);
			if (mode < 2) {
				const auto member = parser.get_tree()->get_member("VALUE");
				BS_TEST_REQUIRE(member.constant && member.constant->initializer);
				CHECK(member.constant->initializer->is_constant);
				const Variant value = member.constant->initializer->reduced_value;
				if (mode == 0) {
					BS_TEST_REQUIRE(value.get_type() == Variant::ARRAY);
					CHECK(Array(value).is_read_only());
					CHECK(Array(value).size() == 2);
				} else {
					BS_TEST_REQUIRE(value.get_type() == Variant::DICTIONARY);
					CHECK(Dictionary(value).is_read_only());
					CHECK(Dictionary(value).size() == 1);
				}
			} else if (mode == 2) {
				const auto *result = local(parser, "result");
				BS_TEST_REQUIRE(result && result->initializer);
				CHECK_FALSE(result->initializer->is_constant);
			} else {
				BS_TEST_REQUIRE(parser.get_errors().size() == 2);
				error_tuple(parser, 0, "No constructor of \"Array\" matches the signature \"Array(int)\".", 1, 15, 1, 23);
				error_tuple(parser, 1, "Assigned value for constant \"VALUE\" isn't a constant expression.", 1, 15, 1, 23);
			}
			CHECK(parser.get_warnings().is_empty());
		}
	}
	TEST_CASE("public_builtin_metadata_agreement_preserves_index_settings_and_overrides") {
		const Dictionary startup = settings_snapshot();
		{
			SettingsRestore restore;
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			const String path = "res://tests/builtin_metadata/public.barista";
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
					const String source = valid ? "const VALUE = Array([1, 2])\nfunc test():\n\tvar result: int = \"abc\".length()\n\tprint(result, VALUE)\n" : "func test():\n\tprint([].append(1))\n";
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
	TEST_CASE("constructor_rejection_has_no_speculative_retyping_and_strict_variant_is_rejected") {
		SettingsRestore restore;
		for (int mode = 0; mode < 3; ++mode) {
			ProjectSettings::get_singleton()->set_setting("debug/barista_script/analysis/strict_dynamic_checks", mode == 1);
			const String source = mode == 0 ? "func test():\n\tvar value = Vector2i(1.0, Vector2())\n\tprint(value)\n" : mode == 1 ? "func test(argument: Variant):\n\tvar value = Vector2(argument)\n\tprint(value)\n"
																																   : "func test(argument: Variant):\n\tvar value = argument.missing()\n\tprint(value)\n";
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, "res://metadata_constructor_boundary.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == (mode == 2));
			diagnostics(parser);
			const auto *value = local(parser, "value");
			BS_TEST_REQUIRE(value && value->initializer && value->initializer->type == BSParser::Node::CALL);
			const auto *call = static_cast<const BSParser::CallNode *>(value->initializer);
			CHECK_FALSE(call->is_constant);
			if (mode == 0) {
				BS_TEST_REQUIRE(call->arguments.size() == 2);
				CHECK(call->arguments[0]->reduced_value.get_type() == Variant::FLOAT);
				CHECK(parser.get_warnings().is_empty());
			}
			if (mode < 2) {
				BS_TEST_REQUIRE(parser.get_errors().size() == 1);
				if (mode == 0)
					error_tuple(parser, 0, "No constructor of \"Vector2i\" matches the signature \"Vector2i(float, Vector2)\".", 2, 17, 2, 41);
				else
					error_tuple(parser, 0, "No constructor of \"Vector2\" matches the signature \"Vector2(Variant)\".", 2, 17, 2, 34);
			} else {
				CHECK(call->get_datatype().is_variant());
				CHECK(parser.get_errors().is_empty());
			}
		}
	}
	TEST_CASE("container_copy_keeps_semantic_constant_without_fabricating_script_payload") {
		for (const char *expression : { "Array([P])", "Dictionary({\"item\": P})" }) {
			BSParser parser;
			const String source = String("class Item:\n\tpass\nconst P = Item\nconst VALUE = ") + expression + "\n";
			BS_TEST_REQUIRE(parser.parse(source, "res://metadata_unmaterialized.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			const auto member = parser.get_tree()->get_member("VALUE");
			BS_TEST_REQUIRE(member.constant && member.constant->initializer);
			CHECK(member.constant->initializer->is_constant);
			CHECK(member.constant->initializer->is_unmaterialized_constant);
			CHECK_FALSE(BSAnalyzer::has_materialized_constant_value(member.constant->initializer));
			CHECK(parser.get_errors().is_empty());
			CHECK(parser.get_warnings().is_empty());
		}
	}
	TEST_CASE("ordinary_name_resolution_prevents_shadowed_constructor_folding") {
		for (const char *source : { "func Array():\n\treturn [1]\nfunc test():\n\tvar value = Array()\n\tprint(value)\n", "class Factory:\n\tstatic func Array():\n\t\treturn [1]\nfunc test():\n\tvar value = Factory.Array()\n\tprint(value)\n", "func test(Array: Callable):\n\tvar value = Array()\n\tprint(value)\n" }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, "res://metadata_shadow.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			const auto *value = local(parser, "value");
			BS_TEST_REQUIRE(value && value->initializer && value->initializer->type == BSParser::Node::CALL);
			const auto *call = static_cast<const BSParser::CallNode *>(value->initializer);
			CHECK_FALSE(call->is_constant);
			CHECK_FALSE(call->callee->get_datatype().is_meta_type);
			CHECK(parser.get_errors().is_empty());
			CHECK(parser.get_warnings().is_empty());
		}
	}
}
