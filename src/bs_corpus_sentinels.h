/**************************************************************************/
/*  bs_corpus_sentinels.h                                                 */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>

namespace barista_script {

/**
 * The two strings the conformance corpus is written in, defined once.
 *
 * `BS_TEST_OK` is the success expectation of every one of the 340 imported `.out` files and the
 * value `project/tests/corpus_harness.gd` compares a clean run against; `BS_CORPUS` prefixes the
 * one summary line CI pins by anchored pattern. Both therefore cross three languages -- C++, the
 * GDScript harness, and the Python importer and CI validator -- and a second spelling of either in
 * any of them is a silent corpus-wide false pass rather than a visible break. They live here, in
 * the compiled extension, because that is the only definition all three can be made to read:
 * GDScript calls the bound accessors below, and the Python tooling extracts the literals from this
 * header and fails when it cannot find them.
 *
 * Nothing here is corpus *policy*. Discovery, pairing and comparison stay in the harness; this
 * class holds two literals and no behaviour.
 */
class BaristaScriptCorpusSentinels final : public godot::RefCounted {
	GDCLASS(BaristaScriptCorpusSentinels, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	/** The expectation text of a case the front-end accepts without a diagnostic. */
	static constexpr const char *SUCCESS_SENTINEL = "BS_TEST_OK";

	/** The prefix of the harness's single machine-readable summary line. */
	static constexpr const char *SUMMARY_PREFIX = "BS_CORPUS";

	static godot::String success_sentinel();
	static godot::String summary_prefix();
};

} // namespace barista_script
