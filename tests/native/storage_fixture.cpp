/**************************************************************************/
/*  storage_fixture.cpp                                                   */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "storage_fixture.h"

#include <cstring>
#include <filesystem>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>

namespace barista_script::native_tests {

Vector<uint8_t> bytes(const String &text) {
	const CharString utf8 = text.utf8();
	Vector<uint8_t> result;
	result.resize(utf8.length());
	if (!result.is_empty()) {
		std::memcpy(result.ptrw(), utf8.get_data(), result.size());
	}
	return result;
}

Vector<uint8_t> read_bytes(const String &path) {
	const PackedByteArray input = FileAccess::get_file_as_bytes(path);
	Vector<uint8_t> result;
	result.resize(input.size());
	if (!result.is_empty()) {
		std::memcpy(result.ptrw(), input.ptr(), result.size());
	}
	return result;
}

bool write_bytes(const String &path, const Vector<uint8_t> &data) {
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	CHECK_MESSAGE(file.is_valid(), "could not open test output: ", path.utf8().get_data());
	if (file.is_null()) {
		return false;
	}
	PackedByteArray buffer;
	buffer.resize(data.size());
	if (!data.is_empty()) {
		std::memcpy(buffer.ptrw(), data.ptr(), data.size());
	}
	file->store_buffer(buffer);
	file->close();
	const bool matches = read_bytes(path) == data;
	CHECK_MESSAGE(matches, "test output did not round-trip: ", path.utf8().get_data());
	return matches;
}

Vector<String> temporary_files(const String &store) {
	Vector<String> result;
	for (const String &name : DirAccess::get_files_at(store.get_base_dir())) {
		if (name.begins_with(store.get_file()) && name.ends_with(".tmp")) {
			result.push_back(store.get_base_dir().path_join(name));
		}
	}
	result.sort();
	return result;
}

int count_tmp_under(const String &root) {
	int count = 0;
	for (const String &name : DirAccess::get_files_at(root)) {
		if (name.ends_with(".tmp")) {
			++count;
		}
	}
	return count;
}

void append_integer(Vector<uint8_t> &destination, uint64_t value, int width) {
	for (int i = 0; i < width; ++i) {
		destination.push_back(uint8_t(value >> (i * 8)));
	}
}

StorageFixture::StorageFixture() :
		index_scope(BaristaScriptLanguage::get_singleton()->get_declaration_index()),
		previous_bootstrap_root(BSAnalyzer::get_bootstrap_allowed_dependency_root()) {
	static uint64_t serial = 0; // Plain metadata; no engine values in static initializers.
	root = "user://native-storage-" + String::num_uint64(OS::get_singleton()->get_process_id()) + String("-") + String::num_uint64(++serial);
	CHECK(DirAccess::make_dir_recursive_absolute(root) == OK);
	BSCache::clear();
	BSCache::clear_source_overrides();
	index().clear();
	BSAnalyzer::set_bootstrap_allowed_dependency_root("");
}

#if defined(WINDOWS_ENABLED)
static std::filesystem::path windows_extended_filesystem_path(const String &absolute_path) {
	// LongPathsEnabled may be off (G1 negative profile). Extended \\?\ / \\?\UNC\ paths keep
	// remove_all working for supplementary-Unicode and ASCII long fixtures without the opt-in.
	String normalized = absolute_path.replace("/", "\\");
	String extended;
	if (normalized.begins_with("\\\\?\\")) {
		extended = normalized;
	} else if (normalized.begins_with("\\\\")) {
		extended = String("\\\\?\\UNC\\") + normalized.substr(2);
	} else {
		extended = String("\\\\?\\") + normalized;
	}
	const Char16String utf16 = extended.utf16();
	return std::filesystem::path(reinterpret_cast<const wchar_t *>(utf16.get_data()));
}
#endif

StorageFixture::~StorageFixture() {
	BSAnalyzer::set_bootstrap_allowed_dependency_root(previous_bootstrap_root);
	std::error_code error;
	const String absolute = ProjectSettings::get_singleton()->globalize_path(root);
#if defined(WINDOWS_ENABLED)
	std::filesystem::remove_all(windows_extended_filesystem_path(absolute), error);
#else
	std::filesystem::remove_all(std::filesystem::u8path(absolute.utf8().get_data()), error);
#endif
	CHECK_MESSAGE(!error, "could not remove native scratch directory: ", error.message());
}

bool verify_case_isolation(void (*scenario)()) {
	StorageFixture ambient;
	const String path = "res://tests/cache_fixtures/script_a.barista";
	const String owner = "res://tests/cache_fixtures/script_b.barista";
	const String override_source = FileAccess::get_file_as_string(path) + String("\n# ambient override\n");
	BSCache::set_source_override(path, override_source);
	BSCache::record_dependency(path, owner);
	Error parse_error = OK;
	const Ref<BSParserRef> parser = BSCache::get_parser(path, BSParserRef::PARSED, parse_error);
	CHECK(parse_error == OK);
	CHECK(parser.is_valid());
	if (parse_error != OK || parser.is_null()) {
		return false;
	}
	BSDeclarationRecord record;
	record.path = "res://native_ambient.barista";
	record.qualified_name = "Ambient";
	record.kind = BSDeclarationKind::CLASS;
	record.base_type = "RefCounted";
	record.source_digest = 42;
	record.global_annotations.push_back("AmbientMark");
	CHECK(ambient.index().commit_record(ambient.index().claim_refresh(record.path), record));
	const String store = ambient.path("ambient.bsi");
	CHECK(ambient.index().flush(store) == OK);
	const auto before = read_bytes(store);
	BSAnalyzer::set_bootstrap_allowed_dependency_root("res://ambient/");
	const String bootstrap = BSAnalyzer::get_bootstrap_allowed_dependency_root();
	auto *cache = BSCache::get_singleton();
	auto *index = &ambient.index();
	const auto directories = DirAccess::get_directories_at("user://");
	scenario();
	CHECK(BSCache::get_singleton() == cache);
	CHECK(&ambient.index() == index);
	CHECK(BSCache::has_source_override(path));
	CHECK(BSCache::get_source_code(path) == override_source);
	const auto inverse = BSCache::get_inverse_dependencies(path);
	CHECK(inverse.size() == 1);
	CHECK(inverse.has(owner));
	CHECK(BSCache::has_parser(path));
	Error after_error = OK;
	const Ref<BSParserRef> after_parser = BSCache::get_parser(path, BSParserRef::PARSED, after_error);
	CHECK(after_error == OK);
	CHECK(after_parser == parser);
	CHECK(BSAnalyzer::get_bootstrap_allowed_dependency_root() == bootstrap);
	CHECK(ambient.index().flush(store) == OK);
	const bool same_index = read_bytes(store) == before;
	CHECK(same_index);
	CHECK(ambient.index().get_annotation_declaring_paths("AmbientMark").size() == 1);
	const bool same_directories = DirAccess::get_directories_at("user://") == directories;
	CHECK_MESSAGE(same_directories, "scenario leaked a native scratch directory");
	return BSCache::get_singleton() == cache && &ambient.index() == index && BSCache::get_source_code(path) == override_source && inverse.size() == 1 && inverse.has(owner) && after_parser == parser && BSAnalyzer::get_bootstrap_allowed_dependency_root() == bootstrap && same_index && same_directories;
}

} // namespace barista_script::native_tests
