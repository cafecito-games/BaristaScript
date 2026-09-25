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
#include "bs_utility_functions.h"

namespace barista_script {

BSCodeGenerator::Address BSCompiler::parse_identifier(CodeGen &p_codegen, Error &r_error, const BSParser::IdentifierNode *p_identifier) {
	const StringName &name = p_identifier->name;
	if (p_codegen.parameters.has(name)) {
		return p_codegen.parameters[name];
	}
	if (p_codegen.locals.has(name)) {
		return p_codegen.locals[name];
	}
	if (p_identifier->source == BSParser::IdentifierNode::STATIC_VARIABLE) {
		int static_index = -1;
		BaristaScript *owner = find_static_owner(p_identifier->variable_source, static_index);
		if (owner == nullptr || static_index < 0) {
			set_error(vformat(R"(The runtime cannot resolve the static variable "%s".)", String(name)), p_identifier);
			r_error = ERR_COMPILATION_FAILED;
			return BSCodeGenerator::Address();
		}
		const BSCodeGenerator::Address target = p_codegen.add_temporary(member_slot_type(p_identifier->get_datatype()));
		const BSCodeGenerator::Address class_address = p_codegen.add_constant((int64_t)owner->get_instance_id());
		const StringName &getter = owner->static_getters[static_index];
		if (getter != StringName() && getter != p_codegen.function_name) {
			p_codegen.generator->write_call(target, class_address, getter, Vector<BSCodeGenerator::Address>());
		} else {
			p_codegen.generator->write_get_static_variable(target, class_address, static_index);
		}
		return target;
	}
	const int member_index = p_codegen.script != nullptr ? p_codegen.script->get_member_index(name) : -1;
	if (member_index >= 0) {
		const StringName &getter = p_codegen.script->get_member_getter(member_index);
		if (getter != StringName() && getter != p_codegen.function_name) {
			const BSCodeGenerator::Address target = p_codegen.add_temporary(member_slot_type(p_identifier->get_datatype()));
			p_codegen.generator->write_call_self(target, getter, Vector<BSCodeGenerator::Address>());
			return target;
		}
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
	if (p_identifier->source == BSParser::IdentifierNode::NATIVE_CLASS &&
			p_identifier->get_datatype().is_meta_type) {
		// Stock Godot exposes no native-class handle object through GDExtension. The language keeps
		// the class name as its private handle value; construction/static dispatch already carry the
		// same name directly in their opcodes, and Type[T] descriptors recognize this representation.
		return p_codegen.add_constant(StringName(name));
	}
	if (p_identifier->source == BSParser::IdentifierNode::STATIC_SELF_CLASS) {
		const BSCodeGenerator::Address target = p_codegen.add_temporary(member_slot_type(p_identifier->get_datatype()));
		p_codegen.generator->write_load_static_self_class(target);
		return target;
	}
	if (p_identifier->get_datatype().is_meta_type &&
			p_identifier->get_datatype().kind == BSParser::DataType::CLASS &&
			p_identifier->get_datatype().class_type != nullptr) {
		if (const Ref<BaristaScript> *class_script = class_scripts.getptr(p_identifier->get_datatype().class_type)) {
			return p_codegen.add_constant((int64_t)(*class_script)->get_instance_id());
		}
		if (p_identifier->get_datatype().class_type == p_codegen.class_node && p_codegen.script != nullptr) {
			return p_codegen.add_constant((int64_t)p_codegen.script->get_instance_id());
		}
	}
	if (p_identifier->get_datatype().is_meta_type &&
			p_identifier->get_datatype().kind == BSParser::DataType::SCRIPT &&
			p_identifier->get_datatype().script_type.is_valid()) {
		return p_codegen.add_constant((int64_t)p_identifier->get_datatype().script_type->get_instance_id());
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
	//
	// A container the program itself produces is excluded for the same reason, one step removed. The
	// analyzer folds `[]`, `{}` and `Array()` so a `const` declaration can hold them, and it makes
	// every folded container read-only so a constant cannot be edited through a reference. Pooling
	// that folded value at a *literal* or *constructor* site would hand every evaluation the same
	// read-only instance: `var x := []` would alias `var y := []`, and `x.push_back(...)` would fail
	// on a read-only array rather than grow a fresh one. Upstream never reaches the constant path
	// for these nodes at all, because its analyzer leaves an array literal non-constant
	// (fs_analyzer.cpp:6824 `reduce_array` @ c9d5e35); BaristaScript folds them for the constant
	// evaluator's sake, so the compiler is where the two uses are told apart. A read of a declared
	// `const` is an IDENTIFIER or an attribute access and still pools, which is what keeps
	// `features/constants_are_read_only` true.
	const Variant::Type reduced_type = p_expression->reduced_value.get_type();
	const bool writes_a_container_site = p_expression->type == BSParser::Node::ARRAY ||
			p_expression->type == BSParser::Node::DICTIONARY || p_expression->type == BSParser::Node::CALL;
	// Array and dictionary literals are always rebuildable now that the typed-container writers carry
	// their complete element descriptors. A folded call result is rebuildable only when it is untyped:
	// replaying the call is what preserves any richer runtime metadata that the literal writers cannot
	// infer from the call node itself.
	bool rebuildable_container = false;
	if (reduced_type == Variant::ARRAY) {
		rebuildable_container = p_expression->type == BSParser::Node::ARRAY ||
				!((Array)p_expression->reduced_value).is_typed();
	} else if (reduced_type == Variant::DICTIONARY) {
		rebuildable_container = p_expression->type == BSParser::Node::DICTIONARY ||
				!((Dictionary)p_expression->reduced_value).is_typed();
	}
	const bool produces_fresh_container = rebuildable_container && writes_a_container_site;
	if (p_expression->is_constant && reduced_type != Variant::OBJECT && !produces_fresh_container &&
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
			const BSParser::DataType result_type = member_slot_type(node->get_datatype());
			const BSCodeGenerator::Address result = p_codegen.add_temporary(result_type);
			Vector<BSCodeGenerator::Address> elements;
			elements.resize(node->elements.size());
			for (int i = 0; i < elements.size(); i++) {
				elements.write[i] = parse_expression(p_codegen, r_error, node->elements[i]);
				if (r_error != OK) {
					return BSCodeGenerator::Address();
				}
			}
			if (result_type.kind == BSParser::DataType::BUILTIN && result_type.builtin_type == Variant::ARRAY &&
					result_type.has_container_element_type(0)) {
				generator->write_construct_typed_array(result, result_type.get_container_element_type(0), elements);
			} else {
				generator->write_construct_array(result, elements);
			}
			for (int i = elements.size() - 1; i >= 0; i--) {
				if (elements[i].mode == BSCodeGenerator::Address::TEMPORARY) {
					generator->pop_temporary();
				}
			}
			return result;
		}

		case BSParser::Node::DICTIONARY: {
			const BSParser::DictionaryNode *node = static_cast<const BSParser::DictionaryNode *>(p_expression);
			const BSParser::DataType result_type = member_slot_type(node->get_datatype());
			const BSCodeGenerator::Address result = p_codegen.add_temporary(result_type);
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
			if (result_type.kind == BSParser::DataType::BUILTIN && result_type.builtin_type == Variant::DICTIONARY &&
					result_type.has_container_element_types()) {
				generator->write_construct_typed_dictionary(result,
						result_type.get_container_element_type_or_variant(0),
						result_type.get_container_element_type_or_variant(1), entries);
			} else {
				generator->write_construct_dictionary(result, entries);
			}
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
			const BSParser::DataType test_type = member_slot_type(node->test_datatype);
			if (test_type.kind == BSParser::DataType::ENUM) {
				generator->write_type_test_enum(result, operand, enum_declared_values(test_type), test_type.is_tagged_union);
			} else {
				generator->write_type_test(result, operand, test_type);
			}
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
					const StringName &getter = p_codegen.script->get_member_getter(member_index);
					if (getter != StringName() && getter != p_codegen.function_name) {
						const BSCodeGenerator::Address result = p_codegen.add_temporary(member_slot_type(node->get_datatype()));
						generator->write_call_self(result, getter, Vector<BSCodeGenerator::Address>());
						return result;
					}
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
	// A call written against a type rather than a value dispatches on the type itself. The two
	// spellings are distinguished here, before the receiver is evaluated, because neither has a
	// receiver expression to evaluate: `Node.new()` names a class, not an object, and `Vector2.ONE`
	// is not a value the frame holds. Upstream materializes an `FSNativeClass` object for the class
	// and dispatches an ordinary call on it; BaristaScript has no such wrapper, so the class is
	// carried in the instruction instead and the dedicated static-call opcodes do the dispatch.
	StringName native_static_class;
	Variant::Type builtin_static_type = Variant::VARIANT_MAX;
	if (p_call->get_callee_type() == BSParser::Node::SUBSCRIPT) {
		const BSParser::SubscriptNode *callee = static_cast<const BSParser::SubscriptNode *>(p_call->callee);
		if (!callee->is_attribute || callee->base == nullptr) {
			set_error("The runtime cannot compile this call target.", p_call);
			r_error = ERR_COMPILATION_FAILED;
			return BSCodeGenerator::Address();
		}
		if (callee->base->type == BSParser::Node::IDENTIFIER) {
			const BSParser::IdentifierNode *base_name = static_cast<const BSParser::IdentifierNode *>(callee->base);
			if (base_name->source == BSParser::IdentifierNode::NATIVE_CLASS) {
				native_static_class = base_name->name;
			} else if (base_name->source == BSParser::IdentifierNode::UNDEFINED_SOURCE &&
					!p_codegen.locals.has(base_name->name) && !p_codegen.parameters.has(base_name->name)) {
				const Variant::Type named_builtin = BSParser::get_builtin_type(base_name->name);
				// Only a meta-typed reading is the builtin *type*; a local that happens to be spelled
				// like one is an ordinary receiver, which the scope checks above have already excluded.
				if (named_builtin < Variant::VARIANT_MAX && base_name->get_datatype().is_meta_type) {
					builtin_static_type = named_builtin;
				}
			}
		}
		if (native_static_class == StringName() && builtin_static_type == Variant::VARIANT_MAX) {
			receiver = parse_expression(p_codegen, r_error, callee->base);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			has_receiver_temporary = receiver.mode == BSCodeGenerator::Address::TEMPORARY;
		}
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
	if (p_call->get_callee_type() == BSParser::Node::SUBSCRIPT) {
		const BSParser::SubscriptNode *callee = static_cast<const BSParser::SubscriptNode *>(p_call->callee);
		const BSParser::DataType receiver_type = callee->base != nullptr
				? member_slot_type(callee->base->get_datatype())
				: BSParser::DataType();
		const bool typed_container_receiver = receiver_type.kind == BSParser::DataType::BUILTIN &&
				(receiver_type.builtin_type == Variant::ARRAY || receiver_type.builtin_type == Variant::DICTIONARY) &&
				receiver_type.has_container_element_types();
		if (typed_container_receiver) {
			const int checked_count = MIN(arguments.size(), p_call->resolved_parameter_types.size());
			for (int i = 0; i < checked_count; i++) {
				const BSParser::DataType parameter_type = member_slot_type(p_call->resolved_parameter_types[i]);
				// A nullable or otherwise unrepresentable element erases the whole container's
				// engine metadata. Validate every specialized mutation parameter, including plain
				// builtin siblings whose carrier would otherwise have provided the check.
				if (parameter_type.is_variant()) {
					continue;
				}
				const BSCodeGenerator::Address checked = p_codegen.add_temporary(parameter_type);
				if (parameter_type.kind == BSParser::DataType::BUILTIN &&
						parameter_type.builtin_type == Variant::ARRAY) {
					generator->write_assign_typed_array_convert(checked, arguments[i]);
				} else if (parameter_type.kind == BSParser::DataType::BUILTIN &&
						parameter_type.builtin_type == Variant::DICTIONARY) {
					generator->write_assign_typed_dictionary_convert(checked, arguments[i]);
				} else {
					generator->write_assign_with_conversion(checked, arguments[i]);
				}
				arguments.write[i] = checked;
				argument_temporaries++;
			}
		}
	}

	if (p_call->is_super) {
		generator->write_super_call(result, p_call->function_name, arguments);
	} else if (native_static_class != StringName()) {
		generator->write_call_native_static(result, native_static_class, p_call->function_name, arguments);
	} else if (builtin_static_type != Variant::VARIANT_MAX) {
		generator->write_call_builtin_type_static(result, builtin_static_type, p_call->function_name, arguments);
	} else if (receiver_is_self) {
		const StringName &function_name = p_call->function_name;
		// A name the script declares is the script's: an engine utility or a builtin type of the same
		// spelling does not take it over.
		const bool names_own_function = p_codegen.class_node != nullptr &&
				p_codegen.class_node->has_function(function_name);
		const Variant::Type builtin_type = BSParser::get_builtin_type(function_name);
		MethodInfo utility_info;
		if (names_own_function) {
			if (p_call->is_static) {
				generator->write_call(result, BSCodeGenerator::Address(BSCodeGenerator::Address::CLASS), function_name, arguments);
			} else {
				generator->write_call_self(result, function_name, arguments);
			}
		} else if (builtin_type < Variant::VARIANT_MAX) {
			generator->write_construct(result, builtin_type, arguments);
		} else if (BSUtilityFunctions::get_function_info(function_name, utility_info)) {
			// A language utility is not an engine utility: it is BaristaScript's own, it shadows no
			// engine name, and the runtime implements it directly rather than through a pointer call.
			generator->write_call_barista_script_utility(result, function_name, arguments);
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
	if (assignee->type == BSParser::Node::IDENTIFIER) {
		const BSParser::IdentifierNode *identifier = static_cast<const BSParser::IdentifierNode *>(assignee);
		if (identifier->source == BSParser::IdentifierNode::STATIC_VARIABLE) {
			int static_index = -1;
			BaristaScript *owner = find_static_owner(identifier->variable_source, static_index);
			if (owner == nullptr || static_index < 0) {
				set_error(vformat(R"(The runtime cannot resolve the static variable "%s".)", String(identifier->name)), assignee);
				r_error = ERR_COMPILATION_FAILED;
				return BSCodeGenerator::Address();
			}
			const BSCodeGenerator::Address class_address = p_codegen.add_constant((int64_t)owner->get_instance_id());
			const BSCodeGenerator::Address value = parse_expression(p_codegen, r_error, p_assignment->assigned_value);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			BSCodeGenerator::Address stored = value;
			BSCodeGenerator::Address current;
			if (has_operation) {
				current = p_codegen.add_temporary(member_slot_type(assignee->get_datatype()));
				const StringName &getter = owner->static_getters[static_index];
				if (getter != StringName() && getter != p_codegen.function_name) {
					generator->write_call(current, class_address, getter, Vector<BSCodeGenerator::Address>());
				} else {
					generator->write_get_static_variable(current, class_address, static_index);
				}
				const BSCodeGenerator::Address combined = p_codegen.add_temporary(member_slot_type(p_assignment->get_datatype()));
				generator->write_binary_operator(combined, p_assignment->variant_op, current, value);
				stored = combined;
			}
			const StringName &setter = owner->static_setters[static_index];
			if (setter != StringName() && setter != p_codegen.function_name) {
				Vector<BSCodeGenerator::Address> arguments;
				arguments.push_back(stored);
				generator->write_call(BSCodeGenerator::Address(), class_address, setter, arguments);
			} else {
				generator->write_set_static_variable(stored, class_address, static_index);
			}
			if (stored.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (current.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (has_operation && value.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			return BSCodeGenerator::Address();
		}
	}
	int accessor_member_index = -1;
	if (assignee->type == BSParser::Node::IDENTIFIER && p_codegen.script != nullptr) {
		const StringName &name = static_cast<const BSParser::IdentifierNode *>(assignee)->name;
		if (!is_local_or_parameter(p_codegen, name)) {
			accessor_member_index = p_codegen.script->get_member_index(name);
		}
	} else if (assignee->type == BSParser::Node::SUBSCRIPT && p_codegen.script != nullptr) {
		const BSParser::SubscriptNode *subscript = static_cast<const BSParser::SubscriptNode *>(assignee);
		if (subscript->is_attribute && subscript->base != nullptr && subscript->base->type == BSParser::Node::SELF) {
			accessor_member_index = p_codegen.script->get_member_index(subscript->attribute->name);
		}
	}
	if (accessor_member_index >= 0) {
		const StringName &setter = p_codegen.script->get_member_setter(accessor_member_index);
		if (setter != StringName() && setter != p_codegen.function_name) {
			BSCodeGenerator::Address current;
			if (has_operation) {
				current = parse_expression(p_codegen, r_error, assignee);
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
				generator->write_binary_operator(combined, p_assignment->variant_op, current, value);
				stored = combined;
			}
			Vector<BSCodeGenerator::Address> arguments;
			arguments.push_back(stored);
			generator->write_call_self(BSCodeGenerator::Address(), setter, arguments);
			if (stored.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (has_operation && value.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (current.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			return BSCodeGenerator::Address();
		}
	}

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
			BSCodeGenerator::Address unchecked_index;
			if (!subscript->is_attribute) {
				index = parse_expression(p_codegen, r_error, subscript->index);
				if (r_error != OK) {
					return BSCodeGenerator::Address();
				}
				const BSParser::DataType base_type = member_slot_type(subscript->base->get_datatype());
				if (base_type.kind == BSParser::DataType::BUILTIN &&
						base_type.builtin_type == Variant::DICTIONARY && base_type.has_container_element_types()) {
					// Stock Dictionary metadata is all-or-nothing. If either declared side is not
					// representable (for example `int?`), the carrier is erased, so validate the key
					// explicitly before both compound reads and the final write.
					unchecked_index = index;
					const BSParser::DataType key_type = member_slot_type(base_type.get_container_element_type_or_variant(0));
					index = p_codegen.add_temporary(key_type);
					generator->write_assign_with_conversion(index, unchecked_index);
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
			BSCodeGenerator::Address unchecked_stored;
			const BSParser::DataType element_type = member_slot_type(assignee->get_datatype());
			const BSParser::DataType base_type = member_slot_type(subscript->base->get_datatype());
			const bool typed_dictionary = base_type.kind == BSParser::DataType::BUILTIN &&
					base_type.builtin_type == Variant::DICTIONARY && base_type.has_container_element_types();
			if (!subscript->is_attribute && (typed_dictionary || slot_needs_runtime_descriptor_check(element_type))) {
				unchecked_stored = stored;
				stored = p_codegen.add_temporary(element_type);
				if (element_type.kind == BSParser::DataType::BUILTIN &&
						element_type.builtin_type == Variant::ARRAY) {
					generator->write_assign_typed_array_convert(stored, unchecked_stored);
				} else if (element_type.kind == BSParser::DataType::BUILTIN &&
						element_type.builtin_type == Variant::DICTIONARY) {
					generator->write_assign_typed_dictionary_convert(stored, unchecked_stored);
				} else {
					generator->write_assign_with_conversion(stored, unchecked_stored);
				}
			}
			if (subscript->is_attribute) {
				generator->write_set_named(base, subscript->attribute->name, stored);
			} else {
				generator->write_set(base, index, stored);
			}
			if (stored.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (unchecked_stored.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (has_operation && value.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (index.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (unchecked_index.mode == BSCodeGenerator::Address::TEMPORARY) {
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
		if (accessor_member_index >= 0) {
			// Reaching here for an accessor means this is its own setter. A write inside the setter
			// targets the backing slot directly; resolving the identifier as a read would call the
			// getter and assign into the temporary it returned instead.
			target = BSCodeGenerator::Address(BSCodeGenerator::Address::MEMBER, accessor_member_index,
					member_slot_type(assignee->get_datatype()));
		} else {
			target = parse_identifier(p_codegen, r_error, static_cast<const BSParser::IdentifierNode *>(assignee));
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
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
