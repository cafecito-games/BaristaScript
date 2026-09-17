/**************************************************************************/
/*  bs_compiler_types.cpp                                                 */
/*                                                                        */
/*  Type lowering: how an analyzed type becomes the descriptor a compiled */
/*  slot carries. Provenance: fs_compiler.cpp `_gdtype_from_datatype`     */
/*  @ c9d5e35.                                                            */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_compiler.h"

namespace barista_script {

BSParser::DataType BSCompiler::member_slot_type(const BSParser::DataType &p_type) {
	// The runtime reads a slot type's carrier and its native class name only. A class pointer is
	// owned by the parse tree, which does not outlive the compilation, so it is never carried into a
	// compiled function.
	BSParser::DataType slot;
	slot.kind = p_type.kind;
	slot.type_source = p_type.type_source;
	slot.builtin_type = p_type.builtin_type;
	slot.native_type = p_type.native_type;
	slot.is_nullable = p_type.is_nullable;
	if (p_type.kind == BSParser::DataType::CLASS || p_type.kind == BSParser::DataType::SCRIPT) {
		// A script-typed slot is checked by the value's own identity at run time, not by a pointer
		// into a tree that has already been freed.
		slot.kind = BSParser::DataType::VARIANT;
	}
	return slot;
}

} // namespace barista_script
