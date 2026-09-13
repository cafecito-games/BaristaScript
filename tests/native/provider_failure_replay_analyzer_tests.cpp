/**************************************************************************/
/*  provider_failure_replay_analyzer_tests.cpp                            */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_analyzer_probe.h"
#include "bs_global_class.h"
#include "storage_fixture.h"
#include "test_require.h"

#include <string>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {

const char *const CORPUS_ROOT = "res://tests/corpus_staging/analyzer/";

String provider_path() {
	return String(CORPUS_ROOT) + "errors/resolve_failed_class_nested_type_holder.notest.barista";
}

String consumer_path(const String &p_stem = "resolve_failed_class_nested_type") {
	return String(CORPUS_ROOT) + "errors/" + p_stem + ".barista";
}

String failed_provider_source() {
	return R"source(# Deliberately fails inheritance so raising this file for a nested-type lookup leaves a sticky
# analyzer error on FSParserRef — not a syntax/parser failure. Used by
# resolve_failed_class_nested_type.fs to assert the consumer diagnostic names the relationship
# instead of saying "because of a parser error", and does not cascade into a Variant nested-type error.
namespace diag.resolve_failed

class_name ResolveFailedHolder extends DoesNotExist

class Inner extends RefCounted:
	var value: int = 0
)source";
}

String healthy_provider_source() {
	return R"source(# Repaired provider keeps the same identity and nested declaration.
namespace diag.resolve_failed

class_name ResolveFailedHolder extends RefCounted

class Inner extends RefCounted:
	var value: int = 0
)source";
}

String consumer_source() {
	return R"source(# When a dependency cannot be raised, a nested type under that class must report one root-cause
# diagnostic that names the resolving type — not a false "parser error" plus a cascaded
# "nested type under Variant" follow-on.
namespace diag.resolve_failed

enum_name ResolveFailedPick:
	Inner(inner: ResolveFailedHolder.Inner)
	Text(text: String)
)source";
}

String expected_failure(const String &p_path = provider_path()) {
	return "Could not resolve class \"diag.resolve_failed.ResolveFailedHolder\" while resolving \"diag.resolve_failed.ResolveFailedPick\". "
		   "The class is declared in \"" +
			p_path + "\", which has errors, the first at line 7: Could not find base class \"DoesNotExist\".";
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

String public_block(const String &p_source, const String &p_path) {
	Ref<BaristaScriptAnalyzerProbe> probe;
	probe.instantiate();
	const Dictionary report = probe->validate_source(p_source, p_path, true);
	String result;
	for (const Variant &value : Array(report.get("errors", Array()))) {
		const Dictionary error = value;
		if (!result.is_empty()) {
			result += "\n";
		}
		result += vformat(">> ERROR at line %d: %s", int(error.get("line", 0)), String(error.get("message", "")));
	}
	return result.is_empty() ? String("BS_TEST_OK") : result;
}

const BSParser::TypeNode *payload_type(const BSParser &p_parser) {
	const auto *head = p_parser.get_tree();
	CHECK(head != nullptr);
	if (head == nullptr) {
		return nullptr;
	}
	CHECK(head->enum_file_decl != nullptr);
	if (head->enum_file_decl == nullptr) {
		return nullptr;
	}
	const auto *declaration = head->enum_file_decl;
	CHECK_FALSE(declaration->values.is_empty());
	if (declaration->values.is_empty()) {
		return nullptr;
	}
	CHECK_FALSE(declaration->values[0].payload_fields.is_empty());
	if (declaration->values[0].payload_fields.is_empty()) {
		return nullptr;
	}
	return declaration->values[0].payload_fields[0].type;
}

void exact_one_error(const BSParser &p_parser, const String &p_message, const BSParser::Node *p_origin) {
	for (const auto &error : p_parser.get_errors()) {
		MESSAGE(std::string(error.message.utf8().get_data()));
	}
	BS_TEST_REQUIRE(p_origin != nullptr);
	BS_TEST_REQUIRE(p_parser.get_errors().size() == 1);
	const auto &error = p_parser.get_errors().front()->get();
	CHECK(error.message == p_message);
	CHECK(error.line == p_origin->start_line);
	CHECK(error.column == p_origin->start_column);
	CHECK(error.end_line == p_origin->end_line);
	CHECK(error.end_column == p_origin->end_column);
}

void analyze_exact_failure(const String &p_path, bool p_check_public) {
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(consumer_source(), p_path, false) == OK);
	BSAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
	const String message = expected_failure();
	exact_one_error(parser, message, payload_type(parser));
	MESSAGE(std::string(error_block(parser).utf8().get_data()));
	CHECK(error_block(parser) == ">> ERROR at line 7: " + message);
	if (p_check_public) {
		const String observed_public = public_block(consumer_source(), p_path + String(".public"));
		MESSAGE(std::string(observed_public.utf8().get_data()));
		CHECK(observed_public == ">> ERROR at line 7: " + message);
	}
	CHECK(analyzer.analyze() != OK);
	CHECK(parser.get_errors().size() == 1);
}

void analyze_healthy(const String &p_source, const String &p_path) {
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(p_source, p_path, false) == OK);
	BSAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() == OK);
	CHECK(parser.get_errors().is_empty());
}

} // namespace

TEST_SUITE("provider_failure_replay_analyzer") {
	TEST_CASE("failed_external_nested_type_replays_the_exact_root_cause") {
		StorageFixture storage;
		install(storage, provider_path(), failed_provider_source());
		const uint64_t generation = storage.index().get_refresh_revision(provider_path());
		analyze_exact_failure(consumer_path(), true);
		analyze_exact_failure(consumer_path("resolve_failed_class_nested_type_again"), false);
		CHECK(storage.index().get_refresh_revision(provider_path()) == generation);

		Error error = OK;
		const Ref<BSParserRef> retained = BSCache::get_parser(provider_path(), BSParserRef::INHERITANCE_SOLVED, error);
		BS_TEST_REQUIRE(retained.is_valid() && retained->get_parser() != nullptr);
		CHECK(error != OK);
		CHECK(error_block(*retained->get_parser()) == ">> ERROR at line 7: Could not find base class \"DoesNotExist\".");
	}

	TEST_CASE("failed_selected_provider_is_terminal_but_a_true_miss_can_fall_back") {
		StorageFixture storage;
		const String selected = String(CORPUS_ROOT) + "errors/preferred_pick.barista";
		const String fallback = String(CORPUS_ROOT) + "errors/fallback_pick.barista";
		install(storage, selected, "namespace preferred\n\nclass_name Pick extends MissingBase\nclass Inner extends RefCounted:\n\tpass\n");
		install(storage, fallback, "namespace fallback\n\nclass_name Pick extends RefCounted\nclass Inner extends RefCounted:\n\tpass\n");

		const String failed_consumer = "namespace preferred\nimport fallback\nclass_name Consumer\nvar value: Pick.Inner\n";
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(failed_consumer, consumer_path("selected_failure"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		const String selected_message = "Could not resolve class \"preferred.Pick\" while resolving \"preferred.Consumer\". The class is declared in \"" + selected + "\", which has errors, the first at line 3: Could not find base class \"MissingBase\".";
		exact_one_error(parser, selected_message, parser.get_tree()->get_member("value").variable->datatype_specifier);

		analyze_healthy("namespace other\nimport fallback\nclass_name Consumer\nvar value: Pick.Inner\n", consumer_path("true_miss_fallback"));
	}

	TEST_CASE("refresh_to_a_repaired_generation_clears_replay_for_new_consumers") {
		StorageFixture storage;
		install(storage, provider_path(), failed_provider_source());
		analyze_exact_failure(consumer_path("before_repair"), false);
		const uint64_t failed_generation = storage.index().get_refresh_revision(provider_path());
		Error error = OK;
		const Ref<BSParserRef> retained = BSCache::get_parser(provider_path(), BSParserRef::INHERITANCE_SOLVED, error);
		BS_TEST_REQUIRE(retained.is_valid() && error != OK);
		const String retained_errors = error_block(*retained->get_parser());

		BSCache::set_source_override(provider_path(), healthy_provider_source());
		BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(provider_path(), healthy_provider_source()) == OK);
		CHECK(storage.index().get_refresh_revision(provider_path()) > failed_generation);
		analyze_healthy(consumer_source(), consumer_path("after_repair"));
		CHECK(public_block(consumer_source(), consumer_path("after_repair_public")) == "BS_TEST_OK");

		const Ref<BSParserRef> fresh = BSCache::get_parser(provider_path(), BSParserRef::INHERITANCE_SOLVED, error);
		BS_TEST_REQUIRE(fresh.is_valid());
		CHECK(fresh != retained);
		CHECK(error == OK);
		CHECK(fresh->get_parser()->get_errors().is_empty());
		CHECK(error_block(*retained->get_parser()) == retained_errors);
	}

	TEST_CASE("healthy_external_nested_type_resolves_without_replay") {
		StorageFixture storage;
		install(storage, provider_path(), healthy_provider_source());
		analyze_healthy(consumer_source(), consumer_path("healthy_provider"));
		CHECK(public_block(consumer_source(), consumer_path("healthy_provider_public")) == "BS_TEST_OK");
	}
}
