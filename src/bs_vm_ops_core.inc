/**************************************************************************/
/*  bs_vm_ops_core.inc                                                    */
/*                                                                        */
/*  Core handlers: operators, stores, control flow, construction and the  */
/*  generic call paths. Included inside the dispatch switch in bs_vm.cpp, */
/*  which owns the operand-reading macros and the exit labels.            */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

case OPCODE_OPERATOR: {
	BS_CHECK_SPACE(5);
	BS_GET_VARIANT_PTR(left, 0);
	BS_GET_VARIANT_PTR(right, 1);
	BS_GET_VARIANT_PTR(destination, 2);
	const int operator_index = code_ptr[BS_IP + 4];
	if (unlikely(operator_index < 0 || operator_index >= Variant::OP_MAX)) {
		error_text = "Bad operator in compiled function.";
		goto vm_error;
	}
	bool valid = false;
	Variant result;
	Variant::evaluate((Variant::Operator)operator_index, *left, *right, result, valid);
	if (unlikely(!valid)) {
		error_text = vformat(R"(Invalid operands "%s" and "%s" for operator "%s".)",
				Variant::get_type_name(left->get_type()), Variant::get_type_name(right->get_type()),
				variant_operator_name((Variant::Operator)operator_index));
		goto vm_error;
	}
	*destination = result;
	BS_IP += 5;
} break;

case OPCODE_SET_KEYED: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(destination, 0);
	BS_GET_VARIANT_PTR(index, 1);
	BS_GET_VARIANT_PTR(value, 2);
	// `Variant::set` is the engine's subscript assignment: it selects the indexed accessor for a
	// sequence and the keyed one for a map, which is what `base[key] = value` means. The keyed
	// accessor alone has no meaning for an Array.
	bool valid = false;
	destination->set(*index, *value, &valid);
	if (unlikely(!valid)) {
		error_text = vformat(R"(Invalid index "%s" for a value of type "%s".)",
				index->stringify(), Variant::get_type_name(destination->get_type()));
		goto vm_error;
	}
	BS_IP += 4;
} break;

case OPCODE_GET_KEYED: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(source, 0);
	BS_GET_VARIANT_PTR(index, 1);
	BS_GET_VARIANT_PTR(destination, 2);
	// `Variant::get` is the engine's subscript read; see the matching store.
	bool valid = false;
	const Variant result = source->get(*index, &valid);
	if (unlikely(!valid)) {
		error_text = vformat(R"(Invalid index "%s" for a value of type "%s".)",
				index->stringify(), Variant::get_type_name(source->get_type()));
		goto vm_error;
	}
	*destination = result;
	BS_IP += 4;
} break;

case OPCODE_SET_NAMED: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(destination, 0);
	BS_GET_VARIANT_PTR(value, 1);
	BS_GET_NAME(property, code_ptr[BS_IP + 3]);
	bool valid = false;
	destination->set_named(property, *value, valid);
	if (unlikely(!valid)) {
		error_text = vformat(R"(Cannot set property "%s" on a value of type "%s".)",
				String(property), Variant::get_type_name(destination->get_type()));
		goto vm_error;
	}
	BS_IP += 4;
} break;

case OPCODE_GET_NAMED: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(source, 0);
	BS_GET_VARIANT_PTR(destination, 1);
	BS_GET_NAME(property, code_ptr[BS_IP + 3]);
	bool valid = false;
	const Variant result = source->get_named(property, valid);
	if (unlikely(!valid)) {
		error_text = vformat(R"(Cannot read property "%s" from a value of type "%s".)",
				String(property), Variant::get_type_name(source->get_type()));
		goto vm_error;
	}
	*destination = result;
	BS_IP += 4;
} break;

case OPCODE_ASSIGN: {
	BS_CHECK_SPACE(3);
	BS_GET_VARIANT_PTR(destination, 0);
	BS_GET_VARIANT_PTR(source, 1);
	*destination = *source;
	BS_IP += 3;
} break;

case OPCODE_ASSIGN_NULL: {
	BS_CHECK_SPACE(2);
	BS_GET_VARIANT_PTR(destination, 0);
	*destination = Variant();
	BS_IP += 2;
} break;

case OPCODE_ASSIGN_TRUE: {
	BS_CHECK_SPACE(2);
	BS_GET_VARIANT_PTR(destination, 0);
	*destination = true;
	BS_IP += 2;
} break;

case OPCODE_ASSIGN_FALSE: {
	BS_CHECK_SPACE(2);
	BS_GET_VARIANT_PTR(destination, 0);
	*destination = false;
	BS_IP += 2;
} break;

case OPCODE_ASSIGN_TYPED_BUILTIN: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(destination, 0);
	BS_GET_VARIANT_PTR(source, 1);
	const int operand = code_ptr[BS_IP + 3];
	const bool nullable = (operand & NULLABLE_TYPE_OPERAND_FLAG) != 0;
	const Variant::Type target_type = (Variant::Type)(operand & ~NULLABLE_TYPE_OPERAND_FLAG);
	if (nullable && source->get_type() == Variant::NIL) {
		*destination = Variant();
	} else if (source->get_type() == target_type) {
		*destination = *source;
	} else if (Variant::can_convert_strict(source->get_type(), target_type)) {
		GDExtensionCallError construct_error;
		construct_error.error = GDEXTENSION_CALL_OK;
		const Variant *construct_arguments[1] = { source };
		Variant converted;
		godot::gdextension_interface::variant_construct((GDExtensionVariantType)target_type, &converted,
				reinterpret_cast<const GDExtensionConstVariantPtr *>(construct_arguments), 1, &construct_error);
		if (unlikely(construct_error.error != GDEXTENSION_CALL_OK)) {
			error_text = vformat(R"(Cannot convert a value of type "%s" to "%s".)",
					Variant::get_type_name(source->get_type()), Variant::get_type_name(target_type));
			goto vm_error;
		}
		*destination = converted;
	} else {
		error_text = vformat(R"(Cannot assign a value of type "%s" to a slot of type "%s".)",
				Variant::get_type_name(source->get_type()), Variant::get_type_name(target_type));
		goto vm_error;
	}
	BS_IP += 4;
} break;

case OPCODE_CONSTRUCT: {
	BS_LOAD_INSTRUCTION_ARGUMENTS
	BS_IP += instruction_argument_count;
	BS_CHECK_SPACE(3);
	const int argument_count = code_ptr[BS_IP + 1];
	const int type_index = code_ptr[BS_IP + 2];
	if (unlikely(argument_count < 0 || argument_count >= instruction_argument_count ||
				type_index < 0 || type_index >= Variant::VARIANT_MAX)) {
		error_text = "Bad constructor operands in compiled function.";
		goto vm_error;
	}
	Variant *destination = instruction_argument_ptr[argument_count];
	GDExtensionCallError construct_error;
	construct_error.error = GDEXTENSION_CALL_OK;
	Variant constructed;
	godot::gdextension_interface::variant_construct((GDExtensionVariantType)type_index, &constructed,
			reinterpret_cast<const GDExtensionConstVariantPtr *>(instruction_argument_ptr), argument_count, &construct_error);
	if (unlikely(construct_error.error != GDEXTENSION_CALL_OK)) {
		// The destination keeps whatever it held: a rejected construction leaves no half-built value
		// for the next instruction to read.
		error_text = vformat(R"(Cannot construct a value of type "%s" from the given arguments.)",
				Variant::get_type_name((Variant::Type)type_index));
		goto vm_error;
	}
	*destination = constructed;
	BS_IP += 3;
} break;

case OPCODE_CONSTRUCT_ARRAY: {
	BS_LOAD_INSTRUCTION_ARGUMENTS
	BS_IP += instruction_argument_count;
	BS_CHECK_SPACE(2);
	const int argument_count = code_ptr[BS_IP + 1];
	if (unlikely(argument_count < 0 || argument_count >= instruction_argument_count)) {
		error_text = "Bad array literal operands in compiled function.";
		goto vm_error;
	}
	Array array;
	array.resize(argument_count);
	for (int element = 0; element < argument_count; element++) {
		array[element] = *instruction_argument_ptr[element];
	}
	*instruction_argument_ptr[argument_count] = array;
	BS_IP += 2;
} break;

case OPCODE_CONSTRUCT_DICTIONARY: {
	BS_LOAD_INSTRUCTION_ARGUMENTS
	BS_IP += instruction_argument_count;
	BS_CHECK_SPACE(2);
	const int pair_count = code_ptr[BS_IP + 1];
	if (unlikely(pair_count < 0 || pair_count * 2 >= instruction_argument_count)) {
		error_text = "Bad dictionary literal operands in compiled function.";
		goto vm_error;
	}
	Dictionary dictionary;
	for (int pair = 0; pair < pair_count; pair++) {
		dictionary[*instruction_argument_ptr[pair * 2]] = *instruction_argument_ptr[pair * 2 + 1];
	}
	*instruction_argument_ptr[pair_count * 2] = dictionary;
	BS_IP += 2;
} break;

case OPCODE_CALL:
case OPCODE_CALL_RETURN: {
	const bool wants_return = code_ptr[BS_IP] == OPCODE_CALL_RETURN;
	BS_LOAD_INSTRUCTION_ARGUMENTS
	BS_IP += instruction_argument_count;
	BS_CHECK_SPACE(3);
	const int argument_count = code_ptr[BS_IP + 1];
	if (unlikely(argument_count < 0 || argument_count + (wants_return ? 1 : 0) >= instruction_argument_count)) {
		error_text = "Bad call operands in compiled function.";
		goto vm_error;
	}
	BS_GET_NAME(method, code_ptr[BS_IP + 2]);
	Variant *base = instruction_argument_ptr[argument_count];
	Variant result;
	GDExtensionCallError call_error;
	call_error.error = GDEXTENSION_CALL_OK;
	// A call on this frame's own receiver is dispatched through the instance rather than through the
	// owner object. The instance is what knows the script's functions, and it knows them before the
	// engine has been handed it -- which is what lets a member initializer call one, whether it names
	// the receiver as `self` or through a local that holds it. Anything the script does not declare
	// is still the owner's to answer.
	BSFunction *own_function = nullptr;
	if (p_instance != nullptr && p_instance->script.is_valid() && base->get_type() == Variant::OBJECT &&
			base->get_validated_object() == p_instance->owner) {
		own_function = p_instance->script->find_function(method);
	}
	if (own_function != nullptr) {
		result = own_function->call(p_instance, const_cast<const Variant **>(instruction_argument_ptr), argument_count, call_error);
	} else {
		base->callp(method, const_cast<const Variant **>(instruction_argument_ptr), argument_count, result, call_error);
	}
	// Only this call's own outcome decides whether the frame continues. A fault raised somewhere
	// under the call -- a signal handler the callee emitted to, say -- has already reported for
	// itself and is not this frame's to abandon, which is also what the engine does for its own
	// callers.
	if (unlikely(call_error.error != GDEXTENSION_CALL_OK)) {
		error_text = vformat(R"(Invalid call to "%s" on a value of type "%s".)",
				String(method), Variant::get_type_name(base->get_type()));
		goto vm_error;
	}
	if (wants_return) {
		*instruction_argument_ptr[argument_count + 1] = result;
	}
	BS_IP += 3;
} break;

case OPCODE_CALL_SELF_BASE: {
	BS_LOAD_INSTRUCTION_ARGUMENTS
	BS_IP += instruction_argument_count;
	BS_CHECK_SPACE(3);
	const int argument_count = code_ptr[BS_IP + 1];
	if (unlikely(argument_count < 0 || argument_count >= instruction_argument_count)) {
		error_text = "Bad super-call operands in compiled function.";
		goto vm_error;
	}
	BS_GET_NAME(method, code_ptr[BS_IP + 2]);
	Variant *destination = instruction_argument_ptr[argument_count];
	BSFunction *inherited = nullptr;
	for (BaristaScript *ancestor = script != nullptr ? script->get_base_barista_script() : nullptr;
			ancestor != nullptr; ancestor = ancestor->get_base_barista_script()) {
		inherited = ancestor->find_function(method);
		if (inherited != nullptr) {
			break;
		}
	}
	if (inherited != nullptr) {
		GDExtensionCallError call_error;
		*destination = inherited->call(p_instance, const_cast<const Variant **>(instruction_argument_ptr), argument_count, call_error);
		if (unlikely(call_error.error != GDEXTENSION_CALL_OK)) {
			error_text = vformat(R"(Invalid super call to "%s".)", String(method));
			goto vm_error;
		}
	} else if (method == SNAME("_init")) {
		// A base with no `_init` of its own has nothing to run, which is what the engine does for an
		// unimplemented virtual.
		*destination = Variant();
	} else {
		// The owner cannot be asked for it: `Object::has_method` and `Object::call` consult the
		// script instance before the engine's own methods, so forwarding there would re-enter this
		// very function. Reaching a native implementation past the script needs a dispatch the
		// extension interface does not offer, so the call is refused by name instead.
		error_text = vformat(R"*(Cannot call "super.%s()": no base script declares it, and a native implementation cannot be reached past the script.)*",
				String(method));
		goto vm_error;
	}
	BS_IP += 3;
} break;

case OPCODE_CALL_UTILITY: {
	BS_LOAD_INSTRUCTION_ARGUMENTS
	BS_IP += instruction_argument_count;
	BS_CHECK_SPACE(3);
	const int argument_count = code_ptr[BS_IP + 1];
	if (unlikely(argument_count < 0 || argument_count >= instruction_argument_count)) {
		error_text = "Bad utility call operands in compiled function.";
		goto vm_error;
	}
	BS_GET_NAME(utility, code_ptr[BS_IP + 2]);
	Variant result;
	String utility_error;
	if (!call_engine_utility(utility, const_cast<const Variant **>(instruction_argument_ptr), argument_count, result, utility_error)) {
		error_text = utility_error;
		goto vm_error;
	}
	*instruction_argument_ptr[argument_count] = result;
	BS_IP += 3;
} break;

case OPCODE_STORE_GLOBAL: {
	BS_CHECK_SPACE(3);
	BS_GET_VARIANT_PTR(destination, 0);
	const int global_index = code_ptr[BS_IP + 2];
	if (unlikely(global_index < 0 || global_index >= BSCoreConstants::get_global_constant_count())) {
		error_text = "Bad global constant index in compiled function.";
		goto vm_error;
	}
	*destination = BSCoreConstants::get_global_constant_value(global_index);
	BS_IP += 3;
} break;

case OPCODE_STORE_NAMED_GLOBAL: {
	BS_CHECK_SPACE(3);
	BS_GET_VARIANT_PTR(destination, 0);
	BS_GET_NAME(global, code_ptr[BS_IP + 2]);
	if (!Engine::get_singleton()->has_singleton(global)) {
		error_text = vformat(R"(Cannot resolve the global "%s".)", String(global));
		goto vm_error;
	}
	*destination = Engine::get_singleton()->get_singleton(global);
	BS_IP += 3;
} break;

case OPCODE_JUMP: {
	BS_CHECK_SPACE(2);
	const int target = code_ptr[BS_IP + 1];
	if (unlikely(target < 0 || target > code_size)) {
		error_text = "Bad jump target in compiled function.";
		goto vm_error;
	}
	BS_IP = target;
} break;

case OPCODE_JUMP_IF:
case OPCODE_JUMP_IF_NOT: {
	const bool jump_when_true = code_ptr[BS_IP] == OPCODE_JUMP_IF;
	BS_CHECK_SPACE(3);
	BS_GET_VARIANT_PTR(test, 0);
	if (test->booleanize() == jump_when_true) {
		const int target = code_ptr[BS_IP + 2];
		if (unlikely(target < 0 || target > code_size)) {
			error_text = "Bad jump target in compiled function.";
			goto vm_error;
		}
		BS_IP = target;
	} else {
		BS_IP += 3;
	}
} break;

case OPCODE_JUMP_IF_SHARED: {
	BS_CHECK_SPACE(3);
	BS_GET_VARIANT_PTR(value, 0);
	if (variant_type_is_shared(value->get_type())) {
		const int target = code_ptr[BS_IP + 2];
		if (unlikely(target < 0 || target > code_size)) {
			error_text = "Bad jump target in compiled function.";
			goto vm_error;
		}
		BS_IP = target;
	} else {
		BS_IP += 3;
	}
} break;

case OPCODE_JUMP_TO_DEF_ARGUMENT: {
	if (unlikely(default_argument_index < 0 || default_argument_index >= default_arguments.size())) {
		error_text = "Bad default-argument selector in compiled function.";
		goto vm_error;
	}
	BS_IP = default_arguments[default_argument_index];
} break;

case OPCODE_RETURN: {
	BS_CHECK_SPACE(2);
	BS_GET_VARIANT_PTR(value, 0);
	return_value = *value;
	goto vm_exit;
} break;

case OPCODE_ASSERT: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(test, 0);
	if (!test->booleanize()) {
		String message;
		if (code_ptr[BS_IP + 3] != 0) {
			BS_GET_VARIANT_PTR(message_value, 1);
			message = message_value->stringify();
		}
		error_text = message.is_empty() ? String("Assertion failed.") : "Assertion failed: " + message;
		goto vm_error;
	}
	BS_IP += 4;
} break;

case OPCODE_BREAKPOINT: {
	// Emitted but inert: stepping and breakpoints belong to the debugger milestone.
	BS_IP += 1;
} break;

case OPCODE_LINE: {
	BS_CHECK_SPACE(2);
	line = code_ptr[BS_IP + 1];
	BS_IP += 2;
} break;

case OPCODE_END: {
	goto vm_exit;
} break;
