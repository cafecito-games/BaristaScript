# warning_registry_test.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT
extends SceneTree

## Behavioural tests for the warning registry (`src/bs_warning.{h,cpp}`).
##
## godot-cpp's `String` and `Variant` are engine-backed, so there is no standalone
## C++ test binary in this repository and every assertion here runs inside a
## loaded Godot runtime against `BaristaScriptWarningRegistry`, the registry's
## only binding.
##
## The suite is parametrized over the whole vocabulary rather than over a chosen
## sample: it asks the registry how many codes exist and then asserts the same
## properties for every one of them, so adding an enumerator without a table
## entry fails here even when it compiles.

# `BSWarning::WarnLevel`.
const LEVEL_IGNORE := 0
const LEVEL_WARN := 1
const LEVEL_ERROR := 2

# `BSWarning::LevelSource`.
const LEVEL_FROM_DECLARED_DEFAULT := 0
const LEVEL_FROM_PROJECT_SETTING := 1
const LEVEL_SETTING_MALFORMED := 2

const SETTING_PREFIX := "debug/barista_script/warnings/"

## The five keys stock Godot 4.7 reads out of a validate() warning dictionary
## (godotengine/godot 4.7-stable, core/object/script_language_extension.h:341-352).
## Columns are deliberately absent; see the decision in src/bs_warning.h.
const VALIDATE_KEYS := ["code", "end_line", "message", "start_line", "string_code"]

var _failures: Array[String] = []


func _initialize() -> void:
	var registry: Object = ClassDB.instantiate("BaristaScriptWarningRegistry")
	if registry == null:
		push_error("BaristaScriptWarningRegistry is not registered; the debug build did not load")
		quit(1)
		return

	var count: int = registry.get_warning_count()
	_check(count > 0, "the registry declares no warning codes")

	_test_vocabulary_closure(registry, count)
	_test_name_round_trip_rejects_strangers(registry, count)
	_test_out_of_range_codes_resolve_to_nothing(registry, count)
	_test_absent_setting_falls_back_to_the_declared_default(registry, count)
	_test_malformed_setting_fails_loudly(registry, count)
	_test_well_formed_setting_overrides_the_default(registry, count)
	_test_project_settings_path_round_trip(registry)
	_test_position_validation(registry)
	_test_validate_dictionary_narrows_at_the_boundary(registry)
	_test_validate_dictionary_refuses_a_position_outside_the_source(registry)
	_test_ordering_is_deterministic_on_one_line(registry)
	_test_ordering_is_idempotent(registry, count)
	_test_confusable_identifier_check(registry)

	for failure in _failures:
		push_error(failure)
	if _failures.is_empty():
		print("BS_WARNING_REGISTRY_OK %d codes" % count)
	else:
		print("BS_WARNING_REGISTRY_FAILED %d failure(s)" % _failures.size())
	quit(0 if _failures.is_empty() else 1)


func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures.append(message)


## Every enumerator resolves to a name, a message, a setting path, a default
## level and a property descriptor -- and each of those is unique where it must
## be. This is the parametrized test the milestone asks for: it walks the whole
## enumeration, so a code the tables forgot cannot hide behind a sampled one.
func _test_vocabulary_closure(registry: Object, count: int) -> void:
	var seen_names := {}
	var seen_setting_paths := {}
	var symbols := PackedStringArray(["S0", "S1", "S2", "S3", "S4", "S5"])

	for code in range(count):
		var name: String = registry.get_name_from_code(code)
		_check(not name.is_empty(), "code %d has no name" % code)
		_check(not seen_names.has(name), "name %s is used by more than one code" % name)
		seen_names[name] = code
		_check(name == name.to_upper(), "name %s is not upper-case" % name)

		_check(
			registry.get_code_from_name(name) == code,
			"name %s does not round-trip back to code %d" % [name, code]
		)

		var setting_path: String = registry.get_setting_path_from_code(code)
		_check(
			setting_path == SETTING_PREFIX + name.to_lower(),
			"code %s has setting path %s" % [name, setting_path]
		)
		_check(
			not seen_setting_paths.has(setting_path),
			"setting path %s is used by more than one code" % setting_path
		)
		seen_setting_paths[setting_path] = code

		var default_level: int = registry.get_default_level(code)
		_check(
			default_level >= LEVEL_IGNORE and default_level <= LEVEL_ERROR,
			"code %s has default level %d, which is not a level" % [name, default_level]
		)

		var message: String = registry.get_message(code, symbols)
		_check(not message.is_empty(), "code %s has no message" % name)
		_check(
			not message.contains("%s"),
			"code %s left an unfilled placeholder: %s" % [name, message]
		)

		var property_info: Dictionary = registry.get_property_info(code)
		_check(
			property_info.get("name", "") == setting_path,
			"code %s describes property %s" % [name, property_info.get("name", "")]
		)
		_check(
			property_info.get("type", -1) == TYPE_INT,
			"code %s describes a non-integer setting" % name
		)
		_check(
			property_info.get("hint", -1) == PROPERTY_HINT_ENUM,
			"code %s does not describe its setting as an enum" % name
		)
		_check(
			property_info.get("hint_string", "") == "Ignore,Warn,Error",
			"code %s offers levels %s" % [name, property_info.get("hint_string", "")]
		)

	_check(seen_names.size() == count, "the registry named %d of %d codes" % [seen_names.size(), count])


## A name the registry does not own resolves to WARNING_MAX -- the "no such
## warning" sentinel -- rather than to code 0 or to a plausible neighbour.
func _test_name_round_trip_rejects_strangers(registry: Object, count: int) -> void:
	for stranger in ["", "unassigned_variable", "UNASSIGNED_VARIABLE ", "NOT_A_WARNING", "0"]:
		_check(
			registry.get_code_from_name(stranger) == count,
			"unknown name %s resolved to a real code" % var_to_str(stranger)
		)


## Fail-closed: a code outside the enumeration produces empty text, never an
## invented "unknown warning" string that would read like a real diagnostic.
func _test_out_of_range_codes_resolve_to_nothing(registry: Object, count: int) -> void:
	var symbols := PackedStringArray(["S0", "S1", "S2", "S3", "S4", "S5"])
	for code in [-1, count, count + 1]:
		_check(
			registry.get_name_from_code(code) == "",
			"out-of-range code %d produced a name" % code
		)
		_check(
			registry.get_message(code, symbols) == "",
			"out-of-range code %d produced a message" % code
		)
		var resolution: Dictionary = registry.resolve_level(code, null)
		_check(
			resolution.get("source", -1) == LEVEL_SETTING_MALFORMED,
			"out-of-range code %d resolved to a level" % code
		)


## Fail-closed: an absent setting falls back to the declared default, and the
## default it falls back to is the one the table declares -- not IGNORE, and not
## whatever the previous code happened to resolve to.
func _test_absent_setting_falls_back_to_the_declared_default(registry: Object, count: int) -> void:
	for code in range(count):
		var resolution: Dictionary = registry.resolve_level(code, null)
		_check(
			resolution.get("source", -1) == LEVEL_FROM_DECLARED_DEFAULT,
			"code %s did not fall back to its default" % registry.get_name_from_code(code)
		)
		_check(
			resolution.get("level", -1) == registry.get_default_level(code),
			"code %s fell back to a level other than its declared default" % registry.get_name_from_code(code)
		)


## Fail-closed: a setting that exists but is not a level is malformed. It must
## not be coerced to the default, so the resolution carries no level at all.
func _test_malformed_setting_fails_loudly(registry: Object, count: int) -> void:
	var malformed := [
		"warn",
		"1",
		1.5,
		true,
		-1,
		LEVEL_ERROR + 1,
		Vector2i(1, 1),
		[],
		{},
	]
	for code in range(count):
		for setting in malformed:
			var resolution: Dictionary = registry.resolve_level(code, setting)
			_check(
				resolution.get("source", -1) == LEVEL_SETTING_MALFORMED,
				"code %s accepted %s as a level" % [registry.get_name_from_code(code), var_to_str(setting)]
			)
			_check(
				not resolution.has("level"),
				"code %s produced a level from the malformed setting %s"
				% [registry.get_name_from_code(code), var_to_str(setting)]
			)


func _test_well_formed_setting_overrides_the_default(registry: Object, count: int) -> void:
	for code in range(count):
		for level in [LEVEL_IGNORE, LEVEL_WARN, LEVEL_ERROR]:
			var resolution: Dictionary = registry.resolve_level(code, level)
			_check(
				resolution.get("source", -1) == LEVEL_FROM_PROJECT_SETTING,
				"code %s ignored its setting" % registry.get_name_from_code(code)
			)
			_check(
				resolution.get("level", -1) == level,
				"code %s resolved to a level other than its setting" % registry.get_name_from_code(code)
			)


## The same three outcomes, this time against the live ProjectSettings rather
## than an injected Variant, because that is where the setting really lives.
func _test_project_settings_path_round_trip(registry: Object) -> void:
	var code: int = registry.get_code_from_name("UNUSED_VARIABLE")
	_check(code != registry.get_warning_count(), "UNUSED_VARIABLE is not a registered code")
	var setting_path: String = registry.get_setting_path_from_code(code)
	var had_setting := ProjectSettings.has_setting(setting_path)
	var previous: Variant = ProjectSettings.get_setting(setting_path) if had_setting else null

	ProjectSettings.set_setting(setting_path, null)
	var absent: Dictionary = registry.resolve_level_from_project_settings(code)
	_check(
		absent.get("source", -1) == LEVEL_FROM_DECLARED_DEFAULT,
		"an absent project setting did not fall back to the declared default"
	)
	_check(
		absent.get("level", -1) == registry.get_default_level(code),
		"an absent project setting produced a level other than the declared default"
	)

	ProjectSettings.set_setting(setting_path, LEVEL_ERROR)
	var present: Dictionary = registry.resolve_level_from_project_settings(code)
	_check(
		present.get("source", -1) == LEVEL_FROM_PROJECT_SETTING,
		"a well-formed project setting was ignored"
	)
	_check(present.get("level", -1) == LEVEL_ERROR, "a well-formed project setting resolved to the wrong level")

	# The registry reports this one loudly; the error on stderr is the test passing.
	ProjectSettings.set_setting(setting_path, "error")
	var malformed: Dictionary = registry.resolve_level_from_project_settings(code)
	_check(
		malformed.get("source", -1) == LEVEL_SETTING_MALFORMED,
		"a malformed project setting was accepted"
	)
	_check(not malformed.has("level"), "a malformed project setting still produced a level")

	ProjectSettings.set_setting(setting_path, previous if had_setting else null)


## Fail-closed: a span outside the source is rejected rather than clamped.
func _test_position_validation(registry: Object) -> void:
	var code: int = registry.get_code_from_name("EMPTY_FILE")
	# start_line, start_column, end_line, end_column, source_line_count, expected
	var cases := [
		[1, 1, 1, 2, 1, true],
		[1, 1, 3, 5, 3, true],
		[3, 4, 3, 9, 3, true],
		[0, 1, 1, 2, 3, false],
		[-1, 1, 1, 2, 3, false],
		[4, 1, 4, 2, 3, false],
		[2, 1, 1, 2, 3, false],
		[1, 1, 4, 2, 3, false],
		[1, 0, 1, 2, 3, false],
		[1, -1, 1, 2, 3, false],
		[1, 1, 1, 0, 3, false],
		[1, 5, 1, 5, 3, false],
		[1, 5, 1, 4, 3, false],
		[1, 1, 1, 2, 0, false],
		[1, 1, 1, 2, -1, false],
		[-1, -1, -1, -1, 3, false],
	]
	for case in cases:
		var actual: bool = registry.has_valid_position(code, case[0], case[1], case[2], case[3], case[4])
		_check(
			actual == case[5],
			"span lines %d-%d columns %d-%d over %d line(s) was judged %s"
			% [case[0], case[2], case[1], case[3], case[4], "valid" if actual else "invalid"]
		)


## The engine boundary: exactly the five keys stock Godot reads, and the column
## span dropped rather than smuggled through under an invented key.
func _test_validate_dictionary_narrows_at_the_boundary(registry: Object) -> void:
	var code: int = registry.get_code_from_name("UNUSED_VARIABLE")
	var warning := {
		"code": code,
		"start_line": 2,
		"start_column": 5,
		"end_line": 2,
		"end_column": 12,
		"symbols": PackedStringArray(["cup"]),
	}
	var emitted: Dictionary = registry.to_validate_dictionary(warning, 4)
	var keys := emitted.keys()
	keys.sort()
	_check(keys == VALIDATE_KEYS, "the validate dictionary carries keys %s" % str(keys))
	_check(emitted.get("start_line", -1) == 2, "the validate dictionary lost the start line")
	_check(emitted.get("end_line", -1) == 2, "the validate dictionary lost the end line")
	_check(emitted.get("code", -1) == code, "the validate dictionary lost the code")
	_check(
		emitted.get("string_code", "") == "UNUSED_VARIABLE",
		"the validate dictionary carries string_code %s" % emitted.get("string_code", "")
	)
	_check(
		emitted.get("message", "").contains("\"cup\""),
		"the validate dictionary did not fill the message symbols: %s" % emitted.get("message", "")
	)


func _test_validate_dictionary_refuses_a_position_outside_the_source(registry: Object) -> void:
	var code: int = registry.get_code_from_name("UNUSED_VARIABLE")
	# The registry reports this one loudly; the error on stderr is the test passing.
	var emitted: Dictionary = registry.to_validate_dictionary(
		{
			"code": code,
			"start_line": 9,
			"start_column": 1,
			"end_line": 9,
			"end_column": 2,
			"symbols": PackedStringArray(["cup"]),
		},
		4
	)
	_check(emitted.is_empty(), "a warning past the end of the source was emitted anyway: %s" % str(emitted))


## Determinism: several warnings on one line sort by (line, column, code) and
## land in that order regardless of the order they were raised in.
func _test_ordering_is_deterministic_on_one_line(registry: Object) -> void:
	var unused_variable: int = registry.get_code_from_name("UNUSED_VARIABLE")
	var shadowed_variable: int = registry.get_code_from_name("SHADOWED_VARIABLE")
	var integer_division: int = registry.get_code_from_name("INTEGER_DIVISION")
	var standalone_expression: int = registry.get_code_from_name("STANDALONE_EXPRESSION")

	var first := _warning(unused_variable, 7, 3, 7, 9)
	var second := _warning(standalone_expression, 7, 3, 7, 20)
	var third := _warning(shadowed_variable, 7, 11, 7, 18)
	var fourth := _warning(integer_division, 7, 24, 7, 31)
	var fifth := _warning(unused_variable, 8, 1, 8, 4)

	# UNUSED_VARIABLE and STANDALONE_EXPRESSION share line 7 column 3, so only the
	# code separates them; INTEGER_DIVISION sorts before SHADOWED_VARIABLE by code
	# but after it by column, which is what makes the column load-bearing.
	var expected := [first, second, third, fourth, fifth]

	var permutations := [
		[first, second, third, fourth, fifth],
		[fifth, fourth, third, second, first],
		[third, first, fifth, second, fourth],
		[fourth, fifth, first, third, second],
		[second, third, fourth, fifth, first],
	]
	for permutation in permutations:
		var sorted_warnings: Array = registry.sort_warnings(permutation)
		_check(
			_order_of(sorted_warnings) == _order_of(expected),
			"permutation %s sorted to %s" % [_order_of(permutation), _order_of(sorted_warnings)]
		)


## Idempotency: the registry answers the same questions the same way twice, and
## sorting an already-sorted list changes nothing.
func _test_ordering_is_idempotent(registry: Object, count: int) -> void:
	var symbols := PackedStringArray(["S0", "S1", "S2", "S3", "S4", "S5"])
	var first_pass: Array[String] = []
	var second_pass: Array[String] = []
	var warnings: Array = []
	for code in range(count):
		first_pass.append("%s|%s|%s|%d" % [
			registry.get_name_from_code(code),
			registry.get_setting_path_from_code(code),
			registry.get_message(code, symbols),
			registry.get_default_level(code),
		])
		# One warning per code, all on the same line and column, so the code is the
		# only key that can separate them.
		warnings.append(_warning(count - 1 - code, 5, 2, 5, 6))
	for code in range(count):
		second_pass.append("%s|%s|%s|%d" % [
			registry.get_name_from_code(code),
			registry.get_setting_path_from_code(code),
			registry.get_message(code, symbols),
			registry.get_default_level(code),
		])
	_check(first_pass == second_pass, "the registry answered differently on a second pass")

	var sorted_once: Array = registry.sort_warnings(warnings)
	var sorted_twice: Array = registry.sort_warnings(sorted_once)
	_check(_order_of(sorted_once) == _order_of(sorted_twice), "sorting a sorted list reordered it")

	var ascending: Array[int] = []
	for code in range(count):
		ascending.append(code)
	_check(_order_of(sorted_once) == ascending, "warnings on one column did not sort by code")


## The reinstated TextServer confusable check. The predicate is fail-open by
## design, so the assertion is conditional on the host actually offering the
## feature -- but which branch ran is reported either way.
func _test_confusable_identifier_check(registry: Object) -> void:
	var available: bool = registry.unicode_security_available()
	if not available:
		print("BS_WARNING_REGISTRY_NOTE TextServer has no FEATURE_UNICODE_SECURITY on this host")
		_check(
			not registry.is_confusable_identifier("user"),
			"the confusable check reported a spoof with the feature unavailable"
		)
		return
	print("BS_WARNING_REGISTRY_NOTE TextServer offers FEATURE_UNICODE_SECURITY")
	_check(not registry.is_confusable_identifier("user"), "a plain ASCII identifier was called a spoof")
	_check(not registry.is_confusable_identifier("_brew_count"), "a plain ASCII identifier was called a spoof")
	# "usеr" carries a Cyrillic "е" (U+0435) where the Latin "e" belongs.
	_check(
		registry.is_confusable_identifier("usеr"),
		"a mixed-script identifier was not called a spoof"
	)


func _warning(code: int, start_line: int, start_column: int, end_line: int, end_column: int) -> Dictionary:
	return {
		"code": code,
		"start_line": start_line,
		"start_column": start_column,
		"end_line": end_line,
		"end_column": end_column,
		"symbols": PackedStringArray(),
	}


func _order_of(warnings: Array) -> Array[int]:
	var order: Array[int] = []
	for warning in warnings:
		order.append(warning["code"])
	return order
