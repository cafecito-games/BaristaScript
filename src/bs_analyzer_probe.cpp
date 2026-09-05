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
#include "bs_diagnostic_names.h"
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
	ClassDB::bind_method(D_METHOD("conformance_witness_lookup"),
			&BaristaScriptAnalyzerProbe::conformance_witness_lookup);
	ClassDB::bind_method(D_METHOD("conformance_hidden_witness"),
			&BaristaScriptAnalyzerProbe::conformance_hidden_witness);
	ClassDB::bind_method(D_METHOD("class_trait_binding_chain_coherence"),
			&BaristaScriptAnalyzerProbe::class_trait_binding_chain_coherence);
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
	StringName lookup_trait;
	String lookup_target;
	if (!registered.is_empty()) {
		lookup_trait = registered[0].trait_name;
		lookup_target = registered[0].target_fqcn;
	}
	result["lookup_trait"] = String(lookup_trait);
	result["lookup_target"] = lookup_target;
	result["registered_after_analyze"] = !lookup_target.is_empty() &&
			registry->has_conformance(lookup_target, lookup_trait);

	// Same-file / no Visibility: membership is visible.
	result["none_sees_registered"] = !lookup_target.is_empty() &&
			registry->has_conformance(lookup_target, lookup_trait);

	{
		BSConformanceRegistry::ScopedInFlightReplacement in_flight(declaring_path);
		result["in_flight_hides_has_conformance"] = lookup_target.is_empty() ||
				!registry->has_conformance(lookup_target, lookup_trait);
	}
	result["after_in_flight_sees_again"] = !lookup_target.is_empty() &&
			registry->has_conformance(lookup_target, lookup_trait);

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
			// Declaring file is unrelated to viewer — must be hidden under ConformanceVisibility.
			result["viewer_hides_unrelated_declaring"] = lookup_target.is_empty() ||
					!registry->has_conformance(lookup_target, lookup_trait);
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
			result["dep_sees_has_conformance"] = !lookup_target.is_empty() &&
					registry->has_conformance(lookup_target, lookup_trait);
		}
	}
	BSCache::clear_source_override(dep_on_declaring);
	BSCache::clear_source_override(declaring_path);

	const StringName old_trait = lookup_trait;
	const String old_target = lookup_target;

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
	result["cleared_after_reanalyze"] = (old_target.is_empty() ||
												!registry->has_conformance(old_target, old_trait)) &&
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
	const Vector<BSConformanceRegistry::Conformance> replaced =
			registry->get_file_conformances(declaring_path);
	StringName replacement_trait;
	String replacement_target;
	if (!replaced.is_empty()) {
		replacement_trait = replaced[0].trait_name;
		replacement_target = replaced[0].target_fqcn;
	}
	result["replaced_has_other"] = !replacement_target.is_empty() &&
			registry->has_conformance(replacement_target, replacement_trait) &&
			replacement_trait != old_trait;
	result["replaced_dropped_old"] = old_target.is_empty() ||
			!registry->has_conformance(old_target, old_trait);

	registry->clear_file(declaring_path);
	registry->clear_file(dep_path);
	registry->clear_file(viewer_path);
	registry->clear_file(unrelated_path);
	registry->clear_file(dep_on_declaring);
	BSCache::clear_source_override(dep_path);
	BSCache::clear_source_override(unrelated_path);

	return result;
}

godot::Dictionary BaristaScriptAnalyzerProbe::conformance_witness_lookup() const {
	godot::Dictionary result;

	BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
	ERR_FAIL_COND_V(registry == nullptr, result);

	const String declaring_path = "res://tests/wit_declare.barista";
	const String native_path = "res://tests/wit_native.barista";
	const String native_arity_path = "res://tests/wit_native_arity.barista";
	const String class_arity_path = "res://tests/wit_class_arity.barista";
	const String viewer_path = "res://tests/wit_viewer.barista";
	const String dep_path = "res://tests/wit_dep.barista";
	const String unrelated_path = "res://tests/wit_unrelated.barista";
	const String dep_on_declaring = "res://tests/wit_dep_on_decl.barista";

	registry->clear_file(declaring_path);
	registry->clear_file(native_path);
	registry->clear_file(native_arity_path);
	registry->clear_file(class_arity_path);
	registry->clear_file(viewer_path);
	registry->clear_file(dep_path);
	registry->clear_file(unrelated_path);
	registry->clear_file(dep_on_declaring);

	// Same-file CLASS target: call witness via self (class_name type annotations are not
	// same-file-resolvable for parameters; self carries the CLASS datatype).
	const String declaring_source =
			"class_name WitTarget extends Node\n"
			"\n"
			"trait WitGreeter:\n"
			"\tabstract func greet() -> String\n"
			"\n"
			"extend WitTarget uses WitGreeter:\n"
			"\tfunc greet() -> String:\n"
			"\t\treturn \"hi\"\n"
			"\n"
			"func call_witness() -> String:\n"
			"\treturn self.greet()\n";
	{
		BSCache::set_source_override(declaring_path, declaring_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(declaring_source, declaring_path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		godot::PackedStringArray errors;
		for (const BSParser::ParserError &pe : parser.get_errors()) {
			errors.push_back(pe.message);
		}
		result["same_file_analyze_ok"] = err == OK && errors.is_empty();
		result["same_file_errors"] = errors;
	}

	const Vector<BSConformanceRegistry::Conformance> registered =
			registry->get_file_conformances(declaring_path);
	result["registered_count"] = registered.size();
	StringName lookup_trait;
	String lookup_target;
	bool has_greet_witness = false;
	int conformance_index = -1;
	if (!registered.is_empty()) {
		lookup_trait = registered[0].trait_name;
		lookup_target = registered[0].target_fqcn;
		conformance_index = registered[0].conformance_index;
		has_greet_witness = registered[0].witnesses.has(SNAME("greet"));
	}
	result["has_greet_witness_key"] = has_greet_witness;
	result["lookup_target"] = lookup_target;
	result["lookup_trait"] = String(lookup_trait);
	result["registered_conformance_index"] = conformance_index;

	String found_file;
	int found_index = -1;
	const bool found_location = !lookup_target.is_empty() &&
			registry->find_witness_location(lookup_target, SNAME("greet"), found_file, found_index);
	result["find_witness_location_ok"] = found_location && found_file == declaring_path &&
			found_index == conformance_index;

	// Arity miss proves the CLASS witness signature was applied (VARIANT miss would not check).
	const String class_arity_source =
			"class_name WitArityTarget extends Node\n"
			"\n"
			"trait WitArityGreeter:\n"
			"\tabstract func greet() -> String\n"
			"\n"
			"extend WitArityTarget uses WitArityGreeter:\n"
			"\tfunc greet() -> String:\n"
			"\t\treturn \"hi\"\n"
			"\n"
			"func call_witness() -> void:\n"
			"\tself.greet(1)\n";
	{
		BSCache::set_source_override(class_arity_path, class_arity_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(class_arity_source, class_arity_path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		bool saw_arity = false;
		for (const BSParser::ParserError &pe : parser.get_errors()) {
			if (pe.message.contains("Too many arguments") && pe.message.contains("greet()")) {
				saw_arity = true;
			}
		}
		result["class_arity_checks_witness"] = saw_arity;
	}
	BSCache::clear_source_override(class_arity_path);
	registry->clear_file(class_arity_path);

	// Native Node target: typed receiver + call / arity.
	const String native_source =
			"trait WitNativeGreeter:\n"
			"\tabstract func wit_greet() -> String\n"
			"\n"
			"extend Node uses WitNativeGreeter:\n"
			"\tfunc wit_greet() -> String:\n"
			"\t\treturn \"hi\"\n"
			"\n"
			"func use(n: Node) -> String:\n"
			"\treturn n.wit_greet()\n";
	{
		BSCache::set_source_override(native_path, native_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(native_source, native_path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		result["native_analyze_ok"] = err == OK && parser.get_errors().is_empty();
	}
	BSCache::clear_source_override(native_path);

	const String native_arity_source =
			"trait WitNativeArity:\n"
			"\tabstract func wit_greet() -> String\n"
			"\n"
			"extend Node uses WitNativeArity:\n"
			"\tfunc wit_greet() -> String:\n"
			"\t\treturn \"hi\"\n"
			"\n"
			"func use(n: Node) -> void:\n"
			"\tn.wit_greet(1)\n";
	{
		BSCache::set_source_override(native_arity_path, native_arity_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(native_arity_source, native_arity_path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		bool saw_arity = false;
		for (const BSParser::ParserError &pe : parser.get_errors()) {
			if (pe.message.contains("Too many arguments") && pe.message.contains("wit_greet()")) {
				saw_arity = true;
			}
		}
		result["native_arity_checks_witness"] = saw_arity;
	}
	BSCache::clear_source_override(native_arity_path);
	registry->clear_file(native_arity_path);
	registry->clear_file(native_path);

	// Unrelated viewer under ConformanceVisibility must not see the CLASS witness location.
	// Re-ensure declaring registration is present (cleared native files only above).
	{
		BSCache::set_source_override(declaring_path, declaring_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(declaring_source, declaring_path, false);
		if (err == OK) {
			analyzer.analyze();
		}
	}
	const Vector<BSConformanceRegistry::Conformance> refreshed =
			registry->get_file_conformances(declaring_path);
	if (!refreshed.is_empty()) {
		lookup_trait = refreshed[0].trait_name;
		lookup_target = refreshed[0].target_fqcn;
	}

	BSCache::set_source_override(dep_path, "class_name WitDep extends Node\n");
	BSCache::set_source_override(unrelated_path, "class_name WitUnrelated extends Node\n");
	const String viewer_source = vformat("class_name WitViewer extends \"%s\"\n", dep_path);
	{
		BSCache::set_source_override(viewer_path, viewer_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		const Error err = parser.parse(viewer_source, viewer_path, false);
		result["viewer_parse_ok"] = err == OK;
		if (err == OK) {
			const BSConformanceRegistry::ScopedVisibility scope(&analyzer.conformance_visibility);
			String hidden_file;
			int hidden_index = -1;
			result["viewer_hides_witness_location"] = lookup_target.is_empty() ||
					!registry->find_witness_location(lookup_target, SNAME("greet"), hidden_file, hidden_index);
			result["viewer_cannot_see_declaring"] =
					!analyzer.conformance_visibility.can_see(declaring_path);
		}
	}
	BSCache::clear_source_override(viewer_path);

	const String dep_on_declaring_source = vformat(
			"class_name WitDepOnDecl extends Node\nconst _link = preload(\"%s\")\n", declaring_path);
	{
		BSCache::set_source_override(declaring_path, declaring_source);
		BSCache::set_source_override(dep_on_declaring, dep_on_declaring_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		const Error err = parser.parse(dep_on_declaring_source, dep_on_declaring, false);
		result["dep_parse_ok"] = err == OK;
		if (err == OK) {
			const BSConformanceRegistry::ScopedVisibility scope(&analyzer.conformance_visibility);
			String seen_file;
			int seen_index = -1;
			result["dep_sees_witness_location"] = !lookup_target.is_empty() &&
					registry->find_witness_location(lookup_target, SNAME("greet"), seen_file, seen_index) &&
					seen_file == declaring_path;
		}
	}
	BSCache::clear_source_override(dep_on_declaring);

	// Reanalysis without conformances clears witness keys.
	const String cleared_source = "class_name WitCleared extends Node\n";
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
	String cleared_file;
	int cleared_index = -1;
	result["cleared_witness_location"] = lookup_target.is_empty() ||
			!registry->find_witness_location(lookup_target, SNAME("greet"), cleared_file, cleared_index);
	result["cleared_file_empty"] = registry->get_file_conformances(declaring_path).is_empty();

	registry->clear_file(declaring_path);
	registry->clear_file(native_path);
	registry->clear_file(native_arity_path);
	registry->clear_file(class_arity_path);
	registry->clear_file(viewer_path);
	registry->clear_file(dep_path);
	registry->clear_file(unrelated_path);
	registry->clear_file(dep_on_declaring);
	BSCache::clear_source_override(declaring_path);
	BSCache::clear_source_override(native_path);
	BSCache::clear_source_override(dep_path);
	BSCache::clear_source_override(unrelated_path);

	return result;
}

godot::Dictionary BaristaScriptAnalyzerProbe::conformance_hidden_witness() const {
	godot::Dictionary result;

	BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
	ERR_FAIL_COND_V(registry == nullptr, result);

	const String declaring_path = "res://tests/hid_declare.barista";
	const String viewer_path = "res://tests/hid_viewer.barista";
	const String dep_path = "res://tests/hid_dep.barista";
	const String target_path = "res://tests/hid_target.barista";

	registry->clear_file(declaring_path);
	registry->clear_file(viewer_path);
	registry->clear_file(dep_path);
	registry->clear_file(target_path);

	// Builtin String target: closed receiver gets the hidden-witness diagnostic.
	// Host class_name lets a dependent extend this file for Visibility without preload-constant issues.
	const String declaring_source =
			"class_name HidDeclareHost extends Node\n"
			"\n"
			"trait HidStringMark:\n"
			"\tabstract func hid_mark() -> int\n"
			"\n"
			"extend String uses HidStringMark:\n"
			"\tfunc hid_mark() -> int:\n"
			"\t\treturn 7\n"
			"\n"
			"func same_file(s: String) -> int:\n"
			"\treturn s.hid_mark()\n";
	{
		BSCache::set_source_override(declaring_path, declaring_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(declaring_source, declaring_path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		result["declaring_analyze_ok"] = err == OK && parser.get_errors().is_empty();
	}

	const Vector<BSConformanceRegistry::Conformance> registered =
			registry->get_file_conformances(declaring_path);
	result["registered_count"] = registered.size();
	StringName lookup_trait;
	String lookup_target;
	bool has_hid_mark = false;
	if (!registered.is_empty()) {
		lookup_trait = registered[0].trait_name;
		lookup_target = registered[0].target_fqcn;
		has_hid_mark = registered[0].witnesses.has(SNAME("hid_mark"));
	}
	result["has_hid_mark_key"] = has_hid_mark;
	result["lookup_target"] = lookup_target;
	result["lookup_trait"] = String(lookup_trait);

	// Registry: visible location finds it with no Visibility; hidden-declaration is empty.
	{
		String found_file;
		int found_index = -1;
		result["visible_find_witness_location"] = !lookup_target.is_empty() &&
				registry->find_witness_location(lookup_target, SNAME("hid_mark"), found_file, found_index) &&
				found_file == declaring_path;
		String hidden_file;
		StringName hidden_trait;
		result["no_visibility_hides_nothing"] = !registry->find_hidden_witness_declaration(
				lookup_target, SNAME("hid_mark"), hidden_file, hidden_trait);
	}

	// Unrelated viewer under ConformanceVisibility: find_witness_location misses,
	// find_hidden_witness_declaration reports declaring file + trait.
	const String viewer_source =
			"class_name HidViewer extends Node\n"
			"\n"
			"func use(s: String) -> int:\n"
			"\treturn s.hid_mark()\n";
	{
		BSCache::set_source_override(viewer_path, viewer_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(viewer_source, viewer_path, false);
		result["viewer_parse_ok"] = err == OK;
		if (err == OK) {
			err = analyzer.analyze();
			godot::PackedStringArray errors;
			for (const BSParser::ParserError &pe : parser.get_errors()) {
				errors.push_back(pe.message);
			}
			result["viewer_errors"] = errors;
			bool saw_hidden = false;
			bool names_method = false;
			bool names_file = false;
			bool names_trait = false;
			for (const String &message : errors) {
				if (message.contains("which this file does not load")) {
					saw_hidden = true;
				}
				if (message.contains("hid_mark()")) {
					names_method = true;
				}
				if (message.contains(bs_diagnostic_file_reference(declaring_path)) ||
						message.contains(declaring_path.get_file())) {
					names_file = true;
				}
				if (!lookup_trait.is_empty() && message.contains(String(lookup_trait))) {
					names_trait = true;
				}
			}
			result["viewer_hidden_diagnostic"] = saw_hidden && names_method && names_file && names_trait;
			result["viewer_analyze_failed"] = err != OK || !errors.is_empty();

			const BSConformanceRegistry::ScopedVisibility scope(&analyzer.conformance_visibility);
			String hidden_file;
			StringName hidden_trait;
			result["viewer_finds_hidden_declaration"] = !lookup_target.is_empty() &&
					registry->find_hidden_witness_declaration(lookup_target, SNAME("hid_mark"),
							hidden_file, hidden_trait) &&
					hidden_file == declaring_path && hidden_trait == lookup_trait;
			String visible_file;
			int visible_index = -1;
			result["viewer_hides_witness_location"] = lookup_target.is_empty() ||
					!registry->find_witness_location(lookup_target, SNAME("hid_mark"), visible_file, visible_index);
		}
	}
	BSCache::clear_source_override(viewer_path);

	// Dependent that extends the declaring host still resolves the witness.
	const String dep_on_declaring_source = vformat(
			"class_name HidDepOnDecl extends \"%s\"\n"
			"\n"
			"func use(s: String) -> int:\n"
			"\treturn s.hid_mark()\n",
			declaring_path);
	{
		BSCache::set_source_override(declaring_path, declaring_source);
		BSCache::set_source_override(dep_path, dep_on_declaring_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(dep_on_declaring_source, dep_path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		godot::PackedStringArray dep_errors;
		for (const BSParser::ParserError &pe : parser.get_errors()) {
			dep_errors.push_back(pe.message);
		}
		result["dep_errors"] = dep_errors;
		result["dep_analyze_ok"] = err == OK && dep_errors.is_empty();
	}
	BSCache::clear_source_override(dep_path);

	// Final CLASS: same-file registration, then an unrelated viewer Visibility must see the
	// declaration as hidden (call-site diagnostic for CLASS needs a resolvable final receiver
	// without loading the declaring file; path-form `extend "res://..."` is not parsed).
	const String final_declaring_source =
			"final class_name HidFinalTarget extends Node\n"
			"\n"
			"trait HidFinalGreeter:\n"
			"\tabstract func hid_greet() -> String\n"
			"\n"
			"extend HidFinalTarget uses HidFinalGreeter:\n"
			"\tfunc hid_greet() -> String:\n"
			"\t\treturn \"hi\"\n"
			"\n"
			"func same_file() -> String:\n"
			"\treturn self.hid_greet()\n";
	String final_lookup_target;
	StringName final_lookup_trait;
	{
		BSCache::set_source_override(declaring_path, final_declaring_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(final_declaring_source, declaring_path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		godot::PackedStringArray errors;
		for (const BSParser::ParserError &pe : parser.get_errors()) {
			errors.push_back(pe.message);
		}
		result["final_declaring_errors"] = errors;
		result["final_declaring_analyze_ok"] = err == OK && errors.is_empty();
		const Vector<BSConformanceRegistry::Conformance> final_registered =
				registry->get_file_conformances(declaring_path);
		if (!final_registered.is_empty()) {
			final_lookup_target = final_registered[0].target_fqcn;
			final_lookup_trait = final_registered[0].trait_name;
			result["final_has_greet_key"] = final_registered[0].witnesses.has(SNAME("hid_greet"));
		} else {
			result["final_has_greet_key"] = false;
		}
		result["final_lookup_target"] = final_lookup_target;
	}
	{
		const String final_viewer_source = "class_name HidFinalViewer extends Node\n";
		BSCache::set_source_override(viewer_path, final_viewer_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		const Error err = parser.parse(final_viewer_source, viewer_path, false);
		result["final_viewer_parse_ok"] = err == OK;
		if (err == OK) {
			const BSConformanceRegistry::ScopedVisibility scope(&analyzer.conformance_visibility);
			String hidden_file;
			StringName hidden_trait;
			result["final_viewer_finds_hidden"] = !final_lookup_target.is_empty() &&
					registry->find_hidden_witness_declaration(final_lookup_target, SNAME("hid_greet"),
							hidden_file, hidden_trait) &&
					hidden_file == declaring_path && hidden_trait == final_lookup_trait;
			String visible_file;
			int visible_index = -1;
			result["final_viewer_hides_location"] = final_lookup_target.is_empty() ||
					!registry->find_witness_location(final_lookup_target, SNAME("hid_greet"),
							visible_file, visible_index);
		}
	}

	registry->clear_file(declaring_path);
	registry->clear_file(viewer_path);
	registry->clear_file(dep_path);
	registry->clear_file(target_path);
	BSCache::clear_source_override(declaring_path);
	BSCache::clear_source_override(viewer_path);
	BSCache::clear_source_override(dep_path);
	BSCache::clear_source_override(target_path);

	return result;
}

godot::Dictionary BaristaScriptAnalyzerProbe::class_trait_binding_chain_coherence() const {
	godot::Dictionary result;

	BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
	ERR_FAIL_COND_V(registry == nullptr, result);

	const String binding_file = "res://tests/ctb_binding.barista";
	const String conformance_file = "res://tests/ctb_conform.barista";
	const String agree_file = "res://tests/ctb_agree.barista";

	registry->clear_file(binding_file);
	registry->clear_file(conformance_file);
	registry->clear_file(agree_file);

	auto builtin_args = [](Variant::Type p_type) -> Vector<BSConformanceRegistry::RecordedTypeArgument> {
		BSConformanceRegistry::RecordedTypeArgument argument;
		argument.kind = BSConformanceRegistry::RecordedTypeArgument::BUILTIN;
		argument.builtin_type = p_type;
		Vector<BSConformanceRegistry::RecordedTypeArgument> arguments;
		arguments.push_back(argument);
		return arguments;
	};

	// Green path: publish a class-uses binding and confirm it is stored.
	BSConformanceRegistry::ClassTraitBinding binding;
	binding.target_fqcn = "res://tests/ctb_parent.barista";
	binding.target_label = "CtbParent";
	binding.target_native_base = SNAME("Node");
	binding.trait_name = SNAME("CtbStorage");
	binding.trait_label = "CtbStorage";
	binding.trait_type_arguments = builtin_args(Variant::INT);
	binding.source_file = binding_file;

	Vector<BSConformanceRegistry::ClassTraitBinding> bindings;
	bindings.push_back(binding);

	const BSConformanceRegistry::RegistrationResult publish =
			registry->try_replace_file_conformances(binding_file,
					Vector<BSConformanceRegistry::Conformance>(), bindings);
	result["publish_ok"] = publish.conflicts.is_empty() && publish.binding_conflicts.is_empty();
	result["publish_registered_count"] = publish.registered_count;
	const Vector<BSConformanceRegistry::ClassTraitBinding> stored =
			registry->get_file_trait_bindings(binding_file);
	result["binding_stored"] = stored.size() == 1 && stored[0].trait_name == SNAME("CtbStorage") &&
			stored[0].trait_type_arguments.size() == 1 &&
			stored[0].trait_type_arguments[0].kind == BSConformanceRegistry::RecordedTypeArgument::BUILTIN &&
			stored[0].trait_type_arguments[0].builtin_type == Variant::INT;

	// Matching args on a descendant Conformance must not reject.
	BSConformanceRegistry::Conformance agree;
	agree.target_keys.push_back("res://tests/ctb_child.barista");
	agree.target_fqcn = "res://tests/ctb_child.barista";
	agree.target_script_path = "res://tests/ctb_child.barista";
	agree.target_is_root_class = true;
	agree.trait_name = SNAME("CtbStorage");
	agree.trait_type_arguments = builtin_args(Variant::INT);
	agree.target_native_base = SNAME("Node");
	agree.target_script_ancestor_fqcns.push_back("res://tests/ctb_parent.barista");
	agree.target_label = "CtbChild";
	agree.source_file = agree_file;
	agree.conformance_index = 0;

	Vector<BSConformanceRegistry::Conformance> agree_candidates;
	agree_candidates.push_back(agree);
	const BSConformanceRegistry::RegistrationResult agree_result =
			registry->try_replace_file_conformances(agree_file, agree_candidates);
	result["agree_registered_count"] = agree_result.registered_count;
	result["agree_no_chain_conflict"] = agree_result.conflicts.is_empty();

	// Conflicting args on the same chain must yield CHAIN_COHERENCE and reject the declaration.
	BSConformanceRegistry::Conformance conflict_entry;
	conflict_entry.target_keys.push_back("res://tests/ctb_other_child.barista");
	conflict_entry.target_fqcn = "res://tests/ctb_other_child.barista";
	conflict_entry.target_script_path = "res://tests/ctb_other_child.barista";
	conflict_entry.target_is_root_class = true;
	conflict_entry.trait_name = SNAME("CtbStorage");
	conflict_entry.trait_type_arguments = builtin_args(Variant::STRING);
	conflict_entry.target_native_base = SNAME("Node");
	conflict_entry.target_script_ancestor_fqcns.push_back("res://tests/ctb_parent.barista");
	conflict_entry.target_label = "CtbOtherChild";
	conflict_entry.source_file = conformance_file;
	conflict_entry.conformance_index = 0;

	Vector<BSConformanceRegistry::Conformance> conflict_candidates;
	conflict_candidates.push_back(conflict_entry);
	const BSConformanceRegistry::RegistrationResult conflict_result =
			registry->try_replace_file_conformances(conformance_file, conflict_candidates);
	result["conflict_registered_count"] = conflict_result.registered_count;
	bool saw_chain = false;
	for (int i = 0; i < conflict_result.conflicts.size(); i++) {
		if (conflict_result.conflicts[i].kind == BSConformanceRegistry::RegistrationConflict::CHAIN_COHERENCE) {
			saw_chain = true;
			result["conflict_index"] = conflict_result.conflicts[i].conformance_index;
			result["conflict_conflicting_label"] = conflict_result.conflicts[i].conflicting_target_label;
			result["conflict_conflicting_file"] = conflict_result.conflicts[i].conflicting_source_file;
			break;
		}
	}
	result["conflict_chain_coherence"] = saw_chain;
	result["conflict_rejected"] = conflict_result.registered_count == 0 && saw_chain;
	result["conflict_store_empty"] = registry->get_file_conformances(conformance_file).is_empty();

	// reduce_type_argument round-trip for a builtin.
	BSParser::DataType int_type;
	int_type.kind = BSParser::DataType::BUILTIN;
	int_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	int_type.builtin_type = Variant::INT;
	const BSConformanceRegistry::RecordedTypeArgument reduced =
			BSConformanceRegistry::reduce_type_argument(int_type);
	result["reduce_builtin_ok"] = reduced.kind == BSConformanceRegistry::RecordedTypeArgument::BUILTIN &&
			reduced.builtin_type == Variant::INT;

	registry->clear_file(binding_file);
	registry->clear_file(conformance_file);
	registry->clear_file(agree_file);

	// --- Reverse load-edge licensing (Foundry ncc_binding_edge_* @ c9d5e35) ---
	// The loaded file cannot see the loader. Comparison is licensed by the loader's
	// recorded load edge via `_file_loads(existing->source_file, p_source_file)`.
	const String loader_file = "res://tests/ctb_load_edge_loader.barista";
	const String loaded_file = "res://tests/ctb_load_edge_loaded.barista";
	const StringName edge_trait = SNAME("CtbLoadEdgeStorage");
	registry->clear_file(loader_file);
	registry->clear_file(loaded_file);

	class LoadsOnlySelf : public BSConformanceRegistry::Visibility {
		String allowed;

	public:
		explicit LoadsOnlySelf(const String &p_allowed) :
				allowed(p_allowed) {}
		bool can_see(const String &p_source_file) const override {
			return p_source_file == allowed;
		}
	};

	HashSet<String> loader_loads;
	loader_loads.insert(loaded_file);

	// Case A: loader publishes a ClassTraitBinding first (with load edge); loaded later
	// publishes a contradicting Conformance without Visibility of the loader.
	{
		BSConformanceRegistry::ClassTraitBinding loader_binding;
		loader_binding.target_fqcn = "res://tests/ctb_load_edge_holder.barista";
		loader_binding.target_label = "CtbLoadEdgeHolder";
		loader_binding.target_native_base = SNAME("RefCounted");
		loader_binding.trait_name = edge_trait;
		loader_binding.trait_label = "CtbLoadEdgeStorage";
		loader_binding.trait_type_arguments = builtin_args(Variant::STRING);
		loader_binding.source_file = loader_file;

		Vector<BSConformanceRegistry::ClassTraitBinding> loader_bindings;
		loader_bindings.push_back(loader_binding);

		LoadsOnlySelf loader_visibility(loader_file);
		{
			const BSConformanceRegistry::ScopedVisibility scoped(&loader_visibility);
			const BSConformanceRegistry::RegistrationResult loader_result =
					registry->try_replace_file_conformances(loader_file,
							Vector<BSConformanceRegistry::Conformance>(), loader_bindings, loader_loads);
			result["edge_loader_binding_ok"] = loader_result.binding_conflicts.is_empty();
		}

		LoadsOnlySelf loaded_visibility(loaded_file);
		const BSConformanceRegistry::ScopedVisibility scoped(&loaded_visibility);

		BSConformanceRegistry::Conformance loaded_conform;
		loaded_conform.target_keys.push_back("RefCounted");
		loaded_conform.target_fqcn = "RefCounted";
		loaded_conform.target_script_path = String();
		loaded_conform.target_is_root_class = true;
		loaded_conform.trait_name = edge_trait;
		loaded_conform.trait_type_arguments = builtin_args(Variant::INT);
		loaded_conform.target_native_base = SNAME("RefCounted");
		loaded_conform.target_label = "RefCounted";
		loaded_conform.source_file = loaded_file;
		loaded_conform.conformance_index = 0;

		Vector<BSConformanceRegistry::Conformance> loaded_candidates;
		loaded_candidates.push_back(loaded_conform);
		const BSConformanceRegistry::RegistrationResult loaded_result =
				registry->try_replace_file_conformances(loaded_file, loaded_candidates);

		bool edge_chain = false;
		for (int i = 0; i < loaded_result.conflicts.size(); i++) {
			if (loaded_result.conflicts[i].kind == BSConformanceRegistry::RegistrationConflict::CHAIN_COHERENCE &&
					loaded_result.conflicts[i].conflicting_source_file == loader_file) {
				edge_chain = true;
				break;
			}
		}
		result["edge_reverse_chain_coherence"] = edge_chain;
		result["edge_reverse_rejected"] = loaded_result.registered_count == 0 && edge_chain;
		result["edge_reverse_store_empty"] = registry->get_file_conformances(loaded_file).is_empty();
	}

	registry->clear_file(loader_file);
	registry->clear_file(loaded_file);

	// Case B: no load edge either way — contradicting pair stays uncompared.
	{
		const String unrelated_file = "res://tests/ctb_load_edge_unrelated.barista";
		const String other_file = "res://tests/ctb_load_edge_other.barista";
		registry->clear_file(unrelated_file);

		HashSet<String> unrelated_loads;
		unrelated_loads.insert(other_file);
		BSConformanceRegistry::Conformance unrelated_conform;
		unrelated_conform.target_keys.push_back("RefCounted");
		unrelated_conform.target_fqcn = "RefCounted";
		unrelated_conform.target_is_root_class = true;
		unrelated_conform.trait_name = edge_trait;
		unrelated_conform.trait_type_arguments = builtin_args(Variant::INT);
		unrelated_conform.target_native_base = SNAME("RefCounted");
		unrelated_conform.target_label = "RefCounted";
		unrelated_conform.source_file = unrelated_file;
		unrelated_conform.conformance_index = 0;
		Vector<BSConformanceRegistry::Conformance> unrelated_candidates;
		unrelated_candidates.push_back(unrelated_conform);
		registry->try_replace_file_conformances(unrelated_file, unrelated_candidates,
				Vector<BSConformanceRegistry::ClassTraitBinding>(), unrelated_loads);

		BSConformanceRegistry::ClassTraitBinding noedge_binding;
		noedge_binding.target_fqcn = "res://tests/ctb_load_edge_holder.barista";
		noedge_binding.target_label = "CtbLoadEdgeHolder";
		noedge_binding.target_native_base = SNAME("RefCounted");
		noedge_binding.trait_name = edge_trait;
		noedge_binding.trait_label = "CtbLoadEdgeStorage";
		noedge_binding.trait_type_arguments = builtin_args(Variant::STRING);
		noedge_binding.source_file = loader_file;
		Vector<BSConformanceRegistry::ClassTraitBinding> noedge_bindings;
		noedge_bindings.push_back(noedge_binding);

		LoadsOnlySelf binding_visibility(loader_file);
		const BSConformanceRegistry::ScopedVisibility scoped(&binding_visibility);
		const BSConformanceRegistry::RegistrationResult noedge_result =
				registry->try_replace_file_conformances(loader_file,
						Vector<BSConformanceRegistry::Conformance>(), noedge_bindings);
		result["edge_noedge_uncompared"] = noedge_result.binding_conflicts.is_empty();

		registry->clear_file(unrelated_file);
	}

	registry->clear_file(loader_file);
	registry->clear_file(loaded_file);

	return result;
}

} // namespace barista_script

#endif // DEBUG_ENABLED
