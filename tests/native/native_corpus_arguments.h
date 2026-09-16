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

// The `--corpus-root=` / `--corpus-case=` values selecting the single corpus case this
// process evaluates. Both are empty for an ordinary, non-corpus run.

const godot::String &corpus_root();
const godot::String &corpus_case();

/** Record the parsed pair. The runner calls this once from `_process`, after argument
 *  validation and before any case runs. */
void set_corpus_arguments(const godot::String &p_root, const godot::String &p_case);

} //namespace barista_script::native_tests
