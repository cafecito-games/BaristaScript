/**************************************************************************/
/*  register_types.cpp                                                    */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "register_types.h"

#include "barista_script.h"
#include "barista_script_language.h"
#include "barista_script_resource_loader.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace {

barista_script::BaristaScriptLanguage *language = nullptr;
godot::Ref<barista_script::BaristaScriptResourceLoader> resource_loader;

} // namespace

void initialize_barista_script(godot::ModuleInitializationLevel p_level) {
	if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(barista_script::BaristaScriptLanguage);
	GDREGISTER_CLASS(barista_script::BaristaScript);
	GDREGISTER_CLASS(barista_script::BaristaScriptResourceLoader);

	language = memnew(barista_script::BaristaScriptLanguage);
	const godot::Error registration_error = godot::Engine::get_singleton()->register_script_language(language);
	if (registration_error != godot::OK) {
		godot::UtilityFunctions::push_error(
				godot::vformat("Failed to register BaristaScript language (error %d).", registration_error));
		memdelete(language);
		language = nullptr;
		return;
	}

	resource_loader.instantiate();
	godot::ResourceLoader::get_singleton()->add_resource_format_loader(resource_loader, true);
}

void uninitialize_barista_script(godot::ModuleInitializationLevel p_level) {
	if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	if (resource_loader.is_valid()) {
		godot::ResourceLoader::get_singleton()->remove_resource_format_loader(resource_loader);
		resource_loader.unref();
	}

	if (language != nullptr) {
		godot::Engine::get_singleton()->unregister_script_language(language);
		memdelete(language);
		language = nullptr;
	}
}

extern "C" {

GDExtensionBool GDE_EXPORT barista_script_library_init(
		GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library,
		GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_object(p_get_proc_address, p_library, r_initialization);
	init_object.register_initializer(initialize_barista_script);
	init_object.register_terminator(uninitialize_barista_script);
	init_object.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_object.init();
}

} // extern "C"
