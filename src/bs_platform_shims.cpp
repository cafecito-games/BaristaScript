/**************************************************************************/
/*  bs_platform_shims.cpp                                                 */
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
	// Evaluator exists; fall back to comparison→bool / else left carrier for rare default failures.
	if (p_op >= Variant::OP_EQUAL && p_op <= Variant::OP_GREATER_EQUAL) {
		return Variant::BOOL;
	}
	if (p_op == Variant::OP_NOT || p_op == Variant::OP_XOR) {
		return Variant::BOOL;
	}
	return p_a;
}

/**
 * Including `bs_platform.h` proves its mappings resolve, but it does not compile the shims: a macro
 * that is never expanded and a class whose members are never called are both invisible to the
 * compiler. This translation unit uses each of them once, so the build keeps proving the shims work
 * against the current godot-cpp rather than only against the one they were written for.
 *
 * These functions are never called. They exist to be compiled.
 */
namespace bs_platform_seam {

String prove_string_builder() {
	StringBuilder builder;
	builder.append("appended as a C string");
	builder.append(String("appended as a String"));
	builder += "concatenated as a C string";
	builder += String("concatenated as a String");
	if (builder.num_strings_appended() == 0 || builder.get_string_length() == 0) {
		return String();
	}
	return builder.as_string();
}

// The comparison operators godot-cpp omits. Both orders and both senses, so a godot-cpp bump that
// adds its own overloads is caught here as an ambiguity rather than at 30 ported call sites.
bool prove_string_name_literal_comparison(const StringName &p_name) {
	const bool equal = bs_string_name_equals_literal(p_name, "BaristaScript") &&
			(p_name == "BaristaScript") && ("BaristaScript" == p_name);
	const bool different = (p_name != "BaristaScript") || ("BaristaScript" != p_name);
	return equal && !different;
}

const StringName &prove_sname() {
	return SNAME("BaristaScript");
}

Variant prove_marshalls() {
	uint8_t bytes[4] = {};
	BSMarshalls::encode_uint32(0x01020304u, bytes);
	if (BSMarshalls::decode_uint32(bytes) != 0x01020304u) {
		return Variant();
	}
	return BSMarshalls::decode_variant(BSMarshalls::encode_variant(Variant(1)));
}

PackedByteArray prove_compression() {
	const PackedByteArray compressed = BSCompression::compress_zstd(PackedByteArray());
	return BSCompression::decompress_zstd(compressed, 0);
}

Variant::Type prove_variant_operators() {
	if (!BSVariantOperators::has_validated_evaluator(Variant::OP_ADD, Variant::INT, Variant::INT)) {
		return Variant::NIL;
	}
	return BSVariantOperators::get_return_type(Variant::OP_ADD, Variant::INT, Variant::INT);
}

} // namespace bs_platform_seam
