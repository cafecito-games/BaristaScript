/**************************************************************************/
/*  bs_platform_shims.cpp                                                 */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_platform.h"

/**
 * Including `bs_platform.h` proves its mappings resolve, but it does not compile the shims: a macro
 * that is never expanded and a class whose members are never called are both invisible to the
 * compiler. This translation unit uses each of them once, so the build keeps proving the shims work
 * against the current godot-cpp rather than only against the one they were written for.
 *
 * These functions are never called. They exist to be compiled.
 */
namespace bs_platform_seam {

String prove_string_builder() {
	StringBuilder builder;
	builder.append("appended as a C string");
	builder.append(String("appended as a String"));
	builder += "concatenated as a C string";
	builder += String("concatenated as a String");
	if (builder.num_strings_appended() == 0 || builder.get_string_length() == 0) {
		return String();
	}
	return builder.as_string();
}

// The comparison operators godot-cpp omits. Both orders and both senses, so a godot-cpp bump that
// adds its own overloads is caught here as an ambiguity rather than at 30 ported call sites.
bool prove_string_name_literal_comparison(const StringName &p_name) {
	const bool equal = bs_string_name_equals_literal(p_name, "BaristaScript") &&
			(p_name == "BaristaScript") && ("BaristaScript" == p_name);
	const bool different = (p_name != "BaristaScript") || ("BaristaScript" != p_name);
	return equal && !different;
}

const StringName &prove_sname() {
	return SNAME("BaristaScript");
}

Variant prove_marshalls() {
	uint8_t bytes[4] = {};
	BSMarshalls::encode_uint32(0x01020304u, bytes);
	if (BSMarshalls::decode_uint32(bytes) != 0x01020304u) {
		return Variant();
	}
	return BSMarshalls::decode_variant(BSMarshalls::encode_variant(Variant(1)));
}

PackedByteArray prove_compression() {
	const PackedByteArray compressed = BSCompression::compress_zstd(PackedByteArray());
	return BSCompression::decompress_zstd(compressed, 0);
}

Variant::Type prove_variant_operators() {
	if (!BSVariantOperators::has_validated_evaluator(Variant::OP_ADD, Variant::INT, Variant::INT)) {
		return Variant::NIL;
	}
	return BSVariantOperators::get_return_type(Variant::OP_ADD, Variant::INT, Variant::INT);
}

} // namespace bs_platform_seam

#include <cstdint>
#include <cstring>
#include <vector>

#if defined(WINDOWS_ENABLED)
#include <windows.h>
#elif defined(UNIX_ENABLED)
#include <cerrno>
#include <cstdio>
#endif

namespace {

bool bs_path_contains_nul(const String &p_path) {
	for (int i = 0; i < p_path.length(); i++) {
		if (p_path[i] == 0) {
			return true;
		}
	}
	return false;
}

Error bs_resolve_store_filesystem_path(const String &p_path, String &r_resolved) {
	if (p_path.is_empty() || bs_path_contains_nul(p_path)) {
		return Error::ERR_INVALID_PARAMETER;
	}

	const int scheme_sep = p_path.find("://");
	if (scheme_sep >= 0) {
		const String scheme = p_path.substr(0, scheme_sep);
		if (scheme != "user" && scheme != "res") {
			return Error::ERR_INVALID_PARAMETER;
		}
		ProjectSettings *settings = ProjectSettings::get_singleton();
		if (settings == nullptr) {
			return Error::ERR_INVALID_PARAMETER;
		}
		r_resolved = settings->globalize_path(p_path);
		if (r_resolved.is_empty() || bs_path_contains_nul(r_resolved)) {
			return Error::ERR_INVALID_PARAMETER;
		}
	} else {
		r_resolved = p_path;
	}

#if defined(UNIX_ENABLED)
	// Match FileAccess::fix_path: on POSIX, '\\' is treated as a separator alias for '/', so
	// FileAccess write/readback and libc ::rename must see the same normalized bytes.
	r_resolved = r_resolved.replace("\\", "/");
#endif
	return Error::OK;
}

#if defined(WINDOWS_ENABLED)

static String bs_windows_ensure_extended_prefix(const String &p_path) {
	String path = p_path.replace("/", "\\");
	if (path.begins_with("\\\\?\\")) {
		return path;
	}
	if (path.begins_with("\\\\")) {
		// UNC \\server\share\... -> \\?\UNC\server\share\...
		return String("\\\\?\\UNC\\") + path.substr(2);
	}
	return String("\\\\?\\") + path;
}

static bool bs_windows_is_drive_absolute(const String &p_path) {
	return p_path.length() >= 2 && p_path[1] == ':' &&
			((p_path[0] >= 'A' && p_path[0] <= 'Z') || (p_path[0] >= 'a' && p_path[0] <= 'z'));
}

Error bs_windows_absolute_utf16(const String &p_path, std::vector<wchar_t> &r_wide, int64_t *r_native_error) {
	// Always prepare absolute UTF-16 with the extended local/UNC prefix for MoveFileExW, for both
	// short and long destinations. Do this without first requiring LongPathsEnabled: unprefixed
	// GetFullPathNameW / MoveFileExW stay subject to the legacy WCHAR MAX_PATH ceiling unless the
	// path is already extended. Length policy uses UTF-16 code units (Char16String::length /
	// wcslen), never String::length() (UTF-32 codepoints) compared against MAX_PATH.

	String path = p_path.replace("/", "\\");
	String extended_input;
	if (path.begins_with("\\\\?\\")) {
		extended_input = path;
	} else if (path.begins_with("\\\\")) {
		extended_input = String("\\\\?\\UNC\\") + path.substr(2);
	} else if (bs_windows_is_drive_absolute(path)) {
		extended_input = String("\\\\?\\") + path;
	} else {
		DWORD dir_needed = GetCurrentDirectoryW(0, nullptr);
		if (dir_needed == 0) {
			if (r_native_error != nullptr) {
				*r_native_error = (int64_t)GetLastError();
			}
			return Error::ERR_FILE_CANT_WRITE;
		}
		std::vector<wchar_t> dir_buf(dir_needed);
		DWORD dir_written = GetCurrentDirectoryW(dir_needed, dir_buf.data());
		if (dir_written == 0 || dir_written >= dir_needed) {
			if (r_native_error != nullptr) {
				*r_native_error = (int64_t)GetLastError();
			}
			return Error::ERR_FILE_CANT_WRITE;
		}
		String cwd = String::utf16(reinterpret_cast<const char16_t *>(dir_buf.data()), (int64_t)dir_written);
		String extended_cwd = bs_windows_ensure_extended_prefix(cwd);
		if (extended_cwd.ends_with("\\")) {
			extended_input = extended_cwd + path;
		} else {
			extended_input = extended_cwd + String("\\") + path;
		}
	}

	const Char16String utf16 = extended_input.utf16();
	DWORD needed = GetFullPathNameW(reinterpret_cast<LPCWSTR>(utf16.get_data()), 0, nullptr, nullptr);
	if (needed == 0) {
		if (r_native_error != nullptr) {
			*r_native_error = (int64_t)GetLastError();
		}
		return Error::ERR_FILE_CANT_WRITE;
	}

	std::vector<wchar_t> absolute(needed);
	DWORD written = GetFullPathNameW(reinterpret_cast<LPCWSTR>(utf16.get_data()), needed, absolute.data(), nullptr);
	if (written == 0 || written >= needed) {
		if (r_native_error != nullptr) {
			*r_native_error = (int64_t)GetLastError();
		}
		return Error::ERR_FILE_CANT_WRITE;
	}

	// GetFullPathNameW may drop the extended prefix; restore it so MoveFileExW always receives one.
	String full = String::utf16(reinterpret_cast<const char16_t *>(absolute.data()), (int64_t)written);
	String prefixed = bs_windows_ensure_extended_prefix(full);

	const Char16String out = prefixed.utf16();
	r_wide.assign(reinterpret_cast<const wchar_t *>(out.get_data()),
			reinterpret_cast<const wchar_t *>(out.get_data()) + out.length() + 1);
	if (r_native_error != nullptr) {
		*r_native_error = 0;
	}
	return Error::OK;
}

#endif // WINDOWS_ENABLED

} // namespace

Error bs_validate_parse_cache_store_path(const String &p_store_path) {
	String resolved;
	return bs_resolve_store_filesystem_path(p_store_path, resolved);
}

Error bs_replace_file(const String &p_temp_path, const String &p_destination_path,
		const char **r_backend, int64_t *r_native_error) {
	if (r_backend != nullptr) {
		*r_backend = "unavailable";
	}
	if (r_native_error != nullptr) {
		*r_native_error = 0;
	}

	String temp_resolved;
	String destination_resolved;
	const Error temp_path_error = bs_resolve_store_filesystem_path(p_temp_path, temp_resolved);
	if (temp_path_error != Error::OK) {
		return temp_path_error;
	}
	const Error destination_path_error = bs_resolve_store_filesystem_path(p_destination_path, destination_resolved);
	if (destination_path_error != Error::OK) {
		return destination_path_error;
	}

#if defined(WINDOWS_ENABLED)
	if (r_backend != nullptr) {
		*r_backend = "MoveFileExW";
	}
	std::vector<wchar_t> temp_wide;
	std::vector<wchar_t> destination_wide;
	Error convert_error = bs_windows_absolute_utf16(temp_resolved, temp_wide, r_native_error);
	if (convert_error != Error::OK) {
		return convert_error;
	}
	convert_error = bs_windows_absolute_utf16(destination_resolved, destination_wide, r_native_error);
	if (convert_error != Error::OK) {
		return convert_error;
	}
	if (!MoveFileExW(temp_wide.data(), destination_wide.data(), MOVEFILE_REPLACE_EXISTING)) {
		const DWORD last_error = GetLastError();
		if (r_native_error != nullptr) {
			*r_native_error = (int64_t)last_error;
		}
		return Error::ERR_FILE_CANT_WRITE;
	}
	if (r_native_error != nullptr) {
		*r_native_error = 0;
	}
	return Error::OK;
#elif defined(UNIX_ENABLED)
	if (r_backend != nullptr) {
		*r_backend = "rename";
	}
	const CharString temp_utf8 = temp_resolved.utf8();
	const CharString destination_utf8 = destination_resolved.utf8();
	if (::rename(temp_utf8.get_data(), destination_utf8.get_data()) != 0) {
		const int saved_errno = errno;
		if (r_native_error != nullptr) {
			*r_native_error = (int64_t)saved_errno;
		}
		return Error::ERR_FILE_CANT_WRITE;
	}
	if (r_native_error != nullptr) {
		*r_native_error = 0;
	}
	return Error::OK;
#else
	return Error::ERR_UNAVAILABLE;
#endif
}
