/**************************************************************************/
/*  bs_analyzer.h                                                         */
/*                                                                        */
/*  M3 analyzer port seam (issue #43 / #57 / #60). Staged resolve_*        */
/*  mirror Foundry FSAnalyzer @ c9d5e35. Inheritance, interface, body     */
/*  fold (#49), declaration commit (#52/#58), call/match/flow (#61),      */
/*  local + member/static final definite assignment (#60 flow TU),        */
/*  if/while/assert null-check + `is` type-test flow narrowing starter,   */
/*  CallSiteValidationContext MethodInfo / signal emit (#60 call TU),     */
/*  unused private/signal + built-in annotation resolve (#60 surface),    */
/*  trait requirement / conformance witness + non-generic signature match */
/*  (#60 conformance TU).                                                 */
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
	 * Hard fork of Foundry `FSAnalyzer::CallSiteValidationContext` (@ c9d5e35,
	 * `fs_analyzer_call_validation.cpp`). Ports MethodInfo / typed-parameter
	 * call arity+type checks and signal emit / emit_signal payload validation.
	 * Generic inference, named-arg canonicalization, and connect/callable
	 * richness remain follow-up under #60.
	 */
	class CallSiteValidationContext {
	public:
		explicit CallSiteValidationContext(BSAnalyzer *p_analyzer);

		void validate_call_arg(const MethodInfo &p_method, const BSParser::CallNode *p_call);
		void validate_call_arg(const List<BSParser::DataType> &p_par_types, int p_default_args_count, bool p_is_vararg, const BSParser::CallNode *p_call, const Vector<int> &p_extra_allowed_argument_counts = Vector<int>(), int p_trailing_unbound_argument_count = 0, const BSParser::DataType *p_rest_parameter_type = nullptr, int p_extra_allowed_argument_offset = 0);
		void validate_argument_against_type(const BSParser::DataType &p_expected_type, BSParser::ExpressionNode *p_argument, int p_argument_number, const StringName &p_function, const BSParser::CallNode *p_call);
		static const BSParser::DataType *rest_element_type(const BSParser::DataType *p_rest_parameter_type);
		String make_invalid_argument_error(
				const StringName &p_function,
				int p_argument_number,
				const BSParser::DataType &p_expected_type,
				const BSParser::DataType &p_actual_type,
				bool p_strict_dynamic_mismatch,
				bool p_strict_nullable_mismatch,
				const BSParser::Node *p_actual_node = nullptr) const;

		BSParser::DataType explicit_signal_type_from_info(const MethodInfo &p_info) const;
		BSParser::DataType explicit_signal_type_from_node(const BSParser::SignalNode *p_signal, const BSParser::DataType &p_receiver_type, const BSParser::ClassNode *p_declaring_class) const;
		bool signal_name_from_constant_arg(const BSParser::CallNode *p_call, int p_signal_arg_index, StringName &r_signal_name) const;
		bool signal_type_from_class_constant_arg(const BSParser::DataType &p_receiver_type, const BSParser::CallNode *p_call, int p_signal_arg_index, BSParser::DataType &r_signal_type) const;
		bool signal_type_from_native_constant_arg(const StringName &p_native_type, const BSParser::CallNode *p_call, int p_signal_arg_index, BSParser::DataType &r_signal_type) const;
		bool local_signal_type_from_constant_arg(const BSParser::CallNode *p_call, int p_signal_arg_index, BSParser::DataType &r_signal_type) const;
		void validate_strict_signal_name_fallback(const BSParser::CallNode *p_call, const BSParser::DataType &p_receiver_type, int p_signal_arg_index);
		bool call_argument_can_be_string_name(const BSParser::CallNode *p_call, int p_argument_index);

		void validate_signal_emit_args(const BSParser::DataType &p_signal_type, const BSParser::CallNode *p_call, int p_first_emit_arg_index);
		void validate_local_object_emit_signal_args(const BSParser::CallNode *p_call, bool p_is_self);

	private:
		BSAnalyzer *analyzer = nullptr;
	};

	/**
	 * Hard fork of Foundry `FSAnalyzer::FlowFinalityContext` (@ c9d5e35,
	 * `fs_analyzer_flow_finality.cpp`). LOCAL + INSTANCE + STATIC `final var`
	 * definite assignment / illegal writes, trait-member flattening into
	 * implementers, plus if/while/assert null-check and `is` type-test flow
	 * narrowing for locals/parameters. Match-branch narrowing, lambda capture
	 * clearing, and compound-assignment narrowed reads remain follow-up under #60.
	 */
	class FlowFinalityContext {
		BSAnalyzer *analyzer = nullptr;
		HashMap<const BSParser::Node *, BSParser::DataType> flow_narrowed_types;
		HashMap<const BSParser::Node *, bool> flow_narrowing_captured_sources;
		HashSet<const BSParser::VariableNode *> flattened_trait_final_nodes;

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

		/** Saves/clears flow narrowing for one function-body analysis pass. */
		class FlowNarrowingScope {
			FlowFinalityContext *context = nullptr;
			HashMap<const BSParser::Node *, BSParser::DataType> previous_flow_narrowed_types;
			HashMap<const BSParser::Node *, bool> previous_flow_narrowing_captured_sources;
			bool restore_captured_sources = false;

		public:
			FlowNarrowingScope(FlowFinalityContext &p_context, bool p_track_captured_sources);
			~FlowNarrowingScope();
		};

		/** Clears `flattened_trait_final_nodes` for one member/static final pass. */
		class FlattenedTraitFinalNodesScope {
			FlowFinalityContext *context = nullptr;

		public:
			explicit FlattenedTraitFinalNodesScope(FlowFinalityContext &p_context);
			void insert(const BSParser::VariableNode *p_variable);
			~FlattenedTraitFinalNodesScope();
		};

		explicit FlowFinalityContext(BSAnalyzer *p_analyzer);

		const BSParser::Node *flow_narrowing_key_from_identifier(const BSParser::IdentifierNode *p_identifier) const;
		void apply_flow_narrowing(const BSParser::IdentifierNode *p_identifier);
		void apply_flow_narrowing(const BSParser::IdentifierNode *p_identifier, const BSParser::DataType &p_type);
		void clear_flow_narrowing(const BSParser::ExpressionNode *p_expression);
		void mark_flow_narrowing_capture(const BSParser::IdentifierNode *p_identifier);
		void clear_captured_flow_narrowing();
		bool null_check_narrowing_identifier(BSParser::ExpressionNode *p_condition, bool p_condition_value, BSParser::IdentifierNode *&r_identifier) const;
		bool type_test_narrowing_identifier(BSParser::ExpressionNode *p_condition, bool p_condition_value, BSParser::IdentifierNode *&r_identifier, BSParser::DataType &r_type) const;
		bool type_test_condition(BSParser::ExpressionNode *p_condition, BSParser::TypeTestNode *&r_type_test, BSParser::IdentifierNode *&r_identifier, bool &r_true_means_match) const;
		void apply_failed_type_test_flow_narrowing(const BSParser::IdentifierNode *p_identifier, const BSParser::DataType &p_tested_type);
		void reduce_condition_expression(BSParser::ExpressionNode *p_condition);
		void apply_flow_narrowing_from_condition(BSParser::ExpressionNode *p_condition, bool p_condition_value);
		const BSParser::DataType *lookup_flow_narrowed_type(const BSParser::Node *p_key) const;
		HashMap<const BSParser::Node *, BSParser::DataType> &get_flow_narrowed_types() { return flow_narrowed_types; }
		const HashMap<const BSParser::Node *, BSParser::DataType> &get_flow_narrowed_types() const { return flow_narrowed_types; }

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
				const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, bool *r_is_self_receiver = nullptr,
				bool p_flattened_trait_body = false) const;
		void scan_illegal_final_writes(const BSParser::Node *p_node,
				const HashSet<const BSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, bool p_in_init,
				bool p_flattened_trait_body = false);
		void analyze_final_definite_assignment_statement(const BSParser::Node *p_statement,
				const HashSet<const BSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, FinalAssignmentState &r_state,
				HashSet<const BSParser::VariableNode *> &r_assigned_anywhere, bool p_flattened_trait_body = false);
		void analyze_final_definite_assignment_suite(const BSParser::SuiteNode *p_suite,
				const HashSet<const BSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, FinalAssignmentState &r_state,
				HashSet<const BSParser::VariableNode *> &r_assigned_anywhere, bool p_flattened_trait_body = false);
		void check_final_reads_in_expression(const BSParser::ExpressionNode *p_expression,
				const HashSet<const BSParser::VariableNode *> &p_finals,
				const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, const FinalAssignmentState &p_state,
				bool p_flattened_trait_body = false);
	};

	/**
	 * Foundry `TraitMethodImplementation` (@ c9d5e35): a concrete method that
	 * satisfies a trait abstract requirement, either as a FunctionNode or as
	 * MethodInfo from a native / foreign script surface.
	 */
	struct TraitMethodImplementation {
		BSParser::FunctionNode *function = nullptr;
		BSParser::ClassNode *owner_class = nullptr;
		MethodInfo method_info;
		String method_info_source;
		bool has_method_info = false;
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
	/** D1-trimmed PropertyInfo → DataType decode for MethodInfo call validation (@ c9d5e35). */
	BSParser::DataType type_from_property(const PropertyInfo &p_property, bool p_is_arg = false, bool p_is_readonly = false) const;

private:
	BSParser *parser = nullptr;
	AnalyzerPhase highest_completed_phase = AnalyzerPhase::NONE;
	bool strict_dynamic_checks = false;
	bool strict_null_checks = false;
	bool update_declaration_index = false;
	BSParser::ClassNode *current_class = nullptr;
	BSParser::FunctionNode *current_function = nullptr;
	CallSiteValidationContext call_site_validation;
	FlowFinalityContext flow_finality;

	Error run_phase_preflight();
	Error run_phase_inheritance_resolution();
	Error run_phase_interface_and_member_surface();
	Error run_phase_body_expression_callable_signal();
	Error run_phase_flow_finality();
	Error run_phase_conformance_witness_body();
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
	/** Foundry resolve_class_body unused pass: UNUSED_PRIVATE_CLASS_VARIABLE + UNUSED_SIGNAL. */
	void warn_unused_class_members(BSParser::ClassNode *p_class);
	/**
	 * Built-in annotation constant-argument resolution before apply (Foundry
	 * resolve_annotation @ c9d5e35). Custom / @autoload depth remains #60 follow-up.
	 */
	void resolve_annotation(BSParser::AnnotationNode *p_annotation, uint32_t p_target_kind = 0);
	/** Counts emit_signal/connect/disconnect/is_connected as signal uses (Foundry @ c9d5e35). */
	void mark_implicit_signal_usage(BSParser::CallNode *p_call, bool p_is_self);

	void reduce_literal(BSParser::LiteralNode *p_literal);
	void reduce_unary_op(BSParser::UnaryOpNode *p_unary_op);
	void reduce_binary_op(BSParser::BinaryOpNode *p_binary_op);
	void reduce_identifier(BSParser::IdentifierNode *p_identifier);
	void reduce_call(BSParser::CallNode *p_call);
	void reduce_subscript(BSParser::SubscriptNode *p_subscript);
	void reduce_array(BSParser::ArrayNode *p_array);
	void reduce_dictionary(BSParser::DictionaryNode *p_dictionary);
	void reduce_ternary(BSParser::TernaryOpNode *p_ternary);
	/** Foundry reduce_type_test starter (@ c9d5e35): resolve `is T` test type for flow narrowing. */
	void reduce_type_test(BSParser::TypeTestNode *p_type_test);
	void analyze_if(BSParser::IfNode *p_if);

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
	/** Foundry validate_trait_requirements @ c9d5e35: abstract methods from used traits. */
	void validate_trait_requirements(BSParser::ClassNode *p_class);
	bool find_trait_implementation(BSParser::ClassNode *p_class, const StringName &p_function_name,
			TraitMethodImplementation &r_implementation);
	/**
	 * Foundry validate_trait_method_signature @ c9d5e35 (non-generic slice): async/static/arity/
	 * parameter/return/rest matching, with `Self` reified to the implementer. Generic method
	 * alpha-equivalence / trait type-argument substitution remain follow-up under #60.
	 */
	bool validate_trait_method_signature(BSParser::ClassNode *p_trait, BSParser::ClassNode *p_implementing_class,
			BSParser::FunctionNode *p_required_function, const TraitMethodImplementation &p_implementation,
			const HashMap<StringName, BSParser::DataType> &p_trait_substitution = HashMap<StringName, BSParser::DataType>());
	bool validate_trait_method_info_signature(BSParser::ClassNode *p_trait, BSParser::ClassNode *p_implementing_class,
			BSParser::FunctionNode *p_required_function, const TraitMethodImplementation &p_implementation,
			const HashMap<StringName, BSParser::DataType> &p_trait_substitution = HashMap<StringName, BSParser::DataType>());
	void resolve_function_signature_in_class(BSParser::FunctionNode *p_function, BSParser::ClassNode *p_class);
	/** Foundry resolve_conformances starter: target/shim resolve + missing-witness checks. */
	void resolve_conformances(BSParser::ClassNode *p_class);
	BSParser::ClassNode *resolve_conformance_target(BSParser::ConformanceNode *p_conformance, BSParser::DataType &r_target_type);
	BSParser::ClassNode *resolve_native_conformance_shim(BSParser::ConformanceNode *p_conformance, const BSParser::DataType &p_native_type);
	BSParser::ClassNode *resolve_builtin_conformance_shim(BSParser::ConformanceNode *p_conformance, const BSParser::DataType &p_builtin_type);
	BSParser::ClassNode *resolve_conformance_trait_use(BSParser::ClassNode *p_scope, BSParser::ClassNode::TraitUse &p_trait_use, const BSParser::Node *p_source);
	bool validate_conformance(BSParser::ConformanceNode *p_conformance, BSParser::ClassNode *p_target, BSParser::ClassNode *p_trait);
	/** Foundry resolve_conformance_bodies: analyze witness methods against the target. */
	void resolve_conformance_bodies(BSParser::ClassNode *p_class);
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
