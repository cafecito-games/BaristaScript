/**************************************************************************/
/*  bs_type.cpp                                                           */
/*                                                                        */
/*  Hard fork of Foundry fs_type.cpp @ c9d5e35 (D1-trimmed).              */
/*  Union sources require every alternative to satisfy the target.        */
/*  Target-UNION uses two-pass select + numeric store-carrier gate.       */
/*  Coroutine[T] assignability matches phantom results invariantly.       */
/*  TYPE_PARAMETER / @Self assignability arms (identity, undecidable     */
/*  target, erased-source runtime check). Free-T undecidable laundering  */
/*  remains M5 residual until method/class type parameters are live.     */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_type.h"

namespace barista_script {

namespace {

// Foundry _is_erased_type_parameter @ c9d5e35: a method-scope parameter is chosen per call and
// erased to Variant, so no check exists or can be emitted for a slot declared with it.
bool _is_erased_type_parameter(const BSParser::DataType &p_type) {
	return p_type.kind == BSParser::DataType::TYPE_PARAMETER &&
			p_type.type_parameter_scope == BSParser::DataType::TYPE_PARAMETER_METHOD;
}

// Foundry _is_type_parameter_bounded_by_final_class @ c9d5e35: a parameter bounded by a `final`
// class denotes exactly that class — no subtype of the bound can exist.
bool _is_type_parameter_bounded_by_final_class(const BSParser::DataType &p_type) {
	return p_type.kind == BSParser::DataType::TYPE_PARAMETER && p_type.type_parameter_bound.size() == 1 &&
			p_type.type_parameter_bound[0].kind == BSParser::DataType::CLASS &&
			p_type.type_parameter_bound[0].class_type != nullptr &&
			p_type.type_parameter_bound[0].class_type->is_final;
}

// Foundry _is_undecidable_type_parameter_target @ c9d5e35: a method-scope parameter is always
// undecidable; a class-scope one is undecidable only without a receiver. `@Self` is excluded — it
// denotes the class the frame runs against (static frames included) and is never a laundering target.
bool _is_undecidable_type_parameter_target(const BSParser::DataType &p_type, const BSTypeCompatibility::Options &p_options) {
	if (_is_erased_type_parameter(p_type)) {
		return true;
	}
	return !p_options.receiver_is_available && p_type.kind == BSParser::DataType::TYPE_PARAMETER &&
			p_type.type_parameter_scope == BSParser::DataType::TYPE_PARAMETER_CLASS &&
			p_type.type_parameter_name != SNAME("@Self");
}

// Foundry _union_store_converts_carrier @ c9d5e35 (D1-trimmed): a UNION slot has no carrier of its
// own, so an alternative reached only by converting the value is admitted only where the store can
// perform that conversion. Under D1 that is the numeric IDENTITY / IMPLICIT_WIDEN /
// CONSTANT_CHECKED model — engine bridges such as String→StringName have no union-store counterpart.
bool _union_store_converts_carrier(const BSParser::DataType &p_alternative, const BSParser::DataType &p_source,
		const BSTypeCompatibility::Options &p_options) {
	if (!p_options.allow_implicit_conversion ||
			!BSNumericConversion::is_numeric_builtin(p_alternative) || !BSNumericConversion::is_numeric_builtin(p_source)) {
		return false;
	}
	switch (BSNumericConversion::classify(p_alternative, p_source, p_options.constant_source_value)) {
		case BSNumericConversion::Conversion::IDENTITY:
		case BSNumericConversion::Conversion::IMPLICIT_WIDEN:
		case BSNumericConversion::Conversion::CONSTANT_CHECKED:
			return true;
		default:
			return false;
	}
}

// Foundry _select_union_alternative @ c9d5e35: prefer an alternative the source satisfies without
// converting; only then admit a converting alternative the union store can actually carry.
bool _select_union_alternative(const BSParser::DataType &p_target, const BSParser::DataType &p_source,
		const BSTypeCompatibility::Options &p_options, BSParser::DataType &r_alternative,
		BSTypeCompatibility::Result &r_member_result) {
	for (int pass = 0; pass < 2; pass++) {
		const bool converting_pass = pass == 1;
		for (int i = 0; i < p_target.union_members.size(); i++) {
			BSParser::DataType target_member = p_target.union_members[i];
			target_member.is_nullable = p_target.is_nullable;
			const BSTypeCompatibility::Result member_result = BSTypeCompatibility::check(target_member, p_source, p_options);
			if (!member_result.compatible || member_result.uses_implicit_conversion != converting_pass) {
				continue;
			}
			if (converting_pass && !_union_store_converts_carrier(target_member, p_source, p_options)) {
				continue;
			}
			r_alternative = target_member;
			r_member_result = member_result;
			return true;
		}
	}
	return false;
}

} // namespace

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

bool BSTypeCompatibility::resolve_final_class_bound(const BSParser::DataType &p_type, BSParser::DataType &r_resolved) {
	// Foundry resolve_final_class_bound @ c9d5e35: `@Self` denotes the class a frame runs against
	// and has its own lowering everywhere; it is never resolved through a declared bound.
	if (p_type.type_parameter_name == SNAME("@Self") || !_is_type_parameter_bounded_by_final_class(p_type)) {
		return false;
	}
	BSParser::DataType resolved = p_type.type_parameter_bound[0];
	resolved.type_source = p_type.type_source;
	// Wrappers the parameter itself declared survive resolution: `T?` resolves to `Label?` and
	// `Type[T]` to a class handle for `Label`, so the resolved shape lowers exactly as the position
	// demanded. The bound's own wrappers survive as well, so the two are combined.
	resolved.is_nullable = resolved.is_nullable || p_type.is_nullable;
	resolved.is_meta_type = resolved.is_meta_type || p_type.is_meta_type;
	resolved.is_type_handle_annotation = resolved.is_type_handle_annotation || p_type.is_type_handle_annotation;
	resolved.is_coroutine = resolved.is_coroutine || p_type.is_coroutine;
	r_resolved = resolved;
	return true;
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

	// Foundry FSTypeCompatibility::check @ c9d5e35 (~1331): Coroutine[T] is its own family — a
	// coroutine target requires a coroutine source (and vice versa), and the phantom result type is
	// matched invariantly like a typed Array element. Generic NATIVE inheritance (BSFunctionState)
	// must not decide this, so the branch runs before the native compatibility logic below.
	if (p_target.is_coroutine || p_source.is_coroutine) {
		Result result(false, false, false);
		if (!p_target.is_coroutine || !p_source.is_coroutine) {
			return result;
		}
		result.compatible = true;
		if (p_target.has_container_element_type(0) && p_source.has_container_element_type(0)) {
			Options element_options = p_options;
			element_options.allow_implicit_conversion = false;
			element_options.constant_source_value = nullptr;
			const Result element_result = check(p_target.get_container_element_type(0), p_source.get_container_element_type(0), element_options);
			result.compatible = element_result.compatible;
			result.requires_runtime_check = element_result.requires_runtime_check;
			result.uses_implicit_conversion = element_result.uses_implicit_conversion;
		}
		return result;
	}

	// Foundry FSTypeCompatibility::check @ c9d5e35 (~1090): TYPE_PARAMETER / @Self assignability.
	// Must run before CLASS/NATIVE/SCRIPT so a TYPE_PARAMETER source into a concrete destination is
	// not rejected by the kind gate below. Free method/class type parameters remain M5-deferred in
	// Barista; the undecidable-target arms are ported so `@Self` keeps Foundry identity / exclusion
	// rules now, and free-`T` laundering refusal lights up once M5 specialization is live.
	if (p_target.kind == BSParser::DataType::TYPE_PARAMETER || p_source.kind == BSParser::DataType::TYPE_PARAMETER) {
		Result result(false, false, false);
		// Type parameters are erased to Variant at runtime, so the two directions are not symmetric.
		if (p_target.kind == BSParser::DataType::TYPE_PARAMETER && p_source.kind == BSParser::DataType::TYPE_PARAMETER) {
			// Two handles are statically compatible only when they denote the same parameter.
			// Nullability is deliberately excluded from this identity comparison so `T` widens to `T?`
			// the same way `Node` widens to `Node?`. The unsafe direction (`T?` into `T`) is already
			// rejected by the strict-null gate above, which runs before this branch.
			BSParser::DataType target_identity = p_target;
			BSParser::DataType source_identity = p_source;
			target_identity.is_nullable = false;
			source_identity.is_nullable = false;
			result.compatible = target_identity == source_identity;
		} else if (_is_undecidable_type_parameter_target(p_target, p_options) && _is_type_parameter_bounded_by_final_class(p_target)) {
			// The parameter denotes exactly its bound, so the assignment is decided against the bound
			// like any other concrete destination.
			return check(p_target.type_parameter_bound[0], p_source, p_options);
		} else if (_is_undecidable_type_parameter_target(p_target, p_options)) {
			// A type-parameter destination has nothing to test a value against and no check is emitted
			// for the assignment. Accepting a concrete value here would launder it into a `T` slot
			// untested, so only a value already known to be `T` satisfies one.
			// Residual (M5): free class/method type parameters are not yet analyzable in Barista;
			// once they are, this arm refuses undecidable free-`T` destinations under
			// `receiver_is_available = false` (static frame) / method-scope erasure.
			result.compatible = false;
		} else {
			// A type parameter as the source is the downcast shape: the destination is a concrete
			// type the runtime can still name, and the erased value carries what a type test needs.
			// A decidable TYPE_PARAMETER destination (`@Self`, or a class-scope param with a receiver)
			// takes the same arm — the store / Self contract layers decide further exactness.
			result.compatible = true;
			result.requires_runtime_check = true;
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

	// Foundry FSTypeCompatibility::check @ c9d5e35: a union *source* satisfies a target only when
	// every alternative does. The runtime carries no tag, so a target that accepts only some
	// alternatives would accept the wrong value at the boundary. Source-UNION is decided before
	// target-UNION so Number→Number / int|String→int|String self-assign recurse through the
	// target-UNION branch per alternative rather than falling through to invariant equality.
	if (p_source.kind == BSParser::DataType::UNION) {
		Result result(false, false, false);
		Options member_options = p_options;
		member_options.constant_source_value = nullptr;
		for (int i = 0; i < p_source.union_members.size(); i++) {
			// Nullability was hoisted onto the union during normalization; put it back on each
			// alternative before it faces the target's own null rules.
			BSParser::DataType source_member = p_source.union_members[i];
			source_member.is_nullable = p_source.is_nullable;
			const Result member_result = check(p_target, source_member, member_options);
			if (!member_result.compatible) {
				return result;
			}
			if (p_target.kind != BSParser::DataType::UNION && member_result.uses_implicit_conversion &&
					(source_member.kind != BSParser::DataType::BUILTIN ||
							p_target.builtin_type != source_member.builtin_type)) {
				// The value is one untyped slot at runtime, so a per-alternative carrier change
				// cannot be emitted for it. Width-only / same-carrier conversions are fine under
				// D1 (int/float are distinct carriers, so Number→float stays rejected).
				return result;
			}
			result.uses_implicit_conversion = result.uses_implicit_conversion || member_result.uses_implicit_conversion;
		}
		result.compatible = true;
		// A union erases to an untyped value, so reaching a typed slot always costs a runtime check.
		result.requires_runtime_check = true;
		return result;
	}

	if (p_target.kind == BSParser::DataType::UNION) {
		// Foundry target-UNION @ c9d5e35: accept a source that satisfies any one alternative, preferring
		// exact matches and admitting converting alternatives only when the union store can convert.
		BSParser::DataType alternative;
		Result member_result;
		Result result(false, false, false);
		if (!_select_union_alternative(p_target, p_source, p_options, alternative, member_result)) {
			return result;
		}
		result.compatible = true;
		result.uses_implicit_conversion = member_result.uses_implicit_conversion;
		// An alternative reached by conversion (or that already required a runtime check) keeps that
		// obligation: the compiled store tests/converts against the whole alternative set.
		result.requires_runtime_check = member_result.requires_runtime_check || member_result.uses_implicit_conversion;
		return result;
	}

	return Result(is_invariant_equal(p_target, p_source), false, false);
}

bool BSTypeCompatibility::selected_union_alternative(const BSParser::DataType &p_target, const BSParser::DataType &p_source,
		const Options &p_options, BSParser::DataType &r_alternative) {
	if (p_target.kind != BSParser::DataType::UNION) {
		return false;
	}
	Result member_result;
	return _select_union_alternative(p_target, p_source, p_options, r_alternative, member_result);
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
	// Foundry _datatype_invariant_equal @ c9d5e35: union members are canonically ordered, so
	// identity is positional member-for-member.
	if (p_a.union_members.size() != p_b.union_members.size()) {
		return false;
	}
	for (int i = 0; i < p_a.union_members.size(); i++) {
		if (!is_invariant_equal(p_a.union_members[i], p_b.union_members[i])) {
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
