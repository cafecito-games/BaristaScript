/**************************************************************************/
/*  bs_build_info.h                                                       */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace barista_script {
godot::String bs_get_build_info_json();
godot::Dictionary bs_get_build_info();
} // namespace barista_script
