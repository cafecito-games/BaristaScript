/**************************************************************************/
/*  analyzer_helpers.cpp                                                  */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "analyzer_helpers.h"

#include "barista_script.h"
#include "bs_cache.h"
#include "bs_conformance_registry.h"
#include "doctest.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <algorithm>
#include <godot_cpp/classes/project_settings.hpp>
#include <random>
#include <string>

using namespace godot;

namespace barista_script::native_tests {

namespace {
constexpr const char *warning_enable = "debug/barista_script/warnings/enable";
constexpr const char *strict_null_setting = "debug/barista_script/analysis/strict_null_checks";
constexpr const char *strict_dynamic_setting = "debug/barista_script/analysis/strict_dynamic_checks";
} // namespace

AnalyzerSettings::AnalyzerSettings() {
	Vector<String> profile_paths;
	profile_paths.push_back(warning_enable);
	profile_paths.push_back("debug/barista_script/warnings/directory_rules");
	profile_paths.push_back(strict_null_setting);
	profile_paths.push_back(strict_dynamic_setting);
	for (int code = 0; code < BSWarning::WARNING_MAX; ++code) {
		profile_paths.push_back(BSWarning::get_setting_path_from_code(static_cast<BSWarning::Code>(code)));
	}
	ProjectSettings *settings = ProjectSettings::get_singleton();
	for (const Dictionary &property : Array(settings->get_property_list())) {
		const String path = property["name"];
		for (const String &base : profile_paths) {
			if (path.begins_with(base + String("."))) {
				remember(path);
				settings->clear(path);
				break;
			}
		}
	}
	set("debug/barista_script/warnings/directory_rules", Dictionary());
	warnings_enabled(false);
	strict_null(false);
	strict_dynamic(false);
	for (int code = 0; code < BSWarning::WARNING_MAX; ++code) {
		warning(static_cast<BSWarning::Code>(code), BSWarning::IGNORE);
	}
}

AnalyzerSettings::~AnalyzerSettings() {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	for (int i = saved.size() - 1; i >= 0; --i) {
		const SavedSetting &setting = saved[i];
		if (setting.present) {
			settings->set_setting(setting.path, setting.value);
		} else if (settings->has_setting(setting.path)) {
			settings->clear(setting.path);
		}
	}
	BSParser::invalidate_analysis_on_strict_settings_change();
	BSParser::update_project_settings();
	// Refresh declares absent warning defaults. Restore absence after updating the
	// parser's cached profile, matching the production corpus profile's restoration.
	for (const SavedSetting &setting : saved) {
		if (!setting.present && settings->has_setting(setting.path)) {
			settings->clear(setting.path);
		}
	}
}

void AnalyzerSettings::remember(const String &path) {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	bool remembered = false;
	for (const SavedSetting &setting : saved) {
		remembered = remembered || setting.path == path;
	}
	if (!remembered) {
		SavedSetting setting;
		setting.path = path;
		setting.present = settings->has_setting(path);
		setting.value = setting.present ? settings->get_setting(path) : Variant();
		if (setting.value.get_type() == Variant::ARRAY) {
			setting.value = Array(setting.value).duplicate(true);
		} else if (setting.value.get_type() == Variant::DICTIONARY) {
			setting.value = Dictionary(setting.value).duplicate(true);
		}
		saved.push_back(setting);
	}
}

void AnalyzerSettings::set(const String &path, const Variant &value) {
	remember(path);
	ProjectSettings::get_singleton()->set_setting(path, value);
	BSParser::invalidate_analysis_on_strict_settings_change();
}

void AnalyzerSettings::warnings_enabled(bool enabled) {
	set(warning_enable, enabled);
}

void AnalyzerSettings::warning(BSWarning::Code code, BSWarning::WarnLevel level) {
	set(BSWarning::get_setting_path_from_code(code), level);
}

void AnalyzerSettings::strict_null(bool enabled) {
	set(strict_null_setting, enabled);
}

void AnalyzerSettings::strict_dynamic(bool enabled) {
	set(strict_dynamic_setting, enabled);
}

StrictAnalyzerSettingsScope::SavedSetting StrictAnalyzerSettingsScope::save(const String &path) {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	SavedSetting saved_setting;
	saved_setting.path = path;
	saved_setting.present = settings->has_setting(path);
	saved_setting.value = saved_setting.present ? settings->get_setting(path) : Variant();
	return saved_setting;
}

void StrictAnalyzerSettingsScope::restore(const SavedSetting &setting) {
	ProjectSettings::get_singleton()->set_setting(setting.path, setting.present ? setting.value : Variant());
}

StrictAnalyzerSettingsScope::StrictAnalyzerSettingsScope() :
		saved_strict_null(save(strict_null_setting)),
		saved_strict_dynamic(save(strict_dynamic_setting)) {
	ProjectSettings::get_singleton()->set_setting(saved_strict_null.path, false);
	ProjectSettings::get_singleton()->set_setting(saved_strict_dynamic.path, false);
	BSParser::invalidate_analysis_on_strict_settings_change();
}

StrictAnalyzerSettingsScope::~StrictAnalyzerSettingsScope() {
	restore(saved_strict_dynamic);
	restore(saved_strict_null);
	BSParser::invalidate_analysis_on_strict_settings_change();
}

void StrictAnalyzerSettingsScope::strict_null(bool enabled) {
	ProjectSettings::get_singleton()->set_setting(saved_strict_null.path, enabled);
}

void StrictAnalyzerSettingsScope::strict_dynamic(bool enabled) {
	ProjectSettings::get_singleton()->set_setting(saved_strict_dynamic.path, enabled);
}

AnalysisResult analyze_source(const String &source, const String &path) {
	HashMap<String, String> sources;
	sources[path] = source;
	BSCacheSourceOverrideGuard overrides(sources);
	AnalysisResult result;
	result.parser = std::make_unique<BSParser>();
	result.path = path;
	result.status = result.parser->parse(source, path, false);
	if (result.status == OK) {
		BSAnalyzer analyzer(result.parser.get());
		result.status = analyzer.analyze();
		result.phase = analyzer.get_highest_completed_phase();
	}
	return result;
}

bool source_analyzes(const String &source, const String &path) {
	HashMap<String, String> sources;
	sources[path] = source;
	BSCacheSourceOverrideGuard overrides(sources);
	return bs_source_analyzes(source, path);
}

void check_analysis(const AnalysisResult &result,
		const std::vector<ExpectedError> &errors,
		const std::vector<ExpectedWarning> &warnings,
		BSAnalyzer::AnalyzerPhase phase) {
	INFO(std::string(result.path.utf8().get_data()));
	BS_TEST_REQUIRE(result.parser != nullptr);
	CHECK(result.valid() == (errors.size() == 0));
	CHECK(result.phase == phase);
	const Vector<const BSParser::ParserError *> actual_errors = result.parser->get_errors_in_source_order();
	BS_TEST_REQUIRE(size_t(actual_errors.size()) == errors.size());
	size_t index = 0;
	for (const ExpectedError &expected : errors) {
		const BSParser::ParserError *actual = actual_errors[int(index++)];
		BS_TEST_REQUIRE(actual != nullptr);
		CHECK(std::string(actual->message.utf8().get_data()) == expected.message);
		CHECK(actual->line == expected.line);
		CHECK(actual->column == expected.column);
	}
	const List<BSWarning> &actual_warnings = result.parser->get_warnings();
	BS_TEST_REQUIRE(size_t(actual_warnings.size()) == warnings.size());
	auto *element = actual_warnings.front();
	for (const ExpectedWarning &expected : warnings) {
		BS_TEST_REQUIRE(element != nullptr);
		const BSWarning &actual = element->get();
		CHECK(actual.code == expected.code);
		CHECK(std::string(actual.get_message().utf8().get_data()) == expected.message);
		CHECK(actual.start_line == expected.start_line);
		CHECK(actual.start_column == expected.start_column);
		CHECK(actual.end_line == expected.end_line);
		CHECK(actual.end_column == expected.end_column);
		element = element->next();
	}
}

const BSParser::FunctionNode *find_function(const AnalysisResult &result, const StringName &name) {
	if (result.parser == nullptr || result.parser->get_tree() == nullptr || !result.parser->get_tree()->has_member(name)) {
		return nullptr;
	}
	const BSParser::ClassNode::Member member = result.parser->get_tree()->get_member(name);
	return member.type == BSParser::ClassNode::Member::FUNCTION ? member.function : nullptr;
}

const BSParser::ExpressionNode *find_expression(const AnalysisResult &result, const StringName &name) {
	if (result.parser == nullptr || result.parser->get_tree() == nullptr || !result.parser->get_tree()->has_member(name)) {
		return nullptr;
	}
	const BSParser::ClassNode::Member member = result.parser->get_tree()->get_member(name);
	return member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr ? member.variable->initializer : nullptr;
}

// Extracted from the legacy fold probe: observes parser shape without recomputing types.
bool expression_has_unary_sign(const BSParser::ExpressionNode *expression) {
	if (expression == nullptr) {
		return false;
	}
	if (expression->type == BSParser::Node::UNARY_OPERATOR) {
		const auto *unary = static_cast<const BSParser::UnaryOpNode *>(expression);
		return unary->operation == BSParser::UnaryOpNode::OP_NEGATIVE || unary->operation == BSParser::UnaryOpNode::OP_POSITIVE || expression_has_unary_sign(unary->operand);
	}
	if (expression->type == BSParser::Node::BINARY_OPERATOR) {
		const auto *binary = static_cast<const BSParser::BinaryOpNode *>(expression);
		return expression_has_unary_sign(binary->left_operand) || expression_has_unary_sign(binary->right_operand);
	}
	return false;
}

bool errors_contain(const AnalysisResult &result, const String &needle) {
	if (result.parser == nullptr) {
		return false;
	}
	for (const auto &error : result.parser->get_errors()) {
		if (error.message.contains(needle)) {
			return true;
		}
	}
	return false;
}

void check_scenario_orders(std::initializer_list<void (*)()> scenarios) {
	const auto setting_snapshot = []() {
		ProjectSettings *project = ProjectSettings::get_singleton();
		Dictionary snapshot;
		for (const Dictionary &property : Array(project->get_property_list())) {
			const String path = property["name"];
			if (path.begins_with("debug/barista_script/warnings/") ||
					path.begins_with(strict_null_setting) || path.begins_with(strict_dynamic_setting)) {
				snapshot[path] = project->get_setting(path);
			}
		}
		return snapshot.duplicate(true);
	};
	const auto run = [&](void (*scenario)()) {
		// The storage verifier performs a bootstrap parse before calling the scenario.
		// Establish its lazy warning defaults inside a restorable scope first, so the
		// snapshot measures scenario changes rather than verifier initialization.
		AnalyzerSettings ambient_settings;
		ambient_settings.warning(BSWarning::UNSAFE_CAST, BSWarning::ERROR);
		const Dictionary before = setting_snapshot();
		BSConformanceRegistry::ScopedCorpusState ambient_registry;
		BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
		const String source = "res://native_ambient_conformance.barista";
		const String target = "res://native_ambient_target.barista";
		BSConformanceRegistry::Conformance conformance;
		conformance.target_keys.push_back(target);
		conformance.target_fqcn = target;
		conformance.target_script_path = target;
		conformance.trait_name = SNAME("NativeAmbientTrait");
		conformance.source_file = source;
		conformance.conformance_index = 7;
		conformance.witnesses[SNAME("native_ambient_witness")] = true;
		BSConformanceRegistry::RecordedTypeArgument argument;
		argument.kind = BSConformanceRegistry::RecordedTypeArgument::BUILTIN;
		argument.builtin_type = Variant::INT;
		conformance.trait_type_arguments.push_back(argument);
		Vector<BSConformanceRegistry::Conformance> entries;
		entries.push_back(conformance);
		HashSet<String> loaded;
		loaded.insert(target);
		registry->try_replace_file_conformances(source, entries, {}, loaded);
		BS_TEST_REQUIRE(registry->has_conformance(target, conformance.trait_name));
		CHECK(verify_case_isolation(scenario));
		const Dictionary after_settings = setting_snapshot();
		for (const Variant &key : before.keys()) {
			CHECK_MESSAGE((after_settings.has(key) && after_settings[key] == before[key]), "ambient setting changed: ", std::string(String(key).utf8().get_data()));
		}
		for (const Variant &key : after_settings.keys()) {
			CHECK_MESSAGE(before.has(key), "ambient setting added: ", std::string(String(key).utf8().get_data()));
		}
		CHECK(BSConformanceRegistry::get_singleton() == registry);
		CHECK(registry->has_conformance(target, conformance.trait_name));
		CHECK(registry->get_conformance_source(target, conformance.trait_name) == source);
		const auto after = registry->get_file_conformances(source);
		BS_TEST_REQUIRE(after.size() == 1);
		CHECK(after[0].conformance_index == 7);
		CHECK(after[0].target_script_path == target);
		CHECK(after[0].witnesses.has(SNAME("native_ambient_witness")));
		BS_TEST_REQUIRE(after[0].trait_type_arguments.size() == 1);
		CHECK(after[0].trait_type_arguments[0].kind == BSConformanceRegistry::RecordedTypeArgument::BUILTIN);
		CHECK(after[0].trait_type_arguments[0].builtin_type == Variant::INT);
		const auto after_loaded = registry->debug_get_loaded_files(source);
		CHECK(after_loaded.size() == 1);
		CHECK(after_loaded.has(target));
	};
	std::vector<void (*)()> order(scenarios);
	for (auto scenario : order) {
		run(scenario);
	}
	std::reverse(order.begin(), order.end());
	for (auto scenario : order) {
		run(scenario);
	}
	std::mt19937 random(155);
	std::shuffle(order.begin(), order.end(), random);
	for (auto scenario : order) {
		run(scenario);
	}
}

const BSParser::Node *find_statement_node(const BSParser::FunctionNode *function, const String &path) {
	if (function == nullptr) {
		return nullptr;
	}
	const PackedStringArray segments = path.split("/", false);
	if (segments.is_empty() || segments[0] != "body") {
		return nullptr;
	}
	const BSParser::Node *node = function->body;
	for (int segment_index = 1; node != nullptr && segment_index < segments.size(); ++segment_index) {
		const String segment = segments[segment_index];
		switch (node->type) {
			case BSParser::Node::SUITE: {
				const auto *suite = static_cast<const BSParser::SuiteNode *>(node);
				const int index = segment.to_int();
				node = index >= 0 && index < suite->statements.size() ? suite->statements[index] : nullptr;
			} break;
			case BSParser::Node::FOR: {
				const auto *loop = static_cast<const BSParser::ForNode *>(node);
				node = segment == "iterator" ? static_cast<const BSParser::Node *>(loop->variable) : segment == "list" ? static_cast<const BSParser::Node *>(loop->list)
						: segment == "loop"																			   ? static_cast<const BSParser::Node *>(loop->loop)
																													   : nullptr;
			} break;
			case BSParser::Node::WHILE: {
				const auto *loop = static_cast<const BSParser::WhileNode *>(node);
				node = segment == "condition" ? static_cast<const BSParser::Node *>(loop->condition) : segment == "loop" ? static_cast<const BSParser::Node *>(loop->loop)
																														 : nullptr;
			} break;
			case BSParser::Node::IF: {
				const auto *branch = static_cast<const BSParser::IfNode *>(node);
				node = segment == "true" ? static_cast<const BSParser::Node *>(branch->true_block) : segment == "false" ? static_cast<const BSParser::Node *>(branch->false_block)
																														: nullptr;
			} break;
			case BSParser::Node::MATCH: {
				const auto *match = static_cast<const BSParser::MatchNode *>(node);
				const int index = segment.to_int();
				node = index >= 0 && index < match->branches.size() ? match->branches[index] : nullptr;
			} break;
			case BSParser::Node::MATCH_BRANCH: {
				const auto *branch = static_cast<const BSParser::MatchBranchNode *>(node);
				if (segment == "block") {
					node = branch->block;
				} else if (segment.begins_with("pattern")) {
					const int index = segment.trim_prefix("pattern").to_int();
					node = index >= 0 && index < branch->patterns.size() ? branch->patterns[index] : nullptr;
				} else {
					node = nullptr;
				}
			} break;
			case BSParser::Node::PATTERN: {
				const auto *pattern = static_cast<const BSParser::PatternNode *>(node);
				const int index = segment.to_int();
				if (index >= 0 && index < pattern->array.size()) {
					node = pattern->array[index];
				} else if (index >= 0 && index < pattern->dictionary.size()) {
					node = pattern->dictionary[index].value_pattern;
				} else {
					node = nullptr;
				}
			} break;
			case BSParser::Node::RETURN:
				node = segment == "value" ? static_cast<const BSParser::Node *>(static_cast<const BSParser::ReturnNode *>(node)->return_value) : nullptr;
				break;
			case BSParser::Node::ASSERT:
				node = segment == "condition" ? static_cast<const BSParser::Node *>(static_cast<const BSParser::AssertNode *>(node)->condition) : nullptr;
				break;
			case BSParser::Node::ARRAY: {
				const auto *array = static_cast<const BSParser::ArrayNode *>(node);
				const int index = segment.to_int();
				node = index >= 0 && index < array->elements.size() ? array->elements[index] : nullptr;
			} break;
			case BSParser::Node::DICTIONARY: {
				const auto *dictionary = static_cast<const BSParser::DictionaryNode *>(node);
				const bool key = segment.begins_with("key");
				const bool value = segment.begins_with("value");
				const int index = segment.trim_prefix(key ? "key" : "value").to_int();
				node = (key || value) && index >= 0 && index < dictionary->elements.size() ? (key ? dictionary->elements[index].key : dictionary->elements[index].value) : nullptr;
			} break;
			default:
				node = nullptr;
				break;
		}
	}
	return node;
}

} // namespace barista_script::native_tests
