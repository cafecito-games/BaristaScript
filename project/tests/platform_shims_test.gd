# platform_shims_test.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT
extends SceneTree

## Behavioural tests for the platform seam shims in `bs_platform.h`, driven through
## `BaristaScriptPlatformProbe`.
##
## godot-cpp's `String` and `StringName` are engine-backed, so the shims can only be
## exercised from inside a loaded Godot runtime. This suite asserts the observable
## behaviour the ported frontend relies on.

const SuiteGuard = preload("res://tests/suite_guard.gd")

const SUCCESS_SENTINEL := "BS_PLATFORM_SHIMS_OK"


func _initialize() -> void:
	var probe := BaristaScriptPlatformProbe.new()
	var failures: Array[String] = []

	_test_string_builder_behavior(probe, failures)
	_test_sname_behavior(probe, failures)

	if failures.is_empty():
		print("%s %d test groups passed" % [SUCCESS_SENTINEL, 2])
	quit(SuiteGuard.report("platform_shims_test", failures))


func _test_string_builder_behavior(probe, failures: Array[String]) -> void:
	var report: Dictionary = probe.string_builder_behavior()

	_expect(failures, report["fresh_count"] == 0, "fresh builder: count is not zero")
	_expect(failures, report["fresh_length"] == 0, "fresh builder: length is not zero")
	_expect(failures, report["fresh_string"] == "", "fresh builder: result is not empty")

	_expect(failures, report["after_empty_string_count"] == 0,
		"empty String append: counted as an append")
	_expect(failures, report["after_empty_string_length"] == 0,
		"empty String append: changed the length")

	_expect(failures, report["after_empty_cstring_count"] == 1,
		"empty C-string append: did not count as an append")
	_expect(failures, report["after_empty_cstring_length"] == 0,
		"empty C-string append: changed the length")

	_expect(failures, report["after_mixed_count"] == 3,
		"mixed appends: wrong append count")
	_expect(failures, report["after_mixed_length"] == 17,
		"mixed appends: wrong string length")

	_expect(failures, report["after_plus_equals_count"] == 5,
		"operator+=: wrong append count")
	_expect(failures, report["after_plus_equals_length"] == 29,
		"operator+=: wrong string length")

	_expect(failures, report["final_count"] == 5, "final builder: wrong append count")
	_expect(failures, report["final_length"] == 29, "final builder: wrong string length")
	_expect(failures, report["final_string"] == " appended as text plus=equals",
		"final builder: wrong string value")


func _test_sname_behavior(probe, failures: Array[String]) -> void:
	var report: Dictionary = probe.sname_behavior()

	_expect(failures, report["site_a"] == "BaristaScript",
		"site_a: wrong string value")
	_expect(failures, report["site_b"] == "BaristaScript",
		"site_b: wrong string value")
	_expect(failures, report["same_site_same_identity"],
		"same call site: did not reuse one cached StringName")
	_expect(failures, report["distinct_sites_equal_value"],
		"distinct call sites: interned values differ")
	_expect(failures, report["distinct_sites_different_identity"],
		"distinct call sites: shared one cached StringName instance")


func _expect(failures: Array[String], condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
