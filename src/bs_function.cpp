/**************************************************************************/
/*  bs_function.cpp                                                       */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_function.h"

#include <godot_cpp/godot.hpp>

namespace barista_script {

namespace {
// The names come from the same list as the enumerators, so the two cannot drift apart.
const char *const opcode_names[] = {
#define BS_OPCODE_NAME(m_name) "OPCODE_" #m_name,
	BS_OPCODE_LIST(BS_OPCODE_NAME)
#undef BS_OPCODE_NAME
};
static_assert(sizeof(opcode_names) / sizeof(opcode_names[0]) == BSFunction::OPCODE_MAX,
		"every opcode enumerator must have a name");
} // namespace

String BSFunction::get_opcode_name(int p_opcode) {
	if (p_opcode < 0 || p_opcode >= OPCODE_MAX) {
		return vformat("OPCODE_UNKNOWN(%d)", p_opcode);
	}
	return String(opcode_names[p_opcode]);
}

BSFunction::~BSFunction() {
	for (BSFunction *lambda : lambdas) {
		memdelete(lambda);
	}
	lambdas.clear();
}

void bs_report_runtime_error(const String &p_description, const String &p_function, const String &p_file, int p_line) {
	const CharString description = p_description.utf8();
	const CharString function = p_function.utf8();
	const CharString file = p_file.utf8();
	godot::gdextension_interface::print_script_error(description.get_data(), function.get_data(), file.get_data(), p_line, false);
}

} // namespace barista_script
