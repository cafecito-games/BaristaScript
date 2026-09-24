/**************************************************************************/
/*  runtime_function_access.h                                             */
/*                                                                        */
/*  The one definition of BSFunction's test accessor.                     */
/*                                                                        */
/*  `BSFunction` keeps its tables private and its friend list is frozen   */
/*  by #243, so every suite that needs to build a probe function or read  */
/*  a compiled one goes through the single friend the header already      */
/*  names. Defining that friend in two translation units would be two     */
/*  different types with one name, so it lives here and is included.      */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_function.h"

namespace barista_script {

struct BSFunctionTestAccess {
	/**
	 * Assembles a function the emitter refuses to write.
	 *
	 * Every reserved opcode is unreachable from source by construction, so the only way to prove the
	 * runtime refuses one by name is to hand it a function that contains one.
	 */
	static BSFunction *make_single_opcode_function(BSFunction::Opcode p_opcode) {
		BSFunction *function = memnew(BSFunction);
		function->name = SNAME("probe");
		function->source = "res://runtime_probe.barista";
		function->stack_size = BSFunction::FIXED_ADDRESSES_MAX;
		function->instruction_arguments_size = 1;
		function->code.push_back(p_opcode);
		function->code.push_back(BSFunction::OPCODE_END);
		return function;
	}

	/** A single `super.<name>()` whose result is discarded, for the handler's own resolution path. */
	static BSFunction *make_super_call_function(const StringName &p_name) {
		BSFunction *function = memnew(BSFunction);
		function->name = SNAME("probe_super");
		function->source = "res://runtime_probe.barista";
		function->stack_size = BSFunction::FIXED_ADDRESSES_MAX;
		function->instruction_arguments_size = 1;
		function->global_names.push_back(p_name);
		function->code.push_back(BSFunction::OPCODE_CALL_SELF_BASE);
		function->code.push_back(1);
		function->code.push_back(BSFunction::ADDR_NIL);
		function->code.push_back(0);
		function->code.push_back(0);
		function->code.push_back(BSFunction::OPCODE_END);
		return function;
	}

	/** The emitted instruction words, for a case that compares two compilations of one source. */
	static const Vector<int> &code_of(const BSFunction *p_function) { return p_function->code; }

	/** How many constants the compilation interned, which pooling order would change. */
	static int constant_count(const BSFunction *p_function) { return p_function->constants.size(); }
};

} // namespace barista_script
