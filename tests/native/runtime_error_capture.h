/**************************************************************************/
/*  runtime_error_capture.h                                               */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/script_backtrace.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>

/**
 * Reads the engine's error channel back.
 *
 * A runtime error is reported, not returned, so the only honest way to assert its text is to read
 * the channel the engine publishes it on. Registering this logger is also what proves the runtime
 * reaches that channel at all.
 */
class BaristaRuntimeErrorCapture final : public godot::Logger {
	GDCLASS(BaristaRuntimeErrorCapture, godot::Logger)

protected:
	static void _bind_methods() {}

public:
	godot::PackedStringArray script_errors;

	void _log_error(const godot::String &p_function, const godot::String &p_file, int32_t p_line,
			const godot::String &p_code, const godot::String &p_rationale, bool p_editor_notify,
			int32_t p_error_type, const godot::TypedArray<godot::Ref<godot::ScriptBacktrace>> &p_backtraces) override;
};
