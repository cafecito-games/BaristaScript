/**************************************************************************/
/*  barista_script_parse_cache.cpp                                        */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script_parse_cache.h"

#include "bs_parser.h"

#include <cstring>

#include <godot_cpp/core/class_db.hpp>

namespace {

// godot-cpp keeps Vector<T> and the generated packed arrays apart, so the handle converts by
// copying bytes and strings once at the boundary; the cache never sees a Variant.
godot::PackedByteArray to_packed_bytes(const Vector<uint8_t> &p_bytes) {
	godot::PackedByteArray result;
	result.resize(p_bytes.size());
	if (p_bytes.size() > 0) {
		memcpy(result.ptrw(), p_bytes.ptr(), p_bytes.size());
	}
	return result;
}

Vector<uint8_t> from_packed_bytes(const godot::PackedByteArray &p_bytes) {
	Vector<uint8_t> result;
	result.resize(p_bytes.size());
	if (p_bytes.size() > 0) {
		memcpy(result.ptrw(), p_bytes.ptr(), p_bytes.size());
	}
	return result;
}

godot::PackedStringArray to_packed_strings(const Vector<String> &p_lines) {
	godot::PackedStringArray result;
	for (int64_t index = 0; index < p_lines.size(); index++) {
		result.push_back(p_lines[index]);
	}
	return result;
}

BSParseCache::WriteFault write_fault_from_int(int p_fault) {
	if (p_fault == (int)BSParseCache::WriteFault::BEFORE_WRITE) {
		return BSParseCache::WriteFault::BEFORE_WRITE;
	}
	if (p_fault == (int)BSParseCache::WriteFault::AFTER_WRITE_BEFORE_RENAME) {
		return BSParseCache::WriteFault::AFTER_WRITE_BEFORE_RENAME;
	}
	if (p_fault == (int)BSParseCache::WriteFault::TRUNCATE_TEMP_AFTER_WRITE) {
		return BSParseCache::WriteFault::TRUNCATE_TEMP_AFTER_WRITE;
	}
	return BSParseCache::WriteFault::NONE;
}

} // namespace

namespace barista_script {

void BaristaScriptParseCache::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load", "store_path"), &BaristaScriptParseCache::load);
	ClassDB::bind_method(D_METHOD("lookup", "script_path", "source"), &BaristaScriptParseCache::lookup);
	ClassDB::bind_method(D_METHOD("put", "script_path", "source", "payload"), &BaristaScriptParseCache::put);
	ClassDB::bind_method(D_METHOD("flush", "store_path", "fault", "version_tag"), &BaristaScriptParseCache::flush);
	ClassDB::bind_method(D_METHOD("evict_entries_with_missing_files"), &BaristaScriptParseCache::evict_entries_with_missing_files);
	ClassDB::bind_method(D_METHOD("has_entry", "script_path"), &BaristaScriptParseCache::has_entry);
	ClassDB::bind_method(D_METHOD("get_entry_count"), &BaristaScriptParseCache::get_entry_count);
	ClassDB::bind_method(D_METHOD("get_load_report"), &BaristaScriptParseCache::get_load_report);
	ClassDB::bind_method(D_METHOD("clear"), &BaristaScriptParseCache::clear);

	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("get_miss_reason_names"), &BaristaScriptParseCache::get_miss_reason_names);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("get_miss_reason_name", "index"), &BaristaScriptParseCache::get_miss_reason_name);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("get_miss_reason_log_line", "index", "script_path"), &BaristaScriptParseCache::get_miss_reason_log_line);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("get_cache_format_version"), &BaristaScriptParseCache::get_cache_format_version);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("get_default_store_path"), &BaristaScriptParseCache::get_default_store_path);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("compute_source_digest", "source"), &BaristaScriptParseCache::compute_source_digest);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("compute_entry_checksum", "record_bytes"), &BaristaScriptParseCache::compute_entry_checksum);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("get_source_code", "path"), &BaristaScriptParseCache::get_source_code);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("set_source_override", "path", "source"), &BaristaScriptParseCache::set_source_override);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("has_source_override", "path"), &BaristaScriptParseCache::has_source_override);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("clear_source_override", "path"), &BaristaScriptParseCache::clear_source_override);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("clear_source_overrides"), &BaristaScriptParseCache::clear_source_overrides);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("record_dependency", "path", "owner"), &BaristaScriptParseCache::record_dependency);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("get_inverse_dependencies", "path"), &BaristaScriptParseCache::get_inverse_dependencies);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("remove_script", "path"), &BaristaScriptParseCache::remove_script);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("move_script", "from", "to"), &BaristaScriptParseCache::move_script);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("clear_script_cache"), &BaristaScriptParseCache::clear_script_cache);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("get_parser", "path", "status", "owner"), &BaristaScriptParseCache::get_parser, DEFVAL(godot::String()));
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("has_parser", "path"), &BaristaScriptParseCache::has_parser);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("remove_parser", "path"), &BaristaScriptParseCache::remove_parser);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("collect_parser_invalidation_closure", "path"), &BaristaScriptParseCache::collect_parser_invalidation_closure);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("collect_parsers_reaching_namespace", "namespace_name"), &BaristaScriptParseCache::collect_parsers_reaching_namespace);
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("invalidate_analysis"), &BaristaScriptParseCache::invalidate_analysis);
#ifdef DEBUG_ENABLED
	ClassDB::bind_static_method("BaristaScriptParseCache", D_METHOD("invalidate_analysis_on_strict_settings_change"), &BaristaScriptParseCache::invalidate_analysis_on_strict_settings_change);
#endif // DEBUG_ENABLED
}

int BaristaScriptParseCache::load(const godot::String &p_store_path) {
	return (int)cache.load(p_store_path);
}

godot::Dictionary BaristaScriptParseCache::lookup(const godot::String &p_script_path, const godot::String &p_source) {
	const BSParseCache::Lookup result = cache.lookup(p_script_path, p_source);
	godot::Dictionary dictionary;
	dictionary["hit"] = result.hit;
	dictionary["reason"] = (int)result.reason;
	dictionary["reason_name"] = bs_miss::get_name(result.reason);
	dictionary["payload"] = to_packed_bytes(result.payload);
	return dictionary;
}

void BaristaScriptParseCache::put(const godot::String &p_script_path, const godot::String &p_source,
		const godot::PackedByteArray &p_payload) {
	cache.put(p_script_path, p_source, from_packed_bytes(p_payload));
}

int BaristaScriptParseCache::flush(const godot::String &p_store_path, int p_fault, int p_version_tag) {
	return (int)cache.flush(p_store_path, write_fault_from_int(p_fault), (uint32_t)p_version_tag);
}

godot::PackedStringArray BaristaScriptParseCache::evict_entries_with_missing_files() {
	return to_packed_strings(cache.evict_entries_with_missing_files());
}

bool BaristaScriptParseCache::has_entry(const godot::String &p_script_path) const {
	return cache.has_entry(p_script_path);
}

int BaristaScriptParseCache::get_entry_count() const {
	return cache.get_entry_count();
}

godot::PackedStringArray BaristaScriptParseCache::get_load_report() const {
	return to_packed_strings(cache.get_load_report());
}

void BaristaScriptParseCache::clear() {
	cache.clear();
}

godot::PackedStringArray BaristaScriptParseCache::get_miss_reason_names() {
	return to_packed_strings(bs_miss::get_names());
}

godot::String BaristaScriptParseCache::get_miss_reason_name(int p_index) {
	return bs_miss::get_name(bs_miss::from_index(p_index));
}

godot::String BaristaScriptParseCache::get_miss_reason_log_line(int p_index, const godot::String &p_script_path) {
	return bs_miss::get_log_line(bs_miss::from_index(p_index), p_script_path);
}

int BaristaScriptParseCache::get_cache_format_version() {
	return (int)BSParseCache::CACHE_FORMAT_VERSION;
}

godot::String BaristaScriptParseCache::get_default_store_path() {
	return BSParseCache::get_default_store_path();
}

int64_t BaristaScriptParseCache::compute_source_digest(const godot::String &p_source) {
	return (int64_t)BSParseCache::compute_source_digest(p_source);
}

int64_t BaristaScriptParseCache::compute_entry_checksum(const godot::PackedByteArray &p_record_bytes) {
	return (int64_t)BSParseCache::compute_entry_checksum(from_packed_bytes(p_record_bytes));
}

godot::String BaristaScriptParseCache::get_source_code(const godot::String &p_path) {
	return BSCache::get_source_code(p_path);
}

void BaristaScriptParseCache::set_source_override(const godot::String &p_path, const godot::String &p_source) {
	BSCache::set_source_override(p_path, p_source);
}

bool BaristaScriptParseCache::has_source_override(const godot::String &p_path) {
	return BSCache::has_source_override(p_path);
}

void BaristaScriptParseCache::clear_source_override(const godot::String &p_path) {
	BSCache::clear_source_override(p_path);
}

void BaristaScriptParseCache::clear_source_overrides() {
	BSCache::clear_source_overrides();
}

void BaristaScriptParseCache::record_dependency(const godot::String &p_path, const godot::String &p_owner) {
	BSCache::record_dependency(p_path, p_owner);
}

godot::PackedStringArray BaristaScriptParseCache::get_inverse_dependencies(const godot::String &p_path) {
	godot::PackedStringArray result;
	for (const String &dependency : BSCache::get_inverse_dependencies(p_path)) {
		result.push_back(dependency);
	}
	return result;
}

void BaristaScriptParseCache::remove_script(const godot::String &p_path) {
	BSCache::remove_script(p_path);
}

void BaristaScriptParseCache::move_script(const godot::String &p_from, const godot::String &p_to) {
	BSCache::move_script(p_from, p_to);
}

void BaristaScriptParseCache::clear_script_cache() {
	BSCache::clear();
}

godot::Dictionary BaristaScriptParseCache::get_parser(const godot::String &p_path, int p_status, const godot::String &p_owner) {
	Error err = OK;
	const BSParserRef::Status status = (BSParserRef::Status)CLAMP(p_status, 0, (int)BSParserRef::FULLY_SOLVED);
	Ref<BSParserRef> ref = BSCache::get_parser(p_path, status, err, p_owner);
	godot::Dictionary result;
	result["valid"] = ref.is_valid();
	result["error"] = (int)err;
	result["status"] = ref.is_valid() ? (int)ref->get_status() : -1;
	result["result"] = ref.is_valid() ? (int)ref->get_result() : (int)err;
	result["path"] = ref.is_valid() ? ref->get_path() : godot::String();
	result["source_hash"] = ref.is_valid() ? (int64_t)ref->get_source_hash() : 0;
	return result;
}

bool BaristaScriptParseCache::has_parser(const godot::String &p_path) {
	return BSCache::has_parser(p_path);
}

void BaristaScriptParseCache::remove_parser(const godot::String &p_path) {
	BSCache::remove_parser(p_path);
}

godot::PackedStringArray BaristaScriptParseCache::collect_parser_invalidation_closure(const godot::String &p_path) {
	godot::PackedStringArray result;
	for (const String &path : BSCache::collect_parser_invalidation_closure(p_path)) {
		result.push_back(path);
	}
	return result;
}

godot::PackedStringArray BaristaScriptParseCache::collect_parsers_reaching_namespace(const godot::String &p_namespace) {
	return to_packed_strings(BSCache::collect_parsers_reaching_namespace(p_namespace));
}

void BaristaScriptParseCache::invalidate_analysis() {
	BSCache::invalidate_analysis();
}

#ifdef DEBUG_ENABLED
bool BaristaScriptParseCache::invalidate_analysis_on_strict_settings_change() {
	return BSParser::invalidate_analysis_on_strict_settings_change();
}
#endif // DEBUG_ENABLED

} // namespace barista_script
