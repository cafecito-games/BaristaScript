/**************************************************************************/
/*  provider_phase_analyzer_tests.cpp                                  */
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

namespace barista_script {
struct ProviderPhaseTestAccess {
	static void inheritance(BSAnalyzer &analyzer, BSParser::ClassNode *node) { analyzer.resolve_class_inheritance(node); }
	static void body(BSAnalyzer &analyzer, BSParser::ClassNode *node) { analyzer.analyze_class_body(node); }
};
} //namespace barista_script
namespace {
// Immutable Foundry c9d5e35 originals; only resource paths and extensions are projected.
const char *const corpus_root = "res://tests/corpus_staging/analyzer/";
void install(StorageFixture &storage, const String &path, const String &source) {
	BSParser syntax;
	BS_TEST_REQUIRE(syntax.parse(source, path, false) == OK);
	BSCache::set_source_override(path, source);
	const auto record = BSDeclarationIndex::record_from_global_class(path, source, bs_resolve_global_class_from_source(source, path));
	BS_TEST_REQUIRE(storage.index().commit_record(storage.index().claim_refresh(path), record));
}
String block(const BSParser &parser) {
	String result;
	for (const auto &error : parser.get_errors()) {
		if (!result.is_empty())
			result += "\n";
		result += vformat(">> ERROR at line %d: %s", error.line, error.message);
	}
	return result.is_empty() ? String("BS_TEST_OK") : result;
}
void original_providers(StorageFixture &storage) {
	install(storage, String(corpus_root) + "errors/final_external_box_writer_trait.notest.barista", R"source(# Helper trait for final_member_from_trait_writes_external_final. Its concrete method writes another
# class's `final` member; the write is resolved in this trait's own context against CafecitoLockedBox.
trait_name CafecitoBoxWriter

func poke(b: CafecitoLockedBox) -> void:
	b.value = 7
)source");
	install(storage, String(corpus_root) + "errors/final_external_final_base_class.notest.barista", R"source(# Helper base class for final_member_from_trait_writes_inherited_final: declares a write-once `final`
# member that an implementer inherits and a constrained trait can reach by name.
class_name CafecitoFinalBaseClass
extends RefCounted

final var id: int = 1
)source");
	install(storage, String(corpus_root) + "errors/final_external_inherited_writer_trait.notest.barista", R"source(# Helper trait for final_member_from_trait_writes_inherited_final. Constrained to a base that declares
# a `final` member, it writes that inherited final from a concrete method; the implementer inheriting
# the base must get the write-once violation just as a directly declared final would.
trait_name CafecitoInheritedWriter
extends CafecitoFinalBaseClass

func shift() -> void:
	id = 9
)source");
	install(storage, String(corpus_root) + "errors/final_external_locked_box.notest.barista", R"source(# Helper for final_member_from_trait_writes_external_final: a global class with a write-once `final`
# member. The trait helper below writes this member from a concrete method.
class_name CafecitoLockedBox
extends RefCounted

final var value: int = 0
)source");
	install(storage, String(corpus_root) + "errors/final_external_same_name_writer_trait.notest.barista", R"source(# Helper trait for final_member_from_trait_writes_same_named_external. Supplies its own `final var
# value` yet writes a *different* class's same-named `final` from a concrete method; the external
# write must still be rejected even though the name collides with this trait's own final.
trait_name CafecitoSameNameWriter

final var value: int = 1

func poke(b: CafecitoLockedBox) -> void:
	b.value = 7
)source");
	install(storage, String(corpus_root) + "errors/final_external_shadowed_nonself_writer_trait.notest.barista", R"source(# Helper trait for final_member_from_trait_writes_nonself_shadowed_final. Declares a `final var` and a
# concrete method that writes it through a *non-self* receiver typed as this trait. The receiver may be
# any implementer whose trait-supplied final is still in effect, so the write must be rejected even
# when the implementer that flattens this method shadows the final with a mutable member.
trait_name CafecitoShadowedFinalTrait

final var id: int = 1

func poke(other: CafecitoShadowedFinalTrait) -> void:
	other.id = 9
)source");
	install(storage, String(corpus_root) + "errors/final_external_static_box.notest.barista", R"source(# Helper for final_static_var_from_trait_writes_external: a global class with a write-once
# `final static var`. The trait helper below writes this static member from a concrete method.
class_name CafecitoStaticBox
extends RefCounted

final static var VALUE: int = 0
)source");
	install(storage, String(corpus_root) + "errors/final_external_static_writer_trait.notest.barista", R"source(# Helper trait for final_static_var_from_trait_writes_external. Its concrete method writes another
# class's `final static var` through a qualified reference, resolved in this trait's own context.
trait_name CafecitoStaticWriter

func poke() -> void:
	CafecitoStaticBox.VALUE = 7
)source");
	install(storage, String(corpus_root) + "errors/final_external_trait_mutable_base.notest.barista", R"source(# Helper trait for final_member_from_external_trait_shadowing_final. Declares a plain `var` and a
# concrete method that writes it; the writing method is resolved in this trait's own context.
trait_name CafecitoExtMutableTrait

var id: int = 1

func mutate() -> void:
	id = 2
)source");
	install(storage, String(corpus_root) + "features/final_static_var_from_trait_qualified_self_init.notest.barista", R"source(# Helper trait for final_static_var_from_trait_qualified_self_init. Fills its own blank `final static
# var` exactly once in `_static_init` through a qualified `Self.COUNT` reference; a static final has a
# single shared slot, so the qualified form is the slot itself and must be accepted.
trait_name CafecitoRegistry

final static var COUNT: int

static func _static_init() -> void:
	CafecitoRegistry.COUNT = 3
)source");
	install(storage, String(corpus_root) + "errors/final_base.notest.barista", R"source(final extends RefCounted
)source");
	install(storage, String(corpus_root) + "errors/trait_body_declaration_error_base.notest.barista", R"source(# Helper trait for trait_body_declaration_error. Its requirement is missing the "abstract"
# modifier, so this file fails to parse and the trait body can never be flattened into an
# implementer. The consumer-side diagnostic must point back here.
trait_name CafecitoBadRequirement

func ping() -> int
)source");
	install(storage, "res://tests/corpus/parser/features/top_level_enum.norun.barista", R"source(enum_name TopLevelParserEnum:
	## Stored on the first enum value.
	A = 10
	## Stored on the second enum value.
	B = A + 1
)source");
	install(storage, "res://tests/corpus/parser/features/tuple_name_declaration.norun.barista", R"source(# A `tuple_name` file declares a single global tuple type, mirroring `enum_name` for enums.
tuple_name TupleNameDeclaredVec2(x: float, y: float)
)source");
}
void original_case(const String &name, const String &source, const String &expected) {
	StorageFixture storage;
	BSConformanceRegistry::ScopedCorpusState registry;
	original_providers(storage);
	const String store = storage.path("before.bsgi");
	BS_TEST_REQUIRE(storage.index().flush(store) == OK);
	const auto before = read_bytes(store);
	for (int dependent = 0; dependent < 2; ++dependent) {
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, String(corpus_root) + name, false) == OK);
		BSAnalyzer analyzer(&parser);
		const Error result = analyzer.analyze();
		INFO(std::string(block(parser).utf8().get_data()));
		CHECK((result == OK) == (expected == "BS_TEST_OK"));
		CHECK(block(parser) == expected);
		for (int repeat = 0; repeat < 2; ++repeat) {
			ProviderPhaseTestAccess::inheritance(analyzer, parser.get_tree());
			ProviderPhaseTestAccess::body(analyzer, parser.get_tree());
			CHECK(block(parser) == expected);
		}
	}
	BS_TEST_REQUIRE(storage.index().flush(store) == OK);
	CHECK(read_bytes(store) == before);
}
} // namespace
TEST_SUITE("provider_phase_analyzer") {
	TEST_CASE("extend_final_class_crossfile") {
		original_case("errors/extend_final_class_crossfile.barista", R"source(extends "res://tests/corpus_staging/analyzer/errors/final_base.notest.barista"

func test():
	pass
)source",
				R"expected(>> ERROR at line 1: Cannot extend final class "final_base.notest.barista".)expected");
	}
	TEST_CASE("extend_final_class_named") {
		original_case("errors/extend_final_class_named.barista", R"source(final class Base:
	pass

class Derived extends Base:
	pass

func test():
	pass
)source",
				R"expected(>> ERROR at line 4: Cannot extend final class "Base".)expected");
	}
	TEST_CASE("extend_non_class_constant_2") {
		original_case("errors/extend_non_class_constant_2.barista", R"source(# GH-75870

class A:
	const X = 1

const Y = A.X # A.X is now resolved.

class B extends A.X:
	pass

func test():
	pass
)source",
				R"expected(>> ERROR at line 8: Identifier "X" is not a preloaded script or class.)expected");
	}
	TEST_CASE("extend_unknown") {
		original_case("errors/extend_unknown.barista", R"source(class Foo:
	pass

class Bar extends Foo.Baz:
	pass

func test():
	print('not ok')
)source",
				R"expected(>> ERROR at line 4: Could not find nested type "Baz".)expected");
	}
	TEST_CASE("extends_global_enum_name_head") {
		original_case("errors/extends_global_enum_name_head.barista", R"source(extends TopLevelParserEnum

func test():
	pass
)source",
				R"expected(>> ERROR at line 1: Cannot extend enum_name file "TopLevelParserEnum".)expected");
	}
	TEST_CASE("extends_global_tuple_name_head") {
		original_case("errors/extends_global_tuple_name_head.barista", R"source(extends TupleNameDeclaredVec2

func test():
	pass
)source",
				R"expected(>> ERROR at line 1: Cannot extend tuple_name file "TupleNameDeclaredVec2".)expected");
	}
	TEST_CASE("final_member_from_trait_writes_external_final") {
		original_case("errors/final_member_from_trait_writes_external_final.barista", R"source(# A flattened trait method that writes a *different* class's `final` member must be rejected exactly
# as the same write from a non-trait method would be: the other class owns its final's single slot.
# This regresses the cross-class case, which name-based slot resolution alone would miss.
extends RefCounted
uses CafecitoBoxWriter

func test() -> void:
	pass
)source",
				R"expected(>> ERROR at line 6: Final variable "value" can only be assigned in its declaration or in "_init()".)expected");
	}
	TEST_CASE("final_member_from_trait_writes_inherited_final") {
		original_case("errors/final_member_from_trait_writes_inherited_final.barista", R"source(# A flattened trait method that writes an *inherited* final (reached through the trait's base
# constraint) must be rejected on an implementer that inherits the same base: the base owns the
# single slot, so the write outside `_init` is a write-once violation.
extends CafecitoFinalBaseClass
uses CafecitoInheritedWriter

func test() -> void:
	pass
)source",
				R"expected(>> ERROR at line 8: Final variable "id" can only be assigned in its declaration or in "_init()".)expected");
	}
	TEST_CASE("final_member_from_trait_writes_nonself_shadowed_final") {
		original_case("errors/final_member_from_trait_writes_nonself_shadowed_final.barista", R"source(# Shadowing a trait's `final var` with a mutable member only makes *this* instance's slot mutable; a
# flattened trait method that writes the final through a non-self receiver typed as the trait still
# targets a final on that other instance, so the write must be rejected.
extends RefCounted
uses CafecitoShadowedFinalTrait

var id: int = 5

func test() -> void:
	pass
)source",
				R"expected(>> ERROR at line 10: Final variable "id" can only be assigned in its declaration or in "_init()".)expected");
	}
	TEST_CASE("final_member_from_trait_writes_same_named_external") {
		original_case("errors/final_member_from_trait_writes_same_named_external.barista", R"source(# Even when an applied trait supplies a `final` of the same name, a flattened trait method that writes
# a *different* class's same-named `final` must be rejected: name overlap alone must not mask a write
# to a genuinely external final, which is identified by its declaring node, not its name.
extends RefCounted
uses CafecitoSameNameWriter

func test() -> void:
	pass
)source",
				R"expected(>> ERROR at line 9: Final variable "value" can only be assigned in its declaration or in "_init()".)expected");
	}
	TEST_CASE("final_static_var_from_trait_writes_external") {
		original_case("errors/final_static_var_from_trait_writes_external.barista", R"source(# A flattened trait method that writes a *different* class's `final static var` must be rejected just
# as outside a trait: the other class owns its static final's single slot. The qualified reference is
# unambiguously external, so it is flagged rather than treated as the implementer's own slot.
extends RefCounted
uses CafecitoStaticWriter

func test() -> void:
	pass
)source",
				R"expected(>> ERROR at line 6: Final variable "VALUE" can only be assigned in its declaration or in "_static_init()".)expected");
	}
	TEST_CASE("trait_body_declaration_error") {
		original_case("errors/trait_body_declaration_error.barista", R"source(# A trait whose own file has declaration errors cannot be flattened here. The cascading
# diagnostic must name the defining file and the underlying error so the reader is not left
# thinking trait resolution itself is unsupported.
extends RefCounted
uses CafecitoBadRequirement

func ping() -> int:
	return 1
)source",
				R"expected(>> ERROR at line 4: Could not resolve body of trait "CafecitoBadRequirement" applied by "trait_body_declaration_error.barista". The trait is declared in "res://tests/corpus_staging/analyzer/errors/trait_body_declaration_error_base.notest.barista", which has errors, the first at line 6: A function must either have a ":" followed by a body, or be marked as "abstract".)expected");
	}
	TEST_CASE("final_static_var_from_trait_qualified_self_init") {
		original_case("features/final_static_var_from_trait_qualified_self_init.barista", R"source(# A flattened trait `_static_init` that assigns its trait-supplied `final static var` through a
# qualified reference fills the single shared static slot and must be accepted on the implementer.
extends RefCounted
uses CafecitoRegistry

func test() -> void:
	print(CafecitoRegistry.COUNT)
)source",
				R"expected(BS_TEST_OK)expected");
	}
}
namespace {
void exact_error(const BSParser &parser, int index, const String &message, const BSParser::Node *site) {
	BS_TEST_REQUIRE(site != nullptr && index >= 0 && index < parser.get_errors().size());
	auto *entry = parser.get_errors().front();
	for (int i = 0; i < index; ++i)
		entry = entry->next();
	const auto &error = entry->get();
	CHECK(error.message == message);
	CHECK(error.line == site->start_line);
	CHECK(error.column == site->start_column);
	CHECK(error.end_line == site->end_line);
	CHECK(error.end_column == site->end_column);
}
void isolation_scenario() {
	original_case("isolation.barista", "uses CafecitoRegistry\n", "BS_TEST_OK");
}
} //namespace
TEST_SUITE("provider_phase_analyzer") {
	TEST_CASE("trait_body_failure_stays_at_body_phase_and_replays_for_separate_consumers") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = "res://tests/provider_phase/broken.barista";
		install(storage, path, "trait_name BrokenTrait\nfunc value() -> int:\n\treturn \"bad\"\n");
		BSParser first, second;
		const String source = "uses BrokenTrait\nfunc own() -> int:\n\treturn \"also bad\"\n";
		BS_TEST_REQUIRE(first.parse(source, "res://tests/provider_phase/first.barista", false) == OK);
		BS_TEST_REQUIRE(second.parse(source, "res://tests/provider_phase/second.barista", false) == OK);
		BSAnalyzer first_analyzer(&first), second_analyzer(&second);
		BS_TEST_REQUIRE(first_analyzer.resolve_interface() == OK);
		BS_TEST_REQUIRE(second_analyzer.resolve_interface() == OK);
		const auto owner = first.get_depended_parser_for(path);
		BS_TEST_REQUIRE(owner.is_valid() && second.get_depended_parser_for(path) == owner);
		CHECK(owner->get_status() == BSParserRef::INTERFACE_SOLVED);
		CHECK(owner->get_parser()->get_errors().is_empty());
		const String owner_error = "Cannot return value of type \"String\" because the function return type is \"int\".";
		for (auto *analyzer : { &first_analyzer, &second_analyzer }) {
			auto &parser = analyzer == &first_analyzer ? first : second;
			CHECK(analyzer->resolve_body() != OK);
			INFO(std::string(block(parser).utf8().get_data()));
			BS_TEST_REQUIRE(parser.get_errors().size() == 2);
			const String wrapper = vformat("Could not resolve body of trait \"BrokenTrait\" applied by \"%s\". The trait is declared in \"%s\", which has errors, the first at line 3: %s", analyzer == &first_analyzer ? "first.barista" : "second.barista", path, owner_error);
			exact_error(parser, 0, wrapper, parser.get_tree());
			auto *function = parser.get_tree()->get_member("own").function;
			BS_TEST_REQUIRE(function && function->body && function->body->statements.size() == 1);
			exact_error(parser, 1, owner_error, function->body->statements[0]);
			const String before = block(parser);
			ProviderPhaseTestAccess::body(*analyzer, parser.get_tree());
			CHECK(block(parser) == before);
			CHECK(owner->raise_status(BSParserRef::FULLY_SOLVED) != OK);
			CHECK(owner->get_parser()->get_errors().size() == 1);
		}
		for (int request = 0; request < 2; ++request) {
			BSParser preload;
			BS_TEST_REQUIRE(preload.parse("const P = preload(\"broken.barista\")\n", "res://tests/provider_phase/preload.barista", false) == OK);
			BSAnalyzer analyzer(&preload);
			CHECK(analyzer.analyze() == OK);
			CHECK(preload.get_errors().is_empty());
			CHECK(preload.get_depended_parser_for(path) == owner);
		}
	}
	TEST_CASE("retained_trait_generation_survives_refresh_and_new_consumer_recovers") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = "res://tests/provider_phase/generation.barista";
		const String old_source = "trait_name GenerationTrait\nfunc value() -> int:\n\treturn \"old\"\n";
		const String repaired = "trait_name GenerationTrait\nfunc value() -> int:\n\treturn 7\n";
		install(storage, path, old_source);
		BSParser retained;
		BS_TEST_REQUIRE(retained.parse("uses GenerationTrait\n", "res://tests/provider_phase/retained.barista", false) == OK);
		BSAnalyzer old_analyzer(&retained);
		BS_TEST_REQUIRE(old_analyzer.resolve_interface() == OK);
		const auto old = retained.get_depended_parser_for(path);
		BS_TEST_REQUIRE(old.is_valid());
		auto *old_trait = retained.get_tree()->used_traits[0].resolved_trait;
		BS_TEST_REQUIRE(old_trait && old->get_parser()->has_class(old_trait));
		BSCache::set_source_override(path, repaired);
		BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, repaired) == OK);
		BSParser fresh;
		BS_TEST_REQUIRE(fresh.parse("uses GenerationTrait\n", "res://tests/provider_phase/fresh.barista", false) == OK);
		BSAnalyzer new_analyzer(&fresh);
		CHECK(new_analyzer.analyze() == OK);
		CHECK(fresh.get_errors().is_empty());
		const auto current = fresh.get_depended_parser_for(path);
		BS_TEST_REQUIRE(current.is_valid() && current != old);
		CHECK(fresh.get_tree()->used_traits[0].resolved_trait != old_trait);
		CHECK(old_analyzer.resolve_body() != OK);
		CHECK(retained.get_tree()->used_traits[0].resolved_trait == old_trait);
		CHECK(old->get_status() == BSParserRef::FULLY_SOLVED);
		CHECK(current->get_parser()->get_errors().is_empty());
		CHECK(block(retained) == ">> ERROR at line 1: Could not resolve body of trait \"GenerationTrait\" applied by \"retained.barista\". The trait is declared in \"res://tests/provider_phase/generation.barista\", which has errors, the first at line 3: Cannot return value of type \"String\" because the function return type is \"int\".");
		BSDeclarationRecord record;
		BS_TEST_REQUIRE(storage.index().try_get_by_path(path, record));
		CHECK(record.source_digest == BSDeclarationIndex::compute_source_digest(repaired));
		CHECK(BSCache::get_source_code(path) == repaired);
	}
	TEST_CASE("direct_trait_prepares_transitive_body_in_declaration_namespace") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState registry;
		install(storage, "res://tests/provider_phase/box.barista", "namespace own\nclass_name Box\nvar count: int\n");
		const String leaf = "res://tests/provider_phase/leaf.barista";
		install(storage, leaf, "namespace own\ntrait_name Leaf\nfunc count(box: Box) -> int:\n\treturn box.count\n");
		const String branch = "res://tests/provider_phase/branch.barista";
		install(storage, branch, "namespace own\ntrait_name Branch\nuses Leaf\n");
		install(storage, "res://tests/provider_phase/shadow.barista", "namespace consumer\nclass_name Box\nvar count: String\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("namespace consumer\nuses own.Branch\n", "res://tests/provider_phase/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		CHECK(parser.get_errors().is_empty());
		BS_TEST_REQUIRE(parser.get_depended_parsers().has(branch));
		const auto direct = parser.get_depended_parsers()[branch];
		CHECK(direct->get_status() == BSParserRef::FULLY_SOLVED);
		BS_TEST_REQUIRE(direct->get_parser()->get_depended_parsers().has(leaf));
		const auto transitive = direct->get_parser()->get_depended_parsers()[leaf];
		CHECK(transitive->get_status() == BSParserRef::FULLY_SOLVED);
		CHECK(transitive->get_parser()->get_tree()->get_member("count").function->resolved_body);
	}
	TEST_CASE("valid_nested_inheritance_and_recursive_preloads_keep_their_phase") {
		StorageFixture storage;
		const String a = "res://tests/provider_phase/a.barista";
		const String b = "res://tests/provider_phase/b.barista";
		install(storage, a, "const B = preload(\"b.barista\")\nclass Base:\n\tpass\nclass Child extends Base:\n\tpass\n");
		install(storage, b, "const A = preload(\"a.barista\")\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("const A = preload(\"a.barista\")\n", "res://tests/provider_phase/use.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		CHECK(parser.get_errors().is_empty());
		const auto owner = parser.get_depended_parser_for(a);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK(owner->get_status() == BSParserRef::INHERITANCE_SOLVED);
		CHECK(owner->raise_status(BSParserRef::FULLY_SOLVED) == OK);
		auto *head = owner->get_parser()->get_tree();
		CHECK(head->get_member("Child").m_class->base_type.class_type == head->get_member("Base").m_class);
	}
	TEST_CASE("inheritance_failure_is_per_declaration_and_refresh_recovers_dependents") {
		StorageFixture storage;
		const String path = "res://tests/provider_phase/base.barista";
		install(storage, path, "final extends RefCounted\n");
		const String source = "extends \"res://tests/provider_phase/base.barista\"\n";
		for (int dependent = 0; dependent < 2; ++dependent) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, storage.path(String::num_int64(dependent) + ".barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			CHECK(block(parser) == ">> ERROR at line 1: Cannot extend final class \"base.barista\".");
			for (int repeat = 0; repeat < 2; ++repeat)
				ProviderPhaseTestAccess::inheritance(analyzer, parser.get_tree());
			CHECK(parser.get_errors().size() == 1);
		}
		const String repaired = "extends RefCounted\n";
		BSCache::set_source_override(path, repaired);
		BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, repaired) == OK);
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, storage.path("repaired.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		CHECK(parser.get_errors().is_empty());
		BSParser separate;
		BS_TEST_REQUIRE(separate.parse("class First extends Unknown:\n\tpass\nclass Second extends Unknown:\n\tpass\n", storage.path("distinct.barista"), false) == OK);
		BSAnalyzer separate_analyzer(&separate);
		CHECK(separate_analyzer.analyze() != OK);
		CHECK(block(separate) == ">> ERROR at line 1: Could not find base class \"Unknown\".\n>> ERROR at line 3: Could not find base class \"Unknown\".");
	}
	TEST_CASE("native_fixture_restores_index_overrides_and_global_state") {
		CHECK(verify_case_isolation(isolation_scenario));
	}
}
TEST_SUITE("provider_phase_analyzer") {
	TEST_CASE("earlier_trait_phase_failures_never_become_body_success") {
		for (const String &provider_source : {
					 String("trait_name EarlyFailure\nextends MissingBase\n"),
					 String("trait_name EarlyFailure\nvar value: MissingType\n") }) {
			StorageFixture storage;
			BSConformanceRegistry::ScopedCorpusState registry;
			const String path = "res://tests/provider_phase/early.barista";
			install(storage, path, provider_source);
			for (int dependent = 0; dependent < 2; ++dependent) {
				BSParser parser;
				BS_TEST_REQUIRE(parser.parse("uses EarlyFailure\n", storage.path("early_consumer.barista"), false) == OK);
				BSAnalyzer analyzer(&parser);
				CHECK(analyzer.analyze() != OK);
				CHECK(!parser.get_errors().is_empty());
				CHECK(parser.get_tree()->used_traits[0].resolved_trait == nullptr);
				const auto owner = parser.get_depended_parser_for(path);
				BS_TEST_REQUIRE(owner.is_valid());
				CHECK(owner->get_status() < BSParserRef::FULLY_SOLVED);
				CHECK(owner->get_result_for_status(BSParserRef::INTERFACE_SOLVED) != OK);
				CHECK(!owner->get_parser()->get_errors().is_empty());
				CHECK(!block(parser).contains("Could not resolve body of trait"));
			}
		}
	}
	TEST_CASE("trait_parse_failure_remains_a_parse_failure_for_each_consumer") {
		StorageFixture storage;
		const String path = "res://tests/provider_phase/parse.barista";
		const String invalid = "trait_name ParseFailure\nfunc broken(\n";
		auto record = BSDeclarationIndex::record_from_global_class(path, invalid,
				bs_resolve_global_class_from_source("trait_name ParseFailure\n", path));
		BSCache::set_source_override(path, invalid);
		BS_TEST_REQUIRE(storage.index().commit_record(storage.index().claim_refresh(path), record));
		for (int dependent = 0; dependent < 2; ++dependent) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("uses ParseFailure\n", storage.path("parse_consumer.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			CHECK(!parser.get_errors().is_empty());
			CHECK(parser.get_tree()->used_traits[0].resolved_trait == nullptr);
			CHECK(!block(parser).contains("Could not resolve body of trait"));
			const auto owner = parser.get_depended_parser_for(path);
			BS_TEST_REQUIRE(owner.is_valid());
			CHECK(owner->raise_status(BSParserRef::PARSED) != OK);
			CHECK(owner->get_status() == BSParserRef::PARSED);
		}
	}
	TEST_CASE("ordinary_trait_initializer_resolves_its_own_final_declaration") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = "res://tests/provider_phase/init.barista";
		install(storage, path, "trait_name Initialized\nfinal var id: int\nfunc _init() -> void:\n\tid = 2\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("uses Initialized\n", storage.path("initialized.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		INFO(std::string(block(parser).utf8().get_data()));
		CHECK(parser.get_errors().is_empty());
		const auto owner = parser.get_depended_parser_for(path);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK(owner->get_status() == BSParserRef::FULLY_SOLVED);
		const auto *trait = owner->get_parser()->get_tree();
		BS_TEST_REQUIRE(trait->get_member("_init").function != nullptr);
		const auto *body = trait->get_member("_init").function->body;
		BS_TEST_REQUIRE(body != nullptr && body->statements.size() == 1);
		const auto *assignment = static_cast<BSParser::AssignmentNode *>(body->statements[0]);
		BS_TEST_REQUIRE(assignment->assignee && assignment->assignee->type == BSParser::Node::IDENTIFIER);
		const auto *identifier = static_cast<BSParser::IdentifierNode *>(assignment->assignee);
		CHECK(identifier->variable_source == trait->get_member("id").variable);
	}
}
TEST_SUITE("provider_phase_analyzer") {
	TEST_CASE("failed_base_provider_keeps_its_earlier_phase_failure_per_consumer") {
		StorageFixture storage;
		const String path = "res://tests/provider_phase/base.barista";
		install(storage, path, "class_name FailedBase\nextends MissingBase\n");
		for (int dependent = 0; dependent < 2; ++dependent) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("extends FailedBase\n", storage.path(String::num_int64(dependent) + ".barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			const String expected = dependent == 0 ? ">> ERROR at line 1: Could not resolve global class base \"FailedBase\"." : ">> ERROR at line 1: Could not resolve base class \"FailedBase\": provider \"res://tests/provider_phase/base.barista\" could not be parsed.";
			INFO(std::string(block(parser).utf8().get_data()));
			CHECK(block(parser) == expected);
			for (int repeat = 0; repeat < 2; ++repeat)
				ProviderPhaseTestAccess::inheritance(analyzer, parser.get_tree());
			CHECK(block(parser) == expected);
			const auto owner = parser.get_depended_parser_for(path);
			BS_TEST_REQUIRE(owner.is_valid());
			CHECK(owner->get_status() == BSParserRef::INHERITANCE_SOLVED);
			CHECK(owner->get_result_for_status(BSParserRef::INTERFACE_SOLVED) != OK);
			CHECK(block(*owner->get_parser()) == ">> ERROR at line 2: Could not find base class \"MissingBase\".");
		}
	}
}
