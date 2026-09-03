/**************************************************************************/
/*  bs_diagnostic_names.h                                                 */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_platform.h"

namespace barista_script {

/**
 * Hard fork of Foundry's `modules/foundry_script/fs_diagnostic_names.h` @
 * c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6, unchanged but for the rename and the seam include.
 *
 * How a diagnostic spells a script-declared entity and how it spells a file. The two rules are
 * deliberately different and must not be merged: a *type* is something the reader copies back into
 * source, a *file* is a location the reader opens.
 *
 * This header takes only string-level dependencies so runtime translation units can share the rules
 * with the front-end instead of re-deriving them; a re-derivation is how the same declaration came
 * to be named one way at compile time and another way at run time.
 */

/**
 * A script-declared entity named for a diagnostic: never a directory, never a `res://` prefix, never
 * an absolute build path. The declaring file's name survives because it is the only distinguishing
 * information an entity with no declared name has, and any `::` segments the identity carries are
 * preserved because they are the declared part of the name.
 *
 * `res://a/b/c.barista` -> `c.barista`; `/abs/a/b/c.barista::Inner` -> `c.barista::Inner`;
 * `Marker` -> `Marker`.
 */
String bs_diagnostic_type_name_for_path(const String &p_fully_qualified_name);

/**
 * A file named for a diagnostic: the `res://` path when the project root can localize it, otherwise
 * the file name alone. A location is genuinely useful and clickable, so it keeps its directories,
 * but it never degrades into a build-machine path when there is no project to localize against.
 */
String bs_diagnostic_file_reference(const String &p_path);

} // namespace barista_script
