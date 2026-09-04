/**************************************************************************/
/*  bs_trait_utils.h                                                      */
/*                                                                        */
/*  Hard fork of Foundry `modules/foundry_script/fs_trait_utils.h` @       */
/*  c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6. FS* -> BS*; engine contact  */
/*  through bs_platform.h. Starter slice: diagnostic naming helpers used  */
/*  by the #60 conformance witness TU. Binding / Self-reify walkers remain*/
/*  follow-up under #60.                                                  */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_diagnostic_names.h"
#include "bs_parser.h"

namespace barista_script {

// How every diagnostic names a class or a trait: its declared name when it has one, otherwise the
// name of the file that declares it. A head class with no `class_name` is the only case that falls
// back, and its `fqcn` is a path, so rendering it raw would put a build-machine path in the message.
static _FORCE_INLINE_ String bs_class_or_trait_diagnostic_name(const BSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return "<unknown>";
	}
	if (p_class->identifier != nullptr) {
		return p_class->identifier->name;
	}
	return bs_diagnostic_type_name_for_path(p_class->fqcn);
}

static _FORCE_INLINE_ StringName bs_trait_identity_name(const BSParser::ClassNode *p_trait) {
	ERR_FAIL_NULL_V(p_trait, StringName());

	const StringName global_name = p_trait->get_global_name();
	if (global_name != StringName()) {
		return global_name;
	}
	return StringName(p_trait->fqcn);
}

} // namespace barista_script
