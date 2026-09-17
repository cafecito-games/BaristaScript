/**************************************************************************/
/*  bs_compiler.cpp                                                       */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_compiler.h"

#include "barista_script.h"
#include "bs_byte_codegen.h"

namespace barista_script {

void BSCompiler::set_error(const String &p_message, const BSParser::Node *p_origin) {
	if (!error.is_empty()) {
		return;
	}
	error = p_message;
	error_line = p_origin != nullptr ? p_origin->start_line : -1;
}

Error BSCompiler::compile(const BSParser *p_parser, BaristaScript *p_script) {
	ERR_FAIL_NULL_V(p_parser, ERR_INVALID_PARAMETER);
	ERR_FAIL_NULL_V(p_script, ERR_INVALID_PARAMETER);
	error = String();
	error_line = -1;

	const BSParser::ClassNode *root = p_parser->get_tree();
	if (root == nullptr) {
		set_error("The script has no class to compile.", nullptr);
		return ERR_COMPILATION_FAILED;
	}
	return compile_class(p_script, root);
}

bool BSCompiler::is_local_or_parameter(const CodeGen &p_codegen, const StringName &p_name) {
	return p_codegen.parameters.has(p_name) || p_codegen.locals.has(p_name);
}

} // namespace barista_script
