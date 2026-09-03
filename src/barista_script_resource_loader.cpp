/**************************************************************************/
/*  barista_script_resource_loader.cpp                                    */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script_resource_loader.h"

#include "barista_script.h"

#include <godot_cpp/classes/file_access.hpp>

namespace barista_script {

namespace {

bool is_barista_path(const godot::String &p_path) {
	return p_path.get_extension().to_lower() == "barista";
}

} // namespace

void BaristaScriptResourceLoader::_bind_methods() {}

godot::PackedStringArray BaristaScriptResourceLoader::_get_recognized_extensions() const {
	godot::PackedStringArray extensions;
	extensions.push_back("barista");
	return extensions;
}

bool BaristaScriptResourceLoader::_recognize_path(const godot::String &p_path, const godot::StringName &p_type) const {
	return is_barista_path(p_path) && (p_type.is_empty() || _handles_type(p_type));
}

bool BaristaScriptResourceLoader::_handles_type(const godot::StringName &p_type) const {
	return p_type == godot::StringName("Script") || p_type == godot::StringName("BaristaScript");
}

godot::String BaristaScriptResourceLoader::_get_resource_type(const godot::String &p_path) const {
	return is_barista_path(p_path) ? godot::String("BaristaScript") : godot::String();
}

godot::String BaristaScriptResourceLoader::_get_resource_script_class(const godot::String &) const {
	return {};
}

int64_t BaristaScriptResourceLoader::_get_resource_uid(const godot::String &) const {
	return -1;
}

godot::PackedStringArray BaristaScriptResourceLoader::_get_dependencies(const godot::String &, bool) const {
	return {};
}

godot::Error BaristaScriptResourceLoader::_rename_dependencies(const godot::String &, const godot::Dictionary &) const {
	return godot::OK;
}

bool BaristaScriptResourceLoader::_exists(const godot::String &p_path) const {
	return godot::FileAccess::file_exists(p_path);
}

godot::PackedStringArray BaristaScriptResourceLoader::_get_classes_used(const godot::String &) const {
	return {};
}

godot::Variant BaristaScriptResourceLoader::_load(const godot::String &p_path, const godot::String &p_original_path, bool, int32_t) const {
	godot::Ref<godot::FileAccess> file = godot::FileAccess::open(p_path, godot::FileAccess::READ);
	if (file.is_null()) {
		return godot::FileAccess::get_open_error();
	}

	const godot::String source = file->get_as_text();
	const godot::Error read_error = file->get_error();
	if (read_error != godot::OK && read_error != godot::ERR_FILE_EOF) {
		return read_error;
	}

	godot::Ref<BaristaScript> script;
	script.instantiate();
	script->_set_source_code(source);
	script->set_path(p_original_path.is_empty() ? p_path : p_original_path);
	return script;
}

} // namespace barista_script
