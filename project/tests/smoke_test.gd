# smoke_test.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

extends SceneTree


func _initialize() -> void:
	var failures: Array[String] = []
	var recognized := ResourceLoader.get_recognized_extensions_for_type("BaristaScript")
	if not recognized.has("barista"):
		failures.append("BaristaScript does not recognize .barista")

	var script := ResourceLoader.load("res://example.barista") as Script
	if script == null:
		failures.append("example.barista did not load as Script")
	else:
		if script.get_class() != "BaristaScript":
			failures.append("loaded resource is %s" % script.get_class())
		if script.get_source_code() != "# BaristaScript recognition fixture\n":
			failures.append("source text was not preserved: %s" % var_to_str(script.get_source_code()))
		if script.can_instantiate():
			failures.append("recognition-only script can instantiate")

	for failure in failures:
		push_error(failure)
	quit(0 if failures.is_empty() else 1)
