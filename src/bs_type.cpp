/**************************************************************************/
/*  bs_type.cpp                                                           */
/*                                                                        */
/*  Hard fork of Foundry fs_type.cpp @ c9d5e35 (D1-trimmed).              */
/*  Union sources require every alternative to satisfy the target.        */
/*  Target-UNION uses two-pass select + numeric store-carrier gate.       */
/*  Coroutine[T] assignability matches phantom results invariantly.       */
/*  TYPE_PARAMETER / @Self assignability arms (identity, undecidable     */
/*  target, erased-source runtime check). destination_is_undecidable     */
/*  type-parameter walk for gradual Self-union admission. Free-T         */
/*  undecidable laundering remains M5 residual until method/class type   */
/*  parameters are live. Trait-target assignability consults declared    */
/*  uses projection and registry recorded-arg conflict (#60). Runtime    */
/*  R13: runtime Function* store is M4; R04 enum completion is implemented.    */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_type.h"

#include "bs_trait_utils.h"

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

bool BSTypeCompatibility::final_class_bound_survives_lowering(const BSParser::DataType &p_resolved_bound, bool p_wrappers_are_expressible) {
	// Foundry final_class_bound_survives_lowering @ c9d5e35.
	if (p_wrappers_are_expressible) {
		return true;
	}
	return !p_resolved_bound.is_nullable && !(p_resolved_bound.is_meta_type && !p_resolved_bound.is_type_handle_annotation);
}

namespace {

// Mirrors Variant::MAX_RECURSION_DEPTH / BSParser::MAX_NESTING_DEPTH (1024). Kept local: the parser
// constant is private and godot-cpp's Variant does not expose the enumerator.
static constexpr int TYPE_WALK_MAX_DEPTH = 1024;

static bool _datatype_names_any_type_parameter(const BSParser::DataType &p_type, int p_depth = 0) {
	if (unlikely(p_depth > TYPE_WALK_MAX_DEPTH)) {
		return false;
	}
	if (p_type.kind == BSParser::DataType::TYPE_PARAMETER) {
		return true;
	}
	const Vector<BSParser::DataType> *slots[] = {
		&p_type.type_parameter_bound,
		&p_type.container_element_types,
		&p_type.type_arguments,
		&p_type.union_members,
		&p_type.method_parameter_types,
		&p_type.method_return_type,
		&p_type.method_rest_parameter_type,
	};
	for (const Vector<BSParser::DataType> *slot : slots) {
		for (const BSParser::DataType &nested : *slot) {
			if (_datatype_names_any_type_parameter(nested, p_depth + 1)) {
				return true;
			}
		}
	}
	return false;
}

// A tagged union's case payloads are never read by this comparison, so a type parameter inside one is
// not evidence. Unlike a union member it is not part of the node's identity either, so it only keeps
// the node from reporting a full match -- the enum's own name and its type arguments are still
// compared, which is what lets `Result[int, U]` contradict `Result[String, float]`.
static bool _evidence_enum_payloads_are_open(const BSParser::DataType &p_type) {
	for (const KeyValue<StringName, BSParser::DataType::EnumCasePayload> &payload : p_type.enum_case_payloads) {
		for (const BSParser::DataType &field_type : payload.value.field_types) {
			if (_datatype_names_any_type_parameter(field_type)) {
				return true;
			}
		}
	}
	return false;
}

static bool _slot_names_any_type_parameter(const Vector<BSParser::DataType> &p_slot) {
	for (const BSParser::DataType &type : p_slot) {
		if (_datatype_names_any_type_parameter(type)) {
			return true;
		}
	}
	return false;
}

static BSTypeCompatibility::ArgumentEvidence _combine_evidence(BSTypeCompatibility::ArgumentEvidence p_current,
		BSTypeCompatibility::ArgumentEvidence p_next) {
	if (p_current == BSTypeCompatibility::ArgumentEvidence::CONFLICT ||
			p_next == BSTypeCompatibility::ArgumentEvidence::CONFLICT) {
		return BSTypeCompatibility::ArgumentEvidence::CONFLICT;
	}
	if (p_current == BSTypeCompatibility::ArgumentEvidence::UNKNOWN ||
			p_next == BSTypeCompatibility::ArgumentEvidence::UNKNOWN) {
		return BSTypeCompatibility::ArgumentEvidence::UNKNOWN;
	}
	return BSTypeCompatibility::ArgumentEvidence::MATCH;
}

static BSTypeCompatibility::ArgumentEvidence _compare_datatype_evidence(const BSParser::DataType &p_a,
		const BSParser::DataType &p_b, int p_depth);

// Union members are canonically ordered by `make_union()`, so identity is positional -- but only once
// every member is reified. A member left on an unreified type parameter sorts by its parameter's
// spelling, and substituting it can move it anywhere in the vector, so pairing by index would invent
// a contradiction between members that substitution could still reconcile: `Array[U] | Array[int]`
// sorts independently of its eventual concrete substitution. An open projected member requires
// compared as sets instead: only a member that contradicts every member on the other side rules out
// all pairings, and anything short of that leaves the node open rather than rejected.
static BSTypeCompatibility::ArgumentEvidence _compare_union_members_evidence(
		const Vector<BSParser::DataType> &p_a_members, const Vector<BSParser::DataType> &p_b_members,
		int p_depth) {
	using ArgumentEvidence = BSTypeCompatibility::ArgumentEvidence;
	const bool a_is_open = _slot_names_any_type_parameter(p_a_members);

	if (p_a_members.size() != p_b_members.size()) {
		// An open member can be substituted with a sibling's type and dedup away, so the member counts
		// only contradict each other when the projected vector is closed.
		return a_is_open ? ArgumentEvidence::UNKNOWN : ArgumentEvidence::CONFLICT;
	}

	if (!a_is_open) {
		ArgumentEvidence evidence = ArgumentEvidence::MATCH;
		for (int i = 0; i < p_a_members.size(); i++) {
			evidence = _combine_evidence(evidence,
					_compare_datatype_evidence(p_a_members[i], p_b_members[i], p_depth + 1));
			if (evidence == ArgumentEvidence::CONFLICT) {
				return evidence;
			}
		}
		return evidence;
	}

	for (int i = 0; i < p_a_members.size(); i++) {
		bool conflicts_with_every_member = true;
		for (int j = 0; j < p_b_members.size() && conflicts_with_every_member; j++) {
			conflicts_with_every_member =
					_compare_datatype_evidence(p_a_members[i], p_b_members[j], p_depth + 1) ==
					ArgumentEvidence::CONFLICT;
		}
		if (conflicts_with_every_member) {
			return ArgumentEvidence::CONFLICT;
		}
	}
	for (int j = 0; j < p_b_members.size(); j++) {
		bool conflicts_with_every_member = true;
		for (int i = 0; i < p_a_members.size() && conflicts_with_every_member; i++) {
			conflicts_with_every_member =
					_compare_datatype_evidence(p_a_members[i], p_b_members[j], p_depth + 1) ==
					ArgumentEvidence::CONFLICT;
		}
		if (conflicts_with_every_member) {
			return ArgumentEvidence::CONFLICT;
		}
	}
	return ArgumentEvidence::UNKNOWN;
}

// Foundry _compare_datatype_evidence @ c9d5e35: projected parameters are open, while
// destination arguments are authored literals. A missing subtree cannot erase a known sibling
// contradiction. D1 compares builtin carriers; M5 symmetric open comparison remains deferred.
static BSTypeCompatibility::ArgumentEvidence _compare_datatype_evidence(const BSParser::DataType &p_a,
		const BSParser::DataType &p_b, int p_depth) {
	using ArgumentEvidence = BSTypeCompatibility::ArgumentEvidence;
	if (unlikely(p_depth > TYPE_WALK_MAX_DEPTH)) {
		return ArgumentEvidence::UNKNOWN;
	}
	if (!p_a.is_set() || !p_b.is_set()) {
		return ArgumentEvidence::UNKNOWN;
	}

	const bool a_is_parameter = p_a.kind == BSParser::DataType::TYPE_PARAMETER;
	// A bounded parameter still admits every subtype of its bound, so the bound is not evidence about
	// the type actually reified there and the whole subtree stays open.
	if (a_is_parameter) {
		return ArgumentEvidence::UNKNOWN;
	}

	if (p_a.kind != p_b.kind ||
			p_a.is_nullable != p_b.is_nullable ||
			p_a.is_meta_type != p_b.is_meta_type ||
			p_a.is_type_handle_annotation != p_b.is_type_handle_annotation ||
			p_a.has_method_signature != p_b.has_method_signature ||
			p_a.signature_is_async != p_b.signature_is_async ||
			(p_a.method_info.flags & METHOD_FLAG_VARARG) != (p_b.method_info.flags & METHOD_FLAG_VARARG)) {
		return ArgumentEvidence::CONFLICT;
	}

	bool identical = false;
	switch (p_a.kind) {
		case BSParser::DataType::VARIANT:
			identical = true;
			break;
		case BSParser::DataType::BUILTIN:
			identical = p_a.builtin_type == p_b.builtin_type;
			break;
		case BSParser::DataType::NATIVE:
		case BSParser::DataType::ENUM:
			identical = p_a.native_type == p_b.native_type;
			break;
		case BSParser::DataType::SCRIPT:
			identical = p_a.script_type == p_b.script_type;
			break;
		case BSParser::DataType::CLASS:
			identical = p_a.class_type == p_b.class_type ||
					(p_a.class_type != nullptr && p_b.class_type != nullptr &&
							p_a.class_type->fqcn == p_b.class_type->fqcn);
			break;
		case BSParser::DataType::TYPE_PARAMETER:
			identical = p_a.type_parameter_name == p_b.type_parameter_name &&
					p_a.type_parameter_scope == p_b.type_parameter_scope &&
					p_a.type_parameter_index == p_b.type_parameter_index;
			break;
		case BSParser::DataType::TUPLE:
			identical = p_a.native_type == p_b.native_type && p_a.script_path == p_b.script_path;
			break;
		case BSParser::DataType::UNION:
			// Members are traversed by `_compare_union_members_evidence()` below rather than compared in
			// one step, so a member left on an unreified parameter is open on its own, like a type
			// parameter anywhere else, and a concrete contradiction in a sibling member still decides
			// the node instead of being erased along with it.
			identical = true;
			break;
		case BSParser::DataType::RESOLVING:
		case BSParser::DataType::UNRESOLVED:
			break;
	}
	if (!identical) {
		return ArgumentEvidence::CONFLICT;
	}

	ArgumentEvidence evidence = ArgumentEvidence::MATCH;
	if (_evidence_enum_payloads_are_open(p_a)) {
		evidence = ArgumentEvidence::UNKNOWN;
	}
	if (p_a.kind == BSParser::DataType::UNION) {
		evidence = _combine_evidence(evidence,
				_compare_union_members_evidence(p_a.union_members, p_b.union_members, p_depth));
		if (evidence == ArgumentEvidence::CONFLICT) {
			return evidence;
		}
	}
	const Vector<BSParser::DataType> *a_slots[] = {
		&p_a.type_parameter_bound,
		&p_a.container_element_types,
		&p_a.type_arguments,
		&p_a.method_parameter_types,
		&p_a.method_return_type,
		&p_a.method_rest_parameter_type,
	};
	const Vector<BSParser::DataType> *b_slots[] = {
		&p_b.type_parameter_bound,
		&p_b.container_element_types,
		&p_b.type_arguments,
		&p_b.method_parameter_types,
		&p_b.method_return_type,
		&p_b.method_rest_parameter_type,
	};
	for (int slot = 0; slot < 6; slot++) {
		if (a_slots[slot]->size() != b_slots[slot]->size()) {
			// A side that declares no components in this slot says nothing about components the other
			// side leaves on an unreified parameter: the two then differ only where neither carries
			// evidence, and rejecting there would be stricter than the erasure this replaced. Two sides
			// that both state their components concretely still contradict each other by arity.
			if (_slot_names_any_type_parameter(*a_slots[slot])) {
				evidence = ArgumentEvidence::UNKNOWN;
				continue;
			}
			return ArgumentEvidence::CONFLICT;
		}
		for (int i = 0; i < a_slots[slot]->size(); i++) {
			evidence = _combine_evidence(evidence,
					_compare_datatype_evidence((*a_slots[slot])[i], (*b_slots[slot])[i], p_depth + 1));
			if (evidence == ArgumentEvidence::CONFLICT) {
				return evidence;
			}
		}
	}
	return evidence;
}

// Foundry _destination_has_erased_type_parameter @ c9d5e35: a method-scope parameter is chosen per
// call and erased before the callee runs, so no frame has anything to check against. Nested under
// unions / signatures the bound is never checked either. Free method-`T` cases stay M5 residual
// until method generics are fully live; the walk matches Foundry for shapes Barista models.
bool _destination_has_erased_type_parameter(const BSParser::DataType &p_type, int p_depth = 0,
		bool p_final_bound_is_representable = true, bool p_wrappers_are_expressible = true) {
	if (unlikely(p_depth > TYPE_WALK_MAX_DEPTH)) {
		return false;
	}

	const bool representable = p_final_bound_is_representable && !(p_type.is_nullable && !p_wrappers_are_expressible);

	if (p_type.kind == BSParser::DataType::TYPE_PARAMETER) {
		if (!_is_erased_type_parameter(p_type)) {
			return false;
		}
		BSParser::DataType resolved;
		if (!BSTypeCompatibility::resolve_final_class_bound(p_type, resolved)) {
			return true;
		}
		if (!BSTypeCompatibility::final_class_bound_survives_lowering(resolved, p_wrappers_are_expressible)) {
			return true;
		}
		return !representable;
	}

	if (p_type.kind == BSParser::DataType::TUPLE) {
		for (const BSParser::DataType &element : p_type.container_element_types) {
			if (element.kind != BSParser::DataType::TYPE_PARAMETER && element.kind != BSParser::DataType::TUPLE) {
				continue;
			}
			if (_destination_has_erased_type_parameter(element, p_depth + 1, representable, p_wrappers_are_expressible)) {
				return true;
			}
		}
		return false;
	}

	const Vector<BSParser::DataType> *reified_slots[] = {
		&p_type.container_element_types,
		&p_type.type_arguments,
	};
	for (const Vector<BSParser::DataType> *slots : reified_slots) {
		for (const BSParser::DataType &slot : *slots) {
			const bool slot_representable = representable && slot.kind != BSParser::DataType::TUPLE;
			if (_destination_has_erased_type_parameter(slot, p_depth + 1, slot_representable, false)) {
				return true;
			}
		}
	}

	const Vector<BSParser::DataType> *erasing_slots[] = {
		&p_type.method_parameter_types,
		&p_type.method_return_type,
		&p_type.method_rest_parameter_type,
		&p_type.union_members,
	};
	for (const Vector<BSParser::DataType> *slots : erasing_slots) {
		for (const BSParser::DataType &slot : *slots) {
			if (_destination_has_erased_type_parameter(slot, p_depth + 1, false, false)) {
				return true;
			}
		}
	}
	return false;
}

// Foundry _depends_on_receiver_type_parameter @ c9d5e35: class-scope non-`@Self` parameters in
// positions a store would validate against a receiver. Callable/signature/union slots stay out —
// erasure walks them separately.
bool _depends_on_receiver_type_parameter(
		const BSParser::DataType &p_type, bool p_nullable_is_expressible, int p_depth, bool p_exempt_final_bound = false) {
	if (unlikely(p_depth > TYPE_WALK_MAX_DEPTH)) {
		return false;
	}
	if (p_depth == 0 && p_type.is_meta_type && !p_type.is_type_handle_annotation) {
		return false;
	}
	if (p_type.is_nullable && !p_nullable_is_expressible) {
		return false;
	}
	if (p_type.kind == BSParser::DataType::TYPE_PARAMETER) {
		const bool wrappers_are_expressible = p_depth == 0 || p_nullable_is_expressible;
		BSParser::DataType resolved_bound;
		if (p_exempt_final_bound && BSTypeCompatibility::resolve_final_class_bound(p_type, resolved_bound) &&
				BSTypeCompatibility::final_class_bound_survives_lowering(resolved_bound, wrappers_are_expressible)) {
			return false;
		}
		return p_type.type_parameter_scope == BSParser::DataType::TYPE_PARAMETER_CLASS &&
				p_type.type_parameter_name != SNAME("@Self");
	}
	const bool child_expressible = p_nullable_is_expressible && p_type.kind == BSParser::DataType::TUPLE;
	const bool crossing_into_container = p_type.kind != BSParser::DataType::TUPLE;
	for (const BSParser::DataType &element_type : p_type.container_element_types) {
		const bool child_exempt = p_exempt_final_bound &&
				!(crossing_into_container && element_type.kind == BSParser::DataType::TUPLE);
		if (_depends_on_receiver_type_parameter(element_type, child_expressible, p_depth + 1, child_exempt)) {
			return true;
		}
	}
	for (const BSParser::DataType &type_argument : p_type.type_arguments) {
		const bool child_exempt = p_exempt_final_bound && type_argument.kind != BSParser::DataType::TUPLE;
		if (_depends_on_receiver_type_parameter(type_argument, false, p_depth + 1, child_exempt)) {
			return true;
		}
	}
	return false;
}

bool _nullable_is_expressible_at_root(const BSParser::DataType &p_type) {
	return p_type.kind == BSParser::DataType::TUPLE;
}

} // namespace

BSTypeCompatibility::ArgumentEvidence BSTypeCompatibility::compare_projected_argument(
		const BSParser::DataType &p_projected, const BSParser::DataType &p_expected) {
	return _compare_datatype_evidence(p_projected, p_expected, 0);
}

bool BSTypeCompatibility::destination_is_undecidable_type_parameter(const BSParser::DataType &p_type, const Options &p_options) {
	// Foundry destination_is_undecidable_type_parameter @ c9d5e35.
	if (_destination_has_erased_type_parameter(p_type)) {
		return true;
	}
	return !p_options.receiver_is_available &&
			_depends_on_receiver_type_parameter(p_type, _nullable_is_expressible_at_root(p_type), 0, true);
}

namespace {

// Foundry _path_identifies_script @ c9d5e35: a resource path identifies the one class that owns
// the file. Foundry checks `is_root_script()` so an inner class does not inherit a sibling's
// path-keyed conformance. BaristaScript has no `is_root_script` flag yet — every compiled
// `.barista` Script resource is a file owner — so path always identifies. R14: M4 owns
// runtime inner-class Script wrappers; source classes retain parser-owned CLASS identity (#140).
bool _path_identifies_script(const Ref<Script> &p_script) {
	(void)p_script;
	return true;
}

// Foundry _class_has_trait @ c9d5e35: resolved_traits walk + registry membership on the chain.
bool _class_has_trait(const BSParser::ClassNode *p_class, const BSParser::ClassNode *p_trait) {
	if (p_class == nullptr || p_trait == nullptr) {
		return false;
	}

	if (p_class == p_trait || p_class->fqcn == p_trait->fqcn) {
		return true;
	}

	const StringName trait_name = bs_trait_identity_name(p_trait);
	const BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
	const BSParser::ClassNode *current = p_class;
	int depth = 0;
	while (current != nullptr) {
		if (unlikely(depth++ > TYPE_WALK_MAX_DEPTH)) {
			return false;
		}
		for (int i = 0; i < current->resolved_traits.size(); i++) {
			const BSParser::ClassNode *trait = current->resolved_traits[i];
			if (trait == p_trait || (trait != nullptr && trait->fqcn == p_trait->fqcn)) {
				return true;
			}
		}

		if (registry != nullptr &&
				(registry->has_conformance(current->fqcn, trait_name) ||
						registry->has_conformance(current->get_global_name(), trait_name))) {
			return true;
		}

		if (current->base_type.kind == BSParser::DataType::CLASS) {
			current = current->base_type.class_type;
		} else if (current->base_type.kind == BSParser::DataType::SCRIPT && current->base_type.script_type.is_valid()) {
			if (registry == nullptr) {
				return false;
			}
			// R14: runtime SCRIPT membership uses registry/native fallback; M4 owns compiled traits.
			return (_path_identifies_script(current->base_type.script_type) &&
						   registry->has_conformance(current->base_type.script_path, trait_name)) ||
					registry->has_conformance(current->base_type.script_type->get_global_name(), trait_name) ||
					registry->native_class_conforms(current->base_type.script_type->get_instance_base_type(), trait_name);
		} else if (current->base_type.kind == BSParser::DataType::NATIVE) {
			return registry != nullptr && registry->native_class_conforms(current->base_type.native_type, trait_name);
		} else {
			break;
		}
	}

	return false;
}

// Foundry _project_class_trait_arguments @ c9d5e35: declared `uses` projection via
// bs_trait_type_argument_bindings / bs_reify_self_in_trait_argument.
bool _project_class_trait_arguments(const BSParser::DataType &p_source,
		const BSParser::ClassNode *p_trait, Vector<BSParser::DataType> &r_arguments) {
	if (p_trait == nullptr || p_trait->type_parameters.is_empty()) {
		return false;
	}

	BSParser::DataType current = p_source;
	int depth = 0;
	while (current.kind == BSParser::DataType::CLASS && current.class_type != nullptr) {
		if (unlikely(depth++ > TYPE_WALK_MAX_DEPTH)) {
			return false;
		}

		if (current.class_type == p_trait || current.class_type->fqcn == p_trait->fqcn) {
			if (current.type_arguments.size() != p_trait->type_parameters.size()) {
				return false;
			}
			r_arguments = current.type_arguments;
			return true;
		}

		HashMap<StringName, BSParser::DataType> class_bindings;
		const Vector<BSParser::TypeParameterNode *> &type_parameters = current.class_type->type_parameters;
		const int binding_count = MIN(type_parameters.size(), current.type_arguments.size());
		for (int i = 0; i < binding_count; i++) {
			const BSParser::TypeParameterNode *type_parameter = type_parameters[i];
			if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
				class_bindings.insert(type_parameter->identifier->name, current.type_arguments[i]);
			}
		}

		const HashMap<StringName, BSParser::DataType> substitution =
				bs_trait_type_argument_bindings(current.class_type, p_trait);
		if (!substitution.is_empty()) {
			r_arguments.clear();
			for (int i = 0; i < p_trait->type_parameters.size(); i++) {
				const BSParser::TypeParameterNode *type_parameter = p_trait->type_parameters[i];
				BSParser::DataType argument;
				if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
					const BSParser::DataType *bound = substitution.getptr(type_parameter->identifier->name);
					if (bound != nullptr) {
						argument = class_bindings.is_empty()
								? *bound
								: BSParser::DataType::substitute(*bound, class_bindings);
					}
				}
				r_arguments.push_back(bs_reify_self_in_trait_argument(current.class_type, argument));
			}
			return true;
		}

		BSParser::DataType parent = current.class_type->base_type;
		if (!class_bindings.is_empty()) {
			parent = BSParser::DataType::substitute(parent, class_bindings);
		}
		current = parent;
	}

	return false;
}

bool _recorded_script_identities_intersect(const BSConformanceRegistry::RecordedTypeArgument &p_left,
		const BSConformanceRegistry::RecordedTypeArgument &p_right) {
	const String left_identities[] = { p_left.script_fqcn, p_left.script_global_name };
	const String right_identities[] = { p_right.script_fqcn, p_right.script_global_name };
	for (const String &left : left_identities) {
		if (left.is_empty()) {
			continue;
		}
		for (const String &right : right_identities) {
			if (!right.is_empty() && left == right) {
				return true;
			}
		}
	}
	return false;
}

bool _recorded_arguments_disagree(const BSConformanceRegistry::RecordedTypeArgument &p_left,
		const BSConformanceRegistry::RecordedTypeArgument &p_right, int p_depth = 0) {
	using RecordedTypeArgument = BSConformanceRegistry::RecordedTypeArgument;
	if (unlikely(p_depth > TYPE_WALK_MAX_DEPTH)) {
		return false;
	}
	if (p_left.kind == RecordedTypeArgument::UNKNOWN || p_right.kind == RecordedTypeArgument::UNKNOWN) {
		return false;
	}
	if (p_left.kind != p_right.kind || p_left.is_nullable != p_right.is_nullable) {
		return true;
	}
	switch (p_left.kind) {
		case RecordedTypeArgument::BUILTIN:
			// D1: carrier alone; no NumericType width comparison.
			if (p_left.builtin_type != p_right.builtin_type) {
				return true;
			}
			break;
		case RecordedTypeArgument::NATIVE_CLASS:
			if (p_left.native_class != p_right.native_class) {
				return true;
			}
			break;
		case RecordedTypeArgument::SCRIPT_CLASS:
			if (!_recorded_script_identities_intersect(p_left, p_right)) {
				return true;
			}
			break;
		case RecordedTypeArgument::UNKNOWN:
			break;
	}

	if (p_left.type_arguments.size() == p_right.type_arguments.size()) {
		for (int i = 0; i < p_left.type_arguments.size(); i++) {
			if (_recorded_arguments_disagree(p_left.type_arguments[i], p_right.type_arguments[i], p_depth + 1)) {
				return true;
			}
		}
	}
	if (p_left.container_element_types.size() == p_right.container_element_types.size()) {
		for (int i = 0; i < p_left.container_element_types.size(); i++) {
			if (_recorded_arguments_disagree(p_left.container_element_types[i], p_right.container_element_types[i],
						p_depth + 1)) {
				return true;
			}
		}
	}
	return false;
}

bool _recorded_arguments_conflict(const Vector<BSConformanceRegistry::RecordedTypeArgument> &p_recorded,
		const Vector<BSParser::DataType> &p_expected) {
	if (p_recorded.size() != p_expected.size()) {
		return false;
	}
	for (int i = 0; i < p_recorded.size(); i++) {
		if (_recorded_arguments_disagree(p_recorded[i], BSConformanceRegistry::reduce_type_argument(p_expected[i]))) {
			return true;
		}
	}
	return false;
}

// Foundry _project_registry_trait_arguments @ c9d5e35.
bool _project_registry_trait_arguments(const BSParser::DataType &p_source,
		const StringName &p_trait_name, Vector<BSConformanceRegistry::RecordedTypeArgument> &r_arguments) {
	r_arguments.clear();
	if (p_trait_name == StringName()) {
		return false;
	}

	const BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
	if (registry == nullptr) {
		return false;
	}

	const BSParser::ClassNode *current = p_source.class_type;
	int depth = 0;
	while (current != nullptr) {
		if (unlikely(depth++ > TYPE_WALK_MAX_DEPTH)) {
			return false;
		}

		if (registry->get_recorded_trait_arguments(current->fqcn, p_trait_name, r_arguments) ||
				registry->get_recorded_trait_arguments(String(current->get_global_name()), p_trait_name, r_arguments)) {
			return true;
		}
		if (registry->has_conformance(current->fqcn, p_trait_name) ||
				registry->has_conformance(current->get_global_name(), p_trait_name)) {
			// This level conforms but recorded nothing, and a nearer conformance shadows any further
			// one, so the chain proves nothing rather than answering from a more distant record.
			return false;
		}

		if (current->base_type.kind == BSParser::DataType::CLASS) {
			current = current->base_type.class_type;
		} else if (current->base_type.kind == BSParser::DataType::SCRIPT && current->base_type.script_type.is_valid()) {
			return (_path_identifies_script(current->base_type.script_type) &&
						   registry->get_recorded_trait_arguments(current->base_type.script_path, p_trait_name, r_arguments)) ||
					registry->get_recorded_trait_arguments(
							String(current->base_type.script_type->get_global_name()), p_trait_name, r_arguments) ||
					registry->get_native_recorded_trait_arguments(
							current->base_type.script_type->get_instance_base_type(), p_trait_name, r_arguments);
		} else if (current->base_type.kind == BSParser::DataType::NATIVE) {
			return registry->get_native_recorded_trait_arguments(current->base_type.native_type, p_trait_name, r_arguments);
		} else {
			break;
		}
	}

	return false;
}

static bool _is_signature_builtin_type(Variant::Type p_type) {
	return p_type == Variant::CALLABLE || p_type == Variant::SIGNAL;
}

static bool _property_signature_equal(const PropertyInfo &p_left, const PropertyInfo &p_right) {
	// Only the type-identifying fields matter for signature compatibility. Storage/editor usage flags do
	// not: a synthesized slot (DataType::to_property_info, usage NONE) must still match an equivalent
	// natural MethodInfo slot (e.g. a utility function like `sin`, usage DEFAULT). The one usage bit that
	// is type-relevant is NIL_IS_VARIANT, which distinguishes a `Variant` slot from a concrete/`void` NIL.
	return p_left.type == p_right.type &&
			p_left.class_name == p_right.class_name &&
			p_left.hint == p_right.hint &&
			p_left.hint_string == p_right.hint_string &&
			(p_left.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == (p_right.usage & PROPERTY_USAGE_NIL_IS_VARIANT);
}

static bool _method_signature_equal(const MethodInfo &p_left, const MethodInfo &p_right) {
	if (!_property_signature_equal(p_left.return_val, p_right.return_val)) {
		return false;
	}
	if (p_left.arguments.size() != p_right.arguments.size()) {
		return false;
	}
	for (int i = 0; i < p_left.arguments.size(); i++) {
		if (!_property_signature_equal(p_left.arguments[i], p_right.arguments[i])) {
			return false;
		}
	}
	return true;
}

static bool _datatype_method_signature_equal(const BSParser::DataType &p_left, const BSParser::DataType &p_right,
		bool p_contravariant_rest = false, bool p_strict_null = false, bool p_allow_runtime_narrowing = true);

// Strict signature-slot comparison for the explicit `Callable[[...], ...]` / `Signal[[...]]` path.
// `operator==` establishes matching outer structure (including is_nullable, generic type arguments, and
// container element kinds) and then the composite slots it only compared shallowly are recursed into so a
// nested Callable/Signal mismatch is still detected. This is the long-standing explicit-signature
// behavior and is intentionally left untouched: only the non-explicit source path is broadened below.
static bool _datatype_signature_slot_equal(const BSParser::DataType &p_left, const BSParser::DataType &p_right) {
	if (p_left != p_right) {
		return false;
	}
	// `operator==` above already established matching outer structure; recurse into the composite
	// slots it only compared shallowly. The size guards keep the lenient outcome it returns for
	// UNDETECTED/INFERRED operands (where the slot vectors may legitimately differ in length).
	if (p_left.container_element_types.size() == p_right.container_element_types.size()) {
		for (int i = 0; i < p_left.container_element_types.size(); i++) {
			if (!_datatype_signature_slot_equal(p_left.container_element_types[i], p_right.container_element_types[i])) {
				return false;
			}
		}
	}
	if (p_left.type_arguments.size() == p_right.type_arguments.size()) {
		for (int i = 0; i < p_left.type_arguments.size(); i++) {
			if (!_datatype_signature_slot_equal(p_left.type_arguments[i], p_right.type_arguments[i])) {
				return false;
			}
		}
	}
	if (p_left.kind == BSParser::DataType::BUILTIN && _is_signature_builtin_type(p_left.builtin_type) &&
			p_left.has_method_signature && p_right.has_method_signature) {
		return _datatype_method_signature_equal(p_left, p_right);
	}
	return true;
}

// Lenient signature-slot comparison for the non-explicit source path (a lambda, function reference, or
// declared signal where at least one side has no explicit annotation). The historical `MethodInfo`
// fallback compared such slots through `DataType::to_property_info`; reusing that serialization keeps
// every slot byte-for-byte identical to the prior behavior, so type parameters, non-hard
// (inferred/undetected) types, generic type arguments, nullability, and deep container nesting all
// collapse exactly as they did before and nothing the old path accepted is newly tightened. The one
// thing the property form cannot express is a Callable/Signal's nested method signature, so recurse to
// recover exactly that — and because the recursion re-enters `_datatype_method_signature_equal`, a nested
// slot that is itself an explicit Callable/Signal is dispatched to the strict comparison above.
static bool _nonexplicit_signature_slot_equal(const BSParser::DataType &p_left, const BSParser::DataType &p_right) {
	if (!_property_signature_equal(p_left.to_property_info(""), p_right.to_property_info(""))) {
		return false;
	}
	if (p_left.kind == BSParser::DataType::BUILTIN && _is_signature_builtin_type(p_left.builtin_type) &&
			p_left.has_method_signature && p_right.has_method_signature) {
		return _datatype_method_signature_equal(p_left, p_right);
	}
	return true;
}

// Whether a Callable/Signal type carries the rich `method_parameter_types`/`method_return_type`
// recursion data rather than only a `MethodInfo`. Explicit `Callable[[...], ...]` annotations set
// `has_explicit_method_signature`, but lambdas and function references populate the rich vectors from
// their FunctionNode without that flag (see `make_callable_type(MethodInfo, FunctionNode)` in the
// analyzer). A native, MethodInfo-only callable records neither parameter nor return DataTypes, so it
// is distinguished by both rich vectors being empty. A Callable always records its return type (even
// `void`), and a zero-argument callable legitimately has an empty parameter vector, so the presence of
// either rich vector signals that the structural recursion is available.
static bool _has_rich_method_signature(const BSParser::DataType &p_type) {
	if (!p_type.has_method_signature) {
		return false;
	}
	return p_type.has_explicit_method_signature ||
			!p_type.method_parameter_types.is_empty() ||
			!p_type.method_return_type.is_empty() ||
			!p_type.method_rest_parameter_type.is_empty();
}

// Compares the rest tails of two Callable/Signal signatures. In an assignment position `p_left` is the
// target (what callers are promised) and `p_right` the assigned source, so the shared contravariant rest
// rule applies. In an invariant position (a nested container element, for instance) neither side is a
// target and the slots must match exactly.
static bool _callable_signature_rest_parameter_type(const BSParser::DataType &p_type, BSParser::DataType &r_rest) {
	if (!(p_type.method_info.flags & METHOD_FLAG_VARARG))
		return false;
	if (p_type.has_method_rest_parameter_type())
		r_rest = p_type.get_method_rest_parameter_type();
	else {
		r_rest.kind = BSParser::DataType::BUILTIN;
		r_rest.builtin_type = Variant::ARRAY;
		r_rest.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	}
	return true;
}

static bool _method_signature_rest_slots_equal(const BSParser::DataType &p_left, const BSParser::DataType &p_right,
		bool (*p_slot_equal)(const BSParser::DataType &, const BSParser::DataType &), bool p_contravariant_rest,
		bool p_strict_null, bool p_allow_runtime_narrowing) {
	if (p_contravariant_rest) {
		BSParser::DataType target_rest;
		BSParser::DataType source_rest;
		const bool target_is_variadic = _callable_signature_rest_parameter_type(p_left, target_rest);
		const bool source_is_variadic = _callable_signature_rest_parameter_type(p_right, source_rest);
		return BSTypeCompatibility::rest_parameter_accepts_required_arguments(
				source_is_variadic ? &source_rest : nullptr,
				target_is_variadic ? &target_rest : nullptr, p_strict_null, p_allow_runtime_narrowing);
	}
	if (p_left.method_rest_parameter_type.size() != p_right.method_rest_parameter_type.size()) {
		return false;
	}
	for (int i = 0; i < p_left.method_rest_parameter_type.size(); i++) {
		if (!p_slot_equal(p_left.method_rest_parameter_type[i], p_right.method_rest_parameter_type[i])) {
			return false;
		}
	}
	return true;
}

static bool _method_signature_slots_equal(const BSParser::DataType &p_left, const BSParser::DataType &p_right,
		bool (*p_slot_equal)(const BSParser::DataType &, const BSParser::DataType &), bool p_contravariant_rest,
		bool p_strict_null, bool p_allow_runtime_narrowing) {
	if (p_left.method_parameter_types.size() != p_right.method_parameter_types.size()) {
		return false;
	}
	for (int i = 0; i < p_left.method_parameter_types.size(); i++) {
		if (!p_slot_equal(p_left.method_parameter_types[i], p_right.method_parameter_types[i])) {
			return false;
		}
	}
	if (!_method_signature_rest_slots_equal(p_left, p_right, p_slot_equal, p_contravariant_rest, p_strict_null, p_allow_runtime_narrowing)) {
		return false;
	}
	if (p_left.builtin_type == Variant::CALLABLE) {
		if (p_left.method_return_type.size() != p_right.method_return_type.size()) {
			return false;
		}
		for (int i = 0; i < p_left.method_return_type.size(); i++) {
			if (!p_slot_equal(p_left.method_return_type[i], p_right.method_return_type[i])) {
				return false;
			}
		}
	}
	return true;
}

static bool _datatype_method_signature_equal(const BSParser::DataType &p_left, const BSParser::DataType &p_right,
		bool p_contravariant_rest, bool p_strict_null, bool p_allow_runtime_narrowing) {
	// AsyncCallable and plain Callable are not interchangeable: a callable whose signature is async
	// carries a coroutine result that a synchronous Callable does not, so their signatures differ.
	if (p_left.signature_is_async != p_right.signature_is_async) {
		return false;
	}
	// Both sides written as explicit annotations: compare the rich slots strictly, exactly as the
	// explicit Callable/Signal path always has.
	if (p_left.has_explicit_method_signature && p_right.has_explicit_method_signature) {
		return _method_signature_slots_equal(p_left, p_right, _datatype_signature_slot_equal, p_contravariant_rest, p_strict_null, p_allow_runtime_narrowing);
	}
	// A lambda/function-reference Callable or a declared Signal carries rich slots without the explicit
	// flag. Compare those slots the way the `MethodInfo` fallback did, but recurse to catch the nested
	// Callable/Signal mismatches the fallback erased — the #382 fix for the non-explicit source path.
	if (_has_rich_method_signature(p_left) && _has_rich_method_signature(p_right)) {
		return _method_signature_slots_equal(p_left, p_right, _nonexplicit_signature_slot_equal, p_contravariant_rest, p_strict_null, p_allow_runtime_narrowing);
	}
	return _method_signature_equal(p_left.method_info, p_right.method_info);
}

} // namespace

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

	// Foundry FSTypeCompatibility::check @ c9d5e35: null satisfies every nullable
	// destination before its underlying builtin, enum, or object kind is compared.
	if (p_target.is_nullable && p_source.kind == BSParser::DataType::BUILTIN && p_source.builtin_type == Variant::NIL) {
		return Result(true, false, false);
	}

	// Pin fs_type.cpp:1076-1087: a Type slot stores a handle, never an instance.
	// NIL has its own legacy handle rule, after the shared dynamic/nullability gates.
	if (p_target.is_type_handle_annotation) {
		if (p_source.kind == BSParser::DataType::BUILTIN && p_source.builtin_type == Variant::NIL)
			return Result(true, false, false);
		if (!p_source.is_meta_type && !p_source.is_type_handle_annotation)
			return Result(false, false, false);
		auto represented = [](BSParser::DataType type) {
			type.is_type_handle_annotation = false;
			type.is_meta_type = false;
			type.is_pseudo_type = false;
			type.is_constant = false;
			type.is_nullable = false;
			return type;
		};
		const BSParser::DataType target_instance = represented(p_target);
		const BSParser::DataType source_instance = represented(p_source);
		// A known native class handle denotes exactly that class. The ordinary instance
		// runtime-narrowing allowance cannot turn a Node class object into Control.
		if (target_instance.kind == BSParser::DataType::NATIVE && source_instance.kind == BSParser::DataType::NATIVE &&
				!ClassDB::is_parent_class(source_instance.native_type, target_instance.native_type))
			return Result(false, false, false);
		return check(target_instance, source_instance, p_options);
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

		if (result.compatible && p_source.kind == BSParser::DataType::BUILTIN && p_target.builtin_type == p_source.builtin_type && _is_signature_builtin_type(p_target.builtin_type)) {
			if (p_target.has_method_signature && p_source.has_method_signature) {
				// Assignment position: the target states what callers may pass, so its rest tail is
				// contravariant while every other slot stays invariant.
				result.compatible = _datatype_method_signature_equal(p_target, p_source, true, p_options.strict_null, p_options.allow_runtime_narrowing);
			} else if (p_target.has_method_signature && !p_source.has_method_signature) {
				result.requires_runtime_check = true;
			}
			// Enforce the async marker even when only one side carries a method signature, so a bare
			// `AsyncCallable` is still distinct from a bare `Callable`. An async target requires an
			// async source, and a synchronous target that carries a signature rejects an async source.
			// A bare, signatureless synchronous `Callable` target still accepts any callable (e.g. the
			// `Callable` parameter of `Signal.connect`), so async-ness is only enforced when the target
			// is itself async or carries an explicit signature.
			if (result.compatible && p_target.builtin_type == Variant::CALLABLE &&
					p_target.signature_is_async != p_source.signature_is_async &&
					(p_target.signature_is_async || p_target.has_method_signature)) {
				result.compatible = false;
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

	// Foundry FSTypeCompatibility::check @ c9d5e35: a plain enum value has the D1 int
	// carrier. Keep the builtin arm's conversion flag when conversions were requested;
	// without conversions the enum/carrier identity is still compatible. A declaration
	// handle or tagged union is not an integer value.
	if (p_target.kind == BSParser::DataType::BUILTIN && p_target.builtin_type == Variant::INT &&
			p_source.kind == BSParser::DataType::ENUM && !p_source.is_meta_type && !p_source.is_tagged_union) {
		return Result(true, false, p_options.allow_implicit_conversion && Variant::can_convert_strict(p_source.builtin_type, p_target.builtin_type));
	}
	// The reverse boundary is enum membership, not an implicit carrier conversion.
	// Foundry's earlier Type[...] arm rejects integer values before this enum branch.
	if (p_target.kind == BSParser::DataType::ENUM && !p_target.is_tagged_union && !p_target.is_type_handle_annotation &&
			p_source.kind == BSParser::DataType::BUILTIN && p_source.builtin_type == Variant::INT) {
		return Result(true, false, false);
	}

	// Pin fs_type.cpp:1325-1329: legacy NIL admission applies to object families,
	// after builtin/enum decisions; tuple and other value shapes are not object slots.
	if (p_source.kind == BSParser::DataType::BUILTIN && p_source.builtin_type == Variant::NIL &&
			(p_target.kind == BSParser::DataType::NATIVE || p_target.kind == BSParser::DataType::CLASS || p_target.kind == BSParser::DataType::SCRIPT)) {
		return Result(!p_options.strict_null || p_target.is_nullable, false, false);
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

	// Foundry FSTypeCompatibility::check @ c9d5e35 (~1352–1447): non-meta trait CLASS targets.
	// Nominal membership alone does not prove the source conforms at the destination's type
	// arguments — gradual no-evidence stays compatible; recorded / projected contradictions reject.
	if (p_target.kind == BSParser::DataType::CLASS && p_target.class_type != nullptr &&
			p_target.class_type->is_trait && !p_target.is_meta_type) {
		Result result(false, false, false);
		if (p_source.kind == BSParser::DataType::CLASS && !p_source.is_meta_type) {
			result.compatible = _class_has_trait(p_source.class_type, p_target.class_type);
			if (result.compatible && p_target.has_type_arguments()) {
				Vector<BSParser::DataType> projected;
				if (_project_class_trait_arguments(p_source, p_target.class_type, projected)) {
					if (projected.size() == p_target.type_arguments.size()) {
						for (int i = 0; i < projected.size(); i++) {
							if (!projected[i].is_set()) {
								continue;
							}
							// Live structure is richer than recorded persistence evidence. UNKNOWN
							// preserves only the nominal membership already established above.
							if (compare_projected_argument(projected[i], p_target.type_arguments[i]) == ArgumentEvidence::CONFLICT) {
								result.compatible = false;
								break;
							}
						}
					}
				} else {
					Vector<BSConformanceRegistry::RecordedTypeArgument> recorded;
					if (_project_registry_trait_arguments(p_source, bs_trait_identity_name(p_target.class_type), recorded) &&
							_recorded_arguments_conflict(recorded, p_target.type_arguments)) {
						result.compatible = false;
					}
				}
			}
			return result;
		}
		if (p_source.kind == BSParser::DataType::SCRIPT && p_source.script_type.is_valid() && !p_source.is_meta_type) {
			const StringName trait_name = bs_trait_identity_name(p_target.class_type);
			const BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
			// R14: M4 owns compiled Script trait membership. Source CLASS membership above is implemented;
			// for SCRIPT sources is registry + native-base conformance only (Foundry fallback path).
			result.compatible = registry != nullptr &&
					((_path_identifies_script(p_source.script_type) &&
							 registry->has_conformance(p_source.script_path, trait_name)) ||
							registry->has_conformance(p_source.script_type->get_global_name(), trait_name) ||
							registry->native_class_conforms(p_source.script_type->get_instance_base_type(), trait_name));
			return result;
		}
		if (p_source.kind == BSParser::DataType::NATIVE && !p_source.is_meta_type) {
			const StringName trait_name = bs_trait_identity_name(p_target.class_type);
			const BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
			result.compatible = registry != nullptr && registry->native_class_conforms(p_source.native_type, trait_name);
			if (result.compatible && p_target.has_type_arguments()) {
				Vector<BSConformanceRegistry::RecordedTypeArgument> recorded;
				if (registry->get_native_recorded_trait_arguments(p_source.native_type, trait_name, recorded) &&
						_recorded_arguments_conflict(recorded, p_target.type_arguments)) {
					result.compatible = false;
				}
			}
			return result;
		}
		if (p_source.kind == BSParser::DataType::BUILTIN && !p_source.is_meta_type) {
			const StringName trait_name = bs_trait_identity_name(p_target.class_type);
			const BSConformanceRegistry *registry = BSConformanceRegistry::get_singleton();
			result.compatible = registry != nullptr && registry->builtin_type_conforms(p_source.builtin_type, trait_name);
			if (result.compatible && p_target.has_type_arguments()) {
				Vector<BSConformanceRegistry::RecordedTypeArgument> recorded;
				if (registry->get_builtin_recorded_trait_arguments(p_source.builtin_type, trait_name, recorded) &&
						_recorded_arguments_conflict(recorded, p_target.type_arguments)) {
					result.compatible = false;
				}
			}
			return result;
		}
		return result;
	}

	if (p_target.kind == BSParser::DataType::NATIVE || p_target.kind == BSParser::DataType::CLASS || p_target.kind == BSParser::DataType::SCRIPT) {
		if (p_source.kind == BSParser::DataType::NATIVE || p_source.kind == BSParser::DataType::CLASS || p_source.kind == BSParser::DataType::SCRIPT) {
			if (p_target.can_reference(p_source)) {
				return Result(true, false, false);
			}
			if (p_options.allow_runtime_narrowing && allows_runtime_narrowing(p_target, p_source)) {
				return Result(true, true, false);
			}
		}
		return Result(false, false, false);
	}

	if (p_target.kind == BSParser::DataType::ENUM && p_source.kind == BSParser::DataType::ENUM) {
		return Result(p_target.native_type == p_source.native_type && p_target.enum_type == p_source.enum_type, false, false);
	}

	if (p_target.kind == BSParser::DataType::TUPLE && p_source.kind == BSParser::DataType::TUPLE) {
		if (p_target.is_meta_type != p_source.is_meta_type)
			return Result(false, false, false);
		if (p_target.tuple_name != StringName())
			return Result(is_invariant_equal(p_target, p_source), false, false);
		if (p_target.container_element_types.size() != p_source.container_element_types.size())
			return Result(false, false, false);
		Options element_options = p_options;
		element_options.allow_implicit_conversion = false;
		element_options.constant_source_value = nullptr;
		Result result(false, false, false);
		for (int i = 0; i < p_target.container_element_types.size(); ++i) {
			const Result forward = check(p_target.container_element_types[i], p_source.container_element_types[i], element_options);
			const Result backward = check(p_source.container_element_types[i], p_target.container_element_types[i], element_options);
			if (!forward.compatible || !backward.compatible)
				return Result(false, false, false);
			result.requires_runtime_check = result.requires_runtime_check || forward.requires_runtime_check;
		}
		result.compatible = true;
		return result;
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
		const BSParser::DataType *p_required_rest_array, bool p_strict_null, bool p_allow_runtime_narrowing) {
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
	element_options.allow_runtime_narrowing = p_allow_runtime_narrowing;
	return check(p_implementation_rest_array->get_container_element_type(0),
			p_required_rest_array->get_container_element_type(0), element_options)
			.compatible;
}

bool BSTypeCompatibility::rest_parameter_accepts_required_argument(const BSParser::DataType *p_implementation_rest_array,
		const BSParser::DataType &p_required_argument_type, bool p_strict_null, bool p_allow_runtime_narrowing) {
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
	element_options.allow_runtime_narrowing = p_allow_runtime_narrowing;
	return check(p_implementation_rest_array->get_container_element_type(0), p_required_argument_type, element_options)
			.compatible;
}

bool BSTypeCompatibility::recorded_arguments_conflict(
		const Vector<BSConformanceRegistry::RecordedTypeArgument> &p_recorded,
		const Vector<BSConformanceRegistry::RecordedTypeArgument> &p_other) {
	if (p_recorded.size() != p_other.size()) {
		return false;
	}
	for (int i = 0; i < p_recorded.size(); i++) {
		if (_recorded_arguments_disagree(p_recorded[i], p_other[i])) {
			return true;
		}
	}
	return false;
}

bool BSTypeCompatibility::recorded_arguments_conflict(
		const Vector<BSConformanceRegistry::RecordedTypeArgument> &p_recorded,
		const Vector<BSParser::DataType> &p_expected) {
	return _recorded_arguments_conflict(p_recorded, p_expected);
}

bool BSTypeCompatibility::project_class_trait_arguments(const BSParser::DataType &p_source,
		const BSParser::ClassNode *p_trait, Vector<BSParser::DataType> &r_arguments) {
	return _project_class_trait_arguments(p_source, p_trait, r_arguments);
}

bool BSTypeCompatibility::project_registry_trait_arguments(const BSParser::DataType &p_source,
		const StringName &p_trait_name, Vector<BSConformanceRegistry::RecordedTypeArgument> &r_arguments) {
	return _project_registry_trait_arguments(p_source, p_trait_name, r_arguments);
}

} // namespace barista_script
