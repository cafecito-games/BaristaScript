/**************************************************************************/
/*  provider_analyzer_test.cpp                                            */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_global_class.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <godot_cpp/classes/project_settings.hpp>
#include <string>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace barista_script {
struct ProviderTestAccess {
	static BSParser::DataType head_type(BSAnalyzer &analyzer, const StringName &name, bool nullable) {
		BSParser::IdentifierNode identifier;
		identifier.name = name;
		BSParser::TypeNode use_site;
		use_site.type_chain.push_back(&identifier);
		use_site.is_nullable = nullable;
		auto *previous = analyzer.current_class;
		analyzer.current_class = analyzer.parser->get_tree();
		const auto type = analyzer.datatype_from_type_node(&use_site);
		analyzer.current_class = previous;
		return type;
	}

	static BSParser::FunctionNode *witness(BSAnalyzer &analyzer, const BSParser::DataType &type, const StringName &name) {
		BSAnalyzer::ForeignAnalyzerVisibilityScope visibility(&analyzer);
		return analyzer.find_conformance_witness(type, name);
	}

	static Ref<BSParserRef> owner(BSAnalyzer &analyzer, const BSParser::ClassNode *node, const BSParser::Node *site) { return analyzer.ensure_external_parser(node, "native owner check", site); }
	static int cached_owners(BSAnalyzer &analyzer) { return analyzer.external_class_parser_cache.size(); }
	static void strict(BSAnalyzer &analyzer) { analyzer.strict_dynamic_checks = true; }
	static void strict_null(BSAnalyzer &analyzer) { analyzer.strict_null_checks = true; }
	static bool same_type(BSAnalyzer &analyzer, const BSParser::DataType &a, const BSParser::DataType &b) { return analyzer.datatype_strict_identity_equal(a, b); }
	static void interface(BSAnalyzer &analyzer, BSParser::ClassNode *node, const BSParser::Node *site) { analyzer.analyze_class_interface(node, site); }
	static void body(BSAnalyzer &analyzer, BSParser::ClassNode *node, const BSParser::Node *site) { analyzer.analyze_class_body(node, site); }

	static void member(BSAnalyzer &analyzer, BSParser::ClassNode *owner, const StringName &name, const BSParser::Node *site) {
		analyzer.resolve_class_member(owner, name, site);
	}
};
} //namespace barista_script
struct ProviderCacheTestAccess {
	static int abandoned() { return BSCache::get_singleton()->abandoned_parser_map.size(); }
	static int edges() { return BSCache::get_singleton()->parser_dependencies.size() + BSCache::get_singleton()->parser_inverse_dependencies.size(); }
};
namespace {
struct UnsafePropertyWarningScope {
	const String path = BSWarning::get_setting_path_from_code(BSWarning::UNSAFE_PROPERTY_ACCESS);
	Variant previous = ProjectSettings::get_singleton()->get_setting(path);
	UnsafePropertyWarningScope() {
		ProjectSettings::get_singleton()->set_setting(path, BSWarning::WARN);
		BSParser::update_project_settings();
	}
	~UnsafePropertyWarningScope() {
		ProjectSettings::get_singleton()->set_setting(path, previous);
		BSParser::update_project_settings();
	}
};
// These are typed, test-local observations of the production parser and owner cache.
// Foundry c9d5e35: owner identity 11635-11755; members 11787/12145-12220;
// named heads 9544-9727; missing instance-property mode distinction 14540-14552.
void diagnostic(const BSParser &parser, const String &message, int line, int column, int end_line, int end_column) {
	for (const auto &error : parser.get_errors()) {
		INFO(std::string(error.message.utf8().get_data()));
		INFO(error.line);
		INFO(error.column);
		INFO(error.end_line);
		INFO(error.end_column);
		CHECK(parser.get_errors().size() == 1);
	}
	BS_TEST_REQUIRE(parser.get_errors().size() == 1);
	const auto &error = parser.get_errors().front()->get();
	CHECK(std::string(error.message.utf8().get_data()) == std::string(message.utf8().get_data()));
	CHECK(error.line == line);
	CHECK(error.column == column);
	CHECK(error.end_line == end_line);
	CHECK(error.end_column == end_column);
}
String provider(StorageFixture &fixture, const String &name, const String &source) {
	const String path = String("res://tests/x2/") + name + String(".barista");
	BSCache::set_source_override(path, source);
	const auto record = BSDeclarationIndex::record_from_global_class(path, source, bs_resolve_global_class_from_source(source, path));
	BSParser syntax;
	const Error parsed = syntax.parse(source, path, false);
	for (const auto &error : syntax.get_errors()) {
		INFO(std::string(error.message.utf8().get_data()));
		CHECK(parsed == OK);
	}
	if (parsed != OK) {
		return String();
	}
	const bool committed = fixture.index().commit_record(fixture.index().claim_refresh(path), record);
	CHECK(committed);
	if (!committed) {
		return String();
	}
	return path;
}
void no_errors(const BSParser &parser) {
	for (const auto &error : parser.get_errors()) {
		INFO(std::string(error.message.utf8().get_data()));
		CHECK(false);
	}
}
} //namespace
TEST_SUITE("provider_analyzer") {
	TEST_CASE("indexed_receiver_owns_provider_and_types_properties_and_calls") {
		StorageFixture fixture;
		const String path = provider(fixture, "provider", "class_name Provider\nvar count: int\nfunc text() -> String:\n\treturn \"ok\"\n");
		BS_TEST_REQUIRE(!path.is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("var p: Provider\nvar n := p.count\nvar s := p.text()\n", fixture.path("consumer.barista"), false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() == OK);
		no_errors(consumer);
		CHECK(consumer.get_depended_parsers().has(path));
		BS_TEST_REQUIRE(consumer.get_tree()->has_member("p"));
		CHECK(consumer.get_tree()->get_member("p").get_datatype().class_type != nullptr);
		CHECK(consumer.get_tree()->get_member("n").get_datatype().builtin_type == Variant::INT);
		CHECK(consumer.get_tree()->get_member("s").get_datatype().builtin_type == Variant::STRING);
	}
	TEST_CASE("foreign_named_tuple_expands_fields_in_owner_scope") {
		StorageFixture fixture;
		const String path = provider(fixture, "pair", "tuple_name Pair(count: int, text: String)\n");
		BS_TEST_REQUIRE(!path.is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("var pair: Pair\nvar x := pair.count\n", fixture.path("consumer.barista"), false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() == OK);
		no_errors(consumer);
		const auto type = consumer.get_tree()->get_member("pair").get_datatype();
		CHECK(type.kind == BSParser::DataType::TUPLE);
		CHECK(type.container_element_types.size() == 2);
		CHECK(consumer.get_depended_parsers().has(path));
		CHECK(consumer.get_tree()->get_member("x").get_datatype().builtin_type == Variant::INT);
	}
	TEST_CASE("remove_script_detaches_without_clearing_retained_generation") {
		StorageFixture fixture;
		const String path = provider(fixture, "provider", "class_name Provider\nvar count: int\n");
		BS_TEST_REQUIRE(!path.is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("var use_site: int\n", fixture.path("consumer.barista"), false) == OK);
		BSAnalyzer analyzer(&consumer);
		Ref<BSParserRef> retained = consumer.get_depended_parser_for(path);
		BS_TEST_REQUIRE(retained.is_valid());
		BS_TEST_REQUIRE(retained->raise_status(BSParserRef::INHERITANCE_SOLVED) == OK);
		BSParser *old_parser = retained->get_parser();
		BSParser::ClassNode *old_class = old_parser->get_tree();
		BSCache::remove_script(path);
		CHECK_FALSE(BSCache::has_parser(path));
		BS_TEST_REQUIRE(retained->get_status() == BSParserRef::INHERITANCE_SOLVED);
		BS_TEST_REQUIRE(retained->get_parser() == old_parser);
		BS_TEST_REQUIRE(retained->get_parser()->has_class(old_class));
		BS_TEST_REQUIRE(!provider(fixture, "provider", "class_name Provider\nvar count: String\n").is_empty());
		Error error = OK;
		Ref<BSParserRef> fresh = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && fresh.is_valid());
		ProviderTestAccess::member(analyzer, old_class, "count", consumer.get_tree()->get_member("use_site").get_source_node());
		no_errors(consumer);
		CHECK(old_class->get_member("count").get_datatype().builtin_type == Variant::INT);
		CHECK(fresh->get_parser()->get_tree()->get_member("count").get_datatype().builtin_type == Variant::STRING);
	}
	TEST_CASE("property_and_method_destination_mismatches_preserve_full_diagnostics") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(!provider(fixture, "provider", "class_name Provider\nvar count: int\nfunc text() -> String:\n\treturn \"ok\"\n").is_empty());
		for (const String &source : { String("var p: Provider\nvar x: String = p.count\n"), String("var p: Provider\nvar x: int = p.text()\n") }) {
			BSParser consumer;
			BS_TEST_REQUIRE(consumer.parse(source, fixture.path("consumer.barista"), false) == OK);
			BSAnalyzer analyzer(&consumer);
			CHECK(analyzer.analyze() != OK);
			const bool property = source.contains("p.count");
			diagnostic(consumer, property ? "Cannot assign a value of type int to variable \"x\" with specified type String." : "Cannot assign a value of type String to variable \"x\" with specified type int.", 2, property ? 17 : 14, 2, property ? 24 : 22);
		}
	}
	TEST_CASE("missing_property_preserves_default_unsafe_and_strict_error_modes") {
		UnsafePropertyWarningScope warning_scope;
		StorageFixture fixture;
		BS_TEST_REQUIRE(!provider(fixture, "provider", "class_name Provider\nvar count: int\n").is_empty());
		for (const bool strict : { false, true }) {
			BSParser consumer;
			BS_TEST_REQUIRE(consumer.parse("var p: Provider\nvar x = p.missing\n", fixture.path("consumer.barista"), false) == OK);
			BSAnalyzer analyzer(&consumer);
			if (strict) {
				ProviderTestAccess::strict(analyzer);
			}
			const Error result = analyzer.analyze();
			if (strict) {
				CHECK(result != OK);
				diagnostic(consumer, "Cannot resolve member \"missing\" on type \"Provider\" in strict dynamic mode.", 2, 11, 2, 18);
			} else {
				CHECK(result == OK);
				no_errors(consumer);
				CHECK(consumer.get_unsafe_lines().has(2));
				int unsafe_warnings = 0;
				for (const auto &warning : consumer.get_warnings()) {
					if (warning.code != BSWarning::UNSAFE_PROPERTY_ACCESS) {
						continue;
					}
					++unsafe_warnings;
					CHECK(warning.get_message() == "The property \"missing\" is not present on the inferred type \"Provider\" (but may be present on a subtype).");
					CHECK(warning.start_line == 2);
					CHECK(warning.start_column == 9);
					CHECK(warning.end_line == 2);
					CHECK(warning.end_column == 18);
				}
				CHECK(unsafe_warnings == 1);
				CHECK(consumer.get_tree()->get_member("x").get_datatype().is_variant());
			}
		}
	}
	TEST_CASE("metatype_members_and_inherited_nested_surfaces") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(!provider(fixture, "provider", "class_name Provider\nconst COUNT = 7\nstatic func text() -> String:\n\treturn \"ok\"\nclass Nested:\n\tvar count: int\nenum State:\n\tREADY = 0\ntuple Pair(count: int, text: String)\n").is_empty());
		BS_TEST_REQUIRE(!provider(fixture, "child", "class_name Child extends Provider\n").is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("var n := Child.COUNT\nvar s := Child.text()\nvar nested: Child.Nested\nvar count := nested.count\nvar state := Child.State.READY\nvar pair: Child.Pair\nvar field := pair.text\nvar callback := Child.text\n", fixture.path("consumer.barista"), false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() == OK);
		no_errors(consumer);
		CHECK(consumer.get_tree()->get_member("n").get_datatype().builtin_type == Variant::INT);
		CHECK(consumer.get_tree()->get_member("s").get_datatype().builtin_type == Variant::STRING);
		CHECK(consumer.get_tree()->get_member("count").get_datatype().builtin_type == Variant::INT);
		CHECK(consumer.get_tree()->get_member("state").get_datatype().kind == BSParser::DataType::ENUM);
		CHECK(consumer.get_tree()->get_member("field").get_datatype().builtin_type == Variant::STRING);
		CHECK(consumer.get_tree()->get_member("callback").get_datatype().has_method_signature);
	}
	TEST_CASE("metatype_missing_member_and_instance_restriction") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(!provider(fixture, "provider", "class_name Provider\nvar count: int\n").is_empty());
		for (const String &name : { String("missing"), String("count") }) {
			BSParser consumer;
			BS_TEST_REQUIRE(consumer.parse(String("var x = Provider.") + name + String("\n"), fixture.path("consumer.barista"), false) == OK);
			BSAnalyzer analyzer(&consumer);
			CHECK(analyzer.analyze() != OK);
			diagnostic(consumer, String("Cannot find member \"") + name + String("\" in base \"Provider\"."), 1, 18, 1, 18 + name.length());
		}
	}
	TEST_CASE("owner_lookup_caches_positive_and_negative_and_preserves_use_sites") {
		StorageFixture fixture;
		const String path = provider(fixture, "provider", "class_name Provider\nvar count: int\n");
		BS_TEST_REQUIRE(!path.is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("var first: int\nvar second: int\n", fixture.path("consumer.barista"), false) == OK);
		BSAnalyzer analyzer(&consumer);
		const auto *site = consumer.get_tree()->get_member("first").get_source_node();
		Ref<BSParserRef> retained = consumer.get_depended_parser_for(path);
		BS_TEST_REQUIRE(retained.is_valid());
		BS_TEST_REQUIRE(retained->raise_status(BSParserRef::PARSED) == OK);
		CHECK(ProviderTestAccess::owner(analyzer, retained->get_parser()->get_tree(), site) == retained);
		BSCache::remove_parser(path);
		CHECK(ProviderTestAccess::owner(analyzer, retained->get_parser()->get_tree(), site) == retained);
		CHECK(ProviderTestAccess::cached_owners(analyzer) == 1);
		BSParser unowned;
		BS_TEST_REQUIRE(unowned.parse("class_name Orphan\nvar count: int\n", fixture.path("orphan.barista"), false) == OK);
		CHECK(ProviderTestAccess::owner(analyzer, unowned.get_tree(), site).is_null());
		CHECK(ProviderTestAccess::owner(analyzer, unowned.get_tree(), site).is_null());
		diagnostic(consumer, "Parser bug (please report): Could not find external parser for class \"Orphan\". (native owner check)", 1, 1, 1, 15);
		const auto *second = consumer.get_tree()->get_member("second").get_source_node();
		CHECK(ProviderTestAccess::owner(analyzer, unowned.get_tree(), second).is_null());
		CHECK(consumer.get_errors().size() == 2);
		BS_TEST_REQUIRE(consumer.get_errors().size() == 2);
		const auto &second_error = consumer.get_errors().back()->get();
		CHECK(second_error.message == "Parser bug (please report): Could not find external parser for class \"Orphan\". (native owner check)");
		CHECK(second_error.line == 2);
		CHECK(second_error.column == 1);
		CHECK(second_error.end_line == 2);
		CHECK(second_error.end_column == 16);
		CHECK(ProviderTestAccess::cached_owners(analyzer) == 2);
	}
	TEST_CASE("transitive_nested_owner_survives_global_replacement") {
		StorageFixture fixture;
		const String path = provider(fixture, "provider", "class_name Provider\nclass Nested:\n\tvar count: int\n");
		BS_TEST_REQUIRE(!path.is_empty());
		const String bridge_path = provider(fixture, "bridge", "class_name Bridge\n");
		BS_TEST_REQUIRE(!bridge_path.is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("var site: int\n", fixture.path("consumer.barista"), false) == OK);
		BSAnalyzer analyzer(&consumer);
		Ref<BSParserRef> bridge = consumer.get_depended_parser_for(bridge_path);
		BS_TEST_REQUIRE(bridge.is_valid());
		BS_TEST_REQUIRE(bridge->raise_status(BSParserRef::PARSED) == OK);
		Ref<BSParserRef> old = bridge->get_parser()->get_depended_parser_for(path);
		BS_TEST_REQUIRE(old.is_valid());
		BS_TEST_REQUIRE(old->raise_status(BSParserRef::INHERITANCE_SOLVED) == OK);
		auto *nested = old->get_parser()->get_tree()->get_member("Nested").m_class;
		BS_TEST_REQUIRE(nested != nullptr);
		BSCache::remove_parser(path);
		BS_TEST_REQUIRE(!provider(fixture, "provider", "class_name Provider\nclass Nested:\n\tvar count: String\n").is_empty());
		Error error = OK;
		auto fresh = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && fresh.is_valid());
		const auto *site = consumer.get_tree()->get_member("site").get_source_node();
		CHECK(ProviderTestAccess::owner(analyzer, nested, site) == old);
		ProviderTestAccess::member(analyzer, nested, "count", site);
		no_errors(consumer);
		CHECK(nested->get_member("count").get_datatype().builtin_type == Variant::INT);
		CHECK(fresh->get_parser()->get_tree()->get_member("Nested").m_class->get_member("count").get_datatype().builtin_type == Variant::STRING);
		CHECK_FALSE(consumer.get_depended_parsers().has(path));
	}
	TEST_CASE("abandoned_metadata_and_parser_edges_release_after_last_owner") {
		StorageFixture fixture;
		for (int i = 0; i < 12; ++i) {
			const String path = provider(fixture, String("provider") + String::num_int64(i), "class_name Provider\nvar count: int\n");
			BS_TEST_REQUIRE(!path.is_empty());
			{
				BSParser consumer;
				BS_TEST_REQUIRE(consumer.parse("", fixture.path("consumer.barista"), false) == OK);
				Ref<BSParserRef> ref = consumer.get_depended_parser_for(path);
				BS_TEST_REQUIRE(ref.is_valid());
				BS_TEST_REQUIRE(ref->raise_status(BSParserRef::INHERITANCE_SOLVED) == OK);
				BSCache::remove_parser(path);
				CHECK(ProviderCacheTestAccess::abandoned() == 1);
			}
			CHECK(ProviderCacheTestAccess::abandoned() == 0);
			CHECK(ProviderCacheTestAccess::edges() == 0);
			BSCache::remove_script(path);
			CHECK(fixture.index().remove_path(path, fixture.index().claim_refresh(path)));
		}
	}
	TEST_CASE("mutually_typed_members_release_after_cache_clear") {
		StorageFixture fixture;
		const String a_path = provider(fixture, "a", "class_name A\nvar b: B\n");
		BS_TEST_REQUIRE(!a_path.is_empty());
		const String b_path = provider(fixture, "b", "class_name B\nvar a: A\n");
		BS_TEST_REQUIRE(!b_path.is_empty());
		uint64_t a_id = 0, b_id = 0;
		{
			Error error = OK;
			auto a = BSCache::get_parser(a_path, BSParserRef::INTERFACE_SOLVED, error);
			BS_TEST_REQUIRE(error == OK && a.is_valid());
			auto b = BSCache::get_parser(b_path, BSParserRef::INTERFACE_SOLVED, error);
			BS_TEST_REQUIRE(error == OK && b.is_valid());
			CHECK(a->get_parser()->get_depended_parsers().has(b_path));
			CHECK(b->get_parser()->get_depended_parsers().has(a_path));
			a_id = a->get_instance_id();
			b_id = b->get_instance_id();
		}
		BSCache::clear();
		CHECK(bool(ObjectDB::get_instance(a_id) == nullptr));
		CHECK(bool(ObjectDB::get_instance(b_id) == nullptr));
	}
	TEST_CASE("live_external_root_keeps_mutual_cycle_and_owner_caches_usable") {
		StorageFixture fixture;
		for (const bool invalidate : { false, true }) {
			const String a_path = provider(fixture, "a", "class_name A\nvar b: B\n");
			BS_TEST_REQUIRE(!a_path.is_empty());
			const String b_path = provider(fixture, "b", "class_name B\nvar a: A\n");
			BS_TEST_REQUIRE(!b_path.is_empty());
			Error error = OK;
			auto a = BSCache::get_parser(a_path, BSParserRef::INTERFACE_SOLVED, error);
			BS_TEST_REQUIRE(error == OK && a.is_valid());
			auto b = BSCache::get_parser(b_path, BSParserRef::INTERFACE_SOLVED, error);
			BS_TEST_REQUIRE(error == OK && b.is_valid());
			const uint64_t a_id = a->get_instance_id(), b_id = b->get_instance_id();
			CHECK(ProviderTestAccess::owner(*a->get_analyzer(), b->get_parser()->get_tree(), a->get_parser()->get_tree()) == b);
			CHECK(ProviderTestAccess::owner(*b->get_analyzer(), a->get_parser()->get_tree(), b->get_parser()->get_tree()) == a);
			if (invalidate) {
				BSCache::remove_parser(a_path);
			}
			b.unref();
			BSCache::clear();
			BS_TEST_REQUIRE(a->get_status() == BSParserRef::INTERFACE_SOLVED);
			BS_TEST_REQUIRE(a->get_parser()->get_tree()->has_member("b"));
			const auto *b_class = a->get_parser()->get_tree()->get_member("b").get_datatype().class_type;
			BS_TEST_REQUIRE(b_class != nullptr);
			CHECK(b_class->get_member("a").get_datatype().class_type == a->get_parser()->get_tree());
			CHECK(bool(ObjectDB::get_instance(b_id) != nullptr));
			CHECK(ProviderCacheTestAccess::abandoned() == 2);
			a.unref();
			BSCache::clear();
			CHECK(bool(ObjectDB::get_instance(a_id) == nullptr));
			CHECK(bool(ObjectDB::get_instance(b_id) == nullptr));
			CHECK(ProviderCacheTestAccess::abandoned() == 0);
			CHECK(ProviderCacheTestAccess::edges() == 0);
		}
	}
	TEST_CASE("retained_witness_identity_survives_conformance_reordering") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState conformances;
		const String head = "class_name Witnesses\ntrait IntWitness:\n\tabstract func marker() -> int\ntrait StringWitness:\n\tabstract func marker() -> String\n";
		const String integer = "extend int uses IntWitness:\n\tfunc marker() -> int:\n\t\treturn 1\n";
		const String text = "extend String uses StringWitness:\n\tfunc marker() -> String:\n\t\treturn \"ok\"\n";
		const String path = provider(fixture, "witnesses", head + integer + text);
		BS_TEST_REQUIRE(!path.is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("", fixture.path("consumer.barista"), false) == OK);
		BSAnalyzer analyzer(&consumer);
		auto old = consumer.get_depended_parser_for(path);
		BS_TEST_REQUIRE(old.is_valid());
		const Error old_error = old->raise_status(BSParserRef::INTERFACE_SOLVED);
		no_errors(*old->get_parser());
		BS_TEST_REQUIRE(old_error == OK);
		BSParser::DataType type;
		type.kind = BSParser::DataType::BUILTIN;
		type.builtin_type = Variant::INT;
		auto *before = ProviderTestAccess::witness(analyzer, type, "marker");
		BS_TEST_REQUIRE(before != nullptr);
		CHECK(before->get_datatype().builtin_type == Variant::INT);
		BSCache::remove_parser(path);
		BS_TEST_REQUIRE(!provider(fixture, "witnesses", head + text + integer).is_empty());
		Error error = OK;
		auto fresh = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(fresh.is_valid());
		no_errors(*fresh->get_parser());
		BS_TEST_REQUIRE(error == OK);
		auto *after = ProviderTestAccess::witness(analyzer, type, "marker");
		BS_TEST_REQUIRE(after != nullptr);
		CHECK(bool(after == before));
		CHECK(after->get_datatype().builtin_type == Variant::INT);
		BSCache::remove_parser(path);
		BS_TEST_REQUIRE(!provider(fixture, "witnesses", "class_name Witnesses\ntrait OtherWitness:\n\tabstract func marker() -> int\nextend int uses OtherWitness:\n\tfunc marker() -> int:\n\t\treturn 2\n").is_empty());
		fresh = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && fresh.is_valid());
		CHECK(bool(ProviderTestAccess::witness(analyzer, type, "marker") == nullptr));
		no_errors(consumer);
	}

	TEST_CASE("named_enum_payload_and_tuple_construction_follow_provider_generation") {
		StorageFixture fixture;
		for (const bool tuple : { false, true }) {
			const String old_source = tuple ? "tuple_name Packet(value: int, count: int)\n" : "enum_name Packet:\n\tEmpty\n\tValue(value: int)\n";
			const String new_source = tuple ? "tuple_name Packet(value: String, count: int)\n" : "enum_name Packet:\n\tEmpty\n\tValue(value: String)\n";
			const String path = provider(fixture, "packet", old_source);
			BS_TEST_REQUIRE(!path.is_empty());
			const String source = tuple ? "var packet := Packet(1, 2)\n" : "var packet := Packet.Value(1)\n";
			BSParser old_consumer;
			BS_TEST_REQUIRE(old_consumer.parse(source, fixture.path("old.barista"), false) == OK);
			BSAnalyzer old_analyzer(&old_consumer);
			const Error old_result = old_analyzer.analyze();
			no_errors(old_consumer);
			BS_TEST_REQUIRE(old_result == OK);
			const auto old_type = old_consumer.get_tree()->get_member("packet").get_datatype();
			if (tuple) {
				BS_TEST_REQUIRE(old_type.container_element_types.size() == 2);
				CHECK(old_type.container_element_types[0].builtin_type == Variant::INT);
			} else {
				CHECK(old_type.is_tagged_union);
				BS_TEST_REQUIRE(old_type.get_enum_case_payload("Value") != nullptr);
				BS_TEST_REQUIRE(old_type.get_enum_case_payload("Value")->field_types.size() == 1);
				CHECK(old_type.get_enum_case_payload("Value")->field_types[0].builtin_type == Variant::INT);
			}
			BSCache::remove_parser(path);
			BS_TEST_REQUIRE(!provider(fixture, "packet", new_source).is_empty());
			BSParser new_consumer;
			BS_TEST_REQUIRE(new_consumer.parse(source, fixture.path("new.barista"), false) == OK);
			BSAnalyzer new_analyzer(&new_consumer);
			CHECK(new_analyzer.analyze() != OK);
			// Constructor argument anchors are LiteralNode: Packet(1) column22; Packet.Value(1) column28.
			diagnostic(new_consumer, tuple ? "Invalid argument 1 for tuple \"Packet\": should be \"String\" but is \"int\"." : "Invalid argument 1 for enum case \"Packet.Value\": should be \"String\" but is \"int\".", 1, tuple ? 22 : 28, 1, tuple ? 23 : 29);
			BSParser repaired_consumer;
			BS_TEST_REQUIRE(repaired_consumer.parse(tuple ? "var packet := Packet(\"new\", 2)\n" : "var packet := Packet.Value(\"new\")\n", fixture.path("repaired.barista"), false) == OK);
			BSAnalyzer repaired_analyzer(&repaired_consumer);
			CHECK(repaired_analyzer.analyze() == OK);
			no_errors(repaired_consumer);
			BSCache::remove_script(path);
			fixture.index().remove_path(path, fixture.index().claim_refresh(path));
		}
	}
	TEST_CASE("retained_foreign_trait_signature_uses_declaring_generation") {
		StorageFixture fixture;
		const String path = provider(fixture, "traits", "trait_name Measure\nfunc value() -> int:\n\treturn 1\n");
		BS_TEST_REQUIRE(!path.is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("uses Measure\nvar site: int\n", fixture.path("consumer.barista"), false) == OK);
		BSAnalyzer analyzer(&consumer);
		BS_TEST_REQUIRE(analyzer.resolve_inheritance() == OK);
		auto old = consumer.get_depended_parser_for(path);
		BS_TEST_REQUIRE(old.is_valid());
		BS_TEST_REQUIRE(old->raise_status(BSParserRef::INHERITANCE_SOLVED) == OK);
		auto *old_class = old->get_parser()->get_tree();
		BSCache::remove_parser(path);
		BS_TEST_REQUIRE(!provider(fixture, "traits", "trait_name Measure\nfunc value() -> String:\n\treturn \"new\"\n").is_empty());
		Error error = OK;
		auto fresh = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && fresh.is_valid());
		ProviderTestAccess::member(analyzer, old_class, "value", consumer.get_tree()->get_member("site").get_source_node());
		no_errors(consumer);
		CHECK(old_class->get_member("value").get_datatype().builtin_type == Variant::INT);
		CHECK(fresh->get_parser()->get_tree()->get_member("value").get_datatype().builtin_type == Variant::STRING);
	}
	TEST_CASE("retained_failed_member_replays_per_dependent_without_poisoning_good_member") {
		StorageFixture fixture;
		const String path = provider(fixture, "broken", "class_name Broken\nconst bad = cycle\nconst cycle = bad\nvar good: int\n");
		BS_TEST_REQUIRE(!path.is_empty());
		BSParser first, second;
		BS_TEST_REQUIRE(first.parse("var site: int\nvar again: int\n", fixture.path("first.barista"), false) == OK);
		BS_TEST_REQUIRE(second.parse("var site: int\nvar again: int\n", fixture.path("second.barista"), false) == OK);
		BSAnalyzer first_analyzer(&first), second_analyzer(&second);
		auto old = first.get_depended_parser_for(path);
		BS_TEST_REQUIRE(old.is_valid());
		BS_TEST_REQUIRE(second.get_depended_parser_for(path) == old);
		CHECK(old->raise_status(BSParserRef::INTERFACE_SOLVED) != OK);
		CHECK(old->get_status() == BSParserRef::INTERFACE_SOLVED);
		BSCache::remove_parser(path);
		BS_TEST_REQUIRE(!provider(fixture, "broken", "class_name Broken\nconst bad = 1\nvar good: String\n").is_empty());
		Error error = OK;
		auto fresh = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && fresh.is_valid());
		for (auto *analyzer : { &first_analyzer, &second_analyzer }) {
			auto &parser = analyzer == &first_analyzer ? first : second;
			const auto *site = parser.get_tree()->get_member("site").get_source_node();
			ProviderTestAccess::member(*analyzer, old->get_parser()->get_tree(), "good", site);
			no_errors(parser);
			CHECK(old->get_parser()->get_tree()->get_member("good").get_datatype().builtin_type == Variant::INT);
			ProviderTestAccess::member(*analyzer, old->get_parser()->get_tree(), "bad", site);
			ProviderTestAccess::member(*analyzer, old->get_parser()->get_tree(), "bad", parser.get_tree()->get_member("again").get_source_node());
			diagnostic(parser, "Could not resolve external class member \"bad\".", 1, 1, 1, 14);
		}
	}

	TEST_CASE("retained_interface_and_body_failures_replay_original_owner_per_consumer") {
		StorageFixture fixture;
		for (const bool body : { false, true }) {
			const String source = body ? "class_name Broken\nfunc value() -> int:\n\treturn \"bad\"\n" : "class_name Broken\nvar value: Missing\n";
			const String path = provider(fixture, "broken", source);
			BS_TEST_REQUIRE(!path.is_empty());
			BSParser first, second;
			BS_TEST_REQUIRE(first.parse("var site: int\nvar again: int\n", fixture.path("first.barista"), false) == OK);
			BS_TEST_REQUIRE(second.parse("var site: int\nvar again: int\n", fixture.path("second.barista"), false) == OK);
			BSAnalyzer first_analyzer(&first), second_analyzer(&second);
			auto old = first.get_depended_parser_for(path);
			BS_TEST_REQUIRE(old.is_valid());
			BS_TEST_REQUIRE(second.get_depended_parser_for(path) == old);
			const auto phase = body ? BSParserRef::FULLY_SOLVED : BSParserRef::INTERFACE_SOLVED;
			CHECK(old->raise_status(phase) != OK);
			BS_TEST_REQUIRE(old->get_status() == phase);
			BSCache::remove_parser(path);
			BS_TEST_REQUIRE(!provider(fixture, "broken", "class_name Broken\nvar value: int\n").is_empty());
			Error error = OK;
			auto fresh = BSCache::get_parser(path, BSParserRef::FULLY_SOLVED, error);
			BS_TEST_REQUIRE(error == OK && fresh.is_valid());
			for (auto *analyzer : { &first_analyzer, &second_analyzer }) {
				auto &parser = analyzer == &first_analyzer ? first : second;
				for (const StringName &name : { StringName("site"), StringName("again") }) {
					const auto *site = parser.get_tree()->get_member(name).get_source_node();
					if (body) {
						ProviderTestAccess::body(*analyzer, old->get_parser()->get_tree(), site);
					} else {
						ProviderTestAccess::interface(*analyzer, old->get_parser()->get_tree(), site);
					}
				}
				diagnostic(parser, body ? "Could not resolve class \"Broken\". The class is declared in \"res://tests/x2/broken.barista\", which has errors, the first at line 3: Cannot return value of type \"String\" because the function return type is \"int\"." : "Could not resolve class \"Broken\".", 1, 1, 1, 14);
			}
			BSCache::remove_script(path);
		}
	}

	TEST_CASE("member_and_body_failure_replay_matrix_recovers_after_explicit_refresh") {
		for (int fault : { 0, 1, 2, 3 }) {
			StorageFixture fixture;
			const String bad = fault == 0 ? "class_name Replay\nvar value: Missing\n" : fault == 1 ? "class_name Replay\nconst value = cycle\nconst cycle = value\n"
					: fault == 2																   ? "class_name Replay\nfunc value() -> int:\n\treturn \"bad\"\n"
																								   : "class_name Replay\nuses MissingTrait\nvar value: int\n";
			const String good = fault == 0 ? "class_name Replay\nvar value: int\n" : fault == 1 ? "class_name Replay\nconst value = 1\n"
					: fault == 2																? "class_name Replay\nfunc value() -> int:\n\treturn 1\n"
																								: "class_name Replay\nvar value: int\n";
			const String path = provider(fixture, "replay", bad);
			BS_TEST_REQUIRE(!path.is_empty());
			for (bool repaired : { false, true }) {
				if (repaired) {
					BSCache::set_source_override(path, good);
					BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, good) == OK);
				}
				for (int dependent : { 0, 1 }) {
					BSParser consumer;
					BS_TEST_REQUIRE(consumer.parse("var site: int\nvar again: int\n", fixture.path(dependent == 0 ? "first.barista" : "second.barista"), false) == OK);
					BSAnalyzer analyzer(&consumer);
					auto owner = consumer.get_depended_parser_for(path);
					BS_TEST_REQUIRE(owner.is_valid() && owner->raise_status(BSParserRef::INHERITANCE_SOLVED) == OK);
					for (const StringName &name : { StringName("site"), StringName("again") }) {
						const auto *site = consumer.get_tree()->get_member(name).get_source_node();
						if (fault == 2)
							ProviderTestAccess::body(analyzer, owner->get_parser()->get_tree(), site);
						else if (fault == 3)
							ProviderTestAccess::interface(analyzer, owner->get_parser()->get_tree(), site);
						else
							ProviderTestAccess::member(analyzer, owner->get_parser()->get_tree(), "value", site);
					}
					if (repaired) {
						CHECK(analyzer.analyze() == OK);
						no_errors(consumer);
					} else {
						CHECK(analyzer.analyze() != OK);
						diagnostic(consumer, fault == 2 ? "Could not resolve class \"Replay\". The class is declared in \"res://tests/x2/replay.barista\", which has errors, the first at line 3: Cannot return value of type \"String\" because the function return type is \"int\"." : fault == 3 ? "Could not resolve class \"Replay\"."
																																																																									: "Could not resolve external class member \"value\".",
								1, 1, 1, 14);
						CHECK(owner->get_parser()->analyzed_source == bad);
					}
				}
			}
		}
	}

	TEST_CASE("global_enum_identity_is_standalone_and_phase_independent") {
		StorageFixture fixture;
		for (const bool namespaced : { false, true }) {
			const String global = namespaced ? "repair_x2.Head" : "Head";
			BSParser::DataType previous_type;
			Ref<BSParserRef> previous_owner;
			const String declaration = (namespaced ? String("namespace repair_x2\n") : String()) + String("enum_name Head:\n\tZERO = 0\n");
			for (const bool warm : { false, true }) {
				for (const bool qualified : { false, true }) {
					BSCache::clear();
					const String path = provider(fixture, "head", declaration);
					BS_TEST_REQUIRE(!path.is_empty());
					Error error = OK;
					if (warm) {
						auto owner = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
						BS_TEST_REQUIRE(error == OK && owner.is_valid());
					}
					const String name = qualified ? global : String("Head");
					const String source = (namespaced ? String("import repair_x2\n") : String()) + String("var value: ") + name + String(" = Head.ZERO\n");
					BSParser consumer;
					BS_TEST_REQUIRE(consumer.parse(source, fixture.path("consumer.barista"), false) == OK);
					BSAnalyzer analyzer(&consumer);
					CHECK(analyzer.analyze() == OK);
					no_errors(consumer);
					const auto type = consumer.get_tree()->get_member("value").get_datatype();
					CHECK(type.enum_type == StringName(global));
					CHECK(type.native_type == StringName(global));
					BS_TEST_REQUIRE(consumer.get_depended_parsers().has(path));
					const auto owner = consumer.get_depended_parsers()[path];
					BS_TEST_REQUIRE(owner.is_valid());
					CHECK(owner->raise_status(BSParserRef::INTERFACE_SOLVED) == OK);
					const auto owner_type = owner->get_parser()->get_tree()->enum_file_decl->get_datatype();
					CHECK(owner_type.enum_type == StringName(global));
					CHECK(owner_type.native_type == StringName(global));
					CHECK(type.native_type == owner_type.native_type);
					if (previous_owner.is_valid()) {
						CHECK(ProviderTestAccess::same_type(analyzer, previous_type, type));
					}
					previous_type = type;
					previous_owner = owner;
				}
			}
		}
	}
	TEST_CASE("indexed_nullable_heads_preserve_marker_and_strict_assignment_contract") {
		StorageFixture fixture;
		for (int kind = 0; kind < 3; ++kind) {
			const String declaration = kind == 0 ? "class_name Head\n" : kind == 1 ? "tuple_name Head(value: int, count: int)\n"
																				   : "enum_name Head:\n\tZERO = 0\n";
			for (const bool warm : { false, true }) {
				for (const bool qualified : { false, true }) {
					BSCache::clear();
					const String path = provider(fixture, "head", String("namespace repair_x2\n") + declaration);
					BS_TEST_REQUIRE(!path.is_empty());
					Error error = OK;
					if (warm) {
						auto owner = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
						BS_TEST_REQUIRE(error == OK && owner.is_valid());
					}
					const String name = qualified ? "repair_x2.Head" : "Head";
					for (const bool nullable_destination : { true, false }) {
						if (!warm) {
							BSCache::remove_parser(path);
						}
						const String source = String("import repair_x2\nvar value: ") + name + String("?\nvar target: ") + name + (nullable_destination ? String("?") : String()) + String(" = value\n");
						BSParser consumer;
						BS_TEST_REQUIRE(consumer.parse(source, fixture.path("consumer.barista"), false) == OK);
						BSAnalyzer analyzer(&consumer);
						ProviderTestAccess::strict_null(analyzer);
						const Error result = analyzer.analyze();
						CHECK(consumer.get_tree()->get_member("value").get_datatype().is_nullable);
						CHECK(consumer.get_tree()->get_member("target").get_datatype().is_nullable == nullable_destination);
						if (nullable_destination) {
							CHECK(result == OK);
							no_errors(consumer);
						} else {
							CHECK(result != OK);
							const String type_name = kind == 0 ? "Head" : "repair_x2.Head";
							const String message = vformat(R"(Cannot assign nullable value of type "%s?" to variable "target"; expected non-nullable "%s".)", type_name, type_name);
							// Identifier `value` follows the literal prefix `var target: <name> = `.
							diagnostic(consumer, message, 3, name.length() + 16, 3, name.length() + 21);
						}
					}
				}
			}
		}
	}
	TEST_CASE("same_file_enum_and_tuple_head_annotations_preserve_nullable_marker") {
		StorageFixture fixture;
		for (const bool tuple : { false, true }) {
			const String source = tuple ? "tuple_name Head(value: int, count: int)\n" : "enum_name Head:\n	Empty\n	Next(value: Head?)\n";
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, fixture.path("head.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			no_errors(parser);
			// The internal TypeNode consumer is also used for same-file head queries.
			// Tuple files admit no host members; use its resolved head without inventing syntax.
			const auto nullable = ProviderTestAccess::head_type(analyzer, "Head", true);
			CHECK(nullable.is_nullable);
			CHECK_FALSE(ProviderTestAccess::head_type(analyzer, "Head", false).is_nullable);
			if (!tuple) {
				const auto type = parser.get_tree()->enum_file_decl->get_datatype();
				BS_TEST_REQUIRE(type.get_enum_case_payload("Next") != nullptr);
				BS_TEST_REQUIRE(type.get_enum_case_payload("Next")->field_types.size() == 1);
				CHECK(type.get_enum_case_payload("Next")->field_types[0].is_nullable);
			}
		}
	}
}
