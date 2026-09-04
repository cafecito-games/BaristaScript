/**************************************************************************/
/*  barista_script.cpp                                                    */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"

#include "barista_script_language.h"
#include "bs_analyzer.h"

namespace barista_script {

void BaristaScript::_bind_methods() {}

bool BaristaScript::_editor_can_reload_from_file() {
	return true;
}

void BaristaScript::_placeholder_erased(void *) {}

bool BaristaScript::_can_instantiate() const {
	return false;
}

godot::Ref<godot::Script> BaristaScript::_get_base_script() const {
	return {};
}

godot::StringName BaristaScript::_get_global_name() const {
	// The qualified name, always: `namespace app.combat` + `class_name Weapon` is
	// `app.combat.Weapon` here exactly as it is in the global class registry, because both come
	// from `bs_build_qualified_global_name()`.
	const godot::String name = resolve_global_class().name;
	return name.is_empty() ? godot::StringName() : godot::StringName(name);
}

bool BaristaScript::_inherits_script(const godot::Ref<godot::Script> &) const {
	return false;
}

godot::StringName BaristaScript::_get_instance_base_type() const {
	return {};
}

void *BaristaScript::_instance_create(godot::Object *) const {
	return nullptr;
}

void *BaristaScript::_placeholder_instance_create(godot::Object *) const {
	return nullptr;
}

bool BaristaScript::_has_source_code() const {
	return true;
}

godot::String BaristaScript::_get_source_code() const {
	return source_code;
}

void BaristaScript::_set_source_code(const godot::String &p_code) {
	source_code = p_code;
}

godot::Error BaristaScript::_reload(bool) {
	return godot::OK;
}

godot::StringName BaristaScript::_get_doc_class_name() const {
	return {};
}

godot::TypedArray<godot::Dictionary> BaristaScript::_get_documentation() const {
	return {};
}

godot::String BaristaScript::_get_class_icon_path() const {
	return {};
}

bool BaristaScript::_has_method(const godot::StringName &) const {
	return false;
}

bool BaristaScript::_has_static_method(const godot::StringName &) const {
	return false;
}

godot::Variant BaristaScript::_get_script_method_argument_count(const godot::StringName &) const {
	return {};
}

godot::Dictionary BaristaScript::_get_method_info(const godot::StringName &) const {
	return {};
}

bool BaristaScript::_is_tool() const {
	return false;
}

bool BaristaScript::_is_valid() const {
	// Semantic validity must agree with `BaristaScriptLanguage::_validate()`: parse + analyze.
	// Declaration-only resolution (`_get_global_class_name`) stays looser so mid-edit bodies do not
	// drop the global name. Issue #43.
	return bs_source_analyzes(source_code, canonicalize_path(get_path()));
}

bool BaristaScript::_is_abstract() const {
	// The gate. `CreateDialog::_should_hide_type` and `ClassDB::can_instantiate`'s script fallback
	// both consult the script object rather than the `is_abstract` flag cached in
	// `global_script_class_cache.cfg`, which is measured to be ignored
	// (docs/namespace-engine-support.md section 5). This is what keeps a `trait_name`, `enum_name`
	// or `tuple_name` file out of the Create Node dialog, and it is the *same* computation the
	// language reports, not a second opinion about it.
	return resolve_global_class().is_abstract;
}

godot::ScriptLanguage *BaristaScript::_get_language() const {
	return BaristaScriptLanguage::get_singleton();
}

bool BaristaScript::_has_script_signal(const godot::StringName &) const {
	return false;
}

godot::TypedArray<godot::Dictionary> BaristaScript::_get_script_signal_list() const {
	return {};
}

bool BaristaScript::_has_property_default_value(const godot::StringName &) const {
	return false;
}

godot::Variant BaristaScript::_get_property_default_value(const godot::StringName &) const {
	return {};
}

void BaristaScript::_update_exports() {}

godot::TypedArray<godot::Dictionary> BaristaScript::_get_script_method_list() const {
	return {};
}

godot::TypedArray<godot::Dictionary> BaristaScript::_get_script_property_list() const {
	return {};
}

int32_t BaristaScript::_get_member_line(const godot::StringName &) const {
	return -1;
}

godot::Dictionary BaristaScript::_get_constants() const {
	return {};
}

godot::TypedArray<godot::StringName> BaristaScript::_get_members() const {
	return {};
}

bool BaristaScript::_is_placeholder_fallback_enabled() const {
	return false;
}

godot::Variant BaristaScript::_get_rpc_config() const {
	return godot::Dictionary();
}

bool BaristaScript::_instance_has(godot::Object *) const {
	return false;
}

BSGlobalClass BaristaScript::resolve_global_class() const {
	return bs_resolve_global_class_from_source(source_code, canonicalize_path(get_path()));
}

godot::String BaristaScript::canonicalize_path(const godot::String &p_path) {
	// One source form exists today, so every path is already canonical. See the header for why the
	// function exists anyway.
	return p_path;
}

} // namespace barista_script
