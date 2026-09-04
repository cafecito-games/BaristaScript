/**************************************************************************/
/*  bs_type_info.h                                                        */
/*                                                                        */
/*  D1 adapter: FoundryTypeInfo numeric-width metadata is deleted. Stock  */
/*  Godot / godot-cpp expose GDExtensionClassMethodArgumentMetadata, but  */
/*  BaristaScript never rehydrates fixed-width NumericType from it.       */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_platform.h"

#include <godot_cpp/core/method_bind.hpp>
#include <godot_cpp/core/object.hpp>

namespace barista_script {

/**
 * Stand-in for Foundry's `FoundryTypeInfo` (@ c9d5e35). Every enumerator other than
 * `METADATA_NONE` named a deleted fixed-width integer; the port keeps only `NONE` so call sites
 * that still pass metadata compile, and every helper below returns `NONE`.
 */
namespace BSTypeInfo {
enum Metadata : int {
	METADATA_NONE = 0,
};
} // namespace BSTypeInfo

/** D1: MethodInfo width metadata is never consulted. */
static _FORCE_INLINE_ BSTypeInfo::Metadata bs_method_argument_meta(const MethodInfo &, int) {
	return BSTypeInfo::METADATA_NONE;
}

/** D1: MethodBind width metadata is never consulted. */
static _FORCE_INLINE_ BSTypeInfo::Metadata bs_method_bind_argument_meta(const MethodBind *, int) {
	return BSTypeInfo::METADATA_NONE;
}

} // namespace barista_script
