/**************************************************************************/
/*  bs_runtime_type.h                                                     */
/*                                                                        */
/*  The runtime descriptor a compiled slot carries for a type Godot's     */
/*  own container typing cannot express. Typed containers and casts       */
/*  fill it in; the slot type frozen in bs_function.h is what it refines. */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_parser.h"
#include "bs_platform.h"

#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/core/object_id.hpp>

namespace barista_script {

class BaristaScript;
/**
 * A value-owned type descriptor used after the parser generation is gone.
 *
 * The compile-time model remains BSParser::DataType. A compiled function interns this smaller
 * descriptor and bytecode names it by index, so no opcode retains a ClassNode or another pointer
 * into an analyzer generation. TUPLE and UNION are part of the common shape agreed with #247; that
 * issue supplies their construction and diagnostics rather than defining a parallel descriptor.
 */
class BSRuntimeType {
public:
	enum Kind {
		VARIANT,
		BUILTIN,
		NATIVE,
		SCRIPT,
		TUPLE,
		UNION,
	};

	Kind kind = VARIANT;
	Variant::Type builtin_type = Variant::NIL;
	StringName native_type;
	// Weak identity avoids script -> function -> descriptor -> script reference cycles. The owning
	// function keeps its own script alive; external script identities fail closed if they disappear.
	ObjectID script_id;
	String script_path;
	bool is_nullable = false;
	bool is_type_handle = false;
	Vector<BSRuntimeType> container_element_types;
	Vector<BSRuntimeType> union_alternatives;

	static BSRuntimeType builtin(Variant::Type p_type, bool p_nullable = false);
	static BSRuntimeType native(const StringName &p_class, bool p_nullable = false);
	static BSRuntimeType script(const Ref<Script> &p_script, bool p_nullable = false);

	/** Lowers a resolved parser type while its owning parser is still alive. */
	static bool from_data_type(const BSParser::DataType &p_type, BaristaScript *p_owner,
			BSRuntimeType &r_type, String &r_error);

	bool has_type() const { return kind != VARIANT; }
	bool is_typed_array() const;
	bool is_typed_dictionary() const;
	String get_name() const;

	/** Exact run-time membership; p_narrowing makes null fail an object `is` test. */
	bool accepts(const Variant &p_value, bool p_allow_implicit_conversion = false,
			bool p_narrowing = false) const;
	/** Converts for a typed store without changing r_value on failure. */
	bool convert(const Variant &p_source, Variant &r_value, String &r_error,
			bool p_allow_erased_container = false) const;
	/** Implements `as`: failed nullable casts yield null; required casts report. */
	bool cast(const Variant &p_source, Variant &r_value, String &r_error) const;

	uint32_t hash() const;
	bool operator==(const BSRuntimeType &p_other) const;
	bool operator!=(const BSRuntimeType &p_other) const { return !(*this == p_other); }

private:
	Ref<Script> get_script() const;
	bool container_metadata(Variant::Type &r_builtin, StringName &r_class, Variant &r_script) const;
	bool matches_container_metadata(Variant::Type p_builtin, const StringName &p_class,
			const Variant &p_script) const;
};

} // namespace barista_script
