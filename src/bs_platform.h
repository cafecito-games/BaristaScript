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

// core/string/ustring.h -- opaque and engine-backed; see the manifest's non-mapping note. core
// declares vformat here too; godot-cpp keeps it in variant/utility_functions.hpp.
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
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

// core/object/class_db.h -- godot-cpp's `ClassDB` forwards the introspection methods onto the
// `ClassDBSingleton` binding (godot-cpp/include/godot_cpp/core/class_db.hpp:214), so the core
// spelling works unchanged. The one method it does not forward is `is_class_exposed()`, which it
// does not need to: the ClassDB an extension talks to is built from the exposed API.
#include <godot_cpp/core/class_db.hpp>
// core/object/object.h -- only PropertyInfo is used; godot-cpp keeps it under core/, not classes/.
#include <godot_cpp/core/property_info.hpp>
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
// core/io/compression.h -- only the ZSTD mode is used; godot-cpp spells it on FileAccess.
#include <godot_cpp/classes/file_access.hpp>
// core/io/marshalls.h -- godot-cpp has no marshalls header; see the BSMarshalls shim below. The
// shim reaches the variant serializer through UtilityFunctions, already included above.
#include <godot_cpp/variant/packed_byte_array.hpp>
// scene/main/multiplayer_api.h -- the `@rpc` annotation reads the RPCMode enumerators from
// MultiplayerAPI and the TransferMode ones from MultiplayerPeer, which core declares in a header
// this one includes and godot-cpp splits into its own.
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
// servers/text/text_server.h -- the confusable-identifier check M1 guarded out, reinstated by the
// warning registry through the public interface rather than the engine-internal TS macro. core
// reaches the primary interface through TS; godot-cpp goes through TextServerManager, so the seam
// includes both headers. See the decision recorded in src/bs_warning.h.
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/text_server_manager.hpp>

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
 * `StringName` compared against a C string literal. Core declares four such operators on
 * `StringName` and four free ones for the reversed operand order
 * (Foundry `core/string/string_name.h:82,84,197,198` @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6,
 * unchanged from stock Godot); godot-cpp declares none of them, so `name == "export"` is not a
 * missing operator but an *ambiguous* one -- the compiler can convert either side -- and every such
 * comparison in the ported front-end fails to build.
 *
 * The four operators below are exact matches, so they resolve the ambiguity rather than adding a conversion, and
 * they answer exactly what core's answer: the comparison a `StringName` makes against the interned
 * form of that literal. They are declared here rather than spelled out at ~30 call sites so that the
 * diff against Foundry stays readable, which is the seam's whole purpose.
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
 * `core/io/marshalls.h` is absent from godot-cpp, and the four functions the tokenizer buffer uses
 * split into two very different cases.
 *
 * The fixed-width integer codecs are byte-order definitions, not engine behaviour: core writes a
 * `uint32_t` little-endian, byte by byte, and reads it back the same way. Reimplementing that is
 * exact, so `encode_uint32`/`decode_uint32` below are the same function core has.
 *
 * `encode_variant`/`decode_variant` are not. godot-cpp reaches the same serializer through
 * `UtilityFunctions::var_to_bytes` / `bytes_to_var`, which is core's `encode_variant` with
 * `p_full_objects = false` -- the mode the buffer already asked for, because a constant is never an
 * object. What godot-cpp does not expose is core's `r_len` out-parameter, so a reader cannot learn
 * how many bytes one value consumed. The seam does not invent one: it hands back the encoded block
 * and leaves framing to the caller, and `BSTokenizerBuffer` length-prefixes each constant for
 * exactly that reason. A shim that guessed the length would be the near-miss the seam forbids.
 */
struct BSMarshalls {
	static void encode_uint32(uint32_t p_value, uint8_t *p_bytes) {
		for (int i = 0; i < 4; i++) {
			p_bytes[i] = uint8_t(p_value & 0xFF);
			p_value >>= 8;
		}
	}

	static uint32_t decode_uint32(const uint8_t *p_bytes) {
		uint32_t value = 0;
		for (int i = 3; i >= 0; i--) {
			value <<= 8;
			value |= uint32_t(p_bytes[i]);
		}
		return value;
	}

	static PackedByteArray encode_variant(const Variant &p_variant) {
		// `false` is core's `p_full_objects = false`: object references are never encoded.
		return UtilityFunctions::var_to_bytes(p_variant);
	}

	static Variant decode_variant(const PackedByteArray &p_bytes) {
		// Mirrors core's `decode_variant(..., p_allow_objects = false)`; a malformed block decodes
		// to `nil` rather than to an object the buffer never wrote.
		return UtilityFunctions::bytes_to_var(p_bytes);
	}
};

/**
 * `core/io/compression.h` is absent from godot-cpp as a class, but the operation is not: the same
 * ZSTD codec is reachable as `PackedByteArray::compress`/`decompress`, taking the mode enumerator
 * from `FileAccess::COMPRESSION_ZSTD`. The shim is a rename over exactly that, and it keeps core's
 * contract that decompression is told the expected size up front rather than growing a buffer.
 *
 * `decompress` returns an empty array on failure, which is indistinguishable from decompressing to
 * nothing; the caller checks the size against the header value it already has, so a truncated or
 * corrupt block is a data error rather than a short read.
 */
struct BSCompression {
	static PackedByteArray compress_zstd(const PackedByteArray &p_bytes) {
		return p_bytes.compress(FileAccess::COMPRESSION_ZSTD);
	}

	static PackedByteArray decompress_zstd(const PackedByteArray &p_bytes, int64_t p_decompressed_size) {
		return p_bytes.decompress(p_decompressed_size, FileAccess::COMPRESSION_ZSTD);
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
