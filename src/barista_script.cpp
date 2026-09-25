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

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/godot.hpp>

namespace barista_script {

namespace {

BSDeclarationKind declaration_kind_of_tree(const BSParser::ClassNode *p_class) {
	if (p_class->is_enum_file) {
		return BSDeclarationKind::ENUM;
	}
	if (p_class->is_tuple_file) {
		return BSDeclarationKind::TUPLE;
	}
	if (p_class->trait_name_used) {
		return BSDeclarationKind::TRAIT;
	}
	if (p_class->identifier != nullptr) {
		return p_class->type_parameters.is_empty() ? BSDeclarationKind::CLASS : BSDeclarationKind::GENERIC_CLASS;
	}
	return BSDeclarationKind::NONE;
}

} // namespace

BaristaScript::~BaristaScript() {
	release_compiled_state();
}

void BaristaScript::release_compiled_state(bool p_preserve_declaration) {
	valid = false;
	if (!p_preserve_declaration) {
		declaration_kind_known = false;
		declaration_kind = BSDeclarationKind::NONE;
		compiled_abstract = false;
		compiled_class_ids.clear();
	}
	instance_base_type = godot::StringName();
	base_script_id = 0;
	inner_classes.clear();
	member_names.clear();
	member_carriers.clear();
	member_types.clear();
	member_properties.clear();
	member_setters.clear();
	member_getters.clear();
	member_indices.clear();
	script_properties.clear();
	member_default_values.clear();
	member_lines.clear();
	constants.clear();
	signals.clear();
	signal_order.clear();
	for (const godot::KeyValue<godot::StringName, BSFunction *> &entry : member_functions) {
		memdelete(entry.value);
	}
	member_functions.clear();
	member_function_order.clear();
	if (implicit_initializer != nullptr) {
		memdelete(implicit_initializer);
		implicit_initializer = nullptr;
	}
	static_names.clear();
	static_values.clear();
	static_types.clear();
	static_setters.clear();
	static_getters.clear();
	static_indices.clear();
	if (static_initializer != nullptr) {
		memdelete(static_initializer);
		static_initializer = nullptr;
	}
}

BaristaScript *BaristaScript::get_base_barista_script() const {
	return base_script_id != 0 ? godot::Object::cast_to<BaristaScript>(godot::ObjectDB::get_instance(base_script_id)) : nullptr;
}

BaristaScript *BaristaScript::get_compiled_class(const godot::String &p_fqcn) const {
	const uint64_t *id = compiled_class_ids.getptr(p_fqcn);
	return id != nullptr ? godot::Object::cast_to<BaristaScript>(godot::ObjectDB::get_instance(*id)) : nullptr;
}

BSFunction *BaristaScript::find_function(const godot::StringName &p_name) const {
	for (const BaristaScript *script = this; script != nullptr; script = script->get_base_barista_script()) {
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

int BaristaScript::get_static_index(const godot::StringName &p_name) const {
	const godot::HashMap<godot::StringName, int>::ConstIterator found = static_indices.find(p_name);
	return found ? found->value : -1;
}

bool BaristaScript::get_static_value(int p_index, godot::Variant &r_value) const {
	if (p_index < 0 || p_index >= static_values.size()) {
		return false;
	}
	r_value = static_values[p_index];
	return true;
}

bool BaristaScript::set_static_value(int p_index, const godot::Variant &p_value, godot::String &r_error) {
	if (p_index < 0 || p_index >= static_values.size() || p_index >= static_types.size()) {
		return false;
	}
	godot::Variant converted;
	if (!static_types[p_index].convert(p_value, converted, r_error, true)) {
		r_error = vformat(R"(Cannot assign the static variable "%s": %s)", String(static_names[p_index]), r_error);
		return false;
	}
	static_values.write[p_index] = converted;
	return true;
}

bool BaristaScript::get_static_property(const godot::StringName &p_name, godot::Variant &r_value) {
	for (BaristaScript *script = this; script != nullptr; script = script->get_base_barista_script()) {
		if (const godot::Variant *constant = script->constants.getptr(p_name)) {
			r_value = *constant;
			return true;
		}
		const int index = script->get_static_index(p_name);
		if (index < 0) {
			continue;
		}
		if (index < script->static_getters.size() && script->static_getters[index] != godot::StringName()) {
			GDExtensionCallError error;
			r_value = call_static(script->static_getters[index], nullptr, 0, error, this);
			return error.error == GDEXTENSION_CALL_OK;
		}
		return script->get_static_value(index, r_value);
	}
	return false;
}

bool BaristaScript::set_static_property(const godot::StringName &p_name, const godot::Variant &p_value, godot::String &r_error) {
	for (BaristaScript *script = this; script != nullptr; script = script->get_base_barista_script()) {
		const int index = script->get_static_index(p_name);
		if (index < 0) {
			continue;
		}
		godot::Variant converted;
		if (!script->static_types[index].convert(p_value, converted, r_error, true)) {
			r_error = vformat(R"(Cannot assign the static variable "%s": %s)", String(p_name), r_error);
			return false;
		}
		if (index < script->static_setters.size() && script->static_setters[index] != godot::StringName()) {
			const godot::Variant *arguments[1] = { &converted };
			GDExtensionCallError error;
			call_static(script->static_setters[index], arguments, 1, error, this);
			return error.error == GDEXTENSION_CALL_OK;
		}
		script->static_values.write[index] = converted;
		return true;
	}
	return false;
}

void BaristaScript::collect_method_signatures(godot::Vector<godot::StringName> &r_names, godot::Vector<int> &r_argument_counts) const {
	for (const BaristaScript *script = this; script != nullptr; script = script->get_base_barista_script()) {
		for (const godot::StringName &name : script->member_function_order) {
			if (r_names.find(name) < 0) {
				BSFunction *const *function_ptr = script->member_functions.getptr(name);
				if (function_ptr == nullptr || *function_ptr == nullptr) {
					continue;
				}
				BSFunction *function = *function_ptr;
				r_names.push_back(name);
				r_argument_counts.push_back(function->get_argument_count());
			}
		}
	}
}

godot::Error BaristaScript::compile() {
	declaration_kind_known = false;
	declaration_kind = BSDeclarationKind::NONE;
	compiled_abstract = false;
	// `compile_class_tree()` installs the new exact identities before compiling each registered
	// class, whose reset must preserve them. Clear the previous generation here at the outer edge.
	compiled_class_ids.clear();
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
		const BSParser::ClassNode *root = parser.get_tree();
		if (root != nullptr) {
			declaration_kind_known = true;
			declaration_kind = declaration_kind_of_tree(root);
			compiled_abstract = root->is_abstract;
		}
		status = analyzer.analyze();
	}
	if (status != godot::OK || !parser.get_errors().is_empty()) {
		release_compiled_state(declaration_kind_known);
		compile_error = parser.get_errors().is_empty()
				? String("The script could not be analyzed.")
				: parser.get_errors().front()->get().message;
		_update_exports();
		return godot::ERR_COMPILATION_FAILED;
	}

	BSCompiler compiler;
	if (compiler.compile(&parser, this) != godot::OK) {
		// Nothing half-built survives a failed compilation: the script is invalid and holds no
		// function, so `_can_instantiate` is false and `_instance_create` yields nothing.
		release_compiled_state(true);
		compiled_class_ids.clear();
		compile_error = compiler.get_error();
		_update_exports();
		return godot::ERR_COMPILATION_FAILED;
	}
	_update_exports();
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

void BaristaScript::_placeholder_erased(void *p_placeholder) {
	placeholders.erase(p_placeholder);
}

bool BaristaScript::_can_instantiate() const {
	// Two separate questions, and both have to be yes: the source compiled, and the declaration it
	// carries is one that may be instantiated at all. An `abstract class_name` compiles perfectly
	// well and is still not something the Create Node dialog may offer.
	return valid && !_is_abstract();
}

godot::Ref<godot::Script> BaristaScript::_get_base_script() const {
	BaristaScript *base = get_base_barista_script();
	return base != nullptr ? godot::Ref<godot::Script>(base) : godot::Ref<godot::Script>();
}

godot::StringName BaristaScript::_get_global_name() const {
	// The qualified name, always: `namespace app.combat` + `class_name Weapon` is
	// `app.combat.Weapon` here exactly as it is in the global class registry, because both come
	// from `bs_build_qualified_global_name()`.
	const godot::String name = resolve_global_class().name;
	return name.is_empty() ? godot::StringName() : godot::StringName(name);
}

bool BaristaScript::_inherits_script(const godot::Ref<godot::Script> &p_script) const {
	for (const BaristaScript *base = get_base_barista_script(); base != nullptr; base = base->get_base_barista_script()) {
		if (base == p_script.ptr()) {
			return true;
		}
	}
	return false;
}

godot::StringName BaristaScript::_get_instance_base_type() const {
	return instance_base_type;
}

bool BaristaScript::run_initializers(BSInstance *p_instance) const {
	godot::Vector<const BaristaScript *> chain;
	for (const BaristaScript *script = this; script != nullptr;) {
		chain.push_back(script);
		const BaristaScript *base = script->get_base_barista_script();
		if (script->base_script_id != 0 && base == nullptr) {
			bs_report_runtime_error(
					vformat(R"(Cannot initialize inner class "%s": its base class is no longer alive.)", String(get_name())),
					"<implicit initializer>", get_path(), 0);
			return false;
		}
		script = base;
	}
	for (int index = chain.size() - 1; index >= 0; index--) {
		if (chain[index]->implicit_initializer == nullptr) {
			continue;
		}
		GDExtensionCallError initializer_error;
		chain[index]->implicit_initializer->call(p_instance, nullptr, 0, initializer_error);
		if (initializer_error.error != GDEXTENSION_CALL_OK || bs_runtime_error_was_reported()) {
			return false;
		}
	}
	return true;
}

void *BaristaScript::create_instance_handle(godot::Object *p_owner, const godot::Variant **p_arguments,
		int p_argument_count) const {
	BaristaScript *self = const_cast<BaristaScript *>(this);
	BSInstance *instance = memnew(BSInstance);
	instance->owner = p_owner;
	instance->script = godot::Ref<BaristaScript>(self);
	for (BaristaScript *base = get_base_barista_script(); base != nullptr; base = base->get_base_barista_script()) {
		instance->retained_bases.push_back(godot::Ref<BaristaScript>(base));
	}
	instance->members.resize(member_names.size());
	self->instances.insert(instance);

	// The initializers run before the engine is handed anything. They are what decides whether an
	// instance exists at all: one that raised has members the declaration does not describe, and
	// handing it over would put a half-built object into the scene. Running them first also means
	// `self.method()` works inside them, because the instance answers for its own script from the
	// moment it is built, without waiting to be attached.
	instance->initializing = true;
	bool initialized = run_initializers(instance);
	if (initialized) {
		if (BSFunction *initializer = find_function(SNAME("_init"))) {
			GDExtensionCallError initializer_error;
			initializer_error.error = GDEXTENSION_CALL_OK;
			initializer->call(instance, p_arguments, p_argument_count, initializer_error);
			initialized = initializer_error.error == GDEXTENSION_CALL_OK && !bs_runtime_error_was_reported();
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
	return create_instance_handle(p_for_object, nullptr, 0);
}

godot::Variant BaristaScript::instantiate(const godot::Variant **p_arguments, int p_argument_count,
		GDExtensionCallError &r_error) {
	r_error.error = GDEXTENSION_CALL_OK;
	if (!_can_instantiate()) {
		r_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		return godot::Variant();
	}
	GDExtensionObjectPtr raw = godot::gdextension_interface::classdb_construct_object3(instance_base_type._native_ptr());
	if (raw == nullptr) {
		r_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		return godot::Variant();
	}
	godot::Object *owner = godot::internal::get_object_instance_binding(raw);
	void *handle = create_instance_handle(owner, p_arguments, p_argument_count);
	if (handle == nullptr) {
		godot::gdextension_interface::object_destroy(raw);
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return godot::Variant();
	}
	godot::gdextension_interface::object_set_script_instance(raw, handle);
	owner->notification(godot::Object::NOTIFICATION_POSTINITIALIZE);
	if (godot::RefCounted *ref_counted = godot::Object::cast_to<godot::RefCounted>(owner)) {
		// `classdb_construct_object3` hands the extension the engine's initial reference. Capture it
		// without incrementing once more; the returned Variant takes the reference it needs.
		return godot::Ref<godot::RefCounted>::_gde_internal_constructor(ref_counted);
	}
	return owner;
}

void *BaristaScript::_placeholder_instance_create(godot::Object *p_for_object) const {
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	if (language == nullptr || p_for_object == nullptr) {
		return nullptr;
	}
	void *placeholder = godot::gdextension_interface::placeholder_script_instance_create(language->_owner,
			const_cast<BaristaScript *>(this)->_owner, p_for_object->_owner);
	if (placeholder != nullptr) {
		placeholders.insert(placeholder);
		const_cast<BaristaScript *>(this)->_update_exports();
	}
	return placeholder;
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

bool BaristaScript::_has_static_method(const godot::StringName &p_method) const {
	const BSFunction *function = find_function(p_method);
	return function != nullptr && function->is_static();
}

godot::Variant BaristaScript::call_static(const godot::StringName &p_method, const godot::Variant **p_arguments,
		int p_argument_count, GDExtensionCallError &r_error, BaristaScript *p_receiver) {
	BSFunction *function = find_function(p_method);
	if (function == nullptr || !function->is_static()) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return godot::Variant();
	}
	return function->call(nullptr, p_arguments, p_argument_count, r_error,
			p_receiver != nullptr ? p_receiver : this);
}

godot::Variant BaristaScript::_get_script_method_argument_count(const godot::StringName &p_method) const {
	const BSFunction *function = find_function(p_method);
	return function != nullptr ? godot::Variant(function->get_argument_count()) : godot::Variant();
}

godot::Dictionary BaristaScript::_get_method_info(const godot::StringName &p_method) const {
	const BSFunction *function = find_function(p_method);
	return function != nullptr ? godot::Dictionary(function->get_method_info()) : godot::Dictionary();
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
	if (declaration_kind_known) {
		return !bs_declaration_kind_is_instantiable(declaration_kind) || compiled_abstract;
	}
	return resolve_global_class().is_abstract;
}

godot::ScriptLanguage *BaristaScript::_get_language() const {
	return BaristaScriptLanguage::get_singleton();
}

bool BaristaScript::_has_script_signal(const godot::StringName &p_signal) const {
	for (const BaristaScript *script = this; script != nullptr; script = script->get_base_barista_script()) {
		if (script->signals.has(p_signal)) {
			return true;
		}
	}
	return false;
}

godot::TypedArray<godot::Dictionary> BaristaScript::_get_script_signal_list() const {
	godot::TypedArray<godot::Dictionary> result;
	godot::HashSet<godot::StringName> seen;
	for (const BaristaScript *script = this; script != nullptr; script = script->get_base_barista_script()) {
		for (const godot::StringName &name : script->signal_order) {
			if (seen.has(name)) {
				continue;
			}
			const godot::MethodInfo *signal = script->signals.getptr(name);
			if (signal != nullptr) {
				result.push_back(godot::Dictionary(*signal));
				seen.insert(name);
			}
		}
	}
	return result;
}

bool BaristaScript::_has_property_default_value(const godot::StringName &p_property) const {
	for (const BaristaScript *script = this; script != nullptr; script = script->get_base_barista_script()) {
		if (script->member_default_values.has(p_property)) {
			return true;
		}
	}
	return false;
}

godot::Variant BaristaScript::_get_property_default_value(const godot::StringName &p_property) const {
	for (const BaristaScript *script = this; script != nullptr; script = script->get_base_barista_script()) {
		if (const godot::Variant *value = script->member_default_values.getptr(p_property)) {
			return *value;
		}
	}
	return godot::Variant();
}

void BaristaScript::_update_exports() {
	const godot::TypedArray<godot::Dictionary> properties = _get_script_property_list();
	godot::Dictionary defaults;
	for (const godot::StringName &name : member_names) {
		if (_has_property_default_value(name)) {
			defaults[name] = _get_property_default_value(name);
		}
	}
	for (void *placeholder : placeholders) {
		godot::gdextension_interface::placeholder_script_instance_update(placeholder,
				properties._native_ptr(), defaults._native_ptr());
	}
}

godot::TypedArray<godot::Dictionary> BaristaScript::_get_script_method_list() const {
	godot::TypedArray<godot::Dictionary> result;
	godot::HashSet<godot::StringName> seen;
	for (const BaristaScript *script = this; script != nullptr; script = script->get_base_barista_script()) {
		for (const godot::StringName &name : script->member_function_order) {
			if (seen.has(name)) {
				continue;
			}
			BSFunction *const *function_ptr = script->member_functions.getptr(name);
			if (function_ptr != nullptr && *function_ptr != nullptr) {
				result.push_back(godot::Dictionary((*function_ptr)->get_method_info()));
				seen.insert(name);
			}
		}
	}
	return result;
}

godot::TypedArray<godot::Dictionary> BaristaScript::_get_script_property_list() const {
	godot::TypedArray<godot::Dictionary> result;
	for (const BaristaScript *script = this; script != nullptr; script = script->get_base_barista_script()) {
		for (const godot::PropertyInfo &property : script->script_properties) {
			result.push_back(godot::Dictionary(property));
		}
	}
	return result;
}

int32_t BaristaScript::_get_member_line(const godot::StringName &p_member) const {
	for (const BaristaScript *script = this; script != nullptr; script = script->get_base_barista_script()) {
		if (const int *line = script->member_lines.getptr(p_member)) {
			return *line;
		}
	}
	return -1;
}

godot::Dictionary BaristaScript::_get_constants() const {
	godot::Dictionary result;
	for (const BaristaScript *script = this; script != nullptr; script = script->get_base_barista_script()) {
		for (const godot::KeyValue<godot::StringName, godot::Variant> &entry : script->constants) {
			if (!result.has(entry.key)) {
				result[entry.key] = entry.value;
			}
		}
	}
	return result;
}

godot::TypedArray<godot::StringName> BaristaScript::_get_members() const {
	godot::TypedArray<godot::StringName> members;
	for (const godot::StringName &member : member_names) {
		members.push_back(member);
	}
	return members;
}

bool BaristaScript::_is_placeholder_fallback_enabled() const {
	return true;
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
