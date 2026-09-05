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
#include "bs_type.h"

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
	ClassDB::bind_method(D_METHOD("recorded_trait_arguments_query"),
			&BaristaScriptAnalyzerProbe::recorded_trait_arguments_query);
	ClassDB::bind_method(D_METHOD("trait_target_assignability"),
			&BaristaScriptAnalyzerProbe::trait_target_assignability);
	ClassDB::bind_method(D_METHOD("witness_collision_arbitration"),
			&BaristaScriptAnalyzerProbe::witness_collision_arbitration);
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

godot::Dictionary BaristaScriptAnalyzerProbe::recorded_trait_arguments_query() const {
	godot::Dictionary result;

	BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
	ERR_FAIL_COND_V(registry == nullptr, result);

	const String script_file = "res://tests/rta_script.barista";
	const String farther_file = "res://tests/rta_farther.barista";
	const String nearer_file = "res://tests/rta_nearer.barista";
	const String native_file = "res://tests/rta_native.barista";
	const String builtin_file = "res://tests/rta_builtin.barista";
	const String empty_near_file = "res://tests/rta_empty_near.barista";

	registry->clear_file(script_file);
	registry->clear_file(farther_file);
	registry->clear_file(nearer_file);
	registry->clear_file(native_file);
	registry->clear_file(builtin_file);
	registry->clear_file(empty_near_file);

	auto builtin_args = [](Variant::Type p_type) -> Vector<BSConformanceRegistry::RecordedTypeArgument> {
		BSConformanceRegistry::RecordedTypeArgument argument;
		argument.kind = BSConformanceRegistry::RecordedTypeArgument::BUILTIN;
		argument.builtin_type = p_type;
		Vector<BSConformanceRegistry::RecordedTypeArgument> arguments;
		arguments.push_back(argument);
		return arguments;
	};

	auto make_conformance = [](const String &p_target_key, const StringName &p_trait,
									const Vector<BSConformanceRegistry::RecordedTypeArgument> &p_args,
									const String &p_source, int p_index) -> BSConformanceRegistry::Conformance {
		BSConformanceRegistry::Conformance entry;
		entry.target_keys.push_back(p_target_key);
		entry.target_fqcn = p_target_key;
		entry.target_script_path = p_target_key.begins_with("res://") ? p_target_key : String();
		entry.target_is_root_class = true;
		entry.trait_name = p_trait;
		entry.trait_type_arguments = p_args;
		entry.target_label = p_target_key;
		entry.source_file = p_source;
		entry.conformance_index = p_index;
		return entry;
	};

	// Visible script-key recorded args.
	{
		Vector<BSConformanceRegistry::Conformance> candidates;
		candidates.push_back(make_conformance("res://tests/rta_target.barista", SNAME("RtaKeeper"),
				builtin_args(Variant::INT), script_file, 0));
		const BSConformanceRegistry::RegistrationResult publish =
				registry->try_replace_file_conformances(script_file, candidates);
		result["script_publish_ok"] = publish.registered_count == 1 && publish.conflicts.is_empty();

		Vector<BSConformanceRegistry::RecordedTypeArgument> recorded;
		const bool found = registry->get_recorded_trait_arguments(
				"res://tests/rta_target.barista", SNAME("RtaKeeper"), recorded);
		result["script_query_ok"] = found && recorded.size() == 1 &&
				recorded[0].kind == BSConformanceRegistry::RecordedTypeArgument::BUILTIN &&
				recorded[0].builtin_type == Variant::INT;
	}

	// Hidden Visibility filters recorded args the same way as membership.
	{
		class HideEverythingVisibility : public BSConformanceRegistry::Visibility {
		public:
			bool can_see(const String &p_source_file) const override {
				(void)p_source_file;
				return false;
			}
		};
		HideEverythingVisibility hidden;
		const BSConformanceRegistry::ScopedVisibility scoped(&hidden);
		Vector<BSConformanceRegistry::RecordedTypeArgument> recorded;
		result["hidden_script_query_false"] = !registry->get_recorded_trait_arguments(
				"res://tests/rta_target.barista", SNAME("RtaKeeper"), recorded);
		result["hidden_native_query_false"] = !registry->get_native_recorded_trait_arguments(
				SNAME("Node"), SNAME("RtaKeeper"), recorded);
		result["hidden_builtin_query_false"] = !registry->get_builtin_recorded_trait_arguments(
				Variant::INT, SNAME("RtaKeeper"), recorded);
	}

	// Builtin exact-key lookup.
	{
		Vector<BSConformanceRegistry::Conformance> candidates;
		BSConformanceRegistry::Conformance entry = make_conformance(
				String(Variant::get_type_name(Variant::INT)), SNAME("RtaKeeper"),
				builtin_args(Variant::STRING), builtin_file, 0);
		entry.target_script_path = String();
		candidates.push_back(entry);
		const BSConformanceRegistry::RegistrationResult publish =
				registry->try_replace_file_conformances(builtin_file, candidates);
		result["builtin_publish_ok"] = publish.registered_count == 1;

		Vector<BSConformanceRegistry::RecordedTypeArgument> recorded;
		const bool found = registry->get_builtin_recorded_trait_arguments(
				Variant::INT, SNAME("RtaKeeper"), recorded);
		result["builtin_query_ok"] = found && recorded.size() == 1 &&
				recorded[0].kind == BSConformanceRegistry::RecordedTypeArgument::BUILTIN &&
				recorded[0].builtin_type == Variant::STRING;
	}

	// Native ClassDB parent walk: Object record reachable from Node.
	{
		Vector<BSConformanceRegistry::Conformance> candidates;
		BSConformanceRegistry::Conformance entry = make_conformance(
				"Object", SNAME("RtaKeeper"), builtin_args(Variant::FLOAT), native_file, 0);
		entry.target_script_path = String();
		entry.target_native_base = SNAME("Object");
		candidates.push_back(entry);
		const BSConformanceRegistry::RegistrationResult publish =
				registry->try_replace_file_conformances(native_file, candidates);
		result["native_publish_ok"] = publish.registered_count == 1;

		Vector<BSConformanceRegistry::RecordedTypeArgument> recorded;
		const bool found = registry->get_native_recorded_trait_arguments(
				SNAME("Node"), SNAME("RtaKeeper"), recorded);
		result["native_parent_walk_ok"] = found && recorded.size() == 1 &&
				recorded[0].kind == BSConformanceRegistry::RecordedTypeArgument::BUILTIN &&
				recorded[0].builtin_type == Variant::FLOAT;
	}

	// Nearer empty conformance shadows a farther recorded one (CLASS chain).
	{
		Vector<BSConformanceRegistry::Conformance> farther_candidates;
		farther_candidates.push_back(make_conformance("res://tests/rta_farther.barista", SNAME("RtaKeeper"),
				builtin_args(Variant::INT), farther_file, 0));
		registry->try_replace_file_conformances(farther_file, farther_candidates);

		Vector<BSConformanceRegistry::Conformance> nearer_candidates;
		BSConformanceRegistry::Conformance empty_near = make_conformance(
				"res://tests/rta_nearer.barista", SNAME("RtaKeeper"),
				Vector<BSConformanceRegistry::RecordedTypeArgument>(), nearer_file, 0);
		nearer_candidates.push_back(empty_near);
		registry->try_replace_file_conformances(nearer_file, nearer_candidates);

		BSParser::ClassNode farther_class;
		farther_class.fqcn = "res://tests/rta_farther.barista";
		BSParser::ClassNode nearer_class;
		nearer_class.fqcn = "res://tests/rta_nearer.barista";
		nearer_class.base_type.kind = BSParser::DataType::CLASS;
		nearer_class.base_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		nearer_class.base_type.class_type = &farther_class;

		BSParser::DataType source;
		source.kind = BSParser::DataType::CLASS;
		source.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		source.class_type = &nearer_class;

		Vector<BSConformanceRegistry::RecordedTypeArgument> recorded;
		// Direct farther query still returns args.
		result["farther_direct_ok"] = registry->get_recorded_trait_arguments(
											  "res://tests/rta_farther.barista", SNAME("RtaKeeper"), recorded) &&
				recorded.size() == 1;
		// Chain from nearer: empty nearer shadows farther.
		result["nearer_empty_shadows"] = !BSTypeCompatibility::project_registry_trait_arguments(
				source, SNAME("RtaKeeper"), recorded);

		// Without the empty nearer, walking from farther returns the record.
		source.class_type = &farther_class;
		result["farther_project_ok"] = BSTypeCompatibility::project_registry_trait_arguments(
											   source, SNAME("RtaKeeper"), recorded) &&
				recorded.size() == 1 &&
				recorded[0].builtin_type == Variant::INT;

		// CLASS chain bottoms out at NATIVE: recorded Object args reachable via Node base.
		farther_class.base_type.kind = BSParser::DataType::NATIVE;
		farther_class.base_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		farther_class.base_type.native_type = SNAME("Node");
		registry->clear_file(farther_file);
		registry->clear_file(nearer_file);
		source.class_type = &farther_class;
		result["class_to_native_project_ok"] = BSTypeCompatibility::project_registry_trait_arguments(
													   source, SNAME("RtaKeeper"), recorded) &&
				recorded.size() == 1 &&
				recorded[0].builtin_type == Variant::FLOAT;
	}

	registry->clear_file(script_file);
	registry->clear_file(farther_file);
	registry->clear_file(nearer_file);
	registry->clear_file(native_file);
	registry->clear_file(builtin_file);
	registry->clear_file(empty_near_file);

	return result;
}

godot::Dictionary BaristaScriptAnalyzerProbe::trait_target_assignability() const {
	godot::Dictionary result;

	BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
	ERR_FAIL_COND_V(registry == nullptr, result);

	const String conflict_file = "res://tests/tta_conflict.barista";
	const String match_file = "res://tests/tta_match.barista";
	const String empty_file = "res://tests/tta_empty.barista";
	const String native_file = "res://tests/tta_native.barista";
	const String builtin_file = "res://tests/tta_builtin.barista";

	registry->clear_file(conflict_file);
	registry->clear_file(match_file);
	registry->clear_file(empty_file);
	registry->clear_file(native_file);
	registry->clear_file(builtin_file);

	BSParser::IdentifierNode trait_id;
	trait_id.name = SNAME("TtaKeeper");
	BSParser::IdentifierNode param_id;
	param_id.name = SNAME("T");
	BSParser::TypeParameterNode type_parameter;
	type_parameter.identifier = &param_id;

	BSParser::ClassNode trait;
	trait.is_trait = true;
	trait.identifier = &trait_id;
	trait.fqcn = "res://tests/tta_keeper.barista";
	trait.type_parameters.push_back(&type_parameter);

	auto make_builtin = [](Variant::Type p_type) -> BSParser::DataType {
		BSParser::DataType type;
		type.kind = BSParser::DataType::BUILTIN;
		type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		type.builtin_type = p_type;
		return type;
	};

	BSParser::DataType target;
	target.kind = BSParser::DataType::CLASS;
	target.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	target.class_type = &trait;
	target.type_arguments.push_back(make_builtin(Variant::STRING));

	auto builtin_recorded = [](Variant::Type p_type) -> Vector<BSConformanceRegistry::RecordedTypeArgument> {
		BSConformanceRegistry::RecordedTypeArgument argument;
		argument.kind = BSConformanceRegistry::RecordedTypeArgument::BUILTIN;
		argument.builtin_type = p_type;
		Vector<BSConformanceRegistry::RecordedTypeArgument> arguments;
		arguments.push_back(argument);
		return arguments;
	};

	auto make_conformance = [](const String &p_target_key, const StringName &p_trait,
									const Vector<BSConformanceRegistry::RecordedTypeArgument> &p_args,
									const String &p_source) -> BSConformanceRegistry::Conformance {
		BSConformanceRegistry::Conformance entry;
		entry.target_keys.push_back(p_target_key);
		entry.target_fqcn = p_target_key;
		entry.target_script_path = p_target_key.begins_with("res://") ? p_target_key : String();
		entry.target_is_root_class = true;
		entry.trait_name = p_trait;
		entry.trait_type_arguments = p_args;
		entry.target_label = p_target_key;
		entry.source_file = p_source;
		entry.conformance_index = 0;
		return entry;
	};

	// CLASS source: registry-recorded INT conflicts with Keeper[String] destination.
	{
		Vector<BSConformanceRegistry::Conformance> candidates;
		candidates.push_back(make_conformance("res://tests/tta_source.barista", SNAME("TtaKeeper"),
				builtin_recorded(Variant::INT), conflict_file));
		registry->try_replace_file_conformances(conflict_file, candidates);

		BSParser::ClassNode source_class;
		source_class.fqcn = "res://tests/tta_source.barista";
		BSParser::DataType source;
		source.kind = BSParser::DataType::CLASS;
		source.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		source.class_type = &source_class;

		const BSTypeCompatibility::Result check_result = BSTypeCompatibility::check(target, source);
		result["class_registry_conflict_rejects"] = !check_result.compatible;
		result["class_registry_membership"] = registry->has_conformance(
				"res://tests/tta_source.barista", SNAME("TtaKeeper"));
	}

	// CLASS source: matching recorded STRING stays compatible.
	{
		Vector<BSConformanceRegistry::Conformance> candidates;
		candidates.push_back(make_conformance("res://tests/tta_match_source.barista", SNAME("TtaKeeper"),
				builtin_recorded(Variant::STRING), match_file));
		registry->try_replace_file_conformances(match_file, candidates);

		BSParser::ClassNode source_class;
		source_class.fqcn = "res://tests/tta_match_source.barista";
		BSParser::DataType source;
		source.kind = BSParser::DataType::CLASS;
		source.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		source.class_type = &source_class;

		result["class_registry_match_accepts"] = BSTypeCompatibility::check(target, source).compatible;
	}

	// CLASS source: empty recorded args = gradual no-evidence, stays compatible.
	{
		Vector<BSConformanceRegistry::Conformance> candidates;
		candidates.push_back(make_conformance("res://tests/tta_empty_source.barista", SNAME("TtaKeeper"),
				Vector<BSConformanceRegistry::RecordedTypeArgument>(), empty_file));
		registry->try_replace_file_conformances(empty_file, candidates);

		BSParser::ClassNode source_class;
		source_class.fqcn = "res://tests/tta_empty_source.barista";
		BSParser::DataType source;
		source.kind = BSParser::DataType::CLASS;
		source.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		source.class_type = &source_class;

		result["class_registry_no_evidence_accepts"] = BSTypeCompatibility::check(target, source).compatible;
	}

	// NATIVE source: Object conformance with conflicting args reachable from Node.
	{
		BSConformanceRegistry::Conformance entry = make_conformance(
				"Object", SNAME("TtaKeeper"), builtin_recorded(Variant::INT), native_file);
		entry.target_script_path = String();
		entry.target_native_base = SNAME("Object");
		Vector<BSConformanceRegistry::Conformance> candidates;
		candidates.push_back(entry);
		registry->try_replace_file_conformances(native_file, candidates);

		BSParser::DataType source;
		source.kind = BSParser::DataType::NATIVE;
		source.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		source.native_type = SNAME("Node");

		result["native_conflict_rejects"] = !BSTypeCompatibility::check(target, source).compatible;
		result["native_membership"] = registry->native_class_conforms(SNAME("Node"), SNAME("TtaKeeper"));
	}

	// BUILTIN source: INT conforms with conflicting recorded args.
	{
		BSConformanceRegistry::Conformance entry = make_conformance(
				String(Variant::get_type_name(Variant::INT)), SNAME("TtaKeeper"),
				builtin_recorded(Variant::FLOAT), builtin_file);
		entry.target_script_path = String();
		Vector<BSConformanceRegistry::Conformance> candidates;
		candidates.push_back(entry);
		registry->try_replace_file_conformances(builtin_file, candidates);

		BSParser::DataType source;
		source.kind = BSParser::DataType::BUILTIN;
		source.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		source.builtin_type = Variant::INT;

		result["builtin_conflict_rejects"] = !BSTypeCompatibility::check(target, source).compatible;
		result["builtin_membership"] = registry->builtin_type_conforms(Variant::INT, SNAME("TtaKeeper"));
	}

	// Declared `uses` projection: class binds Keeper[int] against Keeper[String] destination.
	{
		BSParser::IdentifierNode implementer_id;
		implementer_id.name = SNAME("TtaUsesInt");
		BSParser::ClassNode implementer;
		implementer.identifier = &implementer_id;
		implementer.fqcn = "res://tests/tta_uses_int.barista";
		BSParser::ClassNode::TraitUse trait_use;
		trait_use.resolved_trait = &trait;
		trait_use.resolved_type_arguments.push_back(make_builtin(Variant::INT));
		implementer.used_traits.push_back(trait_use);
		implementer.resolved_traits.push_back(&trait);

		BSParser::DataType source;
		source.kind = BSParser::DataType::CLASS;
		source.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		source.class_type = &implementer;

		Vector<BSParser::DataType> projected;
		result["uses_project_ok"] = BSTypeCompatibility::project_class_trait_arguments(source, &trait, projected) &&
				projected.size() == 1 &&
				projected[0].kind == BSParser::DataType::BUILTIN &&
				projected[0].builtin_type == Variant::INT;
		result["uses_projection_conflict_rejects"] = !BSTypeCompatibility::check(target, source).compatible;
	}

	// Trait typed as itself with matching args (source is the trait specialization).
	{
		BSParser::DataType source = target;
		result["trait_self_match_accepts"] = BSTypeCompatibility::check(target, source).compatible;

		source.type_arguments.write[0] = make_builtin(Variant::INT);
		result["trait_self_conflict_rejects"] = !BSTypeCompatibility::check(target, source).compatible;
	}

	registry->clear_file(conflict_file);
	registry->clear_file(match_file);
	registry->clear_file(empty_file);
	registry->clear_file(native_file);
	registry->clear_file(builtin_file);

	return result;
}

godot::Dictionary BaristaScriptAnalyzerProbe::witness_collision_arbitration() const {
	godot::Dictionary result;

	BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
	ERR_FAIL_COND_V(registry == nullptr, result);

	const String same_file_path = "res://tests/wc_same_file.barista";
	const String first_file = "res://tests/wc_cross_first.barista";
	const String second_file = "res://tests/wc_cross_second.barista";
	const String ok_file = "res://tests/wc_distinct_ok.barista";
	const String registry_first = "res://tests/wc_reg_first.barista";
	const String registry_second = "res://tests/wc_reg_second.barista";

	registry->clear_file(same_file_path);
	registry->clear_file(first_file);
	registry->clear_file(second_file);
	registry->clear_file(ok_file);
	registry->clear_file(registry_first);
	registry->clear_file(registry_second);

	auto error_contains = [](const godot::PackedStringArray &p_errors, const String &p_needle) -> bool {
		for (int i = 0; i < p_errors.size(); i++) {
			if (String(p_errors[i]).contains(p_needle)) {
				return true;
			}
		}
		return false;
	};

	auto analyze = [](const String &p_path, const String &p_source, godot::PackedStringArray &r_errors) -> Error {
		r_errors.clear();
		BSCache::set_source_override(p_path, p_source);
		BSParser parser;
		BSAnalyzer analyzer(&parser);
		Error err = parser.parse(p_source, p_path, false);
		if (err == OK) {
			err = analyzer.analyze();
		}
		for (const BSParser::ParserError &pe : parser.get_errors()) {
			r_errors.push_back(pe.message);
		}
		return err;
	};

	// Same-file: two traits, same witness method name on one target.
	{
		const String source =
				"class_name WcSameTarget extends Node\n"
				"\n"
				"trait WcTraitA:\n"
				"\tabstract func wc_label() -> String\n"
				"\n"
				"trait WcTraitB:\n"
				"\tabstract func wc_label() -> String\n"
				"\n"
				"extend WcSameTarget uses WcTraitA:\n"
				"\tfunc wc_label() -> String:\n"
				"\t\treturn \"a\"\n"
				"\n"
				"extend WcSameTarget uses WcTraitB:\n"
				"\tfunc wc_label() -> String:\n"
				"\t\treturn \"b\"\n";
		godot::PackedStringArray errors;
		analyze(same_file_path, source, errors);
		result["same_file_collision_diagnostic"] =
				error_contains(errors, "already provides a witness for method \"wc_label()\"");
		bool second_registered = false;
		const Vector<BSConformanceRegistry::Conformance> entries =
				registry->get_file_conformances(same_file_path);
		for (const BSConformanceRegistry::Conformance &entry : entries) {
			if (entry.conformance_index == 1) {
				second_registered = true;
			}
		}
		result["same_file_first_registered"] = !entries.is_empty();
		result["same_file_second_rejected"] = !second_registered;
		BSCache::clear_source_override(same_file_path);
		registry->clear_file(same_file_path);
	}

	// Cross-file: second file's witness collides with first's published witness on native Node.
	{
		const String first_source =
				"trait WcCrossTraitA:\n"
				"\tabstract func wc_mark() -> String\n"
				"\n"
				"extend Node uses WcCrossTraitA:\n"
				"\tfunc wc_mark() -> String:\n"
				"\t\treturn \"a\"\n";
		godot::PackedStringArray first_errors;
		analyze(first_file, first_source, first_errors);
		result["cross_first_ok"] = first_errors.is_empty() &&
				!registry->get_file_conformances(first_file).is_empty();

		StringName other_trait;
		const String witness_source =
				registry->get_witness_source("Node", SNAME("wc_mark"), other_trait);
		result["get_witness_source_first"] = witness_source == first_file;

		const String second_source =
				"trait WcCrossTraitB:\n"
				"\tabstract func wc_mark() -> String\n"
				"\n"
				"extend Node uses WcCrossTraitB:\n"
				"\tfunc wc_mark() -> String:\n"
				"\t\treturn \"b\"\n";
		godot::PackedStringArray second_errors;
		analyze(second_file, second_source, second_errors);
		result["cross_file_collision_diagnostic"] =
				error_contains(second_errors, "already has a witness for method \"wc_mark()\"");
		result["cross_second_store_empty"] = registry->get_file_conformances(second_file).is_empty();

		BSCache::clear_source_override(first_file);
		BSCache::clear_source_override(second_file);
		registry->clear_file(first_file);
		registry->clear_file(second_file);
	}

	// Non-colliding: distinct witness method names on the same target.
	{
		const String source =
				"class_name WcOkTarget extends Node\n"
				"\n"
				"trait WcOkTraitA:\n"
				"\tabstract func wc_alpha() -> String\n"
				"\n"
				"trait WcOkTraitB:\n"
				"\tabstract func wc_beta() -> String\n"
				"\n"
				"extend WcOkTarget uses WcOkTraitA:\n"
				"\tfunc wc_alpha() -> String:\n"
				"\t\treturn \"a\"\n"
				"\n"
				"extend WcOkTarget uses WcOkTraitB:\n"
				"\tfunc wc_beta() -> String:\n"
				"\t\treturn \"b\"\n";
		godot::PackedStringArray errors;
		analyze(ok_file, source, errors);
		const Vector<BSConformanceRegistry::Conformance> entries = registry->get_file_conformances(ok_file);
		result["distinct_ok_analyze"] = errors.is_empty();
		result["distinct_ok_registered"] = entries.size() >= 2;
		BSCache::clear_source_override(ok_file);
		registry->clear_file(ok_file);
	}

	// Registry-authoritative WITNESS_COLLISION via try_replace (Foundry concurrent witness test).
	{
		BSConformanceRegistry::Conformance first;
		first.target_keys.push_back("WcRegTarget");
		first.target_fqcn = "WcRegTarget";
		first.target_label = "WcRegTarget";
		first.trait_name = SNAME("WcRegTraitA");
		first.source_file = registry_first;
		first.conformance_index = 0;
		first.witnesses.insert(SNAME("wc_reg_label"), true);

		BSConformanceRegistry::Conformance second;
		second.target_keys.push_back("WcRegTarget");
		second.target_fqcn = "WcRegTarget";
		second.target_label = "WcRegTarget";
		second.trait_name = SNAME("WcRegTraitB");
		second.source_file = registry_second;
		second.conformance_index = 0;
		second.witnesses.insert(SNAME("wc_reg_label"), true);

		Vector<BSConformanceRegistry::Conformance> first_candidates;
		first_candidates.push_back(first);
		const BSConformanceRegistry::RegistrationResult first_result =
				registry->try_replace_file_conformances(registry_first, first_candidates);
		result["registry_first_ok"] = first_result.conflicts.is_empty() && first_result.registered_count == 1;

		Vector<BSConformanceRegistry::Conformance> second_candidates;
		second_candidates.push_back(second);
		const BSConformanceRegistry::RegistrationResult second_result =
				registry->try_replace_file_conformances(registry_second, second_candidates);
		bool saw_witness = false;
		for (int i = 0; i < second_result.conflicts.size(); i++) {
			if (second_result.conflicts[i].kind == BSConformanceRegistry::RegistrationConflict::WITNESS_COLLISION &&
					second_result.conflicts[i].method_name == SNAME("wc_reg_label")) {
				saw_witness = true;
			}
		}
		result["registry_witness_collision"] = saw_witness;
		result["registry_second_rejected"] = second_result.registered_count == 0 &&
				registry->get_file_conformances(registry_second).is_empty();

		registry->clear_file(registry_first);
		registry->clear_file(registry_second);
	}

	return result;
}

} // namespace barista_script

#endif // DEBUG_ENABLED
