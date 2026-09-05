/**************************************************************************/
/*  bs_analyzer.h                                                         */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

/**************************************************************************/
/*  bs_analyzer.h                                                         */
/*                                                                        */
/*  M3 analyzer port seam (issue #43/#57/#60) @ Foundry c9d5e35. Staged    */
/*  resolve_*; inheritance/interface/body fold (#49); declaration commit  */
/*  (#52/#58); call/match/flow (#61); final DA + null/`is` narrowing;     */
/*  CallSiteValidationContext / connect-callable; unused surface; ENUM_CASE*/
/*  / `.Case` / exhaustiveness; Callable.bind/unbind/call/callv/rpc;      */
/*  pending-warning finalize on flow-finality early exit; trait           */
/*  conformance witness; get_operation_type for binary/unary/compound;    */
/*  resolve_class_member same-parser depth; OwnerResolutionFailures /     */
/*  DependentResolutionFailureReplays / ForeignAnalyzerVisibilityScope    */
/*  (real BSConformanceRegistry::ScopedVisibility) for external SCRIPT    */
/*  member + class-phase INTERFACE/BODY failure replay;                   */
/*  ConformanceVisibility can_see BFS; resolve_conformances registers via */
/*  try_replace_file_conformances + ScopedInFlightReplacement;            */
/*  reduce_await + MISSING_AWAIT / REDUNDANT_AWAIT for AsyncCallable→     */
/*  coroutine wrap; Coroutine[T] annotation decode; direct async-call wrap*/
/*  + mark_coroutine_handle_capture (#60 residual).                       */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_conformance_registry.h"
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
	 * call arity+type checks, signal emit / emit_signal payload validation,
	 * named-arg canonicalization, signal connect/callable signature checks,
	 * and Callable.bind / bindv / unbind / call signature transforms.
	 * Generic inference richness remains follow-up under #60.
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

		static bool call_has_named_arguments(const BSParser::CallNode *p_call);
		void reject_named_call_arguments(const BSParser::CallNode *p_call);
		bool canonicalize_named_call_arguments(BSParser::CallNode *p_call, const BSParser::FunctionNode *p_function);

		bool callable_signature_from_type(const BSParser::DataType &p_callable_type, Vector<BSParser::DataType> &r_par_types, int &r_default_arg_count, bool &r_is_vararg) const;
		BSParser::DataType callable_type_from_function(const BSParser::FunctionNode *p_function) const;

		/** Foundry plain_callable_type / over_bound_callable_type / transformed_callable_type @ c9d5e35. */
		BSParser::DataType plain_callable_type() const;
		BSParser::DataType over_bound_callable_type(const BSParser::DataType &p_source_callable_type) const;
		BSParser::DataType transformed_callable_type(const BSParser::DataType &p_source_callable_type, const Vector<BSParser::DataType> &p_parameter_types, int p_default_arg_count, bool p_is_vararg) const;
		BSParser::ArrayNode *array_literal_argument(const BSParser::CallNode *p_call, int p_argument_index) const;
		/** Foundry validate_callable_array_literal_args for Callable.callv array-literal element checks. */
		void validate_callable_array_literal_args(const Vector<BSParser::DataType> &p_par_types, int p_default_args_count, bool p_is_vararg, BSParser::ArrayNode *p_array, const StringName &p_function, const Vector<int> &p_extra_allowed_argument_counts = Vector<int>(), int p_trailing_unbound_argument_count = 0, const BSParser::DataType *p_rest_parameter_type = nullptr);
		/**
		 * Foundry get_function_signature Callable.bind/bindv/unbind/call/callv/call_deferred/rpc/rpc_id
		 * slice (@ c9d5e35). When `p_call` is a typed-Callable attribute call, validates args and
		 * types the result. Returns true when the call was handled (including over-bound invocation
		 * errors). Synchronous call/callv on AsyncCallable wraps the return as Coroutine[T];
		 * deferred/RPC stay non-coroutine NIL.
		 */
		bool try_type_callable_method_call(BSParser::CallNode *p_call, const BSParser::DataType &p_base_type);

		BSParser::DataType explicit_signal_type_from_info(const MethodInfo &p_info) const;
		BSParser::DataType explicit_signal_type_from_node(const BSParser::SignalNode *p_signal, const BSParser::DataType &p_receiver_type, const BSParser::ClassNode *p_declaring_class) const;
		bool signal_name_from_constant_arg(const BSParser::CallNode *p_call, int p_signal_arg_index, StringName &r_signal_name) const;
		bool signal_type_from_class_constant_arg(const BSParser::DataType &p_receiver_type, const BSParser::CallNode *p_call, int p_signal_arg_index, BSParser::DataType &r_signal_type) const;
		bool signal_type_from_native_constant_arg(const StringName &p_native_type, const BSParser::CallNode *p_call, int p_signal_arg_index, BSParser::DataType &r_signal_type) const;
		bool local_signal_type_from_constant_arg(const BSParser::CallNode *p_call, int p_signal_arg_index, BSParser::DataType &r_signal_type) const;
		void validate_strict_signal_name_fallback(const BSParser::CallNode *p_call, const BSParser::DataType &p_receiver_type, int p_signal_arg_index);
		bool call_argument_can_be_string_name(const BSParser::CallNode *p_call, int p_argument_index);

		void validate_signal_emit_args(const BSParser::DataType &p_signal_type, const BSParser::CallNode *p_call, int p_first_emit_arg_index);
		void validate_signal_connect_arg(const BSParser::DataType &p_signal_type, const BSParser::CallNode *p_call, int p_callable_arg_index = 0);
		void validate_local_object_emit_signal_args(const BSParser::CallNode *p_call, bool p_is_self);
		void validate_local_object_signal_callable_arg(const BSParser::CallNode *p_call, bool p_is_self);

	private:
		BSAnalyzer *analyzer = nullptr;
	};

	/**
	 * Hard fork of Foundry `FSAnalyzer::FlowFinalityContext` (@ c9d5e35,
	 * `fs_analyzer_flow_finality.cpp`). LOCAL + INSTANCE + STATIC `final var`
	 * definite assignment / illegal writes, trait-member flattening into
	 * implementers, plus if/while/assert null-check and `is` type-test flow
	 * narrowing for locals/parameters (including ENUM_CASE arms once
	 * `case_datatype` is published). Lambda capture mark/clear and
	 * compound-assignment narrowed-read restore + get_operation_type compound
	 * left-operand typing are wired in `bs_analyzer.cpp` (@ c9d5e35); remaining
	 * flow TU depth stays under #60.
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
		/** Foundry apply_match_branch_flow_narrowing @ c9d5e35: type / null overlays per match arm. */
		void apply_match_branch_flow_narrowing(BSParser::ExpressionNode *p_match_test, BSParser::MatchBranchNode *p_match_branch);
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
	 * Hard fork of Foundry `FSAnalyzer::OwnerResolutionFailures` (@ c9d5e35,
	 * `fs_analyzer.h` ~195–260). Resolution flags and datatypes are shared through
	 * BSCache, so owner-local failures must be memoized alongside them. Records only
	 * errors added by the exact class phase/member so a later foreign caller never
	 * infers failure from unrelated errors already in the owner parser.
	 * Wired for member path (#118) and class-phase INTERFACE/BODY (#60 residual slice).
	 */
	class OwnerResolutionFailures {
	public:
		enum ClassPhase : uint8_t {
			INTERFACE = 1 << 0,
			BODY = 1 << 1,
		};

	private:
		struct ClassFailures {
			uint8_t phases = 0;
			int interface_first_error_index = -1;
			int body_first_error_index = -1;
			HashMap<int, int> member_first_error_indices;
		};

		HashMap<const BSParser::ClassNode *, ClassFailures> failures;

	public:
		void record_class(const BSParser::ClassNode *p_class, ClassPhase p_phase, int p_first_error_index) {
			ClassFailures &class_failures = failures[p_class];
			class_failures.phases |= p_phase;
			int *first_error_index = &class_failures.body_first_error_index;
			if (p_phase == INTERFACE) {
				first_error_index = &class_failures.interface_first_error_index;
			}
			if (*first_error_index < 0 || p_first_error_index < *first_error_index) {
				*first_error_index = p_first_error_index;
			}
		}

		bool has_class(const BSParser::ClassNode *p_class, ClassPhase p_phase) const {
			const ClassFailures *class_failures = failures.getptr(p_class);
			return class_failures != nullptr && (class_failures->phases & p_phase) != 0;
		}

		int first_error_index(const BSParser::ClassNode *p_class, ClassPhase p_phase) const {
			const ClassFailures *class_failures = failures.getptr(p_class);
			if (class_failures == nullptr) {
				return -1;
			}
			if (p_phase == INTERFACE) {
				return class_failures->interface_first_error_index;
			}
			return class_failures->body_first_error_index;
		}

		void record_member(const BSParser::ClassNode *p_class, int p_index, int p_first_error_index) {
			failures[p_class].member_first_error_indices.insert(p_index, p_first_error_index);
		}

		bool has_member(const BSParser::ClassNode *p_class, int p_index) const {
			const ClassFailures *class_failures = failures.getptr(p_class);
			return class_failures != nullptr && class_failures->member_first_error_indices.has(p_index);
		}

		int member_first_error_index(const BSParser::ClassNode *p_class, int p_index) const {
			const ClassFailures *class_failures = failures.getptr(p_class);
			if (class_failures == nullptr) {
				return -1;
			}
			const int *first_error_index = class_failures->member_first_error_indices.getptr(p_index);
			return first_error_index != nullptr ? *first_error_index : -1;
		}
	};

	OwnerResolutionFailures owner_resolution_failures;

	/**
	 * Hard fork of Foundry `FSAnalyzer::DependentResolutionFailureReplays` (@ c9d5e35,
	 * `fs_analyzer.h` ~265–293). Replaying an owner-memoized failure is observable in the
	 * dependent parser, whose analysis may revisit the same shared node many times. Record
	 * replays so one dependent reports each foreign member/phase once, while separate
	 * analyzers and separate operations still propagate it.
	 */
	class DependentResolutionFailureReplays {
		struct ClassReplays {
			uint8_t phases = 0;
			HashSet<int> members;
		};

		HashMap<const BSParser::ClassNode *, ClassReplays> replays;

	public:
		bool record_class(const BSParser::ClassNode *p_class, OwnerResolutionFailures::ClassPhase p_phase) {
			ClassReplays &class_replays = replays[p_class];
			if ((class_replays.phases & p_phase) != 0) {
				return false;
			}
			class_replays.phases |= p_phase;
			return true;
		}

		bool record_member(const BSParser::ClassNode *p_class, int p_index) {
			ClassReplays &class_replays = replays[p_class];
			if (class_replays.members.has(p_index)) {
				return false;
			}
			class_replays.members.insert(p_index);
			return true;
		}
	};

	DependentResolutionFailureReplays dependent_resolution_failure_replays;

	/**
	 * Hard fork of Foundry `FSAnalyzer::ConformanceVisibility` (@ c9d5e35,
	 * `fs_analyzer.h` ~175–190 / `fs_analyzer_conformance.cpp` ~691–734). Decides which
	 * files' retroactive conformances this analysis may honor: its own path and whatever
	 * it transitively depends on (declared preload/extends + resolved depended_parsers).
	 */
	class ConformanceVisibility : public BSConformanceRegistry::Visibility {
		BSAnalyzer *analyzer = nullptr;
		mutable HashSet<String> visible_files;

	public:
		explicit ConformanceVisibility(BSAnalyzer *p_analyzer);
		bool can_see(const String &p_source_file) const override;
	};

	ConformanceVisibility conformance_visibility;

	/**
	 * Hard fork of Foundry `FSAnalyzer::ForeignAnalyzerVisibilityScope` (@ c9d5e35,
	 * `fs_analyzer.h` ~298+). A foreign node's memoized result must be defined by its
	 * owning file; routes the owner's `ConformanceVisibility` via
	 * `BSConformanceRegistry::ScopedVisibility` exactly like Foundry.
	 */
	class ForeignAnalyzerVisibilityScope {
		BSConformanceRegistry::ScopedVisibility visibility_scope;

	public:
		explicit ForeignAnalyzerVisibilityScope(BSAnalyzer *p_owner) :
				visibility_scope(&p_owner->conformance_visibility) {}
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

	/** Foundry `FSAnalyzer::Finally` (@ c9d5e35): run a cleanup lambda on scope exit. */
	template <typename Fn>
	class Finally {
		Fn fn;

	public:
		explicit Finally(Fn p_fn) :
				fn(p_fn) {}
		~Finally() {
			fn();
		}
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
	/** Foundry type_from_metatype @ c9d5e35: meta → value (ENUM → INT / tagged ARRAY). */
	static BSParser::DataType type_from_metatype(const BSParser::DataType &p_meta_type);
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
	/** Foundry `current_lambda` (@ c9d5e35): set while reducing a lambda body for capture marking. */
	BSParser::LambdaNode *current_lambda = nullptr;
	/** Foundry pending_body_resolution_lambdas (@ c9d5e35): flush after each suite statement. */
	Vector<BSParser::LambdaNode *> pending_lambda_bodies;
	CallSiteValidationContext call_site_validation;
	FlowFinalityContext flow_finality;

	// Foundry reduced_contextual_enum_cases / resolved_contextual_enum_cases @ c9d5e35:
	// shorthands reduced before their consumer supplies a union, then the end-of-body sweep.
	LocalVector<BSParser::ExpressionNode *> reduced_contextual_enum_cases;
	HashSet<const BSParser::ExpressionNode *> resolved_contextual_enum_cases;

	Error run_phase_preflight();
	Error run_phase_inheritance_resolution();
	Error run_phase_interface_and_member_surface();
	Error run_phase_body_expression_callable_signal();
	Error run_phase_flow_finality();
	Error run_phase_conformance_witness_body();
	Error run_phase_finalize();

	void resolve_class_inheritance(BSParser::ClassNode *p_class);
	/**
	 * Foundry get_class_node_current_scope_classes @ c9d5e35 (`fs_analyzer_surface.cpp`):
	 * walk base CLASS chain then outer for same-file extends / member lookup.
	 */
	void get_class_node_current_scope_classes(BSParser::ClassNode *p_node, List<BSParser::ClassNode *> *p_list, BSParser::Node *p_source);
	/** Bind an identifier to a VARIABLE/CONSTANT/SIGNAL/FUNCTION/ENUM member of `p_class` when present. */
	bool try_bind_identifier_member(BSParser::IdentifierNode *p_identifier, BSParser::ClassNode *p_class, bool p_mark_inherited);
	/** Walk `p_class` then `base_type.class_type` for a named member bind. */
	bool try_bind_identifier_member_in_inheritance(BSParser::IdentifierNode *p_identifier, BSParser::ClassNode *p_class);
	/**
	 * Foundry resolve_class_member @ c9d5e35 (`fs_analyzer_surface.cpp`): lazily resolve a class
	 * member's datatype with cyclic `RESOLVING` fail-stop before identifier/member binds read it.
	 * Same-parser path is complete for VARIABLE/CONSTANT/FUNCTION/SIGNAL/ENUM/CLASS. External /
	 * SCRIPT members raise via `BSCache::get_parser`, wrap `ForeignAnalyzerVisibilityScope`,
	 * record owner member failures, and replay dependent diagnostics with dedupe.
	 */
	void resolve_class_member(BSParser::ClassNode *p_class, const StringName &p_name, const BSParser::Node *p_source = nullptr);
	void resolve_class_member(BSParser::ClassNode *p_class, int p_index, const BSParser::Node *p_source = nullptr);
	void resolve_datatype(BSParser::DataType &r_type, BSParser::Node *p_source);
	BSParser::DataType datatype_from_type_node(BSParser::TypeNode *p_type_node);
	/**
	 * Foundry resolve_class_interface @ c9d5e35 (`fs_analyzer_surface.cpp` ~2030): own-class
	 * member surface + base INTERFACE walk; foreign SCRIPT raise under
	 * ForeignAnalyzerVisibilityScope with OwnerResolutionFailures::INTERFACE memoization and
	 * DependentResolutionFailureReplays dedupe ("Could not resolve class").
	 */
	void analyze_class_interface(BSParser::ClassNode *p_class, const BSParser::Node *p_source = nullptr);
	/**
	 * Foundry resolve_class_body @ c9d5e35 (`fs_analyzer.cpp` ~3545): own-class body analysis;
	 * foreign SCRIPT raise under ForeignAnalyzerVisibilityScope with
	 * OwnerResolutionFailures::BODY memoization and dependent replay dedupe.
	 */
	void analyze_class_body(BSParser::ClassNode *p_class, const BSParser::Node *p_source = nullptr);
	/** `p_is_lambda`: Foundry resolve_function_body — skip clearing captured-source tracking. */
	void analyze_function_body(BSParser::FunctionNode *p_function, bool p_is_lambda = false);
	/** Foundry resolve_pending_lambda_bodies @ c9d5e35. */
	void resolve_pending_lambda_bodies();
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

	/**
	 * Foundry resolve_enum_values @ c9d5e35 (`fs_analyzer_surface.cpp`): publish
	 * named enum / tagged-union identity + case tags/payload field types.
	 * Generic union parameters remain M5 / #60 follow-up.
	 */
	BSParser::DataType resolve_enum_values(BSParser::EnumNode *p_enum, const BSParser::DataType &p_enum_type, BSParser::ClassNode *p_owner);
	/** Foundry make_class_enum_type shell used before resolve_enum_values. */
	static BSParser::DataType make_class_enum_type(const StringName &p_enum_name, BSParser::ClassNode *p_class, const String &p_script_path, bool p_meta = true);
	/** Look up a same-file / enclosing-class named enum member (lazy-resolves values). */
	BSParser::DataType lookup_local_enum_meta_type(const StringName &p_name, BSParser::Node *p_source);

	void reduce_literal(BSParser::LiteralNode *p_literal);
	void reduce_unary_op(BSParser::UnaryOpNode *p_unary_op);
	void reduce_binary_op(BSParser::BinaryOpNode *p_binary_op);
	/**
	 * Hard fork of Foundry `FSAnalyzer::get_operation_type` (@ c9d5e35, `fs_analyzer.cpp`).
	 * Unary overload forwards through a NIL right operand. D1 drops the mixed INT/UINT carrier-
	 * widening arm and numeric_type stamping; set-wise union enumeration and tagged-union identity
	 * rules are preserved. Engine contact via `BSVariantOperators` / `Variant::evaluate`.
	 */
	BSParser::DataType get_operation_type(Variant::Operator p_operation, const BSParser::DataType &p_a, const BSParser::DataType &p_b, bool &r_valid, const BSParser::Node *p_source);
	BSParser::DataType get_operation_type(Variant::Operator p_operation, const BSParser::DataType &p_a, bool &r_valid, const BSParser::Node *p_source);
	void mark_node_unsafe(const BSParser::Node *p_node);
	void reduce_identifier(BSParser::IdentifierNode *p_identifier);
	/** Foundry reduce_identifier lambda capture walk (@ c9d5e35) after a capturable local bind. */
	void maybe_capture_identifier_in_lambda(BSParser::IdentifierNode *p_identifier);
	/**
	 * Foundry reduce_call @ c9d5e35: `p_is_await` / `p_is_root` gate MISSING_AWAIT on discarded
	 * non-void coroutine results. Direct async-call wrap (bare `async_fn()` → Coroutine[T]) is
	 * applied in validate_local_call; AsyncCallable.call/callv wrapping is already landed.
	 */
	void reduce_call(BSParser::CallNode *p_call, bool p_is_await = false, bool p_is_root = false);
	/**
	 * Foundry mark_coroutine_handle_capture @ c9d5e35 (~1680 / member ~19211): when a coroutine
	 * call's live BSFunctionState handle is captured into a hard Coroutine[T] slot, mark
	 * CallNode::is_coroutine_handle_capture so the compiler can emit OPCODE_CALL_ASYNC.
	 */
	void mark_coroutine_handle_capture(BSParser::ExpressionNode *p_expression, const BSParser::DataType &p_target_type);
	/**
	 * Foundry reduce_await @ c9d5e35: single-level Coroutine[T]→T unwrap, signal→Variant,
	 * constant non-coroutine passthrough, nullable coroutine nullability propagation, and
	 * REDUNDANT_AWAIT on synchronous non-signal operands.
	 */
	void reduce_await(BSParser::AwaitNode *p_await);
	/** Foundry reduce_lambda (@ c9d5e35): Callable type + body under `current_lambda`. */
	void reduce_lambda(BSParser::LambdaNode *p_lambda);
	void reduce_subscript(BSParser::SubscriptNode *p_subscript);
	void reduce_array(BSParser::ArrayNode *p_array);
	void reduce_dictionary(BSParser::DictionaryNode *p_dictionary);
	void reduce_ternary(BSParser::TernaryOpNode *p_ternary);
	/** Foundry reduce_cast @ c9d5e35: cast type qualifies contextual `.Case` in operand position. */
	void reduce_cast(BSParser::CastNode *p_cast);
	/** Foundry reduce_type_test (@ c9d5e35): resolve `is T` + case-bind payload typing. */
	void reduce_type_test(BSParser::TypeTestNode *p_type_test);
	/** Foundry resolve_type_test_case_binds @ c9d5e35: type `is Message.Move(x, y)` binds. */
	void resolve_type_test_case_binds(BSParser::TypeTestNode *p_type_test, const BSParser::DataType &p_test_type);
	void analyze_if(BSParser::IfNode *p_if);
	/** Foundry resolve_match @ c9d5e35 (match-branch narrowing slice): patterns + per-arm narrowing. */
	void resolve_match(BSParser::MatchNode *p_match);
	/**
	 * Foundry resolve_match_branch @ c9d5e35: `p_subject_errored` says the subject expression's
	 * own reduction already reported why its type is unknown. The fallback type it left behind
	 * is indistinguishable from a written-out Variant, so the flag keeps a contextual shorthand
	 * from reporting that same failure once per arm.
	 */
	void resolve_match_branch(BSParser::MatchBranchNode *p_match_branch, BSParser::ExpressionNode *p_match_test, bool p_subject_errored = false);
	/**
	 * Foundry resolve_match_pattern @ c9d5e35: LITERAL / EXPRESSION / WILDCARD / BIND /
	 * ENUM_CASE / ARRAY / DICTIONARY / TUPLE / REST, including contextual `.Case` match /
	 * `is` qualification. Exhaustiveness is computed by check_match_exhaustiveness.
	 */
	void resolve_match_pattern(BSParser::PatternNode *p_match_pattern, BSParser::ExpressionNode *p_match_test, const BSParser::DataType *p_match_test_type = nullptr, bool p_subject_errored = false);

	/** What one match pattern proves about a tagged-union subject's case set (Foundry @ c9d5e35). */
	enum TaggedUnionPatternCoverage {
		TAGGED_UNION_PATTERN_COVERS_NOTHING,
		TAGGED_UNION_PATTERN_COVERS_CASE,
		TAGGED_UNION_PATTERN_COVERS_NULL,
		TAGGED_UNION_PATTERN_COVERAGE_UNPROVABLE,
	};

	static TaggedUnionPatternCoverage tagged_union_pattern_coverage(const BSParser::PatternNode *p_pattern, const BSParser::DataType &p_match_type, int64_t &r_covered_tag);
	static bool match_branch_always_matches(const BSParser::MatchBranchNode *p_branch);
	bool collect_uncovered_tagged_union_cases(const BSParser::MatchNode *p_match, const BSParser::DataType &p_match_type, Vector<String> &r_uncovered) const;
	bool collect_uncovered_domain_values(const BSParser::MatchNode *p_match, const BSParser::DataType &p_match_type, const HashMap<StringName, int64_t> &p_domain_values, Vector<String> &r_uncovered) const;
	/** Foundry resolve_match_case_pattern @ c9d5e35: `Message.Move(x, _)` / `.Move(x, _)` payload typing. */
	void resolve_match_case_pattern(BSParser::PatternNode *p_match_pattern, const BSParser::DataType *p_match_test_type, bool p_subject_errored = false);
	/**
	 * Foundry tagged_union_metatype_from_expected_type @ c9d5e35: invert type_from_metatype
	 * for a tagged-union value type so a contextual shorthand can publish the subject's union.
	 */
	bool tagged_union_metatype_from_expected_type(const BSParser::DataType &p_expected_type,
			const BSParser::Node *p_source, BSParser::DataType &r_enum_meta_type);
	/**
	 * Foundry resolve_contextual_case_pattern_type @ c9d5e35: qualify `.Case` against a
	 * tagged-union match subject or `is` operand.
	 */
	bool resolve_contextual_case_pattern_type(const StringName &p_case_name, const BSParser::DataType *p_subject_type,
			const char *p_subject_description, const BSParser::Node *p_source, BSParser::DataType &r_case_meta_type,
			bool p_subject_errored = false);
	/**
	 * Foundry resolve_contextual_case_value_pattern @ c9d5e35: payload-less `.Quit` as a
	 * match expression pattern.
	 */
	bool resolve_contextual_case_value_pattern(BSParser::ExpressionNode *p_expression, const BSParser::DataType *p_match_test_type, bool p_subject_errored = false);
	/**
	 * Foundry resolve_contextual_enum_case @ c9d5e35: qualify `.Case` / `.Case(...)` against the
	 * consumer's expected tagged-union type (assign / return / annotated var / cast / call arg /
	 * container element via update_container_literal_element_types).
	 */
	bool resolve_contextual_enum_case(BSParser::ExpressionNode *p_expression, const BSParser::DataType &p_expected_type);
	bool contextual_enum_case_awaits_expected_type(BSParser::ExpressionNode *p_expression);
	void register_contextual_enum_case(BSParser::ExpressionNode *p_expression);
	void report_unqualified_contextual_enum_cases();
	/**
	 * Foundry update_container_literal_element_types @ c9d5e35 (contextual-`.Case` slice): push the
	 * consumer's Array/Dictionary element types into literal elements so nested shorthands resolve.
	 * Full Self / gradual / type-handle element checking remains follow-up under #60.
	 */
	bool update_container_literal_element_types(BSParser::ExpressionNode *p_expression, const BSParser::DataType &p_expected_type);
	void update_array_literal_element_type(BSParser::ArrayNode *p_array, const BSParser::DataType &p_element_type);
	void update_dictionary_literal_element_type(BSParser::DictionaryNode *p_dictionary, const BSParser::DataType &p_key_type, const BSParser::DataType &p_value_type);
	/** resolve_contextual_enum_case + container-literal element descent for one consumer site. */
	void qualify_contextual_enum_case_consumer(BSParser::ExpressionNode *p_expression, const BSParser::DataType &p_expected_type);
	/**
	 * Foundry reduce_call_enum_case_construction @ c9d5e35 (non-generic slice): arity + payload
	 * field compatibility + constant bake. Self/generic payload depth remains #60.
	 */
	void reduce_call_enum_case_construction(BSParser::CallNode *p_call, const BSParser::DataType &p_enum_meta_type);

	void validate_bootstrap_namespace_imports();
	bool validate_bootstrap_namespace_import(const String &p_import);
	void validate_local_call(BSParser::CallNode *p_call, BSParser::FunctionNode *p_callee);
	/** Foundry check_match_exhaustiveness @ c9d5e35: bool / tagged-union / plain-enum coverage. */
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
	/** Foundry resolve_conformances: validate + register into BSConformanceRegistry. */
	void resolve_conformances(BSParser::ClassNode *p_class);
	BSParser::ClassNode *resolve_conformance_target(BSParser::ConformanceNode *p_conformance, BSParser::DataType &r_target_type);
	BSParser::ClassNode *resolve_native_conformance_shim(BSParser::ConformanceNode *p_conformance, const BSParser::DataType &p_native_type);
	BSParser::ClassNode *resolve_builtin_conformance_shim(BSParser::ConformanceNode *p_conformance, const BSParser::DataType &p_builtin_type);
	BSParser::ClassNode *resolve_conformance_trait_use(BSParser::ClassNode *p_scope, BSParser::ClassNode::TraitUse &p_trait_use, const BSParser::Node *p_source);
	bool validate_conformance(BSParser::ConformanceNode *p_conformance, BSParser::ClassNode *p_target, BSParser::ClassNode *p_trait);
	/** Foundry resolve_conformance_bodies: analyze witness methods against the target. */
	void resolve_conformance_bodies(BSParser::ClassNode *p_class);
	/** Own members then `base_type.class_type` chain (Foundry inherited method surface @ c9d5e35). */
	BSParser::FunctionNode *find_class_function(BSParser::ClassNode *p_class, const StringName &p_name) const;
	BSParser::DataType resolve_named_type(const String &p_qualified, BSParser::Node *p_source);
	bool errors_are_only_m5_deferred() const;
	/** True when every error at/after `p_from_index` is an M5 deferred diagnostic (or none exist). */
	bool errors_from_index_are_only_m5_deferred(int p_from_index) const;

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

/**
 * Foundry make_coroutine_type @ c9d5e35 (~1646): wrap result T as Coroutine[T]. Principal identity
 * is the native BSFunctionState skin; is_coroutine discriminates await / missing-await; the phantom
 * result type lives in container_element_types[0]. Shared by annotation decode, AsyncCallable wrap,
 * and direct async-call wrap in validate_local_call.
 */
BSParser::DataType make_coroutine_type(const BSParser::DataType &p_result_type);

} // namespace barista_script
