/**************************************************************************/
/*  bs_analyzer.h                                                         */
/*                                                                        */
/*  Narrow M3 analyzer seam for staged cache raises (#27). Full type-model*/
/*  / analyzer port is issue #43; this header only advances lifecycle.    */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_parser.h"

namespace barista_script {

/**
 * Staged-analysis seam used by `BSParserRef::raise_status`.
 *
 * Issue #43 replaces the resolve_* bodies with the real type model. Until then the stages succeed
 * after a successful parse so cache/dependency lifecycle tests can raise monotonically without
 * porting the analyzer corpus. Bootstrap-root filtering lives here because Foundry spells it on
 * `FSAnalyzer` (`fs_analyzer.cpp:9535` @ c9d5e35).
 */
class BSAnalyzer {
	BSParser *parser = nullptr;
	static String bootstrap_allowed_dependency_root;

public:
	explicit BSAnalyzer(BSParser *p_parser) :
			parser(p_parser) {}

	Error resolve_inheritance();
	Error resolve_interface();
	Error resolve_body();

	static bool is_bootstrap_path_allowed(const String &p_path);
	/** Test/editor seam: empty root admits every path (Foundry default). */
	static void set_bootstrap_allowed_dependency_root(const String &p_root);
	static String get_bootstrap_allowed_dependency_root();
};

} // namespace barista_script
