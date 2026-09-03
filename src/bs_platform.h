/**************************************************************************/
/*  bs_platform.h                                                         */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

/**
 * The single compatibility seam between BaristaScript's ported Foundry frontend and godot-cpp.
 *
 * Foundry's frontend is engine-module code: it spells its dependencies as `core/` includes and
 * relies on Godot's core headers being on the include path. A GDExtension has godot-cpp instead,
 * which mirrors most of those types under different paths and, in a few places, does not mirror
 * them at all. This header is the only place that difference is written down. A ported file
 * replaces its whole `core/` include block with `#include "bs_platform.h"` and changes nothing
 * else; no ported file may include a godot-cpp header directly for a type mapped here.
 *
 * The mapping is audited, not asserted. `src/bs_platform_manifest.json` records every upstream
 * upstream dependency of the port set with its resolution, and `tests/audit_platform_seam.py`
 * checks that record against the godot-cpp header set the build will actually generate. What the
 * seam refuses to do matters more than what it does: it never aliases an upstream type to a
 * near-miss godot-cpp type, and it never expands a missing macro to nothing. A gap is a compile
 * error here, not a behaviour change three milestones later.
 */

// core/templates/hash_map.h
#include <godot_cpp/templates/hash_map.hpp>
// core/templates/hash_set.h
#include <godot_cpp/templates/hash_set.hpp>
// core/templates/list.h
#include <godot_cpp/templates/list.hpp>
// core/templates/vector.h
#include <godot_cpp/templates/vector.hpp>

// core/string/ustring.h -- opaque and engine-backed; see the manifest's non-mapping note.
#include <godot_cpp/variant/string.hpp>
// core/string/string_name.h -- the type maps; the SNAME macro is shimmed below.
#include <godot_cpp/variant/string_name.hpp>
// core/string/char_utils.h
#include <godot_cpp/variant/char_utils.hpp>
// core/variant/variant.h
#include <godot_cpp/variant/variant.hpp>

// core/error/error_macros.h
#include <godot_cpp/core/error_macros.hpp>
// core/math/math_defs.h -- core reaches Math:: transitively through core/math/math_funcs.h.
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/math_defs.hpp>

// core/object/ref_counted.h -- core declares RefCounted and Ref<T> together.
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
// core/object/script_language.h -- core declares Script and ScriptLanguage together.
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/script_language.hpp>
// core/io/file_access.h
#include <godot_cpp/classes/file_access.hpp>
// core/io/resource.h
#include <godot_cpp/classes/resource.hpp>
// core/io/resource_loader.h -- a singleton in godot-cpp, static members in core.
#include <godot_cpp/classes/resource_loader.hpp>
// core/io/resource_uid.h -- a singleton in godot-cpp, static members in core.
#include <godot_cpp/classes/resource_uid.hpp>
// core/config/project_settings.h
#include <godot_cpp/classes/project_settings.hpp>
// scene/main/multiplayer_api.h -- only the RPCMode enumerators are used.
#include <godot_cpp/classes/multiplayer_api.hpp>

// Backing for the shims below; not a mapping of any upstream dependency.
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/templates/local_vector.hpp>

// Backing for the parse cache's on-disk store, also not a mapping of any upstream dependency:
// upstream fs_cache is in-memory only -- its only file access is reading script sources
// (fs_cache.cpp:407 at the pinned revision) -- so the store's atomic rename
// (DirAccess::rename_absolute) and FileAccess's byte-array return type (PackedByteArray) are
// BaristaScript additions. Recorded in the manifest's seam_support_headers, not as entries,
// because there is no upstream include site to map them to.
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

/**
 * Ported files are written against Godot's global names. godot-cpp puts everything in `godot`, so
 * the seam opens it once here rather than making every ported file carry a `using` line the
 * upstream file does not have. This is deliberate: the seam exists so that the diff against
 * Foundry stays readable.
 */
using namespace godot;

/**
 * `docs/foundry-reuse-plan.md` section 3 predicted that godot-cpp spells the error macros
 * differently and that the seam would need a shim. It does not: every macro the port set uses is
 * spelled identically. A wrapper would therefore be a rename-free no-op, which the seam's contract
 * forbids. What the seam does instead is refuse to compile when one of them is missing, so a
 * godot-cpp bump that drops or renames a macro fails here rather than silently expanding to
 * nothing at a call site. The list is kept in step with `required_macros` in the manifest by
 * `tests/audit_platform_seam.py`.
 */
#if !defined(ERR_CONTINUE) || !defined(ERR_CONTINUE_MSG)
#error "bs_platform.h: godot-cpp no longer defines the ERR_CONTINUE macros the ported frontend uses."
#endif
#if !defined(ERR_FAIL_COND) || !defined(ERR_FAIL_COND_MSG) || !defined(ERR_FAIL_COND_V) || !defined(ERR_FAIL_COND_V_MSG)
#error "bs_platform.h: godot-cpp no longer defines the ERR_FAIL_COND macros the ported frontend uses."
#endif
#if !defined(ERR_FAIL_INDEX) || !defined(ERR_FAIL_INDEX_V) || !defined(ERR_FAIL_INDEX_V_MSG)
#error "bs_platform.h: godot-cpp no longer defines the ERR_FAIL_INDEX macros the ported frontend uses."
#endif
#if !defined(ERR_FAIL_NULL) || !defined(ERR_FAIL_NULL_MSG) || !defined(ERR_FAIL_NULL_V)
#error "bs_platform.h: godot-cpp no longer defines the ERR_FAIL_NULL macros the ported frontend uses."
#endif
#if !defined(ERR_FAIL_V) || !defined(ERR_FAIL_V_MSG) || !defined(ERR_PRINT) || !defined(DEV_ASSERT)
#error "bs_platform.h: godot-cpp no longer defines the error macros the ported frontend uses."
#endif

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
#error "bs_platform.h: SNAME is already defined; the seam must own the only definition."
#endif
#define SNAME(m_arg)                                                 \
	([]() -> const godot::StringName & {                             \
		static godot::StringName *sname = memnew(StringName(m_arg)); \
		return *sname;                                               \
	}())

/**
 * `core/string/string_builder.h` is absent from godot-cpp. Godot's own implementation was read
 * before this was written: its `as_string()` builds the result with `String::resize_uninitialized`,
 * which godot-cpp's `String` does not have -- it offers `resize`, `ptr` and `ptrw`, but no
 * uninitialized resize -- so a vendored copy would have to be edited, and an edited vendor loses
 * the upstream diffability that was the reason to vendor.
 *
 * What follows reimplements the public API over ordinary concatenation. The observable behaviour is
 * Godot's, deliberately: appending an empty `String` is a no-op that does not count towards
 * `num_strings_appended()`, appending an empty C string does count, and an empty builder stringifies
 * to `""`. The cost profile is not Godot's -- this concatenates instead of writing once into a
 * presized buffer. `FSParser::TreePrinter` is the only consumer and runs under `DEBUG_ENABLED`.
 */
class StringBuilder {
	uint32_t string_length = 0;
	LocalVector<String> strings;

public:
	StringBuilder &append(const String &p_string) {
		if (p_string.is_empty()) {
			return *this;
		}
		string_length += (uint32_t)p_string.length();
		strings.push_back(p_string);
		return *this;
	}

	StringBuilder &append(const char *p_cstring) {
		// Godot counts an empty C string as an append even though it adds no characters, so this
		// does not delegate to the String overload, which skips empties.
		const String converted = String(p_cstring);
		string_length += (uint32_t)converted.length();
		strings.push_back(converted);
		return *this;
	}

	StringBuilder &operator+(const String &p_string) {
		return append(p_string);
	}

	StringBuilder &operator+(const char *p_cstring) {
		return append(p_cstring);
	}

	void operator+=(const String &p_string) {
		append(p_string);
	}

	void operator+=(const char *p_cstring) {
		append(p_cstring);
	}

	int num_strings_appended() const {
		return (int)strings.size();
	}

	uint32_t get_string_length() const {
		return string_length;
	}

	String as_string() const {
		if (string_length == 0) {
			return String();
		}
		String result;
		for (uint32_t i = 0; i < strings.size(); i++) {
			result += strings[i];
		}
		return result;
	}

	operator String() const {
		return as_string();
	}
};

/**
 * D1 gives BaristaScript one integer type, so Foundry's `NumericType` and the numeric tower built
 * on it are deleted rather than ported. Nothing here defines the type; the identifier is redirected
 * to one that does not exist, so any surviving reference -- including a pointer or a reference,
 * which a forward declaration would have allowed -- is a compile error that names the decision.
 *
 * A stub would compile. That is exactly the failure this is here to prevent: it would let the tower
 * grow back one call site at a time, and the corpus would not notice.
 *
 * Verified by hand, not by a test, because the proof is the absence of a translation unit:
 *
 *     #include "bs_platform.h"
 *     NumericType example;   // error: unknown type name
 *                            // 'BS_NumericType_was_deleted_by_D1_see_docs_GRAMMAR_md'
 *     NumericType *pointer;  // the same error; the redirect leaves nothing to point at
 */
#define NumericType BS_NumericType_was_deleted_by_D1_see_docs_GRAMMAR_md
