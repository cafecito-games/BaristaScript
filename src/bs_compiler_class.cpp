/**************************************************************************/
/*  bs_compiler_class.cpp                                                 */
/*                                                                        */
/*  Class-level compilation: the base, the member layout, the implicit    */
/*  initializer and each declared function. Provenance:                   */
/*  fs_compiler.cpp:6432-6612 _compile_class and foundry_script.cpp:1059  */
/*  _super_implicit_constructor @ c9d5e35.                                */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_byte_codegen.h"
#include "bs_compiler.h"

namespace barista_script {

namespace {

/** The name of the synthesized function that runs a class's member initializers. */
const char *IMPLICIT_INITIALIZER_NAME = "@implicit_new";

bool base_is_gdscript(const BSParser::DataType &p_base) {
	if (p_base.script_type.is_valid() && p_base.script_type->get_class() == StringName("GDScript")) {
		return true;
	}
	// A base named by path is judged by the file it names. The analyzer will happily read a `.gd`
	// file as BaristaScript source when its text happens to parse, so the extension is what decides,
	// not whichever shape the analyzer settled on.
	return p_base.script_path.get_extension().to_lower() == "gd";
}

} // namespace

Error BSCompiler::resolve_base(BaristaScript *p_script, const BSParser::ClassNode *p_class) {
	const BSParser::DataType &base = p_class->base_type;

	if (base_is_gdscript(base)) {
		// Stock Godot has no cross-language script inheritance for instances, so an instance whose
		// base is a GDScript class cannot be built at all. Refusing here names the base and the
		// reason; the alternative is an instance that exists but cannot answer for half of itself.
		set_error(vformat(R"(Cannot run a script whose base is the GDScript class "%s": an object holds one script instance, and Godot has no path by which a GDExtension script instance delegates to a GDScript one.)",
						  base.script_path.is_empty() ? String("<unnamed>") : base.script_path),
				p_class);
		return ERR_COMPILATION_FAILED;
	}

	switch (base.kind) {
		case BSParser::DataType::NATIVE:
			p_script->instance_base_type = base.native_type;
			break;
		case BSParser::DataType::CLASS:
		case BSParser::DataType::SCRIPT:
			set_error(vformat(R"(Cannot run a script that extends "%s": a script base is resolved by the cross-file compilation path.)",
							  base.to_string()),
					p_class);
			return ERR_COMPILATION_FAILED;
		default:
			// A class with no written base is a RefCounted, the same default the engine gives a script
			// resource with no `extends`.
			p_script->instance_base_type = SNAME("RefCounted");
			break;
	}
	if (p_script->instance_base_type == StringName()) {
		p_script->instance_base_type = SNAME("RefCounted");
	}
	return OK;
}

Error BSCompiler::compile_class(BaristaScript *p_script, const BSParser::ClassNode *p_class) {
	p_script->release_compiled_state();

	if (resolve_base(p_script, p_class) != OK) {
		return ERR_COMPILATION_FAILED;
	}

	for (const BSParser::ClassNode::Member &member : p_class->members) {
		if (member.type != BSParser::ClassNode::Member::VARIABLE) {
			continue;
		}
		if (member.variable->is_static) {
			set_error(vformat(R"(Cannot run a script with the static variable "%s".)", member.get_name()), member.variable);
			p_script->release_compiled_state();
			return ERR_COMPILATION_FAILED;
		}
		const StringName name = member.variable->identifier->name;
		p_script->member_indices[name] = p_script->member_names.size();
		p_script->member_names.push_back(name);
	}

	if (compile_implicit_initializer(p_script, p_class) != OK) {
		p_script->release_compiled_state();
		return ERR_COMPILATION_FAILED;
	}

	for (const BSParser::ClassNode::Member &member : p_class->members) {
		if (member.type != BSParser::ClassNode::Member::FUNCTION) {
			continue;
		}
		const BSParser::FunctionNode *function_node = member.function;
		if (!function_node->has_body || function_node->body == nullptr) {
			continue;
		}
		BSFunction *function = nullptr;
		if (compile_function(p_script, p_class, function_node, &function) != OK) {
			p_script->release_compiled_state();
			return ERR_COMPILATION_FAILED;
		}
		p_script->member_functions[function_node->identifier->name] = function;
	}

	p_script->valid = true;
	p_script->compile_error = String();
	return OK;
}

Error BSCompiler::compile_implicit_initializer(BaristaScript *p_script, const BSParser::ClassNode *p_class) {
	BSByteCodeGenerator generator;
	CodeGen codegen;
	codegen.generator = &generator;
	codegen.script = p_script;
	codegen.class_node = p_class;
	codegen.function_name = StringName(IMPLICIT_INITIALIZER_NAME);

	generator.write_start(p_script, codegen.function_name, false, Variant(), BSParser::DataType());
	generator.set_initial_line(p_class->start_line);
	generator.start_parameters();
	generator.end_parameters();

	Error result = OK;
	for (const BSParser::ClassNode::Member &member : p_class->members) {
		if (member.type != BSParser::ClassNode::Member::VARIABLE || member.variable->initializer == nullptr) {
			continue;
		}
		const int index = p_script->get_member_index(member.variable->identifier->name);
		if (index < 0) {
			continue;
		}
		generator.write_newline(member.variable->start_line);
		const BSCodeGenerator::Address target(BSCodeGenerator::Address::MEMBER, index,
				member_slot_type(member.variable->get_datatype()));
		const BSCodeGenerator::Address value = parse_expression(codegen, result, member.variable->initializer);
		if (result != OK) {
			return result;
		}
		generator.write_assign(target, value);
		if (value.mode == BSCodeGenerator::Address::TEMPORARY) {
			generator.pop_temporary();
		}
		generator.clear_temporaries();
	}
	generator.write_return(codegen.add_constant(Variant()));

	BSFunction *function = generator.write_end();
	if (function == nullptr) {
		set_error(vformat("The runtime cannot compile %s in a member initializer.", generator.get_error()), p_class);
		return ERR_COMPILATION_FAILED;
	}
	p_script->implicit_initializer = function;
	return OK;
}

Error BSCompiler::compile_function(BaristaScript *p_script, const BSParser::ClassNode *p_class, const BSParser::FunctionNode *p_function, BSFunction **r_function) {
	if (p_function->is_coroutine || p_function->is_declared_async) {
		set_error(vformat(R"*(Cannot run the coroutine "%s()": suspension is not implemented.)*",
						  String(p_function->identifier->name)),
				p_function);
		return ERR_COMPILATION_FAILED;
	}
	if (p_function->is_vararg()) {
		set_error(vformat(R"*(Cannot run "%s()": a rest parameter is not implemented.)*",
						  String(p_function->identifier->name)),
				p_function);
		return ERR_COMPILATION_FAILED;
	}

	BSByteCodeGenerator generator;
	CodeGen codegen;
	codegen.generator = &generator;
	codegen.script = p_script;
	codegen.class_node = p_class;
	codegen.function_node = p_function;
	codegen.function_name = p_function->identifier->name;

	generator.write_start(p_script, codegen.function_name, p_function->is_static, p_function->rpc_config,
			member_slot_type(p_function->get_datatype()));
	generator.set_initial_line(p_function->start_line);
	generator.set_signature(vformat("%s::%s", p_script->get_path(), String(codegen.function_name)));

	for (const BSParser::ParameterNode *parameter : p_function->parameters) {
		codegen.add_parameter(parameter->identifier->name, parameter->initializer != nullptr,
				member_slot_type(parameter->get_datatype()));
	}

	generator.start_parameters();
	Error result = OK;
	for (const BSParser::ParameterNode *parameter : p_function->parameters) {
		if (parameter->initializer == nullptr) {
			continue;
		}
		const BSCodeGenerator::Address value = parse_expression(codegen, result, parameter->initializer);
		if (result != OK) {
			return result;
		}
		generator.write_assign_default_parameter(codegen.parameters[parameter->identifier->name], value,
				parameter->use_conversion_assign);
		if (value.mode == BSCodeGenerator::Address::TEMPORARY) {
			generator.pop_temporary();
		}
	}
	generator.end_parameters();

	result = parse_block(codegen, p_function->body);
	if (result != OK) {
		return result;
	}
	generator.write_return(codegen.add_constant(Variant()));

	BSFunction *function = generator.write_end();
	if (function == nullptr) {
		set_error(vformat(R"*(The runtime cannot compile %s in "%s()".)*", generator.get_error(),
						  String(codegen.function_name)),
				p_function);
		return ERR_COMPILATION_FAILED;
	}
	*r_function = function;
	return OK;
}

} // namespace barista_script
