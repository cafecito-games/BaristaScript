/**************************************************************************/
/*  bs_analyzer.h                                                         */
/*                                                                        */
/*  M3 analyzer port seam (issue #43 / #57). Staged resolve_* mirror      */
/*  Foundry FSAnalyzer @ c9d5e35. Inheritance, interface, body fold       */
/*  (#49), declaration commit (#52/#58), call/match/flow/warning depth    */
/*  (#57 remainder). Full mechanical Foundry TU dump remains follow-up.   */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_parser.h"
#include "bs_type.h"

namespace barista_script {

class BSAnalyzer {
public:
	enum class AnalyzerPhase : int8_t {
		NONE = -1,
		PREFLIGHT = 0,
		DEPENDENCY_PARSE_AVAILABILITY = 1,
		INHERITANCE_RESOLUTION = 2,
		INTERFACE_AND_MEMBER_SURFACE = 3,
		TRAIT_CONFORMANCE_REGISTRATION = 4,
		BODY_EXPRESSION_CALLABLE_SIGNAL = 5,
		FLOW_FINALITY_INVARIANTS = 6,
		CONFORMANCE_WITNESS_BODY = 7,
		FINAL_DIAGNOSTICS_AND_DEPENDENCIES = 8,
	};

	explicit BSAnalyzer(BSParser *p_parser);
	~BSAnalyzer() = default;

	Error resolve_inheritance();
	Error resolve_interface();
	Error resolve_body();
	Error analyze();

	/**
	 * When true, successful/failed analysis updates the private declaration index. Default false
	 * so `_validate()` / `_is_valid()` / probes remain read-only. Only intentional refresh paths
	 * (e.g. `synchronize_declaration_path_from_source`) should enable this (#52 / PR #59 review).
	 */
	void set_update_declaration_index(bool p_enabled) { update_declaration_index = p_enabled; }
	bool get_update_declaration_index() const { return update_declaration_index; }

	static bool is_bootstrap_path_allowed(const String &p_path);
	static void set_bootstrap_allowed_dependency_root(const String &p_root);
	static String get_bootstrap_allowed_dependency_root();

	BSParser *get_parser() const { return parser; }
	AnalyzerPhase get_highest_completed_phase() const { return highest_completed_phase; }

	/** Constant-fold a top-level expression after parse; used by the #49 probe. */
	void reduce_expression(BSParser::ExpressionNode *p_expression, bool p_is_root = false);

	/**
	 * After successful FULLY_SOLVED analysis, commit a declaration record; on failure remove any
	 * prior record for the path (#52). No-op unless `set_update_declaration_index(true)`, and a
	 * safe no-op when the language singleton is absent.
	 *
	 * M5-only deferred diagnostics still allow a declaration commit so GENERIC_CLASS heads remain
	 * discoverable through the private index (#58); `_validate` / `_is_valid` stay invalid.
	 */
	void commit_or_remove_declaration(bool p_success);

	static BSParser::DataType type_from_variant(const Variant &p_value);

private:
	BSParser *parser = nullptr;
	AnalyzerPhase highest_completed_phase = AnalyzerPhase::NONE;
	bool strict_dynamic_checks = false;
	bool strict_null_checks = false;
	bool update_declaration_index = false;
	BSParser::ClassNode *current_class = nullptr;
	BSParser::FunctionNode *current_function = nullptr;

	Error run_phase_preflight();
	Error run_phase_inheritance_resolution();
	Error run_phase_interface_and_member_surface();
	Error run_phase_body_expression_callable_signal();
	Error run_phase_flow_finality();
	Error run_phase_finalize();

	void resolve_class_inheritance(BSParser::ClassNode *p_class);
	void resolve_datatype(BSParser::DataType &r_type, BSParser::Node *p_source);
	BSParser::DataType datatype_from_type_node(BSParser::TypeNode *p_type_node);
	void analyze_class_interface(BSParser::ClassNode *p_class);
	void analyze_class_body(BSParser::ClassNode *p_class);
	void analyze_function_body(BSParser::FunctionNode *p_function);
	void analyze_suite(BSParser::SuiteNode *p_suite);
	void analyze_statement(BSParser::Node *p_node);

	void reduce_literal(BSParser::LiteralNode *p_literal);
	void reduce_unary_op(BSParser::UnaryOpNode *p_unary_op);
	void reduce_binary_op(BSParser::BinaryOpNode *p_binary_op);
	void reduce_identifier(BSParser::IdentifierNode *p_identifier);
	void reduce_call(BSParser::CallNode *p_call);
	void reduce_subscript(BSParser::SubscriptNode *p_subscript);
	void reduce_array(BSParser::ArrayNode *p_array);
	void reduce_dictionary(BSParser::DictionaryNode *p_dictionary);
	void reduce_ternary(BSParser::TernaryOpNode *p_ternary);

	void validate_bootstrap_namespace_imports();
	bool validate_bootstrap_namespace_import(const String &p_import);
	void validate_local_call(BSParser::CallNode *p_call, BSParser::FunctionNode *p_callee);
	void check_match_exhaustiveness(BSParser::MatchNode *p_match);
	bool suite_has_return(const BSParser::SuiteNode *p_suite) const;
	void check_function_flow_finality(BSParser::FunctionNode *p_function);
	void resolve_used_traits(BSParser::ClassNode *p_class);
	BSParser::FunctionNode *find_class_function(BSParser::ClassNode *p_class, const StringName &p_name) const;
	BSParser::DataType resolve_named_type(const String &p_qualified, BSParser::Node *p_source);
	bool errors_are_only_m5_deferred() const;

	void push_error(const String &p_message, const BSParser::Node *p_origin = nullptr);
#ifdef DEBUG_ENABLED
	void push_warning(const BSParser::Node *p_origin, BSWarning::Code p_code, const Vector<String> &p_symbols = Vector<String>());
#endif
	void mark_phase(AnalyzerPhase p_phase);
	void read_strict_settings();

	static String &bootstrap_root_storage();
};

/** Shared helper: parse + analyze; true when no errors remain. */
bool bs_source_analyzes(const String &p_source, const String &p_path);

} // namespace barista_script
