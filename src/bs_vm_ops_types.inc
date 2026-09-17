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

case OPCODE_TYPE_TEST_BUILTIN: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(destination, 0);
	BS_GET_VARIANT_PTR(value, 1);
	const int type_index = code_ptr[BS_IP + 3];
	if (unlikely(type_index < 0 || type_index >= Variant::VARIANT_MAX)) {
		error_text = "Bad type-test operand in compiled function.";
		goto vm_error;
	}
	*destination = value->get_type() == (Variant::Type)type_index;
	BS_IP += 4;
} break;

case OPCODE_TYPE_TEST_NATIVE: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(destination, 0);
	BS_GET_VARIANT_PTR(value, 1);
	BS_GET_NAME(native_class, code_ptr[BS_IP + 3]);
	Object *object = value->get_type() == Variant::OBJECT ? (Object *)*value : nullptr;
	*destination = object != nullptr && ClassDB::is_parent_class(object->get_class(), native_class);
	BS_IP += 4;
} break;

case OPCODE_ASSIGN_TYPED_NATIVE: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(destination, 0);
	BS_GET_VARIANT_PTR(source, 1);
	BS_GET_NAME(native_class, code_ptr[BS_IP + 3]);
	if (source->get_type() == Variant::NIL) {
		// An object slot holds a reference or nothing; nothing is a value it admits.
		*destination = Variant();
	} else {
		Object *object = source->get_validated_object();
		if (unlikely(object == nullptr || !ClassDB::is_parent_class(object->get_class(), native_class))) {
			error_text = vformat(R"(Cannot assign a value of type "%s" to a slot of type "%s".)",
					source->get_type() == Variant::OBJECT ? String("a freed or unrelated object")
														  : Variant::get_type_name(source->get_type()),
					String(native_class));
			goto vm_error;
		}
		*destination = *source;
	}
	BS_IP += 4;
} break;

case OPCODE_CAST_TO_BUILTIN: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(source, 0);
	BS_GET_VARIANT_PTR(destination, 1);
	const int type_index = code_ptr[BS_IP + 3];
	if (unlikely(type_index < 0 || type_index >= Variant::VARIANT_MAX)) {
		error_text = "Bad cast operand in compiled function.";
		goto vm_error;
	}
	GDExtensionCallError cast_error;
	cast_error.error = GDEXTENSION_CALL_OK;
	const Variant *cast_arguments[1] = { source };
	Variant converted;
	godot::gdextension_interface::variant_construct((GDExtensionVariantType)type_index, &converted,
			reinterpret_cast<const GDExtensionConstVariantPtr *>(cast_arguments), 1, &cast_error);
	if (unlikely(cast_error.error != GDEXTENSION_CALL_OK)) {
		error_text = vformat(R"(Cannot cast a value of type "%s" to "%s".)",
				Variant::get_type_name(source->get_type()), Variant::get_type_name((Variant::Type)type_index));
		goto vm_error;
	}
	*destination = converted;
	BS_IP += 4;
} break;

case OPCODE_CAST_TO_NATIVE: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(source, 0);
	BS_GET_VARIANT_PTR(destination, 1);
	BS_GET_NAME(native_class, code_ptr[BS_IP + 3]);
	Object *object = source->get_type() == Variant::OBJECT ? (Object *)*source : nullptr;
	// A failed cast yields null rather than an error: that is what the language's `as` means.
	*destination = object != nullptr && ClassDB::is_parent_class(object->get_class(), native_class) ? *source : Variant();
	BS_IP += 4;
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
