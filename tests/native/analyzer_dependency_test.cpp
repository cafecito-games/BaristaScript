/**************************************************************************/
/*  analyzer_dependency_test.cpp                                          */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                          */
/*  This file is part of BaristaScript, a Godot GDExtension.                */
/*  SPDX-License-Identifier: MIT                                           */
/**************************************************************************/

#include "analyzer_helpers.h"
#include "bs_conformance_registry.h"
#include "bs_script_server.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <algorithm>
#include <array>
#include <random>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
Ref<BSParserRef> parser_at(const String &path, BSParserRef::Status status, const String &owner = String()) {
	Error error = ERR_BUG;
	Ref<BSParserRef> parser = BSCache::get_parser(path, status, error, owner);
	CHECK(error == OK);
	CHECK(parser.is_valid());
	return parser;
}

void synchronize(const String &path, const String &source) {
	CHECK(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, source) == OK);
}

void scenario_transitive_invalidation() {
	StorageFixture fixture;
	const String a = "res://tests/dep_a.barista";
	const String b = "res://tests/dep_b.barista";
	const String c = "res://tests/dep_c.barista";
	const String unrelated = "res://tests/unrelated.barista";
	BSCache::set_source_override(a, "class_name DepA extends Node\n");
	BSCache::set_source_override(b, "class_name DepB extends Node\n");
	BSCache::set_source_override(c, "class_name DepC extends Node\n");
	BSCache::set_source_override(unrelated, "class_name Unrelated extends Node\n");
	parser_at(a, BSParserRef::PARSED);
	parser_at(b, BSParserRef::PARSED, a);
	parser_at(c, BSParserRef::PARSED, b);
	parser_at(unrelated, BSParserRef::PARSED);
	const auto closure = BSCache::collect_parser_invalidation_closure(c);
	CHECK(closure.has(c));
	CHECK(closure.has(b));
	CHECK(closure.has(a));
	CHECK_FALSE(closure.has(unrelated));
	BSCache::remove_parser(c);
	CHECK_FALSE(BSCache::has_parser(c));
	CHECK_FALSE(BSCache::has_parser(b));
	CHECK_FALSE(BSCache::has_parser(a));
	CHECK(BSCache::has_parser(unrelated));
}

void scenario_move_remove() {
	StorageFixture fixture;
	const String old_path = "res://tests/old.barista";
	const String new_path = "res://tests/new.barista";
	BSCache::set_source_override(old_path, "class_name OldName extends Node\n");
	parser_at(old_path, BSParserRef::PARSED);
	CHECK(BSCache::has_parser(old_path));
	BSCache::move_script(old_path, new_path);
	CHECK_FALSE(BSCache::has_parser(old_path));
	BSCache::set_source_override(new_path, "class_name NewName extends Node\n");
	CHECK(parser_at(new_path, BSParserRef::PARSED).is_valid());
	BSCache::remove_script(new_path);
	CHECK_FALSE(BSCache::has_parser(new_path));
}

void scenario_dependency_cycle() {
	StorageFixture fixture;
	const String a = "res://tests/cycle_a.barista";
	const String b = "res://tests/cycle_b.barista";
	BSCache::set_source_override(a, "class_name CycleA extends Node\n");
	BSCache::set_source_override(b, "class_name CycleB extends Node\n");
	CHECK(parser_at(a, BSParserRef::EMPTY, b).is_valid());
	CHECK(parser_at(b, BSParserRef::EMPTY, a).is_valid());
	CHECK(parser_at(a, BSParserRef::FULLY_SOLVED).is_valid());
	CHECK(parser_at(b, BSParserRef::FULLY_SOLVED).is_valid());
}

void scenario_finalization_raises_dependencies() {
	StorageFixture fixture;
	const String dependency = "res://tests/finalization_dependency.notest.barista";
	const String owner = "res://tests/finalization_dependency.barista";
	BSCache::set_source_override(dependency, "class_name FinalizationDependency extends Node\n");
	// Keep the producer's relative preload expression and dependency filenames.
	BSCache::set_source_override(owner, "func test() -> void:\n\tpreload(\"./finalization_dependency.notest.barista\")\n");
	const Ref<BSParserRef> owner_parser = parser_at(owner, BSParserRef::FULLY_SOLVED);
	const Ref<BSParserRef> dependency_parser = parser_at(dependency, BSParserRef::EMPTY);
	BS_TEST_REQUIRE(owner_parser.is_valid() && dependency_parser.is_valid());
	CHECK(owner_parser->get_status() == BSParserRef::FULLY_SOLVED);
	CHECK(dependency_parser->get_status() >= BSParserRef::INHERITANCE_SOLVED);
}

void scenario_host_bootstrap_filtering() {
	StorageFixture fixture;
	for (const String &path : { String("res://tests/cache_fixtures/script_a.barista"), String("res://outside/other.barista") }) {
		BSDeclarationRecord record;
		record.path = path;
		record.source_digest = path.begins_with("res://tests/") ? 1 : 2;
		record.namespace_name = "cachefix";
		record.declares_retroactive_conformances = true;
		CHECK(fixture.index().commit_record(fixture.index().claim_refresh(path), record));
	}
	BSAnalyzer::set_bootstrap_allowed_dependency_root("res://tests/");
	const BSParserHost *host = BSParserHost::get_singleton();
	BS_TEST_REQUIRE(host != nullptr);
	CHECK(host->is_bootstrap_path_allowed("res://tests/cache_fixtures/script_a.barista"));
	CHECK_FALSE(host->is_bootstrap_path_allowed("res://outside/other.barista"));
}

void scenario_analyzer_declaration_commit() {
	StorageFixture fixture;
	const String path = "res://tests/commit_ok.barista";
	const String source = "class_name CommitOk extends Node\n\nfunc _ready() -> void:\n\tpass\n";
	synchronize(path, source);
	BSDeclarationRecord record;
	CHECK(fixture.index().try_get_by_qualified_name("CommitOk", record));
	const int before = fixture.index().get_record_count();
	CHECK(analyze_source(source, path).valid());
	CHECK(source_analyzes(source, path));
	CHECK(fixture.index().get_record_count() == before);
	CHECK(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, "class_name CommitOk extends MissingBaseDefinitely\n") != OK);
	CHECK_FALSE(fixture.index().has_path(path));
}

void scenario_declaration_head_kinds_and_conformance() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	struct Head {
		const char *path;
		const char *source;
		const char *name;
		BSDeclarationKind kind;
	};
	const Head heads[] = {
		{ "res://tests/commit_trait.barista", "trait_name CommitTrait\n", "CommitTrait", BSDeclarationKind::TRAIT },
		{ "res://tests/commit_enum.barista", "enum_name CommitEnum:\n\tA = 0\n\tB = 1\n", "CommitEnum", BSDeclarationKind::ENUM },
		{ "res://tests/commit_tuple.barista", "tuple_name CommitTup(x: int, y: int)\n", "CommitTup", BSDeclarationKind::TUPLE },
		{ "res://tests/commit_generic.barista", "class_name CommitGeneric[T] extends RefCounted\n", "CommitGeneric", BSDeclarationKind::GENERIC_CLASS },
	};
	for (const Head &head : heads) {
		INFO(std::string(head.name));
		const Error status = BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(head.path, head.source);
		// M5 rejects generic analysis after declaration publication; the legacy case
		// specifically preserves its GENERIC_CLASS index record despite that diagnostic.
		CHECK(status == (head.kind == BSDeclarationKind::GENERIC_CLASS ? ERR_PARSE_ERROR : OK));
		BSDeclarationRecord record;
		BS_TEST_REQUIRE(fixture.index().try_get_by_qualified_name(head.name, record));
		CHECK(record.kind == head.kind);
	}
	BSCache::set_source_override(heads[0].path, heads[0].source);
	synchronize("res://tests/commit_conform.barista", "namespace commitns\n\nextend Node uses CommitTrait:\n\tpass\n");
	CHECK(fixture.index().get_conformance_files_in_namespace("commitns").size() >= 1);
	synchronize("res://tests/commit_annot.barista", "namespace annotns\n\nannotation CommitMark targets METHOD\n");
	CHECK(fixture.index().get_annotation_declaring_paths("annotns.CommitMark").size() == 1);
}

bool global_list_contains(const StringName &name) {
	List<StringName> names;
	ScriptServer::get_global_class_list(&names);
	return names.find(name) != nullptr;
}

void scenario_digest_mismatch_discards() {
	StorageFixture fixture;
	const String path = "res://tests/digest_mismatch.barista";
	const String source = "class_name DigestFresh extends Node\n";
	synchronize(path, source);
	BSDeclarationRecord record;
	BS_TEST_REQUIRE(fixture.index().try_get_by_qualified_name("DigestFresh", record));
	const uint64_t digests[] = { 999999, 999999, 888888, 777777 };
	for (int surface = 0; surface < 4; ++surface) {
		record.source_digest = digests[surface];
		CHECK(fixture.index().commit_record(fixture.index().claim_refresh(path), record));
		BSCache::set_source_override(path, source);
		if (surface == 0) {
			BSDeclarationRecord resolved;
			CHECK_FALSE(BaristaScriptLanguage::get_singleton()->try_resolve_declaration("DigestFresh", resolved));
		} else if (surface == 1) {
			CHECK(ScriptServer::get_global_class_path("DigestFresh").is_empty());
		} else if (surface == 2) {
			CHECK_FALSE(global_list_contains("DigestFresh"));
		} else {
			CHECK(String(ScriptServer::get_global_class_native_base("DigestFresh")).is_empty());
		}
		BS_TEST_REQUIRE(fixture.index().try_get_by_qualified_name("DigestFresh", record));
		CHECK(record.source_digest == digests[surface]);
	}
	const String enum_path = "res://tests/digest_enum.barista";
	const String enum_source = "enum_name DigestEnum:\n\tA = 0\n\tB = 1\n";
	BSCache::set_source_override(enum_path, enum_source);
	synchronize(enum_path, enum_source);
	CHECK(ScriptServer::is_global_class_enum("DigestEnum"));
	BSDeclarationRecord enumeration;
	BS_TEST_REQUIRE(fixture.index().try_get_by_qualified_name("DigestEnum", enumeration));
	enumeration.source_digest = 424242;
	CHECK(fixture.index().commit_record(fixture.index().claim_refresh(enum_path), enumeration));
	BSCache::set_source_override(enum_path, enum_source);
	CHECK_FALSE(ScriptServer::is_global_class_enum("DigestEnum"));
	BS_TEST_REQUIRE(fixture.index().try_get_by_qualified_name("DigestEnum", enumeration));
	CHECK(enumeration.source_digest == 424242);
	synchronize(path, source);
	synchronize(enum_path, enum_source);
	CHECK(ScriptServer::get_global_class_path("DigestFresh") == path);
	CHECK(global_list_contains("DigestFresh"));
	CHECK(String(ScriptServer::get_global_class_native_base("DigestFresh")) == "Node");
	BS_TEST_REQUIRE(fixture.index().try_get_by_qualified_name("DigestFresh", record));
	CHECK(record.source_digest == BSDeclarationIndex::compute_source_digest(source));
	CHECK(ScriptServer::is_global_class_enum("DigestEnum"));
	BS_TEST_REQUIRE(fixture.index().try_get_by_qualified_name("DigestEnum", enumeration));
	CHECK(enumeration.source_digest == BSDeclarationIndex::compute_source_digest(enum_source));
}

void scenario_namespace_change_invalidation() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	const String conform = "res://tests/ns_conform.barista";
	const String old_consumer = "res://tests/ns_consumer_old.barista";
	const String new_consumer = "res://tests/ns_consumer_new.barista";
	const String trait = "res://tests/ns_trait.barista";
	const String trait_source = "trait_name NsTrait\n";
	synchronize(trait, trait_source);
	BSCache::set_source_override(trait, trait_source);
	synchronize(conform, "namespace oldns\n\nextend Node uses NsTrait:\n\tpass\n");
	BSCache::set_source_override(old_consumer, "namespace oldns\nclass_name OldConsumer extends Node\n");
	BSCache::set_source_override(new_consumer, "namespace newns\nclass_name NewConsumer extends Node\n");
	parser_at(old_consumer, BSParserRef::PARSED);
	parser_at(new_consumer, BSParserRef::PARSED);
	CHECK(BSCache::has_parser(old_consumer));
	CHECK(BSCache::has_parser(new_consumer));
	BSCache::set_source_override(trait, trait_source);
	synchronize(conform, "namespace newns\n\nextend Node uses NsTrait:\n\tpass\n");
	CHECK_FALSE(BSCache::has_parser(old_consumer));
	CHECK_FALSE(BSCache::has_parser(new_consumer));
}

void scenario_explicit_out_of_root_import() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	synchronize("res://outside/out_trait.barista", "namespace outerspace\ntrait_name OuterTrait\n");
	BSAnalyzer::set_bootstrap_allowed_dependency_root("res://tests/");
	const AnalysisResult result = analyze_source("import outerspace\nclass_name NeedsOuter extends Node\n", "res://tests/needs_outer.barista");
	CHECK_FALSE(result.valid());
	bool saw = false;
	for (const auto &error : result.parser->get_errors()) {
		saw = saw || error.message.contains("outside the provider bootstrap root") || error.message.contains("Cannot import namespace") || error.message.contains("bootstrap cannot import");
	}
	CHECK(saw);
	BSCache::set_source_override("res://outside/out_trait.barista", "namespace outerspace\ntrait_name OuterTrait\n");
	// This deliberately excluded provider may fail synchronization: the legacy assertion
	// owns host filtering, not publication of an out-of-root provider.
	BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source("res://outside/out_conform.barista", "namespace outerspace\n\nextend Node uses OuterTrait:\n\tpass\n");
	const BSParserHost *host = BSParserHost::get_singleton();
	BS_TEST_REQUIRE(host != nullptr);
	CHECK_FALSE(host->is_bootstrap_path_allowed("res://outside/out_conform.barista"));
}

using Scenario = void (*)();
std::array<Scenario, 10> scenarios() {
	return { scenario_transitive_invalidation, scenario_move_remove, scenario_dependency_cycle,
		scenario_finalization_raises_dependencies, scenario_host_bootstrap_filtering,
		scenario_analyzer_declaration_commit, scenario_declaration_head_kinds_and_conformance,
		scenario_digest_mismatch_discards, scenario_namespace_change_invalidation,
		scenario_explicit_out_of_root_import };
}
} // namespace

TEST_SUITE("analyzer_dependency") {
	TEST_CASE("transitive_invalidation") { scenario_transitive_invalidation(); }
	TEST_CASE("move_remove") { scenario_move_remove(); }
	TEST_CASE("dependency_cycle") { scenario_dependency_cycle(); }
	TEST_CASE("finalization_raises_dependencies") { scenario_finalization_raises_dependencies(); }
	TEST_CASE("host_bootstrap_filtering") { scenario_host_bootstrap_filtering(); }
	TEST_CASE("analyzer_declaration_commit") { scenario_analyzer_declaration_commit(); }
	TEST_CASE("declaration_head_kinds_and_conformance") { scenario_declaration_head_kinds_and_conformance(); }
	TEST_CASE("digest_mismatch_discards") { scenario_digest_mismatch_discards(); }
	TEST_CASE("namespace_change_invalidation") { scenario_namespace_change_invalidation(); }
	TEST_CASE("explicit_out_of_root_import") { scenario_explicit_out_of_root_import(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		auto order = scenarios();
		for (Scenario scenario : order) {
			CHECK(verify_case_isolation(scenario));
		}
		std::reverse(order.begin(), order.end());
		for (Scenario scenario : order) {
			CHECK(verify_case_isolation(scenario));
		}
		std::mt19937 random(155);
		std::shuffle(order.begin(), order.end(), random);
		for (Scenario scenario : order) {
			CHECK(verify_case_isolation(scenario));
		}
	}
}
