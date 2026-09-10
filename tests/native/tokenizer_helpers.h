/**************************************************************************/
/*  tokenizer_helpers.h                                                   */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
namespace barista_script::test {
godot::String first_diagnostic(const godot::PackedByteArray &source);
godot::PackedStringArray dump_tokens(const godot::PackedByteArray &source);
godot::PackedStringArray dump_significant_tokens(const godot::PackedByteArray &source);
godot::PackedStringArray dump_buffer_significant_tokens(const godot::PackedByteArray &source, bool compress);
godot::PackedStringArray token_type_names();
godot::PackedStringArray keyword_spellings();
godot::PackedStringArray reserved_spellings();
} //namespace barista_script::test
