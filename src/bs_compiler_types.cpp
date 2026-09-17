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
		// A script-typed slot loses its identity here, because the identity is a pointer into a parse
		// tree that does not outlive the compilation. The slot therefore accepts any value: the
		// script-identity check the declaration asks for is NOT enforced at run time. The type model
		// is what restores it, by carrying an identity the compiled function can own.
		slot.kind = BSParser::DataType::VARIANT;
	}
	return slot;
}

bool BSCompiler::slot_is_checkable(const BSParser::DataType &p_slot) {
	// A value stored into a slot is checked against the slot's carrier, against its native class, or
	// not at all when the slot is untyped. A tuple, a union, an enum or a type parameter is none of
	// those: the writers that would check them exist and refuse, and nothing may reach a slot that
	// would accept anything while claiming to be checked.
	//
	// A builtin carrier is checked exactly; a native class is checked by class identity. What a
	// container slot says about its *elements* is not checked -- `Array[int]` is an `Array` here --
	// and neither is the script identity of a `CLASS`/`SCRIPT` slot, which `member_slot_type` erases
	// above. Both are recorded in docs/runtime.md rather than implied by this list.
	return p_slot.is_variant() || p_slot.kind == BSParser::DataType::BUILTIN ||
			p_slot.kind == BSParser::DataType::NATIVE;
}

bool BSCompiler::refuse_unchecked_slot(const BSParser::DataType &p_slot, const String &p_subject, const BSParser::Node *p_origin) {
	if (slot_is_checkable(p_slot)) {
		return false;
	}
	set_error(vformat(R"(The runtime cannot check the declared type of %s, so it will not run it unchecked.)", p_subject),
			p_origin);
	return true;
}

} // namespace barista_script
