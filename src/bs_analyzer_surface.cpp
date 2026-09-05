/**************************************************************************/
/*  bs_analyzer_surface.cpp                                               */
/*                                                                        */
/*  #60 class-body surface diagnostics. Ports unused-private /            */
/*  unused-signal post-pass, built-in resolve_annotation,                 */
/*  resolve_enum_values, same-file scope inheritance helpers, and CLASS   */
/*  inheritance member bind (@ c9d5e35 fs_analyzer_surface.cpp). Remaining */
/*  surface cross-file SCRIPT richness / resolve_class_member depth       */
/*  remains follow-up under #60. FS* -> BS*; engine contact through       */
/*  bs_platform.h.                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"

#include "barista_script.h"
#include "bs_platform.h"

namespace barista_script {

// Foundry uses Variant::construct; godot-cpp has no matching free function, so convert
// through typed operators for the annotation argument types that MethodInfo declares.
static bool _convert_annotation_argument(Variant &r_value, Variant::Type p_expected_type) {
	if (r_value.get_type() == p_expected_type) {
		return true;
	}
	if (!Variant::can_convert_strict(r_value.get_type(), p_expected_type)) {
		return false;
	}
	switch (p_expected_type) {
		case Variant::BOOL:
			r_value = (bool)r_value;
			return true;
		case Variant::INT:
			r_value = (int64_t)r_value;
			return true;
		case Variant::FLOAT:
			r_value = (double)r_value;
			return true;
		case Variant::STRING:
			r_value = String(r_value);
			return true;
		case Variant::STRING_NAME:
			r_value = StringName(String(r_value));
			return true;
		case Variant::ARRAY:
			if (r_value.get_type() == Variant::ARRAY) {
				return true;
			}
			return false;
		default:
			return false;
	}
}

void BSAnalyzer::resolve_annotation(BSParser::AnnotationNode *p_annotation, uint32_t p_target_kind) {
	if (p_annotation == nullptr) {
		return;
	}
	if (p_annotation->name == SNAME("@autoload")) {
		// Autoload argument validation remains follow-up under #60 surface depth.
		p_annotation->is_resolved = true;
		return;
	}
	if (p_annotation->is_custom) {
		// Custom annotation declaration resolution remains follow-up under #60.
		(void)p_target_kind;
		return;
	}
	ERR_FAIL_COND_MSG(parser == nullptr || !parser->valid_annotations.has(p_annotation->name),
			vformat(R"(Annotation "%s" not found to validate.)", p_annotation->name));

	if (p_annotation->is_resolved) {
		return;
	}
	p_annotation->is_resolved = true;

	// Parser already resolved literal arguments for a few annotations (@icon, region ignores).
	if (!p_annotation->resolved_arguments.is_empty()) {
		return;
	}

	const MethodInfo &annotation_info = parser->valid_annotations[p_annotation->name].info;
	for (int64_t i = 0, j = 0; i < p_annotation->arguments.size(); i++) {
		BSParser::ExpressionNode *argument = p_annotation->arguments[i];
		if (argument == nullptr) {
			continue;
		}
		const PropertyInfo &argument_info = annotation_info.arguments[j];
		if (j + 1 < annotation_info.arguments.size()) {
			++j;
		}

		reduce_expression(argument);

		if (!argument->is_constant) {
			push_error(vformat(R"(Argument %d of annotation "%s" isn't a constant expression.)", i + 1, p_annotation->name), argument);
			return;
		}

		Variant value = argument->reduced_value;
		if (value.get_type() != argument_info.type) {
#ifdef DEBUG_ENABLED
			if (argument_info.type == Variant::INT && value.get_type() == Variant::FLOAT) {
				Vector<String> symbols;
				push_warning(argument, BSWarning::NARROWING_CONVERSION, symbols);
			}
#endif
			if (!_convert_annotation_argument(value, argument_info.type)) {
				push_error(vformat(R"(Invalid argument for annotation "%s": argument %d should be "%s" but is "%s".)",
								   p_annotation->name, i + 1, Variant::get_type_name(argument_info.type), argument->get_datatype().to_string()),
						argument);
				return;
			}
		}

		p_annotation->resolved_arguments.push_back(value);
	}
}

void BSAnalyzer::warn_unused_class_members(BSParser::ClassNode *p_class) {
#ifdef DEBUG_ENABLED
	if (p_class == nullptr) {
		return;
	}
	// Foundry resolve_class_body unused pass @ c9d5e35: private "_" members and all signals.
	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr &&
				member.variable->identifier != nullptr) {
			if (member.variable->usages == 0 && String(member.variable->identifier->name).begins_with("_")) {
				Vector<String> symbols;
				symbols.push_back(String(member.variable->identifier->name));
				push_warning(member.variable->identifier, BSWarning::UNUSED_PRIVATE_CLASS_VARIABLE, symbols);
			}
		} else if (member.type == BSParser::ClassNode::Member::SIGNAL && member.signal != nullptr &&
				member.signal->identifier != nullptr) {
			if (member.signal->usages == 0) {
				Vector<String> symbols;
				symbols.push_back(String(member.signal->identifier->name));
				push_warning(member.signal->identifier, BSWarning::UNUSED_SIGNAL, symbols);
			}
		}
		// Nested CLASS unused is handled when analyze_class_body recurses into the nested class
		// (Foundry resolve_class_body does not re-walk nested members inside the unused pass).
	}
#else
	(void)p_class;
#endif
}

#define ENUM_SEPARATOR "."

BSParser::DataType BSAnalyzer::make_class_enum_type(const StringName &p_enum_name, BSParser::ClassNode *p_class, const String &p_script_path, bool p_meta) {
	BSParser::DataType type;
	type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = BSParser::DataType::ENUM;
	type.builtin_type = p_meta ? Variant::DICTIONARY : Variant::INT;
	type.enum_type = p_enum_name;
	type.is_constant = true;
	type.is_meta_type = p_meta;
	if (p_class != nullptr && !p_class->fqcn.is_empty()) {
		type.native_type = StringName(p_class->fqcn + ENUM_SEPARATOR + String(p_enum_name));
	} else {
		type.native_type = p_enum_name;
	}
	type.class_type = p_class;
	type.script_path = p_script_path;
	return type;
}

BSParser::DataType BSAnalyzer::type_from_metatype(const BSParser::DataType &p_meta_type) {
	BSParser::DataType result = p_meta_type;
	result.is_meta_type = false;
	result.is_pseudo_type = false;
	if (p_meta_type.kind == BSParser::DataType::ENUM) {
		// Tagged unions erase to read-only [tag, payload...] Arrays; plain enums stay INT-backed.
		result.builtin_type = p_meta_type.is_tagged_union ? Variant::ARRAY : Variant::INT;
	} else {
		result.is_constant = false;
	}
	return result;
}

BSParser::DataType BSAnalyzer::resolve_enum_values(BSParser::EnumNode *p_enum, const BSParser::DataType &p_enum_type, BSParser::ClassNode *p_owner) {
	if (p_enum == nullptr || p_owner == nullptr) {
		return p_enum_type;
	}
	if (p_enum->get_datatype().is_set()) {
		return p_enum->get_datatype();
	}

	BSParser::ClassNode *previous_class = current_class;
	current_class = p_owner;

	BSParser::DataType enum_type = p_enum_type;
	enum_type.is_tagged_union = p_enum->is_tagged_union;

	if (!p_enum->type_parameters.is_empty()) {
		// Generic tagged unions remain M5; still publish a shell so later references fail closed.
		push_error("Generic tagged-union specialization is not available until M5.", p_enum);
		enum_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		enum_type.kind = BSParser::DataType::ENUM;
		p_enum->set_datatype(enum_type);
		current_class = previous_class;
		return enum_type;
	}

	// Publish identity before payload field types so a payload naming this union does not re-enter.
	if (enum_type.is_tagged_union) {
		p_enum->set_datatype(enum_type);
	}

	Dictionary dictionary;
	for (int i = 0; i < p_enum->values.size(); i++) {
		BSParser::EnumNode::Value &element = p_enum->values.write[i];
		if (element.identifier == nullptr) {
			continue;
		}

		if (enum_type.is_tagged_union) {
			element.value = i;
			element.resolved = true;
			if (element.has_payload()) {
				BSParser::DataType::EnumCasePayload payload;
				for (const BSParser::EnumNode::PayloadField &field : element.payload_fields) {
					payload.field_names.push_back(field.identifier != nullptr ? field.identifier->name : StringName());
					payload.field_types.push_back(type_from_metatype(datatype_from_type_node(field.type)));
				}
				enum_type.enum_case_payloads[element.identifier->name] = payload;
			}
		} else if (element.custom_value != nullptr) {
			reduce_expression(element.custom_value);
			if (!element.custom_value->is_constant) {
				push_error(R"(Enum values must be constant.)", element.custom_value);
			} else if (element.custom_value->reduced_value.get_type() != Variant::INT) {
				push_error(R"(Enum values must be integers.)", element.custom_value);
			} else {
				element.value = element.custom_value->reduced_value;
				element.resolved = true;
			}
		} else {
			push_error(R"(Enum values must have an explicit integer value.)", element.identifier);
		}

		enum_type.enum_values[element.identifier->name] = element.value;
		dictionary[String(element.identifier->name)] = element.value;
	}

	p_enum->set_datatype(enum_type);
	p_enum->dictionary = dictionary;
	current_class = previous_class;
	return enum_type;
}

BSParser::DataType BSAnalyzer::lookup_local_enum_meta_type(const StringName &p_name, BSParser::Node *p_source) {
	(void)p_source;
	for (BSParser::ClassNode *scope = current_class; scope != nullptr; scope = scope->outer) {
		if (!scope->has_member(p_name)) {
			continue;
		}
		const BSParser::ClassNode::Member &member = scope->get_member(p_name);
		if (member.type != BSParser::ClassNode::Member::ENUM || member.m_enum == nullptr) {
			continue;
		}
		BSParser::DataType enum_meta = member.m_enum->get_datatype();
		if (!enum_meta.is_set()) {
			const String script_path = parser != nullptr ? parser->script_path : String();
			enum_meta = resolve_enum_values(member.m_enum, make_class_enum_type(p_name, scope, script_path, true), scope);
		}
		return enum_meta;
	}
	return BSParser::DataType();
}

void BSAnalyzer::mark_implicit_signal_usage(BSParser::CallNode *p_call, bool p_is_self) {
#ifdef DEBUG_ENABLED
	// Foundry @ c9d5e35: emit_signal / connect / disconnect / is_connected count as uses.
	if (!p_is_self || p_call == nullptr || parser == nullptr || current_class == nullptr || p_call->arguments.is_empty()) {
		return;
	}
	if (p_call->function_name != SNAME("emit_signal") && p_call->function_name != SNAME("connect") &&
			p_call->function_name != SNAME("disconnect") && p_call->function_name != SNAME("is_connected")) {
		return;
	}
	BSParser::ExpressionNode *signal_arg = p_call->arguments[0];
	if (signal_arg == nullptr || !signal_arg->is_constant) {
		return;
	}
	const StringName signal_name = signal_arg->reduced_value;
	if (!current_class->has_member(signal_name)) {
		return;
	}
	const BSParser::ClassNode::Member &member = current_class->get_member(signal_name);
	if (member.type == BSParser::ClassNode::Member::SIGNAL && member.signal != nullptr) {
		member.signal->usages++;
	}
#else
	(void)p_call;
	(void)p_is_self;
#endif
}

void BSAnalyzer::get_class_node_current_scope_classes(BSParser::ClassNode *p_node, List<BSParser::ClassNode *> *p_list, BSParser::Node *p_source) {
	// Foundry get_class_node_current_scope_classes @ c9d5e35: base CLASS chain before outer.
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_NULL(p_list);
	if (p_list->find(p_node) != nullptr) {
		return;
	}
	p_list->push_back(p_node);

	auto resolve_for_scope_traverse = [&](BSParser::ClassNode *p_scope_class) {
		if (p_scope_class == nullptr || p_scope_class->base_type.is_resolving()) {
			return;
		}
		if (!p_scope_class->base_type.is_set()) {
			resolve_class_inheritance(p_scope_class);
		}
		(void)p_source;
	};

	if (p_node->base_type.class_type != nullptr) {
		resolve_for_scope_traverse(p_node->base_type.class_type);
		get_class_node_current_scope_classes(p_node->base_type.class_type, p_list, p_source);
	}
	if (p_node->outer != nullptr) {
		resolve_for_scope_traverse(p_node->outer);
		get_class_node_current_scope_classes(p_node->outer, p_list, p_source);
	}
}

bool BSAnalyzer::try_bind_identifier_member(BSParser::IdentifierNode *p_identifier, BSParser::ClassNode *p_class, bool p_mark_inherited) {
	if (p_identifier == nullptr || p_class == nullptr || !p_class->has_member(p_identifier->name)) {
		return false;
	}
	const BSParser::ClassNode::Member member = p_class->get_member(p_identifier->name);
	if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
		if (p_mark_inherited && !member.variable->is_static) {
			p_identifier->source = BSParser::IdentifierNode::INHERITED_VARIABLE;
		} else {
			p_identifier->source = member.variable->is_static ? BSParser::IdentifierNode::STATIC_VARIABLE : BSParser::IdentifierNode::MEMBER_VARIABLE;
		}
		p_identifier->variable_source = member.variable;
		member.variable->usages++;
		p_identifier->set_datatype(member.variable->get_datatype());
		return true;
	}
	if (member.type == BSParser::ClassNode::Member::CONSTANT && member.constant != nullptr) {
		p_identifier->source = BSParser::IdentifierNode::MEMBER_CONSTANT;
		p_identifier->constant_source = member.constant;
		member.constant->usages++;
		p_identifier->set_datatype(member.constant->get_datatype());
		return true;
	}
	if (member.type == BSParser::ClassNode::Member::SIGNAL && member.signal != nullptr) {
		p_identifier->source = BSParser::IdentifierNode::MEMBER_SIGNAL;
		p_identifier->signal_source = member.signal;
		member.signal->usages++;
		const BSParser::DataType owner_type = p_class->get_datatype().is_set() ? p_class->get_datatype() : (current_class != nullptr ? current_class->get_datatype() : BSParser::DataType());
		p_identifier->set_datatype(call_site_validation.explicit_signal_type_from_node(member.signal, owner_type, p_class));
		return true;
	}
	if (member.type == BSParser::ClassNode::Member::FUNCTION && member.function != nullptr) {
		p_identifier->source = BSParser::IdentifierNode::MEMBER_FUNCTION;
		p_identifier->function_source = member.function;
		p_identifier->function_source_is_static = member.function->is_static;
		p_identifier->set_datatype(call_site_validation.callable_type_from_function(member.function));
		return true;
	}
	if (member.type == BSParser::ClassNode::Member::ENUM && member.m_enum != nullptr) {
		const BSParser::DataType enum_meta = lookup_local_enum_meta_type(p_identifier->name, p_identifier);
		if (enum_meta.is_set()) {
			p_identifier->set_datatype(enum_meta);
			p_identifier->is_constant = true;
			return true;
		}
		// Enum declared on an ancestor: publish via resolve_enum_values on that class.
		const String script_path = parser != nullptr ? parser->script_path : String();
		BSParser::DataType ancestor_enum = resolve_enum_values(member.m_enum, make_class_enum_type(p_identifier->name, p_class, script_path, true), p_class);
		if (ancestor_enum.is_set()) {
			p_identifier->set_datatype(ancestor_enum);
			p_identifier->is_constant = true;
			return true;
		}
	}
	return false;
}

bool BSAnalyzer::try_bind_identifier_member_in_inheritance(BSParser::IdentifierNode *p_identifier, BSParser::ClassNode *p_class) {
	if (p_identifier == nullptr || p_class == nullptr) {
		return false;
	}
	bool first = true;
	// Soft mutual path-extends may install CLASS loops for registration; stop identity revisits.
	HashSet<const BSParser::ClassNode *> visited;
	for (BSParser::ClassNode *lookup = p_class; lookup != nullptr; lookup = lookup->base_type.class_type) {
		if (visited.has(lookup)) {
			break;
		}
		visited.insert(lookup);
		if (try_bind_identifier_member(p_identifier, lookup, !first)) {
			return true;
		}
		first = false;
	}
	return false;
}

} // namespace barista_script
