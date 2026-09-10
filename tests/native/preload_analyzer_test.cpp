/**************************************************************************/
/*  preload_analyzer_test.cpp                                             */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_global_class.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <string>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
String provider(StorageFixture &fixture, const String &name, const String &source) {
	const String path = String("res://tests/x3/") + name + ".barista";
	BSParser syntax;
	const Error parsed = syntax.parse(source, path, false);
	for (const auto &error : syntax.get_errors()) {
		INFO(std::string(error.message.utf8().get_data()));
		CHECK(parsed == OK);
	}
	if (parsed != OK) {
		return String();
	}
	BSCache::set_source_override(path, source);
	auto record = BSDeclarationIndex::record_from_global_class(path, source, bs_resolve_global_class_from_source(source, path));
	record.declares_retroactive_conformances = !syntax.get_tree()->conformances.is_empty();
	const bool committed = fixture.index().commit_record(fixture.index().claim_refresh(path), record);
	CHECK(committed);
	return committed ? path : String();
}
void no_errors(const BSParser &parser) {
	for (const auto &error : parser.get_errors()) {
		INFO(std::string(error.message.utf8().get_data()));
		INFO(error.line);
		INFO(error.column);
		CHECK(false);
	}
}
void diagnostic(const BSParser &parser, const String &message, const BSParser::Node *site) {
	for (const auto &error : parser.get_errors()) {
		INFO(std::string(error.message.utf8().get_data()));
		CHECK(parser.get_errors().size() == 1);
	}
	BS_TEST_REQUIRE(parser.get_errors().size() == 1);
	BS_TEST_REQUIRE(site != nullptr);
	const auto &error = parser.get_errors().front()->get();
	INFO(std::string(error.message.utf8().get_data()));
	CHECK(error.message == message);
	CHECK(error.line == site->start_line);
	CHECK(error.column == site->start_column);
	CHECK(error.end_line == site->end_line);
	CHECK(error.end_column == site->end_column);
}
} //namespace
TEST_SUITE("preload_analyzer") {
	// Foundry c9d5e35 analyzer/features/preload_constant_types_are_inferred.fs
	// and fs_to_preload.notest.fs: same constant producer; static analysis only.
	TEST_CASE("relative_preload_retains_static_provider_and_infers_constant") {
		StorageFixture fixture;
		const String path = provider(fixture, "constants", "const A := 42\nfunc something():\n\treturn \"OK\"\n");
		BS_TEST_REQUIRE(!path.is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("const Constants = preload(\"./constants.barista\")\nvar a := Constants.A\n", "res://tests/x3/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() == OK);
		no_errors(consumer);
		auto *load = static_cast<BSParser::PreloadNode *>(consumer.get_tree()->get_member("Constants").constant->initializer);
		CHECK(load->is_constant);
		CHECK(load->resource.is_null());
		CHECK(load->resolved_path == path);
		CHECK(load->get_datatype().is_meta_type);
		CHECK(load->get_datatype().kind == BSParser::DataType::CLASS);
		CHECK(consumer.get_tree()->get_member("a").get_datatype().builtin_type == Variant::INT);
		BS_TEST_REQUIRE(consumer.get_depended_parsers().has(path));
		CHECK(load->get_datatype().class_type == consumer.get_depended_parsers()[path]->get_parser()->get_tree());
		CHECK(consumer.get_depended_parsers()[path]->get_status() == BSParserRef::INHERITANCE_SOLVED);
	}
	TEST_CASE("missing_relative_preload_reports_pinned_path_expression") {
		StorageFixture fixture;
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("extends RefCounted\n\nconst Missing = preload(\"./preload_missing_relative_target.notest.barista\")\n\nfunc test():\n\tprint(Missing)\n", "res://analyzer/errors/preload_missing_relative_path.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() != OK);
		auto *load = static_cast<BSParser::PreloadNode *>(consumer.get_tree()->get_member("Missing").constant->initializer);
		CHECK(load->path->start_line == 3);
		CHECK(load->path->start_column == 25);
		diagnostic(consumer, "Preload file \"res://analyzer/errors/preload_missing_relative_target.notest.barista\" does not exist.", load->path);
	}
	TEST_CASE("preload_path_requires_constant_string") {
		StorageFixture fixture;
		for (const String &source : { String("var path = \"provider.barista\"\nconst P = preload(path)\n"), String("const P = preload(42)\n") }) {
			BSParser consumer;
			BS_TEST_REQUIRE(consumer.parse(source, "res://tests/x3/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&consumer);
			CHECK(analyzer.analyze() != OK);
			auto *load = static_cast<BSParser::PreloadNode *>(consumer.get_tree()->get_member("P").constant->initializer);
			diagnostic(consumer, "Preloaded path must be a constant string.", load->path);
		}
	}
	TEST_CASE("unused_preload_consumes_inheritance_phase_even_after_body_failure") {
		StorageFixture fixture;
		const String path = provider(fixture, "body_error", "func broken() -> int:\n\treturn \"bad\"\n");
		BS_TEST_REQUIRE(!path.is_empty());
		for (bool warm : { false, true }) {
			if (warm) {
				Error error = OK;
				auto ref = BSCache::get_parser(path, BSParserRef::FULLY_SOLVED, error);
				BS_TEST_REQUIRE(ref.is_valid());
				BS_TEST_REQUIRE(error != OK);
				CHECK(ref->get_status() == BSParserRef::FULLY_SOLVED);
			}
			BSParser consumer;
			BS_TEST_REQUIRE(consumer.parse("const P = preload(\"body_error.barista\")\n", "res://tests/x3/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&consumer);
			CHECK(analyzer.analyze() == OK);
			no_errors(consumer);
			BS_TEST_REQUIRE(consumer.get_depended_parsers().has(path));
			CHECK(consumer.get_depended_parsers()[path]->get_status() == (warm ? BSParserRef::FULLY_SOLVED : BSParserRef::INHERITANCE_SOLVED));
		}
	}
	TEST_CASE("cold_hidden_builtin_witness_matches_warm_without_visibility_edge") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = provider(fixture, "witnesses", "namespace x3_hidden\nclass_name Witnesses\ntrait Marker:\n\tabstract func marker() -> int\nextend int uses Marker:\n\tfunc marker() -> int:\n\t\treturn 1\n");
		BS_TEST_REQUIRE(!path.is_empty());
		BS_TEST_REQUIRE(fixture.index().get_conformance_files_in_namespace("x3_hidden").size() == 1);
		for (bool warm : { false, true }) {
			if (warm) {
				Error error = OK;
				auto ref = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
				BS_TEST_REQUIRE(ref.is_valid());
				no_errors(*ref->get_parser());
				BS_TEST_REQUIRE(error == OK);
			}
			BSParser consumer;
			BS_TEST_REQUIRE(consumer.parse("var value: int\nvar result = value.marker()\n", "res://tests/x3/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&consumer);
			CHECK(analyzer.analyze() != OK);
			auto *call = static_cast<BSParser::CallNode *>(consumer.get_tree()->get_member("result").variable->initializer);
			diagnostic(consumer, "Cannot call \"marker()\" on \"int\": it is supplied by the retroactive conformance to trait \"x3_hidden.Witnesses::Marker\" declared in \"res://tests/x3/witnesses.barista\", which this file does not load. Import that file's namespace, or preload it.", call->callee);
			CHECK(consumer.get_depended_parsers().is_empty());
			CHECK(consumer.get_dependencies().is_empty());
			CHECK(fixture.index().get_record_count() == 1);
		}
	}
	TEST_CASE("preload_replays_parse_and_inheritance_failure_at_each_path") {
		StorageFixture fixture;
		for (const String &bad : { String("func broken(\n"), String("extends MissingParent\n") }) {
			const String path = "res://tests/x3/bad.barista";
			BSCache::remove_parser(path);
			BSCache::set_source_override(path, bad);
			for (int dependent = 0; dependent < 2; ++dependent) {
				BSParser consumer;
				BS_TEST_REQUIRE(consumer.parse("const A = preload(\"bad.barista\")\nconst B = preload(\"bad.barista\")\n", "res://tests/x3/consumer.barista", false) == OK);
				BSAnalyzer analyzer(&consumer);
				CHECK(analyzer.analyze() != OK);
				BS_TEST_REQUIRE(consumer.get_errors().size() == 2);
				int line = 1;
				for (const auto &error : consumer.get_errors()) {
					CHECK(error.message == "Could not preload resource script \"res://tests/x3/bad.barista\".");
					CHECK(error.line == line);
					CHECK(error.column == 19);
					CHECK(error.end_line == line++);
					CHECK(error.end_column == 32);
				}
			}
		}
	}
	TEST_CASE("preload_bootstrap_and_uid_use_canonical_path") {
		StorageFixture fixture;
		const String path = provider(fixture, "allowed/provider", "const A = 1\n");
		BS_TEST_REQUIRE(!path.is_empty());
		BSAnalyzer::set_bootstrap_allowed_dependency_root("res://tests/x3/allowed");
		struct UIDScope {
			int64_t id = ResourceUID::get_singleton()->create_id();
			~UIDScope() { ResourceUID::get_singleton()->remove_id(id); }
		} uid;
		ResourceUID::get_singleton()->add_id(uid.id, path);
		const String uid_path = ResourceUID::get_singleton()->id_to_text(uid.id);
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("const P = preload(\"" + uid_path + "\")\nvar n := P.A\n", "res://tests/x3/allowed/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() == OK);
		no_errors(consumer);
		CHECK(consumer.get_depended_parsers().has(path));
		CHECK(consumer.get_tree()->get_member("n").get_datatype().builtin_type == Variant::INT);
		BSParser rejected;
		BS_TEST_REQUIRE(rejected.parse("const P = preload(\"../outside.barista\")\n", "res://tests/x3/allowed/consumer.barista", false) == OK);
		BSAnalyzer rejected_analyzer(&rejected);
		CHECK(rejected_analyzer.analyze() != OK);
		auto *load = static_cast<BSParser::PreloadNode *>(rejected.get_tree()->get_member("P").constant->initializer);
		diagnostic(rejected, "Build task bootstrap cannot preload script \"res://tests/x3/outside.barista\"; it is outside the provider bootstrap root \"res://tests/x3/allowed\".", load->path);
		CHECK(rejected.get_depended_parsers().is_empty());
	}
	TEST_CASE("ordinary_resource_and_unrecognized_file_are_distinct_from_script") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(write_bytes(fixture.path("plain.tres"), bytes("[gd_resource type=\"Resource\" format=3]\n\n[resource]\nresource_name = \"plain\"\n")));
		BS_TEST_REQUIRE(write_bytes(fixture.path("plain.unknown"), bytes("not a resource\n")));
		for (bool recognized : { true, false }) {
			BSParser consumer;
			const String file = recognized ? "plain.tres" : "plain.unknown";
			BS_TEST_REQUIRE(consumer.parse("const P = preload(\"" + file + "\")\n", fixture.path("consumer.barista"), false) == OK);
			BSAnalyzer analyzer(&consumer);
			const Error error = analyzer.analyze();
			auto *load = static_cast<BSParser::PreloadNode *>(consumer.get_tree()->get_member("P").constant->initializer);
			if (recognized) {
				CHECK(error == OK);
				no_errors(consumer);
				CHECK(load->resource.is_valid());
				CHECK(load->get_datatype().kind == BSParser::DataType::NATIVE);
				CHECK(load->get_datatype().native_type == StringName("Resource"));
				CHECK_FALSE(load->get_datatype().is_meta_type);
			} else {
				CHECK(error != OK);
				diagnostic(consumer, "Preload file \"" + fixture.path(file) + "\" has no resource loaders (unrecognized file extension).", load->path);
			}
			CHECK(consumer.get_depended_parsers().is_empty());
		}
	}
	TEST_CASE("static_handle_exposes_members_and_nested_types_without_private_alias") {
		StorageFixture fixture;
		const String path = provider(fixture, "surface", "class_name Surface\nconst COUNT = 7\nstatic func text() -> String:\n\treturn \"ok\"\nclass Nested:\n\tvar count: int\nenum State:\n\tREADY = 0\ntuple Pair(count: int, text: String)\ntype Hidden = int\n");
		BS_TEST_REQUIRE(!path.is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("const P = preload(\"surface.barista\")\nvar n := P.COUNT\nvar s := P.text()\nvar nested: P.Nested\nvar count := nested.count\nvar state := P.State.READY\nvar pair: P.Pair\nvar field := pair.text\n", "res://tests/x3/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() == OK);
		no_errors(consumer);
		CHECK(consumer.get_tree()->get_member("n").get_datatype().builtin_type == Variant::INT);
		CHECK(consumer.get_tree()->get_member("s").get_datatype().builtin_type == Variant::STRING);
		CHECK(consumer.get_tree()->get_member("count").get_datatype().builtin_type == Variant::INT);
		CHECK(consumer.get_tree()->get_member("field").get_datatype().builtin_type == Variant::STRING);
		BSParser rejected;
		BS_TEST_REQUIRE(rejected.parse("const P = preload(\"surface.barista\")\nvar hidden: P.Hidden\n", "res://tests/x3/consumer.barista", false) == OK);
		BSAnalyzer rejected_analyzer(&rejected);
		CHECK(rejected_analyzer.analyze() != OK);
		diagnostic(rejected, "Could not find nested type \"Hidden\" under base \"Surface\".", rejected.get_tree()->get_member("hidden").variable->datatype_specifier->type_chain[1]);
	}
	TEST_CASE("file_handle_enum_cases_use_declared_identity") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(!provider(fixture, "enum", "namespace x3_heads\nenum_name Packet:\n\tEmpty\n\tValue(value: int)\n").is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("const E = preload(\"enum.barista\")\nvar empty := E.Empty\nvar payload := E.Packet.Value(1)\n", "res://tests/x3/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() == OK);
		no_errors(consumer);
		CHECK(consumer.get_tree()->get_member("empty").get_datatype().kind == BSParser::DataType::ENUM);
		CHECK(consumer.get_tree()->get_member("payload").get_datatype().native_type == StringName("x3_heads.Packet"));
	}
	TEST_CASE("later_preload_licenses_whole_file_witness_without_namespace_import") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = provider(fixture, "witnesses", "namespace x3_hidden\nclass_name Witnesses\ntrait Marker:\n\tabstract func marker() -> int\nextend int uses Marker:\n\tfunc marker() -> int:\n\t\treturn 1\n");
		BS_TEST_REQUIRE(!path.is_empty());
		for (int repeat = 0; repeat < 2; ++repeat) {
			BSParser consumer;
			BS_TEST_REQUIRE(consumer.parse("var value: int\nvar result: int = value.marker()\nconst W = preload(\"witnesses.barista\")\n", "res://tests/x3/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&consumer);
			CHECK(analyzer.analyze() == OK);
			no_errors(consumer);
			BS_TEST_REQUIRE(consumer.get_depended_parsers().has(path));
			CHECK(consumer.get_depended_parsers()[path]->get_status() >= BSParserRef::INTERFACE_SOLVED);
			CHECK(consumer.get_tree()->imports.is_empty());
			CHECK(fixture.index().get_record_count() == 1);
		}
	}
	TEST_CASE("legal_recursive_preloads_retain_and_release_the_static_graph") {
		StorageFixture fixture;
		for (int repeat = 0; repeat < 3; ++repeat) {
			// Adapted pinned preload_cyclic_reference_a/b.notest.fs. Calls are only
			// analyzed; no script object or runtime method is constructed/executed.
			const String a = provider(fixture, "cycle_a", "const B = preload(\"cycle_b.barista\")\n\nconst WAITING_FOR = \"godot\"\n\nstatic func test_cyclic_reference():\n\tB.test_cyclic_reference()\n\nstatic func test_cyclic_reference_2():\n\tB.test_cyclic_reference_2()\n\nstatic func test_cyclic_reference_3():\n\tB.test_cyclic_reference_3()\n");
			const String b = provider(fixture, "cycle_b", "const A = preload(\"cycle_a.barista\")\n\nstatic func test_cyclic_reference():\n\tA.test_cyclic_reference_2()\n\nstatic func test_cyclic_reference_2():\n\tA.test_cyclic_reference_3()\n\nstatic func test_cyclic_reference_3():\n\tprint(A.WAITING_FOR)\n");
			BS_TEST_REQUIRE(!a.is_empty() && !b.is_empty());
			Error error = OK;
			auto ref = BSCache::get_parser(a, BSParserRef::FULLY_SOLVED, error);
			BS_TEST_REQUIRE(ref.is_valid());
			no_errors(*ref->get_parser());
			BS_TEST_REQUIRE(error == OK);
			BS_TEST_REQUIRE(ref->get_parser()->get_depended_parsers().has(b));
			auto other = ref->get_parser()->get_depended_parsers()[b];
			CHECK(other->raise_status(BSParserRef::FULLY_SOLVED) == OK);
			no_errors(*other->get_parser());
			CHECK(other->get_parser()->get_depended_parsers()[a] == ref);
			const uint64_t a_id = ref->get_instance_id(), b_id = other->get_instance_id();
			BSCache::clear();
			CHECK(ObjectDB::get_instance(a_id) != nullptr);
			CHECK(ObjectDB::get_instance(b_id) != nullptr);
			ref.unref();
			other.unref();
			BSCache::clear();
			CHECK(ObjectDB::get_instance(a_id) == nullptr);
			CHECK(ObjectDB::get_instance(b_id) == nullptr);
		}
	}
	TEST_CASE("preloaded_class_alias_can_supply_a_nested_inheritance_base") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(!provider(fixture, "base", "class_name Base\nvar count: int\n").is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("const P = preload(\"base.barista\")\nclass Child extends P:\n\tpass\nvar child: Child\nvar count := child.count\n", "res://tests/x3/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() == OK);
		no_errors(consumer);
		CHECK(consumer.get_tree()->get_member("count").get_datatype().builtin_type == Variant::INT);
	}
	TEST_CASE("diagnostic_probe_isolates_broken_providers_and_does_not_license_witnesses") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = provider(fixture, "broken_conformance", "namespace unrelated\nclass_name Broken\nextend int uses MissingTrait:\n\tfunc marker() -> int:\n\t\treturn 1\n");
		BS_TEST_REQUIRE(!path.is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("var value: int\nvar result = value.marker()\n", "res://tests/x3/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() == OK);
		no_errors(consumer);
		CHECK(analyzer.has_probed_indexed_conformances());
		CHECK(consumer.get_depended_parsers().is_empty());
		CHECK(consumer.get_dependencies().is_empty());
		CHECK(BSCache::collect_parser_invalidation_closure(path).size() == 1);
		CHECK(BSConformanceRegistry::get_singleton()->get_file_conformances(path).is_empty());
		CHECK(BSConformanceRegistry::get_singleton()->debug_get_loaded_files(String("res://tests/x3/consumer.barista")).is_empty());
		Error error = OK;
		auto failed = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(failed.is_valid());
		CHECK(error != OK);
		CHECK_FALSE(failed->get_parser()->get_errors().is_empty());
		CHECK(fixture.index().get_record_count() == 1);
	}
	TEST_CASE("index_addition_invalidates_cold_observer_without_load_edges") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String consumer_path = "res://tests/x3/consumer.barista";
		const String source = "var value: int\nvar result = value.marker()\n";
		BSCache::set_source_override(consumer_path, source);
		Error error = OK;
		auto old = BSCache::get_parser(consumer_path, BSParserRef::FULLY_SOLVED, error);
		BS_TEST_REQUIRE(old.is_valid() && error == OK);
		CHECK(old->get_analyzer()->has_probed_indexed_conformances());
		CHECK(old->get_parser()->get_depended_parsers().is_empty());
		CHECK(old->get_parser()->get_dependencies().is_empty());
		const String path = provider(fixture, "added", "class_name Added\ntrait Marker:\n\tabstract func marker() -> int\nextend int uses Marker:\n\tfunc marker() -> int:\n\t\treturn 1\n");
		BS_TEST_REQUIRE(!path.is_empty());
		BSDeclarationRecord record;
		BS_TEST_REQUIRE(fixture.index().try_get_by_path(path, record));
		// Exercise the language's existing commit notification, including the empty
		// namespace: this observer has no namespace or provider dependency to follow.
		auto *language = BaristaScriptLanguage::get_singleton();
		CHECK(language->commit_declaration_record(language->claim_declaration_refresh(path), record));
		CHECK_FALSE(BSCache::has_parser(consumer_path));
		auto fresh = BSCache::get_parser(consumer_path, BSParserRef::FULLY_SOLVED, error);
		BS_TEST_REQUIRE(fresh.is_valid());
		CHECK(error != OK);
		CHECK(fresh != old);
		CHECK(fresh->get_parser()->get_depended_parsers().is_empty());
		auto *call = static_cast<BSParser::CallNode *>(fresh->get_parser()->get_tree()->get_member("result").variable->initializer);
		diagnostic(*fresh->get_parser(), "Cannot call \"marker()\" on \"int\": it is supplied by the retroactive conformance to trait \"Added::Marker\" declared in \"res://tests/x3/added.barista\", which this file does not load. Import that file's namespace, or preload it.", call->callee);
		no_errors(*old->get_parser());
	}
	TEST_CASE("file_handle_payload_constructor_is_rejected_at_attribute") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(!provider(fixture, "enum", "enum_name Packet:\n\tEmpty\n\tValue(value: int)\n").is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("const E = preload(\"enum.barista\")\nvar payload = E.Value\n", "res://tests/x3/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() != OK);
		auto *subscript = static_cast<BSParser::SubscriptNode *>(consumer.get_tree()->get_member("payload").variable->initializer);
		diagnostic(consumer, "Enum case \"Packet.Value\" carries a payload and must be constructed through the enum name, e.g. \"Packet.Value(...)\".", subscript->attribute);
	}
	TEST_CASE("final_receiver_cold_hidden_diagnostic_preserves_open_receiver_mode") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		for (bool closed : { false, true }) {
			const String name = closed ? "Closed" : "Open";
			const String target = provider(fixture, closed ? "closed" : "open", (closed ? String("final ") : String()) + "class_name " + name + "\n");
			const String witness = provider(fixture, closed ? "closed_witness" : "open_witness", "namespace x3_other\nclass_name " + name + "Witness\ntrait Marker:\n\tabstract func marker() -> int\nextend " + name + " uses Marker:\n\tfunc marker() -> int:\n\t\treturn 1\n");
			BS_TEST_REQUIRE(!target.is_empty() && !witness.is_empty());
			BSParser consumer;
			BS_TEST_REQUIRE(consumer.parse("var value: " + name + "\nvar result = value.marker()\n", "res://tests/x3/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&consumer);
			const Error error = analyzer.analyze();
			if (closed) {
				CHECK(error != OK);
				auto *call = static_cast<BSParser::CallNode *>(consumer.get_tree()->get_member("result").variable->initializer);
				diagnostic(consumer, "Cannot call \"marker()\" on \"Closed\": it is supplied by the retroactive conformance to trait \"x3_other.ClosedWitness::Marker\" declared in \"res://tests/x3/closed_witness.barista\", which this file does not load. Import that file's namespace, or preload it.", call->callee);
			} else {
				CHECK(error == OK);
				no_errors(consumer);
				CHECK_FALSE(analyzer.has_probed_indexed_conformances());
				CHECK(consumer.get_tree()->get_member("result").get_datatype().is_variant());
			}
			CHECK_FALSE(consumer.get_depended_parsers().has(witness));
		}
	}
	TEST_CASE("broken_conformance_preload_replays_at_each_use_in_separate_consumers") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		BS_TEST_REQUIRE(!provider(fixture, "bad", "class_name Bad\nextend int uses MissingTrait:\n\tfunc marker() -> int:\n\t\treturn 1\n").is_empty());
		for (int dependent = 0; dependent < 2; ++dependent) {
			BSParser consumer;
			BS_TEST_REQUIRE(consumer.parse("const A = preload(\"bad.barista\")\nconst B = preload(\"bad.barista\")\n", "res://tests/x3/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&consumer);
			CHECK(analyzer.analyze() != OK);
			BS_TEST_REQUIRE(consumer.get_errors().size() == 2);
			int line = 1;
			for (const auto &error : consumer.get_errors()) {
				CHECK(error.message == "Could not preload resource script \"res://tests/x3/bad.barista\".");
				CHECK(error.line == line);
				CHECK(error.column == 19);
				CHECK(error.end_line == line++);
				CHECK(error.end_column == 32);
			}
		}
	}
	TEST_CASE("preload_of_cyclic_inheritance_remains_invalid") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(!provider(fixture, "inherit_a", "extends \"inherit_b.barista\"\n").is_empty());
		BS_TEST_REQUIRE(!provider(fixture, "inherit_b", "extends \"inherit_a.barista\"\n").is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("const P = preload(\"inherit_a.barista\")\n", "res://tests/x3/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() != OK);
		auto *load = static_cast<BSParser::PreloadNode *>(consumer.get_tree()->get_member("P").constant->initializer);
		diagnostic(consumer, "Could not preload resource script \"res://tests/x3/inherit_a.barista\".", load->path);
	}
	TEST_CASE("cold_probe_reentrancy_terminates_without_consumer_load_edges") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String a = provider(fixture, "reentrant_a", "namespace x3_a\nclass_name A\nvar value: String\nvar probe = value.other()\ntrait Marker:\n\tabstract func marker() -> int\nextend int uses Marker:\n\tfunc marker() -> int:\n\t\treturn 1\n");
		const String b = provider(fixture, "reentrant_b", "namespace x3_b\nclass_name B\nvar value: int\nvar probe = value.marker()\ntrait Other:\n\tabstract func other() -> int\nextend String uses Other:\n\tfunc other() -> int:\n\t\treturn 1\n");
		BS_TEST_REQUIRE(!a.is_empty() && !b.is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("var value: int\nvar result = value.marker()\n", "res://tests/x3/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() != OK);
		auto *call = static_cast<BSParser::CallNode *>(consumer.get_tree()->get_member("result").variable->initializer);
		diagnostic(consumer, "Cannot call \"marker()\" on \"int\": it is supplied by the retroactive conformance to trait \"x3_a.A::Marker\" declared in \"res://tests/x3/reentrant_a.barista\", which this file does not load. Import that file's namespace, or preload it.", call->callee);
		CHECK(consumer.get_depended_parsers().is_empty());
		for (const String &path : { a, b }) {
			Error error = OK;
			auto ref = BSCache::get_parser(path, BSParserRef::EMPTY, error);
			BS_TEST_REQUIRE(ref.is_valid());
			CHECK(ref->get_status() == BSParserRef::INTERFACE_SOLVED);
			CHECK(ref->get_analyzer()->has_probed_indexed_conformances());
		}
	}
	TEST_CASE("preload_handles_cannot_extend_enum_or_tuple_files") {
		StorageFixture fixture;
		for (bool tuple : { false, true }) {
			const String name = tuple ? "Pair" : "Packet";
			const String path = provider(fixture, tuple ? "tuple_only" : "enum_only", tuple ? "tuple_name Pair(value: int, count: int)\n" : "enum_name Packet:\n\tEmpty = 0\n");
			BS_TEST_REQUIRE(!path.is_empty());
			BSParser consumer;
			BS_TEST_REQUIRE(consumer.parse("const P = preload(\"" + path.get_file() + "\")\nclass Child extends P:\n\tpass\n", "res://tests/x3/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&consumer);
			CHECK(analyzer.analyze() != OK);
			diagnostic(consumer, "Cannot extend " + String(tuple ? "tuple_name" : "enum_name") + " file \"" + name + "\".", consumer.get_tree()->get_member("Child").m_class->extends[0]);
		}
	}
	TEST_CASE("file_handle_payload_call_requires_declared_enum_name") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(!provider(fixture, "enum", "enum_name Packet:\n\tEmpty\n\tValue(value: int)\n").is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("const E = preload(\"enum.barista\")\nvar payload = E.Value(1)\n", "res://tests/x3/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() != OK);
		auto *call = static_cast<BSParser::CallNode *>(consumer.get_tree()->get_member("payload").variable->initializer);
		diagnostic(consumer, "Enum case \"Packet.Value\" carries a payload and must be constructed through the enum name, e.g. \"Packet.Value(...)\".", static_cast<BSParser::SubscriptNode *>(call->callee)->attribute);
	}
	TEST_CASE("semantic_preload_handles_do_not_fold_through_absent_runtime_payload") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(!provider(fixture, "first", "class_name First\n").is_empty());
		BS_TEST_REQUIRE(!provider(fixture, "second", "class_name Second\n").is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("const P = preload(\"first.barista\")\nconst Q = preload(\"second.barista\")\nconst IsNull = P == null\nconst Same = P == P\nconst Different = P == Q\nconst Negated = not P\nconst Selected = 1 if P else 2\nconst Alias = P if true else Q\nconst AliasSame = Alias == P\nvar items = [P]\nvar mapping = {P: 1, Q: 2}\nvar pair = (P, 1)\nvar text = str(P)\n", "res://tests/x3/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() == OK);
		no_errors(consumer);
		for (const auto &entry : { std::pair<const char *, bool>("IsNull", false), { "Same", true }, { "Different", false }, { "Negated", false }, { "AliasSame", true } }) {
			auto *value = consumer.get_tree()->get_member(entry.first).constant->initializer;
			CHECK(value->is_constant);
			CHECK(value->reduced_value.get_type() == Variant::BOOL);
			CHECK(bool(value->reduced_value) == entry.second);
		}
		CHECK(int64_t(consumer.get_tree()->get_member("Selected").constant->initializer->reduced_value) == 1);
		CHECK(consumer.get_tree()->get_member("Alias").get_datatype().class_type == consumer.get_tree()->get_member("P").get_datatype().class_type);
		for (const StringName &name : { StringName("items"), StringName("mapping"), StringName("pair"), StringName("text") }) {
			auto *value = consumer.get_tree()->get_member(name).variable->initializer;
			CHECK_FALSE(BSAnalyzer::has_materialized_constant_value(value));
			if (name != StringName("text")) {
				CHECK(value->is_constant);
			}
		}
	}
	TEST_CASE("semantic_preload_containers_remain_constant_without_runtime_payload") {
		StorageFixture fixture;
		BS_TEST_REQUIRE(!provider(fixture, "constant", "class_name ConstantProvider\n").is_empty());
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("const P = preload(\"constant.barista\")\nconst A = [P]\nconst D = {\"provider\": P}\nconst T = (P, 1)\nconst Alias = A\nconst RealNull: Array? = null\n", "res://tests/x3/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() == OK);
		no_errors(consumer);
		for (const StringName &name : { StringName("A"), StringName("D"), StringName("T"), StringName("Alias") }) {
			auto *value = consumer.get_tree()->get_member(name).constant->initializer;
			CAPTURE(name);
			CHECK(value->is_constant);
			CHECK_FALSE(BSAnalyzer::has_materialized_constant_value(value));
		}
		auto *null_value = consumer.get_tree()->get_member("RealNull").constant->initializer;
		CHECK(null_value->is_constant);
		CHECK(BSAnalyzer::has_materialized_constant_value(null_value));
		CHECK(null_value->reduced_value.get_type() == Variant::NIL);
		CHECK(consumer.get_tree()->get_member("RealNull").get_datatype().is_nullable);
	}
}
