/**************************************************************************/
/*  bs_native_db.h                                                        */
/*                                                                        */
/*  ClassDB shapes the analyzer needs that godot-cpp spells differently.  */
/*  Engine MethodBind* for native methods is unavailable; metadata is D1- */
/*  deleted (always NONE). Method/signal info comes from dictionary lists.*/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_platform.h"
#include "bs_type_info.h"

#include <godot_cpp/core/method_bind.hpp>

namespace barista_script {

class BSNativeDB {
public:
	static StringName get_property_getter(const StringName &p_class, const StringName &p_property) {
		return ClassDB::class_get_property_getter(p_class, p_property);
	}

	static StringName get_property_setter(const StringName &p_class, const StringName &p_property) {
		// godot-cpp exposes getter; setter is recovered from the property list when needed.
		const TypedArray<Dictionary> props = ClassDB::class_get_property_list(p_class, false);
		for (int i = 0; i < props.size(); i++) {
			const Dictionary entry = props[i];
			if (StringName(entry.get("name", String())) == p_property) {
				return StringName(entry.get("setter", String()));
			}
		}
		return StringName();
	}

	/** Engine MethodBinds are not exposed; always nullptr (callers tolerate null). */
	static MethodBind *get_method(const StringName &, const StringName &) {
		return nullptr;
	}

	static bool get_method_info(const StringName &p_class, const StringName &p_method, MethodInfo *r_info) {
		ERR_FAIL_NULL_V(r_info, false);
		if (!ClassDB::class_has_method(p_class, p_method, false)) {
			return false;
		}
		const TypedArray<Dictionary> methods = ClassDB::class_get_method_list(p_class, false);
		for (int i = 0; i < methods.size(); i++) {
			const Dictionary entry = methods[i];
			if (StringName(entry.get("name", String())) != p_method) {
				continue;
			}
			*r_info = MethodInfo::from_dict(entry);
			return true;
		}
		return false;
	}

	static bool get_signal(const StringName &p_class, const StringName &p_signal, MethodInfo *r_info) {
		ERR_FAIL_NULL_V(r_info, false);
		const Dictionary entry = ClassDB::class_get_signal(p_class, p_signal);
		if (entry.is_empty()) {
			return false;
		}
		*r_info = MethodInfo::from_dict(entry);
		return true;
	}
};

/** ResourceLoader::load is a singleton method in godot-cpp. */
class BSResourceLoader {
public:
	static Ref<Resource> load(const String &p_path, const String &p_type_hint = String(), ResourceLoader::CacheMode p_cache_mode = ResourceLoader::CACHE_MODE_REUSE) {
		ResourceLoader *loader = ResourceLoader::get_singleton();
		if (loader == nullptr) {
			return Ref<Resource>();
		}
		return loader->load(p_path, p_type_hint, p_cache_mode);
	}
};

} // namespace barista_script
