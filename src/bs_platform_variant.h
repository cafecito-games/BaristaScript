/**************************************************************************/
/*  bs_platform_variant.h                                                 */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

// Private implementation detail of bs_platform.h. Ported frontend files include the umbrella,
// never this header directly; tests/audit_platform_seam.py enforces that boundary.

#include <godot_cpp/variant/variant.hpp>

/** Adapter for core Variant operator introspection that godot-cpp does not expose directly. */
struct BSVariantOperators {
	static bool has_validated_evaluator(godot::Variant::Operator p_op, godot::Variant::Type p_a, godot::Variant::Type p_b);
	static godot::Variant::Type get_return_type(godot::Variant::Operator p_op, godot::Variant::Type p_a, godot::Variant::Type p_b);
};
