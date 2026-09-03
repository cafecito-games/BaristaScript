#include "barista_script.h"

#include "barista_script_language.h"

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
	return {};
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
	return false;
}

bool BaristaScript::_is_abstract() const {
	return false;
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

} // namespace barista_script
