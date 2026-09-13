/**************************************************************************/
/*  namespace_annotation_analyzer_tests.cpp                               */
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

namespace {
const char *const corpus_root = "res://tests/corpus_staging/analyzer/";
void install(StorageFixture &storage, const String &path, const String &source) {
	BSParser syntax;
	BS_TEST_REQUIRE(syntax.parse(source, path, false) == OK);
	BSCache::set_source_override(path, source);
	auto record = BSDeclarationIndex::record_from_global_class(path, source, bs_resolve_global_class_from_source(source, path));
	record.namespace_name = syntax.get_tree()->namespace_name;
	for (const auto *annotation : syntax.get_tree()->annotation_declarations)
		record.global_annotations.push_back(annotation->qualified_name);
	BS_TEST_REQUIRE(storage.index().commit_record(storage.index().claim_refresh(path), record));
}
String block(const BSParser &parser) {
	String result;
	for (const auto &error : parser.get_errors()) {
		if (!result.is_empty())
			result += "\n";
		result += vformat(">> ERROR at line %d: %s", error.line, error.message);
	}
	if (parser.get_errors().is_empty()) {
		for (const auto &warning : parser.get_warnings()) {
			if (!result.is_empty())
				result += "\n";
			result += vformat("~~ WARNING at line %d: (%s) %s", warning.start_line, warning.get_name(), warning.get_message());
		}
	}
	return result.is_empty() ? String("BS_TEST_OK") : result;
}
void original_providers(StorageFixture &storage) {
	install(storage, String(corpus_root) + "errors/annotation_duplicate_in_imported_lib.notest.barista", R"source(namespace cafecito.dupsamefile

annotation twice targets METHOD
annotation twice targets CLASS
)source");
	install(storage, String(corpus_root) + "errors/issue_160_trait_type_annotation_ambiguous/issue_160_trait_ambiguous_alpha.notest.barista", R"source(namespace issue_160.alpha
trait_name Issue160AmbiguousTrait

var alpha_marker: int = 1
)source");
	install(storage, String(corpus_root) + "errors/issue_160_trait_type_annotation_ambiguous/issue_160_trait_ambiguous_beta.notest.barista", R"source(namespace issue_160.beta
trait_name Issue160AmbiguousTrait

var beta_marker: int = 1
)source");
	install(storage, String(corpus_root) + "errors/issue_70_namespace_ambiguous_first.notest.barista", R"source(namespace issue_70.ambiguous.first
class_name Issue70NamespaceAmbiguous
extends RefCounted
)source");
	install(storage, String(corpus_root) + "errors/issue_70_namespace_ambiguous_second.notest.barista", R"source(namespace issue_70.ambiguous.second
class_name Issue70NamespaceAmbiguous
extends RefCounted
)source");
	install(storage, String(corpus_root) + "errors/issue_95_trait_namespace_ambiguous/issue_95_trait_ambiguous_alpha.notest.barista", R"source(namespace issue_95.alpha
trait_name Issue95AmbiguousTrait

var alpha_marker: int = 1
)source");
	install(storage, String(corpus_root) + "errors/issue_95_trait_namespace_ambiguous/issue_95_trait_ambiguous_beta.notest.barista", R"source(namespace issue_95.beta
trait_name Issue95AmbiguousTrait

var beta_marker: int = 1
)source");
	install(storage, String(corpus_root) + "errors/issue_95_trait_namespace_unresolved/issue_95_trait_unimported.notest.barista", R"source(namespace issue_95.unresolved
trait_name Issue95Unimported

var marker: int = 1
)source");
	install(storage, String(corpus_root) + "features/issue_66_namespace_base.notest.barista", R"source(namespace issue_66.characters
class_name Issue66NamespaceBase
extends RefCounted

enum Role:
	HERO = 0
)source");
	install(storage, String(corpus_root) + "features/issue_66_namespace_controller.notest.barista", R"source(namespace issue_66.characters.controllers
class_name Issue66NamespaceController
extends RefCounted

enum State:
	IDLE = 0
)source");
	install(storage, String(corpus_root) + "features/issue_66_namespace_stats.notest.barista", R"source(namespace issue_66.shared
class_name Issue66NamespaceStats
extends RefCounted
)source");
	install(storage, String(corpus_root) + "features/issue_69_namespace_doc_controller.notest.barista", R"source(namespace issue_69_docs.game.characters.controllers
class_name Issue69DocPlayerController
extends RefCounted
)source");
	install(storage, String(corpus_root) + "features/issue_69_namespace_doc_deep_controller.notest.barista", R"source(namespace issue_69_docs.game.characters.controllers.deep
class_name Issue69DocDeepController
extends RefCounted
)source");
	install(storage, String(corpus_root) + "features/issue_69_namespace_doc_player.notest.barista", R"source(namespace issue_69_docs.game.characters
class_name Issue69DocPlayer
extends RefCounted
)source");
	install(storage, String(corpus_root) + "features/issue_70_namespace_base.notest.barista", R"source(namespace issue_70.characters
class_name Issue70NamespaceBase
extends RefCounted

enum Role:
	HERO = 0
)source");
	install(storage, String(corpus_root) + "features/issue_70_namespace_controller.notest.barista", R"source(namespace issue_70.characters.controllers
class_name Issue70NamespaceController
extends RefCounted

enum State:
	IDLE = 0
)source");
	install(storage, String(corpus_root) + "features/issue_70_namespace_native_name.notest.barista", R"source(namespace issue_70.native_conflict
class_name Node
extends RefCounted
)source");
	install(storage, String(corpus_root) + "features/issue_70_namespace_stats.notest.barista", R"source(namespace issue_70.shared
class_name Issue70NamespaceStats
extends RefCounted
)source");
	install(storage, String(corpus_root) + "features/issue_95_trait_namespace/issue_95_trait_damageable.notest.barista", R"source(namespace issue_95.combat
trait_name Issue95Damageable

var health: int = 100

func take_damage(amount: int) -> void:
	health -= amount
)source");
	install(storage, String(corpus_root) + "features/issue_95_trait_namespace/issue_95_trait_loggable.notest.barista", R"source(namespace issue_95.shared
trait_name Issue95Loggable

var log_count: int = 0

func record() -> void:
	log_count += 1
)source");
	install(storage, String(corpus_root) + "features/issue_95_trait_namespace/issue_95_trait_trackable.notest.barista", R"source(namespace issue_95.combat.controllers
trait_name Issue95Trackable

var tracked: bool = false
)source");
	install(storage, String(corpus_root) + "features/transitive_external_parser_lookup_mid.notest.barista", R"source(extends "transitive_external_parser_lookup_root.notest.barista"
)source");
	install(storage, String(corpus_root) + "features/transitive_external_parser_lookup_root.notest.barista", R"source(class LeafType:
	const MARK := "transitive-leaf"
)source");
}
void original_case(const String &name, const String &source, const String &expected) {
	StorageFixture storage;
	BSConformanceRegistry::ScopedCorpusState registry;
	original_providers(storage);
	const String warning_path = BSWarning::get_setting_path_from_code(BSWarning::UNSAFE_CAST);
	const Variant previous_warning = ProjectSettings::get_singleton()->get_setting(warning_path);
	struct RestoreWarning {
		String path;
		Variant value;
		~RestoreWarning() {
			ProjectSettings::get_singleton()->set_setting(path, value);
			BSParser::update_project_settings();
		}
	} restore_warning{ warning_path, previous_warning };
	ProjectSettings::get_singleton()->set_setting(warning_path, BSWarning::WARN);
	BSParser::update_project_settings();
	const String store = storage.path("before.bsgi");
	BS_TEST_REQUIRE(storage.index().flush(store) == OK);
	const auto before = read_bytes(store);
	const auto claim = storage.index().claim_refresh(storage.path("claim.barista"));
	for (int dependent = 0; dependent < 2; ++dependent) {
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, String(corpus_root) + name, false) == OK);
		BSAnalyzer analyzer(&parser);
		const Error result = analyzer.analyze();
		INFO(std::string(block(parser).utf8().get_data()));
		CHECK((result == OK) == !expected.begins_with(">> ERROR"));
		CHECK(block(parser) == expected);
	}
	CHECK(storage.index().get_refresh_revision(storage.path("claim.barista")) == claim);
	BS_TEST_REQUIRE(storage.index().flush(store) == OK);
	CHECK(read_bytes(store) == before);
}
} // namespace
TEST_SUITE("namespace_annotation_analyzer") {
	TEST_CASE("annotation_duplicate_in_imported_usage") {
		original_case("errors/annotation_duplicate_in_imported_usage.barista", R"source(import cafecito.dupsamefile

@twice
func test() -> void:
	pass
)source",
				R"source(>> ERROR at line 3: Ambiguous annotation "@twice": the canonical identity "cafecito.dupsamefile.twice" has multiple declarations.)source");
	}
	TEST_CASE("issue_160_trait_ambiguous") {
		original_case("errors/issue_160_trait_type_annotation_ambiguous/issue_160_trait_ambiguous.barista", R"source(import issue_160.alpha
import issue_160.beta

extends RefCounted

var target: Issue160AmbiguousTrait

func test() -> void:
	pass
)source",
				R"source(>> ERROR at line 6: Could not resolve type "Issue160AmbiguousTrait": imported namespaces "issue_160.alpha" and "issue_160.beta" are ambiguous.)source");
	}
	TEST_CASE("issue_70_namespace_ambiguous_import") {
		original_case("errors/issue_70_namespace_ambiguous_import.barista", R"source(import issue_70.ambiguous.first
import issue_70.ambiguous.second

var target: Issue70NamespaceAmbiguous

func test() -> void:
	pass
)source",
				R"source(>> ERROR at line 4: Could not resolve type "Issue70NamespaceAmbiguous": imported namespaces "issue_70.ambiguous.first" and "issue_70.ambiguous.second" are ambiguous.)source");
	}
	TEST_CASE("issue_70_namespace_non_recursive_import") {
		original_case("errors/issue_70_namespace_non_recursive_import.barista", R"source(import issue_70.characters

var controller: Issue70NamespaceController

func test() -> void:
	pass
)source",
				R"source(>> ERROR at line 3: Could not find type "Issue70NamespaceController" in the current scope.)source");
	}
	TEST_CASE("issue_95_trait_ambiguous") {
		original_case("errors/issue_95_trait_namespace_ambiguous/issue_95_trait_ambiguous.barista", R"source(import issue_95.alpha
import issue_95.beta

extends RefCounted
uses Issue95AmbiguousTrait

func test() -> void:
	pass
)source",
				R"source(>> ERROR at line 5: Could not resolve trait "Issue95AmbiguousTrait": imported namespaces "issue_95.alpha" and "issue_95.beta" are ambiguous.)source");
	}
	TEST_CASE("issue_95_trait_unresolved") {
		original_case("errors/issue_95_trait_namespace_unresolved/issue_95_trait_unresolved.barista", R"source(extends RefCounted
uses Issue95Unimported

func test() -> void:
	pass
)source",
				R"source(>> ERROR at line 2: Could not resolve trait "Issue95Unimported".)source");
	}
	TEST_CASE("issue_66_namespace_resolution.norun") {
		original_case("features/issue_66_namespace_resolution.norun.barista", R"source(namespace issue_66.characters
import issue_66.characters
import issue_66.shared

var same_namespace: Issue66NamespaceBase
var imported: Issue66NamespaceStats
var child_namespace: controllers.Issue66NamespaceController
var fully_qualified: issue_66.characters.Issue66NamespaceBase
var same_namespace_role: Issue66NamespaceBase.Role
var child_namespace_state: controllers.Issue66NamespaceController.State
var fully_qualified_role: issue_66.characters.Issue66NamespaceBase.Role

func require_base(_value: Issue66NamespaceBase) -> void:
	pass

func require_stats(_value: Issue66NamespaceStats) -> void:
	pass

func require_controller(_value: controllers.Issue66NamespaceController) -> void:
	pass

func require_role(_value: Issue66NamespaceBase.Role) -> void:
	pass

func require_state(_value: controllers.Issue66NamespaceController.State) -> void:
	pass
)source",
				R"source(BS_TEST_OK)source");
	}
	TEST_CASE("issue_69_namespace_doc_example.norun") {
		original_case("features/issue_69_namespace_doc_example.norun.barista", R"source(import issue_69_docs.game.characters

var player: Issue69DocPlayer
var controller: controllers.Issue69DocPlayerController
var deep_controller: controllers.deep.Issue69DocDeepController
)source",
				R"source(BS_TEST_OK)source");
	}
	TEST_CASE("issue_70_namespace_resolution.norun") {
		original_case("features/issue_70_namespace_resolution.norun.barista", R"source(namespace issue_70.characters
import issue_70.characters
import issue_70.shared
import issue_70.native_conflict

var same_namespace: Issue70NamespaceBase
var fully_qualified: issue_70.characters.Issue70NamespaceBase
var imported: Issue70NamespaceStats
var child_namespace: controllers.Issue70NamespaceController
var same_namespace_role: Issue70NamespaceBase.Role
var child_namespace_state: controllers.Issue70NamespaceController.State

func require_base(_value: Issue70NamespaceBase) -> void:
	pass

func require_stats(_value: Issue70NamespaceStats) -> void:
	pass

func require_controller(_value: controllers.Issue70NamespaceController) -> void:
	pass

func require_native_node(value: Node) -> int:
	return value.get_child_count()
)source",
				R"source(BS_TEST_OK)source");
	}
	TEST_CASE("issue_95_trait_namespace_resolution.norun") {
		original_case("features/issue_95_trait_namespace/issue_95_trait_namespace_resolution.norun.barista", R"source(namespace issue_95.combat
import issue_95.shared

extends RefCounted
uses Issue95Damageable, Issue95Loggable, controllers.Issue95Trackable

var same_namespace: Issue95Damageable
var imported_short: Issue95Loggable
var fully_qualified: issue_95.shared.Issue95Loggable
var child_namespace: controllers.Issue95Trackable
var fully_qualified_child: issue_95.combat.controllers.Issue95Trackable
var typed_container: Array[Issue95Damageable] = []
var qualified_container: Array[issue_95.shared.Issue95Loggable] = []

func require_damageable(_value: Issue95Damageable) -> void:
	pass

func require_loggable(_value: issue_95.shared.Issue95Loggable) -> void:
	pass

func provide_damageable() -> Issue95Damageable:
	return null

func provide_loggable() -> issue_95.shared.Issue95Loggable:
	return null

func require_trackable(_value: controllers.Issue95Trackable) -> void:
	pass

func test() -> void:
	var value: Variant = self
	if value is Issue95Damageable:
		var _typed: Issue95Damageable = value
	var _cast := value as issue_95.shared.Issue95Loggable
	take_damage(5)
	record()
)source",
				R"source(~~ WARNING at line 34: (UNSAFE_CAST) Casting "Variant" to "Issue95Loggable" is unsafe.)source");
	}
	TEST_CASE("transitive_external_parser_lookup") {
		original_case("features/transitive_external_parser_lookup.barista", R"source(# Regression for #732: analysis in the leaf script must find symbols owned by a parser two
# extends hops away (recursive `find_cached_external_parser_for_class`).
extends "transitive_external_parser_lookup_mid.notest.barista"


func use_leaf(p: LeafType) -> void:
	print(p.MARK)


func test() -> void:
	var leaf := LeafType.new()
	use_leaf(leaf)
	print(leaf.MARK)
)source",
				R"source(BS_TEST_OK)source");
	}
}

namespace {
void check_source(const String &source, const String &expected = "BS_TEST_OK", const String &path = "res://tests/names/consumer.barista") {
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
	BSAnalyzer analyzer(&parser);
	CHECK((analyzer.analyze() == OK) == !expected.begins_with(">> ERROR"));
	INFO(std::string(block(parser).utf8().get_data()));
	CHECK(block(parser) == expected);
}
} //namespace
TEST_SUITE("namespace_annotation_analyzer") {
	TEST_CASE("relative_heads_keep_own_import_exact_and_nested_identity") {
		StorageFixture storage;
		for (const String &ns : { String("a"), String("b"), String("controllers") }) {
			install(storage, "res://tests/names/" + ns + ".barista", "namespace " + ns + ".controllers\nclass_name Item\nenum State:\n\tIDLE = 0\nclass Nested:\n\tpass\n");
		}
		for (const String &source : {
					 String("namespace a\nimport b\nvar x: controllers.Item\nvar e: controllers.Item.State\nvar n: controllers.Item.Nested\n"),
					 String("import b\nimport b\nvar x: controllers.Item\nvar e: controllers.Item.State\n"),
					 String("namespace a\nimport b\nvar x: b.controllers.Item\n") }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, "res://tests/names/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			BS_TEST_REQUIRE(analyzer.analyze() == OK);
			const auto type = parser.get_tree()->get_member("x").get_datatype();
			CHECK(type.kind == BSParser::DataType::CLASS);
			CHECK(type.script_path == (source.begins_with("namespace a\nimport b\nvar x: controllers") ? "res://tests/names/a.barista" : "res://tests/names/b.barista"));
			if (parser.get_tree()->has_member("e")) {
				const auto e = parser.get_tree()->get_member("e").get_datatype();
				CHECK(e.kind == BSParser::DataType::ENUM);
				CHECK(e.script_path == type.script_path);
			}
		}
		install(storage, "res://tests/names/exact.barista", "namespace controllers\nclass_name Item\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("namespace a\nimport b\nvar x: controllers.Item\n", "res://tests/names/exact_consumer.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		BS_TEST_REQUIRE(analyzer.analyze() == OK);
		CHECK(parser.get_tree()->get_member("x").get_datatype().script_path == "res://tests/names/exact.barista");
	}
	TEST_CASE("relative_ambiguity_is_complete_unique_sorted_and_nonrecursive") {
		StorageFixture storage;
		for (const String &ns : { String("a"), String("b"), String("c") })
			install(storage, "res://tests/names/" + ns + ".barista", "namespace " + ns + ".child\nclass_name Item\n");
		for (const String &imports : { String("import c\nimport a\nimport b\nimport a\n"), String("import b\nimport c\nimport a\nimport c\n") }) {
			check_source(imports + String("var x: child.Item\n"), ">> ERROR at line 5: Could not resolve type \"child.Item\": imported namespaces \"a\", \"b\" and \"c\" are ambiguous.");
		}
		check_source("import a\nvar x: Item\n", ">> ERROR at line 2: Could not find type \"Item\" in the current scope.");
	}
	TEST_CASE("relative_namespace_root_preserves_lexical_precedence") {
		StorageFixture storage;
		install(storage, "res://tests/names/item.barista", "namespace a.controllers\nclass_name Item\n");
		check_source("import a\nclass controllers:\n\tclass Item:\n\t\tpass\nvar x: controllers.Item\n");
		check_source("import a\nconst controllers = 7\nvar x: controllers.Item\n", ">> ERROR at line 3: \"controllers\" is a constant but does not contain a type.");
	}
	TEST_CASE("native_names_remain_native_with_own_and_imported_collisions") {
		StorageFixture storage;
		install(storage, "res://tests/names/node.barista", "namespace a\nclass_name Node\nextends RefCounted\n");
		for (const String &context : { String("namespace a\n"), String("import a\n") }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(context + String("var x: Node\nvar y = Node\n"), "res://tests/names/native.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			BS_TEST_REQUIRE(analyzer.analyze() == OK);
			for (const StringName &name : { StringName("x"), StringName("y") }) {
				const auto type = parser.get_tree()->get_member(name).get_datatype();
				CHECK(type.kind == BSParser::DataType::NATIVE);
				CHECK(type.native_type == StringName("Node"));
				CHECK(type.is_meta_type == (name == StringName("y")));
			}
		}
	}
	TEST_CASE("annotation_duplicate_interface_and_full_analysis_have_distinct_diagnostics") {
		StorageFixture storage;
		const String path = "res://tests/names/duplicate.barista";
		const String source = "namespace a\nannotation mark targets METHOD\nannotation mark targets CLASS\n";
		install(storage, path, source);
		Error err = OK;
		Ref<BSParserRef> provider = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, err);
		BS_TEST_REQUIRE(provider.is_valid());
		CHECK(err == OK);
		CHECK(provider->get_status() == BSParserRef::INTERFACE_SOLVED);
		CHECK(provider->get_parser()->get_errors().is_empty());
		for (int i = 0; i < 2; ++i)
			check_source("import a\n@mark\nfunc test():\n\tpass\n", ">> ERROR at line 2: Ambiguous annotation \"@mark\": the canonical identity \"a.mark\" has multiple declarations.", "res://tests/names/consumer" + itos(i) + ".barista");
		check_source(source, ">> ERROR at line 3: Duplicate annotation declaration \"a.mark\".", path);
	}
	TEST_CASE("annotation_valid_unknown_ambiguous_and_own_provider_controls") {
		StorageFixture storage;
		install(storage, "res://tests/names/a.barista", "namespace a\nannotation mark targets METHOD\n");
		install(storage, "res://tests/names/b.barista", "namespace b\nannotation mark targets METHOD\n");
		check_source("import a\nimport a\n@mark\nfunc test():\n\tpass\n");
		check_source("namespace a\nimport b\n@mark\nfunc test():\n\tpass\n");
		check_source("@a.mark\nfunc test():\n\tpass\n");
		check_source("import a\n@missing\nfunc test():\n\tpass\n", ">> ERROR at line 2: Unknown annotation \"@missing\". Custom annotations must be declared in the current namespace or an imported namespace.");
		for (const String &imports : { String("import a\nimport b\n"), String("import b\nimport a\n") })
			check_source(imports + String("@mark\nfunc test():\n\tpass\n"), ">> ERROR at line 3: Ambiguous annotation \"@mark\": it is declared in imported namespaces \"a\" and \"b\".");
	}
	TEST_CASE("annotation_failed_interface_does_not_bind_but_later_body_failure_keeps_signature") {
		StorageFixture storage;
		const String path = "res://tests/names/provider.barista";
		for (bool bad_interface : { true, false }) {
			BSCache::remove_script(path);
			install(storage, path, bad_interface ? "namespace a\nannotation mark(value: Missing) targets METHOD\n" : "namespace a\nannotation mark targets METHOD\nfunc broken() -> int:\n\treturn \"bad\"\n");
			Error err = OK;
			Ref<BSParserRef> provider = BSCache::get_parser(path, BSParserRef::FULLY_SOLVED, err);
			BS_TEST_REQUIRE(provider.is_valid());
			CHECK(err != OK);
			const String owner_errors = block(*provider->get_parser());
			CHECK(provider->get_result_for_status(BSParserRef::INTERFACE_SOLVED) == (bad_interface ? ERR_PARSE_ERROR : OK));
			for (int i = 0; i < 2; ++i) {
				check_source("import a\n@mark\nfunc test():\n\tpass\n", bad_interface ? ">> ERROR at line 2: Unknown annotation \"@mark\". Custom annotations must be declared in the current namespace or an imported namespace." : "BS_TEST_OK", "res://tests/names/consumer" + itos(i) + ".barista");
				CHECK(block(*provider->get_parser()) == owner_errors);
			}
		}
	}
	TEST_CASE("relative_selected_stale_generation_fails_until_explicit_repair") {
		StorageFixture storage;
		const String path = "res://tests/names/provider.barista";
		const String original = "namespace a.child\nclass_name Item\n";
		install(storage, path, original);
		check_source("import a\nvar x: child.Item\n");
		const auto revision = storage.index().get_refresh_revision(path);
		const String store = storage.path("before.bsgi");
		BS_TEST_REQUIRE(storage.index().flush(store) == OK);
		const auto before = read_bytes(store);
		const String changed = "namespace a.child\nclass_name Changed\n";
		BSCache::set_source_override(path, changed);
		for (int i = 0; i < 2; ++i)
			check_source("import a\nvar x: child.Item\n", ">> ERROR at line 2: Could not resolve type \"child.Item\": declaration \"a.child.Item\" from \"res://tests/names/provider.barista\" is stale or invalid.");
		CHECK(storage.index().get_refresh_revision(path) == revision);
		BS_TEST_REQUIRE(storage.index().flush(store) == OK);
		CHECK(read_bytes(store) == before);
		BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, changed);
		CHECK(BSCache::get_source_code(path) == changed);
		CHECK(storage.index().get_refresh_revision(path) > revision);
		check_source("import a\nvar x: child.Changed\n");
	}
	TEST_CASE("transitive_nested_annotation_keeps_retained_owner_across_refresh") {
		StorageFixture storage;
		const String root = "res://tests/names/root.barista", mid = "res://tests/names/mid.barista";
		install(storage, root, "class LeafType:\n\tconst MARK = 1\n");
		install(storage, mid, "extends \"root.barista\"\n");
		BSParser old;
		BS_TEST_REQUIRE(old.parse("extends \"mid.barista\"\nvar leaf: LeafType\n", "res://tests/names/old.barista", false) == OK);
		BSAnalyzer old_analyzer(&old);
		BS_TEST_REQUIRE(old_analyzer.analyze() == OK);
		auto *old_leaf = old.get_tree()->get_member("leaf").get_datatype().class_type;
		BS_TEST_REQUIRE(old_leaf != nullptr);
		BSCache::set_source_override(root, "class LeafType:\n\tconst MARK = \"changed\"\n");
		BSCache::prepare_semantic_refresh(root);
		BSParser fresh;
		BS_TEST_REQUIRE(fresh.parse("extends \"mid.barista\"\nvar leaf: LeafType\n", "res://tests/names/fresh.barista", false) == OK);
		BSAnalyzer fresh_analyzer(&fresh);
		BS_TEST_REQUIRE(fresh_analyzer.analyze() == OK);
		auto *fresh_leaf = fresh.get_tree()->get_member("leaf").get_datatype().class_type;
		BS_TEST_REQUIRE(fresh_leaf != nullptr);
		CHECK(old_leaf != fresh_leaf);
		CHECK(old_leaf->get_member("MARK").constant->initializer->reduced_value == Variant(1));
		CHECK(fresh_leaf->get_member("MARK").constant->initializer->reduced_value == Variant("changed"));
		CHECK(old.get_tree()->get_member("leaf").get_datatype().class_type == old_leaf);
	}
}
TEST_SUITE("namespace_annotation_analyzer") {
	TEST_CASE("crossfile_annotation_duplicate_has_distinct_canonical_diagnostic") {
		StorageFixture storage;
		for (const String &file : { String("one"), String("two") })
			install(storage, "res://tests/names/" + file + ".barista", "namespace a\nannotation mark targets METHOD\n");
		check_source("import a\n@mark\nfunc test():\n\tpass\n", ">> ERROR at line 2: Ambiguous annotation \"@mark\": the canonical identity \"a.mark\" is declared in multiple files.");
	}
	TEST_CASE("relative_wrong_kind_trait_does_not_fall_through_to_import") {
		StorageFixture storage;
		install(storage, "res://tests/names/own.barista", "namespace a.child\nclass_name T\n");
		install(storage, "res://tests/names/imported.barista", "namespace b.child\ntrait_name T\n");
		check_source("namespace a\nimport b\nuses child.T\n", ">> ERROR at line 3: \"child.T\" is not a trait.");
	}
	TEST_CASE("inherited_nested_head_precedes_imported_global_and_restores_state") {
		const auto scenario = []() {
			StorageFixture storage;
			install(storage, "res://tests/names/base.barista", "class LeafType:\n\tconst MARK = 1\n");
			install(storage, "res://tests/names/imported.barista", "namespace a\nclass_name LeafType\nconst MARK = \"wrong\"\n");
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("import a\nextends \"base.barista\"\nvar x: LeafType\n", "res://tests/names/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			BS_TEST_REQUIRE(analyzer.analyze() == OK);
			const auto type = parser.get_tree()->get_member("x").get_datatype();
			CHECK(type.script_path == "res://tests/names/base.barista");
		};
		CHECK(verify_case_isolation(scenario));
	}
}

namespace {
void one_error(const BSParser &parser, const String &message, const BSParser::Node *site) {
	INFO(std::string(block(parser).utf8().get_data()));
	BS_TEST_REQUIRE(parser.get_errors().size() == 1);
	const auto &error = parser.get_errors().front()->get();
	CHECK(error.message == message);
	CHECK(error.line == site->start_line);
	CHECK(error.column == site->start_column);
	CHECK(error.end_line == site->end_line);
	CHECK(error.end_column == site->end_column);
}
void materialization_failure(bool nested) {
	StorageFixture storage;
	const String path = "res://tests/f2/item.barista";
	install(storage, path, "namespace a.child\nclass_name Item\nextends MissingBase\nclass Nested:\n\tpass\n");
	Error err = OK;
	Ref<BSParserRef> retained = BSCache::get_parser(path, BSParserRef::PARSED, err);
	BS_TEST_REQUIRE(retained.is_valid());
	BS_TEST_REQUIRE(err == OK);
	CHECK(retained->get_status() == BSParserRef::PARSED);
	const String store = storage.path("before.bsgi");
	BS_TEST_REQUIRE(storage.index().flush(store) == OK);
	const auto before = read_bytes(store);
	const auto generation = storage.index().get_refresh_revision(path);
	const String type = nested ? "child.Item.Nested" : "child.Item";
	for (int consumer = 0; consumer < 3; ++consumer) {
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("import a\nvar x: " + type + "\n", "res://tests/f2/consumer" + itos(consumer) + ".barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		one_error(parser, consumer == 0 ? "Could not resolve type \"a.child.Item\" from \"res://tests/f2/item.barista\"." : "Could not resolve type \"" + String("child.Item") + "\": provider \"res://tests/f2/item.barista\" could not be parsed.", parser.get_tree()->get_member("x").variable->datatype_specifier);
	}
	BS_TEST_REQUIRE(storage.index().flush(store) == OK);
	CHECK(read_bytes(store) == before);
	CHECK(storage.index().get_refresh_revision(path) == generation);
	const String old_errors = block(*retained->get_parser());
	const String repaired = "namespace a.child\nclass_name Item\nextends RefCounted\nclass Nested:\n\tpass\n";
	BSCache::set_source_override(path, repaired);
	BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, repaired);
	CHECK(BSCache::get_source_code(path) == repaired);
	CHECK(storage.index().get_refresh_revision(path) > generation);
	check_source("import a\nvar x: " + type + "\n");
	CHECK(block(*retained->get_parser()) == old_errors);
	Ref<BSParserRef> fresh = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, err);
	BS_TEST_REQUIRE(fresh.is_valid());
	CHECK(fresh != retained);
	CHECK(err == OK);
}
} //namespace
TEST_SUITE("namespace_annotation_analyzer") {
	TEST_CASE("repair1_variable_root_is_terminal_before_relative_namespace") {
		StorageFixture storage;
		install(storage, "res://tests/f1/item.barista", "namespace a.child\nclass_name Item\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("import a\nvar child: int\nvar x: child.Item\n", "res://tests/f1/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		one_error(parser, "\"child\" is a variable but does not contain a type.", parser.get_tree()->get_member("x").variable->datatype_specifier);
		check_source("import a\nclass Owner:\n\tclass Item:\n\t\tpass\nconst child = Owner\nvar x: child.Item\n");
	}
	TEST_CASE("repair1_dotted_trait_claims_local_and_inherited_roots") {
		StorageFixture storage;
		install(storage, "res://tests/f1/trait.barista", "namespace a.child\ntrait_name T\n");
		for (bool wrong_class : { false, true }) {
			BSParser parser;
			const String source = wrong_class ? "import a\nuses child.T\nclass child:\n\tclass T:\n\t\tpass\n" : "import a\nuses child.T\nconst child = 7\n";
			BS_TEST_REQUIRE(parser.parse(source, "res://tests/f1/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			one_error(parser, wrong_class ? "Class \"T\" cannot be used as a trait." : "Cannot use constant \"child\" as a trait.", parser.get_tree()->used_traits[0].name[0]);
		}
		for (bool inherited : { false, true }) {
			const String declaration = "class child:\n\ttrait T:\n\t\tvar marker: int = 7\n";
			install(storage, "res://tests/f1/base.barista", declaration);
			const String source = inherited ? "import a\nextends \"base.barista\"\nuses child.T\n" : String("import a\nuses child.T\n") + declaration;
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, "res://tests/f1/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			BS_TEST_REQUIRE(analyzer.analyze() == OK);
			auto *trait = parser.get_tree()->used_traits[0].resolved_trait;
			BS_TEST_REQUIRE(trait != nullptr);
			CHECK(trait->has_member("marker"));
			CHECK(trait->get_datatype().script_path == (inherited ? "res://tests/f1/base.barista" : "res://tests/f1/consumer.barista"));
		}
	}
	TEST_CASE("repair1_inherited_compound_tails_keep_root_identity_with_namespace_competitors") {
		StorageFixture storage;
		const String root = "res://tests/f1/root.barista", mid = "res://tests/f1/mid.barista";
		install(storage, root, "class LeafType:\n\tclass Nested:\n\t\tpass\n\tenum Kind:\n\t\tVALUE = 0\n");
		install(storage, mid, "extends \"res://tests/f1/root.barista\"\n");
		for (bool collision : { false, true }) {
			if (collision) {
				install(storage, "res://tests/f1/competing_nested.barista", "namespace a.LeafType\nclass_name Nested\n");
				install(storage, "res://tests/f1/competing_kind.barista", "namespace a.LeafType\nclass_name Kind\n");
			}
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(String(collision ? "import a\n" : "") + "extends \"res://tests/f1/mid.barista\"\n" + "var x: LeafType.Nested\nvar e: LeafType.Kind\n", "res://tests/f1/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			const auto x = parser.get_tree()->get_member("x").get_datatype(), e = parser.get_tree()->get_member("e").get_datatype();
			CHECK(x.kind == BSParser::DataType::CLASS);
			CHECK(x.script_path == root);
			CHECK(e.kind == BSParser::DataType::ENUM);
			CHECK(e.script_path == root);
			Error err = OK;
			auto provider = BSCache::get_parser(root, BSParserRef::INTERFACE_SOLVED, err);
			BS_TEST_REQUIRE(provider.is_valid());
			BS_TEST_REQUIRE(err == OK);
			auto *leaf = provider->get_parser()->get_tree()->get_member("LeafType").m_class;
			CHECK(x.class_type == leaf->get_member("Nested").m_class);
			CHECK(e.enum_type == leaf->get_member("Kind").get_datatype().enum_type);
		}
	}
	TEST_CASE("repair1_native_compound_root_is_terminal_before_relative_namespace") {
		StorageFixture storage;
		install(storage, "res://tests/f1/native_collision.barista", "namespace a.Node\nclass_name Missing\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("import a\nvar x: Node.Missing\n", "res://tests/f1/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		one_error(parser, "Could not find type \"Missing\" in \"Node\".", parser.get_tree()->get_member("x").variable->datatype_specifier->type_chain[1]);
		BSParser good;
		BS_TEST_REQUIRE(good.parse("import a\nvar x: Node.ProcessMode\nvar y: a.Node.Missing\n", "res://tests/f1/good.barista", false) == OK);
		BSAnalyzer good_analyzer(&good);
		BS_TEST_REQUIRE(good_analyzer.analyze() == OK);
		CHECK(good.get_tree()->get_member("x").get_datatype().kind == BSParser::DataType::ENUM);
		CHECK(good.get_tree()->get_member("x").get_datatype().enum_type == StringName("Node.ProcessMode"));
		CHECK(good.get_tree()->get_member("y").get_datatype().script_path == "res://tests/f1/native_collision.barista");
	}
	TEST_CASE("repair1_whole_chain_provider_materialization_failure_is_terminal") { materialization_failure(false); }
	TEST_CASE("repair1_shorter_prefix_provider_materialization_failure_is_terminal") { materialization_failure(true); }
}
TEST_SUITE("namespace_annotation_analyzer") {
	TEST_CASE("repair1_compound_alias_keeps_lexical_visibility") {
		StorageFixture storage;
		install(storage, "res://tests/f1/item.barista", "namespace a.child\nclass_name Item\n");
		check_source("import a\nclass Owner:\n\tclass Item:\n\t\tpass\ntype child = Owner\nvar x: child.Item\n");
		install(storage, "res://tests/f1/alias_base.barista", "class Owner:\n\tclass Item:\n\t\tpass\ntype child = Owner\n");
		check_source("import a\nextends \"res://tests/f1/alias_base.barista\"\nvar x: child.Item\n", ">> ERROR at line 3: Type alias \"child\" is not in scope here. A type alias is visible only inside the file and the body that declare it, so it is neither inherited nor imported.");
	}
	TEST_CASE("repair1_compound_inherited_owner_survives_new_generation_and_restores_state") {
		const auto scenario = []() {
			StorageFixture storage;
			const String root = "res://tests/f1/root.barista", mid = "res://tests/f1/mid.barista";
			const String original = "class LeafType:\n\tclass Nested:\n\t\tpass\n\tenum Kind:\n\t\tVALUE = 0\n";
			install(storage, root, original);
			install(storage, mid, "extends \"root.barista\"\n");
			const String store = storage.path("before.bsgi");
			BS_TEST_REQUIRE(storage.index().flush(store) == OK);
			const auto before = read_bytes(store);
			const auto generation = storage.index().get_refresh_revision(root);
			BSParser old;
			BS_TEST_REQUIRE(old.parse("extends \"mid.barista\"\nvar x: LeafType.Nested\nvar e: LeafType.Kind\n", "res://tests/f1/old.barista", false) == OK);
			BSAnalyzer old_analyzer(&old);
			BS_TEST_REQUIRE(old_analyzer.analyze() == OK);
			const auto old_x = old.get_tree()->get_member("x").get_datatype(), old_e = old.get_tree()->get_member("e").get_datatype();
			CHECK(storage.index().get_refresh_revision(root) == generation);
			BS_TEST_REQUIRE(storage.index().flush(store) == OK);
			CHECK(read_bytes(store) == before);
			const String changed = original.replace("VALUE = 0", "VALUE = 1");
			BSCache::set_source_override(root, changed);
			BSCache::prepare_semantic_refresh(root);
			BSParser fresh;
			BS_TEST_REQUIRE(fresh.parse("extends \"mid.barista\"\nvar x: LeafType.Nested\nvar e: LeafType.Kind\n", "res://tests/f1/fresh.barista", false) == OK);
			BSAnalyzer fresh_analyzer(&fresh);
			BS_TEST_REQUIRE(fresh_analyzer.analyze() == OK);
			const auto fresh_x = fresh.get_tree()->get_member("x").get_datatype(), fresh_e = fresh.get_tree()->get_member("e").get_datatype();
			CHECK(fresh_x.class_type != old_x.class_type);
			CHECK(fresh_x.script_path == root);
			CHECK(fresh_e.kind == BSParser::DataType::ENUM);
			CHECK(fresh_e.enum_type == old_e.enum_type);
			BS_TEST_REQUIRE(old_e.enum_values.has("VALUE"));
			BS_TEST_REQUIRE(fresh_e.enum_values.has("VALUE"));
			CHECK(old_e.enum_values["VALUE"] == 0);
			CHECK(fresh_e.enum_values["VALUE"] == 1);
			CHECK(old.get_tree()->get_member("x").get_datatype().class_type == old_x.class_type);
		};
		CHECK(verify_case_isolation(scenario));
	}
}
TEST_SUITE("namespace_annotation_analyzer") {
	TEST_CASE("repair2_intrinsic_compound_roots_claim_missing_tail") {
		for (const String root : { String("Vector2"), String("Variant"), String("Number"), String("AsyncCallable"), String("Self") }) {
			StorageFixture storage;
			install(storage, "res://tests/f1_intrinsic/missing.barista", "namespace a." + root + "\nclass_name Missing\n");
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("import a\nvar x: " + root + ".Missing\n", "res://tests/f1_intrinsic/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			const String message = root == "Number" || root == "Self" ? "Type \"" + root + "\" does not contain nested types." : "Name \"Missing\" is not a nested type of \"" + root + "\".";
			one_error(parser, message, parser.get_tree()->get_member("x").variable->datatype_specifier->type_chain[1]);
		}
	}
	TEST_CASE("repair2_intrinsic_enum_tails_remain_terminal") {
		for (const String root : { String("Vector2.Axis"), String("Variant.Type") }) {
			StorageFixture storage;
			install(storage, "res://tests/f1_intrinsic/deep.barista", "namespace a." + root + "\nclass_name Missing\n");
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("import a\nvar x: " + root + ".Missing\n", "res://tests/f1_intrinsic/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			one_error(parser, root == "Variant.Type" ? "Variant only contains enum types, which do not have nested types." : "Built-in types only contain enum types, which do not have nested types.", parser.get_tree()->get_member("x").variable->datatype_specifier->type_chain[2]);
		}
	}
	TEST_CASE("repair2_intrinsic_enums_and_qualified_namespace_opposites") {
		StorageFixture storage;
		install(storage, "res://tests/f1_intrinsic/missing.barista", "namespace a.Vector2\nclass_name Missing\n");
		install(storage, "res://tests/f1_intrinsic/item.barista", "namespace a.child\nclass_name Item\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("import a\nvar vector: Vector2.Axis\nvar variant: Variant.Type\nvar native: Node.ProcessMode\nvar qualified: a.Vector2.Missing\nvar relative: child.Item\n", "res://tests/f1_intrinsic/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		BS_TEST_REQUIRE(analyzer.analyze() == OK);
		for (const String name : { String("vector"), String("variant"), String("native") }) {
			const auto type = parser.get_tree()->get_member(name).get_datatype();
			CHECK(type.kind == BSParser::DataType::ENUM);
			CHECK(type.enum_type == StringName(name == "vector" ? "Vector2.Axis" : name == "variant" ? "Variant.Type"
																									 : "Node.ProcessMode"));
		}
		CHECK(parser.get_tree()->get_member("qualified").get_datatype().script_path == "res://tests/f1_intrinsic/missing.barista");
		CHECK(parser.get_tree()->get_member("relative").get_datatype().script_path == "res://tests/f1_intrinsic/item.barista");
	}
	TEST_CASE("repair2_invisible_inherited_alias_continues_to_visible_enclosing_root") {
		for (bool alias : { false, true }) {
			StorageFixture storage;
			install(storage, "res://tests/f1_alias/base.barista", "class HiddenOwner:\n\tclass Item:\n\t\tpass\ntype child = HiddenOwner\n");
			install(storage, "res://tests/f1_alias/competitor.barista", "namespace a.child\nclass_name Item\n");
			BSParser parser;
			const String declaration = alias ? "class VisibleOwner:\n\tclass Item:\n\t\tpass\ntype child = VisibleOwner\n" : "class child:\n\tclass Item:\n\t\tpass\n";
			BS_TEST_REQUIRE(parser.parse(String("import a\n") + declaration + "class Derived extends \"res://tests/f1_alias/base.barista\":\n\tvar x: child.Item\n", "res://tests/f1_alias/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			const Error status = analyzer.analyze();
			CHECK(status == OK);
			if (status != OK) {
				continue;
			}
			auto *owner = parser.get_tree()->get_member(alias ? "VisibleOwner" : "child").m_class;
			auto *derived = parser.get_tree()->get_member("Derived").m_class;
			const auto type = derived->get_member("x").get_datatype();
			CHECK(type.kind == BSParser::DataType::CLASS);
			CHECK(type.class_type == owner->get_member("Item").m_class);
			CHECK(type.script_path == "res://tests/f1_alias/consumer.barista");
		}
	}
	TEST_CASE("repair2_private_only_alias_does_not_leak_to_imported_namespace") {
		StorageFixture storage;
		install(storage, "res://tests/f1_alias/base.barista", "class HiddenOwner:\n\tclass Item:\n\t\tpass\ntype child = HiddenOwner\n");
		install(storage, "res://tests/f1_alias/competitor.barista", "namespace a.child\nclass_name Item\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("import a\nclass Derived extends \"res://tests/f1_alias/base.barista\":\n\tvar x: child.Item\n", "res://tests/f1_alias/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		one_error(parser, "Type alias \"child\" is not in scope here. A type alias is visible only inside the file and the body that declare it, so it is neither inherited nor imported.", parser.get_tree()->get_member("Derived").m_class->get_member("x").variable->datatype_specifier);
	}
}
TEST_SUITE("namespace_annotation_analyzer") {
	TEST_CASE("repair2_selected_visible_alias_failure_is_terminal") {
		StorageFixture storage;
		install(storage, "res://tests/f1_alias/competitor.barista", "namespace a.child\nclass_name Item\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("import a\ntype child = child\nvar x: child.Item\n", "res://tests/f1_alias/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		one_error(parser, "Type alias \"child\" -> \"child\" expands to itself, so it names no type.", parser.get_tree()->get_member("child").type_alias);
		CHECK(parser.get_tree()->get_member("x").get_datatype().is_variant());
	}
}
