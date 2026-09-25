/**************************************************************************/
/*  barista_script.cpp                                                    */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_build_info.h"

#include "barista_script_language.h"
#include "bs_analyzer.h"
#include "bs_cache.h"
#include "bs_compiler.h"
#include "bs_parser.h"
#include "bs_script_instance.h"

#include <godot_cpp/godot.hpp>

namespace barista_script {

BaristaScript::~BaristaScript() {
	release_compiled_state();
}

void BaristaScript::release_compiled_state() {
	valid = false;
	instance_base_type = godot::StringName();
	base_script = godot::Ref<BaristaScript>();
	member_names.clear();
	member_carriers.clear();
	member_types.clear();
	member_indices.clear();
	for (const godot::KeyValue<godot::StringName, BSFunction *> &entry : member_functions) {
		memdelete(entry.value);
	}
	member_functions.clear();
	if (implicit_initializer != nullptr) {
		memdelete(implicit_initializer);
		implicit_initializer = nullptr;
	}
}

BSFunction *BaristaScript::find_function(const godot::StringName &p_name) const {
	for (const BaristaScript *script = this; script != nullptr; script = script->base_script.ptr()) {
		if (godot::HashMap<godot::StringName, BSFunction *>::ConstIterator found = script->member_functions.find(p_name)) {
			return found->value;
		}
	}
	return nullptr;
}

int BaristaScript::get_member_index(const godot::StringName &p_name) const {
	const godot::HashMap<godot::StringName, int>::ConstIterator found = member_indices.find(p_name);
	return found ? found->value : -1;
}

void BaristaScript::collect_method_signatures(godot::Vector<godot::StringName> &r_names, godot::Vector<int> &r_argument_counts) const {
	for (const BaristaScript *script = this; script != nullptr; script = script->base_script.ptr()) {
		for (const godot::KeyValue<godot::StringName, BSFunction *> &entry : script->member_functions) {
			if (r_names.find(entry.key) < 0) {
				r_names.push_back(entry.key);
				r_argument_counts.push_back(entry.value->get_argument_count());
			}
		}
	}
}

godot::Error BaristaScript::compile() {
	const String path = canonicalize_path(get_path());
	HashMap<String, String> overrides;
	if (!path.is_empty()) {
		overrides[path] = source_code;
	}
	BSCacheSourceOverrideGuard source_scope(overrides, true);

	BSParser parser;
	BSAnalyzer analyzer(&parser);
	godot::Error status = parser.parse(source_code, path, false);
	if (status == godot::OK) {
		status = analyzer.analyze();
	}
	if (status != godot::OK || !parser.get_errors().is_empty()) {
		release_compiled_state();
		compile_error = parser.get_errors().is_empty()
				? String("The script could not be analyzed.")
				: parser.get_errors().front()->get().message;
		return godot::ERR_COMPILATION_FAILED;
	}

	BSCompiler compiler;
	if (compiler.compile(&parser, this) != godot::OK) {
		// Nothing half-built survives a failed compilation: the script is invalid and holds no
		// function, so `_can_instantiate` is false and `_instance_create` yields nothing.
		release_compiled_state();
		compile_error = compiler.get_error();
		return godot::ERR_COMPILATION_FAILED;
	}
	return godot::OK;
}

godot::Dictionary BaristaScript::get_build_info() const {
	return bs_get_build_info();
}

void BaristaScript::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_build_info"), &BaristaScript::get_build_info);
	// Script::is_valid() is not exposed to GDScript in Godot 4.7; bind an explicit wrapper so
	// suites and editor tooling can ask the same question `_is_valid()` answers (#43).
	ClassDB::bind_method(D_METHOD("is_valid"), &BaristaScript::_is_valid);
}

bool BaristaScript::_editor_can_reload_from_file() {
	return true;
}

void BaristaScript::_placeholder_erased(void *) {}

bool BaristaScript::_can_instantiate() const {
	// Two separate questions, and both have to be yes: the source compiled, and the declaration it
	// carries is one that may be instantiated at all. An `abstract class_name` compiles perfectly
	// well and is still not something the Create Node dialog may offer.
	return valid && !_is_abstract();
}

godot::Ref<godot::Script> BaristaScript::_get_base_script() const {
	return base_script;
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
	return instance_base_type;
}

void *BaristaScript::_instance_create(godot::Object *p_for_object) const {
	// The same two questions `_can_instantiate()` asks, asked here too: the engine reaches this
	// entry point directly from `Object::set_script`, which never consults `_can_instantiate()`.
	if (!_can_instantiate() || p_for_object == nullptr) {
		return nullptr;
	}
	if (!instance_base_type.is_empty() && !ClassDB::is_parent_class(p_for_object->get_class(), instance_base_type)) {
		ERR_PRINT(vformat(R"(Cannot attach "%s" to a "%s": the script extends "%s".)",
				get_path(), p_for_object->get_class(), String(instance_base_type)));
		return nullptr;
	}

	BaristaScript *self = const_cast<BaristaScript *>(this);
	BSInstance *instance = memnew(BSInstance);
	instance->owner = p_for_object;
	instance->script = godot::Ref<BaristaScript>(self);
	instance->members.resize(member_names.size());
	self->instances.insert(instance);

	// The initializers run before the engine is handed anything. They are what decides whether an
	// instance exists at all: one that raised has members the declaration does not describe, and
	// handing it over would put a half-built object into the scene. Running them first also means
	// `self.method()` works inside them, because the instance answers for its own script from the
	// moment it is built, without waiting to be attached.
	instance->initializing = true;
	bool initialized = true;
	if (implicit_initializer != nullptr) {
		GDExtensionCallError initializer_error;
		initializer_error.error = GDEXTENSION_CALL_OK;
		// What matters is whether *this* call raised, not whether anything raised while it ran.
		const bool raised_before = bs_runtime_error_was_reported();
		implicit_initializer->call(instance, nullptr, 0, initializer_error);
		initialized = initializer_error.error == GDEXTENSION_CALL_OK &&
				(raised_before || !bs_runtime_error_was_reported());
	}
	if (initialized) {
		if (BSFunction *initializer = find_function(SNAME("_init"))) {
			GDExtensionCallError initializer_error;
			initializer_error.error = GDEXTENSION_CALL_OK;
			const bool raised_before = bs_runtime_error_was_reported();
			initializer->call(instance, nullptr, 0, initializer_error);
			initialized = initializer_error.error == GDEXTENSION_CALL_OK &&
					(raised_before || !bs_runtime_error_was_reported());
		}
	}
	instance->initializing = false;

	if (!initialized) {
		// The reason is already on the script-error channel; this says what became of the instance.
		ERR_PRINT(vformat(R"(Cannot create an instance of "%s": its initializers did not run to completion.)", get_path()));
		memdelete(instance);
		return nullptr;
	}

	void *handle = godot::gdextension_interface::script_instance_create3(BSInstance::get_vtable(), instance);
	if (handle == nullptr) {
		// The owner would be left with an instance it never received; release it rather than hand
		// back something the engine does not know about.
		memdelete(instance);
		ERR_PRINT(vformat(R"(Cannot create a script instance for "%s".)", get_path()));
		return nullptr;
	}
	return handle;
}

void *BaristaScript::_placeholder_instance_create(godot::Object *p_for_object) const {
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	if (language == nullptr || p_for_object == nullptr) {
		return nullptr;
	}
	return godot::gdextension_interface::placeholder_script_instance_create(language->_owner,
			const_cast<BaristaScript *>(this)->_owner, p_for_object->_owner);
}

bool BaristaScript::_has_source_code() const {
	return true;
}

godot::String BaristaScript::_get_source_code() const {
	return source_code;
}

void BaristaScript::_set_source_code(const godot::String &p_code) {
	source_code = p_code;
	const String path = canonicalize_path(get_path());
	if (!path.is_empty() && path.begins_with("res://")) {
		BSCache::set_source_override(path, p_code);
		if (auto *language = BaristaScriptLanguage::get_singleton())
			language->synchronize_declaration_path_from_source(path, p_code);
	}
}

godot::Error BaristaScript::_reload(bool) {
	// FoundryScript::reload analyzes its existing source; load_source_code is the
	// separate disk producer. Keep unsaved and empty resource buffers authoritative.
	const String path = canonicalize_path(get_path());
	if (!instances.is_empty()) {
		// A live instance holds pointers into the compiled functions this would replace, and there is
		// no cancellation for a suspended frame yet, so the reload is refused before it changes
		// anything at all -- including the declaration index, which would otherwise be republished
		// from a source the instances are not running.
		ERR_PRINT(vformat(R"(Cannot reload "%s" while %d instance(s) are alive.)", path, instances.size()));
		return godot::ERR_BUSY;
	}
	godot::Error status = godot::OK;
	if (!path.is_empty()) {
		if (auto *language = BaristaScriptLanguage::get_singleton()) {
			status = language->synchronize_declaration_path_from_source(path, source_code);
		}
	}
	const godot::Error compile_status = compile();
	return status != godot::OK ? status : compile_status;
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

bool BaristaScript::_has_method(const godot::StringName &p_method) const {
	return find_function(p_method) != nullptr;
}

bool BaristaScript::_has_static_method(const godot::StringName &) const {
	return false;
}

godot::Variant BaristaScript::_get_script_method_argument_count(const godot::StringName &p_method) const {
	const BSFunction *function = find_function(p_method);
	return function != nullptr ? godot::Variant(function->get_argument_count()) : godot::Variant();
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
	const String path = canonicalize_path(get_path());
	HashMap<String, String> input;
	if (!path.is_empty())
		input[path] = source_code;
	BSCacheSourceOverrideGuard source_scope(input, true);
	return bs_source_analyzes(source_code, path);
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
	godot::TypedArray<godot::StringName> members;
	for (const godot::StringName &member : member_names) {
		members.push_back(member);
	}
	return members;
}

bool BaristaScript::_is_placeholder_fallback_enabled() const {
	return false;
}

godot::Variant BaristaScript::_get_rpc_config() const {
	return godot::Dictionary();
}

bool BaristaScript::_instance_has(godot::Object *p_object) const {
	BSInstance *instance = BSInstance::from_owner(p_object);
	return instance != nullptr && instances.has(instance);
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
