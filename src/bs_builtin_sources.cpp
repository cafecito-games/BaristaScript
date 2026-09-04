/**************************************************************************/
/*  bs_builtin_sources.cpp                                                */
/*                                                                        */
/*  Hard fork of Foundry fs_builtin_sources.cpp @ c9d5e35.                */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_builtin_sources.h"

namespace barista_script {

const char *BSBuiltinSources::PATH_PREFIX = "barista://builtin/";
const char *BSBuiltinSources::EXPORTED_BYTECODE_PREFIX = "res://.barista/builtin/";

HashMap<String, String> &BSBuiltinSources::sources() {
	static HashMap<String, String> *map = nullptr;
	if (map == nullptr) {
		map = memnew(HashMap<String, String>);
	}
	return *map;
}

HashMap<String, String> &BSBuiltinSources::global_name_to_path() {
	static HashMap<String, String> *map = nullptr;
	if (map == nullptr) {
		map = memnew(HashMap<String, String>);
	}
	return *map;
}

HashSet<String> &BSBuiltinSources::enum_names() {
	static HashSet<String> *set = nullptr;
	if (set == nullptr) {
		set = memnew(HashSet<String>);
	}
	return *set;
}

bool BSBuiltinSources::is_builtin_path(const String &p_path) {
	return p_path.begins_with(PATH_PREFIX);
}

String BSBuiltinSources::get_exported_bytecode_path(const String &p_path) {
	if (!is_builtin_path(p_path)) {
		return String();
	}
	const String relative_path = p_path.substr(String(PATH_PREFIX).length());
	if (relative_path.get_extension().to_lower() != "barista") {
		return String();
	}
	const String relative_basename = relative_path.get_basename();
	if (relative_basename.is_empty()) {
		return String();
	}
	return String(EXPORTED_BYTECODE_PREFIX) + relative_basename + ".bsb";
}

void BSBuiltinSources::register_source(const String &p_path, const String &p_source) {
	ERR_FAIL_COND_MSG(!is_builtin_path(p_path),
			vformat("Builtin source path must start with \"%s\", got \"%s\".", PATH_PREFIX, p_path));
	sources()[p_path] = p_source;
}

void BSBuiltinSources::unregister_source(const String &p_path) {
	sources().erase(p_path);
}

bool BSBuiltinSources::get_source(const String &p_path, String &r_source) {
	const String *found = sources().getptr(p_path);
	if (found == nullptr) {
		r_source = String();
		return false;
	}
	r_source = *found;
	return true;
}

void BSBuiltinSources::get_registered_paths(List<String> *r_paths) {
	ERR_FAIL_NULL(r_paths);
	for (const KeyValue<String, String> &entry : sources()) {
		r_paths->push_back(entry.key);
	}
	r_paths->sort();
}

void BSBuiltinSources::clear() {
	sources().clear();
	global_name_to_path().clear();
	enum_names().clear();
}

void BSBuiltinSources::register_global_name(const String &p_name, const String &p_path, bool p_is_enum) {
	global_name_to_path()[p_name] = p_path;
	if (p_is_enum) {
		enum_names().insert(p_name);
	}
}

bool BSBuiltinSources::has_global_name(const String &p_name) {
	return global_name_to_path().has(p_name);
}

bool BSBuiltinSources::has_enum_name(const String &p_name) {
	return enum_names().has(p_name);
}

bool BSBuiltinSources::path_for_global_name(const String &p_name, String &r_path) {
	const String *found = global_name_to_path().getptr(p_name);
	if (found == nullptr) {
		r_path = String();
		return false;
	}
	r_path = *found;
	return true;
}

void BSBuiltinSources::append_global_names(List<StringName> &r_classes, HashSet<StringName> &r_seen) {
	for (const KeyValue<String, String> &entry : global_name_to_path()) {
		const StringName name = StringName(entry.key);
		if (r_seen.has(name)) {
			continue;
		}
		r_seen.insert(name);
		r_classes.push_back(name);
	}
}

} // namespace barista_script
