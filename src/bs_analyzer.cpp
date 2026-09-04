/**************************************************************************/
/*  bs_analyzer.cpp                                                       */
/*                                                                        */
/*  Narrow M3 analyzer seam for staged cache raises (#27). Full port: #43.*/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"

namespace barista_script {

String BSAnalyzer::bootstrap_allowed_dependency_root;

Error BSAnalyzer::resolve_inheritance() {
	// #43 seam: inheritance resolution. Lifecycle advances after a successful parse.
	ERR_FAIL_COND_V(parser == nullptr, ERR_BUG);
	return OK;
}

Error BSAnalyzer::resolve_interface() {
	ERR_FAIL_COND_V(parser == nullptr, ERR_BUG);
	return OK;
}

Error BSAnalyzer::resolve_body() {
	ERR_FAIL_COND_V(parser == nullptr, ERR_BUG);
	return OK;
}

bool BSAnalyzer::is_bootstrap_path_allowed(const String &p_path) {
	if (bootstrap_allowed_dependency_root.is_empty()) {
		return true;
	}
	const String root = bootstrap_allowed_dependency_root.simplify_path();
	const String path = p_path.simplify_path();
	if (path == root) {
		return true;
	}
	const String prefix = root.ends_with("/") ? root : root + "/";
	return path.begins_with(prefix);
}

void BSAnalyzer::set_bootstrap_allowed_dependency_root(const String &p_root) {
	bootstrap_allowed_dependency_root = p_root.simplify_path();
}

String BSAnalyzer::get_bootstrap_allowed_dependency_root() {
	return bootstrap_allowed_dependency_root;
}

} // namespace barista_script
