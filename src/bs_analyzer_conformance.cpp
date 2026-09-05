/**************************************************************************/
/*  bs_analyzer_conformance.cpp                                           */
/*                                                                        */
/*  Hard fork of Foundry `modules/foundry_script/fs_analyzer_conformance   */
/*  .cpp` @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6 (plus               */
/*  validate_trait_requirements / find_trait_implementation /             */
/*  validate_trait_method_signature from fs_analyzer.cpp). FS* -> BS*;   */
/*  engine contact through bs_platform.h.                                 */
/*  Non-generic trait method signature matching (async/static/arity/     */
/*  params/returns/rest + Self reify + MethodInfo). ConformanceVisibility*/
/*  can_see BFS. resolve_conformances publishes validated entries via    */
/*  try_replace_file_conformances under ScopedInFlightReplacement.       */
/*  ClassTraitBinding / RecordedTypeArgument / witness maps / chain      */
/*  coherence remain residual under #60.                                 */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"

#include "barista_script.h"
#include "barista_script_language.h"
#include "bs_cache.h"
#include "bs_conformance_registry.h"
#include "bs_native_db.h"
#include "bs_platform.h"
#include "bs_trait_utils.h"
#include "bs_type.h"

namespace barista_script {

namespace {

const BSParser::Node *_trait_use_source(const BSParser::ClassNode::TraitUse &p_trait_use,
		const BSParser::ClassNode *p_owner) {
	if (!p_trait_use.name.is_empty()) {
		return p_trait_use.name[0];
	}
	return p_owner;
}

const BSParser::Node *_trait_requirement_source(const BSParser::ClassNode *p_class,
		BSParser::ClassNode *p_trait) {
	for (const BSParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		if (trait_use.resolved_trait == p_trait) {
			return _trait_use_source(trait_use, p_class);
		}
		if (trait_use.resolved_trait != nullptr) {
			for (int i = 0; i < trait_use.resolved_trait->resolved_traits.size(); i++) {
				if (trait_use.resolved_trait->resolved_traits[i] == p_trait) {
					return _trait_use_source(trait_use, p_class);
				}
			}
		}
	}
	return p_class->identifier != nullptr ? static_cast<const BSParser::Node *>(p_class->identifier) : p_class;
}

BSParser::ClassNode *_find_local_class_by_name(BSParser::ClassNode *p_root, const StringName &p_name) {
	if (p_root == nullptr || p_name == StringName()) {
		return nullptr;
	}
	if (p_root->identifier != nullptr && p_root->identifier->name == p_name) {
		return p_root;
	}
	for (int i = 0; i < p_root->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_root->members[i];
		if (member.type != BSParser::ClassNode::Member::CLASS || member.m_class == nullptr) {
			continue;
		}
		BSParser::ClassNode *found = _find_local_class_by_name(member.m_class, p_name);
		if (found != nullptr) {
			return found;
		}
	}
	return nullptr;
}

BSParser::ClassNode *_find_local_trait_by_name(BSParser::ClassNode *p_owner, const String &p_name) {
	for (BSParser::ClassNode *scope = p_owner; scope != nullptr; scope = scope->outer) {
		if (!scope->has_member(StringName(p_name))) {
			continue;
		}
		const BSParser::ClassNode::Member member = scope->get_member(StringName(p_name));
		if (member.type == BSParser::ClassNode::Member::CLASS && member.m_class != nullptr && member.m_class->is_trait) {
			return member.m_class;
		}
	}
	return nullptr;
}

BSParser::DataType _self_type_for_class(BSParser::ClassNode *p_class) {
	BSParser::DataType self_type;
	if (p_class == nullptr) {
		return self_type;
	}
	self_type = p_class->get_datatype();
	self_type.is_meta_type = false;
	self_type.type_arguments.clear();
	if (!self_type.is_set() || self_type.is_variant()) {
		self_type.kind = BSParser::DataType::CLASS;
		self_type.class_type = p_class;
		self_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		self_type.builtin_type = Variant::OBJECT;
		self_type.native_type = p_class->base_type.native_type;
		self_type.script_path = p_class->fqcn.begins_with("res://") ? p_class->fqcn : String();
	}
	return self_type;
}

BSParser::DataType _substitute_type_parameters_and_self(const BSParser::DataType &p_type,
		const HashMap<StringName, BSParser::DataType> &p_bindings, const BSParser::DataType &p_self_type) {
	BSParser::DataType substituted = BSParser::DataType::substitute(p_type, p_bindings);
	if (!p_self_type.is_set()) {
		return substituted;
	}
	HashMap<StringName, BSParser::DataType> self_bindings;
	self_bindings.insert(SNAME("@Self"), p_self_type);
	return BSParser::DataType::substitute(substituted, self_bindings);
}

} // namespace

void BSAnalyzer::resolve_function_signature_in_class(BSParser::FunctionNode *p_function, BSParser::ClassNode *p_class) {
	if (p_function == nullptr) {
		return;
	}
	const StringName function_name = p_function->identifier != nullptr ? p_function->identifier->name : StringName();
	if (p_function->get_datatype().is_resolving()) {
		push_error(vformat(R"(Could not resolve function "%s": Cyclic reference.)", function_name), p_function);
		return;
	}
	if (p_function->resolved_signature) {
		return;
	}
	p_function->resolved_signature = true;

	BSParser::ClassNode *previous_class = current_class;
	current_class = p_class;

	BSParser::DataType resolving_datatype;
	resolving_datatype.kind = BSParser::DataType::RESOLVING;
	p_function->set_datatype(resolving_datatype);

	if (p_function->return_type != nullptr) {
		p_function->set_datatype(datatype_from_type_node(p_function->return_type));
	} else {
		BSParser::DataType return_type;
		return_type.type_source = BSParser::DataType::INFERRED;
		return_type.kind = BSParser::DataType::VARIANT;
		p_function->set_datatype(return_type);
	}

	MethodInfo method_info;
	method_info.name = function_name;
	if (p_function->is_static) {
		method_info.flags |= METHOD_FLAG_STATIC;
	}
	// Stock Godot MethodInfo has no METHOD_FLAG_ASYNC; async-ness lives on FunctionNode / DataType.
	if (p_function->is_vararg()) {
		method_info.flags |= METHOD_FLAG_VARARG;
	}

	p_function->default_arg_values.clear();
	for (int p = 0; p < p_function->parameters.size(); p++) {
		BSParser::ParameterNode *parameter = p_function->parameters[p];
		if (parameter == nullptr) {
			continue;
		}
		if (parameter->datatype_specifier != nullptr) {
			parameter->set_datatype(datatype_from_type_node(parameter->datatype_specifier));
		}
		const StringName parameter_name = parameter->identifier != nullptr ? parameter->identifier->name : StringName();
		method_info.arguments.push_back(parameter->get_datatype().to_property_info(parameter_name));
		if (parameter->initializer != nullptr) {
			reduce_expression(parameter->initializer);
			// Foundry assignable path: parameter defaults qualify contextual `.Case` against the
			// parameter's declared type (during signature resolve, before the body sweep).
			qualify_contextual_enum_case_consumer(parameter->initializer, parameter->get_datatype());
			mark_coroutine_handle_capture(parameter->initializer, parameter->get_datatype());
			if (parameter->initializer->is_constant) {
				p_function->default_arg_values.push_back(parameter->initializer->reduced_value);
			} else {
				p_function->default_arg_values.push_back(Variant());
			}
		}
	}
	if (p_function->rest_parameter != nullptr && p_function->rest_parameter->datatype_specifier != nullptr) {
		p_function->rest_parameter->set_datatype(datatype_from_type_node(p_function->rest_parameter->datatype_specifier));
	}
	method_info.default_arguments.clear();
	for (int i = 0; i < p_function->default_arg_values.size(); i++) {
		method_info.default_arguments.push_back(p_function->default_arg_values[i]);
	}
	method_info.return_val = p_function->get_datatype().to_property_info("");
	p_function->info = method_info;
	current_class = previous_class;
}

bool BSAnalyzer::find_trait_implementation(BSParser::ClassNode *p_class, const StringName &p_function_name,
		TraitMethodImplementation &r_implementation) {
	r_implementation = TraitMethodImplementation();
	BSParser::ClassNode *current_class = p_class;
	HashSet<BSParser::ClassNode *> visited_classes;
	while (current_class != nullptr) {
		if (visited_classes.has(current_class)) {
			break;
		}
		visited_classes.insert(current_class);

		if (current_class->is_builtin_conformance_shim) {
			// Builtin MethodInfo surface remains follow-up under #60 when godot-cpp exposes it.
			return false;
		}

		if (current_class->has_function(p_function_name)) {
			BSParser::FunctionNode *function = current_class->get_member(p_function_name).function;
			if (function != nullptr && !function->is_abstract) {
				r_implementation.function = function;
				r_implementation.owner_class = current_class;
				return true;
			}
		}

		if (current_class->base_type.kind == BSParser::DataType::CLASS && current_class->base_type.class_type != nullptr) {
			current_class = current_class->base_type.class_type;
			continue;
		}
		if (current_class->base_type.kind == BSParser::DataType::SCRIPT && !current_class->base_type.script_path.is_empty()) {
			Error err = OK;
			Ref<BSParserRef> base_ref = BSCache::get_parser(current_class->base_type.script_path, BSParserRef::INTERFACE_SOLVED, err,
					parser != nullptr ? parser->script_path : String());
			if (base_ref.is_valid() && err == OK && base_ref->get_parser() != nullptr) {
				current_class = base_ref->get_parser()->get_tree();
				continue;
			}
			current_class = nullptr;
			continue;
		}

		const StringName native_type = current_class->base_type.native_type != StringName()
				? current_class->base_type.native_type
				: (current_class->base_type.kind == BSParser::DataType::NATIVE ? current_class->base_type.native_type : StringName());
		if (native_type != StringName()) {
			MethodInfo info;
			if (BSNativeDB::get_method_info(native_type, p_function_name, &info)) {
				r_implementation.method_info = info;
				r_implementation.method_info_source = vformat(R"(Implementation comes from native class "%s".)", native_type);
				r_implementation.has_method_info = true;
				return true;
			}
		}
		current_class = nullptr;
	}

	return false;
}

void BSAnalyzer::validate_trait_requirements(BSParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->is_trait || p_class->is_abstract) {
		return;
	}

	// Foundry validate_trait_requirements @ c9d5e35: only abstract trait methods are requirements;
	// concrete trait methods do not satisfy another trait's abstracts.
	HashSet<StringName> missing_trait_methods;
	for (int t = 0; t < p_class->resolved_traits.size(); t++) {
		BSParser::ClassNode *trait = p_class->resolved_traits[t];
		if (trait == nullptr) {
			continue;
		}
		analyze_class_interface(trait);
		for (int i = 0; i < trait->members.size(); i++) {
			const BSParser::ClassNode::Member &member = trait->members[i];
			if (member.type != BSParser::ClassNode::Member::FUNCTION ||
					member.function == nullptr || !member.function->is_abstract ||
					member.function->identifier == nullptr) {
				continue;
			}

			TraitMethodImplementation implementation;
			if (!find_trait_implementation(p_class, member.function->identifier->name, implementation)) {
				const StringName function_name = member.function->identifier->name;
				if (!missing_trait_methods.has(function_name)) {
					missing_trait_methods.insert(function_name);
					push_error(vformat(R"*(Class "%s" must implement trait method "%s.%s()".)*",
									   bs_class_or_trait_diagnostic_name(p_class), bs_class_or_trait_diagnostic_name(trait),
									   function_name),
							_trait_requirement_source(p_class, trait));
				}
				continue;
			}
			validate_trait_method_signature(trait, p_class, member.function, implementation);
		}
	}

	for (int i = 0; i < p_class->members.size(); i++) {
		if (p_class->members[i].type == BSParser::ClassNode::Member::CLASS) {
			validate_trait_requirements(p_class->members[i].m_class);
		}
	}
}

bool BSAnalyzer::validate_trait_method_info_signature(BSParser::ClassNode *p_trait,
		BSParser::ClassNode *p_implementing_class, BSParser::FunctionNode *p_required_function,
		const TraitMethodImplementation &p_implementation,
		const HashMap<StringName, BSParser::DataType> &p_trait_substitution) {
	if (p_required_function == nullptr || p_required_function->identifier == nullptr) {
		return false;
	}
	const StringName function_name = p_required_function->identifier->name;
	const String trait_method_name = bs_class_or_trait_diagnostic_name(p_trait) + "." + String(function_name) + "()";
	const BSParser::DataType implementation_self_type = _self_type_for_class(p_implementing_class);

	if (!p_required_function->type_parameters.is_empty()) {
		String message = vformat(R"*(The function "%s()" signature does not match required generic trait method "%s".)*",
				function_name, trait_method_name);
		if (!p_implementation.method_info_source.is_empty()) {
			message += " " + p_implementation.method_info_source;
		}
		push_error(message, p_required_function);
		return false;
	}

	const bool required_is_coroutine = p_required_function->is_coroutine;
	// Stock Godot MethodInfo has no METHOD_FLAG_ASYNC; Foundry extends MethodFlags with bit 256.
	// Native DB entries therefore never report async — treat MethodInfo implementations as sync.
	const bool implementation_is_coroutine = false;
	if (required_is_coroutine != implementation_is_coroutine) {
		String message;
		if (required_is_coroutine) {
			message = vformat(R"*(The function "%s()" must be async because it implements async trait method "%s".)*",
					function_name, trait_method_name);
		} else {
			message = vformat(R"*(The function "%s()" cannot be async because it implements synchronous trait method "%s".)*",
					function_name, trait_method_name);
		}
		if (!p_implementation.method_info_source.is_empty()) {
			message += " " + p_implementation.method_info_source;
		}
		push_error(message, p_required_function);
		return false;
	}

	bool valid = (p_required_function->is_static == ((p_implementation.method_info.flags & METHOD_FLAG_STATIC) != 0));

	BSTypeCompatibility::Options options;
	options.strict_null = strict_null_checks;
	options.strict_dynamic = strict_dynamic_checks;

	if (p_required_function->return_type != nullptr) {
		const BSParser::DataType required_return_type = _substitute_type_parameters_and_self(
				p_required_function->get_datatype(), p_trait_substitution, implementation_self_type);
		const BSParser::DataType implementation_return_type = type_from_property(p_implementation.method_info.return_val, false, false);
		if (implementation_return_type.is_variant()) {
			valid = valid && required_return_type.is_variant();
		} else if (required_return_type.is_set() && implementation_return_type.is_set()) {
			valid = valid && BSTypeCompatibility::check(required_return_type, implementation_return_type, options).compatible;
		}
	}

	const int required_min_argc = p_required_function->parameters.size() - p_required_function->default_arg_values.size();
	const int required_max_argc = p_required_function->is_vararg() ? INT_MAX : p_required_function->parameters.size();
	const int implementation_min_argc = p_implementation.method_info.arguments.size() - p_implementation.method_info.default_arguments.size();
	const int implementation_max_argc = (p_implementation.method_info.flags & METHOD_FLAG_VARARG) ? INT_MAX : p_implementation.method_info.arguments.size();
	valid = valid && implementation_min_argc <= required_min_argc && required_max_argc <= implementation_max_argc;

	if (valid) {
		for (int i = 0; i < p_required_function->parameters.size() && i < p_implementation.method_info.arguments.size(); i++) {
			const BSParser::DataType required_parameter_type = _substitute_type_parameters_and_self(
					p_required_function->parameters[i]->get_datatype(), p_trait_substitution, implementation_self_type);
			const BSParser::DataType implementation_parameter_type = type_from_property(p_implementation.method_info.arguments[i], true, false);
			if (required_parameter_type.is_variant() && required_parameter_type.is_hard_type()) {
				valid = valid && implementation_parameter_type.is_variant();
			} else if (implementation_parameter_type.is_set() && required_parameter_type.is_set()) {
				valid = valid && BSTypeCompatibility::check(implementation_parameter_type, required_parameter_type, options).compatible;
			}
		}
	}

	if (!valid) {
		String message = vformat(R"*(The native function "%s()" signature does not match required trait method "%s".)*",
				function_name, trait_method_name);
		if (!p_implementation.method_info_source.is_empty()) {
			message += " " + p_implementation.method_info_source;
		}
		push_error(message, p_required_function);
		return false;
	}

	return true;
}

bool BSAnalyzer::validate_trait_method_signature(BSParser::ClassNode *p_trait,
		BSParser::ClassNode *p_implementing_class, BSParser::FunctionNode *p_required_function,
		const TraitMethodImplementation &p_implementation,
		const HashMap<StringName, BSParser::DataType> &p_trait_substitution) {
	if (p_required_function == nullptr || p_required_function->identifier == nullptr) {
		return false;
	}
	resolve_function_signature_in_class(p_required_function, p_trait);
	if (p_implementation.has_method_info) {
		return validate_trait_method_info_signature(p_trait, p_implementing_class, p_required_function, p_implementation,
				p_trait_substitution);
	}

	BSParser::FunctionNode *implementation_function = p_implementation.function;
	if (implementation_function == nullptr) {
		return false;
	}
	resolve_function_signature_in_class(implementation_function, p_implementation.owner_class);

	const StringName function_name = p_required_function->identifier->name;
	const String trait_method_name = bs_class_or_trait_diagnostic_name(p_trait) + "." + String(function_name) + "()";
	const BSParser::DataType implementation_self_type = _self_type_for_class(p_implementing_class);

	if (p_required_function->is_coroutine != implementation_function->is_coroutine) {
		if (p_required_function->is_coroutine) {
			push_error(vformat(R"*(The function "%s()" must be async because it implements async trait method "%s".)*",
							   function_name, trait_method_name),
					implementation_function);
		} else {
			push_error(vformat(R"*(The function "%s()" cannot be async because it implements synchronous trait method "%s".)*",
							   function_name, trait_method_name),
					implementation_function);
		}
		return false;
	}

	bool valid = p_required_function->is_static == implementation_function->is_static;

	// Generic method alpha-equivalence remains M5 / #60 follow-up; reject mismatched arity of type
	// parameters so a non-generic implementer cannot silently satisfy a generic requirement.
	if (p_required_function->type_parameters.size() != implementation_function->type_parameters.size()) {
		valid = false;
	}

	BSTypeCompatibility::Options options;
	options.strict_null = strict_null_checks;
	options.strict_dynamic = strict_dynamic_checks;

	if (p_required_function->return_type != nullptr) {
		const BSParser::DataType required_return_type = _substitute_type_parameters_and_self(
				p_required_function->get_datatype(), p_trait_substitution, implementation_self_type);
		const BSParser::DataType implementation_return_type = _substitute_type_parameters_and_self(
				implementation_function->get_datatype(), HashMap<StringName, BSParser::DataType>(), implementation_self_type);
		if (implementation_return_type.is_variant()) {
			valid = valid && required_return_type.is_variant();
		} else if (implementation_return_type.kind == BSParser::DataType::BUILTIN &&
				implementation_return_type.builtin_type == Variant::NIL) {
			// Foundry pin c9d5e35: void/`NIL` impl rejects any hard non-void required return (no
			// extra `!is_variant()` gate — Variant is already soft).
			if (required_return_type.is_hard_type() &&
					!(required_return_type.kind == BSParser::DataType::BUILTIN &&
							required_return_type.builtin_type == Variant::NIL)) {
				valid = false;
			}
		} else if (required_return_type.is_set() && implementation_return_type.is_set()) {
			valid = valid && BSTypeCompatibility::check(required_return_type, implementation_return_type, options).compatible;
		}
	}

	const int required_min_argc = p_required_function->parameters.size() - p_required_function->default_arg_values.size();
	const int required_max_argc = p_required_function->is_vararg() ? INT_MAX : p_required_function->parameters.size();
	const int implementation_min_argc = implementation_function->parameters.size() - implementation_function->default_arg_values.size();
	const int implementation_max_argc = implementation_function->is_vararg() ? INT_MAX : implementation_function->parameters.size();
	valid = valid && implementation_min_argc <= required_min_argc && required_max_argc <= implementation_max_argc;

	if (valid) {
		for (int i = 0; i < p_required_function->parameters.size() && i < implementation_function->parameters.size(); i++) {
			const BSParser::DataType required_parameter_type = _substitute_type_parameters_and_self(
					p_required_function->parameters[i]->get_datatype(), p_trait_substitution, implementation_self_type);
			const BSParser::DataType implementation_parameter_type = _substitute_type_parameters_and_self(
					implementation_function->parameters[i]->get_datatype(), HashMap<StringName, BSParser::DataType>(),
					implementation_self_type);
			if (required_parameter_type.is_variant() && required_parameter_type.is_hard_type()) {
				valid = valid && implementation_parameter_type.is_variant();
			} else if (implementation_parameter_type.is_set() && required_parameter_type.is_set()) {
				valid = valid && BSTypeCompatibility::check(implementation_parameter_type, required_parameter_type, options).compatible;
			}
		}
	}

	{
		const BSParser::DataType required_rest_type = p_required_function->is_vararg()
				? _substitute_type_parameters_and_self(p_required_function->rest_parameter->get_datatype(),
						  p_trait_substitution, implementation_self_type)
				: BSParser::DataType();
		const BSParser::DataType implementation_rest_type = implementation_function->is_vararg()
				? _substitute_type_parameters_and_self(implementation_function->rest_parameter->get_datatype(),
						  HashMap<StringName, BSParser::DataType>(), implementation_self_type)
				: BSParser::DataType();
		valid = valid && BSTypeCompatibility::rest_parameter_accepts_required_arguments(implementation_function->is_vararg() ? &implementation_rest_type : nullptr, p_required_function->is_vararg() ? &required_rest_type : nullptr, strict_null_checks);

		if (valid && implementation_function->is_vararg()) {
			for (int i = implementation_function->parameters.size(); i < p_required_function->parameters.size(); i++) {
				const BSParser::DataType required_parameter_type = _substitute_type_parameters_and_self(
						p_required_function->parameters[i]->get_datatype(), p_trait_substitution, implementation_self_type);
				valid = valid && BSTypeCompatibility::rest_parameter_accepts_required_argument(&implementation_rest_type, required_parameter_type, strict_null_checks);
			}
		}
	}

	if (!valid) {
		push_error(vformat(R"*(The function "%s()" signature does not match required trait method "%s".)*",
						   function_name, trait_method_name),
				implementation_function);
		return false;
	}

	return true;
}

BSParser::ClassNode *BSAnalyzer::resolve_builtin_conformance_shim(BSParser::ConformanceNode *p_conformance, const BSParser::DataType &p_builtin_type) {
	if (p_conformance->builtin_target_shim != nullptr) {
		return p_conformance->builtin_target_shim;
	}

	BSParser::ClassNode *shim = parser->alloc_recovery_node<BSParser::ClassNode>();
	shim->fqcn = String(Variant::get_type_name(p_builtin_type.builtin_type));
	shim->resolved_interface = true;
	shim->resolved_body = true;
	shim->resolved_trait_uses = true;
	shim->is_builtin_conformance_shim = true;

	BSParser::DataType self_type = p_builtin_type;
	self_type.is_meta_type = false;
	if (self_type.type_source == BSParser::DataType::UNDETECTED) {
		self_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	}
	shim->set_datatype(self_type);

	p_conformance->builtin_target_shim = shim;
	return shim;
}

BSParser::ClassNode *BSAnalyzer::resolve_native_conformance_shim(BSParser::ConformanceNode *p_conformance, const BSParser::DataType &p_native_type) {
	if (p_conformance->native_target_shim != nullptr) {
		return p_conformance->native_target_shim;
	}

	BSParser::ClassNode *shim = parser->alloc_recovery_node<BSParser::ClassNode>();
	shim->fqcn = String(p_native_type.native_type);
	shim->extends_used = true;
	shim->base_type = p_native_type;
	shim->base_type.is_meta_type = false;
	if (shim->base_type.type_source == BSParser::DataType::UNDETECTED) {
		shim->base_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	}
	shim->resolved_interface = true;
	shim->resolved_body = true;
	shim->resolved_trait_uses = true;
	shim->is_native_conformance_shim = true;

	BSParser::DataType self_type;
	self_type.kind = BSParser::DataType::CLASS;
	self_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	self_type.class_type = shim;
	self_type.native_type = p_native_type.native_type;
	self_type.is_meta_type = true;
	shim->set_datatype(self_type);

	p_conformance->native_target_shim = shim;
	return shim;
}

BSParser::ClassNode *BSAnalyzer::resolve_conformance_target(BSParser::ConformanceNode *p_conformance, BSParser::DataType &r_target_type) {
	if (p_conformance == nullptr || p_conformance->target == nullptr) {
		return nullptr;
	}

	// Prefer same-file class identity before index / ScriptServer lookups so `extend Head` binds the
	// parse tree under analysis (declaration commit may not have class_type yet).
	if (!p_conformance->target->type_chain.is_empty() && p_conformance->target->type_chain.size() == 1) {
		const StringName name = p_conformance->target->type_chain[0]->name;
		BSParser::ClassNode *local = _find_local_class_by_name(parser->get_tree(), name);
		if (local != nullptr && !local->is_trait) {
			r_target_type.kind = BSParser::DataType::CLASS;
			r_target_type.class_type = local;
			r_target_type.builtin_type = Variant::OBJECT;
			r_target_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			r_target_type.native_type = local->base_type.native_type;
			p_conformance->target->set_datatype(r_target_type);
			return local;
		}
	}

	r_target_type = datatype_from_type_node(p_conformance->target);
	p_conformance->target->set_datatype(r_target_type);

	if (r_target_type.kind == BSParser::DataType::CLASS && r_target_type.class_type != nullptr) {
		return r_target_type.class_type;
	}
	if ((r_target_type.kind == BSParser::DataType::CLASS || r_target_type.kind == BSParser::DataType::SCRIPT) &&
			!r_target_type.script_path.is_empty()) {
		Error err = OK;
		Ref<BSParserRef> ref = BSCache::get_parser(r_target_type.script_path, BSParserRef::INTERFACE_SOLVED, err,
				parser != nullptr ? parser->script_path : String());
		if (ref.is_valid() && err == OK && ref->get_parser() != nullptr) {
			return ref->get_parser()->get_tree();
		}
	}

	if (r_target_type.kind == BSParser::DataType::NATIVE && r_target_type.native_type != StringName() &&
			!r_target_type.is_meta_type) {
		Engine *engine = Engine::get_singleton();
		if (engine != nullptr && engine->has_singleton(r_target_type.native_type)) {
			push_error(vformat(R"(Cannot retroactively conform engine singleton "%s" to a trait.)", r_target_type.native_type), p_conformance->target);
			return nullptr;
		}
		return resolve_native_conformance_shim(p_conformance, r_target_type);
	}

	if (r_target_type.kind == BSParser::DataType::BUILTIN) {
		if (r_target_type.builtin_type == Variant::NIL || r_target_type.builtin_type == Variant::OBJECT) {
			push_error(vformat(R"(Cannot retroactively conform "%s" to a trait.)", Variant::get_type_name(r_target_type.builtin_type)), p_conformance->target);
			return nullptr;
		}
		return resolve_builtin_conformance_shim(p_conformance, r_target_type);
	}

	if (r_target_type.kind == BSParser::DataType::VARIANT) {
		push_error(R"(Retroactive conformance supports only BaristaScript class, native engine-class, and builtin value-type targets.)", p_conformance->target);
		return nullptr;
	}

	if (!r_target_type.is_variant()) {
		push_error(R"(Retroactive conformance supports only BaristaScript class, native engine-class, and builtin value-type targets.)", p_conformance->target);
	}
	return nullptr;
}

BSParser::ClassNode *BSAnalyzer::resolve_conformance_trait_use(BSParser::ClassNode *p_scope, BSParser::ClassNode::TraitUse &p_trait_use, const BSParser::Node *p_source) {
	const String name = p_trait_use.to_string();
	if (name.is_empty()) {
		return nullptr;
	}
	if (!p_trait_use.type_arguments.is_empty()) {
		push_error("Generic trait specialization is not available until M5.", p_source);
		return nullptr;
	}

	BSParser::ClassNode *trait = p_trait_use.resolved_trait;
	if (trait == nullptr) {
		trait = _find_local_trait_by_name(p_scope, name);
	}
	if (trait == nullptr) {
		BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
		BSDeclarationRecord record;
		bool found = false;
		if (language != nullptr) {
			found = language->try_resolve_declaration(name, record);
			if (!found && p_scope != nullptr && !p_scope->namespace_name.is_empty()) {
				found = language->try_resolve_declaration(p_scope->namespace_name + String(".") + name, record);
			}
			if (!found && p_scope != nullptr) {
				for (int j = 0; j < p_scope->imports.size(); j++) {
					found = language->try_resolve_declaration(p_scope->imports[j] + String(".") + name, record);
					if (found) {
						break;
					}
				}
			}
		}
		if (!found || record.kind != BSDeclarationKind::TRAIT) {
			push_error(vformat(R"(Could not find trait "%s".)", name), p_source);
			return nullptr;
		}
		Error err = OK;
		Ref<BSParserRef> trait_ref = BSCache::get_parser(record.path, BSParserRef::INTERFACE_SOLVED, err,
				parser != nullptr ? parser->script_path : String());
		if (trait_ref.is_null() || err != OK || trait_ref->get_parser() == nullptr || trait_ref->get_parser()->get_tree() == nullptr) {
			push_error(vformat(R"(Could not resolve trait "%s".)", name), p_source);
			return nullptr;
		}
		trait = trait_ref->get_parser()->get_tree();
	}
	if (trait == nullptr || !trait->is_trait) {
		push_error(vformat(R"("%s" is not a trait.)", name), p_source);
		return nullptr;
	}

	p_trait_use.resolved_trait = trait;
	resolve_used_traits(trait);
	analyze_class_interface(trait);
	return trait;
}

bool BSAnalyzer::validate_conformance(BSParser::ConformanceNode *p_conformance, BSParser::ClassNode *p_target,
		BSParser::ClassNode *p_trait) {
	if (p_conformance == nullptr || p_target == nullptr || p_trait == nullptr) {
		return false;
	}

	HashMap<StringName, BSParser::FunctionNode *> witnesses_by_name;
	for (int i = 0; i < p_conformance->witnesses.size(); i++) {
		BSParser::FunctionNode *witness = p_conformance->witnesses[i];
		if (witness != nullptr && witness->identifier != nullptr) {
			witnesses_by_name.insert(witness->identifier->name, witness);
		}
	}

	bool valid = true;
	HashSet<StringName> missing_methods;

	Vector<BSParser::ClassNode *> requirement_traits;
	requirement_traits.push_back(p_trait);
	for (int i = 0; i < p_trait->resolved_traits.size(); i++) {
		BSParser::ClassNode *transitive = p_trait->resolved_traits[i];
		if (transitive == nullptr) {
			continue;
		}
		bool already = false;
		for (int j = 0; j < requirement_traits.size(); j++) {
			if (requirement_traits[j] == transitive) {
				already = true;
				break;
			}
		}
		if (!already) {
			requirement_traits.push_back(transitive);
		}
	}

	for (int t = 0; t < requirement_traits.size(); t++) {
		BSParser::ClassNode *requirement_trait = requirement_traits[t];
		analyze_class_interface(requirement_trait);

		for (int i = 0; i < requirement_trait->members.size(); i++) {
			const BSParser::ClassNode::Member &member = requirement_trait->members[i];
			if (member.type != BSParser::ClassNode::Member::FUNCTION || member.function == nullptr) {
				continue;
			}
			BSParser::FunctionNode *required = member.function;
			const StringName function_name = required->identifier != nullptr ? required->identifier->name : StringName();
			if (function_name == StringName()) {
				continue;
			}

			if (witnesses_by_name.has(function_name)) {
				TraitMethodImplementation implementation;
				implementation.function = witnesses_by_name.get(function_name);
				implementation.owner_class = p_target;
				if (!validate_trait_method_signature(requirement_trait, p_target, required, implementation)) {
					valid = false;
				}
				continue;
			}

			if (!required->is_abstract) {
				continue;
			}

			TraitMethodImplementation implementation;
			if (find_trait_implementation(p_target, function_name, implementation)) {
				if (!validate_trait_method_signature(requirement_trait, p_target, required, implementation)) {
					valid = false;
				}
				continue;
			}

			if (!missing_methods.has(function_name)) {
				missing_methods.insert(function_name);
				push_error(vformat(R"*(Conformance of "%s" to trait "%s" must implement trait method "%s.%s()".)*",
								   bs_class_or_trait_diagnostic_name(p_target), bs_class_or_trait_diagnostic_name(p_trait),
								   bs_class_or_trait_diagnostic_name(requirement_trait), function_name),
						p_conformance);
			}
			valid = false;
		}
	}

	return valid;
}

void BSAnalyzer::resolve_conformances(BSParser::ClassNode *p_class) {
	const String source_file = parser != nullptr ? parser->script_path : String();
	BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();

	// Re-analysis replaces this file's previously-registered conformances wholesale. Empty
	// candidates still publish so stale entries clear; ClassTraitBinding publish is residual #60.
	if (p_class == nullptr || p_class->conformances.is_empty()) {
		if (registry != nullptr && !source_file.is_empty()) {
			registry->try_replace_file_conformances(source_file, Vector<BSConformanceRegistry::Conformance>());
		}
		return;
	}

	// Hide this file's previous declarations from *this* thread while validating; other readers
	// keep seeing them until try_replace commits below.
	BSConformanceRegistry::ScopedInFlightReplacement in_flight_replacement(source_file);

	HashMap<String, int> seen_membership_conformances;
	Vector<BSConformanceRegistry::Conformance> valid_entries;
	HashSet<int> reported_declarations;

	BSParser::ClassNode *head = parser != nullptr ? parser->get_tree() : p_class;
	for (int conformance_index = 0; conformance_index < p_class->conformances.size(); conformance_index++) {
		BSParser::ConformanceNode *conformance = p_class->conformances[conformance_index];
		if (conformance == nullptr) {
			continue;
		}

		BSParser::DataType target_type;
		BSParser::ClassNode *target = resolve_conformance_target(conformance, target_type);
		if (target == nullptr) {
			continue;
		}

		if (!target->is_native_conformance_shim && !target->is_builtin_conformance_shim) {
			resolve_used_traits(target);
			analyze_class_interface(target);
		}

		const StringName target_global = target->get_global_name();
		Vector<String> target_keys;
		target_keys.push_back(target->fqcn);
		if (target_global != StringName()) {
			target_keys.push_back(String(target_global));
		}
		if (!target_type.script_path.is_empty() && !target_keys.has(target_type.script_path)) {
			target_keys.push_back(target_type.script_path);
		}

		for (int i = 0; i < conformance->traits.size(); i++) {
			BSParser::ClassNode::TraitUse &trait_use = conformance->traits.write[i];
			BSParser::ClassNode *trait = resolve_conformance_trait_use(head, trait_use, conformance);
			if (trait == nullptr) {
				continue;
			}

			// Coherence: a conformance redundant with the target's own `uses` is rejected.
			bool redundant = false;
			for (int t = 0; t < target->resolved_traits.size(); t++) {
				BSParser::ClassNode *owned_trait = target->resolved_traits[t];
				if (owned_trait == trait || (owned_trait != nullptr && owned_trait->fqcn == trait->fqcn && !trait->fqcn.is_empty())) {
					redundant = true;
					break;
				}
			}
			if (redundant) {
				push_error(vformat(R"(Class "%s" already conforms to trait "%s" through its own "uses"; the conformance is redundant.)",
								   bs_class_or_trait_diagnostic_name(target), bs_class_or_trait_diagnostic_name(trait)),
						conformance);
				continue;
			}

			// Direct trait + already-resolved supertraits (Foundry identity closure without a new port).
			Vector<BSParser::ClassNode *> identity_nodes;
			identity_nodes.push_back(trait);
			for (int t = 0; t < trait->resolved_traits.size(); t++) {
				BSParser::ClassNode *supertrait = trait->resolved_traits[t];
				if (supertrait == nullptr) {
					continue;
				}
				bool already = false;
				for (int j = 0; j < identity_nodes.size(); j++) {
					if (identity_nodes[j] == supertrait) {
						already = true;
						break;
					}
				}
				if (!already) {
					identity_nodes.push_back(supertrait);
				}
			}

			bool membership_conflict = false;
			for (int identity_index = 0; identity_index < identity_nodes.size(); identity_index++) {
				const StringName identity = bs_trait_identity_name(identity_nodes[identity_index]);
				if (identity == StringName()) {
					continue;
				}
				const String pair_key = target->fqcn + "\n" + String(identity);
				const int *existing_conformance = seen_membership_conformances.getptr(pair_key);
				if (existing_conformance != nullptr &&
						(*existing_conformance != conformance_index || identity_index == 0)) {
					push_error(vformat(R"(Class "%s" already has a conformance to trait "%s" in this file.)",
									   bs_class_or_trait_diagnostic_name(target), String(identity)),
							conformance);
					membership_conflict = true;
					reported_declarations.insert(conformance_index);
					break;
				}

				if (registry != nullptr) {
					const String other_source = registry->get_conformance_source(target->fqcn, identity);
					if (!other_source.is_empty() && other_source != source_file) {
						push_error(vformat(R"(Class "%s" already conforms to trait "%s" via a conformance in "%s".)",
										   bs_class_or_trait_diagnostic_name(target), String(identity),
										   bs_diagnostic_file_reference(other_source)),
								conformance);
						membership_conflict = true;
						reported_declarations.insert(conformance_index);
						break;
					}
				}
			}
			if (membership_conflict) {
				continue;
			}

			if (!validate_conformance(conformance, target, trait)) {
				continue;
			}

			BSConformanceRegistry::Conformance entry;
			entry.target_keys = target_keys;
			entry.target_fqcn = target->fqcn;
			entry.target_script_path = target_type.script_path;
			entry.target_is_root_class = target->outer == nullptr;
			entry.target_label = bs_class_or_trait_diagnostic_name(target);
			entry.source_file = source_file;
			entry.conformance_index = conformance_index;
			for (int identity_index = 0; identity_index < identity_nodes.size(); identity_index++) {
				const StringName identity = bs_trait_identity_name(identity_nodes[identity_index]);
				if (identity == StringName()) {
					continue;
				}
				const String pair_key = target->fqcn + "\n" + String(identity);
				const int *existing_conformance = seen_membership_conformances.getptr(pair_key);
				if (existing_conformance != nullptr && *existing_conformance == conformance_index) {
					continue;
				}
				entry.trait_name = identity;
				valid_entries.push_back(entry);
				seen_membership_conformances.insert(pair_key, conformance_index);
			}
		}
	}

	if (registry == nullptr || source_file.is_empty()) {
		return;
	}

	const BSConformanceRegistry::RegistrationResult result =
			registry->try_replace_file_conformances(source_file, valid_entries);
	for (int i = 0; i < result.conflicts.size(); i++) {
		const BSConformanceRegistry::RegistrationConflict &conflict = result.conflicts[i];
		if (reported_declarations.has(conflict.conformance_index)) {
			continue;
		}
		BSParser::ConformanceNode *conformance = conflict.conformance_index >= 0 &&
						conflict.conformance_index < p_class->conformances.size()
				? p_class->conformances[conflict.conformance_index]
				: nullptr;
		if (conformance == nullptr) {
			continue;
		}
		if (conflict.kind == BSConformanceRegistry::RegistrationConflict::DUPLICATE_MEMBERSHIP) {
			push_error(vformat(R"(Class "%s" already conforms to trait "%s" via a conformance in %s.)",
							   conflict.target_label, String(conflict.trait_name),
							   bs_diagnostic_file_reference(conflict.conflicting_source_file)),
					conformance);
		}
	}
}

void BSAnalyzer::resolve_conformance_bodies(BSParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->conformances.is_empty()) {
		return;
	}

	for (int c = 0; c < p_class->conformances.size(); c++) {
		BSParser::ConformanceNode *conformance = p_class->conformances[c];
		if (conformance == nullptr || conformance->target == nullptr) {
			continue;
		}

		const BSParser::DataType target_type = conformance->target->get_datatype();
		BSParser::ClassNode *target = nullptr;
		if (target_type.kind == BSParser::DataType::CLASS && target_type.class_type != nullptr) {
			target = target_type.class_type;
		} else if ((target_type.kind == BSParser::DataType::CLASS || target_type.kind == BSParser::DataType::SCRIPT) &&
				!target_type.script_path.is_empty()) {
			Error err = OK;
			Ref<BSParserRef> ref = BSCache::get_parser(target_type.script_path, BSParserRef::INTERFACE_SOLVED, err,
					parser != nullptr ? parser->script_path : String());
			if (ref.is_valid() && err == OK && ref->get_parser() != nullptr) {
				target = ref->get_parser()->get_tree();
			}
		} else if (target_type.kind == BSParser::DataType::NATIVE) {
			target = conformance->native_target_shim;
		} else if (target_type.kind == BSParser::DataType::BUILTIN) {
			target = conformance->builtin_target_shim;
		}
		if (target == nullptr) {
			continue;
		}

		BSParser::ClassNode *previous_class = current_class;
		current_class = target;
		for (int i = 0; i < conformance->witnesses.size(); i++) {
			BSParser::FunctionNode *witness = conformance->witnesses[i];
			if (witness == nullptr) {
				continue;
			}
			if (witness->return_type != nullptr) {
				witness->set_datatype(datatype_from_type_node(witness->return_type));
			}
			for (int p = 0; p < witness->parameters.size(); p++) {
				BSParser::ParameterNode *parameter = witness->parameters[p];
				if (parameter != nullptr && parameter->datatype_specifier != nullptr) {
					parameter->set_datatype(datatype_from_type_node(parameter->datatype_specifier));
				}
			}
			analyze_function_body(witness);
		}
		current_class = previous_class;
	}
}

BSAnalyzer::ConformanceVisibility::ConformanceVisibility(BSAnalyzer *p_analyzer) {
	analyzer = p_analyzer;
}

bool BSAnalyzer::ConformanceVisibility::can_see(const String &p_source_file) const {
	if (analyzer == nullptr || analyzer->parser == nullptr || p_source_file.is_empty()) {
		return true;
	}
	if (p_source_file == analyzer->parser->script_path || visible_files.has(p_source_file)) {
		return true;
	}

	// Breadth-first over the dependency graph, from two sources per file:
	//
	//  - What it *declares*: the `preload` and `extends` paths its parse tree carries.
	//  - What it has *resolved*: `depended_parsers`, which reaches on through files this
	//    analysis has already pulled in.
	//
	// Every file reached on the way is a dependency too, so the whole visited set is
	// memoized. Only positive answers are kept: the graph grows during an analysis, so a
	// "no" can become a "yes".
	Vector<BSParser *> pending;
	HashSet<const BSParser *> seen;
	pending.push_back(analyzer->parser);
	seen.insert(analyzer->parser);
	// Hard cap: a corrupted or unexpectedly huge dependency fan-out must not hang the editor.
	const int max_visits = 4096;
	int visits = 0;
	while (!pending.is_empty() && visits < max_visits) {
		visits++;
		BSParser *current = pending[pending.size() - 1];
		pending.resize(pending.size() - 1);
		if (current == nullptr) {
			continue;
		}
		const List<String> declared_dependencies = current->get_dependencies();
		for (const List<String>::Element *E = declared_dependencies.front(); E; E = E->next()) {
			visible_files.insert(E->get());
		}
		const HashMap<String, Ref<BSParserRef>> &depended = current->get_depended_parsers();
		for (const KeyValue<String, Ref<BSParserRef>> &dependency : depended) {
			visible_files.insert(dependency.key);
			BSParser *dependency_parser = dependency.value.is_valid() ? dependency.value->get_parser() : nullptr;
			if (dependency_parser != nullptr && !seen.has(dependency_parser)) {
				seen.insert(dependency_parser);
				pending.push_back(dependency_parser);
			}
		}
	}
	return visible_files.has(p_source_file);
}

} // namespace barista_script
