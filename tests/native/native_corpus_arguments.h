/**************************************************************************/
/*  native_corpus_arguments.h                                             */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include <godot_cpp/variant/string.hpp>

namespace barista_script::native_tests {

/** The two `--corpus-order=` values. Ascending is the discovery order; reverse is the exact
 *  opposite of it, and exists so a whole-corpus run can prove its outcomes do not depend on
 *  the order the cases ran in. */
inline constexpr const char *CORPUS_ORDER_ASCENDING = "ascending";
inline constexpr const char *CORPUS_ORDER_REVERSE = "reverse";

// The `--corpus-root=` / `--corpus-case=` values selecting the corpus work this process does.
// Both are empty for an ordinary, non-corpus run. A root without a case selects the whole
// corpus; a case without a root is refused by the runner.

const godot::String &corpus_root();
const godot::String &corpus_case();

/** The requested case order. Empty outside a whole-corpus run. */
const godot::String &corpus_order();

/** Whether this process was asked to evaluate the whole corpus rather than one named case. */
bool whole_corpus_selected();

/** Record the parsed arguments. The runner calls this once from `_process`, after argument
 *  validation and before any case runs. */
void set_corpus_arguments(const godot::String &p_root, const godot::String &p_case,
		const godot::String &p_order);

} //namespace barista_script::native_tests
