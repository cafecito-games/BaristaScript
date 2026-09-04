/**************************************************************************/
/*  barista_script_language.cpp                                           */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script_language.h"

#include "barista_script.h"
#include "bs_analyzer.h"
#include "bs_cache.h"
#include "bs_global_class.h"
#include "bs_parser.h"
#include "bs_tokenizer.h"

#include <godot_cpp/variant/packed_int32_array.hpp>

namespace barista_script {

namespace {

class LanguageParserHost final : public BSParserHost {
public:
	Vector<String> get_conformance_files_in_namespace(const String &p_namespace) const override {
		const BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
		if (language == nullptr) {
			return Vector<String>();
		}
		return language->get_conformance_files_in_namespace(p_namespace);
	}

	bool is_bootstrap_path_allowed(const String &p_path) const override {
		return BSAnalyzer::is_bootstrap_path_allowed(p_path);
	}
};

} // namespace

BaristaScriptInternedStrings::BaristaScriptInternedStrings() :
		_init("_init"),
		_static_init("_static_init"),
		_notification("_notification"),
		_set("_set"),
		_get("_get"),
		_get_property_list("_get_property_list"),
		_validate_property("_validate_property"),
		_property_can_revert("_property_can_revert"),
		_property_get_revert("_property_get_revert"),
		_script_source("script/source") {
}

const BaristaScriptInternedStrings &BaristaScriptLanguage::get_interned_strings() {
	// Allocated once and never destroyed, for the reason `SNAME` in bs_platform.h is: a
	// `StringName` destructor that ran during static destruction would run after the extension was
	// unloaded and the interface function pointers were gone.
	static BaristaScriptInternedStrings *strings = memnew(BaristaScriptInternedStrings);
	return *strings;
}

godot::List<godot::MethodInfo> BaristaScriptLanguage::get_public_function_list() {
	godot::List<godot::MethodInfo> functions;
	const BaristaScriptLanguage *language = get_singleton();
	if (language == nullptr) {
		// The parser can run before the language is registered -- a resource loader may parse during
		// extension start-up -- and an unregistered language publishes no functions.
		return functions;
	}
	const godot::TypedArray<godot::Dictionary> published = language->_get_public_functions();
	for (int i = 0; i < published.size(); i++) {
		const godot::Dictionary entry = published[i];
		godot::MethodInfo info;
		info.name = entry.get("name", godot::String());
		functions.push_back(info);
	}
	return functions;
}

BaristaScriptLanguage *BaristaScriptLanguage::singleton = nullptr;

BaristaScriptLanguage::BaristaScriptLanguage() {
	if (singleton == nullptr) {
		singleton = this;
	}
	parser_host = memnew(LanguageParserHost);
}

BaristaScriptLanguage::~BaristaScriptLanguage() {
	if (parser_host != nullptr) {
		if (BSParserHost::get_singleton() == parser_host) {
			BSParserHost::set_singleton(nullptr);
		}
		memdelete(static_cast<LanguageParserHost *>(parser_host));
		parser_host = nullptr;
	}
	if (singleton == this) {
		singleton = nullptr;
	}
}

void BaristaScriptLanguage::_bind_methods() {}

BaristaScriptLanguage *BaristaScriptLanguage::get_singleton() {
	return singleton;
}

godot::String BaristaScriptLanguage::_get_name() const {
	return "BaristaScript";
}

void BaristaScriptLanguage::_init() {
	BSParserHost::set_singleton(parser_host);
	load_declaration_index();
#ifdef DEBUG_ENABLED
	BSParser::invalidate_analysis_on_strict_settings_change();
#endif // DEBUG_ENABLED
}

godot::String BaristaScriptLanguage::_get_type() const {
	return "BaristaScript";
}

godot::String BaristaScriptLanguage::_get_extension() const {
	return "barista";
}

void BaristaScriptLanguage::_finish() {
	if (BSParserHost::get_singleton() == parser_host) {
		BSParserHost::set_singleton(nullptr);
	}
	declaration_index.clear();
}

godot::PackedStringArray BaristaScriptLanguage::_get_reserved_words() const {
	// The tokenizer's keyword table is the only reserved-word list in the tree. `BSTokenizer`
	// exposes it as two views -- the keywords proper and the spellings D1 reserved as type names
	// (docs/GRAMMAR.md section 2.5) -- so that this surface is a projection of that table rather
	// than a second copy of it.
	godot::PackedStringArray words;
	for (const godot::String &keyword : BSTokenizer::get_keyword_spellings()) {
		words.push_back(keyword);
	}
	for (const godot::String &reserved : BSTokenizer::get_reserved_spellings()) {
		words.push_back(reserved);
	}
	return words;
}

bool BaristaScriptLanguage::_is_control_flow_keyword(const godot::String &) const {
	return false;
}

godot::PackedStringArray BaristaScriptLanguage::_get_comment_delimiters() const {
	return {};
}

godot::PackedStringArray BaristaScriptLanguage::_get_doc_comment_delimiters() const {
	return {};
}

godot::PackedStringArray BaristaScriptLanguage::_get_string_delimiters() const {
	return {};
}

godot::Ref<godot::Script> BaristaScriptLanguage::_make_template(const godot::String &p_template, const godot::String &, const godot::String &) const {
	godot::Ref<BaristaScript> script;
	script.instantiate();
	script->_set_source_code(p_template);
	return script;
}

godot::TypedArray<godot::Dictionary> BaristaScriptLanguage::_get_built_in_templates(const godot::StringName &) const {
	return {};
}

bool BaristaScriptLanguage::_is_using_templates() {
	return false;
}

godot::Dictionary BaristaScriptLanguage::_validate(const godot::String &, const godot::String &, bool, bool, bool, bool) const {
	godot::Dictionary result;
	result["valid"] = false;
	result["functions"] = godot::PackedStringArray();
	result["errors"] = godot::TypedArray<godot::Dictionary>();
	result["warnings"] = godot::TypedArray<godot::Dictionary>();
	result["safe_lines"] = godot::PackedInt32Array();
	return result;
}

godot::String BaristaScriptLanguage::_validate_path(const godot::String &) const {
	return {};
}

bool BaristaScriptLanguage::_has_named_classes() const {
	return false;
}

bool BaristaScriptLanguage::_supports_builtin_mode() const {
	return false;
}

bool BaristaScriptLanguage::_supports_documentation() const {
	return false;
}

bool BaristaScriptLanguage::_can_inherit_from_file() const {
	return false;
}

int32_t BaristaScriptLanguage::_find_function(const godot::String &, const godot::String &) const {
	return -1;
}

godot::String BaristaScriptLanguage::_make_function(const godot::String &, const godot::String &, const godot::PackedStringArray &) const {
	return {};
}

bool BaristaScriptLanguage::_can_make_function() const {
	return false;
}

godot::Error BaristaScriptLanguage::_open_in_external_editor(const godot::Ref<godot::Script> &, int32_t, int32_t) {
	return godot::ERR_UNAVAILABLE;
}

bool BaristaScriptLanguage::_overrides_external_editor() {
	return false;
}

godot::ScriptLanguage::ScriptNameCasing BaristaScriptLanguage::_preferred_file_name_casing() const {
	return godot::ScriptLanguage::SCRIPT_NAME_CASING_SNAKE_CASE;
}

godot::Dictionary BaristaScriptLanguage::_complete_code(const godot::String &, const godot::String &, godot::Object *) const {
	return {};
}

godot::Dictionary BaristaScriptLanguage::_lookup_code(const godot::String &, const godot::String &, const godot::String &, godot::Object *) const {
	return {};
}

godot::String BaristaScriptLanguage::_auto_indent_code(const godot::String &p_code, int32_t, int32_t) const {
	return p_code;
}

void BaristaScriptLanguage::_add_global_constant(const godot::StringName &, const godot::Variant &) {}

void BaristaScriptLanguage::_add_named_global_constant(const godot::StringName &, const godot::Variant &) {}

void BaristaScriptLanguage::_remove_named_global_constant(const godot::StringName &) {}

void BaristaScriptLanguage::_thread_enter() {}

void BaristaScriptLanguage::_thread_exit() {}

godot::String BaristaScriptLanguage::_debug_get_error() const {
	return {};
}

int32_t BaristaScriptLanguage::_debug_get_stack_level_count() const {
	return 0;
}

int32_t BaristaScriptLanguage::_debug_get_stack_level_line(int32_t) const {
	return -1;
}

godot::String BaristaScriptLanguage::_debug_get_stack_level_function(int32_t) const {
	return {};
}

godot::String BaristaScriptLanguage::_debug_get_stack_level_source(int32_t) const {
	return {};
}

godot::Dictionary BaristaScriptLanguage::_debug_get_stack_level_locals(int32_t, int32_t, int32_t) {
	return {};
}

godot::Dictionary BaristaScriptLanguage::_debug_get_stack_level_members(int32_t, int32_t, int32_t) {
	return {};
}

void *BaristaScriptLanguage::_debug_get_stack_level_instance(int32_t) {
	return nullptr;
}

godot::Dictionary BaristaScriptLanguage::_debug_get_globals(int32_t, int32_t) {
	return {};
}

godot::String BaristaScriptLanguage::_debug_parse_stack_level_expression(int32_t, const godot::String &, int32_t, int32_t) {
	return {};
}

godot::TypedArray<godot::Dictionary> BaristaScriptLanguage::_debug_get_current_stack_info() {
	return {};
}

void BaristaScriptLanguage::_reload_all_scripts() {}

void BaristaScriptLanguage::_reload_scripts(const godot::Array &, bool) {}

void BaristaScriptLanguage::_reload_tool_script(const godot::Ref<godot::Script> &, bool) {}

godot::PackedStringArray BaristaScriptLanguage::_get_recognized_extensions() const {
	godot::PackedStringArray extensions;
	extensions.push_back("barista");
	return extensions;
}

godot::TypedArray<godot::Dictionary> BaristaScriptLanguage::_get_public_functions() const {
	return {};
}

godot::Dictionary BaristaScriptLanguage::_get_public_constants() const {
	return {};
}

godot::TypedArray<godot::Dictionary> BaristaScriptLanguage::_get_public_annotations() const {
	return {};
}

void BaristaScriptLanguage::_profiling_start() {}

void BaristaScriptLanguage::_profiling_stop() {}

void BaristaScriptLanguage::_profiling_set_save_native_calls(bool) {}

int32_t BaristaScriptLanguage::_profiling_get_accumulated_data(godot::ScriptLanguageExtensionProfilingInfo *, int32_t) {
	return 0;
}

int32_t BaristaScriptLanguage::_profiling_get_frame_data(godot::ScriptLanguageExtensionProfilingInfo *, int32_t) {
	return 0;
}

void BaristaScriptLanguage::_frame() {}

bool BaristaScriptLanguage::_handles_global_class_type(const godot::String &p_type) const {
	// `p_type` is the resource type the editor's scan recorded for the file
	// (`EditorFileSystem::_get_global_script_class`, editor/file_system/editor_file_system.cpp:2102
	// at 4.7.2-stable), which for a `.barista` file is what
	// `BaristaScriptResourceLoader::_get_resource_type()` returns. The two spellings must agree or
	// the scan finds no language for the class and drops it silently, so this compares against
	// `_get_type()` rather than repeating the literal.
	return p_type == _get_type();
}

godot::Dictionary BaristaScriptLanguage::_get_global_class_name(const godot::String &p_path) const {
	return bs_resolve_global_class(p_path).to_dictionary();
}

Vector<String> BaristaScriptLanguage::get_conformance_files_in_namespace(const String &p_namespace) const {
	return declaration_index.get_conformance_files_in_namespace(p_namespace);
}

uint64_t BaristaScriptLanguage::claim_declaration_refresh(const String &p_path) {
	return declaration_index.claim_refresh(p_path);
}

bool BaristaScriptLanguage::commit_declaration_record(uint64_t p_token, const BSDeclarationRecord &p_record) {
	Vector<String> changed;
	const bool committed = declaration_index.commit_record(p_token, p_record, &changed);
	if (committed) {
		notify_conformance_namespaces_changed(changed);
	}
	return committed;
}

bool BaristaScriptLanguage::remove_declaration_path(const String &p_path, uint64_t p_token) {
	Vector<String> changed;
	const bool removed = declaration_index.remove_path(p_path, p_token, &changed);
	if (removed) {
		notify_conformance_namespaces_changed(changed);
	}
	return removed;
}

void BaristaScriptLanguage::synchronize_declaration_path_from_source(const String &p_path, const String &p_source) {
	const String path = p_path.simplify_path();
	const uint64_t token = declaration_index.claim_refresh(path);
	const BSGlobalClass resolved = bs_resolve_global_class_from_source(p_source, path);
	if (!resolved.declarations_parsed) {
		Vector<String> changed;
		declaration_index.remove_path(path, token, &changed);
		notify_conformance_namespaces_changed(changed);
		return;
	}
	BSDeclarationRecord record = BSDeclarationIndex::record_from_global_class(path, p_source, resolved);
	Vector<String> changed;
	if (declaration_index.commit_record(token, record, &changed)) {
		notify_conformance_namespaces_changed(changed);
	}
}

Error BaristaScriptLanguage::flush_declaration_index(const String &p_store_path) {
	const String path = p_store_path.is_empty() ? BSDeclarationIndex::get_default_store_path() : p_store_path;
	return declaration_index.flush(path);
}

BSDeclarationIndexLoadStatus BaristaScriptLanguage::load_declaration_index(const String &p_store_path) {
	const String path = p_store_path.is_empty() ? BSDeclarationIndex::get_default_store_path() : p_store_path;
	return declaration_index.load(path);
}

void BaristaScriptLanguage::notify_conformance_namespaces_changed(const Vector<String> &p_namespaces) {
	HashSet<String> seen;
	for (int i = 0; i < p_namespaces.size(); i++) {
		const String ns = p_namespaces[i];
		if (ns.is_empty() || seen.has(ns)) {
			continue;
		}
		seen.insert(ns);
		const Vector<String> reaching = BSCache::collect_parsers_reaching_namespace(ns);
		for (int j = 0; j < reaching.size(); j++) {
			BSCache::remove_parser(reaching[j]);
		}
	}
}

} // namespace barista_script
