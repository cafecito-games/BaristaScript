/**************************************************************************/
/*  top_level_enum_analyzer_tests.cpp                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script_language.h"
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

const char *const ROOT = "res://tests/corpus_staging/analyzer/features/";

String path(const String &p_name) {
	return String(ROOT) + p_name + ".barista";
}

void install(StorageFixture &p_storage, const String &p_path, const String &p_source) {
	BSParser syntax;
	BS_TEST_REQUIRE(syntax.parse(p_source, p_path, false) == OK);
	BSCache::set_source_override(p_path, p_source);
	auto record = BSDeclarationIndex::record_from_global_class(p_path, p_source, bs_resolve_global_class_from_source(p_source, p_path));
	record.namespace_name = syntax.get_tree()->namespace_name;
	BS_TEST_REQUIRE(p_storage.index().commit_record(p_storage.index().claim_refresh(p_path), record));
}

struct OpenEnumWarningScope {
	Dictionary saved;

	void set(const String &p_key, const Variant &p_value) {
		auto *settings = ProjectSettings::get_singleton();
		if (!saved.has(p_key)) {
			saved[p_key] = settings->has_setting(p_key) ? settings->get_setting(p_key) : Variant();
		}
		settings->set_setting(p_key, p_value);
	}

	OpenEnumWarningScope() {
		set("debug/barista_script/warnings/enable", true);
		for (int i = 0; i < BSWarning::WARNING_MAX; ++i) {
			set(BSWarning::get_setting_path_from_code(BSWarning::Code(i)), BSWarning::IGNORE);
		}
		set(BSWarning::get_setting_path_from_code(BSWarning::OPEN_ENUM_MATCH_WITHOUT_DEFAULT), BSWarning::WARN);
		BSParser::update_project_settings();
	}

	~OpenEnumWarningScope() {
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

String diagnostic_block(const BSParser &p_parser) {
	String result;
	for (const auto &error : p_parser.get_errors()) {
		if (!result.is_empty()) {
			result += "\n";
		}
		result += vformat(">> ERROR at line %d: %s", error.line, error.message);
	}
	for (const auto &warning : p_parser.get_warnings()) {
		if (!result.is_empty()) {
			result += "\n";
		}
		result += vformat("~~ WARNING at line %d: (%s) %s", warning.start_line, warning.get_name(), warning.get_message());
	}
	return result.is_empty() ? String("BS_TEST_OK") : result;
}

String standalone_provider_source(int p_first = 0) {
	return vformat("enum_name TopLevelStandaloneEnum:\n\tRED = %d\n\tYELLOW = RED + 1\n\tGREEN = %d\n", p_first, p_first + 5);
}

String namespaced_provider_source(int p_first = 0) {
	return vformat("namespace top_level_enum_demo.status\n\nenum_name TopLevelNamespacedEnum:\n\tSTARTED = %d\n\tSTOPPED = STARTED + 1\n", p_first);
}

String standalone_consumer_source() {
	return R"source(const Alias = TopLevelStandaloneEnum

var current: TopLevelStandaloneEnum = TopLevelStandaloneEnum.GREEN
var alias_member: int = Alias.YELLOW

func accept_enum(value: TopLevelStandaloneEnum) -> void:
	match value:
		TopLevelStandaloneEnum.RED:
			pass
		TopLevelStandaloneEnum.GREEN:
			pass
		_:
			pass

func check_member_value() -> void:
	var member_value: int = TopLevelStandaloneEnum.GREEN
	var local: TopLevelStandaloneEnum = TopLevelStandaloneEnum.RED
	var names: Array = TopLevelStandaloneEnum.keys()
	accept_enum(local)
	accept_enum(member_value)
	print(names)
)source";
}

String namespaced_consumer_source() {
	return R"source(namespace top_level_enum_demo.consumer
import top_level_enum_demo.status

var imported_value: TopLevelNamespacedEnum = TopLevelNamespacedEnum.STARTED
var qualified_value: top_level_enum_demo.status.TopLevelNamespacedEnum = (
	top_level_enum_demo.status.TopLevelNamespacedEnum.STOPPED
)

func accepts_imported(value: TopLevelNamespacedEnum) -> void:
	match value:
		TopLevelNamespacedEnum.STARTED:
			pass
		TopLevelNamespacedEnum.STOPPED:
			pass

func accepts_qualified(value: top_level_enum_demo.status.TopLevelNamespacedEnum) -> void:
	match value:
		top_level_enum_demo.status.TopLevelNamespacedEnum.STARTED:
			pass
		top_level_enum_demo.status.TopLevelNamespacedEnum.STOPPED:
			pass
)source";
}

String preload_consumer_source() {
	return R"source(const EnumFile = preload("top_level_enum_standalone.notest.barista")

var direct_value: TopLevelStandaloneEnum = EnumFile.RED
var direct_dictionary: Dictionary = EnumFile.TopLevelStandaloneEnum

func accepts_enum(_value: TopLevelStandaloneEnum) -> void:
	pass

func check_preloaded_enum_file_constant() -> void:
	accepts_enum(EnumFile.GREEN)
)source";
}

BSParser::ExpressionNode *constant_initializer(BSParser &p_parser, const StringName &p_name) {
	CHECK(p_parser.get_tree()->has_member(p_name));
	if (!p_parser.get_tree()->has_member(p_name)) {
		return nullptr;
	}
	const auto member = p_parser.get_tree()->get_member(p_name);
	CHECK(member.type == BSParser::ClassNode::Member::CONSTANT);
	return member.type == BSParser::ClassNode::Member::CONSTANT ? member.constant->initializer : nullptr;
}

BSParser::ExpressionNode *variable_initializer(BSParser &p_parser, const StringName &p_name) {
	CHECK(p_parser.get_tree()->has_member(p_name));
	if (!p_parser.get_tree()->has_member(p_name)) {
		return nullptr;
	}
	const auto member = p_parser.get_tree()->get_member(p_name);
	CHECK(member.type == BSParser::ClassNode::Member::VARIABLE);
	return member.type == BSParser::ClassNode::Member::VARIABLE ? member.variable->initializer : nullptr;
}

BSParser::ExpressionNode *function_return_value(BSParser::ClassNode *p_class, const StringName &p_name) {
	CHECK(p_class != nullptr);
	if (p_class == nullptr) {
		return nullptr;
	}
	CHECK(p_class->has_member(p_name));
	if (!p_class->has_member(p_name)) {
		return nullptr;
	}
	const auto member = p_class->get_member(p_name);
	CHECK(member.type == BSParser::ClassNode::Member::FUNCTION);
	CHECK(member.function != nullptr);
	if (member.type != BSParser::ClassNode::Member::FUNCTION || member.function == nullptr) {
		return nullptr;
	}
	CHECK(member.function->body != nullptr);
	if (member.function->body == nullptr) {
		return nullptr;
	}
	CHECK_FALSE(member.function->body->statements.is_empty());
	if (member.function->body->statements.is_empty()) {
		return nullptr;
	}
	BSParser::Node *statement = member.function->body->statements[member.function->body->statements.size() - 1];
	CHECK(statement->type == BSParser::Node::RETURN);
	return statement->type == BSParser::Node::RETURN ? static_cast<BSParser::ReturnNode *>(statement)->return_value : nullptr;
}

BSParser::IdentifierNode *attribute_root(BSParser::ExpressionNode *p_expression) {
	while (p_expression != nullptr && p_expression->type == BSParser::Node::SUBSCRIPT) {
		auto *subscript = static_cast<BSParser::SubscriptNode *>(p_expression);
		if (!subscript->is_attribute) {
			return nullptr;
		}
		p_expression = subscript->base;
	}
	return p_expression != nullptr && p_expression->type == BSParser::Node::IDENTIFIER
			? static_cast<BSParser::IdentifierNode *>(p_expression)
			: nullptr;
}

void check_string_value(const BSParser::ExpressionNode *p_expression, const String &p_value) {
	BS_TEST_REQUIRE(p_expression != nullptr);
	const BSParser::DataType type = p_expression->get_datatype();
	CHECK(type.kind == BSParser::DataType::BUILTIN);
	CHECK(type.builtin_type == Variant::STRING);
	CHECK_FALSE(type.is_meta_type);
	CHECK(p_expression->is_constant);
	CHECK(p_expression->reduced_value == Variant(p_value));
}

void check_enum_declaration(const BSParser::ExpressionNode *p_expression, int64_t p_first, int64_t p_second) {
	BS_TEST_REQUIRE(p_expression != nullptr);
	const BSParser::DataType type = p_expression->get_datatype();
	CHECK(p_expression->is_constant);
	CHECK(type.kind == BSParser::DataType::ENUM);
	CHECK(type.is_meta_type);
	CHECK(type.builtin_type == Variant::DICTIONARY);
	const bool has_expected_head = type.enum_values.has("RED") || type.enum_values.has("STARTED");
	CHECK(has_expected_head);
	if (type.enum_values.has("RED")) {
		CHECK(type.enum_values["RED"] == p_first);
		CHECK(type.enum_values["YELLOW"] == p_first + 1);
		CHECK(type.enum_values["GREEN"] == p_second);
	} else {
		CHECK(type.enum_values["STARTED"] == p_first);
		CHECK(type.enum_values["STOPPED"] == p_second);
	}
}

void check_ok(BSParser &p_parser) {
	BSAnalyzer analyzer(&p_parser);
	const Error result = analyzer.analyze();
	MESSAGE(std::string(diagnostic_block(p_parser).utf8().get_data()));
	CHECK(result == OK);
	CHECK(diagnostic_block(p_parser) == "BS_TEST_OK");
}

} // namespace

TEST_SUITE("top_level_enum_analyzer") {
	TEST_CASE("exact_standalone_consumer_keeps_declaration_dictionary_and_member_integer_identity") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		OpenEnumWarningScope warnings;
		install(storage, path("top_level_enum_standalone.notest"), standalone_provider_source());
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(standalone_consumer_source(), path("top_level_enum_consumer.norun"), false) == OK);
		check_ok(parser);
		check_enum_declaration(constant_initializer(parser, "Alias"), 0, 5);
		const BSParser::DataType member = variable_initializer(parser, "current")->get_datatype();
		CHECK(member.kind == BSParser::DataType::ENUM);
		CHECK_FALSE(member.is_meta_type);
		CHECK(member.builtin_type == Variant::INT);
	}

	TEST_CASE("exact_namespaced_consumer_resolves_short_and_qualified_identity_with_ordered_open_warnings") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		OpenEnumWarningScope warnings;
		install(storage, path("top_level_enum_namespaced.notest"), namespaced_provider_source());
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(namespaced_consumer_source(), path("top_level_enum_namespaced_consumer.norun"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		MESSAGE(std::string(diagnostic_block(parser).utf8().get_data()));
		CHECK(diagnostic_block(parser) == R"expected(~~ WARNING at line 10: (OPEN_ENUM_MATCH_WITHOUT_DEFAULT) The "match" over "top_level_enum_demo.status.TopLevelNamespacedEnum" has no unguarded "_" or bind branch. "top_level_enum_demo.status.TopLevelNamespacedEnum" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.
~~ WARNING at line 17: (OPEN_ENUM_MATCH_WITHOUT_DEFAULT) The "match" over "top_level_enum_demo.status.TopLevelNamespacedEnum" has no unguarded "_" or bind branch. "top_level_enum_demo.status.TopLevelNamespacedEnum" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.)expected");
		const BSParser::DataType imported = variable_initializer(parser, "imported_value")->get_datatype();
		const BSParser::DataType qualified = variable_initializer(parser, "qualified_value")->get_datatype();
		CHECK(imported.kind == BSParser::DataType::ENUM);
		CHECK(qualified.kind == BSParser::DataType::ENUM);
		CHECK(imported.class_type == qualified.class_type);
		CHECK(imported.native_type == qualified.native_type);
		CHECK(imported.enum_type == qualified.enum_type);
	}

	TEST_CASE("exact_preload_constants_consumer_accepts_only_the_enum_declaration_as_dictionary") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		install(storage, path("top_level_enum_standalone.notest"), standalone_provider_source());
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(preload_consumer_source(), path("top_level_enum_preload_constants.norun"), false) == OK);
		check_ok(parser);
		check_enum_declaration(variable_initializer(parser, "direct_dictionary"), 0, 5);
	}

	TEST_CASE("enum_declarations_are_dictionary_values_but_enum_members_are_not") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		install(storage, path("top_level_enum_standalone.notest"), standalone_provider_source());
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("var declaration: Dictionary = TopLevelStandaloneEnum\nvar member: Dictionary = TopLevelStandaloneEnum.RED\n", path("top_level_enum_dictionary_distinction"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		MESSAGE(std::string(diagnostic_block(parser).utf8().get_data()));
		CHECK(diagnostic_block(parser) == R"expected(>> ERROR at line 2: Cannot assign a value of type TopLevelStandaloneEnum to variable "member" with specified type Dictionary.)expected");
		check_enum_declaration(variable_initializer(parser, "declaration"), 0, 5);
		const BSParser::DataType member = variable_initializer(parser, "member")->get_datatype();
		CHECK(member.kind == BSParser::DataType::ENUM);
		CHECK_FALSE(member.is_meta_type);
		CHECK(member.builtin_type == Variant::INT);
	}

	TEST_CASE("retained_consumers_keep_generation_specific_short_and_qualified_enum_identity") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		const String provider_path = path("top_level_enum_namespaced.notest");
		const String consumer_source = "namespace top_level_enum_demo.consumer\nimport top_level_enum_demo.status\nconst Short = TopLevelNamespacedEnum\nconst Qualified = top_level_enum_demo.status.TopLevelNamespacedEnum\nconst ShortValue = Short.STOPPED\nconst QualifiedValue = Qualified.STOPPED\n";
		install(storage, provider_path, namespaced_provider_source());
		BSParser old_consumer;
		BS_TEST_REQUIRE(old_consumer.parse(consumer_source, path("top_level_enum_retained_old"), false) == OK);
		check_ok(old_consumer);
		const Ref<BSParserRef> retained = old_consumer.get_depended_parsers()[provider_path];
		BS_TEST_REQUIRE(retained.is_valid());
		const BSParser::DataType old_short = constant_initializer(old_consumer, "Short")->get_datatype();
		const BSParser::DataType old_qualified = constant_initializer(old_consumer, "Qualified")->get_datatype();
		CHECK(old_short.class_type == old_qualified.class_type);
		CHECK(constant_initializer(old_consumer, "ShortValue")->reduced_value == Variant(1));
		CHECK(constant_initializer(old_consumer, "QualifiedValue")->reduced_value == Variant(1));

		const String refreshed_source = namespaced_provider_source(40);
		BSCache::set_source_override(provider_path, refreshed_source);
		BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(provider_path, refreshed_source) == OK);
		BSParser fresh_consumer;
		BS_TEST_REQUIRE(fresh_consumer.parse(consumer_source, path("top_level_enum_retained_fresh"), false) == OK);
		check_ok(fresh_consumer);
		const BSParser::DataType fresh_short = constant_initializer(fresh_consumer, "Short")->get_datatype();
		const BSParser::DataType fresh_qualified = constant_initializer(fresh_consumer, "Qualified")->get_datatype();
		CHECK(fresh_short.class_type == fresh_qualified.class_type);
		CHECK(fresh_short.class_type != old_short.class_type);
		CHECK(constant_initializer(fresh_consumer, "ShortValue")->reduced_value == Variant(41));
		CHECK(constant_initializer(fresh_consumer, "QualifiedValue")->reduced_value == Variant(41));
		CHECK(fresh_consumer.get_depended_parsers()[provider_path] != retained);
		CHECK(constant_initializer(old_consumer, "ShortValue")->reduced_value == Variant(1));
	}

	TEST_CASE("qualified_enum_expression_misses_and_failed_providers_stay_errors") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		BSParser missing;
		BS_TEST_REQUIRE(missing.parse("const Alias = absent.namespace.State\n", path("top_level_enum_true_miss"), false) == OK);
		BSAnalyzer missing_analyzer(&missing);
		CHECK(missing_analyzer.analyze() != OK);
		CHECK(diagnostic_block(missing).contains("Identifier \"absent\" not declared"));
		CHECK_FALSE(constant_initializer(missing, "Alias")->is_constant);

		install(storage, path("top_level_enum_failed.notest"), "namespace top_level_enum_demo.failed\n\nenum_name FailedState:\n\tREADY = MissingValue\n");
		BSParser failed;
		BS_TEST_REQUIRE(failed.parse("const Alias = top_level_enum_demo.failed.FailedState\n", path("top_level_enum_failed_consumer"), false) == OK);
		BSAnalyzer failed_analyzer(&failed);
		CHECK(failed_analyzer.analyze() != OK);
		MESSAGE(std::string(diagnostic_block(failed).utf8().get_data()));
		CHECK(diagnostic_block(failed).contains("top_level_enum_failed.notest.barista"));
		CHECK_FALSE(constant_initializer(failed, "Alias")->is_constant);
	}

	TEST_CASE("qualified_enum_namespace_yields_to_parameter_root") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		install(storage, path("top_level_enum_shadow_provider.notest"), "namespace reviewns\n\nenum_name State:\n\tREADY = 1\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func select(reviewns: Dictionary[String, String]) -> String:\n\treturn reviewns.State\n", path("top_level_enum_parameter_shadow"), false) == OK);
		check_ok(parser);
		BSParser::ExpressionNode *selected = function_return_value(parser.get_tree(), "select");
		BS_TEST_REQUIRE(selected != nullptr);
		CHECK(selected->get_datatype().is_variant());
		BSParser::IdentifierNode *root = attribute_root(selected);
		BS_TEST_REQUIRE(root != nullptr);
		CHECK(root->source == BSParser::IdentifierNode::FUNCTION_PARAMETER);
		CHECK(root->parameter_source != nullptr);
		CHECK(root->get_datatype().kind == BSParser::DataType::BUILTIN);
		CHECK(root->get_datatype().builtin_type == Variant::DICTIONARY);
		BS_TEST_REQUIRE(root->get_datatype().container_element_types.size() == 2);
		CHECK(root->get_datatype().container_element_types[1].kind == BSParser::DataType::BUILTIN);
		CHECK(root->get_datatype().container_element_types[1].builtin_type == Variant::STRING);
	}

	TEST_CASE("qualified_enum_namespace_yields_to_local_constant_root") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		install(storage, path("top_level_enum_shadow_provider.notest"), "namespace reviewns\n\nenum_name State:\n\tREADY = 1\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func select() -> String:\n\tconst reviewns = { \"State\": \"local\" }\n\treturn reviewns.State\n", path("top_level_enum_local_shadow"), false) == OK);
		check_ok(parser);
		BSParser::ExpressionNode *selected = function_return_value(parser.get_tree(), "select");
		check_string_value(selected, "local");
		BSParser::IdentifierNode *root = attribute_root(selected);
		BS_TEST_REQUIRE(root != nullptr);
		CHECK(root->source == BSParser::IdentifierNode::LOCAL_CONSTANT);
		CHECK(root->constant_source != nullptr);
	}

	TEST_CASE("qualified_enum_namespace_yields_to_member_constant_root") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		install(storage, path("top_level_enum_shadow_provider.notest"), "namespace reviewns\n\nenum_name State:\n\tREADY = 1\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("const reviewns = { \"State\": \"member\" }\nconst Selected: String = reviewns.State\n", path("top_level_enum_member_shadow"), false) == OK);
		check_ok(parser);
		BSParser::ExpressionNode *selected = constant_initializer(parser, "Selected");
		check_string_value(selected, "member");
		BSParser::IdentifierNode *root = attribute_root(selected);
		BS_TEST_REQUIRE(root != nullptr);
		CHECK(root->source == BSParser::IdentifierNode::MEMBER_CONSTANT);
		CHECK(root->constant_source != nullptr);
	}

	TEST_CASE("qualified_enum_namespace_yields_to_nested_lexical_class_root") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		install(storage, path("top_level_enum_shadow_provider.notest"), "namespace reviewns\n\nenum_name State:\n\tREADY = 1\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("class reviewns:\n\tconst State = \"nested\"\nconst Selected: String = reviewns.State\n", path("top_level_enum_nested_class_shadow"), false) == OK);
		check_ok(parser);
		BSParser::ExpressionNode *selected = constant_initializer(parser, "Selected");
		check_string_value(selected, "nested");
		BSParser::IdentifierNode *root = attribute_root(selected);
		BS_TEST_REQUIRE(root != nullptr);
		CHECK(root->source == BSParser::IdentifierNode::MEMBER_CLASS);
	}

	TEST_CASE("qualified_enum_namespace_yields_to_inherited_member_root") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		install(storage, path("top_level_enum_shadow_provider.notest"), "namespace reviewns\n\nenum_name State:\n\tREADY = 1\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(R"source(class Base:
	const reviewns = { "State": "inherited" }
class InheritedChild extends Base:
	const Selected: String = reviewns.State
)source",
								path("top_level_enum_inherited_shadow"), false) == OK);
		check_ok(parser);
		BSParser::ClassNode *inherited = parser.get_tree()->get_member("InheritedChild").m_class;
		BS_TEST_REQUIRE(inherited != nullptr);
		check_string_value(inherited->get_member("Selected").constant->initializer, "inherited");
		BSParser::IdentifierNode *root = attribute_root(inherited->get_member("Selected").constant->initializer);
		BS_TEST_REQUIRE(root != nullptr);
		CHECK(root->source == BSParser::IdentifierNode::MEMBER_CONSTANT);
	}

	TEST_CASE("adjacent_top_level_enum_container_control_remains_static") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		install(storage, path("top_level_enum_standalone.notest"), standalone_provider_source());
		install(storage, path("top_level_enum_containers_provider.notest"), R"source(func get_values() -> Array[TopLevelStandaloneEnum]:
	return [TopLevelStandaloneEnum.RED]

func get_lookup() -> Dictionary[String, TopLevelStandaloneEnum]:
	return {
		"current": TopLevelStandaloneEnum.GREEN,
	}
)source");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(R"source(const Provider = preload("top_level_enum_containers_provider.notest.barista")

func check_containers() -> void:
	var values: Array[TopLevelStandaloneEnum] = Provider.new().get_values()
	var lookup: Dictionary[String, TopLevelStandaloneEnum] = Provider.new().get_lookup()
	var value: TopLevelStandaloneEnum = values[0]
	var mapped: TopLevelStandaloneEnum = lookup["current"]
	print(value, mapped)
)source",
								path("top_level_enum_containers"), false) == OK);
		check_ok(parser);
	}
}
