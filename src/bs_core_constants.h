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

namespace barista_script {

/**
 * GDExtension cannot enumerate CoreConstants or utility signatures. Both build paths embed the
 * complete corresponding sections from the pinned extension_api-4-7.json, emitted by the engine's
 * core/extension/extension_api_dump.cpp:499-619. Unknown names remain absent.
 */
class BSCoreConstants {
	struct Constant {
		StringName enumeration;
		int64_t value = 0;
		bool bitfield = false;
	};
	struct Metadata {
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
			for (const PropertyInfo &argument : p_arguments) {
				info.arguments.push_back(argument);
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
