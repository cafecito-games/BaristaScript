/**************************************************************************/
/*  bs_script_server.h                                                    */
/*                                                                        */
/*  Fail-closed ScriptServer adapter for the analyzer port. Godot core's  */
/*  ScriptServer is engine-internal; a GDExtension reads flat global      */
/*  classes via ProjectSettings and BaristaScript-private kinds via the   */
/*  declaration index (D5/D6/D7).                                         */
/*  Hard fork seam for Foundry ScriptServer::* @ c9d5e35.                 */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "barista_script_language.h"
#include "bs_builtin_sources.h"
#include "bs_platform.h"

namespace barista_script {

/**
 * Drop-in spelling for Foundry's `ScriptServer::` calls inside the analyzer.
 *
 * Flat GDScript/native globals come from `ProjectSettings::get_global_class_list()`.
 * BaristaScript-private declaration kinds (trait/enum/tuple/generic) come from
 * `BSDeclarationIndex` and are never invented into Godot's engine class cache (D5/D6).
 */
class ScriptServer {
public:
	static uint64_t &cache_version_storage() {
		static uint64_t version = 1;
		return version;
	}

	static void bump_global_class_cache_version() {
		cache_version_storage()++;
	}

	static uint64_t get_global_class_cache_version() {
		return cache_version_storage();
	}

	static TypedArray<Dictionary> _engine_global_class_list() {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		if (settings == nullptr) {
			return TypedArray<Dictionary>();
		}
		return settings->get_global_class_list();
	}

	static bool is_global_class(const StringName &p_name) {
		const String name = String(p_name);
		if (name.is_empty()) {
			return false;
		}
		if (BSBuiltinSources::has_global_name(name)) {
			return true;
		}
		BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
		if (language != nullptr) {
			BSDeclarationRecord rec;
			if (language->try_resolve_declaration(name, rec)) {
				return true;
			}
		}
		const TypedArray<Dictionary> classes = _engine_global_class_list();
		for (int i = 0; i < classes.size(); i++) {
			const Dictionary entry = classes[i];
			if (String(entry.get("class", String())) == name) {
				return true;
			}
		}
		return false;
	}

	static bool is_global_class_enum(const StringName &p_name) {
		const String name = String(p_name);
		if (BSBuiltinSources::has_enum_name(name)) {
			return true;
		}
		BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
		if (language != nullptr) {
			BSDeclarationRecord rec;
			if (language->try_resolve_declaration(name, rec)) {
				return rec.kind == BSDeclarationKind::ENUM;
			}
		}
		return false;
	}

	static bool is_builtin_global_class(const StringName &p_name) {
		return BSBuiltinSources::has_global_name(String(p_name));
	}

	static String get_global_class_path(const StringName &p_name) {
		const String name = String(p_name);
		String builtin_path;
		if (BSBuiltinSources::path_for_global_name(name, builtin_path)) {
			return builtin_path;
		}
		BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
		if (language != nullptr) {
			BSDeclarationRecord rec;
			if (language->try_resolve_declaration(name, rec)) {
				return rec.path;
			}
		}
		const TypedArray<Dictionary> classes = _engine_global_class_list();
		for (int i = 0; i < classes.size(); i++) {
			const Dictionary entry = classes[i];
			if (String(entry.get("class", String())) == name) {
				return String(entry.get("path", String()));
			}
		}
		return String();
	}

	static StringName get_global_class_native_base(const StringName &p_name) {
		const String path = get_global_class_path(p_name);
		if (path.is_empty()) {
			return StringName();
		}
		const TypedArray<Dictionary> classes = _engine_global_class_list();
		for (int i = 0; i < classes.size(); i++) {
			const Dictionary entry = classes[i];
			if (String(entry.get("path", String())) == path) {
				return StringName(entry.get("base", String()));
			}
		}
		BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
		if (language != nullptr) {
			BSDeclarationRecord rec;
			if (language->try_resolve_declaration(String(p_name), rec)) {
				return StringName(rec.base_type);
			}
		}
		return StringName();
	}

	static void get_global_class_list(List<StringName> *r_classes) {
		ERR_FAIL_NULL(r_classes);
		HashSet<StringName> seen;
		const TypedArray<Dictionary> classes = _engine_global_class_list();
		for (int i = 0; i < classes.size(); i++) {
			const Dictionary entry = classes[i];
			const StringName name = StringName(entry.get("class", String()));
			if (name == StringName() || seen.has(name)) {
				continue;
			}
			seen.insert(name);
			r_classes->push_back(name);
		}
		BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
		if (language != nullptr) {
			const Vector<BSDeclarationRecord> records = language->get_declaration_index().get_records();
			for (int i = 0; i < records.size(); i++) {
				const String qualified = records[i].qualified_name;
				if (qualified.is_empty()) {
					continue;
				}
				const StringName name = StringName(qualified);
				if (seen.has(name)) {
					continue;
				}
				BSDeclarationRecord validated;
				if (!language->try_resolve_declaration(qualified, validated)) {
					continue;
				}
				seen.insert(name);
				r_classes->push_back(name);
			}
		}
		BSBuiltinSources::append_global_names(*r_classes, seen);
	}

	static void get_global_class_name_parts(const StringName &p_name, String *r_identifier, String *r_namespace) {
		const String name = String(p_name);
		const int dot = name.rfind(".");
		if (dot < 0) {
			if (r_identifier != nullptr) {
				*r_identifier = name;
			}
			if (r_namespace != nullptr) {
				*r_namespace = String();
			}
			return;
		}
		if (r_namespace != nullptr) {
			*r_namespace = name.substr(0, dot);
		}
		if (r_identifier != nullptr) {
			*r_identifier = name.substr(dot + 1);
		}
	}
};

} // namespace barista_script
