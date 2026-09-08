// Copyright (c) 2026-present Cafecito Games LLC.
// This file is part of BaristaScript, a Godot GDExtension.
// SPDX-License-Identifier: MIT

#pragma once

#include "bs_platform.h"
#include "bs_type_info.h"

namespace barista_script {

// Analyzer-facing slice of Foundry fs_utility_functions.h/.cpp @ c9d5e35.
// This registry never loads resources, runs script code, accesses a debugger stack or creates proxies.
class BSUtilityFunctions {
public:
	enum class ConstantResult { NOT_CONSTANT,
		FOLDED,
		INVALID_ARGUMENT };
	static bool get_function_info(const StringName &p_name, MethodInfo &r_info);
	static List<MethodInfo> get_function_list();
	static bool is_function_constant(const StringName &p_name);
	// Call only after ordinary signature validation, with constant arguments.
	static ConstantResult evaluate_constant(const StringName &p_name, const Vector<Variant> &p_arguments, Variant &r_value, String &r_error);
	// Private, non-executable identity carrier for analyzer constants, never VM registration.
	static Callable make_analyzer_callable(const StringName &p_name);
	static bool is_analyzer_callable(const Callable &p_callable);
};

} // namespace barista_script
