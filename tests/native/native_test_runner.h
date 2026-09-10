/**************************************************************************/
/*  native_test_runner.h                                                  */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once
#include <godot_cpp/classes/scene_tree.hpp>

class BaristaNativeTestRunner final : public godot::SceneTree {
	GDCLASS(BaristaNativeTestRunner, godot::SceneTree)
	bool ran = false;

protected:
	static void _bind_methods() {}

public:
	bool _process(double delta) override;
};
