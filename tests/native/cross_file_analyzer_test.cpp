/**************************************************************************/
/*  cross_file_analyzer_test.cpp                                           */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "barista_script_language.h"
#include "bs_analyzer.h"
#include "bs_cache.h"
#include "bs_conformance_registry.h"
#include "bs_global_class.h"
#include "doctest.h"
#include <godot_cpp/classes/file_access.hpp>
#include <string>

using namespace godot;
using namespace barista_script;

namespace {
// Source oracle: Foundry c9d5e35 fs_analyzer.cpp validate_imports:11510,
// get_imported_global_class:9894, type-node caller:2968, _trait_use_source:10030.
// Parser TypeNode allocation captures the preceding colon; head extents end at EOF
// (bs_parser.cpp parse_type_member and parse), so spans include those node boundaries.
// X1-local setup using the merged production isolation scopes. Shared storage fixtures
// remain owned by #154; no Script bindings are needed for these compiler contracts.
struct Names {
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	BSDeclarationIndex::ScopedCorpusState declarations{ language->get_declaration_index() };
	BSConformanceRegistry::ScopedCorpusState conformances;
	BSCache::ScopedCorpusState cache;
	String bootstrap = BSAnalyzer::get_bootstrap_allowed_dependency_root();
	Names() {
		language->get_declaration_index().clear();
		BSAnalyzer::set_bootstrap_allowed_dependency_root("");
	}
	~Names() { BSAnalyzer::set_bootstrap_allowed_dependency_root(bootstrap); }
	String add(const String &file, const String &source) {
		const String path = "res://tests/x1/" + file + ".barista";
		BSCache::set_source_override(path, source);
		BSDeclarationRecord record = BSDeclarationIndex::record_from_global_class(path, source, bs_resolve_global_class_from_source(source, path));
		BSParser parser;
		if (parser.parse(source, path, false) == OK) {
			record.namespace_name = parser.get_tree()->namespace_name;
			record.declares_retroactive_conformances = !parser.get_tree()->conformances.is_empty();
			for (const auto *annotation : parser.get_tree()->annotation_declarations) {
				record.global_annotations.push_back(annotation->qualified_name);
			}
		}
		auto &index = language->get_declaration_index();
		CHECK(index.commit_record(index.claim_refresh(path), record));
		return path;
	}
};
struct Result {
	Error status = OK;
	Vector<BSParser::ParserError> errors;
	BSParser::DataType type;
};
Result analyze(const String &source) {
	HashMap<String, String> sources;
	sources["res://tests/x1/consumer.barista"] = source;
	BSCacheSourceOverrideGuard overrides(sources);
	BSParser parser;
	Result result;
	result.status = parser.parse(source, "res://tests/x1/consumer.barista", false);
	if (result.status == OK) {
		BSAnalyzer analyzer(&parser);
		result.status = analyzer.analyze();
	}
	for (const auto &error : parser.get_errors()) {
		result.errors.push_back(error);
		INFO(std::string(error.message.utf8().get_data()));
	}
	if (parser.get_tree() != nullptr && parser.get_tree()->has_member("x")) {
		result.type = parser.get_tree()->get_member("x").get_datatype();
	}
	return result;
}
void valid(const String &source) {
	const Result result = analyze(source);
	for (const auto &error : result.errors) {
		INFO(std::string(error.message.utf8().get_data()));
		CHECK(result.status == OK);
	}
	CHECK(result.status == OK);
	CHECK(result.errors.is_empty());
}
void error_is(const Result &result, const String &message, int line, int column, int end_line, int end_column) {
	CHECK(result.status != OK);
	for (const auto &error : result.errors) {
		INFO(std::string(error.message.utf8().get_data()));
		CHECK(result.errors.size() == 1);
	}
	CHECK(result.errors.size() == 1);
	if (result.errors.is_empty()) {
		return;
	}
	const auto &error = result.errors[0];
	CHECK(std::string(error.message.utf8().get_data()) == std::string(message.utf8().get_data()));
	CHECK(error.line == line);
	CHECK(error.column == column);
	CHECK(error.end_line == end_line);
	CHECK(error.end_column == end_column);
}
} //namespace

TEST_SUITE("cross_file_analyzer") {
	TEST_CASE("unknown_import_is_an_error_outside_bootstrap") {
		Names names;
		error_is(analyze("import missing.ns\nfunc test():\n\tpass\n"),
				"Could not find imported namespace \"missing.ns\".", 1, 1, 5, 1);
	}
	TEST_CASE("all_import_candidates_are_sorted_and_duplicates_are_deduplicated") {
		Names names;
		names.add("a", "namespace a\nclass_name Item\n");
		names.add("b", "namespace b\nclass_name Item\n");
		for (const String &imports : { String("import a\nimport b\n"), String("import b\nimport a\n") }) {
			error_is(analyze(imports + String("var x: Item\n")),
					"Could not resolve type \"Item\": imported declarations \"a.Item\" and \"b.Item\" are ambiguous.", 3, 6, 3, 12);
		}
		valid("import a\nimport a\nvar x: Item\n");
		valid("import a\nimport b\nvar x: a.Item\n");
	}
	TEST_CASE("current_namespace_precedes_imports_and_global_fallback") {
		Names names;
		names.add("global", "class_name Item\n");
		const String own = names.add("own", "namespace own\nclass_name Item\n");
		const String other = names.add("other", "namespace other\nclass_name Item\n");
		CHECK(analyze("namespace own\nimport other\nvar x: Item\n").type.script_path == own);
		CHECK(analyze("import other\nvar x: Item\n").type.script_path == other);
		CHECK(analyze("var x: Item\n").type.script_path == "res://tests/x1/global.barista");
		valid("import own\nimport other\nclass Item:\n\tpass\nvar x: Item\n");
	}
	TEST_CASE("trait_and_conformance_uses_reject_ambiguous_imports") {
		Names names;
		names.add("a", "namespace a\ntrait_name T\n");
		names.add("b", "namespace b\ntrait_name T\n");
		for (const String &imports : { String("import a\nimport b\n"), String("import b\nimport a\n") }) {
			const String message = "Could not resolve trait \"T\": imported declarations \"a.T\" and \"b.T\" are ambiguous.";
			error_is(analyze(imports + String("uses T\n")), message, 3, 6, 3, 7);
			error_is(analyze(imports + String("extend Node uses T:\n\tpass\n")), message, 3, 18, 3, 19);
		}
		valid("import a\nimport b\nuses a.T\n");
		valid("import a\nimport a\nuses T\n");
	}
	TEST_CASE("lexical_wrong_kind_does_not_fall_through_to_imported_trait") {
		Names names;
		names.add("trait", "namespace lib\ntrait_name T\n");
		error_is(analyze("import lib\nclass T:\n\tpass\nclass User uses T:\n\tpass\n"), "\"T\" is not a trait.", 4, 17, 4, 18);
		error_is(analyze("import lib\nclass T:\n\tpass\nextend Node uses T:\n\tpass\n"), "\"T\" is not a trait.", 4, 18, 4, 19);
	}
	TEST_CASE("inheritance_uses_the_same_scope_policy") {
		Names names;
		names.add("a", "namespace a\nclass_name Base\n");
		names.add("b", "namespace b\nclass_name Base\n");
		valid("import a\nextends Base\n");
		valid("namespace a\nimport b\nextends Base\n");
		valid("import a\nimport b\nextends a.Base\n");
		error_is(analyze("import b\nimport a\nextends Base\n"),
				"Could not resolve base class \"Base\": imported declarations \"a.Base\" and \"b.Base\" are ambiguous.", 3, 9, 3, 13);
	}
	TEST_CASE("selected_stale_or_broken_provider_blocks_global_and_native_fallback") {
		Names names;
		names.add("global", "class_name Item\n");
		const String path = names.add("own", "namespace own\nclass_name Item\n");
		BSCache::set_source_override(path, "namespace own\nclass_name Changed\n");
		const String message = "Could not resolve type \"Item\": declaration \"own.Item\" from \"res://tests/x1/own.barista\" is stale or invalid.";
		for (int repeat = 0; repeat < 2; repeat++) {
			error_is(analyze("namespace own\nvar x: Item\n"), message, 2, 6, 2, 12);
			BSDeclarationRecord record;
			CHECK(names.language->get_declaration_index().try_get_by_qualified_name("own.Item", record));
			CHECK(record.path == path);
		}
		const String native = names.add("native", "namespace own\nclass_name Node\n");
		BSCache::set_source_override(native, "namespace own\nclass_name ChangedNode\n");
		error_is(analyze("namespace own\nvar x: Node\n"), "Could not resolve type \"Node\": declaration \"own.Node\" from \"res://tests/x1/native.barista\" is stale or invalid.", 2, 6, 2, 12);
		error_is(analyze("namespace own\nfunc test():\n\tvar x = Node\n"), "Could not resolve type \"Node\": declaration \"own.Node\" from \"res://tests/x1/native.barista\" is stale or invalid.", 3, 13, 3, 17);
		names.add("broken", "namespace broken\nclass_name Item\nfunc invalid():\n\tvar x = )\n");
		error_is(analyze("import broken\nvar x: Item\n"), "Could not resolve type \"Item\": provider \"res://tests/x1/broken.barista\" could not be parsed.", 2, 6, 2, 12);
	}
	TEST_CASE("namespace_existence_includes_all_heads_and_declaration_only_descendants") {
		Names names;
		names.add("class", "namespace classes.child\nclass_name Item\n");
		names.add("trait", "namespace traits.child\ntrait_name T\n");
		names.add("enum", "namespace enums.child\nenum_name E:\n\tA = 0\n\tB = 1\n");
		names.add("tuple", "namespace tuples.child\ntuple_name Point(x: int, y: int)\n");
		names.add("annotation", "namespace annotations.child\nannotation mark targets METHOD\n");
		names.add("conformance", "namespace conformances.child\nextend Node uses MissingTrait:\n\tpass\n");
		for (const String &ns : { String("classes"), String("traits"), String("enums"), String("tuples"), String("annotations"), String("conformances") }) {
			valid(String("import ") + ns + String("\nfunc test():\n\tpass\n"));
		}
		// Ancestor discovery grants no exact descendant conformance membership/load edge.
		CHECK(names.language->get_conformance_files_in_namespace("conformances").is_empty());
		CHECK(names.language->get_conformance_files_in_namespace("conformances.child").size() == 1);
		CHECK(BSConformanceRegistry::get_singleton()->get_file_conformances("res://tests/x1/conformance.barista").is_empty());
	}
	TEST_CASE("annotation_ambiguity_and_bootstrap_controls_are_preserved") {
		Names names;
		names.add("a", "namespace a\nannotation mark targets METHOD\n");
		names.add("b", "namespace b\nannotation mark targets METHOD\n");
		for (const String &imports : { String("import a\nimport b\n"), String("import b\nimport a\n") }) {
			error_is(analyze(imports + String("@mark\nfunc test():\n\tpass\n")),
					"Ambiguous annotation \"@mark\": it is declared in imported namespaces \"a\" and \"b\".", 3, 1, 3, 6);
		}
		valid("import a\nimport a\n@mark\nfunc test():\n\tpass\n");
		BSAnalyzer::set_bootstrap_allowed_dependency_root("res://tests/x1/");
		valid("import a\nfunc test():\n\tpass\n");
		BSAnalyzer::set_bootstrap_allowed_dependency_root("res://elsewhere/");
		error_is(analyze("import a\nfunc test():\n\tpass\n"), "Build task bootstrap cannot import namespace \"a\"; annotation \"a.mark\" from \"res://tests/x1/a.barista\" is outside the provider bootstrap root \"res://elsewhere\".", 1, 1, 5, 1);
	}

	TEST_CASE("complete_candidate_set_and_qualified_misses_are_deterministic") {
		Names names;
		for (const String &ns : { String("a"), String("b"), String("c") }) {
			names.add(ns, String("namespace ") + ns + String("\nclass_name Item\n"));
		}
		for (const String &imports : { String("import c\nimport a\nimport b\n"), String("import b\nimport c\nimport a\n") }) {
			error_is(analyze(imports + String("var x: Item\n")), "Could not resolve type \"Item\": imported declarations \"a.Item\", \"b.Item\" and \"c.Item\" are ambiguous.", 4, 6, 4, 12);
		}
		error_is(analyze("import a\nvar x: missing.Item\n"), "Could not find type \"missing.Item\".", 2, 8, 2, 15);
		error_is(analyze("import missing\nimport missing\n"), "Could not find imported namespace \"missing\".", 1, 1, 4, 1);
	}
	TEST_CASE("ordinary_validation_preserves_index_bytes_claims_and_conformance_state") {
		Names names;
		const String path = names.add("a", "namespace a\ntrait_name T\n");
		names.add("b", "namespace b\ntrait_name T\n");
		auto &index = names.language->get_declaration_index();
		BSDeclarationRecord record;
		CHECK(index.try_get_by_path(path, record));
		const uint64_t token = index.claim_refresh(path);
		CHECK(index.flush("user://x1-before.bsi") == OK);
		const PackedByteArray before = FileAccess::get_file_as_bytes("user://x1-before.bsi");
		CHECK_FALSE(before.is_empty());
		const String source = "import a\nimport b\nuses T\n";
		for (int repeat = 0; repeat < 2; repeat++) {
			error_is(analyze(source), "Could not resolve trait \"T\": imported declarations \"a.T\" and \"b.T\" are ambiguous.", 3, 6, 3, 7);
			const Dictionary result = names.language->_validate(source, "res://tests/x1/consumer.barista", true, true, false, false);
			CHECK_FALSE(bool(result["valid"]));
			Ref<BaristaScript> script;
			script.instantiate();
			script->_set_source_code(source);
			CHECK_FALSE(script->_is_valid());
			valid("import a\nuses T\n");
			CHECK(BSConformanceRegistry::get_singleton()->get_file_conformances("res://tests/x1/consumer.barista").is_empty());
			CHECK(BSConformanceRegistry::get_singleton()->get_file_trait_bindings("res://tests/x1/consumer.barista").is_empty());
			CHECK(index.flush("user://x1-after.bsi") == OK);
			CHECK(FileAccess::get_file_as_bytes("user://x1-after.bsi") == before);
		}
		// A claim predating ordinary analysis is still current: lookup neither refreshed
		// nor removed this provider. Committing identical bytes exercises that token.
		CHECK(index.commit_record(token, record));
	}
	TEST_CASE("wrong_kind_index_candidate_does_not_fall_back_to_a_global_trait") {
		Names names;
		names.add("global", "trait_name T\n");
		names.add("own", "namespace own\nclass_name T\n");
		error_is(analyze("namespace own\nuses T\n"), "\"T\" is not a trait.", 2, 6, 2, 7);
		error_is(analyze("namespace own\nextend Node uses T:\n\tpass\n"), "\"T\" is not a trait.", 2, 18, 2, 19);
	}

	TEST_CASE("separate_use_sites_keep_their_own_ambiguity_diagnostics") {
		Names names;
		names.add("a", "namespace a\nclass_name Item\n");
		names.add("b", "namespace b\nclass_name Item\n");
		const Result result = analyze("import a\nimport b\nvar x: Item\nvar y: Item\n");
		CHECK(result.status != OK);
		CHECK(result.errors.size() == 2);
		for (int i = 0; i < result.errors.size(); i++) {
			Result one;
			one.status = result.status;
			one.errors.push_back(result.errors[i]);
			error_is(one, "Could not resolve type \"Item\": imported declarations \"a.Item\" and \"b.Item\" are ambiguous.", 3 + i, 6, 3 + i, 12);
		}
	}
	TEST_CASE("public_validity_matches_typed_lookup_results") {
		Names names;
		names.add("a", "namespace a\nclass_name Item\n");
		names.add("b", "namespace b\nclass_name Item\n");
		for (const String &source : { String("import missing.ns\nfunc test():\n\tpass\n"),
					 String("import a\nimport b\nvar x: Item\n"), String("import a\nimport b\nvar x: a.Item\n"),
					 String("import a\nimport a\nvar x: Item\n") }) {
			const Result typed = analyze(source);
			const bool expected = typed.status == OK;
			const Dictionary result = names.language->_validate(source, "res://tests/x1/consumer.barista", true, true, false, false);
			CHECK(bool(result["valid"]) == expected);
			const Array errors = result["errors"];
			CHECK(errors.size() == typed.errors.size());
			for (int i = 0; i < errors.size() && i < typed.errors.size(); i++) {
				const Dictionary error = errors[i];
				CHECK(String(error["message"]) == typed.errors[i].message);
				CHECK(int(error["line"]) == typed.errors[i].line);
				CHECK(int(error["column"]) == typed.errors[i].column);
			}
			Ref<BaristaScript> script;
			script.instantiate();
			script->_set_source_code(source);
			CHECK(script->_is_valid() == expected);
		}
	}
}
