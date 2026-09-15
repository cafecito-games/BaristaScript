/**************************************************************************/
/*  analyzer_self_identity_access.h                                       */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#ifdef BARISTA_TESTS
#include "bs_parser.h"

namespace barista_script::native_tests {
// Test-only typed access to anonymous analyzer helpers; fixtures live in native tests.
struct SelfIdentityTestAccess {
	using Type = BSParser::DataType;
	static bool alpha_equal(const Type &expected, const Type &actual);
	static bool strict_identity_equal(const Type &expected, const Type &actual);
	static bool matches_substituted_self(const Type &expected, const Type &actual);
	static Type substitute_self(const Type &type, bool mark);
	static bool parameter_matches(const Type &expected, const Type &actual, Type &matched);
	static bool needs_receiver_identity(const Type &expected, const Type &actual);
};
} // namespace barista_script::native_tests
#endif
