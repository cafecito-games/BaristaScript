/**************************************************************************/
/*  bs_platform_probe.h                                                   */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace barista_script {

/**
 * The platform seam's test surface.
 *
 * godot-cpp's `String` and `StringName` are engine-backed, so the shims in `bs_platform.h` can
 * only be exercised from inside a loaded Godot runtime. This class renders their observable
 * behaviour into values a GDScript suite can compare exactly.
 *
 * It exists only in `template_debug`. The suites that reach it run against that build, so the
 * guard costs the suites nothing and keeps a test-only class out of the surface `template_release`
 * publishes.
 */
class BaristaScriptPlatformProbe final : public godot::RefCounted {
	GDCLASS(BaristaScriptPlatformProbe, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	/**
	 * Exercises `StringBuilder` through the checkpoints a behavioural test needs:
	 *
	 *   `fresh_count` / `fresh_length` / `fresh_string`  a new builder before any append.
	 *   `after_empty_string_count` / `after_empty_string_length`  after `append("")`.
	 *   `after_empty_cstring_count` / `after_empty_cstring_length`  after `append("")` as a C string.
	 *   `after_mixed_count` / `after_mixed_length`  after a non-empty `String` and C string.
	 *   `after_plus_equals_count` / `after_plus_equals_length`  after `operator+=`.
	 *   `final_count` / `final_length` / `final_string`  the completed builder.
	 */
	godot::Dictionary string_builder_behavior() const;

	/**
	 * Exercises `SNAME` through the checkpoints a behavioural test needs:
	 *
	 *   `site_a` / `site_b`  the string values from two distinct call sites.
	 *   `same_site_same_identity`  two reads from one call site share one cached `StringName`.
	 *   `distinct_sites_equal_value`  two call sites intern the same literal to equal values.
	 */
	godot::Dictionary sname_behavior() const;
};

} // namespace barista_script

#endif // DEBUG_ENABLED
