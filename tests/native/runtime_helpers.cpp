/**************************************************************************/
/*  runtime_helpers.cpp                                                   */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "runtime_helpers.h"

#include "bs_cache.h"

#include <godot_cpp/classes/os.hpp>

namespace barista_script {
namespace native_tests {

Ref<BaristaScript> compile_script(const String &p_source, const String &p_path) {
	Ref<BaristaScript> script;
	script.instantiate();
	// `take_over_path` rather than `set_path`, so repeated cases can reuse one probe path without
	// colliding in the engine's resource cache.
	script->take_over_path(p_path);
	script->set_source_code(p_source);
	script->compile();
	return script;
}

RuntimeCacheScope::~RuntimeCacheScope() {
	BSCache::clear();
	BSCache::clear_source_overrides();
}

RuntimeErrorScope::RuntimeErrorScope() {
	capture.instantiate();
	OS::get_singleton()->add_logger(capture);
}

RuntimeErrorScope::~RuntimeErrorScope() {
	OS::get_singleton()->remove_logger(capture);
}

bool RuntimeErrorScope::has_error_containing(const String &p_needle) const {
	for (const String &message : capture->script_errors) {
		if (message.contains(p_needle)) {
			return true;
		}
	}
	return false;
}

String RuntimeErrorScope::joined() const {
	String text;
	for (const String &message : capture->script_errors) {
		text += message + String("\n");
	}
	return text;
}

} // namespace native_tests
} // namespace barista_script
