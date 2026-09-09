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
	_test_filesystem_aliases()
	_test_strict_json()
	_test_frontend()
	_test_declaration_fixtures()
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


func _test_filesystem_aliases() -> void:
	var imported := "res://tests/corpus/parser/warnings"
	var original := {}
	for file in DirAccess.get_files_at(imported):
		original[file] = FileAccess.get_file_as_bytes(imported.path_join(file))
	var directory := DirAccess.open("res://.godot")
	for sample in [["res://.godot/repair1_alias", imported, ""], ["res://.godot/repair1_ancestor", "res://tests/corpus/parser", "/warnings"]]:
		var alias: String = sample[0]
		var created := directory.create_link(ProjectSettings.globalize_path(sample[1]), ProjectSettings.globalize_path(alias))
		_expect(created == OK, "create disposable alias")
		if created == OK:
			var harness := Harness.new()
			harness.fixture_stages[alias] = "parser"
			var result := harness.run(alias + sample[2], false, true)
			_expect(result.exit_code == Harness.ExitCode.HARNESS_ERROR, "imported alias update refused: %s" % result)
			DirAccess.remove_absolute(alias)
	var absolute := ProjectSettings.globalize_path(imported)
	var harness := Harness.new()
	harness.fixture_stages[absolute] = "analyzer"
	var cases := [{"path": absolute.path_join("unused_variable.barista")}]
	_expect(not harness._assign_stages(absolute, cases, true).is_empty(), "absolute imported update refused")
	_expect(harness._assign_stages(absolute, cases, false).is_empty() and cases[0].stage == "parser", "absolute imported cases use manifest stage")
	if not directory.is_case_sensitive(absolute):
		var different_case := absolute.to_upper()
		var case_harness := Harness.new()
		case_harness.fixture_stages[different_case] = "parser"
		_expect(case_harness.run(different_case, false, true).exit_code == Harness.ExitCode.HARNESS_ERROR, "case-insensitive imported alias refuses updates")
	var local := "res://.godot/repair1_local"
	DirAccess.make_dir_recursive_absolute(local)
	for linked_name in ["case.barista", "case.out", "nested"]:
		if linked_name != "nested":
			FileAccess.open(local.path_join("case.barista"), FileAccess.WRITE).store_string("")
			FileAccess.open(local.path_join("case.out"), FileAccess.WRITE).store_string(Harness.SUCCESS_SENTINEL + "\n")
			DirAccess.remove_absolute(local.path_join(linked_name))
		var target := imported if linked_name == "nested" else imported.path_join("unused_variable" + (".barista" if linked_name == "case.barista" else ".out"))
		var created := directory.create_link(ProjectSettings.globalize_path(target), ProjectSettings.globalize_path(local.path_join(linked_name)))
		_expect(created == OK, "create disposable linked corpus entry")
		if created == OK:
			var local_harness := Harness.new()
			local_harness.fixture_stages[local] = "parser"
			_expect(local_harness.run(local, false, true).exit_code == Harness.ExitCode.HARNESS_ERROR, "linked source/expectation/directory refused: " + linked_name)
			DirAccess.remove_absolute(local.path_join(linked_name))
		for file in ["case.barista", "case.out"]:
			if FileAccess.file_exists(local.path_join(file)):
				DirAccess.remove_absolute(local.path_join(file))
	# Ordinary aliases of a real local fixture remain usable, including full updates.
	var local_absolute := ProjectSettings.globalize_path(local)
	var current := DirAccess.open(".").get_current_dir()
	var relative := local_absolute.trim_prefix(current.trim_suffix("/") + "/")
	_expect(not relative.is_absolute_path(), "local fixture has ordinary relative spelling")
	for spelling in [local, local_absolute, relative]:
		FileAccess.open(local.path_join("case.barista"), FileAccess.WRITE).store_string("")
		FileAccess.open(local.path_join("case.out"), FileAccess.WRITE).store_string("old local expectation\n")
		var local_harness := Harness.new()
		local_harness.fixture_stages[spelling] = "parser"
		var result := local_harness.run(spelling, false, true)
		_expect(result.exit_code == 0, "ordinary local spelling update: %s: %s" % [spelling, result])
		_expect(FileAccess.get_file_as_string(local.path_join("case.out")) == Harness.SUCCESS_SENTINEL + "\n", "local update writes complete block")
	var local_alias := "res://.godot/repair1_local_alias"
	var alias_created := directory.create_link("repair1_local", ProjectSettings.globalize_path(local_alias))
	_expect(alias_created == OK, "create relative-target local root alias")
	if alias_created == OK:
		FileAccess.open(local.path_join("case.out"), FileAccess.WRITE).store_string("old local expectation\n")
		var alias_harness := Harness.new()
		alias_harness.fixture_stages[local_alias] = "parser"
		_expect(alias_harness.run(local_alias, false, true).exit_code == 0, "safe local root alias retains normal updates")
		DirAccess.remove_absolute(local_alias)
	var cycle := local.path_join("cycle")
	var cycle_created := directory.create_link(ProjectSettings.globalize_path(local), ProjectSettings.globalize_path(cycle))
	_expect(cycle_created == OK, "create disposable directory cycle")
	if cycle_created == OK:
		var cycle_harness := Harness.new()
		cycle_harness.fixture_stages[local] = "parser"
		_expect(cycle_harness.run(local).exit_code == Harness.ExitCode.HARNESS_ERROR, "linked directory cycle is rejected without traversal")
		DirAccess.remove_absolute(cycle)
	for file in ["case.barista", "case.out"]:
		DirAccess.remove_absolute(local.path_join(file))
	DirAccess.remove_absolute(local)
	for file in original:
		_expect(FileAccess.get_file_as_bytes(imported.path_join(file)) == original[file], "imported bytes unchanged: " + file)


func _test_strict_json() -> void:
	var matrix: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://tests/oracle_fixtures/json_contract.json"))
	var harness := Harness.new()
	for valid in matrix.valid:
		_write("strict.json", valid.to_utf8_buffer())
		_expect(not harness._read_unique_json(fixture_root.path_join("strict.json")).has("error"), "strict valid JSON accepted")
	for invalid in matrix.invalid:
		_write("strict.json", invalid.to_utf8_buffer())
		_expect(harness._read_unique_json(fixture_root.path_join("strict.json")).has("error"), "strict malformed JSON rejected: " + invalid)
	var revision: String = harness._read_unique_json("res://tests/corpus/parser/case_stages.json").document.foundry_revision
	_write("strict.json", ('{"schema_version":1,"foundry_revision":"%s","cases":{"case.barista":"parser",},}' % revision).to_utf8_buffer())
	_expect(harness._read_unique_json(fixture_root.path_join("strict.json")).has("error"), "malformed serialized stage manifest rejected before shape validation")


func _test_declaration_fixtures() -> void:
	var probe: Variant = ClassDB.instantiate("BaristaScriptAnalyzerProbe")
	var source := "import cafecito.dupsamefile\n\n@twice\nfunc test() -> void:\n\tpass\n"
	var path := "res://tests/oracle_fixtures/declarations/annotation_duplicate_in_imported_usage.barista"
	var sources := PackedStringArray(["res://tests/oracle_fixtures/declarations/annotation_duplicate_in_imported_lib.notest.barista"])
	var result: Dictionary = probe.evaluate_corpus(source.to_utf8_buffer(), path, "analyzer", sources)
	# The pinned ambiguity message remains an executed #140 provider-replay
	# residual in discovery. This control checks fixture availability, not a
	# replacement semantic expectation for that still-failing upstream case.
	_expect(result.get("fixture_index", {}).get("annotation_providers") == 1 and not result.infrastructure_error, "pinned annotation-only fixture index: %s" % result)
	var a: Dictionary = probe.evaluate_corpus("func test():\n\tpass\n".to_utf8_buffer(), path, "analyzer")
	var b: Dictionary = probe.evaluate_corpus(source.to_utf8_buffer(), path, "analyzer", sources)
	var again: Dictionary = probe.evaluate_corpus("func test():\n\tpass\n".to_utf8_buffer(), path, "analyzer")
	_expect(a == again and b == result, "A-B-A declaration fixture restoration")
	var missing: Dictionary = probe.evaluate_corpus(source.to_utf8_buffer(), path, "analyzer", PackedStringArray(["res://tests/oracle_fixtures/declarations/missing.barista"]))
	_expect(missing.infrastructure_error and not missing.ok, "missing fixture source cannot become semantic result")
