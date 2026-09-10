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
#include "bs_cache.h"
#include "bs_global_class.h"
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

	/** Discovery only: an invalid private candidate must still block engine fallback. */
	static bool has_global_class_candidate(const StringName &p_name) {
		BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
		BSDeclarationRecord record;
		if (language != nullptr && language->get_declaration_index().try_get_by_qualified_name(String(p_name), record)) {
			return true;
		}
		if (BSBuiltinSources::has_global_name(String(p_name))) {
			return true;
		}
		for (const Dictionary &entry : _engine_global_class_list()) {
			if (String(entry.get("class", String())) == String(p_name)) {
				return true;
			}
		}
		return false;
	}

	/** Resolve the selected provider without repairing metadata or falling through a rejected hint. */
	static bool resolve_global_class(const String &p_name, BSDeclarationRecord &r_record) {
		if (p_name.is_empty())
			return false;
		BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
		if (language != nullptr && language->get_declaration_index().try_get_by_qualified_name(p_name, r_record)) {
			return language->is_declaration_current(r_record);
		}
		String builtin_path;
		if (BSBuiltinSources::path_for_global_name(p_name, builtin_path)) {
			r_record.path = builtin_path;
			const auto read = BSCache::read_source_code(builtin_path);
			if (read.error != OK)
				return false;
			r_record = BSDeclarationIndex::record_from_global_class(builtin_path, read.source, bs_resolve_global_class_from_source(read.source, builtin_path));
			return r_record.qualified_name == p_name;
		}
		for (const Dictionary &entry : _engine_global_class_list()) {
			if (String(entry.get("class", String())) != p_name)
				continue;
			const String path = entry.get("path", String());
			r_record.path = path;
			if (path.get_extension() == "barista") {
				// Private metadata for this path also shadows obsolete engine identities.
				if (language != nullptr && language->get_declaration_index().try_get_by_path(path, r_record)) {
					return r_record.qualified_name == p_name && language->is_declaration_current(r_record);
				}
				const auto read = BSCache::read_source_code(path);
				if (read.error != OK)
					return false;
				const auto head = bs_resolve_global_class_from_source(read.source, path);
				if (!head.declarations_parsed || head.name != p_name || head.kind != BSDeclarationKind::CLASS)
					return false;
				r_record = BSDeclarationIndex::record_from_global_class(path, read.source, head);
			} else {
				r_record = BSDeclarationRecord();
				r_record.qualified_name = p_name;
				r_record.path = path;
				r_record.base_type = entry.get("base", String());
				r_record.kind = BSDeclarationKind::CLASS;
			}
			return true;
		}
		return false;
	}

	static bool is_global_class(const StringName &p_name) {
		BSDeclarationRecord record;
		return resolve_global_class(String(p_name), record);
	}

	static bool is_global_class_enum(const StringName &p_name) {
		BSDeclarationRecord record;
		return resolve_global_class(String(p_name), record) && record.kind == BSDeclarationKind::ENUM;
	}

	static bool is_builtin_global_class(const StringName &p_name) {
		return BSBuiltinSources::has_global_name(String(p_name));
	}

	static String get_global_class_path(const StringName &p_name) {
		BSDeclarationRecord record;
		return resolve_global_class(String(p_name), record) ? record.path : String();
	}

	static StringName get_global_class_native_base(const StringName &p_name) {
		BSDeclarationRecord record;
		return resolve_global_class(String(p_name), record) ? StringName(record.base_type) : StringName();
	}

	static void get_global_class_list(List<StringName> *r_classes) {
		ERR_FAIL_NULL(r_classes);
		HashSet<StringName> seen;
		const auto add = [&](const StringName &p_name) {
			if (p_name == StringName() || seen.has(p_name))
				return;
			seen.insert(p_name);
			if (is_global_class(p_name))
				r_classes->push_back(p_name);
		};
		BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
		if (language != nullptr) {
			for (const BSDeclarationRecord &record : language->get_declaration_index().get_records())
				add(record.qualified_name);
		}
		for (const Dictionary &entry : _engine_global_class_list())
			add(StringName(entry.get("class", String())));
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
