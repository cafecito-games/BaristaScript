# build_info_query.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

extends SceneTree

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1 or not ClassDB.class_exists("BaristaScript"):
		quit(2)
		return
	var script: Object = ClassDB.instantiate("BaristaScript")
	if script == null or not script.has_method("get_build_info"):
		quit(2)
		return
	var info: Variant = script.call("get_build_info")
	if not info is Dictionary or info.is_empty():
		quit(2)
		return
	print("BS_BUILD_INFO " + JSON.stringify({"nonce": args[0], "build_info": info, "godot_version": Engine.get_version_info()}))
	quit(0)
