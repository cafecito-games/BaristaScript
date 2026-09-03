# suite_guard.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT
extends RefCounted

## The shared tail of every GDScript suite in this project.
##
## SceneTree quits with status 0 when a script fails to parse, so an exit code
## alone cannot tell "the suite passed" from "the suite never ran". Only code
## that actually loaded and executed can print the sentinel below, so
## tests/run_gdscript_suites.py treats a missing sentinel line as a failure.
## Suites that also print a sentinel of their own keep printing it; this one is
## the guard every suite shares.

const SUCCESS_PREFIX := "BS_SUITE_OK"
const FAILURE_PREFIX := "BS_SUITE_FAILED"


## Reports `failures` for `suite_name` and returns the process exit code, so a
## suite ends with `quit(SuiteGuard.report("my_test", failures))`.
static func report(suite_name: String, failures: Array[String]) -> int:
	for failure in failures:
		push_error(failure)
	if failures.is_empty():
		print("%s %s" % [SUCCESS_PREFIX, suite_name])
		return 0
	print("%s %s %d failure(s)" % [FAILURE_PREFIX, suite_name, failures.size()])
	return 1
