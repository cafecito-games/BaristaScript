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
		const TypedArray<Dictionary> declared = instance->script->_get_script_property_list();
		for (const Dictionary &entry : declared) {
			PropertyInfo property = PropertyInfo::from_dict(entry);
			instance->validate_property(property);
			properties->push_back(property);
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
	if (instance != nullptr && instance->script.is_valid()) {
		const int index = instance->script->get_member_index(*reinterpret_cast<const StringName *>(p_name));
		const PropertyInfo *property = instance->script->get_member_property(index);
		if (property == nullptr) {
			*r_is_valid = false;
			return GDEXTENSION_VARIANT_TYPE_NIL;
		}
		*r_is_valid = true;
		return (GDExtensionVariantType)property->type;
	}
	*r_is_valid = false;
	return GDEXTENSION_VARIANT_TYPE_NIL;
}

GDExtensionBool instance_validate_property(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionPropertyInfo *p_property) {
	BSInstance *instance = instance_of(p_instance);
	if (instance == nullptr || p_property == nullptr || !instance->has_method(SNAME("_validate_property")) ||
			p_property->name == nullptr || p_property->class_name == nullptr || p_property->hint_string == nullptr) {
		return false;
	}
	PropertyInfo property(p_property);
	if (!instance->validate_property(property)) {
		return false;
	}
	p_property->type = (GDExtensionVariantType)property.type;
	*reinterpret_cast<StringName *>(p_property->name) = property.name;
	*reinterpret_cast<StringName *>(p_property->class_name) = property.class_name;
	p_property->hint = property.hint;
	*reinterpret_cast<String *>(p_property->hint_string) = property.hint_string;
	p_property->usage = property.usage;
	return true;
}

GDExtensionBool instance_property_can_revert(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name) {
	BSInstance *instance = instance_of(p_instance);
	return instance != nullptr && instance->property_can_revert(*reinterpret_cast<const StringName *>(p_name));
}

GDExtensionBool instance_property_get_revert(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_value) {
	BSInstance *instance = instance_of(p_instance);
	Variant value;
	if (instance == nullptr || !instance->property_get_revert(*reinterpret_cast<const StringName *>(p_name), value)) {
		return false;
	}
	*reinterpret_cast<Variant *>(r_value) = value;
	return true;
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
	const TypedArray<Dictionary> methods = instance != nullptr && instance->script.is_valid()
			? instance->script->_get_script_method_list()
			: TypedArray<Dictionary>();
	*r_count = (uint32_t)methods.size();
	if (methods.is_empty()) {
		return nullptr;
	}
	GDExtensionMethodInfo *list = memnew_arr(GDExtensionMethodInfo, methods.size());
	auto assign_property = [](GDExtensionPropertyInfo &r_target, const PropertyInfo &p_source) {
		r_target = {};
		r_target.type = (GDExtensionVariantType)p_source.type;
		r_target.name = memnew(StringName(p_source.name));
		r_target.class_name = memnew(StringName(p_source.class_name));
		r_target.hint = p_source.hint;
		r_target.hint_string = memnew(String(p_source.hint_string));
		r_target.usage = p_source.usage;
	};
	for (int i = 0; i < methods.size(); i++) {
		const MethodInfo method = MethodInfo::from_dict(methods[i]);
		GDExtensionMethodInfo &entry = list[i];
		entry = {};
		entry.name = memnew(StringName(method.name));
		assign_property(entry.return_value, method.return_val);
		entry.flags = method.flags;
		entry.id = method.id;
		entry.argument_count = (uint32_t)method.arguments.size();
		entry.arguments = method.arguments.is_empty() ? nullptr : memnew_arr(GDExtensionPropertyInfo, method.arguments.size());
		for (uint32_t argument = 0; argument < entry.argument_count; argument++) {
			assign_property(entry.arguments[argument], method.arguments[argument]);
		}
		entry.default_argument_count = (uint32_t)method.default_arguments.size();
		entry.default_arguments = method.default_arguments.is_empty()
				? nullptr
				: memnew_arr(GDExtensionVariantPtr, method.default_arguments.size());
		for (uint32_t argument = 0; argument < entry.default_argument_count; argument++) {
			entry.default_arguments[argument] = memnew(Variant(method.default_arguments[argument]));
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
		for (uint32_t argument = 0; argument < list[i].default_argument_count; argument++) {
			memdelete(reinterpret_cast<Variant *>(list[i].default_arguments[argument]));
		}
		if (list[i].default_arguments != nullptr) {
			memdelete_arr(list[i].default_arguments);
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
	GDExtensionCallError error;
	const Variant value = instance->call(SNAME("_to_string"), nullptr, 0, error);
	if (error.error == GDEXTENSION_CALL_OK && value.get_type() == Variant::STRING) {
		*reinterpret_cast<String *>(r_out) = value;
		*r_is_valid = true;
	} else {
		*r_is_valid = false;
	}
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
		String error;
		return script->set_static_property(p_name, p_value, error);
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
	const StringName &setter = script->get_member_setter(index);
	if (setter != StringName()) {
		const Variant *arguments[1] = { &converted };
		GDExtensionCallError error;
		call(setter, arguments, 1, error);
		return error.error == GDEXTENSION_CALL_OK;
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
		return script->get_static_property(p_name, r_value);
	}
	const StringName &getter = script->get_member_getter(index);
	if (getter != StringName()) {
		GDExtensionCallError error;
		r_value = const_cast<BSInstance *>(this)->call(getter, nullptr, 0, error);
		return error.error == GDEXTENSION_CALL_OK;
	}
	r_value = members[index];
	return true;
}

bool BSInstance::validate_property(PropertyInfo &r_property) {
	if (!has_method(SNAME("_validate_property"))) {
		return false;
	}
	Dictionary dictionary = r_property;
	const Variant argument = dictionary;
	const Variant *arguments[1] = { &argument };
	GDExtensionCallError error;
	call(SNAME("_validate_property"), arguments, 1, error);
	if (error.error == GDEXTENSION_CALL_OK) {
		r_property = PropertyInfo::from_dict(dictionary);
		return true;
	}
	return false;
}

bool BSInstance::property_can_revert(const StringName &p_name) {
	if (has_method(SNAME("_property_can_revert"))) {
		const Variant argument = p_name;
		const Variant *arguments[1] = { &argument };
		GDExtensionCallError error;
		const Variant result = call(SNAME("_property_can_revert"), arguments, 1, error);
		if (error.error == GDEXTENSION_CALL_OK && result.get_type() == Variant::BOOL && bool(result)) {
			return true;
		}
	}
	return script.is_valid() && script->_has_property_default_value(p_name);
}

bool BSInstance::property_get_revert(const StringName &p_name, Variant &r_value) {
	if (has_method(SNAME("_property_get_revert"))) {
		const Variant argument = p_name;
		const Variant *arguments[1] = { &argument };
		GDExtensionCallError error;
		const Variant result = call(SNAME("_property_get_revert"), arguments, 1, error);
		if (error.error == GDEXTENSION_CALL_OK && result.get_type() != Variant::NIL) {
			r_value = result;
			return true;
		}
	}
	if (script.is_valid() && script->_has_property_default_value(p_name)) {
		r_value = script->_get_property_default_value(p_name);
		return true;
	}
	return false;
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
	return function->call(function->is_static() ? nullptr : this, p_arguments, p_argument_count, r_error,
			function->is_static() ? script.ptr() : nullptr);
}

void BSInstance::notification(int p_what, bool p_reversed) {
	if (script.is_null()) {
		return;
	}
	const Variant what = p_what;
	const Variant *arguments[1] = { &what };
	Vector<BaristaScript *> chain;
	for (BaristaScript *current = script.ptr(); current != nullptr; current = current->get_base_barista_script()) {
		chain.push_back(current);
	}
	const int start = p_reversed ? 0 : chain.size() - 1;
	const int end = p_reversed ? chain.size() : -1;
	const int step = p_reversed ? 1 : -1;
	for (int index = start; index != end; index += step) {
		BSFunction *const *function_ptr = chain[index]->member_functions.getptr(SNAME("_notification"));
		if (function_ptr != nullptr && *function_ptr != nullptr) {
			GDExtensionCallError error;
			(*function_ptr)->call(this, arguments, 1, error);
		}
	}
}

} // namespace barista_script
