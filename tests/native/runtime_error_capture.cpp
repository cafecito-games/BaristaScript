/**************************************************************************/
/*  runtime_error_capture.cpp                                             */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "runtime_error_capture.h"

void BaristaRuntimeErrorCapture::_log_error(const godot::String &, const godot::String &, int32_t,
		const godot::String &p_code, const godot::String &p_rationale, bool, int32_t p_error_type,
		const godot::TypedArray<godot::Ref<godot::ScriptBacktrace>> &) {
	if (p_error_type != godot::Logger::ERROR_TYPE_SCRIPT) {
		return;
	}
	script_errors.push_back(p_rationale.is_empty() ? p_code : p_rationale);
}
