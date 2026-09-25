/**************************************************************************/
/*  bs_script_instance.cpp                                                */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_script_instance.h"

#include "barista_script.h"
#include "barista_script_language.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/godot.hpp>

namespace barista_script {

namespace {

BSInstance *instance_of(GDExtensionScriptInstanceDataPtr p_instance) {
	return reinterpret_cast<BSInstance *>(p_instance);
}

GDExtensionBool instance_set(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) {
	BSInstance *instance = instance_of(p_instance);
	if (instance == nullptr) {
		return false;
	}
	return instance->set_property(*reinterpret_cast<const StringName *>(p_name), *reinterpret_cast<const Variant *>(p_value));
}

GDExtensionBool instance_get(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_value) {
	BSInstance *instance = instance_of(p_instance);
	if (instance == nullptr) {
		return false;
	}
	Variant value;
	if (!instance->get_property(*reinterpret_cast<const StringName *>(p_name), value)) {
		return false;
	}
	*reinterpret_cast<Variant *>(r_value) = value;
	return true;
}

const GDExtensionPropertyInfo *instance_get_property_list(GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) {
	BSInstance *instance = instance_of(p_instance);
	// The C list borrows the names from this list and the matching free callback destroys it, so it
	// has to outlive the call rather than live on this frame.
	List<PropertyInfo> *properties = memnew(List<PropertyInfo>);
	if (instance != nullptr && instance->script.is_valid()) {
		const Vector<StringName> &names = instance->script->get_member_names();
		for (int index = 0; index < names.size(); index++) {
			const Variant::Type carrier = instance->script->get_member_carrier(index);
			properties->push_back(PropertyInfo(carrier, names[index], PROPERTY_HINT_NONE, "",
					carrier == Variant::NIL
							? uint32_t(PROPERTY_USAGE_SCRIPT_VARIABLE | PROPERTY_USAGE_NIL_IS_VARIANT)
							: uint32_t(PROPERTY_USAGE_SCRIPT_VARIABLE)));
		}
	}
	return godot::internal::create_c_property_list(properties, r_count);
}

void instance_free_property_list(GDExtensionScriptInstanceDataPtr, const GDExtensionPropertyInfo *p_list, uint32_t) {
	godot::internal::free_c_property_list(const_cast<GDExtensionPropertyInfo *>(p_list));
}

GDExtensionBool instance_get_class_category(GDExtensionScriptInstanceDataPtr, GDExtensionPropertyInfo *) {
	// The inspector's per-script category is editor presentation, which the runtime does not supply.
	return false;
}

GDExtensionVariantType instance_get_property_type(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) {
	BSInstance *instance = instance_of(p_instance);
	Variant value;
	if (instance != nullptr && instance->get_property(*reinterpret_cast<const StringName *>(p_name), value)) {
		*r_is_valid = true;
		return (GDExtensionVariantType)value.get_type();
	}
	*r_is_valid = false;
	return GDEXTENSION_VARIANT_TYPE_NIL;
}

GDExtensionBool instance_validate_property(GDExtensionScriptInstanceDataPtr, GDExtensionPropertyInfo *) {
	// No property is rewritten on its way to the inspector.
	return false;
}

GDExtensionBool instance_property_can_revert(GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr) {
	return false;
}

GDExtensionBool instance_property_get_revert(GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr, GDExtensionVariantPtr) {
	return false;
}

GDExtensionObjectPtr instance_get_owner(GDExtensionScriptInstanceDataPtr p_instance) {
	BSInstance *instance = instance_of(p_instance);
	return instance != nullptr && instance->owner != nullptr ? instance->owner->_owner : nullptr;
}

void instance_get_property_state(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionScriptInstancePropertyStateAdd p_add, void *p_userdata) {
	BSInstance *instance = instance_of(p_instance);
	if (instance == nullptr || instance->script.is_null()) {
		return;
	}
	const Vector<StringName> &names = instance->script->get_member_names();
	for (int i = 0; i < names.size() && i < instance->members.size(); i++) {
		p_add(&names[i], &instance->members[i], p_userdata);
	}
}

const GDExtensionMethodInfo *instance_get_method_list(GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) {
	BSInstance *instance = instance_of(p_instance);
	Vector<StringName> names;
	Vector<int> argument_counts;
	if (instance != nullptr && instance->script.is_valid()) {
		instance->script->collect_method_signatures(names, argument_counts);
	}
	*r_count = (uint32_t)names.size();
	if (names.is_empty()) {
		return nullptr;
	}
	GDExtensionMethodInfo *list = memnew_arr(GDExtensionMethodInfo, names.size());
	for (int i = 0; i < names.size(); i++) {
		GDExtensionMethodInfo &entry = list[i];
		entry = {};
		entry.name = memnew(StringName(names[i]));
		entry.return_value.type = GDEXTENSION_VARIANT_TYPE_NIL;
		entry.return_value.name = memnew(StringName);
		entry.return_value.class_name = memnew(StringName);
		entry.return_value.hint_string = memnew(String);
		entry.return_value.usage = PROPERTY_USAGE_NIL_IS_VARIANT;
		entry.flags = METHOD_FLAG_NORMAL;
		entry.argument_count = (uint32_t)argument_counts[i];
		entry.arguments = argument_counts[i] > 0 ? memnew_arr(GDExtensionPropertyInfo, argument_counts[i]) : nullptr;
		for (int argument = 0; argument < argument_counts[i]; argument++) {
			GDExtensionPropertyInfo &info = entry.arguments[argument];
			info = {};
			info.type = GDEXTENSION_VARIANT_TYPE_NIL;
			info.name = memnew(StringName(vformat("argument%d", argument)));
			info.class_name = memnew(StringName);
			info.hint_string = memnew(String);
			info.usage = PROPERTY_USAGE_NIL_IS_VARIANT;
		}
	}
	return list;
}

void instance_free_method_list(GDExtensionScriptInstanceDataPtr, const GDExtensionMethodInfo *p_list, uint32_t p_count) {
	if (p_list == nullptr) {
		return;
	}
	GDExtensionMethodInfo *list = const_cast<GDExtensionMethodInfo *>(p_list);
	for (uint32_t i = 0; i < p_count; i++) {
		memdelete(reinterpret_cast<StringName *>(list[i].name));
		memdelete(reinterpret_cast<StringName *>(list[i].return_value.name));
		memdelete(reinterpret_cast<StringName *>(list[i].return_value.class_name));
		memdelete(reinterpret_cast<String *>(list[i].return_value.hint_string));
		for (uint32_t argument = 0; argument < list[i].argument_count; argument++) {
			memdelete(reinterpret_cast<StringName *>(list[i].arguments[argument].name));
			memdelete(reinterpret_cast<StringName *>(list[i].arguments[argument].class_name));
			memdelete(reinterpret_cast<String *>(list[i].arguments[argument].hint_string));
		}
		if (list[i].arguments != nullptr) {
			memdelete_arr(list[i].arguments);
		}
	}
	memdelete_arr(list);
}

GDExtensionBool instance_has_method(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name) {
	BSInstance *instance = instance_of(p_instance);
	return instance != nullptr && instance->has_method(*reinterpret_cast<const StringName *>(p_name));
}

GDExtensionInt instance_get_method_argument_count(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) {
	BSInstance *instance = instance_of(p_instance);
	if (instance != nullptr && instance->script.is_valid()) {
		if (const BSFunction *function = instance->script->find_function(*reinterpret_cast<const StringName *>(p_name))) {
			*r_is_valid = true;
			return function->get_argument_count();
		}
	}
	*r_is_valid = false;
	return 0;
}

void instance_call(GDExtensionScriptInstanceDataPtr p_self, GDExtensionConstStringNamePtr p_method,
		const GDExtensionConstVariantPtr *p_arguments, GDExtensionInt p_argument_count,
		GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
	BSInstance *instance = instance_of(p_self);
	if (instance == nullptr) {
		r_error->error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		*reinterpret_cast<Variant *>(r_return) = Variant();
		return;
	}
	*reinterpret_cast<Variant *>(r_return) = instance->call(*reinterpret_cast<const StringName *>(p_method),
			reinterpret_cast<const Variant **>(const_cast<GDExtensionConstVariantPtr *>(p_arguments)), (int)p_argument_count, *r_error);
}

void instance_notification(GDExtensionScriptInstanceDataPtr p_instance, int32_t p_what, GDExtensionBool p_reversed) {
	BSInstance *instance = instance_of(p_instance);
	if (instance != nullptr) {
		instance->notification(p_what, p_reversed);
	}
}

void instance_to_string(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionBool *r_is_valid, GDExtensionStringPtr r_out) {
	BSInstance *instance = instance_of(p_instance);
	if (instance == nullptr || instance->script.is_null()) {
		*r_is_valid = false;
		return;
	}
	*reinterpret_cast<String *>(r_out) = vformat("[BaristaScript:%s]", instance->script->get_path());
	*r_is_valid = true;
}

void instance_refcount_incremented(GDExtensionScriptInstanceDataPtr) {}

GDExtensionBool instance_refcount_decremented(GDExtensionScriptInstanceDataPtr) {
	// The instance holds no strong reference to its owner, so the owner's own count decides.
	return true;
}

GDExtensionObjectPtr instance_get_script(GDExtensionScriptInstanceDataPtr p_instance) {
	BSInstance *instance = instance_of(p_instance);
	return instance != nullptr && instance->script.is_valid() ? instance->script->_owner : nullptr;
}

GDExtensionBool instance_is_placeholder(GDExtensionScriptInstanceDataPtr) {
	return false;
}

GDExtensionBool instance_set_fallback(GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr, GDExtensionConstVariantPtr) {
	return false;
}

GDExtensionBool instance_get_fallback(GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr, GDExtensionVariantPtr) {
	return false;
}

GDExtensionScriptLanguagePtr instance_get_language(GDExtensionScriptInstanceDataPtr) {
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	return language != nullptr ? language->_owner : nullptr;
}

void instance_free(GDExtensionScriptInstanceDataPtr p_instance) {
	BSInstance *instance = instance_of(p_instance);
	if (instance != nullptr) {
		memdelete(instance);
	}
}

GDExtensionScriptInstanceInfo3 build_vtable() {
	GDExtensionScriptInstanceInfo3 info = {};
	info.set_func = instance_set;
	info.get_func = instance_get;
	info.get_property_list_func = instance_get_property_list;
	info.free_property_list_func = instance_free_property_list;
	info.get_class_category_func = instance_get_class_category;
	info.property_can_revert_func = instance_property_can_revert;
	info.property_get_revert_func = instance_property_get_revert;
	info.get_owner_func = instance_get_owner;
	info.get_property_state_func = instance_get_property_state;
	info.get_method_list_func = instance_get_method_list;
	info.free_method_list_func = instance_free_method_list;
	info.get_property_type_func = instance_get_property_type;
	info.validate_property_func = instance_validate_property;
	info.has_method_func = instance_has_method;
	info.get_method_argument_count_func = instance_get_method_argument_count;
	info.call_func = instance_call;
	info.notification_func = instance_notification;
	info.to_string_func = instance_to_string;
	info.refcount_incremented_func = instance_refcount_incremented;
	info.refcount_decremented_func = instance_refcount_decremented;
	info.get_script_func = instance_get_script;
	info.is_placeholder_func = instance_is_placeholder;
	info.set_fallback_func = instance_set_fallback;
	info.get_fallback_func = instance_get_fallback;
	info.get_language_func = instance_get_language;
	info.free_func = instance_free;
	return info;
}

} // namespace

const GDExtensionScriptInstanceInfo3 *BSInstance::get_vtable() {
	// Built on first use rather than in a static initializer: nothing here is engine-backed, but the
	// rule that keeps Godot values out of static initialization is easiest to keep by not having any.
	static const GDExtensionScriptInstanceInfo3 vtable = build_vtable();
	return &vtable;
}

bool BSInstance::vtable_is_complete() {
	const GDExtensionScriptInstanceInfo3 *vtable = get_vtable();
	// Every callback the engine may invoke, in the order the struct declares them. A null slot here
	// would be a null-pointer call the first time the engine asked for it.
	return vtable->set_func && vtable->get_func && vtable->get_property_list_func &&
			vtable->free_property_list_func && vtable->get_class_category_func &&
			vtable->property_can_revert_func && vtable->property_get_revert_func &&
			vtable->get_owner_func && vtable->get_property_state_func &&
			vtable->get_method_list_func && vtable->free_method_list_func &&
			vtable->get_property_type_func && vtable->validate_property_func &&
			vtable->has_method_func && vtable->get_method_argument_count_func &&
			vtable->call_func && vtable->notification_func && vtable->to_string_func &&
			vtable->refcount_incremented_func && vtable->refcount_decremented_func &&
			vtable->get_script_func && vtable->is_placeholder_func && vtable->set_fallback_func &&
			vtable->get_fallback_func && vtable->get_language_func && vtable->free_func;
}

BSInstance *BSInstance::from_owner(const Object *p_owner) {
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	if (p_owner == nullptr || language == nullptr) {
		return nullptr;
	}
	return reinterpret_cast<BSInstance *>(
			godot::gdextension_interface::object_get_script_instance(p_owner->_owner, language->_owner));
}

BSInstance::~BSInstance() {
	if (script.is_valid()) {
		script->notify_instance_freed(this);
	}
}

bool BSInstance::set_property(const StringName &p_name, const Variant &p_value) {
	if (script.is_null()) {
		return false;
	}
	const int index = script->get_member_index(p_name);
	if (index < 0 || index >= members.size()) {
		return false;
	}
	// A store from outside a compiled function meets the same declaration a compiled store does: a
	// declared carrier takes the value converted, or refuses it. Storing whatever arrived would let
	// the inspector or a scene put a value in a slot the script's own code cannot produce.
	Variant converted;
	String conversion_error;
	const BSRuntimeType *type = script->get_member_type(index);
	if (type == nullptr || !type->convert(p_value, converted, conversion_error, true)) {
		return false;
	}
	members.write[index] = converted;
	return true;
}

bool BSInstance::get_property(const StringName &p_name, Variant &r_value) const {
	if (script.is_null()) {
		return false;
	}
	const int index = script->get_member_index(p_name);
	if (index < 0 || index >= members.size()) {
		return false;
	}
	r_value = members[index];
	return true;
}

bool BSInstance::has_method(const StringName &p_name) const {
	return script.is_valid() && script->find_function(p_name) != nullptr;
}

Variant BSInstance::call(const StringName &p_method, const Variant **p_arguments, int p_argument_count, GDExtensionCallError &r_error) {
	r_error.error = GDEXTENSION_CALL_OK;
	if (script.is_null()) {
		r_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		return Variant();
	}
	BSFunction *function = script->find_function(p_method);
	if (function == nullptr) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return Variant();
	}
	return function->call(this, p_arguments, p_argument_count, r_error);
}

void BSInstance::notification(int p_what, bool p_reversed) {
	if (script.is_null()) {
		return;
	}
	// Godot delivers a notification to the whole script chain; `p_reversed` names the direction the
	// engine walked it in, and is passed on unchanged so a script can tell the two apart.
	BSFunction *function = script->find_function(SNAME("_notification"));
	if (function == nullptr) {
		return;
	}
	const Variant what = p_what;
	const Variant reversed = p_reversed;
	const Variant *arguments[2] = { &what, &reversed };
	GDExtensionCallError error;
	function->call(this, arguments, function->get_argument_count() >= 2 ? 2 : 1, error);
}

} // namespace barista_script
