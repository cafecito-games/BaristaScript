/**************************************************************************/
/*  bs_platform_variant.cpp                                               */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_platform.h"

#include <godot_cpp/core/gdextension_interface_loader.hpp>

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
