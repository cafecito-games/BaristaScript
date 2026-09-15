/**************************************************************************/
/*  analyzer_diagnostics_test.cpp                                         */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                          */
/*  This file is part of BaristaScript, a Godot GDExtension.                */
/*  SPDX-License-Identifier: MIT                                           */
/**************************************************************************/

#include "analyzer_helpers.h"
#include "bs_conformance_registry.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <algorithm>
#include <array>
#include <random>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
bool has_warning(const AnalysisResult &result, BSWarning::Code code, const String &text = String()) {
	for (const BSWarning &warning : result.parser->get_warnings()) {
		if (warning.code == code && (text.is_empty() || warning.get_message().contains(text))) {
			return true;
		}
	}
	return false;
}

void scenario_semantic_errors() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	const auto mismatch = analyze_source("class_name TypeMismatch extends Node\n\nfunc _ready() -> void:\n\tvar x: int = 1.5\n", "res://tests/type_mismatch.barista");
	CHECK_FALSE(mismatch.valid());
	CHECK_FALSE(mismatch.parser->get_errors().is_empty());
	const auto generic = analyze_source("class_name GenericBox[T] extends RefCounted\n", "res://tests/generic_box.barista");
	CHECK_FALSE(generic.valid());
	CHECK(errors_contain(generic, "M5"));
}

void scenario_undeclared_identifier_diagnostic() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	const auto result = analyze_source("func test():\n\tmatch 0:\n\t\t_ when a == 0:\n\t\t\tprint(\"a does not exist\")\n", "res://tests/match_guard_invalid_expression.barista");
	CHECK_FALSE(result.valid());
	BS_TEST_REQUIRE(result.parser->get_errors().size() == 1);
	CHECK(result.parser->get_errors().front()->get().message == "Identifier \"a\" not declared in the current scope.");
}

void scenario_warning_settings() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	const String source = "class_name WarnDiv extends Node\nfunc _ready() -> void:\n\tvar _z: int = 1 / 2\n";
	const String path = "res://tests/warn_div.barista";
	settings.warnings_enabled(true);
	settings.warning(BSWarning::INTEGER_DIVISION, BSWarning::WARN);
	const auto warned = analyze_source(source, path);
	CHECK(warned.valid());
	CHECK_MESSAGE(has_warning(warned, BSWarning::INTEGER_DIVISION), "warning count: ", warned.parser->get_warnings().size(), ", phase: ", int(warned.phase));
	settings.warning(BSWarning::INTEGER_DIVISION, BSWarning::IGNORE);
	CHECK(analyze_source(source, path).parser->get_warnings().is_empty());
	settings.warning(BSWarning::INTEGER_DIVISION, BSWarning::ERROR);
	CHECK_FALSE(analyze_source(source, path).valid());
}

void scenario_unused_locals() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	settings.warnings_enabled(true);
	settings.warning(BSWarning::UNUSED_VARIABLE, BSWarning::WARN);
	settings.warning(BSWarning::UNUSED_PARAMETER, BSWarning::WARN);
	const auto unused = analyze_source("class_name UnusedLocal extends Node\nfunc _ready() -> void:\n\tvar orphan: int = 1\n", "res://tests/unused_local.barista");
	CHECK(unused.valid());
	CHECK_MESSAGE(has_warning(unused, BSWarning::UNUSED_VARIABLE), "warning count: ", unused.parser->get_warnings().size(), ", phase: ", int(unused.phase));
	const auto written = analyze_source("class_name WriteOnlyLocal extends Node\nfunc _ready() -> void:\n\tvar scratch: int\n\tscratch = 1\n", "res://tests/write_only_local.barista");
	CHECK(has_warning(written, BSWarning::UNUSED_VARIABLE, "scratch"));
	const auto parameter = analyze_source("class_name UnusedParam extends Node\nfunc greet(name: String) -> void:\n\tpass\n", "res://tests/unused_param.barista");
	bool saw_parameter = false;
	for (const BSWarning &warning : parameter.parser->get_warnings()) {
		const String message = warning.get_message().to_lower();
		saw_parameter = saw_parameter || (warning.code == BSWarning::UNUSED_PARAMETER && message.contains("never used") && message.contains("greet"));
	}
	CHECK(saw_parameter);
	const auto used = analyze_source("class_name UsedLocal extends Node\nfunc _ready() -> void:\n\tvar keep: int = 1\n\tvar _sink: int = keep\n", "res://tests/used_local.barista");
	for (const BSWarning &warning : used.parser->get_warnings()) {
		CHECK_FALSE(warning.get_message().contains("keep"));
	}
}

void scenario_match_and_flow() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	const auto incomplete = analyze_source("class_name MatchIncomplete extends Node\nfunc check(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n", "res://tests/match_incomplete.barista");
	CHECK_FALSE(incomplete.valid());
	BS_TEST_REQUIRE(incomplete.parser->get_errors().size() == 1);
	const auto &error = incomplete.parser->get_errors().front()->get();
	CHECK(error.message == "Not all code paths return a value. The \"match\" over \"bool\" does not cover: false.");
	CHECK(error.line == 2);
	CHECK(error.column == 1);
	CHECK(analyze_source("class_name MatchOk extends Node\nfunc check(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n\t\tfalse:\n\t\t\treturn 0\n", "res://tests/match_ok.barista").valid());
}

void scenario_noreturn_flow() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	CHECK(analyze_source("class_name NoreturnOk extends Node\n@noreturn\nfunc die() -> void:\n\tpush_fatal(\"boom\")\nfunc value() -> int:\n\tdie()\n", "res://tests/noreturn_ok.barista").valid());
	const auto incomplete = analyze_source("class_name NoreturnIncomplete extends Node\n@noreturn\nfunc die() -> void:\n\tpass\n", "res://tests/noreturn_incomplete.barista");
	CHECK_FALSE(incomplete.valid());
	CHECK(errors_contain(incomplete, "cannot complete normally"));
	const auto nested = analyze_source("class_name NoreturnNestedReturn extends Node\n@noreturn\nfunc die(flag: bool) -> void:\n\tif flag:\n\t\treturn\n\tpush_fatal(\"boom\")\n", "res://tests/noreturn_nested.barista");
	CHECK_FALSE(nested.valid());
	CHECK(errors_contain(nested, "cannot return"));
}

void scenario_final_pattern_and_nested_expression_reads() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	const auto pattern = analyze_source("class_name FinalPatternRead extends Node\nfinal var id: int\nfunc _init(value: int) -> void:\n\tmatch value:\n\t\tself.id:\n\t\t\tid = 1\n\t\t_:\n\t\t\tid = 2\n", "res://tests/final_pattern_read.barista");
	CHECK(errors_contain(pattern, "Final variable \"id\" may be used before assignment"));
	const auto type_test = analyze_source("class_name FinalTypeTestRead extends Node\nfinal var item: Variant\nfunc _init() -> void:\n\tif item is Node:\n\t\tpass\n\titem = null\n", "res://tests/final_type_test_read.barista");
	CHECK(errors_contain(type_test, "Final variable \"item\" may be used before assignment"));
	const auto tuple = analyze_source("class_name FinalTupleRead extends Node\nfinal var id: int\nfunc _init() -> void:\n\tvar _pair := (self.id, 1)\n\tid = 2\n", "res://tests/final_tuple_read.barista");
	CHECK(errors_contain(tuple, "Final variable \"id\" may be used before assignment"));
}

void scenario_unary_sign_constant_folding() {
	StorageFixture fixture;
	AnalyzerSettings settings;
	struct Sample {
		const char *source;
		int64_t value;
		bool unary;
	};
	for (const Sample &sample : { Sample{ "-2 ** 2", 4, false }, Sample{ "(-2) ** 2", 4, false }, Sample{ "-(2 ** 2)", -4, true }, Sample{ "- 2 ** 2", -4, true }, Sample{ "+2 ** 2", 4, false } }) {
		const auto result = analyze_source("var probe_expression = " + String(sample.source) + "\n", "res://tests/fold_probe.barista");
		BS_TEST_REQUIRE(result.valid());
		const auto *expression = find_expression(result);
		BS_TEST_REQUIRE(expression != nullptr);
		CHECK(expression->is_constant);
		CHECK(expression->reduced_value == Variant(sample.value));
		CHECK(expression_has_unary_sign(expression) == sample.unary);
	}
}

using Scenario = void (*)();
std::array<Scenario, 8> scenarios() {
	return { scenario_semantic_errors, scenario_undeclared_identifier_diagnostic,
		scenario_warning_settings, scenario_unused_locals, scenario_match_and_flow,
		scenario_noreturn_flow, scenario_final_pattern_and_nested_expression_reads,
		scenario_unary_sign_constant_folding };
}
void scenario_unused_class_members_and_signals() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	settings.warnings_enabled(true);
	settings.warning(BSWarning::UNUSED_PRIVATE_CLASS_VARIABLE, BSWarning::WARN);
	settings.warning(BSWarning::UNUSED_SIGNAL, BSWarning::WARN);
	const String unused_private = "class_name UnusedPrivateMember extends Node\nvar _orphan: int = 1\nfunc _ready() -> void:\n\tpass\n";
	const Dictionary private_report = BaristaScriptLanguage::get_singleton()->_validate(unused_private, "res://tests/unused_private_member.barista", true, true, true, false);
	CHECK_MESSAGE((bool(private_report.get("valid", false)) == true), "unused private member stays valid at WARN");
	bool saw_private = false;
	for (const Dictionary &warn : Array(private_report.get("warnings", Array()))) {
		const String code = warn.get("string_code", "");
		const String msg = warn.get("message", "");
		if (code.contains("UNUSED_PRIVATE_CLASS_VARIABLE") && msg.contains("_orphan") && msg.contains("never used in the class")) {
			saw_private = true;
		}
	}
	CHECK_MESSAGE((saw_private), "unused private member produces UNUSED_PRIVATE_CLASS_VARIABLE message");
	const String used_private = "class_name UsedPrivateMember extends Node\nvar _keep: int = 1\nfunc _ready() -> void:\n\tvar _sink: int = _keep\n";
	const Dictionary used_private_report = BaristaScriptLanguage::get_singleton()->_validate(used_private, "res://tests/used_private_member.barista", true, true, true, false);
	bool saw_used_private = false;
	for (const Dictionary &warn : Array(used_private_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("UNUSED_PRIVATE_CLASS_VARIABLE") && String(warn.get("message", "")).contains("_keep")) {
			saw_used_private = true;
		}
	}
	CHECK_MESSAGE((!saw_used_private), "used private member does not warn as unused");
	const String public_unused = "class_name PublicUnusedMember extends Node\nvar visible: int = 1\nfunc _ready() -> void:\n\tpass\n";
	const Dictionary public_report = BaristaScriptLanguage::get_singleton()->_validate(public_unused, "res://tests/public_unused_member.barista", true, true, true, false);
	bool saw_public = false;
	for (const Dictionary &warn : Array(public_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("UNUSED_PRIVATE_CLASS_VARIABLE")) {
			saw_public = true;
		}
	}
	CHECK_MESSAGE((!saw_public), "public unused member does not produce UNUSED_PRIVATE_CLASS_VARIABLE");
	const String unused_signal = "class_name UnusedSignalScript extends Node\nsignal lonely\nfunc _ready() -> void:\n\tpass\n";
	const Dictionary signal_report = BaristaScriptLanguage::get_singleton()->_validate(unused_signal, "res://tests/unused_signal.barista", true, true, true, false);
	CHECK_MESSAGE((bool(signal_report.get("valid", false)) == true), "unused signal stays valid at WARN");
	bool saw_signal = false;
	for (const Dictionary &warn : Array(signal_report.get("warnings", Array()))) {
		const String code = warn.get("string_code", "");
		const String msg = warn.get("message", "");
		if (code.contains("UNUSED_SIGNAL") && msg.contains("lonely") && msg.contains("never explicitly used")) {
			saw_signal = true;
		}
	}
	CHECK_MESSAGE((saw_signal), "unused signal produces UNUSED_SIGNAL message");
	const String used_signal = "class_name UsedSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\temit_signal(\"ping\")\n";
	const Dictionary used_signal_report = BaristaScriptLanguage::get_singleton()->_validate(used_signal, "res://tests/used_signal.barista", true, true, true, false);
	bool saw_used_signal = false;
	for (const Dictionary &warn : Array(used_signal_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("UNUSED_SIGNAL") && String(warn.get("message", "")).contains("ping")) {
			saw_used_signal = true;
		}
	}
	CHECK_MESSAGE((!saw_used_signal), "emit_signal counts as signal use");
	const String connect_signal = "class_name ConnectSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\tconnect(\"ping\", Callable())\n";
	const Dictionary connect_report = BaristaScriptLanguage::get_singleton()->_validate(connect_signal, "res://tests/connect_signal.barista", true, true, true, false);
	bool saw_connect_unused = false;
	for (const Dictionary &warn : Array(connect_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("UNUSED_SIGNAL") && String(warn.get("message", "")).contains("ping")) {
			saw_connect_unused = true;
		}
	}
	CHECK_MESSAGE((!saw_connect_unused), "bare connect counts as signal use");
	const String disconnect_signal = "class_name DisconnectSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\tdisconnect(\"ping\", Callable())\n";
	const Dictionary disconnect_report = BaristaScriptLanguage::get_singleton()->_validate(disconnect_signal, "res://tests/disconnect_signal.barista", true, true, true, false);
	bool saw_disconnect_unused = false;
	for (const Dictionary &warn : Array(disconnect_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("UNUSED_SIGNAL") && String(warn.get("message", "")).contains("ping")) {
			saw_disconnect_unused = true;
		}
	}
	CHECK_MESSAGE((!saw_disconnect_unused), "bare disconnect counts as signal use");
	const String is_connected_signal = "class_name IsConnectedSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\tvar _linked: bool = is_connected(\"ping\", Callable())\n";
	const Dictionary is_connected_report = BaristaScriptLanguage::get_singleton()->_validate(is_connected_signal, "res://tests/is_connected_signal.barista", true, true, true, false);
	bool saw_is_connected_unused = false;
	for (const Dictionary &warn : Array(is_connected_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("UNUSED_SIGNAL") && String(warn.get("message", "")).contains("ping")) {
			saw_is_connected_unused = true;
		}
	}
	CHECK_MESSAGE((!saw_is_connected_unused), "bare is_connected counts as signal use");
	const String nested = "class_name NestedUnusedOuter extends Node\nclass Inner:\n\tsignal nested_lonely\n\tfunc _ready() -> void:\n\t\tpass\nfunc _ready() -> void:\n\tpass\n";
	const Dictionary nested_report = BaristaScriptLanguage::get_singleton()->_validate(nested, "res://tests/nested_unused_signal.barista", true, true, true, false);
	int nested_count = 0;
	for (const Dictionary &warn : Array(nested_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("UNUSED_SIGNAL") && String(warn.get("message", "")).contains("nested_lonely")) {
			nested_count += 1;
		}
	}
	CHECK_MESSAGE((nested_count == 1), "nested unused signal warns exactly once");
	const String ignored = "class_name IgnoredSignalScript extends Node\n@warning_ignore(\"unused_signal\")\nsignal quiet\nfunc _ready() -> void:\n\tpass\n";
	const Dictionary ignored_report = BaristaScriptLanguage::get_singleton()->_validate(ignored, "res://tests/ignored_signal.barista", true, true, true, false);
	bool saw_ignored = false;
	for (const Dictionary &warn : Array(ignored_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("UNUSED_SIGNAL") && String(warn.get("message", "")).contains("quiet")) {
			saw_ignored = true;
		}
	}
	CHECK_MESSAGE((!saw_ignored), "@warning_ignore(\"unused_signal\") suppresses UNUSED_SIGNAL via resolve_annotation");
}

} // namespace

TEST_SUITE("analyzer_diagnostics") {
	TEST_CASE("unused_class_members_and_signals") { scenario_unused_class_members_and_signals(); }
	TEST_CASE("semantic_errors") { scenario_semantic_errors(); }
	TEST_CASE("undeclared_identifier_diagnostic") { scenario_undeclared_identifier_diagnostic(); }
	TEST_CASE("warning_settings") { scenario_warning_settings(); }
	TEST_CASE("unused_locals") { scenario_unused_locals(); }
	TEST_CASE("match_and_flow") { scenario_match_and_flow(); }
	TEST_CASE("noreturn_flow") { scenario_noreturn_flow(); }
	TEST_CASE("final_pattern_and_nested_expression_reads") { scenario_final_pattern_and_nested_expression_reads(); }
	TEST_CASE("unary_sign_constant_folding") { scenario_unary_sign_constant_folding(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		auto order = scenarios();
		for (Scenario scenario : order) {
			CHECK(verify_case_isolation(scenario));
		}
		std::reverse(order.begin(), order.end());
		for (Scenario scenario : order) {
			CHECK(verify_case_isolation(scenario));
		}
		std::mt19937 random(155);
		std::shuffle(order.begin(), order.end(), random);
		for (Scenario scenario : order) {
			CHECK(verify_case_isolation(scenario));
		}
	}
}
