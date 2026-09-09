// Copyright (c) 2026-present Cafecito Games LLC.
// This file is part of BaristaScript, a Godot GDExtension.
// SPDX-License-Identifier: MIT

// Analyzer-only adaptation of Foundry fs_utility_functions.cpp:499-511 and
// fs_utility_callable.cpp @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6.
// Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md).
// Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "bs_utility_functions.h"

namespace barista_script {
namespace {
struct UtilityInfo {
	MethodInfo method;
	bool is_constant = false;
};

const Vector<UtilityInfo> &registry() {
	// Like the language's interned names, lifetime ends with the extension, not after its API unload.
	static const Vector<UtilityInfo> *entries = []() {
		Vector<UtilityInfo> *result = memnew(Vector<UtilityInfo>);
#define RET(m_type) PropertyInfo(Variant::m_type, "")
#define RETCLS(m_class) PropertyInfo(Variant::OBJECT, "", PROPERTY_HINT_RESOURCE_TYPE, m_class)
#define NOARGS MethodInfo()
#define ARGS(...) MethodInfo("", __VA_ARGS__)
#define ARG(m_name, m_type) PropertyInfo(Variant::m_type, m_name)
#define ARGVAR(m_name) PropertyInfo(Variant::NIL, m_name, PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)
#define REGISTER_FUNC(m_func, m_is_const, m_return, m_args, m_is_vararg, m_defaults) \
	{                                                                                \
		UtilityInfo entry;                                                           \
		String name(#m_func);                                                        \
		entry.method = m_args;                                                       \
		entry.method.name = name.begins_with("_") ? name.substr(1) : name;           \
		entry.method.return_val = m_return;                                          \
		entry.is_constant = m_is_const;                                              \
		if (m_is_vararg) {                                                           \
			entry.method.flags |= METHOD_FLAG_VARARG;                                \
		}                                                                            \
		result->push_back(entry);                                                    \
	}
		// Exact producer lines fs_utility_functions.cpp:501-511. All defaults are empty; the
		// unused macro token is deliberately not evaluated (there is no runtime registration).
		/* clang-format off */
	REGISTER_FUNC( type_exists,    true,  RET(BOOL),          ARGS( ARG("type", STRING_NAME)        ), false, varray(     ));
	REGISTER_FUNC( _char,          true,  RET(STRING),        ARGS( ARG("code", INT)                ), false, varray(     ));
	REGISTER_FUNC( ord,            true,  RET(INT),           ARGS( ARG("char", STRING)             ), false, varray(     ));
	REGISTER_FUNC( range,          false, RET(ARRAY),         NOARGS,                                  true,  varray(     ));
	REGISTER_FUNC( load,           false, RETCLS("Resource"), ARGS( ARG("path", STRING)             ), false, varray(     ));
	REGISTER_FUNC( print_debug,    false, RET(NIL),           NOARGS,                                  true,  varray(     ));
	REGISTER_FUNC( print_stack,    false, RET(NIL),           NOARGS,                                  false, varray(     ));
	REGISTER_FUNC( get_stack,      false, RET(ARRAY),         NOARGS,                                  false, varray(     ));
	REGISTER_FUNC( len,            true,  RET(INT),           ARGS( ARGVAR("var")                   ), false, varray(     ));
	REGISTER_FUNC( is_instance_of, true,  RET(BOOL),          ARGS( ARGVAR("value"), ARGVAR("type") ), false, varray(     ));
	REGISTER_FUNC( create_proxy_dynamic, false, RET(OBJECT),  ARGS( ARG("type", OBJECT), ARG("handler", CALLABLE) ), false, varray( ));
		/* clang-format on */
#undef REGISTER_FUNC
#undef ARGVAR
#undef ARG
#undef ARGS
#undef NOARGS
#undef RETCLS
#undef RET
		return result;
	}();
	return *entries;
}

class AnalyzerUtilityCallable final : public CallableCustom {
	StringName name;
	static bool equal(const CallableCustom *p_a, const CallableCustom *p_b) {
		return static_cast<const AnalyzerUtilityCallable *>(p_a)->name == static_cast<const AnalyzerUtilityCallable *>(p_b)->name;
	}
	static bool less(const CallableCustom *p_a, const CallableCustom *p_b) {
		return static_cast<const AnalyzerUtilityCallable *>(p_a)->name < static_cast<const AnalyzerUtilityCallable *>(p_b)->name;
	}

public:
	static bool recognizes(const Callable &p_callable) {
		const CallableCustom *custom = p_callable.is_custom() ? p_callable.get_custom() : nullptr;
		return custom != nullptr && custom->get_compare_equal_func() == equal;
	}
	explicit AnalyzerUtilityCallable(const StringName &p_name) : name(p_name) {}
	uint32_t hash() const override { return name.hash(); }
	String get_as_text() const override { return "@BaristaScriptAnalyzer::" + String(name); }
	CompareEqualFunc get_compare_equal_func() const override { return equal; }
	CompareLessFunc get_compare_less_func() const override { return less; }
	bool is_valid() const override { return false; } // Identity only, never executable runtime support.
	ObjectID get_object() const override { return ObjectID(); }
	int get_argument_count(bool &r_valid) const override {
		MethodInfo info;
		r_valid = BSUtilityFunctions::get_function_info(name, info);
		return r_valid ? info.arguments.size() : 0;
	}
	void call(const Variant **, int, Variant &r_value, GDExtensionCallError &r_error) const override {
		r_value = Variant();
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		r_error.argument = 0;
		r_error.expected = 0;
	}
};
} // namespace

bool BSUtilityFunctions::get_function_info(const StringName &p_name, MethodInfo &r_info) {
	for (const UtilityInfo &entry : registry()) {
		if (entry.method.name == p_name) {
			r_info = entry.method;
			return true;
		}
	}
	return false;
}

List<MethodInfo> BSUtilityFunctions::get_function_list() {
	List<MethodInfo> result;
	for (const UtilityInfo &entry : registry()) {
		result.push_back(entry.method);
	}
	return result;
}

bool BSUtilityFunctions::is_function_constant(const StringName &p_name) {
	for (const UtilityInfo &entry : registry()) {
		if (entry.method.name == p_name) {
			return entry.is_constant;
		}
	}
	return false;
}

Callable BSUtilityFunctions::make_analyzer_callable(const StringName &p_name) {
	MethodInfo info;
	ERR_FAIL_COND_V(!get_function_info(p_name, info), Callable());
	return Callable(memnew(AnalyzerUtilityCallable(p_name)));
}

bool BSUtilityFunctions::is_analyzer_callable(const Callable &p_callable) {
	return AnalyzerUtilityCallable::recognizes(p_callable);
}

BSUtilityFunctions::ConstantResult BSUtilityFunctions::evaluate_constant(const StringName &p_name, const Vector<Variant> &p_arguments, Variant &r_value, String &r_error) {
	if (!is_function_constant(p_name)) {
		return ConstantResult::NOT_CONSTANT;
	}
	MethodInfo info;
	get_function_info(p_name, info);
	// Signature validation belongs to CallSiteValidationContext. Guard its precondition without
	// attempting a second conversion/arity policy here.
	ERR_FAIL_COND_V(p_arguments.size() != info.arguments.size(), ConstantResult::NOT_CONSTANT);
	if (p_name == SNAME("type_exists")) {
		r_value = ClassDB::class_exists(p_arguments[0]);
	} else if (p_name == SNAME("char")) {
		const int64_t code = p_arguments[0];
		if (code < 0 || code > UINT32_MAX) {
			r_error = "Expected an integer between 0 and 2^32 - 1.";
			return ConstantResult::INVALID_ARGUMENT;
		}
		r_value = String::chr(code);
	} else if (p_name == SNAME("ord")) {
		const String text = p_arguments[0];
		if (text.length() != 1) {
			r_error = "Expected a string of length 1 (a character).";
			return ConstantResult::INVALID_ARGUMENT;
		}
		r_value = text.unicode_at(0);
	} else if (p_name == SNAME("len")) {
		const Variant &value = p_arguments[0];
		if (value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME) {
			r_value = String(value).length();
		} else if (value.get_type() == Variant::ARRAY || value.get_type() == Variant::DICTIONARY ||
				(value.get_type() >= Variant::PACKED_BYTE_ARRAY && value.get_type() <= Variant::PACKED_VECTOR4_ARRAY)) {
			// Engine-owned built-in size operation, never arbitrary Object/script dispatch.
			Variant container = value;
			r_value = container.call(SNAME("size"));
		} else {
			r_error = vformat("Value of type '%s' can't provide a length.", Variant::get_type_name(value.get_type()));
			return ConstantResult::INVALID_ARGUMENT;
		}
	} else if (p_name == SNAME("is_instance_of")) {
		if (p_arguments[1].get_type() == Variant::OBJECT) {
			// Runtime script/native/trait identity and freed-instance checks belong to M4/M7.
			return ConstantResult::NOT_CONSTANT;
		}
		if (p_arguments[1].get_type() != Variant::INT || int64_t(p_arguments[1]) < 0 || int64_t(p_arguments[1]) >= Variant::VARIANT_MAX) {
			r_error = "Invalid type argument for is_instance_of(), use TYPE_* constants for built-in types.";
			return ConstantResult::INVALID_ARGUMENT;
		}
		r_value = p_arguments[0].get_type() == int64_t(p_arguments[1]);
	} else {
		return ConstantResult::NOT_CONSTANT;
	}
	return ConstantResult::FOLDED;
}

} // namespace barista_script
