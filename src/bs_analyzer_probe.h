/**************************************************************************/
/*  bs_analyzer_probe.h                                                   */
/*                                                                        */
/*  Debug-only analyzer probe for #43/#49/#52 GDScript suites.            */
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
};

} // namespace barista_script

#endif // DEBUG_ENABLED
