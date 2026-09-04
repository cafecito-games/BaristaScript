/**************************************************************************/
/*  bs_analyzer.h                                                         */
/*                                                                        */
/*  M3 analyzer port seam (issue #43 / #57 / #60). Staged resolve_*        */
/*  mirror Foundry FSAnalyzer @ c9d5e35. Inheritance, interface, body     */
/*  fold (#49), declaration commit (#52/#58), call/match/flow (#61),      */
/*  local + member/static final definite assignment (#60 flow TU).        */
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

	/**
	 * Hard fork of Foundry `FSAnalyzer::FlowFinalityContext` (@ c9d5e35,
	 * `fs_analyzer_flow_finality.cpp`). Ports LOCAL + INSTANCE + STATIC
	 * `final var` definite assignment / illegal writes. Trait flattening and
	 * flow narrowing remain follow-up under #60.
	 */
	class FlowFinalityContext {
	public:
		enum class FinalAssignmentScope {
			LOCAL,
			INSTANCE_MEMBER,
			STATIC_MEMBER,
		};

		struct FinalAssignmentState {
			HashSet<const BSParser::VariableNode *> assigned;
			HashSet<const BSParser::VariableNode *> maybe_assigned;
			bool reachable = true;
		};

		explicit FlowFinalityContext(BSAnalyzer *p_analyzer);

		void check_final_member_assignments(BSParser::ClassNode *p_class);
		void check_final_static_assignments(BSParser::ClassNode *p_class);
		void check_final_local_assignments(BSParser::ClassNode *p_class);
		void analyze_function_local_finals(const BSParser::FunctionNode *p_function);
		void collect_local_finals(const BSParser::Node *p_node,
				HashSet<const BSParser::VariableNode *> &r_finals,
				HashMap<StringName, const BSParser::VariableNode *> &r_finals_by_name);
		static void merge_final_assignment_branches(const FinalAssignmentState &p_first, const FinalAssignmentState &p_second, FinalAssignmentState &r_out);
		const BSParser::VariableNode *final_member_assignment_target(const BSParser::ExpressionNode *p_expression,
				const HashSet<const BSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, bool *r_is_self_receiver = nullptr) const;
		void scan_illegal_final_writes(const BSParser::Node *p_node,
				const HashSet<const BSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, bool p_in_init);
		void analyze_final_definite_assignment_statement(const BSParser::Node *p_statement,
				const HashSet<const BSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, FinalAssignmentState &r_state,
				HashSet<const BSParser::VariableNode *> &r_assigned_anywhere);
		void analyze_final_definite_assignment_suite(const BSParser::SuiteNode *p_suite,
				const HashSet<const BSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, FinalAssignmentState &r_state,
				HashSet<const BSParser::VariableNode *> &r_assigned_anywhere);
		void check_final_reads_in_expression(const BSParser::ExpressionNode *p_expression,
				const HashSet<const BSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, const FinalAssignmentState &p_state);

	private:
		BSAnalyzer *analyzer = nullptr;
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
	FlowFinalityContext flow_finality;

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
	void warn_unused_locals(BSParser::SuiteNode *p_suite);
	void warn_unused_parameters(BSParser::FunctionNode *p_function);

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
	/** True when a RETURN statement appears on some path (excludes `@noreturn` calls). */
	bool suite_has_explicit_return(const BSParser::SuiteNode *p_suite) const;
	bool node_terminates(const BSParser::Node *p_node) const;
	bool node_has_explicit_return(const BSParser::Node *p_node) const;
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
