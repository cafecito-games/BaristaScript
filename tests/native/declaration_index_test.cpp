/**************************************************************************/
/*  declaration_index_test.cpp                                            */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_parser.h"
#include "storage_fixture.h"

using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
using Status = BSDeclarationIndexLoadStatus;
using Fault = BSDeclarationIndex::WriteFault;

BSDeclarationRecord record(const String &path, const String &name, uint64_t digest, const String &ns = "") {
	BSDeclarationRecord value;
	value.path = path;
	value.qualified_name = name;
	value.namespace_name = ns;
	value.source_digest = digest;
	value.kind = name.is_empty() ? BSDeclarationKind::NONE : BSDeclarationKind::CLASS;
	value.base_type = name.is_empty() ? String() : String("Node");
	return value;
}

bool commit(BSDeclarationIndex &index, const BSDeclarationRecord &value) {
	return BaristaScriptLanguage::get_singleton()->commit_declaration_record(index.claim_refresh(value.path), value);
}
} // namespace

TEST_SUITE("declaration_index") {
	static void scenario_cold_and_round_trip() {
		StorageFixture fixture;
		auto &index = fixture.index();
		CHECK(index.load(fixture.path("absent.bsi")) == Status::COLD);
		CHECK(index.get_record_count() == 0);
		const String a = "res://tests/declaration_index_fixtures/a.barista";
		auto alpha = record(a, "game.Alpha", BSDeclarationIndex::compute_source_digest("class_name Alpha extends Node"), "game");
		alpha.global_annotations.push_back("game.Mark");
		CHECK(commit(index, alpha));
		auto conform = record("res://tests/declaration_index_fixtures/conform.barista", "", 42, "game");
		conform.declares_retroactive_conformances = true;
		CHECK(commit(index, conform));
		const String store = fixture.path("round_trip.bsi");
		CHECK(index.flush(store) == OK);
		const auto first = read_bytes(store);
		CHECK(index.flush(store) == OK);
		CHECK(read_bytes(store) == first);
		// The old suite rewrote this fixture. The native suite uses it as an immutable oracle.
		CHECK(first == read_bytes("res://tests/declaration_index_fixtures/golden_index.bsi"));
		index.clear();
		CHECK(index.load(store) == Status::OK);
		CHECK(index.get_record_count() == 2);
		const auto conformances = index.get_conformance_files_in_namespace("game");
		CHECK(conformances.size() == 1);
		if (conformances.size() == 1) {
			CHECK(conformances[0].ends_with("conform.barista"));
		}
		CHECK(index.get_annotation_declaring_paths("game.Mark").size() == 1);
	}
	TEST_CASE("cold_and_round_trip") { scenario_cold_and_round_trip(); }

	static void scenario_kinds_and_views() {
		StorageFixture fixture;
		struct KindRow {
			const char *path;
			BSDeclarationKind kind;
			const char *name;
			const char *ns;
		};
		const KindRow rows[] = {
			{ "res://tests/k_class.barista", BSDeclarationKind::CLASS, "KClass", "" },
			{ "res://tests/k_generic.barista", BSDeclarationKind::GENERIC_CLASS, "pkg.Box", "pkg" },
			{ "res://tests/k_trait.barista", BSDeclarationKind::TRAIT, "pkg.Trait", "pkg" },
			{ "res://tests/k_enum.barista", BSDeclarationKind::ENUM, "pkg.Enum", "pkg" },
			{ "res://tests/k_tuple.barista", BSDeclarationKind::TUPLE, "pkg.Tup", "pkg" },
		};
		for (const auto &row : rows) {
			auto value = record(row.path, row.name, 1, row.ns);
			value.kind = row.kind;
			value.base_type = row.kind == BSDeclarationKind::CLASS || row.kind == BSDeclarationKind::GENERIC_CLASS ? "RefCounted" : "";
			value.is_abstract = row.kind != BSDeclarationKind::CLASS;
			CHECK(commit(fixture.index(), value));
		}
		CHECK(fixture.index().get_record_count() == 5);
	}
	TEST_CASE("kinds_and_views") { scenario_kinds_and_views(); }

	static void scenario_lifecycle_tokens() {
		StorageFixture fixture;
		auto &index = fixture.index();
		auto *language = BaristaScriptLanguage::get_singleton();
		const String path = "res://tests/token.barista";
		const auto old_token = index.claim_refresh(path);
		const auto new_token = index.claim_refresh(path);
		CHECK_FALSE(language->commit_declaration_record(old_token, record(path, "Old", 1)));
		CHECK(language->commit_declaration_record(new_token, record(path, "New", 2)));
		const auto failed = index.claim_refresh(path);
		CHECK(language->remove_declaration_path(path, failed));
		CHECK(index.get_record_count() == 0);
	}
	TEST_CASE("lifecycle_tokens") { scenario_lifecycle_tokens(); }

	static void scenario_commit_rejects_noncanonical() {
		StorageFixture fixture;
		auto &index = fixture.index();
		auto evil = record("user://evil.barista", "", 1, "evil");
		evil.declares_retroactive_conformances = true;
		CHECK_FALSE(commit(index, evil));
		CHECK(index.get_record_count() == 0);
		CHECK(index.get_conformance_files_in_namespace("evil").is_empty());
		auto invalid = record("res://tests/bad_kind.barista", "BadKind", 2);
		invalid.kind = static_cast<BSDeclarationKind>(99);
		CHECK_FALSE(commit(index, invalid));
		CHECK(index.get_record_count() == 0);
	}
	TEST_CASE("commit_rejects_noncanonical") { scenario_commit_rejects_noncanonical(); }

	static void scenario_corrupt_fixtures() {
		StorageFixture fixture;
		const String directory = "res://tests/declaration_index_fixtures";
		const auto golden = read_bytes(directory.path_join("golden_index.bsi"));
		CHECK(golden.size() > 20);
		if (golden.size() <= 20) {
			return;
		}
		struct Mutation {
			const char *name;
			Status status;
		};
		const Mutation rows[] = { { "bad_magic", Status::BAD_MAGIC }, { "old_version", Status::UNSUPPORTED_VERSION },
			{ "truncated", Status::TRUNCATED }, { "bad_checksum", Status::BAD_CHECKSUM } };
		for (int i = 0; i < 4; ++i) {
			auto changed = golden;
			switch (i) {
				case 0:
					changed.write[0] = 'X';
					break;
				case 1:
					changed.write[4] = 2;
					changed.write[changed.size() - 1] = uint8_t(changed[changed.size() - 1] + 1);
					break;
				case 2:
					changed.resize(8);
					break;
				case 3:
					changed.write[12] = uint8_t(changed[12] + 1);
					break;
			}
			const String name = String(rows[i].name) + String(".bsi");
			CHECK(changed == read_bytes(directory.path_join(name)));
			const String path = fixture.path(name);
			if (!write_bytes(path, changed)) {
				return;
			}
			fixture.index().clear();
			CHECK(fixture.index().load(path) == rows[i].status);
			CHECK(fixture.index().get_record_count() == 0);
			// Also run the unchanged fixture itself through the production loader.
			fixture.index().clear();
			CHECK(fixture.index().load(directory.path_join(name)) == rows[i].status);
			CHECK(fixture.index().get_record_count() == 0);
		}
	}
	TEST_CASE("corrupt_fixtures") { scenario_corrupt_fixtures(); }

	static void scenario_atomic_write_faults() {
		StorageFixture fixture;
		auto &index = fixture.index();
		CHECK(commit(index, record("res://tests/fault.barista", "Fault", 7)));
		const String store = fixture.path("atomic.bsi");
		CHECK(index.flush(store) == OK);
		const auto before = read_bytes(store);
		for (Fault fault : { Fault::BEFORE_WRITE, Fault::AFTER_WRITE_BEFORE_RENAME, Fault::TRUNCATE_TEMP_AFTER_WRITE }) {
			CHECK(index.flush(store, fault) != OK);
			CHECK(read_bytes(store) == before);
		}
	}
	TEST_CASE("atomic_write_faults") { scenario_atomic_write_faults(); }

	static void scenario_host_conformance() {
		StorageFixture fixture;
		auto value = record("res://tests/host_conform.barista", "", 9, "hostns");
		value.declares_retroactive_conformances = true;
		CHECK(commit(fixture.index(), value));
		const BSParserHost *host = BSParserHost::get_singleton();
		CHECK(host != nullptr);
		if (!host) {
			return;
		}
		CHECK(host->get_conformance_files_in_namespace("hostns").size() == 1);
		CHECK(host->get_conformance_files_in_namespace("").is_empty());
		BSAnalyzer::set_bootstrap_allowed_dependency_root("res://tests/");
		CHECK(host->is_bootstrap_path_allowed("res://tests/host_conform.barista"));
		CHECK_FALSE(host->is_bootstrap_path_allowed("res://outside/x.barista"));
	}
	TEST_CASE("host_conformance") { scenario_host_conformance(); }
	TEST_CASE("repeated_and_reversed_cases_restore_ambient_state") {
		void (*scenarios[])() = {
			scenario_cold_and_round_trip,
			scenario_kinds_and_views,
			scenario_lifecycle_tokens,
			scenario_commit_rejects_noncanonical,
			scenario_corrupt_fixtures,
			scenario_atomic_write_faults,
			scenario_host_conformance,
		};
		const int count = int(sizeof(scenarios) / sizeof(scenarios[0]));
		for (int pass = 0; pass < 2; ++pass) {
			for (int i = 0; i < count; ++i) {
				CHECK(verify_case_isolation(scenarios[i]));
			}
			for (int i = count - 1; i >= 0; --i) {
				CHECK(verify_case_isolation(scenarios[i]));
			}
		}
	}
} // TEST_SUITE
