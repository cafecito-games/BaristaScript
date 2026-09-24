/**************************************************************************/
/*  corpus_helpers.cpp                                                    */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "corpus_helpers.h"

#include "bs_corpus_sentinels.h"
#include "native_corpus_arguments.h"
#include "storage_fixture.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <iostream>

using namespace godot;

namespace barista_script::native_tests {
namespace {

String success_sentinel() {
	return String(BaristaScriptCorpusSentinels::SUCCESS_SENTINEL);
}

struct CasePathComparator {
	bool operator()(const CorpusCase &p_left, const CorpusCase &p_right) const {
		return p_left.path < p_right.path;
	}
};

struct PendingDirectory {
	String path;
	bool ignored = false;
};

/**
 * Godot's JSON parser accepts extensions such as trailing commas and literal control
 * characters. A permissive parse is not manifest evidence, so the grammar and the decoded
 * keys are validated before any deserialized value is used.
 */
class StrictJsonValidator {
	String source;
	int position = 0;
	String problem;

	void skip_space() {
		while (position < source.length()) {
			const char32_t code = source[position];
			if (code != ' ' && code != '\t' && code != '\r' && code != '\n') {
				return;
			}
			position++;
		}
	}

	bool take(const String &p_token) {
		skip_space();
		if (source.substr(position, p_token.length()) != p_token) {
			return false;
		}
		position += p_token.length();
		return true;
	}

	bool parse_string() {
		if (!take("\"")) {
			return false;
		}
		while (position < source.length()) {
			const char32_t code = source.unicode_at(position);
			position++;
			if (code == '"') {
				return true;
			}
			if (code < 32) {
				return false;
			}
			if (code != '\\') {
				continue;
			}
			if (position == source.length()) {
				return false;
			}
			const char32_t escape = source[position];
			position++;
			if (escape == 'u') {
				for (int digit = 0; digit < 4; digit++) {
					if (position == source.length() || !String("0123456789abcdefABCDEF").contains(source.substr(position, 1))) {
						return false;
					}
					position++;
				}
			} else if (!String("\"\\/bfnrt").contains(String::chr(escape))) {
				return false;
			}
		}
		return false;
	}

	bool digits() {
		const int start = position;
		while (position < source.length() && source[position] >= '0' && source[position] <= '9') {
			position++;
		}
		return position > start;
	}

	bool parse_number() {
		const int start = position;
		if (position < source.length() && source[position] == '-') {
			position++;
		}
		if (position == source.length()) {
			position = start;
			return false;
		}
		if (source[position] == '0') {
			position++;
		} else if (source[position] >= '1' && source[position] <= '9') {
			digits();
		} else {
			position = start;
			return false;
		}
		if (position + 1 < source.length() && source[position] == '.' && source[position + 1] >= '0' && source[position + 1] <= '9') {
			position++;
			digits();
		}
		const int before_exponent = position;
		if (position < source.length() && (source[position] == 'e' || source[position] == 'E')) {
			position++;
			if (position < source.length() && (source[position] == '+' || source[position] == '-')) {
				position++;
			}
			if (!digits()) {
				position = before_exponent;
			}
		}
		return true;
	}

	bool parse_array(int p_depth) {
		position++;
		if (take("]")) {
			return true;
		}
		while (parse_value(p_depth + 1)) {
			if (take("]")) {
				return true;
			}
			if (!take(",")) {
				return false;
			}
		}
		return false;
	}

	bool parse_object(int p_depth) {
		position++;
		if (take("}")) {
			return true;
		}
		Dictionary keys;
		while (true) {
			skip_space();
			int start = position;
			if (!parse_string()) {
				return false;
			}
			const Variant decoded = JSON::parse_string(source.substr(start, position - start));
			if (decoded.get_type() != Variant::STRING) {
				return false;
			}
			const String key = decoded;
			if (keys.has(key)) {
				problem = "duplicate JSON key " + key;
				return false;
			}
			keys[key] = true;
			if (!take(":")) {
				return false;
			}
			skip_space();
			start = position;
			// The object's own depth is reused here, matching the GDScript validator.
			if (!parse_value(p_depth)) {
				return false;
			}
			if (key == "schema_version" && source.substr(start, position - start) != "1") {
				problem = "schema_version must be integer 1";
				return false;
			}
			if (take("}")) {
				return true;
			}
			if (!take(",")) {
				return false;
			}
		}
	}

	bool parse_value(int p_depth) {
		skip_space();
		if (position == source.length() || p_depth > 256) {
			return false;
		}
		const char32_t code = source[position];
		if (code == '"') {
			return parse_string();
		}
		if (code == '{') {
			return parse_object(p_depth + 1);
		}
		if (code == '[') {
			return parse_array(p_depth);
		}
		for (const char *literal : { "true", "false", "null" }) {
			if (take(literal)) {
				return true;
			}
		}
		return parse_number();
	}

public:
	String validate(const String &p_text) {
		source = p_text;
		position = 0;
		problem = String();
		if (!parse_value(0)) {
			return problem.is_empty() ? vformat("invalid strict JSON at character %d", position) : problem;
		}
		skip_space();
		return position == source.length() ? String() : vformat("trailing JSON content at character %d", position);
	}
};

CorpusOutcome failure_outcome(const CorpusCase &p_case, int p_reason, const String &p_message, const String &p_actual = String()) {
	CorpusOutcome outcome;
	outcome.passed = false;
	outcome.reason = p_reason;
	outcome.path = p_case.path;
	outcome.expectation_path = p_case.expectation_path;
	outcome.message = p_message;
	outcome.actual = p_actual;
	// The expectation is deliberately re-read and lossily decoded here, exactly as the
	// GDScript emitter does, so an invalid expectation still reports whatever text it holds.
	outcome.expected = FileAccess::get_file_as_string(p_case.expectation_path).trim_suffix("\n");
	return outcome;
}

Dictionary fixture_index_dictionary(const CorpusResult &p_result) {
	Dictionary index;
	if (!p_result.fixture_index_reported) {
		return index;
	}
	index["sources"] = p_result.fixture_sources;
	index["heads"] = p_result.fixture_heads;
	index["annotation_providers"] = p_result.fixture_annotation_providers;
	index["conformance_providers"] = p_result.fixture_conformance_providers;
	return index;
}

} //namespace

CorpusDiscovery discover_corpus(const String &p_root) {
	CorpusDiscovery discovery;
	Vector<PendingDirectory> pending;
	pending.push_back({ p_root, false });
	while (!pending.is_empty()) {
		const PendingDirectory frame = pending[pending.size() - 1];
		pending.remove_at(pending.size() - 1);
		const String directory_path = frame.path;
		bool ignored = frame.ignored;

		const Ref<DirAccess> directory = DirAccess::open(directory_path);
		if (directory.is_null()) {
			discovery.unreadable_directories.push_back(directory_path);
			continue;
		}
		directory->list_dir_begin();
		PackedStringArray names;
		String entry = directory->get_next();
		while (!entry.is_empty()) {
			names.push_back(entry);
			entry = directory->get_next();
		}
		directory->list_dir_end();
		names.sort();
		// Directory listing hides dot-prefixed entries, so the marker is probed by path
		// rather than looked up in the listing.
		if (!ignored && FileAccess::file_exists(directory_path + String("/") + CORPUS_IGNORE_MARKER)) {
			ignored = true;
		}
		if (directory->is_link(CORPUS_IGNORE_MARKER)) {
			discovery.discovery_errors.push_back(symlink_error(directory_path + String("/") + CORPUS_IGNORE_MARKER));
		}

		PackedStringArray subdirectories;
		PackedStringArray source_names;
		PackedStringArray expectation_names;
		for (int i = 0; i < names.size(); i++) {
			const String name = names[i];
			if (name == "." || name == "..") {
				continue;
			}
			const String entry_path = directory_path + String("/") + name;
			if (directory->is_link(entry_path)) {
				discovery.discovery_errors.push_back(symlink_error(entry_path));
				continue;
			}
			if (DirAccess::dir_exists_absolute(entry_path)) {
				subdirectories.push_back(entry_path);
			} else if (name.ends_with(CORPUS_CASE_EXTENSION)) {
				source_names.push_back(name);
			} else if (name.ends_with(CORPUS_EXPECTATION_EXTENSION)) {
				expectation_names.push_back(name);
			}
		}

		for (int i = 0; i < source_names.size(); i++) {
			const String name = source_names[i];
			if (name.ends_with(CORPUS_HELPER_SUFFIX) || ignored) {
				discovery.skipped_count++;
				continue;
			}
			CorpusCase discovered;
			discovered.path = directory_path + String("/") + name;
			discovered.expectation_path = directory_path + String("/") + name.get_basename() + CORPUS_EXPECTATION_EXTENSION;
			discovery.cases.push_back(discovered);
		}

		for (int i = 0; i < expectation_names.size(); i++) {
			if (ignored) {
				continue;
			}
			const String name = expectation_names[i];
			const String paired_source = name.trim_suffix(CORPUS_EXPECTATION_EXTENSION) + CORPUS_CASE_EXTENSION;
			if (!source_names.has(paired_source)) {
				discovery.orphaned_expectations.push_back(directory_path + String("/") + name);
			}
		}

		for (int i = 0; i < subdirectories.size(); i++) {
			pending.push_back({ subdirectories[i], ignored });
		}
	}
	discovery.cases.sort_custom<CasePathComparator>();
	discovery.orphaned_expectations.sort();
	discovery.unreadable_directories.sort();
	// The traversal stack is LIFO over sorted subdirectories, so without this the joined
	// complaint would list directories in reverse path order.
	discovery.discovery_errors.sort();
	return discovery;
}

CorpusExpectationGate check_expectation_bytes(const PackedByteArray &p_bytes) {
	CorpusExpectationGate gate;
	const String text = p_bytes.get_string_from_utf8();
	if (text.to_utf8_buffer() != p_bytes) {
		return gate;
	}
	for (int i = 0; i < p_bytes.size(); i++) {
		if (p_bytes[i] == 0) {
			return gate;
		}
	}
	if (!text.ends_with("\n") || text == "\n" || text.ends_with("\n\n") || text.contains("\r")) {
		return gate;
	}
	gate.valid = true;
	gate.expected = text.trim_suffix("\n");
	return gate;
}

String escape_mismatch_value(const String &p_value) {
	String escaped;
	for (int index = 0; index < p_value.length(); index++) {
		const char32_t code = p_value.unicode_at(index);
		switch (code) {
			case 0x5C:
				escaped += "\\\\";
				break;
			case 0x22:
				escaped += "\\\"";
				break;
			case 0x0D:
				escaped += "\\r";
				break;
			case 0x0A:
				escaped += "\\n";
				break;
			case 0x09:
				escaped += "\\t";
				break;
			default:
				if (code < 0x20 || code == 0x7F) {
					escaped += vformat("\\x%02x", int(code));
				} else {
					escaped += String::chr(code);
				}
				break;
		}
	}
	return escaped;
}

/**
 * The consistency complaint about a runtime transcript, or the empty string when it is sound.
 *
 * A transcript's first line is its status token, and the token has to agree with the outcome the
 * evaluator reported: a mismatch means the two halves of the result were assembled from different
 * runs, which would silently adjudicate one case against another's status.
 */
static String runtime_result_error(const CorpusResult &p_result) {
	if (p_result.infrastructure_error) {
		// An infrastructure error carries a complaint rather than a transcript, and the caller
		// reports it without comparing anything, so no status line is required of it.
		return p_result.ok ? "malformed runtime result: inconsistent stage/outcome" : String();
	}
	if (!p_result.analysis_ran) {
		return "malformed runtime result: inconsistent stage/outcome";
	}
	const String status = p_result.output.get_slice("\n", 0);
	if (status != String(RUNTIME_STATUS_OK) && status != String(RUNTIME_STATUS_RUNTIME_ERROR) &&
			status != String(RUNTIME_STATUS_ANALYZER_ERROR)) {
		return "malformed runtime result: unrecognized status token";
	}
	if (p_result.ok != (status == String(RUNTIME_STATUS_OK))) {
		return "malformed runtime result: contradictory success block";
	}
	if (p_result.output.contains("\r") || p_result.output.ends_with("\n")) {
		return "malformed runtime result: invalid block terminator";
	}
	return String();
}

String corpus_result_error(const CorpusResult &p_result, const String &p_stage) {
	if (p_result.output.is_empty()) {
		return "malformed frontend result: nonempty output required";
	}
	if (p_stage == "runtime") {
		return runtime_result_error(p_result);
	}
	if ((p_stage == "parser" && p_result.analysis_ran) || (p_result.infrastructure_error && p_result.ok) ||
			(p_stage == "analyzer" && p_result.ok && !p_result.analysis_ran)) {
		return "malformed frontend result: inconsistent stage/outcome";
	}
	if ((p_result.output == success_sentinel() && !p_result.ok) ||
			(p_stage == "parser" && p_result.ok && p_result.output != success_sentinel())) {
		return "malformed frontend result: contradictory success block";
	}
	if (p_result.output.contains("\r") || p_result.output.ends_with("\n")) {
		return "malformed frontend result: invalid block terminator";
	}
	return String();
}

CorpusOutcome compare_corpus_result(const CorpusCase &p_case, const String &p_expected, const CorpusResult &p_result) {
	const String shape_error = corpus_result_error(p_result, p_case.stage);
	if (!shape_error.is_empty()) {
		return failure_outcome(p_case, CORPUS_INVALID_RESULT, shape_error);
	}
	if (p_result.infrastructure_error) {
		return failure_outcome(p_case, CORPUS_INVALID_RESULT, p_result.output);
	}
	if (p_expected != p_result.output) {
		CorpusOutcome outcome = failure_outcome(p_case, CORPUS_OUTPUT_MISMATCH,
				vformat("output mismatch\nexpected: \"%s\"\nactual:   \"%s\"",
						escape_mismatch_value(p_expected), escape_mismatch_value(p_result.output)),
				p_result.output);
		outcome.fixture_index = fixture_index_dictionary(p_result);
		outcome.analysis_ran = p_result.analysis_ran;
		return outcome;
	}
	CorpusOutcome outcome;
	outcome.passed = true;
	outcome.path = p_case.path;
	outcome.expected = p_expected;
	outcome.actual = p_result.output;
	outcome.analysis_ran = p_result.analysis_ran;
	outcome.fixture_index = fixture_index_dictionary(p_result);
	return outcome;
}

CorpusOutcome run_corpus_case(const CorpusCase &p_case, const PackedStringArray &p_fixture_paths) {
	if (!FileAccess::file_exists(p_case.expectation_path)) {
		return failure_outcome(p_case, CORPUS_MISSING_EXPECTATION, "missing expectation " + p_case.expectation_path);
	}
	const Ref<FileAccess> expectation_file = FileAccess::open(p_case.expectation_path, FileAccess::READ);
	if (expectation_file.is_null()) {
		return failure_outcome(p_case, CORPUS_INVALID_EXPECTATION,
				vformat("expectation %s is unreadable (error %d)", p_case.expectation_path, int(FileAccess::get_open_error())));
	}
	const PackedByteArray expectation_bytes = expectation_file->get_buffer(expectation_file->get_length());
	const String expectation_text = expectation_bytes.get_string_from_utf8();
	if (expectation_text.to_utf8_buffer() != expectation_bytes) {
		return failure_outcome(p_case, CORPUS_INVALID_EXPECTATION, "expectation " + p_case.expectation_path + " is not valid UTF-8");
	}
	const CorpusExpectationGate gate = check_expectation_bytes(expectation_bytes);
	if (!gate.valid) {
		return failure_outcome(p_case, CORPUS_INVALID_EXPECTATION,
				"expectation " + p_case.expectation_path + " requires a nonempty block with exactly one final LF, without CR or NUL");
	}

	const Ref<FileAccess> source_file = FileAccess::open(p_case.path, FileAccess::READ);
	if (source_file.is_null()) {
		return failure_outcome(p_case, CORPUS_UNREADABLE_SOURCE,
				vformat("source %s is unreadable (error %d)", p_case.path, int(FileAccess::get_open_error())));
	}
	const PackedByteArray source_bytes = source_file->get_buffer(source_file->get_length());
	// The stage the manifest declares is what chooses the evaluator, so a case cannot be
	// adjudicated at a stage it was not imported for.
	const CorpusResult result = p_case.stage == "runtime"
			? evaluate_runtime_case(source_bytes, p_case.path, p_case.stage, p_fixture_paths)
			: evaluate_corpus_case(source_bytes, p_case.path, p_case.stage, p_fixture_paths);
	return compare_corpus_result(p_case, gate.expected, result);
}

Dictionary corpus_case_result_dictionary(const CorpusOutcome &p_outcome) {
	Dictionary result;
	result["passed"] = p_outcome.passed;
	if (p_outcome.passed) {
		result["path"] = p_outcome.path;
		result["expected"] = p_outcome.expected;
		result["actual"] = p_outcome.actual;
		result["analysis_ran"] = p_outcome.analysis_ran;
		result["fixture_index"] = p_outcome.fixture_index;
		return result;
	}
	result["reason"] = p_outcome.reason;
	result["path"] = p_outcome.path;
	result["expectation_path"] = p_outcome.expectation_path;
	result["message"] = p_outcome.message;
	result["actual"] = p_outcome.actual;
	result["expected"] = p_outcome.expected;
	if (p_outcome.reason == CORPUS_OUTPUT_MISMATCH) {
		result["fixture_index"] = p_outcome.fixture_index;
		result["analysis_ran"] = p_outcome.analysis_ran;
	}
	return result;
}

String symlink_error(const String &p_path) {
	return "unsupported corpus symlink: " + p_path;
}

bool valid_case_relative(const String &p_path) {
	if (!p_path.ends_with(CORPUS_CASE_EXTENSION) || p_path.ends_with(CORPUS_HELPER_SUFFIX) ||
			p_path.contains("\\") || p_path.begins_with("/")) {
		return false;
	}
	const PackedStringArray parts = p_path.split("/");
	for (int i = 0; i < parts.size(); i++) {
		if (parts[i].is_empty() || parts[i] == "." || parts[i] == "..") {
			return false;
		}
	}
	return true;
}

String validate_strict_json(const String &p_text) {
	StrictJsonValidator validator;
	return validator.validate(p_text);
}

CorpusJsonDocument read_unique_json(const String &p_path) {
	CorpusJsonDocument read;
	const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		read.error = "missing/unreadable JSON: " + p_path;
		return read;
	}
	const PackedByteArray bytes = file->get_buffer(file->get_length());
	const String text = bytes.get_string_from_utf8();
	bool has_nul = false;
	for (int i = 0; i < bytes.size(); i++) {
		has_nul = has_nul || bytes[i] == 0;
	}
	if (text.to_utf8_buffer() != bytes || has_nul) {
		read.error = "invalid UTF-8 JSON: " + p_path;
		return read;
	}
	const Variant parsed = JSON::parse_string(text);
	if (parsed.get_type() != Variant::DICTIONARY) {
		read.error = "invalid JSON object: " + p_path;
		return read;
	}
	// Establish that strings decode, then reject the syntax extensions before accepting data.
	const String complaint = validate_strict_json(text);
	if (!complaint.is_empty()) {
		read.error = complaint + String(": ") + p_path;
		return read;
	}
	read.document = parsed;
	return read;
}

CorpusStageManifest validate_stage_manifest(const Dictionary &p_stages, const String &p_root, const String &p_revision) {
	CorpusStageManifest manifest;
	const Variant schema_version = p_stages.get("schema_version", Variant());
	// FLOAT is accepted because Godot's JSON decoder produces it for the integer 1: requiring
	// Variant::INT here rejects every real manifest while every hand-built Dictionary in a
	// test still passes. The integer *spelling* is enforced one layer up, on the JSON text,
	// by StrictJsonValidator; this layer only rejects a value that is not numerically 1.
	const bool numeric_schema = schema_version.get_type() == Variant::INT || schema_version.get_type() == Variant::FLOAT;
	if (p_stages.keys().size() != 3 || !numeric_schema || double(schema_version) != 1.0 ||
			p_stages.get("foundry_revision", Variant()) != Variant(p_revision) ||
			p_stages.get("cases", Variant()).get_type() != Variant::DICTIONARY) {
		manifest.error = "invalid/stale case stage manifest: " + p_root;
		return manifest;
	}
	const CorpusDiscovery discovery = discover_corpus(p_root);
	if (!discovery.discovery_errors.is_empty()) {
		manifest.error = String("; ").join(discovery.discovery_errors);
		return manifest;
	}
	if (!discovery.unreadable_directories.is_empty()) {
		manifest.error = "unreadable imported directory: " + p_root;
		return manifest;
	}
	Dictionary remaining = Dictionary(p_stages["cases"]).duplicate();
	const Array relative_paths = remaining.keys();
	for (int i = 0; i < relative_paths.size(); i++) {
		const Variant key = relative_paths[i];
		const Variant stage = remaining[key];
		if (key.get_type() != Variant::STRING || !valid_case_relative(String(key)) ||
				(stage != Variant("parser") && stage != Variant("analyzer") && stage != Variant("runtime"))) {
			manifest.error = vformat("invalid case stage entry: %s/%s", p_root, String(key));
			return manifest;
		}
	}
	Dictionary owned;
	for (int i = 0; i < discovery.cases.size(); i++) {
		const String relative = discovery.cases[i].path.trim_prefix(p_root + String("/"));
		if (!remaining.has(relative)) {
			manifest.error = "missing case stage entry: " + discovery.cases[i].path;
			return manifest;
		}
		owned[discovery.cases[i].path] = remaining[relative];
		remaining.erase(relative);
	}
	if (!remaining.is_empty()) {
		manifest.error = "extra/helper case stage entries: " + p_root;
		return manifest;
	}
	manifest.stages = owned;
	return manifest;
}

PackedStringArray fixture_source_paths(const String &p_root) {
	PackedStringArray paths;
	const Ref<DirAccess> directory = DirAccess::open(p_root);
	if (directory.is_null()) {
		return paths;
	}
	const PackedStringArray files = directory->get_files();
	for (int i = 0; i < files.size(); i++) {
		if (files[i].ends_with(CORPUS_CASE_EXTENSION)) {
			paths.push_back(p_root.path_join(files[i]));
		}
	}
	const PackedStringArray children = directory->get_directories();
	for (int i = 0; i < children.size(); i++) {
		paths.append_array(fixture_source_paths(p_root.path_join(children[i])));
	}
	paths.sort();
	return paths;
}

PackedStringArray analyzer_fixture_paths() {
	PackedStringArray paths = fixture_source_paths(CORPUS_ANALYZER_ROOT);
	paths.append_array(fixture_source_paths("res://tests/corpus/parser"));
	paths.append_array(fixture_source_paths("res://tests/corpus_support/parser"));
	paths.sort();
	return paths;
}

bool corpus_path_under(const String &p_path, const String &p_root) {
	return p_path == p_root || p_path.begins_with(p_root.trim_suffix("/") + String("/"));
}

String normalize_corpus_root(const String &p_root, String *r_error) {
	if (r_error != nullptr) {
		*r_error = String();
	}
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (settings == nullptr) {
		if (r_error != nullptr) {
			*r_error = "missing project settings for corpus root: " + p_root;
		}
		return String();
	}
	String root = p_root.replace("\\", "/");
	if (!root.begins_with("res://") && !root.begins_with("user://")) {
		root = settings->localize_path(root);
	}
	root = root.simplify_path().trim_suffix("/");
	if (!root.begins_with("res://") && !root.begins_with("user://")) {
		if (r_error != nullptr) {
			*r_error = "corpus root is outside the project: " + p_root;
		}
		return String();
	}
	// A symlinked root is refused rather than resolved: every entry inside a discovered tree
	// is refused the same way, and a read-only run has no reason to follow one.
	const Ref<DirAccess> parent = DirAccess::open(root.get_base_dir());
	if (parent.is_valid() && parent->is_link(root)) {
		if (r_error != nullptr) {
			*r_error = symlink_error(root);
		}
		return String();
	}
	if (!DirAccess::dir_exists_absolute(root)) {
		if (r_error != nullptr) {
			*r_error = "corpus root is not a readable directory: " + root;
		}
		return String();
	}
	return root;
}

int select_discovered_case(const CorpusDiscovery &p_discovery, const String &p_root,
		const String &p_relative, String *r_error) {
	if (r_error != nullptr) {
		*r_error = String();
	}
	const String selected_path = p_root.path_join(p_relative);
	int selected = -1;
	int matches = 0;
	for (int i = 0; i < p_discovery.cases.size(); i++) {
		if (p_discovery.cases[i].path == selected_path) {
			selected = i;
			matches++;
		}
	}
	if (matches == 1) {
		return selected;
	}
	if (r_error != nullptr) {
		// A duplicated identity is refused rather than resolved to either match, and must not
		// be reported as an absence: the two are different problems for whoever reads the log.
		*r_error = matches == 0 ? "exact case was not discovered: " + p_relative
								: "exact case is ambiguous: " + p_relative;
	}
	return -1;
}

CorpusEnvironment prepare_corpus_environment(const String &p_root_argument) {
	CorpusEnvironment environment;
	environment.root = normalize_corpus_root(p_root_argument, &environment.error);
	if (!environment.error.is_empty()) {
		return environment;
	}
	environment.discovery = discover_corpus(environment.root);
	if (!environment.discovery.discovery_errors.is_empty()) {
		environment.error = String("; ").join(environment.discovery.discovery_errors);
		return environment;
	}
	if (!environment.discovery.unreadable_directories.is_empty()) {
		environment.error = "corpus directory is unreadable: " + String(environment.discovery.unreadable_directories[0]);
		return environment;
	}
	const CorpusJsonDocument registry = read_unique_json("res://../scripts/corpus_sources.json");
	if (!registry.error.is_empty()) {
		environment.error = registry.error;
		return environment;
	}
	if (registry.document.get("revision", Variant()).get_type() != Variant::STRING) {
		environment.error = "invalid corpus registry revision";
		return environment;
	}
	const CorpusJsonDocument document = read_unique_json(environment.root.path_join("case_stages.json"));
	if (!document.error.is_empty()) {
		environment.error = document.error;
		return environment;
	}
	const CorpusStageManifest manifest = validate_stage_manifest(document.document, environment.root,
			registry.document["revision"]);
	if (!manifest.error.is_empty()) {
		environment.error = manifest.error;
		return environment;
	}
	environment.stages = manifest.stages;
	// Only the analyzer root stages fixtures; see CORPUS_RUNTIME_ROOT for why the runtime root
	// deliberately stages none.
	if (corpus_path_under(environment.root, CORPUS_ANALYZER_ROOT) ||
			corpus_path_under(CORPUS_ANALYZER_ROOT, environment.root)) {
		environment.fixture_paths = analyzer_fixture_paths();
	}
	return environment;
}

CorpusModeReport evaluate_environment_case(const CorpusEnvironment &p_environment, int p_index) {
	CorpusModeReport report;
	report.selected = true;
	CorpusCase selected_case = p_environment.discovery.cases[p_index];
	report.relative_case = selected_case.path.trim_prefix(p_environment.root + String("/"));
	report.outcome.path = selected_case.path;
	report.outcome.expectation_path = selected_case.expectation_path;
	if (!p_environment.stages.has(selected_case.path)) {
		report.error = "unregistered case has no manifest entry: " + selected_case.path;
		return report;
	}
	selected_case.stage = p_environment.stages[selected_case.path];
	report.outcome = run_corpus_case(selected_case, p_environment.fixture_paths);
	return report;
}

static CorpusModeReport selected_corpus_case_report() {
	CorpusModeReport report;
	report.relative_case = corpus_case();
	if (report.relative_case.is_empty()) {
		return report;
	}
	report.selected = true;
	// A best-effort identity from the moment the selection is known, so a failure raised
	// before discovery still names the case it was asked about.
	report.outcome.path = String(corpus_root()).path_join(report.relative_case);
	const CorpusEnvironment environment = prepare_corpus_environment(corpus_root());
	if (!environment.error.is_empty()) {
		report.error = environment.error;
		return report;
	}
	report.outcome.path = environment.root.path_join(report.relative_case);
	if (!valid_case_relative(report.relative_case)) {
		report.error = "invalid exact case: " + report.relative_case;
		return report;
	}
	String error;
	const int selected = select_discovered_case(environment.discovery, environment.root, report.relative_case, &error);
	if (selected < 0) {
		report.error = error;
		return report;
	}
	return evaluate_environment_case(environment, selected);
}

CorpusShardSlice corpus_shard_slice(int p_total, int p_shard, int p_shard_count) {
	CorpusShardSlice slice;
	if (p_shard_count < 1 || p_shard < 0 || p_shard >= p_shard_count || p_total < 0) {
		return slice;
	}
	const int base = p_total / p_shard_count;
	const int remainder = p_total % p_shard_count;
	slice.start = p_shard * base + (p_shard < remainder ? p_shard : remainder);
	slice.end = slice.start + base + (p_shard < remainder ? 1 : 0);
	return slice;
}

CorpusWholeRunReport run_whole_corpus() {
	CorpusWholeRunReport run;
	const CorpusEnvironment environment = prepare_corpus_environment(corpus_root());
	if (!environment.error.is_empty()) {
		run.error = environment.error;
		return run;
	}
	const bool reversed = corpus_order() == String(CORPUS_ORDER_REVERSE);
	run.total = environment.discovery.cases.size();
	const CorpusShardSlice slice = corpus_shard_slice(run.total, corpus_shard(), corpus_shard_count());
	run.start = slice.start;
	run.planned = slice.end - slice.start;
	Dictionary plan;
	plan["planned"] = run.planned;
	plan["order"] = reversed ? CORPUS_ORDER_REVERSE : CORPUS_ORDER_ASCENDING;
	plan["root"] = environment.root;
	plan["shard"] = corpus_shard();
	plan["shards"] = corpus_shard_count();
	plan["total"] = run.total;
	plan["start"] = run.start;
	std::cout << "BS_CORPUS_PLAN " << JSON::stringify(plan).utf8().get_data() << std::endl;
	for (int offset = slice.start; offset < slice.end; offset++) {
		// The order is applied to the whole population and the slice is cut from the result,
		// so reversing moves cases between shards as well as within one.
		const int index = reversed ? run.total - 1 - offset : offset;
		{
			// One scope per case, exactly as a single-case process gets one for its only case:
			// the ambient user:// subtree, declaration index and cache are rebuilt from nothing
			// before each evaluation rather than inherited from the case before it.
			StorageFixture fixture;
			CorpusModeReport report = evaluate_environment_case(environment, index);
			describe_infrastructure_failure(report);
			emit_corpus_guards(report);
		}
		run.completed++;
	}
	Dictionary completion;
	completion["planned"] = run.planned;
	completion["completed"] = run.completed;
	completion["shard"] = corpus_shard();
	completion["shards"] = corpus_shard_count();
	std::cout << "BS_CORPUS_COMPLETE " << JSON::stringify(completion).utf8().get_data() << std::endl;
	return run;
}

void describe_infrastructure_failure(CorpusModeReport &p_report) {
	if (!p_report.selected || p_report.error.is_empty()) {
		return;
	}
	// This is the one failure class whose payload would otherwise be blank. It carries the
	// same INVALID_RESULT reason a malformed evaluation does, plus the complaint that already
	// reached stdout, so the machine-readable record is self-describing too.
	p_report.outcome.passed = false;
	p_report.outcome.reason = CORPUS_INVALID_RESULT;
	p_report.outcome.message = p_report.error;
	p_report.outcome.expected = FileAccess::get_file_as_string(p_report.outcome.expectation_path).trim_suffix("\n");
}

CorpusModeReport run_selected_corpus_case() {
	CorpusModeReport report = selected_corpus_case_report();
	describe_infrastructure_failure(report);
	return report;
}

void emit_corpus_guards(const CorpusModeReport &p_report) {
	std::cout << "BS_CASE_RESULT " << JSON::stringify(corpus_case_result_dictionary(p_report.outcome)).utf8().get_data() << std::endl;
	std::cout << "BS_CASE_RAN " << p_report.relative_case.utf8().get_data() << std::endl;
}

} //namespace barista_script::native_tests
