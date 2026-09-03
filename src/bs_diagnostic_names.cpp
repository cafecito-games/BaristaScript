/**************************************************************************/
/*  bs_diagnostic_names.cpp                                               */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_diagnostic_names.h"

namespace barista_script {

String bs_diagnostic_type_name_for_path(const String &p_fully_qualified_name) {
	if (p_fully_qualified_name.is_empty()) {
		return p_fully_qualified_name;
	}

	// Only the leading segment is a path. Everything from the first `::` onward is declared name --
	// inner classes, traits, and namespace segments alike -- and is kept verbatim.
	const int separator = p_fully_qualified_name.find("::");
	if (separator < 0) {
		return p_fully_qualified_name.get_file();
	}
	return p_fully_qualified_name.substr(0, separator).get_file() + p_fully_qualified_name.substr(separator);
}

String bs_diagnostic_file_reference(const String &p_path) {
	if (p_path.is_empty()) {
		return p_path;
	}

	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (settings == nullptr) {
		return p_path.get_file();
	}

	const String localized = settings->localize_path(p_path);
	if (!localized.begins_with("res://") && !localized.begins_with("user://")) {
		// A file outside every resource root has no location a reader of this project can act on,
		// and its absolute spelling depends on the machine that produced the diagnostic.
		return p_path.get_file();
	}
	return localized;
}

} // namespace barista_script
