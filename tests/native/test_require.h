/**************************************************************************/
/*  test_require.h                                                        */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "doctest.h"

// REQUIRE cannot unwind with exceptions disabled. Evaluate once, report the
// expression, and explicitly return from a void case/helper before unsafe access.
#define BS_TEST_REQUIRE(condition)                      \
	do {                                                \
		const bool bs_test_condition = bool(condition); \
		CHECK_MESSAGE(bs_test_condition, #condition);   \
		if (!bs_test_condition) {                       \
			return;                                     \
		}                                               \
	} while (false)
