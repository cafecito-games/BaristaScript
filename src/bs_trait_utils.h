/**************************************************************************/
/*  bs_trait_utils.h                                                      */
/*                                                                        */
/*  Hard fork of Foundry `modules/foundry_script/fs_trait_utils.h` @       */
/*  c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6. FS* -> BS*; engine contact  */
/*  through bs_platform.h. Diagnostic naming + identity helpers +         */
/*  ClassTraitBinding / RecordedTypeArgument projection walkers for #60.  */
/*  Full class_has_named_trait / open-Self residual under #60.            */
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

/** Trait itself followed by already-resolved supertraits, deduplicated by identity name. */
Vector<BSParser::ClassNode *> bs_trait_identity_closure_nodes(const BSParser::ClassNode *p_trait);

/** One `uses` entry's binding of the trait it names. Empty when the entry supplied none. */
HashMap<StringName, BSParser::DataType> bs_trait_use_type_argument_bindings(
		const BSParser::ClassNode *p_trait, const BSParser::ClassNode::TraitUse &p_trait_use);

/**
 * How `p_class` binds `p_trait`'s type parameters through direct `uses` and transitive
 * supertrait hops. Empty when the class does not apply the trait or supplies no arguments.
 */
HashMap<StringName, BSParser::DataType> bs_trait_type_argument_bindings(
		const BSParser::ClassNode *p_class, const BSParser::ClassNode *p_trait);

bool bs_trait_argument_references_self(const BSParser::DataType &p_argument);
bool bs_trait_implementer_reifies_self(const BSParser::ClassNode *p_implementer);

/** `p_argument` with every `Self` resolved to `p_implementer` when the implementer reifies Self. */
BSParser::DataType bs_reify_self_in_trait_argument(
		const BSParser::ClassNode *p_implementer, const BSParser::DataType &p_argument);

/**
 * Arguments one conformance supplies for one trait identity in its implied closure.
 * Foundry `fs_project_conformance_trait_arguments` @ c9d5e35.
 */
bool bs_project_conformance_trait_arguments(
		const BSParser::ClassNode *p_direct_trait,
		const Vector<BSParser::DataType> &p_conformance_arguments,
		const HashMap<StringName, BSParser::DataType> &p_direct_bindings,
		const BSParser::ClassNode *p_identity_trait,
		const BSParser::ClassNode *p_implementer,
		Vector<BSParser::DataType> &r_arguments);

} // namespace barista_script
