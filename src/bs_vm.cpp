/**************************************************************************/
/*  bs_vm.cpp                                                             */
/*                                                                        */
/*  The dispatch loop. Provenance: fs_vm.cpp:2471-2851 and :7524-7614     */
/*  @ c9d5e35, with the handler families in the per-family .inc files.    */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_core_constants.h"
#include "bs_function.h"
#include "bs_script_instance.h"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace barista_script {

namespace {

/**
 * A frame's operand words are read relative to the instruction pointer, so each opcode's operand
 * count and its `instruction_pointer += n` are the same statement of its layout. The three address
 * spaces a word can name -- the frame's stack, the function's constant pool, and the instance's
 * members -- are selected by the top byte of the word, exactly as the emitter wrote it.
 */
constexpr int MAX_CALL_DEPTH = 1024;

thread_local int call_depth = 0;
thread_local bool runtime_error_reported = false;

struct CallDepthGuard {
	bool entered = false;
	CallDepthGuard() {
		entered = ++call_depth <= MAX_CALL_DEPTH;
	}
	~CallDepthGuard() { call_depth--; }
};

/** True for the carriers whose value is a shared reference, so an in-place edit is already visible. */
bool variant_type_is_shared(Variant::Type p_type) {
	switch (p_type) {
		case Variant::OBJECT:
		case Variant::ARRAY:
		case Variant::DICTIONARY:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_COLOR_ARRAY:
		case Variant::PACKED_VECTOR4_ARRAY:
			return true;
		default:
			return false;
	}
}

/** The operator's source spelling, for a diagnostic that names what the program wrote. */
String variant_operator_name(Variant::Operator p_operator) {
	static const char *const names[Variant::OP_MAX] = {
		"==", "!=", "<", "<=", ">", ">=",
		"+", "-", "*", "/", "unary-", "unary+", "%", "**",
		"<<", ">>", "&", "|", "^", "~",
		"and", "or", "xor", "not", "in"
	};
	if (p_operator < 0 || p_operator >= Variant::OP_MAX) {
		return "<unknown>";
	}
	return names[p_operator];
}

/**
 * Calls one of the engine's global utility functions by name.
 *
 * The engine exposes utilities only through a typed pointer call, whose arguments are the
 * parameters' own carriers rather than Variants. A variadic utility declares every parameter as a
 * Variant, so its call is exact; a utility with a fixed, typed signature would need each argument
 * materialized in its declared carrier first, which nothing emits yet. That case is refused by name
 * instead of being guessed at.
 */
bool call_engine_utility(const StringName &p_name, const Variant **p_arguments, int p_argument_count, Variant &r_return, String &r_error) {
	MethodInfo info;
	if (!BSCoreConstants::get_utility_function(p_name, info)) {
		r_error = vformat(R"(Cannot call utility function "%s": the engine has no such function.)", String(p_name));
		return false;
	}
	if ((info.flags & METHOD_FLAG_VARARG) == 0) {
		r_error = vformat(R"(Cannot call utility function "%s": only variadic utility functions are reachable from compiled code.)", String(p_name));
		return false;
	}
	int64_t hash = 0;
	if (!BSCoreConstants::get_utility_function_hash(p_name, hash)) {
		r_error = vformat(R"(Cannot call utility function "%s": no pinned signature hash.)", String(p_name));
		return false;
	}
	GDExtensionPtrUtilityFunction function = godot::gdextension_interface::variant_get_ptr_utility_function(&p_name, hash);
	if (function == nullptr) {
		r_error = vformat(R"(Cannot call utility function "%s": the engine rejected the pinned signature.)", String(p_name));
		return false;
	}
	switch (info.return_val.type) {
		case Variant::NIL: {
			function(nullptr, reinterpret_cast<const void **>(p_arguments), p_argument_count);
			r_return = Variant();
		} break;
		case Variant::STRING: {
			String result;
			function(&result, reinterpret_cast<const void **>(p_arguments), p_argument_count);
			r_return = result;
		} break;
		default: {
			r_error = vformat(R"(Cannot call utility function "%s": its return carrier is not reachable from compiled code.)", String(p_name));
			return false;
		}
	}
	return true;
}

/** Reports only the frame that found the fault, not every frame the fault unwinds through. */
void report_runtime_error_once(const String &p_description, const StringName &p_function, const String &p_file, int p_line) {
	if (runtime_error_reported) {
		return;
	}
	runtime_error_reported = true;
	bs_report_runtime_error(p_description, p_function, p_file, p_line);
}

} // namespace

bool bs_runtime_error_was_reported() {
	return runtime_error_reported;
}

Variant BSFunction::call(BSInstance *p_instance, const Variant **p_arguments, int p_argument_count, GDExtensionCallError &r_error) {
	// A frame that raises tells its caller so. The engine-facing boundary translates that back into a
	// completed call, because the reason is already on the script-error channel and a call error
	// would be reported a second time as a method that does not exist.
	r_error.error = GDEXTENSION_CALL_OK;
	r_error.argument = 0;
	r_error.expected = 0;

	if (code.is_empty()) {
		return Variant();
	}

	CallDepthGuard depth_guard;
	if (call_depth == 1) {
		// A new outermost call: whatever the previous one reported is answered for and done with.
		runtime_error_reported = false;
	}
	if (unlikely(!depth_guard.entered)) {
		report_runtime_error_once("Stack overflow. Check for infinite recursion in your script.", name, source, initial_line);
		return Variant();
	}

	const int optional_count = MAX(default_arguments.size() - 1, 0);
	int default_argument_index = 0;
	if (p_argument_count > argument_count) {
		r_error.error = GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
		r_error.expected = argument_count;
		return Variant();
	}
	if (p_argument_count < argument_count - optional_count) {
		r_error.error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = argument_count - optional_count;
		return Variant();
	}
	default_argument_index = argument_count - p_argument_count;

	Vector<Variant> stack;
	stack.resize(MAX(stack_size, (int)FIXED_ADDRESSES_MAX));
	Vector<Variant *> instruction_arguments;
	instruction_arguments.resize(MAX(instruction_arguments_size, 1));

	if (p_instance != nullptr && p_instance->owner != nullptr) {
		stack.write[ADDR_STACK_SELF] = p_instance->owner;
	}
	stack.write[ADDR_STACK_CLASS] = script;
	for (int i = 0; i < p_argument_count && i < argument_count; i++) {
		stack.write[i + FIXED_ADDRESSES_MAX] = *p_arguments[i];
	}

	Variant *address_spaces[ADDR_TYPE_MAX] = {
		stack.ptrw(),
		constants.ptrw(),
		p_instance != nullptr ? p_instance->members.ptrw() : nullptr,
	};
	const int address_limits[ADDR_TYPE_MAX] = {
		(int)stack.size(),
		(int)constants.size(),
		p_instance != nullptr ? (int)p_instance->members.size() : 0,
	};

	const int *code_ptr = code.ptr();
	const int code_size = code.size();
	Variant **instruction_argument_ptr = instruction_arguments.ptrw();

	int instruction_pointer = 0;
	int line = initial_line;
	String error_text;
	Variant return_value;

#define BS_IP instruction_pointer
#define BS_CHECK_SPACE(m_space)                      \
	if (unlikely((BS_IP + (m_space)) > code_size)) { \
		error_text = "Truncated compiled function."; \
		goto vm_error;                               \
	}

#define BS_GET_VARIANT_PTR(m_name, m_offset)                                                \
	Variant *m_name = nullptr;                                                              \
	{                                                                                       \
		const int address = code_ptr[BS_IP + 1 + (m_offset)];                               \
		const int address_type = (address & ADDR_TYPE_MASK) >> ADDR_BITS;                   \
		if (unlikely(address_type < 0 || address_type >= ADDR_TYPE_MAX)) {                  \
			error_text = "Bad address type in compiled function.";                          \
			goto vm_error;                                                                  \
		}                                                                                   \
		const int address_index = address & ADDR_MASK;                                      \
		if (unlikely(address_index < 0 || address_index >= address_limits[address_type])) { \
			error_text = address_type == ADDR_TYPE_MEMBER && p_instance == nullptr          \
					? String("Cannot access a member without an instance.")                 \
					: String("Bad address index in compiled function.");                    \
			goto vm_error;                                                                  \
		}                                                                                   \
		m_name = &address_spaces[address_type][address_index];                              \
	}

#define BS_GET_NAME(m_name, m_index)                                   \
	if (unlikely((m_index) < 0 || (m_index) >= global_names.size())) { \
		error_text = "Bad name index in compiled function.";           \
		goto vm_error;                                                 \
	}                                                                  \
	const StringName &m_name = global_names[m_index]

#define BS_LOAD_INSTRUCTION_ARGUMENTS                                           \
	BS_CHECK_SPACE(2);                                                          \
	const int instruction_argument_count = code_ptr[BS_IP + 1];                 \
	if (unlikely(instruction_argument_count < 0 ||                              \
				instruction_argument_count > instruction_arguments.size())) {   \
		error_text = "Bad instruction argument count.";                         \
		goto vm_error;                                                          \
	}                                                                           \
	BS_CHECK_SPACE(2 + instruction_argument_count);                             \
	for (int argument = 0; argument < instruction_argument_count; argument++) { \
		BS_GET_VARIANT_PTR(value, argument + 1);                                \
		instruction_argument_ptr[argument] = value;                             \
	}                                                                           \
	BS_IP += 1;

	while (instruction_pointer < code_size) {
		switch (code_ptr[instruction_pointer]) {
#include "bs_vm_ops_async.inc"
#include "bs_vm_ops_core.inc"
#include "bs_vm_ops_iterators.inc"
#include "bs_vm_ops_members.inc"
#include "bs_vm_ops_types.inc"
			default: {
				// Fail closed: an opcode with no handler names itself rather than falling through.
				error_text = vformat("Opcode not implemented: %s.", get_opcode_name(code_ptr[instruction_pointer]));
				goto vm_error;
			}
		}
	}
	error_text = "Ran past the end of a compiled function.";

vm_error:
	// The call happened; it raised. The engine is told the call completed, because the script-error
	// channel already carries the reason and a call-error code would be reported a second time as a
	// missing method that in fact exists.
	report_runtime_error_once(error_text, name, source, line);
	return Variant();

vm_exit:
	return return_value;

#undef BS_LOAD_INSTRUCTION_ARGUMENTS
#undef BS_GET_NAME
#undef BS_GET_VARIANT_PTR
#undef BS_CHECK_SPACE
#undef BS_IP
}

} // namespace barista_script
