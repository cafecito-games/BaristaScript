/**************************************************************************/
/*  bs_runtime_type.cpp                                                   */
/*                                                                        */
/*  See bs_runtime_type.h.                                                */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_runtime_type.h"

#include "barista_script.h"
#include "bs_parser.h"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace barista_script {

namespace {

uint32_t mix_hash(uint32_t p_hash, uint32_t p_value) {
	return (p_hash ^ p_value) * 16777619U;
}

bool construct_variant(Variant::Type p_type, const Variant &p_source, Variant &r_value) {
	GDExtensionCallError error;
	error.error = GDEXTENSION_CALL_OK;
	const Variant *arguments[1] = { &p_source };
	Variant converted;
	gdextension_interface::variant_construct((GDExtensionVariantType)p_type, &converted,
			reinterpret_cast<const GDExtensionConstVariantPtr *>(arguments), 1, &error);
	if (error.error != GDEXTENSION_CALL_OK) {
		return false;
	}
	r_value = converted;
	return true;
}

Ref<Script> resolve_script_type(const BSParser::DataType &p_type, BaristaScript *p_owner) {
	if (p_type.script_type.is_valid()) {
		return p_type.script_type;
	}
	if (p_type.kind == BSParser::DataType::CLASS && p_type.class_type != nullptr && p_owner != nullptr) {
		if (BaristaScript *compiled = p_owner->get_compiled_class(p_type.class_type->fqcn)) {
			return Ref<Script>(compiled);
		}
	}
	const String path = p_type.declaring_script_path();
	if (p_owner != nullptr && (path.is_empty() || BaristaScript::is_canonically_equal_paths(path, p_owner->get_path()))) {
		return Ref<Script>(p_owner);
	}
	if (!path.is_empty()) {
		return ResourceLoader::get_singleton()->load(path, "Script");
	}
	return Ref<Script>();
}

Ref<Script> script_from_handle(const Variant &p_value) {
	if (p_value.get_type() == Variant::OBJECT) {
		return Ref<Script>(Object::cast_to<Script>(p_value.get_validated_object()));
	}
	if (p_value.get_type() == Variant::INT) {
		const ObjectID id((uint64_t)(int64_t)p_value);
		return Ref<Script>(Object::cast_to<Script>(ObjectDB::get_instance(id)));
	}
	return Ref<Script>();
}

} // namespace

BSRuntimeType BSRuntimeType::builtin(Variant::Type p_type, bool p_nullable) {
	BSRuntimeType type;
	type.kind = BUILTIN;
	type.builtin_type = p_type;
	type.is_nullable = p_nullable;
	return type;
}

BSRuntimeType BSRuntimeType::native(const StringName &p_class, bool p_nullable) {
	BSRuntimeType type;
	type.kind = NATIVE;
	type.builtin_type = Variant::OBJECT;
	type.native_type = p_class;
	type.is_nullable = p_nullable;
	return type;
}

BSRuntimeType BSRuntimeType::script(const Ref<Script> &p_script, bool p_nullable) {
	BSRuntimeType type;
	type.kind = SCRIPT;
	type.builtin_type = Variant::OBJECT;
	if (p_script.is_valid()) {
		type.script_id = p_script->get_instance_id();
		type.script_path = p_script->get_path();
	}
	type.is_nullable = p_nullable;
	return type;
}

bool BSRuntimeType::from_data_type(const BSParser::DataType &p_type, BaristaScript *p_owner,
		BSRuntimeType &r_type, String &r_error) {
	r_type = BSRuntimeType();
	if (!p_type.type_arguments.is_empty()) {
		r_error = vformat(R"(Runtime type "%s" requires M5 specialization support.)", p_type.to_string());
		return false;
	}
	r_type.is_nullable = p_type.is_nullable;
	r_type.is_type_handle = p_type.is_type_handle_annotation || p_type.is_meta_type;
	switch (p_type.kind) {
		case BSParser::DataType::VARIANT:
		case BSParser::DataType::RESOLVING:
		case BSParser::DataType::UNRESOLVED:
			r_type.kind = VARIANT;
			return true;
		case BSParser::DataType::BUILTIN:
			r_type.kind = BUILTIN;
			r_type.builtin_type = p_type.builtin_type;
			if ((p_type.builtin_type == Variant::ARRAY || p_type.builtin_type == Variant::DICTIONARY) &&
					p_type.has_container_element_types()) {
				for (const BSParser::DataType &element : p_type.container_element_types) {
					BSRuntimeType lowered;
					if (!from_data_type(element, p_owner, lowered, r_error)) {
						return false;
					}
					r_type.container_element_types.push_back(lowered);
				}
			}
			return true;
		case BSParser::DataType::NATIVE:
			r_type.kind = NATIVE;
			r_type.builtin_type = Variant::OBJECT;
			r_type.native_type = p_type.native_type;
			return true;
		case BSParser::DataType::SCRIPT:
		case BSParser::DataType::CLASS:
			r_type.kind = SCRIPT;
			r_type.builtin_type = Variant::OBJECT;
			r_type.native_type = p_type.native_type;
			{
				const Ref<Script> script_ref = resolve_script_type(p_type, p_owner);
				if (script_ref.is_valid()) {
					r_type.script_id = script_ref->get_instance_id();
					r_type.script_path = script_ref->get_path();
				}
			}
			if (r_type.script_id.is_null()) {
				r_error = vformat(R"(Runtime script type "%s" has no stable script identity.)", p_type.to_string());
				return false;
			}
			return true;
		case BSParser::DataType::ENUM:
			if (p_type.is_tagged_union) {
				r_error = vformat(R"(Runtime tagged-union type "%s" belongs to #247.)", p_type.to_string());
				return false;
			}
			r_type.kind = BUILTIN;
			r_type.builtin_type = Variant::INT;
			return true;
		case BSParser::DataType::TUPLE:
			r_error = vformat(R"(Runtime tuple type "%s" belongs to #247.)", p_type.to_string());
			return false;
		case BSParser::DataType::UNION:
			r_error = vformat(R"(Runtime union type "%s" belongs to #247.)", p_type.to_string());
			return false;
		case BSParser::DataType::TYPE_PARAMETER:
			if (p_type.type_parameter_name == SNAME("@Self") && p_type.type_parameter_bound.size() == 1) {
				BSParser::DataType bound = p_type.type_parameter_bound[0];
				bound.is_nullable = bound.is_nullable || p_type.is_nullable;
				bound.is_meta_type = p_type.is_meta_type;
				bound.is_type_handle_annotation = p_type.is_type_handle_annotation;
				return from_data_type(bound, p_owner, r_type, r_error);
			}
			r_error = vformat(R"(Runtime type parameter "%s" requires M5 specialization support.)", p_type.to_string());
			return false;
	}
	r_error = vformat(R"(Runtime type "%s" is unresolved.)", p_type.to_string());
	return false;
}

bool BSRuntimeType::is_typed_array() const {
	return kind == BUILTIN && builtin_type == Variant::ARRAY && container_element_types.size() == 1;
}

bool BSRuntimeType::is_typed_dictionary() const {
	return kind == BUILTIN && builtin_type == Variant::DICTIONARY && container_element_types.size() == 2;
}

String BSRuntimeType::get_name() const {
	String name;
	switch (kind) {
		case VARIANT:
			name = "Variant";
			break;
		case BUILTIN:
			name = Variant::get_type_name(builtin_type);
			if (is_typed_array()) {
				name += "[" + container_element_types[0].get_name() + "]";
			} else if (is_typed_dictionary()) {
				name += "[" + container_element_types[0].get_name() + ", " + container_element_types[1].get_name() + "]";
			}
			break;
		case NATIVE:
			name = native_type;
			break;
		case SCRIPT:
			name = script_path.is_empty() ? String("<script>") : script_path;
			break;
		case TUPLE:
			name = "tuple";
			break;
		case UNION:
			for (int i = 0; i < union_alternatives.size(); i++) {
				name += (i == 0 ? String() : String(" | ")) + union_alternatives[i].get_name();
			}
			break;
	}
	return name + (is_nullable ? "?" : "");
}

bool BSRuntimeType::container_metadata(Variant::Type &r_builtin, StringName &r_class, Variant &r_script) const {
	r_builtin = Variant::NIL;
	r_class = StringName();
	r_script = Variant();
	if (is_nullable && kind == BUILTIN) {
		// Stock typed containers cannot encode a nullable builtin element.
		return false;
	}
	switch (kind) {
		case VARIANT:
			return false;
		case BUILTIN:
			r_builtin = builtin_type;
			return builtin_type != Variant::NIL;
		case NATIVE:
			if (is_type_handle) {
				r_builtin = Variant::STRING_NAME;
				return true;
			}
			r_builtin = Variant::OBJECT;
			r_class = native_type;
			return true;
		case SCRIPT:
			if (is_type_handle) {
				// A weak ObjectID is the private class-handle carrier. Pooling a Script Ref in an owning
				// function's constants would recreate the cycle this descriptor deliberately avoids.
				r_builtin = Variant::INT;
				return true;
			}
			r_builtin = Variant::OBJECT;
			r_class = native_type;
			r_script = get_script();
			return r_script.get_type() == Variant::OBJECT && r_script.get_validated_object() != nullptr;
		case TUPLE:
		case UNION:
			return false;
	}
	return false;
}

bool BSRuntimeType::matches_container_metadata(Variant::Type p_builtin, const StringName &p_class,
		const Variant &p_script) const {
	Variant::Type expected_builtin;
	StringName expected_class;
	Variant expected_script;
	if (!container_metadata(expected_builtin, expected_class, expected_script) || p_builtin != expected_builtin) {
		return false;
	}
	if (is_type_handle) {
		return p_class == expected_class;
	}
	if (kind == NATIVE) {
		return p_class == expected_class;
	}
	if (kind == SCRIPT) {
		Object *object = p_script.get_type() == Variant::OBJECT ? p_script.get_validated_object() : nullptr;
		Script *script = object != nullptr ? Object::cast_to<Script>(object) : nullptr;
		return script != nullptr && script->get_instance_id() == uint64_t(script_id);
	}
	return true;
}

bool BSRuntimeType::accepts(const Variant &p_value, bool p_allow_implicit_conversion, bool p_narrowing) const {
	if (p_value.get_type() == Variant::NIL) {
		if (kind == VARIANT) {
			return true;
		}
		if (is_nullable) {
			return true;
		}
		return !p_narrowing && (kind == NATIVE || kind == SCRIPT);
	}
	if (is_type_handle) {
		if (kind == NATIVE) {
			return p_value.get_type() == Variant::STRING_NAME &&
					ClassDB::is_parent_class(StringName(p_value), native_type);
		}
		if (kind == SCRIPT) {
			Ref<Script> candidate = script_from_handle(p_value);
			while (candidate.is_valid()) {
				if (candidate->get_instance_id() == uint64_t(script_id)) {
					return true;
				}
				candidate = candidate->get_base_script();
			}
			return false;
		}
		return false;
	}
	switch (kind) {
		case VARIANT:
			return true;
		case BUILTIN: {
			if (p_value.get_type() != builtin_type) {
				return p_allow_implicit_conversion && Variant::can_convert_strict(p_value.get_type(), builtin_type);
			}
			if (is_typed_array()) {
				const Array array = p_value;
				Variant::Type element_builtin;
				StringName element_class;
				Variant element_script;
				if (!container_element_types[0].container_metadata(element_builtin, element_class, element_script)) {
					if (array.is_typed()) {
						return false;
					}
					for (const Variant &element : array) {
						if (!container_element_types[0].accepts(element, false, p_narrowing)) {
							return false;
						}
					}
					return true;
				}
				if (!array.is_typed() || !container_element_types[0].matches_container_metadata((Variant::Type)array.get_typed_builtin(), array.get_typed_class_name(), array.get_typed_script())) {
					return false;
				}
				for (const Variant &element : array) {
					if (!container_element_types[0].accepts(element, false, p_narrowing)) {
						return false;
					}
				}
				return true;
			}
			if (is_typed_dictionary()) {
				const Dictionary dictionary = p_value;
				Variant::Type key_builtin;
				StringName key_class;
				Variant key_script;
				Variant::Type value_builtin;
				StringName value_class;
				Variant value_script;
				const bool typed_key = container_element_types[0].container_metadata(key_builtin, key_class, key_script);
				const bool typed_value = container_element_types[1].container_metadata(value_builtin, value_class, value_script);
				if (!typed_key || !typed_value) {
					if (dictionary.is_typed()) {
						return false;
					}
					for (const Variant &key : dictionary.keys()) {
						if (!container_element_types[0].accepts(key, false, p_narrowing) ||
								!container_element_types[1].accepts(dictionary[key], false, p_narrowing)) {
							return false;
						}
					}
					return true;
				}
				if (!dictionary.is_typed() ||
						!container_element_types[0].matches_container_metadata(
								(Variant::Type)dictionary.get_typed_key_builtin(),
								dictionary.get_typed_key_class_name(), dictionary.get_typed_key_script()) ||
						!container_element_types[1].matches_container_metadata(
								(Variant::Type)dictionary.get_typed_value_builtin(),
								dictionary.get_typed_value_class_name(), dictionary.get_typed_value_script())) {
					return false;
				}
				for (const Variant &key : dictionary.keys()) {
					if (!container_element_types[0].accepts(key, false, p_narrowing) ||
							!container_element_types[1].accepts(dictionary[key], false, p_narrowing)) {
						return false;
					}
				}
				return true;
			}
			return true;
		}
		case NATIVE: {
			if (p_value.get_type() != Variant::OBJECT) {
				return false;
			}
			Object *object = p_value.get_validated_object();
			return object != nullptr && ClassDB::is_parent_class(object->get_class(), native_type);
		}
		case SCRIPT: {
			if (p_value.get_type() != Variant::OBJECT || script_id.is_null()) {
				return false;
			}
			Object *object = p_value.get_validated_object();
			if (object == nullptr) {
				return false;
			}
			Ref<Script> candidate = object->get_script();
			while (candidate.is_valid()) {
				if (candidate->get_instance_id() == uint64_t(script_id)) {
					return true;
				}
				candidate = candidate->get_base_script();
			}
			return false;
		}
		case TUPLE:
			if (p_value.get_type() != Variant::ARRAY || Array(p_value).size() != container_element_types.size()) {
				return false;
			}
			for (int i = 0; i < container_element_types.size(); i++) {
				if (!container_element_types[i].accepts(Array(p_value)[i], false, p_narrowing)) {
					return false;
				}
			}
			return true;
		case UNION:
			for (const BSRuntimeType &alternative : union_alternatives) {
				if (alternative.accepts(p_value, p_allow_implicit_conversion, p_narrowing)) {
					return true;
				}
			}
			return false;
	}
	return false;
}

bool BSRuntimeType::convert(const Variant &p_source, Variant &r_value, String &r_error,
		bool p_allow_erased_container) const {
	if (p_source.get_type() == Variant::NIL && (is_nullable || kind == NATIVE || kind == SCRIPT)) {
		r_value = Variant();
		return true;
	}
	if (kind == VARIANT) {
		r_value = p_source;
		return true;
	}
	if (is_type_handle && kind == SCRIPT && accepts(p_source)) {
		const Ref<Script> candidate = script_from_handle(p_source);
		r_value = (int64_t)candidate->get_instance_id();
		return true;
	}
	if (is_typed_array()) {
		Variant array_value = p_source;
		bool carrier_converted = false;
		if (p_source.get_type() != Variant::ARRAY &&
				Variant::can_convert_strict(p_source.get_type(), Variant::ARRAY)) {
			carrier_converted = construct_variant(Variant::ARRAY, p_source, array_value);
		}
		if (array_value.get_type() != Variant::ARRAY) {
			r_error = vformat(R"(Cannot assign a value of type "%s" to a slot of type "%s".)",
					Variant::get_type_name(p_source.get_type()), get_name());
			return false;
		}
		const Array source = array_value;
		if (accepts(array_value)) {
			r_value = array_value;
			return true;
		}
		Variant::Type target_element_builtin;
		StringName target_element_class;
		Variant target_element_script;
		const bool target_has_metadata = container_element_types[0].container_metadata(
				target_element_builtin, target_element_class, target_element_script);
		// A descriptor such as `int?` cannot be represented in Godot's Array metadata. Retype a
		// statically compatible typed source into an erased-but-validated result so the wider slot
		// still permits values (notably null) that the source metadata would reject.
		if ((source.is_typed() && target_has_metadata) ||
				(!source.is_typed() && !p_allow_erased_container && !carrier_converted)) {
			r_error = vformat(R"(Cannot assign an array of type "%s" to a slot of type "%s".)",
					source.is_typed() ? String("typed Array") : String("Array"), get_name());
			return false;
		}
		Vector<Variant> converted;
		converted.resize(source.size());
		for (int i = 0; i < source.size(); i++) {
			String element_error;
			if (!container_element_types[0].convert(source[i], converted.write[i], element_error, true)) {
				r_error = vformat(R"(Cannot assign element %d of type "%s" to "%s": %s)", i,
						Variant::get_type_name(source[i].get_type()), get_name(), element_error);
				return false;
			}
		}
		Array result;
		if (target_has_metadata) {
			result.set_typed(target_element_builtin, target_element_class, target_element_script);
		}
		for (const Variant &element : converted) {
			result.push_back(element);
		}
		r_value = result;
		return true;
	}
	if (is_typed_dictionary()) {
		if (p_source.get_type() != Variant::DICTIONARY) {
			r_error = vformat(R"(Cannot assign a value of type "%s" to a slot of type "%s".)",
					Variant::get_type_name(p_source.get_type()), get_name());
			return false;
		}
		const Dictionary source = p_source;
		if (accepts(p_source)) {
			r_value = p_source;
			return true;
		}
		Variant::Type key_builtin;
		StringName key_class;
		Variant key_script;
		Variant::Type value_builtin;
		StringName value_class;
		Variant value_script;
		const bool typed_key = container_element_types[0].container_metadata(key_builtin, key_class, key_script);
		const bool typed_value = container_element_types[1].container_metadata(value_builtin, value_class, value_script);
		const bool target_has_metadata = typed_key && typed_value;
		if ((source.is_typed() && target_has_metadata) || (!source.is_typed() && !p_allow_erased_container)) {
			r_error = vformat(R"(Cannot assign a dictionary of type "%s" to a slot of type "%s".)",
					source.is_typed() ? String("typed Dictionary") : String("Dictionary"), get_name());
			return false;
		}
		const Array keys = source.keys();
		Vector<Variant> converted_keys;
		Vector<Variant> converted_values;
		converted_keys.resize(keys.size());
		converted_values.resize(keys.size());
		for (int i = 0; i < keys.size(); i++) {
			String key_error;
			String value_error;
			if (!container_element_types[0].convert(keys[i], converted_keys.write[i], key_error, true)) {
				r_error = vformat(R"(Cannot assign dictionary key of type "%s" to "%s": %s)",
						Variant::get_type_name(keys[i].get_type()), get_name(), key_error);
				return false;
			}
			if (!container_element_types[1].convert(source[keys[i]], converted_values.write[i], value_error, true)) {
				r_error = vformat(R"(Cannot assign dictionary value of type "%s" to "%s": %s)",
						Variant::get_type_name(source[keys[i]].get_type()), get_name(), value_error);
				return false;
			}
		}
		Dictionary result;
		if (target_has_metadata) {
			result.set_typed(key_builtin, key_class, key_script, value_builtin, value_class, value_script);
		}
		for (int i = 0; i < converted_keys.size(); i++) {
			if (result.has(converted_keys[i])) {
				r_error = vformat(R"(Converting dictionary key %d to "%s" would duplicate an earlier key.)",
						i, container_element_types[0].get_name());
				return false;
			}
			result[converted_keys[i]] = converted_values[i];
		}
		r_value = result;
		return true;
	}
	if (accepts(p_source)) {
		r_value = p_source;
		return true;
	}
	if (kind == NATIVE && !is_type_handle) {
		if (p_source.get_type() != Variant::OBJECT) {
			r_error = "Trying to assign value of type '" + Variant::get_type_name(p_source.get_type()) +
					"' to a variable of type '" + String(native_type) + "'.";
			return false;
		}
		Object *object = p_source.get_validated_object();
		if (object == nullptr) {
			r_error = "Trying to assign invalid previously freed instance.";
			return false;
		}
		r_error = "Trying to assign value of type '" + object->get_class() +
				"' to a variable of type '" + String(native_type) + "'.";
		return false;
	}
	if (kind == BUILTIN && Variant::can_convert_strict(p_source.get_type(), builtin_type) &&
			construct_variant(builtin_type, p_source, r_value)) {
		return true;
	}
	r_error = vformat(R"(Cannot assign a value of type "%s" to a slot of type "%s".)",
			Variant::get_type_name(p_source.get_type()), get_name());
	return false;
}

bool BSRuntimeType::cast(const Variant &p_source, Variant &r_value, String &r_error) const {
	if (accepts(p_source, false, true)) {
		if (is_type_handle && kind == SCRIPT) {
			const Ref<Script> candidate = script_from_handle(p_source);
			r_value = (int64_t)candidate->get_instance_id();
		} else {
			r_value = p_source;
		}
		return true;
	}
	if (kind == BUILTIN && p_source.get_type() != Variant::NIL &&
			(is_typed_array() || is_typed_dictionary()
							? convert(p_source, r_value, r_error, true)
							: construct_variant(builtin_type, p_source, r_value))) {
		return true;
	}
	if (is_nullable) {
		r_value = Variant();
		return true;
	}
	r_error = vformat(R"(Cannot cast a value of type "%s" to non-nullable "%s".)",
			Variant::get_type_name(p_source.get_type()), get_name());
	return false;
}

uint32_t BSRuntimeType::hash() const {
	uint32_t value = 2166136261U;
	value = mix_hash(value, (uint32_t)kind);
	value = mix_hash(value, (uint32_t)builtin_type);
	value = mix_hash(value, native_type.hash());
	const uint64_t id = script_id;
	value = mix_hash(value, (uint32_t)id);
	value = mix_hash(value, (uint32_t)(id >> 32));
	value = mix_hash(value, is_nullable ? 1U : 0U);
	value = mix_hash(value, is_type_handle ? 1U : 0U);
	for (const BSRuntimeType &element : container_element_types) {
		value = mix_hash(value, element.hash());
	}
	for (const BSRuntimeType &alternative : union_alternatives) {
		value = mix_hash(value, alternative.hash());
	}
	return value;
}

bool BSRuntimeType::operator==(const BSRuntimeType &p_other) const {
	return kind == p_other.kind && builtin_type == p_other.builtin_type && native_type == p_other.native_type &&
			script_id == p_other.script_id && is_nullable == p_other.is_nullable &&
			is_type_handle == p_other.is_type_handle &&
			container_element_types == p_other.container_element_types &&
			union_alternatives == p_other.union_alternatives;
}

Ref<Script> BSRuntimeType::get_script() const {
	if (script_id.is_null()) {
		return Ref<Script>();
	}
	return Ref<Script>(Object::cast_to<Script>(ObjectDB::get_instance(script_id)));
}

} // namespace barista_script
