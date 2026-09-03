# global_class_test.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

extends SceneTree

## What a `.barista` file contributes to the engine's global class registry.
##
## Every row of the fail-closed contract in issue #11 is a fixture in
## `res://tests/global_class_fixtures/` and an entry in EXPECTED below, so the
## contract and the assertions cannot drift apart: a fixture with no entry, or an
## entry naming no fixture, fails the suite.
##
## The registry half of the suite reads back what the editor scan actually
## registered, so it needs `godot --headless --path project --editor --quit` to
## have run first -- which is exactly the order AGENTS.md documents and CI uses.

const SuiteGuard = preload("res://tests/suite_guard.gd")

const FIXTURE_DIRECTORY := "res://tests/global_class_fixtures"
const CLASS_CACHE_PATH := "res://.godot/global_script_class_cache.cfg"

## One row per fixture: what `bs_resolve_global_class()` must report for it.
##
## `kind_name` is `bs_declaration_kind_name()`'s spelling, not a second vocabulary; the suite
## checks every name here against the list the probe publishes, so a renamed kind fails loudly
## instead of silently matching nothing.
const EXPECTED := {
	# A namespaced global class: the qualified name, always.
	"namespaced_weapon.barista": {
		"parsed": true, "name": "app.combat.Weapon", "base_type": "Node",
		"icon_path": "res://icon.svg", "is_abstract": false, "is_tool": false,
		"kind_name": "class_name",
	},
	# The flat control: no namespace, so no leading dot and no empty segment.
	"flat_weapon.barista": {
		"parsed": true, "name": "FlatWeapon", "base_type": "Node",
		"icon_path": "", "is_abstract": false, "is_tool": false,
		"kind_name": "class_name",
	},
	# No `extends`: the implicit base.
	"implicit_base.barista": {
		"parsed": true, "name": "ImplicitBase", "base_type": "RefCounted",
		"icon_path": "", "is_abstract": false, "is_tool": false,
		"kind_name": "class_name",
	},
	# `abstract` on an instantiable kind: the flag comes from the source, the kind does not change.
	"abstract_weapon.barista": {
		"parsed": true, "name": "AbstractWeapon", "base_type": "Node",
		"icon_path": "", "is_abstract": true, "is_tool": false,
		"kind_name": "class_name",
	},
	"tool_weapon.barista": {
		"parsed": true, "name": "ToolWeapon", "base_type": "Node",
		"icon_path": "", "is_abstract": false, "is_tool": true,
		"kind_name": "class_name",
	},
	# A namespaced `enum_name` file: empty base and abstract, so it is a type and not a script.
	"damage_kind.barista": {
		"parsed": true, "name": "app.combat.DamageKind", "base_type": "",
		"icon_path": "", "is_abstract": true, "is_tool": false,
		"kind_name": "enum_name",
	},
	"grid_position.barista": {
		"parsed": true, "name": "GridPosition", "base_type": "",
		"icon_path": "", "is_abstract": true, "is_tool": false,
		"kind_name": "tuple_name",
	},
	"damageable.barista": {
		"parsed": true, "name": "Damageable", "base_type": "",
		"icon_path": "", "is_abstract": true, "is_tool": false,
		"kind_name": "trait_name",
	},
	# A generic `class_name`: named, but not the engine's to instantiate (GRAMMAR D6).
	"boxed.barista": {
		"parsed": true, "name": "Boxed", "base_type": "",
		"icon_path": "", "is_abstract": true, "is_tool": false,
		"kind_name": "generic class_name",
	},
	# No head declaration: a script, not a global class. The base is still known.
	"plain_script.barista": {
		"parsed": true, "name": "", "base_type": "Node",
		"icon_path": "", "is_abstract": false, "is_tool": false,
		"kind_name": "none",
	},
	# A head that does not parse: no name, and nothing else either.
	"broken_head.barista": {
		"parsed": false, "name": "", "base_type": "",
		"icon_path": "", "is_abstract": false, "is_tool": false,
		"kind_name": "none",
	},
	# A body that does not parse under a well-formed head. Resolution reads declarations only, as
	# stock does, so a class keeps its name while its body is mid-edit.
	"broken_body.barista": {
		"parsed": true, "name": "BrokenBody", "base_type": "Node",
		"icon_path": "", "is_abstract": false, "is_tool": false,
		"kind_name": "class_name",
	},
	# `enum_name` with `extends`: reported, and never with a base, which would make it instantiable.
	"enum_with_extends.barista": {
		"parsed": false, "name": "", "base_type": "",
		"icon_path": "", "is_abstract": false, "is_tool": false,
		"kind_name": "none",
	},
	# A namespace segment that is not an identifier: no name at all, never a partial dotted string.
	"invalid_namespace.barista": {
		"parsed": false, "name": "", "base_type": "",
		"icon_path": "", "is_abstract": false, "is_tool": false,
		"kind_name": "none",
	},
	"path_base.barista": {
		"parsed": true, "name": "PathBase", "base_type": "Node2D",
		"icon_path": "", "is_abstract": false, "is_tool": false,
		"kind_name": "class_name",
	},
	# `extends "res://…"`: the base file's own base, resolved without the analyzer.
	"path_derived.barista": {
		"parsed": true, "name": "PathDerived", "base_type": "Node2D",
		"icon_path": "", "is_abstract": false, "is_tool": false,
		"kind_name": "class_name",
	},
	# The same through a path relative to the extending file.
	"relative_derived.barista": {
		"parsed": true, "name": "RelativeDerived", "base_type": "Node2D",
		"icon_path": "", "is_abstract": false, "is_tool": false,
		"kind_name": "class_name",
	},
	# Two files that extend each other. Resolution terminates and reports no base.
	"cycle_a.barista": {
		"parsed": true, "name": "CycleA", "base_type": "",
		"icon_path": "", "is_abstract": false, "is_tool": false,
		"kind_name": "class_name",
	},
	"cycle_b.barista": {
		"parsed": true, "name": "CycleB", "base_type": "",
		"icon_path": "", "is_abstract": false, "is_tool": false,
		"kind_name": "class_name",
	},
}

## The fixture that proves each declaration kind is produced at all. Every kind the probe publishes
## must appear here, which is what makes the vocabulary closed rather than merely enumerated.
const KIND_WITNESS := {
	"none": "plain_script.barista",
	"class_name": "flat_weapon.barista",
	"generic class_name": "boxed.barista",
	"trait_name": "damageable.barista",
	"enum_name": "damage_kind.barista",
	"tuple_name": "grid_position.barista",
}

## What the engine must answer for a registered name once the editor has scanned.
const EXPECTED_INSTANTIABLE := {
	"app.combat.Weapon": true,
	"FlatWeapon": true,
	"ImplicitBase": true,
	"PathDerived": true,
	"RelativeDerived": true,
	"CycleA": true,
	"AbstractWeapon": false,
	"app.combat.DamageKind": false,
	"GridPosition": false,
	"Damageable": false,
	"Boxed": false,
}


func _initialize() -> void:
	var failures: Array[String] = []
	var probe: Object = ClassDB.instantiate("BaristaScriptGlobalClassProbe")
	if probe == null:
		failures.append("BaristaScriptGlobalClassProbe is not registered")
		quit(SuiteGuard.report("global_class_test", failures))
		return

	_check_fixture_coverage(failures)
	_check_resolutions(probe, failures)
	_check_language_agrees(probe, failures)
	_check_vocabulary_closure(probe, failures)
	_check_qualified_name_builder(probe, failures)
	_check_handled_type(probe, failures)
	_check_script_surface(failures)
	_check_registry(failures)
	_check_class_cache(failures)

	quit(SuiteGuard.report("global_class_test", failures))


## Every `.barista` fixture has an expectation and every expectation has a fixture.
func _check_fixture_coverage(failures: Array[String]) -> void:
	var directory := DirAccess.open(FIXTURE_DIRECTORY)
	if directory == null:
		failures.append("cannot open %s" % FIXTURE_DIRECTORY)
		return
	var present: Array[String] = []
	for file_name in directory.get_files():
		if file_name.ends_with(".barista"):
			present.append(file_name)
	for file_name in present:
		if not EXPECTED.has(file_name):
			failures.append("fixture %s has no expectation in EXPECTED" % file_name)
	for file_name in EXPECTED:
		if not present.has(file_name):
			failures.append("EXPECTED names %s, which is not a fixture" % file_name)


func _check_resolutions(probe: Object, failures: Array[String]) -> void:
	for file_name in EXPECTED:
		var path := "%s/%s" % [FIXTURE_DIRECTORY, file_name]
		var expected: Dictionary = EXPECTED[file_name]
		var report: Dictionary = probe.resolve_path(path)
		for key in expected:
			if report.get(key) != expected[key]:
				failures.append("%s: %s is %s, expected %s" % [
					file_name, key, var_to_str(report.get(key)), var_to_str(expected[key])])

		# Idempotency: unchanged source, identical report. A second resolution that differed would
		# mean the first one left something behind.
		var repeated: Dictionary = probe.resolve_path(path)
		if repeated != report:
			failures.append("%s: a second resolution differs from the first" % file_name)

		# The same source text, handed over directly, must resolve the same way a file does. This is
		# the path `BaristaScript` itself takes for the source it is holding.
		var source := FileAccess.get_file_as_string(path)
		var from_source: Dictionary = probe.resolve_source(source, path)
		if from_source != report:
			failures.append("%s: resolving the source text differs from resolving the file" % file_name)

		# The instantiability predicate, consumed. Nothing that is not instantiable may carry a base
		# or claim to be concrete: those are the two gates that keep it out of the Create Node dialog.
		var kind: int = report["kind"]
		if not probe.declaration_kind_is_instantiable(kind):
			if report["base_type"] != "":
				failures.append("%s: a %s declaration reports base %s" % [
					file_name, report["kind_name"], report["base_type"]])
			if not report["is_abstract"]:
				failures.append("%s: a %s declaration is not abstract" % [file_name, report["kind_name"]])

		# A name is either empty or fully qualified; a partially built dotted string is neither.
		var name: String = report["name"]
		if name.begins_with(".") or name.ends_with(".") or name.contains(".."):
			failures.append("%s: malformed qualified name %s" % [file_name, var_to_str(name)])


## The registered language reports exactly what the resolver resolved.
func _check_language_agrees(probe: Object, failures: Array[String]) -> void:
	for file_name in EXPECTED:
		var path := "%s/%s" % [FIXTURE_DIRECTORY, file_name]
		var reported: Dictionary = probe.language_global_class_name(path)
		var resolved: Dictionary = probe.resolve_path(path)
		for key in ["name", "base_type", "icon_path", "is_abstract", "is_tool"]:
			if reported.get(key) != resolved.get(key):
				failures.append("%s: the language reports %s = %s, the resolver %s" % [
					file_name, key, var_to_str(reported.get(key)), var_to_str(resolved.get(key))])
		# The engine reads exactly these five keys and nothing else
		# (core/object/script_language_extension.h:657 at 4.7.2-stable).
		var keys := reported.keys()
		keys.sort()
		if keys != ["base_type", "icon_path", "is_abstract", "is_tool", "name"]:
			failures.append("%s: the reported dictionary has keys %s" % [file_name, var_to_str(keys)])


## Every declaration kind is named once, answered by every consumer, and produced by a fixture.
func _check_vocabulary_closure(probe: Object, failures: Array[String]) -> void:
	var names: PackedStringArray = probe.declaration_kind_names()
	if names.is_empty():
		failures.append("the declaration-kind vocabulary is empty")
		return

	var seen: Array[String] = []
	for index in names.size():
		var kind_name := names[index]
		if kind_name.is_empty():
			failures.append("declaration kind %d has no name" % index)
		if seen.has(kind_name):
			failures.append("declaration kind name %s is used twice" % kind_name)
		seen.append(kind_name)
		if not probe.declaration_kind_index_is_valid(index):
			failures.append("declaration kind %d is not accepted as a kind" % index)
		# The predicate must answer for every value; an unanswered one would be a fall-through.
		var instantiable: bool = probe.declaration_kind_is_instantiable(index)
		if instantiable and kind_name != "none" and kind_name != "class_name":
			failures.append("%s is reported instantiable" % kind_name)
		if not KIND_WITNESS.has(kind_name):
			failures.append("no fixture witnesses the declaration kind %s" % kind_name)
			continue
		var witness: String = KIND_WITNESS[kind_name]
		var report: Dictionary = probe.resolve_path("%s/%s" % [FIXTURE_DIRECTORY, witness])
		if report["kind_name"] != kind_name:
			failures.append("%s was meant to witness %s but resolves to %s" % [
				witness, kind_name, report["kind_name"]])

	for kind_name in KIND_WITNESS:
		if not seen.has(kind_name):
			failures.append("KIND_WITNESS names %s, which is not a declaration kind" % kind_name)

	# `MAX` is the enumerator count and every consumer refuses it.
	if probe.declaration_kind_index_is_valid(names.size()):
		failures.append("the enumerator count is accepted as a declaration kind")
	if probe.declaration_kind_index_is_valid(-1):
		failures.append("-1 is accepted as a declaration kind")


func _check_qualified_name_builder(probe: Object, failures: Array[String]) -> void:
	var cases := [
		["app.combat", "Weapon", "app.combat.Weapon"],
		["", "Weapon", "Weapon"],
		["app", "Weapon", "app.Weapon"],
		# A namespace with no identifier is not a global class, so it has no name.
		["app.combat", "", ""],
		["", "", ""],
	]
	for case in cases:
		var built: String = probe.build_qualified_global_name(case[0], case[1])
		if built != case[2]:
			failures.append("qualified name of (%s, %s) is %s, expected %s" % [
				var_to_str(case[0]), var_to_str(case[1]), var_to_str(built), var_to_str(case[2])])


func _check_handled_type(probe: Object, failures: Array[String]) -> void:
	# The editor asks the language whether it handles the resource type its scan recorded, which for
	# a `.barista` file is what the resource loader returns. If the two spellings disagree the class
	# is dropped without a word.
	if not probe.language_handles_global_class_type("BaristaScript"):
		failures.append("the language does not claim the BaristaScript global class type")
	for other in ["GDScript", "Script", "", "baristascript"]:
		if probe.language_handles_global_class_type(other):
			failures.append("the language claims the global class type %s" % var_to_str(other))
	# The coupling itself: whatever class a loaded `.barista` resource is, the language must claim
	# that type, because the editor's scan records exactly that name and looks the language up by it.
	var loaded: Object = ResourceLoader.load("%s/flat_weapon.barista" % FIXTURE_DIRECTORY)
	if loaded == null:
		failures.append("flat_weapon.barista did not load")
	elif not probe.language_handles_global_class_type(loaded.get_class()):
		failures.append("the language does not claim the loaded resource type %s" % loaded.get_class())


## What the script object itself answers, which is the gate the editor consults.
func _check_script_surface(failures: Array[String]) -> void:
	var expectations := {
		"namespaced_weapon.barista": ["app.combat.Weapon", false],
		"flat_weapon.barista": ["FlatWeapon", false],
		"damage_kind.barista": ["app.combat.DamageKind", true],
		"damageable.barista": ["Damageable", true],
		"grid_position.barista": ["GridPosition", true],
		"boxed.barista": ["Boxed", true],
		"abstract_weapon.barista": ["AbstractWeapon", true],
		"plain_script.barista": ["", false],
		"broken_head.barista": ["", false],
	}
	for file_name in expectations:
		var script: Object = ResourceLoader.load("%s/%s" % [FIXTURE_DIRECTORY, file_name])
		if script == null:
			failures.append("%s did not load" % file_name)
			continue
		var expected: Array = expectations[file_name]
		if str(script.get_global_name()) != expected[0]:
			failures.append("%s: the script's global name is %s, expected %s" % [
				file_name, var_to_str(str(script.get_global_name())), var_to_str(expected[0])])
		if script.is_abstract() != expected[1]:
			failures.append("%s: the script reports is_abstract %s" % [file_name, script.is_abstract()])
		# Instance creation is M4's; nothing here may start offering it.
		if script.can_instantiate():
			failures.append("%s: the script claims it can instantiate" % file_name)


## Read back what the editor's scan registered. Needs the editor import to have run.
func _check_registry(failures: Array[String]) -> void:
	if not FileAccess.file_exists(CLASS_CACHE_PATH):
		failures.append(
			"%s does not exist; run `godot --headless --path project --editor --quit` first"
			% CLASS_CACHE_PATH)
		return
	for class_name_key in EXPECTED_INSTANTIABLE:
		var expected: bool = EXPECTED_INSTANTIABLE[class_name_key]
		var actual: bool = ClassDB.can_instantiate(class_name_key)
		if actual != expected:
			failures.append("ClassDB.can_instantiate(%s) is %s, expected %s" % [
				class_name_key, actual, expected])


## The qualified name survives to the editor's own class cache, keyed and based as reported.
func _check_class_cache(failures: Array[String]) -> void:
	if not FileAccess.file_exists(CLASS_CACHE_PATH):
		return # Already reported by _check_registry.
	var config := ConfigFile.new()
	if config.load(CLASS_CACHE_PATH) != OK:
		failures.append("%s did not parse as a ConfigFile" % CLASS_CACHE_PATH)
		return
	var entries: Array = config.get_value("", "list", [])
	var by_name := {}
	for entry in entries:
		by_name[str(entry.get("class", ""))] = entry

	# Only the names the resolver reported may be here, and every one of them must be.
	for file_name in EXPECTED:
		var expected: Dictionary = EXPECTED[file_name]
		var expected_name: String = expected["name"]
		if expected_name.is_empty():
			continue
		if not by_name.has(expected_name):
			failures.append("%s is missing from %s" % [expected_name, CLASS_CACHE_PATH])
			continue
		var entry: Dictionary = by_name[expected_name]
		if str(entry.get("base", "")) != expected["base_type"]:
			failures.append("%s is cached with base %s, expected %s" % [
				expected_name, var_to_str(str(entry.get("base", ""))), var_to_str(expected["base_type"])])
		if bool(entry.get("is_abstract", false)) != expected["is_abstract"]:
			failures.append("%s is cached with is_abstract %s" % [expected_name, entry.get("is_abstract")])
		if bool(entry.get("is_tool", false)) != expected["is_tool"]:
			failures.append("%s is cached with is_tool %s" % [expected_name, entry.get("is_tool")])
		if str(entry.get("language", "")) != "BaristaScript":
			failures.append("%s is cached under language %s" % [expected_name, entry.get("language")])
		if str(entry.get("path", "")) != "%s/%s" % [FIXTURE_DIRECTORY, file_name]:
			failures.append("%s is cached at path %s" % [expected_name, entry.get("path")])

	# A file that reported nothing must have contributed nothing.
	for file_name in EXPECTED:
		if not (EXPECTED[file_name]["name"] as String).is_empty():
			continue
		for cached_name in by_name:
			var entry: Dictionary = by_name[cached_name]
			if str(entry.get("path", "")) == "%s/%s" % [FIXTURE_DIRECTORY, file_name]:
				failures.append("%s contributed the global class %s" % [file_name, cached_name])
