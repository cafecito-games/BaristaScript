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

case OPCODE_LOAD_STATIC_SELF_CLASS: {
	BS_CHECK_SPACE(2);
	BS_GET_VARIANT_PTR(destination, 0);
	if (unlikely(static_receiver == nullptr)) {
		error_text = "Cannot load Self without a declaring script.";
		goto vm_error;
	}
	// Script class handles use the same weak ObjectID carrier as pooled named classes.
	*destination = stack[ADDR_STACK_CLASS];
	BS_IP += 2;
} break;

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
	const Variant::Operator variant_operator = (Variant::Operator)operator_index;
	Variant::evaluate(variant_operator, *left, *right, result, valid);
	if (unlikely(!valid)) {
		// Integer division and remainder by zero are the one arithmetic failure whose cause the
		// operand types do not show: `1 / 0` and `1 / "a"` both come back invalid, and reporting the
		// first as a type mismatch sends the reader looking for the wrong bug. The check survives the
		// deleted numeric tower (D1) because it describes the operator, not a width: upstream's
		// wording lives in fs_numeric_ops.cpp:621, but the sentence names `int`, which is the only
		// integer type BaristaScript has.
		const bool is_division = variant_operator == Variant::OP_DIVIDE || variant_operator == Variant::OP_MODULE;
		if (is_division && left->get_type() == Variant::INT && right->get_type() == Variant::INT &&
				(int64_t)*right == 0) {
			error_text = vformat(R"(The "%s" operator on "int" has a divisor of zero.)",
					variant_operator_name(variant_operator));
			goto vm_error;
		}
		if (variant_operator == Variant::OP_NEGATE || variant_operator == Variant::OP_POSITIVE ||
				variant_operator == Variant::OP_BIT_NEGATE || variant_operator == Variant::OP_NOT) {
			// A unary opcode carries a dummy right operand; naming it would invent a second operand
			// the program never wrote.
			error_text = "Invalid operand '" + describe_value_type(*left) + "' in operator '" +
					variant_operator_name(variant_operator) + "'.";
			goto vm_error;
		}
		error_text = "Invalid operands '" + describe_value_type(*left) + "' and '" +
				describe_value_type(*right) + "' in operator '" + variant_operator_name(variant_operator) + "'.";
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
		if (value_is_read_only(*destination)) {
			// A sealed container refuses every key alike, so naming the key would suggest the store
			// would have worked with a different one.
			error_text = "Invalid assignment on read-only value (on base: '" + describe_value_type(*destination) + "').";
		} else {
			String subject = index->operator String();
			subject = subject.is_empty() ? "of type '" + describe_value_type(*index) + "'" : "'" + subject + "'";
			error_text = "Invalid assignment of property or key " + subject + " with value of type '" +
					describe_value_type(*value) + "' on a base object of type '" + describe_value_type(*destination) + "'.";
		}
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
		String subject = index->operator String();
		subject = subject.is_empty() ? "of type '" + describe_value_type(*index) + "'" : "'" + subject + "'";
		// A sequence indexed past its end is a range fault, not an unknown key: the index is
		// well-formed and the base does accept indices, so the reader needs the bound, not the name.
		// godot-cpp has neither `VariantGetError` nor `Variant::get_indexed_element_type`, so the
		// carriers that answer an integer index are recognized directly.
		const bool indexed = variant_type_is_indexed(source->get_type());
		error_text = indexed
				? "Out of bounds get index " + subject + " (on base: '" + describe_value_type(*source) + "')"
				: "Invalid access to property or key " + subject + " on a base object of type '" +
						describe_value_type(*source) + "'.";
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
	String script_error;
	if (BaristaScript *class_script = script_from_handle(*destination)) {
		valid = class_script->set_static_property(property, *value, script_error);
	} else {
		destination->set_named(property, *value, valid);
	}
	if (unlikely(!valid)) {
		if (!script_error.is_empty()) {
			error_text = script_error;
			goto vm_error;
		}
		if (value_is_read_only(*destination)) {
			error_text = "Invalid assignment on read-only value (on base: '" + describe_value_type(*destination) + "').";
		} else {
			error_text = "Invalid assignment of property or key '" + String(property) + "' with value of type '" +
					describe_value_type(*value) + "' on a base object of type '" + describe_value_type(*destination) + "'.";
		}
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
	Variant result;
	if (BaristaScript *class_script = script_from_handle(*source)) {
		valid = class_script->get_static_property(property, result);
	} else {
		result = source->get_named(property, valid);
	}
	if (unlikely(!valid)) {
		error_text = "Invalid access to property or key '" + String(property) + "' on a base object of type '" +
				describe_value_type(*source) + "'.";
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
	if (BaristaScript *class_script = script_from_handle(*base)) {
		if (method == SNAME("new")) {
			result = class_script->instantiate(const_cast<const Variant **>(instruction_argument_ptr), argument_count, call_error);
		} else {
			result = class_script->call_static(method, const_cast<const Variant **>(instruction_argument_ptr), argument_count,
					call_error, class_script);
		}
	} else if (own_function != nullptr) {
		result = own_function->call(own_function->is_static() ? nullptr : p_instance,
				const_cast<const Variant **>(instruction_argument_ptr), argument_count, call_error,
				own_function->is_static() ? p_instance->script.ptr() : nullptr);
	} else {
		base->callp(method, const_cast<const Variant **>(instruction_argument_ptr), argument_count, result, call_error);
	}
	// Only this call's own outcome decides whether the frame continues. A fault raised somewhere
	// under the call -- a signal handler the callee emitted to, say -- has already reported for
	// itself and is not this frame's to abandon, which is also what the engine does for its own
	// callers.
	if (unlikely(call_error.error != GDEXTENSION_CALL_OK)) {
		String method_name = method;
		String base_name = describe_value_type(*base);
		bool names_its_own_target = false;
		if (method == SNAME("call")) {
			// `.call(...)` is a forwarder, so naming it would hide what actually failed. On a
			// Callable the target is the Callable itself; on anything else the first argument names
			// the method being forwarded to.
			if (base->get_type() == Variant::CALLABLE) {
				method_name = base->operator String() + " (Callable)";
				names_its_own_target = true;
			} else if (argument_count >= 1) {
				method_name = instruction_argument_ptr[0]->operator String() + " (via call)";
			}
		} else if (method == SNAME("free") && call_error.error == GDEXTENSION_CALL_ERROR_INVALID_METHOD) {
			// `free` exists on every Object but refuses on a RefCounted, where the reference count
			// owns the lifetime. Reporting it as a missing method would be actively misleading.
			if (base->get_type() == Variant::OBJECT) {
				error_text = Object::cast_to<RefCounted>(base->get_validated_object()) != nullptr
						? String("Attempted to free a RefCounted object.")
						: String("Attempted to free a locked object (calling or emitting).");
				goto vm_error;
			}
		}
		error_text = names_its_own_target
				? describe_call_error("function '" + method_name + "'",
						  const_cast<const Variant **>(instruction_argument_ptr), argument_count, call_error)
				: describe_call_error("function '" + method_name + "' in base '" + base_name + "'",
						  const_cast<const Variant **>(instruction_argument_ptr), argument_count, call_error);
		goto vm_error;
	}
	if (wants_return) {
		// A call that completed but produced nothing may still be a mistake: the program asked for a
		// value from a method declared to return none, and silently handing it null would hide the
		// error at the point it can still be explained. Only a nil result can be one of these, so the
		// (comparatively expensive) declaration lookup never runs on the common path.
		if (unlikely(result.get_type() == Variant::NIL)) {
			bool returns_void = false;
			if (base->get_type() == Variant::OBJECT) {
				// `free` is checked before the receiver is validated, because a successful `free` is
				// exactly the call that leaves the receiver gone: looking the method up on the object
				// afterwards would find nothing and let the mistake through.
				Object *receiver = base->get_validated_object();
				MethodInfo declared;
				returns_void = method == SNAME("free") ||
						(receiver != nullptr && BSNativeDB::get_method_info(receiver->get_class(), method, &declared) &&
								declared.return_val.type == Variant::NIL &&
								(declared.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == 0);
			} else if (base->get_type() != Variant::NIL) {
				const BSCoreConstants::BuiltinMethod *builtin = BSCoreConstants::get_builtin_method(base->get_type(), method);
				returns_void = builtin != nullptr && builtin->info.return_val.type == Variant::NIL &&
						(builtin->info.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == 0;
			}
			if (returns_void) {
				error_text = R"(Trying to get a return value of a method that returns "void")";
				goto vm_error;
			}
		}
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
	GDExtensionCallError call_error;
	call_error.error = GDEXTENSION_CALL_OK;
	if (unlikely(!call_engine_utility(utility, const_cast<const Variant **>(instruction_argument_ptr), argument_count,
				result, call_error, utility_error))) {
		error_text = utility_error.is_empty()
				? describe_call_error(vformat(R"*(utility function "%s()")*", String(utility)),
						  const_cast<const Variant **>(instruction_argument_ptr), argument_count, call_error)
				: utility_error;
		goto vm_error;
	}
	*instruction_argument_ptr[argument_count] = result;
	BS_IP += 3;
} break;

case OPCODE_CALL_BARISTA_SCRIPT_UTILITY: {
	BS_LOAD_INSTRUCTION_ARGUMENTS
	BS_IP += instruction_argument_count;
	BS_CHECK_SPACE(3);
	const int argument_count = code_ptr[BS_IP + 1];
	if (unlikely(argument_count < 0 || argument_count >= instruction_argument_count)) {
		error_text = "Bad language utility call operands in compiled function.";
		goto vm_error;
	}
	BS_GET_NAME(utility, code_ptr[BS_IP + 2]);
	Variant result;
	GDExtensionCallError call_error;
	call_error.error = GDEXTENSION_CALL_OK;
	if (unlikely(!call_language_utility(utility, const_cast<const Variant **>(instruction_argument_ptr),
				argument_count, result, call_error))) {
		// A utility that has something specific to say returns the sentence itself; only a failure
		// with nothing to add falls back to the generic shape, so the reader never gets "invalid
		// argument" where "Value of type 'Color' can't provide a length" was available.
		const String detail = result.get_type() == Variant::STRING ? (String)result : String();
		error_text = detail.is_empty()
				? describe_call_error(vformat(R"*(BaristaScript utility function "%s()")*", String(utility)),
						  const_cast<const Variant **>(instruction_argument_ptr), argument_count, call_error)
				: vformat(R"*(Error calling BaristaScript utility function "%s()": %s)*", String(utility), detail);
		goto vm_error;
	}
	*instruction_argument_ptr[argument_count] = result;
	BS_IP += 3;
} break;

case OPCODE_CALL_BUILTIN_STATIC: {
	BS_LOAD_INSTRUCTION_ARGUMENTS
	BS_IP += instruction_argument_count;
	BS_CHECK_SPACE(4);
	const int argument_count = code_ptr[BS_IP + 1];
	if (unlikely(argument_count < 0 || argument_count >= instruction_argument_count)) {
		error_text = "Bad builtin static call operands in compiled function.";
		goto vm_error;
	}
	const int builtin_type = code_ptr[BS_IP + 2];
	if (unlikely(builtin_type < 0 || builtin_type >= Variant::VARIANT_MAX)) {
		error_text = "Bad builtin type in compiled function.";
		goto vm_error;
	}
	BS_GET_NAME(method, code_ptr[BS_IP + 3]);
	Variant result;
	GDExtensionCallError call_error;
	call_error.error = GDEXTENSION_CALL_OK;
	Variant::callp_static((Variant::Type)builtin_type, method, const_cast<const Variant **>(instruction_argument_ptr),
			argument_count, result, call_error);
	if (unlikely(call_error.error != GDEXTENSION_CALL_OK)) {
		error_text = describe_call_error("static function '" + String(method) + "' in type '" +
						Variant::get_type_name((Variant::Type)builtin_type) + "'",
				const_cast<const Variant **>(instruction_argument_ptr), argument_count, call_error);
		goto vm_error;
	}
	*instruction_argument_ptr[argument_count] = result;
	BS_IP += 4;
} break;

case OPCODE_CALL_NATIVE_STATIC: {
	BS_LOAD_INSTRUCTION_ARGUMENTS
	BS_IP += instruction_argument_count;
	BS_CHECK_SPACE(4);
	const int argument_count = code_ptr[BS_IP + 1];
	if (unlikely(argument_count < 0 || argument_count >= instruction_argument_count)) {
		error_text = "Bad native static call operands in compiled function.";
		goto vm_error;
	}
	BS_GET_NAME(native_class, code_ptr[BS_IP + 2]);
	BS_GET_NAME(method, code_ptr[BS_IP + 3]);
	Variant result;
	if (method == SNAME("new")) {
		// `Node.new()` is not a static method of `Node`: upstream reaches it through the
		// `FSNativeClass` object it materializes for every engine class, whose `new` instantiates.
		// BaristaScript has no such wrapper -- a class name is carried in the instruction, not as a
		// value -- so construction is answered here, where the class name is known.
		if (unlikely(argument_count != 0)) {
			error_text = vformat(R"*(Invalid call to "%s.new()". Expected 0 argument(s).)*", String(native_class));
			goto vm_error;
		}
		if (unlikely(!ClassDB::can_instantiate(native_class))) {
			error_text = vformat(R"(Cannot instantiate "%s": the engine class is abstract or disabled.)",
					String(native_class));
			goto vm_error;
		}
		result = ClassDB::instantiate(native_class);
	} else {
		// A static engine method has no receiver object to dispatch on, so it is asked of the
		// ClassDB singleton by name. `class_call_static` is variadic at the engine boundary, which is
		// why the class and the method travel as the first two arguments rather than as parameters.
		Vector<Variant> forwarded;
		forwarded.resize(argument_count + 2);
		forwarded.write[0] = native_class;
		forwarded.write[1] = method;
		for (int i = 0; i < argument_count; i++) {
			forwarded.write[i + 2] = *instruction_argument_ptr[i];
		}
		Vector<const Variant *> forwarded_pointers;
		forwarded_pointers.resize(forwarded.size());
		for (int i = 0; i < forwarded.size(); i++) {
			forwarded_pointers.write[i] = &forwarded[i];
		}
		GDExtensionCallError call_error;
		call_error.error = GDEXTENSION_CALL_OK;
		Variant singleton = ClassDBSingleton::get_singleton();
		singleton.callp(SNAME("class_call_static"), const_cast<const Variant **>(forwarded_pointers.ptrw()), forwarded_pointers.size(),
				result, call_error);
		if (unlikely(call_error.error != GDEXTENSION_CALL_OK)) {
			error_text = describe_call_error("static function '" + String(method) + "' in type '" +
							String(native_class) + "'",
					const_cast<const Variant **>(instruction_argument_ptr), argument_count, call_error);
			goto vm_error;
		}
	}
	*instruction_argument_ptr[argument_count] = result;
	BS_IP += 4;
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
