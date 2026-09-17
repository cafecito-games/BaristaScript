/**************************************************************************/
/*  bs_compiler_expressions.cpp                                           */
/*                                                                        */
/*  Expression lowering. Provenance: fs_compiler.cpp `_parse_expression`  */
/*  @ c9d5e35.                                                            */
/*                                                                        */
/*  Temporaries are strictly last-in first-out: a result temporary is     */
/*  taken before its operands so it outlives them, and every operand is   */
/*  released in the reverse of the order it was taken.                    */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_compiler.h"
#include "bs_core_constants.h"

namespace barista_script {

BSCodeGenerator::Address BSCompiler::parse_identifier(CodeGen &p_codegen, Error &r_error, const BSParser::IdentifierNode *p_identifier) {
	const StringName &name = p_identifier->name;
	if (p_codegen.parameters.has(name)) {
		return p_codegen.parameters[name];
	}
	if (p_codegen.locals.has(name)) {
		return p_codegen.locals[name];
	}
	const int member_index = p_codegen.script != nullptr ? p_codegen.script->get_member_index(name) : -1;
	if (member_index >= 0) {
		return BSCodeGenerator::Address(BSCodeGenerator::Address::MEMBER, member_index,
				member_slot_type(p_identifier->get_datatype()));
	}
	if (Engine::get_singleton()->has_singleton(name)) {
		const BSCodeGenerator::Address target = p_codegen.add_temporary(member_slot_type(p_identifier->get_datatype()));
		p_codegen.generator->write_store_named_global(target, name);
		return target;
	}
	if (BSCoreConstants::is_global_constant(name)) {
		const BSCodeGenerator::Address target = p_codegen.add_temporary(member_slot_type(p_identifier->get_datatype()));
		p_codegen.generator->write_store_global(target, BSCoreConstants::get_global_constant_index(name), name);
		return target;
	}
	// The only remaining reading that has a lowering is a property the native base declares, which
	// the owner object answers. Every other classification -- a signal, an inner class, a method
	// used as a value, a static variable -- would silently become a property read that returns null,
	// so it is refused by name instead.
	if (p_identifier->source != BSParser::IdentifierNode::INHERITED_VARIABLE) {
		set_error(vformat(R"(The runtime cannot compile a reference to "%s" here.)", String(name)), p_identifier);
		r_error = ERR_COMPILATION_FAILED;
		return BSCodeGenerator::Address();
	}
	const BSCodeGenerator::Address target = p_codegen.add_temporary(member_slot_type(p_identifier->get_datatype()));
	p_codegen.generator->write_get_member(target, name);
	return target;
}

BSCodeGenerator::Address BSCompiler::parse_expression(CodeGen &p_codegen, Error &r_error, const BSParser::ExpressionNode *p_expression, bool p_discard_result) {
	BSCodeGenerator *generator = p_codegen.generator;

	// A value the analyzer already reduced needs no instruction at all. Object-valued constants are
	// excluded: a live object is identity, not a value the constant pool can own.
	if (p_expression->is_constant && p_expression->reduced_value.get_type() != Variant::OBJECT &&
			!p_expression->get_datatype().is_meta_type) {
		return p_codegen.add_constant(p_expression->reduced_value);
	}

	switch (p_expression->type) {
		case BSParser::Node::LITERAL:
			return p_codegen.add_constant(static_cast<const BSParser::LiteralNode *>(p_expression)->value);

		case BSParser::Node::SELF: {
			if (p_codegen.function_node != nullptr && p_codegen.function_node->is_static) {
				set_error(R"(A static function has no "self".)", p_expression);
				r_error = ERR_COMPILATION_FAILED;
				return BSCodeGenerator::Address();
			}
			return BSCodeGenerator::Address(BSCodeGenerator::Address::SELF);
		}

		case BSParser::Node::IDENTIFIER:
			return parse_identifier(p_codegen, r_error, static_cast<const BSParser::IdentifierNode *>(p_expression));

		case BSParser::Node::ARRAY: {
			const BSParser::ArrayNode *node = static_cast<const BSParser::ArrayNode *>(p_expression);
			const BSCodeGenerator::Address result = p_codegen.add_temporary(member_slot_type(node->get_datatype()));
			Vector<BSCodeGenerator::Address> elements;
			elements.resize(node->elements.size());
			for (int i = 0; i < elements.size(); i++) {
				elements.write[i] = parse_expression(p_codegen, r_error, node->elements[i]);
				if (r_error != OK) {
					return BSCodeGenerator::Address();
				}
			}
			generator->write_construct_array(result, elements);
			for (int i = elements.size() - 1; i >= 0; i--) {
				if (elements[i].mode == BSCodeGenerator::Address::TEMPORARY) {
					generator->pop_temporary();
				}
			}
			return result;
		}

		case BSParser::Node::DICTIONARY: {
			const BSParser::DictionaryNode *node = static_cast<const BSParser::DictionaryNode *>(p_expression);
			const BSCodeGenerator::Address result = p_codegen.add_temporary(member_slot_type(node->get_datatype()));
			Vector<BSCodeGenerator::Address> entries;
			for (const BSParser::DictionaryNode::Pair &pair : node->elements) {
				const BSCodeGenerator::Address key = parse_expression(p_codegen, r_error, pair.key);
				if (r_error != OK) {
					return BSCodeGenerator::Address();
				}
				const BSCodeGenerator::Address value = parse_expression(p_codegen, r_error, pair.value);
				if (r_error != OK) {
					return BSCodeGenerator::Address();
				}
				entries.push_back(key);
				entries.push_back(value);
			}
			generator->write_construct_dictionary(result, entries);
			for (int i = entries.size() - 1; i >= 0; i--) {
				if (entries[i].mode == BSCodeGenerator::Address::TEMPORARY) {
					generator->pop_temporary();
				}
			}
			return result;
		}

		case BSParser::Node::UNARY_OPERATOR: {
			const BSParser::UnaryOpNode *node = static_cast<const BSParser::UnaryOpNode *>(p_expression);
			const BSCodeGenerator::Address result = p_codegen.add_temporary(member_slot_type(node->get_datatype()));
			const BSCodeGenerator::Address operand = parse_expression(p_codegen, r_error, node->operand);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			generator->write_unary_operator(result, node->variant_op, operand);
			if (operand.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			return result;
		}

		case BSParser::Node::BINARY_OPERATOR: {
			const BSParser::BinaryOpNode *node = static_cast<const BSParser::BinaryOpNode *>(p_expression);
			const BSCodeGenerator::Address result = p_codegen.add_temporary(member_slot_type(node->get_datatype()));
			const bool is_and = node->operation == BSParser::BinaryOpNode::OP_LOGIC_AND;
			const bool is_or = node->operation == BSParser::BinaryOpNode::OP_LOGIC_OR;
			if (is_and || is_or) {
				const BSCodeGenerator::Address left = parse_expression(p_codegen, r_error, node->left_operand);
				if (r_error != OK) {
					return BSCodeGenerator::Address();
				}
				if (is_and) {
					generator->write_and_left_operand(left);
				} else {
					generator->write_or_left_operand(left);
				}
				const BSCodeGenerator::Address right = parse_expression(p_codegen, r_error, node->right_operand);
				if (r_error != OK) {
					return BSCodeGenerator::Address();
				}
				if (is_and) {
					generator->write_and_right_operand(right);
					generator->write_end_and(result);
				} else {
					generator->write_or_right_operand(right);
					generator->write_end_or(result);
				}
				if (right.mode == BSCodeGenerator::Address::TEMPORARY) {
					generator->pop_temporary();
				}
				if (left.mode == BSCodeGenerator::Address::TEMPORARY) {
					generator->pop_temporary();
				}
				return result;
			}
			const BSCodeGenerator::Address left = parse_expression(p_codegen, r_error, node->left_operand);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			const BSCodeGenerator::Address right = parse_expression(p_codegen, r_error, node->right_operand);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			generator->write_binary_operator(result, node->variant_op, left, right);
			if (right.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (left.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			return result;
		}

		case BSParser::Node::TERNARY_OPERATOR: {
			const BSParser::TernaryOpNode *node = static_cast<const BSParser::TernaryOpNode *>(p_expression);
			const BSCodeGenerator::Address result = p_codegen.add_temporary(member_slot_type(node->get_datatype()));
			generator->write_start_ternary(result);
			const BSCodeGenerator::Address condition = parse_expression(p_codegen, r_error, node->condition);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			generator->write_ternary_condition(condition);
			if (condition.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			const BSCodeGenerator::Address true_value = parse_expression(p_codegen, r_error, node->true_expr);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			generator->write_ternary_true_expr(true_value);
			if (true_value.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			const BSCodeGenerator::Address false_value = parse_expression(p_codegen, r_error, node->false_expr);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			generator->write_ternary_false_expr(false_value);
			if (false_value.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			generator->write_end_ternary();
			return result;
		}

		case BSParser::Node::TYPE_TEST: {
			const BSParser::TypeTestNode *node = static_cast<const BSParser::TypeTestNode *>(p_expression);
			if (!node->case_binds.is_empty()) {
				set_error("The runtime cannot compile a type test that binds a case payload.", p_expression);
				r_error = ERR_COMPILATION_FAILED;
				return BSCodeGenerator::Address();
			}
			const BSCodeGenerator::Address result = p_codegen.add_temporary(member_slot_type(node->get_datatype()));
			const BSCodeGenerator::Address operand = parse_expression(p_codegen, r_error, node->operand);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			generator->write_type_test(result, operand, member_slot_type(node->test_datatype));
			if (operand.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (generator->has_error()) {
				set_error(vformat("The runtime cannot compile %s.", generator->get_error()), p_expression);
				r_error = ERR_COMPILATION_FAILED;
			}
			return result;
		}

		case BSParser::Node::CAST: {
			const BSParser::CastNode *node = static_cast<const BSParser::CastNode *>(p_expression);
			const BSCodeGenerator::Address result = p_codegen.add_temporary(member_slot_type(node->get_datatype()));
			const BSCodeGenerator::Address operand = parse_expression(p_codegen, r_error, node->operand);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			generator->write_cast(result, operand, member_slot_type(node->get_datatype()));
			if (operand.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (generator->has_error()) {
				set_error(vformat("The runtime cannot compile %s.", generator->get_error()), p_expression);
				r_error = ERR_COMPILATION_FAILED;
			}
			return result;
		}

		case BSParser::Node::SUBSCRIPT: {
			const BSParser::SubscriptNode *node = static_cast<const BSParser::SubscriptNode *>(p_expression);
			if (node->base == nullptr) {
				set_error("The runtime cannot compile a contextual enum case.", p_expression);
				r_error = ERR_COMPILATION_FAILED;
				return BSCodeGenerator::Address();
			}
			// `self.member` with a compiled slot is the slot itself: going through the owner object
			// would ask the engine for a property the script, not the object, owns.
			if (node->is_attribute && node->base->type == BSParser::Node::SELF && p_codegen.script != nullptr) {
				const int member_index = p_codegen.script->get_member_index(node->attribute->name);
				if (member_index >= 0) {
					return BSCodeGenerator::Address(BSCodeGenerator::Address::MEMBER, member_index,
							member_slot_type(node->get_datatype()));
				}
			}
			const BSCodeGenerator::Address result = p_codegen.add_temporary(member_slot_type(node->get_datatype()));
			const BSCodeGenerator::Address base = parse_expression(p_codegen, r_error, node->base);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			if (node->is_attribute) {
				generator->write_get_named(result, node->attribute->name, base);
			} else {
				const BSCodeGenerator::Address index = parse_expression(p_codegen, r_error, node->index);
				if (r_error != OK) {
					return BSCodeGenerator::Address();
				}
				generator->write_get(result, index, base);
				if (index.mode == BSCodeGenerator::Address::TEMPORARY) {
					generator->pop_temporary();
				}
			}
			if (base.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			return result;
		}

		case BSParser::Node::CALL:
			return parse_call(p_codegen, r_error, static_cast<const BSParser::CallNode *>(p_expression), p_discard_result);

		case BSParser::Node::ASSIGNMENT:
			return parse_assignment(p_codegen, r_error, static_cast<const BSParser::AssignmentNode *>(p_expression));

		default: {
			set_error(vformat("The runtime cannot compile a %s expression.",
							  BSParser::get_node_type_name(p_expression->type)),
					p_expression);
			r_error = ERR_COMPILATION_FAILED;
			return BSCodeGenerator::Address();
		}
	}
}

BSCodeGenerator::Address BSCompiler::parse_call(CodeGen &p_codegen, Error &r_error, const BSParser::CallNode *p_call, bool p_discard_result) {
	BSCodeGenerator *generator = p_codegen.generator;

	if (p_call->is_tuple_construction || p_call->is_enum_case_construction || p_call->is_proxy_construct ||
			p_call->enum_call_kind != BSParser::CallNode::ENUM_CALL_NONE) {
		set_error("The runtime cannot compile this call form.", p_call);
		r_error = ERR_COMPILATION_FAILED;
		return BSCodeGenerator::Address();
	}

	// A discarded result needs no destination, which is what tells the emitter to use the call
	// opcode that writes nothing.
	const BSCodeGenerator::Address result = p_discard_result
			? BSCodeGenerator::Address()
			: p_codegen.add_temporary(member_slot_type(p_call->get_datatype()));

	BSCodeGenerator::Address receiver;
	bool has_receiver_temporary = false;
	bool receiver_is_self = false;
	if (p_call->get_callee_type() == BSParser::Node::SUBSCRIPT) {
		const BSParser::SubscriptNode *callee = static_cast<const BSParser::SubscriptNode *>(p_call->callee);
		if (!callee->is_attribute || callee->base == nullptr) {
			set_error("The runtime cannot compile this call target.", p_call);
			r_error = ERR_COMPILATION_FAILED;
			return BSCodeGenerator::Address();
		}
		receiver = parse_expression(p_codegen, r_error, callee->base);
		if (r_error != OK) {
			return BSCodeGenerator::Address();
		}
		has_receiver_temporary = receiver.mode == BSCodeGenerator::Address::TEMPORARY;
	} else {
		receiver_is_self = true;
	}

	Vector<BSCodeGenerator::Address> arguments;
	arguments.resize(p_call->arguments.size());
	const Vector<int> &evaluation_order = p_call->argument_evaluation_order;
	int argument_temporaries = 0;
	for (int position = 0; position < p_call->arguments.size(); position++) {
		const int index = evaluation_order.is_empty() ? position : evaluation_order[position];
		const BSCodeGenerator::Address argument = parse_expression(p_codegen, r_error, p_call->arguments[index]);
		if (r_error != OK) {
			return BSCodeGenerator::Address();
		}
		if (argument.mode == BSCodeGenerator::Address::TEMPORARY) {
			argument_temporaries++;
		}
		arguments.write[index] = argument;
	}

	if (p_call->is_super) {
		generator->write_super_call(result, p_call->function_name, arguments);
	} else if (receiver_is_self) {
		const StringName &function_name = p_call->function_name;
		const Variant::Type builtin_type = BSParser::get_builtin_type(function_name);
		MethodInfo utility_info;
		if (builtin_type < Variant::VARIANT_MAX) {
			generator->write_construct(result, builtin_type, arguments);
		} else if (BSCoreConstants::get_utility_function(function_name, utility_info)) {
			generator->write_call_utility(result, function_name, arguments);
		} else {
			generator->write_call_self(result, function_name, arguments);
		}
	} else {
		generator->write_call(result, receiver, p_call->function_name, arguments);
	}

	for (int i = 0; i < argument_temporaries; i++) {
		generator->pop_temporary();
	}
	if (has_receiver_temporary) {
		generator->pop_temporary();
	}
	if (generator->has_error()) {
		set_error(vformat("The runtime cannot compile %s.", generator->get_error()), p_call);
		r_error = ERR_COMPILATION_FAILED;
	}
	return result;
}

BSCodeGenerator::Address BSCompiler::parse_assignment(CodeGen &p_codegen, Error &r_error, const BSParser::AssignmentNode *p_assignment) {
	BSCodeGenerator *generator = p_codegen.generator;
	const BSParser::ExpressionNode *assignee = p_assignment->assignee;
	const bool has_operation = p_assignment->operation != BSParser::AssignmentNode::OP_NONE;

	// A property of the owner object, reached either bare or through `self`, is written through the
	// object rather than into a compiled slot.
	StringName owner_property;
	if (assignee->type == BSParser::Node::IDENTIFIER) {
		const BSParser::IdentifierNode *identifier = static_cast<const BSParser::IdentifierNode *>(assignee);
		const StringName &name = identifier->name;
		if (!is_local_or_parameter(p_codegen, name) &&
				(p_codegen.script == nullptr || p_codegen.script->get_member_index(name) < 0)) {
			// As on the reading side, only a property of the native base is written through the owner.
			if (identifier->source != BSParser::IdentifierNode::INHERITED_VARIABLE) {
				set_error(vformat(R"(The runtime cannot compile an assignment to "%s".)", String(name)), assignee);
				r_error = ERR_COMPILATION_FAILED;
				return BSCodeGenerator::Address();
			}
			owner_property = name;
		}
	} else if (assignee->type == BSParser::Node::SUBSCRIPT) {
		const BSParser::SubscriptNode *subscript = static_cast<const BSParser::SubscriptNode *>(assignee);
		if (subscript->is_attribute && subscript->base != nullptr && subscript->base->type == BSParser::Node::SELF &&
				(p_codegen.script == nullptr || p_codegen.script->get_member_index(subscript->attribute->name) < 0)) {
			owner_property = subscript->attribute->name;
		}
	}

	if (owner_property != StringName()) {
		const BSCodeGenerator::Address value = parse_expression(p_codegen, r_error, p_assignment->assigned_value);
		if (r_error != OK) {
			return BSCodeGenerator::Address();
		}
		BSCodeGenerator::Address stored = value;
		if (has_operation) {
			const BSCodeGenerator::Address combined = p_codegen.add_temporary(member_slot_type(p_assignment->get_datatype()));
			const BSCodeGenerator::Address current = p_codegen.add_temporary(member_slot_type(assignee->get_datatype()));
			generator->write_get_member(current, owner_property);
			generator->write_binary_operator(combined, p_assignment->variant_op, current, value);
			generator->pop_temporary();
			stored = combined;
		}
		generator->write_set_member(stored, owner_property);
		if (stored.mode == BSCodeGenerator::Address::TEMPORARY) {
			generator->pop_temporary();
		}
		if (has_operation && value.mode == BSCodeGenerator::Address::TEMPORARY) {
			generator->pop_temporary();
		}
		return BSCodeGenerator::Address();
	}

	if (assignee->type == BSParser::Node::SUBSCRIPT) {
		const BSParser::SubscriptNode *subscript = static_cast<const BSParser::SubscriptNode *>(assignee);
		if (subscript->base == nullptr) {
			set_error("The runtime cannot compile this assignment target.", assignee);
			r_error = ERR_COMPILATION_FAILED;
			return BSCodeGenerator::Address();
		}
		const bool is_self_member = subscript->is_attribute && subscript->base->type == BSParser::Node::SELF &&
				p_codegen.script != nullptr && p_codegen.script->get_member_index(subscript->attribute->name) >= 0;
		if (!is_self_member) {
			const BSCodeGenerator::Address base = parse_expression(p_codegen, r_error, subscript->base);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			BSCodeGenerator::Address index;
			if (!subscript->is_attribute) {
				index = parse_expression(p_codegen, r_error, subscript->index);
				if (r_error != OK) {
					return BSCodeGenerator::Address();
				}
			}
			const BSCodeGenerator::Address value = parse_expression(p_codegen, r_error, p_assignment->assigned_value);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			BSCodeGenerator::Address stored = value;
			if (has_operation) {
				const BSCodeGenerator::Address combined = p_codegen.add_temporary(member_slot_type(p_assignment->get_datatype()));
				const BSCodeGenerator::Address current = p_codegen.add_temporary(member_slot_type(assignee->get_datatype()));
				if (subscript->is_attribute) {
					generator->write_get_named(current, subscript->attribute->name, base);
				} else {
					generator->write_get(current, index, base);
				}
				generator->write_binary_operator(combined, p_assignment->variant_op, current, value);
				generator->pop_temporary();
				stored = combined;
			}
			if (subscript->is_attribute) {
				generator->write_set_named(base, subscript->attribute->name, stored);
			} else {
				generator->write_set(base, index, stored);
			}
			if (stored.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (has_operation && value.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (index.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (base.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			return BSCodeGenerator::Address();
		}
	}

	BSCodeGenerator::Address target;
	if (assignee->type == BSParser::Node::SUBSCRIPT) {
		const BSParser::SubscriptNode *subscript = static_cast<const BSParser::SubscriptNode *>(assignee);
		target = BSCodeGenerator::Address(BSCodeGenerator::Address::MEMBER,
				p_codegen.script->get_member_index(subscript->attribute->name), member_slot_type(assignee->get_datatype()));
	} else if (assignee->type == BSParser::Node::IDENTIFIER) {
		target = parse_identifier(p_codegen, r_error, static_cast<const BSParser::IdentifierNode *>(assignee));
		if (r_error != OK) {
			return BSCodeGenerator::Address();
		}
	} else {
		set_error("The runtime cannot compile this assignment target.", assignee);
		r_error = ERR_COMPILATION_FAILED;
		return BSCodeGenerator::Address();
	}

	const BSCodeGenerator::Address value = parse_expression(p_codegen, r_error, p_assignment->assigned_value);
	if (r_error != OK) {
		return BSCodeGenerator::Address();
	}
	BSCodeGenerator::Address stored = value;
	if (has_operation) {
		const BSCodeGenerator::Address combined = p_codegen.add_temporary(member_slot_type(p_assignment->get_datatype()));
		generator->write_binary_operator(combined, p_assignment->variant_op, target, value);
		stored = combined;
	}
	if (p_assignment->use_conversion_assign) {
		generator->write_assign_with_conversion(target, stored);
	} else {
		generator->write_assign(target, stored);
	}
	if (stored.mode == BSCodeGenerator::Address::TEMPORARY) {
		generator->pop_temporary();
	}
	if (has_operation && value.mode == BSCodeGenerator::Address::TEMPORARY) {
		generator->pop_temporary();
	}
	if (target.mode == BSCodeGenerator::Address::TEMPORARY) {
		generator->pop_temporary();
	}
	return BSCodeGenerator::Address();
}

} // namespace barista_script
