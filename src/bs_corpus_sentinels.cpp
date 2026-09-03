/**************************************************************************/
/*  bs_corpus_sentinels.cpp                                               */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_corpus_sentinels.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace barista_script {

void BaristaScriptCorpusSentinels::_bind_methods() {
	ClassDB::bind_static_method("BaristaScriptCorpusSentinels", D_METHOD("success_sentinel"),
			&BaristaScriptCorpusSentinels::success_sentinel);
	ClassDB::bind_static_method("BaristaScriptCorpusSentinels", D_METHOD("summary_prefix"),
			&BaristaScriptCorpusSentinels::summary_prefix);
}

String BaristaScriptCorpusSentinels::success_sentinel() {
	return String(SUCCESS_SENTINEL);
}

String BaristaScriptCorpusSentinels::summary_prefix() {
	return String(SUMMARY_PREFIX);
}

} // namespace barista_script
