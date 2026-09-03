/**************************************************************************/
/*  bs_global_class_probe.h                                               */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace barista_script {

/**
 * The global-class resolver's test surface.
 *
 * godot-cpp's `String` and `Variant` are engine-backed, so a ported front-end can only be exercised
 * from inside a loaded Godot; a registered class is the only way GDScript can reach one. This owns
 * no policy: every method forwards to `bs_global_class.h` or to the registered language, and
 * renders what came back.
 *
 * It exists only in `template_debug`. The test suites run against that build -- the editor loads
 * the `debug` library entry in `project/bin/barista_script.gdextension`, and CI's editor-recognition
 * job is gated on `target-type == 'template_debug'` -- so guarding it costs the suites nothing and
 * keeps a test-only class out of the surface `template_release` publishes.
 */
class BaristaScriptGlobalClassProbe final : public godot::RefCounted {
	GDCLASS(BaristaScriptGlobalClassProbe, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	/**
	 * `bs_resolve_global_class(p_path)` -- the path-reading entry point the language uses --
	 * rendered as the reported `Dictionary` plus the two fields the engine never sees:
	 *
	 *   `declarations_parsed`  whether the declaration parse accepted the source, which is what
	 *                          tells a file that declares nothing from one whose head did not
	 *                          parse. See `source_parses()` for the stricter question.
	 *   `kind`       the `BSDeclarationKind` index the resolution decided on.
	 *   `kind_name`  its one spelling, from `bs_declaration_kind_name()`.
	 */
	godot::Dictionary resolve_path(const godot::String &p_path) const;

	/** The same report for source text that is not on disk, as `BaristaScript` resolves its own. */
	godot::Dictionary resolve_source(const godot::String &p_source, const godot::String &p_path) const;

	/** `bs_source_parses()` -- the whole-file parse behind `BaristaScript::_is_valid()`. */
	bool source_parses(const godot::String &p_source, const godot::String &p_path) const;

	/**
	 * What the registered `BaristaScriptLanguage` reports for `p_path`.
	 *
	 * This is the surface the editor actually calls, reached through the language singleton rather
	 * than through the resolver, so a test can prove the two agree instead of assuming they do.
	 */
	godot::Dictionary language_global_class_name(const godot::String &p_path) const;

	/** `BaristaScriptLanguage::_handles_global_class_type()`, likewise through the singleton. */
	bool language_handles_global_class_type(const godot::String &p_type) const;

	/** Every `BSDeclarationKind` spelling, in enum order: index i names kind i. */
	godot::PackedStringArray declaration_kind_names() const;

	/**
	 * Whether `p_kind` names a declaration kind at all.
	 *
	 * `BSDeclarationKind::MAX` is the enumerator count and every consumer refuses it; this is how a
	 * vocabulary-closure test asks for that refusal without tripping the engine error stream, which
	 * is what calling a consumer with `MAX` would do.
	 */
	bool declaration_kind_index_is_valid(int p_kind) const;

	/**
	 * `bs_declaration_kind_is_instantiable()` by kind index, so a test cannot restate it. Defined
	 * only for an index `declaration_kind_index_is_valid()` accepts.
	 */
	bool declaration_kind_is_instantiable(int p_kind) const;

	/** `bs_build_qualified_global_name()`, the one place a namespace and an identifier are joined. */
	godot::String build_qualified_global_name(const godot::String &p_namespace, const godot::String &p_identifier) const;
};

} // namespace barista_script

#endif // DEBUG_ENABLED
