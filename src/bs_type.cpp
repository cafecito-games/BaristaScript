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
		if (p_target.builtin_type == p_source.builtin_type) {
			return Result(true, false, false);
		}
		const BSNumericConversion::Conversion conversion = BSNumericConversion::classify(p_target, p_source, p_options.constant_source_value);
		if (conversion == BSNumericConversion::Conversion::IDENTITY) {
			return Result(true, false, false);
		}
		if (p_options.allow_implicit_conversion && conversion == BSNumericConversion::Conversion::IMPLICIT_WIDEN) {
			return Result(true, false, true);
		}
		if (conversion == BSNumericConversion::Conversion::CONSTANT_CHECKED) {
			return Result(true, false, true);
		}
		return Result(false, false, false);
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

} // namespace barista_script
