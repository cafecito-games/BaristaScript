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
	// Addresses exist only while this parser generation is being compiled. Keeping the analyzed
	// shape here lets the emitter lower it into a BSRuntimeType -- which records a stable weak script
	// identity and owns all nested container descriptors -- before the parser can be released. No
	// DataType is retained by BSFunction after write_end().
	return p_type;
}

bool BSCompiler::slot_is_checkable(const BSParser::DataType &p_slot) {
	return p_slot.is_variant() || p_slot.kind == BSParser::DataType::BUILTIN ||
			p_slot.kind == BSParser::DataType::NATIVE || p_slot.kind == BSParser::DataType::CLASS ||
			p_slot.kind == BSParser::DataType::SCRIPT ||
			(p_slot.kind == BSParser::DataType::TYPE_PARAMETER &&
					p_slot.type_parameter_name == SNAME("@Self") && p_slot.type_parameter_bound.size() == 1) ||
			(p_slot.kind == BSParser::DataType::ENUM && !p_slot.is_tagged_union);
}

bool BSCompiler::slot_needs_runtime_descriptor_check(const BSParser::DataType &p_slot) {
	if (p_slot.is_type_handle_annotation || p_slot.is_meta_type) {
		return true;
	}
	if (p_slot.kind == BSParser::DataType::BUILTIN && p_slot.is_nullable) {
		return true;
	}
	return p_slot.kind == BSParser::DataType::BUILTIN &&
			(p_slot.builtin_type == Variant::ARRAY || p_slot.builtin_type == Variant::DICTIONARY) &&
			p_slot.has_container_element_types();
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
