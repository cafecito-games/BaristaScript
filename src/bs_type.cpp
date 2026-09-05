/**************************************************************************/
/*  bs_type.cpp                                                           */
/*                                                                        */
/*  Hard fork of Foundry fs_type.cpp @ c9d5e35 (D1-trimmed).              */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_type.h"

namespace barista_script {

bool BSNumericConversion::is_numeric_builtin(const BSParser::DataType &p_type) {
	if (p_type.kind != BSParser::DataType::BUILTIN) {
		return false;
	}
	return p_type.builtin_type == Variant::INT || p_type.builtin_type == Variant::FLOAT;
}

BSNumericConversion::Conversion BSNumericConversion::classify(const BSParser::DataType &p_target, const BSParser::DataType &p_source, const Variant *p_constant_source_value) {
	if (!is_numeric_builtin(p_target) || !is_numeric_builtin(p_source)) {
		return Conversion::INVALID;
	}
	if (p_target.builtin_type == p_source.builtin_type) {
		return Conversion::IDENTITY;
	}
	// int -> float is the only implicit numeric conversion under D1.
	if (p_target.builtin_type == Variant::FLOAT && p_source.builtin_type == Variant::INT) {
		return Conversion::IMPLICIT_WIDEN;
	}
	if (p_target.builtin_type == Variant::INT && p_source.builtin_type == Variant::FLOAT) {
		if (p_constant_source_value != nullptr && p_constant_source_value->get_type() == Variant::FLOAT) {
			const double value = p_constant_source_value->operator double();
			if (value == Math::floor(value) && value >= (double)INT64_MIN && value <= (double)INT64_MAX) {
				return Conversion::CONSTANT_CHECKED;
			}
		}
		return Conversion::EXPLICIT_REQUIRED;
	}
	return Conversion::INVALID;
}

BSTypeCompatibility::Result BSTypeCompatibility::check(const BSParser::DataType &p_target, const BSParser::DataType &p_source) {
	return check(p_target, p_source, Options());
}

BSTypeCompatibility::Result BSTypeCompatibility::check(const BSParser::DataType &p_target, const BSParser::DataType &p_source, const Options &p_options) {
	if (!p_target.is_set() || !p_source.is_set()) {
		return Result(false, false, false);
	}
	if (p_target.is_variant()) {
		return Result(true, false, false);
	}
	if (p_source.is_variant()) {
		if (p_options.strict_dynamic) {
			return Result(false, false, false);
		}
		return Result(true, true, false);
	}

	if (p_source.is_nullable && !p_target.is_nullable) {
		if (p_options.strict_null) {
			return Result(false, false, false);
		}
		// Soft nullability: accepted with a runtime check.
		BSParser::DataType non_null_source = p_source;
		non_null_source.is_nullable = false;
		Result inner = check(p_target, non_null_source, p_options);
		if (inner.compatible) {
			inner.requires_runtime_check = true;
		}
		return inner;
	}

	if (p_target.kind == BSParser::DataType::BUILTIN && p_source.kind == BSParser::DataType::BUILTIN) {
		Result result(false, false, false);
		if (p_target.builtin_type == p_source.builtin_type) {
			result.compatible = true;
		}
		// Foundry FSTypeCompatibility::check @ c9d5e35: at conversion sites, Variant::can_convert_strict
		// bridges engine-accepted pairs such as String→StringName (Object.connect signal names) and
		// String→NodePath. D1 still gates numerics through classify afterward so float→int needs a
		// proven constant (or an explicit `as`), matching GRAMMAR.md conversions.
		if (!result.compatible && p_options.allow_implicit_conversion) {
			result.compatible = Variant::can_convert_strict(p_source.builtin_type, p_target.builtin_type);
			result.uses_implicit_conversion = result.compatible;
		}

		const bool both_numeric = BSNumericConversion::is_numeric_builtin(p_target) &&
				BSNumericConversion::is_numeric_builtin(p_source);
		if (result.compatible && both_numeric) {
			const BSNumericConversion::Conversion conversion =
					BSNumericConversion::classify(p_target, p_source, p_options.constant_source_value);
			const bool conversion_allowed = conversion == BSNumericConversion::Conversion::IDENTITY ||
					(p_options.allow_implicit_conversion &&
							(conversion == BSNumericConversion::Conversion::IMPLICIT_WIDEN ||
									conversion == BSNumericConversion::Conversion::CONSTANT_CHECKED));
			if (!conversion_allowed) {
				result.compatible = false;
				result.uses_implicit_conversion = false;
			} else if (conversion != BSNumericConversion::Conversion::IDENTITY) {
				result.uses_implicit_conversion = true;
			}
		}

		// Foundry FSTypeCompatibility::check @ c9d5e35: after carrier agreement, typed Array /
		// Dictionary containers still compare element types (invariant; no implicit conversion).
		if (result.compatible && p_target.builtin_type == Variant::ARRAY && p_source.builtin_type == Variant::ARRAY) {
			if (p_target.has_container_element_type(0) && p_source.has_container_element_type(0)) {
				Options element_options = p_options;
				element_options.allow_implicit_conversion = false;
				element_options.constant_source_value = nullptr;
				const Result element_result = check(p_target.get_container_element_type(0), p_source.get_container_element_type(0), element_options);
				result.compatible = element_result.compatible;
				result.requires_runtime_check = result.requires_runtime_check || element_result.requires_runtime_check;
				result.uses_implicit_conversion = result.uses_implicit_conversion || element_result.uses_implicit_conversion;
			} else if (p_target.has_container_element_type(0)) {
				// Typed container from untyped: carriers agree; contents need a runtime store check.
				result.requires_runtime_check = true;
			}
		}
		if (result.compatible && p_target.builtin_type == Variant::DICTIONARY && p_source.builtin_type == Variant::DICTIONARY) {
			Options element_options = p_options;
			element_options.allow_implicit_conversion = false;
			element_options.constant_source_value = nullptr;
			if (p_target.has_container_element_type(0) && p_source.has_container_element_type(0)) {
				const Result key_result = check(p_target.get_container_element_type(0), p_source.get_container_element_type(0), element_options);
				result.compatible = key_result.compatible;
				result.requires_runtime_check = result.requires_runtime_check || key_result.requires_runtime_check;
				result.uses_implicit_conversion = result.uses_implicit_conversion || key_result.uses_implicit_conversion;
			}
			if (result.compatible && p_target.has_container_element_type(1) && p_source.has_container_element_type(1)) {
				const Result value_result = check(p_target.get_container_element_type(1), p_source.get_container_element_type(1), element_options);
				result.compatible = value_result.compatible;
				result.requires_runtime_check = result.requires_runtime_check || value_result.requires_runtime_check;
				result.uses_implicit_conversion = result.uses_implicit_conversion || value_result.uses_implicit_conversion;
			}
			if (result.compatible && p_target.has_container_element_types() && !p_source.has_container_element_types()) {
				result.requires_runtime_check = true;
			}
		}
		return result;
	}

	if (p_target.kind == BSParser::DataType::NATIVE || p_target.kind == BSParser::DataType::CLASS || p_target.kind == BSParser::DataType::SCRIPT) {
		if (p_source.kind == BSParser::DataType::NATIVE || p_source.kind == BSParser::DataType::CLASS || p_source.kind == BSParser::DataType::SCRIPT) {
			if (p_target.can_reference(p_source)) {
				return Result(true, false, false);
			}
			if (allows_runtime_narrowing(p_target, p_source)) {
				return Result(true, true, false);
			}
		}
		return Result(false, false, false);
	}

	if (p_target.kind == BSParser::DataType::ENUM && p_source.kind == BSParser::DataType::ENUM) {
		return Result(p_target.native_type == p_source.native_type && p_target.enum_type == p_source.enum_type, false, false);
	}

	if (p_target.kind == BSParser::DataType::TUPLE && p_source.kind == BSParser::DataType::TUPLE) {
		return Result(is_invariant_equal(p_target, p_source), false, false);
	}

	if (p_target.kind == BSParser::DataType::UNION) {
		for (int i = 0; i < p_target.union_members.size(); i++) {
			Result member = check(p_target.union_members[i], p_source, p_options);
			if (member.compatible) {
				return member;
			}
		}
		return Result(false, false, false);
	}

	return Result(is_invariant_equal(p_target, p_source), false, false);
}

bool BSTypeCompatibility::is_compatible(const BSParser::DataType &p_target, const BSParser::DataType &p_source, bool p_allow_implicit_conversion) {
	Options options;
	options.allow_implicit_conversion = p_allow_implicit_conversion;
	return check(p_target, p_source, options).compatible;
}

bool BSTypeCompatibility::is_invariant_equal(const BSParser::DataType &p_a, const BSParser::DataType &p_b) {
	if (p_a.kind != p_b.kind || p_a.builtin_type != p_b.builtin_type || p_a.is_meta_type != p_b.is_meta_type) {
		return false;
	}
	if (p_a.native_type != p_b.native_type || p_a.script_path != p_b.script_path) {
		return false;
	}
	if (p_a.container_element_types.size() != p_b.container_element_types.size()) {
		return false;
	}
	for (int i = 0; i < p_a.container_element_types.size(); i++) {
		if (!is_invariant_equal(p_a.container_element_types[i], p_b.container_element_types[i])) {
			return false;
		}
	}
	if (p_a.type_arguments.size() != p_b.type_arguments.size()) {
		return false;
	}
	for (int i = 0; i < p_a.type_arguments.size(); i++) {
		if (!is_invariant_equal(p_a.type_arguments[i], p_b.type_arguments[i])) {
			return false;
		}
	}
	return true;
}

bool BSTypeCompatibility::allows_runtime_narrowing(const BSParser::DataType &p_narrow, const BSParser::DataType &p_wide) {
	if (p_narrow.kind == BSParser::DataType::TUPLE || p_wide.kind == BSParser::DataType::TUPLE) {
		return false;
	}
	if (p_narrow.kind == BSParser::DataType::NATIVE && p_wide.kind == BSParser::DataType::NATIVE) {
		return ClassDB::is_parent_class(p_narrow.native_type, p_wide.native_type);
	}
	return false;
}

bool BSTypeCompatibility::rest_parameter_type_is_narrowing(const BSParser::DataType &p_rest_parameter_type) {
	return p_rest_parameter_type.kind == BSParser::DataType::BUILTIN &&
			p_rest_parameter_type.builtin_type == Variant::ARRAY &&
			p_rest_parameter_type.has_container_element_type(0) &&
			!p_rest_parameter_type.get_container_element_type(0).is_variant();
}

bool BSTypeCompatibility::rest_parameter_accepts_required_arguments(const BSParser::DataType *p_implementation_rest_array,
		const BSParser::DataType *p_required_rest_array, bool p_strict_null) {
	if (p_required_rest_array == nullptr) {
		return true;
	}
	if (p_implementation_rest_array == nullptr) {
		return false;
	}
	if (!rest_parameter_type_is_narrowing(*p_implementation_rest_array)) {
		return true;
	}
	if (!rest_parameter_type_is_narrowing(*p_required_rest_array)) {
		return false;
	}
	Options element_options;
	element_options.strict_null = p_strict_null;
	return check(p_implementation_rest_array->get_container_element_type(0),
			p_required_rest_array->get_container_element_type(0), element_options)
			.compatible;
}

bool BSTypeCompatibility::rest_parameter_accepts_required_argument(const BSParser::DataType *p_implementation_rest_array,
		const BSParser::DataType &p_required_argument_type, bool p_strict_null) {
	if (p_implementation_rest_array == nullptr) {
		return false;
	}
	if (!rest_parameter_type_is_narrowing(*p_implementation_rest_array)) {
		return true;
	}
	if (p_required_argument_type.is_variant() && p_required_argument_type.is_hard_type()) {
		return false;
	}
	if (!p_required_argument_type.is_set()) {
		return true;
	}
	Options element_options;
	element_options.strict_null = p_strict_null;
	return check(p_implementation_rest_array->get_container_element_type(0), p_required_argument_type, element_options)
			.compatible;
}

} // namespace barista_script
