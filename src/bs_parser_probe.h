/**************************************************************************/
/*  bs_parser_probe.h                                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace barista_script {

// Retained for project/tests/analyzer_test.gd::_test_can_reference until that
// suite migrates. Parser contracts themselves now run directly in native C++.
class BaristaScriptParserProbe final : public godot::RefCounted {
	GDCLASS(BaristaScriptParserProbe, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// Descriptions carry kind, builtin_type, is_meta_type, native_type, script_path
	// and recursive container_element_types; the adapter owns no type behavior.
	bool can_reference(const godot::Dictionary &p_self, const godot::Dictionary &p_other) const;
};

} // namespace barista_script

#endif // DEBUG_ENABLED
