/**************************************************************************/
/*  platform_test.cpp                                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_platform.h"
#include "doctest.h"

using namespace godot;

namespace {
const StringName &sname_site_a() { return SNAME("BaristaScript"); }
const StringName &sname_site_b() { return SNAME("BaristaScript"); }
} //namespace

TEST_SUITE("platform") {
	TEST_CASE("string_builder_behavior") {
		StringBuilder builder;
		CHECK(builder.num_strings_appended() == 0);
		CHECK(builder.get_string_length() == 0);
		CHECK(builder.as_string() == "");
		builder.append(String());
		CHECK(builder.num_strings_appended() == 0);
		CHECK(builder.get_string_length() == 0);
		builder.append("");
		CHECK(builder.num_strings_appended() == 1);
		CHECK(builder.get_string_length() == 0);
		builder.append(String(" appended"));
		builder.append(" as text");
		CHECK(builder.num_strings_appended() == 3);
		CHECK(builder.get_string_length() == 17);
		builder += String(" plus=");
		builder += "equals";
		CHECK(builder.num_strings_appended() == 5);
		CHECK(builder.get_string_length() == 29);
		CHECK(builder.as_string() == " appended as text plus=equals");
		CHECK(builder.num_strings_appended() == 5);
		CHECK(builder.get_string_length() == 29);
	}
	TEST_CASE("sname_behavior") {
		const StringName &first = sname_site_a(), &second = sname_site_a(), &other = sname_site_b();
		CHECK(String(first) == "BaristaScript");
		CHECK(String(other) == "BaristaScript");
		CHECK(&first == &second);
		CHECK(first == other);
	}
}
