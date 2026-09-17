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

#include <string>

namespace barista_script {
namespace native_tests {

/**
 * A `String` in a form a failed assertion can still read.
 *
 * `String::utf8()` returns a temporary whose buffer dies at the end of the full expression, so
 * handing its pointer to an assertion message prints whatever happens to be at that address.
 */
inline std::string readable(const String &p_value) {
	return std::string(p_value.utf8().get_data());
}

/** Compiles `p_source` into a fresh script, or returns an invalid script carrying the diagnostic. */
Ref<BaristaScript> compile_script(const String &p_source, const String &p_path);

/**
 * Clears the shared parse cache when it goes out of scope.
 *
 * Compiling a script that names a dependency leaves that dependency's parser in the cache, which is
 * what a cache is for and what production wants. A case that triggers one has to put the process
 * back the way it found it, the same way the storage suites do.
 */
class RuntimeCacheScope {
public:
	~RuntimeCacheScope();
};

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
