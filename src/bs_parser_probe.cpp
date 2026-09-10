/**************************************************************************/
/*  bs_parser_probe.cpp                                                   */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_parser_probe.h"

#ifdef DEBUG_ENABLED

#include "bs_parser.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

using namespace godot;

namespace barista_script {

void BaristaScriptParserProbe::_bind_methods() {
	ClassDB::bind_method(D_METHOD("can_reference", "self_type", "other_type"), &BaristaScriptParserProbe::can_reference);
}

namespace {

BSParser::DataType data_type_from_dictionary(const Dictionary &p_description) {
	BSParser::DataType type;
	type.kind = (BSParser::DataType::Kind)(int)p_description.get("kind", (int)BSParser::DataType::BUILTIN);
	type.builtin_type = (Variant::Type)(int)p_description.get("builtin_type", (int)Variant::NIL);
	type.is_meta_type = p_description.get("is_meta_type", false);
	type.native_type = p_description.get("native_type", String());
	type.script_path = p_description.get("script_path", String());
	const Array elements = p_description.get("container_element_types", Array());
	for (int i = 0; i < elements.size(); i++) {
		type.container_element_types.push_back(data_type_from_dictionary(elements[i]));
	}
	return type;
}

} // namespace

bool BaristaScriptParserProbe::can_reference(const Dictionary &p_self, const Dictionary &p_other) const {
	const BSParser::DataType self_type = data_type_from_dictionary(p_self);
	const BSParser::DataType other_type = data_type_from_dictionary(p_other);
	return self_type.can_reference(other_type);
}

} // namespace barista_script

#endif // DEBUG_ENABLED
