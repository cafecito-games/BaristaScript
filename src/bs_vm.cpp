/**************************************************************************/
/*  bs_vm.cpp                                                             */
/*                                                                        */
/*  The dispatch loop. Provenance: fs_vm.cpp:2471-2851 and :7524-7614     */
/*  @ c9d5e35, with the handler families in the per-family .inc files.    */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_core_constants.h"
#include "bs_function.h"
#include "bs_native_db.h"
#include "bs_script_instance.h"
#include "bs_utility_functions.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace barista_script {

namespace {

/**
 * A frame's operand words are read relative to the instruction pointer, so each opcode's operand
 * count and its `instruction_pointer += n` are the same statement of its layout. The three address
 * spaces a word can name -- the frame's stack, the function's constant pool, and the instance's
 * members -- are selected by the top byte of the word, exactly as the emitter wrote it.
 */
constexpr int MAX_CALL_DEPTH = 1024;

thread_local int call_depth = 0;
thread_local bool runtime_error_reported = false;

struct CallDepthGuard {
	bool entered = false;
	CallDepthGuard() {
		entered = ++call_depth <= MAX_CALL_DEPTH;
	}
	~CallDepthGuard() { call_depth--; }
};

/**
 * True for the carriers whose subscript is an integer position rather than a key.
 *
 * A failed read on one of these is a range fault with a well-formed index, which reads nothing like
 * an unknown key, so the diagnostic has to tell the two apart. godot-cpp exposes neither
 * `Variant::get_indexed_element_type` nor the engine's `VariantGetError`, so the set is spelled out.
 * Provenance: the indexed setters/getters registered in core/variant/variant_setget.cpp @ 4.7.
 */
bool variant_type_is_indexed(Variant::Type p_type) {
	switch (p_type) {
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		case Variant::VECTOR4:
		case Variant::VECTOR4I:
		case Variant::QUATERNION:
		case Variant::COLOR:
		case Variant::TRANSFORM2D:
		case Variant::BASIS:
		case Variant::ARRAY:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_COLOR_ARRAY:
		case Variant::PACKED_VECTOR4_ARRAY:
			return true;
		default:
			return false;
	}
}

/** True for the carriers whose value is a shared reference, so an in-place edit is already visible. */
bool variant_type_is_shared(Variant::Type p_type) {
	switch (p_type) {
		case Variant::OBJECT:
		case Variant::ARRAY:
		case Variant::DICTIONARY:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_COLOR_ARRAY:
		case Variant::PACKED_VECTOR4_ARRAY:
			return true;
		default:
			return false;
	}
}

/**
 * Renders a value's type the way a runtime diagnostic names it.
 *
 * This is not `Variant::get_type_name`: a diagnostic has to distinguish a null reference from an
 * object whose owner has already been freed, because the two have completely different causes, and
 * it has to name the class of a live object rather than the word "Object". A typed container names
 * its element types, so a store rejection reads as the mismatch it is.
 *
 * Provenance: fs_vm.cpp:59-101 `_get_var_type` @ c9d5e35, minus the fork-only class wrappers
 * (`FSNativeClass`, `FSSpecializedClassHandle`) that BaristaScript does not have.
 */
String describe_value_type(const Variant &p_value) {
	if (p_value.get_type() == Variant::OBJECT) {
		Object *object = p_value.get_validated_object();
		if (object == nullptr) {
			// A non-null ObjectID whose object is gone is a use-after-free, which reads nothing like
			// a plain null and must not be reported as one.
			return ((Object *)p_value) != nullptr ? String("previously freed") : String("null instance");
		}
		return object->get_class();
	}
	if (p_value.get_type() == Variant::ARRAY) {
		const Array array = p_value;
		return array.is_typed() ? "Array[" + Variant::get_type_name((Variant::Type)array.get_typed_builtin()) + "]" : String("Array");
	}
	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary dictionary = p_value;
		return dictionary.is_typed()
				? "Dictionary[" + Variant::get_type_name((Variant::Type)dictionary.get_typed_key_builtin()) + ", " +
						Variant::get_type_name((Variant::Type)dictionary.get_typed_value_builtin()) + "]"
				: String("Dictionary");
	}
	return Variant::get_type_name(p_value.get_type());
}

/** True when the value is a container the engine has sealed against further edits. */
bool value_is_read_only(const Variant &p_value) {
	// `Variant::is_read_only` is core-only; the two carriers that can be sealed answer for
	// themselves, and no other carrier has the concept at all.
	if (p_value.get_type() == Variant::ARRAY) {
		return ((Array)p_value).is_read_only();
	}
	if (p_value.get_type() == Variant::DICTIONARY) {
		return ((Dictionary)p_value).is_read_only();
	}
	return false;
}

/**
 * Renders a failed call the way the caller wrote it.
 *
 * `p_where` names the callee in the caller's own words ("function 'x' in base 'Array'"), so one
 * formatter serves every call opcode and the reader is never shown an internal spelling.
 *
 * Provenance: fs_vm.cpp:1836-1880 `_get_call_error` @ c9d5e35, minus the declared-parameter branch
 * that needs a BaristaScript callee's own signature (that belongs with the typed-boundary work).
 */
String describe_call_error(const String &p_where, const Variant **p_arguments, int p_argument_count,
		const GDExtensionCallError &p_error) {
	switch (p_error.error) {
		case GDEXTENSION_CALL_OK:
			return String();
		case GDEXTENSION_CALL_ERROR_INVALID_METHOD:
			return "Invalid call. Nonexistent " + p_where + ".";
		case GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT: {
			if (p_error.argument < 0 || p_error.argument >= p_argument_count || p_arguments[p_error.argument] == nullptr) {
				return "Invalid type in " + p_where + ".";
			}
			return "Invalid type in " + p_where + ". Cannot convert argument " + itos(p_error.argument + 1) +
					" from " + Variant::get_type_name(p_arguments[p_error.argument]->get_type()) + " to " +
					Variant::get_type_name((Variant::Type)p_error.expected) + ".";
		}
		case GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS:
		case GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS:
			return "Invalid call to " + p_where + ". Expected " + itos(p_error.expected) + " argument(s).";
		case GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL:
			return "Attempt to call " + p_where + " on a null instance.";
		case GDEXTENSION_CALL_ERROR_METHOD_NOT_CONST:
			return "Attempt to call " + p_where + " on a const instance.";
		default:
			break;
	}
	return "Bug: Invalid call error code " + itos((int)p_error.error) + ".";
}

/** The operator's source spelling, for a diagnostic that names what the program wrote. */
String variant_operator_name(Variant::Operator p_operator) {
	static const char *const names[Variant::OP_MAX] = {
		"==", "!=", "<", "<=", ">", ">=",
		"+", "-", "*", "/", "unary-", "unary+", "%", "**",
		"<<", ">>", "&", "|", "^", "~",
		"and", "or", "xor", "not", "in"
	};
	if (p_operator < 0 || p_operator >= Variant::OP_MAX) {
		return "<unknown>";
	}
	return names[p_operator];
}

/**
 * Calls one of the engine's global utility functions.
 *
 * The engine exposes a utility only as a typed pointer call: the arguments are the parameters' own
 * native carriers, not Variants, and so is the return. That is why this is not a one-liner. Each
 * argument is converted into the carrier the signature declares and handed over by internal
 * pointer; a parameter declared as a Variant -- which is how every variadic utility declares all of
 * them -- travels as the Variant it already is.
 *
 * The conversion is the same widening the engine performs at any typed boundary, so `floor(1)`
 * reaches a `float` parameter exactly as it would through an ordinary call. A value that cannot
 * convert is reported as an argument error rather than silently reinterpreted.
 *
 * This is NOT the validated fast path (#240 decision 2): no `*_VALIDATED` opcode is emitted and no
 * signature is cached. It is the only call shape the engine offers for a utility at all.
 */
bool call_engine_utility(const StringName &p_name, const Variant **p_arguments, int p_argument_count,
		Variant &r_return, GDExtensionCallError &r_error, String &r_error_message) {
	r_error.error = GDEXTENSION_CALL_OK;
	r_error.argument = 0;
	r_error.expected = 0;

	MethodInfo info;
	if (!BSCoreConstants::get_utility_function(p_name, info)) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return false;
	}
	int64_t hash = 0;
	if (!BSCoreConstants::get_utility_function_hash(p_name, hash)) {
		r_error_message = vformat(R"*(Cannot call utility function "%s()": no pinned signature hash.)*", String(p_name));
		return false;
	}
	GDExtensionPtrUtilityFunction function = godot::gdextension_interface::variant_get_ptr_utility_function(&p_name, hash);
	if (function == nullptr) {
		r_error_message = vformat(R"*(Cannot call utility function "%s()": the engine rejected the pinned signature.)*", String(p_name));
		return false;
	}

	const bool is_vararg = (info.flags & METHOD_FLAG_VARARG) != 0;
	const int declared_count = info.arguments.size();
	// A non-variadic utility's arity is exact. No engine utility declares a default argument: the
	// pinned `extension_api-4-7.json` carries a `default_value` on none of the 114 utility
	// functions' parameters, which is why the overloads that look defaulted are separate names
	// (`var_to_bytes` and `var_to_bytes_with_objects`, not one function with a flag). A pointer call
	// has no way to omit an argument anyway, so if a future API pin ever introduces one, this is the
	// check that has to learn about it -- and it will say so by refusing the call rather than by
	// passing whatever happens to be in the argument array.
	if (!is_vararg && p_argument_count != declared_count) {
		r_error.error = p_argument_count < declared_count ? GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS
														  : GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
		r_error.expected = declared_count;
		return false;
	}

	const auto declared_as_variant = [&](int p_index) {
		return p_index >= declared_count ||
				(info.arguments[p_index].type == Variant::NIL &&
						(info.arguments[p_index].usage & PROPERTY_USAGE_NIL_IS_VARIANT) != 0);
	};

	// The converted values must outlive the call: the pointers handed to the engine point into
	// them. They are filled first and the vector is never resized afterwards, so no pointer taken
	// below can be left dangling by a reallocation.
	Vector<Variant> converted;
	converted.resize(p_argument_count);
	for (int i = 0; i < p_argument_count; i++) {
		if (declared_as_variant(i) || p_arguments[i]->get_type() == info.arguments[i].type) {
			converted.write[i] = *p_arguments[i];
			continue;
		}
		const Variant::Type declared = info.arguments[i].type;
		if (!Variant::can_convert_strict(p_arguments[i]->get_type(), declared)) {
			r_error.error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
			r_error.argument = i;
			r_error.expected = declared;
			return false;
		}
		// godot-cpp has no `Variant::construct`, so the engine's own one-argument constructor is
		// invoked directly: it is the conversion the language performs at every other typed
		// boundary, which is what keeps `floor(1)` behaving like `floor(1.0)`.
		GDExtensionCallError construct_error;
		construct_error.error = GDEXTENSION_CALL_OK;
		Variant result;
		const GDExtensionConstVariantPtr construct_argument = p_arguments[i];
		godot::gdextension_interface::variant_construct((GDExtensionVariantType)declared,
				reinterpret_cast<GDExtensionUninitializedVariantPtr>(&result), &construct_argument, 1, &construct_error);
		if (construct_error.error != GDEXTENSION_CALL_OK) {
			r_error.error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
			r_error.argument = i;
			r_error.expected = declared;
			return false;
		}
		converted.write[i] = result;
	}

	// One writable view of the storage, taken after the last write to it. Mixing `Vector`'s const
	// and writable accessors while pointers into the buffer are being collected would, on a shared
	// buffer, copy it out from under the pointers already taken.
	Variant *converted_values = converted.ptrw();
	Vector<const void *> pointers;
	pointers.resize(p_argument_count);
	for (int i = 0; i < p_argument_count; i++) {
		if (declared_as_variant(i)) {
			pointers.write[i] = &converted_values[i];
			continue;
		}
		GDExtensionVariantGetInternalPtrFunc getter =
				godot::gdextension_interface::variant_get_ptr_internal_getter((GDExtensionVariantType)info.arguments[i].type);
		if (getter == nullptr) {
			r_error_message = vformat(R"*(Cannot call utility function "%s()": argument %d has no reachable carrier.)*",
					String(p_name), i + 1);
			return false;
		}
		pointers.write[i] = getter(reinterpret_cast<GDExtensionVariantPtr>(&converted_values[i]));
	}

	const bool returns_variant = info.return_val.type == Variant::NIL &&
			(info.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT) != 0;
	if (info.return_val.type == Variant::NIL && !returns_variant) {
		function(nullptr, pointers.ptr(), p_argument_count);
		r_return = Variant();
		return true;
	}
	if (returns_variant) {
		Variant result;
		function(&result, pointers.ptr(), p_argument_count);
		r_return = result;
		return true;
	}
	// A typed return needs storage of its own carrier for the engine to write into. A
	// default-constructed Variant of that type owns exactly that storage, so the result lands in a
	// Variant with no per-type switch and no second copy.
	GDExtensionCallError construct_error;
	construct_error.error = GDEXTENSION_CALL_OK;
	Variant storage;
	godot::gdextension_interface::variant_construct((GDExtensionVariantType)info.return_val.type,
			reinterpret_cast<GDExtensionUninitializedVariantPtr>(&storage), nullptr, 0, &construct_error);
	GDExtensionVariantGetInternalPtrFunc return_getter =
			godot::gdextension_interface::variant_get_ptr_internal_getter((GDExtensionVariantType)info.return_val.type);
	if (construct_error.error != GDEXTENSION_CALL_OK || return_getter == nullptr) {
		r_error_message = vformat(R"*(Cannot call utility function "%s()": its return carrier is not reachable from compiled code.)*",
				String(p_name));
		return false;
	}
	function(return_getter(reinterpret_cast<GDExtensionVariantPtr>(&storage)), pointers.ptr(), p_argument_count);
	r_return = storage;
	return true;
}

/**
 * Runs one of the language's own utility functions.
 *
 * These are not engine utilities: the engine has never heard of them, they are reached by name from
 * compiled code alone, and their bodies live here rather than behind a pointer call. Provenance:
 * fs_utility_functions.cpp `FSUtilityFunctionsDefinitions` @ c9d5e35, restricted to the entries
 * `bs_utility_functions.cpp` registers.
 *
 * A failure is reported the way the engine reports a failed call, through `r_error`, with `r_return`
 * carrying an explanatory string when the utility has one. The caller renders both.
 */
bool call_language_utility(const StringName &p_name, const Variant **p_arguments, int p_argument_count,
		Variant &r_return, GDExtensionCallError &r_error) {
	MethodInfo info;
	if (!BSUtilityFunctions::get_function_info(p_name, info)) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return false;
	}
	r_error.error = GDEXTENSION_CALL_OK;
	r_error.argument = 0;
	r_error.expected = 0;
	r_return = Variant();

	const auto fail_argument = [&](int p_index, Variant::Type p_expected, const Variant &p_message) {
		r_return = p_message;
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = p_index;
		r_error.expected = p_expected;
		return false;
	};
	// A variadic utility declares no fixed arity; every other one is exact, and the arity is checked
	// before any argument is read so a short call cannot index past the array.
	if ((info.flags & METHOD_FLAG_VARARG) == 0 && p_argument_count != (int)info.arguments.size()) {
		r_error.error = p_argument_count < (int)info.arguments.size()
				? GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS
				: GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
		r_error.expected = info.arguments.size();
		return false;
	}
	// Then the carriers, before any body reads one. A body converts its argument the moment it
	// touches it -- `const int64_t code = *p_arguments[0]` turns a String into zero and carries on --
	// so a value the signature does not admit has to be refused here or it becomes a plausible
	// answer instead of an error. The analyzer rejects most of these statically; a `Variant`-typed
	// value reaching the call is exactly the case it cannot. Provenance: the
	// `DEBUG_VALIDATE_ARG_TYPE` guard each upstream body opens with, fs_utility_functions.cpp:59-66
	// @ c9d5e35, hoisted to one place because the signature already says what each body would check.
	for (int i = 0; i < p_argument_count && i < (int)info.arguments.size(); i++) {
		const Variant::Type declared = info.arguments[i].type;
		const bool declared_as_variant = declared == Variant::NIL &&
				(info.arguments[i].usage & PROPERTY_USAGE_NIL_IS_VARIANT) != 0;
		if (declared_as_variant || Variant::can_convert_strict(p_arguments[i]->get_type(), declared)) {
			continue;
		}
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = i;
		r_error.expected = declared;
		return false;
	}

	if (p_name == SNAME("len")) {
		const Variant &value = *p_arguments[0];
		switch (value.get_type()) {
			case Variant::STRING:
			case Variant::STRING_NAME:
				r_return = ((String)value).length();
				break;
			case Variant::DICTIONARY:
				r_return = ((Dictionary)value).size();
				break;
			case Variant::ARRAY:
				r_return = ((Array)value).size();
				break;
			case Variant::PACKED_BYTE_ARRAY:
				r_return = ((PackedByteArray)value).size();
				break;
			case Variant::PACKED_INT32_ARRAY:
				r_return = ((PackedInt32Array)value).size();
				break;
			case Variant::PACKED_INT64_ARRAY:
				r_return = ((PackedInt64Array)value).size();
				break;
			case Variant::PACKED_FLOAT32_ARRAY:
				r_return = ((PackedFloat32Array)value).size();
				break;
			case Variant::PACKED_FLOAT64_ARRAY:
				r_return = ((PackedFloat64Array)value).size();
				break;
			case Variant::PACKED_STRING_ARRAY:
				r_return = ((PackedStringArray)value).size();
				break;
			case Variant::PACKED_VECTOR2_ARRAY:
				r_return = ((PackedVector2Array)value).size();
				break;
			case Variant::PACKED_VECTOR3_ARRAY:
				r_return = ((PackedVector3Array)value).size();
				break;
			case Variant::PACKED_COLOR_ARRAY:
				r_return = ((PackedColorArray)value).size();
				break;
			case Variant::PACKED_VECTOR4_ARRAY:
				r_return = ((PackedVector4Array)value).size();
				break;
			default:
				return fail_argument(0, Variant::NIL,
						vformat("Value of type '%s' can't provide a length.", Variant::get_type_name(value.get_type())));
		}
		return true;
	}
	if (p_name == SNAME("char")) {
		const int64_t code = *p_arguments[0];
		// The admissible range is the whole unsigned 32-bit one, not the assigned Unicode range. That
		// is upstream's bound (fs_utility_functions.cpp:110-114 @ c9d5e35) and, more importantly, it
		// is the bound `BSUtilityFunctions::evaluate_constant` folds a constant `char()` against.
		// Refusing surrogates or values above U+10FFFF only here would make the same expression a
		// folded constant in one path and a runtime error in the other.
		if (code < 0 || code > UINT32_MAX) {
			return fail_argument(0, Variant::INT, "Expected an integer between 0 and 2^32 - 1.");
		}
		r_return = String::chr(code);
		return true;
	}
	if (p_name == SNAME("ord")) {
		const String text = *p_arguments[0];
		if (text.length() != 1) {
			return fail_argument(0, Variant::STRING, "Expected a string of length 1 (a character).");
		}
		r_return = text.unicode_at(0);
		return true;
	}
	if (p_name == SNAME("type_exists")) {
		r_return = ClassDB::class_exists(*p_arguments[0]);
		return true;
	}
	if (p_name == SNAME("is_instance_of")) {
		const Variant &value = *p_arguments[0];
		const Variant &type = *p_arguments[1];
		if (type.get_type() == Variant::INT) {
			const int64_t builtin = type;
			if (builtin < 0 || builtin >= Variant::VARIANT_MAX) {
				return fail_argument(1, Variant::INT, "Invalid type argument for \"is_instance_of()\", use the \"TYPE_*\" constants.");
			}
			r_return = value.get_type() == (Variant::Type)builtin;
			return true;
		}
		if (type.get_type() != Variant::OBJECT) {
			return fail_argument(1, Variant::NIL, "Invalid type argument for \"is_instance_of()\", should be a \"TYPE_*\" constant, a class or a script.");
		}
		Object *type_object = type.get_validated_object();
		Object *value_object = value.get_type() == Variant::OBJECT ? value.get_validated_object() : nullptr;
		if (type_object == nullptr) {
			return fail_argument(1, Variant::OBJECT, "Type argument is a previously freed instance.");
		}
		if (value_object == nullptr) {
			r_return = false;
			return true;
		}
		Script *type_script = Object::cast_to<Script>(type_object);
		if (type_script == nullptr) {
			r_return = false;
			return true;
		}
		// A script's instances are recognized by walking the receiver's own script chain: an
		// inherited script satisfies a base script's test, exactly as a subclass satisfies its base.
		Ref<Script> candidate = value_object->get_script();
		while (candidate.is_valid()) {
			if (candidate.ptr() == type_script) {
				r_return = true;
				return true;
			}
			candidate = candidate->get_base_script();
		}
		r_return = false;
		return true;
	}
	if (p_name == SNAME("range")) {
		// `range` declares no fixed parameters -- its arity is one, two or three -- so the signature
		// loop above has nothing to check it against, and each bound is converted the moment it is
		// read. The carriers are therefore checked here, which is where the arity is known.
		// Provenance: the per-arity `DEBUG_VALIDATE_ARG_TYPE(n, Variant::INT)` guards in upstream's
		// own `range`, fs_utility_functions.cpp:126-... @ c9d5e35.
		for (int i = 0; i < p_argument_count; i++) {
			if (!Variant::can_convert_strict(p_arguments[i]->get_type(), Variant::INT)) {
				return fail_argument(i, Variant::INT, Variant());
			}
		}
		int64_t from = 0;
		int64_t to = 0;
		int64_t step = 1;
		switch (p_argument_count) {
			case 1:
				to = *p_arguments[0];
				break;
			case 2:
				from = *p_arguments[0];
				to = *p_arguments[1];
				break;
			case 3:
				from = *p_arguments[0];
				to = *p_arguments[1];
				step = *p_arguments[2];
				if (step == 0) {
					return fail_argument(2, Variant::INT, "Step argument is zero!");
				}
				break;
			default:
				r_error.error = p_argument_count < 1 ? GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS
													 : GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
				r_error.expected = p_argument_count < 1 ? 1 : 3;
				return false;
		}
		Array result;
		// The count is computed before the loop so a huge or empty range costs one allocation and no
		// repeated growth, and so the sign of the step decides emptiness once rather than per element.
		//
		// The span is computed in unsigned arithmetic throughout, because it is exactly the quantity
		// that does not fit: `range(-9223372036854775808, 9223372036854775807)` is a legal pair of
		// integers whose difference is not an `int64_t`, and computing it signed is undefined
		// behaviour rather than the "Range too big." refusal the caller is owed. Negating the step is
		// unsigned for the same reason -- `-INT64_MIN` has no signed value, while its magnitude is
		// representable unsigned.
		uint64_t span = 0;
		uint64_t stride = 0;
		if (step > 0 && to > from) {
			span = (uint64_t)to - (uint64_t)from;
			stride = (uint64_t)step;
		} else if (step < 0 && to < from) {
			span = (uint64_t)from - (uint64_t)to;
			stride = -(uint64_t)step;
		}
		// Rounded up without the `span + stride - 1` that would overflow a span near the unsigned
		// maximum -- which is precisely the span this guard exists to reject.
		const uint64_t count = stride == 0 ? 0 : span / stride + (span % stride != 0 ? 1 : 0);
		if (count > (uint64_t)INT32_MAX) {
			r_return = "Range too big.";
			r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
			return false;
		}
		result.resize((int64_t)count);
		for (uint64_t index = 0; index < count; index++) {
			// The last element is `from + (count - 1) * step`, which is within the range's own bounds
			// and therefore representable. The arithmetic is still done unsigned so an intermediate
			// product cannot trip signed overflow on the way to a value that does fit.
			result[(int64_t)index] = (int64_t)((uint64_t)from + index * (uint64_t)step);
		}
		r_return = result;
		return true;
	}
	if (p_name == SNAME("load")) {
		r_return = ResourceLoader::get_singleton()->load(*p_arguments[0]);
		return true;
	}
	if (p_name == SNAME("print_debug")) {
		// The stack frame upstream appends belongs with the debugger surface (#252); the values
		// themselves are printed here so a script that uses it is not stopped by the gap.
		String line;
		for (int i = 0; i < p_argument_count; i++) {
			line += p_arguments[i]->stringify();
		}
		UtilityFunctions::print(line);
		return true;
	}
	// `print_stack`, `get_stack` and `create_proxy_dynamic` need surfaces this milestone does not
	// have -- the debugger stack (#252) and the proxy runtime -- and inventing an answer for them
	// would be worse than saying so.
	r_return = vformat(R"*(the runtime does not implement "%s()" yet.)*", String(p_name));
	r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
	return false;
}

/** Reports only the frame that found the fault, not every frame the fault unwinds through. */
void report_runtime_error_once(const String &p_description, const StringName &p_function, const String &p_file, int p_line) {
	if (runtime_error_reported) {
		return;
	}
	runtime_error_reported = true;
	bs_report_runtime_error(p_description, p_function, p_file, p_line);
}

} // namespace

bool bs_runtime_error_was_reported() {
	return runtime_error_reported;
}

Variant BSFunction::call(BSInstance *p_instance, const Variant **p_arguments, int p_argument_count, GDExtensionCallError &r_error) {
	// A frame that raises tells its caller so. The engine-facing boundary translates that back into a
	// completed call, because the reason is already on the script-error channel and a call error
	// would be reported a second time as a method that does not exist.
	r_error.error = GDEXTENSION_CALL_OK;
	r_error.argument = 0;
	r_error.expected = 0;

	if (code.is_empty()) {
		return Variant();
	}

	CallDepthGuard depth_guard;
	if (call_depth == 1) {
		// A new outermost call: whatever the previous one reported is answered for and done with.
		runtime_error_reported = false;
	}
	if (unlikely(!depth_guard.entered)) {
		report_runtime_error_once("Stack overflow. Check for infinite recursion in your script.", name, source, initial_line);
		return Variant();
	}

	if (unlikely(argument_types.size() < argument_count || argument_types.size() > argument_count + 1)) {
		report_runtime_error_once("Bad argument signature in compiled function.", name, source, initial_line);
		return Variant();
	}
	const bool is_vararg = argument_types.size() == argument_count + 1;
	const int optional_count = MAX(default_arguments.size() - 1, 0);
	int default_argument_index = 0;
	if (p_argument_count > argument_count && !is_vararg) {
		r_error.error = GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
		r_error.expected = argument_count;
		return Variant();
	}
	if (p_argument_count < argument_count - optional_count) {
		r_error.error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = argument_count - optional_count;
		return Variant();
	}
	default_argument_index = MAX(argument_count - p_argument_count, 0);

	Vector<Variant> stack;
	stack.resize(MAX(stack_size, (int)FIXED_ADDRESSES_MAX));
	Vector<Variant *> instruction_arguments;
	instruction_arguments.resize(MAX(instruction_arguments_size, 1));

	if (p_instance != nullptr && p_instance->owner != nullptr) {
		stack.write[ADDR_STACK_SELF] = p_instance->owner;
	}
	stack.write[ADDR_STACK_CLASS] = script;
	for (int i = 0; i < p_argument_count && i < argument_count; i++) {
		Variant converted;
		String conversion_error;
		if (!argument_types[i].convert(*p_arguments[i], converted, conversion_error)) {
			report_runtime_error_once(vformat("Invalid argument %d for %s(): %s", i + 1, String(name), conversion_error),
					name, source, initial_line);
			return Variant();
		}
		stack.write[i + FIXED_ADDRESSES_MAX] = converted;
	}
	if (is_vararg) {
		const int rest_count = MAX(p_argument_count - argument_count, 0);
		Array rest;
		rest.resize(rest_count);
		for (int i = 0; i < rest_count; i++) {
			rest[i] = *p_arguments[argument_count + i];
		}
		Variant converted_rest;
		String conversion_error;
		// The VM creates the rest tail as an erased Array. Its declared element type is
		// enforced while materializing the parameter, including the empty-tail case.
		if (!argument_types[argument_count].convert(rest, converted_rest, conversion_error, true)) {
			report_runtime_error_once("Invalid rest arguments for " + String(name) + "(): " + conversion_error,
					name, source, initial_line);
			return Variant();
		}
		stack.write[argument_count + FIXED_ADDRESSES_MAX] = converted_rest;
	}

	Variant *address_spaces[ADDR_TYPE_MAX] = {
		stack.ptrw(),
		constants.ptrw(),
		p_instance != nullptr ? p_instance->members.ptrw() : nullptr,
	};
	const int address_limits[ADDR_TYPE_MAX] = {
		(int)stack.size(),
		(int)constants.size(),
		p_instance != nullptr ? (int)p_instance->members.size() : 0,
	};

	const int *code_ptr = code.ptr();
	const int code_size = code.size();
	Variant **instruction_argument_ptr = instruction_arguments.ptrw();

	int instruction_pointer = 0;
	int line = initial_line;
	String error_text;
	Variant return_value;

#define BS_IP instruction_pointer
#define BS_CHECK_SPACE(m_space)                      \
	if (unlikely((BS_IP + (m_space)) > code_size)) { \
		error_text = "Truncated compiled function."; \
		goto vm_error;                               \
	}

#define BS_GET_VARIANT_PTR(m_name, m_offset)                                                \
	Variant *m_name = nullptr;                                                              \
	{                                                                                       \
		const int address = code_ptr[BS_IP + 1 + (m_offset)];                               \
		const int address_type = (address & ADDR_TYPE_MASK) >> ADDR_BITS;                   \
		if (unlikely(address_type < 0 || address_type >= ADDR_TYPE_MAX)) {                  \
			error_text = "Bad address type in compiled function.";                          \
			goto vm_error;                                                                  \
		}                                                                                   \
		const int address_index = address & ADDR_MASK;                                      \
		if (unlikely(address_index < 0 || address_index >= address_limits[address_type])) { \
			error_text = address_type == ADDR_TYPE_MEMBER && p_instance == nullptr          \
					? String("Cannot access a member without an instance.")                 \
					: String("Bad address index in compiled function.");                    \
			goto vm_error;                                                                  \
		}                                                                                   \
		m_name = &address_spaces[address_type][address_index];                              \
	}

#define BS_GET_NAME(m_name, m_index)                                   \
	if (unlikely((m_index) < 0 || (m_index) >= global_names.size())) { \
		error_text = "Bad name index in compiled function.";           \
		goto vm_error;                                                 \
	}                                                                  \
	const StringName &m_name = global_names[m_index]

#define BS_GET_RUNTIME_TYPE(m_name, m_index)                            \
	if (unlikely((m_index) < 0 || (m_index) >= runtime_types.size())) { \
		error_text = "Bad runtime-type index in compiled function.";    \
		goto vm_error;                                                  \
	}                                                                   \
	const BSRuntimeType &m_name = runtime_types[m_index]

#define BS_LOAD_INSTRUCTION_ARGUMENTS                                           \
	BS_CHECK_SPACE(2);                                                          \
	const int instruction_argument_count = code_ptr[BS_IP + 1];                 \
	if (unlikely(instruction_argument_count < 0 ||                              \
				instruction_argument_count > instruction_arguments.size())) {   \
		error_text = "Bad instruction argument count.";                         \
		goto vm_error;                                                          \
	}                                                                           \
	BS_CHECK_SPACE(2 + instruction_argument_count);                             \
	for (int argument = 0; argument < instruction_argument_count; argument++) { \
		BS_GET_VARIANT_PTR(value, argument + 1);                                \
		instruction_argument_ptr[argument] = value;                             \
	}                                                                           \
	BS_IP += 1;

	while (instruction_pointer < code_size) {
		switch (code_ptr[instruction_pointer]) {
#include "bs_vm_ops_async.inc"
#include "bs_vm_ops_core.inc"
#include "bs_vm_ops_iterators.inc"
#include "bs_vm_ops_members.inc"
#include "bs_vm_ops_types.inc"
			default: {
				// Fail closed: an opcode with no handler names itself rather than falling through.
				error_text = vformat("Opcode not implemented: %s.", get_opcode_name(code_ptr[instruction_pointer]));
				goto vm_error;
			}
		}
	}
	error_text = "Ran past the end of a compiled function.";

vm_error:
	// The call happened; it raised. The engine is told the call completed, because the script-error
	// channel already carries the reason and a call-error code would be reported a second time as a
	// missing method that in fact exists.
	report_runtime_error_once(error_text, name, source, line);
	return Variant();

vm_exit:
	return return_value;

#undef BS_LOAD_INSTRUCTION_ARGUMENTS
#undef BS_GET_NAME
#undef BS_GET_RUNTIME_TYPE
#undef BS_GET_VARIANT_PTR
#undef BS_CHECK_SPACE
#undef BS_IP
}

} // namespace barista_script
