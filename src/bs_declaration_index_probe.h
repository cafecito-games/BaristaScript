/**************************************************************************/
/*  bs_declaration_index_probe.h                                          */
/*                                                                        */
/*  Debug-only test surface for the private declaration index (#44).      */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace barista_script {

class BaristaScriptDeclarationIndexProbe final : public godot::RefCounted {
	GDCLASS(BaristaScriptDeclarationIndexProbe, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	static int get_format_version();
	static godot::String get_default_store_path();
	static int64_t compute_source_digest(const godot::String &p_source);
	static godot::PackedStringArray load_status_names();

	int load(const godot::String &p_store_path);
	int flush(const godot::String &p_store_path, int p_fault);
	void clear();
	int get_record_count() const;
	godot::PackedStringArray get_load_report() const;
	godot::Array get_records() const;
	godot::PackedStringArray get_conformance_files_in_namespace(const godot::String &p_namespace) const;
	godot::PackedStringArray get_annotation_declaring_paths(const godot::String &p_annotation) const;

	int64_t claim_refresh(const godot::String &p_path);
	bool commit_record(int64_t p_token, const godot::Dictionary &p_record);
	bool remove_path(const godot::String &p_path, int64_t p_token);
	void remove_path_unconditional(const godot::String &p_path);
	void synchronize_path_from_source(const godot::String &p_path, const godot::String &p_source);

	/** Language-host conformance lookup (installed BSParserHost). */
	godot::PackedStringArray host_conformance_files_in_namespace(const godot::String &p_namespace) const;
	bool host_is_bootstrap_path_allowed(const godot::String &p_path) const;
	void set_bootstrap_root(const godot::String &p_root);

	/**
	 * Digest-validating name lookup (#58). Returns an empty Dictionary when absent or discarded.
	 * On digest mismatch against available source, discards + reanalyzes before returning.
	 */
	godot::Dictionary lookup_qualified_name(const godot::String &p_qualified_name);

	/**
	 * ScriptServer surfaces that must also digest-validate private names (#62).
	 * Non-const: resolve may discard + reanalyze (same as lookup_qualified_name).
	 */
	bool script_server_is_global_class_enum(const godot::String &p_name);
	godot::String script_server_get_global_class_path(const godot::String &p_name);
	godot::StringName script_server_get_global_class_native_base(const godot::String &p_name);
	godot::PackedStringArray script_server_get_global_class_list();
};

} // namespace barista_script

#endif // DEBUG_ENABLED
