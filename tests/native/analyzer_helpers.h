/**************************************************************************/
/*  analyzer_helpers.h                                                    */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_analyzer.h"
#include "bs_warning.h"
#include <memory>
#include <string>
#include <vector>

namespace barista_script::native_tests {

struct ExpectedError {
	std::string message;
	int line;
	int column;
};

struct ExpectedWarning {
	BSWarning::Code code;
	std::string message;
	int start_line;
	int start_column;
	int end_line;
	int end_column;
};

struct AnalysisResult {
	std::unique_ptr<BSParser> parser;
	Error status = ERR_BUG;
	BSAnalyzer::AnalyzerPhase phase = BSAnalyzer::AnalyzerPhase::NONE;
	String path;

	bool valid() const { return status == OK && parser != nullptr && parser->get_errors().is_empty(); }
};

class AnalyzerSettings {
	struct SavedSetting {
		String path;
		bool present = false;
		Variant value;
	};
	Vector<SavedSetting> saved;

	void set(const String &path, const Variant &value);

public:
	AnalyzerSettings();
	~AnalyzerSettings();
	AnalyzerSettings(const AnalyzerSettings &) = delete;
	AnalyzerSettings &operator=(const AnalyzerSettings &) = delete;

	void warnings_enabled(bool enabled);
	void warning(BSWarning::Code code, BSWarning::WarnLevel level);
	void strict_null(bool enabled);
	void strict_dynamic(bool enabled);
};

class StrictAnalyzerSettingsScope {
	struct SavedSetting {
		String path;
		bool present = false;
		Variant value;
	};
	SavedSetting saved_strict_null;
	SavedSetting saved_strict_dynamic;

	static SavedSetting save(const String &path);
	static void restore(const SavedSetting &setting);

public:
	StrictAnalyzerSettingsScope();
	~StrictAnalyzerSettingsScope();
	StrictAnalyzerSettingsScope(const StrictAnalyzerSettingsScope &) = delete;
	StrictAnalyzerSettingsScope &operator=(const StrictAnalyzerSettingsScope &) = delete;

	void strict_null(bool enabled);
	void strict_dynamic(bool enabled);
};

AnalysisResult analyze_source(const String &source, const String &path);
bool source_analyzes(const String &source, const String &path);
void check_analysis(const AnalysisResult &result,
		const std::vector<ExpectedError> &errors,
		const std::vector<ExpectedWarning> &warnings,
		BSAnalyzer::AnalyzerPhase phase);
const BSParser::FunctionNode *find_function(const AnalysisResult &result, const StringName &name);
const BSParser::ExpressionNode *find_expression(const AnalysisResult &result, const StringName &name = "probe_expression");
bool expression_has_unary_sign(const BSParser::ExpressionNode *expression);
bool errors_contain(const AnalysisResult &result, const String &needle);
void check_scenario_orders(std::initializer_list<void (*)()> scenarios);
const BSParser::Node *find_statement_node(const BSParser::FunctionNode *function, const String &path);

} // namespace barista_script::native_tests
