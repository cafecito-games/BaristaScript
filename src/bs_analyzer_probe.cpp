/**************************************************************************/
/*  bs_analyzer_probe.cpp                                                 */
/*                                                                        */
/*  Debug-only analyzer probe for #43/#49/#52 GDScript suites.            */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#ifdef DEBUG_ENABLED

#include "bs_analyzer_probe.h"

#include "barista_script_language.h"
#include "bs_analyzer.h"
#include "bs_cache.h"
#include "bs_conformance_registry.h"
#include "bs_parser.h"

namespace barista_script {

namespace {

bool _expression_has_unary_sign(const BSParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return false;
	}
	if (p_expression->type == BSParser::Node::UNARY_OPERATOR) {
		const BSParser::UnaryOpNode *unary = static_cast<const BSParser::UnaryOpNode *>(p_expression);
		if (unary->operation == BSParser::UnaryOpNode::OP_NEGATIVE || unary->operation == BSParser::UnaryOpNode::OP_POSITIVE) {
			return true;
		}
		return _expression_has_unary_sign(unary->operand);
	}
	if (p_expression->type == BSParser::Node::BINARY_OPERATOR) {
		const BSParser::BinaryOpNode *binary = static_cast<const BSParser::BinaryOpNode *>(p_expression);
		return _expression_has_unary_sign(binary->left_operand) || _expression_has_unary_sign(binary->right_operand);
	}
	return false;
}

const BSParser::ExpressionNode *_find_fold_expression(const BSParser::ClassNode *p_tree) {
	if (p_tree == nullptr) {
		return nullptr;
	}
	for (int i = 0; i < p_tree->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_tree->members[i];
		if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr &&
				member.variable->identifier != nullptr && member.variable->identifier->name == SNAME("probe_expression")) {
			return member.variable->initializer;
		}
	}
	return nullptr;
}

} // namespace

void BaristaScriptAnalyzerProbe::_bind_methods() {
	ClassDB::bind_method(D_METHOD("fold_expression", "expression_source"), &BaristaScriptAnalyzerProbe::fold_expression);
	ClassDB::bind_method(D_METHOD("analyze_source", "source", "path"), &BaristaScriptAnalyzerProbe::analyze_source);
	ClassDB::bind_method(D_METHOD("is_semantically_valid", "source", "path"), &BaristaScriptAnalyzerProbe::is_semantically_valid);
	ClassDB::bind_method(D_METHOD("validate_source", "source", "path", "warnings"), &BaristaScriptAnalyzerProbe::validate_source, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("conformance_visibility_can_see", "source", "path", "candidates"),
			&BaristaScriptAnalyzerProbe::conformance_visibility_can_see);
	ClassDB::bind_method(D_METHOD("scoped_visibility_nest_restore"), &BaristaScriptAnalyzerProbe::scoped_visibility_nest_restore);
	ClassDB::bind_method(D_METHOD("conformance_registry_registration"),
			&BaristaScriptAnalyzerProbe::conformance_registry_registration);
}

godot::Dictionary BaristaScriptAnalyzerProbe::fold_expression(const godot::String &p_expression_source) const {
	godot::Dictionary result;
	result["ok"] = false;
	result["value"] = Variant();
	result["value_type"] = (int)Variant::NIL;
	result["has_unary_sign"] = false;
	godot::PackedStringArray errors;

	const String path = "res://tests/fold_probe.barista";
	const String source = vformat("var probe_expression = %s\n", p_expression_source);
	Error err = ERR_BUG;
	bool has_unary_sign = false;
	bool is_constant = false;
	Variant reduced_value;
	bool found_expression = false;
	{
		BSCache::set_source_override(path, source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		err = parser.parse(source, path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		for (const BSParser::ParserError &pe : parser.get_errors()) {
			errors.push_back(pe.message);
		}
		if (err == OK && errors.is_empty()) {
			const BSParser::ExpressionNode *expression = _find_fold_expression(parser.get_tree());
			if (expression != nullptr) {
				found_expression = true;
				has_unary_sign = _expression_has_unary_sign(expression);
				is_constant = expression->is_constant;
				reduced_value = expression->reduced_value;
			}
		}
	}
	BSCache::clear_source_override(path);
	result["errors"] = errors;
	if (err != OK || !errors.is_empty()) {
		return result;
	}
	if (!found_expression) {
		errors.push_back("No return expression found.");
		result["errors"] = errors;
		return result;
	}
	result["has_unary_sign"] = has_unary_sign;
	if (!is_constant) {
		errors.push_back("Expression did not constant-fold.");
		result["errors"] = errors;
		return result;
	}
	result["ok"] = true;
	result["value"] = reduced_value;
	result["value_type"] = (int)reduced_value.get_type();
	return result;
}

godot::Dictionary BaristaScriptAnalyzerProbe::analyze_source(const godot::String &p_source, const godot::String &p_path) const {
	godot::Dictionary result;
	const String path = p_path.is_empty() ? String("res://tests/analyzer_probe.barista") : p_path;
	godot::PackedStringArray errors;
	Error err = ERR_BUG;
	int phase = -1;
	{
		BSCache::set_source_override(path, p_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		err = parser.parse(p_source, path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		for (const BSParser::ParserError &pe : parser.get_errors()) {
			errors.push_back(pe.message);
		}
		phase = (int)analyzer.get_highest_completed_phase();
	}
	BSCache::clear_source_override(path);
	result["valid"] = err == OK && errors.is_empty();
	result["errors"] = errors;
	result["phase"] = phase;
	return result;
}

bool BaristaScriptAnalyzerProbe::is_semantically_valid(const godot::String &p_source, const godot::String &p_path) const {
	const String path = p_path.is_empty() ? String("res://tests/analyzer_probe.barista") : p_path;
	bool ok = false;
	{
		BSCache::set_source_override(path, p_source);
		ok = bs_source_analyzes(p_source, path);
	}
	BSCache::clear_source_override(path);
	return ok;
}

godot::Dictionary BaristaScriptAnalyzerProbe::validate_source(const godot::String &p_source, const godot::String &p_path, bool p_warnings) const {
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	ERR_FAIL_COND_V(language == nullptr, godot::Dictionary());
	const String path = p_path.is_empty() ? String("res://tests/analyzer_probe.barista") : p_path;
	return language->_validate(p_source, path, true, true, p_warnings, false);
}

godot::Dictionary BaristaScriptAnalyzerProbe::conformance_visibility_can_see(const godot::String &p_source,
		const godot::String &p_path, const godot::PackedStringArray &p_candidates) const {
	godot::Dictionary result;
	result["ok"] = false;
	godot::Dictionary can_see;
	godot::PackedStringArray errors;
	const String path = p_path.is_empty() ? String("res://tests/visibility_probe.barista") : p_path;
	{
		BSCache::set_source_override(path, p_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		const Error err = parser.parse(p_source, path, false);
		for (const BSParser::ParserError &pe : parser.get_errors()) {
			errors.push_back(pe.message);
		}
		if (err == OK) {
			// Install the analyzer's visibility the way resolve_*/analyze and foreign scopes do.
			const BSConformanceRegistry::ScopedVisibility scope(&analyzer.conformance_visibility);
			for (int i = 0; i < p_candidates.size(); i++) {
				const String candidate = p_candidates[i];
				can_see[candidate] = analyzer.conformance_visibility.can_see(candidate);
			}
			result["ok"] = true;
		}
	}
	BSCache::clear_source_override(path);
	result["can_see"] = can_see;
	result["errors"] = errors;
	return result;
}

godot::Dictionary BaristaScriptAnalyzerProbe::scoped_visibility_nest_restore() const {
	godot::Dictionary result;

	class AllowOnlyA : public BSConformanceRegistry::Visibility {
	public:
		bool can_see(const String &p_source_file) const override {
			return p_source_file == "res://a.barista";
		}
	};
	class AllowOnlyB : public BSConformanceRegistry::Visibility {
	public:
		bool can_see(const String &p_source_file) const override {
			return p_source_file == "res://b.barista";
		}
	};

	AllowOnlyA allow_a;
	AllowOnlyB allow_b;

	// No visibility installed: everything visible (runtime/tooling default).
	result["none_sees_a"] = BSConformanceRegistry::debug_is_visible("res://a.barista");
	result["none_sees_b"] = BSConformanceRegistry::debug_is_visible("res://b.barista");
	result["none_sees_c"] = BSConformanceRegistry::debug_is_visible("res://c.barista");

	{
		BSConformanceRegistry::ScopedVisibility outer(&allow_a);
		result["outer_sees_a"] = BSConformanceRegistry::debug_is_visible("res://a.barista");
		result["outer_hides_b"] = !BSConformanceRegistry::debug_is_visible("res://b.barista");
		{
			BSConformanceRegistry::ScopedVisibility nested(&allow_b);
			result["nested_sees_b"] = BSConformanceRegistry::debug_is_visible("res://b.barista");
			result["nested_hides_a"] = !BSConformanceRegistry::debug_is_visible("res://a.barista");
		}
		result["restored_sees_a"] = BSConformanceRegistry::debug_is_visible("res://a.barista");
		result["restored_hides_b"] = !BSConformanceRegistry::debug_is_visible("res://b.barista");
	}
	result["cleared_sees_a"] = BSConformanceRegistry::debug_is_visible("res://a.barista");
	result["cleared_sees_b"] = BSConformanceRegistry::debug_is_visible("res://b.barista");

	{
		BSConformanceRegistry::ScopedVisibility outer(&allow_a);
		BSConformanceRegistry::ScopedInFlightReplacement in_flight("res://a.barista");
		result["in_flight_hides_own"] = !BSConformanceRegistry::debug_is_visible("res://a.barista");
	}
	result["after_in_flight_sees_a"] = BSConformanceRegistry::debug_is_visible("res://a.barista");

	// Empty store lookups fail closed.
	BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
	result["empty_has_conformance"] = registry != nullptr &&
			!registry->has_conformance("res://Widget", StringName("SomeTrait"));

	return result;
}

godot::Dictionary BaristaScriptAnalyzerProbe::conformance_registry_registration() const {
	godot::Dictionary result;

	BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
	ERR_FAIL_COND_V(registry == nullptr, result);

	const String declaring_path = "res://tests/reg_declare.barista";
	const String dep_path = "res://tests/reg_dep.barista";
	const String viewer_path = "res://tests/reg_viewer.barista";
	const String unrelated_path = "res://tests/reg_unrelated.barista";
	const StringName trait_name = StringName("RegMarker");
	const String target_key = "Node";

	registry->clear_file(declaring_path);
	registry->clear_file(dep_path);
	registry->clear_file(viewer_path);
	registry->clear_file(unrelated_path);

	const String declaring_source =
			"trait RegMarker:\n\tpass\n\nextend Node uses RegMarker:\n\tpass\n";
	{
		BSCache::set_source_override(declaring_path, declaring_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(declaring_source, declaring_path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		result["analyze_ok"] = err == OK && parser.get_errors().is_empty();
	}
	BSCache::clear_source_override(declaring_path);

	const Vector<BSConformanceRegistry::Conformance> registered =
			registry->get_file_conformances(declaring_path);
	result["registered_count"] = registered.size();
	result["registered_after_analyze"] = registry->has_conformance(target_key, trait_name);

	// Same-file / no Visibility: membership is visible.
	result["none_sees_registered"] = registry->has_conformance(target_key, trait_name);

	{
		BSConformanceRegistry::ScopedInFlightReplacement in_flight(declaring_path);
		result["in_flight_hides_has_conformance"] = !registry->has_conformance(target_key, trait_name);
	}
	result["after_in_flight_sees_again"] = registry->has_conformance(target_key, trait_name);

	// Dependency viewer with ConformanceVisibility can_see the declaring file.
	BSCache::set_source_override(dep_path, "class_name RegDep extends Node\n");
	BSCache::set_source_override(unrelated_path, "class_name RegUnrelated extends Node\n");
	const String viewer_source = vformat("class_name RegViewer extends \"%s\"\n", dep_path);
	{
		BSCache::set_source_override(viewer_path, viewer_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		const Error err = parser.parse(viewer_source, viewer_path, false);
		result["viewer_parse_ok"] = err == OK;
		if (err == OK) {
			const BSConformanceRegistry::ScopedVisibility scope(&analyzer.conformance_visibility);
			// Preload declaring path into visible set via dependency edge: extends dep only.
			// Declaring file is unrelated to viewer — must be hidden under ConformanceVisibility.
			result["viewer_hides_unrelated_declaring"] =
					!registry->has_conformance(target_key, trait_name);
			result["viewer_can_see_own"] = analyzer.conformance_visibility.can_see(viewer_path);
			result["viewer_can_see_dep"] = analyzer.conformance_visibility.can_see(dep_path);
			result["viewer_cannot_see_declaring"] =
					!analyzer.conformance_visibility.can_see(declaring_path);
			result["viewer_cannot_see_unrelated"] =
					!analyzer.conformance_visibility.can_see(unrelated_path);
		}
	}
	BSCache::clear_source_override(viewer_path);

	// A file that depends on the declaring path can see its registered conformances.
	const String dep_on_declaring = "res://tests/reg_dep_on_decl.barista";
	const String dep_on_declaring_source =
			vformat("class_name RegDepOnDecl extends Node\nconst _link = preload(\"%s\")\n", declaring_path);
	{
		BSCache::set_source_override(declaring_path, declaring_source);
		BSCache::set_source_override(dep_on_declaring, dep_on_declaring_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		const Error err = parser.parse(dep_on_declaring_source, dep_on_declaring, false);
		result["dep_parse_ok"] = err == OK;
		if (err == OK) {
			const BSConformanceRegistry::ScopedVisibility scope(&analyzer.conformance_visibility);
			result["dep_can_see_declaring"] = analyzer.conformance_visibility.can_see(declaring_path);
			result["dep_sees_has_conformance"] = registry->has_conformance(target_key, trait_name);
		}
	}
	BSCache::clear_source_override(dep_on_declaring);
	BSCache::clear_source_override(declaring_path);

	// Reanalysis with no conformances clears stale entries.
	const String cleared_source = "class_name RegCleared extends Node\n";
	{
		BSCache::set_source_override(declaring_path, cleared_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(cleared_source, declaring_path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		result["reanalyze_clear_ok"] = err == OK && parser.get_errors().is_empty();
	}
	BSCache::clear_source_override(declaring_path);
	result["cleared_after_reanalyze"] = !registry->has_conformance(target_key, trait_name) &&
			registry->get_file_conformances(declaring_path).is_empty();

	// Reanalysis replaces with a different trait membership.
	const String replace_source =
			"trait RegOther:\n\tpass\n\nextend Node uses RegOther:\n\tpass\n";
	{
		BSCache::set_source_override(declaring_path, replace_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(replace_source, declaring_path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		result["reanalyze_replace_ok"] = err == OK && parser.get_errors().is_empty();
	}
	BSCache::clear_source_override(declaring_path);
	result["replaced_has_other"] = registry->has_conformance(target_key, StringName("RegOther"));
	result["replaced_dropped_old"] = !registry->has_conformance(target_key, trait_name);

	registry->clear_file(declaring_path);
	registry->clear_file(dep_path);
	registry->clear_file(viewer_path);
	registry->clear_file(unrelated_path);
	registry->clear_file(dep_on_declaring);
	BSCache::clear_source_override(dep_path);
	BSCache::clear_source_override(unrelated_path);

	return result;
}

} // namespace barista_script

#endif // DEBUG_ENABLED
