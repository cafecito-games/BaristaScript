/**************************************************************************/
/*  bs_platform_names.h                                                   */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

// Private implementation detail of bs_platform.h. Ported frontend files include the umbrella,
// never this header directly; tests/audit_platform_seam.py enforces that boundary.

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/char_utils.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

/**
 * `SNAME` is a core macro with no godot-cpp counterpart, and `fs_parser.cpp` uses it 39 times. It
 * caches one `StringName` per call site, which is the whole point: constructing a `StringName`
 * crosses the GDExtension interface.
 *
 * The cached object is allocated once and never destroyed. A function-local `StringName` object
 * would run its destructor during static destruction, after the extension has been unloaded and
 * the interface function pointers are gone. Leaking one `StringName` per call site is the cheaper
 * of the two, and matches how the engine treats its own interned names.
 */
#ifdef SNAME
#error "bs_platform_names.h: SNAME is already defined; the seam must own the only definition."
#endif
#define SNAME(m_arg)                                                        \
	([]() -> const godot::StringName & {                                    \
		static godot::StringName *sname = memnew(godot::StringName(m_arg)); \
		return *sname;                                                      \
	}())

/**
 * `StringName` compared against a C string literal. Core declares four such operators on
 * `StringName` and four free ones for the reversed operand order
 * (Foundry `core/string/string_name.h:82,84,197,198` @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6,
 * unchanged from stock Godot); godot-cpp declares none of them, so `name == "export"` is not a
 * missing operator but an *ambiguous* one -- the compiler can convert either side -- and every such
 * comparison in the ported front-end fails to build.
 */
_FORCE_INLINE_ bool bs_string_name_equals_literal(const godot::StringName &p_name, const char *p_literal) {
	return p_name == godot::StringName(p_literal);
}

_FORCE_INLINE_ bool operator==(const godot::StringName &p_name, const char *p_literal) {
	return bs_string_name_equals_literal(p_name, p_literal);
}
_FORCE_INLINE_ bool operator!=(const godot::StringName &p_name, const char *p_literal) {
	return !bs_string_name_equals_literal(p_name, p_literal);
}
_FORCE_INLINE_ bool operator==(const char *p_literal, const godot::StringName &p_name) {
	return bs_string_name_equals_literal(p_name, p_literal);
}
_FORCE_INLINE_ bool operator!=(const char *p_literal, const godot::StringName &p_name) {
	return !bs_string_name_equals_literal(p_name, p_literal);
}

/**
 * `core/string/string_builder.h` is absent from godot-cpp. This preserves its observable API over
 * ordinary concatenation; `FSParser::TreePrinter` is the only consumer and is debug-only.
 */
class StringBuilder {
	uint32_t string_length = 0;
	godot::LocalVector<godot::String> strings;

public:
	StringBuilder &append(const godot::String &p_string) {
		if (p_string.is_empty()) {
			return *this;
		}
		string_length += (uint32_t)p_string.length();
		strings.push_back(p_string);
		return *this;
	}

	StringBuilder &append(const char *p_cstring) {
		// Godot counts an empty C string as an append even though it adds no characters.
		const godot::String converted = godot::String(p_cstring);
		string_length += (uint32_t)converted.length();
		strings.push_back(converted);
		return *this;
	}

	StringBuilder &operator+(const godot::String &p_string) { return append(p_string); }
	StringBuilder &operator+(const char *p_cstring) { return append(p_cstring); }
	void operator+=(const godot::String &p_string) { append(p_string); }
	void operator+=(const char *p_cstring) { append(p_cstring); }
	int num_strings_appended() const { return (int)strings.size(); }
	uint32_t get_string_length() const { return string_length; }

	godot::String as_string() const {
		if (string_length == 0) {
			return godot::String();
		}
		godot::String result;
		for (uint32_t i = 0; i < strings.size(); i++) {
			result += strings[i];
		}
		return result;
	}

	operator godot::String() const { return as_string(); }
};
