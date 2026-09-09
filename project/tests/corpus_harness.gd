# corpus_harness.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT
extends RefCounted

## Golden-file corpus harness for BaristaScript conformance runs.
##
## A corpus case is a `.barista` source paired with a `.out` expectation whose
## complete block is either the success sentinel or the exact expected diagnostic
## text. Comparison is exact: differences that differ only in trailing whitespace
## or line endings are failures, never normalized away.

const CASE_EXTENSION := ".barista"
const HELPER_SUFFIX := ".notest" + CASE_EXTENSION
const EXPECTATION_EXTENSION := ".out"

## The marker file that opts a directory out of collection.
##
## Deliberately not Foundry's `.fsignore`: the upstream corpus root carries an
## empty `.fsignore` of its own (it opts the parser scripts out of the *runtime*
## suite), so a re-import that copied dotfiles verbatim would land a marker that
## silently skips all 340 cases while the run still exits 0. A name upstream does
## not use cannot be imported by accident.
const IGNORE_MARKER := ".baristaignore"

## Both sentinels come from the compiled extension
## (src/bs_corpus_sentinels.h), which is the one definition the C++ side, this
## harness and the Python importer/CI validator all read. A literal here would be
## a second spelling, and a corpus-wide false pass is what a second spelling
## buys.
static var SUCCESS_SENTINEL: String = BaristaScriptCorpusSentinels.success_sentinel()
static var SUMMARY_PREFIX: String = BaristaScriptCorpusSentinels.summary_prefix()

enum ExitCode {
	PASSED = 0,
	CASES_FAILED = 1,
	HARNESS_ERROR = 2,
}

enum FailureReason {
	MISSING_EXPECTATION,
	ORPHANED_EXPECTATION,
	INVALID_EXPECTATION,
	UNREADABLE_SOURCE,
	OUTPUT_MISMATCH,
	INVALID_RESULT,
}

## Injection is transport-only. Results follow the native raw oracle schema.
var case_evaluator: Callable
var fixture_stages: Dictionary = {}
var fixture_paths: PackedStringArray = []
var frontend_factory: Callable = func(): return ClassDB.instantiate("BaristaScriptAnalyzerProbe")


## Runs every discovered case under `corpus_root` and returns
## `{"exit_code": int, "output": Array[String]}`. The output always ends with
## exactly one machine-readable summary line, so consecutive runs over an
## unchanged tree are byte-identical.
func run(corpus_root: String, allow_empty: bool = false, update_expectations: bool = false, exact_case: String = "") -> Dictionary:
	var identity := _path_identity(corpus_root)
	if identity.has("error"):
		return error_result("BS_ERROR " + identity.error)
	corpus_root = identity.path
	var output: Array[String] = []
	if not DirAccess.dir_exists_absolute(corpus_root):
		output.append("BS_ERROR corpus root is not a readable directory: %s" % corpus_root)
		output.append(_summary_line(0, 0, 0))
		return {"exit_code": ExitCode.HARNESS_ERROR, "output": output}

	var discovery := _discover_corpus(corpus_root)
	var cases: Array = discovery["cases"]
	var orphaned_expectations: Array[String] = discovery["orphaned_expectations"]
	var skipped_count: int = discovery["skipped_count"]
	var unreadable_directories: Array[String] = discovery["unreadable_directories"]

	# A directory that exists but cannot be opened hides an unknown number of
	# cases, so the run cannot report a trustworthy verdict at all: aborting is
	# the only fail-closed answer, and it must outrank --allow-empty.
	if not unreadable_directories.is_empty():
		for unreadable in unreadable_directories:
			output.append("BS_ERROR corpus directory is unreadable: %s" % unreadable)
		output.append(_summary_line(0, 0, 0))
		return {"exit_code": ExitCode.HARNESS_ERROR, "output": output}

	var stage_error := _assign_stages(corpus_root, cases, update_expectations)
	if not stage_error.is_empty():
		return error_result("BS_ERROR " + stage_error)

	if not discovery.get("discovery_errors", []).is_empty():
		return error_result("BS_ERROR " + "; ".join(discovery.discovery_errors))

	if not exact_case.is_empty():
		if not _valid_case_relative(exact_case) or update_expectations:
			return error_result("BS_ERROR invalid exact case or expectation update")
		var selected: Array = cases.filter(func(case): return case.path == corpus_root.path_join(exact_case))
		if selected.size() != 1:
			return error_result("BS_ERROR exact case was not discovered: %s" % exact_case)
		cases = selected

	var failures: Array[Dictionary] = []
	var results: Array[Dictionary] = []
	var passed := 0
	for case_info in cases:
		var outcome := _run_case(case_info)
		results.append(outcome)
		if outcome["passed"]:
			passed += 1
		else:
			failures.append(outcome)
			var message_lines: PackedStringArray = outcome["message"].split("\n")
			output.append("FAIL %s: %s" % [outcome["path"], message_lines[0]])
			for message_index in range(1, message_lines.size()):
				output.append("     %s" % message_lines[message_index])

	for orphan in orphaned_expectations:
		output.append("FAIL orphaned expectation %s: no matching %s case" % [orphan, CASE_EXTENSION])
		failures.append({
			"passed": false,
			"reason": FailureReason.ORPHANED_EXPECTATION,
			"path": orphan,
			"expectation_path": orphan,
			"message": "orphaned expectation: no matching %s case" % CASE_EXTENSION,
		})

	var discovered_nothing := cases.is_empty() and orphaned_expectations.is_empty()
	if cases.is_empty() and not allow_empty:
		output.append(
			"BS_ERROR no cases discovered in %s (pass --allow-empty when the corpus is intentionally empty)"
			% corpus_root
		)

	if update_expectations:
		var refusal := _refusal_reason(failures, discovered_nothing, allow_empty)
		if not refusal.is_empty():
			output.append("BS_ERROR --update-expectations refused: %s" % refusal)
			output.append(_summary_line(passed, cases.size(), skipped_count))
			return {"exit_code": ExitCode.HARNESS_ERROR, "output": output}
		var updated := 0
		for failure in failures:
			if failure["reason"] != FailureReason.OUTPUT_MISMATCH:
				continue
			var write_error := _write_expectation(failure["expectation_path"], failure["actual"])
			if write_error != OK:
				output.append(
					"BS_ERROR --update-expectations could not write %s (error %d)"
					% [failure["expectation_path"], write_error]
				)
				output.append(_summary_line(passed, cases.size(), skipped_count))
				return {"exit_code": ExitCode.HARNESS_ERROR, "output": output}
			updated += 1
		output.append("updated %d expectation(s)" % updated)
		# Every failure was a mismatch and has been rewritten, so the update
		# itself succeeded even though the pre-update run reported failures.
		output.append(_summary_line(passed + updated, cases.size(), skipped_count))
		return {"exit_code": ExitCode.PASSED, "output": output}

	var exit_code := ExitCode.PASSED
	if not failures.is_empty() or (cases.is_empty() and not allow_empty):
		exit_code = ExitCode.CASES_FAILED

	for failure in failures:
		if failure.reason in [FailureReason.UNREADABLE_SOURCE, FailureReason.INVALID_RESULT]:
			exit_code = ExitCode.HARNESS_ERROR
	output.append(_summary_line(passed, cases.size(), skipped_count))
	return {"exit_code": exit_code, "output": output, "results": results}


## Builds a harness-error result for callers that parse arguments themselves,
## so the exit-code vocabulary and summary shape stay defined in one place.
func error_result(message: String) -> Dictionary:
	return {
		"exit_code": ExitCode.HARNESS_ERROR,
		"output": [message, _summary_line(0, 0, 0)],
	}


## The single home of case discovery: `.notest.barista` helpers, ignored
## directories, `.barista`/`.out` pairing, orphan detection, and directories that
## cannot be opened. Everything is sorted by path so results never depend on
## directory iteration order.
func _discover_corpus(corpus_root: String) -> Dictionary:
	var cases: Array[Dictionary] = []
	var orphaned_expectations: Array[String] = []
	var unreadable_directories: Array[String] = []
	var discovery_errors: Array[String] = []
	var skipped_count := 0
	var pending: Array = [[corpus_root, false]]
	while not pending.is_empty():
		var frame: Array = pending.pop_back()
		var directory_path: String = frame[0]
		var ignored: bool = frame[1]

		var directory := DirAccess.open(directory_path)
		if directory == null:
			unreadable_directories.append(directory_path)
			continue
		directory.list_dir_begin()
		var names: Array[String] = []
		var entry := directory.get_next()
		while not entry.is_empty():
			names.append(entry)
			entry = directory.get_next()
		directory.list_dir_end()
		names.sort()
		# DirAccess iteration hides dot-prefixed entries, so the marker is
		# checked by path rather than looked up in the listing.
		if not ignored and FileAccess.file_exists("%s/%s" % [directory_path, IGNORE_MARKER]):
			ignored = true

		if directory.is_link(IGNORE_MARKER):
			discovery_errors.append("unsupported corpus symlink: %s/%s" % [directory_path, IGNORE_MARKER])

		var subdirectories: Array[String] = []
		var source_names: Array[String] = []
		var expectation_names: Array[String] = []
		for name in names:
			if name == "." or name == "..":
				continue
			var entry_path := "%s/%s" % [directory_path, name]
			if directory.is_link(entry_path):
				discovery_errors.append("unsupported corpus symlink: %s" % entry_path)
				continue
			if DirAccess.dir_exists_absolute(entry_path):
				subdirectories.append(entry_path)
			elif name.ends_with(CASE_EXTENSION):
				source_names.append(name)
			elif name.ends_with(EXPECTATION_EXTENSION):
				expectation_names.append(name)

		var source_name_set := {}
		for name in source_names:
			source_name_set[name] = true

		for name in source_names:
			if name.ends_with(HELPER_SUFFIX):
				skipped_count += 1
			elif ignored:
				skipped_count += 1
			else:
				cases.append({
					"path": "%s/%s" % [directory_path, name],
					"expectation_path": "%s/%s%s" % [directory_path, name.get_basename(), EXPECTATION_EXTENSION],
				})

		for name in expectation_names:
			if ignored:
				continue
			var paired_source := "%s%s" % [name.trim_suffix(EXPECTATION_EXTENSION), CASE_EXTENSION]
			if not source_name_set.has(paired_source):
				orphaned_expectations.append("%s/%s" % [directory_path, name])

		for subdirectory in subdirectories:
			pending.append([subdirectory, ignored])

	cases.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return a["path"] < b["path"])
	orphaned_expectations.sort()
	unreadable_directories.sort()
	return {
		"cases": cases,
		"orphaned_expectations": orphaned_expectations,
		"unreadable_directories": unreadable_directories,
		"discovery_errors": discovery_errors,
		"skipped_count": skipped_count,
	}


## Exact UTF-8 block comparison removes only the required final LF.
func _run_case(case_info: Dictionary) -> Dictionary:
	var expectation_path: String = case_info["expectation_path"]
	if not FileAccess.file_exists(expectation_path):
		return _failure(case_info, FailureReason.MISSING_EXPECTATION, "missing expectation %s" % expectation_path)

	var expectation_bytes := FileAccess.get_file_as_bytes(expectation_path)
	var expectation_error := FileAccess.get_open_error()
	if expectation_error != OK:
		return _failure(
			case_info,
			FailureReason.INVALID_EXPECTATION,
			"expectation %s is unreadable (error %d)" % [expectation_path, expectation_error]
		)
	var expectation_text := expectation_bytes.get_string_from_utf8()
	if expectation_text.to_utf8_buffer() != expectation_bytes:
		return _failure(
			case_info,
			FailureReason.INVALID_EXPECTATION,
			"expectation %s is not valid UTF-8" % expectation_path
		)
	if expectation_bytes.has(0) or not expectation_text.ends_with("\n") or expectation_text == "\n" or expectation_text.ends_with("\n\n") or expectation_text.contains("\r"):
		return _failure(case_info, FailureReason.INVALID_EXPECTATION, "expectation %s requires a nonempty block with exactly one final LF, without CR or NUL" % expectation_path)
	var expected := expectation_text.trim_suffix("\n")

	var evaluation: Variant = _evaluate(case_info["path"], case_info["stage"])
	var shape_error := _result_error(evaluation, case_info["stage"])
	if not shape_error.is_empty():
		return _failure(case_info, FailureReason.INVALID_RESULT, shape_error)
	if evaluation.get("source_unreadable", false):
		return _failure(case_info, FailureReason.UNREADABLE_SOURCE, evaluation["output"])

	if evaluation.infrastructure_error:
		return _failure(case_info, FailureReason.INVALID_RESULT, evaluation.output)
	var actual: String = evaluation["output"]
	if expected != actual:
		var failure := _failure(
			case_info,
			FailureReason.OUTPUT_MISMATCH,
			"output mismatch\nexpected: \"%s\"\nactual:   \"%s\""
			% [_escape_mismatch_value(expected), _escape_mismatch_value(actual)],
			actual
		)
		failure["fixture_index"] = evaluation.get("fixture_index", {})
		failure["analysis_ran"] = evaluation.analysis_ran
		return failure
	return {"passed": true, "path": case_info.path, "expected": expected, "actual": actual, "analysis_ran": evaluation.analysis_ran, "fixture_index": evaluation.get("fixture_index", {})}


func _evaluate(case_path: String, stage: String) -> Variant:
	if case_evaluator.is_valid():
		return case_evaluator.call(case_path)
	return _evaluate_with_language(case_path, stage)


func _result_error(result: Variant, stage: String) -> String:
	if not result is Dictionary:
		return "malformed frontend result: expected Dictionary"
	for key in ["ok", "analysis_ran", "infrastructure_error"]:
		if not result.get(key) is bool:
			return "malformed frontend result: Boolean %s required" % key
	if not result.get("output") is String or result.output.is_empty():
		return "malformed frontend result: nonempty output required"
	if stage == "parser" and result.analysis_ran or result.infrastructure_error and result.ok or stage == "analyzer" and result.ok and not result.analysis_ran:
		return "malformed frontend result: inconsistent stage/outcome"
	if result.has("source_unreadable") and (not result.source_unreadable is bool or result.source_unreadable and (result.ok or not result.infrastructure_error)):
		return "malformed frontend result: inconsistent unreadable source"
	if result.output == SUCCESS_SENTINEL and not result.ok or stage == "parser" and result.ok and result.output != SUCCESS_SENTINEL:
		return "malformed frontend result: contradictory success block"
	if result.output.contains("\r") or result.output.ends_with("\n"):
		return "malformed frontend result: invalid block terminator"
	return ""


func _evaluate_with_language(case_path: String, stage: String) -> Variant:
	var source_bytes := FileAccess.get_file_as_bytes(case_path)
	var open_error := FileAccess.get_open_error()
	if open_error != OK:
		return {"ok": false, "output": "source %s is unreadable (error %d)" % [case_path, open_error], "source_unreadable": true, "analysis_ran": false, "infrastructure_error": true}
	var probe: Variant = frontend_factory.call()
	if probe == null or not probe.has_method("evaluate_corpus"):
		return {"ok": false, "output": "missing raw corpus frontend probe: %s" % case_path, "analysis_ran": false, "infrastructure_error": true}
	if fixture_paths.is_empty():
		return probe.evaluate_corpus(source_bytes, case_path, stage)
	return probe.evaluate_corpus(source_bytes, case_path, stage, fixture_paths)


func _failure(
	case_info: Dictionary, reason: int, message: String, actual: String = ""
) -> Dictionary:
	return {
		"passed": false,
		"reason": reason,
		"path": case_info["path"],
		"expectation_path": case_info["expectation_path"],
		"message": message,
		"actual": actual,
		"expected": FileAccess.get_file_as_string(case_info["expectation_path"]).trim_suffix("\n"),
	}


func _refusal_reason(failures: Array[Dictionary], discovered_nothing: bool, allow_empty: bool) -> String:
	if discovered_nothing and not allow_empty:
		return "no cases discovered"
	for failure in failures:
		if failure["reason"] != FailureReason.OUTPUT_MISMATCH:
			return "%s failed for a reason other than an output mismatch" % failure["path"]
	return ""


func _write_expectation(expectation_path: String, actual: String) -> int:
	var file := FileAccess.open(expectation_path, FileAccess.WRITE)
	if file == null:
		return FileAccess.get_open_error()
	file.store_string(actual + "\n")
	return OK


## Presentation-only escaping for OUTPUT_MISMATCH diagnostics. Remaining ASCII C0
## and DEL render as lowercase \\xNN escapes; comparison and --update-expectations
## stay on the raw values.
func _escape_mismatch_value(value: String) -> String:
	var escaped := ""
	for index in value.length():
		var code := value.unicode_at(index)
		match code:
			0x5C:
				escaped += "\\\\"
			0x22:
				escaped += "\\\""
			0x0D:
				escaped += "\\r"
			0x0A:
				escaped += "\\n"
			0x09:
				escaped += "\\t"
			_:
				if code < 0x20 or code == 0x7F:
					escaped += "\\x%02x" % code
				else:
					escaped += value[index]
	return escaped


func _summary_line(passed: int, total: int, skipped_count: int) -> String:
	return "%s %d/%d skipped=%d" % [SUMMARY_PREFIX, passed, total, skipped_count]


# Imported roots always use their own generated manifest, including descendant runs.
# Local fixtures must explicitly choose a stage; this cannot bypass imported metadata.
func _assign_stages(corpus_root: String, cases: Array, update: bool) -> String:
	var identity := _path_identity(corpus_root)
	if identity.has("error"):
		return identity.error
	corpus_root = identity.path
	for case in cases:
		identity = _path_identity(case.path)
		if identity.has("error"):
			return identity.error
		case.path = identity.path
		if case.has("expectation_path"):
			identity = _path_identity(case.expectation_path)
			if identity.has("error"):
				return identity.error
			case.expectation_path = identity.path
	var registry_path := ProjectSettings.globalize_path("res://").path_join("../scripts/corpus_sources.json")
	var registry := _read_unique_json(registry_path)
	if registry.has("error"):
		return registry.error
	var document: Dictionary = registry.document
	if not document.get("corpora") is Dictionary or not document.get("revision") is String:
		return "invalid corpus registry: %s" % registry_path
	var owned := {}
	for record in document.corpora.values():
		if record.get("state") != "active":
			continue
		var root := "res://" + String(record.destination).trim_prefix("project/")
		if not (_under(corpus_root, root) or _under(root, corpus_root)):
			continue
		if update:
			return "--update-expectations refused: generated imported root %s is importer-owned" % root
		var manifest := _read_unique_json(root.path_join("case_stages.json"))
		if manifest.has("error"):
			return manifest.error
		var validated := _validate_stage_manifest(manifest.document, root, document.revision)
		if validated.has("error"):
			return validated.error
		owned.merge(validated.stages)

	var staging_root := "res://tests/corpus_staging/analyzer"
	if _under(corpus_root, staging_root) or _under(staging_root, corpus_root):
		if update:
			return "--update-expectations refused: discovery staging is importer-owned"
		var manifest := _read_unique_json(staging_root.path_join("case_stages.json"))
		if manifest.has("error"):
			return manifest.error
		var validated := _validate_stage_manifest(manifest.document, staging_root, document.revision)
		if validated.has("error"):
			return validated.error
		owned.merge(validated.stages)
		fixture_paths = _fixture_source_paths(staging_root)
		fixture_paths.append_array(_fixture_source_paths("res://tests/corpus/parser"))
		fixture_paths.sort()

	for case in cases:
		if owned.has(case.path):
			case["stage"] = owned[case.path]
			continue
		if _under(case.path, "res://tests/corpus"):
			return "unregistered imported case has no manifest: %s" % case.path
		var selected := ""
		for root in fixture_stages:
			if _under(case.path, root):
				if not selected.is_empty():
					return "overlapping fixture stages: %s" % case.path
				if not fixture_stages[root] is String or not fixture_stages[root] in ["parser", "analyzer"]:
					return "invalid explicit fixture stage: %s" % case.path
				selected = fixture_stages[root]
		if not selected in ["parser", "analyzer"]:
			return "explicit fixture stage required: %s" % case.path
		case["stage"] = selected
	return ""


func _validate_stage_manifest(stages: Dictionary, root: String, revision: String) -> Dictionary:
	var identity := _path_identity(root)
	if identity.has("error"):
		return identity
	root = identity.path
	if stages.keys().size() != 3 or typeof(stages.get("schema_version")) not in [TYPE_INT, TYPE_FLOAT] or stages.get("schema_version") != 1 or stages.get("foundry_revision") != revision or not stages.get("cases") is Dictionary:
		return {"error": "invalid/stale case stage manifest: %s" % root}
	var discovery := _discover_corpus(root)
	if not discovery.get("discovery_errors", []).is_empty():
		return {"error": "; ".join(discovery.discovery_errors)}
	if not discovery.unreadable_directories.is_empty():
		return {"error": "unreadable imported directory: %s" % root}
	var remaining: Dictionary = stages.cases.duplicate()
	var owned := {}
	for path in remaining:
		if not path is String or not _valid_case_relative(path) or not remaining[path] in ["parser", "analyzer"]:
			return {"error": "invalid case stage entry: %s/%s" % [root, str(path)]}
	for case in discovery.cases:
		var relative: String = case.path.trim_prefix(root + "/")
		if not remaining.has(relative):
			return {"error": "missing case stage entry: %s" % case.path}
		owned[case.path] = remaining[relative]
		remaining.erase(relative)
	if not remaining.is_empty():
		return {"error": "extra/helper case stage entries: %s" % root}
	return {"stages": owned}


func _under(path: String, root: String) -> bool:
	var path_identity := _path_identity(path)
	var root_identity := _path_identity(root)
	if path_identity.has("error") or root_identity.has("error"):
		return false
	return _filesystem_under(ProjectSettings.globalize_path(path_identity.path), ProjectSettings.globalize_path(root_identity.path))


func _valid_case_relative(path: String) -> bool:
	if not path.ends_with(CASE_EXTENSION) or path.ends_with(HELPER_SUFFIX) or path.contains("\\") or path.begins_with("/"):
		return false
	for part in path.split("/"):
		if part in ["", ".", ".."]:
			return false
	return true


# Map filesystem/resource/relative spellings to one case identity. Resolve root
# aliases with the filesystem API; linked entries inside a discovered tree are
# rejected separately so cycles or a linked expectation cannot hide ownership.
var _filesystem_roots: Dictionary = {}

func _resolve_filesystem_path(path: String, depth: int = 0) -> Dictionary:
	if depth > 64:
		return {"error": "cyclic or excessive filesystem links: %s" % path}
	path = path.replace("\\", "/").simplify_path()
	var parent := path.get_base_dir()
	if parent == path or parent.is_empty():
		return {"path": path}
	var resolved := _resolve_filesystem_path(parent, depth + 1)
	if resolved.has("error"):
		return resolved
	var candidate: String = resolved.path.path_join(path.get_file())
	var directory := DirAccess.open(resolved.path)
	if directory != null and directory.is_link(candidate):
		var target := directory.read_link(candidate)
		if target.is_empty():
			return {"error": "unreadable filesystem link: %s" % candidate}
		if not target.is_absolute_path():
			target = String(resolved.path).path_join(target)
		return _resolve_filesystem_path(target, depth + 1)
	return {"path": candidate}


func _filesystem_under(path: String, root: String) -> bool:
	var directory := DirAccess.open(root)
	if directory == null:
		directory = DirAccess.open(root.get_base_dir())
	if directory != null and not directory.is_case_sensitive(root):
		path = path.to_lower()
		root = root.to_lower()
	return path == root or path.begins_with(root.trim_suffix("/") + "/")


func _path_identity(path: String) -> Dictionary:
	if path.begins_with("res://") or path.begins_with("user://"):
		path = ProjectSettings.globalize_path(path)
	elif not path.is_absolute_path():
		var current := DirAccess.open(".")
		if current == null:
			return {"error": "cannot resolve relative corpus path: %s" % path}
		path = current.get_current_dir().path_join(path)
	var resolved := _resolve_filesystem_path(path)
	if resolved.has("error"):
		return resolved
	for scheme in ["res://", "user://"]:
		if not _filesystem_roots.has(scheme):
			var root := _resolve_filesystem_path(ProjectSettings.globalize_path(scheme))
			if root.has("error"):
				return root
			_filesystem_roots[scheme] = String(root.path).trim_suffix("/")
		var root: String = _filesystem_roots[scheme]
		if _filesystem_under(resolved.path, root):
			return {"path": scheme + String(resolved.path).substr(root.length()).trim_prefix("/")}
	return resolved


# Godot's JSON parser accepts extensions such as trailing commas and literal
# control characters. Validate JSON grammar and duplicate decoded keys before
# accepting the deserialized document; a permissive parse is not manifest evidence.
class StrictJSON:
	var source: String
	var position := 0
	var problem := ""
	var number := RegEx.new()

	func validate(text: String) -> String:
		source = text
		number.compile("-?(0|[1-9][0-9]*)(\\.[0-9]+)?([eE][+-]?[0-9]+)?")
		if not _value(0):
			return problem if not problem.is_empty() else "invalid strict JSON at character %d" % position
		_space()
		return "" if position == source.length() else "trailing JSON content at character %d" % position

	func _space() -> void:
		while position < source.length() and source[position] in [" ", "\t", "\r", "\n"]:
			position += 1

	func _take(token: String) -> bool:
		_space()
		if source.substr(position, token.length()) != token:
			return false
		position += token.length()
		return true

	func _string() -> bool:
		if not _take('"'):
			return false
		while position < source.length():
			var code := source.unicode_at(position)
			position += 1
			if code == 34:
				return true
			if code < 32:
				return false
			if code != 92:
				continue
			if position == source.length():
				return false
			var escape := source[position]
			position += 1
			if escape == "u":
				for digit in 4:
					if position == source.length() or not source[position] in "0123456789abcdefABCDEF":
						return false
					position += 1
			elif not escape in ['"', "\\", "/", "b", "f", "n", "r", "t"]:
				return false
		return false

	func _value(depth: int) -> bool:
		_space()
		if position == source.length() or depth > 256:
			return false
		match source[position]:
			'"':
				return _string()
			"{":
				return _object(depth + 1)
			"[":
				position += 1
				if _take("]"):
					return true
				while _value(depth + 1):
					if _take("]"):
						return true
					if not _take(","):
						return false
				return false
		for literal in ["true", "false", "null"]:
			if _take(literal):
				return true
		var matched := number.search(source, position)
		if matched == null or matched.get_start() != position:
			return false
		position = matched.get_end()
		return true

	func _object(depth: int) -> bool:
		position += 1
		if _take("}"):
			return true
		var keys := {}
		while true:
			_space()
			var start := position
			if not _string():
				return false
			var key: String = JSON.parse_string(source.substr(start, position - start))
			if keys.has(key):
				problem = "duplicate JSON key %s" % key
				return false
			keys[key] = true
			if not _take(":"):
				return false
			_space()
			start = position
			if not _value(depth):
				return false
			if key == "schema_version" and source.substr(start, position - start) != "1":
				problem = "schema_version must be integer 1"
				return false
			if _take("}"):
				return true
			if not _take(","):
				return false
		return false


func _read_unique_json(path: String) -> Dictionary:
	var bytes := FileAccess.get_file_as_bytes(path)
	if FileAccess.get_open_error() != OK:
		return {"error": "missing/unreadable JSON: %s" % path}
	var text := bytes.get_string_from_utf8()
	if text.to_utf8_buffer() != bytes or bytes.has(0):
		return {"error": "invalid UTF-8 JSON: %s" % path}
	var parser := JSON.new()
	if parser.parse(text) != OK or not parser.data is Dictionary:
		return {"error": "invalid JSON object: %s" % path}
	# Establish that strings can be decoded (including UTF-16 escape pairs),
	# then reject the syntax extensions before using any decoded manifest data.
	var complaint := StrictJSON.new().validate(text)
	if not complaint.is_empty():
		return {"error": "%s: %s" % [complaint, path]}
	return {"document": parser.data}


# Dependency availability is independent of the selected execution case.
func _fixture_source_paths(root: String) -> PackedStringArray:
	var paths := PackedStringArray()
	var directory := DirAccess.open(root)
	if directory == null:
		return paths
	for file in directory.get_files():
		if file.ends_with(CASE_EXTENSION):
			paths.append(root.path_join(file))
	for child in directory.get_directories():
		paths.append_array(_fixture_source_paths(root.path_join(child)))
	paths.sort()
	return paths
