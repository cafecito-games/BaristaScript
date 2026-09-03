/**************************************************************************/
/*  barista_script_language.cpp                                           */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script_language.h"

#include "barista_script.h"

#include <godot_cpp/variant/packed_int32_array.hpp>

namespace barista_script {

BaristaScriptLanguage *BaristaScriptLanguage::singleton = nullptr;

BaristaScriptLanguage::BaristaScriptLanguage() {
	if (singleton == nullptr) {
		singleton = this;
	}
}

BaristaScriptLanguage::~BaristaScriptLanguage() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void BaristaScriptLanguage::_bind_methods() {}

BaristaScriptLanguage *BaristaScriptLanguage::get_singleton() {
	return singleton;
}

godot::String BaristaScriptLanguage::_get_name() const {
	return "BaristaScript";
}

void BaristaScriptLanguage::_init() {}

godot::String BaristaScriptLanguage::_get_type() const {
	return "BaristaScript";
}

godot::String BaristaScriptLanguage::_get_extension() const {
	return "barista";
}

void BaristaScriptLanguage::_finish() {}

godot::PackedStringArray BaristaScriptLanguage::_get_reserved_words() const {
	return {};
}

bool BaristaScriptLanguage::_is_control_flow_keyword(const godot::String &) const {
	return false;
}

godot::PackedStringArray BaristaScriptLanguage::_get_comment_delimiters() const {
	return {};
}

godot::PackedStringArray BaristaScriptLanguage::_get_doc_comment_delimiters() const {
	return {};
}

godot::PackedStringArray BaristaScriptLanguage::_get_string_delimiters() const {
	return {};
}

godot::Ref<godot::Script> BaristaScriptLanguage::_make_template(const godot::String &p_template, const godot::String &, const godot::String &) const {
	godot::Ref<BaristaScript> script;
	script.instantiate();
	script->_set_source_code(p_template);
	return script;
}

godot::TypedArray<godot::Dictionary> BaristaScriptLanguage::_get_built_in_templates(const godot::StringName &) const {
	return {};
}

bool BaristaScriptLanguage::_is_using_templates() {
	return false;
}

godot::Dictionary BaristaScriptLanguage::_validate(const godot::String &, const godot::String &, bool, bool, bool, bool) const {
	godot::Dictionary result;
	result["valid"] = false;
	result["functions"] = godot::PackedStringArray();
	result["errors"] = godot::TypedArray<godot::Dictionary>();
	result["warnings"] = godot::TypedArray<godot::Dictionary>();
	result["safe_lines"] = godot::PackedInt32Array();
	return result;
}

godot::String BaristaScriptLanguage::_validate_path(const godot::String &) const {
	return {};
}

bool BaristaScriptLanguage::_has_named_classes() const {
	return false;
}

bool BaristaScriptLanguage::_supports_builtin_mode() const {
	return false;
}

bool BaristaScriptLanguage::_supports_documentation() const {
	return false;
}

bool BaristaScriptLanguage::_can_inherit_from_file() const {
	return false;
}

int32_t BaristaScriptLanguage::_find_function(const godot::String &, const godot::String &) const {
	return -1;
}

godot::String BaristaScriptLanguage::_make_function(const godot::String &, const godot::String &, const godot::PackedStringArray &) const {
	return {};
}

bool BaristaScriptLanguage::_can_make_function() const {
	return false;
}

godot::Error BaristaScriptLanguage::_open_in_external_editor(const godot::Ref<godot::Script> &, int32_t, int32_t) {
	return godot::ERR_UNAVAILABLE;
}

bool BaristaScriptLanguage::_overrides_external_editor() {
	return false;
}

godot::ScriptLanguage::ScriptNameCasing BaristaScriptLanguage::_preferred_file_name_casing() const {
	return godot::ScriptLanguage::SCRIPT_NAME_CASING_SNAKE_CASE;
}

godot::Dictionary BaristaScriptLanguage::_complete_code(const godot::String &, const godot::String &, godot::Object *) const {
	return {};
}

godot::Dictionary BaristaScriptLanguage::_lookup_code(const godot::String &, const godot::String &, const godot::String &, godot::Object *) const {
	return {};
}

godot::String BaristaScriptLanguage::_auto_indent_code(const godot::String &p_code, int32_t, int32_t) const {
	return p_code;
}

void BaristaScriptLanguage::_add_global_constant(const godot::StringName &, const godot::Variant &) {}

void BaristaScriptLanguage::_add_named_global_constant(const godot::StringName &, const godot::Variant &) {}

void BaristaScriptLanguage::_remove_named_global_constant(const godot::StringName &) {}

void BaristaScriptLanguage::_thread_enter() {}

void BaristaScriptLanguage::_thread_exit() {}

godot::String BaristaScriptLanguage::_debug_get_error() const {
	return {};
}

int32_t BaristaScriptLanguage::_debug_get_stack_level_count() const {
	return 0;
}

int32_t BaristaScriptLanguage::_debug_get_stack_level_line(int32_t) const {
	return -1;
}

godot::String BaristaScriptLanguage::_debug_get_stack_level_function(int32_t) const {
	return {};
}

godot::String BaristaScriptLanguage::_debug_get_stack_level_source(int32_t) const {
	return {};
}

godot::Dictionary BaristaScriptLanguage::_debug_get_stack_level_locals(int32_t, int32_t, int32_t) {
	return {};
}

godot::Dictionary BaristaScriptLanguage::_debug_get_stack_level_members(int32_t, int32_t, int32_t) {
	return {};
}

void *BaristaScriptLanguage::_debug_get_stack_level_instance(int32_t) {
	return nullptr;
}

godot::Dictionary BaristaScriptLanguage::_debug_get_globals(int32_t, int32_t) {
	return {};
}

godot::String BaristaScriptLanguage::_debug_parse_stack_level_expression(int32_t, const godot::String &, int32_t, int32_t) {
	return {};
}

godot::TypedArray<godot::Dictionary> BaristaScriptLanguage::_debug_get_current_stack_info() {
	return {};
}

void BaristaScriptLanguage::_reload_all_scripts() {}

void BaristaScriptLanguage::_reload_scripts(const godot::Array &, bool) {}

void BaristaScriptLanguage::_reload_tool_script(const godot::Ref<godot::Script> &, bool) {}

godot::PackedStringArray BaristaScriptLanguage::_get_recognized_extensions() const {
	godot::PackedStringArray extensions;
	extensions.push_back("barista");
	return extensions;
}

godot::TypedArray<godot::Dictionary> BaristaScriptLanguage::_get_public_functions() const {
	return {};
}

godot::Dictionary BaristaScriptLanguage::_get_public_constants() const {
	return {};
}

godot::TypedArray<godot::Dictionary> BaristaScriptLanguage::_get_public_annotations() const {
	return {};
}

void BaristaScriptLanguage::_profiling_start() {}

void BaristaScriptLanguage::_profiling_stop() {}

void BaristaScriptLanguage::_profiling_set_save_native_calls(bool) {}

int32_t BaristaScriptLanguage::_profiling_get_accumulated_data(godot::ScriptLanguageExtensionProfilingInfo *, int32_t) {
	return 0;
}

int32_t BaristaScriptLanguage::_profiling_get_frame_data(godot::ScriptLanguageExtensionProfilingInfo *, int32_t) {
	return 0;
}

void BaristaScriptLanguage::_frame() {}

bool BaristaScriptLanguage::_handles_global_class_type(const godot::String &) const {
	return false;
}

godot::Dictionary BaristaScriptLanguage::_get_global_class_name(const godot::String &) const {
	return {};
}

} // namespace barista_script
