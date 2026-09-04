/**************************************************************************/
/*  bs_analyzer_flow_finality.cpp                                         */
/*                                                                        */
/*  Hard fork of Foundry `modules/foundry_script/fs_analyzer_flow_finality */
/*  .cpp` @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6. FS* -> BS*; engine  */
/*  contact through bs_platform.h. LOCAL + INSTANCE + STATIC final        */
/*  definite assignment + trait flattening for #60; narrowing follow-up.  */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"

#include "barista_script.h"

namespace barista_script {

BSAnalyzer::FlowFinalityContext::FlowFinalityContext(BSAnalyzer *p_analyzer) :
		analyzer(p_analyzer) {
}

BSAnalyzer::FlowFinalityContext::FlattenedTraitFinalNodesScope::FlattenedTraitFinalNodesScope(FlowFinalityContext &p_context) {
	context = &p_context;
	if (context != nullptr) {
		context->flattened_trait_final_nodes.clear();
	}
}

void BSAnalyzer::FlowFinalityContext::FlattenedTraitFinalNodesScope::insert(const BSParser::VariableNode *p_variable) {
	if (context != nullptr && p_variable != nullptr) {
		context->flattened_trait_final_nodes.insert(p_variable);
	}
}

BSAnalyzer::FlowFinalityContext::FlattenedTraitFinalNodesScope::~FlattenedTraitFinalNodesScope() {
	if (context != nullptr) {
		context->flattened_trait_final_nodes.clear();
	}
}

// Mirrors Foundry compiler `_is_flattenable_trait_member` (@ c9d5e35).
static bool _is_flattenable_trait_member(const BSParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case BSParser::ClassNode::Member::VARIABLE:
		case BSParser::ClassNode::Member::CONSTANT:
		case BSParser::ClassNode::Member::ENUM:
		case BSParser::ClassNode::Member::ENUM_VALUE:
		case BSParser::ClassNode::Member::SIGNAL:
			return true;
		case BSParser::ClassNode::Member::FUNCTION:
			return p_member.function != nullptr && !p_member.function->is_abstract;
		default:
			return false;
	}
}

// Collects members an implementing class receives from applied traits (first-trait-wins,
// shadowed by class/base declarations) — Foundry `_collect_flattened_trait_members`.
static void _collect_flattened_trait_members(const BSParser::ClassNode *p_class,
		LocalVector<const BSParser::ClassNode::Member *> &r_members) {
	if (p_class == nullptr || p_class->resolved_traits.is_empty()) {
		return;
	}

	HashSet<StringName> defined;
	for (const BSParser::ClassNode *owner = p_class; owner != nullptr; owner = owner->base_type.class_type) {
		for (int i = 0; i < owner->members.size(); i++) {
			const StringName name = StringName(owner->members[i].get_name());
			if (name != StringName()) {
				defined.insert(name);
			}
		}
	}

	for (int t = 0; t < p_class->resolved_traits.size(); t++) {
		BSParser::ClassNode *trait = p_class->resolved_traits[t];
		if (trait == nullptr) {
			continue;
		}
		for (int i = 0; i < trait->members.size(); i++) {
			const BSParser::ClassNode::Member &member = trait->members[i];
			if (!_is_flattenable_trait_member(member)) {
				continue;
			}
			const StringName name = StringName(member.get_name());
			if (name == StringName() || defined.has(name)) {
				continue;
			}
			defined.insert(name);
			r_members.push_back(&member);
		}
	}
}

void BSAnalyzer::FlowFinalityContext::merge_final_assignment_branches(const FinalAssignmentState &p_first, const FinalAssignmentState &p_second, FinalAssignmentState &r_out) {
	if (!p_first.reachable && !p_second.reachable) {
		r_out.assigned.clear();
		r_out.maybe_assigned.clear();
		r_out.reachable = false;
		return;
	}
	if (!p_first.reachable) {
		r_out = p_second;
		return;
	}
	if (!p_second.reachable) {
		r_out = p_first;
		return;
	}
	r_out.reachable = true;
	r_out.assigned.clear();
	for (const BSParser::VariableNode *variable : p_first.assigned) {
		if (p_second.assigned.has(variable)) {
			r_out.assigned.insert(variable);
		}
	}
	r_out.maybe_assigned.clear();
	for (const BSParser::VariableNode *variable : p_first.maybe_assigned) {
		r_out.maybe_assigned.insert(variable);
	}
	for (const BSParser::VariableNode *variable : p_second.maybe_assigned) {
		r_out.maybe_assigned.insert(variable);
	}
}

void BSAnalyzer::FlowFinalityContext::check_final_member_assignments(BSParser::ClassNode *p_class) {
	if (p_class == nullptr || analyzer == nullptr) {
		return;
	}
	// A trait's members are flattened into and checked on each implementing class (Foundry @ c9d5e35).
	if (p_class->is_trait) {
		return;
	}

	HashSet<const BSParser::VariableNode *> finals;
	HashMap<StringName, const BSParser::VariableNode *> finals_by_name;
	BSParser::FunctionNode *init_function = nullptr;
	bool init_function_from_trait = false;

	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
			BSParser::VariableNode *variable = member.variable;
			if (variable->is_final && !variable->is_static) {
				if (variable->property != BSParser::VariableNode::PROP_NONE) {
					analyzer->push_error(vformat(R"(Final variable "%s" cannot declare a getter or setter.)", variable->identifier->name), variable);
				} else if (variable->onready) {
					analyzer->push_error(vformat(R"(Final variable "%s" cannot be annotated with "@onready".)", variable->identifier->name), variable);
				} else if (variable->identifier != nullptr) {
					finals.insert(variable);
					finals_by_name[variable->identifier->name] = variable;
				}
			}
		} else if (member.type == BSParser::ClassNode::Member::FUNCTION) {
			if (member.function != nullptr && member.function->identifier != nullptr && member.function->identifier->name == SNAME("_init")) {
				init_function = member.function;
			}
		}
	}

	LocalVector<const BSParser::ClassNode::Member *> trait_members;
	_collect_flattened_trait_members(p_class, trait_members);
	for (const BSParser::ClassNode::Member *member_ptr : trait_members) {
		const BSParser::ClassNode::Member &member = *member_ptr;
		if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
			BSParser::VariableNode *variable = member.variable;
			if (variable->is_final && !variable->is_static) {
				if (variable->property != BSParser::VariableNode::PROP_NONE) {
					analyzer->push_error(vformat(R"(Final variable "%s" cannot declare a getter or setter.)", variable->identifier->name), variable);
				} else if (variable->onready) {
					analyzer->push_error(vformat(R"(Final variable "%s" cannot be annotated with "@onready".)", variable->identifier->name), variable);
				} else if (variable->identifier != nullptr) {
					finals.insert(variable);
					finals_by_name[variable->identifier->name] = variable;
				}
			}
		} else if (member.type == BSParser::ClassNode::Member::FUNCTION) {
			if (init_function == nullptr && member.function != nullptr && member.function->identifier != nullptr &&
					member.function->identifier->name == SNAME("_init")) {
				init_function = member.function;
				init_function_from_trait = true;
			}
		}
	}

	FlattenedTraitFinalNodesScope trait_final_nodes(*this);
	for (int t = 0; t < p_class->resolved_traits.size(); t++) {
		const BSParser::ClassNode *trait = p_class->resolved_traits[t];
		if (trait == nullptr) {
			continue;
		}
		for (int i = 0; i < trait->members.size(); i++) {
			const BSParser::ClassNode::Member &member = trait->members[i];
			if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr &&
					member.variable->is_final && !member.variable->is_static) {
				trait_final_nodes.insert(member.variable);
			}
		}
	}

	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == BSParser::ClassNode::Member::FUNCTION) {
			BSParser::FunctionNode *function = member.function;
			if (function != nullptr) {
				scan_illegal_final_writes(function->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, function == init_function);
			}
		} else if (member.type == BSParser::ClassNode::Member::ENUM && member.m_enum != nullptr) {
			for (int f = 0; f < member.m_enum->functions.size(); f++) {
				BSParser::FunctionNode *function = member.m_enum->functions[f];
				if (function != nullptr) {
					scan_illegal_final_writes(function->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false);
				}
			}
		} else if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
			scan_illegal_final_writes(member.variable->initializer, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false);
			if (member.variable->property == BSParser::VariableNode::PROP_INLINE) {
				if (member.variable->getter != nullptr) {
					scan_illegal_final_writes(member.variable->getter->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false);
				}
				if (member.variable->setter != nullptr) {
					scan_illegal_final_writes(member.variable->setter->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false);
				}
			}
		}
	}

	for (const BSParser::ClassNode::Member *member_ptr : trait_members) {
		const BSParser::ClassNode::Member &member = *member_ptr;
		if (member.type == BSParser::ClassNode::Member::FUNCTION && member.function != nullptr) {
			scan_illegal_final_writes(member.function->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER,
					member.function == init_function, true);
		} else if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
			scan_illegal_final_writes(member.variable->initializer, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false, true);
			if (member.variable->property == BSParser::VariableNode::PROP_INLINE) {
				if (member.variable->getter != nullptr) {
					scan_illegal_final_writes(member.variable->getter->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false, true);
				}
				if (member.variable->setter != nullptr) {
					scan_illegal_final_writes(member.variable->setter->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false, true);
				}
			}
		}
	}

	if (!finals.is_empty()) {
		FinalAssignmentState init_state;
		Vector<const BSParser::VariableNode *> blank_finals;
		for (int i = 0; i < p_class->members.size(); i++) {
			const BSParser::ClassNode::Member &member = p_class->members[i];
			if (member.type != BSParser::ClassNode::Member::VARIABLE || member.variable == nullptr) {
				continue;
			}
			BSParser::VariableNode *variable = member.variable;
			if (variable->initializer != nullptr) {
				check_final_reads_in_expression(variable->initializer, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, init_state);
			}
			if (!finals.has(variable)) {
				continue;
			}
			if (variable->initializer != nullptr) {
				init_state.assigned.insert(variable);
				init_state.maybe_assigned.insert(variable);
			} else {
				blank_finals.push_back(variable);
			}
		}

		for (const BSParser::ClassNode::Member *member_ptr : trait_members) {
			const BSParser::ClassNode::Member &member = *member_ptr;
			if (member.type != BSParser::ClassNode::Member::VARIABLE || member.variable == nullptr) {
				continue;
			}
			BSParser::VariableNode *variable = member.variable;
			if (variable->initializer != nullptr) {
				check_final_reads_in_expression(variable->initializer, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, init_state, true);
			}
			if (!finals.has(variable)) {
				continue;
			}
			if (variable->initializer != nullptr) {
				init_state.assigned.insert(variable);
				init_state.maybe_assigned.insert(variable);
			} else {
				blank_finals.push_back(variable);
			}
		}

		if (init_function != nullptr) {
			for (int i = 0; i < init_function->parameters.size(); i++) {
				if (init_function->parameters[i] != nullptr) {
					check_final_reads_in_expression(init_function->parameters[i]->initializer, finals, finals_by_name,
							FinalAssignmentScope::INSTANCE_MEMBER, init_state, init_function_from_trait);
				}
			}
			HashSet<const BSParser::VariableNode *> assigned_anywhere;
			analyze_final_definite_assignment_suite(init_function->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER,
					init_state, assigned_anywhere, init_function_from_trait);
			if (init_state.reachable) {
				for (int i = 0; i < blank_finals.size(); i++) {
					const BSParser::VariableNode *variable = blank_finals[i];
					if (!init_state.assigned.has(variable)) {
						analyzer->push_error(vformat(R"*(Final variable "%s" must be definitely assigned in its declaration or in "_init()".)*", variable->identifier->name), variable);
					}
				}
			}
		} else {
			for (int i = 0; i < blank_finals.size(); i++) {
				const BSParser::VariableNode *variable = blank_finals[i];
				analyzer->push_error(vformat(R"*(Final variable "%s" must be definitely assigned in its declaration or in "_init()".)*", variable->identifier->name), variable);
			}
		}
	}

	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == BSParser::ClassNode::Member::CLASS) {
			check_final_member_assignments(member.m_class);
		}
	}
}

void BSAnalyzer::FlowFinalityContext::check_final_static_assignments(BSParser::ClassNode *p_class) {
	if (p_class == nullptr || analyzer == nullptr) {
		return;
	}
	if (p_class->is_trait) {
		return;
	}

	HashSet<const BSParser::VariableNode *> finals;
	HashMap<StringName, const BSParser::VariableNode *> finals_by_name;
	BSParser::FunctionNode *static_init_function = nullptr;
	bool static_init_function_from_trait = false;

	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
			BSParser::VariableNode *variable = member.variable;
			if (variable->is_final && variable->is_static) {
				if (variable->property != BSParser::VariableNode::PROP_NONE) {
					analyzer->push_error(vformat(R"(Final variable "%s" cannot declare a getter or setter.)", variable->identifier->name), variable);
				} else if (variable->onready) {
					analyzer->push_error(vformat(R"(Final variable "%s" cannot be annotated with "@onready".)", variable->identifier->name), variable);
				} else if (variable->identifier != nullptr) {
					finals.insert(variable);
					finals_by_name[variable->identifier->name] = variable;
				}
			}
		} else if (member.type == BSParser::ClassNode::Member::FUNCTION) {
			if (member.function != nullptr && member.function->identifier != nullptr && member.function->identifier->name == SNAME("_static_init")) {
				static_init_function = member.function;
			}
		}
	}

	LocalVector<const BSParser::ClassNode::Member *> trait_members;
	_collect_flattened_trait_members(p_class, trait_members);
	for (const BSParser::ClassNode::Member *member_ptr : trait_members) {
		const BSParser::ClassNode::Member &member = *member_ptr;
		if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
			BSParser::VariableNode *variable = member.variable;
			if (variable->is_final && variable->is_static) {
				if (variable->property != BSParser::VariableNode::PROP_NONE) {
					analyzer->push_error(vformat(R"(Final variable "%s" cannot declare a getter or setter.)", variable->identifier->name), variable);
				} else if (variable->identifier != nullptr) {
					finals.insert(variable);
					finals_by_name[variable->identifier->name] = variable;
				}
			}
		} else if (member.type == BSParser::ClassNode::Member::FUNCTION) {
			if (static_init_function == nullptr && member.function != nullptr && member.function->identifier != nullptr &&
					member.function->identifier->name == SNAME("_static_init")) {
				static_init_function = member.function;
				static_init_function_from_trait = true;
			}
		}
	}

	FlattenedTraitFinalNodesScope trait_final_nodes(*this);
	for (int t = 0; t < p_class->resolved_traits.size(); t++) {
		const BSParser::ClassNode *trait = p_class->resolved_traits[t];
		if (trait == nullptr) {
			continue;
		}
		for (int i = 0; i < trait->members.size(); i++) {
			const BSParser::ClassNode::Member &member = trait->members[i];
			if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr &&
					member.variable->is_final && member.variable->is_static) {
				trait_final_nodes.insert(member.variable);
			}
		}
	}

	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == BSParser::ClassNode::Member::FUNCTION) {
			BSParser::FunctionNode *function = member.function;
			if (function != nullptr) {
				scan_illegal_final_writes(function->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, function == static_init_function);
			}
		} else if (member.type == BSParser::ClassNode::Member::ENUM && member.m_enum != nullptr) {
			for (int f = 0; f < member.m_enum->functions.size(); f++) {
				BSParser::FunctionNode *function = member.m_enum->functions[f];
				if (function != nullptr) {
					scan_illegal_final_writes(function->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false);
				}
			}
		} else if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
			scan_illegal_final_writes(member.variable->initializer, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false);
			if (member.variable->property == BSParser::VariableNode::PROP_INLINE) {
				if (member.variable->getter != nullptr) {
					scan_illegal_final_writes(member.variable->getter->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false);
				}
				if (member.variable->setter != nullptr) {
					scan_illegal_final_writes(member.variable->setter->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false);
				}
			}
		}
	}

	for (const BSParser::ClassNode::Member *member_ptr : trait_members) {
		const BSParser::ClassNode::Member &member = *member_ptr;
		if (member.type == BSParser::ClassNode::Member::FUNCTION && member.function != nullptr) {
			scan_illegal_final_writes(member.function->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER,
					member.function == static_init_function, true);
		} else if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
			scan_illegal_final_writes(member.variable->initializer, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false, true);
			if (member.variable->property == BSParser::VariableNode::PROP_INLINE) {
				if (member.variable->getter != nullptr) {
					scan_illegal_final_writes(member.variable->getter->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false, true);
				}
				if (member.variable->setter != nullptr) {
					scan_illegal_final_writes(member.variable->setter->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false, true);
				}
			}
		}
	}

	if (!finals.is_empty()) {
		FinalAssignmentState init_state;
		Vector<const BSParser::VariableNode *> blank_finals;
		for (int i = 0; i < p_class->members.size(); i++) {
			const BSParser::ClassNode::Member &member = p_class->members[i];
			if (member.type != BSParser::ClassNode::Member::VARIABLE || member.variable == nullptr) {
				continue;
			}
			BSParser::VariableNode *variable = member.variable;
			if (!variable->is_static) {
				continue;
			}
			if (variable->initializer != nullptr) {
				check_final_reads_in_expression(variable->initializer, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, init_state);
			}
			if (!finals.has(variable)) {
				continue;
			}
			if (variable->initializer != nullptr) {
				init_state.assigned.insert(variable);
				init_state.maybe_assigned.insert(variable);
			} else {
				blank_finals.push_back(variable);
			}
		}

		for (const BSParser::ClassNode::Member *member_ptr : trait_members) {
			const BSParser::ClassNode::Member &member = *member_ptr;
			if (member.type != BSParser::ClassNode::Member::VARIABLE || member.variable == nullptr) {
				continue;
			}
			BSParser::VariableNode *variable = member.variable;
			if (!variable->is_static) {
				continue;
			}
			if (variable->initializer != nullptr) {
				check_final_reads_in_expression(variable->initializer, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, init_state, true);
			}
			if (!finals.has(variable)) {
				continue;
			}
			if (variable->initializer != nullptr) {
				init_state.assigned.insert(variable);
				init_state.maybe_assigned.insert(variable);
			} else {
				blank_finals.push_back(variable);
			}
		}

		if (static_init_function != nullptr) {
			for (int i = 0; i < static_init_function->parameters.size(); i++) {
				if (static_init_function->parameters[i] != nullptr) {
					check_final_reads_in_expression(static_init_function->parameters[i]->initializer, finals, finals_by_name,
							FinalAssignmentScope::STATIC_MEMBER, init_state, static_init_function_from_trait);
				}
			}
			HashSet<const BSParser::VariableNode *> assigned_anywhere;
			analyze_final_definite_assignment_suite(static_init_function->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER,
					init_state, assigned_anywhere, static_init_function_from_trait);
			if (init_state.reachable) {
				for (int i = 0; i < blank_finals.size(); i++) {
					const BSParser::VariableNode *variable = blank_finals[i];
					if (!init_state.assigned.has(variable)) {
						analyzer->push_error(vformat(R"*(Final variable "%s" must be definitely assigned in its declaration or in "_static_init()".)*", variable->identifier->name), variable);
					}
				}
			}
		} else {
			for (int i = 0; i < blank_finals.size(); i++) {
				const BSParser::VariableNode *variable = blank_finals[i];
				analyzer->push_error(vformat(R"*(Final variable "%s" must be definitely assigned in its declaration or in "_static_init()".)*", variable->identifier->name), variable);
			}
		}
	}

	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == BSParser::ClassNode::Member::CLASS) {
			check_final_static_assignments(member.m_class);
		}
	}
}

void BSAnalyzer::FlowFinalityContext::check_final_local_assignments(BSParser::ClassNode *p_class) {
	if (p_class == nullptr || analyzer == nullptr) {
		return;
	}
	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == BSParser::ClassNode::Member::FUNCTION) {
			analyze_function_local_finals(member.function);
		} else if (member.type == BSParser::ClassNode::Member::CLASS) {
			check_final_local_assignments(member.m_class);
		} else if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
			if (member.variable->initializer != nullptr) {
				HashSet<const BSParser::VariableNode *> nested_finals;
				HashMap<StringName, const BSParser::VariableNode *> nested_finals_by_name;
				collect_local_finals(member.variable->initializer, nested_finals, nested_finals_by_name);
			}
			if (member.variable->property == BSParser::VariableNode::PROP_INLINE) {
				analyze_function_local_finals(member.variable->getter);
				analyze_function_local_finals(member.variable->setter);
			}
		}
	}
}

void BSAnalyzer::FlowFinalityContext::analyze_function_local_finals(const BSParser::FunctionNode *p_function) {
	if (p_function == nullptr || p_function->body == nullptr) {
		return;
	}
	HashSet<const BSParser::VariableNode *> finals;
	HashMap<StringName, const BSParser::VariableNode *> finals_by_name;
	collect_local_finals(p_function->body, finals, finals_by_name);
	if (!finals.is_empty()) {
		scan_illegal_final_writes(p_function->body, finals, finals_by_name, FinalAssignmentScope::LOCAL, true);

		FinalAssignmentState state;
		HashSet<const BSParser::VariableNode *> assigned_anywhere;
		analyze_final_definite_assignment_suite(p_function->body, finals, finals_by_name, FinalAssignmentScope::LOCAL, state, assigned_anywhere);
	}
}

void BSAnalyzer::FlowFinalityContext::collect_local_finals(const BSParser::Node *p_node,
		HashSet<const BSParser::VariableNode *> &r_finals,
		HashMap<StringName, const BSParser::VariableNode *> &r_finals_by_name) {
	if (p_node == nullptr) {
		return;
	}
	switch (p_node->type) {
		case BSParser::Node::SUITE: {
			const BSParser::SuiteNode *suite = static_cast<const BSParser::SuiteNode *>(p_node);
			for (int i = 0; i < suite->statements.size(); i++) {
				collect_local_finals(suite->statements[i], r_finals, r_finals_by_name);
			}
		} break;
		case BSParser::Node::IF: {
			const BSParser::IfNode *if_node = static_cast<const BSParser::IfNode *>(p_node);
			collect_local_finals(if_node->condition, r_finals, r_finals_by_name);
			collect_local_finals(if_node->true_block, r_finals, r_finals_by_name);
			collect_local_finals(if_node->false_block, r_finals, r_finals_by_name);
		} break;
		case BSParser::Node::FOR: {
			const BSParser::ForNode *for_node = static_cast<const BSParser::ForNode *>(p_node);
			collect_local_finals(for_node->list, r_finals, r_finals_by_name);
			collect_local_finals(for_node->loop, r_finals, r_finals_by_name);
		} break;
		case BSParser::Node::WHILE: {
			const BSParser::WhileNode *while_node = static_cast<const BSParser::WhileNode *>(p_node);
			collect_local_finals(while_node->condition, r_finals, r_finals_by_name);
			collect_local_finals(while_node->loop, r_finals, r_finals_by_name);
		} break;
		case BSParser::Node::MATCH: {
			const BSParser::MatchNode *match_node = static_cast<const BSParser::MatchNode *>(p_node);
			collect_local_finals(match_node->test, r_finals, r_finals_by_name);
			for (int i = 0; i < match_node->branches.size(); i++) {
				if (match_node->branches[i] != nullptr) {
					collect_local_finals(match_node->branches[i]->guard_body, r_finals, r_finals_by_name);
					collect_local_finals(match_node->branches[i]->block, r_finals, r_finals_by_name);
				}
			}
		} break;
		case BSParser::Node::VARIABLE: {
			const BSParser::VariableNode *variable = static_cast<const BSParser::VariableNode *>(p_node);
			if (variable->is_final && variable->identifier != nullptr) {
				r_finals.insert(variable);
				r_finals_by_name[variable->identifier->name] = variable;
			}
			collect_local_finals(variable->initializer, r_finals, r_finals_by_name);
		} break;
		case BSParser::Node::VARIABLE_DESTRUCTURE: {
			const BSParser::VariableDestructureNode *destructure = static_cast<const BSParser::VariableDestructureNode *>(p_node);
			for (int i = 0; i < destructure->bindings.size(); i++) {
				const BSParser::VariableNode *binding = destructure->bindings[i];
				if (binding != nullptr && binding->is_final && binding->identifier != nullptr) {
					r_finals.insert(binding);
					r_finals_by_name[binding->identifier->name] = binding;
				}
			}
			collect_local_finals(destructure->initializer, r_finals, r_finals_by_name);
		} break;
		case BSParser::Node::LAMBDA: {
			const BSParser::LambdaNode *lambda = static_cast<const BSParser::LambdaNode *>(p_node);
			analyze_function_local_finals(lambda->function);
		} break;
		case BSParser::Node::ASSIGNMENT: {
			const BSParser::AssignmentNode *assignment = static_cast<const BSParser::AssignmentNode *>(p_node);
			collect_local_finals(assignment->assignee, r_finals, r_finals_by_name);
			collect_local_finals(assignment->assigned_value, r_finals, r_finals_by_name);
		} break;
		case BSParser::Node::RETURN: {
			const BSParser::ReturnNode *return_node = static_cast<const BSParser::ReturnNode *>(p_node);
			collect_local_finals(return_node->return_value, r_finals, r_finals_by_name);
		} break;
		case BSParser::Node::ASSERT: {
			const BSParser::AssertNode *assert_node = static_cast<const BSParser::AssertNode *>(p_node);
			collect_local_finals(assert_node->condition, r_finals, r_finals_by_name);
			collect_local_finals(assert_node->message, r_finals, r_finals_by_name);
		} break;
		case BSParser::Node::BINARY_OPERATOR: {
			const BSParser::BinaryOpNode *binary = static_cast<const BSParser::BinaryOpNode *>(p_node);
			collect_local_finals(binary->left_operand, r_finals, r_finals_by_name);
			collect_local_finals(binary->right_operand, r_finals, r_finals_by_name);
		} break;
		case BSParser::Node::UNARY_OPERATOR: {
			const BSParser::UnaryOpNode *unary = static_cast<const BSParser::UnaryOpNode *>(p_node);
			collect_local_finals(unary->operand, r_finals, r_finals_by_name);
		} break;
		case BSParser::Node::TERNARY_OPERATOR: {
			const BSParser::TernaryOpNode *ternary = static_cast<const BSParser::TernaryOpNode *>(p_node);
			collect_local_finals(ternary->condition, r_finals, r_finals_by_name);
			collect_local_finals(ternary->true_expr, r_finals, r_finals_by_name);
			collect_local_finals(ternary->false_expr, r_finals, r_finals_by_name);
		} break;
		case BSParser::Node::CALL: {
			const BSParser::CallNode *call = static_cast<const BSParser::CallNode *>(p_node);
			collect_local_finals(call->callee, r_finals, r_finals_by_name);
			for (int i = 0; i < call->arguments.size(); i++) {
				collect_local_finals(call->arguments[i], r_finals, r_finals_by_name);
			}
		} break;
		case BSParser::Node::SUBSCRIPT: {
			const BSParser::SubscriptNode *subscript = static_cast<const BSParser::SubscriptNode *>(p_node);
			collect_local_finals(subscript->base, r_finals, r_finals_by_name);
			if (!subscript->is_attribute) {
				collect_local_finals(subscript->index, r_finals, r_finals_by_name);
			}
		} break;
		case BSParser::Node::ARRAY: {
			const BSParser::ArrayNode *array = static_cast<const BSParser::ArrayNode *>(p_node);
			for (int i = 0; i < array->elements.size(); i++) {
				collect_local_finals(array->elements[i], r_finals, r_finals_by_name);
			}
		} break;
		case BSParser::Node::DICTIONARY: {
			const BSParser::DictionaryNode *dictionary = static_cast<const BSParser::DictionaryNode *>(p_node);
			for (int i = 0; i < dictionary->elements.size(); i++) {
				collect_local_finals(dictionary->elements[i].key, r_finals, r_finals_by_name);
				collect_local_finals(dictionary->elements[i].value, r_finals, r_finals_by_name);
			}
		} break;
		default:
			break;
	}
}

const BSParser::VariableNode *BSAnalyzer::FlowFinalityContext::final_member_assignment_target(const BSParser::ExpressionNode *p_expression,
		const HashSet<const BSParser::VariableNode *> &p_finals,
		const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, bool *r_is_self_receiver,
		bool p_flattened_trait_body) const {
	(void)p_finals;
	if (r_is_self_receiver != nullptr) {
		*r_is_self_receiver = false;
	}
	if (p_expression == nullptr) {
		return nullptr;
	}
	// Flattened trait bodies resolve bare/`self` names against the implementer's tracked slots.
	if (p_flattened_trait_body) {
		if (p_expression->type == BSParser::Node::IDENTIFIER) {
			const BSParser::IdentifierNode *identifier = static_cast<const BSParser::IdentifierNode *>(p_expression);
			switch (identifier->source) {
				case BSParser::IdentifierNode::FUNCTION_PARAMETER:
				case BSParser::IdentifierNode::LOCAL_VARIABLE:
				case BSParser::IdentifierNode::LOCAL_CONSTANT:
				case BSParser::IdentifierNode::LOCAL_ITERATOR:
				case BSParser::IdentifierNode::LOCAL_BIND:
					return nullptr;
				default:
					break;
			}
			HashMap<StringName, const BSParser::VariableNode *>::ConstIterator found = p_finals_by_name.find(identifier->name);
			if (found) {
				if (r_is_self_receiver != nullptr) {
					*r_is_self_receiver = true;
				}
				return found->value;
			}
			const bool is_member_kind = identifier->source == BSParser::IdentifierNode::MEMBER_VARIABLE ||
					identifier->source == BSParser::IdentifierNode::INHERITED_VARIABLE ||
					identifier->source == BSParser::IdentifierNode::STATIC_VARIABLE;
			if (is_member_kind && identifier->variable_source != nullptr && flattened_trait_final_nodes.has(identifier->variable_source)) {
				return nullptr;
			}
		} else if (p_expression->type == BSParser::Node::SUBSCRIPT) {
			const BSParser::SubscriptNode *subscript = static_cast<const BSParser::SubscriptNode *>(p_expression);
			if (!subscript->is_attribute || subscript->attribute == nullptr) {
				return nullptr;
			}
			if (subscript->base != nullptr && subscript->base->type == BSParser::Node::SELF) {
				HashMap<StringName, const BSParser::VariableNode *>::ConstIterator found = p_finals_by_name.find(subscript->attribute->name);
				if (found) {
					if (r_is_self_receiver != nullptr) {
						*r_is_self_receiver = true;
					}
					return found->value;
				}
				const bool attribute_is_member_kind = subscript->attribute->source == BSParser::IdentifierNode::MEMBER_VARIABLE ||
						subscript->attribute->source == BSParser::IdentifierNode::INHERITED_VARIABLE ||
						subscript->attribute->source == BSParser::IdentifierNode::STATIC_VARIABLE;
				if (attribute_is_member_kind && subscript->attribute->variable_source != nullptr &&
						flattened_trait_final_nodes.has(subscript->attribute->variable_source)) {
					return nullptr;
				}
			}
		} else {
			return nullptr;
		}
	}
	// LOCAL / STATIC: bare identifier with the matching source. STATIC also accepts qualified forms.
	if (p_scope != FinalAssignmentScope::INSTANCE_MEMBER) {
		if (p_expression->type == BSParser::Node::IDENTIFIER) {
			const BSParser::IdentifierNode *identifier = static_cast<const BSParser::IdentifierNode *>(p_expression);
			const BSParser::IdentifierNode::Source expected_source = p_scope == FinalAssignmentScope::STATIC_MEMBER ? BSParser::IdentifierNode::STATIC_VARIABLE : BSParser::IdentifierNode::LOCAL_VARIABLE;
			if (identifier->source == expected_source && identifier->variable_source != nullptr && identifier->variable_source->is_final) {
				if (r_is_self_receiver != nullptr) {
					*r_is_self_receiver = true;
				}
				return identifier->variable_source;
			}
		}
		if (p_scope == FinalAssignmentScope::STATIC_MEMBER && p_expression->type == BSParser::Node::SUBSCRIPT) {
			const BSParser::SubscriptNode *subscript = static_cast<const BSParser::SubscriptNode *>(p_expression);
			if (subscript->is_attribute && subscript->attribute != nullptr &&
					subscript->attribute->source == BSParser::IdentifierNode::STATIC_VARIABLE &&
					subscript->attribute->variable_source != nullptr && subscript->attribute->variable_source->is_final) {
				const BSParser::VariableNode *static_final = subscript->attribute->variable_source;
				HashMap<StringName, const BSParser::VariableNode *>::ConstIterator found = p_finals_by_name.find(subscript->attribute->name);
				if (found && found->value == static_final && r_is_self_receiver != nullptr) {
					*r_is_self_receiver = true;
				}
				return static_final;
			}
		}
		return nullptr;
	}

	if (p_expression->type == BSParser::Node::IDENTIFIER) {
		const BSParser::IdentifierNode *identifier = static_cast<const BSParser::IdentifierNode *>(p_expression);
		const bool is_member = identifier->source == BSParser::IdentifierNode::MEMBER_VARIABLE || identifier->source == BSParser::IdentifierNode::INHERITED_VARIABLE;
		if (is_member && identifier->variable_source != nullptr && identifier->variable_source->is_final) {
			if (r_is_self_receiver != nullptr) {
				*r_is_self_receiver = true;
			}
			return identifier->variable_source;
		}
		return nullptr;
	}
	if (p_expression->type == BSParser::Node::SUBSCRIPT) {
		const BSParser::SubscriptNode *subscript = static_cast<const BSParser::SubscriptNode *>(p_expression);
		if (subscript->is_attribute && subscript->attribute != nullptr && subscript->base != nullptr) {
			const bool attribute_is_variable = subscript->attribute->source == BSParser::IdentifierNode::MEMBER_VARIABLE ||
					subscript->attribute->source == BSParser::IdentifierNode::INHERITED_VARIABLE ||
					subscript->attribute->source == BSParser::IdentifierNode::STATIC_VARIABLE;
			const BSParser::VariableNode *attribute_final = (attribute_is_variable && subscript->attribute->variable_source != nullptr &&
																	subscript->attribute->variable_source->is_final && !subscript->attribute->variable_source->is_static)
					? subscript->attribute->variable_source
					: nullptr;
			if (subscript->base->type == BSParser::Node::SELF) {
				HashMap<StringName, const BSParser::VariableNode *>::ConstIterator found = p_finals_by_name.find(subscript->attribute->name);
				if (found) {
					if (r_is_self_receiver != nullptr) {
						*r_is_self_receiver = true;
					}
					return found->value;
				}
				if (attribute_final != nullptr) {
					if (r_is_self_receiver != nullptr) {
						*r_is_self_receiver = true;
					}
					return attribute_final;
				}
			} else if (attribute_final != nullptr) {
				return attribute_final;
			}
		}
	}
	return nullptr;
}

void BSAnalyzer::FlowFinalityContext::scan_illegal_final_writes(const BSParser::Node *p_node,
		const HashSet<const BSParser::VariableNode *> &p_finals,
		const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, bool p_in_init,
		bool p_flattened_trait_body) {
	if (p_node == nullptr || analyzer == nullptr) {
		return;
	}

	switch (p_node->type) {
		case BSParser::Node::SUITE: {
			const BSParser::SuiteNode *suite = static_cast<const BSParser::SuiteNode *>(p_node);
			for (int i = 0; i < suite->statements.size(); i++) {
				scan_illegal_final_writes(suite->statements[i], p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			}
		} break;
		case BSParser::Node::IF: {
			const BSParser::IfNode *if_node = static_cast<const BSParser::IfNode *>(p_node);
			scan_illegal_final_writes(if_node->condition, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(if_node->true_block, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(if_node->false_block, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case BSParser::Node::FOR: {
			const BSParser::ForNode *for_node = static_cast<const BSParser::ForNode *>(p_node);
			scan_illegal_final_writes(for_node->list, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(for_node->loop, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case BSParser::Node::WHILE: {
			const BSParser::WhileNode *while_node = static_cast<const BSParser::WhileNode *>(p_node);
			scan_illegal_final_writes(while_node->condition, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(while_node->loop, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case BSParser::Node::MATCH: {
			const BSParser::MatchNode *match_node = static_cast<const BSParser::MatchNode *>(p_node);
			scan_illegal_final_writes(match_node->test, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			for (int i = 0; i < match_node->branches.size(); i++) {
				if (match_node->branches[i] != nullptr) {
					scan_illegal_final_writes(match_node->branches[i]->guard_body, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
					scan_illegal_final_writes(match_node->branches[i]->block, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
				}
			}
		} break;
		case BSParser::Node::ASSIGNMENT: {
			const BSParser::AssignmentNode *assignment = static_cast<const BSParser::AssignmentNode *>(p_node);
			bool is_self_receiver = false;
			const BSParser::VariableNode *target = final_member_assignment_target(assignment->assignee, p_finals, p_finals_by_name, p_scope, &is_self_receiver, p_flattened_trait_body);
			if (p_scope == FinalAssignmentScope::LOCAL) {
				if (target != nullptr && p_finals.has(target) && !p_in_init) {
					analyzer->push_error(vformat(R"(Final variable "%s" cannot be assigned inside a lambda.)", target->identifier->name), assignment->assignee);
				}
			} else if (target != nullptr && !(p_finals.has(target) && is_self_receiver && p_in_init)) {
				// Only legal write: this class's own final on self, lexically in `_init` / `_static_init`.
				const char *slot_function = p_scope == FinalAssignmentScope::STATIC_MEMBER ? "_static_init()" : "_init()";
				analyzer->push_error(vformat(R"*(Final variable "%s" can only be assigned in its declaration or in "%s".)*", target->identifier->name, slot_function), assignment->assignee);
			}
			scan_illegal_final_writes(assignment->assignee, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(assignment->assigned_value, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case BSParser::Node::LAMBDA: {
			const BSParser::LambdaNode *lambda = static_cast<const BSParser::LambdaNode *>(p_node);
			if (lambda->function != nullptr) {
				scan_illegal_final_writes(lambda->function->body, p_finals, p_finals_by_name, p_scope, false, p_flattened_trait_body);
			}
		} break;
		case BSParser::Node::VARIABLE: {
			const BSParser::VariableNode *variable = static_cast<const BSParser::VariableNode *>(p_node);
			scan_illegal_final_writes(variable->initializer, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case BSParser::Node::VARIABLE_DESTRUCTURE: {
			const BSParser::VariableDestructureNode *destructure = static_cast<const BSParser::VariableDestructureNode *>(p_node);
			scan_illegal_final_writes(destructure->initializer, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case BSParser::Node::RETURN: {
			const BSParser::ReturnNode *return_node = static_cast<const BSParser::ReturnNode *>(p_node);
			scan_illegal_final_writes(return_node->return_value, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case BSParser::Node::ASSERT: {
			const BSParser::AssertNode *assert_node = static_cast<const BSParser::AssertNode *>(p_node);
			scan_illegal_final_writes(assert_node->condition, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(assert_node->message, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case BSParser::Node::BINARY_OPERATOR: {
			const BSParser::BinaryOpNode *binary = static_cast<const BSParser::BinaryOpNode *>(p_node);
			scan_illegal_final_writes(binary->left_operand, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(binary->right_operand, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case BSParser::Node::UNARY_OPERATOR: {
			const BSParser::UnaryOpNode *unary = static_cast<const BSParser::UnaryOpNode *>(p_node);
			scan_illegal_final_writes(unary->operand, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case BSParser::Node::TERNARY_OPERATOR: {
			const BSParser::TernaryOpNode *ternary = static_cast<const BSParser::TernaryOpNode *>(p_node);
			scan_illegal_final_writes(ternary->condition, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(ternary->true_expr, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(ternary->false_expr, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case BSParser::Node::CALL: {
			const BSParser::CallNode *call = static_cast<const BSParser::CallNode *>(p_node);
			scan_illegal_final_writes(call->callee, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			for (int i = 0; i < call->arguments.size(); i++) {
				scan_illegal_final_writes(call->arguments[i], p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			}
		} break;
		case BSParser::Node::SUBSCRIPT: {
			const BSParser::SubscriptNode *subscript = static_cast<const BSParser::SubscriptNode *>(p_node);
			scan_illegal_final_writes(subscript->base, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			if (!subscript->is_attribute) {
				scan_illegal_final_writes(subscript->index, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			}
		} break;
		case BSParser::Node::ARRAY: {
			const BSParser::ArrayNode *array = static_cast<const BSParser::ArrayNode *>(p_node);
			for (int i = 0; i < array->elements.size(); i++) {
				scan_illegal_final_writes(array->elements[i], p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			}
		} break;
		case BSParser::Node::DICTIONARY: {
			const BSParser::DictionaryNode *dictionary = static_cast<const BSParser::DictionaryNode *>(p_node);
			for (int i = 0; i < dictionary->elements.size(); i++) {
				scan_illegal_final_writes(dictionary->elements[i].key, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
				scan_illegal_final_writes(dictionary->elements[i].value, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			}
		} break;
		default:
			break;
	}
}

void BSAnalyzer::FlowFinalityContext::check_final_reads_in_expression(const BSParser::ExpressionNode *p_expression,
		const HashSet<const BSParser::VariableNode *> &p_finals,
		const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, const FinalAssignmentState &p_state,
		bool p_flattened_trait_body) {
	if (p_expression == nullptr || analyzer == nullptr) {
		return;
	}

	bool is_self_receiver = false;
	const BSParser::VariableNode *referenced = final_member_assignment_target(p_expression, p_finals, p_finals_by_name, p_scope, &is_self_receiver, p_flattened_trait_body);
	if (referenced != nullptr && is_self_receiver && p_finals.has(referenced) && !p_state.assigned.has(referenced)) {
		analyzer->push_error(vformat(R"(Final variable "%s" may be used before assignment.)", referenced->identifier->name), p_expression);
	}

	switch (p_expression->type) {
		case BSParser::Node::BINARY_OPERATOR: {
			const BSParser::BinaryOpNode *binary = static_cast<const BSParser::BinaryOpNode *>(p_expression);
			check_final_reads_in_expression(binary->left_operand, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			check_final_reads_in_expression(binary->right_operand, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
		} break;
		case BSParser::Node::UNARY_OPERATOR: {
			const BSParser::UnaryOpNode *unary = static_cast<const BSParser::UnaryOpNode *>(p_expression);
			check_final_reads_in_expression(unary->operand, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
		} break;
		case BSParser::Node::TERNARY_OPERATOR: {
			const BSParser::TernaryOpNode *ternary = static_cast<const BSParser::TernaryOpNode *>(p_expression);
			check_final_reads_in_expression(ternary->condition, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			check_final_reads_in_expression(ternary->true_expr, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			check_final_reads_in_expression(ternary->false_expr, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
		} break;
		case BSParser::Node::CALL: {
			const BSParser::CallNode *call = static_cast<const BSParser::CallNode *>(p_expression);
			check_final_reads_in_expression(call->callee, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			for (int i = 0; i < call->arguments.size(); i++) {
				check_final_reads_in_expression(call->arguments[i], p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			}
		} break;
		case BSParser::Node::SUBSCRIPT: {
			const BSParser::SubscriptNode *subscript = static_cast<const BSParser::SubscriptNode *>(p_expression);
			if (referenced == nullptr || !is_self_receiver) {
				check_final_reads_in_expression(subscript->base, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
				if (!subscript->is_attribute) {
					check_final_reads_in_expression(subscript->index, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
				}
			}
		} break;
		case BSParser::Node::ARRAY: {
			const BSParser::ArrayNode *array = static_cast<const BSParser::ArrayNode *>(p_expression);
			for (int i = 0; i < array->elements.size(); i++) {
				check_final_reads_in_expression(array->elements[i], p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			}
		} break;
		case BSParser::Node::DICTIONARY: {
			const BSParser::DictionaryNode *dictionary = static_cast<const BSParser::DictionaryNode *>(p_expression);
			for (int i = 0; i < dictionary->elements.size(); i++) {
				check_final_reads_in_expression(dictionary->elements[i].key, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
				check_final_reads_in_expression(dictionary->elements[i].value, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			}
		} break;
		case BSParser::Node::ASSIGNMENT: {
			const BSParser::AssignmentNode *assignment = static_cast<const BSParser::AssignmentNode *>(p_expression);
			check_final_reads_in_expression(assignment->assigned_value, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
		} break;
		case BSParser::Node::LAMBDA:
			// Nested lambda scopes are analyzed independently via collect_local_finals.
			break;
		default:
			break;
	}
}

void BSAnalyzer::FlowFinalityContext::analyze_final_definite_assignment_statement(const BSParser::Node *p_statement,
		const HashSet<const BSParser::VariableNode *> &p_finals,
		const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, FinalAssignmentState &r_state,
		HashSet<const BSParser::VariableNode *> &r_assigned_anywhere, bool p_flattened_trait_body) {
	if (p_statement == nullptr || !r_state.reachable || analyzer == nullptr) {
		return;
	}

	switch (p_statement->type) {
		case BSParser::Node::ASSIGNMENT: {
			const BSParser::AssignmentNode *assignment = static_cast<const BSParser::AssignmentNode *>(p_statement);
			check_final_reads_in_expression(assignment->assigned_value, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);

			bool is_self_receiver = false;
			const BSParser::VariableNode *target = final_member_assignment_target(assignment->assignee, p_finals, p_finals_by_name, p_scope, &is_self_receiver, p_flattened_trait_body);
			if (target == nullptr || !is_self_receiver || !p_finals.has(target)) {
				check_final_reads_in_expression(assignment->assignee, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
				break;
			}

			if (r_state.maybe_assigned.has(target)) {
				analyzer->push_error(vformat(R"(Cannot assign to final variable "%s"; it is already assigned.)", target->identifier->name), assignment->assignee);
			} else if (assignment->operation != BSParser::AssignmentNode::OP_NONE) {
				analyzer->push_error(vformat(R"(Final variable "%s" may be used before assignment.)", target->identifier->name), assignment->assignee);
			}
			r_state.assigned.insert(target);
			r_state.maybe_assigned.insert(target);
			r_assigned_anywhere.insert(target);
		} break;
		case BSParser::Node::IF: {
			const BSParser::IfNode *if_node = static_cast<const BSParser::IfNode *>(p_statement);
			check_final_reads_in_expression(if_node->condition, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);

			FinalAssignmentState true_state = r_state;
			analyze_final_definite_assignment_suite(if_node->true_block, p_finals, p_finals_by_name, p_scope, true_state, r_assigned_anywhere, p_flattened_trait_body);

			FinalAssignmentState false_state = r_state;
			if (if_node->false_block != nullptr) {
				analyze_final_definite_assignment_suite(if_node->false_block, p_finals, p_finals_by_name, p_scope, false_state, r_assigned_anywhere, p_flattened_trait_body);
			}

			merge_final_assignment_branches(true_state, false_state, r_state);
		} break;
		case BSParser::Node::FOR: {
			const BSParser::ForNode *for_node = static_cast<const BSParser::ForNode *>(p_statement);
			check_final_reads_in_expression(for_node->list, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
			FinalAssignmentState body_state = r_state;
			analyze_final_definite_assignment_suite(for_node->loop, p_finals, p_finals_by_name, p_scope, body_state, r_assigned_anywhere, p_flattened_trait_body);
			for (const BSParser::VariableNode *variable : body_state.maybe_assigned) {
				r_state.maybe_assigned.insert(variable);
			}
		} break;
		case BSParser::Node::WHILE: {
			const BSParser::WhileNode *while_node = static_cast<const BSParser::WhileNode *>(p_statement);
			check_final_reads_in_expression(while_node->condition, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
			FinalAssignmentState body_state = r_state;
			analyze_final_definite_assignment_suite(while_node->loop, p_finals, p_finals_by_name, p_scope, body_state, r_assigned_anywhere, p_flattened_trait_body);
			for (const BSParser::VariableNode *variable : body_state.maybe_assigned) {
				r_state.maybe_assigned.insert(variable);
			}
		} break;
		case BSParser::Node::MATCH: {
			const BSParser::MatchNode *match_node = static_cast<const BSParser::MatchNode *>(p_statement);
			check_final_reads_in_expression(match_node->test, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);

			bool has_unguarded_catchall = false;
			bool has_branch = false;
			FinalAssignmentState merged;
			merged.reachable = false;
			for (int i = 0; i < match_node->branches.size(); i++) {
				const BSParser::MatchBranchNode *branch = match_node->branches[i];
				if (branch == nullptr) {
					continue;
				}
				FinalAssignmentState branch_state = r_state;
				analyze_final_definite_assignment_suite(branch->guard_body, p_finals, p_finals_by_name, p_scope, branch_state, r_assigned_anywhere, p_flattened_trait_body);
				analyze_final_definite_assignment_suite(branch->block, p_finals, p_finals_by_name, p_scope, branch_state, r_assigned_anywhere, p_flattened_trait_body);
				if (!has_branch) {
					merged = branch_state;
					has_branch = true;
				} else {
					FinalAssignmentState intersection;
					merge_final_assignment_branches(merged, branch_state, intersection);
					merged = intersection;
				}
				if (branch->has_wildcard && branch->guard_body == nullptr) {
					has_unguarded_catchall = true;
					break;
				}
			}

			if (!has_branch) {
				break;
			}
			if (!has_unguarded_catchall) {
				FinalAssignmentState fallthrough = r_state;
				FinalAssignmentState intersection;
				merge_final_assignment_branches(merged, fallthrough, intersection);
				merged = intersection;
			}
			r_state = merged;
		} break;
		case BSParser::Node::RETURN: {
			const BSParser::ReturnNode *return_node = static_cast<const BSParser::ReturnNode *>(p_statement);
			check_final_reads_in_expression(return_node->return_value, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
			// Member/static blank finals must be assigned before `_init`/`_static_init` returns.
			if (p_scope != FinalAssignmentScope::LOCAL) {
				const char *init_name = p_scope == FinalAssignmentScope::STATIC_MEMBER ? "_static_init()" : "_init()";
				for (const BSParser::VariableNode *variable : p_finals) {
					if (!r_state.assigned.has(variable)) {
						analyzer->push_error(vformat(R"*(Final variable "%s" must be definitely assigned before returning from "%s".)*", variable->identifier->name, init_name), return_node);
					}
				}
			}
			r_state.reachable = false;
		} break;
		case BSParser::Node::BREAK:
		case BSParser::Node::CONTINUE: {
			r_state.reachable = false;
		} break;
		case BSParser::Node::VARIABLE: {
			const BSParser::VariableNode *variable = static_cast<const BSParser::VariableNode *>(p_statement);
			check_final_reads_in_expression(variable->initializer, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
			if (p_scope == FinalAssignmentScope::LOCAL && variable->is_final && p_finals.has(variable) && variable->initializer != nullptr) {
				r_state.assigned.insert(variable);
				r_state.maybe_assigned.insert(variable);
				r_assigned_anywhere.insert(variable);
			}
		} break;
		case BSParser::Node::VARIABLE_DESTRUCTURE: {
			const BSParser::VariableDestructureNode *destructure = static_cast<const BSParser::VariableDestructureNode *>(p_statement);
			check_final_reads_in_expression(destructure->initializer, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
			if (p_scope == FinalAssignmentScope::LOCAL && destructure->initializer != nullptr) {
				for (int i = 0; i < destructure->bindings.size(); i++) {
					const BSParser::VariableNode *binding = destructure->bindings[i];
					if (binding == nullptr || !p_finals.has(binding)) {
						continue;
					}
					r_state.assigned.insert(binding);
					r_state.maybe_assigned.insert(binding);
					r_assigned_anywhere.insert(binding);
				}
			}
		} break;
		case BSParser::Node::ASSERT: {
			const BSParser::AssertNode *assert_node = static_cast<const BSParser::AssertNode *>(p_statement);
			check_final_reads_in_expression(assert_node->condition, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
			check_final_reads_in_expression(assert_node->message, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
		} break;
		case BSParser::Node::SUITE: {
			analyze_final_definite_assignment_suite(static_cast<const BSParser::SuiteNode *>(p_statement), p_finals, p_finals_by_name, p_scope, r_state, r_assigned_anywhere, p_flattened_trait_body);
		} break;
		default: {
			if (p_statement->is_expression()) {
				check_final_reads_in_expression(static_cast<const BSParser::ExpressionNode *>(p_statement), p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
				if (p_statement->type == BSParser::Node::CALL && static_cast<const BSParser::CallNode *>(p_statement)->is_noreturn) {
					r_state.reachable = false;
				}
			}
		} break;
	}
}

void BSAnalyzer::FlowFinalityContext::analyze_final_definite_assignment_suite(const BSParser::SuiteNode *p_suite,
		const HashSet<const BSParser::VariableNode *> &p_finals,
		const HashMap<StringName, const BSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, FinalAssignmentState &r_state,
		HashSet<const BSParser::VariableNode *> &r_assigned_anywhere, bool p_flattened_trait_body) {
	if (p_suite == nullptr) {
		return;
	}
	for (int i = 0; i < p_suite->statements.size(); i++) {
		analyze_final_definite_assignment_statement(p_suite->statements[i], p_finals, p_finals_by_name, p_scope, r_state, r_assigned_anywhere, p_flattened_trait_body);
	}
}

} // namespace barista_script
