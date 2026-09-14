/**************************************************************************/
/*  base_outer_analyzer_tests.cpp                                         */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "barista_script_language.h"
#include "bs_analyzer_probe.h"
#include "bs_global_class.h"
#include "storage_fixture.h"
#include "test_require.h"

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

String error_block(const BSParser &p_parser) {
	String result;
	for (const auto &error : p_parser.get_errors()) {
		if (!result.is_empty()) {
			result += "\n";
		}
		result += vformat(">> ERROR at line %d: %s", error.line, error.message);
	}
	return result.is_empty() ? String("BS_TEST_OK") : result;
}

void no_errors(const BSParser &p_parser) {
	for (const auto &error : p_parser.get_errors()) {
		MESSAGE(std::string(error.message.utf8().get_data()));
	}
	CHECK(error_block(p_parser) == "BS_TEST_OK");
}

String exact_a_source() {
	return "class APrime:\n\tpass\n";
}

String exact_b_source() {
	return "";
}

String exact_c_source() {
	return "enum TestEnum:\n\tHELLO_WORLD = 0\n";
}

String exact_base_source() {
	return "const A: = preload(\"base_outer_resolution_a.notest.barista\")\n\n"
		   "class InnerClassInBase:\n"
		   "\tconst C: = preload(\"base_outer_resolution_c.notest.barista\")\n";
}

String exact_extend_source() {
	return "extends \"base_outer_resolution_base.notest.barista\"\n\n"
		   "const B: = preload(\"base_outer_resolution_b.notest.barista\")\n\n"
		   "static func test_a(a: A) -> void:\n"
		   "\tprint(a is A)\n\n"
		   "static func test_b(b: B) -> void:\n"
		   "\tprint(b is B)\n\n"
		   "class InnerClass extends InnerClassInBase:\n"
		   "\tstatic func test_c(c: C) -> void:\n"
		   "\t\tprint(c is C)\n\n"
		   "\tclass InnerInnerClass:\n"
		   "\t\tstatic func test_a_b_c(a: A, b: B, c: C) -> void:\n"
		   "\t\t\tprint(a is A and b is B and c is C)\n\n"
		   "\t\tstatic func test_enum(test_enum: C.TestEnum) -> void:\n"
		   "\t\t\tprint(test_enum == C.TestEnum.HELLO_WORLD)\n\n"
		   "\t\tstatic func test_a_prime(a_prime: A.APrime) -> void:\n"
		   "\t\t\tprint(a_prime is A.APrime)\n";
}

String exact_consumer_source() {
	return "const A: = preload(\"base_outer_resolution_a.notest.barista\")\n"
		   "const B: = preload(\"base_outer_resolution_b.notest.barista\")\n"
		   "const C: = preload(\"base_outer_resolution_c.notest.barista\")\n\n"
		   "const Extend: = preload(\"base_outer_resolution_extend.notest.barista\")\n\n"
		   "func test() -> void:\n"
		   "\tExtend.test_a(A.new())\n"
		   "\tExtend.test_b(B.new())\n"
		   "\tExtend.InnerClass.test_c(C.new())\n"
		   "\tExtend.InnerClass.InnerInnerClass.test_a_b_c(A.new(), B.new(), C.new())\n"
		   "\tExtend.InnerClass.InnerInnerClass.test_enum(C.TestEnum.HELLO_WORLD)\n"
		   "\tExtend.InnerClass.InnerInnerClass.test_a_prime(A.APrime.new())\n";
}

BSParser::ClassNode *nested(BSParser::ClassNode *p_class, const StringName &p_name);

String exact_inner_base_source() {
	return "extends InnerA\n\n"
		   "func test():\n"
		   "\tsuper.test()\n\n"
		   "class InnerA extends InnerAB:\n"
		   "\tfunc test():\n"
		   "\t\tprint(\"InnerA.test\")\n"
		   "\t\tsuper.test()\n\n"
		   "\tclass InnerAB extends InnerB:\n"
		   "\t\tfunc test():\n"
		   "\t\t\tprint(\"InnerA.InnerAB.test\")\n"
		   "\t\t\tsuper.test()\n\n"
		   "class InnerB:\n"
		   "\tfunc test():\n"
		   "\t\tprint(\"InnerB.test\")\n";
}

String exact_external_inner_base_source() {
	return "extends \"inner_base.barista\".InnerA.InnerAB\n\n"
		   "func test():\n"
		   "\tsuper.test()\n";
}

void public_agreement(const String &p_source, const String &p_path, bool p_valid) {
	Ref<BaristaScriptAnalyzerProbe> probe;
	probe.instantiate();
	CHECK(bool(probe->analyze_source(p_source, p_path).get("valid", !p_valid)) == p_valid);
	CHECK(bool(BaristaScriptLanguage::get_singleton()->_validate(p_source, p_path, true, true, true, true).get("valid", !p_valid)) == p_valid);
	Ref<BaristaScript> resource;
	resource.instantiate();
	resource->_set_source_code(p_source);
	resource->set_path(p_path);
	CHECK(resource->_is_valid() == p_valid);
}

void require_inner_base_identity(BSParser::ClassNode *p_root) {
	BS_TEST_REQUIRE(p_root != nullptr);
	BSParser::ClassNode *inner_a = nested(p_root, "InnerA");
	BSParser::ClassNode *inner_ab = nested(inner_a, "InnerAB");
	BSParser::ClassNode *inner_b = nested(p_root, "InnerB");
	BS_TEST_REQUIRE(inner_a != nullptr && inner_ab != nullptr && inner_b != nullptr);
	CHECK(p_root->base_type.class_type == inner_a);
	CHECK(inner_a->base_type.class_type == inner_ab);
	CHECK(inner_ab->base_type.class_type == inner_b);
}

void install_exact_graph(StorageFixture &p_storage) {
	install(p_storage, path("base_outer_resolution_a.notest"), exact_a_source());
	install(p_storage, path("base_outer_resolution_b.notest"), exact_b_source());
	install(p_storage, path("base_outer_resolution_c.notest"), exact_c_source());
	install(p_storage, path("base_outer_resolution_base.notest"), exact_base_source());
	install(p_storage, path("base_outer_resolution_extend.notest"), exact_extend_source());
}

Ref<BSParserRef> dependency(BSParser &p_parser, const String &p_path) {
	CHECK(p_parser.get_depended_parsers().has(p_path));
	return p_parser.get_depended_parsers().has(p_path) ? p_parser.get_depended_parsers()[p_path] : Ref<BSParserRef>();
}

BSParser::FunctionNode *function(BSParser::ClassNode *p_class, const StringName &p_name) {
	CHECK(bool(p_class != nullptr && p_class->has_member(p_name)));
	if (p_class == nullptr || !p_class->has_member(p_name)) {
		return nullptr;
	}
	const auto member = p_class->get_member(p_name);
	CHECK(bool(member.type == BSParser::ClassNode::Member::FUNCTION && member.function != nullptr));
	return member.type == BSParser::ClassNode::Member::FUNCTION ? member.function : nullptr;
}

BSParser::ClassNode *nested(BSParser::ClassNode *p_class, const StringName &p_name) {
	CHECK(bool(p_class != nullptr && p_class->has_member(p_name)));
	if (p_class == nullptr || !p_class->has_member(p_name)) {
		return nullptr;
	}
	const auto member = p_class->get_member(p_name);
	CHECK(bool(member.type == BSParser::ClassNode::Member::CLASS && member.m_class != nullptr));
	return member.type == BSParser::ClassNode::Member::CLASS ? member.m_class : nullptr;
}

void analyze_exact_graph(StorageFixture &p_storage, const String &p_stem) {
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(exact_consumer_source(), path(p_stem), false) == OK);
	BSAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() == OK);
	MESSAGE(std::string(error_block(parser).utf8().get_data()));
	no_errors(parser);

	const auto a = dependency(parser, path("base_outer_resolution_a.notest"));
	const auto b = dependency(parser, path("base_outer_resolution_b.notest"));
	const auto c = dependency(parser, path("base_outer_resolution_c.notest"));
	const auto extend = dependency(parser, path("base_outer_resolution_extend.notest"));
	BS_TEST_REQUIRE(a.is_valid() && b.is_valid() && c.is_valid() && extend.is_valid());
	CHECK(parser.get_depended_parsers().size() == 4);
	const auto base = dependency(*extend->get_parser(), path("base_outer_resolution_base.notest"));
	BS_TEST_REQUIRE(base.is_valid());
	CHECK(extend->get_parser()->get_depended_parsers().size() == 2);
	CHECK(base->get_parser()->get_depended_parsers().size() == 2);
	BSParser::ClassNode *extend_class = extend->get_parser()->get_tree();
	BSParser::ClassNode *inner = nested(extend_class, "InnerClass");
	BSParser::ClassNode *deep = nested(inner, "InnerInnerClass");
	CHECK(function(extend_class, "test_a")->parameters[0]->get_datatype().class_type == a->get_parser()->get_tree());
	CHECK(function(extend_class, "test_b")->parameters[0]->get_datatype().class_type == b->get_parser()->get_tree());
	CHECK(function(inner, "test_c")->parameters[0]->get_datatype().class_type == c->get_parser()->get_tree());
	const auto parameters = function(deep, "test_a_b_c")->parameters;
	BS_TEST_REQUIRE(parameters.size() == 3);
	CHECK(parameters[0]->get_datatype().class_type == a->get_parser()->get_tree());
	CHECK(parameters[1]->get_datatype().class_type == b->get_parser()->get_tree());
	CHECK(parameters[2]->get_datatype().class_type == c->get_parser()->get_tree());
}

} // namespace

TEST_SUITE("base_outer_analyzer") {
	TEST_CASE("exact_inner_base_keeps_reentrant_nested_inheritance_without_self_conflict") {
		StorageFixture storage;
		const String source = exact_inner_base_source();
		const String source_path = path("inner_base");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, source_path, false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		MESSAGE(std::string(error_block(parser).utf8().get_data()));
		no_errors(parser);
		require_inner_base_identity(parser.get_tree());
		public_agreement(source, source_path, true);
	}

	TEST_CASE("external_inner_base_retains_exact_provider_identity_cold_and_warm") {
		for (const bool warm : { false, true }) {
			CAPTURE(warm);
			StorageFixture storage;
			const String provider_path = path("inner_base");
			const String consumer_path = path(warm ? "external_inner_base_warm" : "external_inner_base");
			install(storage, provider_path, exact_inner_base_source());
			Ref<BSParserRef> warmed;
			if (warm) {
				Error error = OK;
				warmed = BSCache::get_parser(provider_path, BSParserRef::FULLY_SOLVED, error);
				BS_TEST_REQUIRE(warmed.is_valid() && error == OK);
			}

			const String source = exact_external_inner_base_source();
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, consumer_path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			MESSAGE(std::string(error_block(parser).utf8().get_data()));
			no_errors(parser);
			const Ref<BSParserRef> retained = dependency(parser, provider_path);
			BS_TEST_REQUIRE(retained.is_valid());
			if (warm) {
				CHECK(retained == warmed);
			}
			require_inner_base_identity(retained->get_parser()->get_tree());
			BSParser::ClassNode *provider_inner_ab = nested(nested(retained->get_parser()->get_tree(), "InnerA"), "InnerAB");
			BS_TEST_REQUIRE(provider_inner_ab != nullptr);
			CHECK(parser.get_tree()->base_type.class_type == provider_inner_ab);
			CHECK(provider_inner_ab->outer == nested(retained->get_parser()->get_tree(), "InnerA"));
			public_agreement(source, consumer_path, true);
		}
	}

	TEST_CASE("quoted_path_nested_tail_rejects_missing_and_nonclass_members") {
		for (const bool missing : { false, true }) {
			CAPTURE(missing);
			StorageFixture storage;
			const String provider_path = path("quoted_tail_provider.notest");
			install(storage, provider_path,
					"class Inner:\n"
					"\tconst VALUE := 1\n");
			const String tail = missing ? "Missing" : "Inner.VALUE";
			const String source = "extends \"quoted_tail_provider.notest.barista\"." + tail + "\n";
			const String consumer_path = path(missing ? "quoted_tail_missing.notest" : "quoted_tail_nonclass.notest");
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, consumer_path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			CHECK(error_block(parser) == (missing ? ">> ERROR at line 1: Could not find nested type \"Missing\"." : ">> ERROR at line 1: Identifier \"VALUE\" is not a preloaded script or class."));
			public_agreement(source, consumer_path, false);
		}
	}

	TEST_CASE("resolving_edge_filter_preserves_true_outer_conflicts") {
		struct Conflict {
			const char *name;
			const char *source;
			const char *expected;
		};
		for (const Conflict &conflict : {
					 Conflict{ "nested_outer_class_identifier_conflict", "class_name NestedOuterClassIdentifierConflict\n\nclass Inner:\n\tvar NestedOuterClassIdentifierConflict := 1\n\nfunc test() -> void:\n\tpass\n", ">> ERROR at line 4: The member \"NestedOuterClassIdentifierConflict\" already exists in outer class NestedOuterClassIdentifierConflict." },
					 Conflict{ "nested_outer_constant_conflict", "class Outer:\n\tconst LIMIT := 1\n\n\tclass Inner:\n\t\tvar LIMIT := 2\n\nfunc test() -> void:\n\tpass\n", ">> ERROR at line 5: The member \"LIMIT\" already exists in outer class Outer." },
					 Conflict{ "nested_outer_inherited_surface_conflict", "class Base:\n\tconst TOKEN := 1\n\nclass Outer extends Base:\n\tclass Inner:\n\t\tvar TOKEN := 2\n\nfunc test() -> void:\n\tpass\n", ">> ERROR at line 6: The member \"TOKEN\" already exists in outer class Base." },
					 Conflict{ "nested_outer_nested_class_conflict", "class Outer:\n\tclass Helper:\n\t\tpass\n\n\tclass Inner:\n\t\tvar Helper := 1\n\nfunc test() -> void:\n\tpass\n", ">> ERROR at line 6: The member \"Helper\" already exists in outer class Outer." },
			 }) {
			CAPTURE(conflict.name);
			StorageFixture storage;
			const String source = conflict.source;
			const String source_path = storage.path(String(conflict.name) + ".barista");
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, source_path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			CHECK(error_block(parser) == conflict.expected);
			public_agreement(source, source_path, false);
		}
	}

	TEST_CASE("exact_base_outer_resolution_graph_keeps_all_six_call_sites_static") {
		StorageFixture storage;
		install_exact_graph(storage);
		const uint64_t base_generation = storage.index().get_refresh_revision(path("base_outer_resolution_base.notest"));
		analyze_exact_graph(storage, "base_outer_resolution");
		analyze_exact_graph(storage, "base_outer_resolution_again");
		CHECK(storage.index().get_refresh_revision(path("base_outer_resolution_base.notest")) == base_generation);
	}

	TEST_CASE("nearest_lexical_preload_wins_at_multiple_outer_depths") {
		StorageFixture storage;
		install(storage, path("shadow_base_pick.notest"), "const BASE = true\n");
		install(storage, path("shadow_outer_pick.notest"), "const OUTER = true\n");
		install(storage, path("shadow_base.notest"),
				"class BaseInner:\n"
				"\tconst Pick = preload(\"shadow_base_pick.notest.barista\")\n");
		const String derived = "extends \"shadow_base.notest.barista\"\n"
							   "const Pick = preload(\"shadow_outer_pick.notest.barista\")\n"
							   "static func outer(value: Pick) -> void:\n\tprint(value)\n"
							   "class Inner extends BaseInner:\n"
							   "\tclass Deep:\n"
							   "\t\tstatic func inner(value: Pick) -> void:\n\t\t\tprint(value)\n";
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(derived, path("shadow_derived.notest"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		no_errors(parser);
		const auto outer_pick = dependency(parser, path("shadow_outer_pick.notest"));
		BSParser::ClassNode *inner = nested(parser.get_tree(), "Inner");
		BSParser::ClassNode *deep = nested(inner, "Deep");
		CHECK(function(parser.get_tree(), "outer")->parameters[0]->get_datatype().class_type == outer_pick->get_parser()->get_tree());
		CHECK(function(deep, "inner")->parameters[0]->get_datatype().class_type == outer_pick->get_parser()->get_tree());
	}

	TEST_CASE("selected_inherited_preload_failure_is_terminal_but_true_miss_falls_back") {
		StorageFixture storage;
		install(storage, path("fallback_a.notest"), "class_name A\n");
		install(storage, path("broken_a.notest"), "extends MissingBase\n");
		install(storage, path("failure_base.notest"), "const A = preload(\"broken_a.notest.barista\")\n");

		BSParser failed;
		BS_TEST_REQUIRE(failed.parse("extends \"failure_base.notest.barista\"\nvar value: A\n", path("failure_derived.notest"), false) == OK);
		BSAnalyzer failed_analyzer(&failed);
		CHECK(failed_analyzer.analyze() != OK);
		MESSAGE(std::string(error_block(failed).utf8().get_data()));
		CHECK(error_block(failed).contains(">> ERROR at line 2: Could not resolve external class member \"A\"."));
		CHECK(failed.get_tree()->get_member("value").get_datatype().is_variant());

		install(storage, path("miss_base.notest"), "const Other = 1\n");
		BSParser fallback;
		BS_TEST_REQUIRE(fallback.parse("extends \"miss_base.notest.barista\"\nvar value: A\n", path("miss_derived.notest"), false) == OK);
		BSAnalyzer fallback_analyzer(&fallback);
		CHECK(fallback_analyzer.analyze() == OK);
		no_errors(fallback);
		CHECK(fallback.get_tree()->get_member("value").get_datatype().class_type != nullptr);
		CHECK(fallback.get_tree()->get_member("value").get_datatype().class_type->get_global_name() == StringName("A"));
	}

	TEST_CASE("repeated_selected_inherited_preload_failure_stays_terminal_after_replay_dedupe") {
		for (const bool install_fallback : { false, true }) {
			CAPTURE(install_fallback);
			StorageFixture storage;
			if (install_fallback) {
				install(storage, path("repeated_fallback_pick.notest"), "class_name Pick\n");
			}
			install(storage, path("repeated_broken_pick.notest"), "extends MissingBase\n");
			install(storage, path("repeated_failure_base.notest"), "const Pick = preload(\"repeated_broken_pick.notest.barista\")\n");

			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("extends \"repeated_failure_base.notest.barista\"\nvar first: Pick\nvar second: Pick\n", path(install_fallback ? "repeated_failure_with_fallback.notest" : "repeated_failure_without_fallback.notest"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			MESSAGE(std::string(error_block(parser).utf8().get_data()));
			CHECK(error_block(parser) == ">> ERROR at line 1: Could not resolve class \"repeated_failure_base.notest.barista\".\n"
										 ">> ERROR at line 2: Could not resolve external class member \"Pick\".\n"
										 ">> ERROR at line 1: Could not resolve class \"repeated_failure_base.notest.barista\". The class has errors, the first at line 1: Could not preload resource script \"res://tests/corpus_staging/analyzer/features/repeated_broken_pick.notest.barista\".");
			CHECK(parser.get_tree()->get_member("first").get_datatype().is_variant());
			CHECK(parser.get_tree()->get_member("second").get_datatype().is_variant());
		}
	}

	TEST_CASE("inherited_outer_lookup_does_not_leak_across_siblings") {
		StorageFixture storage;
		install(storage, path("sibling_pick.notest"), "const SIBLING = true\n");
		install(storage, path("sibling_base.notest"),
				"class Left:\n\tconst Pick = preload(\"sibling_pick.notest.barista\")\n"
				"class Right:\n\tpass\n");
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("extends \"sibling_base.notest.barista\"\nclass Inner extends Right:\n\tvar value: Pick\n", path("sibling_derived.notest"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		CHECK(error_block(parser) == ">> ERROR at line 3: Could not find type \"Pick\" in the current scope.");
	}

	TEST_CASE("refresh_replaces_inherited_outer_handle_without_mutating_retained_owner") {
		StorageFixture storage;
		install(storage, path("refresh_first.notest"), "const FIRST = true\n");
		install(storage, path("refresh_second.notest"), "const SECOND = true\n");
		const String base_path = path("refresh_base.notest");
		const String derived_path = path("refresh_derived.notest");
		const String first_base = "const Pick = preload(\"refresh_first.notest.barista\")\n";
		const String second_base = "const Pick = preload(\"refresh_second.notest.barista\")\n";
		const String derived = "extends \"refresh_base.notest.barista\"\nstatic func take(value: Pick) -> void:\n\tprint(value)\n";
		install(storage, base_path, first_base);
		install(storage, derived_path, derived);

		Error error = OK;
		Ref<BSParserRef> old_derived = BSCache::get_parser(derived_path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(old_derived.is_valid() && error == OK);
		BSParser::ClassNode *old_pick = function(old_derived->get_parser()->get_tree(), "take")->parameters[0]->get_datatype().class_type;
		BS_TEST_REQUIRE(old_pick != nullptr);
		const uint64_t generation = storage.index().get_refresh_revision(base_path);

		BSCache::set_source_override(base_path, second_base);
		BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(base_path, second_base) == OK);
		CHECK(storage.index().get_refresh_revision(base_path) > generation);
		Ref<BSParserRef> fresh_derived = BSCache::get_parser(derived_path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(fresh_derived.is_valid() && error == OK);
		BSParser::ClassNode *fresh_pick = function(fresh_derived->get_parser()->get_tree(), "take")->parameters[0]->get_datatype().class_type;
		BS_TEST_REQUIRE(fresh_pick != nullptr);
		CHECK(fresh_derived != old_derived);
		CHECK(fresh_pick != old_pick);
		CHECK(old_pick->has_member("FIRST"));
		CHECK(fresh_pick->has_member("SECOND"));
	}
}
