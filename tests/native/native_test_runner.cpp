/**************************************************************************/
/*  native_test_runner.cpp                                                */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#define DOCTEST_CONFIG_IMPLEMENT
#include "native_test_runner.h"
#include "doctest.h"
#include "native_build_id.h"
#include "result_protocol.h"
#include <godot_cpp/classes/os.hpp>
#include <iostream>

namespace {
// Only plain C++ metadata is initialized before Godot loads the extension.
int completed_cases = 0;
int assertions = 0;
int failed_cases = 0;
int failed_assertions = 0;
bool completed_run = false;
struct CompletionListener : doctest::IReporter {
	explicit CompletionListener(const doctest::ContextOptions &) {}
	void report_query(const doctest::QueryData &) override {}
	void test_run_start() override {
		completed_cases = assertions = failed_cases = failed_assertions = 0;
		completed_run = false;
	}
	void test_run_end(const doctest::TestRunStats &stats) override {
		assertions = stats.numAsserts;
		failed_cases = stats.numTestCasesFailed;
		failed_assertions = stats.numAssertsFailed;
		completed_run = true;
	}
	void test_case_end(const doctest::CurrentTestCaseStats &) override { ++completed_cases; }
	void test_case_start(const doctest::TestCaseData &) override {}
	void test_case_reenter(const doctest::TestCaseData &) override {}
	void test_case_exception(const doctest::TestCaseException &) override {}
	void subcase_start(const doctest::SubcaseSignature &) override {}
	void subcase_end() override {}
	void log_assert(const doctest::AssertData &) override {}
	void log_message(const doctest::MessageData &) override {}
	void test_case_skipped(const doctest::TestCaseData &) override {}
};
DOCTEST_REGISTER_LISTENER("completion", 0, CompletionListener);

bool exact_filter(const godot::String &value) {
	return !value.contains("*") && !value.contains("?") && !value.contains(",");
}
} // namespace

bool BaristaNativeTestRunner::_process(double) {
	if (ran) {
		return false;
	}
	ran = true;
	godot::String suite, case_name, nonce;
	bool listing = false;
	for (const godot::String &argument : godot::OS::get_singleton()->get_cmdline_user_args()) {
		if (argument.begins_with("--native-suite=")) {
			suite = argument.trim_prefix("--native-suite=");
		} else if (argument.begins_with("--native-case=")) {
			case_name = argument.trim_prefix("--native-case=");
		} else if (argument.begins_with("--native-nonce=")) {
			nonce = argument.trim_prefix("--native-nonce=");
		} else if (argument == "--native-list") {
			listing = true;
		} else {
			std::cerr << "Unknown native runner argument: " << argument.utf8().get_data() << std::endl;
			quit(2);
			return false;
		}
	}
	if (suite.is_empty() || nonce.is_empty() || !exact_filter(suite) || !exact_filter(case_name)) {
		std::cerr << "Native runner requires exact suite/case filters and a run nonce" << std::endl;
		quit(2);
		return false;
	}
	doctest::Context context;
	context.setOption("order-by", "name");
	context.setOption("case-sensitive", true);
	context.setOption("no-colors", true);
	context.setOption("no-breaks", true);
	context.addFilter("test-suite", suite.utf8().get_data());
	if (!case_name.is_empty()) {
		context.addFilter("test-case", case_name.utf8().get_data());
	}
	context.setOption("list-test-cases", listing);
	const int result = context.run();
	if (!listing && completed_run) {
		std::cout << BS_NATIVE_RESULT_PREFIX << "{\"protocol\":" << BS_NATIVE_PROTOCOL_VERSION
				  << ",\"suite\":\"" << suite.json_escape().utf8().get_data()
				  << "\",\"case\":\"" << case_name.json_escape().utf8().get_data()
				  << "\",\"nonce\":\"" << nonce.json_escape().utf8().get_data()
				  << "\",\"build_id\":\"" << BS_NATIVE_BUILD_ID
				  << "\",\"cases\":" << completed_cases << ",\"assertions\":" << assertions
				  << ",\"failed_cases\":" << failed_cases << ",\"failed_assertions\":" << failed_assertions << "}" << std::endl;
	}
	quit(result || (!listing && (!completed_run || completed_cases == 0 || assertions == 0)) ? 1 : 0);
	return false;
}

TEST_CASE("intentional failing assertion" * doctest::test_suite("runner_failure")) {
	CHECK_MESSAGE(false, "Failure propagation self-test; never part of the native suite manifest.");
}
