/**************************************************************************/
/*  bs_platform_variant.cpp                                               */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_platform.h"

#include <godot_cpp/core/gdextension_interface_loader.hpp>

bool BSVariantOperators::construct(Variant::Type p_type, const Variant &p_source, Variant &r_value) {
	alignas(8) uint8_t storage[GODOT_CPP_VARIANT_SIZE]{};
	const GDExtensionConstVariantPtr arguments[1] = { p_source._native_ptr() };
	GDExtensionCallError error{};
	gdextension_interface::variant_construct((GDExtensionVariantType)p_type,
			(GDExtensionUninitializedVariantPtr)storage, arguments, 1, &error);
	if (error.error != GDEXTENSION_CALL_OK) {
		gdextension_interface::variant_destroy((GDExtensionVariantPtr)storage);
		return false;
	}
	r_value = Variant((GDExtensionConstVariantPtr)storage);
	gdextension_interface::variant_destroy((GDExtensionVariantPtr)storage);
	return true;
}

bool BSVariantOperators::construct_container(Variant::Type p_type, const Variant *p_arguments, int p_count, Variant &r_value) {
	// Only the pinned Array/Dictionary overloads enter this pure-value adapter.
	if ((p_type != Variant::ARRAY && p_type != Variant::DICTIONARY) ||
			(p_count != 1 && p_count != (p_type == Variant::ARRAY ? 4 : 7)) || !p_arguments)
		return false;
	GDExtensionConstVariantPtr arguments[7]{};
	for (int i = 0; i < p_count; ++i)
		arguments[i] = p_arguments[i]._native_ptr();
	alignas(8) uint8_t storage[GODOT_CPP_VARIANT_SIZE]{};
	GDExtensionCallError error{};
	gdextension_interface::variant_construct((GDExtensionVariantType)p_type,
			(GDExtensionUninitializedVariantPtr)storage, arguments, p_count, &error);
	if (error.error != GDEXTENSION_CALL_OK) {
		gdextension_interface::variant_destroy((GDExtensionVariantPtr)storage);
		return false;
	}
	const Variant result((GDExtensionConstVariantPtr)storage);
	gdextension_interface::variant_destroy((GDExtensionVariantPtr)storage);
	const auto script_slot = [](const Variant &p_slot) {
		// Godot exposes absent script metadata as an OBJECT-null Variant, while
		// source `null` arguments arrive as NIL; their Variant types differ.
		return p_slot.get_type() == Variant::NIL ? Variant(static_cast<Object *>(nullptr)) : p_slot;
	};
	// Typed constructors call set_typed/assign, whose failures do not propagate
	// through CallError. Assignment is atomic: failed conversion leaves the fresh
	// destination empty. Check metadata too, since failed set_typed can leave an
	// untyped destination. Do not publish either failed result as a constant.
	if (p_count > 1 && p_type == Variant::ARRAY) {
		const Array value = result;
		if (value.get_typed_builtin() != int64_t(p_arguments[1]) ||
				value.get_typed_class_name() != StringName(p_arguments[2]) ||
				value.get_typed_script() != script_slot(p_arguments[3]) ||
				value.size() != Array(p_arguments[0]).size())
			return false;
	} else if (p_count > 1) {
		const Dictionary value = result;
		if (value.get_typed_key_builtin() != int64_t(p_arguments[1]) ||
				value.get_typed_key_class_name() != StringName(p_arguments[2]) ||
				value.get_typed_key_script() != script_slot(p_arguments[3]) ||
				value.get_typed_value_builtin() != int64_t(p_arguments[4]) ||
				value.get_typed_value_class_name() != StringName(p_arguments[5]) ||
				value.get_typed_value_script() != script_slot(p_arguments[6]) ||
				(value.is_empty() && !Dictionary(p_arguments[0]).is_empty()))
			return false;
		// Successful key conversions may collapse multiple keys; size equality is
		// not a valid Dictionary postcondition.
	}
	r_value = result;
	return true;
}

bool BSVariantOperators::has_validated_evaluator(Variant::Operator p_op, Variant::Type p_a, Variant::Type p_b) {
	if (p_op < 0 || p_op >= Variant::OP_MAX || p_a < 0 || p_a >= Variant::VARIANT_MAX || p_b < 0 || p_b >= Variant::VARIANT_MAX) {
		return false;
	}
	return godot::gdextension_interface::variant_get_ptr_operator_evaluator(
				   static_cast<GDExtensionVariantOperator>(p_op),
				   static_cast<GDExtensionVariantType>(p_a),
				   static_cast<GDExtensionVariantType>(p_b)) != nullptr;
}

Variant::Type BSVariantOperators::get_return_type(Variant::Operator p_op, Variant::Type p_a, Variant::Type p_b) {
	if (!has_validated_evaluator(p_op, p_a, p_b)) {
		return Variant::NIL;
	}
	Variant a = UtilityFunctions::type_convert(Variant(), (int64_t)p_a);
	Variant b = UtilityFunctions::type_convert(Variant(), (int64_t)p_b);
	if (p_op == Variant::OP_DIVIDE || p_op == Variant::OP_MODULE) {
		auto non_zero = [](Variant::Type p_type, Variant &r_value) {
			switch (p_type) {
				case Variant::INT:
					r_value = (int64_t)1;
					break;
				case Variant::FLOAT:
					r_value = 1.0;
					break;
				case Variant::VECTOR2:
					r_value = Vector2(1, 1);
					break;
				case Variant::VECTOR2I:
					r_value = Vector2i(1, 1);
					break;
				case Variant::VECTOR3:
					r_value = Vector3(1, 1, 1);
					break;
				case Variant::VECTOR3I:
					r_value = Vector3i(1, 1, 1);
					break;
				case Variant::VECTOR4:
					r_value = Vector4(1, 1, 1, 1);
					break;
				case Variant::VECTOR4I:
					r_value = Vector4i(1, 1, 1, 1);
					break;
				default:
					break;
			}
		};
		non_zero(p_a, a);
		non_zero(p_b, b);
	}
	Variant result;
	bool valid = false;
	Variant::evaluate(p_op, a, b, result, valid);
	if (valid) {
		return result.get_type();
	}
	if (p_op >= Variant::OP_EQUAL && p_op <= Variant::OP_GREATER_EQUAL) {
		return Variant::BOOL;
	}
	if (p_op == Variant::OP_NOT || p_op == Variant::OP_XOR) {
		return Variant::BOOL;
	}
	return p_a;
}
