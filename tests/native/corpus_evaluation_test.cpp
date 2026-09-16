/**************************************************************************/
/*  corpus_evaluation_test.cpp                                            */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer_probe.h"
#include "bs_corpus_evaluation.h"
#include "doctest.h"
#include "storage_fixture.h"
#include "test_require.h"

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

TEST_SUITE("corpus_evaluation") {
	TEST_CASE("clean_source_yields_success_sentinel") {
		StorageFixture fixture;
		const CorpusResult result = evaluate_corpus_case(
				String("var value: int = 1\n").to_utf8_buffer(),
				"res://case.barista", "analyzer", PackedStringArray());
		CHECK(result.ok);
		CHECK(result.analysis_ran);
		CHECK_FALSE(result.infrastructure_error);
		CHECK(result.output == "BS_TEST_OK");
	}
	TEST_CASE("analyzer_errors_render_in_source_order") {
		StorageFixture fixture;
		const CorpusResult result = evaluate_corpus_case(
				String("var value: int = \"text\"\n").to_utf8_buffer(),
				"res://case.barista", "analyzer", PackedStringArray());
		CHECK_FALSE(result.ok);
		CHECK(result.analysis_ran);
		CHECK(result.output.begins_with(">> ERROR at line 1: "));
	}
	TEST_CASE("parser_stage_reports_first_message_bare") {
		StorageFixture fixture;
		const CorpusResult result = evaluate_corpus_case(
				String("func broken(\n").to_utf8_buffer(),
				"res://case.barista", "parser", PackedStringArray());
		CHECK_FALSE(result.ok);
		CHECK_FALSE(result.analysis_ran);
		CHECK_FALSE(result.output.begins_with(">> ERROR"));
	}
	TEST_CASE("invalid_utf8_is_a_diagnostic_not_an_infrastructure_error") {
		StorageFixture fixture;
		PackedByteArray bytes;
		bytes.push_back(0x76);
		bytes.push_back(0xFF);
		const CorpusResult result = evaluate_corpus_case(
				bytes, "res://case.barista", "analyzer", PackedStringArray());
		CHECK_FALSE(result.ok);
		CHECK_FALSE(result.infrastructure_error);
	}
	TEST_CASE("fixture_index_is_reported_only_for_a_case_that_named_fixtures") {
		StorageFixture fixture;
		Ref<BaristaScriptAnalyzerProbe> probe;
		probe.instantiate();
		const String source = "func test():\n\tpass\n";
		// Only the fixture path is read from disk; the case itself is supplied as bytes, so its
		// path is shape-validated rather than opened and need not name an existing file.
		const String path = "res://tests/oracle_fixtures/declarations/annotation_duplicate_in_imported_usage.barista";
		const Dictionary without_fixtures = probe->evaluate_corpus(source.to_utf8_buffer(), path, "analyzer");
		CHECK_FALSE(without_fixtures.has("fixture_index"));
		PackedStringArray fixtures;
		fixtures.push_back("res://tests/oracle_fixtures/declarations/annotation_duplicate_in_imported_lib.notest.barista");
		const Dictionary with_fixtures = probe->evaluate_corpus(source.to_utf8_buffer(), path, "analyzer", fixtures);
		BS_TEST_REQUIRE(with_fixtures.has("fixture_index"));
		CHECK(int(Dictionary(with_fixtures["fixture_index"])["sources"]) == 1);
		// An infrastructure error raised before fixture discovery reports no counts at all.
		const Dictionary rejected = probe->evaluate_corpus(source.to_utf8_buffer(), path, "unknown", fixtures);
		CHECK(bool(rejected["infrastructure_error"]));
		CHECK_FALSE(rejected.has("fixture_index"));
	}
	TEST_CASE("rejects_non_res_path") {
		StorageFixture fixture;
		const CorpusResult result = evaluate_corpus_case(
				String("var value: int = 1\n").to_utf8_buffer(),
				"/tmp/case.barista", "analyzer", PackedStringArray());
		CHECK(result.infrastructure_error);
	}
}
