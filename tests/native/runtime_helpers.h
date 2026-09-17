/**************************************************************************/
/*  runtime_helpers.h                                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "barista_script.h"
#include "bs_platform.h"
#include "runtime_error_capture.h"

namespace barista_script {
namespace native_tests {

/** Compiles `p_source` into a fresh script, or returns an invalid script carrying the diagnostic. */
Ref<BaristaScript> compile_script(const String &p_source, const String &p_path);

/** Installs an error-channel reader for as long as it is alive. */
class RuntimeErrorScope {
	Ref<BaristaRuntimeErrorCapture> capture;

public:
	RuntimeErrorScope();
	~RuntimeErrorScope();

	const PackedStringArray &errors() const { return capture->script_errors; }
	bool has_error_containing(const String &p_needle) const;
	String joined() const;
};

} // namespace native_tests
} // namespace barista_script
