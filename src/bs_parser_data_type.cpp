/**************************************************************************/
/*  bs_parser_data_type.cpp                                               */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_parser.h"

#include "barista_script.h"
#include "bs_diagnostic_names.h"

/**
 * Hard fork of Foundry's `BSParser::DataType` implementation
 * (`modules/foundry_script/fs_parser_data_type.cpp` @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6).
 * It is a separate translation unit upstream and stays one here: `bs_parser.cpp` is already the
 * largest file in the tree, and the split is where the upstream diff is readable.
 *
 * Two divergences, both recorded at the code that carries them: D1 deletes the integer registry and
 * every width descriptor, and the three engine predicates godot-cpp does not mirror take their
 * conservative branch rather than a guessed answer (see `_enum_signature_leaf_round_trips`).
 */

namespace barista_script {

// This function is used to determine that a type is "built-in" as opposed to native
// and custom classes. So `Variant::NIL` and `Variant::OBJECT` are excluded:
// `Variant::NIL` - `null` is literal, not a type.
// `Variant::OBJECT` - `Object` should be treated as a class, not as a built-in type.
// D1 also drops upstream's third exclusion, `Variant::UINT` (fs_parser_data_type.cpp:23 @ c9d5e35):
// stock Godot has no unsigned carrier to exclude.
static HashMap<StringName, Variant::Type> builtin_types;
Variant::Type BSParser::get_builtin_type(const StringName &p_type) {
	if (unlikely(builtin_types.is_empty())) {
		for (int i = 0; i < Variant::VARIANT_MAX; i++) {
			Variant::Type type = (Variant::Type)i;
			if (type != Variant::NIL && type != Variant::OBJECT) {
				builtin_types[Variant::get_type_name(type)] = type;
			}
		}
	}

	if (builtin_types.has(p_type)) {
		return builtin_types[p_type];
	}
	return Variant::VARIANT_MAX;
}

void BSParser::clear_builtin_type_cache() {
	builtin_types.clear();
}

// D1 deletes upstream's four-entry integer registry (fs_parser_data_type.cpp:45-56 @ c9d5e35). It
// existed because `int`/`long` shared the `INT` carrier and `uint`/`ulong` shared `UINT`, so a
// carrier could not tell the four spellings apart. BaristaScript has one integer spelling on one
// carrier (docs/GRAMMAR.md section 7.1), so `get_builtin_type()` is the whole registry and
// `is_builtin_data_type()` is a predicate over it.

const StringName &BSParser::get_number_type_name() {
	static const StringName number_type_name = StringName("Number");
	return number_type_name;
}

BSParser::DataType BSParser::make_number_type() {
	// `Number` is closed and, under D1, has exactly two members: `int` and `float`
	// (docs/GRAMMAR.md section 7.1). Upstream derives the integer members from the registry above.
	Vector<DataType> members;
	for (const Variant::Type numeric_carrier : { Variant::INT, Variant::FLOAT }) {
		DataType member;
		member.type_source = DataType::ANNOTATED_EXPLICIT;
		member.kind = DataType::BUILTIN;
		member.builtin_type = numeric_carrier;
		members.push_back(member);
	}
	return DataType::make_union(members);
}

String BSParser::get_builtin_type_source_name(Variant::Type p_builtin_type) {
	// D1: a carrier names itself. Upstream first asks whether a declared width has a public spelling
	// that agrees with the carrier (fs_parser_data_type.cpp:97-102 @ c9d5e35).
	return Variant::get_type_name(p_builtin_type);
}

static String _datatype_signature_type_to_string(const BSParser::DataType &p_type, bool p_nil_is_void = false) {
	if (p_nil_is_void && p_type.kind == BSParser::DataType::BUILTIN && p_type.builtin_type == Variant::NIL) {
		return "void";
	}
	return p_type.to_string();
}

static String _method_signature_to_string(const Vector<BSParser::DataType> &p_argument_types, const Vector<BSParser::DataType> &p_rest_parameter_type, bool p_is_vararg, const Vector<BSParser::DataType> &p_return_type, bool p_has_return) {
	Vector<String> argument_types;
	for (const BSParser::DataType &argument_type : p_argument_types) {
		argument_types.append(_datatype_signature_type_to_string(argument_type));
	}
	// A rich rest tail renders as the final `...Array[T]` entry, matching the source spelling.
	for (const BSParser::DataType &rest_type : p_rest_parameter_type) {
		argument_types.append("..." + _datatype_signature_type_to_string(rest_type));
	}
	// A gradual tail -- and every native or external vararg -- records only the arity bit and leaves the
	// rich slot empty, so without this the type renders identically to a fixed-arity one. `...Array` is
	// the surface spelling that re-parses to exactly this type, so it stays valid to write back into
	// source. A filled rich slot already rendered its element type above and must not print twice.
	if (p_is_vararg && p_rest_parameter_type.is_empty()) {
		argument_types.append("...Array");
	}

	PackedStringArray joinable_argument_types;
	for (const String &argument_type : argument_types) {
		joinable_argument_types.push_back(argument_type);
	}
	const String arguments = String(", ").join(joinable_argument_types);
	if (p_has_return) {
		const String return_type = p_return_type.is_empty() ? "void" : _datatype_signature_type_to_string(p_return_type[0], true);
		return vformat("[[%s], %s]", arguments, return_type);
	}
	return vformat("[[%s]]", arguments);
}

// Canonical sort key for a union member. Ordering members by a key that does not uniquely identify
// a type would leave distinct members in the order they were written -- the sort is not stable --
// so `A | B` and `B | A` could compare unequal despite denoting the same set. The key therefore
// appends whatever identifies the type beyond its rendered name: the declaring path of a script or
// class, the qualified name of a native type, enum, or tuple, and the declaration site of a type
// parameter.
static String _union_member_sort_key(const BSParser::DataType &p_member) {
	String key = itos(p_member.kind) + "\x1f" + p_member.to_string_diagnostic() + "\x1f";
	switch (p_member.kind) {
		case BSParser::DataType::BUILTIN:
			// D1: the carrier is the whole identity of a built-in member; upstream also appends the
			// width descriptor here (fs_parser_data_type.cpp:146 @ c9d5e35).
			key += itos(p_member.builtin_type);
			break;
		case BSParser::DataType::NATIVE:
		case BSParser::DataType::ENUM:
			key += String(p_member.native_type) + "\x1f" + String(p_member.enum_type) + "\x1f" +
					String(p_member.enum_case_name);
			break;
		case BSParser::DataType::SCRIPT:
			key += p_member.script_type.is_valid() ? p_member.script_type->get_path() : p_member.script_path;
			break;
		case BSParser::DataType::CLASS:
			key += p_member.class_type != nullptr ? p_member.class_type->fqcn : String();
			break;
		case BSParser::DataType::TUPLE:
			key += String(p_member.tuple_name) + "\x1f" + String(p_member.native_type) + "\x1f" +
					p_member.script_path;
			break;
		case BSParser::DataType::TYPE_PARAMETER:
			key += String(p_member.type_parameter_name) + "\x1f" + itos(p_member.type_parameter_scope) + "\x1f" +
					itos(p_member.type_parameter_index);
			break;
		case BSParser::DataType::UNION:
			// Normalization flattens nested unions, so a member is never itself a union.
		case BSParser::DataType::VARIANT:
		case BSParser::DataType::RESOLVING:
		case BSParser::DataType::UNRESOLVED:
			break;
	}
	return key;
}

// Orders union members deterministically, so set identity is positional identity and diagnostics
// never depend on the order the author happened to write.
struct _UnionMemberSort {
	bool operator()(const BSParser::DataType &p_left, const BSParser::DataType &p_right) const {
		return _union_member_sort_key(p_left) < _union_member_sort_key(p_right);
	}
};

BSParser::DataType BSParser::DataType::make_union(const Vector<DataType> &p_members) {
	Vector<DataType> flattened;
	bool nullable = false;
	for (const DataType &member : p_members) {
		nullable = nullable || member.is_nullable;
		if (member.kind == UNION) {
			// A nested union is already normalized, so its members are neither unions nor nullable.
			for (const DataType &inner_member : member.union_members) {
				flattened.push_back(inner_member);
			}
			continue;
		}
		DataType stored_member = member;
		stored_member.is_nullable = false;
		flattened.push_back(stored_member);
	}

	Vector<DataType> members;
	for (const DataType &member : flattened) {
		if (!members.has(member)) {
			members.push_back(member);
		}
	}
	members.sort_custom<_UnionMemberSort>();

	if (members.is_empty()) {
		return DataType();
	}
	if (members.size() == 1) {
		// A single alternative is not a set: it is that type, keeping its runtime typing, so a
		// one-member alias is indistinguishable from what it aliases.
		DataType only_member = members[0];
		only_member.is_nullable = only_member.is_nullable || nullable;
		return only_member;
	}

	DataType result;
	result.kind = UNION;
	result.type_source = ANNOTATED_EXPLICIT;
	result.union_members = members;
	result.is_nullable = nullable;
	return result;
}

// Renders a union with `p_member_to_string` applied to each alternative. A nullable union hangs its
// `?` off the first alternative rather than the whole type: nullability is hoisted into the union
// during normalization, there is no parenthesized type form to write `(A | B)?` with, and `A? | B`
// normalizes straight back to this same type, so the rendering is valid source.
static String _union_to_string(const BSParser::DataType &p_union, String (BSParser::DataType::*p_member_to_string)() const) {
	String result;
	for (int i = 0; i < p_union.union_members.size(); i++) {
		if (i > 0) {
			result += " | ";
		}
		result += (p_union.union_members[i].*p_member_to_string)();
		if (i == 0 && p_union.is_nullable) {
			result += "?";
		}
	}
	return result;
}

String BSParser::DataType::to_string() const {
	if (kind == UNION) {
		return _union_to_string(*this, &DataType::to_string);
	}
	if (is_type_handle_annotation) {
		DataType represented_type = *this;
		represented_type.is_meta_type = false;
		represented_type.is_type_handle_annotation = false;
		represented_type.is_constant = false;
		represented_type.is_nullable = false;
		const String nullable_suffix = is_nullable ? "?" : "";
		return vformat("Type[%s]%s", represented_type.to_string(), nullable_suffix);
	}

	if (is_coroutine) {
		// Coroutine[T] is a source-level skin over BSFunctionState. Render the phantom result
		// type from container_element_types[0] as the only choke point, so the underlying native
		// class name never leaks into hovers, errors, or completion. A void result (NIL) and an
		// absent result render with their source spellings.
		String element = "Variant";
		if (has_container_element_type(0)) {
			const DataType result_type = get_container_element_type(0);
			if (result_type.kind == BUILTIN && result_type.builtin_type == Variant::NIL) {
				element = "void";
			} else {
				element = result_type.to_string();
			}
		}
		const String nullable_suffix = is_nullable ? "?" : "";
		return vformat("Coroutine[%s]%s", element, nullable_suffix);
	}

	String result;
	bool valid_kind = true;
	switch (kind) {
		case VARIANT:
			result = "Variant";
			break;
		case BUILTIN:
			if (builtin_type == Variant::NIL) {
				return "null";
			}
			if (builtin_type == Variant::CALLABLE && has_explicit_method_signature) {
				const char *callable_name = signature_is_async ? "AsyncCallable" : "Callable";
				result = vformat("%s%s", callable_name, _method_signature_to_string(method_parameter_types, method_rest_parameter_type, (method_info.flags & METHOD_FLAG_VARARG) != 0, method_return_type, true));
				break;
			}
			if (builtin_type == Variant::CALLABLE && signature_is_async) {
				// A bare AsyncCallable (or a reference to an async method) carries the async marker
				// without an explicit signature; still render it as AsyncCallable for readability.
				result = "AsyncCallable";
				break;
			}
			if (builtin_type == Variant::SIGNAL && has_explicit_method_signature) {
				// A signal has no variadic spelling to round-trip to, so it renders no rest tail at all.
				result = vformat("Signal%s", _method_signature_to_string(method_parameter_types, Vector<DataType>(), false, method_return_type, false));
				break;
			}
			if (builtin_type == Variant::ARRAY && has_container_element_type(0)) {
				result = vformat("Array[%s]", get_container_element_type(0).to_string());
				break;
			}
			if (builtin_type == Variant::DICTIONARY && has_container_element_types()) {
				result = vformat("Dictionary[%s, %s]", get_container_element_type_or_variant(0).to_string(), get_container_element_type_or_variant(1).to_string());
				break;
			}
			// `to_string()` is the source spelling of a type: refactorings write its result straight back
			// into a script, so every name it produces has to be one the built-in registry can resolve.
			// D1 leaves the carrier's own name as the only spelling a built-in slot has.
			result = get_builtin_type_source_name(builtin_type);
			break;
		case NATIVE:
			if (is_meta_type) {
				// Upstream renders a native meta type as its `FSNativeClass` resource wrapper
				// (fs_parser_data_type.cpp:305 @ c9d5e35). BaristaScript has no such wrapper yet, and
				// inventing a class name that resolves to nothing would break `to_string()`'s contract
				// that every name it produces is one the reader can write back. The engine's own name
				// for a native class handle is what is left.
				result = "GDExtensionNativeClass";
				break;
			}
			result = String(native_type);
			break;
		case CLASS:
			if (class_type->identifier != nullptr) {
				result = String(class_type->identifier->name);
				break;
			}
			// A head class with no `class_name` has only its declaring file to be named by, and its
			// `fqcn` is that file's whole path.
			result = bs_diagnostic_type_name_for_path(class_type->fqcn);
			break;
		case SCRIPT: {
			if (is_meta_type) {
				result = script_type.is_valid() ? String(script_type->get_global_name()) : "";
				break;
			}
			String name = script_type.is_valid() ? script_type->get_name() : "";
			if (!name.is_empty()) {
				result = name;
				break;
			}
			if (!script_path.is_empty()) {
				result = bs_diagnostic_type_name_for_path(script_path);
				break;
			}
			result = String(native_type);
			break;
		}
		case ENUM: {
			// native_type contains either the native class defining the enum
			// or the fully qualified class name of the script defining the enum
			result = String(native_type).get_file(); // Remove path, keep filename
			break;
		}
		case TUPLE: {
			if (tuple_name != StringName()) {
				result = String(tuple_name);
				break;
			}
			// An unnamed tuple prints its structural shape, including field names when the type came
			// from a named declaration that was erased, e.g. `(x: float, y: float)`.
			String elements;
			for (int i = 0; i < container_element_types.size(); i++) {
				if (i > 0) {
					elements += ", ";
				}
				if (i < tuple_field_names.size() && tuple_field_names[i] != StringName()) {
					elements += String(tuple_field_names[i]) + ": ";
				}
				elements += container_element_types[i].to_string();
			}
			result = vformat("(%s)", elements);
			break;
		}
		case TYPE_PARAMETER:
			result = type_parameter_name == SNAME("@Self") ? "Self" : String(type_parameter_name);
			break;
		case RESOLVING:
		case UNRESOLVED:
			result = "<unresolved type>";
			break;
		default:
			valid_kind = false;
			break;
	}

	if (!valid_kind) {
		ERR_FAIL_V_MSG("<unresolved type>", "Kind set outside the enum range.");
	}

	// Render specialized type arguments, e.g. Box[int] or Box[String, float].
	if (kind != TYPE_PARAMETER && !type_arguments.is_empty()) {
		String arguments;
		for (int i = 0; i < type_arguments.size(); i++) {
			if (i > 0) {
				arguments += ", ";
			}
			arguments += type_arguments[i].to_string();
		}
		result += vformat("[%s]", arguments);
	}
	if (is_nullable && kind != VARIANT && !(kind == BUILTIN && builtin_type == Variant::NIL)) {
		result += "?";
	}
	return result;
}

String BSParser::DataType::to_string_diagnostic() const {
	if (kind == UNION) {
		// Each alternative renders with its own diagnostic spelling.
		return _union_to_string(*this, &DataType::to_string_diagnostic);
	}
	// D1 removes the one case that made a diagnostic spelling differ from a source spelling. Upstream
	// has native-only 8- and 16-bit descriptors with no source name (fs_parser_data_type.cpp:417-429
	// @ c9d5e35), which would otherwise render as their carrier and produce contrastive diagnostics
	// reading "should be uint but is uint". With one integer type there is nothing to disambiguate,
	// so the diagnostic spelling is the source spelling. The method is kept rather than collapsed
	// into `to_string()` because it is the name every diagnostic site already asks for, and a union
	// still routes its members through it.
	return to_string();
}

String BSParser::DataType::declaring_script_path() const {
	switch (kind) {
		case CLASS: {
			if (!script_path.is_empty()) {
				return script_path;
			}
			if (class_type != nullptr) {
				// A class fqcn's leading segment is the canonicalized declaring path; everything from
				// the first `::` onward is declared name.
				return class_type->fqcn.get_slice("::", 0);
			}
			return String();
		}
		case SCRIPT: {
			if (!script_path.is_empty()) {
				return script_path;
			}
			if (script_type.is_valid()) {
				return script_type->get_path();
			}
			return String();
		}
		case TUPLE:
		case ENUM:
			// A named tuple and a class enum both carry their declaring script directly; native and
			// global declarations leave it empty, which correctly names no file.
			return script_path;
		default:
			return String();
	}
}

// Renders the class chain that declares a named tuple, from the nominal identity stamped into
// `native_type`: the declaring class's fqcn followed by `"." + tuple_name`. An fqcn is the
// canonicalized declaring path for a head class and `path::Outer::Inner` for a nested one, so the
// chain is everything after the first `"::"` with the separators rendered as `"."`. A remainder that
// is empty or is the declaring script itself means the tuple is declared at the script's top level
// and has no class to name.
static String _named_tuple_declaring_class_chain(const BSParser::DataType &p_type) {
	const String native_type = String(p_type.native_type);
	const String suffix = "." + String(p_type.tuple_name);
	if (!native_type.ends_with(suffix)) {
		return String();
	}
	const String owner = native_type.substr(0, native_type.length() - suffix.length());
	if (owner.is_empty()) {
		return String();
	}
	const int separator = owner.find("::");
	if (separator >= 0) {
		return owner.substr(separator + 2).replace("::", ".");
	}
	if (!p_type.script_path.is_empty() && BaristaScript::is_canonically_equal_paths(owner, p_type.script_path)) {
		return String();
	}
	return owner;
}

static String _declaring_class_phrase(const String &p_article, const String &p_subject, const String &p_class_chain) {
	if (p_class_chain.is_empty()) {
		return vformat("%s %s is declared at the script's top level", p_article, p_subject);
	}
	return vformat(R"(%s %s is declared by class "%s")", p_article, p_subject, p_class_chain);
}

static bool _slot_is_self_type_parameter(const BSParser::DataType &p_type) {
	return p_type.kind == BSParser::DataType::TYPE_PARAMETER && p_type.type_parameter_name == SNAME("@Self");
}

// Finds the first parallel slot where exactly one side still reads `Self` while the other carries a
// concrete binding in its place: a named composite type (`tuple Pair(index: int, owner: Self)`)
// renders only its declared name, so a value whose `Self` positions were substituted at construction
// contrasts against the declaration as two identical spellings. Walks only lists whose sizes match —
// a size mismatch means the two sides differ structurally, which is not this clause's collision.
// `r_bound_slot` receives the concrete type standing where the other side reads `Self`, and
// `r_field_name` the innermost enclosing tuple field name when there is one.
static bool _find_divergent_self_binding(const BSParser::DataType &p_first, const BSParser::DataType &p_second,
		bool &r_first_side_is_self, BSParser::DataType &r_bound_slot, StringName &r_field_name) {
	const bool first_is_self = _slot_is_self_type_parameter(p_first);
	const bool second_is_self = _slot_is_self_type_parameter(p_second);
	if (first_is_self != second_is_self) {
		r_first_side_is_self = first_is_self;
		r_bound_slot = first_is_self ? p_second : p_first;
		return true;
	}
	if (first_is_self) {
		// Both sides read `Self`; there is no binding to contrast at this slot.
		return false;
	}
	if (p_first.kind != p_second.kind) {
		return false;
	}
	// Two distinct nominal declarations can share a displayed name, so pairing their slots
	// positionally would describe one declaration's slot with the other's type; the incompatibility
	// is then the outer identities, not any `Self` binding. Require the same nominal identity, per
	// kind as `operator==` defines it, before descending.
	switch (p_first.kind) {
		case BSParser::DataType::TUPLE: {
			// A named tuple's nominal identity is its class-qualified `native_type` plus declaring
			// `script_path`; the field layout must also line up for slots to pair positionally.
			if (p_first.tuple_name != p_second.tuple_name || p_first.tuple_field_names != p_second.tuple_field_names ||
					p_first.native_type != p_second.native_type || p_first.script_path != p_second.script_path) {
				return false;
			}
		} break;
		case BSParser::DataType::CLASS: {
			const bool same_class = p_first.class_type == p_second.class_type ||
					(p_first.class_type != nullptr && p_second.class_type != nullptr &&
							p_first.class_type->fqcn == p_second.class_type->fqcn);
			if (!same_class) {
				return false;
			}
		} break;
		case BSParser::DataType::SCRIPT: {
			if (p_first.script_type != p_second.script_type || p_first.script_path != p_second.script_path) {
				return false;
			}
		} break;
		case BSParser::DataType::NATIVE:
		case BSParser::DataType::ENUM: {
			if (p_first.native_type != p_second.native_type) {
				return false;
			}
		} break;
		default:
			break;
	}
	if (p_first.container_element_types.size() == p_second.container_element_types.size()) {
		for (int i = 0; i < p_first.container_element_types.size(); i++) {
			if (_find_divergent_self_binding(p_first.container_element_types[i], p_second.container_element_types[i],
						r_first_side_is_self, r_bound_slot, r_field_name)) {
				if (r_field_name == StringName() && p_first.kind == BSParser::DataType::TUPLE &&
						i < p_first.tuple_field_names.size()) {
					r_field_name = p_first.tuple_field_names[i];
				}
				return true;
			}
		}
	}
	if (p_first.type_arguments.size() == p_second.type_arguments.size()) {
		for (int i = 0; i < p_first.type_arguments.size(); i++) {
			if (_find_divergent_self_binding(p_first.type_arguments[i], p_second.type_arguments[i],
						r_first_side_is_self, r_bound_slot, r_field_name)) {
				return true;
			}
		}
	}
	if (p_first.method_parameter_types.size() == p_second.method_parameter_types.size()) {
		for (int i = 0; i < p_first.method_parameter_types.size(); i++) {
			if (_find_divergent_self_binding(p_first.method_parameter_types[i], p_second.method_parameter_types[i],
						r_first_side_is_self, r_bound_slot, r_field_name)) {
				return true;
			}
		}
	}
	if (p_first.method_return_type.size() == p_second.method_return_type.size()) {
		for (int i = 0; i < p_first.method_return_type.size(); i++) {
			if (_find_divergent_self_binding(p_first.method_return_type[i], p_second.method_return_type[i],
						r_first_side_is_self, r_bound_slot, r_field_name)) {
				return true;
			}
		}
	}
	if (p_first.method_rest_parameter_type.size() == p_second.method_rest_parameter_type.size()) {
		for (int i = 0; i < p_first.method_rest_parameter_type.size(); i++) {
			if (_find_divergent_self_binding(p_first.method_rest_parameter_type[i], p_second.method_rest_parameter_type[i],
						r_first_side_is_self, r_bound_slot, r_field_name)) {
				return true;
			}
		}
	}
	return false;
}

String BSParser::DataType::same_rendered_name_clause(const DataType &p_first, const String &p_first_subject, const DataType &p_second, const String &p_second_subject) {
	if (p_first.to_string_diagnostic() != p_second.to_string_diagnostic()) {
		return String();
	}
	const String first_path = p_first.declaring_script_path();
	const String second_path = p_second.declaring_script_path();
	if (!first_path.is_empty() && !second_path.is_empty() &&
			!BaristaScript::is_canonically_equal_paths(first_path, second_path)) {
		const String first_reference = bs_diagnostic_file_reference(first_path);
		const String second_reference = bs_diagnostic_file_reference(second_path);
		if (first_reference != second_reference) {
			return vformat(R"( The %s is declared in "%s"; the %s is declared in "%s".)",
					p_first_subject, first_reference, p_second_subject, second_reference);
		}
		// Both spellings fell back to the same basename (neither file localizes under a resource
		// root), so a file clause would repeat the colliding name twice and disambiguate nothing.
	}
	// Two classes of one script can declare same-named tuples: a named tuple renders its declared
	// name alone, so nothing in the spelling says which class declared it.
	if (p_first.kind == TUPLE && p_second.kind == TUPLE && p_first.tuple_name != StringName() &&
			p_second.tuple_name != StringName() &&
			(p_first.native_type != p_second.native_type ||
					!BaristaScript::is_canonically_equal_paths(p_first.script_path, p_second.script_path))) {
		const String first_chain = _named_tuple_declaring_class_chain(p_first);
		const String second_chain = _named_tuple_declaring_class_chain(p_second);
		if (first_chain != second_chain) {
			return vformat(" %s; %s.", _declaring_class_phrase("The", p_first_subject, first_chain),
					_declaring_class_phrase("the", p_second_subject, second_chain));
		}
		// Both sides name the same declaring class in different files whose references collapsed, so
		// naming the class would repeat the colliding name twice and disambiguate nothing.
	}
	// Same declaration on both sides: the spellings can still collide when they differ only in their
	// `Self` binding, because a named composite renders its declared name without its slots.
	bool first_side_is_self = false;
	DataType bound_slot;
	StringName field_name;
	if (_find_divergent_self_binding(p_first, p_second, first_side_is_self, bound_slot, field_name)) {
		const String &self_subject = first_side_is_self ? p_first_subject : p_second_subject;
		const String &bound_subject = first_side_is_self ? p_second_subject : p_first_subject;
		const String position = field_name == StringName() ? String("in its place") : vformat(R"(as field "%s")", field_name);
		return vformat(R"( The %s's "Self" stands for the exact receiver at this use; the %s has "%s" %s.)",
				self_subject, bound_subject, bound_slot.to_string_diagnostic(), position);
	}
	return String();
}

BSParser::DataType BSParser::DataType::substitute(const DataType &p_type, const HashMap<StringName, DataType> &p_bindings,
		bool p_mark_substituted_self) {
	if (p_type.kind == TYPE_PARAMETER) {
		const DataType *binding = p_bindings.getptr(p_type.type_parameter_name);
		if (binding != nullptr) {
			DataType result = *binding;
			if (p_type.is_type_handle_annotation) {
				result.is_meta_type = true;
				result.is_type_handle_annotation = true;
				result.is_pseudo_type = false;
				result.is_constant = p_type.is_constant;
				result.is_nullable = p_type.is_nullable;
			} else {
				result.is_nullable = result.is_nullable || p_type.is_nullable;
			}
			if (p_mark_substituted_self && p_type.type_parameter_name == SNAME("@Self")) {
				result.is_substituted_self = true;
			}
			return result;
		}
		// Unbound parameter: leave it intact so an outer scope can substitute it later, but specialize
		// its bound so a bound referencing a substituted parameter (e.g. `[U: T]` with `T := int`)
		// reflects the binding.
		DataType result = p_type;
		for (int i = 0; i < result.type_parameter_bound.size(); i++) {
			result.type_parameter_bound.write[i] = substitute(result.type_parameter_bound[i], p_bindings, p_mark_substituted_self);
		}
		return result;
	}

	DataType result = p_type;
	// Element and type-argument positions are constraint slots, so a binding solved from a
	// width-erased value type enters them unconstrained rather than carrying the wide descriptor the
	// carrier-only decode had to invent (see `as_container_slot_type`).
	for (int i = 0; i < result.container_element_types.size(); i++) {
		result.set_container_element_type(i, substitute(result.container_element_types[i], p_bindings, p_mark_substituted_self));
	}
	for (int i = 0; i < result.type_arguments.size(); i++) {
		result.set_type_argument(i, substitute(result.type_arguments[i], p_bindings, p_mark_substituted_self));
	}
	for (int i = 0; i < result.method_parameter_types.size(); i++) {
		result.method_parameter_types.write[i] = substitute(result.method_parameter_types[i], p_bindings, p_mark_substituted_self);
	}
	for (int i = 0; i < result.method_return_type.size(); i++) {
		result.method_return_type.write[i] = substitute(result.method_return_type[i], p_bindings, p_mark_substituted_self);
	}
	for (int i = 0; i < result.method_rest_parameter_type.size(); i++) {
		result.method_rest_parameter_type.write[i] = substitute(result.method_rest_parameter_type[i], p_bindings, p_mark_substituted_self);
	}
	// A tagged union nested inside a substituted slot carries its own payload schema, already written
	// in terms of the parameters bound here (`Result[T, String]` inside `enum Bundle[T]` holds
	// `Ok(value: T)`). Rewriting only its type arguments would leave the schema naming a parameter the
	// use site has already bound. A union that names itself is published as an identity shell with an
	// empty schema, so this terminates: each level is filled in, and specialized, when it is used.
	for (KeyValue<StringName, EnumCasePayload> &payload : result.enum_case_payloads) {
		for (int i = 0; i < payload.value.field_types.size(); i++) {
			payload.value.field_types.write[i] = substitute(payload.value.field_types[i], p_bindings, p_mark_substituted_self);
		}
	}
	if (result.kind == UNION) {
		// Substitution can make two alternatives equal (`T | int` with `T := int`) or introduce a
		// nested union, so the substituted set is re-normalized rather than written back in place.
		Vector<DataType> substituted_members;
		for (const DataType &member : result.union_members) {
			substituted_members.push_back(substitute(member, p_bindings, p_mark_substituted_self));
		}
		DataType substituted_union = make_union(substituted_members);
		substituted_union.is_nullable = substituted_union.is_nullable || result.is_nullable;
		return substituted_union;
	}
	return result;
}

// D-engine: the rich signature channel is deleted, not ported.
//
// Upstream encodes a Callable's or signal's full signature, and a `Coroutine[T]`'s result type, into
// a `PropertyInfo` under `PROPERTY_HINT_CALLABLE_TYPE` and `PROPERTY_HINT_COROUTINE_TYPE`
// (fs_parser_data_type.cpp:711-1000 @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6). Both hints are
// additions Foundry makes to its own engine fork; stock Godot 4.7 has neither enumerator, so on this
// host there is no channel to write into and no decoder to read one back.
//
// The whole encoder -- `_encode_signature_leaf_name`, `_encode_signature_type`,
// `_encode_signature_type_base`, `_encode_method_signature_suffix`, `_encode_coroutine_result_element`,
// `_encode_coroutine_container_element`, `_signature_type_handle_represented_type` and the two
// round-trip predicates `_enum_signature_leaf_round_trips` / `_signature_type_is_encodable` -- is
// therefore deleted rather than kept as unreachable code, exactly as D1 deletes the numeric tower.
// `to_property_info()` takes the branch upstream already takes when a slot cannot round-trip: the
// hint is omitted and the value crosses the boundary untyped (gradual). That is a narrowing of what
// crosses a *cross-script* boundary, never a wrong answer at one.
//
// Nothing else is lost with it: the deleted predicates were the only consumers of the three engine
// registries godot-cpp does not mirror (`CoreConstants::is_global_enum`,
// `ScriptServer::is_global_class_enum`, `ClassDB::is_class_exposed`), so no guessed answer stands in
// for any of them. `to_string()`'s own signature spelling (`_method_signature_to_string`) is a
// different renderer and is ported unchanged.

String bs_encode_type_handle_property_class_name(const StringName &p_represented_class) {
	return vformat("Type[%s]", p_represented_class);
}

bool bs_decode_type_handle_property_class_name(const String &p_class_name, String &r_represented_class) {
	if (!p_class_name.begins_with("Type[") || !p_class_name.ends_with("]")) {
		return false;
	}
	r_represented_class = p_class_name.substr(5, p_class_name.length() - 6);
	return !r_represented_class.is_empty();
}

static String _encode_type_handle_class_name(const StringName &p_represented_class) {
	return bs_encode_type_handle_property_class_name(p_represented_class == StringName() ? StringName("Object") : p_represented_class);
}

PropertyInfo BSParser::DataType::to_property_info(const String &p_name) const {
	PropertyInfo result;
	result.name = p_name;
	result.usage = PROPERTY_USAGE_NONE;

	if (!is_hard_type()) {
		result.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
		return result;
	}

	switch (kind) {
		case BUILTIN:
			// D1: a plain PropertyInfo transports the carrier, and under one integer type the carrier is
			// the whole of the slot's numeric type, so nothing is erased here. Upstream drops a width
			// descriptor at this boundary (fs_parser_data_type.cpp:1038-1044 @ c9d5e35).
			result.type = builtin_type;
			// A Callable's or signal's signature, and a coroutine array element's result type, would be
			// written here under hints stock Godot does not have; see the deletion note above. They
			// cross untyped instead.
			if (builtin_type == Variant::ARRAY && has_container_element_type(0)) {
				const DataType elem_type = get_container_element_type(0);
				switch (elem_type.kind) {
					case BUILTIN:
						result.hint = PROPERTY_HINT_ARRAY_TYPE;
						result.hint_string = Variant::get_type_name(elem_type.builtin_type);
						break;
					case NATIVE:
						result.hint = PROPERTY_HINT_ARRAY_TYPE;
						result.hint_string = elem_type.native_type;
						break;
					case SCRIPT:
						result.hint = PROPERTY_HINT_ARRAY_TYPE;
						if (elem_type.script_type.is_valid() && elem_type.script_type->get_global_name() != StringName()) {
							result.hint_string = elem_type.script_type->get_global_name();
						} else {
							result.hint_string = elem_type.native_type;
						}
						break;
					case CLASS:
						result.hint = PROPERTY_HINT_ARRAY_TYPE;
						if (elem_type.class_type != nullptr && elem_type.class_type->get_global_name() != StringName()) {
							result.hint_string = elem_type.class_type->get_global_name();
						} else {
							result.hint_string = elem_type.native_type;
						}
						break;
					case ENUM:
						result.hint = PROPERTY_HINT_ARRAY_TYPE;
						result.hint_string = String(elem_type.native_type).replace("::", ".");
						break;
					case TUPLE:
					case UNION:
					case TYPE_PARAMETER:
					case VARIANT:
					case RESOLVING:
					case UNRESOLVED:
						break;
				}
			} else if (builtin_type == Variant::DICTIONARY && has_container_element_types()) {
				const DataType key_type = get_container_element_type_or_variant(0);
				const DataType value_type = get_container_element_type_or_variant(1);
				if ((key_type.kind == VARIANT && value_type.kind == VARIANT) || key_type.kind == RESOLVING ||
						key_type.kind == UNRESOLVED || value_type.kind == RESOLVING || value_type.kind == UNRESOLVED) {
					break;
				}
				String key_hint, value_hint;
				switch (key_type.kind) {
					case BUILTIN:
						key_hint = Variant::get_type_name(key_type.builtin_type);
						break;
					case NATIVE:
						key_hint = key_type.native_type;
						break;
					case SCRIPT:
						if (key_type.script_type.is_valid() && key_type.script_type->get_global_name() != StringName()) {
							key_hint = key_type.script_type->get_global_name();
						} else {
							key_hint = key_type.native_type;
						}
						break;
					case CLASS:
						if (key_type.class_type != nullptr && key_type.class_type->get_global_name() != StringName()) {
							key_hint = key_type.class_type->get_global_name();
						} else {
							key_hint = key_type.native_type;
						}
						break;
					case ENUM:
						key_hint = String(key_type.native_type).replace("::", ".");
						break;
					default:
						key_hint = "Variant";
						break;
				}
				switch (value_type.kind) {
					case BUILTIN:
						value_hint = Variant::get_type_name(value_type.builtin_type);
						break;
					case NATIVE:
						value_hint = value_type.native_type;
						break;
					case SCRIPT:
						if (value_type.script_type.is_valid() && value_type.script_type->get_global_name() != StringName()) {
							value_hint = value_type.script_type->get_global_name();
						} else {
							value_hint = value_type.native_type;
						}
						break;
					case CLASS:
						if (value_type.class_type != nullptr && value_type.class_type->get_global_name() != StringName()) {
							value_hint = value_type.class_type->get_global_name();
						} else {
							value_hint = value_type.native_type;
						}
						break;
					case ENUM:
						value_hint = String(value_type.native_type).replace("::", ".");
						break;
					default:
						value_hint = "Variant";
						break;
				}
				// A `Coroutine[T]` key or value element would override the bare native name here with the
				// signature-grammar element hint; see the deletion note above. It keeps the native name.
				result.hint = PROPERTY_HINT_DICTIONARY_TYPE;
				result.hint_string = key_hint + ";" + value_hint;
			}
			break;
		case NATIVE:
			result.type = Variant::OBJECT;
			if (is_type_handle_annotation) {
				result.class_name = _encode_type_handle_class_name(native_type);
			} else if (is_meta_type) {
				result.class_name = StringName("GDExtensionNativeClass");
			} else {
				// A `Coroutine[T]` handle would carry its result type under `PROPERTY_HINT_COROUTINE_TYPE`
				// here; see the deletion note above. It crosses as its bare native class instead.
				result.class_name = native_type;
			}
			break;
		case SCRIPT:
			result.type = Variant::OBJECT;
			if (is_type_handle_annotation) {
				const StringName represented = script_type.is_valid() && script_type->get_global_name() != StringName()
						? script_type->get_global_name()
						: native_type;
				result.class_name = _encode_type_handle_class_name(represented);
			} else if (is_meta_type) {
				result.class_name = script_type.is_valid() ? script_type->get_global_name() : Script::get_class_static();
			} else if (script_type.is_valid() && script_type->get_global_name() != StringName()) {
				result.class_name = script_type->get_global_name();
			} else {
				result.class_name = native_type;
			}
			break;
		case CLASS:
			result.type = Variant::OBJECT;
			if (is_type_handle_annotation) {
				const StringName represented = class_type != nullptr && class_type->get_global_name() != StringName()
						? class_type->get_global_name()
						: native_type;
				result.class_name = _encode_type_handle_class_name(represented);
			} else if (is_meta_type) {
				result.class_name = BaristaScript::get_class_static();
			} else if (class_type != nullptr && class_type->get_global_name() != StringName()) {
				result.class_name = class_type->get_global_name();
			} else {
				result.class_name = native_type;
			}
			break;
		case ENUM:
			if (is_meta_type) {
				result.type = Variant::DICTIONARY;
			} else {
				result.type = Variant::INT;
				result.usage |= PROPERTY_USAGE_CLASS_IS_ENUM;
				result.class_name = String(native_type).replace("::", ".");
			}
			break;
		case TUPLE:
			// Tuples erase to a read-only Array at runtime; the precise shape only exists statically,
			// so no container hint is emitted.
			result.type = Variant::ARRAY;
			break;
		case UNION:
			// A multi-member union erases to untyped at runtime: a PropertyInfo transports exactly one
			// Variant::Type, which a set of alternatives cannot supply. The normalized member list
			// lives in the rich compiled Foundry metadata channel instead. A one-member alias never
			// reaches here, having collapsed to its member during normalization.
		case TYPE_PARAMETER:
			// Type parameters are erased to Variant outside the type checker.
		case VARIANT:
		case RESOLVING:
		case UNRESOLVED:
			result.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
			break;
	}

	if (is_nullable && result.type == Variant::OBJECT && result.class_name != StringName()) {
		result.class_name = String(result.class_name) + "?";
	}

	return result;
}

static Variant::Type _variant_type_to_typed_array_element_type(Variant::Type p_type) {
	switch (p_type) {
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
			return Variant::INT;
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
			return Variant::FLOAT;
		case Variant::PACKED_STRING_ARRAY:
			return Variant::STRING;
		case Variant::PACKED_VECTOR2_ARRAY:
			return Variant::VECTOR2;
		case Variant::PACKED_VECTOR3_ARRAY:
			return Variant::VECTOR3;
		case Variant::PACKED_COLOR_ARRAY:
			return Variant::COLOR;
		case Variant::PACKED_VECTOR4_ARRAY:
			return Variant::VECTOR4;
		default:
			return Variant::NIL;
	}
}

bool BSParser::DataType::is_typed_container_type() const {
	return kind == BSParser::DataType::BUILTIN && _variant_type_to_typed_array_element_type(builtin_type) != Variant::NIL;
}

BSParser::DataType BSParser::DataType::get_typed_container_type() const {
	BSParser::DataType type;
	type.kind = BSParser::DataType::BUILTIN;
	type.builtin_type = _variant_type_to_typed_array_element_type(builtin_type);
	return type;
}

int BSParser::DataType::get_tuple_field_index(const StringName &p_name) const {
	if (p_name == StringName()) {
		return -1;
	}
	for (int i = 0; i < tuple_field_names.size(); i++) {
		if (tuple_field_names[i] == p_name) {
			return i;
		}
	}
	return -1;
}

// Referencing hands out the value without re-validating it against this slot. D1: no NumericType /
// width/signedness alias checks -- the Variant carrier is the whole numeric type.
bool BSParser::DataType::can_reference(const BSParser::DataType &p_other) const {
	if (p_other.is_meta_type) {
		return false;
	}

	if (kind == UNION || p_other.kind == UNION) {
		return false;
	}

	if (kind == TUPLE || p_other.kind == TUPLE) {
		if (kind != TUPLE || p_other.kind != TUPLE) {
			return false;
		}
		if (container_element_types.size() != p_other.container_element_types.size()) {
			return false;
		}
		for (int i = 0; i < container_element_types.size(); i++) {
			if (!container_element_types[i].can_reference(p_other.container_element_types[i])) {
				return false;
			}
		}
		return true;
	}

	if (builtin_type != p_other.builtin_type) {
		return false;
	}

	if (builtin_type != Variant::OBJECT) {
		return true;
	}

	if (native_type == StringName()) {
		return true;
	} else if (p_other.native_type == StringName()) {
		return false;
	} else if (native_type != p_other.native_type && !ClassDB::is_parent_class(p_other.native_type, native_type)) {
		return false;
	}

	Ref<Script> script = script_type;
	if (kind == BSParser::DataType::CLASS && script.is_null()) {
		// #52: resolve CLASS handles through the staged parser/analyzer cache (Foundry
		// FSCache::get_shallow_script equivalent for ancestry checks).
		if (script_path.is_empty() && class_type == nullptr) {
			return false;
		}
		BSParser::ClassNode *self_class = class_type;
		if (self_class == nullptr) {
			Error err = OK;
			Ref<BSParserRef> ref = BSCache::get_parser(script_path, BSParserRef::INHERITANCE_SOLVED, err);
			if (ref.is_null() || err != OK || ref->get_parser() == nullptr) {
				ERR_PRINT(vformat(R"(Error while getting cache for script "%s".)", script_path));
				return false;
			}
			self_class = ref->get_parser()->get_tree();
		}
		if (self_class == nullptr) {
			return false;
		}
		if (p_other.kind == BSParser::DataType::CLASS) {
			Ref<Script> script_other = p_other.script_type;
			if (script_other.is_valid()) {
				return false;
			}
			BSParser::ClassNode *other_class = p_other.class_type;
			if (other_class == nullptr) {
				if (p_other.script_path.is_empty()) {
					return false;
				}
				Error other_err = OK;
				Ref<BSParserRef> other_ref = BSCache::get_parser(p_other.script_path, BSParserRef::INHERITANCE_SOLVED, other_err);
				if (other_ref.is_null() || other_err != OK || other_ref->get_parser() == nullptr) {
					ERR_PRINT(vformat(R"(Error while getting cache for script "%s".)", p_other.script_path));
					return false;
				}
				other_class = other_ref->get_parser()->get_tree();
			}
			if (other_class == nullptr) {
				return false;
			}
			// Slot `self` can hold value `other` when other is self or a descendant.
			BSParser::ClassNode *cursor = other_class;
			while (cursor != nullptr) {
				if (cursor == self_class || (!self_class->fqcn.is_empty() && cursor->fqcn == self_class->fqcn)) {
					return true;
				}
				if (cursor->base_type.kind != BSParser::DataType::CLASS) {
					break;
				}
				cursor = cursor->base_type.class_type;
			}
			return false;
		}
		return true;
	}

	Ref<Script> script_other = p_other.script_type;
	if (p_other.kind == BSParser::DataType::CLASS && script_other.is_null()) {
		if (p_other.script_path.is_empty()) {
			return false;
		}
		Error err = OK;
		Ref<BSParserRef> ref = BSCache::get_parser(p_other.script_path, BSParserRef::PARSED, err);
		if (ref.is_null() || err != OK) {
			ERR_PRINT(vformat(R"(Error while getting cache for script "%s".)", p_other.script_path));
			return false;
		}
		return false;
	}

	if (script.is_null()) {
		return true;
	} else if (script_other.is_null()) {
		return false;
	} else if (script != script_other) {
		// godot-cpp's Script binding does not expose inherits_script(); walk bases instead.
		Ref<Script> cursor = script_other;
		bool inherits = false;
		while (cursor.is_valid()) {
			if (cursor == script) {
				inherits = true;
				break;
			}
			cursor = cursor->get_base_script();
		}
		if (!inherits) {
			return false;
		}
	}

	return true;
}
} // namespace barista_script
