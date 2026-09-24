/**************************************************************************/
/*  runtime_corpus_test.cpp                                               */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "corpus_helpers.h"
#include "doctest.h"
#include "native_corpus_arguments.h"
#include "runtime_helpers.h"
#include "storage_fixture.h"
#include "test_require.h"

#include "bs_corpus_evaluation.h"

#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {

/** Evaluate one in-memory case at the runtime stage, with no staged fixtures. */
CorpusResult evaluate(const String &p_source, const String &p_path = "res://tests/corpus/runtime/probe.barista") {
	return evaluate_runtime_case(p_source.to_utf8_buffer(), p_path, "runtime", PackedStringArray());
}

/** The transcript's status token: its first line, which every transcript has. */
String status_of(const CorpusResult &p_result) {
	return p_result.output.get_slice("\n", 0);
}

/** The transcript below its status token, which may be empty. */
PackedStringArray body_of(const CorpusResult &p_result) {
	PackedStringArray lines = p_result.output.split("\n");
	lines.remove_at(0);
	return lines;
}

} // namespace

TEST_SUITE("runtime_corpus") {
	TEST_CASE("printed output renders as the ok status and one line per print") {
		const CorpusResult result = evaluate(
				"func test():\n"
				"\tprint(1)\n"
				"\tprint(2)\n");
		CHECK_MESSAGE(!result.infrastructure_error, readable(result.output));
		CHECK(result.ok);
		CHECK(result.analysis_ran);
		CHECK(result.output == String("FS_TEST_OK\n1\n2"));
	}

	TEST_CASE("a case that prints nothing is the bare status token") {
		const CorpusResult result = evaluate(
				"func test():\n"
				"\tpass\n");
		CHECK_MESSAGE(!result.infrastructure_error, readable(result.output));
		CHECK(result.output == String("FS_TEST_OK"));
		CHECK(body_of(result).is_empty());
	}

	TEST_CASE("a multi-line print becomes one transcript line per line") {
		// A `print` of embedded newlines arrives as a single message. Stored whole it would
		// render as one transcript entry containing a newline, and every later comparison would
		// be against a block whose line count disagrees with the expectation's.
		const CorpusResult result = evaluate(
				"func test():\n"
				"\tprint(\"alpha\\nbeta\")\n");
		CHECK(result.output == String("FS_TEST_OK\nalpha\nbeta"));
	}

	TEST_CASE("a bare print is a blank transcript line, not an absent one") {
		// `features/recursion` prints one between two results and its expectation carries the
		// blank line, so dropping it would shift every later line of that transcript by one.
		const CorpusResult result = evaluate(
				"func test():\n"
				"\tprint(\"before\")\n"
				"\tprint()\n"
				"\tprint(\"after\")\n");
		CHECK_MESSAGE(!result.infrastructure_error, readable(result.output));
		CHECK(result.output == String("FS_TEST_OK\nbefore\n\nafter"));
		const PackedStringArray body = body_of(result);
		BS_TEST_REQUIRE(body.size() == 3);
		CHECK(body[1] == String());
	}

	TEST_CASE("consecutive blank printed lines are all kept") {
		const CorpusResult result = evaluate(
				"func test():\n"
				"\tprint()\n"
				"\tprint()\n"
				"\tprint(\"end\")\n");
		CHECK(result.output == String("FS_TEST_OK\n\n\nend"));
		CHECK(body_of(result).size() == 3);
	}

	TEST_CASE("a print whose payload contains a blank line keeps it") {
		const CorpusResult result = evaluate(
				"func test():\n"
				"\tprint(\"alpha\\n\\nomega\")\n");
		CHECK(result.output == String("FS_TEST_OK\nalpha\n\nomega"));
		CHECK(body_of(result).size() == 3);
	}

	TEST_CASE("a runtime error renders the script error line and selects its status") {
		const CorpusResult result = evaluate(
				"func test():\n"
				"\tvar divisor: int = 0\n"
				"\tprint(1 % divisor)\n",
				"res://tests/corpus/runtime/modulo.barista");
		CHECK_MESSAGE(!result.infrastructure_error, readable(result.output));
		CHECK_FALSE(result.ok);
		CHECK(status_of(result) == String("FS_TEST_RUNTIME_ERROR"));
		const PackedStringArray body = body_of(result);
		BS_TEST_REQUIRE(body.size() >= 1);
		const String error_line = body[body.size() - 1];
		CHECK_MESSAGE(error_line.begins_with(">> SCRIPT ERROR at res://tests/corpus/runtime/modulo.barista:3 on test(): "),
				readable(error_line));
	}

	TEST_CASE("an analyzer error replaces the whole transcript") {
		// The producer never reaches the virtual machine for a front-end diagnostic, so there
		// is no printed output to interleave and the error block is the entire body.
		const CorpusResult result = evaluate(
				"func test() -> int:\n"
				"\treturn \"not an int\"\n");
		CHECK_MESSAGE(!result.infrastructure_error, readable(result.output));
		CHECK_FALSE(result.ok);
		CHECK(result.analysis_ran);
		CHECK(status_of(result) == String("FS_TEST_ANALYZER_ERROR"));
		const PackedStringArray body = body_of(result);
		BS_TEST_REQUIRE(body.size() == 1);
		CHECK_MESSAGE(body[0].begins_with(">> ERROR at line 2: "), readable(body[0]));
	}

	TEST_CASE("warnings precede everything the case prints") {
		const CorpusResult result = evaluate(
				"func test():\n"
				"\tvar unused: int = 1\n"
				"\tprint(\"ran\")\n");
		CHECK_MESSAGE(!result.infrastructure_error, readable(result.output));
		const PackedStringArray body = body_of(result);
		BS_TEST_REQUIRE(body.size() == 2);
		CHECK_MESSAGE(body[0].begins_with("~~ WARNING at line 2: (UNUSED_VARIABLE) "), readable(body[0]));
		CHECK(body[1] == String("ran"));
	}

	TEST_CASE("a refused compilation carries a prefix no expectation can hold") {
		// The pin is a burndown board, and a family that is not implemented yet must never
		// coincide with a real expectation. `>> COMPILE ERROR: ` appears in no imported `.out`
		// file, so a refusal can only ever compare unequal.
		const CorpusResult result = evaluate(
				"func test():\n"
				"\tvar greet := func(): print(\"hi\")\n"
				"\tgreet.call()\n");
		CHECK_MESSAGE(!result.infrastructure_error, readable(result.output));
		CHECK_FALSE(result.ok);
		CHECK(status_of(result) == String("FS_TEST_RUNTIME_ERROR"));
		const PackedStringArray body = body_of(result);
		BS_TEST_REQUIRE(body.size() >= 1);
		CHECK_MESSAGE(body[body.size() - 1].begins_with(">> COMPILE ERROR: "), readable(body[body.size() - 1]));
		// The guarantee the shape rests on, asserted rather than assumed.
		CHECK_FALSE(String(RUNTIME_COMPILE_ERROR_PREFIX).begins_with(">> ERROR"));
		CHECK_FALSE(String(RUNTIME_COMPILE_ERROR_PREFIX).begins_with(">> SCRIPT ERROR"));
	}

	TEST_CASE("the transcript reader is released after every case, including a failing one") {
		const int before = BaristaScriptCorpusTranscript::live_instances();
		evaluate("func test():\n\tprint(\"ok\")\n");
		CHECK(BaristaScriptCorpusTranscript::live_instances() == before);
		evaluate("func test():\n\tvar divisor: int = 0\n\tprint(1 / divisor)\n");
		CHECK(BaristaScriptCorpusTranscript::live_instances() == before);
		// A refused compilation returns before the reader is ever installed; a case that
		// returned with one installed would leave the count raised here.
		evaluate("func test():\n\tvar greet := func(): print(\"hi\")\n\tgreet.call()\n");
		CHECK(BaristaScriptCorpusTranscript::live_instances() == before);
	}

	TEST_CASE("output published outside a case never enters its transcript") {
		UtilityFunctions::print("harness line that belongs to no case");
		const CorpusResult result = evaluate("func test():\n\tprint(\"mine\")\n");
		CHECK(result.output == String("FS_TEST_OK\nmine"));
	}

	TEST_CASE("an invalid stage or path is an infrastructure error, not a transcript") {
		for (const String &path : { String("tests/corpus/runtime/relative.barista"),
					 String("res://tests/corpus/runtime/../runtime/probe.barista"),
					 String("res://tests/corpus/runtime/probe.gd") }) {
			const CorpusResult result = evaluate_runtime_case(String("func test():\n\tpass\n").to_utf8_buffer(),
					path, "runtime", PackedStringArray());
			CHECK_MESSAGE(result.infrastructure_error, readable(path));
			CHECK_FALSE(result.ok);
		}
		const CorpusResult staged = evaluate_runtime_case(String("func test():\n\tpass\n").to_utf8_buffer(),
				"res://tests/corpus/runtime/probe.barista", "analyzer", PackedStringArray());
		CHECK(staged.infrastructure_error);
	}

	TEST_CASE("evaluating the same case twice in one process gives the same transcript") {
		// Order independence is the property sharding rests on. Two evaluations of one case are
		// the smallest form of it: anything a case leaves behind shows up in the second run.
		const String source =
				"func test():\n"
				"\tvar total: int = 0\n"
				"\tfor step in range(3):\n"
				"\t\ttotal += step\n"
				"\tprint(total)\n";
		const CorpusResult first = evaluate(source);
		const CorpusResult second = evaluate(source);
		CHECK_MESSAGE(first.output == second.output, readable(first.output + String(" != ") + second.output));
		CHECK(first.ok == second.ok);
	}

	TEST_CASE("a transcript never carries a CR or a trailing newline") {
		// The comparison is exact, and `check_expectation_bytes` already refuses an expectation
		// carrying either. A transcript that carried one could not match any expectation at all.
		for (const String &source : { String("func test():\n\tpass\n"),
					 String("func test():\n\tprint(\"line\")\n"),
					 String("func test():\n\tvar divisor: int = 0\n\tprint(1 / divisor)\n") }) {
			const CorpusResult result = evaluate(source);
			CHECK_FALSE(result.output.contains("\r"));
			CHECK_FALSE(result.output.ends_with("\n"));
			CHECK_FALSE(result.output.is_empty());
			CHECK(corpus_result_error(result, "runtime").is_empty());
		}
	}

	TEST_CASE("the shape check refuses a status the transcript vocabulary has no token for") {
		CorpusResult forged;
		forged.analysis_ran = true;
		forged.output = "FS_TEST_COMPILER_ERROR\n>> ERROR: nope";
		CHECK_FALSE(corpus_result_error(forged, "runtime").is_empty());
		forged.output = String(RUNTIME_STATUS_OK) + "\nprinted";
		CHECK_FALSE(corpus_result_error(forged, "runtime").is_empty());
		forged.ok = true;
		CHECK(corpus_result_error(forged, "runtime").is_empty());
	}

	TEST_CASE("selected_corpus_case_emits_guards") {
		// The driver the supervisor's isolated path invokes. It mirrors the analyzer corpus's
		// case of the same name because the two corpora share every mechanism below
		// `run_selected_corpus_case()`; what differs is the suite the report names, which is
		// how a triage run says which corpus it adjudicated.
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

	TEST_CASE("whole_corpus_emits_guarded_records") {
		if (!whole_corpus_selected()) {
			CHECK(corpus_root().is_empty() == corpus_case().is_empty());
			return;
		}
		const CorpusWholeRunReport run = run_whole_corpus();
		// Per-case outcomes are deliberately not asserted: the pin is the supervisor's to
		// enforce, so this case stays green while emitting the failures the pin declares.
		CHECK_MESSAGE(run.error.is_empty(), run.error.utf8().get_data());
		CHECK(run.planned > 0);
		CHECK(run.completed == run.planned);
		const CorpusShardSlice slice = corpus_shard_slice(run.total, corpus_shard(), corpus_shard_count());
		CHECK(run.start == slice.start);
		CHECK(run.planned == slice.end - slice.start);
	}
}
