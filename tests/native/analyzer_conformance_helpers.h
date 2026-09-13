/**************************************************************************/
/*  analyzer_conformance_helpers.h                                        */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                          */
/*  This file is part of BaristaScript, a Godot GDExtension.                */
/*  SPDX-License-Identifier: MIT                                           */
/**************************************************************************/

#pragma once

#include "bs_analyzer.h"

namespace barista_script {
// Private test-only access; observation bodies preserve the old probe's typed
// compiler/registry operations while replacing its script Dictionary carrier.
struct AnalyzerMigrationTestAccess {
	struct ProjectedArgumentObservation {
		String name;
		bool projected = false;
		bool compatible = false;
		BSTypeCompatibility::ArgumentEvidence evidence = BSTypeCompatibility::ArgumentEvidence::UNKNOWN;
	};
	struct TraitTargetObservation {
		bool class_registry_conflict_rejects = false;
		bool class_registry_membership = false;
		bool class_registry_match_accepts = false;
		bool class_registry_no_evidence_accepts = false;
		bool native_conflict_rejects = false;
		bool native_membership = false;
		bool builtin_conflict_rejects = false;
		bool builtin_membership = false;
		bool uses_project_ok = false;
		bool uses_projection_conflict_rejects = false;
		bool unknown_nominal_projects = false;
		bool unknown_nominal_rejects = false;
		bool structured_nominal_projects = false;
		bool structured_nominal_rejects = false;
		bool live_arity_no_evidence_accepts = false;
		bool trait_self_match_accepts = false;
		bool trait_self_conflict_rejects = false;
		Vector<ProjectedArgumentObservation> structured_arguments;
	};
	static TraitTargetObservation trait_target_assignability();
	struct ExhaustionObservation {
		String name;
		bool helper = false;
		String alternative_set;
		bool scope_restored = false;
		bool subject_type_test = false;
		Vector<BSParser::ParserError> errors;
	};
	static Vector<ExhaustionObservation> type_test_exhaustion_controls();
	static bool can_see(BSAnalyzer &analyzer, const String &path);
	struct ConformanceRegistryRegistrationObservation {
		bool analyze_ok = false;
		int64_t registered_count = -1;
		String lookup_trait;
		String lookup_target;
		bool registered_after_analyze = false;
		bool none_sees_registered = false;
		bool in_flight_hides_has_conformance = false;
		bool after_in_flight_sees_again = false;
		bool viewer_parse_ok = false;
		bool viewer_hides_unrelated_declaring = false;
		bool viewer_can_see_own = false;
		bool viewer_can_see_dep = false;
		bool viewer_cannot_see_declaring = false;
		bool viewer_cannot_see_unrelated = false;
		bool dep_parse_ok = false;
		bool dep_can_see_declaring = false;
		bool dep_sees_has_conformance = false;
		bool reanalyze_clear_ok = false;
		bool cleared_after_reanalyze = false;
		bool reanalyze_replace_ok = false;
		bool replaced_has_other = false;
		bool replaced_dropped_old = false;
	};
	static ConformanceRegistryRegistrationObservation conformance_registry_registration();

	struct ConformanceWitnessLookupObservation {
		bool same_file_analyze_ok = false;
		godot::PackedStringArray same_file_errors;
		int64_t registered_count = -1;
		bool has_greet_witness_key = false;
		String lookup_target;
		String lookup_trait;
		int64_t registered_conformance_index = -1;
		bool find_witness_location_ok = false;
		bool class_arity_checks_witness = false;
		bool native_analyze_ok = false;
		bool native_arity_checks_witness = false;
		bool viewer_parse_ok = false;
		bool viewer_hides_witness_location = false;
		bool viewer_cannot_see_declaring = false;
		bool dep_parse_ok = false;
		bool dep_sees_witness_location = false;
		bool reanalyze_clear_ok = false;
		bool cleared_witness_location = false;
		bool cleared_file_empty = false;
	};
	static ConformanceWitnessLookupObservation conformance_witness_lookup();

	struct ConformanceHiddenWitnessObservation {
		bool declaring_analyze_ok = false;
		int64_t registered_count = -1;
		bool has_hid_mark_key = false;
		String lookup_target;
		String lookup_trait;
		bool visible_find_witness_location = false;
		bool no_visibility_hides_nothing = false;
		bool viewer_parse_ok = false;
		godot::PackedStringArray viewer_errors;
		bool viewer_hidden_diagnostic = false;
		bool viewer_analyze_failed = false;
		bool viewer_finds_hidden_declaration = false;
		bool viewer_hides_witness_location = false;
		godot::PackedStringArray dep_errors;
		bool dep_analyze_ok = false;
		godot::PackedStringArray final_declaring_errors;
		bool final_declaring_analyze_ok = false;
		bool final_has_greet_key = false;
		String final_lookup_target;
		bool final_viewer_parse_ok = false;
		bool final_viewer_finds_hidden = false;
		bool final_viewer_hides_location = false;
	};
	static ConformanceHiddenWitnessObservation conformance_hidden_witness();

	struct ClassTraitBindingChainCoherenceObservation {
		bool publish_ok = false;
		int64_t publish_registered_count = -1;
		bool binding_stored = false;
		int64_t agree_registered_count = -1;
		bool agree_no_chain_conflict = false;
		int64_t conflict_registered_count = -1;
		int64_t conflict_index = -1;
		String conflict_conflicting_label;
		String conflict_conflicting_file;
		bool conflict_chain_coherence = false;
		bool conflict_rejected = false;
		bool conflict_store_empty = false;
		bool reduce_builtin_ok = false;
		bool edge_loader_binding_ok = false;
		bool edge_reverse_chain_coherence = false;
		bool edge_reverse_rejected = false;
		bool edge_reverse_store_empty = false;
		bool edge_noedge_uncompared = false;
	};
	static ClassTraitBindingChainCoherenceObservation class_trait_binding_chain_coherence();

	struct RecordedTraitArgumentsQueryObservation {
		bool script_publish_ok = false;
		bool script_query_ok = false;
		bool hidden_script_query_false = false;
		bool hidden_native_query_false = false;
		bool hidden_builtin_query_false = false;
		bool builtin_publish_ok = false;
		bool builtin_query_ok = false;
		bool native_publish_ok = false;
		bool native_parent_walk_ok = false;
		bool farther_direct_ok = false;
		bool nearer_empty_shadows = false;
		bool farther_project_ok = false;
		bool class_to_native_project_ok = false;
	};
	static RecordedTraitArgumentsQueryObservation recorded_trait_arguments_query();

	struct WitnessCollisionArbitrationObservation {
		bool same_file_collision_diagnostic = false;
		bool same_file_first_registered = false;
		bool same_file_second_rejected = false;
		bool cross_first_ok = false;
		bool get_witness_source_first = false;
		bool cross_file_collision_diagnostic = false;
		bool cross_second_store_empty = false;
		bool distinct_ok_analyze = false;
		bool distinct_ok_registered = false;
		bool registry_first_ok = false;
		bool registry_witness_collision = false;
		bool registry_second_rejected = false;
	};
	static WitnessCollisionArbitrationObservation witness_collision_arbitration();

	struct CompleteSelfReferentialEnumTypeObservation {
		bool analyze_ok = false;
		godot::PackedStringArray errors;
		bool found_chain = false;
		int64_t declared_case_count = -1;
		bool has_link_payload = false;
		bool has_branch_payload = false;
		bool shell_is_enum = false;
		bool shell_values_empty = false;
		int64_t completed_values_count = -1;
		bool completed_has_end = false;
		bool completed_has_link = false;
		bool completed_has_branch = false;
		bool nested_link_stays_shell = false;
		bool idempotent_values = false;
		bool idempotent_nested_shell = false;
		int64_t array_container_size = -1;
		int64_t array_element_values_count = -1;
	};
	static CompleteSelfReferentialEnumTypeObservation complete_self_referential_enum_type();
};
} // namespace barista_script
