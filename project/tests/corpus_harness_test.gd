# corpus_harness_test.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT
extends SceneTree

const Harness = preload("res://tests/corpus_harness.gd")

const FIXTURES_ROOT := "res://tests/corpus_fixtures"
const PARSE_ERROR := "parse error: unexpected token '}'"

func _initialize() -> void:
	var failures: Array[String] = []

	_test_paired_case_passes(failures)
	_test_missing_expectation_fails(failures)
	_test_orphaned_expectation_fails(failures)
	_test_invalid_utf8_expectation_fails(failures)
	_test_parse_failure_against_sentinel_fails(failures)
	_test_trailing_whitespace_drift_fails(failures)
	_test_line_ending_drift_fails(failures)
	_test_fsignore_directory_is_skipped_and_counted(failures)
	_test_notest_helper_is_not_a_case(failures)
	_test_zero_cases_fails_without_allow_empty(failures)
	_test_failing_case_means_nonzero_exit(failures)
	_test_summary_is_single_last_line(failures)
	_test_deterministic_output(failures)
	_test_update_expectations_refuses_non_mismatch(failures)
	_test_update_expectations_rewrites_mismatches(failures)
	_test_unreadable_corpus_root_is_harness_error(failures)
	_test_unreadable_directory_is_harness_error(failures)
	_test_runner_process_arguments(failures)

	for failure in failures:
		push_error(failure)
	if failures.is_empty():
		print("corpus harness fail-closed contract: all assertions passed")
	quit(0 if failures.is_empty() else 1)


func _test_paired_case_passes(failures: Array[String]) -> void:
	var result := _run_harness("%s/passing" % FIXTURES_ROOT)
	_expect(failures, result["exit_code"] == Harness.ExitCode.PASSED, "paired case must pass: %s" % _text(result))
	_expect(
		failures,
		_last_line(result) == "%s 1/1 skipped=1" % Harness.SUMMARY_PREFIX,
		"paired-case summary mismatch: %s" % _last_line(result)
	)


func _test_missing_expectation_fails(failures: Array[String]) -> void:
	var result := _run_harness("%s/missing_expectation" % FIXTURES_ROOT)
	_expect(failures, result["exit_code"] == Harness.ExitCode.CASES_FAILED, "missing .out must fail the run")
	_expect(
		failures,
		_text(result).contains(
			"FAIL %s/missing_expectation/case.fs: missing expectation %s/missing_expectation/case.out"
			% [FIXTURES_ROOT, FIXTURES_ROOT]
		),
		"missing .out must be a named failure naming the expected .out path: %s" % _text(result)
	)


func _test_orphaned_expectation_fails(failures: Array[String]) -> void:
	var result := _run_harness("%s/orphaned_expectation" % FIXTURES_ROOT)
	_expect(failures, result["exit_code"] == Harness.ExitCode.CASES_FAILED, "orphaned .out must fail the run")
	_expect(
		failures,
		_text(result).contains(
			"FAIL orphaned expectation %s/orphaned_expectation/case.out: no matching .fs case" % FIXTURES_ROOT
		),
		"orphaned .out must be a named run-level failure: %s" % _text(result)
	)


func _test_invalid_utf8_expectation_fails(failures: Array[String]) -> void:
	var result := _run_harness("%s/invalid_expectation" % FIXTURES_ROOT)
	_expect(failures, result["exit_code"] == Harness.ExitCode.CASES_FAILED, "non-UTF-8 .out must fail the run")
	_expect(
		failures,
		_text(result).contains(
			"FAIL %s/invalid_expectation/case.fs: expectation %s/invalid_expectation/case.out is not valid UTF-8"
			% [FIXTURES_ROOT, FIXTURES_ROOT]
		),
		"non-UTF-8 .out must be a named failure distinct from missing and orphaned: %s" % _text(result)
	)


func _test_parse_failure_against_sentinel_fails(failures: Array[String]) -> void:
	var result := _run_harness(
		"%s/parse_failure_vs_ok" % FIXTURES_ROOT,
		false,
		false,
		func(_case_path: String) -> Dictionary:
			return {"ok": false, "output": PARSE_ERROR}
	)
	_expect(failures, result["exit_code"] == Harness.ExitCode.CASES_FAILED, "parse failure against the sentinel must fail")
	var text := _text(result)
	_expect(
		failures,
		text.contains('expected: "%s"' % Harness.SUCCESS_SENTINEL) and text.contains('actual:   "%s"' % PARSE_ERROR),
		"parse failure must print actual vs expected: %s" % text
	)


func _test_trailing_whitespace_drift_fails(failures: Array[String]) -> void:
	var result := _run_harness(
		"%s/trailing_whitespace_drift" % FIXTURES_ROOT,
		false,
		false,
		func(_case_path: String) -> Dictionary:
			return {"ok": true, "output": ""}
	)
	_expect(failures, result["exit_code"] == Harness.ExitCode.CASES_FAILED, "trailing-whitespace-only drift must fail")
	_expect(
		failures,
		_text(result).contains('expected: "%s "' % Harness.SUCCESS_SENTINEL)
			and _text(result).contains('actual:   "%s"' % Harness.SUCCESS_SENTINEL),
		"trailing whitespace must survive into the exact comparison: %s" % _text(result)
	)


func _test_line_ending_drift_fails(failures: Array[String]) -> void:
	var result := _run_harness(
		"%s/line_ending_drift" % FIXTURES_ROOT,
		false,
		false,
		func(_case_path: String) -> Dictionary:
			return {"ok": true, "output": ""}
	)
	_expect(failures, result["exit_code"] == Harness.ExitCode.CASES_FAILED, "line-ending-only drift must fail")
	_expect(
		failures,
		_text(result).contains('expected: "%s\r"' % Harness.SUCCESS_SENTINEL),
		"a carriage return in the expectation must survive into the exact comparison: %s" % _text(result)
	)


func _test_fsignore_directory_is_skipped_and_counted(failures: Array[String]) -> void:
	var direct := _run_harness("%s/ignored_directory" % FIXTURES_ROOT)
	_expect(failures, direct["exit_code"] == Harness.ExitCode.CASES_FAILED, "a corpus root inside .fsignore scope still has no cases")
	_expect(
		failures,
		_text(direct).contains("no cases discovered") and _last_line(direct) == "%s 0/0 skipped=1" % Harness.SUMMARY_PREFIX,
		".fsignore must skip the directory and report the skipped count: %s" % _text(direct)
	)

	# The assertion is on the skipped count, not on the case total: sibling fixture directories
	# (the tokenizer contract cases, and whatever later milestones add) legitimately change how
	# many cases the tree holds, and pinning that number here would make an unrelated fixture
	# addition look like an .fsignore regression.
	var full_tree := _run_harness(FIXTURES_ROOT)
	_expect(
		failures,
		_last_line(full_tree).ends_with(" skipped=2"),
		"full fixture tree must count the ignored case and the helper as skipped: %s" % _last_line(full_tree)
	)
	_expect(
		failures,
		not _text(full_tree).contains("FAIL res://tests/corpus_fixtures/ignored_directory"),
		"a skipped case must never be reported as a failure"
	)


func _test_notest_helper_is_not_a_case(failures: Array[String]) -> void:
	var result := _run_harness("%s/passing" % FIXTURES_ROOT)
	_expect(
		failures,
		_last_line(result) == "%s 1/1 skipped=1" % Harness.SUMMARY_PREFIX,
		"helper.notest.fs must be skipped and counted, never a case: %s" % _last_line(result)
	)


func _test_zero_cases_fails_without_allow_empty(failures: Array[String]) -> void:
	var empty_root := "%s/empty_corpus" % FIXTURES_ROOT
	var refused := _run_harness(empty_root)
	_expect(failures, refused["exit_code"] == Harness.ExitCode.CASES_FAILED, "zero cases must fail the run")
	_expect(failures, _text(refused).contains("no cases discovered"), "zero cases must say so: %s" % _text(refused))

	var allowed := _run_harness(empty_root, true)
	_expect(failures, allowed["exit_code"] == Harness.ExitCode.PASSED, "--allow-empty must permit an empty corpus")
	_expect(
		failures,
		_last_line(allowed) == "%s 0/0 skipped=0" % Harness.SUMMARY_PREFIX,
		"allowed empty corpus summary mismatch: %s" % _last_line(allowed)
	)


func _test_failing_case_means_nonzero_exit(failures: Array[String]) -> void:
	var result := _run_harness(FIXTURES_ROOT)
	_expect(
		failures,
		result["exit_code"] == Harness.ExitCode.CASES_FAILED,
		"any failing case must give a non-zero exit code"
	)


func _test_summary_is_single_last_line(failures: Array[String]) -> void:
	var result := _run_harness(FIXTURES_ROOT)
	var output: Array = result["output"]
	var summary_lines := 0
	for line in output:
		if String(line).begins_with(Harness.SUMMARY_PREFIX):
			summary_lines += 1
	_expect(
		failures,
		summary_lines == 1 and String(output[output.size() - 1]).begins_with(Harness.SUMMARY_PREFIX),
		"exactly one summary line, and it must be last"
	)


func _test_deterministic_output(failures: Array[String]) -> void:
	var first := _run_harness(FIXTURES_ROOT)
	var second := _run_harness(FIXTURES_ROOT)
	_expect(
		failures,
		_text(first) == _text(second) and first["exit_code"] == second["exit_code"],
		"two runs over an unchanged tree must be byte-identical"
	)


func _test_update_expectations_refuses_non_mismatch(failures: Array[String]) -> void:
	var result := _run_harness("%s/missing_expectation" % FIXTURES_ROOT, false, true)
	_expect(
		failures,
		result["exit_code"] == Harness.ExitCode.HARNESS_ERROR,
		"--update-expectations must refuse non-mismatch failures"
	)
	_expect(
		failures,
		_text(result).contains("--update-expectations refused"),
		"refusal must be a named harness error: %s" % _text(result)
	)
	_expect(
		failures,
		not FileAccess.file_exists("%s/missing_expectation/case.out" % FIXTURES_ROOT),
		"a refused update must not write the missing expectation"
	)


func _test_update_expectations_rewrites_mismatches(failures: Array[String]) -> void:
	var temporary_root := "user://bs_corpus_update_test"
	_copy_fixture_directory("%s/trailing_whitespace_drift" % FIXTURES_ROOT, temporary_root)
	var updated := _run_harness(temporary_root, false, true)
	_expect(
		failures,
		updated["exit_code"] == Harness.ExitCode.PASSED,
		"update mode must succeed when every failure is an output mismatch: %s" % _text(updated)
	)
	var rerun := _run_harness(temporary_root)
	_expect(
		failures,
		rerun["exit_code"] == Harness.ExitCode.PASSED and _last_line(rerun) == "%s 1/1 skipped=0" % Harness.SUMMARY_PREFIX,
		"a rewritten expectation must make the next run pass: %s" % _text(rerun)
	)
	var rewritten := FileAccess.get_file_as_bytes("%s/case.out" % temporary_root).get_string_from_utf8()
	_expect(
		failures,
		rewritten == "%s\n" % Harness.SUCCESS_SENTINEL,
		"update must write the actual output as the new expectation, got: %s" % [rewritten]
	)
	_remove_directory(temporary_root)


func _test_unreadable_corpus_root_is_harness_error(failures: Array[String]) -> void:
	var result := _run_harness("res://tests/does_not_exist")
	_expect(failures, result["exit_code"] == Harness.ExitCode.HARNESS_ERROR, "unreadable corpus root must be exit code 2")
	_expect(
		failures,
		_text(result).contains("not a readable directory"),
		"unreadable corpus root must be a named harness error: %s" % _text(result)
	)


## An unreadable directory hides an unknown number of cases. It must abort the
## run rather than shrink the corpus, and it must outrank --allow-empty, which
## would otherwise turn a locked-out corpus into a green run.
func _test_unreadable_directory_is_harness_error(failures: Array[String]) -> void:
	var nested_root := "user://bs_corpus_unreadable_nested"
	var locked_child := "%s/locked" % nested_root
	_remove_directory(nested_root)
	_copy_fixture_directory("%s/passing" % FIXTURES_ROOT, nested_root)
	_copy_fixture_directory("%s/passing" % FIXTURES_ROOT, locked_child)

	var locked_root := "user://bs_corpus_unreadable_root"
	_remove_directory(locked_root)
	DirAccess.make_dir_recursive_absolute(locked_root)

	if not _make_unreadable(locked_child) or not _make_unreadable(locked_root):
		failures.append(
			"could not revoke read permission on %s; the unreadable-directory contract went unverified"
			% ProjectSettings.globalize_path(locked_child)
		)
	else:
		var nested := _run_harness(nested_root)
		_expect(
			failures,
			nested["exit_code"] == Harness.ExitCode.HARNESS_ERROR,
			"an unreadable subdirectory must abort the run, not silently shrink the corpus: %s" % _text(nested)
		)
		_expect(
			failures,
			_text(nested).contains("BS_ERROR corpus directory is unreadable: %s" % locked_child),
			"the unreadable directory must be named: %s" % _text(nested)
		)
		_expect(
			failures,
			_run_harness(nested_root, true)["exit_code"] == Harness.ExitCode.HARNESS_ERROR,
			"--allow-empty must not downgrade an unreadable subdirectory to a pass"
		)
		_expect(
			failures,
			_run_harness(nested_root, false, true)["exit_code"] == Harness.ExitCode.HARNESS_ERROR,
			"--update-expectations must refuse to run against an unreadable subdirectory"
		)

		var unreadable_root := _run_harness(locked_root, true)
		_expect(
			failures,
			unreadable_root["exit_code"] == Harness.ExitCode.HARNESS_ERROR,
			"an unreadable corpus root must be a harness error even with --allow-empty: %s" % _text(unreadable_root)
		)

	_make_readable(locked_child)
	_make_readable(locked_root)
	_remove_directory(nested_root)
	_remove_directory(locked_root)


func _test_runner_process_arguments(failures: Array[String]) -> void:
	var bad_arguments := _run_runner_process(["--bogus"])
	_expect(
		failures,
		bad_arguments["exit_code"] == Harness.ExitCode.HARNESS_ERROR and String(bad_arguments["output"]).contains("unknown argument"),
		"unknown argument must be a named harness error with exit code 2"
	)

	var default_corpus := _run_runner_process([])
	_expect(
		failures,
		default_corpus["exit_code"] == Harness.ExitCode.CASES_FAILED and String(default_corpus["output"]).contains("no cases discovered"),
		"the empty default corpus must fail without --allow-empty"
	)

	var allowed_corpus := _run_runner_process(["--allow-empty"])
	_expect(
		failures,
		allowed_corpus["exit_code"] == Harness.ExitCode.PASSED,
		"the empty default corpus must pass with --allow-empty"
	)


func _run_harness(
	corpus_root: String,
	allow_empty: bool = false,
	update_expectations: bool = false,
	evaluator: Callable = Callable()
) -> Dictionary:
	var harness := Harness.new()
	if evaluator.is_valid():
		harness.case_evaluator = evaluator
	return harness.run(corpus_root, allow_empty, update_expectations)


func _run_runner_process(arguments: Array) -> Dictionary:
	var output: Array = []
	var project_directory := ProjectSettings.globalize_path("res://")
	var exit_code := OS.execute(
		OS.get_executable_path(),
		["--headless", "--path", project_directory, "--script", "res://tests/corpus_runner.gd", "--"] + arguments,
		output
	)
	return {"exit_code": exit_code, "output": str(output)}


func _text(result: Dictionary) -> String:
	return "\n".join(result["output"])


func _last_line(result: Dictionary) -> String:
	var output: Array = result["output"]
	return String(output[output.size() - 1])


func _expect(failures: Array[String], condition: bool, description: String) -> void:
	if not condition:
		failures.append(description)


## Revokes read permission via POSIX mode bits and confirms the directory is
## genuinely unopenable, so a platform that cannot produce the condition is
## reported rather than passing the assertion vacuously.
func _make_unreadable(path: String) -> bool:
	OS.execute("chmod", ["000", ProjectSettings.globalize_path(path)])
	return DirAccess.open(path) == null


func _make_readable(path: String) -> void:
	OS.execute("chmod", ["755", ProjectSettings.globalize_path(path)])


func _copy_fixture_directory(source_root: String, destination_root: String) -> void:
	DirAccess.make_dir_recursive_absolute(destination_root)
	var directory := DirAccess.open(source_root)
	if directory == null:
		push_error("fixture copy could not open %s" % source_root)
		return
	directory.list_dir_begin()
	var entry := directory.get_next()
	while not entry.is_empty():
		if entry == "." or entry == "..":
			entry = directory.get_next()
			continue
		var source_path := "%s/%s" % [source_root, entry]
		var destination_path := "%s/%s" % [destination_root, entry]
		if DirAccess.dir_exists_absolute(source_path):
			_copy_fixture_directory(source_path, destination_path)
		else:
			var contents := FileAccess.get_file_as_bytes(source_path)
			var target := FileAccess.open(destination_path, FileAccess.WRITE)
			target.store_buffer(contents)
			target.close()
		entry = directory.get_next()
	directory.list_dir_end()


func _remove_directory(path: String) -> void:
	var directory := DirAccess.open(path)
	if directory == null:
		return
	directory.list_dir_begin()
	var entries: Array[String] = []
	var entry := directory.get_next()
	while not entry.is_empty():
		entries.append(entry)
		entry = directory.get_next()
	directory.list_dir_end()
	for name in entries:
		var child := "%s/%s" % [path, name]
		if DirAccess.dir_exists_absolute(child):
			_remove_directory(child)
		else:
			DirAccess.remove_absolute(child)
	DirAccess.remove_absolute(path)
