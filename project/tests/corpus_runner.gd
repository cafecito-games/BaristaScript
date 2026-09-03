# corpus_runner.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT
extends SceneTree

const Harness = preload("res://tests/corpus_harness.gd")

const DEFAULT_CORPUS_ROOT := "res://tests/corpus"

func _initialize() -> void:
	var corpus_root := DEFAULT_CORPUS_ROOT
	var allow_empty := false
	var update_expectations := false
	var harness := Harness.new()

	var arguments := OS.get_cmdline_user_args()
	var index := 0
	while index < arguments.size():
		var argument: String = arguments[index]
		match argument:
			"--corpus":
				index += 1
				if index >= arguments.size():
					_finish(harness.error_result("BS_ERROR --corpus requires a path"))
					return
				corpus_root = arguments[index]
			"--allow-empty":
				allow_empty = true
			"--update-expectations":
				update_expectations = true
			_:
				_finish(harness.error_result("BS_ERROR unknown argument: %s" % argument))
				return
		index += 1

	_finish(harness.run(corpus_root, allow_empty, update_expectations))


func _finish(result: Dictionary) -> void:
	for line: String in result["output"]:
		print(line)
	quit(result["exit_code"])
