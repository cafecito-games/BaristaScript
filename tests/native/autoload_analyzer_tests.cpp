/**************************************************************************/
/*  autoload_analyzer_tests.cpp                                           */
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
bool seed(StorageFixture &fixture, const String &path, const String &source) {
	BSParser parser;
	if (parser.parse(source, path, false) != OK) {
		CHECK_MESSAGE(false, "provider setup parse failed");
		return false;
	}
	BSCache::set_source_override(path, source);
	auto record = BSDeclarationIndex::record_from_global_class(path, source, bs_resolve_global_class_from_source(source, path));
	return fixture.index().commit_record(fixture.index().claim_refresh(path), record);
}
Dictionary project_settings_snapshot() {
	Dictionary result;
	const TypedArray<Dictionary> properties = ProjectSettings::get_singleton()->get_property_list();
	for (int i = 0; i < properties.size(); i++) {
		const Dictionary property = properties[i];
		const String name = property["name"];
		if (ProjectSettings::get_singleton()->has_setting(name))
			result[name] = ProjectSettings::get_singleton()->get_setting(name);
	}
	return result;
}
void public_agreement(const String &source, const String &path, bool valid) {
	Ref<BaristaScriptAnalyzerProbe> probe;
	probe.instantiate();
	CHECK(bool(probe->analyze_source(source, path).get("valid", !valid)) == valid);
	CHECK(bool(BaristaScriptLanguage::get_singleton()->_validate(source, path, true, true, true, true).get("valid", !valid)) == valid);
	Ref<BaristaScript> resource;
	resource.instantiate();
	resource->_set_source_code(source);
	resource->set_path(path);
	CHECK(resource->_is_valid() == valid);
}
void error_at(const BSParser::ParserError &error, const String &message, const BSParser::Node *site) {
	CHECK(std::string(error.message.utf8().get_data()) == std::string(message.utf8().get_data()));
	CHECK(error.line == site->start_line);
	CHECK(error.column == site->start_column);
	CHECK(error.end_line == site->end_line);
	CHECK(error.end_column == site->end_column);
}
void exact_error(const BSParser &parser, const String &message, const BSParser::Node *site) {
	BS_TEST_REQUIRE(parser.get_errors().size() == 1);
	const auto &error = parser.get_errors().front()->get();
	CHECK(std::string(error.message.utf8().get_data()) == std::string(message.utf8().get_data()));
	CHECK(error.line == site->start_line);
	CHECK(error.column == site->start_column);
	CHECK(error.end_line == site->end_line);
	CHECK(error.end_column == site->end_column);
}
void rejected_argument(const String &arguments, const String &message, int argument_index, bool dependency = false, const String &suffix = String()) {
	StorageFixture fixture;
	BSParser parser;
	const String source = "@autoload(" + arguments + ")\nclass_name Service extends Node\n" + suffix;
	const String path = "res://tests/autoload/service.barista";
	BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
	BS_TEST_REQUIRE(parser.get_tree()->annotations.size() == 1);
	auto *annotation = parser.get_tree()->annotations.front()->get();
	BSAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
	BS_TEST_REQUIRE(annotation->arguments.size() > argument_index);
	const BSParser::Node *site = annotation->arguments[argument_index];
	if (dependency) {
		BS_TEST_REQUIRE(site->type == BSParser::Node::ARRAY);
		auto *array = static_cast<const BSParser::ArrayNode *>(site);
		BS_TEST_REQUIRE(array->elements.size() == 1);
		site = array->elements[0];
	}
	exact_error(parser, message, site);
	CHECK(annotation->is_resolved);
	CHECK(annotation->resolved_arguments.is_empty());
	const int errors = parser.get_errors().size();
	analyzer.resolve_interface();
	CHECK(parser.get_errors().size() == errors);
	public_agreement(source, path, false);
}
} //namespace
TEST_SUITE("autoload_analyzer") {
	TEST_CASE("normalizes_defaults_signed_endpoints_and_constant_expressions") {
		StorageFixture fixture;
		for (const String &args : { String(), String("()"), String("(depends_on = [], order_id = -2147483648)"), String("([], 2147483647)"), String("(order_id = 10 + 5, depends_on = [])"), String("([], order_id = 0)"), String("(order_id = 0)") }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("@autoload" + args + "\nclass_name Service extends Node\n", "res://tests/autoload/service.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			CHECK(parser.get_errors().is_empty());
			public_agreement("@autoload" + args + "\nclass_name Service extends Node\n", "res://tests/autoload/service.barista", true);
			BS_TEST_REQUIRE(parser.get_tree()->annotations.size() == 1);
			auto *annotation = parser.get_tree()->annotations.front()->get();
			CHECK(annotation->is_resolved);
			BS_TEST_REQUIRE(annotation->resolved_arguments.size() == 2);
			CHECK(annotation->resolved_arguments[0].get_type() == Variant::ARRAY);
			CHECK(Array(annotation->resolved_arguments[0]).is_empty());
			CHECK(annotation->resolved_arguments[1].get_type() == Variant::INT);
			const int64_t expected = args.contains("-2147483648") ? INT64_C(-2147483648) : args.contains("2147483647") ? INT64_C(2147483647)
					: args.contains("10 + 5")																		   ? 15
																													   : 0;
			CHECK(int64_t(annotation->resolved_arguments[1]) == expected);
			analyzer.resolve_interface();
			CHECK(annotation->resolved_arguments.size() == 2);
			CHECK(parser.get_errors().is_empty());
		}
	}
	TEST_CASE("rejects_order_type_and_range_without_coercion") {
		for (const String &value : { String("1.0"), String("true"), String("runtime") })
			rejected_argument("order_id = " + value, "Argument \"order_id\" of annotation \"@autoload\" must be a constant integer expression.", 0, false, "var runtime: int\n");
		for (const String &value : { String("2147483648"), String("-2147483649") })
			rejected_argument("order_id = " + value, "\"order_id\" of annotation \"@autoload\" must fit in a 32-bit signed integer.", 0);
	}
	TEST_CASE("requires_literal_array_and_script_class_elements") {
		for (const String &value : { String("123"), String("ALIAS") })
			rejected_argument("depends_on = " + value, "Argument \"depends_on\" of annotation \"@autoload\" must be an array of class names.", 0, false, "const ALIAS = []\n");
		for (const String &value : { String("\"Service\""), String("123"), String("Node") })
			rejected_argument("depends_on = [" + value + "]", "Dependency 1 of annotation \"@autoload\" must resolve to a script class.", 0, true);
	}
	TEST_CASE("binds_each_slot_once_and_requires_positional_prefix") {
		rejected_argument("order_id = 1, order_id = 2", "Parameter \"order_id\" of annotation \"@autoload\" was specified more than once.", 1);
		rejected_argument("[], depends_on = []", "Parameter \"depends_on\" of annotation \"@autoload\" was specified more than once.", 1);
		rejected_argument("unknown = 1", "Annotation \"@autoload\" has no parameter named \"unknown\".", 0);
		rejected_argument("order_id = 1, []", "Positional argument after named argument in annotation \"@autoload\".", 1);
	}
	TEST_CASE("excess_arguments_reach_autoload_binding_diagnostic") {
		rejected_argument("[], 0, 1", "Annotation \"@autoload\" takes at most 2 argument(s), but 3 were given.", 2);
	}
	TEST_CASE("rejects_unnamed_trait_and_non_node_roots") {
		StorageFixture fixture;
		for (const String &declaration : { String("extends Node"), String("trait_name Service extends Node"), String("class_name Service extends RefCounted") }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("@autoload\n" + declaration + "\n", "res://tests/autoload/service.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			BS_TEST_REQUIRE(parser.get_tree()->annotations.size() == 1);
			auto *annotation = parser.get_tree()->annotations.front()->get();
			exact_error(parser, declaration.contains("RefCounted") ? "\"@autoload\" requires the script to inherit from \"Node\"." : "\"@autoload\" requires \"class_name\".", annotation);
			CHECK(annotation->resolved_arguments.is_empty());
			public_agreement("@autoload\n" + declaration + "\n", "res://tests/autoload/service.barista", false);
		}
	}
	TEST_CASE("duplicate_annotations_remain_invalid_and_idempotent") {
		StorageFixture fixture;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("@autoload\n@autoload\nclass_name Service extends Node\n", "res://tests/autoload/service.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		BS_TEST_REQUIRE(parser.get_tree()->annotations.size() == 2);
		BS_TEST_REQUIRE(parser.get_errors().size() == 2);
		auto *error = parser.get_errors().front();
		for (auto *annotation : parser.get_tree()->annotations) {
			error_at(error->get(), "\"@autoload\" annotation can only be used once per script.", annotation);
			error = error->next();
			CHECK(annotation->is_resolved);
			CHECK(annotation->resolved_arguments.is_empty());
		}
		analyzer.resolve_interface();
		CHECK(parser.get_errors().size() == 2);
		public_agreement("@autoload\n@autoload\nclass_name Service extends Node\n", "res://tests/autoload/service.barista", false);
	}
	TEST_CASE("enum_and_tuple_heads_are_not_class_name_roots") {
		StorageFixture fixture;
		for (const String &declaration : { String("enum_name Service:\n\tZERO = 0\n"), String("tuple_name Service(value: int, count: int)\n") }) {
			BSParser admitted;
			BS_TEST_REQUIRE(admitted.parse(declaration, "res://tests/autoload/service.barista", false) == OK);
			BSParser parser;
			CHECK(parser.parse("@autoload\n" + declaration, "res://tests/autoload/service.barista", false) != OK);
			BS_TEST_REQUIRE(parser.get_errors().size() == 2);
			const bool is_enum = declaration.begins_with("enum_name");
			// Both parse_program and parse_enum_name/parse_tuple_name reject the
			// annotation, before advancing past the same declaration keyword.
			for (const auto &error : parser.get_errors()) {
				CHECK(error.message == (is_enum ? "An \"enum_name\" file may only contain its enum declaration." : "A \"tuple_name\" file may only contain its tuple declaration."));
				// parse_program rejects immediately after advancing the declaration keyword;
				// push_error uses that previous token, matching the pinned parser's admission gate.
				CHECK(error.line == 2);
				CHECK(error.column == 1);
				CHECK(error.end_line == 2);
				CHECK(error.end_column == (is_enum ? 10 : 11));
			}
			public_agreement("@autoload\n" + declaration, "res://tests/autoload/service.barista", false);
		}
	}
	TEST_CASE("external_dependency_identity_and_native_base_are_owner_backed") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(seed(fixture, "res://tests/autoload/dependency.barista", "namespace services\nclass_name Dependency extends Node2D\n"));
		const String source = "@autoload(depends_on = [Dependency, Dependency], order_id = 10 + 5)\nimport services\nclass_name Service extends Dependency\n";
		for (bool direct_interface : { false, true }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, "res://tests/autoload/service.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((direct_interface ? analyzer.resolve_interface() : analyzer.analyze()) == OK);
			CHECK(parser.get_errors().is_empty());
			BS_TEST_REQUIRE(parser.get_tree()->annotations.size() == 1);
			const auto *annotation = parser.get_tree()->annotations.front()->get();
			BS_TEST_REQUIRE(annotation->resolved_arguments.size() == 2);
			const Array names = annotation->resolved_arguments[0];
			BS_TEST_REQUIRE(names.size() == 2);
			CHECK(StringName(names[0]) == StringName("services.Dependency"));
			CHECK(names[0] == names[1]);
			CHECK(int64_t(annotation->resolved_arguments[1]) == 15);
			CHECK(parser.get_tree()->base_type.native_type == StringName("Node2D"));
		}
		public_agreement(source, "res://tests/autoload/service.barista", true);
	}
	TEST_CASE("root_only_annotation_placement_preserves_parser_rejection") {
		StorageFixture fixture;
		const String source = "class_name Service extends Node\nclass Inner extends Node:\n\t@autoload\n\tvar value: int\n";
		BSParser parser;
		CHECK(parser.parse(source, "res://tests/autoload/service.barista", false) != OK);
		BS_TEST_REQUIRE(parser.get_errors().size() == 1);
		const auto &error = parser.get_errors().front()->get();
		CHECK(error.message == "Annotation \"@autoload\" must be at the top of the script, before \"extends\" and \"class_name\".");
		// parse_annotation has consumed the annotation token when checking target kinds.
		CHECK(error.line == 3);
		CHECK(error.column == 5);
		CHECK(error.end_line == 3);
		CHECK(error.end_column == 14);
		public_agreement(source, "res://tests/autoload/service.barista", false);
	}
	TEST_CASE("expression_errors_and_lexical_claims_prevent_partial_publication") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(seed(fixture, "res://tests/autoload/dependency.barista", "class_name Dependency extends Node\n"));
		const String path = "res://tests/autoload/service.barista";
		for (bool missing_order : { false, true }) {
			const String source = missing_order ? "@autoload(depends_on = [Dependency], order_id = Missing)\nclass_name Service extends Node\n" : "@autoload(depends_on = [Dependency, Missing])\nclass_name Service extends Node\n";
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			BS_TEST_REQUIRE(parser.get_tree()->annotations.size() == 1);
			const auto *annotation = parser.get_tree()->annotations.front()->get();
			BS_TEST_REQUIRE(annotation->arguments.size() == (missing_order ? 2 : 1));
			const BSParser::Node *site = annotation->arguments[missing_order ? 1 : 0];
			if (!missing_order) {
				BS_TEST_REQUIRE(site->type == BSParser::Node::ARRAY);
				const auto *array = static_cast<const BSParser::ArrayNode *>(site);
				BS_TEST_REQUIRE(array->elements.size() == 2);
				site = array->elements[1];
			}
			BS_TEST_REQUIRE(parser.get_errors().size() == 2);
			error_at(parser.get_errors().front()->get(), "Identifier \"Missing\" not declared in the current scope.", site);
			error_at(parser.get_errors().back()->get(), missing_order ? "Argument \"order_id\" of annotation \"@autoload\" must be a constant integer expression." : "Dependency 2 of annotation \"@autoload\" must resolve to a script class.", site);
			CHECK(annotation->is_resolved);
			CHECK(annotation->resolved_arguments.is_empty());
			analyzer.resolve_interface();
			CHECK(parser.get_errors().size() == 2);
			public_agreement(source, path, false);
		}
		const String source = "@autoload(depends_on = [Dependency])\nclass_name Service extends Node\nconst Dependency = 1\n";
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		BS_TEST_REQUIRE(parser.get_tree()->annotations.size() == 1);
		const auto *annotation = parser.get_tree()->annotations.front()->get();
		BS_TEST_REQUIRE(annotation->arguments.size() == 1);
		BS_TEST_REQUIRE(annotation->arguments[0]->type == BSParser::Node::ARRAY);
		const auto *array = static_cast<const BSParser::ArrayNode *>(annotation->arguments[0]);
		BS_TEST_REQUIRE(array->elements.size() == 1);
		exact_error(parser, "Dependency 1 of annotation \"@autoload\" must resolve to a script class.", array->elements[0]);
		CHECK(annotation->resolved_arguments.is_empty());
		public_agreement(source, path, false);
	}
	TEST_CASE("stale_and_missing_dependency_claims_do_not_normalize_or_repair") {
		for (int state : { 0, 1, 2 }) {
			StorageFixture fixture;
			const String path = "res://tests/autoload/dependency.barista";
			const String provider = "class_name Dependency extends Node\n";
			BS_TEST_REQUIRE(seed(fixture, path, provider));
			const uint64_t revision = fixture.index().get_refresh_revision(path);
			if (state == 0)
				BSCache::set_source_override(path, "class_name Dependency extends Node2D\n");
			if (state == 1)
				BSCache::clear_source_override(path);
			if (state == 2)
				BSCache::set_source_override(path, "class_name Different extends Node\n");
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("@autoload(depends_on = [Dependency])\nclass_name Service extends Node\n", "res://tests/autoload/service.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			BS_TEST_REQUIRE(parser.get_tree()->annotations.size() == 1);
			auto *annotation = parser.get_tree()->annotations.front()->get();
			BS_TEST_REQUIRE(annotation->arguments.size() == 1);
			BS_TEST_REQUIRE(annotation->arguments[0]->type == BSParser::Node::ARRAY);
			auto *dependencies = static_cast<BSParser::ArrayNode *>(annotation->arguments[0]);
			BS_TEST_REQUIRE(dependencies->elements.size() == 1);
			BS_TEST_REQUIRE(parser.get_errors().size() == 2);
			error_at(parser.get_errors().front()->get(), "Could not resolve type \"Dependency\": declaration \"Dependency\" from \"res://tests/autoload/dependency.barista\" is stale or invalid.", dependencies->elements[0]);
			error_at(parser.get_errors().back()->get(), "Dependency 1 of annotation \"@autoload\" must resolve to a script class.", dependencies->elements[0]);
			CHECK(annotation->resolved_arguments.is_empty());
			CHECK(fixture.index().get_refresh_revision(path) == revision);
			BSCache::set_source_override(path, provider);
			BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, provider) == OK);
			BSParser repaired;
			BS_TEST_REQUIRE(repaired.parse("@autoload(depends_on = [Dependency])\nclass_name Service extends Node\n", "res://tests/autoload/service.barista", false) == OK);
			BSAnalyzer repaired_analyzer(&repaired);
			CHECK(repaired_analyzer.analyze() == OK);
			CHECK(repaired.get_errors().is_empty());
		}
	}
	TEST_CASE("public_apis_agree_and_preserve_settings_index_and_overrides") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String provider_path = "res://tests/autoload/dependency.barista";
		const String path = "res://tests/autoload/service.barista";
		BS_TEST_REQUIRE(seed(fixture, provider_path, "class_name Dependency extends Node\n"));
		const Dictionary settings = project_settings_snapshot();
		const String store = fixture.path("index.bin");
		BS_TEST_REQUIRE(fixture.index().flush(store) == OK);
		const auto original = read_bytes(store);
		const uint64_t revision = fixture.index().get_refresh_revision(provider_path);
		Ref<BaristaScriptAnalyzerProbe> probe;
		probe.instantiate();
		for (int state : { 0, 1, 2 }) {
			const String previous = state == 2 ? "class_name Unsaved extends Node\n" : "";
			if (state == 0) {
				BSCache::clear_source_override(path);
			} else {
				BSCache::set_source_override(path, previous);
			}
			for (bool valid : { true, false }) {
				const String source = valid ? "@autoload(depends_on = [Dependency], order_id = 15)\nclass_name Service extends Node\n" : "@autoload(order_id = true)\nclass_name Service extends Node\n";
				CHECK(bool(probe->analyze_source(source, path).get("valid", !valid)) == valid);
				const Dictionary validation = BaristaScriptLanguage::get_singleton()->_validate(source, path, true, true, true, true);
				CHECK(bool(validation.get("valid", !valid)) == valid);
				Ref<BaristaScript> resource;
				resource.instantiate();
				resource->_set_source_code(source);
				resource->set_path(path);
				CHECK(resource->_is_valid() == valid);
				CHECK(BSCache::has_source_override(path) == (state != 0));
				if (state != 0) {
					CHECK(BSCache::get_source_code(path) == previous);
				}
				CHECK(project_settings_snapshot() == settings);
				CHECK(fixture.index().get_refresh_revision(provider_path) == revision);
				CHECK(fixture.index().flush(store) == OK);
				CHECK(read_bytes(store) == original);
				CHECK(BSConformanceRegistry::get_singleton()->get_file_conformances(path).is_empty());
				CHECK(BSConformanceRegistry::get_singleton()->get_file_trait_bindings(path).is_empty());
			}
		}
	}
}
