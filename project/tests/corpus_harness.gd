# corpus_harness.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT
extends RefCounted

## Golden-file corpus harness for BaristaScript conformance runs.
##
## A corpus case is a `.barista` source paired with a `.out` expectation whose
## first line is either the success sentinel or the exact expected diagnostic
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
}

## Optional evaluator override: `func(case_path: String) -> Dictionary` returning
## `{"ok": bool, "output": String}` and, when the source itself cannot be read,
## `{"source_unreadable": true}`. Tests inject evaluators here; production runs
## use the registered BaristaScript frontend via `_evaluate_with_language`.
var case_evaluator: Callable


## Runs every discovered case under `corpus_root` and returns
## `{"exit_code": int, "output": Array[String]}`. The output always ends with
## exactly one machine-readable summary line, so consecutive runs over an
## unchanged tree are byte-identical.
func run(corpus_root: String, allow_empty: bool = false, update_expectations: bool = false) -> Dictionary:
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

	var failures: Array[Dictionary] = []
	var passed := 0
	for case_info in cases:
		var outcome := _run_case(case_info)
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

	output.append(_summary_line(passed, cases.size(), skipped_count))
	return {"exit_code": exit_code, "output": output}


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

		var subdirectories: Array[String] = []
		var source_names: Array[String] = []
		var expectation_names: Array[String] = []
		for name in names:
			if name == "." or name == "..":
				continue
			var entry_path := "%s/%s" % [directory_path, name]
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
		"skipped_count": skipped_count,
	}


## Evaluates one case and compares its expectation byte-for-byte. The
## expectation is the first line of the `.out` file with no normalization of
## any kind: a trailing space or a `\r` survives into the comparison and fails.
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
	var expected := _first_line(expectation_text)

	var evaluation := _evaluate(case_info["path"])
	if evaluation.get("source_unreadable", false):
		return _failure(case_info, FailureReason.UNREADABLE_SOURCE, evaluation["output"])

	var actual: String = SUCCESS_SENTINEL if evaluation["ok"] else evaluation["output"]
	if expected != actual:
		return _failure(
			case_info,
			FailureReason.OUTPUT_MISMATCH,
			"output mismatch\nexpected: \"%s\"\nactual:   \"%s\""
			% [_escape_mismatch_value(expected), _escape_mismatch_value(actual)],
			actual
		)
	return {"passed": true}


func _evaluate(case_path: String) -> Dictionary:
	if case_evaluator.is_valid():
		return case_evaluator.call(case_path)
	return _evaluate_with_language(case_path)


## Production evaluation: run the case source through the BaristaScript
## frontend and report its first diagnostic, or the success sentinel when it
## reports none.
##
## The source is handed over as raw bytes rather than as a String on purpose. A
## String has already been decoded, and malformed UTF-8 has already become
## U+FFFD by then -- which is exactly the substitution the frontend must report
## instead of absorbing.
##
## The frontend reaches the parser, which is what makes the imported corpus mean
## anything: a tokenizer-only evaluation would have accepted every case whose
## source lexes, and 340 of them do. Tokenizer diagnostics still arrive here --
## the parser reports them as its own as it consumes the token stream -- so the
## tokenizer contract fixtures are evaluated by the same one path as the corpus.
func _evaluate_with_language(case_path: String) -> Dictionary:
	var source_bytes := FileAccess.get_file_as_bytes(case_path)
	var open_error := FileAccess.get_open_error()
	if open_error != OK:
		return {
			"ok": false,
			"output": "source %s is unreadable (error %d)" % [case_path, open_error],
			"source_unreadable": true,
		}
	var diagnostic: String = BaristaScriptParserProbe.new().first_parse_diagnostic(source_bytes, case_path)
	if diagnostic.is_empty():
		return {"ok": true, "output": ""}
	return {"ok": false, "output": diagnostic}


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


func _first_line(text: String) -> String:
	var newline := text.find("\n")
	if newline == -1:
		return text
	return text.substr(0, newline)


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
