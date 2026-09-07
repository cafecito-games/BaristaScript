/**************************************************************************/
/*  bs_analyzer_finalization.cpp                                         */
/*                                                                        */
/*  Hard fork of Foundry `modules/foundry_script/                         */
/*  fs_analyzer_finalization.cpp` @                                      */
/*  c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6. FS* -> BS*; engine contact */
/*  through bs_platform.h. Carries pending-warning application and the   */
/*  depended-parser inheritance finalization loop.                       */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"

#include "bs_cache.h"

namespace barista_script {

Error BSAnalyzer::run_phase_finalize() {
#ifdef DEBUG_ENABLED
	parser->apply_pending_warnings();
#endif
	for (const KeyValue<String, Ref<BSParserRef>> &dependency : parser->get_depended_parsers()) {
		if (dependency.value.is_null()) {
			return ERR_PARSE_ERROR;
		}
		// Foundry finalizes every dependency's inheritance surface even when the use site only
		// needed it parsed (for example a standalone preload expression).
		dependency.value->raise_status(BSParserRef::INHERITANCE_SOLVED);
	}
	mark_phase(AnalyzerPhase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

} // namespace barista_script
