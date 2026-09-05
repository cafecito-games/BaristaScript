/**************************************************************************/
/*  bs_analyzer_probe.h                                                   */
/*                                                                        */
/*  Debug-only analyzer probe for #43/#49/#52 GDScript suites.            */
/*  Includes complete_self_referential_enum_type (#60 residual).          */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#ifdef DEBUG_ENABLED

#include "bs_platform.h"

#include <godot_cpp/classes/ref_counted.hpp>

namespace barista_script {

class BaristaScriptAnalyzerProbe : public godot::RefCounted {
	GDCLASS(BaristaScriptAnalyzerProbe, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	/**
	 * Parse `return <expr>` inside a throwaway function, analyze, and return a Dictionary:
	 * `ok`, `value`, `value_type`, `has_unary_sign`, `errors` (PackedStringArray).
	 * Walks the parser AST only — does not re-tokenize for sign semantics (#49).
	 */
	godot::Dictionary fold_expression(const godot::String &p_expression_source) const;

	/** Run `_validate`-equivalent analysis; returns valid + first error message. */
	godot::Dictionary analyze_source(const godot::String &p_source, const godot::String &p_path) const;

	/** Whether `_is_valid` / analyze agree that the source is semantically valid. */
	bool is_semantically_valid(const godot::String &p_source, const godot::String &p_path) const;

	/** Language `_validate` dictionary for warning / error flag exercises. */
	godot::Dictionary validate_source(const godot::String &p_source, const godot::String &p_path, bool p_warnings = true) const;

	/**
	 * After parse (no full analyze), report ConformanceVisibility::can_see for each path.
	 * Returns Dictionary: ok, can_see (Dictionary path→bool), errors.
	 */
	godot::Dictionary conformance_visibility_can_see(const godot::String &p_source, const godot::String &p_path,
			const godot::PackedStringArray &p_candidates) const;

	/**
	 * Exercise ScopedVisibility nest/restore and ScopedInFlightReplacement against
	 * BSConformanceRegistry::debug_is_visible. Returns Dictionary of bool checks.
	 */
	godot::Dictionary scoped_visibility_nest_restore() const;

	/**
	 * Drive resolve_conformances registration: analyze a declaring file, then probe
	 * has_conformance under Visibility / in-flight / reanalysis / ConformanceVisibility.
	 * Returns Dictionary of bool checks plus registered_count.
	 */
	godot::Dictionary conformance_registry_registration() const;

	/**
	 * Drive witness method-name registration + find_witness_location + member-miss
	 * call resolution via find_conformance_witness. Returns Dictionary of bool checks.
	 */
	godot::Dictionary conformance_witness_lookup() const;

	/**
	 * Drive find_hidden_witness_declaration + find_hidden_conformance_witness diagnostic
	 * on a viewer that does not load the declaring file. Returns Dictionary of bool checks.
	 */
	godot::Dictionary conformance_hidden_witness() const;

	/**
	 * Drive ClassTraitBinding publish + CHAIN_COHERENCE rejection when a Conformance
	 * contradicts a visible uses-binding on the same script chain, plus reverse
	 * load-edge licensing when Visibility cannot reach the other file (Foundry
	 * native_chain_coherence / p_loaded_files @ c9d5e35).
	 */
	godot::Dictionary class_trait_binding_chain_coherence() const;

	/**
	 * Drive declaration-side recorded trait-argument queries:
	 * get_recorded_trait_arguments / get_native_recorded_trait_arguments /
	 * get_builtin_recorded_trait_arguments + project_registry_trait_arguments
	 * (visibility, nearer-empty shadowing, ClassDB parent walk). Foundry
	 * recorded_trait_arguments @ c9d5e35.
	 */
	godot::Dictionary recorded_trait_arguments_query() const;

	/**
	 * Drive BSTypeCompatibility::check trait-target assignability with recorded /
	 * projected type-argument conflict rejection (Foundry fs_type.cpp ~1352–1447).
	 */
	godot::Dictionary trait_target_assignability() const;

	/**
	 * Drive WITNESS_COLLISION: same-file seen_witnesses_by_target early diagnostic,
	 * cross-file get_witness_source + try_replace arbitration, and non-colliding OK
	 * (Foundry resolve_conformances / _declaration_witnesses_collide @ c9d5e35).
	 */
	godot::Dictionary witness_collision_arbitration() const;

	/**
	 * Drive complete_self_referential_enum_type on a non-generic recursive tagged union
	 * (Foundry Recursive completion is finite and stable @ c9d5e35): empty Link shell
	 * gains cases; nested Link edges stay shells; Array[Chain] completes through element;
	 * completion is idempotent. Also checks nested Chain.Link(Chain.End) construction.
	 */
	godot::Dictionary complete_self_referential_enum_type() const;
};

} // namespace barista_script

#endif // DEBUG_ENABLED
