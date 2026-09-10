/**************************************************************************/
/*  bs_build_info.cpp                                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_build_info.h"
#include "bs_build_info.gen.h"

#include <godot_cpp/classes/json.hpp>

namespace barista_script {

godot::String bs_get_build_info_json() {
	// Consume the complete envelope so optimized libraries retain its framing too.
	const godot::String envelope = godot::String::utf8(BS_BUILD_INFO_ENVELOPE);
	return envelope.substr(BS_BUILD_INFO_OFFSET, BS_BUILD_INFO_LENGTH);
}

godot::Dictionary bs_get_build_info() {
	const godot::Variant parsed = godot::JSON::parse_string(bs_get_build_info_json());
	ERR_FAIL_COND_V(parsed.get_type() != godot::Variant::DICTIONARY, godot::Dictionary());
	godot::Dictionary result = parsed;
	// Godot's JSON parser represents numbers as floats; the protocol schema is an integer.
	result["schema"] = int64_t(result["schema"]);
	return result;
}

} // namespace barista_script
