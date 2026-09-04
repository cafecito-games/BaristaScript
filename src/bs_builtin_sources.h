/**************************************************************************/
/*  bs_builtin_sources.h                                                  */
/*                                                                        */
/*  Hard fork of Foundry fs_builtin_sources.h @ c9d5e35. Reserved         */
/*  barista://builtin/ virtual sources for shipped types.                 */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_platform.h"

namespace barista_script {

class BSBuiltinSources {
	static HashMap<String, String> &sources();
	static HashMap<String, String> &global_name_to_path();
	static HashSet<String> &enum_names();

public:
	static const char *PATH_PREFIX;
	static const char *EXPORTED_BYTECODE_PREFIX;

	static bool is_builtin_path(const String &p_path);
	static String get_exported_bytecode_path(const String &p_path);
	static void register_source(const String &p_path, const String &p_source);
	static void unregister_source(const String &p_path);
	static bool get_source(const String &p_path, String &r_source);
	static void get_registered_paths(List<String> *r_paths);
	static void clear();

	/** Analyzer ScriptServer adapter helpers. */
	static void register_global_name(const String &p_name, const String &p_path, bool p_is_enum);
	static bool has_global_name(const String &p_name);
	static bool has_enum_name(const String &p_name);
	static bool path_for_global_name(const String &p_name, String &r_path);
	static void append_global_names(List<StringName> &r_classes, HashSet<StringName> &r_seen);
};

} // namespace barista_script
