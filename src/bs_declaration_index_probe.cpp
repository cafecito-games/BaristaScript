/**************************************************************************/
/*  bs_declaration_index_probe.cpp                                        */
/*                                                                        */
/*  Debug-only test surface for the private declaration index (#44).      */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#ifdef DEBUG_ENABLED

#include "bs_declaration_index_probe.h"

#include "barista_script_language.h"
#include "bs_analyzer.h"
#include "bs_declaration_index.h"
#include "bs_parser.h"
#include "bs_script_server.h"

#include <godot_cpp/core/class_db.hpp>

namespace barista_script {

namespace {

BSDeclarationRecord record_from_dictionary(const godot::Dictionary &p_record) {
	BSDeclarationRecord record;
	record.path = p_record.get("path", godot::String());
	record.source_digest = (uint64_t)(int64_t)p_record.get("source_digest", 0);
	record.namespace_name = p_record.get("namespace_name", godot::String());
	record.qualified_name = p_record.get("qualified_name", godot::String());
	record.kind = (BSDeclarationKind)(int)p_record.get("kind", 0);
	record.base_type = p_record.get("base_type", godot::String());
	record.is_abstract = p_record.get("is_abstract", false);
	record.is_tool = p_record.get("is_tool", false);
	record.icon_path = p_record.get("icon_path", godot::String());
	record.declares_retroactive_conformances = p_record.get("declares_retroactive_conformances", false);
	const godot::PackedStringArray annotations = p_record.get("global_annotations", godot::PackedStringArray());
	for (int i = 0; i < annotations.size(); i++) {
		record.global_annotations.push_back(annotations[i]);
	}
	return record;
}

godot::Dictionary dictionary_from_record(const BSDeclarationRecord &p_record) {
	godot::Dictionary dictionary;
	dictionary["path"] = p_record.path;
	dictionary["source_digest"] = (int64_t)p_record.source_digest;
	dictionary["namespace_name"] = p_record.namespace_name;
	dictionary["qualified_name"] = p_record.qualified_name;
	dictionary["kind"] = (int)p_record.kind;
	dictionary["kind_name"] = bs_declaration_kind_name(p_record.kind);
	dictionary["base_type"] = p_record.base_type;
	dictionary["is_abstract"] = p_record.is_abstract;
	dictionary["is_tool"] = p_record.is_tool;
	dictionary["icon_path"] = p_record.icon_path;
	dictionary["declares_retroactive_conformances"] = p_record.declares_retroactive_conformances;
	godot::PackedStringArray annotations;
	for (int i = 0; i < p_record.global_annotations.size(); i++) {
		annotations.push_back(p_record.global_annotations[i]);
	}
	dictionary["global_annotations"] = annotations;
	return dictionary;
}

godot::PackedStringArray to_packed(const Vector<String> &p_values) {
	godot::PackedStringArray result;
	for (int i = 0; i < p_values.size(); i++) {
		result.push_back(p_values[i]);
	}
	return result;
}

BSDeclarationIndex &index() {
	static BSDeclarationIndex fallback;
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	ERR_FAIL_COND_V(language == nullptr, fallback);
	return language->get_declaration_index();
}

} // namespace

void BaristaScriptDeclarationIndexProbe::_bind_methods() {
	ClassDB::bind_static_method("BaristaScriptDeclarationIndexProbe", D_METHOD("get_format_version"), &BaristaScriptDeclarationIndexProbe::get_format_version);
	ClassDB::bind_static_method("BaristaScriptDeclarationIndexProbe", D_METHOD("get_default_store_path"), &BaristaScriptDeclarationIndexProbe::get_default_store_path);
	ClassDB::bind_static_method("BaristaScriptDeclarationIndexProbe", D_METHOD("compute_source_digest", "source"), &BaristaScriptDeclarationIndexProbe::compute_source_digest);
	ClassDB::bind_static_method("BaristaScriptDeclarationIndexProbe", D_METHOD("load_status_names"), &BaristaScriptDeclarationIndexProbe::load_status_names);

	ClassDB::bind_method(D_METHOD("load", "store_path"), &BaristaScriptDeclarationIndexProbe::load);
	ClassDB::bind_method(D_METHOD("flush", "store_path", "fault"), &BaristaScriptDeclarationIndexProbe::flush);
	ClassDB::bind_method(D_METHOD("clear"), &BaristaScriptDeclarationIndexProbe::clear);
	ClassDB::bind_method(D_METHOD("get_record_count"), &BaristaScriptDeclarationIndexProbe::get_record_count);
	ClassDB::bind_method(D_METHOD("get_load_report"), &BaristaScriptDeclarationIndexProbe::get_load_report);
	ClassDB::bind_method(D_METHOD("get_records"), &BaristaScriptDeclarationIndexProbe::get_records);
	ClassDB::bind_method(D_METHOD("get_conformance_files_in_namespace", "namespace_name"), &BaristaScriptDeclarationIndexProbe::get_conformance_files_in_namespace);
	ClassDB::bind_method(D_METHOD("get_annotation_declaring_paths", "annotation"), &BaristaScriptDeclarationIndexProbe::get_annotation_declaring_paths);
	ClassDB::bind_method(D_METHOD("claim_refresh", "path"), &BaristaScriptDeclarationIndexProbe::claim_refresh);
	ClassDB::bind_method(D_METHOD("commit_record", "token", "record"), &BaristaScriptDeclarationIndexProbe::commit_record);
	ClassDB::bind_method(D_METHOD("remove_path", "path", "token"), &BaristaScriptDeclarationIndexProbe::remove_path);
	ClassDB::bind_method(D_METHOD("remove_path_unconditional", "path"), &BaristaScriptDeclarationIndexProbe::remove_path_unconditional);
	ClassDB::bind_method(D_METHOD("synchronize_path_from_source", "path", "source"), &BaristaScriptDeclarationIndexProbe::synchronize_path_from_source);
	ClassDB::bind_method(D_METHOD("host_conformance_files_in_namespace", "namespace_name"), &BaristaScriptDeclarationIndexProbe::host_conformance_files_in_namespace);
	ClassDB::bind_method(D_METHOD("host_is_bootstrap_path_allowed", "path"), &BaristaScriptDeclarationIndexProbe::host_is_bootstrap_path_allowed);
	ClassDB::bind_method(D_METHOD("set_bootstrap_root", "root"), &BaristaScriptDeclarationIndexProbe::set_bootstrap_root);
	ClassDB::bind_method(D_METHOD("lookup_qualified_name", "qualified_name"), &BaristaScriptDeclarationIndexProbe::lookup_qualified_name);
	ClassDB::bind_method(D_METHOD("script_server_is_global_class_enum", "name"), &BaristaScriptDeclarationIndexProbe::script_server_is_global_class_enum);
	ClassDB::bind_method(D_METHOD("script_server_get_global_class_path", "name"), &BaristaScriptDeclarationIndexProbe::script_server_get_global_class_path);
	ClassDB::bind_method(D_METHOD("script_server_get_global_class_list"), &BaristaScriptDeclarationIndexProbe::script_server_get_global_class_list);
}

int BaristaScriptDeclarationIndexProbe::get_format_version() {
	return (int)BSDeclarationIndex::FORMAT_VERSION;
}

godot::String BaristaScriptDeclarationIndexProbe::get_default_store_path() {
	return BSDeclarationIndex::get_default_store_path();
}

int64_t BaristaScriptDeclarationIndexProbe::compute_source_digest(const godot::String &p_source) {
	return (int64_t)BSDeclarationIndex::compute_source_digest(p_source);
}

godot::PackedStringArray BaristaScriptDeclarationIndexProbe::load_status_names() {
	godot::PackedStringArray names;
	for (int i = 0; i <= (int)BSDeclarationIndexLoadStatus::UNSORTED; i++) {
		names.push_back(bs_declaration_index_load_status_name((BSDeclarationIndexLoadStatus)i));
	}
	return names;
}

int BaristaScriptDeclarationIndexProbe::load(const godot::String &p_store_path) {
	return (int)index().load(p_store_path);
}

int BaristaScriptDeclarationIndexProbe::flush(const godot::String &p_store_path, int p_fault) {
	BSDeclarationIndex::WriteFault fault = BSDeclarationIndex::WriteFault::NONE;
	if (p_fault == 1) {
		fault = BSDeclarationIndex::WriteFault::BEFORE_WRITE;
	} else if (p_fault == 2) {
		fault = BSDeclarationIndex::WriteFault::AFTER_WRITE_BEFORE_RENAME;
	} else if (p_fault == 3) {
		fault = BSDeclarationIndex::WriteFault::TRUNCATE_TEMP_AFTER_WRITE;
	}
	return (int)index().flush(p_store_path, fault);
}

void BaristaScriptDeclarationIndexProbe::clear() {
	index().clear();
}

int BaristaScriptDeclarationIndexProbe::get_record_count() const {
	return index().get_record_count();
}

godot::PackedStringArray BaristaScriptDeclarationIndexProbe::get_load_report() const {
	return to_packed(index().get_load_report());
}

godot::Array BaristaScriptDeclarationIndexProbe::get_records() const {
	godot::Array result;
	const Vector<BSDeclarationRecord> records = index().get_records();
	for (int i = 0; i < records.size(); i++) {
		result.push_back(dictionary_from_record(records[i]));
	}
	return result;
}

godot::PackedStringArray BaristaScriptDeclarationIndexProbe::get_conformance_files_in_namespace(const godot::String &p_namespace) const {
	return to_packed(index().get_conformance_files_in_namespace(p_namespace));
}

godot::PackedStringArray BaristaScriptDeclarationIndexProbe::get_annotation_declaring_paths(const godot::String &p_annotation) const {
	return to_packed(index().get_annotation_declaring_paths(p_annotation));
}

int64_t BaristaScriptDeclarationIndexProbe::claim_refresh(const godot::String &p_path) {
	return (int64_t)index().claim_refresh(p_path);
}

bool BaristaScriptDeclarationIndexProbe::commit_record(int64_t p_token, const godot::Dictionary &p_record) {
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	ERR_FAIL_COND_V(language == nullptr, false);
	return language->commit_declaration_record((uint64_t)p_token, record_from_dictionary(p_record));
}

bool BaristaScriptDeclarationIndexProbe::remove_path(const godot::String &p_path, int64_t p_token) {
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	ERR_FAIL_COND_V(language == nullptr, false);
	return language->remove_declaration_path(p_path, (uint64_t)p_token);
}

void BaristaScriptDeclarationIndexProbe::remove_path_unconditional(const godot::String &p_path) {
	Vector<String> changed;
	index().remove_path_unconditional(p_path, &changed);
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	if (language != nullptr) {
		language->notify_conformance_namespaces_changed(changed);
	}
}

void BaristaScriptDeclarationIndexProbe::synchronize_path_from_source(const godot::String &p_path, const godot::String &p_source) {
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	ERR_FAIL_COND(language == nullptr);
	language->synchronize_declaration_path_from_source(p_path, p_source);
}

godot::PackedStringArray BaristaScriptDeclarationIndexProbe::host_conformance_files_in_namespace(const godot::String &p_namespace) const {
	const BSParserHost *host = BSParserHost::get_singleton();
	ERR_FAIL_COND_V(host == nullptr, godot::PackedStringArray());
	return to_packed(host->get_conformance_files_in_namespace(p_namespace));
}

bool BaristaScriptDeclarationIndexProbe::host_is_bootstrap_path_allowed(const godot::String &p_path) const {
	const BSParserHost *host = BSParserHost::get_singleton();
	ERR_FAIL_COND_V(host == nullptr, true);
	return host->is_bootstrap_path_allowed(p_path);
}

void BaristaScriptDeclarationIndexProbe::set_bootstrap_root(const godot::String &p_root) {
	BSAnalyzer::set_bootstrap_allowed_dependency_root(p_root);
}

godot::Dictionary BaristaScriptDeclarationIndexProbe::lookup_qualified_name(const godot::String &p_qualified_name) {
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	ERR_FAIL_COND_V(language == nullptr, godot::Dictionary());
	BSDeclarationRecord record;
	if (!language->try_resolve_declaration(p_qualified_name, record)) {
		return godot::Dictionary();
	}
	return dictionary_from_record(record);
}

bool BaristaScriptDeclarationIndexProbe::script_server_is_global_class_enum(const godot::String &p_name) const {
	return ScriptServer::is_global_class_enum(StringName(p_name));
}

godot::String BaristaScriptDeclarationIndexProbe::script_server_get_global_class_path(const godot::String &p_name) const {
	return ScriptServer::get_global_class_path(StringName(p_name));
}

godot::PackedStringArray BaristaScriptDeclarationIndexProbe::script_server_get_global_class_list() const {
	List<StringName> names;
	ScriptServer::get_global_class_list(&names);
	godot::PackedStringArray result;
	for (const List<StringName>::Element *E = names.front(); E; E = E->next()) {
		result.push_back(String(E->get()));
	}
	return result;
}

} // namespace barista_script

#endif // DEBUG_ENABLED
