# corpus_oracle_test.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

extends SceneTree
const Harness = preload("res://tests/corpus_harness.gd")
const SuiteGuard = preload("res://tests/suite_guard.gd")
const PRODUCER := "res://tests/oracle_fixtures/producer/"
var failures: Array[String] = []
var fixture_root := "user://oracle_contract"

func _initialize() -> void:
	DirAccess.make_dir_recursive_absolute(fixture_root)
	_write("case.barista", "".to_utf8_buffer())
	_test_comparator()
	_test_bad_bytes_and_results()
	_test_stages()
	_test_frontend()
	var frontend := Harness.new()
	frontend.fixture_stages["res://tests/oracle_fixtures/frontend"] = "analyzer"
	var native_cases := frontend.run("res://tests/oracle_fixtures/frontend")
	_expect(native_cases.exit_code == 0 and native_cases.output.back() == "%s 4/4 skipped=0" % Harness.SUMMARY_PREFIX, "real staged frontend fixtures: %s" % native_cases)
	for file in DirAccess.get_files_at(fixture_root):
		DirAccess.remove_absolute(fixture_root.path_join(file))
	DirAccess.remove_absolute(fixture_root)
	quit(SuiteGuard.report("corpus_oracle_test", failures))

func _expect(condition: bool, description: String) -> void:
	if not condition:
		failures.append(description)

func _write(path: String, bytes: PackedByteArray) -> void:
	FileAccess.open(fixture_root.path_join(path), FileAccess.WRITE).store_buffer(bytes)

func _result(output: String, ok: bool = true, ran: bool = true) -> Dictionary:
	return {"ok": ok, "output": output, "analysis_ran": ran, "infrastructure_error": false}

func _run(expected: PackedByteArray, actual: Variant, update: bool = false) -> Dictionary:
	_write("case.out", expected)
	var harness := Harness.new()
	harness.fixture_stages[fixture_root] = "analyzer"
	harness.case_evaluator = func(_path: String): return actual
	return harness.run(fixture_root, false, update)

func _block(name: String) -> String:
	return FileAccess.get_file_as_string(PRODUCER + name + ".block").trim_suffix("\n")

func _test_comparator() -> void:
	var warnings := _block("warnings__confusable_identifier")
	_expect(_run((warnings + "\n").to_utf8_buffer(), _result(warnings)).exit_code == 0, "successful warning block survives")
	var three := _block("warnings__assert_always_true")
	var lines := three.split("\n")
	for actual in [lines[0] + "\n" + lines[1] + "changed\n" + lines[2], lines[0] + "\n" + lines[2], three + "\n" + lines[2], lines[0] + "\n" + lines[2] + "\n" + lines[1]]:
		var result := _run((three + "\n").to_utf8_buffer(), _result(actual))
		_expect(result.exit_code == Harness.ExitCode.CASES_FAILED and "\n".join(result.output).contains("output mismatch"), "later warning mutation must mismatch")
	var errors := _block("features__contextual_tagged_union_shorthand.norun")
	_expect(_run((errors + "\n").to_utf8_buffer(), _result(errors, false)).exit_code == 0, "six-error equality")
	_expect(_run((errors + "\n").to_utf8_buffer(), _result(errors.split("\n")[0], false)).exit_code == 1, "later error removal mismatches")
	_expect(_run((warnings + "\n").to_utf8_buffer(), _result(three), true).exit_code == 0, "local complete block update")
	_expect(FileAccess.get_file_as_bytes(fixture_root.path_join("case.out")) == (three + "\n").to_utf8_buffer(), "update writes exact full block plus one LF")
	_expect(_run((three + "\n").to_utf8_buffer(), _result(three)).exit_code == 0, "updated block rerun passes")

func _test_bad_bytes_and_results() -> void:
	for bytes in [PackedByteArray(), PackedByteArray([10]), "first".to_utf8_buffer(), "first\n\n".to_utf8_buffer(), "first\r\n".to_utf8_buffer(), "first\rsecond\n".to_utf8_buffer(), PackedByteArray([97, 0, 10]), PackedByteArray([255, 10])]:
		var result := _run(bytes, _result("first"), true)
		_expect(result.exit_code == Harness.ExitCode.HARNESS_ERROR, "invalid expectation cannot update: %s" % bytes)
		_expect(FileAccess.get_file_as_bytes(fixture_root.path_join("case.out")) == bytes, "invalid expectation retained")
	var malformed: Array = [null, [], {}, {"ok": true}, _result(""), _result("first", true, false), _result(Harness.SUCCESS_SENTINEL, false)]
	for key in ["ok", "output", "analysis_ran", "infrastructure_error"]:
		var missing := _result("first")
		missing.erase(key)
		malformed.append(missing)
		var wrong := _result("first")
		wrong[key] = 123
		malformed.append(wrong)
	var infrastructure := _result("frontend failed", false)
	infrastructure.infrastructure_error = true
	malformed.append(infrastructure)
	for actual in malformed:
		_expect(_run("first\n".to_utf8_buffer(), actual, true).exit_code == Harness.ExitCode.HARNESS_ERROR, "malformed/infra result cannot update: %s" % str(actual))
	var harness := Harness.new()
	harness.fixture_stages[fixture_root] = "parser"
	_write("case.out", (Harness.SUCCESS_SENTINEL + "\n").to_utf8_buffer())
	harness.frontend_factory = func(): return null
	_expect(harness.run(fixture_root).exit_code == Harness.ExitCode.HARNESS_ERROR, "missing probe fails closed")
	var unreadable: Variant = harness._evaluate_with_language(fixture_root.path_join("missing.barista"), "parser")
	_expect(unreadable.source_unreadable and unreadable.infrastructure_error, "unreadable source distinct from empty")

func _test_stages() -> void:
	var harness := Harness.new()
	_expect(harness.run(fixture_root).exit_code == Harness.ExitCode.HARNESS_ERROR, "custom stage required")
	harness.fixture_stages[fixture_root] = 123
	_expect(harness.run(fixture_root).exit_code == Harness.ExitCode.HARNESS_ERROR, "invalid custom stage fails closed")
	harness.fixture_stages = {"res://tests/corpus": "parser"}
	_expect(not harness._assign_stages("res://tests/corpus", [{"path": "res://tests/corpus/unregistered.barista"}], false).is_empty(), "fixture selection cannot bypass imported registration")
	harness.fixture_stages.clear()
	for imported in ["res://tests/corpus", "res://tests/corpus/parser", "res://tests/corpus/parser/warnings"]:
		var result := harness.run(imported, false, true)
		_expect(result.exit_code == Harness.ExitCode.HARNESS_ERROR and "\n".join(result.output).contains("importer-owned"), "imported/descendant update refused")
	for text in ['{"cases":{"a":1,"a":2}}', '{"cases":{"a":1,"\\u0061":2}}', '{"schema_version":true}', '{"schema_version":1.0}', '{"schema_version":2}', '[]', '{']:
		_write("stages.json", text.to_utf8_buffer())
		_expect(harness._read_unique_json(fixture_root.path_join("stages.json")).has("error"), "malformed/duplicate JSON rejected: " + text)
	var revision: String = harness._read_unique_json("res://tests/corpus/parser/case_stages.json").document.foundry_revision
	var valid := {"schema_version": 1, "foundry_revision": revision, "cases": {"case.barista": "parser"}}
	_expect(not harness._validate_stage_manifest(valid, fixture_root, revision).has("error"), "valid stage manifest")
	for replacement in [{}, {"other.barista": "parser"}, {"case.barista": "bogus"}, {"../case.barista": "parser"}, {"case.barista": "parser", "helper.notest.barista": "analyzer"}]:
		var invalid := valid.duplicate(true)
		invalid.cases = replacement
		_expect(harness._validate_stage_manifest(invalid, fixture_root, revision).has("error"), "missing/extra/traversal/helper/stage fails closed")
	for field in ["schema_version", "foundry_revision"]:
		var invalid := valid.duplicate(true)
		invalid[field] = "stale"
		_expect(harness._validate_stage_manifest(invalid, fixture_root, revision).has("error"), "stale schema/revision rejected")
	_expect(harness._read_unique_json(fixture_root + "/missing.json").has("error"), "missing manifest fails closed")
	var cases := [{"path": fixture_root + "/a/a.barista"}, {"path": fixture_root + "/b/b.barista"}]
	harness.fixture_stages = {fixture_root + "/a": "parser", fixture_root + "/b": "analyzer"}
	_expect(harness._assign_stages(fixture_root, cases, false).is_empty() and cases[0].stage == "parser" and cases[1].stage == "analyzer", "aggregate fixture selects each case's owner")

func _test_frontend() -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var path := "res://tests/oracle_fixtures/control.barista"
	for bytes in [PackedByteArray([255]), PackedByteArray([0xc0, 0x80]), PackedByteArray([0xe2, 0x82])]:
		var result: Dictionary = probe.evaluate_corpus(bytes, path, "analyzer")
		_expect(result.output == "Invalid UTF-8 in source at byte offset 0." and not result.analysis_ran and not result.infrastructure_error, "strict decode precedes parser/analyzer")
	var offset: Dictionary = probe.evaluate_corpus(PackedByteArray([65, 0xe2, 40, 0xa1]), path, "analyzer")
	_expect(offset.output == "Invalid UTF-8 in source at byte offset 2.", "strict decode exact offset")
	var parse_bytes := FileAccess.get_file_as_bytes("res://tests/corpus_fixtures/tokenizer/deferred_to_parser_parameter.barista")
	var first := FileAccess.get_file_as_string("res://tests/corpus_fixtures/tokenizer/deferred_to_parser_parameter.out").trim_suffix("\n")
	var rejected: Dictionary = probe.evaluate_corpus(parse_bytes, path, "analyzer")
	_expect(not first.is_empty() and rejected.output == first and not rejected.analysis_ran, "parser short circuit exact existing diagnostic")
	for stage in ["parser", "analyzer"]:
		var clean: Dictionary = probe.evaluate_corpus("func test():\n\tpush_error(\"MUST NEVER EXECUTE\")\n".to_utf8_buffer(), path, stage)
		_expect(clean.ok and clean.output == Harness.SUCCESS_SENTINEL and clean.analysis_ran == (stage == "analyzer"), "clean static acceptance without execution: %s" % clean)
	var empty: Dictionary = probe.evaluate_corpus(PackedByteArray(), path, "parser")
	_expect(not empty.infrastructure_error, "empty valid bytes are readable input")
	var controls: Dictionary = probe.corpus_format_controls()
	_expect(controls.failure_without_diagnostic.infrastructure_error, "non-OK without diagnostics is infrastructure failure")
	_expect(controls.ordered_errors.output == ">> ERROR at line 1: 2\n>> ERROR at line 1: 3\n>> ERROR at line 1: 1\n>> ERROR at line 3: 0", "error source order with column/tie stability")
	_expect(controls.ordered_warnings.output == "~~ WARNING at line 9: (INTEGER_DIVISION) Integer division. Decimal part will be discarded.\n~~ WARNING at line 2: (INTEGER_DIVISION) Integer division. Decimal part will be discarded.", "warnings retain emission list order")
	_expect(not controls.errors_suppress_warnings.output.contains("WARNING"), "errors suppress warnings")
	_test_profile(probe, path)
	var state: Dictionary = probe.corpus_state_controls()
	for key in state:
		_expect(state[key], "temporary state isolation: " + key)

func _test_profile(probe: Object, path: String) -> void:
	var settings := {
		"debug/barista_script/warnings/enable": false,
		"debug/barista_script/warnings/unused_variable": 2,
		"debug/barista_script/warnings/unused_variable.macos": 2,
		"debug/barista_script/warnings/untyped_declaration": 2,
		"debug/barista_script/warnings/inferred_declaration": 2,
		"debug/barista_script/warnings/directory_rules": {"res://": 1},
		"debug/barista_script/analysis/strict_null_checks": true,
		"debug/barista_script/analysis/strict_dynamic_checks": true,
	}
	var saved := {}
	for key in settings:
		saved[key] = [ProjectSettings.has_setting(key), ProjectSettings.get_setting(key)]
		ProjectSettings.set_setting(key, settings[key])
	var absent := "debug/barista_script/warnings/integer_division"
	saved[absent] = [ProjectSettings.has_setting(absent), ProjectSettings.get_setting(absent)]
	if ProjectSettings.has_setting(absent):
		ProjectSettings.clear(absent)
	var unused := FileAccess.get_file_as_bytes(PRODUCER + "warnings__unused_variable.source")
	var expected := _block("warnings__unused_variable")
	var clean := "func test():\n\tpass\n".to_utf8_buffer()
	var error := FileAccess.get_file_as_bytes(PRODUCER + "analyzer__leading_number_separator.source")
	var parser_warning := "extends Node\n\nfunc test():\n\tvar port = 0\n\tvar pοrt = 1\n\tprints(port, pοrt)\n".to_utf8_buffer()
	var sequence: Array = [unused, clean, unused, PackedByteArray([255]), error, "func test(:\n".to_utf8_buffer(), parser_warning, clean, unused]
	var reverse := sequence.duplicate()
	reverse.reverse()
	for source in sequence + reverse:
		var result: Dictionary = probe.evaluate_corpus(source, path, "analyzer")
		if source == unused:
			_expect(result.ok and result.output == expected, "real analyzer warning pinned oracle: %s" % result)
		elif source == error:
			_expect(not result.ok and result.analysis_ran and result.output == _block("analyzer__leading_number_separator"), "real analyzer rejection: %s" % result)
		elif source == parser_warning:
			_expect(result.ok and result.output == _block("warnings__confusable_identifier").split("\n")[0], "real parser-produced warning: %s" % result)
		for key in settings:
			_expect(ProjectSettings.has_setting(key) and ProjectSettings.get_setting(key) == settings[key], "restore ambient setting " + key)
		_expect(not ProjectSettings.has_setting(absent), "restore absent warning setting")
	var ignored: Dictionary = probe.evaluate_corpus("extends Node\n@warning_ignore(\"unused_signal\")\nsignal quiet\nfunc test():\n\tpass\n".to_utf8_buffer(), path, "analyzer")
	_expect(ignored.ok and ignored.output == Harness.SUCCESS_SENTINEL, "annotation warning ignore retained: %s" % ignored)
	for key in saved:
		if saved[key][0]:
			ProjectSettings.set_setting(key, saved[key][1])
		elif ProjectSettings.has_setting(key):
			ProjectSettings.clear(key)
