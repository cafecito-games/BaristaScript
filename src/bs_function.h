/**************************************************************************/
/*  bs_function.h                                                         */
/*                                                                        */
/*  Analyzer-facing opaque BSFunction forward (M4 owns the runtime body). */
/*  Conformance registry stores borrowed BSFunction* witnesses; M3 never  */
/*  dereferences them. Provenance: fs_function.h @ c9d5e35.               */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_platform.h"

namespace barista_script {

/**
 * Opaque compiled-function handle. The analyzer/conformance registry only stores pointers;
 * M4 defines the class body. A complete type is required so HashMap values compile.
 */
class BSFunction {
public:
	BSFunction() = default;
	~BSFunction() = default;
};

} // namespace barista_script
