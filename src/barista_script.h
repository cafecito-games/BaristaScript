/**************************************************************************/
/*  barista_script.h                                                      */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_function.h"
#include "bs_global_class.h"

#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>

namespace barista_script {

class BSInstance;

class BaristaScript final : public godot::ScriptExtension {
	GDCLASS(BaristaScript, godot::ScriptExtension)

	friend class BSCompiler;
	friend class BSInstance;

	godot::String source_code;

	/**
	 * The compiled form, rebuilt from source by every `_reload`.
	 *
	 * `valid` is the single answer to "can this script run": it is false until a compilation
	 * succeeds and false again the moment one fails, and `_can_instantiate` and `_instance_create`
	 * both read it, so a failed compilation can never leave a half-built function reachable.
	 */
	bool valid = false;
	godot::String compile_error;
	godot::StringName instance_base_type;
	godot::Ref<BaristaScript> base_script;
	godot::Vector<godot::StringName> member_names;
	// The declared carrier of each member, by the same index. A value stored from outside a compiled
	// function -- the inspector, a scene, GDScript -- has to meet the declaration the same way a
	// compiled store does, and this is what it is checked against.
	godot::Vector<godot::Variant::Type> member_carriers;
	godot::HashMap<godot::StringName, int> member_indices;
	godot::HashMap<godot::StringName, BSFunction *> member_functions;
	BSFunction *implicit_initializer = nullptr;
	godot::HashSet<BSInstance *> instances;

	void release_compiled_state();

protected:
	static void _bind_methods();

public:
	~BaristaScript();

	godot::Dictionary get_build_info() const;
	bool _editor_can_reload_from_file() override;
	void _placeholder_erased(void *p_placeholder) override;
	bool _can_instantiate() const override;
	godot::Ref<godot::Script> _get_base_script() const override;
	godot::StringName _get_global_name() const override;
	bool _inherits_script(const godot::Ref<godot::Script> &p_script) const override;
	godot::StringName _get_instance_base_type() const override;
	void *_instance_create(godot::Object *p_for_object) const override;
	void *_placeholder_instance_create(godot::Object *p_for_object) const override;
	bool _has_source_code() const override;
	godot::String _get_source_code() const override;
	void _set_source_code(const godot::String &p_code) override;
	godot::Error _reload(bool p_keep_state) override;
	godot::StringName _get_doc_class_name() const override;
	godot::TypedArray<godot::Dictionary> _get_documentation() const override;
	godot::String _get_class_icon_path() const override;
	bool _has_method(const godot::StringName &p_method) const override;
	bool _has_static_method(const godot::StringName &p_method) const override;
	godot::Variant _get_script_method_argument_count(const godot::StringName &p_method) const override;
	godot::Dictionary _get_method_info(const godot::StringName &p_method) const override;
	bool _is_tool() const override;
	bool _is_valid() const override;
	bool _is_abstract() const override;
	godot::ScriptLanguage *_get_language() const override;
	bool _has_script_signal(const godot::StringName &p_signal) const override;
	godot::TypedArray<godot::Dictionary> _get_script_signal_list() const override;
	bool _has_property_default_value(const godot::StringName &p_property) const override;
	godot::Variant _get_property_default_value(const godot::StringName &p_property) const override;
	void _update_exports() override;
	godot::TypedArray<godot::Dictionary> _get_script_method_list() const override;
	godot::TypedArray<godot::Dictionary> _get_script_property_list() const override;
	int32_t _get_member_line(const godot::StringName &p_member) const override;
	godot::Dictionary _get_constants() const override;
	godot::TypedArray<godot::StringName> _get_members() const override;
	bool _is_placeholder_fallback_enabled() const override;
	godot::Variant _get_rpc_config() const override;
	bool _instance_has(godot::Object *p_object) const override;

	/**
	 * The source path a script path denotes.
	 *
	 * Foundry canonicalizes its compiled variants (`.fsc`, `.fsb`) back onto the `.fs` source they
	 * were built from (foundry_script.cpp:2998-3004 @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6), so
	 * that one script has one identity in the global class table, in dependency lists, and in
	 * diagnostics whatever form of it was loaded. BaristaScript has exactly one form today --
	 * `.barista` -- so the mapping is the identity, and this is the single place the compiled-format
	 * milestones add their extensions rather than a second rule appearing at a comparison site.
	 */
	static godot::String canonicalize_path(const godot::String &p_path);

	/**
	 * The global class this script's own source declares.
	 *
	 * The script and the language answer the same question from the same function:
	 * `BaristaScriptLanguage::_get_global_class_name()` resolves a path on disk, this resolves the
	 * source text the script is holding -- which, mid-edit, is the one the editor cares about --
	 * and both land in `bs_resolve_global_class_from_source`. Two independent answers to "is this
	 * file instantiable?" is the defect this arrangement exists to prevent.
	 */
	BSGlobalClass resolve_global_class() const;
	static bool is_canonically_equal_paths(const godot::String &p_path_a, const godot::String &p_path_b) {
		return canonicalize_path(p_path_a) == canonicalize_path(p_path_b);
	}

	/** The diagnostic the last failed compilation produced, empty when the script compiled. */
	godot::String get_compile_error() const { return compile_error; }
	/** The compiled function this script or one of its bases declares, or null. */
	BSFunction *find_function(const godot::StringName &p_name) const;
	BaristaScript *get_base_barista_script() const { return base_script.ptr(); }
	const godot::Vector<godot::StringName> &get_member_names() const { return member_names; }
	/** The declared carrier of the member at `p_index`, or NIL when it is untyped. */
	godot::Variant::Type get_member_carrier(int p_index) const {
		return p_index >= 0 && p_index < member_carriers.size() ? member_carriers[p_index] : godot::Variant::NIL;
	}
	int get_member_index(const godot::StringName &p_name) const;
	/** Appends this script's methods, and its bases', with each one's declared argument count. */
	void collect_method_signatures(godot::Vector<godot::StringName> &r_names, godot::Vector<int> &r_argument_counts) const;
	/** Compiles the current source, replacing any previous compiled form. */
	godot::Error compile();
	void notify_instance_freed(BSInstance *p_instance) { instances.erase(p_instance); }
	/** How many instances this script still lists. Zero once every owner has been freed. */
	int get_instance_count() const { return instances.size(); }
};

} // namespace barista_script
