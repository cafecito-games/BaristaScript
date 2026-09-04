/**************************************************************************/
/*  bs_core_constants.h                                                   */
/*                                                                        */
/*  Fail-closed CoreConstants adapter. godot-cpp does not mirror the      */
/*  engine-internal global-constant registry; unknown names return false. */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_platform.h"

namespace barista_script {

/**
 * Stand-in for `CoreConstants::*` (@ c9d5e35). A GDExtension cannot enumerate Godot's
 * `@GlobalScope` constants the way an engine module can, so every query fails closed: the analyzer
 * must not invent a global enum or constant when the registry is unreachable.
 */
class CoreConstants {
public:
	static bool is_global_enum(const StringName &) { return false; }
	static bool is_global_constant(const StringName &) { return false; }
	static int get_global_constant_index(const StringName &) { return -1; }
	static StringName get_global_constant_enum(int) { return StringName(); }
	static int64_t get_global_constant_value(int) { return 0; }
	static void get_enum_values(const StringName &, HashMap<StringName, int64_t> *) {}
};

} // namespace barista_script
