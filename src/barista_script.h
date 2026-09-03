/**************************************************************************/
/*  barista_script.h                                                      */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>

namespace barista_script {

class BaristaScript final : public godot::ScriptExtension {
	GDCLASS(BaristaScript, godot::ScriptExtension)

	godot::String source_code;

protected:
	static void _bind_methods();

public:
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
	static bool is_canonically_equal_paths(const godot::String &p_path_a, const godot::String &p_path_b) {
		return canonicalize_path(p_path_a) == canonicalize_path(p_path_b);
	}
};

} // namespace barista_script
