/**************************************************************************/
/*  bs_vm_ops_types.inc                                                   */
/*                                                                        */
/*  Type tests, casts and slot type adjustment. Included inside the       */
/*  dispatch switch in bs_vm.cpp, which owns the operand-reading macros   */
/*  and the exit labels.                                                  */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

case OPCODE_TYPE_TEST_BUILTIN:
case OPCODE_TYPE_TEST_ARRAY:
case OPCODE_TYPE_TEST_DICTIONARY:
case OPCODE_TYPE_TEST_NATIVE:
case OPCODE_TYPE_TEST_SCRIPT:
case OPCODE_TYPE_TEST_TUPLE: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(destination, 0);
	BS_GET_VARIANT_PTR(value, 1);
	BS_GET_RUNTIME_TYPE(type, code_ptr[BS_IP + 3]);
	*destination = type.accepts(*value, false, true);
	BS_IP += 4;
} break;

case OPCODE_ASSIGN_TYPED_BUILTIN:
case OPCODE_ASSIGN_TYPED_ARRAY:
case OPCODE_ASSIGN_TYPED_DICTIONARY:
case OPCODE_ASSIGN_TYPED_NATIVE:
case OPCODE_ASSIGN_TYPED_SCRIPT:
case OPCODE_ASSIGN_TYPED_PARAMETER:
case OPCODE_ASSIGN_TYPED_TUPLE:
case OPCODE_ASSIGN_TYPED_UNION:
case OPCODE_ASSIGN_TYPED_ARRAY_CONVERT:
case OPCODE_ASSIGN_TYPED_DICTIONARY_CONVERT: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(destination, 0);
	BS_GET_VARIANT_PTR(source, 1);
	BS_GET_RUNTIME_TYPE(type, code_ptr[BS_IP + 3]);
	Variant converted;
	const bool allow_erased_container = code_ptr[BS_IP] == OPCODE_ASSIGN_TYPED_ARRAY_CONVERT ||
			code_ptr[BS_IP] == OPCODE_ASSIGN_TYPED_DICTIONARY_CONVERT;
	if (unlikely(!type.convert(*source, converted, error_text, allow_erased_container))) {
		goto vm_error;
	}
	*destination = converted;
	BS_IP += 4;
} break;

case OPCODE_CAST_TO_BUILTIN:
case OPCODE_CAST_TO_NATIVE:
case OPCODE_CAST_TO_SCRIPT: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(source, 0);
	BS_GET_VARIANT_PTR(destination, 1);
	BS_GET_RUNTIME_TYPE(type, code_ptr[BS_IP + 3]);
	Variant converted;
	if (unlikely(!type.cast(*source, converted, error_text))) {
		goto vm_error;
	}
	*destination = converted;
	BS_IP += 4;
} break;

case OPCODE_CONSTRUCT_TYPED_ARRAY: {
	BS_LOAD_INSTRUCTION_ARGUMENTS
	BS_IP += instruction_argument_count;
	BS_CHECK_SPACE(3);
	const int argument_count = code_ptr[BS_IP + 1];
	if (unlikely(argument_count < 0 || argument_count >= instruction_argument_count)) {
		error_text = "Bad typed-array literal operands in compiled function.";
		goto vm_error;
	}
	BS_GET_RUNTIME_TYPE(element_type, code_ptr[BS_IP + 2]);
	Array source;
	source.resize(argument_count);
	for (int i = 0; i < argument_count; i++) {
		source[i] = *instruction_argument_ptr[i];
	}
	BSRuntimeType array_type = BSRuntimeType::builtin(Variant::ARRAY);
	array_type.container_element_types.push_back(element_type);
	Variant converted;
	if (unlikely(!array_type.convert(source, converted, error_text, true))) {
		goto vm_error;
	}
	*instruction_argument_ptr[argument_count] = converted;
	BS_IP += 3;
} break;

case OPCODE_CONSTRUCT_TYPED_DICTIONARY: {
	BS_LOAD_INSTRUCTION_ARGUMENTS
	BS_IP += instruction_argument_count;
	BS_CHECK_SPACE(4);
	const int pair_count = code_ptr[BS_IP + 1];
	if (unlikely(pair_count < 0 || pair_count * 2 >= instruction_argument_count)) {
		error_text = "Bad typed-dictionary literal operands in compiled function.";
		goto vm_error;
	}
	BS_GET_RUNTIME_TYPE(key_type, code_ptr[BS_IP + 2]);
	BS_GET_RUNTIME_TYPE(value_type, code_ptr[BS_IP + 3]);
	Dictionary source;
	for (int i = 0; i < pair_count; i++) {
		source[*instruction_argument_ptr[i * 2]] = *instruction_argument_ptr[i * 2 + 1];
	}
	BSRuntimeType dictionary_type = BSRuntimeType::builtin(Variant::DICTIONARY);
	dictionary_type.container_element_types.push_back(key_type);
	dictionary_type.container_element_types.push_back(value_type);
	Variant converted;
	if (unlikely(!dictionary_type.convert(source, converted, error_text, true))) {
		goto vm_error;
	}
	*instruction_argument_ptr[pair_count * 2] = converted;
	BS_IP += 4;
} break;

case OPCODE_RETURN_TYPED_BUILTIN:
case OPCODE_RETURN_TYPED_ARRAY:
case OPCODE_RETURN_TYPED_DICTIONARY:
case OPCODE_RETURN_TYPED_NATIVE:
case OPCODE_RETURN_TYPED_SCRIPT: {
	BS_CHECK_SPACE(3);
	BS_GET_VARIANT_PTR(value, 0);
	BS_GET_RUNTIME_TYPE(type, code_ptr[BS_IP + 2]);
	if (unlikely(!type.convert(*value, return_value, error_text,
			code_ptr[BS_IP] == OPCODE_RETURN_TYPED_ARRAY || code_ptr[BS_IP] == OPCODE_RETURN_TYPED_DICTIONARY))) {
		goto vm_error;
	}
	goto vm_exit;
} break;

#define BS_TYPE_ADJUST(m_opcode, m_type)              \
	case OPCODE_##m_opcode: {                         \
		BS_CHECK_SPACE(2);                            \
		BS_GET_VARIANT_PTR(slot, 0);                  \
		*slot = Variant(m_type());                    \
		BS_IP += 2;                                   \
	} break;

	BS_TYPE_ADJUST(TYPE_ADJUST_BOOL, bool)
	BS_TYPE_ADJUST(TYPE_ADJUST_INT, int64_t)
	BS_TYPE_ADJUST(TYPE_ADJUST_FLOAT, double)
	BS_TYPE_ADJUST(TYPE_ADJUST_STRING, String)
	BS_TYPE_ADJUST(TYPE_ADJUST_VECTOR2, Vector2)
	BS_TYPE_ADJUST(TYPE_ADJUST_VECTOR2I, Vector2i)
	BS_TYPE_ADJUST(TYPE_ADJUST_RECT2, Rect2)
	BS_TYPE_ADJUST(TYPE_ADJUST_RECT2I, Rect2i)
	BS_TYPE_ADJUST(TYPE_ADJUST_VECTOR3, Vector3)
	BS_TYPE_ADJUST(TYPE_ADJUST_VECTOR3I, Vector3i)
	BS_TYPE_ADJUST(TYPE_ADJUST_TRANSFORM2D, Transform2D)
	BS_TYPE_ADJUST(TYPE_ADJUST_VECTOR4, Vector4)
	BS_TYPE_ADJUST(TYPE_ADJUST_VECTOR4I, Vector4i)
	BS_TYPE_ADJUST(TYPE_ADJUST_PLANE, Plane)
	BS_TYPE_ADJUST(TYPE_ADJUST_QUATERNION, Quaternion)
	BS_TYPE_ADJUST(TYPE_ADJUST_AABB, AABB)
	BS_TYPE_ADJUST(TYPE_ADJUST_BASIS, Basis)
	BS_TYPE_ADJUST(TYPE_ADJUST_TRANSFORM3D, Transform3D)
	BS_TYPE_ADJUST(TYPE_ADJUST_PROJECTION, Projection)
	BS_TYPE_ADJUST(TYPE_ADJUST_COLOR, Color)
	BS_TYPE_ADJUST(TYPE_ADJUST_STRING_NAME, StringName)
	BS_TYPE_ADJUST(TYPE_ADJUST_NODE_PATH, NodePath)
	BS_TYPE_ADJUST(TYPE_ADJUST_RID, RID)
	BS_TYPE_ADJUST(TYPE_ADJUST_CALLABLE, Callable)
	BS_TYPE_ADJUST(TYPE_ADJUST_SIGNAL, Signal)
	BS_TYPE_ADJUST(TYPE_ADJUST_DICTIONARY, Dictionary)
	BS_TYPE_ADJUST(TYPE_ADJUST_ARRAY, Array)
	BS_TYPE_ADJUST(TYPE_ADJUST_PACKED_BYTE_ARRAY, PackedByteArray)
	BS_TYPE_ADJUST(TYPE_ADJUST_PACKED_INT32_ARRAY, PackedInt32Array)
	BS_TYPE_ADJUST(TYPE_ADJUST_PACKED_INT64_ARRAY, PackedInt64Array)
	BS_TYPE_ADJUST(TYPE_ADJUST_PACKED_FLOAT32_ARRAY, PackedFloat32Array)
	BS_TYPE_ADJUST(TYPE_ADJUST_PACKED_FLOAT64_ARRAY, PackedFloat64Array)
	BS_TYPE_ADJUST(TYPE_ADJUST_PACKED_STRING_ARRAY, PackedStringArray)
	BS_TYPE_ADJUST(TYPE_ADJUST_PACKED_VECTOR2_ARRAY, PackedVector2Array)
	BS_TYPE_ADJUST(TYPE_ADJUST_PACKED_VECTOR3_ARRAY, PackedVector3Array)
	BS_TYPE_ADJUST(TYPE_ADJUST_PACKED_COLOR_ARRAY, PackedColorArray)
	BS_TYPE_ADJUST(TYPE_ADJUST_PACKED_VECTOR4_ARRAY, PackedVector4Array)
#undef BS_TYPE_ADJUST

case OPCODE_TYPE_ADJUST_OBJECT: {
	BS_CHECK_SPACE(2);
	BS_GET_VARIANT_PTR(slot, 0);
	*slot = (Object *)nullptr;
	BS_IP += 2;
} break;
