/**************************************************************************/
/*  bs_platform_seam.cpp                                                  */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_platform.h"

#include "bs_core_constants.h"

// Compile the complete mapped adapter; analyzer_test.gd exercises every producer entry at runtime.
[[maybe_unused]] static bool global_constant_seam_proof(const StringName &p_name) {
	return barista_script::CoreConstants::is_global_constant(p_name);
}
