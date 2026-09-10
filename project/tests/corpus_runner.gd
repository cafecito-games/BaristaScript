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
	var stage := ""
	var exact_case := ""
	var build_info_nonce := ""
	var allow_empty := false
	var update_expectations := false
	var harness := Harness.new()

	var arguments := OS.get_cmdline_user_args()
	var index := 0
	while index < arguments.size():
		var argument: String = arguments[index]
		match argument:
			"--build-info-nonce":
				index += 1
				if index >= arguments.size() or arguments[index].is_empty() or not build_info_nonce.is_empty():
					_finish(harness.error_result("BS_ERROR --build-info-nonce requires one nonempty nonce"))
					return
				build_info_nonce = arguments[index]
			"--corpus":
				index += 1
				if index >= arguments.size():
					_finish(harness.error_result("BS_ERROR --corpus requires a path"))
					return
				corpus_root = arguments[index]
			"--case":
				index += 1
				if index >= arguments.size():
					_finish(harness.error_result("BS_ERROR --case requires a relative case"))
					return
				exact_case = arguments[index]
			"--stage":
				index += 1
				if index >= arguments.size() or not arguments[index] in ["parser", "analyzer"]:
					_finish(harness.error_result("BS_ERROR --stage requires parser or analyzer"))
					return
				stage = arguments[index]
			"--allow-empty":
				allow_empty = true
			"--update-expectations":
				update_expectations = true
			_:
				_finish(harness.error_result("BS_ERROR unknown argument: %s" % argument))
				return
		index += 1

	if not build_info_nonce.is_empty():
		var script := BaristaScript.new()
		print("BS_BUILD_INFO " + JSON.stringify({"nonce": build_info_nonce, "build_info": script.get_build_info(), "godot_version": Engine.get_version_info()}))
	if not stage.is_empty():
		harness.fixture_stages[corpus_root] = stage
	var result := harness.run(corpus_root, allow_empty, update_expectations, exact_case)
	if not exact_case.is_empty():
		for case_result in result.get("results", []):
			print("BS_CASE_RESULT " + JSON.stringify(case_result))
			print("BS_CASE_RAN " + exact_case)
	_finish(result)


func _finish(result: Dictionary) -> void:
	for line: String in result["output"]:
		print(line)
	quit(result["exit_code"])
