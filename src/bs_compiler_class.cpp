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

	// A file that declares a trait, an enum or a tuple declares a type, not a runnable class, and a
	// generic class has no runtime form until its type arguments are reified. None of them has an
	// instance to build, so none of them compiles to one.
	if (p_class->is_trait) {
		set_error("A trait declares a contract, not a runnable class.", p_class);
		return ERR_COMPILATION_FAILED;
	}
	if (p_class->is_enum_file || p_class->is_tuple_file) {
		set_error("A type declaration file has no runnable class.", p_class);
		return ERR_COMPILATION_FAILED;
	}
	if (!p_class->type_parameters.is_empty()) {
		set_error("A generic class has no runtime form until its type arguments are reified.", p_class);
		return ERR_COMPILATION_FAILED;
	}

	if (resolve_base(p_script, p_class) != OK) {
		return ERR_COMPILATION_FAILED;
	}

	// Every member is accounted for before any of them is lowered. A kind that is skipped instead of
	// refused produces a script that compiles, runs, and is missing whatever the skipped member
	// declared -- a signal that cannot be emitted, an enum whose qualified form resolves to nothing.
	for (const BSParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case BSParser::ClassNode::Member::VARIABLE:
			case BSParser::ClassNode::Member::FUNCTION:
			case BSParser::ClassNode::Member::CONSTANT:
				// A constant is folded into the expressions that read it, so it needs no member slot.
				break;
			default:
				set_error(vformat(R"(Cannot run a script with the %s "%s": the runtime does not compile that member kind yet.)",
								  member.get_type_name().to_lower(), member.get_name()),
						member.get_source_node());
				p_script->release_compiled_state();
				return ERR_COMPILATION_FAILED;
		}
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
		if (refuse_unchecked_slot(member_slot_type(member.variable->get_datatype()),
					vformat(R"(the member "%s")", String(name)), member.variable)) {
			p_script->release_compiled_state();
			return ERR_COMPILATION_FAILED;
		}
		if (p_script->member_names.size() > BSFunction::ADDR_MASK) {
			// A member address packs its index beside an address-space tag; see the emitter's bound.
			set_error(vformat("Cannot run a script with more than %d members.", (int)BSFunction::ADDR_MASK),
					member.variable);
			p_script->release_compiled_state();
			return ERR_COMPILATION_FAILED;
		}
		const BSParser::DataType slot = member_slot_type(member.variable->get_datatype());
		p_script->member_indices[name] = p_script->member_names.size();
		p_script->member_names.push_back(name);
		p_script->member_carriers.push_back(
				slot.kind == BSParser::DataType::BUILTIN ? slot.builtin_type : Variant::NIL);
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
		if (member.type != BSParser::ClassNode::Member::VARIABLE) {
			continue;
		}
		const int index = p_script->get_member_index(member.variable->identifier->name);
		if (index < 0) {
			continue;
		}
		const BSCodeGenerator::Address target(BSCodeGenerator::Address::MEMBER, index,
				member_slot_type(member.variable->get_datatype()));
		generator.write_newline(member.variable->start_line);
		if (member.variable->initializer == nullptr) {
			// A member slot starts out nil, which is the right answer for an untyped one. A declared
			// carrier has to start out as that carrier's own empty value instead, the same way a
			// local does, so a never-assigned `int` member reads back as 0.
			if (target.type.kind == BSParser::DataType::BUILTIN && target.type.builtin_type != Variant::NIL) {
				generator.clear_address(target);
			}
			continue;
		}
		const BSCodeGenerator::Address value = parse_expression(codegen, result, member.variable->initializer);
		if (result != OK) {
			return result;
		}
		if (member.variable->use_conversion_assign) {
			// The same rule a local declaration follows: a declared carrier that the initializer does
			// not already have is converted on the way in, or `var ratio: float = 1` would hold an
			// integer and divide like one.
			generator.write_assign_with_conversion(target, value);
		} else {
			generator.write_assign(target, value);
		}
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

	if (refuse_unchecked_slot(member_slot_type(p_function->get_datatype()),
				vformat(R"*(the return value of "%s()")*", String(codegen.function_name)), p_function)) {
		return ERR_COMPILATION_FAILED;
	}
	for (const BSParser::ParameterNode *parameter : p_function->parameters) {
		if (refuse_unchecked_slot(member_slot_type(parameter->get_datatype()),
					vformat(R"(the parameter "%s")", String(parameter->identifier->name)), parameter)) {
			return ERR_COMPILATION_FAILED;
		}
		codegen.add_parameter(parameter->identifier->name, parameter->initializer != nullptr,
				member_slot_type(parameter->get_datatype()));
	}
	if (p_function->rest_parameter != nullptr) {
		const BSParser::ParameterNode *rest = p_function->rest_parameter;
		const BSParser::DataType rest_type = member_slot_type(rest->get_datatype());
		if (refuse_unchecked_slot(rest_type, vformat(R"(the rest parameter "%s")", String(rest->identifier->name)), rest)) {
			return ERR_COMPILATION_FAILED;
		}
		const uint32_t address = generator.add_rest_parameter(rest->identifier->name, rest_type, rest_type);
		codegen.parameters[rest->identifier->name] =
				BSCodeGenerator::Address(BSCodeGenerator::Address::FUNCTION_PARAMETER, address, rest_type);
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
