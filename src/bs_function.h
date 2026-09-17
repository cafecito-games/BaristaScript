/**************************************************************************/
/*  bs_function.h                                                         */
/*                                                                        */
/*  The compiled function the virtual machine runs: the opcode list, the  */
/*  address encoding and the function tables. Provenance: fs_function.h   */
/*  :753-1160, :995-1004 and :1006-1014 @ c9d5e35, without the deleted    */
/*  D1 numeric opcodes and the four specialization opcodes generics own.  */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_parser.h"
#include "bs_platform.h"

namespace barista_script {

class BaristaScript;
class BSInstance;

/**
 * A native method named by class and method rather than by an engine `MethodBind *`.
 *
 * An extension cannot obtain the engine's method-bind pointers, so the method-bind opcodes carry
 * the names the dynamic call path would resolve. The handle exists so the opcode and emitter
 * shapes a validated fast path needs are already in place; nothing emits it.
 */
struct BSMethodBindHandle {
	StringName class_name;
	StringName method_name;
};

/**
 * Every opcode, in one list, expanded into both the enumerator and its name.
 *
 * The list is frozen: an opcode keeps its number, because a compiled function built by one
 * translation unit has to be read the same way by every other one.
 *
 * **A new opcode is inserted immediately before `END`, never after it.** `END` is the list's
 * terminator and the dispatch loop's stop condition, and its own number is never written into a
 * compiled function, so moving it is the one renumbering that is safe. Appending after `END` would
 * leave the terminator in the middle of the list and give the new opcode a number the loop would
 * never reach.
 *
 * Fourteen entries are reserved and never emitted -- the ten `*_VALIDATED` opcodes, and the four
 * validated method-bind and native-static calls. Every call goes through the generic Variant paths;
 * keeping their slots is what makes a validated fast path a local change rather than a renumbering.
 *
 * Two families of upstream opcodes are absent rather than reserved, because neither has anything to
 * mean here. The five numeric opcodes belonged to a fixed-width integer tower this language does not
 * have: one integer type, one carrier. The four specialization opcodes -- `GET_TYPE_PARAMETER`,
 * `ASSIGN_TYPED_CLASS_PARAMETER`, `CONSTRUCT_SPECIALIZED` and `MAKE_SPECIALIZED_CLASS_HANDLE` --
 * read reified type arguments from a receiver, and no receiver carries any.
 */
#define BS_OPCODE_LIST(X)                     \
	X(OPERATOR)                               \
	X(OPERATOR_VALIDATED)                     \
	X(TYPE_TEST_BUILTIN)                      \
	X(TYPE_TEST_ARRAY)                        \
	X(TYPE_TEST_DICTIONARY)                   \
	X(TYPE_TEST_TUPLE)                        \
	X(TYPE_TEST_ENUM)                         \
	X(TYPE_TEST_ENUM_CASE)                    \
	X(TYPE_TEST_NATIVE)                       \
	X(TYPE_TEST_SCRIPT)                       \
	X(SET_KEYED)                              \
	X(SET_KEYED_VALIDATED)                    \
	X(SET_INDEXED_VALIDATED)                  \
	X(GET_KEYED)                              \
	X(GET_KEYED_VALIDATED)                    \
	X(GET_INDEXED_VALIDATED)                  \
	X(SET_NAMED)                              \
	X(SET_NAMED_VALIDATED)                    \
	X(GET_NAMED)                              \
	X(GET_NAMED_VALIDATED)                    \
	X(SET_MEMBER)                             \
	X(GET_MEMBER)                             \
	X(SET_STATIC_VARIABLE)                    \
	X(GET_STATIC_VARIABLE)                    \
	X(ASSIGN)                                 \
	X(ASSIGN_NULL)                            \
	X(ASSIGN_TRUE)                            \
	X(ASSIGN_FALSE)                           \
	X(ASSIGN_TYPED_BUILTIN)                   \
	X(ASSIGN_TYPED_ARRAY)                     \
	X(ASSIGN_TYPED_DICTIONARY)                \
	X(ASSIGN_TYPED_NATIVE)                    \
	X(ASSIGN_TYPED_SCRIPT)                    \
	X(ASSIGN_TYPED_PARAMETER)                 \
	X(ASSIGN_TYPED_TUPLE)                     \
	X(ASSIGN_TYPED_UNION)                     \
	X(VALIDATE_CALL_ARGUMENT)                 \
	X(ASSIGN_TYPED_ARRAY_CONVERT)             \
	X(ASSIGN_TYPED_DICTIONARY_CONVERT)        \
	X(CAST_TO_BUILTIN)                        \
	X(CAST_TO_NATIVE)                         \
	X(CAST_TO_SCRIPT)                         \
	X(CONSTRUCT)                              \
	X(CONSTRUCT_VALIDATED)                    \
	X(CONSTRUCT_ARRAY)                        \
	X(CONSTRUCT_TYPED_ARRAY)                  \
	X(CONSTRUCT_TUPLE)                        \
	X(CONSTRUCT_DICTIONARY)                   \
	X(CONSTRUCT_TYPED_DICTIONARY)             \
	X(CALL)                                   \
	X(CALL_RETURN)                            \
	X(CALL_ASYNC)                             \
	X(CALL_ENUM)                              \
	X(CALL_ENUM_RETURN)                       \
	X(CALL_ENUM_ASYNC)                        \
	X(CALL_UTILITY)                           \
	X(CALL_UTILITY_VALIDATED)                 \
	X(CALL_BARISTA_SCRIPT_UTILITY)            \
	X(CALL_BUILTIN_TYPE_VALIDATED)            \
	X(CALL_SELF_BASE)                         \
	X(CALL_METHOD_BIND)                       \
	X(CALL_METHOD_BIND_RET)                   \
	X(CALL_BUILTIN_STATIC)                    \
	X(CALL_NATIVE_STATIC)                     \
	X(CALL_NATIVE_STATIC_VALIDATED_RETURN)    \
	X(CALL_NATIVE_STATIC_VALIDATED_NO_RETURN) \
	X(CALL_METHOD_BIND_VALIDATED_RETURN)      \
	X(CALL_METHOD_BIND_VALIDATED_NO_RETURN)   \
	X(AWAIT)                                  \
	X(AWAIT_RESUME)                           \
	X(CREATE_LAMBDA)                          \
	X(CREATE_SELF_LAMBDA)                     \
	X(JUMP)                                   \
	X(JUMP_IF)                                \
	X(JUMP_IF_NOT)                            \
	X(JUMP_TO_DEF_ARGUMENT)                   \
	X(JUMP_IF_SHARED)                         \
	X(RETURN)                                 \
	X(RETURN_TYPED_BUILTIN)                   \
	X(RETURN_TYPED_ARRAY)                     \
	X(RETURN_TYPED_DICTIONARY)                \
	X(RETURN_TYPED_NATIVE)                    \
	X(RETURN_TYPED_SCRIPT)                    \
	X(ITERATE_BEGIN)                          \
	X(ITERATE_BEGIN_INT)                      \
	X(ITERATE_BEGIN_FLOAT)                    \
	X(ITERATE_BEGIN_VECTOR2)                  \
	X(ITERATE_BEGIN_VECTOR2I)                 \
	X(ITERATE_BEGIN_VECTOR3)                  \
	X(ITERATE_BEGIN_VECTOR3I)                 \
	X(ITERATE_BEGIN_STRING)                   \
	X(ITERATE_BEGIN_DICTIONARY)               \
	X(ITERATE_BEGIN_ARRAY)                    \
	X(ITERATE_BEGIN_PACKED_BYTE_ARRAY)        \
	X(ITERATE_BEGIN_PACKED_INT32_ARRAY)       \
	X(ITERATE_BEGIN_PACKED_INT64_ARRAY)       \
	X(ITERATE_BEGIN_PACKED_FLOAT32_ARRAY)     \
	X(ITERATE_BEGIN_PACKED_FLOAT64_ARRAY)     \
	X(ITERATE_BEGIN_PACKED_STRING_ARRAY)      \
	X(ITERATE_BEGIN_PACKED_VECTOR2_ARRAY)     \
	X(ITERATE_BEGIN_PACKED_VECTOR3_ARRAY)     \
	X(ITERATE_BEGIN_PACKED_COLOR_ARRAY)       \
	X(ITERATE_BEGIN_PACKED_VECTOR4_ARRAY)     \
	X(ITERATE_BEGIN_OBJECT)                   \
	X(ITERATE_BEGIN_RANGE)                    \
	X(ITERATE)                                \
	X(ITERATE_INT)                            \
	X(ITERATE_FLOAT)                          \
	X(ITERATE_VECTOR2)                        \
	X(ITERATE_VECTOR2I)                       \
	X(ITERATE_VECTOR3)                        \
	X(ITERATE_VECTOR3I)                       \
	X(ITERATE_STRING)                         \
	X(ITERATE_DICTIONARY)                     \
	X(ITERATE_ARRAY)                          \
	X(ITERATE_PACKED_BYTE_ARRAY)              \
	X(ITERATE_PACKED_INT32_ARRAY)             \
	X(ITERATE_PACKED_INT64_ARRAY)             \
	X(ITERATE_PACKED_FLOAT32_ARRAY)           \
	X(ITERATE_PACKED_FLOAT64_ARRAY)           \
	X(ITERATE_PACKED_STRING_ARRAY)            \
	X(ITERATE_PACKED_VECTOR2_ARRAY)           \
	X(ITERATE_PACKED_VECTOR3_ARRAY)           \
	X(ITERATE_PACKED_COLOR_ARRAY)             \
	X(ITERATE_PACKED_VECTOR4_ARRAY)           \
	X(ITERATE_OBJECT)                         \
	X(ITERATE_RANGE)                          \
	X(STORE_GLOBAL)                           \
	X(STORE_NAMED_GLOBAL)                     \
	X(TYPE_ADJUST_BOOL)                       \
	X(TYPE_ADJUST_INT)                        \
	X(TYPE_ADJUST_FLOAT)                      \
	X(TYPE_ADJUST_STRING)                     \
	X(TYPE_ADJUST_VECTOR2)                    \
	X(TYPE_ADJUST_VECTOR2I)                   \
	X(TYPE_ADJUST_RECT2)                      \
	X(TYPE_ADJUST_RECT2I)                     \
	X(TYPE_ADJUST_VECTOR3)                    \
	X(TYPE_ADJUST_VECTOR3I)                   \
	X(TYPE_ADJUST_TRANSFORM2D)                \
	X(TYPE_ADJUST_VECTOR4)                    \
	X(TYPE_ADJUST_VECTOR4I)                   \
	X(TYPE_ADJUST_PLANE)                      \
	X(TYPE_ADJUST_QUATERNION)                 \
	X(TYPE_ADJUST_AABB)                       \
	X(TYPE_ADJUST_BASIS)                      \
	X(TYPE_ADJUST_TRANSFORM3D)                \
	X(TYPE_ADJUST_PROJECTION)                 \
	X(TYPE_ADJUST_COLOR)                      \
	X(TYPE_ADJUST_STRING_NAME)                \
	X(TYPE_ADJUST_NODE_PATH)                  \
	X(TYPE_ADJUST_RID)                        \
	X(TYPE_ADJUST_OBJECT)                     \
	X(TYPE_ADJUST_CALLABLE)                   \
	X(TYPE_ADJUST_SIGNAL)                     \
	X(TYPE_ADJUST_DICTIONARY)                 \
	X(TYPE_ADJUST_ARRAY)                      \
	X(TYPE_ADJUST_PACKED_BYTE_ARRAY)          \
	X(TYPE_ADJUST_PACKED_INT32_ARRAY)         \
	X(TYPE_ADJUST_PACKED_INT64_ARRAY)         \
	X(TYPE_ADJUST_PACKED_FLOAT32_ARRAY)       \
	X(TYPE_ADJUST_PACKED_FLOAT64_ARRAY)       \
	X(TYPE_ADJUST_PACKED_STRING_ARRAY)        \
	X(TYPE_ADJUST_PACKED_VECTOR2_ARRAY)       \
	X(TYPE_ADJUST_PACKED_VECTOR3_ARRAY)       \
	X(TYPE_ADJUST_PACKED_COLOR_ARRAY)         \
	X(TYPE_ADJUST_PACKED_VECTOR4_ARRAY)       \
	X(LOAD_STATIC_SELF_CLASS)                 \
	X(ASSERT)                                 \
	X(BREAKPOINT)                             \
	X(LINE)                                   \
	X(END)

class BSFunction {
	friend class BSCompiler;
	friend class BSByteCodeGenerator;
	friend class BaristaScript;
	friend class BSInstance;
#ifdef BARISTA_TESTS
	// Lets a native case assemble a function by hand, which is the only way to reach an opcode the
	// emitter refuses to write.
	friend struct BSFunctionTestAccess;
#endif

public:
	// Set on the builtin-type operand of OPCODE_ASSIGN_TYPED_BUILTIN / OPCODE_RETURN_TYPED_BUILTIN to
	// mark the target as nullable, so a null source is stored as-is instead of being rejected or
	// converted. The flag sits well above Variant::VARIANT_MAX, so the real type is recovered by
	// masking it off.
	//
	// Bit 24 is also where an *address* word's space tag begins, and the two never meet: this flag
	// is only ever set on a type operand, which is a plain integer the address decoder never reads.
	// A flag added here must stay on type operands for the same reason.
	static constexpr int NULLABLE_TYPE_OPERAND_FLAG = 1 << 24;

	enum Opcode {
#define BS_DECLARE_OPCODE(m_name) OPCODE_##m_name,
		BS_OPCODE_LIST(BS_DECLARE_OPCODE)
#undef BS_DECLARE_OPCODE
				OPCODE_MAX
	};

	/** The enumerator's own spelling, for diagnostics. Never null; unknown values name themselves. */
	static String get_opcode_name(int p_opcode);

	enum Address {
		ADDR_BITS = 24,
		ADDR_MASK = ((1 << ADDR_BITS) - 1),
		ADDR_TYPE_MASK = ~ADDR_MASK,
		ADDR_TYPE_STACK = 0,
		ADDR_TYPE_CONSTANT = 1,
		ADDR_TYPE_MEMBER = 2,
		ADDR_TYPE_MAX = 3,
	};

	enum FixedAddresses {
		ADDR_STACK_SELF = 0,
		ADDR_STACK_CLASS = 1,
		ADDR_STACK_NIL = 2,
		FIXED_ADDRESSES_MAX = 3,
		ADDR_SELF = ADDR_STACK_SELF | (ADDR_TYPE_STACK << ADDR_BITS),
		ADDR_CLASS = ADDR_STACK_CLASS | (ADDR_TYPE_STACK << ADDR_BITS),
		ADDR_NIL = ADDR_STACK_NIL | (ADDR_TYPE_STACK << ADDR_BITS),
	};

	BSFunction() = default;
	~BSFunction();

	BSFunction(const BSFunction &) = delete;
	BSFunction &operator=(const BSFunction &) = delete;

	/**
	 * Runs the function.
	 *
	 * `p_instance` is null for a static call and for a call made before an instance exists, in which
	 * case `self` is nil and any member access reports rather than dereferencing.
	 */
	Variant call(BSInstance *p_instance, const Variant **p_arguments, int p_argument_count, GDExtensionCallError &r_error);

	const StringName &get_name() const { return name; }
	const String &get_source() const { return source; }
	BaristaScript *get_script() const { return script; }
	bool is_static() const { return is_static_function; }
	int get_argument_count() const { return argument_count; }
	int get_default_argument_count() const { return default_arguments.size(); }
	int get_max_stack_size() const { return stack_size; }
	int get_initial_line() const { return initial_line; }
	const BSParser::DataType &get_return_type() const { return return_type; }
	const Vector<BSParser::DataType> &get_argument_types() const { return argument_types; }
	const Variant &get_rpc_config() const { return rpc_config; }

private:
	BaristaScript *script = nullptr;
	StringName name;
	String source;
	String signature;
	bool is_static_function = false;
	Variant rpc_config;

	BSParser::DataType return_type;
	Vector<BSParser::DataType> argument_types;
	int argument_count = 0;
	int stack_size = 0;
	int instruction_arguments_size = 0;
	int initial_line = 0;

	Vector<int> code;
	Vector<Variant> constants;
	Vector<StringName> global_names;
	Vector<BSMethodBindHandle> methods;
	Vector<BSFunction *> lambdas;
	// Code positions of each optional parameter's default-value block, indexed from the first
	// optional parameter. `JUMP_TO_DEF_ARGUMENT` selects one from the actual argument count.
	Vector<int> default_arguments;
};

/**
 * Reports a runtime error on the engine's script-error channel, the channel a corpus transcript is
 * made of. `p_function`, `p_file` and `p_line` name the frame the error was raised in.
 */
void bs_report_runtime_error(const String &p_description, const String &p_function, const String &p_file, int p_line);

/**
 * Whether the call that just returned reported a runtime error.
 *
 * A frame that fails abandons its caller too, and reporting at every level turns one fault into as
 * many messages as the call stack is deep. Only the frame that found the fault reports; the flag is
 * cleared when a new outermost call begins, so a caller outside the virtual machine can still ask
 * whether the call it just made ran to completion.
 */
bool bs_runtime_error_was_reported();

} // namespace barista_script
