/**************************************************************************/
/*  bs_platform_probe.cpp                                                 */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_platform_probe.h"

#ifdef DEBUG_ENABLED

#include "bs_platform.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace barista_script {

namespace {

const StringName &sname_site_a() {
	return SNAME("BaristaScript");
}

const StringName &sname_site_b() {
	return SNAME("BaristaScript");
}

} // namespace

void BaristaScriptPlatformProbe::_bind_methods() {
	ClassDB::bind_method(D_METHOD("string_builder_behavior"), &BaristaScriptPlatformProbe::string_builder_behavior);
	ClassDB::bind_method(D_METHOD("sname_behavior"), &BaristaScriptPlatformProbe::sname_behavior);
}

Dictionary BaristaScriptPlatformProbe::string_builder_behavior() const {
	Dictionary report;

	StringBuilder builder;
	report["fresh_count"] = builder.num_strings_appended();
	report["fresh_length"] = builder.get_string_length();
	report["fresh_string"] = builder.as_string();

	builder.append(String());
	report["after_empty_string_count"] = builder.num_strings_appended();
	report["after_empty_string_length"] = builder.get_string_length();

	builder.append("");
	report["after_empty_cstring_count"] = builder.num_strings_appended();
	report["after_empty_cstring_length"] = builder.get_string_length();

	builder.append(String(" appended"));
	builder.append(" as text");
	report["after_mixed_count"] = builder.num_strings_appended();
	report["after_mixed_length"] = builder.get_string_length();

	builder += String(" plus=");
	builder += "equals";
	report["after_plus_equals_count"] = builder.num_strings_appended();
	report["after_plus_equals_length"] = builder.get_string_length();

	report["final_count"] = builder.num_strings_appended();
	report["final_length"] = builder.get_string_length();
	report["final_string"] = builder.as_string();

	return report;
}

Dictionary BaristaScriptPlatformProbe::sname_behavior() const {
	const StringName &first_read = sname_site_a();
	const StringName &second_read = sname_site_a();
	const StringName &other_site = sname_site_b();

	Dictionary report;
	report["site_a"] = String(first_read);
	report["site_b"] = String(other_site);
	report["same_site_same_identity"] = &first_read == &second_read;
	report["distinct_sites_equal_value"] = first_read == other_site;
	report["distinct_sites_different_identity"] = &first_read != &other_site;
	return report;
}

} // namespace barista_script

#endif // DEBUG_ENABLED
