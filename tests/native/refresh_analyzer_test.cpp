/**************************************************************************/
/*  refresh_analyzer_test.cpp                                             */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_conformance_registry.h"
#include "bs_global_class.h"
#include "bs_trait_utils.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <string>
using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;
namespace barista_script {
struct RefreshTestAccess {
	static void hook(const std::function<void(const String &, bool)> &callback) { BSAnalyzer::refresh_publication_hook = callback; }
};
} //namespace barista_script
namespace {
const char *ROOT = "res://tests/refresh/";
bool seed(StorageFixture &fixture, const String &name, const String &source) {
	const String path = String(ROOT) + name + ".barista";
	BSParser syntax;
	if (syntax.parse(source, path, false) != OK) {
		CHECK_MESSAGE(false, "provider setup did not parse");
		return false;
	}
	BSCache::set_source_override(path, source);
	auto record = BSDeclarationIndex::record_from_global_class(path, source, bs_resolve_global_class_from_source(source, path));
	record.declares_retroactive_conformances = !syntax.get_tree()->conformances.is_empty();
	return fixture.index().commit_record(fixture.index().claim_refresh(path), record);
}
void no_errors(const BSParser &parser) {
	for (const auto &error : parser.get_errors()) {
		INFO(std::string(error.message.utf8().get_data()));
		CHECK(false);
	}
}
} //namespace
TEST_SUITE("refresh_analyzer") {
	TEST_CASE("synchronize_restores_exact_override_after_success_and_failure") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = String(ROOT) + "restore.barista";
		for (bool present : { false, true }) {
			for (const String &previous : { String(), String("class_name Previous\n") }) {
				for (const String &source : { String("class_name Current\n"), String("class_name\n"), String("class_name Current\nvar value: int = \"bad\"\n") }) {
					if (present)
						BSCache::set_source_override(path, previous);
					else
						BSCache::clear_source_override(path);
					BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, source);
					CHECK(BSCache::has_source_override(path) == present);
					if (present && BSCache::has_source_override(path))
						CHECK(BSCache::get_source_code(path) == previous);
				}
			}
		}
	}
	TEST_CASE("temporary_override_does_not_restore_over_newer_identical_authority") {
		StorageFixture fixture;
		const String path = String(ROOT) + "ownership.barista";
		for (int mode : { 0, 1, 2 }) {
			BSCache::set_source_override(path, "previous");
			HashMap<String, String> temporary;
			temporary[path] = "installed";
			{
				BSCacheSourceOverrideGuard guard(temporary, true);
				if (mode == 2)
					BSCache::clear_source_override(path);
				BSCache::set_source_override(path, mode == 0 ? "changed" : "installed");
			}
			CHECK(BSCache::get_source_code(path) == (mode == 0 ? "changed" : "installed"));
		}
		BSCache::set_source_override(path, "previous");
		HashMap<String, String> first;
		first[path] = "outer";
		HashMap<String, String> second;
		second[path] = "inner";
		{
			BSCacheSourceOverrideGuard outer(first, true);
			{
				BSCacheSourceOverrideGuard inner(second, true);
			}
			CHECK(BSCache::get_source_code(path) == "outer");
		}
		CHECK(BSCache::get_source_code(path) == "previous");
	}
	TEST_CASE("refresh_invalidates_noninheritance_closure_and_preserves_unrelated_and_retained") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		BS_TEST_REQUIRE(seed(fixture, "c", "const VALUE = 1\n"));
		BS_TEST_REQUIRE(seed(fixture, "b", "const C = preload(\"c.barista\")\nconst VALUE = C.VALUE\n"));
		for (const String &name : { String("a"), String("d") })
			BS_TEST_REQUIRE(seed(fixture, name, "const B = preload(\"b.barista\")\nvar value: int = B.VALUE\n"));
		BS_TEST_REQUIRE(seed(fixture, "u", "var value: int = 1\n"));
		Error error = OK;
		auto a = BSCache::get_parser(String(ROOT) + "a.barista", BSParserRef::FULLY_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && a.is_valid());
		auto d = BSCache::get_parser(String(ROOT) + "d.barista", BSParserRef::FULLY_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && d.is_valid());
		auto u = BSCache::get_parser(String(ROOT) + "u.barista", BSParserRef::FULLY_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && u.is_valid());
		const auto closure = BSCache::collect_parser_invalidation_closure(String(ROOT) + "b.barista");
		CHECK(closure.has(String(ROOT) + "a.barista"));
		CHECK(closure.has(String(ROOT) + "d.barista"));
		CHECK_FALSE(closure.has(String(ROOT) + "u.barista"));
		const String changed = "const VALUE = \"changed\"\n";
		BSCache::set_source_override(String(ROOT) + "b.barista", changed);
		BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(String(ROOT) + "b.barista", changed);
		CHECK_FALSE(BSCache::has_parser(String(ROOT) + "a.barista"));
		CHECK_FALSE(BSCache::has_parser(String(ROOT) + "d.barista"));
		CHECK(BSCache::has_parser(String(ROOT) + "u.barista"));
		CHECK(a->get_parser()->get_tree()->get_member("value").get_datatype().builtin_type == Variant::INT);
		CHECK(BSCache::get_parser(String(ROOT) + "u.barista", BSParserRef::FULLY_SOLVED, error) == u);
		if (!BSCache::has_source_override(String(ROOT) + "b.barista"))
			return; // Restore defect is independently asserted above.
		auto latest = BSCache::get_parser(String(ROOT) + "a.barista", BSParserRef::FULLY_SOLVED, error);
		BS_TEST_REQUIRE(error != OK && latest.is_valid());
		BS_TEST_REQUIRE(latest->get_parser()->get_errors().size() == 1);
		const auto &diagnostic = latest->get_parser()->get_errors().front()->get();
		CHECK(diagnostic.message == "Cannot assign a value of type \"String\" to a variable of type \"int\".");
		const auto *initializer = latest->get_parser()->get_tree()->get_member("value").variable->initializer;
		CHECK(diagnostic.line == initializer->start_line);
		CHECK(diagnostic.column == initializer->start_column);
		CHECK(diagnostic.end_line == initializer->end_line);
		CHECK(diagnostic.end_column == initializer->end_column);
	}
	TEST_CASE("resource_source_and_reload_refresh_only_after_path_assignment") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = String(ROOT) + "resource.barista";
		BS_TEST_REQUIRE(seed(fixture, "resource", "class_name Before\n"));
		Ref<BaristaScript> resource;
		resource.instantiate();
		resource->_set_source_code("class_name Pathless\n");
		BSDeclarationRecord record;
		BS_TEST_REQUIRE(fixture.index().try_get_by_path(path, record));
		CHECK(record.qualified_name == "Before");
		resource->set_path(path);
		resource->_set_source_code("class_name Edited\n");
		BS_TEST_REQUIRE(fixture.index().try_get_by_path(path, record));
		CHECK(record.qualified_name == "Edited");
		CHECK(BSCache::get_source_code(path) == "class_name Edited\n");
		BSCache::set_source_override(path, "class_name Reloaded\n");
		CHECK(resource->_reload(false) == OK);
		BS_TEST_REQUIRE(fixture.index().try_get_by_path(path, record));
		// Pinned reload uses the resource buffer, not a competing override/disk input.
		CHECK(record.qualified_name == "Edited");
		CHECK(resource->_get_source_code() == "class_name Edited\n");
		CHECK(BSCache::get_source_code(path) == "class_name Reloaded\n");
		resource->_set_source_code("");
		CHECK(resource->_reload(false) == OK);
		CHECK(resource->_get_source_code().is_empty());
		CHECK(BSCache::has_source_override(path));
		CHECK(BSCache::get_source_code(path).is_empty());
		BS_TEST_REQUIRE(fixture.index().try_get_by_path(path, record));
		CHECK(record.qualified_name.is_empty());
		CHECK(record.source_digest == BSDeclarationIndex::compute_source_digest(""));
	}
	TEST_CASE("remove_and_move_clear_old_metadata_and_refresh_new_path") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String source = "class_name Removal\ntrait Trait:\n\tabstract func marker() -> int\nextend int uses Trait:\n\tfunc marker() -> int:\n\t\treturn 1\n";
		const String path = String(ROOT) + "remove.barista";
		BS_TEST_REQUIRE(seed(fixture, "remove", source));
		Error error = OK;
		auto old = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && old.is_valid());
		BS_TEST_REQUIRE(BSConformanceRegistry::get_singleton()->get_file_conformances(path).size() == 1);
		BSCache::remove_script(path);
		BSDeclarationRecord record;
		CHECK_FALSE(fixture.index().try_get_by_path(path, record));
		CHECK(BSConformanceRegistry::get_singleton()->get_file_conformances(path).is_empty());
		CHECK_FALSE(BSCache::has_source_override(path));
		CHECK(old->get_parser()->get_tree()->qualified_global_name == "Removal");
		BS_TEST_REQUIRE(seed(fixture, "remove", source));
		const String destination = String(ROOT) + "moved.barista";
		BSCache::set_source_override(destination, "class_name Moved\n");
		BSCache::move_script(path, destination);
		CHECK_FALSE(fixture.index().try_get_by_path(path, record));
		BS_TEST_REQUIRE(fixture.index().try_get_by_path(destination, record));
		CHECK(record.qualified_name == "Moved");
		CHECK_FALSE(BSCache::has_source_override(path));
	}
	TEST_CASE("superseded_refresh_never_publishes_or_clears_newer_authority") {
		for (bool final_write : { false, true }) {
			for (bool same_source : { false, true }) {
				for (int failure : { 0, 1, 2 }) {
					StorageFixture fixture;
					BSConformanceRegistry::ScopedCorpusState registry;
					const String path = String(ROOT) + "interleaved.barista";
					if (failure == 2 && !final_write)
						continue; // A parse failure reaches only final cleanup.
					const String first = failure == 2 ? "class_name\n" : failure ? "class_name A\nvar value: int = \"bad\"\n"
																				 : "class_name A\ntrait Trait:\n\tabstract func marker() -> int\nextend int uses Trait:\n\tfunc marker() -> int:\n\t\treturn 1\n";
					const String second = same_source ? first : "class_name B\ntrait Other:\n\tabstract func marker() -> int\nextend String uses Other:\n\tfunc marker() -> int:\n\t\treturn 2\n";
					// Same-source B must be a successful publication for this control.
					if (same_source && failure)
						continue;
					BSCache::set_source_override(path, "previous");
					bool invoked = false;
					StringName expected_trait;
					RefreshTestAccess::hook([&](const String &candidate_path, bool final) {
						if (invoked || final != final_write || candidate_path != path)
							return;
						invoked = true;
						BSCache::set_source_override(path, second);
						BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, second);
						const auto accepted = BSConformanceRegistry::get_singleton()->get_file_conformances(path);
						CHECK(accepted.size() == 1);
						if (accepted.size() == 1)
							expected_trait = accepted[0].trait_name;
					});
					BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, first);
					RefreshTestAccess::hook({});
					CHECK(invoked);
					CHECK(BSCache::get_source_code(path) == second);
					BSDeclarationRecord record;
					BS_TEST_REQUIRE(fixture.index().try_get_by_path(path, record));
					CHECK(record.qualified_name == (same_source ? "A" : "B"));
					CHECK(record.source_digest == BSDeclarationIndex::compute_source_digest(second));
					const auto entries = BSConformanceRegistry::get_singleton()->get_file_conformances(path);
					BS_TEST_REQUIRE(entries.size() == 1);
					CHECK(entries[0].trait_name == expected_trait);
				}
			}
		}
	}
	TEST_CASE("source_mismatch_before_either_write_does_not_attach_new_digest_to_old_ast") {
		for (bool final_write : { false, true }) {
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			const String path = String(ROOT) + "mismatch.barista";
			const String original = "class_name Original\n";
			const String candidate = "class_name Candidate\n";
			BS_TEST_REQUIRE(seed(fixture, "mismatch", original));
			bool invoked = false;
			const String newer = "class_name NewBytes\ntrait Current:\n\tabstract func marker() -> int\nextend int uses Current:\n\tfunc marker() -> int:\n\t\treturn 1\n";
			StringName accepted_trait;
			RefreshTestAccess::hook([&](const String &candidate_path, bool final) {
				if (invoked || final != final_write || candidate_path != path)
					return;
				invoked = true;
				BSCache::set_source_override(path, newer);
				// Same index claim: only the source guard prevents A's empty replacement
				// from erasing a newer interface publication.
				BSParser current;
				BS_TEST_REQUIRE(current.parse(newer, path, false) == OK);
				BSAnalyzer current_analyzer(&current);
				BS_TEST_REQUIRE(current_analyzer.analyze() == OK);
				const auto entries = BSConformanceRegistry::get_singleton()->get_file_conformances(path);
				BS_TEST_REQUIRE(entries.size() == 1);
				accepted_trait = entries[0].trait_name;
			});
			BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, candidate);
			RefreshTestAccess::hook({});
			CHECK(invoked);
			BSDeclarationRecord record;
			BS_TEST_REQUIRE(fixture.index().try_get_by_path(path, record));
			CHECK(record.qualified_name == "Original");
			CHECK(record.source_digest == BSDeclarationIndex::compute_source_digest(original));
			CHECK(BSCache::get_source_code(path) == newer);
			const auto entries = BSConformanceRegistry::get_singleton()->get_file_conformances(path);
			BS_TEST_REQUIRE(entries.size() == 1);
			CHECK(entries[0].trait_name == accepted_trait);
		}
	}
	TEST_CASE("registration_only_rejection_preserves_new_valid_declaration_subset") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = String(ROOT) + "mixed.barista";
		const String source = "class_name Mixed\ntrait Trait:\n\tabstract func marker() -> int\nextend int uses Trait:\n\tfunc marker() -> int:\n\t\treturn 1\nextend int uses Trait:\n\tfunc marker() -> int:\n\t\treturn 2\n";
		BSParser admitted;
		BS_TEST_REQUIRE(admitted.parse(source, path, false) == OK);
		BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, source);
		const auto entries = BSConformanceRegistry::get_singleton()->get_file_conformances(path);
		BS_TEST_REQUIRE(entries.size() == 1);
		CHECK(entries[0].trait_name == bs_trait_identity_name(admitted.get_tree()->get_member("Trait").m_class));
		BSDeclarationRecord record;
		CHECK_FALSE(fixture.index().try_get_by_path(path, record));
	}
	TEST_CASE("old_analyzer_cannot_publish_new_source_digest_or_replace_new_record") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = String(ROOT) + "claim.barista";
		const String old_source = "class_name Old\n";
		const String new_source = "class_name New\n";
		BS_TEST_REQUIRE(seed(fixture, "claim", old_source));
		BSParser old;
		BSAnalyzer analyzer(&old);
		analyzer.set_update_declaration_index(true);
		BS_TEST_REQUIRE(old.parse(old_source, path, false) == OK);
		BS_TEST_REQUIRE(seed(fixture, "claim", new_source));
		CHECK(analyzer.analyze() == OK);
		no_errors(old);
		BSDeclarationRecord record;
		BS_TEST_REQUIRE(fixture.index().try_get_by_path(path, record));
		CHECK(record.qualified_name == "New");
		CHECK(record.source_digest == BSDeclarationIndex::compute_source_digest(new_source));
	}
	TEST_CASE("retained_interface_cannot_replace_new_conformance_generation") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = String(ROOT) + "witness.barista";
		const String old_source = "trait Old:\n\tabstract func marker() -> int\nextend int uses Old:\n\tfunc marker() -> int:\n\t\treturn 1\n";
		const String new_source = "trait New:\n\tabstract func marker() -> int\nextend int uses New:\n\tfunc marker() -> int:\n\t\treturn 2\n";
		BS_TEST_REQUIRE(seed(fixture, "witness", old_source));
		Error error = OK;
		auto old = BSCache::get_parser(path, BSParserRef::INHERITANCE_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && old.is_valid());
		BSCache::remove_parser(path);
		BS_TEST_REQUIRE(seed(fixture, "witness", new_source));
		auto fresh = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && fresh.is_valid());
		auto before = BSConformanceRegistry::get_singleton()->get_file_conformances(path);
		BS_TEST_REQUIRE(before.size() == 1);
		CHECK(old->raise_status(BSParserRef::INTERFACE_SOLVED) == OK);
		auto after = BSConformanceRegistry::get_singleton()->get_file_conformances(path);
		BS_TEST_REQUIRE(after.size() == 1);
		CHECK(after[0].trait_name == before[0].trait_name);
	}
}

TEST_SUITE("refresh_analyzer") {
	TEST_CASE("return_graph_refresh_remove_move_repair_is_repeatable") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String b_path = String(ROOT) + "b.barista";
		const String moved_path = String(ROOT) + "moved.barista";
		const String good = "const C = preload(\"c.barista\")\nstatic func value() -> int:\n\treturn C.VALUE\n";
		const String changed = "const C = preload(\"c.barista\")\nstatic func value() -> String:\n\treturn \"changed\"\n";
		BS_TEST_REQUIRE(seed(fixture, "c", "const VALUE = 1\n"));
		BS_TEST_REQUIRE(seed(fixture, "b", good));
		for (const String &name : { String("a"), String("d") })
			BS_TEST_REQUIRE(seed(fixture, name, "const B = preload(\"b.barista\")\nvar result: int = B.value()\n"));
		BS_TEST_REQUIRE(seed(fixture, "u", "var value: int = 1\n"));
		Error error = OK;
		auto unrelated = BSCache::get_parser(String(ROOT) + "u.barista", BSParserRef::FULLY_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && unrelated.is_valid());
		Vector<uint8_t> previous_bytes;
		for (int cycle = 0; cycle < 2; ++cycle) {
			for (const String &name : { String("a"), String("d") }) {
				auto consumer = BSCache::get_parser(String(ROOT) + name + ".barista", BSParserRef::FULLY_SOLVED, error);
				BS_TEST_REQUIRE(error == OK && consumer.is_valid());
			}
			const auto old_closure = BSCache::collect_parser_invalidation_closure(String(ROOT) + "c.barista");
			CHECK(old_closure.size() == 4);
			CHECK(old_closure.has(b_path));
			CHECK(old_closure.has(String(ROOT) + "a.barista"));
			CHECK(old_closure.has(String(ROOT) + "d.barista"));
			BSCache::set_source_override(b_path, changed);
			CHECK(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(b_path, changed) == OK);
			for (const String &name : { String("a"), String("d") }) {
				auto consumer = BSCache::get_parser(String(ROOT) + name + ".barista", BSParserRef::FULLY_SOLVED, error);
				BS_TEST_REQUIRE(error != OK && consumer.is_valid());
				const auto &errors = consumer->get_parser()->get_errors();
				BS_TEST_REQUIRE(errors.size() == 1);
				const auto &diagnostic = errors.front()->get();
				CHECK(diagnostic.message == "Cannot assign a value of type \"String\" to a variable of type \"int\".");
				// The second-line call is the pinned assignment initializer source node.
				CHECK(diagnostic.line == 2);
				CHECK(diagnostic.column == 19);
				CHECK(diagnostic.end_line == 2);
				CHECK(diagnostic.end_column == 28);
			}
			CHECK(fixture.index().flush(fixture.path("repeat.index")) == OK);
			const auto serialized = read_bytes(fixture.path("repeat.index"));
			if (cycle != 0)
				CHECK(serialized == previous_bytes);
			previous_bytes = serialized;
			CHECK(fixture.index().flush(fixture.path("repeat.index")) == OK);
			CHECK(read_bytes(fixture.path("repeat.index")) == serialized);
			BSCache::set_source_override(moved_path, good);
			BSCache::move_script(b_path, moved_path);
			CHECK_FALSE(fixture.index().has_path(b_path));
			CHECK(fixture.index().has_path(moved_path));
			CHECK_FALSE(BSCache::has_parser(String(ROOT) + "a.barista"));
			CHECK_FALSE(BSCache::has_parser(String(ROOT) + "d.barista"));
			for (const String &name : { String("a"), String("d") }) {
				auto consumer = BSCache::get_parser(String(ROOT) + name + ".barista", BSParserRef::FULLY_SOLVED, error);
				BS_TEST_REQUIRE(error != OK && consumer.is_valid());
				bool missing = false;
				for (const auto &diagnostic : consumer->get_parser()->get_errors()) {
					if (diagnostic.message == "Preload file \"res://tests/refresh/b.barista\" does not exist.") {
						missing = true;
						CHECK(diagnostic.line == 1);
						CHECK(diagnostic.column == 19);
						CHECK(diagnostic.end_line == 1);
						CHECK(diagnostic.end_column == 30);
					}
				}
				CHECK(missing);
			}
			BSCache::remove_script(moved_path);
			CHECK_FALSE(fixture.index().has_path(moved_path));
			BSCache::set_source_override(b_path, good);
			CHECK(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(b_path, good) == OK);
			CHECK(BSCache::get_parser(String(ROOT) + "u.barista", BSParserRef::FULLY_SOLVED, error) == unrelated);
			for (const String &name : { String("a"), String("d") }) {
				auto consumer = BSCache::get_parser(String(ROOT) + name + ".barista", BSParserRef::FULLY_SOLVED, error);
				BS_TEST_REQUIRE(error == OK && consumer.is_valid());
				no_errors(*consumer->get_parser());
			}
		}
	}
}

TEST_SUITE("refresh_analyzer") {
	TEST_CASE("namespace_annotation_and_conformance_refresh_preserves_new_and_removes_old_identity") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = String(ROOT) + "namespaced.barista";
		const String body = "class_name Provider\nannotation Mark targets METHOD\nconst C = preload(\"c.barista\")\ntrait Trait:\n\tabstract func marker() -> int\nclass Bound uses Trait:\n\tfunc marker() -> int:\n\t\treturn 1\nextend int uses Trait:\n\tfunc marker() -> int:\n\t\treturn 1\n";
		const String old_source = "namespace before\n" + body;
		const String new_source = "namespace after\n" + body;
		BS_TEST_REQUIRE(seed(fixture, "c", "const VALUE = 1\n"));
		BS_TEST_REQUIRE(seed(fixture, "namespaced", old_source));
		BS_TEST_REQUIRE(seed(fixture, "anchor", "namespace after\nclass_name Anchor\n"));
		BS_TEST_REQUIRE(seed(fixture, "old_observer", "import before\n@Mark\nfunc test():\n\tpass\n"));
		BS_TEST_REQUIRE(seed(fixture, "new_observer", "import after\nfunc test():\n\tpass\n"));
		BS_TEST_REQUIRE(seed(fixture, "u", "var value: int = 1\n"));
		BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, old_source) == OK);
		Error error = OK;
		auto old_provider = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && old_provider.is_valid());
		auto old_observer = BSCache::get_parser(String(ROOT) + "old_observer.barista", BSParserRef::FULLY_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && old_observer.is_valid());
		auto new_observer = BSCache::get_parser(String(ROOT) + "new_observer.barista", BSParserRef::FULLY_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && new_observer.is_valid());
		auto unrelated = BSCache::get_parser(String(ROOT) + "u.barista", BSParserRef::FULLY_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && unrelated.is_valid());
		const auto old_entries = BSConformanceRegistry::get_singleton()->get_file_conformances(path);
		BS_TEST_REQUIRE(old_entries.size() == 1);
		// Only applied type arguments produce ClassTraitBinding records.
		CHECK(BSConformanceRegistry::get_singleton()->get_file_trait_bindings(path).is_empty());
		CHECK(BSConformanceRegistry::get_singleton()->debug_get_loaded_files(path).has(String(ROOT) + "c.barista"));
		BSCache::set_source_override(path, new_source);
		BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, new_source) == OK);
		CHECK_FALSE(BSCache::has_parser(String(ROOT) + "old_observer.barista"));
		CHECK_FALSE(BSCache::has_parser(String(ROOT) + "new_observer.barista"));
		CHECK(BSCache::has_parser(String(ROOT) + "u.barista"));
		BSDeclarationRecord record;
		CHECK_FALSE(fixture.index().try_get_by_qualified_name("before.Provider", record));
		CHECK(fixture.index().try_get_by_qualified_name("after.Provider", record));
		CHECK(fixture.index().get_annotation_declaring_paths("before.Mark").is_empty());
		CHECK(fixture.index().get_annotation_declaring_paths("after.Mark").size() == 1);
		CHECK(fixture.index().get_conformance_files_in_namespace("before").is_empty());
		CHECK(fixture.index().get_conformance_files_in_namespace("after").size() == 1);
		const auto current_entries = BSConformanceRegistry::get_singleton()->get_file_conformances(path);
		BS_TEST_REQUIRE(current_entries.size() == 1);
		CHECK(current_entries[0].trait_name != old_entries[0].trait_name);
		CHECK(bs_trait_identity_name(old_provider->get_parser()->get_tree()->get_member("Trait").m_class) == old_entries[0].trait_name);
		const String updated_consumer = "import after\n@Mark\nfunc test():\n\tpass\n";
		BSCache::set_source_override(String(ROOT) + "old_observer.barista", updated_consumer);
		auto updated = BSCache::get_parser(String(ROOT) + "old_observer.barista", BSParserRef::FULLY_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && updated.is_valid());
		no_errors(*updated->get_parser());
		CHECK(BSCache::get_parser(String(ROOT) + "u.barista", BSParserRef::FULLY_SOLVED, error) == unrelated);
		// Parse/body failure must clear all three per-file registry stores, unlike a
		// registration-only rejected declaration with a newly accepted sibling.
		for (const String &invalid : { String("class_name\n"), new_source + String("var broken: int = \"bad\"\n") }) {
			// A typed registry-state control covers the binding store without inventing
			// unsupported user generic syntax. The source fixture above establishes
			// the actual conformance and load licensing independently.
			BSConformanceRegistry::ClassTraitBinding binding;
			binding.target_fqcn = "sentinel";
			binding.trait_name = current_entries[0].trait_name;
			binding.source_file = path;
			Vector<BSConformanceRegistry::ClassTraitBinding> bindings;
			bindings.push_back(binding);
			HashSet<String> loaded;
			loaded.insert(String(ROOT) + "c.barista");
			BSConformanceRegistry::get_singleton()->try_replace_file_conformances(path, current_entries, bindings, loaded);
			BS_TEST_REQUIRE(BSConformanceRegistry::get_singleton()->get_file_trait_bindings(path).size() == 1);
			BS_TEST_REQUIRE(BSConformanceRegistry::get_singleton()->debug_get_loaded_files(path).has(String(ROOT) + "c.barista"));
			BSCache::set_source_override(path, invalid);
			CHECK(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, invalid) != OK);
			CHECK_FALSE(fixture.index().has_path(path));
			CHECK(BSConformanceRegistry::get_singleton()->get_file_conformances(path).is_empty());
			CHECK(BSConformanceRegistry::get_singleton()->get_file_trait_bindings(path).is_empty());
			CHECK(BSConformanceRegistry::get_singleton()->debug_get_loaded_files(path).is_empty());
			BSCache::set_source_override(path, new_source);
			CHECK(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, new_source) == OK);
		}
		BSCache::set_source_override(path, "");
		CHECK(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, "") == OK);
		CHECK(BSConformanceRegistry::get_singleton()->get_file_conformances(path).is_empty());
		CHECK(BSConformanceRegistry::get_singleton()->get_file_trait_bindings(path).is_empty());
		CHECK(BSConformanceRegistry::get_singleton()->debug_get_loaded_files(path).is_empty());
		CHECK(fixture.index().get_annotation_declaring_paths("after.Mark").is_empty());
	}
	TEST_CASE("repeated_refresh_reclaims_detached_generations_and_edges") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = String(ROOT) + "release.barista";
		const String consumer_path = String(ROOT) + "owner.barista";
		for (int iteration = 0; iteration < 3; ++iteration) {
			BS_TEST_REQUIRE(seed(fixture, "release", "class_name Release\nclass Nested:\n\tvar value: int\n"));
			BS_TEST_REQUIRE(seed(fixture, "owner", "const P = preload(\"release.barista\")\nvar item: P.Nested\n"));
			Error error = OK;
			auto owner = BSCache::get_parser(consumer_path, BSParserRef::FULLY_SOLVED, error);
			BS_TEST_REQUIRE(error == OK && owner.is_valid());
			auto provider = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
			BS_TEST_REQUIRE(error == OK && provider.is_valid());
			const auto owner_id = owner->get_instance_id(), provider_id = provider->get_instance_id();
			BSCache::set_source_override(path, "class_name Release\nclass Nested:\n\tvar value: String\n");
			BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, BSCache::get_source_code(path)) == OK);
			CHECK(provider->get_parser()->get_tree()->get_member("Nested").m_class->get_member("value").get_datatype().builtin_type == Variant::INT);
			owner.unref();
			provider.unref();
			BSCache::clear();
			CHECK(ObjectDB::get_instance(owner_id) == nullptr);
			CHECK(ObjectDB::get_instance(provider_id) == nullptr);
			CHECK(BSCache::collect_parser_invalidation_closure(path).size() == 1);
			CHECK(BSCache::get_inverse_dependencies(path).is_empty());
			CHECK(fixture.index().get_record_count() == 2);
		}
	}
}
