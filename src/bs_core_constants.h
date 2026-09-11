/**************************************************************************/
/*  bs_core_constants.h                                                   */
/*                                                                        */
/*  Pinned engine global metadata adapter for constants and utilities.   */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_platform.h"
#include "bs_type_info.h"

#include <initializer_list>
#include <limits>

namespace barista_script {

/**
 * GDExtension cannot enumerate CoreConstants or utility signatures. Both build paths embed the
 * complete corresponding sections from the pinned extension_api-4-7.json, emitted by the engine's
 * core/extension/extension_api_dump.cpp:499-619. Unknown names remain absent.
 */
class BSCoreConstants {
public:
	struct BuiltinMethod {
		MethodInfo info;
		int64_t hash = 0;
		Vector<int64_t> compatibility_hashes;
	};
	struct BuiltinConstructor {
		int index = 0;
		MethodInfo info;
	};
	struct BuiltinConstant {
		StringName name;
		Variant::Type type = Variant::NIL;
		Variant (*get_value)() = nullptr;
	};
	struct BuiltinEnum {
		StringName name;
		Vector<StringName> names;
		Vector<int64_t> values;
	};
	struct Builtin {
		StringName name;
		uint32_t present_sections = 0;
		Vector<BuiltinConstructor> constructors;
		Vector<BuiltinMethod> methods;
		Vector<PropertyInfo> members;
		Vector<BuiltinConstant> constants;
		Vector<BuiltinEnum> enums;
	};

private:
#ifdef BARISTA_TESTS
	friend struct BSCoreConstantsTestAccess;
#endif
	struct Constant {
		StringName enumeration;
		int64_t value = 0;
		bool bitfield = false;
	};
	struct Metadata {
		HashMap<StringName, Builtin> builtins;
		Vector<StringName> builtin_order;
		void add_builtin(const StringName &name, uint32_t sections) {
			Builtin value;
			value.name = name;
			value.present_sections = sections;
			builtins.insert(name, value);
			builtin_order.push_back(name);
		}
		void add_constructor(const StringName &owner, int index, std::initializer_list<PropertyInfo> arguments, std::initializer_list<Variant> defaults) {
			BuiltinConstructor value;
			value.index = index;
			value.info.name = owner;
			value.info.return_val = property(owner, "");
			for (const auto &a : arguments)
				value.info.arguments.push_back(a);
			for (const auto &d : defaults)
				value.info.default_arguments.push_back(d);
			builtins[owner].constructors.push_back(value);
		}
		void add_builtin_method(const StringName &owner, const StringName &name, const PropertyInfo &result, uint32_t flags, int64_t hash,
				std::initializer_list<int64_t> compatibility, std::initializer_list<PropertyInfo> arguments, std::initializer_list<Variant> defaults) {
			BuiltinMethod value;
			value.hash = hash;
			value.info.name = name;
			value.info.return_val = result;
			value.info.flags |= flags;
			for (int64_t h : compatibility)
				value.compatibility_hashes.push_back(h);
			for (const auto &a : arguments)
				value.info.arguments.push_back(a);
			for (const auto &d : defaults)
				value.info.default_arguments.push_back(d);
			builtins[owner].methods.push_back(value);
		}
		void add_builtin_constant(const StringName &owner, const StringName &name, Variant::Type type, Variant (*factory)()) {
			BuiltinConstant c;
			c.name = name;
			c.type = type;
			c.get_value = factory;
			builtins[owner].constants.push_back(c);
		}
		void add_builtin_enum(const StringName &owner, const StringName &name, std::initializer_list<StringName> names, std::initializer_list<int64_t> values) {
			BuiltinEnum e;
			e.name = name;
			for (const auto &n : names)
				e.names.push_back(n);
			for (int64_t v : values)
				e.values.push_back(v);
			builtins[owner].enums.push_back(e);
		}
		Vector<Constant> constants;
		HashMap<StringName, int> indices;
		HashMap<StringName, HashMap<StringName, int64_t>> enums;
		HashMap<StringName, MethodInfo> utilities;
		HashMap<StringName, int64_t> utility_hashes;

		void add_constant(const StringName &p_name, int64_t p_value, const StringName &p_enum, bool p_bitfield) {
			Constant value;
			value.enumeration = p_enum;
			value.value = p_value;
			value.bitfield = p_bitfield;
			indices.insert(p_name, constants.size());
			constants.push_back(value);
			if (p_enum != StringName()) {
				enums[p_enum].insert(p_name, p_value);
			}
		}
		void add_utility(const StringName &p_name, const PropertyInfo &p_return, bool p_vararg,
				int64_t p_hash, std::initializer_list<PropertyInfo> p_arguments) {
			MethodInfo info;
			info.name = p_name;
			info.return_val = p_return;
			if (p_vararg) {
				info.flags |= METHOD_FLAG_VARARG;
			}
			// Engine vararg API entries describe a repeated placeholder, not a fixed minimum.
			// Foundry info_from_utility_func preserves fixed slots only for non-varargs.
			if (!p_vararg) {
				for (const PropertyInfo &argument : p_arguments) {
					info.arguments.push_back(argument);
				}
			}
			utilities.insert(p_name, info);
			utility_hashes.insert(p_name, p_hash);
		}
	};
	static PropertyInfo property(const String &p_type, const StringName &p_name) {
		if (p_type.is_empty()) {
			return PropertyInfo(Variant::NIL, p_name);
		}
		if (p_type == "Variant") {
			return PropertyInfo(Variant::NIL, p_name, PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT);
		}
		for (int i = 0; i < Variant::VARIANT_MAX; i++) {
			if (Variant::get_type_name(Variant::Type(i)) == p_type) {
				return PropertyInfo(Variant::Type(i), p_name);
			}
		}
		CRASH_NOW_MSG("Generated utility signature names an unknown engine carrier.");
		return PropertyInfo();
	}

#include "gen/bs_global_api.gen.h"

	static const Metadata &metadata() {
		// Intentionally retained for process lifetime so engine-backed data is not destroyed after API teardown.
		static const Metadata *data = []() {
			Metadata *result = memnew(Metadata);
			populate(*result);
			return result;
		}();
		return *data;
	}

public:
	static const Vector<StringName> &get_builtin_order() { return metadata().builtin_order; }
	static const Builtin *get_builtin(Variant::Type type) { return type >= 0 && type < Variant::VARIANT_MAX ? metadata().builtins.getptr(Variant::get_type_name(type)) : nullptr; }
	static const BuiltinMethod *get_builtin_method(Variant::Type type, const StringName &name) {
		const Builtin *b = get_builtin(type);
		if (b)
			for (const auto &m : b->methods)
				if (m.info.name == name)
					return &m;
		return nullptr;
	}
	static const PropertyInfo *get_builtin_member(Variant::Type type, const StringName &name) {
		const Builtin *b = get_builtin(type);
		if (b)
			for (const auto &m : b->members)
				if (m.name == name)
					return &m;
		return nullptr;
	}
	static const BuiltinConstant *get_builtin_constant(Variant::Type type, const StringName &name) {
		const Builtin *b = get_builtin(type);
		if (b)
			for (const auto &c : b->constants)
				if (c.name == name)
					return &c;
		return nullptr;
	}
	static const BuiltinEnum *get_builtin_enum(Variant::Type type, const StringName &name) {
		const Builtin *b = get_builtin(type);
		if (b)
			for (const auto &e : b->enums)
				if (e.name == name)
					return &e;
		return nullptr;
	}
	static const BuiltinEnum *get_builtin_enum_value(Variant::Type type, const StringName &name, int64_t &value) {
		const Builtin *b = get_builtin(type);
		if (b)
			for (const auto &e : b->enums)
				for (int i = 0; i < e.names.size(); ++i)
					if (e.names[i] == name) {
						value = e.values[i];
						return &e;
					}
		return nullptr;
	}
	static bool is_global_enum(const StringName &p_name) { return metadata().enums.has(p_name); }
	static bool is_global_constant(const StringName &p_name) { return metadata().indices.has(p_name); }
	static int get_global_constant_index(const StringName &p_name) {
		const int *index = metadata().indices.getptr(p_name);
		return index != nullptr ? *index : -1;
	}
	static StringName get_global_constant_enum(int p_index) {
		ERR_FAIL_INDEX_V(p_index, metadata().constants.size(), StringName());
		return metadata().constants[p_index].enumeration;
	}
	static int64_t get_global_constant_value(int p_index) {
		ERR_FAIL_INDEX_V(p_index, metadata().constants.size(), 0);
		return metadata().constants[p_index].value;
	}
	static void get_enum_values(const StringName &p_name, HashMap<StringName, int64_t> *r_values) {
		const HashMap<StringName, int64_t> *values = metadata().enums.getptr(p_name);
		if (values != nullptr && r_values != nullptr) {
			*r_values = *values;
		}
	}
	static bool get_utility_function(const StringName &p_name, MethodInfo &r_info) {
		const MethodInfo *info = metadata().utilities.getptr(p_name);
		if (info == nullptr) {
			return false;
		}
		r_info = *info;
		return true;
	}
};

} // namespace barista_script
