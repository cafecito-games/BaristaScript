/**************************************************************************/
/*  bs_platform_shims.cpp                                                 */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_platform.h"

/**
 * Including `bs_platform.h` proves its mappings resolve, but it does not compile the shims: a macro
 * that is never expanded and a class whose members are never called are both invisible to the
 * compiler. This translation unit uses each of them once, so the build keeps proving the shims work
 * against the current godot-cpp rather than only against the one they were written for.
 *
 * These functions are never called. They exist to be compiled.
 */
namespace bs_platform_seam {

String prove_string_builder() {
	StringBuilder builder;
	builder.append("appended as a C string");
	builder.append(String("appended as a String"));
	builder += "concatenated as a C string";
	builder += String("concatenated as a String");
	if (builder.num_strings_appended() == 0 || builder.get_string_length() == 0) {
		return String();
	}
	return builder.as_string();
}

const StringName &prove_sname() {
	return SNAME("BaristaScript");
}

} // namespace bs_platform_seam
