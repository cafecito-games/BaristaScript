/**************************************************************************/
/*  warnings_test.cpp                                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_warning.h"
#include "doctest.h"
#include "test_require.h"
#include "warning_expectations.h"
#include <algorithm>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <iterator>
#include <set>
#include <tuple>
#include <vector>

using namespace godot;
using namespace barista_script;
using namespace barista_script::test;
using W = BSWarning;

namespace {
W warning(W::Code code, int sl = 1, int sc = 1, int el = 1, int ec = 1) {
	W result;
	result.code = code;
	result.start_line = sl;
	result.start_column = sc;
	result.end_line = el;
	result.end_column = ec;
	return result;
}
W symbolic_warning(W::Code code) {
	auto result = warning(code);
	for (const char *symbol : { "S0", "S1", "S2", "S3", "S4", "S5" }) {
		result.symbols.push_back(symbol);
	}
	return result;
}
std::vector<W::Code> order(const Vector<W> &warnings) {
	std::vector<W::Code> result;
	for (const auto &warning : warnings) {
		result.push_back(warning.code);
	}
	return result;
}
// CHECK records failure and continues in this exception-disabled configuration.
// The scope guard also restores settings on explicit early returns.
struct SettingGuard {
	String path;
	Variant previous;
	explicit SettingGuard(const String &p_path) : path(p_path) {
		auto *settings = ProjectSettings::get_singleton();
		if (settings->has_setting(path)) {
			previous = settings->get_setting(path);
		}
	}
	~SettingGuard() { ProjectSettings::get_singleton()->set_setting(path, previous); }
	SettingGuard(const SettingGuard &) = delete;
	SettingGuard &operator=(const SettingGuard &) = delete;
};
String platform_feature() {
	for (const char *candidate : { "windows", "macos", "linux", "android", "ios", "web" }) {
		if (OS::get_singleton()->has_feature(candidate)) {
			return candidate;
		}
	}
	return {};
}
void project_settings_round_trip() {
	const auto code = W::UNUSED_VARIABLE;
	const String path = W::get_setting_path_from_code(code);
	const auto feature = platform_feature();
	BS_TEST_REQUIRE(!feature.is_empty());
	const String override_path = path + String(".") + feature;
	SettingGuard base_guard(path), override_guard(override_path);
	auto *settings = ProjectSettings::get_singleton();
	settings->set_setting(override_path, Variant());
	settings->set_setting(path, Variant());
	W::WarnLevel level = W::ERROR;
	CHECK(W::resolve_level_from_project_settings(code, level) == W::LEVEL_FROM_DECLARED_DEFAULT);
	CHECK(level == W::get_default_value(code));
	settings->set_setting(path, W::ERROR);
	CHECK(W::resolve_level_from_project_settings(code, level) == W::LEVEL_FROM_PROJECT_SETTING);
	CHECK(level == W::ERROR);
	settings->set_setting(path, "error");
	CHECK(W::resolve_level_from_project_settings(code, level) == W::LEVEL_SETTING_MALFORMED);
	CHECK(level == W::ERROR);
	settings->set_setting(path, Variant());
	settings->set_setting(override_path, W::IGNORE);
	CHECK(W::resolve_level_from_project_settings(code, level) == W::LEVEL_FROM_PROJECT_SETTING);
	CHECK(level == W::IGNORE);
	settings->set_setting(override_path, "ignore");
	CHECK(W::resolve_level_from_project_settings(code, level) == W::LEVEL_SETTING_MALFORMED);
	CHECK(level == W::IGNORE);
}
} //namespace

// Excluded from the required manifest: the supervisor invokes this exact case
// and requires precisely its two intentional failures, with cleanup checks run.
TEST_CASE("warning settings restore after failed assertions" * doctest::test_suite("runner_failure")) {
	const String path = W::get_setting_path_from_code(W::UNUSED_VARIABLE), feature = platform_feature();
	BS_TEST_REQUIRE(!feature.is_empty());
	const String override_path = path + String(".") + feature;
	SettingGuard original_base(path), original_override(override_path);
	auto *settings = ProjectSettings::get_singleton();
	for (bool present : { false, true }) {
		settings->set_setting(path, present ? Variant(W::WARN) : Variant());
		settings->set_setting(override_path, present ? Variant(W::ERROR) : Variant());
		{
			SettingGuard base_guard(path), override_guard(override_path);
			settings->set_setting(path, W::IGNORE);
			settings->set_setting(override_path, "malformed");
			CHECK_MESSAGE(false, "intentional assertion while owning project settings");
		}
		CHECK(settings->has_setting(path) == present);
		CHECK(settings->has_setting(override_path) == present);
		if (present) {
			CHECK(int(settings->get_setting(path)) == W::WARN);
			CHECK(int(settings->get_setting(override_path)) == W::ERROR);
		}
	}
	MESSAGE("settings restoration checked after both failed assertions");
}

TEST_SUITE("warnings") {
	TEST_CASE("vocabulary_closure") {
		BS_TEST_REQUIRE(W::WARNING_MAX > 0);
		BS_TEST_REQUIRE(std::size(warning_expectations) == W::WARNING_MAX);
		std::set<String> names, paths;
		for (int i = 0; i < W::WARNING_MAX; ++i) {
			const auto code = static_cast<W::Code>(i);
			const auto &expected = warning_expectations[i];
			INFO(expected.name);
			const String name = W::get_name_from_code(code), path = W::get_setting_path_from_code(code);
			CHECK_FALSE(name.is_empty());
			CHECK(name == expected.name);
			CHECK(names.insert(name).second);
			CHECK(name == name.to_upper());
			CHECK(W::get_code_from_name(name) == code);
			CHECK(path == String("debug/barista_script/warnings/") + name.to_lower());
			CHECK(paths.insert(path).second);
			const int level = W::get_default_value(code);
			CHECK(level >= W::IGNORE);
			CHECK(level <= W::ERROR);
			CHECK(level == expected.level);
			const auto message = symbolic_warning(code).get_message();
			CHECK_FALSE(message.is_empty());
			CHECK_FALSE(message.contains("%s"));
			CHECK(message == expected.message);
			const auto property = W::get_property_info(code);
			CHECK(property.name == path);
			CHECK(property.type == Variant::INT);
			CHECK(property.hint == PROPERTY_HINT_ENUM);
			CHECK(property.hint_string == "Ignore,Warn,Error");
		}
		CHECK(names.size() == W::WARNING_MAX);
	}
	TEST_CASE("name_round_trip_rejects_strangers") {
		for (const char *stranger : { "", "unassigned_variable", "UNASSIGNED_VARIABLE ", "NOT_A_WARNING", "0" }) {
			CHECK(W::get_code_from_name(stranger) == W::WARNING_MAX);
		}
	}
	TEST_CASE("out_of_range_codes_resolve_to_nothing") {
		for (int value : { -1, int(W::WARNING_MAX), int(W::WARNING_MAX) + 1 }) {
			const auto code = static_cast<W::Code>(value);
			CHECK(W::get_name_from_code(code).is_empty());
			CHECK(symbolic_warning(code).get_message().is_empty());
			W::WarnLevel level = W::ERROR;
			CHECK(W::resolve_level(code, Variant(), level) == W::LEVEL_SETTING_MALFORMED);
			CHECK(level == W::ERROR);
		}
	}
	TEST_CASE("absent_setting_falls_back_to_the_declared_default") {
		for (int i = 0; i < W::WARNING_MAX; ++i) {
			const auto code = static_cast<W::Code>(i);
			W::WarnLevel level = W::ERROR;
			CHECK(W::resolve_level(code, Variant(), level) == W::LEVEL_FROM_DECLARED_DEFAULT);
			CHECK(level == W::get_default_value(code));
		}
	}
	TEST_CASE("malformed_setting_fails_loudly") {
		const Variant malformed[] = { "warn", "1", 1.5, true, -1, W::ERROR + 1, Vector2i(1, 1), Array(), Dictionary() };
		for (int i = 0; i < W::WARNING_MAX; ++i) {
			for (const auto &setting : malformed) {
				W::WarnLevel level = W::ERROR;
				CHECK(W::resolve_level(static_cast<W::Code>(i), setting, level) == W::LEVEL_SETTING_MALFORMED);
				CHECK(level == W::ERROR);
			}
		}
	}
	TEST_CASE("well_formed_setting_overrides_the_default") {
		for (int i = 0; i < W::WARNING_MAX; ++i) {
			for (auto requested : { W::IGNORE, W::WARN, W::ERROR }) {
				W::WarnLevel level = W::WARN;
				CHECK(W::resolve_level(static_cast<W::Code>(i), requested, level) == W::LEVEL_FROM_PROJECT_SETTING);
				CHECK(level == requested);
			}
		}
	}
	TEST_CASE("project_settings_path_round_trip") { project_settings_round_trip(); }
	TEST_CASE("project_settings_restore_existing_values_and_absence") {
		const String path = W::get_setting_path_from_code(W::UNUSED_VARIABLE), feature = platform_feature();
		BS_TEST_REQUIRE(!feature.is_empty());
		const String override_path = path + String(".") + feature;
		SettingGuard base_guard(path), override_guard(override_path);
		auto *settings = ProjectSettings::get_singleton();
		// Repeat in both orders inside one process, including a pre-existing override.
		for (bool reverse : { false, true }) {
			for (int i = 0; i < 2; ++i) {
				const bool present = (i == 0) != reverse;
				settings->set_setting(path, present ? Variant(W::WARN) : Variant());
				settings->set_setting(override_path, present ? Variant(W::ERROR) : Variant());
				project_settings_round_trip();
				CHECK(settings->has_setting(path) == present);
				CHECK(settings->has_setting(override_path) == present);
				if (present) {
					CHECK(int(settings->get_setting(path)) == W::WARN);
					CHECK(int(settings->get_setting(override_path)) == W::ERROR);
				}
			}
		}
	}
	TEST_CASE("position_validation") {
		const auto lengths = W::line_lengths_of("espresso\n\ncup");
		BS_TEST_REQUIRE(lengths.size() == 3);
		CHECK(lengths[0] == 8);
		CHECK(lengths[1] == 0);
		CHECK(lengths[2] == 3);
		const auto empty = W::line_lengths_of("");
		BS_TEST_REQUIRE(empty.size() == 1);
		CHECK(empty[0] == 0);
		const auto crlf = W::line_lengths_of("a\r\nbb\r\n");
		BS_TEST_REQUIRE(crlf.size() == 3);
		CHECK(crlf[0] == 1);
		CHECK(crlf[1] == 2);
		CHECK(crlf[2] == 0);
		const int cases[][5] = { { 1, 1, 1, 9, true }, { 1, 1, 3, 4, true }, { 3, 2, 3, 4, true }, { 2, 1, 2, 1, true }, { 1, 9, 1, 9, true }, { 0, 1, 1, 2, false }, { -1, 1, 1, 2, false }, { 4, 1, 4, 2, false }, { 2, 1, 1, 2, false }, { 1, 1, 4, 2, false }, { 1, 0, 1, 2, false }, { 1, -1, 1, 2, false }, { 1, 1, 1, 0, false }, { 1, 5, 1, 4, false }, { 1, 10, 1, 10, false }, { 1, 1, 1, 10, false }, { 2, 2, 2, 2, false }, { 3, 1, 3, 5, false }, { -1, -1, -1, -1, false } };
		for (const auto &row : cases) {
			CHECK(warning(W::EMPTY_FILE, row[0], row[1], row[2], row[3]).has_valid_position(lengths) == bool(row[4]));
		}
		CHECK_FALSE(warning(W::EMPTY_FILE, 1, 1, 1, 2).has_valid_position(PackedInt32Array()));
	}
	TEST_CASE("validate_dictionary_narrows_at_the_boundary") {
		auto value = warning(W::UNUSED_VARIABLE, 2, 5, 2, 12);
		value.symbols.push_back("cup");
		const auto emitted = value.to_validate_dictionary(W::line_lengths_of("var\nvar cup = 1\npass\npass"));
		CHECK(emitted.size() == 5);
		for (const char *key : { "code", "end_line", "message", "start_line", "string_code" }) {
			CHECK(emitted.has(key));
		}
		CHECK(int(emitted.get("start_line", -1)) == 2);
		CHECK(int(emitted.get("end_line", -1)) == 2);
		CHECK(int(emitted.get("code", -1)) == W::UNUSED_VARIABLE);
		CHECK(String(emitted.get("string_code", "")) == "UNUSED_VARIABLE");
		CHECK(String(emitted.get("message", "")).contains("\"cup\""));
	}
	TEST_CASE("validate_dictionary_refuses_a_position_outside_the_source") {
		const auto lengths = W::line_lengths_of("var\nvar cup = 1\npass\npass");
		for (auto value : { warning(W::UNUSED_VARIABLE, 9, 1, 9, 2), warning(W::UNUSED_VARIABLE, 2, 40, 2, 47) }) {
			value.symbols.push_back("cup");
			CHECK(value.to_validate_dictionary(lengths).is_empty());
		}
	}
	TEST_CASE("ordering_is_deterministic_on_one_line") {
		const W expected[] = { warning(W::UNUSED_VARIABLE, 7, 3, 7, 9), warning(W::STANDALONE_EXPRESSION, 7, 3, 7, 20), warning(W::SHADOWED_VARIABLE, 7, 11, 7, 18), warning(W::INTEGER_DIVISION, 7, 24, 7, 31), warning(W::UNUSED_VARIABLE, 8, 1, 8, 4) };
		const int permutations[][5] = { { 0, 1, 2, 3, 4 }, { 4, 3, 2, 1, 0 }, { 2, 0, 4, 1, 3 }, { 3, 4, 0, 2, 1 }, { 1, 2, 3, 4, 0 } };
		for (const auto &permutation : permutations) {
			Vector<W> warnings;
			for (int index : permutation) {
				warnings.push_back(expected[index]);
			}
			W::sort_warnings(warnings);
			BS_TEST_REQUIRE(warnings.size() == 5);
			for (int i = 0; i < 5; ++i) {
				CHECK(W::compare(warnings[i], expected[i]) == 0);
			}
		}
	}
	TEST_CASE("ordering_is_idempotent") {
		using Answer = std::tuple<String, String, String, int>;
		std::vector<Answer> first, second;
		Vector<W> warnings;
		std::vector<W::Code> ascending;
		for (int pass = 0; pass < 2; ++pass) {
			for (int i = 0; i < W::WARNING_MAX; ++i) {
				const auto code = static_cast<W::Code>(i);
				(pass == 0 ? first : second).emplace_back(W::get_name_from_code(code), W::get_setting_path_from_code(code), symbolic_warning(code).get_message(), W::get_default_value(code));
			}
		}
		CHECK(first == second);
		for (int i = 0; i < W::WARNING_MAX; ++i) {
			warnings.push_back(warning(static_cast<W::Code>(W::WARNING_MAX - 1 - i), 5, 2, 5, 6));
			ascending.push_back(static_cast<W::Code>(i));
		}
		W::sort_warnings(warnings);
		const auto once = order(warnings);
		W::sort_warnings(warnings);
		CHECK(once == order(warnings));
		CHECK(once == ascending);
	}
	TEST_CASE("confusable_identifier_check") {
		CHECK_FALSE(W::is_confusable_identifier("user"));
		if (!W::unicode_security_available()) {
			MESSAGE("TextServer has no FEATURE_UNICODE_SECURITY on this host");
			return;
		}
		MESSAGE("TextServer offers FEATURE_UNICODE_SECURITY");
		CHECK_FALSE(W::is_confusable_identifier("_brew_count"));
		CHECK(W::is_confusable_identifier(String::utf8("usеr")));
	}
}
