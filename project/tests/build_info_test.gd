# build_info_test.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

extends SceneTree

const SuiteGuard = preload("res://tests/suite_guard.gd")
var failures: Array[String] = []

func check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
		push_error(message)

func _init() -> void:
	var script := BaristaScript.new()
	var first: Dictionary = script.get_build_info()
	check(first.size() == 8, "compiled build-info schema fields")
	check(typeof(first.get("schema")) == TYPE_INT and first.get("schema") == 1, "schema is an exact integer")
	check(first.get("extension_version", "") != "", "extension version is compiled")
	check(first.get("config_sha256", "").length() == 64, "configuration fingerprint is compiled")
	check(first.get("godot_api", "") != "" and first.get("godot_runtime", "") != "", "selected Godot versions are compiled")
	var original: Dictionary = first.duplicate(true)
	first["extension_version"] = "caller mutation"
	first["source"]["revision"] = "caller mutation"
	first["godot_cpp"]["revision"] = "caller mutation"
	first["build"]["architecture"] = "caller mutation"
	check(script.get_build_info() == original, "each call returns independent nested dictionaries")
	check(original["build"]["native_tests"] == false, "ordinary distributions have no native test identity")
	check(original["source"]["state"] in ["clean", "dirty", "unknown"], "source state is explicit")
	check(original["godot_cpp"]["state"] in ["clean", "dirty", "unknown"], "dependency state is explicit")
	quit(SuiteGuard.report("build_info_test", failures))
