/**************************************************************************/
/*  bs_type.h                                                             */
/*                                                                        */
/*  Hard fork of Foundry fs_type.h @ c9d5e35. D1 deletes NumericType /    */
/*  width promotion; int/float use Variant carriers only.                 */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_conformance_registry.h"
#include "bs_parser.h"
#include "bs_platform.h"

namespace barista_script {

/**
 * D1 numeric rules: a slot's Variant carrier is the whole numeric type. There is no width /
 * signedness promotion matrix. `promote_integer_pair` succeeds only for INT/INT (result INT).
 */
class BSNumericConversion {
public:
	enum class Conversion {
		IDENTITY,
		IMPLICIT_WIDEN,
		CONSTANT_CHECKED,
		EXPLICIT_REQUIRED,
		INVALID,
	};

	static bool is_numeric_builtin(const BSParser::DataType &p_type);
	static Conversion classify(const BSParser::DataType &p_target, const BSParser::DataType &p_source, const Variant *p_constant_source_value);
};

/**
 * Structural / gradual type compatibility used by the analyzer (Foundry FSTypeCompatibility).
 * Conversion sites consult Variant::can_convert_strict (String→StringName / NodePath, …) and
 * then apply D1 numeric classify so float→int stays explicit unless constant-proven.
 * Union sources require every alternative to satisfy the target (Number→Number / set-wise assign).
 * Target-UNION selection prefers exact alternatives, then numeric store-carrier conversions only.
 * TYPE_PARAMETER / `@Self` use Foundry identity / undecidable-target / erased-source arms.
 */
class BSTypeCompatibility {
public:
	struct Options {
		bool allow_implicit_conversion = false;
		bool strict_dynamic = false;
		bool strict_null = false;
		const Variant *constant_source_value = nullptr;
		bool receiver_is_available = true;
	};

	struct Result {
		bool compatible = false;
		bool requires_runtime_check = false;
		bool uses_implicit_conversion = false;

		Result() = default;
		Result(bool p_compatible, bool p_requires_runtime_check, bool p_uses_implicit_conversion) :
				compatible(p_compatible),
				requires_runtime_check(p_requires_runtime_check),
				uses_implicit_conversion(p_uses_implicit_conversion) {}
	};

	static Result check(const BSParser::DataType &p_target, const BSParser::DataType &p_source);
	static Result check(const BSParser::DataType &p_target, const BSParser::DataType &p_source, const Options &p_options);
	/**
	 * Foundry selected_union_alternative @ c9d5e35: the alternative a UNION target admits
	 * `p_source` through (exact preferred, then numeric store-carrier conversion only).
	 */
	static bool selected_union_alternative(const BSParser::DataType &p_target, const BSParser::DataType &p_source,
			const Options &p_options, BSParser::DataType &r_alternative);
	static bool is_compatible(const BSParser::DataType &p_target, const BSParser::DataType &p_source, bool p_allow_implicit_conversion = false);
	static bool is_invariant_equal(const BSParser::DataType &p_a, const BSParser::DataType &p_b);
	static bool allows_runtime_narrowing(const BSParser::DataType &p_narrow, const BSParser::DataType &p_wide);

	/**
	 * Foundry resolve_final_class_bound @ c9d5e35: a type parameter bounded only by a `final`
	 * class denotes exactly that class. `@Self` is never resolved through a declared bound.
	 */
	static bool resolve_final_class_bound(const BSParser::DataType &p_type, BSParser::DataType &r_resolved);

	/**
	 * Foundry final_class_bound_survives_lowering @ c9d5e35: a resolved final bound keeps evidence
	 * only where lowering still states nullability / class-handle wrappers.
	 */
	static bool final_class_bound_survives_lowering(const BSParser::DataType &p_resolved_bound, bool p_wrappers_are_expressible);

	/**
	 * Foundry destination_is_undecidable_type_parameter @ c9d5e35: erased method-scope parameters
	 * anywhere, or class-scope non-`@Self` parameters without a receiver. Free method/`T` generics
	 * remain M5 residual until those parameters are fully live; the walk is Foundry-faithful for
	 * TYPE_PARAMETER shapes Barista already models.
	 */
	static bool destination_is_undecidable_type_parameter(const BSParser::DataType &p_type, const Options &p_options);

	/** Foundry FSTypeCompatibility rest-tail helpers @ c9d5e35 (trait signature matching). */
	static bool rest_parameter_type_is_narrowing(const BSParser::DataType &p_rest_parameter_type);
	static bool rest_parameter_accepts_required_arguments(const BSParser::DataType *p_implementation_rest_array,
			const BSParser::DataType *p_required_rest_array, bool p_strict_null);
	static bool rest_parameter_accepts_required_argument(const BSParser::DataType *p_implementation_rest_array,
			const BSParser::DataType &p_required_argument_type, bool p_strict_null);

	/**
	 * Foundry recorded_arguments_conflict @ c9d5e35: true only when two flattened
	 * recorded argument vectors are both confidently identified and disagree.
	 * Differing arity is an absence of evidence, like an empty vector. D1: no
	 * NumericType width comparison — builtin carriers alone decide.
	 */
	static bool recorded_arguments_conflict(
			const Vector<BSConformanceRegistry::RecordedTypeArgument> &p_recorded,
			const Vector<BSConformanceRegistry::RecordedTypeArgument> &p_other);

	/**
	 * Foundry project_registry_trait_arguments @ c9d5e35: the trait arguments a
	 * retroactive conformance on `p_source`'s CLASS chain recorded for
	 * `p_trait_name`. Nearest conforming level wins (including empty nearer
	 * shadowing a farther record). False when no level recorded any.
	 *
	 * Residual (#60): BSTypeCompatibility::check does not yet consult this for
	 * trait-target assignability (Foundry fs_type.cpp ~1352–1447). Probe-tested
	 * until that call site lands.
	 */
	static bool project_registry_trait_arguments(const BSParser::DataType &p_source,
			const StringName &p_trait_name,
			Vector<BSConformanceRegistry::RecordedTypeArgument> &r_arguments);
};

} // namespace barista_script
