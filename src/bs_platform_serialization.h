/**************************************************************************/
/*  bs_platform_serialization.h                                           */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

// Private implementation detail of bs_platform.h. Ported frontend files include the umbrella,
// never this header directly; tests/audit_platform_seam.py enforces that boundary.

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

/** The exact fixed-width codecs and object-free Variant serialization used by the token buffer. */
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

	static godot::PackedByteArray encode_variant(const godot::Variant &p_variant) {
		return godot::UtilityFunctions::var_to_bytes(p_variant);
	}

	static godot::Variant decode_variant(const godot::PackedByteArray &p_bytes) {
		return godot::UtilityFunctions::bytes_to_var(p_bytes);
	}
};

/** The ZSTD operations exposed by godot-cpp under PackedByteArray rather than Compression. */
struct BSCompression {
	static godot::PackedByteArray compress_zstd(const godot::PackedByteArray &p_bytes) {
		return p_bytes.compress(godot::FileAccess::COMPRESSION_ZSTD);
	}

	static godot::PackedByteArray decompress_zstd(const godot::PackedByteArray &p_bytes, int64_t p_decompressed_size) {
		return p_bytes.decompress(p_decompressed_size, godot::FileAccess::COMPRESSION_ZSTD);
	}
};
