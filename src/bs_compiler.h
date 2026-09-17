/**************************************************************************/
/*  bs_compiler.h                                                         */
/*                                                                        */
/*  The compile driver: walks an analyzed tree and emits through a        */
/*  BSCodeGenerator. Provenance: fs_compiler.cpp:7138 compile(), :5719    */
/*  _prepare_compilation and :6432-6612 _compile_class @ c9d5e35.         */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_analyzer.h"
#include "bs_codegen.h"
#include "bs_parser.h"
#include "bs_platform.h"

namespace barista_script {

class BaristaScript;

/**
 * Turns an analyzed parse tree into the compiled functions a script runs.
 *
 * The driver never guesses. A construct it cannot lower, or a back end that refuses one, stops the
 * compilation with a message naming the construct, and the script stays invalid: `_can_instantiate`
 * is false and `_instance_create` yields nothing, so a half-built function is never reachable.
 *
 * The walk is split by family across translation units so the families can be widened
 * independently: expressions, statements, type lowering, class members, lambdas and conformance.
 */
class BSCompiler {
public:
	/** One function's compilation scope: its emitter, its parameters and its visible locals. */
	struct CodeGen {
		BaristaScript *script = nullptr;
		const BSParser::ClassNode *class_node = nullptr;
		const BSParser::FunctionNode *function_node = nullptr;
		StringName function_name;
		BSCodeGenerator *generator = nullptr;

		HashMap<StringName, BSCodeGenerator::Address> parameters;
		HashMap<StringName, BSCodeGenerator::Address> locals;
		List<HashMap<StringName, BSCodeGenerator::Address>> locals_stack;

		void start_block() {
			locals_stack.push_back(locals);
			generator->start_block();
		}

		void end_block() {
			locals = locals_stack.back()->get();
			locals_stack.pop_back();
			generator->end_block();
		}

		BSCodeGenerator::Address add_parameter(const StringName &p_name, bool p_is_optional, const BSParser::DataType &p_type) {
			const uint32_t address = generator->add_parameter(p_name, p_is_optional, p_type, p_type);
			parameters[p_name] = BSCodeGenerator::Address(BSCodeGenerator::Address::FUNCTION_PARAMETER, address, p_type);
			return parameters[p_name];
		}

		BSCodeGenerator::Address add_local(const StringName &p_name, const BSParser::DataType &p_type) {
			const uint32_t address = generator->add_local(p_name, p_type);
			locals[p_name] = BSCodeGenerator::Address(BSCodeGenerator::Address::LOCAL_VARIABLE, address, p_type);
			return locals[p_name];
		}

		BSCodeGenerator::Address add_local_constant(const StringName &p_name, const Variant &p_value) {
			const uint32_t address = generator->add_local_constant(p_name, p_value);
			locals[p_name] = BSCodeGenerator::Address(BSCodeGenerator::Address::CONSTANT, address);
			return locals[p_name];
		}

		BSCodeGenerator::Address add_temporary(const BSParser::DataType &p_type) {
			return BSCodeGenerator::Address(BSCodeGenerator::Address::TEMPORARY, generator->add_temporary(p_type), p_type);
		}

		BSCodeGenerator::Address add_constant(const Variant &p_value) {
			return BSCodeGenerator::Address(BSCodeGenerator::Address::CONSTANT, generator->add_or_get_constant(p_value));
		}
	};

	/**
	 * Compiles `p_root`'s head class into `p_script`, replacing whatever it held.
	 *
	 * The script is left invalid and unchanged in every observable way when the compilation fails.
	 */
	Error compile(const BSParser *p_parser, BaristaScript *p_script);

	const String &get_error() const { return error; }
	int get_error_line() const { return error_line; }

private:
	String error;
	int error_line = -1;

	void set_error(const String &p_message, const BSParser::Node *p_origin);

	// Class members (glue lane).
	Error compile_class(BaristaScript *p_script, const BSParser::ClassNode *p_class);
	Error resolve_base(BaristaScript *p_script, const BSParser::ClassNode *p_class);
	Error compile_implicit_initializer(BaristaScript *p_script, const BSParser::ClassNode *p_class);
	Error compile_function(BaristaScript *p_script, const BSParser::ClassNode *p_class, const BSParser::FunctionNode *p_function, BSFunction **r_function);

	// Statements (core lane).
	Error parse_block(CodeGen &p_codegen, const BSParser::SuiteNode *p_block, bool p_add_locals = true);
	Error parse_statement(CodeGen &p_codegen, const BSParser::SuiteNode *p_block, const BSParser::Node *p_statement);

	// Expressions (core lane).
	BSCodeGenerator::Address parse_expression(CodeGen &p_codegen, Error &r_error, const BSParser::ExpressionNode *p_expression, bool p_discard_result = false);
	BSCodeGenerator::Address parse_call(CodeGen &p_codegen, Error &r_error, const BSParser::CallNode *p_call, bool p_discard_result);
	BSCodeGenerator::Address parse_assignment(CodeGen &p_codegen, Error &r_error, const BSParser::AssignmentNode *p_assignment);
	BSCodeGenerator::Address parse_identifier(CodeGen &p_codegen, Error &r_error, const BSParser::IdentifierNode *p_identifier);

	// Type lowering (core lane).
	static bool is_local_or_parameter(const CodeGen &p_codegen, const StringName &p_name);
	static BSParser::DataType member_slot_type(const BSParser::DataType &p_type);
};

} // namespace barista_script
