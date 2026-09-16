/**************************************************************************/
/*  analyzer_corpus_test.cpp                                              */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "corpus_helpers.h"
#include "doctest.h"
#include "native_corpus_arguments.h"
#include "storage_fixture.h"
#include "test_require.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {

const char *const PRODUCER_ROOT = "res://tests/oracle_fixtures/producer/";

PackedByteArray bytes_of(const String &p_text) {
	return p_text.to_utf8_buffer();
}

PackedByteArray raw_bytes(const std::initializer_list<uint8_t> &p_values) {
	PackedByteArray bytes;
	for (uint8_t value : p_values) {
		bytes.push_back(value);
	}
	return bytes;
}

/** The pinned producer block for `p_name`, with its single trailing LF removed. */
String producer_block(const String &p_name) {
	return FileAccess::get_file_as_string(String(PRODUCER_ROOT) + p_name + ".block").trim_suffix("\n");
}

PackedByteArray producer_source(const String &p_name) {
	return FileAccess::get_file_as_bytes(String(PRODUCER_ROOT) + p_name + ".source");
}

bool remove_tree(const String &p_path) {
	if (!DirAccess::dir_exists_absolute(p_path)) {
		return true;
	}
	const PackedStringArray files = DirAccess::get_files_at(p_path);
	for (int i = 0; i < files.size(); i++) {
		DirAccess::remove_absolute(p_path.path_join(files[i]));
	}
	const PackedStringArray children = DirAccess::get_directories_at(p_path);
	for (int i = 0; i < children.size(); i++) {
		remove_tree(p_path.path_join(children[i]));
	}
	// Dot-prefixed markers are hidden from the listings above and must be removed by name.
	DirAccess::remove_absolute(p_path.path_join(CORPUS_IGNORE_MARKER));
	DirAccess::remove_absolute(p_path.path_join(".fsignore"));
	return DirAccess::remove_absolute(p_path) == OK;
}

/** A disposable `user://` corpus tree that is rebuilt empty on construction. */
class TemporaryCorpus {
	String root_path;

public:
	explicit TemporaryCorpus(const String &p_name) {
		root_path = String("user://analyzer_corpus_") + p_name;
		remove_tree(root_path);
		DirAccess::make_dir_recursive_absolute(root_path);
	}
	~TemporaryCorpus() { remove_tree(root_path); }

	const String &root() const { return root_path; }

	String write(const String &p_relative, const PackedByteArray &p_bytes) const {
		const String path = root_path.path_join(p_relative);
		DirAccess::make_dir_recursive_absolute(path.get_base_dir());
		const Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		if (file.is_valid()) {
			file->store_buffer(p_bytes);
		}
		return path;
	}

	String write(const String &p_relative, const String &p_text) const {
		return write(p_relative, bytes_of(p_text));
	}
};

/**
 * Revoke or restore read permission on one file and confirm the change took effect.
 *
 * Only a file is ever locked, never a directory: a mode-000 directory would survive the
 * disposable `user://` root's own cleanup, while an unreadable file is still unlinkable.
 */
bool set_file_readable(const String &p_path, bool p_readable) {
	OS *os = OS::get_singleton();
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (os == nullptr || settings == nullptr || os->get_name() == "Windows") {
		return false;
	}
	PackedStringArray arguments;
	arguments.push_back(p_readable ? "644" : "000");
	arguments.push_back(settings->globalize_path(p_path));
	Array output;
	if (os->execute("chmod", arguments, output) != 0) {
		return false;
	}
	return FileAccess::open(p_path, FileAccess::READ).is_valid() == p_readable;
}

CorpusResult synthetic_result(const String &p_output, bool p_ok = true, bool p_analysis_ran = true,
		bool p_infrastructure_error = false) {
	CorpusResult result;
	result.output = p_output;
	result.ok = p_ok;
	result.analysis_ran = p_analysis_ran;
	result.infrastructure_error = p_infrastructure_error;
	return result;
}

/** Capture what `emit_corpus_guards` writes to stdout so an ordinary run emits no guards. */
std::vector<std::string> captured_guard_lines(const CorpusModeReport &p_report) {
	std::ostringstream capture;
	std::streambuf *previous = std::cout.rdbuf(capture.rdbuf());
	emit_corpus_guards(p_report);
	std::cout.rdbuf(previous);
	std::vector<std::string> lines;
	std::string line;
	std::istringstream stream(capture.str());
	while (std::getline(stream, line)) {
		lines.push_back(line);
	}
	return lines;
}

PackedStringArray sorted_keys(const Dictionary &p_dictionary) {
	PackedStringArray keys;
	const Array raw = p_dictionary.keys();
	for (int i = 0; i < raw.size(); i++) {
		keys.push_back(raw[i]);
	}
	keys.sort();
	return keys;
}

} //namespace

TEST_SUITE("analyzer_corpus") {
	TEST_CASE("failure_reason_ordinals_match_the_gdscript_enum") {
		// These ordinals are serialized into BS_CASE_RESULT and read back by the supervisor,
		// so they are pinned to `FailureReason` in project/tests/corpus_harness.gd:42-49.
		static_assert(CORPUS_MISSING_EXPECTATION == 0, "MISSING_EXPECTATION is ordinal 0");
		static_assert(CORPUS_ORPHANED_EXPECTATION == 1, "ORPHANED_EXPECTATION is ordinal 1");
		static_assert(CORPUS_INVALID_EXPECTATION == 2, "INVALID_EXPECTATION is ordinal 2");
		static_assert(CORPUS_UNREADABLE_SOURCE == 3, "UNREADABLE_SOURCE is ordinal 3");
		static_assert(CORPUS_OUTPUT_MISMATCH == 4, "OUTPUT_MISMATCH is ordinal 4");
		static_assert(CORPUS_INVALID_RESULT == 5, "INVALID_RESULT is ordinal 5");
		CHECK(int(CORPUS_MISSING_EXPECTATION) == 0);
		CHECK(int(CORPUS_ORPHANED_EXPECTATION) == 1);
		CHECK(int(CORPUS_INVALID_EXPECTATION) == 2);
		CHECK(int(CORPUS_UNREADABLE_SOURCE) == 3);
		CHECK(int(CORPUS_OUTPUT_MISMATCH) == 4);
		CHECK(int(CORPUS_INVALID_RESULT) == 5);
	}

	TEST_CASE("expectation_gate_accepts_and_rejects_exact_byte_forms") {
		CHECK(check_expectation_bytes(bytes_of("BS_TEST_OK\n")).valid);
		CHECK(check_expectation_bytes(bytes_of("BS_TEST_OK\n")).expected == "BS_TEST_OK");
		// A trailing space is content, never whitespace to be trimmed away.
		CHECK(check_expectation_bytes(bytes_of("BS_TEST_OK \n")).expected == "BS_TEST_OK ");
		CHECK(check_expectation_bytes(bytes_of("first\nsecond\n")).expected == "first\nsecond");
		// An interior blank line is legal; only a trailing one is not.
		CHECK(check_expectation_bytes(bytes_of("first\n\nsecond\n")).valid);
		CHECK(check_expectation_bytes(bytes_of(String::utf8("caf\u00e9\n"))).valid);

		CHECK_FALSE(check_expectation_bytes(PackedByteArray()).valid);
		CHECK_FALSE(check_expectation_bytes(bytes_of("\n")).valid);
		CHECK_FALSE(check_expectation_bytes(bytes_of("first")).valid);
		CHECK_FALSE(check_expectation_bytes(bytes_of("first\n\n")).valid);
		CHECK_FALSE(check_expectation_bytes(bytes_of("first\r\n")).valid);
		CHECK_FALSE(check_expectation_bytes(bytes_of("first\rsecond\n")).valid);
		CHECK_FALSE(check_expectation_bytes(raw_bytes({ 97, 0, 10 })).valid);
		CHECK_FALSE(check_expectation_bytes(raw_bytes({ 255, 10 })).valid);
		CHECK_FALSE(check_expectation_bytes(raw_bytes({ 0xc0, 0x80, 10 })).valid);
	}

	TEST_CASE("comparison_trims_exactly_one_trailing_newline") {
		// Two LFs are rejected outright rather than trimmed twice, so a block can never
		// lose a genuine trailing blank line to normalization.
		CHECK(check_expectation_bytes(bytes_of("block\n")).expected == "block");
		CHECK_FALSE(check_expectation_bytes(bytes_of("block\n\n")).valid);
	}

	TEST_CASE("multi_line_block_mutations_all_mismatch") {
		const String block = producer_block("warnings__assert_always_true");
		BS_TEST_REQUIRE(block.split("\n").size() == 3);
		const PackedStringArray lines = block.split("\n");
		CorpusCase corpus_case;
		corpus_case.path = "user://analyzer_corpus_compare/case.barista";
		corpus_case.expectation_path = "user://analyzer_corpus_compare/case.out";
		corpus_case.stage = "analyzer";

		CHECK(compare_corpus_result(corpus_case, block, synthetic_result(block)).passed);
		const PackedStringArray mutations = {
			lines[0] + String("\n") + lines[1] + "changed\n" + lines[2],
			lines[0] + String("\n") + lines[2],
			block + String("\n") + lines[2],
			lines[0] + String("\n") + lines[2] + "\n" + lines[1],
			block + String(" "),
			String(" ") + block,
		};
		for (int i = 0; i < mutations.size(); i++) {
			const CorpusOutcome outcome = compare_corpus_result(corpus_case, block, synthetic_result(mutations[i]));
			CHECK_FALSE(outcome.passed);
			CHECK(outcome.reason == CORPUS_OUTPUT_MISMATCH);
			CHECK(outcome.actual == mutations[i]);
		}
	}

	TEST_CASE("warning_block_rendering_is_pinned_whole") {
		StorageFixture fixture;
		// The multi-line `\n`-join and the exact `~~ WARNING at line N: (CODE) message`
		// rendering are pinned here as whole blocks; a prefix check would accept the
		// wrong diagnostic under the right shape.
		const String expected = String::utf8(
				"~~ WARNING at line 5: (CONFUSABLE_IDENTIFIER) The identifier \"p\u03bfrt\" has misleading "
				"characters and might be confused with something else.\n"
				"~~ WARNING at line 12: (CONFUSABLE_IDENTIFIER) The identifier \"p\u03bfrt\" has misleading "
				"characters and might be confused with something else.");
		CHECK(producer_block("warnings__confusable_identifier") == expected);
		const CorpusResult result = evaluate_corpus_case(producer_source("warnings__confusable_identifier"),
				"res://tests/oracle_fixtures/producer/confusable.barista", "analyzer", PackedStringArray());
		CHECK(result.ok);
		CHECK(result.analysis_ran);
		CHECK_FALSE(result.infrastructure_error);
		CHECK(result.output == expected);

		const CorpusResult single = evaluate_corpus_case(producer_source("warnings__unused_variable"),
				"res://tests/oracle_fixtures/producer/unused.barista", "analyzer", PackedStringArray());
		CHECK(single.output ==
				"~~ WARNING at line 2: (UNUSED_VARIABLE) The local variable \"unused\" is declared but never "
				"used in the block. If this is intended, prefix it with an underscore: \"_unused\".");
	}

	TEST_CASE("error_block_rendering_is_pinned_whole") {
		StorageFixture fixture;
		const CorpusResult single = evaluate_corpus_case(producer_source("analyzer__leading_number_separator"),
				"res://tests/oracle_fixtures/producer/leading.barista", "analyzer", PackedStringArray());
		CHECK_FALSE(single.ok);
		CHECK(single.analysis_ran);
		CHECK(single.output == ">> ERROR at line 3: Identifier \"_123\" not declared in the current scope.");
		CHECK(producer_block("analyzer__leading_number_separator") == single.output);

		const CorpusResult multiple = evaluate_corpus_case(
				bytes_of("func test():\n\tvar first = _missing_one\n\tvar second = _missing_two\n"),
				"res://tests/oracle_fixtures/producer/two_errors.barista", "analyzer", PackedStringArray());
		CHECK_FALSE(multiple.ok);
		CHECK(multiple.output ==
				">> ERROR at line 2: Identifier \"_missing_one\" not declared in the current scope.\n"
				">> ERROR at line 3: Identifier \"_missing_two\" not declared in the current scope.");
		CHECK_FALSE(multiple.output.ends_with("\n"));
		CHECK_FALSE(multiple.output.contains("\r"));
	}

	TEST_CASE("mismatch_escaping_renders_visible_escapes") {
		// U+0000 cannot live in a Godot String, so U+0001 exercises the same \xNN path.
		const String exotic = String("\"\\\t\n") + String::chr(1) + String::chr(127) + String::utf8("abc\u00e9");
		CHECK(escape_mismatch_value(exotic) == String::utf8("\\\"\\\\\\t\\n\\x01\\x7fabc\u00e9"));
		CHECK(escape_mismatch_value("carriage\rreturn") == "carriage\\rreturn");
		CorpusCase corpus_case;
		corpus_case.path = "user://analyzer_corpus_escape/case.barista";
		corpus_case.expectation_path = "user://analyzer_corpus_escape/case.out";
		corpus_case.stage = "analyzer";
		const CorpusOutcome outcome = compare_corpus_result(corpus_case, "BS_TEST_OK", synthetic_result(exotic, false));
		CHECK(outcome.reason == CORPUS_OUTPUT_MISMATCH);
		CHECK(outcome.message == String::utf8("output mismatch\nexpected: \"BS_TEST_OK\"\nactual:   \"\\\"\\\\\\t\\n\\x01\\x7fabc\u00e9\""));
		// Escaping is presentation only; the comparison and the payload keep raw bytes.
		CHECK(outcome.actual == exotic);
	}

	TEST_CASE("result_shape_errors_match_the_gdscript_oracle") {
		CHECK(corpus_result_error(synthetic_result("BS_TEST_OK"), "analyzer").is_empty());
		CHECK(corpus_result_error(synthetic_result("BS_TEST_OK", true, false), "parser").is_empty());
		CHECK(corpus_result_error(synthetic_result("first", false, false), "parser").is_empty());
		CHECK(corpus_result_error(synthetic_result("", true), "analyzer") ==
				"malformed frontend result: nonempty output required");
		CHECK(corpus_result_error(synthetic_result("first", false, true), "parser") ==
				"malformed frontend result: inconsistent stage/outcome");
		CHECK(corpus_result_error(synthetic_result("first", true, true, true), "analyzer") ==
				"malformed frontend result: inconsistent stage/outcome");
		CHECK(corpus_result_error(synthetic_result("BS_TEST_OK", true, false), "analyzer") ==
				"malformed frontend result: inconsistent stage/outcome");
		CHECK(corpus_result_error(synthetic_result("BS_TEST_OK", false, true), "analyzer") ==
				"malformed frontend result: contradictory success block");
		CHECK(corpus_result_error(synthetic_result("other", true, false), "parser") ==
				"malformed frontend result: contradictory success block");
		CHECK(corpus_result_error(synthetic_result("first\r\nsecond", false, true), "analyzer") ==
				"malformed frontend result: invalid block terminator");
		CHECK(corpus_result_error(synthetic_result("first\n", false, true), "analyzer") ==
				"malformed frontend result: invalid block terminator");
	}

	TEST_CASE("case_result_shape_matches_the_gdscript_emitter") {
		TemporaryCorpus corpus("shape");
		corpus.write("case.barista", String("func test():\n\tpass\n"));
		CorpusCase corpus_case;
		corpus_case.path = corpus.root().path_join("case.barista");
		corpus_case.expectation_path = corpus.root().path_join("case.out");
		corpus_case.stage = "analyzer";

		const PackedStringArray pass_keys = { "actual", "analysis_ran", "expected", "fixture_index", "passed", "path" };
		const PackedStringArray failure_keys = { "actual", "expectation_path", "expected", "message", "passed", "path", "reason" };
		const PackedStringArray mismatch_keys = { "actual", "analysis_ran", "expectation_path", "expected",
			"fixture_index", "message", "passed", "path", "reason" };

		const Dictionary passed = corpus_case_result_dictionary(
				compare_corpus_result(corpus_case, "BS_TEST_OK", synthetic_result("BS_TEST_OK")));
		CHECK(sorted_keys(passed) == pass_keys);
		CHECK(bool(passed["passed"]));
		CHECK(Dictionary(passed["fixture_index"]).is_empty());

		const Dictionary mismatch = corpus_case_result_dictionary(
				compare_corpus_result(corpus_case, "BS_TEST_OK", synthetic_result("other", false)));
		CHECK(sorted_keys(mismatch) == mismatch_keys);
		CHECK(int(mismatch["reason"]) == CORPUS_OUTPUT_MISMATCH);

		const Dictionary invalid_result = corpus_case_result_dictionary(
				compare_corpus_result(corpus_case, "BS_TEST_OK", synthetic_result("", true)));
		CHECK(sorted_keys(invalid_result) == failure_keys);
		CHECK(int(invalid_result["reason"]) == CORPUS_INVALID_RESULT);

		// A missing expectation and an invalid one carry the same key set as any other
		// non-mismatch failure: `fixture_index` and `analysis_ran` are mismatch-only.
		const Dictionary missing = corpus_case_result_dictionary(run_corpus_case(corpus_case, PackedStringArray()));
		CHECK(sorted_keys(missing) == failure_keys);
		CHECK(int(missing["reason"]) == CORPUS_MISSING_EXPECTATION);
		CHECK(String(missing["message"]) == "missing expectation " + corpus_case.expectation_path);
		CHECK(String(missing["actual"]).is_empty());

		corpus.write("case.out", raw_bytes({ 0xff, 0xfe, 10 }));
		const Dictionary invalid_expectation = corpus_case_result_dictionary(run_corpus_case(corpus_case, PackedStringArray()));
		CHECK(sorted_keys(invalid_expectation) == failure_keys);
		CHECK(int(invalid_expectation["reason"]) == CORPUS_INVALID_EXPECTATION);
		CHECK(String(invalid_expectation["message"]) ==
				"expectation " + corpus_case.expectation_path + " is not valid UTF-8");

		// The open-failure branch is distinct from the decode and terminator branches: the
		// file still exists, so this must not degrade into MISSING_EXPECTATION.
		corpus.write("case.out", String("BS_TEST_OK\n"));
		if (set_file_readable(corpus_case.expectation_path, false)) {
			const Dictionary unreadable = corpus_case_result_dictionary(run_corpus_case(corpus_case, PackedStringArray()));
			const bool restored = set_file_readable(corpus_case.expectation_path, true);
			CHECK(sorted_keys(unreadable) == failure_keys);
			CHECK(int(unreadable["reason"]) == CORPUS_INVALID_EXPECTATION);
			CHECK(String(unreadable["message"]).begins_with("expectation " + corpus_case.expectation_path + " is unreadable (error "));
			CHECK_MESSAGE(restored, "the locked expectation must be readable again before cleanup");
		} else {
			// Revoking read permission is the only way to reach this branch; an environment
			// that cannot do it leaves the contract unverified rather than silently passing.
			CHECK_MESSAGE(FileAccess::file_exists(corpus_case.expectation_path),
					"could not revoke read permission; the unreadable-expectation branch went unverified");
		}

		corpus.write("case.out", String("BS_TEST_OK\r\n"));
		const Dictionary bad_terminator = corpus_case_result_dictionary(run_corpus_case(corpus_case, PackedStringArray()));
		CHECK(int(bad_terminator["reason"]) == CORPUS_INVALID_EXPECTATION);
		const String terminator_complaint = "expectation " + corpus_case.expectation_path +
				String(" requires a nonempty block with exactly one final LF, without CR or NUL");
		CHECK(String(bad_terminator["message"]) == terminator_complaint);
	}

	TEST_CASE("failure_expected_field_is_reread_from_disk") {
		TemporaryCorpus corpus("expected_field");
		corpus.write("case.barista", String("func test():\n\tpass\n"));
		corpus.write("case.out", String("on disk\n"));
		CorpusCase corpus_case;
		corpus_case.path = corpus.root().path_join("case.barista");
		corpus_case.expectation_path = corpus.root().path_join("case.out");
		corpus_case.stage = "analyzer";
		const CorpusOutcome outcome = compare_corpus_result(corpus_case, "supplied", synthetic_result("actual", false));
		CHECK(outcome.reason == CORPUS_OUTPUT_MISMATCH);
		// The emitter re-reads the file rather than reusing the gated block.
		CHECK(outcome.expected == "on disk");
	}

	TEST_CASE("gate_precedes_evaluation") {
		// A case whose source cannot be read still reports the expectation problem first:
		// the gate runs before the front end is ever asked to evaluate anything.
		TemporaryCorpus corpus("ordering");
		CorpusCase corpus_case;
		corpus_case.path = corpus.root().path_join("absent.barista");
		corpus_case.expectation_path = corpus.root().path_join("absent.out");
		corpus_case.stage = "analyzer";
		CHECK(run_corpus_case(corpus_case, PackedStringArray()).reason == CORPUS_MISSING_EXPECTATION);
		corpus.write("absent.out", String("BS_TEST_OK\n\n"));
		CHECK(run_corpus_case(corpus_case, PackedStringArray()).reason == CORPUS_INVALID_EXPECTATION);
	}

	TEST_CASE("unreadable_source_is_reason_three") {
		TemporaryCorpus corpus("unreadable_source");
		corpus.write("case.out", String("BS_TEST_OK\n"));
		CorpusCase corpus_case;
		corpus_case.path = corpus.root().path_join("case.barista");
		corpus_case.expectation_path = corpus.root().path_join("case.out");
		corpus_case.stage = "analyzer";
		const CorpusOutcome outcome = run_corpus_case(corpus_case, PackedStringArray());
		CHECK(outcome.reason == CORPUS_UNREADABLE_SOURCE);
		CHECK(outcome.message.begins_with("source " + corpus_case.path + " is unreadable (error "));
		// An empty but readable source is ordinary input, never an unreadable one.
		StorageFixture fixture;
		const CorpusResult empty = evaluate_corpus_case(PackedByteArray(),
				"res://tests/corpus_fixtures/passing/paired_ok.barista", "parser", PackedStringArray());
		CHECK_FALSE(empty.infrastructure_error);
		CHECK(empty.ok);
	}

	TEST_CASE("paired_case_runs_end_to_end_against_its_expectation") {
		StorageFixture fixture;
		const CorpusDiscovery discovery = discover_corpus("res://tests/corpus_fixtures/passing");
		BS_TEST_REQUIRE(discovery.cases.size() == 1);
		CorpusCase corpus_case = discovery.cases[0];
		corpus_case.stage = "parser";
		const CorpusOutcome outcome = run_corpus_case(corpus_case, PackedStringArray());
		CHECK_MESSAGE(outcome.passed, outcome.message.utf8().get_data());
		CHECK(outcome.expected == "BS_TEST_OK");
		CHECK(outcome.actual == "BS_TEST_OK");
		CHECK_FALSE(outcome.analysis_ran);
		CHECK(outcome.fixture_index.is_empty());

		// The same pair evaluated against a drifted expectation is an exact-comparison
		// mismatch rather than anything the gate or the taxonomy absorbs.
		CorpusCase drifted = corpus_case;
		drifted.expectation_path = "res://tests/corpus_fixtures/trailing_whitespace_drift/case.out";
		const CorpusOutcome mismatch = run_corpus_case(drifted, PackedStringArray());
		CHECK(mismatch.reason == CORPUS_OUTPUT_MISMATCH);
		CHECK(mismatch.expected == "BS_TEST_OK ");
		CHECK(mismatch.actual == "BS_TEST_OK");
	}

	TEST_CASE("infrastructure_error_is_reason_five") {
		CorpusCase corpus_case;
		corpus_case.path = "user://analyzer_corpus_infrastructure/case.barista";
		corpus_case.expectation_path = "user://analyzer_corpus_infrastructure/case.out";
		corpus_case.stage = "analyzer";
		const CorpusOutcome outcome = compare_corpus_result(corpus_case, "BS_TEST_OK",
				synthetic_result("Frontend failed without a diagnostic.", false, true, true));
		CHECK(outcome.reason == CORPUS_INVALID_RESULT);
		CHECK(outcome.message == "Frontend failed without a diagnostic.");
	}

	TEST_CASE("discovery_pairs_cases_by_basename") {
		const CorpusDiscovery discovery = discover_corpus("res://tests/corpus_fixtures/passing");
		BS_TEST_REQUIRE(discovery.cases.size() == 1);
		CHECK(discovery.cases[0].path == "res://tests/corpus_fixtures/passing/paired_ok.barista");
		CHECK(discovery.cases[0].expectation_path == "res://tests/corpus_fixtures/passing/paired_ok.out");
		CHECK(discovery.orphaned_expectations.is_empty());
		CHECK(discovery.discovery_errors.is_empty());
		CHECK(discovery.unreadable_directories.is_empty());
	}

	TEST_CASE("notest_helper_is_never_a_case") {
		const CorpusDiscovery discovery = discover_corpus("res://tests/corpus_fixtures/passing");
		CHECK(discovery.cases.size() == 1);
		// helper.notest.barista is present, is counted as skipped, and never demands a .out.
		CHECK(discovery.skipped_count == 1);
		CHECK(discovery.orphaned_expectations.is_empty());
		CHECK_FALSE(valid_case_relative("helper.notest.barista"));
	}

	TEST_CASE("orphaned_expectation_is_reported_without_a_case") {
		const CorpusDiscovery discovery = discover_corpus("res://tests/corpus_fixtures/orphaned_expectation");
		CHECK(discovery.cases.is_empty());
		BS_TEST_REQUIRE(discovery.orphaned_expectations.size() == 1);
		CHECK(discovery.orphaned_expectations[0] == "res://tests/corpus_fixtures/orphaned_expectation/case.out");
	}

	TEST_CASE("ignore_marker_is_inherited_by_descendants") {
		TemporaryCorpus corpus("ignore_marker");
		corpus.write("collected/case.barista", String("func test():\n\tpass\n"));
		corpus.write("collected/case.out", String("BS_TEST_OK\n"));
		corpus.write("hidden/case.barista", String("func test():\n\tpass\n"));
		corpus.write("hidden/case.out", String("BS_TEST_OK\n"));
		corpus.write("hidden/deeper/nested.barista", String("func test():\n\tpass\n"));
		corpus.write("hidden/deeper/nested.out", String("BS_TEST_OK\n"));
		corpus.write("hidden/orphan.out", String("BS_TEST_OK\n"));
		corpus.write("hidden/" + String(CORPUS_IGNORE_MARKER), String(""));

		const CorpusDiscovery discovery = discover_corpus(corpus.root());
		BS_TEST_REQUIRE(discovery.cases.size() == 1);
		CHECK(discovery.cases[0].path == corpus.root().path_join("collected/case.barista"));
		// The marker is inherited: the nested descendant is skipped too, and skipped cases
		// are counted rather than failed.
		CHECK(discovery.skipped_count == 2);
		// An ignored directory's expectations are not orphans either.
		CHECK(discovery.orphaned_expectations.is_empty());
		CHECK(discovery.discovery_errors.is_empty());
	}

	TEST_CASE("fsignore_is_not_an_ignore_marker") {
		// The upstream corpus ships its own .fsignore. Honoring it would silently skip every
		// case while the run still exited zero, so the marker name is deliberately different.
		TemporaryCorpus corpus("fsignore");
		corpus.write("case.barista", String("func test():\n\tpass\n"));
		corpus.write("case.out", String("BS_TEST_OK\n"));
		corpus.write(".fsignore", String(""));
		const CorpusDiscovery discovery = discover_corpus(corpus.root());
		CHECK(discovery.cases.size() == 1);
		CHECK(discovery.skipped_count == 0);
	}

	TEST_CASE("discovery_order_is_path_sorted") {
		TemporaryCorpus corpus("sorted");
		const PackedStringArray names = { "zulu", "alpha", "mike" };
		for (int i = 0; i < names.size(); i++) {
			corpus.write(names[i] + String("/case.barista"), String("func test():\n\tpass\n"));
			corpus.write(names[i] + String("/case.out"), String("BS_TEST_OK\n"));
			corpus.write(names[i] + String("/orphan.out"), String("BS_TEST_OK\n"));
		}
		const CorpusDiscovery discovery = discover_corpus(corpus.root());
		BS_TEST_REQUIRE(discovery.cases.size() == 3);
		CHECK(discovery.cases[0].path == corpus.root().path_join("alpha/case.barista"));
		CHECK(discovery.cases[1].path == corpus.root().path_join("mike/case.barista"));
		CHECK(discovery.cases[2].path == corpus.root().path_join("zulu/case.barista"));
		BS_TEST_REQUIRE(discovery.orphaned_expectations.size() == 3);
		CHECK(discovery.orphaned_expectations[0] == corpus.root().path_join("alpha/orphan.out"));
		CHECK(discovery.orphaned_expectations[2] == corpus.root().path_join("zulu/orphan.out"));
	}

	TEST_CASE("unreadable_directory_is_recorded_not_skipped") {
		// A directory that cannot be opened hides an unknown number of cases, so it is
		// recorded rather than quietly shrinking the corpus.
		const String absent = "user://analyzer_corpus_absent_root";
		remove_tree(absent);
		const CorpusDiscovery discovery = discover_corpus(absent);
		CHECK(discovery.cases.is_empty());
		BS_TEST_REQUIRE(discovery.unreadable_directories.size() == 1);
		CHECK(discovery.unreadable_directories[0] == absent);
	}

	TEST_CASE("symlinked_entries_are_rejected_without_traversal") {
		TemporaryCorpus corpus("symlinks");
		TemporaryCorpus target("symlink_target");
		target.write("case.barista", String("func test():\n\tpass\n"));
		target.write("case.out", String("BS_TEST_OK\n"));
		corpus.write("real.barista", String("func test():\n\tpass\n"));
		corpus.write("real.out", String("BS_TEST_OK\n"));

		ProjectSettings *settings = ProjectSettings::get_singleton();
		BS_TEST_REQUIRE(settings != nullptr);
		Ref<DirAccess> directory = DirAccess::open(corpus.root());
		BS_TEST_REQUIRE(directory.is_valid());
		const String link_error = "unsupported corpus symlink: ";

		struct LinkedEntry {
			const char *name;
			const char *destination;
		};
		const LinkedEntry entries[] = {
			{ "linked.barista", "case.barista" },
			{ "real_alias.out", "case.out" },
			{ "linked_directory", nullptr },
		};
		for (const LinkedEntry &entry : entries) {
			const String link = corpus.root().path_join(entry.name);
			const String destination = entry.destination == nullptr ? target.root() : target.root().path_join(entry.destination);
			BS_TEST_REQUIRE(directory->create_link(settings->globalize_path(destination), settings->globalize_path(link)) == OK);
			const CorpusDiscovery discovery = discover_corpus(corpus.root());
			CHECK(discovery.discovery_errors.has(link_error + link));
			// The real neighbour is still collected, the link is never classified as a case
			// or an expectation, and a linked directory is never traversed into.
			BS_TEST_REQUIRE(discovery.cases.size() == 1);
			CHECK(discovery.cases[0].path == corpus.root().path_join("real.barista"));
			CHECK(discovery.orphaned_expectations.is_empty());
			CHECK(discovery.skipped_count == 0);
			DirAccess::remove_absolute(link);
		}

		// A linked ignore marker is reported and still suppresses collection: the marker is
		// probed by path, so refusing the link must not quietly re-enable an ignored tree.
		const String marker_link = corpus.root().path_join(CORPUS_IGNORE_MARKER);
		BS_TEST_REQUIRE(directory->create_link(settings->globalize_path(target.root().path_join("case.out")),
								settings->globalize_path(marker_link)) == OK);
		const CorpusDiscovery ignored = discover_corpus(corpus.root());
		CHECK(ignored.discovery_errors.has(link_error + corpus.root() + String("/") + CORPUS_IGNORE_MARKER));
		CHECK(ignored.cases.is_empty());
		CHECK(ignored.skipped_count == 1);
		DirAccess::remove_absolute(marker_link);

		const CorpusDiscovery restored = discover_corpus(corpus.root());
		CHECK(restored.discovery_errors.is_empty());
		CHECK(restored.cases.size() == 1);
	}

	TEST_CASE("valid_case_relative_rejects_traversal_and_helpers") {
		CHECK(valid_case_relative("case.barista"));
		CHECK(valid_case_relative("errors/nested/case.barista"));
		CHECK_FALSE(valid_case_relative("case.out"));
		CHECK_FALSE(valid_case_relative("helper.notest.barista"));
		CHECK_FALSE(valid_case_relative("/absolute/case.barista"));
		CHECK_FALSE(valid_case_relative("../escape/case.barista"));
		CHECK_FALSE(valid_case_relative("./case.barista"));
		CHECK_FALSE(valid_case_relative("double//case.barista"));
		CHECK_FALSE(valid_case_relative("windows\\case.barista"));
		CHECK_FALSE(valid_case_relative(""));
	}

	TEST_CASE("strict_json_matrix_matches_the_shared_fixture") {
		const Variant parsed = JSON::parse_string(
				FileAccess::get_file_as_string("res://tests/oracle_fixtures/json_contract.json"));
		BS_TEST_REQUIRE(parsed.get_type() == Variant::DICTIONARY);
		const Dictionary matrix = parsed;
		const Array valid = matrix["valid"];
		const Array invalid = matrix["invalid"];
		CHECK(valid.size() > 0);
		CHECK(invalid.size() > 0);
		for (int i = 0; i < valid.size(); i++) {
			CHECK_MESSAGE(validate_strict_json(String(valid[i])).is_empty(), String(valid[i]).utf8().get_data());
		}
		for (int i = 0; i < invalid.size(); i++) {
			CHECK_MESSAGE(!validate_strict_json(String(invalid[i])).is_empty(), String(invalid[i]).utf8().get_data());
		}
		// Duplicate decoded keys are rejected even when their escapes differ.
		CHECK_FALSE(validate_strict_json("{\"cases\":{\"a\":1,\"a\":2}}").is_empty());
		CHECK_FALSE(validate_strict_json("{\"cases\":{\"a\":1,\"\\u0061\":2}}").is_empty());
		// schema_version must be the integer 1, never a float or a Boolean spelling.
		CHECK(validate_strict_json("{\"schema_version\":1}").is_empty());
		CHECK_FALSE(validate_strict_json("{\"schema_version\":1.0}").is_empty());
		CHECK_FALSE(validate_strict_json("{\"schema_version\":2}").is_empty());
		CHECK_FALSE(validate_strict_json("{\"schema_version\":true}").is_empty());

		TemporaryCorpus corpus("strict_json");
		corpus.write("document.json", String("{\"a\":1,}"));
		CHECK_FALSE(read_unique_json(corpus.root().path_join("document.json")).error.is_empty());
		corpus.write("document.json", String("[]"));
		CHECK_FALSE(read_unique_json(corpus.root().path_join("document.json")).error.is_empty());
		corpus.write("document.json", raw_bytes({ '{', '"', 0xff, '"', ':', '1', '}' }));
		CHECK_FALSE(read_unique_json(corpus.root().path_join("document.json")).error.is_empty());
		CHECK_FALSE(read_unique_json(corpus.root().path_join("absent.json")).error.is_empty());
		corpus.write("document.json", String("{\"a\":1}"));
		CHECK(read_unique_json(corpus.root().path_join("document.json")).error.is_empty());
	}

	TEST_CASE("stage_manifest_shape_is_validated") {
		TemporaryCorpus corpus("stages");
		corpus.write("case.barista", String("func test():\n\tpass\n"));
		corpus.write("case.out", String("BS_TEST_OK\n"));
		const String revision = "c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6";
		Dictionary cases;
		cases["case.barista"] = "parser";
		Dictionary manifest;
		manifest["schema_version"] = 1;
		manifest["foundry_revision"] = revision;
		manifest["cases"] = cases;

		const CorpusStageManifest valid = validate_stage_manifest(manifest, corpus.root(), revision);
		CHECK(valid.error.is_empty());
		CHECK(String(valid.stages[corpus.root().path_join("case.barista")]) == "parser");

		CHECK_FALSE(validate_stage_manifest(manifest, corpus.root(), "other revision").error.is_empty());
		for (const char *field : { "schema_version", "foundry_revision" }) {
			Dictionary stale = manifest.duplicate(true);
			stale[field] = "stale";
			CHECK_FALSE(validate_stage_manifest(stale, corpus.root(), revision).error.is_empty());
		}
		Dictionary extra_key = manifest.duplicate(true);
		extra_key["unexpected"] = 1;
		CHECK_FALSE(validate_stage_manifest(extra_key, corpus.root(), revision).error.is_empty());

		const PackedStringArray rejected_case_keys = { "", "other.barista", "../case.barista", "case.out" };
		for (int i = 0; i < rejected_case_keys.size(); i++) {
			Dictionary replacement;
			replacement[rejected_case_keys[i]] = "parser";
			Dictionary invalid = manifest.duplicate(true);
			invalid["cases"] = replacement;
			CHECK_FALSE(validate_stage_manifest(invalid, corpus.root(), revision).error.is_empty());
		}
		Dictionary empty_cases;
		Dictionary without_entries = manifest.duplicate(true);
		without_entries["cases"] = empty_cases;
		CHECK_FALSE(validate_stage_manifest(without_entries, corpus.root(), revision).error.is_empty());

		Dictionary bogus_stage;
		bogus_stage["case.barista"] = "bogus";
		Dictionary invalid_stage = manifest.duplicate(true);
		invalid_stage["cases"] = bogus_stage;
		CHECK_FALSE(validate_stage_manifest(invalid_stage, corpus.root(), revision).error.is_empty());

		Dictionary helper_entry;
		helper_entry["case.barista"] = "parser";
		helper_entry["helper.notest.barista"] = "analyzer";
		Dictionary with_helper = manifest.duplicate(true);
		with_helper["cases"] = helper_entry;
		CHECK_FALSE(validate_stage_manifest(with_helper, corpus.root(), revision).error.is_empty());

		// An unreadable directory aborts validation instead of shrinking the manifest.
		CHECK_FALSE(validate_stage_manifest(manifest, "user://analyzer_corpus_absent_stage_root", revision).error.is_empty());

		// So does a symlinked entry: a manifest validated over a tree with an alias in it
		// would be attesting to cases the run refuses to execute.
		ProjectSettings *settings = ProjectSettings::get_singleton();
		Ref<DirAccess> directory = DirAccess::open(corpus.root());
		BS_TEST_REQUIRE(settings != nullptr);
		BS_TEST_REQUIRE(directory.is_valid());
		const String link = corpus.root().path_join("alias.barista");
		BS_TEST_REQUIRE(directory->create_link(settings->globalize_path(corpus.root().path_join("case.barista")),
								settings->globalize_path(link)) == OK);
		const CorpusStageManifest aliased = validate_stage_manifest(manifest, corpus.root(), revision);
		DirAccess::remove_absolute(link);
		CHECK(aliased.error == "unsupported corpus symlink: " + link);
		CHECK(aliased.stages.is_empty());
		CHECK(validate_stage_manifest(manifest, corpus.root(), revision).error.is_empty());

		// The committed parser corpus validates against the real registry revision.
		const CorpusJsonDocument registry = read_unique_json("res://../scripts/corpus_sources.json");
		if (registry.error.is_empty()) {
			const CorpusJsonDocument document = read_unique_json("res://tests/corpus/parser/case_stages.json");
			CHECK(document.error.is_empty());
			const CorpusStageManifest imported = validate_stage_manifest(
					document.document, "res://tests/corpus/parser", registry.document["revision"]);
			CHECK(imported.error.is_empty());
			CHECK(imported.stages.size() > 300);
		}
	}

	TEST_CASE("fixture_source_paths_are_sorted_and_barista_only") {
		const PackedStringArray paths = fixture_source_paths("res://tests/corpus_support/parser");
		CHECK(paths.size() > 0);
		for (int i = 0; i < paths.size(); i++) {
			CHECK(String(paths[i]).ends_with(CORPUS_CASE_EXTENSION));
			CHECK(String(paths[i]).begins_with("res://tests/corpus_support/parser/"));
			if (i > 0) {
				// evaluate_corpus_case rejects an unsorted fixture list outright.
				CHECK(String(paths[i - 1]) < String(paths[i]));
			}
		}
		CHECK(fixture_source_paths("res://tests/does_not_exist").is_empty());
		const PackedStringArray staging = staging_fixture_paths();
		for (int i = 1; i < staging.size(); i++) {
			CHECK(String(staging[i - 1]) < String(staging[i]));
		}
		CHECK(corpus_path_under(CORPUS_ANALYZER_STAGING_ROOT, "res://tests/corpus_staging"));
		CHECK(corpus_path_under("res://tests/corpus_staging", "res://tests/corpus_staging"));
		CHECK_FALSE(corpus_path_under("res://tests/corpus_staging_other", "res://tests/corpus_staging"));
	}

	TEST_CASE("exact_case_selection_requires_one_discovered_case") {
		TemporaryCorpus corpus("selection");
		corpus.write("errors/case.barista", String("func test():\n\tpass\n"));
		corpus.write("errors/case.out", String("BS_TEST_OK\n"));
		corpus.write("errors/helper.notest.barista", String("func helper():\n\tpass\n"));
		const CorpusDiscovery discovery = discover_corpus(corpus.root());
		BS_TEST_REQUIRE(discovery.cases.size() == 1);

		String error = "not cleared";
		const int selected = select_discovered_case(discovery, corpus.root(), "errors/case.barista", &error);
		BS_TEST_REQUIRE(selected == 0);
		CHECK(error.is_empty());
		// The selected identity is the corpus root joined with the relative case, which is
		// exactly what the supervisor re-derives from the BS_CASE_RESULT payload.
		CHECK(discovery.cases[selected].path == corpus.root().path_join("errors/case.barista"));

		const PackedStringArray undiscovered = { "errors/missing.barista", "case.barista",
			"errors/helper.notest.barista", "errors/case.out" };
		for (int i = 0; i < undiscovered.size(); i++) {
			CHECK(select_discovered_case(discovery, corpus.root(), undiscovered[i], &error) == -1);
			CHECK(error == "exact case was not discovered: " + String(undiscovered[i]));
		}

		// An ambiguous identity is refused rather than resolved to either match.
		CorpusDiscovery duplicated;
		duplicated.cases.push_back(discovery.cases[0]);
		duplicated.cases.push_back(discovery.cases[0]);
		CHECK(select_discovered_case(duplicated, corpus.root(), "errors/case.barista", &error) == -1);
		CHECK(error == "exact case was not discovered: errors/case.barista");
	}

	TEST_CASE("corpus_root_normalization_rejects_aliases_and_outside_paths") {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		BS_TEST_REQUIRE(settings != nullptr);
		String error = "not cleared";
		const String resource_root = "res://tests/corpus_fixtures/passing";
		CHECK(normalize_corpus_root(resource_root, &error) == resource_root);
		CHECK(error.is_empty());
		// An absolute filesystem spelling of the same directory is one identity with it.
		CHECK(normalize_corpus_root(settings->globalize_path(resource_root), &error) == resource_root);
		CHECK(error.is_empty());
		CHECK(normalize_corpus_root(resource_root + String("/"), &error) == resource_root);
		CHECK(normalize_corpus_root("res://tests/corpus_fixtures/../corpus_fixtures/passing", &error) == resource_root);

		// A path outside the project has no resource identity at all.
		CHECK(normalize_corpus_root("/tmp", &error).is_empty());
		CHECK(error == "corpus root is outside the project: /tmp");
		CHECK(normalize_corpus_root("res://tests/does_not_exist", &error).is_empty());
		CHECK(error == "corpus root is not a readable directory: res://tests/does_not_exist");

		// A symlinked root is refused rather than resolved. This is the whole reason the
		// alias machinery of corpus_harness.gd stays unported: a read-only run never needs
		// to follow an alias, and refusing one cannot write through it.
		TemporaryCorpus target("normalize_target");
		target.write("case.barista", String("func test():\n\tpass\n"));
		Ref<DirAccess> directory = DirAccess::open("user://");
		BS_TEST_REQUIRE(directory.is_valid());
		const String alias = "user://analyzer_corpus_normalize_alias";
		DirAccess::remove_absolute(alias);
		BS_TEST_REQUIRE(directory->create_link(settings->globalize_path(target.root()),
								settings->globalize_path(alias)) == OK);
		const String refused = normalize_corpus_root(alias, &error);
		DirAccess::remove_absolute(alias);
		CHECK(refused.is_empty());
		CHECK(error == "unsupported corpus symlink: " + alias);
	}

	TEST_CASE("guard_lines_are_ordered_result_then_ran") {
		// The supervisor parses BS_CASE_RESULT before BS_CASE_RAN, and BS_CASE_RAN means the
		// case ran, not that it passed, so a failing case emits both lines too.
		CorpusModeReport report;
		report.selected = true;
		report.relative_case = "errors/case.barista";
		report.outcome.passed = false;
		report.outcome.reason = CORPUS_OUTPUT_MISMATCH;
		report.outcome.path = "res://tests/corpus/parser/errors/case.barista";
		report.outcome.expectation_path = "res://tests/corpus/parser/errors/case.out";
		report.outcome.message = "output mismatch";
		report.outcome.expected = "expected \"block\"";
		report.outcome.actual = "actual\tblock";
		const String payload = JSON::stringify(corpus_case_result_dictionary(report.outcome));
		const Variant decoded = JSON::parse_string(payload);
		BS_TEST_REQUIRE(decoded.get_type() == Variant::DICTIONARY);
		const Dictionary round_tripped = decoded;
		CHECK(String(round_tripped["path"]) == report.outcome.path);
		CHECK(String(round_tripped["actual"]) == report.outcome.actual);
		CHECK(String(round_tripped["expected"]) == report.outcome.expected);
		CHECK_FALSE(bool(round_tripped["passed"]));
		CHECK(int(round_tripped["reason"]) == 4);
		// The payload is one line: JSON escaping keeps tabs and quotes out of the transport.
		CHECK_FALSE(payload.contains("\n"));
		CHECK_FALSE(payload.contains("\t"));

		const std::vector<std::string> lines = captured_guard_lines(report);
		BS_TEST_REQUIRE(lines.size() == 2);
		// BS_CASE_RESULT comes first; BS_CASE_RAN reports that the case ran, not that it passed.
		CHECK(lines[0] == std::string("BS_CASE_RESULT ") + payload.utf8().get_data());
		CHECK(lines[1] == "BS_CASE_RAN errors/case.barista");
	}

	TEST_CASE("selected_corpus_case_emits_guards") {
		// Ordinary suite runs pass no corpus selection and this case is a no-op beyond the
		// assertion that no selection was made.
		if (corpus_case().is_empty()) {
			CHECK(corpus_root().is_empty());
			return;
		}
		StorageFixture fixture;
		const CorpusModeReport report = run_selected_corpus_case();
		emit_corpus_guards(report);
		CHECK(report.selected);
		CHECK_MESSAGE(report.error.is_empty(), report.error.utf8().get_data());
		CHECK_MESSAGE(report.outcome.passed, report.outcome.message.utf8().get_data());
	}
}
