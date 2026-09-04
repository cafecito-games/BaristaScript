/**************************************************************************/
/*  barista_script_language.h                                             */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_declaration_index.h"
#include "bs_platform.h"

#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>

namespace barista_script {

class BSParserHost;

/**
 * The interned names the front-end compares identifiers against.
 *
 * Foundry hangs this table on its language singleton and reaches it as
 * `FSLanguage::get_singleton()->strings._init` (foundry_script.h:1255-1267 @
 * c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6). Constructing a `StringName` crosses the GDExtension
 * interface, so the parser must not build one per comparison; and a bare `"_init"` literal at a
 * comparison site is how two spellings of the same special name drift apart. This is the only place
 * these names are written.
 *
 * It is deliberately not a member of the language object. The parser runs before the language is
 * registered (a resource loader may parse during extension start-up), and a table reached through a
 * singleton that may still be null is a table with two behaviours. These are constants; they are
 * built once, on first use, and are the same in every process that loads the extension.
 */
struct BaristaScriptInternedStrings {
	godot::StringName _init;
	godot::StringName _static_init;
	godot::StringName _notification;
	godot::StringName _set;
	godot::StringName _get;
	godot::StringName _get_property_list;
	godot::StringName _validate_property;
	godot::StringName _property_can_revert;
	godot::StringName _property_get_revert;
	godot::StringName _script_source;

	BaristaScriptInternedStrings();
};

class BaristaScriptLanguage final : public godot::ScriptLanguageExtension {
	GDCLASS(BaristaScriptLanguage, godot::ScriptLanguageExtension)

	static BaristaScriptLanguage *singleton;

protected:
	static void _bind_methods();

public:
	BaristaScriptLanguage();
	~BaristaScriptLanguage() override;

	static BaristaScriptLanguage *get_singleton();

	godot::String _get_name() const override;
	void _init() override;
	godot::String _get_type() const override;
	godot::String _get_extension() const override;
	void _finish() override;
	godot::PackedStringArray _get_reserved_words() const override;
	bool _is_control_flow_keyword(const godot::String &p_keyword) const override;
	godot::PackedStringArray _get_comment_delimiters() const override;
	godot::PackedStringArray _get_doc_comment_delimiters() const override;
	godot::PackedStringArray _get_string_delimiters() const override;
	godot::Ref<godot::Script> _make_template(const godot::String &p_template, const godot::String &p_class_name, const godot::String &p_base_class_name) const override;
	godot::TypedArray<godot::Dictionary> _get_built_in_templates(const godot::StringName &p_object) const override;
	bool _is_using_templates() override;
	godot::Dictionary _validate(const godot::String &p_script, const godot::String &p_path, bool p_validate_functions, bool p_validate_errors, bool p_validate_warnings, bool p_validate_safe_lines) const override;
	godot::String _validate_path(const godot::String &p_path) const override;
	bool _has_named_classes() const override;
	bool _supports_builtin_mode() const override;
	bool _supports_documentation() const override;
	bool _can_inherit_from_file() const override;
	int32_t _find_function(const godot::String &p_function, const godot::String &p_code) const override;
	godot::String _make_function(const godot::String &p_class_name, const godot::String &p_function_name, const godot::PackedStringArray &p_function_args) const override;
	bool _can_make_function() const override;
	godot::Error _open_in_external_editor(const godot::Ref<godot::Script> &p_script, int32_t p_line, int32_t p_column) override;
	bool _overrides_external_editor() override;
	godot::ScriptLanguage::ScriptNameCasing _preferred_file_name_casing() const override;
	godot::Dictionary _complete_code(const godot::String &p_code, const godot::String &p_path, godot::Object *p_owner) const override;
	godot::Dictionary _lookup_code(const godot::String &p_code, const godot::String &p_symbol, const godot::String &p_path, godot::Object *p_owner) const override;
	godot::String _auto_indent_code(const godot::String &p_code, int32_t p_from_line, int32_t p_to_line) const override;
	void _add_global_constant(const godot::StringName &p_name, const godot::Variant &p_value) override;
	void _add_named_global_constant(const godot::StringName &p_name, const godot::Variant &p_value) override;
	void _remove_named_global_constant(const godot::StringName &p_name) override;
	void _thread_enter() override;
	void _thread_exit() override;
	godot::String _debug_get_error() const override;
	int32_t _debug_get_stack_level_count() const override;
	int32_t _debug_get_stack_level_line(int32_t p_level) const override;
	godot::String _debug_get_stack_level_function(int32_t p_level) const override;
	godot::String _debug_get_stack_level_source(int32_t p_level) const override;
	godot::Dictionary _debug_get_stack_level_locals(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) override;
	godot::Dictionary _debug_get_stack_level_members(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) override;
	void *_debug_get_stack_level_instance(int32_t p_level) override;
	godot::Dictionary _debug_get_globals(int32_t p_max_subitems, int32_t p_max_depth) override;
	godot::String _debug_parse_stack_level_expression(int32_t p_level, const godot::String &p_expression, int32_t p_max_subitems, int32_t p_max_depth) override;
	godot::TypedArray<godot::Dictionary> _debug_get_current_stack_info() override;
	void _reload_all_scripts() override;
	void _reload_scripts(const godot::Array &p_scripts, bool p_soft_reload) override;
	void _reload_tool_script(const godot::Ref<godot::Script> &p_script, bool p_soft_reload) override;
	godot::PackedStringArray _get_recognized_extensions() const override;
	godot::TypedArray<godot::Dictionary> _get_public_functions() const override;
	godot::Dictionary _get_public_constants() const override;
	godot::TypedArray<godot::Dictionary> _get_public_annotations() const override;
	void _profiling_start() override;
	void _profiling_stop() override;
	void _profiling_set_save_native_calls(bool p_enable) override;
	int32_t _profiling_get_accumulated_data(godot::ScriptLanguageExtensionProfilingInfo *p_info_array, int32_t p_info_max) override;
	int32_t _profiling_get_frame_data(godot::ScriptLanguageExtensionProfilingInfo *p_info_array, int32_t p_info_max) override;
	void _frame() override;
	bool _handles_global_class_type(const godot::String &p_type) const override;
	godot::Dictionary _get_global_class_name(const godot::String &p_path) const override;

	/** The interned special-method names. See `BaristaScriptInternedStrings`. */
	static const BaristaScriptInternedStrings &get_interned_strings();

	/** The private declaration index owned by this language (#44). */
	BSDeclarationIndex &get_declaration_index() { return declaration_index; }
	const BSDeclarationIndex &get_declaration_index() const { return declaration_index; }

	Vector<String> get_conformance_files_in_namespace(const String &p_namespace) const;
	/** Commit/remove helpers used by probes and the #43 analyzer seam. */
	uint64_t claim_declaration_refresh(const String &p_path);
	bool commit_declaration_record(uint64_t p_token, const BSDeclarationRecord &p_record);
	bool remove_declaration_path(const String &p_path, uint64_t p_token);
	void synchronize_declaration_path_from_source(const String &p_path, const String &p_source);
	/**
	 * Name lookup with digest validation (#44 leftover / #58). When source is available and the
	 * stored digest mismatches, discards the stale entry, schedules reanalysis, and retries.
	 */
	bool try_resolve_declaration(const String &p_qualified_name, BSDeclarationRecord &r_record);
	Error flush_declaration_index(const String &p_store_path = String());
	BSDeclarationIndexLoadStatus load_declaration_index(const String &p_store_path = String());
	void notify_conformance_namespaces_changed(const Vector<String> &p_namespaces);

private:
	BSDeclarationIndex declaration_index;
	BSParserHost *parser_host = nullptr;

public:
	/**
	 * The public functions of the language, in the shape the front-end wants them.
	 *
	 * Upstream asks the language singleton directly (`FSLanguage::get_public_functions()`,
	 * fs_parser.cpp:3000 @ c9d5e35). A `ScriptLanguageExtension` answers the same question as an
	 * array of dictionaries, and answers nothing at all before it is registered, so this converts
	 * and tolerates both.
	 */
	static godot::List<godot::MethodInfo> get_public_function_list();
};

} // namespace barista_script
