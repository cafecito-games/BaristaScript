/**************************************************************************/
/*  bs_analyzer_surface.cpp                                               */
/*                                                                        */
/*  #60 class-body surface diagnostics starter. Ports unused-private /    */
/*  unused-signal post-pass and built-in resolve_annotation from Foundry  */
/*  fs_analyzer.cpp resolve_class_body / resolve_annotation @ c9d5e35     */
/*  (Foundry keeps these in the main analyzer TU; BaristaScript stages    */
/*  them here under the #60 surface residual while fs_analyzer_surface    */
/*  inheritance/interface depth remains follow-up). FS* -> BS*; engine    */
/*  contact through bs_platform.h.                                        */
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
		} else if (member.type == BSParser::ClassNode::Member::CLASS) {
			warn_unused_class_members(member.m_class);
		}
	}
#else
	(void)p_class;
#endif
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

} // namespace barista_script
