/**************************************************************************/
/*  bs_declaration_index.cpp                                              */
/*                                                                        */
/*  Private BaristaScript declaration index (issue #44 / epic #42).       */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_declaration_index.h"

#include <algorithm>
#include <cstring>

namespace barista_script {

const char *const BSDeclarationIndex::STORE_MAGIC = "BSGI";

namespace {

constexpr uint64_t BS_INDEX_SOURCE_DIGEST_BASIS = 14695981039346656037ULL;
constexpr uint64_t BS_INDEX_RECORD_CHECKSUM_BASIS = 1469598103934665403ULL;
constexpr uint64_t BS_INDEX_FILE_CHECKSUM_BASIS = 1469598103934665509ULL;

uint64_t bs_hash_bytes(const uint8_t *p_data, uint64_t p_length, uint64_t p_basis) {
	uint64_t hash = p_basis;
	for (uint64_t i = 0; i < p_length; i++) {
		hash ^= (uint64_t)p_data[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

void bs_put_u32(Vector<uint8_t> &p_destination, uint32_t p_value) {
	p_destination.push_back((uint8_t)(p_value & 0xFF));
	p_destination.push_back((uint8_t)((p_value >> 8) & 0xFF));
	p_destination.push_back((uint8_t)((p_value >> 16) & 0xFF));
	p_destination.push_back((uint8_t)((p_value >> 24) & 0xFF));
}

uint32_t bs_get_u32(const uint8_t *p_source, uint64_t p_offset) {
	return (uint32_t)p_source[p_offset] | ((uint32_t)p_source[p_offset + 1] << 8) |
			((uint32_t)p_source[p_offset + 2] << 16) | ((uint32_t)p_source[p_offset + 3] << 24);
}

void bs_put_u64(Vector<uint8_t> &p_destination, uint64_t p_value) {
	for (int shift = 0; shift < 64; shift += 8) {
		p_destination.push_back((uint8_t)((p_value >> shift) & 0xFF));
	}
}

uint64_t bs_get_u64(const uint8_t *p_source, uint64_t p_offset) {
	uint64_t value = 0;
	for (int index = 0; index < 8; index++) {
		value |= (uint64_t)p_source[p_offset + index] << (index * 8);
	}
	return value;
}

void bs_put_u8(Vector<uint8_t> &p_destination, uint8_t p_value) {
	p_destination.push_back(p_value);
}

bool bs_put_string(Vector<uint8_t> &p_destination, const String &p_value, String &r_error) {
	const PackedByteArray utf8 = p_value.to_utf8_buffer();
	for (int64_t i = 0; i < utf8.size(); i++) {
		if (utf8.ptr()[i] == 0) {
			r_error = "embedded NUL in length-prefixed string";
			return false;
		}
	}
	bs_put_u32(p_destination, (uint32_t)utf8.size());
	for (int64_t i = 0; i < utf8.size(); i++) {
		p_destination.push_back(utf8.ptr()[i]);
	}
	return true;
}

bool bs_get_string(const uint8_t *p_data, uint64_t p_size, uint64_t &r_offset, String &r_value, String &r_error) {
	if (r_offset + 4 > p_size) {
		r_error = "truncated length prefix";
		return false;
	}
	const uint32_t length = bs_get_u32(p_data, r_offset);
	r_offset += 4;
	if (r_offset + length > p_size) {
		r_error = "truncated string payload";
		return false;
	}
	for (uint32_t i = 0; i < length; i++) {
		if (p_data[r_offset + i] == 0) {
			r_error = "embedded NUL in length-prefixed string";
			return false;
		}
	}
	// Match BSParseCache: decode then re-encode so embedded NULs and lossy UTF-8 cannot invent a
	// shorter trusted key.
	r_value = String::utf8((const char *)(p_data + r_offset), (int64_t)length);
	r_offset += length;
	if (length == 0) {
		return true;
	}
	if (r_value.is_empty()) {
		r_error = "invalid UTF-8";
		return false;
	}
	const PackedByteArray round_trip = r_value.to_utf8_buffer();
	if ((uint64_t)round_trip.size() != (uint64_t)length ||
			memcmp(round_trip.ptr(), p_data + r_offset - length, length) != 0) {
		r_error = "invalid UTF-8";
		return false;
	}
	return true;
}

bool is_canonical_res_path(const String &p_path) {
	if (p_path.is_empty() || !p_path.begins_with("res://")) {
		return false;
	}
	if (p_path.contains("\\")) {
		return false;
	}
	// Reject `res:////` style duplicates past the scheme.
	if (p_path.substr(6).contains("//")) {
		return false;
	}
	return p_path == p_path.simplify_path();
}

void discard_temp(const String &p_temp_path) {
	if (FileAccess::file_exists(p_temp_path)) {
		DirAccess::remove_absolute(p_temp_path);
	}
}

void truncate_for_test(const String &p_path, int64_t p_length) {
	const PackedByteArray whole = FileAccess::get_file_as_bytes(p_path);
	const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	ERR_FAIL_COND_MSG(file.is_null(), "truncate_for_test: could not reopen '" + p_path + "'.");
	file->store_buffer(whole.slice(0, p_length));
	file->close();
}

} // namespace

String bs_declaration_index_load_status_name(BSDeclarationIndexLoadStatus p_status) {
	switch (p_status) {
		case BSDeclarationIndexLoadStatus::OK:
			return "OK";
		case BSDeclarationIndexLoadStatus::COLD:
			return "COLD";
		case BSDeclarationIndexLoadStatus::BAD_MAGIC:
			return "BAD_MAGIC";
		case BSDeclarationIndexLoadStatus::UNSUPPORTED_VERSION:
			return "UNSUPPORTED_VERSION";
		case BSDeclarationIndexLoadStatus::TRUNCATED:
			return "TRUNCATED";
		case BSDeclarationIndexLoadStatus::TRAILING_BYTES:
			return "TRAILING_BYTES";
		case BSDeclarationIndexLoadStatus::BAD_CHECKSUM:
			return "BAD_CHECKSUM";
		case BSDeclarationIndexLoadStatus::INVALID_KIND:
			return "INVALID_KIND";
		case BSDeclarationIndexLoadStatus::INVALID_PATH:
			return "INVALID_PATH";
		case BSDeclarationIndexLoadStatus::INVALID_UTF8:
			return "INVALID_UTF8";
		case BSDeclarationIndexLoadStatus::DUPLICATE_PATH:
			return "DUPLICATE_PATH";
		case BSDeclarationIndexLoadStatus::DUPLICATE_NAME:
			return "DUPLICATE_NAME";
		case BSDeclarationIndexLoadStatus::UNSORTED:
			return "UNSORTED";
	}
	ERR_FAIL_V_MSG(String(), "bs_declaration_index_load_status_name: unhandled status.");
}

String BSDeclarationIndex::get_default_store_path() {
	return "res://.godot/barista_script/declaration_index.bsi";
}

uint64_t BSDeclarationIndex::compute_source_digest(const String &p_source) {
	const PackedByteArray utf8 = p_source.to_utf8_buffer();
	Vector<uint8_t> bytes;
	bs_put_u32(bytes, FORMAT_VERSION);
	bs_put_u64(bytes, (uint64_t)utf8.size());
	const int prefix = bytes.size();
	bytes.resize(prefix + utf8.size());
	if (utf8.size() > 0) {
		memcpy(bytes.ptrw() + prefix, utf8.ptr(), utf8.size());
	}
	return bs_hash_bytes(bytes.ptr(), bytes.size(), BS_INDEX_SOURCE_DIGEST_BASIS);
}

uint64_t BSDeclarationIndex::compute_record_checksum(const Vector<uint8_t> &p_record_bytes) {
	return bs_hash_bytes(p_record_bytes.ptr(), (uint64_t)p_record_bytes.size(), BS_INDEX_RECORD_CHECKSUM_BASIS);
}

uint64_t BSDeclarationIndex::compute_file_checksum(const Vector<uint8_t> &p_file_bytes) {
	return bs_hash_bytes(p_file_bytes.ptr(), (uint64_t)p_file_bytes.size(), BS_INDEX_FILE_CHECKSUM_BASIS);
}

void BSDeclarationIndex::_sort_unique(Vector<String> &p_values) {
	if (p_values.size() <= 1) {
		return;
	}
	Vector<String> sorted = p_values;
	std::sort(sorted.ptrw(), sorted.ptrw() + sorted.size());
	Vector<String> unique;
	for (int i = 0; i < sorted.size(); i++) {
		if (unique.is_empty() || unique[unique.size() - 1] != sorted[i]) {
			unique.push_back(sorted[i]);
		}
	}
	p_values = unique;
}

void BSDeclarationIndex::_rebuild_views_unlocked() {
	path_by_qualified_name.clear();
	conformance_files_by_namespace.clear();
	paths_by_annotation.clear();

	Vector<String> paths;
	for (const KeyValue<String, BSDeclarationRecord> &entry : by_path) {
		paths.push_back(entry.key);
	}
	_sort_unique(paths);

	for (const String &path : paths) {
		const BSDeclarationRecord &record = by_path[path];
		if (record.has_head_declaration()) {
			path_by_qualified_name[record.qualified_name] = path;
		}
		if (record.declares_retroactive_conformances && !record.namespace_name.is_empty()) {
			conformance_files_by_namespace[record.namespace_name].push_back(path);
		}
		for (int i = 0; i < record.global_annotations.size(); i++) {
			paths_by_annotation[record.global_annotations[i]].push_back(path);
		}
	}

	for (KeyValue<String, Vector<String>> &entry : conformance_files_by_namespace) {
		_sort_unique(entry.value);
	}
	for (KeyValue<String, Vector<String>> &entry : paths_by_annotation) {
		_sort_unique(entry.value);
	}
}

void BSDeclarationIndex::_erase_path_unlocked(const String &p_path, Vector<String> *r_changed_namespaces) {
	HashMap<String, BSDeclarationRecord>::Iterator found = by_path.find(p_path);
	if (found == by_path.end()) {
		return;
	}
	const BSDeclarationRecord previous = found->value;
	by_path.erase(p_path);
	_rebuild_views_unlocked();
	if (r_changed_namespaces != nullptr && previous.declares_retroactive_conformances && !previous.namespace_name.is_empty()) {
		r_changed_namespaces->push_back(previous.namespace_name);
	}
}

void BSDeclarationIndex::_insert_path_unlocked(const BSDeclarationRecord &p_record, Vector<String> *r_changed_namespaces) {
	String previous_namespace;
	bool previous_conformance = false;
	if (HashMap<String, BSDeclarationRecord>::Iterator found = by_path.find(p_record.path); found != by_path.end()) {
		previous_namespace = found->value.namespace_name;
		previous_conformance = found->value.declares_retroactive_conformances;
	}
	by_path[p_record.path] = p_record;
	_rebuild_views_unlocked();
	if (r_changed_namespaces != nullptr) {
		if (previous_conformance && !previous_namespace.is_empty()) {
			r_changed_namespaces->push_back(previous_namespace);
		}
		if (p_record.declares_retroactive_conformances && !p_record.namespace_name.is_empty()) {
			r_changed_namespaces->push_back(p_record.namespace_name);
		}
		_sort_unique(*r_changed_namespaces);
	}
}

bool BSDeclarationIndex::_is_token_current_unlocked(const String &p_path, uint64_t p_token) const {
	if (p_token == 0 || p_token <= generation_floor) {
		return false;
	}
	const uint64_t *claimed = generations.getptr(p_path);
	return claimed != nullptr && *claimed == p_token;
}

uint64_t BSDeclarationIndex::_claim_unlocked(const String &p_path) {
	const uint64_t token = ++generation_counter;
	generations[p_path] = token;
	return token;
}

uint64_t BSDeclarationIndex::claim_refresh(const String &p_path) {
	std::lock_guard<std::mutex> generation_lock(generation_mutex);
	return _claim_unlocked(p_path);
}

void BSDeclarationIndex::claim_rename_refresh(const String &p_from, const String &p_to, uint64_t &r_from_token, uint64_t &r_to_token) {
	std::lock_guard<std::mutex> generation_lock(generation_mutex);
	r_to_token = _claim_unlocked(p_to);
	r_from_token = _claim_unlocked(p_from);
}

void BSDeclarationIndex::invalidate_claims(const String &p_path) {
	std::lock_guard<std::mutex> generation_lock(generation_mutex);
	generations[p_path] = ++generation_counter;
}

void BSDeclarationIndex::invalidate_all_claims() {
	std::lock_guard<std::mutex> generation_lock(generation_mutex);
	generation_floor = ++generation_counter;
	generations.clear();
}

bool BSDeclarationIndex::commit_record(uint64_t p_token, const BSDeclarationRecord &p_record, Vector<String> *r_changed_namespaces) {
	BSDeclarationRecord record = p_record;
	record.path = record.path.simplify_path();
	_sort_unique(record.global_annotations);

	std::lock_guard<std::mutex> generation_lock(generation_mutex);
	if (!_is_token_current_unlocked(record.path, p_token)) {
		return false;
	}
	std::lock_guard<std::mutex> lock(mutex);
	_insert_path_unlocked(record, r_changed_namespaces);
	return true;
}

bool BSDeclarationIndex::remove_path(const String &p_path, uint64_t p_token, Vector<String> *r_changed_namespaces) {
	const String path = p_path.simplify_path();
	std::lock_guard<std::mutex> generation_lock(generation_mutex);
	if (!_is_token_current_unlocked(path, p_token)) {
		return false;
	}
	std::lock_guard<std::mutex> lock(mutex);
	_erase_path_unlocked(path, r_changed_namespaces);
	return true;
}

void BSDeclarationIndex::remove_path_unconditional(const String &p_path, Vector<String> *r_changed_namespaces) {
	const String path = p_path.simplify_path();
	{
		std::lock_guard<std::mutex> generation_lock(generation_mutex);
		generations[path] = ++generation_counter;
	}
	std::lock_guard<std::mutex> lock(mutex);
	_erase_path_unlocked(path, r_changed_namespaces);
}

void BSDeclarationIndex::clear() {
	{
		std::lock_guard<std::mutex> generation_lock(generation_mutex);
		generation_floor = ++generation_counter;
		generations.clear();
	}
	std::lock_guard<std::mutex> lock(mutex);
	by_path.clear();
	path_by_qualified_name.clear();
	conformance_files_by_namespace.clear();
	paths_by_annotation.clear();
	load_report.clear();
	last_load_status = BSDeclarationIndexLoadStatus::COLD;
}

Vector<BSDeclarationRecord> BSDeclarationIndex::get_records() const {
	std::lock_guard<std::mutex> lock(mutex);
	Vector<String> paths;
	for (const KeyValue<String, BSDeclarationRecord> &entry : by_path) {
		paths.push_back(entry.key);
	}
	Vector<String> sorted = paths;
	_sort_unique(sorted);
	Vector<BSDeclarationRecord> records;
	for (const String &path : sorted) {
		records.push_back(by_path[path]);
	}
	return records;
}

bool BSDeclarationIndex::has_path(const String &p_path) const {
	std::lock_guard<std::mutex> lock(mutex);
	return by_path.has(p_path.simplify_path());
}

bool BSDeclarationIndex::try_get_by_path(const String &p_path, BSDeclarationRecord &r_record) const {
	std::lock_guard<std::mutex> lock(mutex);
	HashMap<String, BSDeclarationRecord>::ConstIterator found = by_path.find(p_path.simplify_path());
	if (found == by_path.end()) {
		return false;
	}
	r_record = found->value;
	return true;
}

bool BSDeclarationIndex::try_get_by_qualified_name(const String &p_name, BSDeclarationRecord &r_record) const {
	std::lock_guard<std::mutex> lock(mutex);
	const String *path = path_by_qualified_name.getptr(p_name);
	if (path == nullptr) {
		return false;
	}
	r_record = by_path[*path];
	return true;
}

Vector<String> BSDeclarationIndex::get_conformance_files_in_namespace(const String &p_namespace) const {
	std::lock_guard<std::mutex> lock(mutex);
	if (p_namespace.is_empty()) {
		return Vector<String>();
	}
	HashMap<String, Vector<String>>::ConstIterator found = conformance_files_by_namespace.find(p_namespace);
	if (found == conformance_files_by_namespace.end()) {
		return Vector<String>();
	}
	return found->value;
}

Vector<String> BSDeclarationIndex::get_annotation_declaring_paths(const String &p_qualified_annotation) const {
	std::lock_guard<std::mutex> lock(mutex);
	HashMap<String, Vector<String>>::ConstIterator found = paths_by_annotation.find(p_qualified_annotation);
	if (found == paths_by_annotation.end()) {
		return Vector<String>();
	}
	return found->value;
}

int BSDeclarationIndex::get_record_count() const {
	std::lock_guard<std::mutex> lock(mutex);
	return by_path.size();
}

BSDeclarationRecord BSDeclarationIndex::record_from_global_class(const String &p_path, const String &p_source, const BSGlobalClass &p_resolved) {
	BSDeclarationRecord record;
	record.path = p_path.simplify_path();
	record.source_digest = compute_source_digest(p_source);
	record.qualified_name = p_resolved.name;
	record.kind = p_resolved.kind;
	record.base_type = p_resolved.base_type;
	record.is_abstract = p_resolved.is_abstract;
	record.is_tool = p_resolved.is_tool;
	record.icon_path = p_resolved.icon_path;
	if (!record.qualified_name.is_empty()) {
		const int separator = record.qualified_name.rfind(".");
		if (separator >= 0) {
			record.namespace_name = record.qualified_name.substr(0, separator);
		}
	}
	return record;
}

bool BSDeclarationIndex::_encode_record(const BSDeclarationRecord &p_record, Vector<uint8_t> &r_bytes, String &r_error) {
	r_bytes.clear();
	if (!bs_put_string(r_bytes, p_record.path, r_error)) {
		return false;
	}
	bs_put_u64(r_bytes, p_record.source_digest);
	if (!bs_put_string(r_bytes, p_record.namespace_name, r_error)) {
		return false;
	}
	if (!bs_put_string(r_bytes, p_record.qualified_name, r_error)) {
		return false;
	}
	bs_put_u32(r_bytes, (uint32_t)p_record.kind);
	if (!bs_put_string(r_bytes, p_record.base_type, r_error)) {
		return false;
	}
	bs_put_u8(r_bytes, p_record.is_abstract ? 1 : 0);
	bs_put_u8(r_bytes, p_record.is_tool ? 1 : 0);
	if (!bs_put_string(r_bytes, p_record.icon_path, r_error)) {
		return false;
	}
	bs_put_u32(r_bytes, (uint32_t)p_record.global_annotations.size());
	for (int i = 0; i < p_record.global_annotations.size(); i++) {
		if (!bs_put_string(r_bytes, p_record.global_annotations[i], r_error)) {
			return false;
		}
	}
	bs_put_u8(r_bytes, p_record.declares_retroactive_conformances ? 1 : 0);
	return true;
}

bool BSDeclarationIndex::_decode_record(const uint8_t *p_data, uint64_t p_size, uint64_t &r_offset, BSDeclarationRecord &r_record, String &r_error) {
	r_record = BSDeclarationRecord();
	if (!bs_get_string(p_data, p_size, r_offset, r_record.path, r_error)) {
		return false;
	}
	if (r_offset + 8 > p_size) {
		r_error = "truncated source digest";
		return false;
	}
	r_record.source_digest = bs_get_u64(p_data, r_offset);
	r_offset += 8;
	if (!bs_get_string(p_data, p_size, r_offset, r_record.namespace_name, r_error)) {
		return false;
	}
	if (!bs_get_string(p_data, p_size, r_offset, r_record.qualified_name, r_error)) {
		return false;
	}
	if (r_offset + 4 > p_size) {
		r_error = "truncated kind";
		return false;
	}
	const uint32_t kind_value = bs_get_u32(p_data, r_offset);
	r_offset += 4;
	if (kind_value >= (uint32_t)BSDeclarationKind::MAX) {
		r_error = "invalid kind";
		return false;
	}
	r_record.kind = (BSDeclarationKind)kind_value;
	if (!bs_get_string(p_data, p_size, r_offset, r_record.base_type, r_error)) {
		return false;
	}
	if (r_offset + 2 > p_size) {
		r_error = "truncated flags";
		return false;
	}
	r_record.is_abstract = p_data[r_offset++] != 0;
	r_record.is_tool = p_data[r_offset++] != 0;
	if (!bs_get_string(p_data, p_size, r_offset, r_record.icon_path, r_error)) {
		return false;
	}
	if (r_offset + 4 > p_size) {
		r_error = "truncated annotation count";
		return false;
	}
	const uint32_t annotation_count = bs_get_u32(p_data, r_offset);
	r_offset += 4;
	String previous_annotation;
	for (uint32_t i = 0; i < annotation_count; i++) {
		String annotation;
		if (!bs_get_string(p_data, p_size, r_offset, annotation, r_error)) {
			return false;
		}
		if (!previous_annotation.is_empty() && annotation <= previous_annotation) {
			r_error = "unsorted annotations";
			return false;
		}
		previous_annotation = annotation;
		r_record.global_annotations.push_back(annotation);
	}
	if (r_offset + 1 > p_size) {
		r_error = "truncated conformance flag";
		return false;
	}
	r_record.declares_retroactive_conformances = p_data[r_offset++] != 0;
	return true;
}

BSDeclarationIndexLoadStatus BSDeclarationIndex::load(const String &p_store_path) {
	clear();

	const PackedByteArray bytes = FileAccess::get_file_as_bytes(p_store_path);
	const Error open_error = FileAccess::get_open_error();
	if (open_error == Error::ERR_FILE_NOT_FOUND) {
		last_load_status = BSDeclarationIndexLoadStatus::COLD;
		return last_load_status;
	}
	if (open_error != Error::OK) {
		last_load_status = BSDeclarationIndexLoadStatus::TRUNCATED;
		const String line = "declaration index '" + p_store_path + "' exists but cannot be read";
		load_report.push_back(line);
		ERR_PRINT(line);
		return last_load_status;
	}

	const uint8_t *data = bytes.ptr();
	const uint64_t size = bytes.size();
	auto reject = [&](BSDeclarationIndexLoadStatus p_status, const String &p_detail) {
		clear();
		last_load_status = p_status;
		const String line = "declaration index '" + p_store_path + "' rejected (" +
				bs_declaration_index_load_status_name(p_status) + "): " + p_detail;
		load_report.push_back(line);
		ERR_PRINT(line);
		return last_load_status;
	};

	if (size < 12) {
		return reject(BSDeclarationIndexLoadStatus::TRUNCATED, "shorter than header");
	}
	if (memcmp(data, STORE_MAGIC, 4) != 0) {
		return reject(BSDeclarationIndexLoadStatus::BAD_MAGIC, "magic is not BSGI");
	}
	const uint32_t version = bs_get_u32(data, 4);
	if (version != FORMAT_VERSION) {
		return reject(BSDeclarationIndexLoadStatus::UNSUPPORTED_VERSION, "version " + String::num_uint64(version));
	}
	if (size < 20) {
		return reject(BSDeclarationIndexLoadStatus::TRUNCATED, "missing file checksum");
	}
	const uint64_t stored_file_checksum = bs_get_u64(data, size - 8);
	const uint64_t computed_file_checksum = bs_hash_bytes(data, size - 8, BS_INDEX_FILE_CHECKSUM_BASIS);
	if (stored_file_checksum != computed_file_checksum) {
		return reject(BSDeclarationIndexLoadStatus::BAD_CHECKSUM, "file checksum mismatch");
	}

	const uint32_t entry_count = bs_get_u32(data, 8);
	uint64_t offset = 12;
	String previous_path;
	HashSet<String> seen_paths;
	HashSet<String> seen_names;
	Vector<BSDeclarationRecord> loaded;

	for (uint32_t i = 0; i < entry_count; i++) {
		const uint64_t record_start = offset;
		BSDeclarationRecord record;
		String error;
		if (!_decode_record(data, size - 8, offset, record, error)) {
			if (error == "invalid UTF-8") {
				return reject(BSDeclarationIndexLoadStatus::INVALID_UTF8, error);
			}
			if (error == "invalid kind") {
				return reject(BSDeclarationIndexLoadStatus::INVALID_KIND, error);
			}
			if (error.begins_with("truncated") || error.begins_with("embedded")) {
				return reject(BSDeclarationIndexLoadStatus::TRUNCATED, error);
			}
			if (error == "unsorted annotations") {
				return reject(BSDeclarationIndexLoadStatus::UNSORTED, error);
			}
			return reject(BSDeclarationIndexLoadStatus::TRUNCATED, error);
		}
		if (offset + 8 > size - 8) {
			return reject(BSDeclarationIndexLoadStatus::TRUNCATED, "missing record checksum");
		}
		const uint64_t stored_record_checksum = bs_get_u64(data, offset);
		offset += 8;
		const uint64_t computed_record_checksum = bs_hash_bytes(data + record_start, offset - 8 - record_start, BS_INDEX_RECORD_CHECKSUM_BASIS);
		if (stored_record_checksum != computed_record_checksum) {
			return reject(BSDeclarationIndexLoadStatus::BAD_CHECKSUM, "record checksum mismatch for '" + record.path + "'");
		}
		if (!is_canonical_res_path(record.path)) {
			return reject(BSDeclarationIndexLoadStatus::INVALID_PATH, "non-canonical path '" + record.path + "'");
		}
		if (!previous_path.is_empty() && record.path <= previous_path) {
			return reject(BSDeclarationIndexLoadStatus::UNSORTED, "records not strictly sorted by path");
		}
		previous_path = record.path;
		if (seen_paths.has(record.path)) {
			return reject(BSDeclarationIndexLoadStatus::DUPLICATE_PATH, record.path);
		}
		seen_paths.insert(record.path);
		if (record.has_head_declaration()) {
			if (seen_names.has(record.qualified_name)) {
				return reject(BSDeclarationIndexLoadStatus::DUPLICATE_NAME, record.qualified_name);
			}
			seen_names.insert(record.qualified_name);
		}
		loaded.push_back(record);
	}

	if (offset != size - 8) {
		return reject(BSDeclarationIndexLoadStatus::TRAILING_BYTES, "bytes remain after records");
	}

	std::lock_guard<std::mutex> lock(mutex);
	for (const BSDeclarationRecord &record : loaded) {
		by_path[record.path] = record;
	}
	_rebuild_views_unlocked();
	last_load_status = BSDeclarationIndexLoadStatus::OK;
	return last_load_status;
}

Error BSDeclarationIndex::flush(const String &p_store_path, WriteFault p_fault) {
	Vector<BSDeclarationRecord> records = get_records();

	Vector<uint8_t> store;
	store.push_back((uint8_t)STORE_MAGIC[0]);
	store.push_back((uint8_t)STORE_MAGIC[1]);
	store.push_back((uint8_t)STORE_MAGIC[2]);
	store.push_back((uint8_t)STORE_MAGIC[3]);
	bs_put_u32(store, FORMAT_VERSION);
	bs_put_u32(store, (uint32_t)records.size());

	for (const BSDeclarationRecord &record : records) {
		Vector<uint8_t> encoded;
		String error;
		if (!_encode_record(record, encoded, error)) {
			ERR_PRINT("declaration index flush failed to encode '" + record.path + "': " + error);
			return Error::ERR_INVALID_DATA;
		}
		const uint64_t checksum = compute_record_checksum(encoded);
		for (int i = 0; i < encoded.size(); i++) {
			store.push_back(encoded[i]);
		}
		bs_put_u64(store, checksum);
	}
	bs_put_u64(store, compute_file_checksum(store));

	if (p_fault == WriteFault::BEFORE_WRITE) {
		ERR_PRINT("declaration index flush failed before write (simulated)");
		return Error::ERR_FILE_CANT_WRITE;
	}

	const String directory = p_store_path.get_base_dir();
	if (!DirAccess::dir_exists_absolute(directory)) {
		const Error make_error = DirAccess::make_dir_recursive_absolute(directory);
		if (make_error != Error::OK) {
			ERR_PRINT("declaration index flush could not create '" + directory + "'");
			return make_error;
		}
	}

	static std::atomic<uint64_t> flush_counter{ 0 };
	const String temp_path = p_store_path + String(".") +
			String::num_uint64(bs_hash_bytes(store.ptr(), (uint64_t)store.size(), BS_INDEX_FILE_CHECKSUM_BASIS), 16) +
			String(".") + String::num_uint64(flush_counter.fetch_add(1)) + String(".tmp");

	const Ref<FileAccess> file = FileAccess::open(temp_path, FileAccess::WRITE);
	if (file.is_null()) {
		ERR_PRINT("declaration index flush cannot open '" + temp_path + "'");
		return Error::ERR_FILE_CANT_WRITE;
	}

	PackedByteArray store_bytes;
	store_bytes.resize(store.size());
	if (store.size() > 0) {
		memcpy(store_bytes.ptrw(), store.ptr(), store.size());
	}
	const bool buffered = file->store_buffer(store_bytes);
	file->flush();
	const Error write_error = file->get_error();
	file->close();
	if (!buffered || (write_error != Error::OK && write_error != Error::ERR_FILE_EOF)) {
		discard_temp(temp_path);
		ERR_PRINT("declaration index flush write failed for '" + temp_path + "'");
		return Error::ERR_FILE_CANT_WRITE;
	}

	if (p_fault == WriteFault::TRUNCATE_TEMP_AFTER_WRITE) {
		truncate_for_test(temp_path, store_bytes.size() / 2);
	}

	const PackedByteArray written_back = FileAccess::get_file_as_bytes(temp_path);
	if (FileAccess::get_open_error() != Error::OK || written_back != store_bytes) {
		discard_temp(temp_path);
		ERR_PRINT("declaration index flush read-back failed for '" + temp_path + "'");
		return Error::ERR_FILE_CANT_WRITE;
	}

	if (p_fault == WriteFault::AFTER_WRITE_BEFORE_RENAME) {
		ERR_PRINT("declaration index flush failed between write and rename (simulated)");
		return Error::ERR_FILE_CANT_WRITE;
	}

	const Error rename_error = DirAccess::rename_absolute(temp_path, p_store_path);
	if (rename_error != Error::OK) {
		discard_temp(temp_path);
		ERR_PRINT("declaration index flush rename failed for '" + temp_path + "'");
		return rename_error;
	}
	return Error::OK;
}

} // namespace barista_script
