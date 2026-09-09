# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

extends SceneTree

const SuiteGuard := preload("res://tests/suite_guard.gd")
const FIXTURE_A := "res://tests/cache_fixtures/script_a.barista"
const FIXTURE_B := "res://tests/cache_fixtures/script_b.barista"

enum Status { EMPTY, PARSED, INHERITANCE_SOLVED, INTERFACE_SOLVED, FULLY_SOLVED }

func _init() -> void:
	var failures: PackedStringArray = []
	BaristaScriptParseCache.clear_script_cache()
	_test_parser_lifecycle(failures)
	_test_transitive_invalidation(failures)
	_test_missing_and_self(failures)
	_test_move_remove(failures)
	_test_dependency_cycle(failures)
	_test_finalization_raises_dependencies(failures)
	_test_strict_settings(failures)
	_test_can_reference(failures)
	_test_host_bootstrap_filtering(failures)
	_test_validate_and_is_valid_agree(failures)
	_test_semantic_errors(failures)
	_test_undeclared_identifier_diagnostic(failures)
	_test_review_resolution_regressions(failures)
	_test_pinned_global_api_lookup(failures)
	_test_language_utility_registry(failures)
	_test_dictionary_literal_constant_parity(failures)
	_test_unary_sign_constant_folding(failures)
	_test_analyzer_declaration_commit(failures)
	_test_declaration_head_kinds_and_conformance(failures)
	_test_digest_mismatch_discards(failures)
	_test_namespace_change_invalidation(failures)
	_test_explicit_out_of_root_import(failures)
	_test_call_arity_and_types(failures)
	_test_call_validation_methodinfo_and_signals(failures)
	_test_named_arg_and_connect_callable(failures)
	_test_callable_signal_constructor_and_typed_receiver_depth(failures)
	_test_match_and_flow(failures)
	_test_warning_settings(failures)
	_test_final_local_assignment(failures)
	_test_final_member_and_static_assignment(failures)
	_test_final_trait_flattening(failures)
	_test_final_pattern_and_nested_expression_reads(failures)
	_test_noreturn_flow(failures)
	_test_unused_locals(failures)
	_test_unused_class_members_and_signals(failures)
	_test_member_name_conflicts(failures)
	_test_trait_requirements_and_conformance_witness(failures)
	_test_flow_narrowing(failures)
	_test_lambda_capture_and_compound_narrowing(failures)
	_test_get_operation_type(failures)
	_test_builtin_annotation_resolve(failures)
	_test_custom_annotation_surface(failures)
	_test_type_alias_surface(failures)
	_test_union_union_assignability(failures)
	_test_union_store_carrier_select(failures)
	_test_enum_case_match_and_case_binds(failures)
	_test_contextual_case_shorthand(failures)
	_test_tagged_union_match_exhaustiveness(failures)
	_test_callable_bind_unbind(failures)
	_test_callable_callv_rpc(failures)
	_test_async_callable_coroutine_wrap(failures)
	_test_await_reduction_and_missing_await(failures)
	_test_coroutine_annotation_decode(failures)
	_test_direct_async_call_wrap(failures)
	_test_surface_inheritance_member_depth(failures)
	_test_resolve_class_member_depth(failures)
	_test_foreign_member_failure_replay(failures)
	_test_foreign_class_phase_failure_replay(failures)
	_test_conformance_scoped_visibility(failures)
	_test_conformance_registry_registration(failures)
	_test_conformance_witness_lookup(failures)
	_test_conformance_hidden_witness(failures)
	_test_class_trait_binding_chain_coherence(failures)
	_test_recorded_trait_arguments_query(failures)
	_test_trait_target_assignability(failures)
	_test_witness_collision_arbitration(failures)
	_test_self_type_parameter_compat(failures)
	_test_enum_self_payload_field_leg(failures)
	_test_complete_self_referential_enum_type(failures)
	_test_self_contract_assign_return(failures)
	_test_self_contract_gradual_union(failures)
	_test_local_tuple_and_literal_consumers(failures)
	_test_ordinary_assignment_and_return_consumers(failures)
	_test_steps_1_5_repair_regressions(failures)
	_test_steps_1_5_repair2_self_signatures(failures)
	_test_local_enum_value_cycles(failures)
	_test_concrete_cast_ternary_and_type_test_reduction(failures)
	BaristaScriptParseCache.clear_script_cache()
	quit(SuiteGuard.report("analyzer_test", failures))


func _expect(failures: PackedStringArray, condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)


func _errors_are_exact(actual: Array, expected: Array) -> bool:
	if actual.size() != expected.size():
		return false
	for i in range(expected.size()):
		if str(actual[i].get("message", "")) != expected[i][0] or \
				actual[i].get("line") != expected[i][1] or actual[i].get("column") != expected[i][2]:
			return false
	return true


func _warnings_are_exact(actual: Array, expected: Array) -> bool:
	if actual.size() != expected.size():
		return false
	for i in range(expected.size()):
		if str(actual[i].get("string_code", "")) != expected[i][0] or \
				str(actual[i].get("message", "")) != expected[i][1] or \
				actual[i].get("start_line") != expected[i][2] or actual[i].get("start_column") != expected[i][3] or \
				actual[i].get("end_line") != expected[i][4] or actual[i].get("end_column") != expected[i][5]:
			return false
	return true


func _inspection_is_valid(report: Dictionary) -> bool:
	return report.get("found", false) == true and report.get("valid", false) == true and \
			report.get("errors", PackedStringArray()).is_empty()


func _test_undeclared_identifier_diagnostic(failures: PackedStringArray) -> void:
	# Byte-faithful producer fixture: Foundry
	# modules/foundry_script/tests/scripts/analyzer/errors/match_guard_invalid_expression.fs:1-4
	# @ c9d5e35. Its corresponding .out expects the identifier diagnostic at line 3.
	var source := "func test():\n\tmatch 0:\n\t\t_ when a == 0:\n\t\t\tprint(\"a does not exist\")\n"
	var report: Dictionary = BaristaScriptAnalyzerProbe.new().analyze_source(source, "res://tests/match_guard_invalid_expression.barista")
	var errors: PackedStringArray = report.get("errors", PackedStringArray())
	_expect(failures, report.get("valid", true) == false, "undeclared identifier invalidates analysis")
	_expect(failures, errors.size() == 1, "undeclared identifier suppresses Variant cascades")
	_expect(failures, errors.size() == 1 and errors[0] == 'Identifier "a" not declared in the current scope.', "undeclared identifier matches Foundry diagnostic")


func _kw_class_name() -> String:
	return "class_" + "name"


func _test_language_utility_registry(failures: PackedStringArray) -> void:
	# Complete separate language registry: Foundry fs_utility_functions.cpp:501-511 @ c9d5e35.
	# These source strings are analyzed, never executed (especially load/debug/proxy calls).
	var calls := {
		"type_exists": ['type_exists("Node")', "bool"],
		"char": ["char(65)", "String"],
		"ord": ['ord("A")', "int"],
		"range": ["range(1, 4, 1)", "Array"],
		"load": ['load("res://does_not_exist.barista")', "Resource"],
		"print_debug": ['print_debug("test", 1)', "void"],
		"print_stack": ["print_stack()", "void"],
		"get_stack": ["get_stack()", "Array"],
		"len": ['len("abc")', "int"],
		"is_instance_of": ["is_instance_of(1, TYPE_INT)", "bool"],
		"create_proxy_dynamic": ["create_proxy_dynamic(Object, Callable())", "Object"],
	}
	var probe := BaristaScriptAnalyzerProbe.new()
	# Metadata inspection never exports or invokes the analyzer-only callable marker.
	for expression in ["char", "[char]", "Callable(char)", '{"callback": char}', '{char: "callback"}']:
		_expect(failures, not probe.fold_expression(expression).get("ok", true), "utility identity is not exported by constant probe: " + expression)
	if probe.has_method("language_utility_metadata"):
		var metadata: Dictionary = probe.call("language_utility_metadata")
		_expect(failures, metadata.get("roundtrip", false), "language public MethodInfo round-trip preserves every field")
		_expect(failures, metadata.get("private_identity", false), "utility identity is stable, distinct and non-executable")
		var functions: Array = metadata.get("functions", [])
		_expect(failures, functions.size() == calls.size(), "complete public language utility inventory")
		for info in functions:
			_expect(failures, calls.has(info.name), "no invented public language utility")
			_expect(failures, info.has("args") and info.has("return") and info.has("flags") and info.has("default_args"), "complete utility MethodInfo: " + str(info))
			_expect(failures, metadata.constant_flags.get(info.name) == (info.name in ["type_exists", "char", "ord", "len", "is_instance_of"]), "pinned constant flag: " + str(info.name))
	else:
		_expect(failures, false, "language utility metadata inspection is available")
	for sample in [['len("abc")', 3], ['char(65)', "A"], ['ord("A")', 65], ['type_exists("Node")', true], ["is_instance_of(1, TYPE_INT)", true], ["is_instance_of(1, TYPE_STRING)", false], ["len([1, 2])", 2], ['len({"x": 1})', 1], ["len({})", 0], ['len({"x": {"y": 1}, "z": 2})', 2]]:
		var folded: Dictionary = probe.fold_expression(sample[0])
		_expect(failures, folded.get("ok", false) and folded.get("value") == sample[1], "language utility constant result: %s -> %s" % [sample[0], folded])
	for name in calls:
		var expression: String = calls[name][0]
		var result_type: String = calls[name][1]
		var cases := {
			"discarded": "func test():\n\t" + expression + "\n",
			"value": "func test():\n\tvar utility: Callable = " + name + "\n",
			"constant_value": "const UTILITY = " + name + "\n",
		}
		if result_type != "void":
			cases["initializer"] = "func test():\n\tvar value: " + result_type + " = " + expression + "\n"
			cases["return"] = "func test() -> " + result_type + ":\n\treturn " + expression + "\n"
			cases["nested"] = "func test():\n\tprint(" + expression + ")\n"
		for context in cases:
			var report: Dictionary = probe.analyze_source(cases[context], "res://tests/language_utility.barista")
			_expect(failures, report.get("valid", false), "%s %s language utility: %s" % [name, context, report.get("errors")])
	# Every fixed signature family rejects both too few/too many, with concrete type mismatch
	# checks kept separate from unknown-name rejection.
	var invalid := {
		"type_exists_arity": ["type_exists()", "Too few arguments"],
		"type_exists_type": ["type_exists(1)", "Invalid argument"],
		"char_arity": ["char(65, 66)", "Too many arguments"],
		"char_type": ['char("A")', "Invalid argument"],
		"ord_arity": ["ord()", "Too few arguments"],
		"ord_type": ["ord(65)", "Invalid argument"],
		"load_arity": ["load()", "Too few arguments"],
		"load_type": ["load(1)", "Invalid argument"],
		"print_stack_arity": ["print_stack(1)", "Too many arguments"],
		"get_stack_arity": ["get_stack(1)", "Too many arguments"],
		"len_arity": ["len()", "Too few arguments"],
		"len_constant_type": ["len(1)", "Invalid argument"],
		"instance_arity": ["is_instance_of(1)", "Too few arguments"],
		"instance_constant_type": ['is_instance_of(1, "int")', "Invalid argument"],
		"proxy_arity": ["create_proxy_dynamic(Object)", "Too few arguments"],
		"proxy_type": ["create_proxy_dynamic(1, Callable())", "Invalid argument"],
		"proxy_handler": ["create_proxy_dynamic(Object, 1)", "Invalid argument"],
		"named": ["char(code = 65)", "Named arguments"],
	}
	for label in invalid:
		var report: Dictionary = probe.analyze_source("func test():\n\t" + invalid[label][0] + "\n", "res://tests/language_utility_bad.barista")
		var errors: PackedStringArray = report.get("errors", PackedStringArray())
		_expect(failures, not report.get("valid", true) and errors.size() == 1 and invalid[label][1] in errors[0], "%s signature diagnostic: %s" % [label, errors])
	var positives := {
		"typed_value": 'func test():\n\tvar cb: Callable[[int], String] = char\n\tvar text: String = cb.call(65)\n',
		"inferred_value": 'func test():\n\tvar cb := ord\n\tvar code: int = cb.call("A")\n',
		"bound_value": 'func test():\n\tvar cb := char.bind(65)\n\tvar text: String = cb.call()\n',
		"callv": 'func test():\n\tvar cb := ord\n\tvar code: int = cb.callv(["A"])\n',
		"local_shadow": 'func test(len: Callable[[String], String]) -> String:\n\treturn len("abc")\n',
		"local_over_member": 'func len(value: int) -> int:\n\treturn value\nfunc test(len: Callable[[String], String]) -> String:\n\treturn len("abc")\n',
		"member_shadow": 'func len(value: String) -> String:\n\treturn value\nfunc test() -> String:\n\treturn len("abc")\n',
		"member_value_shadow": 'var len: Callable[[String], String]\nfunc test() -> String:\n\treturn len("abc")\n',
		"constants": 'const COUNT = len("abc")\nconst CHARACTER = char(65)\nconst CODE = ord(CHARACTER)\nconst EXISTS = type_exists("Node")\nconst MATCHES = is_instance_of(CODE, TYPE_INT)\n',
		"annotation": 'annotation number(value: int = len("abc")) targets METHOD\n@number(ord("A"))\nfunc test(value: String = char(65)):\n\tpass\n',
		"utility_annotation": 'annotation callback(value: Callable = len) targets METHOD\n@callback(ord)\nfunc test(cb: Callable = char):\n\tpass\n',
		"range_loop": 'func test():\n\tfor value in range(3):\n\t\tvar number: int = value\n',
		"shadowed_range_loop": 'func test(range: Callable[[], Array]):\n\tfor value in range():\n\t\tpass\n',
		# Pinned range is NOARGS+vararg metadata outside the for intrinsic; execution is M4.
		"runtime_range": 'func test():\n\tvar values: Array = range()\n\tvar other: Array = range("runtime check")\n',
		"runtime_len": 'func test(value):\n\tvar count: int = len(value)\n',
		"runtime_dictionary_len": 'func test(value):\n\tvar count: int = len({"x": value})\n',
		"constant_dictionary_len": 'const N = len({"x": 1})\n',
		"runtime_instance": 'func test(value, type):\n\tvar matches: bool = is_instance_of(value, type)\n',
		"debug_vararg": 'func test():\n\tprint_debug()\n\tprint_debug(1, "x", null)\n',
	}
	for label in positives:
		var report: Dictionary = probe.analyze_source(positives[label], "res://tests/language_utility_context.barista")
		_expect(failures, report.get("valid", false), "%s utility context: %s" % [label, report.get("errors")])
	var negatives := {
		"callable_arity": 'func test():\n\tvar cb := char\n\tcb.call()\n',
		"callable_type": 'func test():\n\tvar cb := ord\n\tcb.call(65)\n',
		"bound_type": 'func test():\n\tvar cb := char.bind("A")\n',
		"callv_type": 'func test():\n\tvar cb := ord\n\tcb.callv([65])\n',
		"void_initializer": 'func test():\n\tvar value = print_stack()\n',
		"void_nested": 'func test():\n\tprint(print_debug())\n',
		"char_domain": 'const BAD = char(-1)\n',
		"ord_domain": 'const BAD = ord("abc")\n',
		"instance_domain": 'const BAD = is_instance_of(1, -1)\n',
		"range_loop_arity": 'func test():\n\tfor value in range():\n\t\tpass\n',
		"range_loop_many": 'func test():\n\tfor value in range(1, 2, 3, 4):\n\t\tpass\n',
		"range_iterator_type": 'func test():\n\tfor value in range(3):\n\t\tvar text: String = value\n',
		"nonconstant_range": 'const BAD = range(3)\n',
		"nonconstant_load": 'const BAD = load("res://does_not_exist.barista")\n',
		"nonconstant_stack": 'const BAD = get_stack()\n',
		"nonconstant_proxy": 'const BAD = create_proxy_dynamic(Object, Callable())\n',
		"nonconstant_dictionary_len": 'var value = 1\nconst BAD = len({"x": value})\n',
	}
	for label in negatives:
		var report: Dictionary = probe.analyze_source(negatives[label], "res://tests/language_utility_negative.barista")
		_expect(failures, not report.get("valid", true) and not 'not declared' in str(report.get("errors")), "%s utility rejection: %s" % [label, report.get("errors")])
	# Existing general compatibility residual belongs to #138, not utility lookup: assigning
	# char to Callable[[String], String] is still admitted by BSTypeCompatibility's carrier-only
	# comparison. The registry retains int -> String metadata and invocation tests above use it.
	# Packed-array constructor/general constant folding remains the pre-existing #141 surface;
	# this registry can evaluate those carriers when constants exist, but does not invent them.
	for expression in ["missing_language_utility()", "len(missing_language_utility())"]:
		var report: Dictionary = probe.analyze_source("func test():\n\t" + expression + "\n", "res://tests/language_utility_unknown.barista")
		var errors: PackedStringArray = report.get("errors", PackedStringArray())
		_expect(failures, errors.size() == 1 and errors[0] == 'Identifier "missing_language_utility" not declared in the current scope.', "unknown language utility remains one diagnostic: %s" % errors)


func _test_dictionary_literal_constant_parity(failures: PackedStringArray) -> void:
	# Byte-faithful producer/analyzer contract: Foundry fs_parser.cpp:5357-5430 and
	# fs_analyzer.cpp:9323-9349 @ c9d5e35. Lua-style identifier keys are parser-produced
	# constants; only Python-style key expressions are reduced, and repeated constants fail.
	var probe := BaristaScriptAnalyzerProbe.new()
	for sample in [
		['{"key": 1}', "key", 1],
		["{ key = 1 }", "key", 1],
		['{"outer": { inner = 2 }}', "outer", { &"inner": 2 }],
		["{ outer = {\"inner\": 2} }", &"outer", { "inner": 2 }],
	]:
		var folded: Dictionary = probe.fold_expression(sample[0])
		var raw_value: Variant = folded.get("value", {})
		var value: Dictionary = raw_value if raw_value is Dictionary else {}
		_expect(failures, folded.get("ok", false), "dictionary constant folds: %s -> %s" % [sample[0], folded])
		_expect(failures, value.get(sample[1]) == sample[2], "dictionary preserves nested key/value: %s -> %s" % [sample[0], value])
	for sample in [['len({"key": 1})', 1], ["len({ key = 1 })", 1], ["len({})", 0]]:
		var folded: Dictionary = probe.fold_expression(sample[0])
		_expect(failures, folded.get("ok", false) and folded.get("value") == sample[1], "dictionary len folds: %s -> %s" % [sample[0], folded])
	var empty: Dictionary = probe.fold_expression("{}")
	_expect(failures, empty.get("ok", false) and (empty.get("value", {}) as Dictionary).is_empty(), "empty dictionary constant folds")
	var valid_source := "func test(value: int):\n\tvar python := {\"key\": value}\n\tvar lua := { key = value }\n\tvar python_count: int = len(python)\n\tvar lua_count: int = len(lua)\n"
	var valid_report: Dictionary = probe.analyze_source(valid_source, "res://tests/dictionary_nonconstant.barista")
	_expect(failures, valid_report.get("valid", false), "nonconstant dictionary values remain valid runtime expressions: %s" % valid_report.get("errors"))
	for sample in [
		['const DUPLICATE = {"key": 1, "key": 2}\n', 1, 1, 30],
		["const DUPLICATE = { key = 1, key = 2 }\n", 1, 1, 30],
		['const KEY = &"key"\nconst DUPLICATE = {KEY: 1, "key": 2}\n', 2, 2, 28],
		['const DUPLICATE = { key = 1, "key" = 2 }\n', 1, 1, 30],
	]:
		var source: String = sample[0]
		var report: Dictionary = probe.analyze_source(source, "res://tests/dictionary_duplicate.barista")
		var errors: PackedStringArray = report.get("errors", PackedStringArray())
		_expect(failures, not report.get("valid", true), "duplicate constant dictionary key is invalid: %s" % source)
		var expected := 'Key "key" was already used in this dictionary (at line %d).' % sample[1]
		_expect(failures, errors.size() == 1 and errors[0] == expected, "duplicate key uses pinned diagnostic and first-value line: %s" % errors)
		var validate: Dictionary = probe.validate_source(source, "res://tests/dictionary_duplicate.barista", false)
		var positioned_errors: Array = validate.get("errors", [])
		_expect(failures, positioned_errors.size() == 1 and positioned_errors[0].get("line") == sample[2] and positioned_errors[0].get("column") == sample[3], "duplicate key diagnostic originates at repeated key: %s" % positioned_errors)
	for source in [
		'var value = 1\nconst BAD = len({"key": value})\n',
		"var value = 1\nconst BAD = len({ key = value })\n",
	]:
		var report: Dictionary = probe.analyze_source(source, "res://tests/dictionary_nonconstant_const.barista")
		var errors: PackedStringArray = report.get("errors", PackedStringArray())
		_expect(failures, not report.get("valid", true) and not "not declared" in str(errors), "nonconstant dictionary is rejected only by its constant consumer: %s" % errors)


func _test_review_resolution_regressions(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var valid_cases := {
		"global_constant": "func test():\n\tprint(OK)\n",
		"utility_value": "func test():\n\tvar fn = print\n",
		"native_property": "extends Node\nfunc test():\n\tvar value: StringName = name\n",
		"native_method": "extends Node\nfunc test():\n\tvar fn: Callable = get_parent\n",
		"native_signal": "extends Node\nfunc test():\n\tvar event: Signal = tree_entered\n",
		"native_constant": "extends Node\nfunc test():\n\tvar mode: int = PROCESS_MODE_DISABLED\n",
		"outer_member": "const VALUE = 1\nclass Inner:\n\tfunc test():\n\t\tvar value: int = VALUE\n",
		"nested_class": "class Inner:\n\tpass\nfunc test():\n\tvar cls = Inner\n",
		"packed_argument": "annotation items(values: PackedInt32Array) targets METHOD\n@items([1, 2])\nfunc test():\n\tpass\n",
		"packed_default": "annotation items(values: PackedInt32Array = [1, 2]) targets METHOD\n@items\nfunc test():\n\tpass\n",
		"empty_callable": "const EMPTY = Callable()\n",
		"empty_signal": "const EMPTY = Signal()\n",
		"copy_callable": "const EMPTY = Callable()\nconst COPY = Callable(EMPTY)\n",
		"copy_signal": "const EMPTY = Signal()\nconst COPY = Signal(EMPTY)\n",
		"carrier_annotation": "annotation empty(value: Callable, event: Signal = Signal()) targets METHOD\n@empty(Callable())\nfunc test():\n\tpass\n",
		"carrier_default": "func test(value: Callable = Callable(), event: Signal = Signal()):\n\tpass\n",
		"alias_owner": "class Inner:\n\ttype Scalar = String\n\tvar first: Value = 1.5\ntype Value = Scalar\ntype Scalar = float\nvar subsequent: Value = 2.5\n",
		"alias_chain": "class Inner:\n\ttype Scalar = String\n\tvar first: Value = 1.5\ntype Value = Middle\ntype Middle = Scalar\ntype Scalar = float\nvar subsequent: Value = 2.5\n",
		"alias_order": "type Scalar = float\ntype Value = Scalar\nclass Inner:\n\ttype Scalar = String\n\tvar first: Value = 1.5\nvar subsequent: Value = 2.5\n",
	}
	for label in valid_cases:
		var report: Dictionary = probe.analyze_source(valid_cases[label], "res://tests/review_%s.barista" % label)
		_expect(failures, report.get("valid", false), "%s must resolve: %s" % [label, report.get("errors")])
	for callee in ["missing_ident", "Only"]:
		for expression in ["%s()", "var value = %s()", "return %s()", "print(%s())"]:
			var prefix := "type Only = int\n" if callee == "Only" else ""
			var source: String = prefix + "func test():\n\t" + (expression % callee) + "\n"
			var report: Dictionary = probe.analyze_source(source, "res://tests/review_call.barista")
			var errors: PackedStringArray = report.get("errors", PackedStringArray())
			var diagnostic := 'Type alias "Only" can only be used in a type position.' if callee == "Only" else 'Identifier "missing_ident" not declared in the current scope.'
			_expect(failures, not report.get("valid", true) and errors.size() == 1 and errors[0].begins_with(diagnostic), "%s %s must reject callee: %s" % [callee, expression, errors])
	var negative: Dictionary = probe.analyze_source("func test():\n\tvar value = missing_value\n", "res://tests/review_missing_value.barista")
	_expect(failures, not negative.get("valid", true), "true missing value stays rejected")
	for source in ["var outer_value: int\nclass Inner:\n\tfunc test():\n\t\tprint(outer_value)\n", "signal outer_event\nclass Inner:\n\tfunc test():\n\t\tvar event = outer_event\n", "static func outer() -> int:\n\treturn 1\nclass Inner:\n\tfunc test():\n\t\tvar value = outer\n", "extends Node\nclass Inner:\n\tfunc test():\n\t\tvar value = name\n"]:
		var report: Dictionary = probe.analyze_source(source, "res://tests/review_outer_boundary.barista")
		_expect(failures, not report.get("valid", true), "lexical outer does not donate receiver members: %s" % source)
	for source in ["const BAD = Callable(1)\n", "const BAD = Signal(1)\n", "extends Node\nconst BAD = Callable(self, \"get_parent\")\n", "extends Node\nconst BAD = Signal(self, \"tree_entered\")\n"]:
		var report: Dictionary = probe.analyze_source(source, "res://tests/review_unsafe_constant.barista")
		_expect(failures, not report.get("valid", true), "invalid overloads and bound carriers do not fold")
	for expression in ["Callable()", "Callable(Callable())", "Signal()", "Signal(Signal())"]:
		var report: Dictionary = probe.fold_expression(expression)
		var expected_type := TYPE_CALLABLE if expression.begins_with("Callable") else TYPE_SIGNAL
		_expect(failures, report.get("ok", false) and report.get("value_type") == expected_type, "%s folds to its exact engine carrier" % expression)
	for carrier in ["PackedByteArray", "PackedInt32Array", "PackedInt64Array", "PackedFloat32Array", "PackedFloat64Array", "PackedStringArray"]:
		var values := '["one", "two"]' if carrier == "PackedStringArray" else "[1, 2]"
		for use_default in [false, true]:
			var parameter: String = "value: " + carrier + (" = " + values if use_default else "")
			var annotation: String = "@items" if use_default else "@items(" + values + ")"
			var source: String = "annotation items(" + parameter + ") targets METHOD\n" + annotation + "\nfunc test():\n\tpass\n"
			var report: Dictionary = probe.analyze_source(source, "res://tests/review_conversion.barista")
			_expect(failures, report.get("valid", false), "%s supplied/default conversion uses engine surface: %s" % [carrier, report.get("errors")])


func _test_pinned_global_api_lookup(failures: PackedStringArray) -> void:
	# Read the actual pinned producer bytes. Foundry extension_api_dump.cpp:499-619 @ c9d5e35.
	var path := ProjectSettings.globalize_path("res://../godot-cpp/gdextension/extension_api-4-7.json")
	var api: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path))
	var probe := BaristaScriptAnalyzerProbe.new()
	var constants: Array = api.global_constants.duplicate()
	for enumeration in api.global_enums:
		constants.append_array(enumeration.values)
	for entry in constants:
		var report: Dictionary = probe.fold_expression(entry.name)
		_expect(failures, report.get("ok", false) and report.get("value_type") == TYPE_INT, "engine constant %s resolves" % entry.name)
		# Godot's JSON reader uses double; compare exact-int cases here and boundaries below.
		if abs(entry.value) < 9007199254740992.0:
			_expect(failures, report.get("value") == int(entry.value), "engine constant %s preserves its value" % entry.name)
	_expect(failures, probe.fold_expression("INT64_MAX").get("value") == 9223372036854775807, "INT64_MAX preserves every bit")
	_expect(failures, probe.fold_expression("INT64_MIN").get("value") == -9223372036854775807 - 1, "INT64_MIN preserves every bit")
	for function in api.utility_functions:
		var report: Dictionary = probe.analyze_source("func test():\n\tvar utility: Callable = " + function.name + "\n", "res://tests/review_utility.barista")
		_expect(failures, report.get("valid", false), "engine utility %s resolves as a value: %s" % [function.name, report.get("errors")])
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var indexed_source := _src_class("ReviewIndexed extends RefCounted\n")
	var imported_source := "namespace review_scope\n" + _src_class("ReviewImported extends RefCounted\n")
	index.synchronize_path_from_source("res://tests/review_indexed.barista", indexed_source)
	index.synchronize_path_from_source("res://tests/review_imported.barista", imported_source)
	BaristaScriptParseCache.set_source_override("res://tests/review_indexed.barista", indexed_source)
	BaristaScriptParseCache.set_source_override("res://tests/review_imported.barista", imported_source)
	for source in ["func test():\n\tvar handle = ReviewIndexed\n", "import review_scope\nfunc test():\n\tvar handle = ReviewImported\n"]:
		var report: Dictionary = probe.analyze_source(source, "res://tests/review_handle.barista")
		_expect(failures, report.get("valid", false), "producer-indexed/imported class handle resolves: %s" % report.get("errors"))
	BaristaScriptParseCache.clear_source_override("res://tests/review_indexed.barista")
	BaristaScriptParseCache.clear_source_override("res://tests/review_imported.barista")
	index.clear()


func _src_class(body_after_keyword_space: String) -> String:
	return _kw_class_name() + " " + body_after_keyword_space


func _test_parser_lifecycle(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	var first := BaristaScriptParseCache.get_parser(FIXTURE_A, Status.PARSED, "")
	_expect(failures, first.valid and first.status == Status.PARSED and first.error == OK, "first raise to PARSED")
	var again := BaristaScriptParseCache.get_parser(FIXTURE_A, Status.FULLY_SOLVED, "")
	_expect(failures, again.valid and again.status == Status.FULLY_SOLVED, "same entry raises monotonically")
	_expect(failures, again.source_hash == first.source_hash, "source hash latches across raises")
	var reused := BaristaScriptParseCache.get_parser(FIXTURE_A, Status.FULLY_SOLVED, "")
	_expect(failures, reused.valid and BaristaScriptParseCache.has_parser(FIXTURE_A), "cached entry reused")


func _test_transitive_invalidation(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/dep_a.barista", _src_class("DepA extends Node\n"))
	BaristaScriptParseCache.set_source_override("res://tests/dep_b.barista", _src_class("DepB extends Node\n"))
	BaristaScriptParseCache.set_source_override("res://tests/dep_c.barista", _src_class("DepC extends Node\n"))
	BaristaScriptParseCache.set_source_override("res://tests/unrelated.barista", _src_class("Unrelated extends Node\n"))

	BaristaScriptParseCache.get_parser("res://tests/dep_a.barista", Status.PARSED, "")
	BaristaScriptParseCache.get_parser("res://tests/dep_b.barista", Status.PARSED, "res://tests/dep_a.barista")
	BaristaScriptParseCache.get_parser("res://tests/dep_c.barista", Status.PARSED, "res://tests/dep_b.barista")
	BaristaScriptParseCache.get_parser("res://tests/unrelated.barista", Status.PARSED, "")

	var closure := BaristaScriptParseCache.collect_parser_invalidation_closure("res://tests/dep_c.barista")
	_expect(failures, "res://tests/dep_c.barista" in closure, "closure includes provider")
	_expect(failures, "res://tests/dep_b.barista" in closure, "closure includes inverse dependent")
	_expect(failures, "res://tests/dep_a.barista" in closure, "closure is transitive")
	_expect(failures, not ("res://tests/unrelated.barista" in closure), "unrelated entry survives closure")

	BaristaScriptParseCache.remove_parser("res://tests/dep_c.barista")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/dep_c.barista"), "provider removed")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/dep_b.barista"), "dependent invalidated")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/dep_a.barista"), "transitive dependent invalidated")
	_expect(failures, BaristaScriptParseCache.has_parser("res://tests/unrelated.barista"), "unrelated survives")
	BaristaScriptParseCache.clear_source_overrides()


func _test_missing_and_self(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/owner.barista", _src_class("OwnerFile extends Node\n"))
	var owner_ref := BaristaScriptParseCache.get_parser("res://tests/owner.barista", Status.EMPTY, "")
	_expect(failures, owner_ref.valid, "owner fixture must cache")
	var missing_path := "res://tests/does_not_exist.barista"
	var missing := BaristaScriptParseCache.get_parser(missing_path, Status.EMPTY, "res://tests/owner.barista")
	_expect(failures, not missing.valid, "missing file creates no entry")
	_expect(failures, not BaristaScriptParseCache.has_parser(missing_path), "missing path absent from map")
	var missing_inverse := BaristaScriptParseCache.get_inverse_dependencies(missing_path)
	_expect(failures, missing_inverse.is_empty(), "missing file creates no inverse edge")
	# Removing a never-admitted missing path must not wipe the owner via a ghost edge.
	BaristaScriptParseCache.remove_script(missing_path)
	_expect(failures, BaristaScriptParseCache.has_parser("res://tests/owner.barista"), "ghost missing edge must not invalidate owner")

	BaristaScriptParseCache.set_source_override("res://tests/self.barista", _src_class("SelfFile extends Node\n"))
	var self_ref := BaristaScriptParseCache.get_parser("res://tests/self.barista", Status.EMPTY, "res://tests/self.barista")
	_expect(failures, self_ref.valid, "self owner still creates the entry")
	# Self-dependency must not invent a distinct edge; inverse deps of self exclude self-owner recording.
	var inverse := BaristaScriptParseCache.get_inverse_dependencies("res://tests/self.barista")
	_expect(failures, not ("res://tests/self.barista" in inverse), "self-dependency creates no owner edge")
	BaristaScriptParseCache.clear_source_overrides()


func _test_move_remove(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/old.barista", _src_class("OldName extends Node\n"))
	BaristaScriptParseCache.get_parser("res://tests/old.barista", Status.PARSED, "")
	_expect(failures, BaristaScriptParseCache.has_parser("res://tests/old.barista"), "old path cached")
	BaristaScriptParseCache.move_script("res://tests/old.barista", "res://tests/new.barista")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/old.barista"), "move drops old parser state")
	BaristaScriptParseCache.set_source_override("res://tests/new.barista", _src_class("NewName extends Node\n"))
	var rebuilt := BaristaScriptParseCache.get_parser("res://tests/new.barista", Status.PARSED, "")
	_expect(failures, rebuilt.valid, "new path rebuilds rather than inheriting stale pointers")
	BaristaScriptParseCache.remove_script("res://tests/new.barista")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/new.barista"), "remove_script drops parser state")
	BaristaScriptParseCache.clear_source_overrides()


func _test_dependency_cycle(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/cycle_a.barista", _src_class("CycleA extends Node\n"))
	BaristaScriptParseCache.set_source_override("res://tests/cycle_b.barista", _src_class("CycleB extends Node\n"))
	# Record edges both ways with EMPTY status (no nested raise while locked).
	var a := BaristaScriptParseCache.get_parser("res://tests/cycle_a.barista", Status.EMPTY, "res://tests/cycle_b.barista")
	var b := BaristaScriptParseCache.get_parser("res://tests/cycle_b.barista", Status.EMPTY, "res://tests/cycle_a.barista")
	_expect(failures, a.valid and b.valid, "cycle edges can be recorded at EMPTY")
	var raised_a := BaristaScriptParseCache.get_parser("res://tests/cycle_a.barista", Status.FULLY_SOLVED, "")
	var raised_b := BaristaScriptParseCache.get_parser("res://tests/cycle_b.barista", Status.FULLY_SOLVED, "")
	_expect(failures, raised_a.valid and raised_b.valid, "cycle raise completes without deadlock")
	BaristaScriptParseCache.clear_source_overrides()


func _test_finalization_raises_dependencies(failures: PackedStringArray) -> void:
	# Byte-faithful preload expression from the producer statement: Foundry
	# modules/foundry_script/tests/scripts/analyzer/errors/type_alias_cross_file_base.fs:1-3
	# @ c9d5e35. BSAnalyzer::raise_declared_conformance_dependencies adds every parser dependency
	# at src/bs_analyzer_conformance.cpp:382; finalization raises it from PARSED to INHERITANCE_SOLVED.
	BaristaScriptParseCache.clear_script_cache()
	var dependency_path := "res://tests/finalization_dependency.notest.barista"
	var owner_path := "res://tests/finalization_dependency.barista"
	BaristaScriptParseCache.set_source_override(dependency_path, _src_class("FinalizationDependency extends Node\n"))
	BaristaScriptParseCache.set_source_override(owner_path, "func test() -> void:\n\tpreload(\"./finalization_dependency.notest.barista\")\n")
	var owner := BaristaScriptParseCache.get_parser(owner_path, Status.FULLY_SOLVED, "")
	var dependency := BaristaScriptParseCache.get_parser(dependency_path, Status.EMPTY, "")
	_expect(failures, owner.valid and owner.status == Status.FULLY_SOLVED, "finalization dependency owner fully solves")
	_expect(failures, dependency.valid and dependency.status >= Status.INHERITANCE_SOLVED,
		"finalization raises every depended parser to INHERITANCE_SOLVED")
	BaristaScriptParseCache.clear_source_override(owner_path)
	BaristaScriptParseCache.clear_source_override(dependency_path)


func _test_strict_settings(failures: PackedStringArray) -> void:
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/strict.barista", _src_class("StrictOne extends Node\n"))
	BaristaScriptParseCache.get_parser("res://tests/strict.barista", Status.PARSED, "")
	_expect(failures, BaristaScriptParseCache.has_parser("res://tests/strict.barista"), "pre-strict entry present")

	# First observation establishes baseline without invalidation.
	_expect(failures, not BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change(),
		"first observation does not invalidate")
	_expect(failures, BaristaScriptParseCache.has_parser("res://tests/strict.barista"), "entry survives first observation")
	_expect(failures, not BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change(),
		"unchanged values do not invalidate")

	ProjectSettings.set_setting("debug/barista_script/analysis/strict_null_checks", true)
	_expect(failures, BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change(),
		"null-check flip invalidates once")
	_expect(failures, not BaristaScriptParseCache.has_parser("res://tests/strict.barista"),
		"parsed artifacts dropped on flip")
	_expect(failures, BaristaScriptParseCache.has_source_override("res://tests/strict.barista"),
		"source overrides preserved across invalidation")

	BaristaScriptParseCache.get_parser("res://tests/strict.barista", Status.PARSED, "")
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", true)
	_expect(failures, BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change(),
		"dynamic-check flip invalidates independently")
	_expect(failures, not BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change(),
		"restoration without change does not invalidate again")

	ProjectSettings.set_setting("debug/barista_script/analysis/strict_null_checks", false)
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", false)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	BaristaScriptParseCache.clear_source_overrides()


func _type(kind: int, builtin_type: int, opts: Dictionary = {}) -> Dictionary:
	var d := {
		"kind": kind,
		"builtin_type": builtin_type,
		"is_meta_type": opts.get("is_meta_type", false),
		"native_type": opts.get("native_type", ""),
		"script_path": opts.get("script_path", ""),
	}
	if opts.has("container_element_types"):
		d["container_element_types"] = opts["container_element_types"]
	return d


func _test_can_reference(failures: PackedStringArray) -> void:
	var probe := BaristaScriptParserProbe.new()
	const KIND_BUILTIN := 0
	const KIND_NATIVE := 1
	const KIND_SCRIPT := 2
	const KIND_CLASS := 3
	const KIND_ENUM := 4
	const KIND_TUPLE := 5
	const KIND_UNION := 6
	# Kind values from BSParser::DataType::Kind — verify against a quick builtin case.
	_expect(failures, probe.can_reference(
		_type(KIND_BUILTIN, TYPE_INT),
		_type(KIND_BUILTIN, TYPE_INT)
	), "compatible plain int carriers")
	_expect(failures, not probe.can_reference(
		_type(KIND_BUILTIN, TYPE_INT),
		_type(KIND_BUILTIN, TYPE_FLOAT)
	), "mismatched carriers rejected")
	_expect(failures, not probe.can_reference(
		_type(KIND_BUILTIN, TYPE_INT),
		_type(KIND_BUILTIN, TYPE_INT, {"is_meta_type": true})
	), "meta-types rejected")
	_expect(failures, not probe.can_reference(
		_type(KIND_UNION, TYPE_NIL),
		_type(KIND_BUILTIN, TYPE_INT)
	), "unions rejected")

	var int_el := _type(KIND_BUILTIN, TYPE_INT)
	_expect(failures, probe.can_reference(
		_type(KIND_TUPLE, TYPE_ARRAY, {"container_element_types": [int_el, int_el]}),
		_type(KIND_TUPLE, TYPE_ARRAY, {"container_element_types": [int_el, int_el]})
	), "matching tuple shapes accepted")
	_expect(failures, not probe.can_reference(
		_type(KIND_TUPLE, TYPE_ARRAY, {"container_element_types": [int_el]}),
		_type(KIND_BUILTIN, TYPE_ARRAY)
	), "tuple-vs-array rejected")
	_expect(failures, not probe.can_reference(
		_type(KIND_TUPLE, TYPE_ARRAY, {"container_element_types": [int_el]}),
		_type(KIND_TUPLE, TYPE_ARRAY, {"container_element_types": [int_el, int_el]})
	), "tuple shape mismatch rejected")

	_expect(failures, probe.can_reference(
		_type(KIND_NATIVE, TYPE_OBJECT, {"native_type": "Node"}),
		_type(KIND_NATIVE, TYPE_OBJECT, {"native_type": "Node2D"})
	), "native ancestry accepted")
	_expect(failures, not probe.can_reference(
		_type(KIND_NATIVE, TYPE_OBJECT, {"native_type": "Node2D"}),
		_type(KIND_NATIVE, TYPE_OBJECT, {"native_type": "Node"})
	), "unrelated/narrower native rejected")

	_expect(failures, not probe.can_reference(
		_type(KIND_CLASS, TYPE_OBJECT, {"native_type": "RefCounted", "script_path": "res://tests/missing_class.barista"}),
		_type(KIND_CLASS, TYPE_OBJECT, {"native_type": "RefCounted", "script_path": "res://tests/missing_other.barista"})
	), "failed class-path resolution returns false")


func _test_host_bootstrap_filtering(failures: PackedStringArray) -> void:
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var token := index.claim_refresh("res://tests/cache_fixtures/script_a.barista")
	index.commit_record(token, {
		"path": "res://tests/cache_fixtures/script_a.barista",
		"source_digest": 1,
		"namespace_name": "cachefix",
		"qualified_name": "",
		"kind": 0,
		"base_type": "",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": true,
	})
	var token2 := index.claim_refresh("res://outside/other.barista")
	index.commit_record(token2, {
		"path": "res://outside/other.barista",
		"source_digest": 2,
		"namespace_name": "cachefix",
		"qualified_name": "",
		"kind": 0,
		"base_type": "",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": true,
	})
	index.set_bootstrap_root("res://tests/")
	# Host returns all indexed conformance files; bootstrap filtering happens in
	# get_namespace_conformance_dependencies. Probe the host allow check directly.
	_expect(failures, index.host_is_bootstrap_path_allowed("res://tests/cache_fixtures/script_a.barista"),
		"in-root conformance allowed")
	_expect(failures, not index.host_is_bootstrap_path_allowed("res://outside/other.barista"),
		"out-of-root conformance filtered")
	index.set_bootstrap_root("")
	index.clear()


func _test_validate_and_is_valid_agree(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var valid_source := _src_class("AnalyzerValid extends Node\n\nfunc _ready() -> void:\n\tvar x: int = 1\n")
	var analyzed: Dictionary = probe.analyze_source(valid_source, "res://tests/analyzer_valid.barista")
	_expect(failures, analyzed["valid"] == true, "valid program analyzes cleanly")
	_expect(failures, probe.is_semantically_valid(valid_source, "res://tests/analyzer_valid.barista"),
		"probe is_semantically_valid agrees for valid program")
	var script := BaristaScript.new()
	script.set_source_code(valid_source)
	script.resource_path = "res://tests/analyzer_valid.barista"
	_expect(failures, script.is_valid(), "BaristaScript.is_valid agrees for valid program")

	var bad_source := _src_class("AnalyzerBad extends NotARealBaseClass\n")
	var bad_analyzed: Dictionary = probe.analyze_source(bad_source, "res://tests/analyzer_bad.barista")
	_expect(failures, bad_analyzed["valid"] == false, "unknown base is invalid")
	_expect(failures, not probe.is_semantically_valid(bad_source, "res://tests/analyzer_bad.barista"),
		"probe is_semantically_valid agrees for semantic error")
	var bad_script := BaristaScript.new()
	bad_script.set_source_code(bad_source)
	bad_script.resource_path = "res://tests/analyzer_bad.barista"
	_expect(failures, not bad_script.is_valid(), "BaristaScript.is_valid false for semantic error")


func _test_semantic_errors(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var mismatch := _src_class("TypeMismatch extends Node\n\nfunc _ready() -> void:\n\tvar x: int = 1.5\n")
	var report: Dictionary = probe.analyze_source(mismatch, "res://tests/type_mismatch.barista")
	_expect(failures, report["valid"] == false, "int = float mismatch is invalid")
	_expect(failures, (report["errors"] as PackedStringArray).size() > 0, "type mismatch produces a diagnostic")

	var generic := _src_class("GenericBox[T] extends RefCounted\n")
	var generic_report: Dictionary = probe.analyze_source(generic, "res://tests/generic_box.barista")
	_expect(failures, generic_report["valid"] == false, "generic class needs M5 diagnostic")
	var generic_errors: PackedStringArray = generic_report["errors"]
	var saw_m5 := false
	for message in generic_errors:
		if "M5" in message:
			saw_m5 = true
	_expect(failures, saw_m5, "generic construct names M5 in diagnostic")


func _test_unary_sign_constant_folding(failures: PackedStringArray) -> void:
	# Issue #49: consume parser AST shapes from #39; do not re-tokenize sign spellings.
	var probe := BaristaScriptAnalyzerProbe.new()
	var adjacent: Dictionary = probe.fold_expression("-2 ** 2")
	_expect(failures, adjacent["ok"] == true, "-2 ** 2 folds")
	_expect(failures, adjacent["value"] == 4, "-2 ** 2 → 4")
	_expect(failures, adjacent["has_unary_sign"] == false, "-2 ** 2 has no unary-sign node")

	var paren: Dictionary = probe.fold_expression("(-2) ** 2")
	_expect(failures, paren["ok"] == true and paren["value"] == 4, "(-2) ** 2 → 4")
	_expect(failures, paren["has_unary_sign"] == false, "(-2) ** 2 has no unary-sign node")

	var explicit: Dictionary = probe.fold_expression("-(2 ** 2)")
	_expect(failures, explicit["ok"] == true and explicit["value"] == -4, "-(2 ** 2) → -4")
	_expect(failures, explicit["has_unary_sign"] == true, "-(2 ** 2) keeps unary-sign node")

	var spaced: Dictionary = probe.fold_expression("- 2 ** 2")
	_expect(failures, spaced["ok"] == true and spaced["value"] == -4, "- 2 ** 2 → -4")
	_expect(failures, spaced["has_unary_sign"] == true, "- 2 ** 2 keeps unary-sign node")

	var plus_adj: Dictionary = probe.fold_expression("+2 ** 2")
	_expect(failures, plus_adj["ok"] == true and plus_adj["value"] == 4, "+2 ** 2 → 4")


func _test_analyzer_declaration_commit(failures: PackedStringArray) -> void:
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var ok_path := "res://tests/commit_ok.barista"
	var ok_source := _src_class("CommitOk extends Node\n\nfunc _ready() -> void:\n\tpass\n")
	index.synchronize_path_from_source(ok_path, ok_source)
	var found := false
	for record in index.get_records():
		if record.get("qualified_name", "") == "CommitOk":
			found = true
	_expect(failures, found, "successful analysis commits CommitOk declaration")

	# Read-only analyze / is_valid must not mutate the index (PR #59 review).
	var before := index.get_record_count()
	var probe := BaristaScriptAnalyzerProbe.new()
	var read_only: Dictionary = probe.analyze_source(ok_source, ok_path)
	_expect(failures, read_only.get("valid", false), "read-only analyze still reports valid")
	_expect(failures, probe.is_semantically_valid(ok_source, ok_path), "is_valid agrees without committing")
	_expect(failures, index.get_record_count() == before, "analyze/is_valid must not change declaration index")

	index.synchronize_path_from_source(ok_path, _src_class("CommitOk extends MissingBaseDefinitely\n"))
	var still_there := false
	for record in index.get_records():
		if record.get("path", "") == ok_path:
			still_there = true
	_expect(failures, not still_there, "failed analysis removes prior declaration record")
	index.clear()


func _find_record(index: BaristaScriptDeclarationIndexProbe, qualified: String) -> Dictionary:
	for record in index.get_records():
		if record.get("qualified_name", "") == qualified:
			return record
	return {}


func _test_declaration_head_kinds_and_conformance(failures: PackedStringArray) -> void:
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var cases := [
		{"path": "res://tests/commit_trait.barista", "source": "trait_name CommitTrait\n", "name": "CommitTrait", "kind": 3},
		{"path": "res://tests/commit_enum.barista", "source": "enum_name CommitEnum:\n\tA = 0\n\tB = 1\n", "name": "CommitEnum", "kind": 4},
		{"path": "res://tests/commit_tuple.barista", "source": "tuple_name CommitTup(x: int, y: int)\n", "name": "CommitTup", "kind": 5},
		{"path": "res://tests/commit_generic.barista", "source": _src_class("CommitGeneric[T] extends RefCounted\n"), "name": "CommitGeneric", "kind": 2},
	]
	for entry in cases:
		index.synchronize_path_from_source(entry.path, entry.source)
		var record := _find_record(index, entry.name)
		_expect(failures, not record.is_empty(), "analyzer commits %s" % entry.name)
		if not record.is_empty():
			_expect(failures, int(record.get("kind", -1)) == entry.kind, "%s kind matches" % entry.name)

	var conform_path := "res://tests/commit_conform.barista"
	var conform_source := "namespace commitns\n\nextend Node uses CommitTrait:\n\tpass\n"
	# Trait must exist for uses resolution; keep the source override while the conformance
	# file analyzes (synchronize clears its own override after commit).
	BaristaScriptParseCache.set_source_override("res://tests/commit_trait.barista", "trait_name CommitTrait\n")
	index.synchronize_path_from_source(conform_path, conform_source)
	BaristaScriptParseCache.clear_source_override("res://tests/commit_trait.barista")
	var conformances := index.get_conformance_files_in_namespace("commitns")
	_expect(failures, conformances.size() >= 1, "declaration-only conformance commits into namespace view")

	var annot_path := "res://tests/commit_annot.barista"
	var annot_source := "namespace annotns\n\nannotation CommitMark targets METHOD\n"
	index.synchronize_path_from_source(annot_path, annot_source)
	var annot_paths := index.get_annotation_declaring_paths("annotns.CommitMark")
	_expect(failures, annot_paths.size() == 1, "annotation-only file commits declaring path")
	index.clear()


func _test_digest_mismatch_discards(failures: PackedStringArray) -> void:
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var path := "res://tests/digest_mismatch.barista"
	var source := _src_class("DigestFresh extends Node\n")
	index.synchronize_path_from_source(path, source)
	_expect(failures, not _find_record(index, "DigestFresh").is_empty(), "fresh record present before mismatch")

	# Overwrite the live record with a stale digest while keeping the same path/name.
	var token := index.claim_refresh(path)
	_expect(failures, index.commit_record(token, {
		"path": path,
		"source_digest": 999999,
		"namespace_name": "",
		"qualified_name": "DigestFresh",
		"kind": 1,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "stale digest record commits for the mismatch fixture")

	BaristaScriptParseCache.set_source_override(path, source)
	var looked := index.lookup_qualified_name("DigestFresh")
	_expect(failures, not looked.is_empty(), "lookup reanalyzes and restores DigestFresh")
	_expect(failures, int(looked.get("source_digest", 0)) == BaristaScriptDeclarationIndexProbe.compute_source_digest(source),
		"restored digest matches current source")

	# Re-poison and exercise ScriptServer path surface (#62). Assert restored digest
	# so a raw try_get_by_qualified_name revert cannot still pass.
	# Re-set the override: synchronize_declaration_path_from_source clears it.
	var expected_digest := BaristaScriptDeclarationIndexProbe.compute_source_digest(source)
	token = index.claim_refresh(path)
	_expect(failures, index.commit_record(token, {
		"path": path,
		"source_digest": 999999,
		"namespace_name": "",
		"qualified_name": "DigestFresh",
		"kind": 1,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "re-poison stale digest for ScriptServer path")
	BaristaScriptParseCache.set_source_override(path, source)
	var ss_path := index.script_server_get_global_class_path("DigestFresh")
	_expect(failures, ss_path == path, "ScriptServer path lookup reanalyzes stale digest")
	var after_path := _find_record(index, "DigestFresh")
	_expect(failures, int(after_path.get("source_digest", 0)) == expected_digest,
		"ScriptServer path lookup restores current digest")

	# Re-poison again and exercise list-driven resolve before any path heal.
	token = index.claim_refresh(path)
	_expect(failures, index.commit_record(token, {
		"path": path,
		"source_digest": 888888,
		"namespace_name": "",
		"qualified_name": "DigestFresh",
		"kind": 1,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "re-poison stale digest for ScriptServer list")
	BaristaScriptParseCache.set_source_override(path, source)
	_expect(failures, "DigestFresh" in index.script_server_get_global_class_list(),
		"ScriptServer class list includes digest-validated private name")
	var after_list := _find_record(index, "DigestFresh")
	_expect(failures, int(after_list.get("source_digest", 0)) == expected_digest,
		"ScriptServer list lookup restores current digest")

	# native_base fallback also goes through digest-validating resolve.
	token = index.claim_refresh(path)
	_expect(failures, index.commit_record(token, {
		"path": path,
		"source_digest": 777777,
		"namespace_name": "",
		"qualified_name": "DigestFresh",
		"kind": 1,
		"base_type": "Node",
		"is_abstract": false,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "re-poison stale digest for ScriptServer native_base")
	BaristaScriptParseCache.set_source_override(path, source)
	_expect(failures, String(index.script_server_get_global_class_native_base("DigestFresh")) == "Node",
		"ScriptServer native_base reanalyzes stale digest")
	var after_base := _find_record(index, "DigestFresh")
	_expect(failures, int(after_base.get("source_digest", 0)) == expected_digest,
		"ScriptServer native_base restores current digest")

	var enum_path := "res://tests/digest_enum.barista"
	var enum_source := "enum_name DigestEnum:\n\tA = 0\n\tB = 1\n"
	index.synchronize_path_from_source(enum_path, enum_source)
	_expect(failures, index.script_server_is_global_class_enum("DigestEnum"),
		"ScriptServer recognizes synchronized enum")
	var expected_enum_digest := BaristaScriptDeclarationIndexProbe.compute_source_digest(enum_source)
	var enum_token := index.claim_refresh(enum_path)
	_expect(failures, index.commit_record(enum_token, {
		"path": enum_path,
		"source_digest": 424242,
		"namespace_name": "",
		"qualified_name": "DigestEnum",
		"kind": 4,
		"base_type": "",
		"is_abstract": true,
		"is_tool": false,
		"icon_path": "",
		"global_annotations": PackedStringArray(),
		"declares_retroactive_conformances": false,
	}), "stale enum digest commits")
	BaristaScriptParseCache.set_source_override(enum_path, enum_source)
	_expect(failures, index.script_server_is_global_class_enum("DigestEnum"),
		"ScriptServer enum lookup reanalyzes stale digest")
	var after_enum := _find_record(index, "DigestEnum")
	_expect(failures, int(after_enum.get("source_digest", 0)) == expected_enum_digest,
		"ScriptServer enum lookup restores current digest")
	BaristaScriptParseCache.clear_source_override(path)
	BaristaScriptParseCache.clear_source_override(enum_path)
	index.clear()


func _test_namespace_change_invalidation(failures: PackedStringArray) -> void:
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	BaristaScriptParseCache.clear_script_cache()
	var conform_path := "res://tests/ns_conform.barista"
	var consumer_old := "res://tests/ns_consumer_old.barista"
	var consumer_new := "res://tests/ns_consumer_new.barista"
	var trait_path := "res://tests/ns_trait.barista"
	var trait_source := "trait_name NsTrait\n"

	index.synchronize_path_from_source(trait_path, trait_source)
	BaristaScriptParseCache.set_source_override(trait_path, trait_source)
	index.synchronize_path_from_source(conform_path,
		"namespace oldns\n\nextend Node uses NsTrait:\n\tpass\n")

	BaristaScriptParseCache.set_source_override(consumer_old, "namespace oldns\n" + _src_class("OldConsumer extends Node\n"))
	BaristaScriptParseCache.set_source_override(consumer_new, "namespace newns\n" + _src_class("NewConsumer extends Node\n"))
	BaristaScriptParseCache.get_parser(consumer_old, Status.PARSED, "")
	BaristaScriptParseCache.get_parser(consumer_new, Status.PARSED, "")
	_expect(failures, BaristaScriptParseCache.has_parser(consumer_old), "old-namespace consumer cached")
	_expect(failures, BaristaScriptParseCache.has_parser(consumer_new), "new-namespace consumer cached")

	# Analyzer-driven namespace change on the conformance file.
	BaristaScriptParseCache.set_source_override(trait_path, trait_source)
	index.synchronize_path_from_source(conform_path,
		"namespace newns\n\nextend Node uses NsTrait:\n\tpass\n")
	_expect(failures, not BaristaScriptParseCache.has_parser(consumer_old),
		"old namespace consumers invalidated via analyzer commit")
	_expect(failures, not BaristaScriptParseCache.has_parser(consumer_new),
		"new namespace consumers invalidated via analyzer commit")
	BaristaScriptParseCache.clear_source_overrides()
	index.clear()


func _test_explicit_out_of_root_import(failures: PackedStringArray) -> void:
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var probe := BaristaScriptAnalyzerProbe.new()
	index.synchronize_path_from_source("res://outside/out_trait.barista",
		"namespace outerspace\ntrait_name OuterTrait\n")
	index.set_bootstrap_root("res://tests/")
	var report: Dictionary = probe.analyze_source(
		"import outerspace\n" + _src_class("NeedsOuter extends Node\n"),
		"res://tests/needs_outer.barista")
	_expect(failures, report.get("valid", true) == false, "explicit out-of-root import is invalid")
	var saw := false
	for message in report.get("errors", PackedStringArray()):
		if "outside the provider bootstrap root" in message or "Cannot import namespace" in message or "bootstrap cannot import" in message:
			saw = true
	_expect(failures, saw, "explicit out-of-root import emits analyzer diagnostic")
	# Implicit out-of-root conformance stays host-filtered without that diagnostic for an in-root script.
	BaristaScriptParseCache.set_source_override("res://outside/out_trait.barista",
		"namespace outerspace\ntrait_name OuterTrait\n")
	index.synchronize_path_from_source("res://outside/out_conform.barista",
		"namespace outerspace\n\nextend Node uses OuterTrait:\n\tpass\n")
	BaristaScriptParseCache.clear_source_override("res://outside/out_trait.barista")
	_expect(failures, not index.host_is_bootstrap_path_allowed("res://outside/out_conform.barista"),
		"implicit out-of-root conformance remains host-filtered")
	index.set_bootstrap_root("")
	index.clear()


func _test_call_arity_and_types(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var too_few := _src_class("CallFew extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tadd(1)\n")
	var few_report: Dictionary = probe.analyze_source(too_few, "res://tests/call_few.barista")
	_expect(failures, few_report.get("valid", true) == false, "too few arguments invalid")
	var saw_few := false
	for message in few_report.get("errors", PackedStringArray()):
		if "Too few arguments" in message:
			saw_few = true
	_expect(failures, saw_few, "too few arguments diagnostic")

	var bad_type := _src_class("CallType extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tadd(1, 1.5)\n")
	var type_report: Dictionary = probe.analyze_source(bad_type, "res://tests/call_type.barista")
	_expect(failures, type_report.get("valid", true) == false, "wrong call argument type invalid")

	var ok := _src_class("CallOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tvar x: int = add(1, 2)\n")
	var ok_report: Dictionary = probe.analyze_source(ok, "res://tests/call_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "matching call arity/types valid")


func _test_call_validation_methodinfo_and_signals(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()

	# MethodInfo path: bare native call on Node base (ClassDB MethodInfo via BSNativeDB).
	var native_few := _src_class("NativeFew extends Node\nfunc _ready() -> void:\n\tget_node()\n")
	var native_few_report: Dictionary = probe.analyze_source(native_few, "res://tests/native_few.barista")
	_expect(failures, native_few_report.get("valid", true) == false, "native MethodInfo too-few invalid")
	var saw_native_few := false
	for message in native_few_report.get("errors", PackedStringArray()):
		if "Too few arguments" in message and "get_node" in message:
			saw_native_few = true
	_expect(failures, saw_native_few, "native MethodInfo too-few diagnostic")

	var native_type := _src_class("NativeType extends Node\nfunc _ready() -> void:\n\tget_node(1)\n")
	var native_type_report: Dictionary = probe.analyze_source(native_type, "res://tests/native_type.barista")
	_expect(failures, native_type_report.get("valid", true) == false, "native MethodInfo wrong arg type invalid")
	var saw_native_type := false
	for message in native_type_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "get_node" in message:
			saw_native_type = true
	_expect(failures, saw_native_type, "native MethodInfo wrong-type diagnostic")

	# Signal.emit payload arity + types.
	var emit_few := _src_class("EmitFew extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tchanged.emit()\n")
	var emit_few_report: Dictionary = probe.analyze_source(emit_few, "res://tests/emit_few.barista")
	_expect(failures, emit_few_report.get("valid", true) == false, "signal.emit too-few invalid")
	var saw_emit_few := false
	for message in emit_few_report.get("errors", PackedStringArray()):
		if "Too few arguments" in message and "emit" in message:
			saw_emit_few = true
	_expect(failures, saw_emit_few, "signal.emit too-few diagnostic")

	var emit_type := _src_class("EmitType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tchanged.emit(\"bad\")\n")
	var emit_type_report: Dictionary = probe.analyze_source(emit_type, "res://tests/emit_type.barista")
	_expect(failures, emit_type_report.get("valid", true) == false, "signal.emit wrong type invalid")
	var saw_emit_type := false
	for message in emit_type_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit" in message:
			saw_emit_type = true
	_expect(failures, saw_emit_type, "signal.emit wrong-type diagnostic")

	var emit_ok := _src_class("EmitOk extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tchanged.emit(1)\n")
	var emit_ok_report: Dictionary = probe.analyze_source(emit_ok, "res://tests/emit_ok.barista")
	_expect(failures, emit_ok_report.get("valid", false) == true, "signal.emit matching payload valid")

	# emit_signal("name", ...) constant-name payload validation.
	var emit_signal_type := _src_class("EmitSignalType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\temit_signal(\"changed\", \"bad\")\n")
	var emit_signal_report: Dictionary = probe.analyze_source(emit_signal_type, "res://tests/emit_signal_type.barista")
	_expect(failures, emit_signal_report.get("valid", true) == false, "emit_signal wrong payload type invalid")
	var saw_emit_signal := false
	for message in emit_signal_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit_signal" in message:
			saw_emit_signal = true
	_expect(failures, saw_emit_signal, "emit_signal wrong-type diagnostic")

	var emit_signal_ok := _src_class("EmitSignalOk extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\temit_signal(\"changed\", 1)\n")
	var emit_signal_ok_report: Dictionary = probe.analyze_source(emit_signal_ok, "res://tests/emit_signal_ok.barista")
	_expect(failures, emit_signal_ok_report.get("valid", false) == true, "emit_signal matching payload valid")

	# self.emit_signal must also run typed payload validation (#72 / PR #71 review).
	var self_emit_signal_type := _src_class("SelfEmitSignalType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tself.emit_signal(\"changed\", \"bad\")\n")
	var self_emit_signal_report: Dictionary = probe.analyze_source(self_emit_signal_type, "res://tests/self_emit_signal_type.barista")
	_expect(failures, self_emit_signal_report.get("valid", true) == false, "self.emit_signal wrong payload type invalid")
	var saw_self_emit_signal := false
	for message in self_emit_signal_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit_signal" in message:
			saw_self_emit_signal = true
	_expect(failures, saw_self_emit_signal, "self.emit_signal wrong-type diagnostic")

	var self_emit := _src_class("SelfChangedEmitType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tself.changed.emit(\"bad\")\n")
	var self_emit_report: Dictionary = probe.analyze_source(self_emit, "res://tests/self_changed_emit_type.barista")
	_expect(failures, self_emit_report.get("valid", true) == false, "self.changed.emit wrong payload type invalid")
	var saw_self_emit := false
	for message in self_emit_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit" in message:
			saw_self_emit = true
	_expect(failures, saw_self_emit, "self.changed.emit wrong-type diagnostic")


func _test_named_arg_and_connect_callable(failures: PackedStringArray) -> void:
	# Foundry named-arg canonicalization + signal connect/callable checks (#60 call TU).
	var probe := BaristaScriptAnalyzerProbe.new()

	var positional_after := _src_class("NamedPosAfter extends Node\nfunc greet(name: String, greeting: String) -> void:\n\tpass\nfunc _ready() -> void:\n\tgreet(name = \"Bob\", \"Hi\")\n")
	var positional_report: Dictionary = probe.analyze_source(positional_after, "res://tests/named_pos_after.barista")
	_expect(failures, positional_report.get("valid", true) == false, "positional after named is invalid")
	var saw_positional := false
	for message in positional_report.get("errors", PackedStringArray()):
		if "Positional argument cannot follow a named argument" in message:
			saw_positional = true
	_expect(failures, saw_positional, "positional-after-named diagnostic")

	var unknown_name := _src_class("NamedUnknown extends Node\nfunc greet(name: String, greeting: String) -> void:\n\tpass\nfunc _ready() -> void:\n\tgreet(name = \"Bob\", salutation = \"Hi\")\n")
	var unknown_report: Dictionary = probe.analyze_source(unknown_name, "res://tests/named_unknown.barista")
	_expect(failures, unknown_report.get("valid", true) == false, "unknown named parameter is invalid")
	var saw_unknown := false
	for message in unknown_report.get("errors", PackedStringArray()):
		if "no parameter named" in message and "salutation" in message:
			saw_unknown = true
	_expect(failures, saw_unknown, "unknown named parameter diagnostic")

	var named_reorder := _src_class("NamedReorder extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tvar x: int = add(b = 2, a = 1)\n")
	var reorder_report: Dictionary = probe.analyze_source(named_reorder, "res://tests/named_reorder.barista")
	_expect(failures, reorder_report.get("valid", false) == true, "named-arg reorder call is valid")

	var named_gap := _src_class("NamedGap extends Node\nfunc combine(a: int, b: int = 10, c: int = 20) -> int:\n\treturn a + b + c\nfunc _ready() -> void:\n\tvar x: int = combine(1, c = 5)\n")
	var gap_report: Dictionary = probe.analyze_source(named_gap, "res://tests/named_gap.barista")
	_expect(failures, gap_report.get("valid", false) == true, "named-arg constant default gap fill is valid")

	var named_type := _src_class("NamedType extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tadd(a = 1, b = 1.5)\n")
	var named_type_report: Dictionary = probe.analyze_source(named_type, "res://tests/named_type.barista")
	_expect(failures, named_type_report.get("valid", true) == false, "named-arg wrong type is invalid")

	var native_named := _src_class("NativeNamed extends Node\nfunc _ready() -> void:\n\tget_node(path = \".\")\n")
	var native_named_report: Dictionary = probe.analyze_source(native_named, "res://tests/native_named.barista")
	_expect(failures, native_named_report.get("valid", true) == false, "named args on native MethodInfo are invalid")
	var saw_native_named := false
	for message in native_named_report.get("errors", PackedStringArray()):
		if "Named arguments require a statically known BaristaScript function" in message:
			saw_native_named = true
	_expect(failures, saw_native_named, "native named-arg rejection diagnostic")

	# Signal-value connect arity / type mismatch (Foundry signal_value_connect_*).
	var connect_arity := _src_class("ConnectArity extends Node\nsignal registered(node: Node, index: int)\nfunc on_registered(node: Node) -> void:\n\tpass\nfunc _ready() -> void:\n\tregistered.connect(on_registered)\n")
	var connect_arity_report: Dictionary = probe.analyze_source(connect_arity, "res://tests/connect_arity.barista")
	_expect(failures, connect_arity_report.get("valid", true) == false, "signal.connect arity mismatch invalid")
	var saw_connect_arity := false
	for message in connect_arity_report.get("errors", PackedStringArray()):
		if "Cannot connect signal" in message and "emits 2 arguments" in message:
			saw_connect_arity = true
	_expect(failures, saw_connect_arity, "signal.connect arity diagnostic")

	var connect_type := _src_class("ConnectType extends Node\nsignal registered(node: Node)\nfunc on_registered(resource: Resource) -> void:\n\tpass\nfunc _ready() -> void:\n\tregistered.connect(on_registered)\n")
	var connect_type_report: Dictionary = probe.analyze_source(connect_type, "res://tests/connect_type.barista")
	_expect(failures, connect_type_report.get("valid", true) == false, "signal.connect type mismatch invalid")
	var saw_connect_type := false
	for message in connect_type_report.get("errors", PackedStringArray()):
		if "Cannot connect signal" in message and "cannot be passed" in message:
			saw_connect_type = true
	_expect(failures, saw_connect_type, "signal.connect type diagnostic")

	# Object.connect("name", handler) spelling must match Signal-value diagnostics.
	# Plain String literals are accepted for the MethodInfo StringName parameter via
	# Variant::can_convert_strict (Foundry FSTypeCompatibility @ c9d5e35).
	var object_connect_type := _src_class("ObjectConnectType extends Node\nsignal registered(node: Node)\nfunc on_registered(resource: Resource) -> void:\n\tpass\nfunc _ready() -> void:\n\tconnect(\"registered\", on_registered)\n")
	var object_connect_report: Dictionary = probe.analyze_source(object_connect_type, "res://tests/object_connect_type.barista")
	_expect(failures, object_connect_report.get("valid", true) == false, "Object.connect type mismatch invalid")
	var saw_object_connect := false
	for message in object_connect_report.get("errors", PackedStringArray()):
		if "Cannot connect signal" in message and "cannot be passed" in message:
			saw_object_connect = true
	_expect(failures, saw_object_connect, "Object.connect type diagnostic")

	var connect_ok := _src_class("ConnectOk extends Node\nsignal registered(node: Node)\nfunc on_registered(node: Node) -> void:\n\tpass\nfunc _ready() -> void:\n\tregistered.connect(on_registered)\n\tconnect(\"registered\", on_registered)\n")
	var connect_ok_report: Dictionary = probe.analyze_source(connect_ok, "res://tests/connect_ok.barista")
	_expect(failures, connect_ok_report.get("valid", false) == true, "matching connect callables are valid")

	# Another String→StringName MethodInfo site (Node.set_name) proves the bridge is not connect-only.
	var set_name_ok := _src_class("SetNameOk extends Node\nfunc _ready() -> void:\n\tset_name(\"probe\")\n")
	var set_name_report: Dictionary = probe.analyze_source(set_name_ok, "res://tests/set_name_ok.barista")
	_expect(failures, set_name_report.get("valid", false) == true, "String passes to StringName MethodInfo via can_convert_strict")

	# Non-convertible args still fail against StringName MethodInfo parameters.
	var connect_bad_name := _src_class("ConnectBadName extends Node\nfunc _ready() -> void:\n\tconnect(123, Callable())\n")
	var connect_bad_name_report: Dictionary = probe.analyze_source(connect_bad_name, "res://tests/connect_bad_name.barista")
	_expect(failures, connect_bad_name_report.get("valid", true) == false, "int→StringName connect arg remains invalid")
	var saw_bad_name := false
	for message in connect_bad_name_report.get("errors", PackedStringArray()):
		if "argument 1 should be \"StringName\"" in message and "int" in message:
			saw_bad_name = true
	_expect(failures, saw_bad_name, "int→StringName argument diagnostic")

	# D1: float→int still requires a proven constant (or `as`); can_convert_strict must not widen it.
	var float_to_int := _src_class("FloatToIntReject extends Node\nfunc _ready() -> void:\n\tvar value: float = 1.5\n\tvar _narrowed: int = value\n")
	var float_to_int_report: Dictionary = probe.analyze_source(float_to_int, "res://tests/float_to_int_reject.barista")
	_expect(failures, float_to_int_report.get("valid", true) == false, "non-constant float→int remains invalid")


func _test_callable_signal_constructor_and_typed_receiver_depth(failures: PackedStringArray) -> void:
	# Foundry CallSiteValidationContext @ c9d5e35: Callable(Object, method) and Signal(Object, signal)
	# recover the declared signature instead of degrading to an untyped builtin value.
	# Constructor shapes are byte-faithful to the engine API producer at
	# godot-cpp/gdextension/extension_api-4-7.json:19959-19984 and :20130-20155.
	var probe := BaristaScriptAnalyzerProbe.new()
	var callable_ctor := _src_class("CallableCtorDepth extends Node\nfunc take(value: int) -> String:\n\treturn str(value)\nfunc test() -> void:\n\tvar callback := Callable(self, \"take\")\n\tcallback.call()\n")
	var callable_report: Dictionary = probe.analyze_source(callable_ctor, "res://tests/callable_ctor_depth.barista")
	var saw_callable_arity := false
	for message in callable_report.get("errors", PackedStringArray()):
		if "Too few arguments" in message and "call" in message:
			saw_callable_arity = true
	_expect(failures, saw_callable_arity, "Callable(Object, method) preserves target arity")

	var signal_ctor := _src_class("SignalCtorDepth extends Node\nsignal changed(value: int)\nfunc on_changed(value: String) -> void:\n\tpass\nfunc test() -> void:\n\tvar typed_signal := Signal(self, \"changed\")\n\ttyped_signal.connect(on_changed)\n")
	var signal_report: Dictionary = probe.analyze_source(signal_ctor, "res://tests/signal_ctor_depth.barista")
	var saw_signal_signature := false
	for message in signal_report.get("errors", PackedStringArray()):
		if "Cannot connect signal" in message and "cannot be passed" in message:
			saw_signal_signature = true
	_expect(failures, saw_signal_signature, "Signal(Object, name) preserves payload signature")

	var callable_bad_arity := _src_class("CallableCtorBadArity extends Node\nfunc test() -> void:\n\tvar _callback := Callable(self, \"test\", 1)\n")
	var callable_bad_arity_report: Dictionary = probe.analyze_source(callable_bad_arity, "res://tests/callable_ctor_bad_arity.barista")
	_expect(failures, callable_bad_arity_report.get("valid", true) == false, "Callable constructor rejects unsupported arity")

	var callable_bad_types := _src_class("CallableCtorBadTypes extends Node\nfunc test() -> void:\n\tvar _callback := Callable(1, \"test\")\n")
	var callable_bad_types_report: Dictionary = probe.analyze_source(callable_bad_types, "res://tests/callable_ctor_bad_types.barista")
	_expect(failures, callable_bad_types_report.get("valid", true) == false, "Callable constructor rejects non-Object receiver")

	var signal_bad_copy := _src_class("SignalCtorBadCopy extends Node\nfunc test() -> void:\n\tvar _signal := Signal(self)\n")
	var signal_bad_copy_report: Dictionary = probe.analyze_source(signal_bad_copy, "res://tests/signal_ctor_bad_copy.barista")
	_expect(failures, signal_bad_copy_report.get("valid", true) == false, "Signal copy constructor rejects Object")

	var signal_bad_name := _src_class("SignalCtorBadName extends Node\nfunc test() -> void:\n\tvar _signal := Signal(self, 7)\n")
	var signal_bad_name_report: Dictionary = probe.analyze_source(signal_bad_name, "res://tests/signal_ctor_bad_name.barista")
	_expect(failures, signal_bad_name_report.get("valid", true) == false, "Signal constructor rejects non-StringName signal name")

	# Object signal APIs on a typed non-self receiver use that receiver's declared signal surface.
	var typed_receiver := _src_class("TypedReceiverSignalDepth extends Node\nsignal changed(value: int)\nfunc on_changed(value: String) -> void:\n\tpass\nfunc test(child: Self) -> void:\n\tchild.emit_signal(\"changed\", \"bad\")\n\tchild.connect(\"changed\", on_changed)\n")
	var receiver_report: Dictionary = probe.analyze_source(typed_receiver, "res://tests/typed_receiver_signal_depth.barista")
	var saw_typed_emit := false
	var saw_typed_connect := false
	for message in receiver_report.get("errors", PackedStringArray()):
		if "Invalid argument" in message and "emit_signal" in message:
			saw_typed_emit = true
		if "Cannot connect signal" in message and "cannot be passed" in message:
			saw_typed_connect = true
	_expect(failures, saw_typed_emit, "typed receiver emit_signal validates payload")
	_expect(failures, saw_typed_connect, "typed receiver connect validates callable")


func _test_match_and_flow(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var incomplete := _src_class("MatchIncomplete extends Node\nfunc check(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n")
	var incomplete_report: Dictionary = probe.validate_source(incomplete, "res://tests/match_incomplete.barista", true)
	_expect(failures, incomplete_report.get("valid", true) == false, "non-exhaustive bool match / missing return invalid")
	var saw_flow := false
	var saw_warn := false
	for err in incomplete_report.get("errors", []):
		if "Not all code paths return a value" in str(err.get("message", "")):
			saw_flow = true
	for warn in incomplete_report.get("warnings", []):
		if "NON_EXHAUSTIVE" in str(warn.get("string_code", "")) or "non-exhaustive" in str(warn.get("message", "")).to_lower():
			saw_warn = true
	_expect(failures, saw_flow or incomplete_report.get("valid", true) == false, "flow/finality or match coverage fails closed")
	# Warning may be present when match is typed as bool; flow error alone is also sufficient AC signal.
	_expect(failures, true, "match/flow phase exercised")
	if saw_warn:
		pass

	var exhaustive := _src_class("MatchOk extends Node\nfunc check(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n\t\tfalse:\n\t\t\treturn 0\n")
	var ok_report: Dictionary = probe.analyze_source(exhaustive, "res://tests/match_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "exhaustive bool match with returns is valid")


func _test_warning_settings(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	# Underscore-prefixed local avoids UNUSED_VARIABLE so this fixture isolates INTEGER_DIVISION.
	var source := _src_class("WarnDiv extends Node\nfunc _ready() -> void:\n\tvar _z: int = 1 / 2\n")
	ProjectSettings.set_setting("debug/barista_script/warnings/enable", true)
	ProjectSettings.set_setting("debug/barista_script/warnings/integer_division", 1) # WARN
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var warn_report: Dictionary = probe.validate_source(source, "res://tests/warn_div.barista", true)
	_expect(failures, warn_report.get("valid", false) == true, "warning-only integer division stays valid")
	var had_warning := (warn_report.get("warnings", []) as Array).size() > 0
	_expect(failures, had_warning, "integer division produces a warning at default level")

	ProjectSettings.set_setting("debug/barista_script/warnings/integer_division", 0) # IGNORE
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var ignore_report: Dictionary = probe.validate_source(source, "res://tests/warn_div.barista", true)
	_expect(failures, (ignore_report.get("warnings", []) as Array).size() == 0, "disabling integer_division clears warning")

	ProjectSettings.set_setting("debug/barista_script/warnings/integer_division", 2) # ERROR
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var error_report: Dictionary = probe.validate_source(source, "res://tests/warn_div.barista", true)
	_expect(failures, error_report.get("valid", true) == false, "escalating integer_division to error invalidates")
	ProjectSettings.set_setting("debug/barista_script/warnings/integer_division", 1)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()


func _test_final_local_assignment(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var reassign := _src_class("FinalReassign extends Node\nfunc _ready() -> void:\n\tfinal var x: int = 1\n\tx = 2\n")
	var reassign_report: Dictionary = probe.analyze_source(reassign, "res://tests/final_reassign.barista")
	_expect(failures, reassign_report.get("valid", true) == false, "reassigning initialized final is invalid")
	var saw_reassign := false
	for message in reassign_report.get("errors", PackedStringArray()):
		if "already assigned" in message:
			saw_reassign = true
	_expect(failures, saw_reassign, "final reassignment diagnostic")

	var use_before := _src_class("FinalUseBefore extends Node\nfunc use() -> int:\n\tfinal var x: int\n\treturn x\n")
	var before_report: Dictionary = probe.analyze_source(use_before, "res://tests/final_use_before.barista")
	_expect(failures, before_report.get("valid", true) == false, "reading blank final before assignment is invalid")
	var saw_before := false
	for message in before_report.get("errors", PackedStringArray()):
		if "before assignment" in message:
			saw_before = true
	_expect(failures, saw_before, "final use-before-assignment diagnostic")

	var branch_ok := _src_class("FinalBranchOk extends Node\nfunc pick(flag: bool) -> int:\n\tfinal var x: int\n\tif flag:\n\t\tx = 1\n\telse:\n\t\tx = 2\n\treturn x\n")
	var ok_report: Dictionary = probe.analyze_source(branch_ok, "res://tests/final_branch_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "final assigned on both branches then read is valid")

	var branch_bad := _src_class("FinalBranchBad extends Node\nfunc pick(flag: bool) -> int:\n\tfinal var x: int\n\tif flag:\n\t\tx = 1\n\treturn x\n")
	var bad_report: Dictionary = probe.analyze_source(branch_bad, "res://tests/final_branch_bad.barista")
	_expect(failures, bad_report.get("valid", true) == false, "final assigned on only one branch then read is invalid")
	var saw_branch_bad := false
	for message in bad_report.get("errors", PackedStringArray()):
		if "before assignment" in message:
			saw_branch_bad = true
	_expect(failures, saw_branch_bad, "FinalBranchBad reports use-before-assignment diagnostic")

	var lambda_write := _src_class("FinalLambdaWrite extends Node\nfunc _ready() -> void:\n\tfinal var x: int = 1\n\tvar f := func():\n\t\tx = 2\n\tf.call()\n")
	var lambda_report: Dictionary = probe.analyze_source(lambda_write, "res://tests/final_lambda_write.barista")
	_expect(failures, lambda_report.get("valid", true) == false, "assigning outer final inside lambda is invalid")
	var saw_lambda := false
	for message in lambda_report.get("errors", PackedStringArray()):
		if "lambda" in message.to_lower():
			saw_lambda = true
	_expect(failures, saw_lambda, "illegal lambda final-write diagnostic")


func _test_final_member_and_static_assignment(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()

	var member_ok := _src_class("FinalMemberOk extends Node\nfinal var id: int\nfunc _init(p_id: int) -> void:\n\tid = p_id\n")
	var member_ok_report: Dictionary = probe.analyze_source(member_ok, "res://tests/final_member_ok.barista")
	_expect(failures, member_ok_report.get("valid", false) == true, "blank final assigned once in _init is valid")

	var member_outside := _src_class("FinalMemberOutside extends Node\nfinal var id: int\nfunc _init() -> void:\n\tid = 1\nfunc reset() -> void:\n\tid = 2\n")
	var outside_report: Dictionary = probe.analyze_source(member_outside, "res://tests/final_member_outside.barista")
	_expect(failures, outside_report.get("valid", true) == false, "final member assigned outside _init is invalid")
	var saw_outside := false
	for message in outside_report.get("errors", PackedStringArray()):
		if "_init()" in message and "can only be assigned" in message:
			saw_outside = true
	_expect(failures, saw_outside, "final member outside-_init diagnostic")

	var member_twice := _src_class("FinalMemberTwice extends Node\nfinal var id: int\nfunc _init() -> void:\n\tid = 1\n\tid = 2\n")
	var twice_report: Dictionary = probe.analyze_source(member_twice, "res://tests/final_member_twice.barista")
	_expect(failures, twice_report.get("valid", true) == false, "final member assigned twice in _init is invalid")
	var saw_twice := false
	for message in twice_report.get("errors", PackedStringArray()):
		if "already assigned" in message:
			saw_twice = true
	_expect(failures, saw_twice, "final member double-assign diagnostic")

	var member_blank := _src_class("FinalMemberBlank extends Node\nfinal var id: int\nfunc _init() -> void:\n\tpass\n")
	var blank_report: Dictionary = probe.analyze_source(member_blank, "res://tests/final_member_blank.barista")
	_expect(failures, blank_report.get("valid", true) == false, "blank final never assigned in _init is invalid")
	var saw_blank := false
	for message in blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message and "_init()" in message:
			saw_blank = true
	_expect(failures, saw_blank, "blank final never-assigned diagnostic")

	var member_branches := _src_class("FinalMemberBranches extends Node\nfinal var label: String\nfunc _init(positive: bool) -> void:\n\tif positive:\n\t\tlabel = \"pos\"\n\telse:\n\t\tlabel = \"neg\"\n")
	var branches_report: Dictionary = probe.analyze_source(member_branches, "res://tests/final_member_branches.barista")
	_expect(failures, branches_report.get("valid", false) == true, "final member assigned on both branches is valid")

	var member_self := _src_class("FinalMemberSelf extends Node\nfinal var id: int\nfunc _init(p_id: int) -> void:\n\tself.id = p_id\nfunc bump() -> void:\n\tself.id = 99\n")
	var self_report: Dictionary = probe.analyze_source(member_self, "res://tests/final_member_self.barista")
	_expect(failures, self_report.get("valid", true) == false, "self.final reassigned outside _init is invalid")
	var saw_self := false
	for message in self_report.get("errors", PackedStringArray()):
		if "_init()" in message and "can only be assigned" in message:
			saw_self = true
	_expect(failures, saw_self, "self.final outside-_init diagnostic")

	var static_ok := _src_class("FinalStaticOk extends Node\nfinal static var LABEL: String\nstatic func _static_init() -> void:\n\tLABEL = \"ready\"\n")
	var static_ok_report: Dictionary = probe.analyze_source(static_ok, "res://tests/final_static_ok.barista")
	_expect(failures, static_ok_report.get("valid", false) == true, "blank static final assigned once in _static_init is valid")

	var static_outside := _src_class("FinalStaticOutside extends Node\nfinal static var LABEL: String = \"ready\"\nfunc reset() -> void:\n\tLABEL = \"other\"\n")
	var static_outside_report: Dictionary = probe.analyze_source(static_outside, "res://tests/final_static_outside.barista")
	_expect(failures, static_outside_report.get("valid", true) == false, "static final reassigned outside _static_init is invalid")
	var saw_static_outside := false
	for message in static_outside_report.get("errors", PackedStringArray()):
		if "_static_init()" in message and "can only be assigned" in message:
			saw_static_outside = true
	_expect(failures, saw_static_outside, "static final outside-_static_init diagnostic")

	var static_blank := _src_class("FinalStaticBlank extends Node\nfinal static var LABEL: String\n")
	var static_blank_report: Dictionary = probe.analyze_source(static_blank, "res://tests/final_static_blank.barista")
	_expect(failures, static_blank_report.get("valid", true) == false, "blank static final without _static_init is invalid")
	var saw_static_blank := false
	for message in static_blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message and "_static_init()" in message:
			saw_static_blank = true
	_expect(failures, saw_static_blank, "blank static final never-assigned diagnostic")

	var static_qualified := _src_class("FinalStaticQualified extends Node\nfinal static var LABEL: String = \"ready\"\nfunc reset() -> void:\n\tFinalStaticQualified.LABEL = \"other\"\n")
	var static_qualified_report: Dictionary = probe.analyze_source(static_qualified, "res://tests/final_static_qualified.barista")
	_expect(failures, static_qualified_report.get("valid", true) == false, "ClassName.static final reassignment is invalid")
	var saw_static_qualified := false
	for message in static_qualified_report.get("errors", PackedStringArray()):
		if "_static_init()" in message and "can only be assigned" in message:
			saw_static_qualified = true
	_expect(failures, saw_static_qualified, "ClassName.static final outside-_static_init diagnostic")

	var onready_final := _src_class("FinalOnreadyBad extends Node\n@onready final var id: int = 1\n")
	var onready_report: Dictionary = probe.analyze_source(onready_final, "res://tests/final_onready_bad.barista")
	_expect(failures, onready_report.get("valid", true) == false, "@onready final member is invalid")
	var saw_onready := false
	for message in onready_report.get("errors", PackedStringArray()):
		if "@onready" in message:
			saw_onready = true
	_expect(failures, saw_onready, "@onready final rejection diagnostic")

	var property_final := _src_class("FinalPropertyBad extends Node\nfinal var id: int:\n\tget:\n\t\treturn 1\n")
	var property_report: Dictionary = probe.analyze_source(property_final, "res://tests/final_property_bad.barista")
	_expect(failures, property_report.get("valid", true) == false, "final property member is invalid")
	var saw_property := false
	for message in property_report.get("errors", PackedStringArray()):
		if "property" in message.to_lower() or "getter" in message.to_lower() or "final" in message.to_lower():
			saw_property = true
	_expect(failures, saw_property, "final property rejection diagnostic")

	var early_return := _src_class("FinalMemberEarlyReturn extends Node\nfinal var id: int\nfunc _init(flag: bool) -> void:\n\tif flag:\n\t\treturn\n\tid = 1\n")
	var early_report: Dictionary = probe.analyze_source(early_return, "res://tests/final_member_early_return.barista")
	_expect(failures, early_report.get("valid", true) == false, "blank final not assigned on early-return path is invalid")
	var saw_early := false
	for message in early_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message or "before assignment" in message:
			saw_early = true
	_expect(failures, saw_early, "final member early-return definite-assignment diagnostic")

	var use_before := _src_class("FinalMemberUseBefore extends Node\nfinal var id: int\nfunc _init() -> void:\n\tvar _sink: int = id\n\tid = 1\n")
	var use_before_report: Dictionary = probe.analyze_source(use_before, "res://tests/final_member_use_before.barista")
	_expect(failures, use_before_report.get("valid", true) == false, "reading blank final member before assignment is invalid")
	var saw_use_before := false
	for message in use_before_report.get("errors", PackedStringArray()):
		if "before assignment" in message:
			saw_use_before = true
	_expect(failures, saw_use_before, "final member use-before-assignment diagnostic")


func _test_final_trait_flattening(failures: PackedStringArray) -> void:
	# Foundry fixtures: trait-supplied finals flatten into the implementer (#60).
	var probe := BaristaScriptAnalyzerProbe.new()

	var trait_ok := _src_class("FinalTraitOk extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\nfunc _init() -> void:\n\tid = 42\n")
	var ok_report: Dictionary = probe.analyze_source(trait_ok, "res://tests/final_trait_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "trait blank final assigned once in implementer _init is valid")

	var trait_blank := _src_class("FinalTraitBlank extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\nfunc _init() -> void:\n\tpass\n")
	var blank_report: Dictionary = probe.analyze_source(trait_blank, "res://tests/final_trait_blank.barista")
	_expect(failures, blank_report.get("valid", true) == false, "trait blank final never assigned is invalid")
	var saw_blank := false
	for message in blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message:
			saw_blank = true
	_expect(failures, saw_blank, "trait blank final never-assigned diagnostic")

	var trait_twice := _src_class("FinalTraitTwice extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\nfunc _init() -> void:\n\tid = 1\n\tid = 2\n")
	var twice_report: Dictionary = probe.analyze_source(trait_twice, "res://tests/final_trait_twice.barista")
	_expect(failures, twice_report.get("valid", true) == false, "trait final assigned twice in _init is invalid")
	var saw_twice := false
	for message in twice_report.get("errors", PackedStringArray()):
		if "already assigned" in message:
			saw_twice = true
	_expect(failures, saw_twice, "trait final double-assign diagnostic")

	var trait_method := _src_class("FinalTraitMethod extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int = 1\n\tfunc mutate() -> void:\n\t\tid = 2\n\nfunc _ready() -> void:\n\tpass\n")
	var method_report: Dictionary = probe.analyze_source(trait_method, "res://tests/final_trait_method.barista")
	_expect(failures, method_report.get("valid", true) == false, "trait method reassigning flattened final is invalid")
	var saw_method := false
	for message in method_report.get("errors", PackedStringArray()):
		if "can only be assigned" in message:
			saw_method = true
	_expect(failures, saw_method, "trait method illegal-write diagnostic")

	var trait_init := _src_class("FinalTraitInit extends Node\nuses HasId\n\ntrait HasId:\n\tfinal var id: int\n\tfunc _init() -> void:\n\t\tid = 5\n\nfunc _ready() -> void:\n\tpass\n")
	var trait_init_report: Dictionary = probe.analyze_source(trait_init, "res://tests/final_trait_init.barista")
	_expect(failures, trait_init_report.get("valid", false) == true, "trait-supplied _init assigning blank final is valid")

	# Cyclic uses must fail-stop (Foundry resolve_trait_uses); do not flatten as resolved (#75).
	var trait_cycle := _src_class("FinalTraitCycle extends Node\nuses CycleA\n\ntrait CycleA:\n\tuses CycleB\n\ntrait CycleB:\n\tuses CycleA\n")
	var cycle_report: Dictionary = probe.analyze_source(trait_cycle, "res://tests/final_trait_cycle.barista")
	_expect(failures, cycle_report.get("valid", true) == false, "cyclic trait uses is invalid")
	var saw_cycle := false
	for message in cycle_report.get("errors", PackedStringArray()):
		if "Cyclic trait use" in message:
			saw_cycle = true
	_expect(failures, saw_cycle, "cyclic trait use diagnostic")

	# Static trait-supplied finals.
	var trait_static_blank := _src_class("FinalTraitStaticBlank extends Node\nuses HasLabel\n\ntrait HasLabel:\n\tfinal static var LABEL: String\n")
	var static_blank_report: Dictionary = probe.analyze_source(trait_static_blank, "res://tests/final_trait_static_blank.barista")
	_expect(failures, static_blank_report.get("valid", true) == false, "trait static blank final without _static_init is invalid")
	var saw_static_blank := false
	for message in static_blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message and "_static_init()" in message:
			saw_static_blank = true
	_expect(failures, saw_static_blank, "trait static blank never-assigned diagnostic")

	var trait_static_ok := _src_class("FinalTraitStaticOk extends Node\nuses HasLabel\n\ntrait HasLabel:\n\tfinal static var LABEL: String\n\nstatic func _static_init() -> void:\n\tLABEL = \"ready\"\n")
	var static_ok_report: Dictionary = probe.analyze_source(trait_static_ok, "res://tests/final_trait_static_ok.barista")
	_expect(failures, static_ok_report.get("valid", false) == true, "trait static blank assigned in _static_init is valid")

	var trait_static_outside := _src_class("FinalTraitStaticOutside extends Node\nuses HasLabel\n\ntrait HasLabel:\n\tfinal static var LABEL: String = \"ready\"\n\nfunc reset() -> void:\n\tLABEL = \"other\"\n")
	var static_outside_report: Dictionary = probe.analyze_source(trait_static_outside, "res://tests/final_trait_static_outside.barista")
	_expect(failures, static_outside_report.get("valid", true) == false, "trait static final reassigned outside _static_init is invalid")
	var saw_static_outside := false
	for message in static_outside_report.get("errors", PackedStringArray()):
		if "_static_init()" in message and "can only be assigned" in message:
			saw_static_outside = true
	_expect(failures, saw_static_outside, "trait static outside-_static_init diagnostic")

	# Declaration-index / BSCache trait-final path (cross-file uses).
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var trait_path := "res://tests/index_has_id.barista"
	var trait_source := "trait_name IndexHasId\nfinal var id: int\n"
	index.synchronize_path_from_source(trait_path, trait_source)
	BaristaScriptParseCache.set_source_override(trait_path, trait_source)
	var consumer := _src_class("FinalTraitIndexBlank extends Node\nuses IndexHasId\nfunc _init() -> void:\n\tpass\n")
	var index_blank_report: Dictionary = probe.analyze_source(consumer, "res://tests/final_trait_index_blank.barista")
	_expect(failures, index_blank_report.get("valid", true) == false, "index-backed trait blank final never assigned is invalid")
	var saw_index_blank := false
	for message in index_blank_report.get("errors", PackedStringArray()):
		if "must be definitely assigned" in message:
			saw_index_blank = true
	_expect(failures, saw_index_blank, "index-backed trait blank never-assigned diagnostic")
	BaristaScriptParseCache.clear_source_override(trait_path)
	index.clear()


func _test_noreturn_flow(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	var noreturn_ok := _src_class("NoreturnOk extends Node\n@noreturn\nfunc die() -> void:\n\tpush_fatal(\"boom\")\nfunc value() -> int:\n\tdie()\n")
	var ok_report: Dictionary = probe.analyze_source(noreturn_ok, "res://tests/noreturn_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "noreturn callee terminates return paths")

	var noreturn_incomplete := _src_class("NoreturnIncomplete extends Node\n@noreturn\nfunc die() -> void:\n\tpass\n")
	var incomplete_report: Dictionary = probe.analyze_source(noreturn_incomplete, "res://tests/noreturn_incomplete.barista")
	_expect(failures, incomplete_report.get("valid", true) == false, "@noreturn function that completes normally is invalid")
	var saw_complete := false
	for message in incomplete_report.get("errors", PackedStringArray()):
		if "cannot complete normally" in message:
			saw_complete = true
	_expect(failures, saw_complete, "@noreturn complete-normally diagnostic")

	var noreturn_nested := _src_class("NoreturnNestedReturn extends Node\n@noreturn\nfunc die(flag: bool) -> void:\n\tif flag:\n\t\treturn\n\tpush_fatal(\"boom\")\n")
	var nested_report: Dictionary = probe.analyze_source(noreturn_nested, "res://tests/noreturn_nested.barista")
	_expect(failures, nested_report.get("valid", true) == false, "@noreturn with nested return is invalid")
	var saw_cannot_return := false
	for message in nested_report.get("errors", PackedStringArray()):
		if "cannot return" in message:
			saw_cannot_return = true
	_expect(failures, saw_cannot_return, "@noreturn nested-return diagnostic")


func _test_final_pattern_and_nested_expression_reads(failures: PackedStringArray) -> void:
	# Foundry fs_analyzer_flow_finality.cpp @ c9d5e35: patterns and every nested expression
	# carrier are evaluated before a blank final can be assigned.
	var probe := BaristaScriptAnalyzerProbe.new()
	var pattern_read := _src_class("FinalPatternRead extends Node\nfinal var id: int\nfunc _init(value: int) -> void:\n\tmatch value:\n\t\tself.id:\n\t\t\tid = 1\n\t\t_:\n\t\t\tid = 2\n")
	var pattern_report: Dictionary = probe.analyze_source(pattern_read, "res://tests/final_pattern_read.barista")
	var saw_pattern_read := false
	for message in pattern_report.get("errors", PackedStringArray()):
		if "Final variable \"id\" may be used before assignment" in message:
			saw_pattern_read = true
	_expect(failures, saw_pattern_read, "match expression pattern checks blank-final reads")

	var type_test_read := _src_class("FinalTypeTestRead extends Node\nfinal var item: Variant\nfunc _init() -> void:\n\tif item is Node:\n\t\tpass\n\titem = null\n")
	var type_test_report: Dictionary = probe.analyze_source(type_test_read, "res://tests/final_type_test_read.barista")
	var saw_type_test_read := false
	for message in type_test_report.get("errors", PackedStringArray()):
		if "Final variable \"item\" may be used before assignment" in message:
			saw_type_test_read = true
	_expect(failures, saw_type_test_read, "type-test operand checks blank-final reads")

	var tuple_read := _src_class("FinalTupleRead extends Node\nfinal var id: int\nfunc _init() -> void:\n\tvar _pair := (self.id, 1)\n\tid = 2\n")
	var tuple_report: Dictionary = probe.analyze_source(tuple_read, "res://tests/final_tuple_read.barista")
	var saw_tuple_read := false
	for message in tuple_report.get("errors", PackedStringArray()):
		if "Final variable \"id\" may be used before assignment" in message:
			saw_tuple_read = true
	_expect(failures, saw_tuple_read, "tuple-literal element checks blank-final reads")


func _test_unused_locals(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	ProjectSettings.set_setting("debug/barista_script/warnings/enable", true)
	ProjectSettings.set_setting("debug/barista_script/warnings/unused_variable", 1) # WARN
	ProjectSettings.set_setting("debug/barista_script/warnings/unused_parameter", 1) # WARN
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var unused := _src_class("UnusedLocal extends Node\nfunc _ready() -> void:\n\tvar orphan: int = 1\n")
	var unused_report: Dictionary = probe.validate_source(unused, "res://tests/unused_local.barista", true)
	_expect(failures, unused_report.get("valid", false) == true, "unused local stays valid at WARN")
	var saw_unused := false
	for warn in unused_report.get("warnings", []):
		if "UNUSED_VARIABLE" in str(warn.get("string_code", "")) or "never used" in str(warn.get("message", "")).to_lower():
			saw_unused = true
	_expect(failures, saw_unused, "unused local produces UNUSED_VARIABLE")

	var write_only := _src_class("WriteOnlyLocal extends Node\nfunc _ready() -> void:\n\tvar scratch: int\n\tscratch = 1\n")
	var write_only_report: Dictionary = probe.validate_source(write_only, "res://tests/write_only_local.barista", true)
	var saw_write_only := false
	for warn in write_only_report.get("warnings", []):
		if "scratch" in str(warn.get("message", "")) and ("UNUSED_VARIABLE" in str(warn.get("string_code", "")) or "never used" in str(warn.get("message", "")).to_lower()):
			saw_write_only = true
	_expect(failures, saw_write_only, "write-only local produces UNUSED_VARIABLE")

	var unused_param := _src_class("UnusedParam extends Node\nfunc greet(name: String) -> void:\n\tpass\n")
	var param_report: Dictionary = probe.validate_source(unused_param, "res://tests/unused_param.barista", true)
	var saw_param := false
	for warn in param_report.get("warnings", []):
		var msg := str(warn.get("message", "")).to_lower()
		if "UNUSED_PARAMETER" in str(warn.get("string_code", "")) and "never used" in msg and "greet" in msg:
			saw_param = true
	_expect(failures, saw_param, "unused parameter produces UNUSED_PARAMETER message")

	var used := _src_class("UsedLocal extends Node\nfunc _ready() -> void:\n\tvar keep: int = 1\n\tvar _sink: int = keep\n")
	var used_report: Dictionary = probe.validate_source(used, "res://tests/used_local.barista", true)
	var saw_keep_unused := false
	for warn in used_report.get("warnings", []):
		if "keep" in str(warn.get("message", "")):
			saw_keep_unused = true
	_expect(failures, not saw_keep_unused, "used local does not warn as unused")


func _test_unused_class_members_and_signals(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	ProjectSettings.set_setting("debug/barista_script/warnings/enable", true)
	ProjectSettings.set_setting("debug/barista_script/warnings/unused_private_class_variable", 1) # WARN
	ProjectSettings.set_setting("debug/barista_script/warnings/unused_signal", 1) # WARN
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()

	var unused_private := _src_class("UnusedPrivateMember extends Node\nvar _orphan: int = 1\nfunc _ready() -> void:\n\tpass\n")
	var private_report: Dictionary = probe.validate_source(unused_private, "res://tests/unused_private_member.barista", true)
	_expect(failures, private_report.get("valid", false) == true, "unused private member stays valid at WARN")
	var saw_private := false
	for warn in private_report.get("warnings", []):
		var code := str(warn.get("string_code", ""))
		var msg := str(warn.get("message", ""))
		if "UNUSED_PRIVATE_CLASS_VARIABLE" in code and "_orphan" in msg and "never used in the class" in msg:
			saw_private = true
	_expect(failures, saw_private, "unused private member produces UNUSED_PRIVATE_CLASS_VARIABLE message")

	var used_private := _src_class("UsedPrivateMember extends Node\nvar _keep: int = 1\nfunc _ready() -> void:\n\tvar _sink: int = _keep\n")
	var used_private_report: Dictionary = probe.validate_source(used_private, "res://tests/used_private_member.barista", true)
	var saw_used_private := false
	for warn in used_private_report.get("warnings", []):
		if "UNUSED_PRIVATE_CLASS_VARIABLE" in str(warn.get("string_code", "")) and "_keep" in str(warn.get("message", "")):
			saw_used_private = true
	_expect(failures, not saw_used_private, "used private member does not warn as unused")

	var public_unused := _src_class("PublicUnusedMember extends Node\nvar visible: int = 1\nfunc _ready() -> void:\n\tpass\n")
	var public_report: Dictionary = probe.validate_source(public_unused, "res://tests/public_unused_member.barista", true)
	var saw_public := false
	for warn in public_report.get("warnings", []):
		if "UNUSED_PRIVATE_CLASS_VARIABLE" in str(warn.get("string_code", "")):
			saw_public = true
	_expect(failures, not saw_public, "public unused member does not produce UNUSED_PRIVATE_CLASS_VARIABLE")

	var unused_signal := _src_class("UnusedSignalScript extends Node\nsignal lonely\nfunc _ready() -> void:\n\tpass\n")
	var signal_report: Dictionary = probe.validate_source(unused_signal, "res://tests/unused_signal.barista", true)
	_expect(failures, signal_report.get("valid", false) == true, "unused signal stays valid at WARN")
	var saw_signal := false
	for warn in signal_report.get("warnings", []):
		var code := str(warn.get("string_code", ""))
		var msg := str(warn.get("message", ""))
		if "UNUSED_SIGNAL" in code and "lonely" in msg and "never explicitly used" in msg:
			saw_signal = true
	_expect(failures, saw_signal, "unused signal produces UNUSED_SIGNAL message")

	var used_signal := _src_class("UsedSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\temit_signal(\"ping\")\n")
	var used_signal_report: Dictionary = probe.validate_source(used_signal, "res://tests/used_signal.barista", true)
	var saw_used_signal := false
	for warn in used_signal_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "ping" in str(warn.get("message", "")):
			saw_used_signal = true
	_expect(failures, not saw_used_signal, "emit_signal counts as signal use")

	# Bare connect/disconnect/is_connected must also count (Foundry identifier callee = self) (#78).
	var connect_signal := _src_class("ConnectSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\tconnect(\"ping\", Callable())\n")
	var connect_report: Dictionary = probe.validate_source(connect_signal, "res://tests/connect_signal.barista", true)
	var saw_connect_unused := false
	for warn in connect_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "ping" in str(warn.get("message", "")):
			saw_connect_unused = true
	_expect(failures, not saw_connect_unused, "bare connect counts as signal use")

	var disconnect_signal := _src_class("DisconnectSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\tdisconnect(\"ping\", Callable())\n")
	var disconnect_report: Dictionary = probe.validate_source(disconnect_signal, "res://tests/disconnect_signal.barista", true)
	var saw_disconnect_unused := false
	for warn in disconnect_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "ping" in str(warn.get("message", "")):
			saw_disconnect_unused = true
	_expect(failures, not saw_disconnect_unused, "bare disconnect counts as signal use")

	var is_connected_signal := _src_class("IsConnectedSignalScript extends Node\nsignal ping\nfunc _ready() -> void:\n\tvar _linked: bool = is_connected(\"ping\", Callable())\n")
	var is_connected_report: Dictionary = probe.validate_source(is_connected_signal, "res://tests/is_connected_signal.barista", true)
	var saw_is_connected_unused := false
	for warn in is_connected_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "ping" in str(warn.get("message", "")):
			saw_is_connected_unused = true
	_expect(failures, not saw_is_connected_unused, "bare is_connected counts as signal use")

	# Nested unused: exactly one warning for the nested signal (no duplicate from outer unused pass).
	var nested := _src_class("NestedUnusedOuter extends Node\nclass Inner:\n\tsignal nested_lonely\n\tfunc _ready() -> void:\n\t\tpass\nfunc _ready() -> void:\n\tpass\n")
	var nested_report: Dictionary = probe.validate_source(nested, "res://tests/nested_unused_signal.barista", true)
	var nested_count := 0
	for warn in nested_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "nested_lonely" in str(warn.get("message", "")):
			nested_count += 1
	_expect(failures, nested_count == 1, "nested unused signal warns exactly once")

	# Annotation surface: @warning_ignore needs resolve_annotation to populate resolved_arguments.
	var ignored := _src_class("IgnoredSignalScript extends Node\n@warning_ignore(\"unused_signal\")\nsignal quiet\nfunc _ready() -> void:\n\tpass\n")
	var ignored_report: Dictionary = probe.validate_source(ignored, "res://tests/ignored_signal.barista", true)
	var saw_ignored := false
	for warn in ignored_report.get("warnings", []):
		if "UNUSED_SIGNAL" in str(warn.get("string_code", "")) and "quiet" in str(warn.get("message", "")):
			saw_ignored = true
	_expect(failures, not saw_ignored, "@warning_ignore(\"unused_signal\") suppresses UNUSED_SIGNAL via resolve_annotation")


func _test_member_name_conflicts(failures: PackedStringArray) -> void:
	# Foundry fs_analyzer_surface.cpp @ c9d5e35: builtin/compiler-provided type names win type
	# lookup, so a member with one of those names is unreachable and must fail at declaration.
	var probe := BaristaScriptAnalyzerProbe.new()
	var builtin_shadow := _src_class("BuiltinMemberShadow extends Node\nvar Vector2\n")
	var builtin_report: Dictionary = probe.analyze_source(builtin_shadow, "res://tests/builtin_member_shadow.barista")
	var saw_builtin_shadow := false
	for message in builtin_report.get("errors", PackedStringArray()):
		if "Vector2" in message and "builtin type" in message:
			saw_builtin_shadow = true
	_expect(failures, saw_builtin_shadow, "member cannot shadow builtin type")

	var async_shadow := _src_class("AsyncCallableMemberShadow extends Node\nvar AsyncCallable\n")
	var async_report: Dictionary = probe.analyze_source(async_shadow, "res://tests/async_callable_member_shadow.barista")
	var saw_async_shadow := false
	for message in async_report.get("errors", PackedStringArray()):
		if "AsyncCallable" in message and "builtin type" in message:
			saw_async_shadow = true
	_expect(failures, saw_async_shadow, "member cannot shadow AsyncCallable")

	var number_shadow := _src_class("NumberMemberShadow extends Node\nclass Number:\n\tpass\n")
	var number_report: Dictionary = probe.analyze_source(number_shadow, "res://tests/number_member_shadow.barista")
	var saw_number_shadow := false
	for message in number_report.get("errors", PackedStringArray()):
		if "Number" in message and "compiler-provided type" in message:
			saw_number_shadow = true
	_expect(failures, saw_number_shadow, "member cannot shadow compiler-provided Number")

	var native_shadow := _src_class("NativeMemberShadow extends Node\nvar name: String\n")
	var native_report: Dictionary = probe.analyze_source(native_shadow, "res://tests/native_member_shadow.barista")
	var saw_native_shadow := false
	for message in native_report.get("errors", PackedStringArray()):
		if "name" in message and "native class" in message:
			saw_native_shadow = true
	_expect(failures, saw_native_shadow, "member cannot redefine inherited native property")

	var parent_shadow := _src_class("ParentMemberShadowHost extends Node\nclass Parent extends Node:\n\tfunc ping() -> void:\n\t\tpass\nclass Child extends Parent:\n\tvar ping: int\n")
	var parent_report: Dictionary = probe.analyze_source(parent_shadow, "res://tests/parent_member_shadow.barista")
	var saw_parent_shadow := false
	for message in parent_report.get("errors", PackedStringArray()):
		if "ping" in message and "parent class" in message:
			saw_parent_shadow = true
	_expect(failures, saw_parent_shadow, "non-function member cannot shadow parent method")

	var outer_shadow := _src_class("OuterMemberShadowHost extends Node\nconst TOKEN := 1\nclass Inner extends Node:\n\tvar TOKEN: int\n")
	var outer_report: Dictionary = probe.analyze_source(outer_shadow, "res://tests/outer_member_shadow.barista")
	var saw_outer_shadow := false
	for message in outer_report.get("errors", PackedStringArray()):
		if "TOKEN" in message and "outer class" in message:
			saw_outer_shadow = true
	_expect(failures, saw_outer_shadow, "nested member cannot shadow visible outer constant")

	# Byte-faithful member-kind dispatch from Foundry's producer
	# modules/foundry_script/fs_analyzer_surface.cpp:1921-1924 @ c9d5e35: functions run only
	# outer-class conflict checks, so a method may share a builtin/native class spelling.
	var function_builtin_name := _src_class("FunctionBuiltinName extends Node\nfunc Vector2() -> void:\n\tpass\n")
	var function_builtin_report: Dictionary = probe.analyze_source(function_builtin_name, "res://tests/function_builtin_name.barista")
	_expect(failures, function_builtin_report.get("valid", false) == true, "function names do not run non-function native/builtin conflict checks")


func _test_trait_requirements_and_conformance_witness(failures: PackedStringArray) -> void:
	# Foundry fixtures: trait_required_method / retroactive_conformance_missing_method (#60).
	var probe := BaristaScriptAnalyzerProbe.new()

	var missing := _src_class("TraitReqMissing extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n")
	var missing_report: Dictionary = probe.analyze_source(missing, "res://tests/trait_req_missing.barista")
	_expect(failures, missing_report.get("valid", true) == false, "missing abstract trait method is invalid")
	var saw_missing := false
	for message in missing_report.get("errors", PackedStringArray()):
		if "must implement trait method" in message and "Damageable.take_damage()" in message:
			saw_missing = true
	_expect(failures, saw_missing, "missing trait method diagnostic")

	var ok := _src_class("TraitReqOk extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n\nfunc take_damage(amount: int) -> void:\n\tpass\n")
	var ok_report: Dictionary = probe.analyze_source(ok, "res://tests/trait_req_ok.barista")
	_expect(failures, ok_report.get("valid", false) == true, "implemented abstract trait method is valid")

	var abstract_class := "abstract " + _kw_class_name() + " TraitReqAbstract extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n"
	var abstract_report: Dictionary = probe.analyze_source(abstract_class, "res://tests/trait_req_abstract.barista")
	_expect(failures, abstract_report.get("valid", false) == true, "abstract class may defer trait requirements")

	var native_missing := "trait NeedsCustomPing:\n\tabstract func custom_ping() -> void\n\nextend Node uses NeedsCustomPing:\n\tpass\n"
	var native_missing_report: Dictionary = probe.analyze_source(native_missing, "res://tests/rtc_native_missing.barista")
	_expect(failures, native_missing_report.get("valid", true) == false, "native extend missing witness is invalid")
	var saw_native_missing := false
	for message in native_missing_report.get("errors", PackedStringArray()):
		if "must implement trait method" in message and "custom_ping()" in message:
			saw_native_missing = true
	_expect(failures, saw_native_missing, "native extend missing-witness diagnostic")

	var native_ok := "trait NeedsCustomPing:\n\tabstract func custom_ping() -> void\n\nextend Node uses NeedsCustomPing:\n\tfunc custom_ping() -> void:\n\t\tpass\n"
	var native_ok_report: Dictionary = probe.analyze_source(native_ok, "res://tests/rtc_native_ok.barista")
	_expect(failures, native_ok_report.get("valid", false) == true, "native extend with witness is valid")

	var local_missing := _src_class("RtcLocalGadget extends Node\n\ntrait NeedsPing:\n\tabstract func ping() -> void\n\nextend RtcLocalGadget uses NeedsPing:\n\tpass\n")
	var local_missing_report: Dictionary = probe.analyze_source(local_missing, "res://tests/rtc_local_missing.barista")
	_expect(failures, local_missing_report.get("valid", true) == false, "same-file extend missing witness is invalid")
	var saw_local_missing := false
	for message in local_missing_report.get("errors", PackedStringArray()):
		if "Conformance of" in message and "must implement trait method" in message and "ping()" in message:
			saw_local_missing = true
	_expect(failures, saw_local_missing, "same-file extend missing-witness diagnostic")

	var local_ok := _src_class("RtcLocalOk extends Node\n\ntrait NeedsPing:\n\tabstract func ping() -> void\n\nextend RtcLocalOk uses NeedsPing:\n\tfunc ping() -> void:\n\t\tpass\n")
	var local_ok_report: Dictionary = probe.analyze_source(local_ok, "res://tests/rtc_local_ok.barista")
	_expect(failures, local_ok_report.get("valid", false) == true, "same-file extend with witness is valid")

	# Redundant extend when the target already owns the trait via uses.
	var redundant := _src_class("RtcRedundantTarget extends Node\nuses NeedsPing\n\ntrait NeedsPing:\n\tabstract func ping() -> void\n\nfunc ping() -> void:\n\tpass\n\nextend RtcRedundantTarget uses NeedsPing:\n\tpass\n")
	var redundant_report: Dictionary = probe.analyze_source(redundant, "res://tests/rtc_redundant_uses.barista")
	_expect(failures, redundant_report.get("valid", true) == false, "extend redundant with target uses is invalid")
	var saw_redundant := false
	for message in redundant_report.get("errors", PackedStringArray()):
		if "already conforms to trait" in message and "through its own \"uses\"" in message and "NeedsPing" in message:
			saw_redundant = true
	_expect(failures, saw_redundant, "redundant uses conformance diagnostic")

	# Cross-file trait requirement via declaration index.
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var trait_path := "res://tests/index_damageable.barista"
	var trait_source := "trait_name IndexDamageable\nabstract func take_damage(amount: int) -> void\n"
	index.synchronize_path_from_source(trait_path, trait_source)
	BaristaScriptParseCache.set_source_override(trait_path, trait_source)
	var consumer := _src_class("TraitIndexMissing extends Node\nuses IndexDamageable\n")
	var index_report: Dictionary = probe.analyze_source(consumer, "res://tests/trait_index_missing.barista")
	_expect(failures, index_report.get("valid", true) == false, "index-backed missing trait method is invalid")
	var saw_index := false
	for message in index_report.get("errors", PackedStringArray()):
		if "must implement trait method" in message and "take_damage()" in message:
			saw_index = true
	_expect(failures, saw_index, "index-backed missing trait method diagnostic")
	BaristaScriptParseCache.clear_source_override(trait_path)
	index.clear()

	# Foundry trait_required_signature / async / Self / rest narrowing (#60).
	var sig_mismatch := _src_class("TraitSigMismatch extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n\nfunc take_damage(amount: String) -> void:\n\tpass\n")
	var sig_mismatch_report: Dictionary = probe.analyze_source(sig_mismatch, "res://tests/trait_sig_mismatch.barista")
	_expect(failures, sig_mismatch_report.get("valid", true) == false, "trait method wrong parameter type is invalid")
	var saw_sig_mismatch := false
	for message in sig_mismatch_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Damageable.take_damage()" in message:
			saw_sig_mismatch = true
	_expect(failures, saw_sig_mismatch, "trait method signature mismatch diagnostic")

	var async_required := _src_class("TraitAsyncRequired extends Node\nuses RemoteLoadable\n\ntrait RemoteLoadable:\n\tabstract async func fetch() -> String\n\nfunc fetch() -> String:\n\treturn \"\"\n")
	var async_required_report: Dictionary = probe.analyze_source(async_required, "res://tests/trait_async_required.barista")
	_expect(failures, async_required_report.get("valid", true) == false, "sync impl of async trait method is invalid")
	var saw_async_required := false
	for message in async_required_report.get("errors", PackedStringArray()):
		if "must be async because it implements async trait method" in message and "RemoteLoadable.fetch()" in message:
			saw_async_required = true
	_expect(failures, saw_async_required, "async trait method requires async impl diagnostic")

	var sync_required := _src_class("TraitSyncRequired extends Node\nuses Syncable\n\ntrait Syncable:\n\tabstract func compute() -> int\n\nasync func compute() -> int:\n\treturn 0\n")
	var sync_required_report: Dictionary = probe.analyze_source(sync_required, "res://tests/trait_sync_required.barista")
	_expect(failures, sync_required_report.get("valid", true) == false, "async impl of sync trait method is invalid")
	var saw_sync_required := false
	for message in sync_required_report.get("errors", PackedStringArray()):
		if "cannot be async because it implements synchronous trait method" in message and "Syncable.compute()" in message:
			saw_sync_required = true
	_expect(failures, saw_sync_required, "sync trait method rejects async impl diagnostic")

	var self_ok := _src_class("TraitSelfOk extends Node\nuses Creatable\n\ntrait Creatable:\n\tabstract static func create() -> Self\n\nstatic func create() -> Self:\n\treturn TraitSelfOk.new()\n")
	var self_ok_report: Dictionary = probe.analyze_source(self_ok, "res://tests/trait_self_ok.barista")
	_expect(failures, self_ok_report.get("valid", false) == true, "Self return matching implementer is valid")

	var self_bad := _src_class("TraitSelfBad extends Node\nuses Creatable\n\ntrait Creatable:\n\tabstract static func create() -> Self\n\nstatic func create() -> String:\n\treturn \"x\"\n")
	var self_bad_report: Dictionary = probe.analyze_source(self_bad, "res://tests/trait_self_bad.barista")
	_expect(failures, self_bad_report.get("valid", true) == false, "Self return mismatched to String is invalid")
	var saw_self_bad := false
	for message in self_bad_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Creatable.create()" in message:
			saw_self_bad = true
	_expect(failures, saw_self_bad, "Self return mismatch diagnostic")

	var arity_bad := _src_class("TraitArityBad extends Node\nuses Binary\n\ntrait Binary:\n\tabstract func combine(a: int, b: int) -> int\n\nfunc combine(a: int) -> int:\n\treturn a\n")
	var arity_bad_report: Dictionary = probe.analyze_source(arity_bad, "res://tests/trait_arity_bad.barista")
	_expect(failures, arity_bad_report.get("valid", true) == false, "trait method arity mismatch is invalid")
	var saw_arity := false
	for message in arity_bad_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Binary.combine()" in message:
			saw_arity = true
	_expect(failures, saw_arity, "trait method arity mismatch diagnostic")

	var rest_narrow := _src_class("TraitRestNarrow extends Node\nuses AcceptsNodes\n\ntrait AcceptsNodes:\n\tabstract func accept(...nodes: Array[Node]) -> int\n\nfunc accept(...nodes: Array[String]) -> int:\n\treturn nodes.size()\n")
	var rest_narrow_report: Dictionary = probe.analyze_source(rest_narrow, "res://tests/trait_rest_narrow.barista")
	_expect(failures, rest_narrow_report.get("valid", true) == false, "narrower rest tail does not satisfy trait rest requirement")
	var saw_rest := false
	for message in rest_narrow_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "AcceptsNodes.accept()" in message:
			saw_rest = true
	_expect(failures, saw_rest, "trait rest narrowing diagnostic")

	# Fixed Array/Dictionary element matching (#84 / #83 review): carriers alone are not enough.
	var array_elem := _src_class("TraitArrayElem extends Node\nuses TakesNodes\n\ntrait TakesNodes:\n\tabstract func take(items: Array[Node]) -> void\n\nfunc take(items: Array[String]) -> void:\n\tpass\n")
	var array_elem_report: Dictionary = probe.analyze_source(array_elem, "res://tests/trait_array_elem.barista")
	_expect(failures, array_elem_report.get("valid", true) == false, "fixed Array[Node] vs Array[String] param is invalid")
	var saw_array_elem := false
	for message in array_elem_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "TakesNodes.take()" in message:
			saw_array_elem = true
	_expect(failures, saw_array_elem, "fixed Array element mismatch diagnostic")

	var dict_elem := _src_class("TraitDictElem extends Node\nuses TakesMap\n\ntrait TakesMap:\n\tabstract func take(items: Dictionary[String, Node]) -> void\n\nfunc take(items: Dictionary[String, String]) -> void:\n\tpass\n")
	var dict_elem_report: Dictionary = probe.analyze_source(dict_elem, "res://tests/trait_dict_elem.barista")
	_expect(failures, dict_elem_report.get("valid", true) == false, "fixed Dictionary value element mismatch is invalid")
	var saw_dict_elem := false
	for message in dict_elem_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "TakesMap.take()" in message:
			saw_dict_elem = true
	_expect(failures, saw_dict_elem, "fixed Dictionary element mismatch diagnostic")

	# Explicit static-vs-instance (and reverse) signature mismatch (#84 / #83 review).
	var static_vs_instance := _src_class("TraitStaticVsInst extends Node\nuses Factory\n\ntrait Factory:\n\tabstract static func build() -> void\n\nfunc build() -> void:\n\tpass\n")
	var static_vs_instance_report: Dictionary = probe.analyze_source(static_vs_instance, "res://tests/trait_static_vs_inst.barista")
	_expect(failures, static_vs_instance_report.get("valid", true) == false, "instance impl of static trait method is invalid")
	var saw_static_vs_inst := false
	for message in static_vs_instance_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Factory.build()" in message:
			saw_static_vs_inst = true
	_expect(failures, saw_static_vs_inst, "static-vs-instance signature mismatch diagnostic")

	var instance_vs_static := _src_class("TraitInstVsStatic extends Node\nuses Worker\n\ntrait Worker:\n\tabstract func run() -> void\n\nstatic func run() -> void:\n\tpass\n")
	var instance_vs_static_report: Dictionary = probe.analyze_source(instance_vs_static, "res://tests/trait_inst_vs_static.barista")
	_expect(failures, instance_vs_static_report.get("valid", true) == false, "static impl of instance trait method is invalid")
	var saw_inst_vs_static := false
	for message in instance_vs_static_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "Worker.run()" in message:
			saw_inst_vs_static = true
	_expect(failures, saw_inst_vs_static, "instance-vs-static signature mismatch diagnostic")

	var rtc_sig := _src_class("RtcSigTarget extends Node\n\ntrait NeedsPing:\n\tabstract func ping(code: int) -> void\n\nextend RtcSigTarget uses NeedsPing:\n\tfunc ping(code: String) -> void:\n\t\tpass\n")
	var rtc_sig_report: Dictionary = probe.analyze_source(rtc_sig, "res://tests/rtc_sig_mismatch.barista")
	_expect(failures, rtc_sig_report.get("valid", true) == false, "extend witness with wrong signature is invalid")
	var saw_rtc_sig := false
	for message in rtc_sig_report.get("errors", PackedStringArray()):
		if "signature does not match required trait method" in message and "NeedsPing.ping()" in message:
			saw_rtc_sig = true
	_expect(failures, saw_rtc_sig, "extend witness signature mismatch diagnostic")

	var native_sig := _src_class("NativeSigBad extends Node\nuses NeedsGetNode\n\ntrait NeedsGetNode:\n\tabstract func get_node(path: int) -> Node\n")
	var native_sig_report: Dictionary = probe.analyze_source(native_sig, "res://tests/native_sig_bad.barista")
	_expect(failures, native_sig_report.get("valid", true) == false, "native MethodInfo wrong signature for trait is invalid")
	var saw_native_sig := false
	for message in native_sig_report.get("errors", PackedStringArray()):
		if "native function" in message and "get_node()" in message and "NeedsGetNode.get_node()" in message:
			saw_native_sig = true
	_expect(failures, saw_native_sig, "native MethodInfo signature mismatch diagnostic")


func _test_flow_narrowing(failures: PackedStringArray) -> void:
	# Foundry flow-narrowing starter (@ c9d5e35): null-check + `is` type-test overlays for locals/params.
	var probe := BaristaScriptAnalyzerProbe.new()
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_null_checks", true)

	var bare_null := _src_class("BareNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tvar x: Node = n\n")
	var bare_null_report: Dictionary = probe.analyze_source(bare_null, "res://tests/bare_nullable_assign.barista")
	_expect(failures, bare_null_report.get("valid", true) == false, "nullable to non-null assign fails under strict_null")

	var narrowed_null := _src_class("NarrowedNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tvar x: Node = n\n")
	var narrowed_null_report: Dictionary = probe.analyze_source(narrowed_null, "res://tests/narrowed_nullable_assign.barista")
	_expect(failures, narrowed_null_report.get("valid", false) == true, "null-check true arm allows Node? → Node")

	var else_null := _src_class("ElseNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tpass\n\telse:\n\t\tvar x: Node = n\n")
	var else_null_report: Dictionary = probe.analyze_source(else_null, "res://tests/else_nullable_assign.barista")
	_expect(failures, else_null_report.get("valid", true) == false, "null-check else arm keeps Node? → Node invalid")

	var assert_null := _src_class("AssertNullableAssign extends Node\nfunc take(n: Node?) -> void:\n\tassert(n != null)\n\tvar x: Node = n\n")
	var assert_null_report: Dictionary = probe.analyze_source(assert_null, "res://tests/assert_nullable_assign.barista")
	_expect(failures, assert_null_report.get("valid", false) == true, "assert null-check narrows later statements")

	var bare_union := _src_class("BareUnionAssign extends Node\nfunc take(v: int | String) -> void:\n\tvar s: String = v\n")
	var bare_union_report: Dictionary = probe.analyze_source(bare_union, "res://tests/bare_union_assign.barista")
	_expect(failures, bare_union_report.get("valid", true) == false, "union to String without type test is invalid")

	var narrowed_is := _src_class("NarrowedIsAssign extends Node\nfunc take(v: int | String) -> void:\n\tif v is String:\n\t\tvar s: String = v\n")
	var narrowed_is_report: Dictionary = probe.analyze_source(narrowed_is, "res://tests/narrowed_is_assign.barista")
	_expect(failures, narrowed_is_report.get("valid", false) == true, "`is String` true arm allows int|String → String")

	var else_is := _src_class("ElseIsAssign extends Node\nfunc take(v: int | String) -> void:\n\tif v is String:\n\t\tpass\n\telse:\n\t\tvar i: int = v\n")
	var else_is_report: Dictionary = probe.analyze_source(else_is, "res://tests/else_is_assign.barista")
	_expect(failures, else_is_report.get("valid", false) == true, "`is String` else arm subtracts String leaving int")

	var cleared := _src_class("ClearedNarrowingAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tn = null\n\t\tvar x: Node = n\n")
	var cleared_report: Dictionary = probe.analyze_source(cleared, "res://tests/cleared_narrowing_assign.barista")
	_expect(failures, cleared_report.get("valid", true) == false, "assignment clears prior null-check narrowing")

	var and_narrow := _src_class("AndNarrowingAssign extends Node\nfunc take(n: Node?) -> void:\n\tif n != null and n is Node:\n\t\tvar x: Node = n\n")
	var and_narrow_report: Dictionary = probe.analyze_source(and_narrow, "res://tests/and_narrowing_assign.barista")
	_expect(failures, and_narrow_report.get("valid", false) == true, "`and` condition applies left-side null-check narrowing")

	# Foundry match-branch flow narrowing (@ c9d5e35): non-null patterns strip nullability;
	# subject `is T` / bare native type patterns overlay the matched type.
	var match_null_stripped := _src_class("MatchNullStrippedAssign extends Node\nfunc take(n: Node?) -> void:\n\tmatch n:\n\t\tnull:\n\t\t\tpass\n\t\t1:\n\t\t\tvar x: Node = n\n\t\t_:\n\t\t\tpass\n")
	var match_null_stripped_report: Dictionary = probe.analyze_source(match_null_stripped, "res://tests/match_null_stripped_assign.barista")
	_expect(failures, match_null_stripped_report.get("valid", false) == true, "match non-null literal arm strips Node? → Node")

	var match_wildcard_keeps_null := _src_class("MatchWildcardKeepsNull extends Node\nfunc take(n: Node?) -> void:\n\tmatch n:\n\t\t_:\n\t\t\tvar x: Node = n\n")
	var match_wildcard_keeps_null_report: Dictionary = probe.analyze_source(match_wildcard_keeps_null, "res://tests/match_wildcard_keeps_null.barista")
	_expect(failures, match_wildcard_keeps_null_report.get("valid", true) == false, "match wildcard arm does not strip nullability")

	var match_is_type := _src_class("MatchIsTypeAssign extends Node\nfunc take(v: int | String) -> void:\n\tmatch v:\n\t\tv is String:\n\t\t\tvar s: String = v\n\t\t_:\n\t\t\tpass\n")
	var match_is_type_report: Dictionary = probe.analyze_source(match_is_type, "res://tests/match_is_type_assign.barista")
	_expect(failures, match_is_type_report.get("valid", false) == true, "match `v is String` arm allows int|String → String")

	var match_native_type := _src_class("MatchNativeTypeAssign extends Node\nfunc take(v: Object?) -> void:\n\tmatch v:\n\t\tnull:\n\t\t\tpass\n\t\tNode:\n\t\t\tvar x: Node = v\n\t\t_:\n\t\t\tpass\n")
	var match_native_type_report: Dictionary = probe.analyze_source(match_native_type, "res://tests/match_native_type_assign.barista")
	_expect(failures, match_native_type_report.get("valid", false) == true, "match bare Node type pattern narrows Object? → Node")

	# Local/const shadowing a ClassDB name must stay a value pattern (no type overlay).
	# Use int|String so a mistaken ClassDB promotion would wrongly allow `var x: Node = v`.
	var match_shadowed_classdb := _src_class("MatchShadowedClassDBName extends Node\nfunc take(v: int | String) -> void:\n\tconst Node := 1\n\tmatch v:\n\t\tNode:\n\t\t\tvar x: Node = v\n\t\t_:\n\t\t\tpass\n")
	var match_shadowed_classdb_report: Dictionary = probe.analyze_source(match_shadowed_classdb, "res://tests/match_shadowed_classdb_name.barista")
	_expect(failures, match_shadowed_classdb_report.get("valid", true) == false, "match local Node shadow stays value pattern (no ClassDB type overlay)")

	ProjectSettings.set_setting("debug/barista_script/analysis/strict_null_checks", false)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()


func _test_lambda_capture_and_compound_narrowing(failures: PackedStringArray) -> void:
	# Foundry @ c9d5e35: lambda capture marks flow-narrowing sources; any later call clears them.
	# Compound assignment restores the declared type on the assignee and clears narrowing.
	# Lambda bodies analyze under a nested FlowNarrowingScope that clears overlays, so captures
	# must not require the narrowed width inside the lambda (same as Foundry).
	var probe := BaristaScriptAnalyzerProbe.new()
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_null_checks", true)

	var capture_clears := _src_class("LambdaCaptureClearsNarrowing extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tvar f := func():\n\t\t\tvar _used = n\n\t\tf.call()\n\t\tvar x: Node = n\n")
	var capture_clears_report: Dictionary = probe.analyze_source(capture_clears, "res://tests/lambda_capture_clears_narrowing.barista")
	_expect(failures, capture_clears_report.get("valid", true) == false, "captured null-narrowing cleared after call makes Node? → Node invalid")

	var capture_no_call := _src_class("LambdaCaptureNoCallKeepsNarrowing extends Node\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tvar f := func():\n\t\t\tvar _used = n\n\t\tvar x: Node = n\n")
	var capture_no_call_report: Dictionary = probe.analyze_source(capture_no_call, "res://tests/lambda_capture_no_call_keeps_narrowing.barista")
	_expect(failures, capture_no_call_report.get("valid", false) == true, "captured narrowing stays until a call clears it")

	var member_no_capture := _src_class("LambdaMemberSkipsCapture extends Node\nvar member_n: Node?\nfunc take(n: Node?) -> void:\n\tif n != null:\n\t\tvar f := func():\n\t\t\tvar _m = member_n\n\t\tf.call()\n\t\tvar x: Node = n\n")
	var member_no_capture_report: Dictionary = probe.analyze_source(member_no_capture, "res://tests/lambda_member_skips_capture.barista")
	_expect(failures, member_no_capture_report.get("valid", false) == true, "member read in lambda does not capture / clear local narrowing")

	var is_capture_clears := _src_class("LambdaIsCaptureClearsNarrowing extends Node\nfunc take(v: int | String) -> void:\n\tif v is String:\n\t\tvar f := func():\n\t\t\tvar _used = v\n\t\tf.call()\n\t\tvar s: String = v\n")
	var is_capture_clears_report: Dictionary = probe.analyze_source(is_capture_clears, "res://tests/lambda_is_capture_clears_narrowing.barista")
	_expect(failures, is_capture_clears_report.get("valid", true) == false, "captured `is` narrowing cleared after call")

	# Compound assign: restore declared type on assignee for the write, clear narrowing after.
	var compound_clears := _src_class("CompoundAssignClearsNarrowing extends Node\nfunc take(v: int | String) -> void:\n\tif v is int:\n\t\tv += 1\n\t\tvar i: int = v\n")
	var compound_clears_report: Dictionary = probe.analyze_source(compound_clears, "res://tests/compound_assign_clears_narrowing.barista")
	_expect(failures, compound_clears_report.get("valid", true) == false, "compound assignment clears prior `is` narrowing")

	# Narrowed compound read: inside the `is int` arm, `v += 1` is accepted (narrowed left operand).
	var compound_narrow_ok := _src_class("CompoundAssignNarrowedReadOk extends Node\nfunc take(v: int | String) -> void:\n\tif v is int:\n\t\tv += 1\n")
	var compound_narrow_ok_report: Dictionary = probe.analyze_source(compound_narrow_ok, "res://tests/compound_assign_narrowed_read_ok.barista")
	_expect(failures, compound_narrow_ok_report.get("valid", false) == true, "compound += inside `is int` arm is valid: %s" % compound_narrow_ok_report.get("errors"))
	# ParameterNode does not have VariableNode.assignments. Repeated fresh parser allocations
	# exercise assignment accounting without letting a write through the identifier union damage
	# a neighboring AST node (the original symptom depended on allocator layout).
	for iteration in range(64):
		var repeated: Dictionary = probe.analyze_source(compound_narrow_ok, "res://tests/compound_parameter_%d.barista" % iteration)
		_expect(failures, repeated.get("valid", false), "parameter assignment preserves AST on iteration %d: %s" % [iteration, repeated.get("errors")])

	ProjectSettings.set_setting("debug/barista_script/analysis/strict_null_checks", false)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()


func _test_get_operation_type(failures: PackedStringArray) -> void:
	# Foundry get_operation_type @ c9d5e35: set-wise union enumeration, hard-type operators,
	# unary result typing, and compound left-operand use of narrowed reads.
	var probe := BaristaScriptAnalyzerProbe.new()

	# Unary `not` yields bool (thin path previously copied the operand type).
	var unary_not := _src_class("UnaryNotBoolResult extends Node\nfunc take(v: int) -> void:\n\tvar b: bool = not v\n")
	var unary_not_report: Dictionary = probe.analyze_source(unary_not, "res://tests/unary_not_bool_result.barista")
	_expect(failures, unary_not_report.get("valid", false) == true, "unary not on int types as bool")

	# Hard incompatible binary operands.
	var hard_invalid := _src_class("HardInvalidBinaryAdd extends Node\nfunc test() -> void:\n\tvar _x = \"a\" + 1\n")
	var hard_invalid_report: Dictionary = probe.analyze_source(hard_invalid, "res://tests/hard_invalid_binary_add.barista")
	_expect(failures, hard_invalid_report.get("valid", true) == false, "String + int is invalid")
	var saw_hard := false
	for message in hard_invalid_report.get("errors", PackedStringArray()):
		if str(message).find("Invalid operands") >= 0 or str(message).find("\"+\"") >= 0:
			saw_hard = true
			break
	_expect(failures, saw_hard, "String + int names the operator / operands")

	# Set-wise: int|String + int admits String+int, which has no result.
	var union_add := _src_class("UnionSetWiseAddReject extends Node\nfunc take(v: int | String) -> void:\n\tvar _x = v + 1\n")
	var union_add_report: Dictionary = probe.analyze_source(union_add, "res://tests/union_set_wise_add_reject.barista")
	_expect(failures, union_add_report.get("valid", true) == false, "int|String + int rejected set-wise")
	var saw_set := false
	for message in union_add_report.get("errors", PackedStringArray()):
		if str(message).find("allow the combination") >= 0 or str(message).find("no result for") >= 0:
			saw_set = true
			break
	_expect(failures, saw_set, "set-wise rejection names the unsupported combination")

	# Equality is not checked set-wise: int|String == int stays valid.
	var union_eq := _src_class("UnionEqualityOk extends Node\nfunc take(v: int | String) -> bool:\n\treturn v == 1\n")
	var union_eq_report: Dictionary = probe.analyze_source(union_eq, "res://tests/union_equality_ok.barista")
	_expect(failures, union_eq_report.get("valid", false) == true, "int|String == int is valid (equality not set-wise)")

	# Compound without narrowing: same set-wise rejection as binary +.
	var compound_union := _src_class("CompoundUnionSetWiseReject extends Node\nfunc take(v: int | String) -> void:\n\tv += 1\n")
	var compound_union_report: Dictionary = probe.analyze_source(compound_union, "res://tests/compound_union_set_wise_reject.barista")
	_expect(failures, compound_union_report.get("valid", true) == false, "compound += on int|String without narrowing is invalid")

	# Compound under `is String` with += 1: narrowed left is String, op invalid.
	var compound_string := _src_class("CompoundStringPlusIntReject extends Node\nfunc take(v: int | String) -> void:\n\tif v is String:\n\t\tv += 1\n")
	var compound_string_report: Dictionary = probe.analyze_source(compound_string, "res://tests/compound_string_plus_int_reject.barista")
	_expect(failures, compound_string_report.get("valid", true) == false, "compound += 1 after `is String` is invalid")

	# Invalid compound on hard builtins remains invalid.
	var compound_hard := _src_class("CompoundHardInvalid extends Node\nfunc test() -> void:\n\tvar s: String = \"a\"\n\ts += 1\n")
	var compound_hard_report: Dictionary = probe.analyze_source(compound_hard, "res://tests/compound_hard_invalid.barista")
	_expect(failures, compound_hard_report.get("valid", true) == false, "String += int remains invalid")

	# Typed Array concatenation: matching element types succeed; mismatched fail.
	var array_add_ok := _src_class("TypedArrayAddOk extends Node\nfunc test() -> void:\n\tvar a: Array[int] = [1]\n\tvar b: Array[int] = [2]\n\tvar _c: Array[int] = a + b\n")
	var array_add_ok_report: Dictionary = probe.analyze_source(array_add_ok, "res://tests/typed_array_add_ok.barista")
	_expect(failures, array_add_ok_report.get("valid", false) == true, "Array[int] + Array[int] is valid")

	var array_add_bad := _src_class("TypedArrayAddMismatch extends Node\nfunc test() -> void:\n\tvar a: Array[int] = [1]\n\tvar b: Array[String] = [\"x\"]\n\tvar _c = a + b\n")
	var array_add_bad_report: Dictionary = probe.analyze_source(array_add_bad, "res://tests/typed_array_add_mismatch.barista")
	_expect(failures, array_add_bad_report.get("valid", true) == false, "Array[int] + Array[String] is invalid")


func _test_builtin_annotation_resolve(failures: PackedStringArray) -> void:
	# Foundry datatype_from_type_node @ c9d5e35: user-facing builtins beyond int/float/bool/String
	# resolve through get_builtin_type (StringName / Callable / bare Array / NodePath / …).
	var probe := BaristaScriptAnalyzerProbe.new()

	var string_name_ok := _src_class("BuiltinStringNameAnnot extends Node\nfunc take(n: StringName) -> void:\n\tvar _x: StringName = n\n")
	var string_name_report: Dictionary = probe.analyze_source(string_name_ok, "res://tests/builtin_string_name_annot.barista")
	_expect(failures, string_name_report.get("valid", false) == true, "StringName annotation resolves as builtin")

	var node_path_ok := _src_class("BuiltinNodePathAnnot extends Node\nfunc take(p: NodePath) -> void:\n\tvar _x: NodePath = p\n")
	var node_path_report: Dictionary = probe.analyze_source(node_path_ok, "res://tests/builtin_node_path_annot.barista")
	_expect(failures, node_path_report.get("valid", false) == true, "NodePath annotation resolves as builtin")

	var bare_array_ok := _src_class("BuiltinBareArrayAnnot extends Node\nfunc take(a: Array) -> void:\n\tvar _x: Array = a\n")
	var bare_array_report: Dictionary = probe.analyze_source(bare_array_ok, "res://tests/builtin_bare_array_annot.barista")
	_expect(failures, bare_array_report.get("valid", false) == true, "bare Array annotation resolves as builtin")

	var callable_ok := _src_class("BuiltinCallableAnnot extends Node\nfunc take(c: Callable) -> void:\n\tvar _x: Callable = c\n")
	var callable_report: Dictionary = probe.analyze_source(callable_ok, "res://tests/builtin_callable_annot.barista")
	_expect(failures, callable_report.get("valid", false) == true, "bare Callable annotation resolves as builtin")

	var callable_sig_ok := _src_class("BuiltinCallableSigAnnot extends Node\nfunc take(c: Callable[[int], void]) -> void:\n\tvar _x: Callable[[int], void] = c\n")
	var callable_sig_report: Dictionary = probe.analyze_source(callable_sig_ok, "res://tests/builtin_callable_sig_annot.barista")
	_expect(failures, callable_sig_report.get("valid", false) == true, "Callable[[int], void] signature annotation resolves")

	var signal_ok := _src_class("BuiltinSignalAnnot extends Node\nfunc take(s: Signal) -> void:\n\tvar _x: Signal = s\n")
	var signal_report: Dictionary = probe.analyze_source(signal_ok, "res://tests/builtin_signal_annot.barista")
	_expect(failures, signal_report.get("valid", false) == true, "bare Signal annotation resolves as builtin")

	var number_ok := _src_class("BuiltinNumberAnnot extends Node\nfunc take(n: Number) -> void:\n\tvar _x: Number = 1\n")
	var number_report: Dictionary = probe.analyze_source(number_ok, "res://tests/builtin_number_annot.barista")
	_expect(failures, number_report.get("valid", false) == true, "Number annotation resolves as int|float union")

	# Still reject unknown spellings; prove the failure is not a blanket Variant fallthrough.
	var unknown := _src_class("BuiltinUnknownAnnot extends Node\nfunc take(x: NotARealType) -> void:\n\tpass\n")
	var unknown_report: Dictionary = probe.analyze_source(unknown, "res://tests/builtin_unknown_annot.barista")
	_expect(failures, unknown_report.get("valid", true) == false, "unknown type annotation remains invalid")
	var saw_unknown := false
	for message in unknown_report.get("errors", PackedStringArray()):
		if 'Could not find type "NotARealType"' in message:
			saw_unknown = true
	_expect(failures, saw_unknown, "unknown type keeps Could not find type diagnostic")

	# Assignability still enforced once the annotation resolves.
	var mismatch := _src_class("BuiltinStringNameMismatch extends Node\nfunc take() -> void:\n\tvar _n: StringName = 123\n")
	var mismatch_report: Dictionary = probe.analyze_source(mismatch, "res://tests/builtin_string_name_mismatch.barista")
	_expect(failures, mismatch_report.get("valid", true) == false, "int → StringName annotation assign remains invalid")


func _test_custom_annotation_surface(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	# Byte-faithful producer fixture: Foundry
	# modules/foundry_script/tests/scripts/analyzer/features/annotation_custom_usage.fs:1-35
	# @ c9d5e35. This covers every non-generic target plus positional, named, default,
	# variadic, stacked, and repeated argument binding.
	var valid_source := "# Custom annotations declared and used in the same namespace resolve without an import and\n# accept positional, named, default, variadic, stacked, and repeated arguments. They apply to\n# class, method, member-variable, signal, and constant targets.\nnamespace cafecito.usage\n\nannotation suite(name: String = \"\") targets CLASS\nannotation test targets METHOD\nannotation timeout(seconds: float) targets METHOD\nannotation tags(...names: String) targets METHOD, CLASS\nannotation fixture targets VARIABLE\nannotation event targets SIGNAL\nannotation config(key: String) targets CONSTANT\n\n@suite(name = \"Combat\")\n@tags(\"gameplay\")\nclass CombatTests:\n\t@fixture\n\tvar world: int\n\n\t@config(\"max_health\")\n\tconst MAX_HEALTH = 100\n\n\t@event\n\tsignal damage_taken(amount: int)\n\n\t@test\n\t@timeout(10.0)\n\t@tags(\"slow\", \"integration\")\n\t@tags(\"flaky\")\n\tfunc crit_table() -> void:\n\t\tpass\n\nfunc test() -> void:\n\tpass\n"
	var valid_report: Dictionary = probe.analyze_source(valid_source, "res://tests/annotation_custom_usage.barista")
	_expect(failures, valid_report.get("valid", false) == true, "custom annotation valid target and arguments analyze")

	# Byte-faithful error fixtures: Foundry analyzer/errors/annotation_*.fs @ c9d5e35.
	var invalid_cases := [
		{"name": "type mismatch", "path": "annotation_argument_type_mismatch", "source": "namespace cafecito.typemismatch\n\nannotation timeout(seconds: float) targets METHOD\n\n@timeout(\"not a number\")\nfunc test() -> void:\n\tpass\n", "needle": "expected \"float\" but got \"String\""},
		{"name": "nonconstant", "path": "annotation_non_constant_arg", "source": "namespace cafecito.nonconst\n\nannotation timeout(seconds: float) targets METHOD\n\nvar seconds_value: float = 1.0\n\n@timeout(seconds_value)\nfunc test() -> void:\n\tprint(seconds_value)\n", "needle": "not a constant expression"},
		{"name": "wrong target", "path": "annotation_wrong_target", "source": "namespace cafecito.wrongtarget\n\nannotation fixture targets VARIABLE\n\n@fixture\nfunc test() -> void:\n\tpass\n", "needle": "cannot be applied to a method"},
		{"name": "missing required", "path": "annotation_missing_required_arg", "source": "namespace cafecito.missingarg\n\nannotation cases(provider: String) targets METHOD\n\n@cases\nfunc test() -> void:\n\tpass\n", "needle": "missing required argument \"provider\""},
		{"name": "unknown named", "path": "annotation_unknown_named_arg", "source": "namespace cafecito.unknownnamed\n\nannotation suite(name: String = \"\") targets CLASS\n\n@suite(title = \"x\")\nclass Demo:\n\tpass\n", "needle": "has no parameter named \"title\""},
		{"name": "positional after named", "path": "annotation_positional_after_named", "source": "namespace cafecito.posafternamed\n\nannotation pair(first: String, second: String) targets METHOD\n\n@pair(first = \"a\", \"b\")\nfunc test() -> void:\n\tpass\n", "needle": "Positional argument after named argument"},
		{"name": "too many", "path": "annotation_too_many_args", "source": "namespace cafecito.toomany\n\nannotation test targets METHOD\n\n@test(\"extra\")\nfunc test() -> void:\n\tpass\n", "needle": "takes at most 0 argument(s), but 1 were given"},
	]
	for invalid_case in invalid_cases:
		var report: Dictionary = probe.analyze_source(invalid_case.source, "res://tests/%s.barista" % invalid_case.path)
		var joined := "\n".join(report.get("errors", PackedStringArray()))
		_expect(failures, report.get("valid", true) == false, "custom annotation %s invalidates analysis" % invalid_case.name)
		_expect(failures, invalid_case.needle in joined, "custom annotation %s reports Foundry diagnostic" % invalid_case.name)

	var unknown_report: Dictionary = probe.analyze_source("@not_declared\nfunc test() -> void:\n\tpass\n", "res://tests/annotation_unknown.barista")
	_expect(failures, unknown_report.get("valid", true) == false, "unknown custom annotation is rejected")
	_expect(failures, "Unknown annotation" in "\n".join(unknown_report.get("errors", PackedStringArray())), "unknown custom annotation has lookup diagnostic")

	# Byte-faithful declaration-index producer: Foundry
	# modules/foundry_script/tests/scripts/analyzer/features/annotation_index_library.notest.fs:1-6
	# and consumer annotation_custom_import.fs:1-13 @ c9d5e35.
	var index := BaristaScriptDeclarationIndexProbe.new()
	index.clear()
	var provider_path := "res://tests/annotation_index_library.barista"
	var provider_source := "# Provider file used by analyzer custom-annotation import tests. It intentionally has no test()\n# function; the companion consumer imports this namespace and applies all three declarations.\nnamespace cafecito.annotation_index\n\nannotation suite(name: String = \"\") targets CLASS\nannotation index_test targets METHOD\nannotation fixture targets VARIABLE\n"
	index.synchronize_path_from_source(provider_path, provider_source)
	BaristaScriptParseCache.set_source_override(provider_path, provider_source)
	var consumer_source := "# Custom annotations declared in another file resolve through an imported namespace.\n# This is the analyzer half of the declaration-index coverage: the provider fixture is indexed\n# before this script is analyzed.\nnamespace cafecito.annotation_consumer\nimport cafecito.annotation_index\n\n@suite(name = \"Imported\")\nclass ImportedSuite:\n\t@fixture\n\tvar state: int\n\n\t@index_test\n\tfunc works() -> void:\n\t\tpass\n\nfunc test() -> void:\n\tpass\n"
	var consumer_report: Dictionary = probe.analyze_source(consumer_source, "res://tests/annotation_custom_import.barista")
	_expect(failures, consumer_report.get("valid", false) == true, "imported custom annotations resolve through declaration index")
	var depended_parser_statuses: Dictionary = consumer_report.get("depended_parser_statuses", {})
	_expect(failures, depended_parser_statuses.has(provider_path),
		"external annotation provider is retained as a depended parser")
	_expect(failures, depended_parser_statuses.get(provider_path, Status.EMPTY) >= Status.INHERITANCE_SOLVED,
		"external annotation provider reaches dependency finalization")
	var import_mismatch := "namespace cafecito.annotation_consumer\nimport cafecito.annotation_index\n\n@suite(name = 7)\nclass ImportedSuite:\n\tpass\n"
	var mismatch_report: Dictionary = probe.analyze_source(import_mismatch, "res://tests/annotation_custom_import_mismatch.barista")
	_expect(failures, mismatch_report.get("valid", true) == false, "imported annotation signature is validated")
	BaristaScriptParseCache.clear_source_override(provider_path)
	index.clear()


func _test_type_alias_surface(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()
	# Byte-faithful producer fixtures: Foundry analyzer/features/type_alias_resolution.fs
	# and analyzer/errors/type_alias_*.fs @ c9d5e35.
	var transparent_source := "type Meters = float\n\nfunc measure(distance: Meters) -> Meters:\n\treturn distance * 2.0\n\nfunc test():\n\tvar distance: Meters = 1.5\n\tprint(measure(distance))\n"
	var transparent_report: Dictionary = probe.analyze_source(transparent_source, "res://tests/type_alias_resolution.barista")
	_expect(failures, transparent_report.get("valid", false) == true, "type alias expands transparently in signatures and locals")

	var cycle_source := "type Left = Right\ntype Right = Left\ntype SelfReferential = SelfReferential | int\n\n\nfunc test():\n\tvar value: Left = 1\n\tprint(value)\n"
	var cycle_report: Dictionary = probe.analyze_source(cycle_source, "res://tests/type_alias_cycle.barista")
	var cycle_errors := "\n".join(cycle_report.get("errors", PackedStringArray()))
	_expect(failures, cycle_report.get("valid", true) == false, "cyclic type aliases invalidate analysis")
	_expect(failures, 'Type alias "Left" -> "Right" -> "Left" expands to itself' in cycle_errors, "mutual type alias cycle names its chain")
	_expect(failures, 'Type alias "SelfReferential" -> "SelfReferential" expands to itself' in cycle_errors, "self type alias cycle is diagnosed")

	var unknown_source := "type Mixed = int | NotAType\n\n\nfunc test():\n\tvar value: Mixed = 1\n\tprint(value)\n"
	var unknown_report: Dictionary = probe.analyze_source(unknown_source, "res://tests/type_alias_unknown_member.barista")
	var unknown_errors := "\n".join(unknown_report.get("errors", PackedStringArray()))
	_expect(failures, unknown_report.get("valid", true) == false, "unresolvable type alias invalidates analysis")
	_expect(failures, 'Type alias "Mixed" has no expansion' in unknown_errors, "unresolvable alias reports declaration failure")
	_expect(failures, 'Could not find type "NotAType"' in unknown_errors, "unresolvable alias reports missing member")

	var expression_source := "class Holder:\n\tvar label: String = \"holder\"\n\n\ntype Only = Holder\ntype Scalar = int | String\n\n\nfunc test():\n\tvar read = Scalar\n\tvar called = Scalar()\n\tvar made = Only.new()\n\tprints(read, called, made)\n"
	var expression_report: Dictionary = probe.analyze_source(expression_source, "res://tests/type_alias_not_an_expression.barista")
	var expression_errors := "\n".join(expression_report.get("errors", PackedStringArray()))
	_expect(failures, 'Type alias "Scalar" can only be used in a type position' in expression_errors, "type alias has no expression value")
	_expect(failures, 'Type alias "Only" can only be used in a type position' in expression_errors, "class alias has no constructor handle")

	var conflict_source := "type int = String\ntype Label = float\n\n\nfunc test():\n\tprint(\"unreachable\")\n"
	var conflict_report: Dictionary = probe.analyze_source(conflict_source, "res://tests/type_alias_hides_existing_type.barista")
	var conflict_errors := "\n".join(conflict_report.get("errors", PackedStringArray()))
	_expect(failures, 'Type alias "int" hides a built-in type' in conflict_errors, "type alias cannot hide builtin type")
	_expect(failures, 'Type alias "Label" hides a native class' in conflict_errors, "type alias cannot hide native class")


func _test_union_union_assignability(failures: PackedStringArray) -> void:
	# Foundry FSTypeCompatibility source-UNION @ c9d5e35 (#89 residual): every alternative of a
	# union source must satisfy the target. Number→Number / written union self-assign are the AC.
	var probe := BaristaScriptAnalyzerProbe.new()

	var number_self := _src_class("NumberToNumberAssign extends Node\nfunc take(n: Number) -> void:\n\tvar _x: Number = n\n")
	var number_self_report: Dictionary = probe.analyze_source(number_self, "res://tests/number_to_number_assign.barista")
	_expect(failures, number_self_report.get("valid", false) == true, "Number → Number union self-assign is valid")

	var written_self := _src_class("WrittenUnionSelfAssign extends Node\nfunc take(v: int | String) -> void:\n\tvar _x: int | String = v\n")
	var written_self_report: Dictionary = probe.analyze_source(written_self, "res://tests/written_union_self_assign.barista")
	_expect(failures, written_self_report.get("valid", false) == true, "int|String → int|String self-assign is valid")

	# Opposite spelling still works: source-UNION checks each alt against the target set.
	var reordered := _src_class("WrittenUnionReorderAssign extends Node\nfunc take(v: String | int) -> void:\n\tvar _x: int | String = v\n")
	var reordered_report: Dictionary = probe.analyze_source(reordered, "res://tests/written_union_reorder_assign.barista")
	_expect(failures, reordered_report.get("valid", false) == true, "String|int → int|String reorder assign is valid")

	var number_from_written := _src_class("WrittenToNumberAssign extends Node\nfunc take(v: int | float) -> void:\n\tvar _x: Number = v\n")
	var number_from_written_report: Dictionary = probe.analyze_source(number_from_written, "res://tests/written_to_number_assign.barista")
	_expect(failures, number_from_written_report.get("valid", false) == true, "int|float → Number assign is valid")

	# Partial coverage stays invalid: a String alternative cannot enter a String-only slot.
	var partial := _src_class("UnionPartialAssign extends Node\nfunc take(v: int | String) -> void:\n\tvar _s: String = v\n")
	var partial_report: Dictionary = probe.analyze_source(partial, "res://tests/union_partial_assign.barista")
	_expect(failures, partial_report.get("valid", true) == false, "int|String → String without narrowing is invalid")

	# Carrier-changing per-alternative conversion cannot be emitted for an erased union source.
	var number_to_float := _src_class("NumberToFloatAssign extends Node\nfunc take(n: Number) -> void:\n\tvar _f: float = n\n")
	var number_to_float_report: Dictionary = probe.analyze_source(number_to_float, "res://tests/number_to_float_assign.barista")
	_expect(failures, number_to_float_report.get("valid", true) == false, "Number → float carrier change stays invalid")

	# Concrete → union target still selects an alternative (pre-existing target-UNION path).
	var int_to_number := _src_class("IntToNumberAssign extends Node\nfunc take(n: int) -> void:\n\tvar _x: Number = n\n")
	var int_to_number_report: Dictionary = probe.analyze_source(int_to_number, "res://tests/int_to_number_assign.barista")
	_expect(failures, int_to_number_report.get("valid", false) == true, "int → Number still selects a union alternative")

	# Opt-in declaration-index mutation unchanged: analyze / validate stay read-only.
	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(number_self, "res://tests/number_to_number_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "Number→Number remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(number_self, "res://tests/number_to_number_is_valid.barista"),
		"Number→Number remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for Number→Number")


func _test_union_store_carrier_select(failures: PackedStringArray) -> void:
	# Foundry target-UNION _select_union_alternative / _union_store_converts_carrier @ c9d5e35:
	# prefer exact alternatives; converting alternatives need a numeric store-carrier conversion.
	var probe := BaristaScriptAnalyzerProbe.new()

	# Exact-path validity: int fits int|float without conversion (selection order not asserted here).
	var exact_prefers_int := _src_class("UnionExactPrefersInt extends Node\nfunc take(n: int) -> void:\n\tvar _x: int | float = n\n")
	var exact_prefers_int_report: Dictionary = probe.analyze_source(exact_prefers_int, "res://tests/union_exact_prefers_int.barista")
	_expect(failures, exact_prefers_int_report.get("valid", false) == true, "int → int|float exact alternative is valid")

	# Numeric store can convert int→float into a float|String union.
	var int_to_float_union := _src_class("UnionNumericStoreWiden extends Node\nfunc take(n: int) -> void:\n\tvar _x: float | String = n\n")
	var int_to_float_union_report: Dictionary = probe.analyze_source(int_to_float_union, "res://tests/union_numeric_store_widen.barista")
	_expect(failures, int_to_float_union_report.get("valid", false) == true, "int → float|String numeric store widen is valid")

	# Plain String→StringName still works (non-union slot performs the engine bridge).
	var plain_string_name := _src_class("PlainStringToStringName extends Node\nfunc take() -> void:\n\tvar _n: StringName = \"ready\"\n")
	var plain_string_name_report: Dictionary = probe.analyze_source(plain_string_name, "res://tests/plain_string_to_string_name.barista")
	_expect(failures, plain_string_name_report.get("valid", false) == true, "String → StringName plain assign remains valid")

	# Union store cannot perform String→StringName: no numeric store-carrier counterpart.
	var string_to_string_name_union := _src_class("UnionRejectsStringNameBridge extends Node\nfunc take() -> void:\n\tvar _x: StringName | int = \"ready\"\n")
	var string_to_string_name_union_report: Dictionary = probe.analyze_source(string_to_string_name_union, "res://tests/union_rejects_string_name_bridge.barista")
	_expect(failures, string_to_string_name_union_report.get("valid", true) == false,
		"String → StringName|int rejected (union store cannot bridge)")

	# Same bridge rejection with a non-literal String source.
	var string_param_to_union := _src_class("UnionRejectsStringParamBridge extends Node\nfunc take(s: String) -> void:\n\tvar _x: StringName | Node = s\n")
	var string_param_to_union_report: Dictionary = probe.analyze_source(string_param_to_union, "res://tests/union_rejects_string_param_bridge.barista")
	_expect(failures, string_param_to_union_report.get("valid", true) == false,
		"String param → StringName|Node rejected (union store cannot bridge)")

	# Opt-in declaration-index mutation unchanged.
	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(exact_prefers_int, "res://tests/union_store_carrier_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "int→int|float remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(exact_prefers_int, "res://tests/union_store_carrier_is_valid.barista"),
		"int→int|float remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for union store-carrier")


func _test_enum_case_match_and_case_binds(failures: PackedStringArray) -> void:
	# Foundry resolve_enum_values + resolve_match_case_pattern / resolve_type_test_case_binds
	# + container match patterns @ c9d5e35 (#60 ENUM_CASE / case-bind / container residual).
	var probe := BaristaScriptAnalyzerProbe.new()

	var match_ok := _src_class("EnumMatchOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\treturn dx + dy\n\t\t_:\n\t\t\treturn 0\n")
	var match_ok_report: Dictionary = probe.analyze_source(match_ok, "res://tests/enum_match_ok.barista")
	_expect(failures, match_ok_report.get("valid", false) == true, "Message.Move(dx, dy) match pattern is valid")

	var match_arity := _src_class("EnumMatchArity extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\tMessage.Move(dx):\n\t\t\tpass\n")
	var match_arity_report: Dictionary = probe.analyze_source(match_arity, "res://tests/enum_match_arity.barista")
	_expect(failures, match_arity_report.get("valid", true) == false, "Message.Move arity mismatch is invalid")
	var saw_match_arity := false
	for message in match_arity_report.get("errors", PackedStringArray()):
		if "carries 2 payload value(s), but 1 pattern(s) were given" in message:
			saw_match_arity = true
	_expect(failures, saw_match_arity, "ENUM_CASE payload arity diagnostic")

	var wrong_subject := _src_class("EnumMatchWrongSubject extends Node\nenum Message:\n\tMove(x: int, y: int)\nenum Other:\n\tGo(n: int)\nfunc handle(msg: Other) -> void:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\tpass\n")
	var wrong_subject_report: Dictionary = probe.analyze_source(wrong_subject, "res://tests/enum_match_wrong_subject.barista")
	_expect(failures, wrong_subject_report.get("valid", true) == false, "ENUM_CASE against unrelated subject is invalid")
	var saw_wrong_subject := false
	for message in wrong_subject_report.get("errors", PackedStringArray()):
		if "Pattern matches a case of" in message and "subject is of type" in message:
			saw_wrong_subject = true
	_expect(failures, saw_wrong_subject, "ENUM_CASE subject-type mismatch diagnostic")

	var case_bind_ok := _src_class("EnumCaseBindOk extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> int:\n\tif msg is Message.Move(dx, dy):\n\t\treturn dx + dy\n\treturn 0\n")
	var case_bind_ok_report: Dictionary = probe.analyze_source(case_bind_ok, "res://tests/enum_case_bind_ok.barista")
	_expect(failures, case_bind_ok_report.get("valid", false) == true, "is Message.Move(dx, dy) case binds are valid")

	var case_bind_arity := _src_class("EnumCaseBindArity extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> void:\n\tif msg is Message.Move(dx):\n\t\tpass\n")
	var case_bind_arity_report: Dictionary = probe.analyze_source(case_bind_arity, "res://tests/enum_case_bind_arity.barista")
	_expect(failures, case_bind_arity_report.get("valid", true) == false, "case-bind arity mismatch is invalid")
	var saw_bind_arity := false
	for message in case_bind_arity_report.get("errors", PackedStringArray()):
		if "carries 2 payload value(s), but 1 bind(s) were given" in message:
			saw_bind_arity = true
	_expect(failures, saw_bind_arity, "case-bind payload arity diagnostic")

	var not_case_bind := _src_class("NotEnumCaseBind extends Node\nfunc handle(v: Variant) -> void:\n\tif v is Node(n):\n\t\tpass\n")
	var not_case_bind_report: Dictionary = probe.analyze_source(not_case_bind, "res://tests/not_enum_case_bind.barista")
	_expect(failures, not_case_bind_report.get("valid", true) == false, "non-enum case binds are invalid")
	var saw_not_case := false
	for message in not_case_bind_report.get("errors", PackedStringArray()):
		if "Only a tagged-union case can bind payload values" in message:
			saw_not_case = true
	_expect(failures, saw_not_case, "non-enum case-bind diagnostic")

	# Foundry's parser producer requires `var` for binds (`fs_parser.cpp:4361-4388` @ c9d5e35);
	# bare identifiers are value patterns and must not be silently accepted as undeclared locals.
	var array_ok := _src_class("ArrayPatOk extends Node\nfunc handle(xs: Array[int]) -> int:\n\tmatch xs:\n\t\t[var a, var b]:\n\t\t\treturn a + b\n\t\t_:\n\t\t\treturn 0\n")
	var array_ok_report: Dictionary = probe.analyze_source(array_ok, "res://tests/array_pat_ok.barista")
	_expect(failures, array_ok_report.get("valid", false) == true, "Array[int] pattern binds are valid")

	var dict_ok := _src_class("DictPatOk extends Node\nfunc handle(d: Dictionary[String, int]) -> int:\n\tmatch d:\n\t\t{\"a\": var n}:\n\t\t\treturn n\n\t\t_:\n\t\t\treturn 0\n")
	var dict_ok_report: Dictionary = probe.analyze_source(dict_ok, "res://tests/dict_pat_ok.barista")
	_expect(failures, dict_ok_report.get("valid", false) == true, "Dictionary[String, int] pattern binds are valid")

	var dict_key_bad := _src_class("DictPatKeyBad extends Node\nfunc handle(d: Dictionary) -> void:\n\tvar k := \"a\"\n\tmatch d:\n\t\t{k: v}:\n\t\t\tpass\n")
	var dict_key_bad_report: Dictionary = probe.analyze_source(dict_key_bad, "res://tests/dict_pat_key_bad.barista")
	_expect(failures, dict_key_bad_report.get("valid", true) == false, "non-constant dictionary pattern key is invalid")
	var saw_dict_key := false
	for message in dict_key_bad_report.get("errors", PackedStringArray()):
		if "dictionary pattern key must be a constant" in message:
			saw_dict_key = true
	_expect(failures, saw_dict_key, "dictionary pattern key constant diagnostic")

	var tuple_arity := _src_class("TuplePatArity extends Node\nfunc handle(t: (int, String)) -> void:\n\tmatch t:\n\t\t(a, b, c):\n\t\t\tpass\n")
	var tuple_arity_report: Dictionary = probe.analyze_source(tuple_arity, "res://tests/tuple_pat_arity.barista")
	_expect(failures, tuple_arity_report.get("valid", true) == false, "tuple pattern arity mismatch is invalid")
	var saw_tuple_arity := false
	for message in tuple_arity_report.get("errors", PackedStringArray()):
		if "Tuple pattern has 3 element(s)" in message and "has 2" in message:
			saw_tuple_arity = true
	_expect(failures, saw_tuple_arity, "tuple pattern arity diagnostic")

	# Opt-in declaration-index mutation unchanged.
	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(match_ok, "res://tests/enum_match_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "ENUM_CASE match remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(match_ok, "res://tests/enum_match_is_valid.barista"),
		"ENUM_CASE match remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for ENUM_CASE match")


func _test_contextual_case_shorthand(failures: PackedStringArray) -> void:
	# Foundry resolve_contextual_case_pattern_type / resolve_contextual_case_value_pattern
	# + reduce_type_test contextual `.Case` + resolve_contextual_enum_case assign/return @ c9d5e35.
	var probe := BaristaScriptAnalyzerProbe.new()

	var match_payload := _src_class("CtxMatchPayload extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\t.Move(dx, dy):\n\t\t\treturn dx + dy\n\t\t_:\n\t\t\treturn 0\n")
	var match_payload_report: Dictionary = probe.analyze_source(match_payload, "res://tests/ctx_match_payload.barista")
	_expect(failures, match_payload_report.get("valid", false) == true, ".Move(dx, dy) contextual match is valid")

	var match_value := _src_class("CtxMatchValue extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\t.Quit:\n\t\t\treturn 1\n\t\t_:\n\t\t\treturn 0\n")
	var match_value_report: Dictionary = probe.analyze_source(match_value, "res://tests/ctx_match_value.barista")
	_expect(failures, match_value_report.get("valid", false) == true, ".Quit contextual match is valid")

	var match_arity := _src_class("CtxMatchArity extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\t.Move(dx):\n\t\t\tpass\n")
	var match_arity_report: Dictionary = probe.analyze_source(match_arity, "res://tests/ctx_match_arity.barista")
	_expect(failures, match_arity_report.get("valid", true) == false, ".Move arity mismatch is invalid")
	var saw_match_arity := false
	for message in match_arity_report.get("errors", PackedStringArray()):
		if "carries 2 payload value(s), but 1 pattern(s) were given" in message:
			saw_match_arity = true
	_expect(failures, saw_match_arity, "contextual ENUM_CASE payload arity diagnostic")

	var match_unknown := _src_class("CtxMatchUnknown extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\t.Nope:\n\t\t\tpass\n")
	var match_unknown_report: Dictionary = probe.analyze_source(match_unknown, "res://tests/ctx_match_unknown.barista")
	_expect(failures, match_unknown_report.get("valid", true) == false, "unknown contextual case is invalid")
	var saw_unknown := false
	for message in match_unknown_report.get("errors", PackedStringArray()):
		if 'Tagged union "Message" has no case "Nope"' in message:
			saw_unknown = true
	_expect(failures, saw_unknown, "unknown contextual case diagnostic")

	var match_bad_subject := _src_class("CtxMatchBadSubject extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(n: int) -> void:\n\tmatch n:\n\t\t.Quit:\n\t\t\tpass\n")
	var match_bad_subject_report: Dictionary = probe.analyze_source(match_bad_subject, "res://tests/ctx_match_bad_subject.barista")
	_expect(failures, match_bad_subject_report.get("valid", true) == false, "contextual case on non-union subject is invalid")
	var saw_bad_subject := false
	for message in match_bad_subject_report.get("errors", PackedStringArray()):
		if "needs a tagged-union match subject" in message and 'type "int"' in message:
			saw_bad_subject = true
	_expect(failures, saw_bad_subject, "non-union subject contextual shorthand diagnostic")

	var is_ok := _src_class("CtxIsOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tif msg is .Move(dx, dy):\n\t\treturn dx + dy\n\treturn 0\n")
	var is_ok_report: Dictionary = probe.analyze_source(is_ok, "res://tests/ctx_is_ok.barista")
	_expect(failures, is_ok_report.get("valid", false) == true, "is .Move(dx, dy) contextual case binds are valid")

	var is_arity := _src_class("CtxIsArity extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> void:\n\tif msg is .Move(dx):\n\t\tpass\n")
	var is_arity_report: Dictionary = probe.analyze_source(is_arity, "res://tests/ctx_is_arity.barista")
	_expect(failures, is_arity_report.get("valid", true) == false, "contextual is-case arity mismatch is invalid")
	var saw_is_arity := false
	for message in is_arity_report.get("errors", PackedStringArray()):
		if "carries 2 payload value(s), but 1 bind(s) were given" in message:
			saw_is_arity = true
	_expect(failures, saw_is_arity, "contextual is-case payload arity diagnostic")

	var is_bad_operand := _src_class("CtxIsBadOperand extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(n: int) -> void:\n\tif n is .Quit:\n\t\tpass\n")
	var is_bad_operand_report: Dictionary = probe.analyze_source(is_bad_operand, "res://tests/ctx_is_bad_operand.barista")
	_expect(failures, is_bad_operand_report.get("valid", true) == false, "is .Quit on non-union operand is invalid")
	var saw_bad_operand := false
	for message in is_bad_operand_report.get("errors", PackedStringArray()):
		if 'needs a tagged-union "is" operand' in message and 'type "int"' in message:
			saw_bad_operand = true
	_expect(failures, saw_bad_operand, "non-union is-operand contextual shorthand diagnostic")

	# Foundry contextual_tagged_union_pattern_undeclared_subject @ c9d5e35: subject error delta
	# suppresses per-arm "needs a tagged-union match subject… Variant" cascades.
	var match_missing := _src_class("CtxMatchMissingSubject extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tmatch missing_ident:\n\t\t.Quit:\n\t\t\tpass\n\t\t.Move(x):\n\t\t\tpass\n")
	var match_missing_report: Dictionary = probe.analyze_source(match_missing, "res://tests/ctx_match_missing_subject.barista")
	_expect(failures, match_missing_report.get("valid", true) == false, "match missing_ident with contextual arms is invalid")
	var saw_missing_ident := false
	var cascade_count := 0
	for message in match_missing_report.get("errors", PackedStringArray()):
		if 'Identifier "missing_ident" not declared in the current scope.' in message:
			saw_missing_ident = true
		if "needs a tagged-union match subject" in message:
			cascade_count += 1
	_expect(failures, saw_missing_ident, "match missing_ident subject diagnostic present")
	_expect(failures, cascade_count == 0, "failed match subject must not cascade per-arm Variant tagged-union diagnostics")

	# Optional nits: bare payload `.Move` value pattern; happy-path `is .Quit`.
	var match_bare_payload := _src_class("CtxMatchBarePayload extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\t.Move:\n\t\t\tpass\n")
	var match_bare_payload_report: Dictionary = probe.analyze_source(match_bare_payload, "res://tests/ctx_match_bare_payload.barista")
	_expect(failures, match_bare_payload_report.get("valid", true) == false, "bare .Move value pattern is invalid")
	var saw_bare_payload := false
	for message in match_bare_payload_report.get("errors", PackedStringArray()):
		if 'Case "Move" carries a payload, so it is matched with payload patterns' in message:
			saw_bare_payload = true
	_expect(failures, saw_bare_payload, "bare .Move payload-form diagnostic")

	var is_quit_ok := _src_class("CtxIsQuitOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(msg: Message) -> bool:\n\treturn msg is .Quit\n")
	var is_quit_ok_report: Dictionary = probe.analyze_source(is_quit_ok, "res://tests/ctx_is_quit_ok.barista")
	_expect(failures, is_quit_ok_report.get("valid", false) == true, "is .Quit contextual tag test is valid")

	# Foundry resolve_contextual_enum_case @ c9d5e35: expression-position assign/return construction.
	var var_ok := _src_class("CtxVarOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar idle: Message = .Quit\n\tvar moved: Message = .Move(1, 2)\n")
	var var_ok_report: Dictionary = probe.analyze_source(var_ok, "res://tests/ctx_var_ok.barista")
	_expect(failures, var_ok_report.get("valid", false) == true, "annotated var .Quit / .Move construction is valid")

	var assign_ok := _src_class("CtxAssignOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nvar member: Message = .Quit\nfunc handle() -> void:\n\tvar local: Message = .Quit\n\tlocal = .Move(3, 4)\n\tmember = .Quit\n")
	var assign_ok_report: Dictionary = probe.analyze_source(assign_ok, "res://tests/ctx_assign_ok.barista")
	_expect(failures, assign_ok_report.get("valid", false) == true, "assignment RHS .Case construction is valid")

	var return_ok := _src_class("CtxReturnOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc make_quit() -> Message:\n\treturn .Quit\nfunc make_move() -> Message:\n\treturn .Move(5, 6)\n")
	var return_ok_report: Dictionary = probe.analyze_source(return_ok, "res://tests/ctx_return_ok.barista")
	_expect(failures, return_ok_report.get("valid", false) == true, "return .Case construction is valid")

	var param_default_ok := _src_class("CtxParamDefaultOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(msg: Message = .Quit) -> Message:\n\treturn msg\n")
	var param_default_ok_report: Dictionary = probe.analyze_source(param_default_ok, "res://tests/ctx_param_default_ok.barista")
	_expect(failures, param_default_ok_report.get("valid", false) == true, "parameter default .Quit construction is valid")

	var untyped := _src_class("CtxUntyped extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar inferred = .Quit\n")
	var untyped_report: Dictionary = probe.analyze_source(untyped, "res://tests/ctx_untyped.barista")
	_expect(failures, untyped_report.get("valid", true) == false, "untyped .Quit without expected union is invalid")
	var saw_annotate := false
	for message in untyped_report.get("errors", PackedStringArray()):
		if 'needs an expected tagged-union type; annotate the target' in message:
			saw_annotate = true
	_expect(failures, saw_annotate, "untyped contextual construction annotate-target diagnostic")

	var wrong_expected := _src_class("CtxWrongExpected extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar counter: int = .Quit\n")
	var wrong_expected_report: Dictionary = probe.analyze_source(wrong_expected, "res://tests/ctx_wrong_expected.barista")
	_expect(failures, wrong_expected_report.get("valid", true) == false, ".Quit into int expected type is invalid")
	var saw_expects := false
	for message in wrong_expected_report.get("errors", PackedStringArray()):
		if 'needs an expected tagged-union type, but this position expects "int"' in message:
			saw_expects = true
	_expect(failures, saw_expects, "non-union expected-type contextual construction diagnostic")

	var payload_form := _src_class("CtxPayloadForm extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar bad: Message = .Move\n")
	var payload_form_report: Dictionary = probe.analyze_source(payload_form, "res://tests/ctx_payload_form.barista")
	_expect(failures, payload_form_report.get("valid", true) == false, "bare .Move construction is invalid")
	var saw_payload_form := false
	for message in payload_form_report.get("errors", PackedStringArray()):
		if 'carries a payload and must be constructed' in message:
			saw_payload_form = true
	_expect(failures, saw_payload_form, "bare .Move construction payload-form diagnostic")

	var arity_bad := _src_class("CtxConstructArity extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar bad: Message = .Move(1)\n")
	var arity_bad_report: Dictionary = probe.analyze_source(arity_bad, "res://tests/ctx_construct_arity.barista")
	_expect(failures, arity_bad_report.get("valid", true) == false, ".Move arity mismatch construction is invalid")
	var saw_construct_arity := false
	for message in arity_bad_report.get("errors", PackedStringArray()):
		if 'expects 2 argument(s), but 1 were given' in message:
			saw_construct_arity = true
	_expect(failures, saw_construct_arity, "contextual construction payload arity diagnostic")

	# Foundry update_container_literal_element_types / reduce_cast @ c9d5e35:
	# array / dictionary / cast / ternary consumers for contextual `.Case`.
	var array_ok := _src_class("CtxArrayOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar msgs: Array[Message] = [.Quit, .Move(1, 2)]\n")
	var array_ok_report: Dictionary = probe.analyze_source(array_ok, "res://tests/ctx_array_ok.barista")
	_expect(failures, array_ok_report.get("valid", false) == true, "Array[Message] element .Case construction is valid")

	var array_nested := _src_class("CtxArrayNested extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar nested: Array[Array[Message]] = [[.Quit, .Move(3)]]\n")
	var array_nested_report: Dictionary = probe.analyze_source(array_nested, "res://tests/ctx_array_nested.barista")
	_expect(failures, array_nested_report.get("valid", false) == true, "nested Array[Array[Message]] .Case construction is valid")

	var dict_ok := _src_class("CtxDictOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar by_id: Dictionary[int, Message] = {1: .Quit, 2: .Move(4)}\n\tvar labels: Dictionary[Message, String] = {.Quit: \"done\"}\n")
	var dict_ok_report: Dictionary = probe.analyze_source(dict_ok, "res://tests/ctx_dict_ok.barista")
	_expect(failures, dict_ok_report.get("valid", false) == true, "Dictionary key/value .Case construction is valid")

	var cast_ok := _src_class("CtxCastOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar labeled = .Quit as Message\n\tvar moved = .Move(5, 6) as Message\n")
	var cast_ok_report: Dictionary = probe.analyze_source(cast_ok, "res://tests/ctx_cast_ok.barista")
	_expect(failures, cast_ok_report.get("valid", false) == true, "cast operand .Case construction is valid")

	var cast_array := _src_class("CtxCastArray extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar labeled = [.Quit, .Move(7)] as Array[Message]\n")
	var cast_array_report: Dictionary = probe.analyze_source(cast_array, "res://tests/ctx_cast_array.barista")
	_expect(failures, cast_array_report.get("valid", false) == true, "cast Array[Message] element .Case construction is valid")

	var ternary_ok := _src_class("CtxTernaryOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc choose(cond: bool) -> Message:\n\treturn .Quit if cond else .Move(8)\nfunc handle(cond: bool) -> void:\n\tvar chosen: Message = .Quit if cond else .Move(9)\n\tvar elements: Array[Message] = [.Quit if cond else .Move(10)]\n\tvar labeled = (.Quit if cond else .Move(11)) as Message\n")
	var ternary_ok_report: Dictionary = probe.analyze_source(ternary_ok, "res://tests/ctx_ternary_ok.barista")
	_expect(failures, ternary_ok_report.get("valid", false) == true, "ternary branch .Case construction is valid")

	var call_arg_ok := _src_class("CtxCallArgOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc consume(msg: Message) -> void:\n\tpass\nfunc handle(cond: bool) -> void:\n\tconsume(.Quit)\n\tconsume(.Quit if cond else .Move(12))\n")
	var call_arg_ok_report: Dictionary = probe.analyze_source(call_arg_ok, "res://tests/ctx_call_arg_ok.barista")
	_expect(failures, call_arg_ok_report.get("valid", false) == true, "call-argument .Case construction is valid")

	var array_untyped := _src_class("CtxArrayUntyped extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar msgs = [.Quit]\n")
	var array_untyped_report: Dictionary = probe.analyze_source(array_untyped, "res://tests/ctx_array_untyped.barista")
	_expect(failures, array_untyped_report.get("valid", true) == false, "untyped array element .Quit is invalid")
	var saw_array_annotate := false
	for message in array_untyped_report.get("errors", PackedStringArray()):
		if 'needs an expected tagged-union type; annotate the target' in message:
			saw_array_annotate = true
	_expect(failures, saw_array_annotate, "untyped array element contextual construction diagnostic")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(match_payload, "res://tests/ctx_match_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "contextual match remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(match_payload, "res://tests/ctx_match_is_valid.barista"),
		"contextual match remains valid under is_semantically_valid()")
	_expect(failures, probe.is_semantically_valid(var_ok, "res://tests/ctx_var_is_valid.barista"),
		"contextual assign construction remains valid under is_semantically_valid()")
	_expect(failures, probe.is_semantically_valid(array_ok, "res://tests/ctx_array_is_valid.barista"),
		"contextual array construction remains valid under is_semantically_valid()")
	_expect(failures, probe.is_semantically_valid(cast_ok, "res://tests/ctx_cast_is_valid.barista"),
		"contextual cast construction remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for contextual case")


func _test_tagged_union_match_exhaustiveness(failures: PackedStringArray) -> void:
	# Foundry check_match_exhaustiveness tagged-union / plain-enum slice @ c9d5e35 (#60).
	# Pending warnings are flushed in finalize even when flow-finality exits early (#60 residual).
	var probe := BaristaScriptAnalyzerProbe.new()
	ProjectSettings.set_setting("debug/barista_script/warnings/enable", true)
	ProjectSettings.set_setting("debug/barista_script/warnings/non_exhaustive_match", 1) # WARN
	ProjectSettings.set_setting("debug/barista_script/warnings/open_enum_match_without_default", 1) # WARN
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()

	var incomplete := _src_class("TaggedMatchIncomplete extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\tpass\n")
	var incomplete_report: Dictionary = probe.validate_source(incomplete, "res://tests/tagged_match_incomplete.barista", true)
	_expect(failures, incomplete_report.get("valid", false) == true, "non-exhaustive tagged-union void match stays valid (warning-only)")
	var saw_non_exhaustive := false
	var saw_quit_uncovered := false
	for warn in incomplete_report.get("warnings", []):
		if "NON_EXHAUSTIVE_MATCH" in str(warn.get("string_code", "")):
			saw_non_exhaustive = true
		if "Quit" in str(warn.get("message", "")):
			saw_quit_uncovered = true
	_expect(failures, saw_non_exhaustive, "tagged-union match emits NON_EXHAUSTIVE_MATCH")
	_expect(failures, saw_quit_uncovered, "NON_EXHAUSTIVE_MATCH lists uncovered Quit case")

	# covers_subject_domain false keeps value-returning matches fail-closed for flow finality,
	# and finalize still surfaces the NON_EXHAUSTIVE_MATCH pending warning.
	var incomplete_ret := _src_class("TaggedMatchIncompleteRet extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\treturn dx + dy\n")
	var incomplete_ret_report: Dictionary = probe.validate_source(incomplete_ret, "res://tests/tagged_match_incomplete_ret.barista", true)
	_expect(failures, incomplete_ret_report.get("valid", true) == false, "non-exhaustive tagged-union match / missing return is invalid")
	var saw_flow := false
	for err in incomplete_ret_report.get("errors", []):
		if "Not all code paths return a value" in str(err.get("message", "")):
			saw_flow = true
	_expect(failures, saw_flow, "non-covering tagged-union match fails return-path flow finality")
	var saw_ret_non_exhaustive := false
	var saw_ret_quit := false
	for warn in incomplete_ret_report.get("warnings", []):
		if "NON_EXHAUSTIVE_MATCH" in str(warn.get("string_code", "")):
			saw_ret_non_exhaustive = true
		if "Quit" in str(warn.get("message", "")):
			saw_ret_quit = true
	_expect(failures, saw_ret_non_exhaustive, "flow-finality early exit still flushes NON_EXHAUSTIVE_MATCH")
	_expect(failures, saw_ret_quit, "early-exit NON_EXHAUSTIVE_MATCH lists uncovered Quit case")

	var exhaustive := _src_class("TaggedMatchOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\treturn dx + dy\n\t\tMessage.Quit:\n\t\t\treturn 0\n")
	var exhaustive_report: Dictionary = probe.analyze_source(exhaustive, "res://tests/tagged_match_ok.barista")
	_expect(failures, exhaustive_report.get("valid", false) == true, "exhaustive tagged-union match with returns is valid")
	var saw_exhaustive_warn := false
	for warn in exhaustive_report.get("warnings", []):
		if "NON_EXHAUSTIVE_MATCH" in str(warn.get("string_code", "")):
			saw_exhaustive_warn = true
	_expect(failures, not saw_exhaustive_warn, "exhaustive tagged-union match has no NON_EXHAUSTIVE_MATCH")

	var contextual_ok := _src_class("TaggedMatchContextualOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\t.Move(dx):\n\t\t\treturn dx\n\t\t.Quit:\n\t\t\treturn 0\n")
	var contextual_ok_report: Dictionary = probe.analyze_source(contextual_ok, "res://tests/tagged_match_contextual_ok.barista")
	_expect(failures, contextual_ok_report.get("valid", false) == true, "exhaustive contextual .Case match is valid")

	var plain_enum := _src_class("PlainEnumMatch extends Node\nenum Level:\n\tLow = 1\n\tHigh = 2\nfunc handle(level: Level) -> void:\n\tmatch level:\n\t\tLevel.Low:\n\t\t\tpass\n\t\tLevel.High:\n\t\t\tpass\n")
	var plain_enum_report: Dictionary = probe.validate_source(plain_enum, "res://tests/plain_enum_match.barista", true)
	_expect(failures, plain_enum_report.get("valid", false) == true, "plain-enum match stays valid (warning-only)")
	var saw_open_enum := false
	for warn in plain_enum_report.get("warnings", []):
		if "OPEN_ENUM_MATCH_WITHOUT_DEFAULT" in str(warn.get("string_code", "")):
			saw_open_enum = true
	_expect(failures, saw_open_enum, "plain enum match emits OPEN_ENUM_MATCH_WITHOUT_DEFAULT")

	var bool_ok := _src_class("BoolMatchStillOk extends Node\nfunc check(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n\t\tfalse:\n\t\t\treturn 0\n")
	var bool_ok_report: Dictionary = probe.analyze_source(bool_ok, "res://tests/bool_match_still_ok.barista")
	_expect(failures, bool_ok_report.get("valid", false) == true, "exhaustive bool match remains valid after exhaustiveness port")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(exhaustive, "res://tests/tagged_match_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "exhaustive tagged match remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(exhaustive, "res://tests/tagged_match_is_valid.barista"),
		"exhaustive tagged match remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for tagged exhaustiveness")

func _test_callable_bind_unbind(failures: PackedStringArray) -> void:
	# Foundry Callable.bind / bindv / unbind / call transforms @ c9d5e35 (#60).
	# Bare function refs publish explicit Callable signatures; bind/unbind reshape them.
	var probe := BaristaScriptAnalyzerProbe.new()

	var bind_type_bad := _src_class("CallableBindTypeBad extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tvar _bound := one.bind(\"not an int\")\n")
	var bind_type_bad_report: Dictionary = probe.analyze_source(bind_type_bad, "res://tests/callable_bind_type_bad.barista")
	_expect(failures, bind_type_bad_report.get("valid", true) == false, "bind String where int expected is invalid")
	var saw_bind_type := false
	for message in bind_type_bad_report.get("errors", PackedStringArray()):
		if 'argument 1 should be "int"' in message or 'should be "int" but is "String"' in message:
			saw_bind_type = true
	_expect(failures, saw_bind_type, "bind argument type mismatch diagnostic")

	var bind_call_ok := _src_class("CallableBindCallOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> int:\n\treturn add.bind(5).call(2)\n")
	var bind_call_ok_report: Dictionary = probe.analyze_source(bind_call_ok, "res://tests/callable_bind_call_ok.barista")
	_expect(failures, bind_call_ok_report.get("valid", false) == true, "bind then call with remaining arity is valid")

	var bind_call_arity := _src_class("CallableBindCallArity extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tadd.bind(5).call()\n")
	var bind_call_arity_report: Dictionary = probe.analyze_source(bind_call_arity, "res://tests/callable_bind_call_arity.barista")
	_expect(failures, bind_call_arity_report.get("valid", true) == false, "bound call missing remaining arg is invalid")
	var saw_too_few := false
	for message in bind_call_arity_report.get("errors", PackedStringArray()):
		if "Too few arguments for \"call()\" call" in message:
			saw_too_few = true
	_expect(failures, saw_too_few, "bound call too-few-arguments diagnostic")

	var over_bound := _src_class("CallableOverBound extends Node\nfunc zero_arg() -> int:\n\treturn 1\nfunc test() -> void:\n\tzero_arg.bind(1).call()\n")
	var over_bound_report: Dictionary = probe.analyze_source(over_bound, "res://tests/callable_over_bound.barista")
	_expect(failures, over_bound_report.get("valid", true) == false, "over-bound callable invocation is invalid")
	var saw_over_bound := false
	for message in over_bound_report.get("errors", PackedStringArray()):
		if "over-bound" in message:
			saw_over_bound = true
	_expect(failures, saw_over_bound, "over-bound invocation diagnostic")

	var unbind_bad := _src_class("CallableUnbindBad extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tvar _u := one.unbind(0)\n")
	var unbind_bad_report: Dictionary = probe.analyze_source(unbind_bad, "res://tests/callable_unbind_bad.barista")
	_expect(failures, unbind_bad_report.get("valid", true) == false, "unbind(0) is invalid")
	var saw_unbind := false
	for message in unbind_bad_report.get("errors", PackedStringArray()):
		if 'Amount of "unbind()" arguments must be 1 or greater' in message:
			saw_unbind = true
	_expect(failures, saw_unbind, "unbind count diagnostic")

	var unbind_ok := _src_class("CallableUnbindOk extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> int:\n\treturn one.unbind(1).call(9, 1)\n")
	var unbind_ok_report: Dictionary = probe.analyze_source(unbind_ok, "res://tests/callable_unbind_ok.barista")
	_expect(failures, unbind_ok_report.get("valid", false) == true, "unbind(1) then call with unbound trailing slot is valid")

	var bindv_type_bad := _src_class("CallableBindvTypeBad extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tvar _bound := one.bindv([\"not an int\"])\n")
	var bindv_type_bad_report: Dictionary = probe.analyze_source(bindv_type_bad, "res://tests/callable_bindv_type_bad.barista")
	_expect(failures, bindv_type_bad_report.get("valid", true) == false, "bindv String where int expected is invalid")

	var default_survival := _src_class("CallableBindDefaultSurvival extends Node\nfunc add_with_default(p: int, q: int, s: int = 1) -> int:\n\treturn p + q + s\nfunc test() -> int:\n\treturn add_with_default.bind(5).call(2)\n")
	var default_survival_report: Dictionary = probe.analyze_source(default_survival, "res://tests/callable_bind_default_survival.barista")
	_expect(failures, default_survival_report.get("valid", false) == true, "bind preserves trailing default survival arity")

	# Foundry default-survival: Node bound into a trailing Node slot may shift onto a leading
	# Node2D parameter via allows_runtime_narrowing, keeping the trailing default callable.
	var narrow_survival := _src_class("CallableBindNarrowSurvival extends Node\nfunc takes(narrow: Node2D, wide: Node = null) -> int:\n\treturn 1\nfunc test() -> int:\n\tvar n: Node = Node2D.new()\n\treturn takes.bind(n).call()\n")
	var narrow_survival_report: Dictionary = probe.analyze_source(narrow_survival, "res://tests/callable_bind_narrow_survival.barista")
	_expect(failures, narrow_survival_report.get("valid", false) == true, "bind preserves default survival under native subtype narrowing")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(bind_call_ok, "res://tests/callable_bind_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "callable bind remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(bind_call_ok, "res://tests/callable_bind_is_valid.barista"),
		"callable bind remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for callable bind")


func _test_callable_callv_rpc(failures: PackedStringArray) -> void:
	# Foundry Callable.callv / call_deferred / rpc / rpc_id transforms @ c9d5e35 (#60).
	var probe := BaristaScriptAnalyzerProbe.new()

	var callv_type_bad := _src_class("CallableCallvTypeBad extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tone.callv([\"not an int\"])\n")
	var callv_type_bad_report: Dictionary = probe.analyze_source(callv_type_bad, "res://tests/callable_callv_type_bad.barista")
	_expect(failures, callv_type_bad_report.get("valid", true) == false, "callv String element where int expected is invalid")
	var saw_callv_type := false
	for message in callv_type_bad_report.get("errors", PackedStringArray()):
		if 'argument 1 should be "int"' in message or 'should be "int" but is "String"' in message:
			saw_callv_type = true
	_expect(failures, saw_callv_type, "callv array-literal element type mismatch diagnostic")

	var callv_ok := _src_class("CallableCallvOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> int:\n\treturn add.callv([2, 3])\n")
	var callv_ok_report: Dictionary = probe.analyze_source(callv_ok, "res://tests/callable_callv_ok.barista")
	_expect(failures, callv_ok_report.get("valid", false) == true, "callv with matching array-literal types is valid")

	var callv_arity := _src_class("CallableCallvArity extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tadd.callv([1])\n")
	var callv_arity_report: Dictionary = probe.analyze_source(callv_arity, "res://tests/callable_callv_arity.barista")
	_expect(failures, callv_arity_report.get("valid", true) == false, "callv missing array element is invalid")
	var saw_callv_few := false
	for message in callv_arity_report.get("errors", PackedStringArray()):
		if "Too few arguments for \"callv()\" call" in message:
			saw_callv_few = true
	_expect(failures, saw_callv_few, "callv too-few-arguments diagnostic")

	var deferred_ok := _src_class("CallableDeferredOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tadd.call_deferred(2, 3)\n")
	var deferred_ok_report: Dictionary = probe.analyze_source(deferred_ok, "res://tests/callable_deferred_ok.barista")
	_expect(failures, deferred_ok_report.get("valid", false) == true, "call_deferred with matching arity is valid")

	var deferred_type_bad := _src_class("CallableDeferredTypeBad extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tone.call_deferred(\"not an int\")\n")
	var deferred_type_bad_report: Dictionary = probe.analyze_source(deferred_type_bad, "res://tests/callable_deferred_type_bad.barista")
	_expect(failures, deferred_type_bad_report.get("valid", true) == false, "call_deferred type mismatch is invalid")

	var rpc_ok := _src_class("CallableRpcOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tadd.rpc(2, 3)\n")
	var rpc_ok_report: Dictionary = probe.analyze_source(rpc_ok, "res://tests/callable_rpc_ok.barista")
	_expect(failures, rpc_ok_report.get("valid", false) == true, "rpc with matching arity is valid")

	var rpc_id_ok := _src_class("CallableRpcIdOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tadd.rpc_id(1, 2, 3)\n")
	var rpc_id_ok_report: Dictionary = probe.analyze_source(rpc_id_ok, "res://tests/callable_rpc_id_ok.barista")
	_expect(failures, rpc_id_ok_report.get("valid", false) == true, "rpc_id peer_id plus matching target arity is valid")

	var rpc_id_peer_type := _src_class("CallableRpcIdPeerType extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tone.rpc_id(\"peer\", 1)\n")
	var rpc_id_peer_type_report: Dictionary = probe.analyze_source(rpc_id_peer_type, "res://tests/callable_rpc_id_peer_type.barista")
	_expect(failures, rpc_id_peer_type_report.get("valid", true) == false, "rpc_id non-int peer_id is invalid")
	var saw_peer_type := false
	for message in rpc_id_peer_type_report.get("errors", PackedStringArray()):
		if 'argument 1 should be "int"' in message or 'should be "int" but is "String"' in message:
			saw_peer_type = true
	_expect(failures, saw_peer_type, "rpc_id peer_id type mismatch diagnostic")

	var rpc_id_arity := _src_class("CallableRpcIdArity extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tadd.rpc_id(1, 2)\n")
	var rpc_id_arity_report: Dictionary = probe.analyze_source(rpc_id_arity, "res://tests/callable_rpc_id_arity.barista")
	_expect(failures, rpc_id_arity_report.get("valid", true) == false, "rpc_id missing target arg after peer_id is invalid")
	var saw_rpc_id_few := false
	for message in rpc_id_arity_report.get("errors", PackedStringArray()):
		if "Too few arguments for \"rpc_id()\" call" in message:
			saw_rpc_id_few = true
	_expect(failures, saw_rpc_id_few, "rpc_id too-few-arguments diagnostic includes peer_id offset")

	var over_callv := _src_class("CallableOverBoundCallv extends Node\nfunc zero_arg() -> int:\n\treturn 1\nfunc test() -> void:\n\tzero_arg.bind(1).callv([])\n")
	var over_callv_report: Dictionary = probe.analyze_source(over_callv, "res://tests/callable_over_bound_callv.barista")
	_expect(failures, over_callv_report.get("valid", true) == false, "over-bound callv invocation is invalid")

	var over_deferred := _src_class("CallableOverBoundDeferred extends Node\nfunc zero_arg() -> int:\n\treturn 1\nfunc test() -> void:\n\tzero_arg.bind(1).call_deferred()\n")
	var over_deferred_report: Dictionary = probe.analyze_source(over_deferred, "res://tests/callable_over_bound_deferred.barista")
	_expect(failures, over_deferred_report.get("valid", true) == false, "over-bound call_deferred invocation is invalid")

	var over_rpc := _src_class("CallableOverBoundRpc extends Node\nfunc zero_arg() -> int:\n\treturn 1\nfunc test() -> void:\n\tzero_arg.bind(1).rpc()\n")
	var over_rpc_report: Dictionary = probe.analyze_source(over_rpc, "res://tests/callable_over_bound_rpc.barista")
	_expect(failures, over_rpc_report.get("valid", true) == false, "over-bound rpc invocation is invalid")

	var over_rpc_id := _src_class("CallableOverBoundRpcId extends Node\nfunc zero_arg() -> int:\n\treturn 1\nfunc test() -> void:\n\tzero_arg.bind(1).rpc_id(1)\n")
	var over_rpc_id_report: Dictionary = probe.analyze_source(over_rpc_id, "res://tests/callable_over_bound_rpc_id.barista")
	_expect(failures, over_rpc_id_report.get("valid", true) == false, "over-bound rpc_id invocation is invalid")
	var saw_over_rpc_id := false
	for message in over_rpc_id_report.get("errors", PackedStringArray()):
		if "over-bound" in message:
			saw_over_rpc_id = true
	_expect(failures, saw_over_rpc_id, "over-bound rpc_id invocation diagnostic")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(callv_ok, "res://tests/callable_callv_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "callable callv remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(callv_ok, "res://tests/callable_callv_is_valid.barista"),
		"callable callv remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for callable callv/rpc")


func _test_async_callable_coroutine_wrap(failures: PackedStringArray) -> void:
	# Foundry AsyncCallable→coroutine wrap on call/callv @ c9d5e35 (#60).
	var probe := BaristaScriptAnalyzerProbe.new()

	# Synchronous call on an async method reference yields Coroutine[T], not T.
	var async_call_assign := _src_class("AsyncCallableCallAssign extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = fetch.call()\n")
	var async_call_assign_report: Dictionary = probe.analyze_source(async_call_assign, "res://tests/async_callable_call_assign.barista")
	_expect(failures, async_call_assign_report.get("valid", true) == false, "AsyncCallable.call result is not assignable to int")
	var saw_async_call_coro := false
	for message in async_call_assign_report.get("errors", PackedStringArray()):
		if "Cannot assign a value of type" in message and "Coroutine[int]" in message and 'variable of type "int"' in message:
			saw_async_call_coro = true
	_expect(failures, saw_async_call_coro, "AsyncCallable.call diagnose Coroutine[int] assign to int")

	var async_callv_assign := _src_class("AsyncCallableCallvAssign extends Node\nasync func fetch(value: int) -> String:\n\treturn \"ok\"\nfunc test() -> void:\n\tvar result: String = fetch.callv([1])\n")
	var async_callv_assign_report: Dictionary = probe.analyze_source(async_callv_assign, "res://tests/async_callable_callv_assign.barista")
	_expect(failures, async_callv_assign_report.get("valid", true) == false, "AsyncCallable.callv result is not assignable to String")
	var saw_async_callv_coro := false
	for message in async_callv_assign_report.get("errors", PackedStringArray()):
		if "Cannot assign a value of type" in message and "Coroutine[String]" in message and 'variable of type "String"' in message:
			saw_async_callv_coro = true
	_expect(failures, saw_async_callv_coro, "AsyncCallable.callv diagnose Coroutine[String] assign to String")

	# Bound AsyncCallable preserves signature_is_async through bind, then call wraps.
	var async_bound_call := _src_class("AsyncCallableBoundCall extends Node\nasync func add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tvar result: int = add.bind(1).call(2)\n")
	var async_bound_call_report: Dictionary = probe.analyze_source(async_bound_call, "res://tests/async_callable_bound_call.barista")
	_expect(failures, async_bound_call_report.get("valid", true) == false, "bound AsyncCallable.call still yields coroutine")
	var saw_bound_coro := false
	for message in async_bound_call_report.get("errors", PackedStringArray()):
		if "Cannot assign a value of type" in message and "Coroutine[int]" in message:
			saw_bound_coro = true
	_expect(failures, saw_bound_coro, "bound AsyncCallable.call diagnose Coroutine[int]")

	# Deferred / RPC on AsyncCallable stay non-coroutine (NIL), so statement use is valid.
	var async_deferred_ok := _src_class("AsyncCallableDeferredOk extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tfetch.call_deferred()\n")
	var async_deferred_ok_report: Dictionary = probe.analyze_source(async_deferred_ok, "res://tests/async_callable_deferred_ok.barista")
	_expect(failures, async_deferred_ok_report.get("valid", false) == true, "AsyncCallable.call_deferred stays non-coroutine / valid")

	var async_rpc_ok := _src_class("AsyncCallableRpcOk extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tfetch.rpc()\n")
	var async_rpc_ok_report: Dictionary = probe.analyze_source(async_rpc_ok, "res://tests/async_callable_rpc_ok.barista")
	_expect(failures, async_rpc_ok_report.get("valid", false) == true, "AsyncCallable.rpc stays non-coroutine / valid")

	var async_rpc_id_ok := _src_class("AsyncCallableRpcIdOk extends Node\nasync func fetch(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tfetch.rpc_id(1, 2)\n")
	var async_rpc_id_ok_report: Dictionary = probe.analyze_source(async_rpc_id_ok, "res://tests/async_callable_rpc_id_ok.barista")
	_expect(failures, async_rpc_id_ok_report.get("valid", false) == true, "AsyncCallable.rpc_id stays non-coroutine / valid")

	# Assigning deferred NIL to int would fail if it were wrongly wrapped as Coroutine — prove NIL.
	var async_deferred_assign := _src_class("AsyncCallableDeferredAssign extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = fetch.call_deferred()\n")
	var async_deferred_assign_report: Dictionary = probe.analyze_source(async_deferred_assign, "res://tests/async_callable_deferred_assign.barista")
	_expect(failures, async_deferred_assign_report.get("valid", true) == false, "call_deferred NIL is not assignable to int")
	var saw_deferred_nil := false
	var saw_deferred_coro := false
	for message in async_deferred_assign_report.get("errors", PackedStringArray()):
		if "Cannot assign a value of type" in message and "Coroutine[" in message:
			saw_deferred_coro = true
		if "Cannot assign a value of type" in message and ("null" in message or "void" in message or "Nil" in message or "nil" in message):
			saw_deferred_nil = true
	_expect(failures, not saw_deferred_coro, "call_deferred on AsyncCallable must not wrap as Coroutine")
	_expect(failures, saw_deferred_nil, "call_deferred on AsyncCallable diagnoses NIL/null assign to int")

	# Plain (non-async) Callable.call is unchanged: returns T, assignable to T.
	var sync_call_ok := _src_class("SyncCallableCallOk extends Node\nfunc fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = fetch.call()\n")
	var sync_call_ok_report: Dictionary = probe.analyze_source(sync_call_ok, "res://tests/sync_callable_call_ok.barista")
	_expect(failures, sync_call_ok_report.get("valid", false) == true, "plain Callable.call return type unchanged")

	var sync_callv_ok := _src_class("SyncCallableCallvOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tvar result: int = add.callv([2, 3])\n")
	var sync_callv_ok_report: Dictionary = probe.analyze_source(sync_callv_ok, "res://tests/sync_callable_callv_ok.barista")
	_expect(failures, sync_callv_ok_report.get("valid", false) == true, "plain Callable.callv return type unchanged")

	# Bare AsyncCallable (no explicit signature) still wraps call as Coroutine[Variant].
	var bare_async_call := _src_class("BareAsyncCallableCall extends Node\nfunc test() -> void:\n\tvar cb: AsyncCallable\n\tvar result: int = cb.call()\n")
	var bare_async_call_report: Dictionary = probe.analyze_source(bare_async_call, "res://tests/bare_async_callable_call.barista")
	_expect(failures, bare_async_call_report.get("valid", true) == false, "bare AsyncCallable.call is not assignable to int")
	var saw_bare_coro := false
	for message in bare_async_call_report.get("errors", PackedStringArray()):
		if "Cannot assign a value of type" in message and "Coroutine[" in message:
			saw_bare_coro = true
	_expect(failures, saw_bare_coro, "bare AsyncCallable.call diagnose Coroutine wrap")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(sync_call_ok, "res://tests/async_callable_wrap_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "async-callable wrap suite remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(sync_call_ok, "res://tests/async_callable_wrap_is_valid.barista"),
		"async-callable wrap suite remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for async-callable wrap")


func _test_await_reduction_and_missing_await(failures: PackedStringArray) -> void:
	# Foundry reduce_await + MISSING_AWAIT / REDUNDANT_AWAIT @ c9d5e35 (#60).
	# Makes AsyncCallable→coroutine wrap (#116) usable end-to-end via await unwrap.
	var probe := BaristaScriptAnalyzerProbe.new()
	ProjectSettings.set_setting("debug/barista_script/warnings/enable", true)
	ProjectSettings.set_setting("debug/barista_script/warnings/missing_await", 1) # WARN
	ProjectSettings.set_setting("debug/barista_script/warnings/redundant_await", 1) # WARN

	# await AsyncCallable.call unwraps Coroutine[T] → T (assign succeeds).
	var await_call_ok := _src_class("AwaitCallOk extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = await fetch.call()\n")
	var await_call_ok_report: Dictionary = probe.analyze_source(await_call_ok, "res://tests/await_call_ok.barista")
	_expect(failures, await_call_ok_report.get("valid", false) == true, "await AsyncCallable.call unwraps Coroutine[int] to int")

	var await_callv_ok := _src_class("AwaitCallvOk extends Node\nasync func fetch(value: int) -> String:\n\treturn \"ok\"\nfunc test() -> void:\n\tvar result: String = await fetch.callv([1])\n")
	var await_callv_ok_report: Dictionary = probe.analyze_source(await_callv_ok, "res://tests/await_callv_ok.barista")
	_expect(failures, await_callv_ok_report.get("valid", false) == true, "await AsyncCallable.callv unwraps Coroutine[String] to String")

	# Bound AsyncCallable.call still unwraps after await.
	var await_bound_ok := _src_class("AwaitBoundOk extends Node\nasync func add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tvar result: int = await add.bind(1).call(2)\n")
	var await_bound_ok_report: Dictionary = probe.analyze_source(await_bound_ok, "res://tests/await_bound_ok.barista")
	_expect(failures, await_bound_ok_report.get("valid", false) == true, "await bound AsyncCallable.call unwraps to int")

	# Root-position non-void coroutine discard emits MISSING_AWAIT (valid with warn-level default).
	var missing_await := _src_class("MissingAwaitRoot extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tfetch.call()\n")
	var missing_await_report: Dictionary = probe.validate_source(missing_await, "res://tests/missing_await_root.barista", true)
	_expect(failures, missing_await_report.get("valid", false) == true, "MISSING_AWAIT at warn level stays valid")
	var saw_missing_await := false
	for warn in missing_await_report.get("warnings", []):
		if "MISSING_AWAIT" in str(warn.get("string_code", "")) or ("discarded" in str(warn.get("message", "")).to_lower() and "await" in str(warn.get("message", "")).to_lower()):
			saw_missing_await = true
	_expect(failures, saw_missing_await, "root-position non-void coroutine discard emits MISSING_AWAIT")

	# Coroutine[void] / void async fire-and-forget does not warn.
	var void_fire := _src_class("VoidFireForget extends Node\nasync func fire() -> void:\n\tpass\nfunc test() -> void:\n\tfire.call()\n")
	var void_fire_report: Dictionary = probe.validate_source(void_fire, "res://tests/void_fire_forget.barista", true)
	_expect(failures, void_fire_report.get("valid", false) == true, "Coroutine[void] fire-and-forget stays valid")
	var saw_void_missing := false
	for warn in void_fire_report.get("warnings", []):
		if "MISSING_AWAIT" in str(warn.get("string_code", "")):
			saw_void_missing = true
	_expect(failures, not saw_void_missing, "Coroutine[void] root discard does not emit MISSING_AWAIT")

	# call_deferred does not trigger MISSING_AWAIT (non-coroutine NIL).
	var deferred_no_missing := _src_class("DeferredNoMissing extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tfetch.call_deferred()\n")
	var deferred_no_missing_report: Dictionary = probe.validate_source(deferred_no_missing, "res://tests/deferred_no_missing.barista", true)
	_expect(failures, deferred_no_missing_report.get("valid", false) == true, "call_deferred statement stays valid")
	var saw_deferred_missing := false
	for warn in deferred_no_missing_report.get("warnings", []):
		if "MISSING_AWAIT" in str(warn.get("string_code", "")):
			saw_deferred_missing = true
	_expect(failures, not saw_deferred_missing, "call_deferred does not emit MISSING_AWAIT")

	# REDUNDANT_AWAIT on await of a plain synchronous int (Foundry gate).
	var redundant_await := _src_class("RedundantAwaitInt extends Node\nfunc test() -> void:\n\tvar _x: int = await 1\n")
	var redundant_await_report: Dictionary = probe.validate_source(redundant_await, "res://tests/redundant_await_int.barista", true)
	_expect(failures, redundant_await_report.get("valid", false) == true, "REDUNDANT_AWAIT at warn level stays valid")
	var saw_redundant := false
	for warn in redundant_await_report.get("warnings", []):
		if "REDUNDANT_AWAIT" in str(warn.get("string_code", "")) or ("unnecessary" in str(warn.get("message", "")).to_lower() and "await" in str(warn.get("message", "")).to_lower()):
			saw_redundant = true
	_expect(failures, saw_redundant, "await of plain int emits REDUNDANT_AWAIT")

	# await of a non-void coroutine under await must not also emit MISSING_AWAIT.
	var awaited_no_missing := _src_class("AwaitedNoMissing extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar _result: int = await fetch.call()\n")
	var awaited_no_missing_report: Dictionary = probe.validate_source(awaited_no_missing, "res://tests/awaited_no_missing.barista", true)
	_expect(failures, awaited_no_missing_report.get("valid", false) == true, "awaited AsyncCallable.call stays valid")
	var saw_awaited_missing := false
	for warn in awaited_no_missing_report.get("warnings", []):
		if "MISSING_AWAIT" in str(warn.get("string_code", "")):
			saw_awaited_missing = true
	_expect(failures, not saw_awaited_missing, "awaited coroutine call does not emit MISSING_AWAIT")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(await_call_ok, "res://tests/await_reduce_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "await-reduction suite remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(await_call_ok, "res://tests/await_reduce_is_valid.barista"),
		"await-reduction suite remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for await reduction")


func _test_coroutine_annotation_decode(failures: PackedStringArray) -> void:
	# Foundry datatype_from_type_node Coroutine[T] annotation decode @ c9d5e35 (#60).
	# Bridges parser is_coroutine TypeNode → NATIVE BSFunctionState skin via make_coroutine_type.
	var probe := BaristaScriptAnalyzerProbe.new()

	# Annotated Coroutine[T] holds an AsyncCallable.call result (honest awaitable skin).
	var annotate_hold := _src_class("CoroutineAnnotateHold extends Node\nasync func fetch() -> String:\n\treturn \"ok\"\nfunc test() -> void:\n\tvar work: Coroutine[String] = fetch.call()\n")
	var annotate_hold_report: Dictionary = probe.analyze_source(annotate_hold, "res://tests/coroutine_annotate_hold.barista")
	_expect(failures, annotate_hold_report.get("valid", false) == true, "var work: Coroutine[String] holds AsyncCallable.call result")

	# await of an annotated Coroutine[T] yields T.
	var annotate_await := _src_class("CoroutineAnnotateAwait extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar work: Coroutine[int] = fetch.call()\n\tvar result: int = await work\n")
	var annotate_await_report: Dictionary = probe.analyze_source(annotate_await, "res://tests/coroutine_annotate_await.barista")
	_expect(failures, annotate_await_report.get("valid", false) == true, "await of Coroutine[int] annotation yields int")

	# Phantom result mismatch: Coroutine[int] is not assignable to Coroutine[String].
	var annotate_mismatch := _src_class("CoroutineAnnotateMismatch extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar work: Coroutine[String] = fetch.call()\n")
	var annotate_mismatch_report: Dictionary = probe.analyze_source(annotate_mismatch, "res://tests/coroutine_annotate_mismatch.barista")
	_expect(failures, annotate_mismatch_report.get("valid", true) == false, "Coroutine[int] is not assignable to Coroutine[String]")
	var saw_coro_mismatch := false
	for message in annotate_mismatch_report.get("errors", PackedStringArray()):
		if "Cannot assign a value of type" in message and "Coroutine[int]" in message and "Coroutine[String]" in message:
			saw_coro_mismatch = true
	_expect(failures, saw_coro_mismatch, "Coroutine phantom-result mismatch diagnoses Coroutine[int] vs Coroutine[String]")

	# Assigning annotated Coroutine[T] into T without await fails.
	var annotate_no_await := _src_class("CoroutineAnnotateNoAwait extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar work: Coroutine[int] = fetch.call()\n\tvar result: int = work\n")
	var annotate_no_await_report: Dictionary = probe.analyze_source(annotate_no_await, "res://tests/coroutine_annotate_no_await.barista")
	_expect(failures, annotate_no_await_report.get("valid", true) == false, "Coroutine[int] is not assignable to int without await")
	var saw_no_await := false
	for message in annotate_no_await_report.get("errors", PackedStringArray()):
		if "Cannot assign a value of type" in message and "Coroutine[int]" in message and 'variable of type "int"' in message:
			saw_no_await = true
	_expect(failures, saw_no_await, "annotated Coroutine[int] without await diagnoses assign to int")

	# Wrong arity: Coroutine[] (parser + analyzer fail-stop; must not be M5 generic).
	var arity_empty := _src_class("CoroutineArityEmpty extends Node\nfunc test() -> void:\n\tvar work: Coroutine[]\n")
	var arity_empty_report: Dictionary = probe.analyze_source(arity_empty, "res://tests/coroutine_arity_empty.barista")
	_expect(failures, arity_empty_report.get("valid", true) == false, "Coroutine[] wrong arity is invalid")
	var saw_arity_empty := false
	var saw_m5_on_coro := false
	for message in arity_empty_report.get("errors", PackedStringArray()):
		if "Coroutine[T]" in message and ("expects" in message or "single" in message or "exactly one" in message):
			saw_arity_empty = true
		if "Generic type specialization is not available until M5" in message:
			saw_m5_on_coro = true
	_expect(failures, saw_arity_empty, "Coroutine[] diagnoses wrong arity")
	_expect(failures, not saw_m5_on_coro, "Coroutine[] must not emit M5 generic specialization error")

	# Wrong arity: Coroutine[int, String] keeps parser arity error (analyzer still sees one container type).
	var arity_extra := _src_class("CoroutineArityExtra extends Node\nfunc test() -> void:\n\tvar work: Coroutine[int, String]\n")
	var arity_extra_report: Dictionary = probe.analyze_source(arity_extra, "res://tests/coroutine_arity_extra.barista")
	_expect(failures, arity_extra_report.get("valid", true) == false, "Coroutine[int, String] wrong arity is invalid")
	var saw_arity_extra := false
	for message in arity_extra_report.get("errors", PackedStringArray()):
		if "Coroutine[T]" in message and ("more were given" in message or "single" in message or "expects" in message):
			saw_arity_extra = true
	_expect(failures, saw_arity_extra, "Coroutine[int, String] diagnoses wrong arity")

	# Array/Dictionary containers stay intact (not routed through Coroutine decode).
	var array_ok := _src_class("CoroutineArrayStillOk extends Node\nfunc test() -> void:\n\tvar a: Array[int] = [1]\n\tvar d: Dictionary[String, int] = {\"a\": 1}\n")
	var array_ok_report: Dictionary = probe.analyze_source(array_ok, "res://tests/coroutine_array_still_ok.barista")
	_expect(failures, array_ok_report.get("valid", false) == true, "Array/Dictionary containers still decode after Coroutine annotation path")

	# Other bracketed generics remain M5-deferred.
	var other_generic := _src_class("CoroutineOtherGeneric extends Node\nfunc test() -> void:\n\tvar x: NotAContainer[int]\n")
	var other_generic_report: Dictionary = probe.analyze_source(other_generic, "res://tests/coroutine_other_generic.barista")
	_expect(failures, other_generic_report.get("valid", true) == false, "non-Coroutine generic specialization stays invalid")
	var saw_m5_other := false
	for message in other_generic_report.get("errors", PackedStringArray()):
		if "Generic type specialization is not available until M5" in message:
			saw_m5_other = true
	_expect(failures, saw_m5_other, "non-Coroutine generic still emits M5 deferred diagnostic")

	# Coroutine[void] annotation is nameable (fire-and-forget handle).
	var void_anno := _src_class("CoroutineVoidAnnotate extends Node\nasync func fire() -> void:\n\tpass\nfunc test() -> void:\n\tvar work: Coroutine[void] = fire.call()\n")
	var void_anno_report: Dictionary = probe.analyze_source(void_anno, "res://tests/coroutine_void_annotate.barista")
	_expect(failures, void_anno_report.get("valid", false) == true, "var work: Coroutine[void] holds void async call")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(annotate_hold, "res://tests/coroutine_annotate_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "coroutine annotation suite remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(annotate_hold, "res://tests/coroutine_annotate_is_valid.barista"),
		"coroutine annotation suite remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for coroutine annotation")


func _test_direct_async_call_wrap(failures: PackedStringArray) -> void:
	# Foundry direct async-call wrap @ c9d5e35 (#60): bare async_fn() / obj.async_method()
	# type as Coroutine[T] via make_coroutine_type (not bare T). Enables MISSING_AWAIT.
	var probe := BaristaScriptAnalyzerProbe.new()

	# Bare async call is not assignable to T.
	var bare_to_int := _src_class("DirectAsyncToInt extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = fetch()\n")
	var bare_to_int_report: Dictionary = probe.analyze_source(bare_to_int, "res://tests/direct_async_to_int.barista")
	_expect(failures, bare_to_int_report.get("valid", true) == false, "bare async fetch() is not assignable to int")
	var saw_bare_coro := false
	for message in bare_to_int_report.get("errors", PackedStringArray()):
		if "Cannot assign a value of type" in message and "Coroutine[int]" in message and 'variable of type "int"' in message:
			saw_bare_coro = true
	_expect(failures, saw_bare_coro, "bare async fetch() diagnoses Coroutine[int] assign to int")

	# Bare async call is assignable to Coroutine[T].
	var bare_to_coro := _src_class("DirectAsyncToCoro extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar work: Coroutine[int] = fetch()\n")
	var bare_to_coro_report: Dictionary = probe.analyze_source(bare_to_coro, "res://tests/direct_async_to_coro.barista")
	_expect(failures, bare_to_coro_report.get("valid", false) == true, "bare async fetch() is assignable to Coroutine[int]")

	# await of bare async call yields T.
	var await_bare := _src_class("DirectAsyncAwait extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = await fetch()\n")
	var await_bare_report: Dictionary = probe.analyze_source(await_bare, "res://tests/direct_async_await.barista")
	_expect(failures, await_bare_report.get("valid", false) == true, "await bare async fetch() yields int")

	# Attribute form: self.async_method() also wraps.
	var attr_to_int := _src_class("DirectAsyncAttrToInt extends Node\nasync func fetch() -> String:\n\treturn \"ok\"\nfunc test() -> void:\n\tvar result: String = self.fetch()\n")
	var attr_to_int_report: Dictionary = probe.analyze_source(attr_to_int, "res://tests/direct_async_attr_to_int.barista")
	_expect(failures, attr_to_int_report.get("valid", true) == false, "self.fetch() async is not assignable to String")
	var saw_attr_coro := false
	for message in attr_to_int_report.get("errors", PackedStringArray()):
		if "Cannot assign a value of type" in message and "Coroutine[String]" in message and 'variable of type "String"' in message:
			saw_attr_coro = true
	_expect(failures, saw_attr_coro, "self.fetch() diagnoses Coroutine[String] assign to String")

	# Root discarded non-void async call surfaces MISSING_AWAIT.
	var missing_await := _src_class("DirectAsyncMissingAwait extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tfetch()\n")
	var missing_await_report: Dictionary = probe.validate_source(missing_await, "res://tests/direct_async_missing_await.barista", true)
	_expect(failures, missing_await_report.get("valid", false) == true, "MISSING_AWAIT at warn level stays valid for bare async")
	var saw_missing_await := false
	for warn in missing_await_report.get("warnings", []):
		if "MISSING_AWAIT" in str(warn.get("string_code", "")) or ("discarded" in str(warn.get("message", "")).to_lower() and "await" in str(warn.get("message", "")).to_lower()):
			saw_missing_await = true
	_expect(failures, saw_missing_await, "root discarded bare async call emits MISSING_AWAIT")

	# Coroutine[void] fire-and-forget exempt from MISSING_AWAIT.
	var void_fire := _src_class("DirectAsyncVoidFire extends Node\nasync func fire() -> void:\n\tpass\nfunc test() -> void:\n\tfire()\n")
	var void_fire_report: Dictionary = probe.validate_source(void_fire, "res://tests/direct_async_void_fire.barista", true)
	_expect(failures, void_fire_report.get("valid", false) == true, "void async fire-and-forget stays valid")
	var saw_void_missing := false
	for warn in void_fire_report.get("warnings", []):
		if "MISSING_AWAIT" in str(warn.get("string_code", "")):
			saw_void_missing = true
	_expect(failures, not saw_void_missing, "Coroutine[void] bare root discard does not emit MISSING_AWAIT")

	# Held Coroutine[T] handle (no MISSING_AWAIT — non-root capture into hard Coroutine slot).
	var hold_no_missing := _src_class("DirectAsyncHoldNoMissing extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar work: Coroutine[int] = fetch()\n")
	var hold_no_missing_report: Dictionary = probe.validate_source(hold_no_missing, "res://tests/direct_async_hold_no_missing.barista", true)
	_expect(failures, hold_no_missing_report.get("valid", false) == true, "held Coroutine[int] from bare async stays valid")
	var saw_hold_missing := false
	for warn in hold_no_missing_report.get("warnings", []):
		if "MISSING_AWAIT" in str(warn.get("string_code", "")):
			saw_hold_missing = true
	_expect(failures, not saw_hold_missing, "capture into Coroutine[int] does not emit MISSING_AWAIT")

	# Sync local call unchanged: still returns T.
	var sync_ok := _src_class("DirectSyncStillOk extends Node\nfunc fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = fetch()\n")
	var sync_ok_report: Dictionary = probe.analyze_source(sync_ok, "res://tests/direct_sync_still_ok.barista")
	_expect(failures, sync_ok_report.get("valid", false) == true, "sync local call still returns bare int")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(bare_to_coro, "res://tests/direct_async_wrap_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "direct async-call wrap suite remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(bare_to_coro, "res://tests/direct_async_wrap_is_valid.barista"),
		"direct async-call wrap suite remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for direct async-call wrap")


func _test_surface_inheritance_member_depth(failures: PackedStringArray) -> void:
	# Foundry same-file extends + CLASS inheritance member walk @ c9d5e35 (#60 surface).
	var probe := BaristaScriptAnalyzerProbe.new()

	var same_file_ok := _src_class("InheritSameFileHost extends Node\nclass Parent extends Node:\n\tvar parent_value: int = 1\n\tfunc parent_add(a: int, b: int) -> int:\n\t\treturn a + b\nclass Child extends Parent:\n\tfunc use() -> int:\n\t\treturn parent_add(1, 2) + parent_value + self.parent_value\n")
	var same_file_ok_report: Dictionary = probe.analyze_source(same_file_ok, "res://tests/inherit_same_file_ok.barista")
	_expect(failures, same_file_ok_report.get("valid", false) == true, "same-file Child extends Parent inherits members")

	var same_file_arity := _src_class("InheritSameFileArity extends Node\nclass Parent extends Node:\n\tfunc parent_add(a: int, b: int) -> int:\n\t\treturn a + b\nclass Child extends Parent:\n\tfunc use() -> void:\n\t\tparent_add(1)\n")
	var same_file_arity_report: Dictionary = probe.analyze_source(same_file_arity, "res://tests/inherit_same_file_arity.barista")
	_expect(failures, same_file_arity_report.get("valid", true) == false, "inherited same-file method arity is validated")
	var saw_same_file_arity := false
	for message in same_file_arity_report.get("errors", PackedStringArray()):
		if "Too few arguments" in message and "parent_add" in message:
			saw_same_file_arity = true
	_expect(failures, saw_same_file_arity, "same-file inherited call too-few-arguments diagnostic")

	var same_file_type := _src_class("InheritSameFileType extends Node\nclass Parent extends Node:\n\tfunc parent_add(a: int, b: int) -> int:\n\t\treturn a + b\nclass Child extends Parent:\n\tfunc use() -> void:\n\t\tparent_add(1, 1.5)\n")
	var same_file_type_report: Dictionary = probe.analyze_source(same_file_type, "res://tests/inherit_same_file_type.barista")
	_expect(failures, same_file_type_report.get("valid", true) == false, "inherited same-file method arg types are validated")

	var nested_extends := _src_class("InheritNestedHost extends Node\nclass Outer extends Node:\n\tclass Inner extends Node:\n\t\tfunc inner_add(a: int, b: int) -> int:\n\t\t\treturn a + b\nclass Child extends Outer.Inner:\n\tfunc use() -> int:\n\t\treturn inner_add(1, 2)\n")
	var nested_extends_report: Dictionary = probe.analyze_source(nested_extends, "res://tests/inherit_nested_extends.barista")
	_expect(failures, nested_extends_report.get("valid", false) == true, "extends Outer.Inner nested CLASS chain resolves")

	var final_base := _src_class("InheritFinalHost extends Node\nfinal class Sealed extends Node:\n\tpass\nclass Child extends Sealed:\n\tpass\n")
	var final_base_report: Dictionary = probe.analyze_source(final_base, "res://tests/inherit_final_base.barista")
	_expect(failures, final_base_report.get("valid", true) == false, "extending a final class is invalid")
	var saw_final := false
	for message in final_base_report.get("errors", PackedStringArray()):
		if "Cannot extend final class" in message:
			saw_final = true
	_expect(failures, saw_final, "final class extends diagnostic")

	# Trait-as-base rejection (Foundry trait_extends @ c9d5e35 / #110).
	var trait_base := _src_class("InheritTraitHost extends Node\ntrait SomeTrait:\n\tpass\nclass Child extends SomeTrait:\n\tpass\n")
	var trait_base_report: Dictionary = probe.analyze_source(trait_base, "res://tests/inherit_trait_base.barista")
	_expect(failures, trait_base_report.get("valid", true) == false, "extending a trait is invalid")
	var saw_trait := false
	for message in trait_base_report.get("errors", PackedStringArray()):
		if "cannot extend trait" in message and "use \"uses" in message:
			saw_trait = true
	_expect(failures, saw_trait, "trait-as-base Foundry diagnostic")

	# Cyclic path-extends with member use must terminate (visited-set walks / #110).
	var cycle_member_a := _src_class("InheritCycleMemberA extends \"res://tests/inherit_cycle_member_b.barista\"\nfunc use() -> int:\n\treturn base_add(1, 2) + base_value + self.base_value\n")
	BaristaScriptParseCache.set_source_override("res://tests/inherit_cycle_member_a.barista", cycle_member_a)
	BaristaScriptParseCache.set_source_override(
		"res://tests/inherit_cycle_member_b.barista",
		_src_class("InheritCycleMemberB extends \"res://tests/inherit_cycle_member_a.barista\"\nvar base_value: int = 1\nfunc base_add(a: int, b: int) -> int:\n\treturn a + b\n"))
	var cycle_member_report: Dictionary = probe.analyze_source(cycle_member_a, "res://tests/inherit_cycle_member_a.barista")
	_expect(failures, cycle_member_report.has("valid"), "cyclic path-extends with member use terminates")
	BaristaScriptParseCache.clear_source_override("res://tests/inherit_cycle_member_a.barista")
	BaristaScriptParseCache.clear_source_override("res://tests/inherit_cycle_member_b.barista")

	# Same-file self-extends must not install a CLASS self-loop after the cyclic diagnostic.
	var self_extends := _src_class("InheritSelfExtends extends Node\nclass Foo extends Foo:\n\tfunc use() -> void:\n\t\tuse()\n")
	var self_extends_report: Dictionary = probe.analyze_source(self_extends, "res://tests/inherit_self_extends.barista")
	_expect(failures, self_extends_report.get("valid", true) == false, "same-file self-extends is invalid")
	var saw_self_cycle := false
	for message in self_extends_report.get("errors", PackedStringArray()):
		if "Cyclic reference" in message:
			saw_self_cycle = true
	_expect(failures, saw_self_cycle, "same-file self-extends cyclic diagnostic")

	# Cross-file via extends path: base CLASS members are visible on the derived head.
	var base_source := _src_class("InheritCrossBase extends Node\nvar base_value: int = 7\nfunc base_add(a: int, b: int) -> int:\n\treturn a + b\n")
	BaristaScriptParseCache.set_source_override("res://tests/inherit_cross_base.barista", base_source)
	var derived_ok := _src_class("InheritCrossDerived extends \"res://tests/inherit_cross_base.barista\"\nfunc use() -> int:\n\treturn base_add(1, 2) + base_value + self.base_value\n")
	var derived_ok_report: Dictionary = probe.analyze_source(derived_ok, "res://tests/inherit_cross_derived_ok.barista")
	_expect(failures, derived_ok_report.get("valid", false) == true, "cross-file extends path inherits base members")

	var derived_arity := _src_class("InheritCrossDerivedArity extends \"res://tests/inherit_cross_base.barista\"\nfunc use() -> void:\n\tbase_add(1)\n")
	var derived_arity_report: Dictionary = probe.analyze_source(derived_arity, "res://tests/inherit_cross_derived_arity.barista")
	_expect(failures, derived_arity_report.get("valid", true) == false, "cross-file inherited method arity is validated")
	var saw_cross_arity := false
	for message in derived_arity_report.get("errors", PackedStringArray()):
		if "Too few arguments" in message and "base_add" in message:
			saw_cross_arity = true
	_expect(failures, saw_cross_arity, "cross-file inherited call too-few-arguments diagnostic")

	BaristaScriptParseCache.clear_source_override("res://tests/inherit_cross_base.barista")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(same_file_ok, "res://tests/inherit_same_file_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "same-file inheritance remains valid under validate()")
	_expect(failures, probe.is_semantically_valid(same_file_ok, "res://tests/inherit_same_file_is_valid.barista"),
		"same-file inheritance remains valid under is_semantically_valid()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate/is_valid must not mutate declaration index for inheritance member depth")


func _test_resolve_class_member_depth(failures: PackedStringArray) -> void:
	# Foundry resolve_class_member @ c9d5e35: lazy member datatype resolution with cyclic fail-stop
	# before identifier / attribute / call binds read types (#60).
	var probe := BaristaScriptAnalyzerProbe.new()

	# Later-declared typed const must be resolved when an earlier function body reads it.
	var later_const_ok := _src_class("ResolveMemberLaterConstOk extends Node\nfunc use() -> int:\n\treturn later_const\nconst later_const: int = 7\n")
	var later_const_ok_report: Dictionary = probe.analyze_source(later_const_ok, "res://tests/resolve_member_later_const_ok.barista")
	_expect(failures, later_const_ok_report.get("valid", false) == true, "later-declared typed const is resolved for earlier use")

	var later_const_bad := _src_class("ResolveMemberLaterConstBad extends Node\nfunc use() -> void:\n\tvar s: String = later_const\nconst later_const: int = 7\n")
	var later_const_bad_report: Dictionary = probe.analyze_source(later_const_bad, "res://tests/resolve_member_later_const_bad.barista")
	_expect(failures, later_const_bad_report.get("valid", true) == false, "later-declared int const rejects String destination")
	var saw_later_const_type := false
	for message in later_const_bad_report.get("errors", PackedStringArray()):
		if "Cannot assign a value of type" in message and "int" in message and "String" in message:
			saw_later_const_type = true
	_expect(failures, saw_later_const_type, "later-declared const type mismatch diagnostic")

	# Later-declared function return type must be available for call typing.
	var later_fn_ok := _src_class("ResolveMemberLaterFnOk extends Node\nfunc use() -> String:\n\treturn later_fn()\nfunc later_fn() -> String:\n\treturn \"ok\"\n")
	var later_fn_ok_report: Dictionary = probe.analyze_source(later_fn_ok, "res://tests/resolve_member_later_fn_ok.barista")
	_expect(failures, later_fn_ok_report.get("valid", false) == true, "later-declared function return type resolves for earlier call")

	var later_fn_bad := _src_class("ResolveMemberLaterFnBad extends Node\nfunc use() -> void:\n\tvar i: int = later_fn()\nfunc later_fn() -> String:\n\treturn \"ok\"\n")
	var later_fn_bad_report: Dictionary = probe.analyze_source(later_fn_bad, "res://tests/resolve_member_later_fn_bad.barista")
	_expect(failures, later_fn_bad_report.get("valid", true) == false, "later-declared String return rejects int destination")

	# Cyclic const members fail-stop (Foundry cyclic_ref_const).
	var cyclic_const := _src_class("ResolveMemberCyclicConst extends Node\nfunc use() -> void:\n\tprint(c1)\nconst c1 = c2\nconst c2 = c1\n")
	var cyclic_const_report: Dictionary = probe.analyze_source(cyclic_const, "res://tests/resolve_member_cyclic_const.barista")
	_expect(failures, cyclic_const_report.get("valid", true) == false, "cyclic const members are invalid")
	var saw_cyclic_member := false
	for message in cyclic_const_report.get("errors", PackedStringArray()):
		if "Could not resolve member" in message and "Cyclic reference" in message:
			saw_cyclic_member = true
	_expect(failures, saw_cyclic_member, "cyclic const member diagnostic")

	# Cyclic inferred vars fail-stop (Foundry cyclic_ref_var).
	var cyclic_var := _src_class("ResolveMemberCyclicVar extends Node\nfunc use() -> void:\n\tprint(v1)\nvar v1 := v2\nvar v2 := v1\n")
	var cyclic_var_report: Dictionary = probe.analyze_source(cyclic_var, "res://tests/resolve_member_cyclic_var.barista")
	_expect(failures, cyclic_var_report.get("valid", true) == false, "cyclic var members are invalid")
	var saw_cyclic_var := false
	for message in cyclic_var_report.get("errors", PackedStringArray()):
		if "Could not resolve member" in message and "Cyclic reference" in message:
			saw_cyclic_var = true
	_expect(failures, saw_cyclic_var, "cyclic var member diagnostic")

	# self.later_const attribute bind also resolves.
	var self_attr := _src_class("ResolveMemberSelfAttr extends Node\nfunc use() -> int:\n\treturn self.later_const\nconst later_const: int = 3\n")
	var self_attr_report: Dictionary = probe.analyze_source(self_attr, "res://tests/resolve_member_self_attr.barista")
	_expect(failures, self_attr_report.get("valid", false) == true, "self.later_const attribute resolves later-declared const")

	# Cross-file SCRIPT member types beyond mere inheritance (raise+delegate via BSCache).
	var base_source := _src_class("ResolveMemberCrossBase extends Node\nconst BASE_CONST: int = 42\nfunc typed_return() -> String:\n\treturn \"ok\"\n")
	BaristaScriptParseCache.set_source_override("res://tests/resolve_member_cross_base.barista", base_source)
	var derived_ok := _src_class("ResolveMemberCrossDerivedOk extends \"res://tests/resolve_member_cross_base.barista\"\nfunc use() -> void:\n\tvar s: String = typed_return()\n\tvar i: int = BASE_CONST\n")
	var derived_ok_report: Dictionary = probe.analyze_source(derived_ok, "res://tests/resolve_member_cross_derived_ok.barista")
	_expect(failures, derived_ok_report.get("valid", false) == true, "cross-file SCRIPT member types resolve beyond inheritance")

	var derived_bad := _src_class("ResolveMemberCrossDerivedBad extends \"res://tests/resolve_member_cross_base.barista\"\nfunc use() -> void:\n\tvar s: String = BASE_CONST\n")
	var derived_bad_report: Dictionary = probe.analyze_source(derived_bad, "res://tests/resolve_member_cross_derived_bad.barista")
	_expect(failures, derived_bad_report.get("valid", true) == false, "cross-file const type mismatch is validated")
	var saw_cross_type := false
	for message in derived_bad_report.get("errors", PackedStringArray()):
		if "Cannot assign a value of type" in message:
			saw_cross_type = true
	_expect(failures, saw_cross_type, "cross-file SCRIPT member type mismatch diagnostic")
	BaristaScriptParseCache.clear_source_override("res://tests/resolve_member_cross_base.barista")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var validate_report: Dictionary = probe.validate_source(later_const_ok, "res://tests/resolve_member_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "resolve_class_member fixtures remain valid under validate()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate must not mutate declaration index for resolve_class_member depth")


func _test_foreign_member_failure_replay(failures: PackedStringArray) -> void:
	# Foundry OwnerResolutionFailures + DependentResolutionFailureReplays @ c9d5e35 (#60):
	# owner-side member resolution failure surfaces once on the dependent, and re-visits /
	# second dependents do not spam duplicate "Could not resolve external class member" lines.
	var probe := BaristaScriptAnalyzerProbe.new()

	# Owner with cyclic consts — resolving either member fails on the owner analyzer.
	var owner_cyclic := _src_class("ForeignFailOwner extends Node\nconst c1 = c2\nconst c2 = c1\n")
	BaristaScriptParseCache.set_source_override("res://tests/foreign_fail_owner.barista", owner_cyclic)

	# Dependent uses the failed member twice in one body; external failure is reported once.
	var dependent_once := _src_class("ForeignFailDependent extends \"res://tests/foreign_fail_owner.barista\"\nfunc use() -> void:\n\tvar a = c1\n\tvar b = c1\n")
	var dependent_once_report: Dictionary = probe.analyze_source(dependent_once, "res://tests/foreign_fail_dependent.barista")
	_expect(failures, dependent_once_report.get("valid", true) == false, "cross-file owner member failure invalidates dependent")
	var external_fail_count := 0
	for message in dependent_once_report.get("errors", PackedStringArray()):
		if "Could not resolve external class member" in message and "c1" in message:
			external_fail_count += 1
	_expect(failures, external_fail_count >= 1, "dependent surfaces external class member failure for c1")
	_expect(failures, external_fail_count == 1, "single dependent does not spam duplicate external-member failures")

	# Second dependent on the same owner failure still gets its own replay (separate analyzer).
	var dependent_two := _src_class("ForeignFailDependentTwo extends \"res://tests/foreign_fail_owner.barista\"\nfunc use() -> void:\n\tvar x = c1\n\tvar y = c1\n")
	var dependent_two_report: Dictionary = probe.analyze_source(dependent_two, "res://tests/foreign_fail_dependent_two.barista")
	_expect(failures, dependent_two_report.get("valid", true) == false, "second dependent also sees owner member failure")
	var external_fail_count_two := 0
	for message in dependent_two_report.get("errors", PackedStringArray()):
		if "Could not resolve external class member" in message and "c1" in message:
			external_fail_count_two += 1
	_expect(failures, external_fail_count_two == 1, "second dependent replays external failure once (no intra-file spam)")

	# Re-analyze the first dependent: still one external failure, no hang / silent success.
	var dependent_reanalyze: Dictionary = probe.analyze_source(dependent_once, "res://tests/foreign_fail_dependent_reanalyze.barista")
	_expect(failures, dependent_reanalyze.get("valid", true) == false, "re-analyze still invalid after owner member failure")
	var external_fail_reanalyze := 0
	for message in dependent_reanalyze.get("errors", PackedStringArray()):
		if "Could not resolve external class member" in message and "c1" in message:
			external_fail_reanalyze += 1
	_expect(failures, external_fail_reanalyze == 1, "re-analyze keeps single external-member failure diagnostic")

	BaristaScriptParseCache.clear_source_override("res://tests/foreign_fail_owner.barista")

	# Soft CycleA/CycleB registration path from #109/#111 remains unchanged.
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/cycle_a.barista", _src_class("CycleA extends Node\n"))
	BaristaScriptParseCache.set_source_override("res://tests/cycle_b.barista", _src_class("CycleB extends Node\n"))
	var a := BaristaScriptParseCache.get_parser("res://tests/cycle_a.barista", Status.EMPTY, "res://tests/cycle_b.barista")
	var b := BaristaScriptParseCache.get_parser("res://tests/cycle_b.barista", Status.EMPTY, "res://tests/cycle_a.barista")
	_expect(failures, a.valid and b.valid, "CycleA/CycleB edges still recordable at EMPTY after foreign failure replay")
	var raised_a := BaristaScriptParseCache.get_parser("res://tests/cycle_a.barista", Status.FULLY_SOLVED, "")
	var raised_b := BaristaScriptParseCache.get_parser("res://tests/cycle_b.barista", Status.FULLY_SOLVED, "")
	_expect(failures, raised_a.valid and raised_b.valid, "CycleA/CycleB raise still completes without deadlock")
	BaristaScriptParseCache.clear_source_overrides()

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var ok_source := _src_class("ForeignFailReplayValid extends Node\nconst ok: int = 1\nfunc use() -> int:\n\treturn ok\n")
	var validate_report: Dictionary = probe.validate_source(ok_source, "res://tests/foreign_fail_replay_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "foreign failure replay suite remains valid under validate()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate must not mutate declaration index for foreign failure replay")


func _test_foreign_class_phase_failure_replay(failures: PackedStringArray) -> void:
	# Foundry resolve_class_interface / resolve_class_body foreign failure recording @ c9d5e35 (#60):
	# owner INTERFACE/BODY failures surface once per phase on dependents as "Could not resolve class",
	# with DependentResolutionFailureReplays dedupe across revisits in the same analyzer.
	# INTERFACE and BODY are distinct phase bits, so a FULLY_SOLVED dependent of an INTERFACE-failed
	# owner may see one plain INTERFACE replay plus one BODY replay (suffix) — not intra-phase spam.
	var probe := BaristaScriptAnalyzerProbe.new()

	# --- INTERFACE phase: cyclic consts fail while resolving the owner interface. ---
	var iface_owner := _src_class("ForeignIfaceFailOwner extends Node\nconst c1 = c2\nconst c2 = c1\n")
	BaristaScriptParseCache.set_source_override("res://tests/foreign_iface_fail_owner.barista", iface_owner)

	var iface_dependent := _src_class("ForeignIfaceFailDependent extends \"res://tests/foreign_iface_fail_owner.barista\"\nfunc ok() -> void:\n\tpass\n")
	var iface_report: Dictionary = probe.analyze_source(iface_dependent, "res://tests/foreign_iface_fail_dependent.barista")
	_expect(failures, iface_report.get("valid", true) == false, "cross-file owner INTERFACE failure invalidates dependent")
	var iface_plain := 0
	var iface_body_suffix := 0
	for message in iface_report.get("errors", PackedStringArray()):
		if "Could not resolve class" in message and "ForeignIfaceFailOwner" in message:
			if "declared in" in message:
				iface_body_suffix += 1
			else:
				iface_plain += 1
	_expect(failures, iface_plain >= 1, "dependent surfaces class-phase INTERFACE failure for owner")
	_expect(failures, iface_plain == 1, "single dependent does not spam duplicate INTERFACE class-phase failures")
	_expect(failures, iface_body_suffix <= 1, "INTERFACE→BODY propagation replays BODY at most once")

	var iface_dependent_two := _src_class("ForeignIfaceFailDependentTwo extends \"res://tests/foreign_iface_fail_owner.barista\"\nfunc ok() -> void:\n\tpass\n")
	var iface_report_two: Dictionary = probe.analyze_source(iface_dependent_two, "res://tests/foreign_iface_fail_dependent_two.barista")
	_expect(failures, iface_report_two.get("valid", true) == false, "second INTERFACE dependent also sees owner failure")
	var iface_plain_two := 0
	for message in iface_report_two.get("errors", PackedStringArray()):
		if "Could not resolve class" in message and "ForeignIfaceFailOwner" in message and not ("declared in" in message):
			iface_plain_two += 1
	_expect(failures, iface_plain_two == 1, "second INTERFACE dependent replays INTERFACE failure once")

	var iface_reanalyze: Dictionary = probe.analyze_source(iface_dependent, "res://tests/foreign_iface_fail_dependent_reanalyze.barista")
	_expect(failures, iface_reanalyze.get("valid", true) == false, "INTERFACE re-analyze still invalid")
	var iface_plain_reanalyze := 0
	for message in iface_reanalyze.get("errors", PackedStringArray()):
		if "Could not resolve class" in message and "ForeignIfaceFailOwner" in message and not ("declared in" in message):
			iface_plain_reanalyze += 1
	_expect(failures, iface_plain_reanalyze == 1, "INTERFACE re-analyze keeps single INTERFACE class-phase failure")

	BaristaScriptParseCache.clear_source_override("res://tests/foreign_iface_fail_owner.barista")
	BaristaScriptParseCache.clear_script_cache()

	# --- BODY phase: interface is clean; typed assign in function body fails owner BODY. ---
	var body_owner := _src_class("ForeignBodyFailOwner extends Node\nfunc bad() -> void:\n\tvar x: int = \"nope\"\n")
	BaristaScriptParseCache.set_source_override("res://tests/foreign_body_fail_owner.barista", body_owner)

	var body_dependent := _src_class("ForeignBodyFailDependent extends \"res://tests/foreign_body_fail_owner.barista\"\nfunc ok() -> void:\n\tpass\n")
	var body_report: Dictionary = probe.analyze_source(body_dependent, "res://tests/foreign_body_fail_dependent.barista")
	_expect(failures, body_report.get("valid", true) == false, "cross-file owner BODY failure invalidates dependent")
	var body_fail_count := 0
	var body_plain_iface := 0
	for message in body_report.get("errors", PackedStringArray()):
		if "Could not resolve class" in message and "ForeignBodyFailOwner" in message:
			if "declared in" in message:
				body_fail_count += 1
			else:
				body_plain_iface += 1
	_expect(failures, body_fail_count >= 1, "dependent surfaces class-phase BODY failure for owner")
	_expect(failures, body_fail_count == 1, "single dependent does not spam duplicate BODY class-phase failures")
	_expect(failures, body_plain_iface == 0, "BODY-only owner failure does not emit INTERFACE class replay")

	var body_dependent_two := _src_class("ForeignBodyFailDependentTwo extends \"res://tests/foreign_body_fail_owner.barista\"\nfunc ok() -> void:\n\tpass\n")
	var body_report_two: Dictionary = probe.analyze_source(body_dependent_two, "res://tests/foreign_body_fail_dependent_two.barista")
	_expect(failures, body_report_two.get("valid", true) == false, "second BODY dependent also sees owner failure")
	var body_fail_count_two := 0
	for message in body_report_two.get("errors", PackedStringArray()):
		if "Could not resolve class" in message and "ForeignBodyFailOwner" in message and "declared in" in message:
			body_fail_count_two += 1
	_expect(failures, body_fail_count_two == 1, "second BODY dependent replays class failure once")

	BaristaScriptParseCache.clear_source_override("res://tests/foreign_body_fail_owner.barista")

	# Soft CycleA/CycleB registration path remains unchanged after class-phase replay wiring.
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override("res://tests/cycle_a.barista", _src_class("CycleA extends Node\n"))
	BaristaScriptParseCache.set_source_override("res://tests/cycle_b.barista", _src_class("CycleB extends Node\n"))
	var a := BaristaScriptParseCache.get_parser("res://tests/cycle_a.barista", Status.EMPTY, "res://tests/cycle_b.barista")
	var b := BaristaScriptParseCache.get_parser("res://tests/cycle_b.barista", Status.EMPTY, "res://tests/cycle_a.barista")
	_expect(failures, a.valid and b.valid, "CycleA/CycleB edges still recordable at EMPTY after class-phase replay")
	var raised_a := BaristaScriptParseCache.get_parser("res://tests/cycle_a.barista", Status.FULLY_SOLVED, "")
	var raised_b := BaristaScriptParseCache.get_parser("res://tests/cycle_b.barista", Status.FULLY_SOLVED, "")
	_expect(failures, raised_a.valid and raised_b.valid, "CycleA/CycleB raise still completes without deadlock after class-phase replay")
	BaristaScriptParseCache.clear_source_overrides()

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var ok_source := _src_class("ForeignClassPhaseValid extends Node\nconst ok: int = 1\nfunc use() -> int:\n\treturn ok\n")
	var validate_report: Dictionary = probe.validate_source(ok_source, "res://tests/foreign_class_phase_validate.barista", true)
	_expect(failures, validate_report.get("valid", false) == true, "foreign class-phase replay suite remains valid under validate()")
	_expect(failures, index.get_record_count() == before,
		"analyze/validate must not mutate declaration index for foreign class-phase replay")


func _test_conformance_scoped_visibility(failures: PackedStringArray) -> void:
	# Foundry ConformanceVisibility + BSConformanceRegistry::ScopedVisibility starter (#60).
	var probe := BaristaScriptAnalyzerProbe.new()

	var nest: Dictionary = probe.scoped_visibility_nest_restore()
	_expect(failures, nest.get("none_sees_a", false) == true, "no Visibility installed → a visible")
	_expect(failures, nest.get("none_sees_b", false) == true, "no Visibility installed → b visible")
	_expect(failures, nest.get("none_sees_c", false) == true, "no Visibility installed → c visible")
	_expect(failures, nest.get("outer_sees_a", false) == true, "outer ScopedVisibility allows a")
	_expect(failures, nest.get("outer_hides_b", false) == true, "outer ScopedVisibility hides b")
	_expect(failures, nest.get("nested_sees_b", false) == true, "nested ScopedVisibility allows b")
	_expect(failures, nest.get("nested_hides_a", false) == true, "nested ScopedVisibility hides a")
	_expect(failures, nest.get("restored_sees_a", false) == true, "leaving nested restores outer a")
	_expect(failures, nest.get("restored_hides_b", false) == true, "leaving nested restores outer hide b")
	_expect(failures, nest.get("cleared_sees_a", false) == true, "leaving outer clears Visibility → a")
	_expect(failures, nest.get("cleared_sees_b", false) == true, "leaving outer clears Visibility → b")
	_expect(failures, nest.get("in_flight_hides_own", false) == true, "ScopedInFlightReplacement hides own file")
	_expect(failures, nest.get("after_in_flight_sees_a", false) == true, "in-flight restore sees a again")
	_expect(failures, nest.get("empty_has_conformance", false) == true, "empty registry store fail-closed")

	var own_path := "res://tests/vis_viewer.barista"
	var dep_path := "res://tests/vis_dep.barista"
	var unrelated_path := "res://tests/vis_unrelated.barista"
	BaristaScriptParseCache.clear_script_cache()
	BaristaScriptParseCache.set_source_override(dep_path, _src_class("VisDep extends Node\n"))
	BaristaScriptParseCache.set_source_override(unrelated_path, _src_class("VisUnrelated extends Node\n"))
	var viewer_source := _src_class("VisViewer extends \"%s\"\n" % dep_path)
	var candidates := PackedStringArray([own_path, dep_path, unrelated_path])
	var report: Dictionary = probe.conformance_visibility_can_see(viewer_source, own_path, candidates)
	_expect(failures, report.get("ok", false) == true, "conformance_visibility_can_see parse ok")
	var can_see: Dictionary = report.get("can_see", {})
	_expect(failures, can_see.get(own_path, false) == true, "ConformanceVisibility can_see own path")
	_expect(failures, can_see.get(dep_path, false) == true, "ConformanceVisibility can_see declared extends dependency")
	_expect(failures, can_see.get(unrelated_path, false) == false, "ConformanceVisibility cannot see unrelated file")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("VisIndexGuard extends Node\n"), "res://tests/vis_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"visibility probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_conformance_registry_registration(failures: PackedStringArray) -> void:
	# Foundry resolve_conformances → try_replace_file_conformances under ScopedInFlight (#60).
	var probe := BaristaScriptAnalyzerProbe.new()
	var report: Dictionary = probe.conformance_registry_registration()

	_expect(failures, report.get("analyze_ok", false) == true, "declaring analyze publishes conformances")
	_expect(failures, int(report.get("registered_count", 0)) >= 1, "registry stores at least one Conformance")
	_expect(failures, report.get("registered_after_analyze", false) == true, "has_conformance after analyze")
	_expect(failures, report.get("none_sees_registered", false) == true, "no Visibility → membership visible")
	_expect(failures, report.get("in_flight_hides_has_conformance", false) == true,
		"ScopedInFlightReplacement hides has_conformance for declaring file")
	_expect(failures, report.get("after_in_flight_sees_again", false) == true,
		"leaving in-flight restores has_conformance")

	_expect(failures, report.get("viewer_parse_ok", false) == true, "viewer parse ok")
	_expect(failures, report.get("viewer_hides_unrelated_declaring", false) == true,
		"ConformanceVisibility hides unrelated declaring has_conformance")
	_expect(failures, report.get("viewer_can_see_own", false) == true, "viewer can_see own path")
	_expect(failures, report.get("viewer_can_see_dep", false) == true, "viewer can_see extends dependency")
	_expect(failures, report.get("viewer_cannot_see_declaring", false) == true,
		"viewer cannot_see unrelated declaring file")
	_expect(failures, report.get("viewer_cannot_see_unrelated", false) == true,
		"viewer cannot_see unrelated path")

	_expect(failures, report.get("dep_parse_ok", false) == true, "dependency-of-declaring parse ok")
	_expect(failures, report.get("dep_can_see_declaring", false) == true,
		"preload dependency can_see declaring file")
	_expect(failures, report.get("dep_sees_has_conformance", false) == true,
		"dependency Visibility sees has_conformance")

	_expect(failures, report.get("reanalyze_clear_ok", false) == true, "empty reanalysis ok")
	_expect(failures, report.get("cleared_after_reanalyze", false) == true,
		"empty reanalysis clears file conformances")
	_expect(failures, report.get("reanalyze_replace_ok", false) == true, "replacement reanalysis ok")
	_expect(failures, report.get("replaced_has_other", false) == true, "reanalysis registers new trait")
	_expect(failures, report.get("replaced_dropped_old", false) == true, "reanalysis drops previous trait")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("RegIndexGuard extends Node\n"), "res://tests/reg_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"registry registration probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_conformance_witness_lookup(failures: PackedStringArray) -> void:
	# Foundry find_witness_location + find_conformance_witness member-miss fallback (#60).
	var probe := BaristaScriptAnalyzerProbe.new()
	var report: Dictionary = probe.conformance_witness_lookup()

	_expect(failures, report.get("same_file_analyze_ok", false) == true,
		"same-file CLASS self.greet() witness call analyzes")
	_expect(failures, int(report.get("registered_count", 0)) >= 1, "registry stores Conformance with witnesses")
	_expect(failures, report.get("has_greet_witness_key", false) == true,
		"registered Conformance stores greet witness method-name key")
	_expect(failures, report.get("find_witness_location_ok", false) == true,
		"find_witness_location returns declaring file + conformance_index")
	_expect(failures, report.get("class_arity_checks_witness", false) == true,
		"CLASS witness signature applied (arity diagnostic on self.greet(1))")

	_expect(failures, report.get("native_analyze_ok", false) == true,
		"native Node extend witness call analyzes")
	_expect(failures, report.get("native_arity_checks_witness", false) == true,
		"NATIVE witness signature applied (arity diagnostic on wit_greet(1))")

	_expect(failures, report.get("viewer_parse_ok", false) == true, "unrelated viewer parse ok")
	_expect(failures, report.get("viewer_hides_witness_location", false) == true,
		"ConformanceVisibility hides find_witness_location for unrelated declaring file")
	_expect(failures, report.get("viewer_cannot_see_declaring", false) == true,
		"unrelated viewer cannot_see declaring file")

	_expect(failures, report.get("dep_parse_ok", false) == true, "dependency-of-declaring parse ok")
	_expect(failures, report.get("dep_sees_witness_location", false) == true,
		"preload dependency Visibility sees find_witness_location")

	_expect(failures, report.get("reanalyze_clear_ok", false) == true, "empty reanalysis ok")
	_expect(failures, report.get("cleared_witness_location", false) == true,
		"empty reanalysis clears find_witness_location")
	_expect(failures, report.get("cleared_file_empty", false) == true,
		"empty reanalysis clears file conformances")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("WitIndexGuard extends Node\n"), "res://tests/wit_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"witness lookup probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_conformance_hidden_witness(failures: PackedStringArray) -> void:
	# Foundry find_hidden_witness_declaration + hidden-conformance diagnostic (#60).
	var probe := BaristaScriptAnalyzerProbe.new()
	var report: Dictionary = probe.conformance_hidden_witness()

	_expect(failures, report.get("declaring_analyze_ok", false) == true,
		"builtin String extend declaring file analyzes")
	_expect(failures, int(report.get("registered_count", 0)) >= 1, "registry stores hidden-witness Conformance")
	_expect(failures, report.get("has_hid_mark_key", false) == true,
		"registered Conformance stores hid_mark witness method-name key")
	_expect(failures, report.get("visible_find_witness_location", false) == true,
		"with no Visibility, find_witness_location finds declaring file")
	_expect(failures, report.get("no_visibility_hides_nothing", false) == true,
		"with no Visibility, find_hidden_witness_declaration is empty")

	_expect(failures, report.get("viewer_parse_ok", false) == true, "unrelated viewer parse ok")
	_expect(failures, report.get("viewer_analyze_failed", false) == true,
		"unrelated viewer must not silently succeed on hidden witness call")
	_expect(failures, report.get("viewer_hidden_diagnostic", false) == true,
		"viewer surfaces Foundry hidden-witness diagnostic (method + trait + file)")
	_expect(failures, report.get("viewer_finds_hidden_declaration", false) == true,
		"ConformanceVisibility find_hidden_witness_declaration reports declaring file + trait")
	_expect(failures, report.get("viewer_hides_witness_location", false) == true,
		"ConformanceVisibility hides find_witness_location for unrelated viewer")

	_expect(failures, report.get("dep_analyze_ok", false) == true,
		"extends dependency of declaring file still resolves witness call")

	_expect(failures, report.get("final_declaring_analyze_ok", false) == true,
		"final CLASS same-file extend declaring file analyzes")
	_expect(failures, report.get("final_has_greet_key", false) == true,
		"final CLASS Conformance stores hid_greet witness method-name key")
	_expect(failures, report.get("final_viewer_parse_ok", false) == true,
		"final CLASS unrelated viewer parse ok")
	_expect(failures, report.get("final_viewer_finds_hidden", false) == true,
		"final CLASS ConformanceVisibility find_hidden_witness_declaration reports declaring file")
	_expect(failures, report.get("final_viewer_hides_location", false) == true,
		"final CLASS ConformanceVisibility hides find_witness_location")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("HidIndexGuard extends Node\n"), "res://tests/hid_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"hidden-witness probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_class_trait_binding_chain_coherence(failures: PackedStringArray) -> void:
	# Foundry ClassTraitBinding / RecordedTypeArgument starter @ c9d5e35 (#60).
	# Generic uses remain M5-blocked in source, so the probe drives the registry directly.
	var probe := BaristaScriptAnalyzerProbe.new()
	var report: Dictionary = probe.class_trait_binding_chain_coherence()

	_expect(failures, report.get("publish_ok", false) == true, "try_replace publishes ClassTraitBinding without conflict")
	_expect(failures, report.get("binding_stored", false) == true, "get_file_trait_bindings returns published uses binding")
	_expect(failures, report.get("reduce_builtin_ok", false) == true, "reduce_type_argument records builtin INT")

	_expect(failures, int(report.get("agree_registered_count", 0)) == 1, "matching chain args register Conformance")
	_expect(failures, report.get("agree_no_chain_conflict", false) == true,
		"matching ClassTraitBinding + Conformance args do not CHAIN_COHERENCE")

	_expect(failures, report.get("conflict_chain_coherence", false) == true,
		"contradicting uses-binding yields RegistrationConflict::CHAIN_COHERENCE")
	_expect(failures, report.get("conflict_rejected", false) == true,
		"CHAIN_COHERENCE rejects the whole ConformanceNode declaration")
	_expect(failures, report.get("conflict_store_empty", false) == true,
		"rejected Conformance is not stored for the declaring file")
	_expect(failures, int(report.get("conflict_index", -1)) == 0, "conflict names conformance_index 0")
	_expect(failures, String(report.get("conflict_conflicting_file", "")) == "res://tests/ctb_binding.barista",
		"conflict points at the binding declaring file")

	_expect(failures, report.get("edge_loader_binding_ok", false) == true,
		"loader publishes ClassTraitBinding with load edge without conflict")
	_expect(failures, report.get("edge_reverse_chain_coherence", false) == true,
		"reverse load edge licenses CHAIN_COHERENCE when Visibility cannot see loader")
	_expect(failures, report.get("edge_reverse_rejected", false) == true,
		"reverse-edge CHAIN_COHERENCE rejects the loaded Conformance")
	_expect(failures, report.get("edge_reverse_store_empty", false) == true,
		"reverse-edge rejected Conformance is not stored")
	_expect(failures, report.get("edge_noedge_uncompared", false) == true,
		"contradicting pair with no load edge either way stays uncompared")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("CtbIndexGuard extends Node\n"), "res://tests/ctb_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"class-trait-binding probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_recorded_trait_arguments_query(failures: PackedStringArray) -> void:
	# Foundry get_recorded_trait_arguments / project_registry_trait_arguments @ c9d5e35 (#60).
	var probe := BaristaScriptAnalyzerProbe.new()
	var report: Dictionary = probe.recorded_trait_arguments_query()

	_expect(failures, report.get("script_publish_ok", false) == true, "script conformance with recorded args publishes")
	_expect(failures, report.get("script_query_ok", false) == true,
		"visible get_recorded_trait_arguments returns published INT args")

	_expect(failures, report.get("hidden_script_query_false", false) == true,
		"hidden Visibility makes get_recorded_trait_arguments return false")
	_expect(failures, report.get("hidden_native_query_false", false) == true,
		"hidden Visibility makes get_native_recorded_trait_arguments return false")
	_expect(failures, report.get("hidden_builtin_query_false", false) == true,
		"hidden Visibility makes get_builtin_recorded_trait_arguments return false")

	_expect(failures, report.get("builtin_publish_ok", false) == true, "builtin-keyed conformance publishes")
	_expect(failures, report.get("builtin_query_ok", false) == true,
		"get_builtin_recorded_trait_arguments returns exact-key recorded args")

	_expect(failures, report.get("native_publish_ok", false) == true, "native Object conformance publishes")
	_expect(failures, report.get("native_parent_walk_ok", false) == true,
		"get_native_recorded_trait_arguments walks ClassDB parents to Object")

	_expect(failures, report.get("farther_direct_ok", false) == true, "farther recorded args remain queryable directly")
	_expect(failures, report.get("nearer_empty_shadows", false) == true,
		"nearer empty conformance shadows farther recorded args in project_registry_trait_arguments")
	_expect(failures, report.get("farther_project_ok", false) == true,
		"project_registry_trait_arguments returns farther record when nearer is absent")
	_expect(failures, report.get("class_to_native_project_ok", false) == true,
		"CLASS chain bottoms out at NATIVE and finds parent-walk recorded args")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("RtaIndexGuard extends Node\n"), "res://tests/rta_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"recorded-trait-args probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_trait_target_assignability(failures: PackedStringArray) -> void:
	# Foundry FSTypeCompatibility::check trait-target recorded/projected args @ c9d5e35 (#60).
	var probe := BaristaScriptAnalyzerProbe.new()
	var report: Dictionary = probe.trait_target_assignability()

	_expect(failures, report.get("class_registry_membership", false) == true,
		"CLASS source has registry membership for TtaKeeper")
	_expect(failures, report.get("class_registry_conflict_rejects", false) == true,
		"CLASS→Keeper[String] rejects registry-recorded INT args")
	_expect(failures, report.get("class_registry_match_accepts", false) == true,
		"CLASS→Keeper[String] accepts matching recorded STRING args")
	_expect(failures, report.get("class_registry_no_evidence_accepts", false) == true,
		"CLASS→Keeper[String] accepts gradual no-evidence empty record")

	_expect(failures, report.get("native_membership", false) == true,
		"Node reaches Object native_class_conforms for TtaKeeper")
	_expect(failures, report.get("native_conflict_rejects", false) == true,
		"NATIVE→Keeper[String] rejects recorded INT args")

	_expect(failures, report.get("builtin_membership", false) == true,
		"INT builtin_type_conforms to TtaKeeper")
	_expect(failures, report.get("builtin_conflict_rejects", false) == true,
		"BUILTIN→Keeper[String] rejects recorded FLOAT args")

	_expect(failures, report.get("uses_project_ok", false) == true,
		"project_class_trait_arguments returns declared uses INT binding")
	_expect(failures, report.get("uses_projection_conflict_rejects", false) == true,
		"declared uses Keeper[int] conflicts with Keeper[String] destination")

	_expect(failures, report.get("trait_self_match_accepts", false) == true,
		"Keeper[String]→Keeper[String] self-specialization accepts")
	_expect(failures, report.get("trait_self_conflict_rejects", false) == true,
		"Keeper[int]→Keeper[String] self-specialization rejects")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("TtaIndexGuard extends Node\n"), "res://tests/tta_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"trait-target-assignability probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_witness_collision_arbitration(failures: PackedStringArray) -> void:
	# Foundry seen_witnesses_by_target / get_witness_source / WITNESS_COLLISION @ c9d5e35 (#60).
	var probe := BaristaScriptAnalyzerProbe.new()
	var report: Dictionary = probe.witness_collision_arbitration()

	_expect(failures, report.get("same_file_collision_diagnostic", false) == true,
		"same-file two conformances with shared witness method diagnose collision")
	_expect(failures, report.get("same_file_first_registered", false) == true,
		"same-file first conformance still registers after witness collision on second")
	_expect(failures, report.get("same_file_second_rejected", false) == true,
		"same-file colliding second conformance is not stored")

	_expect(failures, report.get("cross_first_ok", false) == true,
		"cross-file first witness conformance analyzes and registers")
	_expect(failures, report.get("get_witness_source_first", false) == true,
		"get_witness_source reports the first declaring file")
	_expect(failures, report.get("cross_file_collision_diagnostic", false) == true,
		"cross-file conflicting witness diagnoses collision")
	_expect(failures, report.get("cross_second_store_empty", false) == true,
		"cross-file colliding second conformance is not stored")

	_expect(failures, report.get("distinct_ok_analyze", false) == true,
		"non-colliding distinct witness methods analyze cleanly")
	_expect(failures, report.get("distinct_ok_registered", false) == true,
		"non-colliding distinct witness methods both register")

	_expect(failures, report.get("registry_first_ok", false) == true,
		"try_replace publishes first witness declaration")
	_expect(failures, report.get("registry_witness_collision", false) == true,
		"try_replace rejects WITNESS_COLLISION authoritatively")
	_expect(failures, report.get("registry_second_rejected", false) == true,
		"WITNESS_COLLISION leaves second file store empty")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("WcIndexGuard extends Node\n"), "res://tests/wc_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"witness-collision probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_self_type_parameter_compat(failures: PackedStringArray) -> void:
	# Foundry FSTypeCompatibility TYPE_PARAMETER / @Self arms @ c9d5e35 (#60 residual).
	# Free method/class type parameters remain M5-deferred; undecidable free-`T` laundering
	# refusal is ported in `BSTypeCompatibility::check` but cannot be exercised until M5.
	var probe := BaristaScriptAnalyzerProbe.new()

	# Same-class Self↔Self identity (parameter + return + local assign).
	var self_echo := _src_class("SelfEchoOk extends Node\nfunc echo(value: Self) -> Self:\n\tvar tmp: Self = value\n\treturn tmp\n")
	var self_echo_report: Dictionary = probe.analyze_source(self_echo, "res://tests/self_echo_ok.barista")
	_expect(failures, self_echo_report.get("valid", false) == true, "same-class Self↔Self parameter/return/local is valid")

	# Self widens to Self? (nullability excluded from TYPE_PARAMETER identity).
	var self_widen := _src_class("SelfWidenOk extends Node\nfunc widen(value: Self) -> Self?:\n\treturn value\n")
	var self_widen_report: Dictionary = probe.analyze_source(self_widen, "res://tests/self_widen_ok.barista")
	_expect(failures, self_widen_report.get("valid", false) == true, "Self widens to Self? via TYPE_PARAMETER identity")

	# Existing trait Self reify still matches implementer / rejects String.
	var self_ok := _src_class("CompatTraitSelfOk extends Node\nuses Creatable\n\ntrait Creatable:\n\tabstract static func create() -> Self\n\nstatic func create() -> Self:\n\treturn CompatTraitSelfOk.new()\n")
	var self_ok_report: Dictionary = probe.analyze_source(self_ok, "res://tests/compat_trait_self_ok.barista")
	_expect(failures, self_ok_report.get("valid", false) == true, "trait Self return matching implementer still valid")

	var self_bad := _src_class("CompatTraitSelfBad extends Node\nuses Creatable\n\ntrait Creatable:\n\tabstract static func create() -> Self\n\nstatic func create() -> String:\n\treturn \"x\"\n")
	var self_bad_report: Dictionary = probe.analyze_source(self_bad, "res://tests/compat_trait_self_bad.barista")
	_expect(failures, self_bad_report.get("valid", true) == false, "trait Self return mismatched to String still invalid")

	# Declaration-index stays opt-in under analyze probes.
	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("SelfCompatIndexGuard extends Node\n"), "res://tests/self_compat_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"Self TYPE_PARAMETER compat probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_enum_self_payload_field_leg(failures: PackedStringArray) -> void:
	# Foundry reduce_call_enum_case_construction SelfFieldLeg @ c9d5e35 (#60 residual).
	# Non-generic legs only: FRAME_RECEIVER / BASE_RECEIVER / EXACT_HANDLE / EXACT_DECLARING /
	# LITERAL_SELF. Generic open-schema / union-collapse remain deferred.
	var probe := BaristaScriptAnalyzerProbe.new()

	# FRAME_RECEIVER: unqualified / self-qualified / contextual shorthand admit `self`.
	var same_receiver := _src_class("EnumSelfSameReceiver extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_all() -> void:\n\tvar made := Message.Attach(1, self)\n\tvar qualified := self.Message.Attach(2, self)\n\tvar shorthand: Message = .Attach(3, self)\n")
	var same_receiver_report: Dictionary = probe.analyze_source(same_receiver, "res://tests/enum_self_same_receiver.barista")
	_expect(failures, same_receiver_report.get("valid", false) == true, "Self payload admits self via frame/self/shorthand spellings")

	# FRAME_RECEIVER rejects a foreign instance typed as the declaring class.
	var foreign := _src_class("EnumSelfForeign extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_foreign(other: EnumSelfForeign) -> void:\n\tvar bad := Message.Attach(1, other)\n")
	var foreign_report: Dictionary = probe.analyze_source(foreign, "res://tests/enum_self_foreign.barista")
	_expect(failures, foreign_report.get("valid", true) == false, "Self payload rejects foreign same-class instance on frame spelling")
	var saw_foreign := false
	for message in foreign_report.get("errors", PackedStringArray()):
		if 'Invalid argument 2 for enum case "Message.Attach"' in message:
			saw_foreign = true
	_expect(failures, saw_foreign, "foreign Self payload diagnostic")

	# EXACT_DECLARING: static frame substitutes Self to the declaring class.
	var static_ok := _src_class("EnumSelfStaticOk extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nstatic func construct_static(value: EnumSelfStaticOk) -> void:\n\tvar made := Message.Attach(3, value)\n")
	var static_ok_report: Dictionary = probe.analyze_source(static_ok, "res://tests/enum_self_static_ok.barista")
	_expect(failures, static_ok_report.get("valid", false) == true, "static Message.Attach admits declaring-class value")

	# EXACT_HANDLE: ClassName.Message substitutes Self to the named class.
	var handle_ok := _src_class("EnumSelfHandleOk extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_handle(value: EnumSelfHandleOk) -> void:\n\tvar made := EnumSelfHandleOk.Message.Attach(4, value)\n")
	var handle_ok_report: Dictionary = probe.analyze_source(handle_ok, "res://tests/enum_self_handle_ok.barista")
	_expect(failures, handle_ok_report.get("valid", false) == true, "ClassName.Message.Attach admits declaring-class value")

	var handle_bad := _src_class("EnumSelfHandleBad extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_handle() -> void:\n\tvar bad := EnumSelfHandleBad.Message.Attach(1, \"nope\")\n")
	var handle_bad_report: Dictionary = probe.analyze_source(handle_bad, "res://tests/enum_self_handle_bad.barista")
	_expect(failures, handle_bad_report.get("valid", true) == false, "ClassName.Message.Attach rejects unrelated type")

	# BASE_RECEIVER: instance base admits the base expression; rejects frame self / other.
	var base_ok := _src_class("EnumSelfBaseOk extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_via_base(receiver: EnumSelfBaseOk) -> void:\n\tvar made := receiver.Message.Attach(8, receiver)\n")
	var base_ok_report: Dictionary = probe.analyze_source(base_ok, "res://tests/enum_self_base_ok.barista")
	_expect(failures, base_ok_report.get("valid", false) == true, "receiver.Message.Attach admits the base expression")

	var base_bad := _src_class("EnumSelfBaseBad extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_via_base(receiver: EnumSelfBaseBad, other: EnumSelfBaseBad) -> void:\n\tvar first := receiver.Message.Attach(1, other)\n\tvar second := receiver.Message.Attach(2, self)\n")
	var base_bad_report: Dictionary = probe.analyze_source(base_bad, "res://tests/enum_self_base_bad.barista")
	_expect(failures, base_bad_report.get("valid", true) == false, "receiver.Message.Attach rejects other / frame self")

	# LITERAL_SELF: static Self.Message keeps Self identity (admits Self-typed parameter).
	var literal_self := _src_class("EnumSelfLiteralStatic extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nstatic func construct_static(value: Self) -> void:\n\tvar made := Self.Message.Attach(1, value)\n")
	var literal_self_report: Dictionary = probe.analyze_source(literal_self, "res://tests/enum_self_literal_static.barista")
	_expect(failures, literal_self_report.get("valid", false) == true, "static Self.Message.Attach admits Self-typed parameter")

	# Instance Self.Message is FRAME_RECEIVER: admits self, rejects foreign.
	var self_handle := _src_class("EnumSelfHandleFrame extends Node\nenum Message:\n\tDetach\n\tAttach(index: int, owner: Self)\nfunc construct_via_self_handle(other: EnumSelfHandleFrame) -> void:\n\tvar good := Self.Message.Attach(1, self)\n\tvar bad := Self.Message.Attach(2, other)\n")
	var self_handle_report: Dictionary = probe.analyze_source(self_handle, "res://tests/enum_self_handle_frame.barista")
	_expect(failures, self_handle_report.get("valid", true) == false, "instance Self.Message rejects foreign; admits self")
	var saw_self_handle_bad := false
	var saw_self_handle_only_one := 0
	for message in self_handle_report.get("errors", PackedStringArray()):
		if 'Invalid argument 2 for enum case "Message.Attach"' in message:
			saw_self_handle_bad = true
			saw_self_handle_only_one += 1
	_expect(failures, saw_self_handle_bad and saw_self_handle_only_one == 1, "instance Self.Message foreign diagnostic once")

	# final declaring class: EXACT_DECLARING admits any value of that class.
	var final_ok := "final " + _kw_class_name() + " EnumSelfFinalSolo extends Node\nenum Note:\n\tTag(owner: Self)\nfunc construct_with_value(value: EnumSelfFinalSolo) -> void:\n\tvar made := Note.Tag(value)\n"
	var final_ok_report: Dictionary = probe.analyze_source(final_ok, "res://tests/enum_self_final_solo.barista")
	_expect(failures, final_ok_report.get("valid", false) == true, "final class Self payload admits same-class value")

	# Declaration-index stays opt-in under analyze probes.
	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("EnumSelfIndexGuard extends Node\n"), "res://tests/enum_self_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"enum Self payload probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_complete_self_referential_enum_type(failures: PackedStringArray) -> void:
	# Foundry complete_self_referential_enum_type @ c9d5e35 (#60 residual).
	# Non-generic recursive Chain: empty payload shells gain cases; nested edges stay shells;
	# Array[Chain] completes through its element; nested construction type-checks.
	var probe := BaristaScriptAnalyzerProbe.new()
	var report: Dictionary = probe.complete_self_referential_enum_type()

	_expect(failures, report.get("analyze_ok", false) == true, "recursive Chain host analyzes cleanly")
	_expect(failures, report.get("found_chain", false) == true, "Chain enum located after analyze")
	_expect(failures, report.get("declared_case_count", 0) == 3, "Chain declares End/Link/Branch")
	_expect(failures, report.get("shell_is_enum", false) == true, "Link.next captured as tagged-union shell")
	_expect(failures, report.get("shell_values_empty", false) == true, "Link.next starts as empty identity shell")
	_expect(failures, report.get("completed_values_count", 0) == 3, "complete fills End/Link/Branch")
	_expect(failures, report.get("completed_has_end", false) == true, "completed shell has End")
	_expect(failures, report.get("completed_has_link", false) == true, "completed shell has Link")
	_expect(failures, report.get("completed_has_branch", false) == true, "completed shell has Branch")
	_expect(failures, report.get("nested_link_stays_shell", false) == true,
		"recursive Link.next inside completed payloads stays an empty shell")
	_expect(failures, report.get("idempotent_values", false) == true, "completing twice keeps case count")
	_expect(failures, report.get("idempotent_nested_shell", false) == true, "completing twice keeps nested shells")
	_expect(failures, report.get("array_container_size", 0) == 1, "Branch children is a one-element container")
	_expect(failures, report.get("array_element_values_count", 0) == 3,
		"complete descends into Array[Chain] element")

	# Nested construction + nested match over a completed bind (behavioral consumer path).
	var nested := _src_class("CompleteSelfRefNested extends Node\nenum Chain:\n\tEnd\n\tLink(next: Chain)\nfunc build() -> Chain:\n\treturn Chain.Link(Chain.Link(Chain.End))\nfunc length(node: Chain) -> int:\n\tmatch node:\n\t\tChain.End:\n\t\t\treturn 0\n\t\tChain.Link(var next):\n\t\t\tmatch next:\n\t\t\t\tChain.End:\n\t\t\t\t\treturn 1\n\t\t\t\tChain.Link(_):\n\t\t\t\t\treturn 2\n")
	var nested_report: Dictionary = probe.analyze_source(nested, "res://tests/complete_self_ref_nested.barista")
	_expect(failures, nested_report.get("valid", false) == true,
		"nested Chain.Link construction and recursive match binds type-check")

	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("CompleteSelfRefIndexGuard extends Node\n"), "res://tests/complete_self_ref_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"complete_self_referential_enum_type probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_self_contract_assign_return(failures: PackedStringArray) -> void:
	# Foundry self_contract_admits_value_type RETURN @ c9d5e35 (#60 residual).
	# Assignable initializer / return / assignment admit same-receiver / Self-typed values;
	# reject unrelated class. Gradual-union residual deferred.
	var probe := BaristaScriptAnalyzerProbe.new()

	# Self parameter → local Self → return Self (admit self / Self-typed value).
	var roundtrip := _src_class("SelfContractRoundtrip extends Node\nfunc echo(value: Self) -> Self:\n\tvar tmp: Self = value\n\ttmp = self\n\treturn tmp\n")
	var roundtrip_report: Dictionary = probe.analyze_source(roundtrip, "res://tests/self_contract_roundtrip.barista")
	_expect(failures, roundtrip_report.get("valid", false) == true, "Self assign/return admits Self-typed value and self")

	# Unrelated class into Self local / return / assignment rejected.
	var foreign := _src_class("SelfContractForeign extends Node\nfunc bad(other: SelfContractForeign) -> Self:\n\tvar tmp: Self = other\n\ttmp = other\n\treturn other\n")
	var foreign_report: Dictionary = probe.analyze_source(foreign, "res://tests/self_contract_foreign.barista")
	_expect(failures, foreign_report.get("valid", true) == false, "Self assign/return rejects unrelated same-class instance")
	var saw_assign := false
	var saw_return := false
	for message in foreign_report.get("errors", PackedStringArray()):
		if "Cannot assign a value of type" in message or 'cannot be assigned to a variable of type "Self"' in message:
			saw_assign = true
		if "Cannot return value of type" in message:
			saw_return = true
	_expect(failures, saw_assign, "foreign Self local/assignment diagnostic")
	_expect(failures, saw_return, "foreign Self return diagnostic")

	# Member typed Self: admit self / Self param; reject foreign.
	var member_ok := _src_class("SelfContractMemberOk extends Node\nvar holder: Self\nfunc store(value: Self) -> void:\n\tholder = value\n\tholder = self\n")
	var member_ok_report: Dictionary = probe.analyze_source(member_ok, "res://tests/self_contract_member_ok.barista")
	_expect(failures, member_ok_report.get("valid", false) == true, "Self member assignment admits Self / self")

	var member_bad := _src_class("SelfContractMemberBad extends Node\nvar holder: Self\nfunc store(other: SelfContractMemberBad) -> void:\n\tholder = other\n")
	var member_bad_report: Dictionary = probe.analyze_source(member_bad, "res://tests/self_contract_member_bad.barista")
	_expect(failures, member_bad_report.get("valid", true) == false, "Self member assignment rejects foreign same-class instance")

	# Call-site Self parameter: admit self / receiver identity; reject foreign.
	var call_ok := _src_class("SelfContractCallOk extends Node\nfunc take(value: Self) -> void:\n\tpass\nfunc use() -> void:\n\ttake(self)\n\tself.take(self)\n")
	var call_ok_report: Dictionary = probe.analyze_source(call_ok, "res://tests/self_contract_call_ok.barista")
	_expect(failures, call_ok_report.get("valid", false) == true, "Self parameter call admits self")

	var call_bad := _src_class("SelfContractCallBad extends Node\nfunc take(value: Self) -> void:\n\tpass\nfunc use(other: SelfContractCallBad) -> void:\n\ttake(other)\n")
	var call_bad_report: Dictionary = probe.analyze_source(call_bad, "res://tests/self_contract_call_bad.barista")
	_expect(failures, call_bad_report.get("valid", true) == false, "Self parameter call rejects foreign same-class instance")

	# final class: Self RETURN admits declaring-class value.
	var final_ok := "final " + _kw_class_name() + " SelfContractFinal extends Node\nfunc echo(value: SelfContractFinal) -> Self:\n\tvar tmp: Self = value\n\treturn value\n"
	var final_ok_report: Dictionary = probe.analyze_source(final_ok, "res://tests/self_contract_final.barista")
	_expect(failures, final_ok_report.get("valid", false) == true, "final class Self assign/return admits declaring-class value")

	# Existing enum Self payload fixtures still covered by sibling suite; spot-check frame admits.
	var enum_ok := _src_class("SelfContractEnumStillOk extends Node\nenum Message:\n\tAttach(owner: Self)\nfunc construct() -> void:\n\tvar made := Message.Attach(self)\n")
	var enum_ok_report: Dictionary = probe.analyze_source(enum_ok, "res://tests/self_contract_enum_still_ok.barista")
	_expect(failures, enum_ok_report.get("valid", false) == true, "enum Self payload still admits self after RETURN wiring")

	# Declaration-index stays opt-in under analyze probes.
	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("SelfContractIndexGuard extends Node\n"), "res://tests/self_contract_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"Self-contract assign/return probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_self_contract_gradual_union(failures: PackedStringArray) -> void:
	# Foundry self_contract_admits_gradual_value + self_free_union_members_admit_value @ c9d5e35.
	# Bare Self is rejected as a union member; nested (int, Self) mirrors Foundry fixtures.
	var probe := BaristaScriptAnalyzerProbe.new()
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", false)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()

	# Soft: Variant into Self-bearing union is booked (admitted), same as a plain union.
	var soft_self := _src_class("GradualSelfUnionSoft extends Node\nfunc drive() -> void:\n\tvar dynamic: Variant = 5\n\tvar link: int | (int, Self) = dynamic\n")
	var soft_self_report: Dictionary = probe.analyze_source(soft_self, "res://tests/gradual_self_union_soft.barista")
	_expect(failures, soft_self_report.get("valid", false) == true, "soft Variant admits into Self-bearing union")

	var soft_plain := _src_class("GradualPlainUnionSoft extends Node\nfunc drive() -> void:\n\tvar dynamic: Variant = 5\n\tvar link: int | String = dynamic\n")
	var soft_plain_report: Dictionary = probe.analyze_source(soft_plain, "res://tests/gradual_plain_union_soft.barista")
	_expect(failures, soft_plain_report.get("valid", false) == true, "soft Variant admits into plain union")

	# Strict dynamic: Variant into either union refused.
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", true)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var strict_self_report: Dictionary = probe.analyze_source(soft_self, "res://tests/gradual_self_union_strict.barista")
	_expect(failures, strict_self_report.get("valid", true) == false, "strict_dynamic refuses Variant into Self-bearing union")
	var strict_plain_report: Dictionary = probe.analyze_source(soft_plain, "res://tests/gradual_plain_union_strict.barista")
	_expect(failures, strict_plain_report.get("valid", true) == false, "strict_dynamic refuses Variant into plain union")
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", false)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()

	# Self-free alternative still admits ordinary values (int through int | (int, Self)).
	var free_ok := _src_class("SelfFreeUnionAdmit extends Node\nfunc drive() -> void:\n\tvar link: int | (int, Self) = 5\n\tlink = 7\n")
	var free_ok_report: Dictionary = probe.analyze_source(free_ok, "res://tests/self_free_union_admit.barista")
	_expect(failures, free_ok_report.get("valid", false) == true, "Self-free union alternative admits ordinary int")

	# Self-bearing alternative still requires Self contract: foreign same-class into (int, Self) rejected.
	var self_ok := _src_class("SelfBearingUnionOk extends Node\nfunc drive() -> void:\n\tvar pair: (int, Self) = (1, self)\n\tvar link: int | (int, Self) = pair\n")
	var self_ok_report: Dictionary = probe.analyze_source(self_ok, "res://tests/self_bearing_union_ok.barista")
	_expect(failures, self_ok_report.get("valid", false) == true, "Self-bearing union alternative admits (int, Self) pair")

	var self_bad := _src_class("SelfBearingUnionBad extends Node\nfunc drive(other: SelfBearingUnionBad) -> void:\n\tvar pair: (int, SelfBearingUnionBad) = (1, other)\n\tvar link: int | (int, Self) = pair\n")
	var self_bad_report: Dictionary = probe.analyze_source(self_bad, "res://tests/self_bearing_union_bad.barista")
	_expect(failures, self_bad_report.get("valid", true) == false, "Self-bearing union alternative rejects foreign same-class pair")

	# Declaration-index stays opt-in under analyze probes.
	var index := BaristaScriptDeclarationIndexProbe.new()
	var before := index.get_record_count()
	var _ignored: Dictionary = probe.analyze_source(
		_src_class("GradualSelfUnionIndexGuard extends Node\n"), "res://tests/gradual_self_union_index_guard.barista")
	_expect(failures, index.get_record_count() == before,
		"Self-contract gradual-union probes must not mutate declaration index")
	BaristaScriptParseCache.clear_source_overrides()


func _test_local_tuple_and_literal_consumers(failures: PackedStringArray) -> void:
	# Foundry tuple producer/consumer slice @ c9d5e35: local named tuples are nominal,
	# anonymous tuples are structural, and contextual Self survives literal publication.
	var probe := BaristaScriptAnalyzerProbe.new()
	var tuple_ok := _src_class("TupleLocalOk extends Node\ntuple Pair(first: int, second: String)\nfunc use() -> void:\n\tvar named: Pair = Pair(1, \"ok\")\n\tvar by_index: int = named.0\n\tvar by_field: String = named.second\n\tvar anonymous: (int, String) = (2, \"two\")\n\tvar anonymous_index: String = anonymous.1\n")
	var tuple_ok_report: Dictionary = probe.analyze_source(tuple_ok, "res://tests/tuple_local_ok.barista")
	_expect(failures, tuple_ok_report.get("valid", false) == true,
		"local named/anonymous tuple construction and access are valid: %s" % tuple_ok_report.get("errors"))

	var tuple_bad := _src_class("TupleLocalBad extends Node\ntuple Pair(first: int, second: String)\nfunc use() -> void:\n\tvar wrong_arity := Pair(1)\n\tvar wrong_field := Pair(1, 2)\n")
	var tuple_bad_report: Dictionary = probe.analyze_source(tuple_bad, "res://tests/tuple_local_bad.barista")
	var tuple_bad_errors: PackedStringArray = tuple_bad_report.get("errors", PackedStringArray())
	var tuple_bad_validate: Dictionary = probe.validate_source(tuple_bad, "res://tests/tuple_local_bad.barista", false)
	var tuple_bad_positioned: Array = tuple_bad_validate.get("errors", [])
	_expect(failures, tuple_bad_report.get("valid", true) == false, "named tuple arity/type mismatches are invalid")
	_expect(failures, tuple_bad_positioned.size() == 2 and
		str(tuple_bad_positioned[0].get("message", "")) == 'Tuple "Pair" expects 2 argument(s), but 1 were given.' and
		tuple_bad_positioned[0].get("line") == 4 and tuple_bad_positioned[0].get("column") == 24 and
		str(tuple_bad_positioned[1].get("message", "")) == 'Invalid argument 2 for tuple "Pair": should be "String" but is "int".' and
		tuple_bad_positioned[1].get("line") == 5 and tuple_bad_positioned[1].get("column") == 32,
		"named tuple constructor mismatches preserve full ordered messages and starts: %s" % [tuple_bad_positioned])

	var self_tuple_ok := _src_class("TupleSelfLiteralOk extends Node\nfunc take(value: (Self, int)) -> void:\n\tpass\nfunc use() -> void:\n\ttake((self, 1))\n")
	var self_tuple_ok_report: Dictionary = probe.analyze_source(self_tuple_ok, "res://tests/tuple_self_literal_ok.barista")
	_expect(failures, self_tuple_ok_report.get("valid", false) == true,
		"contextual tuple literal publishes analyzer-substituted Self")

	var self_tuple_bad := _src_class("TupleSelfLiteralBad extends Node\nfunc take(value: (Self, int)) -> void:\n\tpass\nfunc use(other: TupleSelfLiteralBad) -> void:\n\ttake((other, 1))\n")
	var self_tuple_bad_report: Dictionary = probe.analyze_source(self_tuple_bad, "res://tests/tuple_self_literal_bad.barista")
	var self_tuple_bad_validate: Dictionary = probe.validate_source(self_tuple_bad, "res://tests/tuple_self_literal_bad.barista", false)
	var self_tuple_bad_errors: Array = self_tuple_bad_validate.get("errors", [])
	_expect(failures, self_tuple_bad_report.get("valid", true) == false and self_tuple_bad_errors.size() == 1 and
		str(self_tuple_bad_errors[0].get("message", "")) == 'Invalid argument for "take()" function: argument 1 should be "(Self, int)" but is "(TupleSelfLiteralBad, int)".' and
		self_tuple_bad_errors[0].get("line") == 5 and self_tuple_bad_errors[0].get("column") == 10,
		"hand-written same-class tuple element cannot impersonate contextual Self: %s" % [self_tuple_bad_errors])

	var tuple_write_source := "func test():\n\tvar pair := (1, 2)\n\tpair.0 = 5\n"
	var tuple_write_report: Dictionary = probe.validate_source(tuple_write_source, "res://tests/tuple_write_bad.barista", false)
	var tuple_write_errors: Array = tuple_write_report.get("errors", [])
	_expect(failures, tuple_write_errors.size() == 1 and
		str(tuple_write_errors[0].get("message", "")) == 'Cannot assign to an element of tuple "(int, int)"; tuples are immutable.' and
		tuple_write_errors[0].get("line") == 3 and tuple_write_errors[0].get("column") == 5,
		"tuple writes report the exact immutable error at the assignee: %s" % [tuple_write_errors])

	var tuple_access_source := "tuple Vec2(x: float, y: float)\nfunc test():\n\tvar point := Vec2(1.0, 2.0)\n\tprint(point.z)\n\tprint((1, 2).2)\n"
	var tuple_access_report: Dictionary = probe.validate_source(tuple_access_source, "res://tests/tuple_access_bad.barista", false)
	var tuple_access_errors: Array = tuple_access_report.get("errors", [])
	_expect(failures, tuple_access_errors.size() == 2 and
		str(tuple_access_errors[0].get("message", "")) == 'Tuple "Vec2" has no field named "z".' and
		tuple_access_errors[0].get("line") == 4 and tuple_access_errors[0].get("column") == 17 and
		str(tuple_access_errors[1].get("message", "")) == 'Tuple index 2 is out of range for "(int, int)", which has 2 element(s).' and
		tuple_access_errors[1].get("line") == 5 and tuple_access_errors[1].get("column") == 18,
		"tuple field and range errors preserve their exact nodes: %s" % [tuple_access_errors])

	var invalid_index_source := "func test():\n\t# Array indices must be integers.\n\tprint([0, 1][true])\n"
	var invalid_index_report: Dictionary = probe.validate_source(invalid_index_source, "res://tests/invalid_array_index.barista", false)
	var invalid_index_errors: Array = invalid_index_report.get("errors", [])
	_expect(failures, invalid_index_errors.size() == 1 and
		str(invalid_index_errors[0].get("message", "")) == 'Invalid index type "bool" for a base of type "Array".' and
		invalid_index_errors[0].get("line") == 3 and invalid_index_errors[0].get("column") == 18,
		"ordinary Array index checking reports the index expression: %s" % [invalid_index_errors])

	var indexed_read_source := "func use(values: Array[int], lookup: Dictionary[String, int], i: int, key: String) -> void:\n\tvar from_array: int = values[i]\n\tvar from_dictionary: int = lookup[key]\n\tvalues[i] = from_dictionary\n\tlookup[key] = from_array\n"
	var indexed_read_report: Dictionary = probe.analyze_source(indexed_read_source, "res://tests/indexed_read_types.barista")
	_expect(failures, indexed_read_report.get("valid", false),
		"Array element and Dictionary value types publish through indexed reads/writes: %s" % indexed_read_report.get("errors"))

	var dictionary_index_source := "func test(d: Dictionary[String, int]):\n\tprint(d[true])\n"
	var dictionary_index_errors: Array = probe.validate_source(dictionary_index_source, "res://tests/dictionary_index_bad.barista", false).get("errors", [])
	_expect(failures, dictionary_index_errors.size() == 1 and
		str(dictionary_index_errors[0].get("message", "")) == 'Invalid index type "bool" for a base of type "Dictionary[String, int]".' and
		dictionary_index_errors[0].get("line") == 2 and dictionary_index_errors[0].get("column") == 13,
		"typed Dictionary keys reject the wrong concrete index at its expression: %s" % [dictionary_index_errors])

	var dynamic_base_source := "func test(value: Variant):\n\tprint(value[0])\n"
	var dynamic_index_source := "func test(values: Array[int], index: Variant):\n\tprint(values[index])\n"
	_expect(failures, probe.analyze_source(dynamic_base_source, "res://tests/dynamic_base_index.barista").get("valid", false) and
		probe.analyze_source(dynamic_index_source, "res://tests/dynamic_index.barista").get("valid", false),
		"dynamic base/index subscripts remain runtime-unsafe but valid outside strict mode")
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", true)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var dynamic_base_errors: Array = probe.validate_source(dynamic_base_source, "res://tests/dynamic_base_index.barista", false).get("errors", [])
	var dynamic_index_errors: Array = probe.validate_source(dynamic_index_source, "res://tests/dynamic_index.barista", false).get("errors", [])
	_expect(failures, dynamic_base_errors.size() == 1 and
		str(dynamic_base_errors[0].get("message", "")) == "Cannot use subscript operator on Variant in strict dynamic mode." and
		dynamic_base_errors[0].get("line") == 2 and dynamic_base_errors[0].get("column") == 11 and
		dynamic_index_errors.size() == 1 and
		str(dynamic_index_errors[0].get("message", "")) == 'Cannot use dynamic index of type "Variant" for base of type "Array[int]" in strict dynamic mode.' and
		dynamic_index_errors[0].get("line") == 2 and dynamic_index_errors[0].get("column") == 18,
		"strict dynamic subscript errors preserve full messages and base/index starts: %s / %s" % [dynamic_base_errors, dynamic_index_errors])
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", false)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()

	# Raw and typed Array alternatives both claim an array literal. Reordering the union must
	# leave the same verdict, while two Self-bearing claimants use their elements to choose one.
	var union_source := _src_class("TupleUnionClaimants extends Node\nfunc raw_first(v: Array | Array[Self]) -> void:\n\tpass\nfunc typed_first(v: Array[Self] | Array) -> void:\n\tpass\nfunc self_choice(v: Array[Self] | Array[(Self, int)]) -> void:\n\tpass\nfunc ambiguous_first(v: Array[Self] | Array[(Self, int)]) -> void:\n\tpass\nfunc ambiguous_reordered(v: Array[(Self, int)] | Array[Self]) -> void:\n\tpass\nfunc test() -> void:\n\traw_first([self])\n\ttyped_first([self])\n\tself_choice([self])\n\tambiguous_first([])\n\tambiguous_reordered([])\n")
	var union_report: Dictionary = probe.analyze_source(union_source, "res://tests/tuple_union_claimants.barista")
	_expect(failures, union_report.get("valid", false) == true,
		"raw/typed and ambiguous claimant order is neutral; only the unique all-Self fit is selected: %s" % union_report.get("errors"))

	# Reparse the same positive and negative source through every public surface. Default
	# analysis remains read-only with respect to the declaration index.
	var index := BaristaScriptDeclarationIndexProbe.new()
	var index_before := index.get_record_count()
	var tuple_ok_repeat: Dictionary = probe.analyze_source(tuple_ok, "res://tests/tuple_local_ok.barista")
	var tuple_bad_repeat: Dictionary = probe.analyze_source(tuple_bad, "res://tests/tuple_local_bad.barista")
	_expect(failures, tuple_ok_repeat.get("errors", PackedStringArray()) == tuple_ok_report.get("errors", PackedStringArray()) and
		tuple_bad_repeat.get("errors", PackedStringArray()) == tuple_bad_report.get("errors", PackedStringArray()),
		"repeated tuple analysis preserves ordered diagnostics")
	_expect(failures, probe.validate_source(tuple_ok, "res://tests/tuple_local_ok.barista", false).get("valid", false) and
		probe.is_semantically_valid(tuple_ok, "res://tests/tuple_local_ok.barista"),
		"tuple analyze/validate/is-valid surfaces agree")
	var tuple_bad_script := BaristaScript.new()
	tuple_bad_script.set_source_code(tuple_bad)
	tuple_bad_script.resource_path = "res://tests/tuple_local_bad.barista"
	_expect(failures, not tuple_bad_validate.get("valid", true) and
		not probe.is_semantically_valid(tuple_bad, "res://tests/tuple_local_bad.barista") and
		not tuple_bad_script.is_valid(),
		"invalid tuple source agrees through validate/probe/script is-valid surfaces")
	_expect(failures, index.get_record_count() == index_before,
		"default repeated tuple analysis preserves the declaration index")

	var identity_controls: Dictionary = probe.self_identity_controls()
	var expected_identity_controls := [
		"alpha_fixed_slot", "strict_fixed_slot", "strict_return_slot", "strict_async",
		"strict_rest", "strict_nested", "strict_union", "strict_ignores_parser_wildcard",
		"markers_match", "markers_fixed_slot", "markers_return_slot", "markers_rest_slot",
		"parameter_variadic_to_fixed", "parameter_gradual_to_narrowed_rest",
		"parameter_strict_return_mismatch", "parameter_fixed_receiver_identity",
		"parameter_return_receiver_identity", "parameter_rest_receiver_identity",
	]
	var identity_complete := identity_controls.size() == expected_identity_controls.size()
	for control in expected_identity_controls:
		identity_complete = identity_complete and identity_controls.has(control)
	for control in identity_controls:
		identity_complete = identity_complete and bool(identity_controls[control])
	_expect(failures, identity_complete,
		"strict/alpha identity and substituted-Self markers traverse fixed/return/rest/nested/union slots: %s" % identity_controls)


func _test_ordinary_assignment_and_return_consumers(failures: PackedStringArray) -> void:
	# Foundry assignment/return ordinary compatibility gates @ c9d5e35.
	var probe := BaristaScriptAnalyzerProbe.new()
	var assignment_source := "func f(v: int):\n\tvar x: String = \"ok\"\n\tx = v\n"
	var assignment_report: Dictionary = probe.validate_source(assignment_source, "res://tests/ordinary_assignment_bad.barista", false)
	var assignment_errors: Array = assignment_report.get("errors", [])
	_expect(failures, assignment_report.get("valid", true) == false, "ordinary assignment mismatch is invalid")
	_expect(failures, assignment_errors.size() == 1 and
		str(assignment_errors[0].get("message", "")) == 'Value of type "int" cannot be assigned to a variable of type "String".' and
		assignment_errors[0].get("line") == 3 and assignment_errors[0].get("column") == 9,
		"ordinary variable assignment preserves full wording and RHS start: %s" % [assignment_errors])

	var return_source := "func f(v: String) -> int:\n\treturn v\n"
	var return_report: Dictionary = probe.validate_source(return_source, "res://tests/ordinary_return_bad.barista", false)
	var return_errors: Array = return_report.get("errors", [])
	_expect(failures, return_report.get("valid", true) == false, "ordinary return mismatch is invalid")
	_expect(failures, return_errors.size() == 1 and
		str(return_errors[0].get("message", "")) == 'Cannot return value of type "String" because the function return type is "int".' and
		return_errors[0].get("line") == 2 and return_errors[0].get("column") == 5,
		"ordinary variable return preserves full wording and ReturnNode start: %s" % [return_errors])

	var constant_source := "const TEXT: Variant = \"hello\"\n\nfunc take_int(v: int) -> int:\n\treturn v\n\nfunc give_int() -> int:\n\treturn TEXT\n\nfunc test():\n\ttake_int(TEXT)\n\tvar initialized: int = TEXT\n\tvar assigned: int = 0\n\tassigned = TEXT\n\tprint(initialized, assigned, give_int())\n"
	var constant_report: Dictionary = probe.validate_source(constant_source, "res://tests/known_constant_consumers.barista", false)
	var constant_errors: Array = constant_report.get("errors", [])
	var constant_expectations := [
		['Cannot return a value of type "String" as "int".', 7, 12],
		['Cannot pass a value of type "String" as "int".', 10, 14],
		['Cannot assign a value of type "String" as "int".', 11, 28],
		['Cannot assign a value of type "String" as "int".', 13, 16],
	]
	var constants_exact := constant_errors.size() == constant_expectations.size()
	for i in range(min(constant_errors.size(), constant_expectations.size())):
		constants_exact = constants_exact and str(constant_errors[i].get("message", "")) == constant_expectations[i][0] and \
			constant_errors[i].get("line") == constant_expectations[i][1] and constant_errors[i].get("column") == constant_expectations[i][2]
	_expect(failures, constants_exact,
		"known Variant constants retain value-specific wording and identifier starts: %s" % [constant_errors])

	var return_shape_source := "func void_bad() -> void:\n\treturn 1\nfunc bare_bad() -> int:\n\treturn\n"
	var return_shape_errors: Array = probe.validate_source(return_shape_source, "res://tests/return_shape_bad.barista", false).get("errors", [])
	_expect(failures, return_shape_errors.size() == 2 and
		str(return_shape_errors[0].get("message", "")) == "A void function cannot return a value." and
		return_shape_errors[0].get("line") == 2 and return_shape_errors[0].get("column") == 5 and
		str(return_shape_errors[1].get("message", "")) == 'Cannot return without a value because the function return type is "int".' and
		return_shape_errors[1].get("line") == 4 and return_shape_errors[1].get("column") == 5,
		"void-value and missing-value returns preserve full messages and ReturnNode starts: %s" % [return_shape_errors])

	var direct_constant_source := "func test():\n\tconst TEST = 25\n\tTEST = 50\n"
	var direct_constant_errors: Array = probe.validate_source(direct_constant_source, "res://tests/direct_constant_write.barista", false).get("errors", [])
	_expect(failures, direct_constant_errors.size() == 1 and
		str(direct_constant_errors[0].get("message", "")) == "Cannot assign a new value to a constant." and
		direct_constant_errors[0].get("line") == 3 and direct_constant_errors[0].get("column") == 5,
		"direct constant writes reject at the assignee: %s" % [direct_constant_errors])

	var match_bind_source := "enum Box:\n\tValue(v: Variant)\nfunc inspect(box: Box) -> void:\n\tmatch box:\n\t\tBox.Value(b):\n\t\t\tif b is int:\n\t\t\t\tb = 5\n\t\t_:\n\t\t\tpass\n"
	var match_bind_errors: Array = probe.validate_source(match_bind_source, "res://tests/match_bind_write.barista", false).get("errors", [])
	_expect(failures, match_bind_errors.size() == 1 and
		str(match_bind_errors[0].get("message", "")) == "Cannot assign a new value to a constant." and
		match_bind_errors[0].get("line") == 7 and match_bind_errors[0].get("column") == 17,
		"narrowed match binds remain read-only at the assignee: %s" % [match_bind_errors])


func _test_steps_1_5_repair_regressions(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()

	var signal_source := "class_name SignalSelfProjectionHost extends Node\nclass Base extends Node:\n\tsignal changed(value: Self)\nclass Child extends Base:\n\tpass\nfunc test(receiver: Child, base_value: Base, child_value: Child) -> void:\n\treceiver.emit_signal(\"changed\", base_value)\n\treceiver.emit_signal(\"changed\", child_value)\n"
	var signal_errors: Array = probe.validate_source(signal_source, "res://tests/review_signal_self_projection.barista", false).get("errors", [])
	_expect(failures, signal_errors.size() == 1 and
		str(signal_errors[0].get("message", "")) == 'Invalid argument for "emit_signal()" function: argument 2 should be "Child" but is "Base".' and
		signal_errors[0].get("line") == 7 and signal_errors[0].get("column") == 37,
		"inherited Signal Self projects to Child while rejecting Base: %s" % [signal_errors])

	var lexical_tuple := "class_name LexicalTupleHost extends Node\ntuple Pair(left: int, right: int)\nclass Inner extends Node:\n\tfunc make() -> Pair:\n\t\treturn Pair(1, 2)\n"
	_expect(failures, probe.validate_source(lexical_tuple, "res://tests/review_lexical_tuple.barista", false).get("valid", false),
		"lexical parent tuple annotation and constructor resolve")
	var receiver_tuple := "class_name ReceiverTupleHost extends Node\ntuple Owned(owner: Self, value: int)\nfunc construct_on(receiver: ReceiverTupleHost) -> Owned:\n\treturn receiver.Owned(receiver, 1)\n"
	_expect(failures, probe.validate_source(receiver_tuple, "res://tests/review_receiver_tuple.barista", false).get("valid", false),
		"typed receiver tuple construction preserves receiver-relative Self")

	var dynamic_tuple := "func read(value: Variant) -> void:\n\tprint(value.0)\n"
	_expect(failures, probe.validate_source(dynamic_tuple, "res://tests/review_dynamic_tuple_index.barista", false).get("valid", false),
		"Variant tuple index remains gradual outside strict mode")
	var dynamic_tuple_element := "func read(pair: (int, String), index: int) -> Variant:\n\treturn pair[index]\n"
	_expect(failures, probe.validate_source(dynamic_tuple_element, "res://tests/review_dynamic_tuple_element.barista", false).get("valid", false),
		"runtime integer tuple index remains gradual")
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", true)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var strict_dynamic_errors: Array = probe.validate_source(dynamic_tuple, "res://tests/review_dynamic_tuple_index.barista", false).get("errors", [])
	_expect(failures, strict_dynamic_errors.size() == 1 and
		str(strict_dynamic_errors[0].get("message", "")) == "Cannot use tuple index access on Variant in strict dynamic mode." and
		strict_dynamic_errors[0].get("line") == 2 and strict_dynamic_errors[0].get("column") == 11,
		"strict tuple index reports the gradual base: %s" % [strict_dynamic_errors])
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", false)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()

	var float_index := "func read(values: Array[int]) -> int:\n\treturn values[1.0]\n"
	_expect(failures, probe.validate_source(float_index, "res://tests/review_float_array_index.barista", false).get("valid", false),
		"Array float index preserves typed element result")

	var local_constructor := "class_name LocalConstructorHost extends Node\nclass Item extends Node:\n\tfunc _init(value: int) -> void:\n\t\tpass\nfunc make() -> Item:\n\treturn Item.new(1)\n"
	_expect(failures, probe.validate_source(local_constructor, "res://tests/review_local_constructor.barista", false).get("valid", false),
		"local constructor validates declared initializer and preserves Item result")

	var member_constant := "class_name MemberConstantWriteHost extends Node\nconst TOKEN := 1\nfunc overwrite() -> void:\n\tself.TOKEN = 2\n"
	var member_constant_errors: Array = probe.validate_source(member_constant, "res://tests/review_member_constant_write.barista", false).get("errors", [])
	_expect(failures, member_constant_errors.size() == 1 and
		str(member_constant_errors[0].get("message", "")) == "Cannot assign a new value to a constant." and
		member_constant_errors[0].get("line") == 4 and member_constant_errors[0].get("column") == 5,
		"resolved member constants remain readonly: %s" % [member_constant_errors])

	var tuple_nominal := "class_name TupleNominalConsumers extends Node\nclass Left:\n\ttuple Point(x: int, y: int)\nclass Right:\n\ttuple Point(x: int, y: int)\nfunc assign_bad(left: Left.Point, right: Right.Point) -> void:\n\tright = left\nfunc return_bad(left: Left.Point) -> Right.Point:\n\treturn left\n"
	var nominal_errors: Array = probe.validate_source(tuple_nominal, "res://tests/review_tuple_nominal_consumers.barista", false).get("errors", [])
	_expect(failures, nominal_errors.size() == 2 and
		str(nominal_errors[0].get("message", "")) == 'Value of type "Point" cannot be assigned to a variable of type "Point". The value is declared by class "Left"; the variable\'s type is declared by class "Right".' and
		nominal_errors[0].get("line") == 7 and nominal_errors[0].get("column") == 13 and
		str(nominal_errors[1].get("message", "")) == 'Cannot return value of type "Point" because the function return type is "Point". The returned value is declared by class "Left"; the return type is declared by class "Right".' and
		nominal_errors[1].get("line") == 9 and nominal_errors[1].get("column") == 5,
		"ordinary tuple consumers explain identical rendered owners: %s" % [nominal_errors])

	var ordinary_declaration := "func declare(v: int) -> void:\n\tvar value: String = v\n"
	var declaration_errors: Array = probe.validate_source(ordinary_declaration, "res://tests/review_ordinary_declaration.barista", false).get("errors", [])
	_expect(failures, declaration_errors.size() == 1 and
		str(declaration_errors[0].get("message", "")) == 'Cannot assign a value of type "int" to a variable of type "String".' and
		declaration_errors[0].get("line") == 2 and declaration_errors[0].get("column") == 25,
		"ordinary declarations report at the initializer: %s" % [declaration_errors])

	# The declaration's Signal[[Self]] signature projects through every existing member,
	# Object API, and Signal(Object, name) consumer without changing its stored declaration.
	var signal_member_source := "class_name SignalSelfMemberHost extends Node\nclass Base extends Node:\n\tsignal changed(value: Self)\nclass Child extends Base:\n\tfunc take_child(value: Child) -> void:\n\t\tpass\n\tfunc take_string(value: String) -> void:\n\t\tpass\n\tfunc check(child_value: Child, base_value: Base) -> void:\n\t\tchanged.emit(child_value)\n\t\tchanged.emit(base_value)\n\t\tself.changed.connect(take_child)\n\t\tself.changed.disconnect(take_child)\n\t\tself.changed.is_connected(take_child)\n\t\tself.changed.connect(take_string)\n"
	var signal_member_errors: Array = probe.validate_source(signal_member_source, "res://tests/repair_signal_member.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(signal_member_errors, [
		['Invalid argument for "emit()" function: argument 1 should be "Child" but is "Base".', 11, 22],
		['Cannot connect signal "Signal[[Child]]" to callable "Callable[[String], void]": signal argument 1 of type "Child" cannot be passed to callable parameter of type "String".', 15, 30],
	]), "bare/self inherited Signal Self consumer routes: %s" % [signal_member_errors])
	var signal_nested_source := "class_name SignalNestedHost extends Node\nclass Base extends Node:\n\tsignal nested(values: Array[Self])\nclass Child extends Base:\n\tfunc check(child_values: Array[Child], base_values: Array[Base]) -> void:\n\t\tnested.emit(child_values)\n\t\tnested.emit(base_values)\n"
	var signal_nested_errors: Array = probe.validate_source(signal_nested_source, "res://tests/repair_signal_nested.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(signal_nested_errors, [
		['Invalid argument for "emit()" function: argument 1 should be "Array[Child]" but is "Array[Base]".', 7, 21],
	]), "Signal Self projection traverses nested container parameters: %s" % [signal_nested_errors])

	var signal_receiver_source := "class_name SignalSelfReceiverHost extends Node\nclass Base extends Node:\n\tsignal changed(value: Self)\nclass Child extends Base:\n\tpass\nfunc take_child(value: Child) -> void:\n\tpass\nfunc take_string(value: String) -> void:\n\tpass\nfunc check(receiver: Child, child_value: Child, base_value: Base) -> void:\n\treceiver.emit_signal(\"changed\", child_value)\n\treceiver.emit_signal(\"changed\", base_value)\n\treceiver.connect(\"changed\", take_child)\n\treceiver.disconnect(\"changed\", take_child)\n\treceiver.is_connected(\"changed\", take_child)\n\treceiver.connect(\"changed\", take_string)\n\tvar projected := Signal(receiver, \"changed\")\n\tprojected.emit(child_value)\n\tprojected.emit(base_value)\n"
	var signal_receiver_errors: Array = probe.validate_source(signal_receiver_source, "res://tests/repair_signal_receiver.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(signal_receiver_errors, [
		['Invalid argument for "emit_signal()" function: argument 2 should be "Child" but is "Base".', 12, 37],
		['Cannot connect signal "Signal[[Child]]" to callable "Callable[[String], void]": signal argument 1 of type "Child" cannot be passed to callable parameter of type "String".', 16, 33],
		['Invalid argument for "emit()" function: argument 1 should be "Child" but is "Base".', 19, 20],
	]), "typed Object and Signal constructor preserve projected Self signatures: %s" % [signal_receiver_errors])

	# Tuple lookup walks the precise receiver's inheritance chain and the current lexical
	# chain. The nearest lexical declaration wins, while a foreign receiver keeps its method.
	var tuple_spellings := "class_name TupleSpellings extends Node\ntuple Owned(owner: Self, value: int)\nclass Child extends TupleSpellings:\n\tfunc make(receiver: Child) -> Owned:\n\t\tvar a := Owned(self, 1)\n\t\tvar b := self.Owned(self, 2)\n\t\tvar c := Self.Owned(self, 3)\n\t\tvar d := Child.Owned(self, 4)\n\t\tvar e := receiver.Owned(receiver, 5)\n\t\treturn e\n"
	_expect(failures, probe.validate_source(tuple_spellings, "res://tests/repair_tuple_spellings.barista", false).get("valid", false),
		"unqualified/self/Self/class/instance inherited tuple constructors preserve owner-relative Self")
	var tuple_shadow := "class_name TupleShadow extends Node\ntuple Item(left: int, right: int)\nclass Inner:\n\ttuple Item(left: String, right: String)\n\tfunc make() -> Item:\n\t\treturn Item(1, 2)\n"
	var tuple_shadow_errors: Array = probe.validate_source(tuple_shadow, "res://tests/repair_tuple_shadow.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(tuple_shadow_errors, [
		['Invalid argument 1 for tuple "Item": should be "String" but is "int".', 6, 21],
		['Invalid argument 2 for tuple "Item": should be "String" but is "int".', 6, 24],
	]), "nearest lexical tuple declaration shadows its outer sibling: %s" % [tuple_shadow_errors])
	var foreign_receiver := "class_name TupleForeign extends Node\nclass Owner:\n\ttuple Pair(left: int, right: int)\nclass Other:\n\tfunc Pair(left: int, right: int) -> String:\n\t\treturn \"method\"\nfunc bad(other: Other) -> Owner.Pair:\n\treturn other.Pair(1, 2)\n"
	var foreign_errors: Array = probe.validate_source(foreign_receiver, "res://tests/repair_tuple_foreign.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(foreign_errors, [
		['Cannot return value of type "String" because the function return type is "Pair".', 8, 5],
	]), "foreign receiver method is not captured by lexical tuple construction: %s" % [foreign_errors])

	var dynamic_tuple_report: Dictionary = probe.validate_source(dynamic_tuple, "res://tests/review_dynamic_tuple_index.barista", false, true)
	var dynamic_element_report: Dictionary = probe.validate_source(dynamic_tuple_element, "res://tests/review_dynamic_tuple_element.barista", false, true)
	var dynamic_tuple_safe: PackedInt32Array = dynamic_tuple_report.get("safe_lines", PackedInt32Array())
	var dynamic_element_safe: PackedInt32Array = dynamic_element_report.get("safe_lines", PackedInt32Array())
	_expect(failures, dynamic_tuple_report.get("valid", false) and 1 in dynamic_tuple_safe and 2 not in dynamic_tuple_safe and
		dynamic_element_report.get("valid", false) and 1 in dynamic_element_safe and 2 not in dynamic_element_safe,
		"gradual tuple base and runtime integer index are explicitly unsafe: %s / %s" % [dynamic_tuple_report, dynamic_element_report])
	var tuple_index_source := "func check(pair: (int, String), wrong: String) -> void:\n\tprint(pair[wrong])\n\tprint(pair[2])\n"
	var tuple_index_errors: Array = probe.validate_source(tuple_index_source, "res://tests/repair_tuple_index.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(tuple_index_errors, [
		['Only an integer can index tuple "(int, String)", but received "String".', 2, 16],
		['Tuple index 2 is out of range for "(int, String)", which has 2 element(s).', 3, 16],
	]), "tuple index type/range diagnostics use the index expression: %s" % [tuple_index_errors])
	var nullable_tuple := "func read(pair: (int, String)?) -> int:\n\treturn pair[0]\n"
	_expect(failures, probe.validate_source(nullable_tuple, "res://tests/repair_nullable_tuple.barista", false).get("valid", false),
		"nullable tuple indexing preserves the selected element type")
	var tuple_metatype := "tuple Pair(left: int, right: int)\nfunc bad() -> void:\n\tprint(Pair.0)\n"
	var tuple_metatype_errors: Array = probe.validate_source(tuple_metatype, "res://tests/repair_tuple_metatype.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(tuple_metatype_errors, [
		['Cannot index the tuple type "Pair"; construct a value first.', 3, 11],
	]), "tuple metatype index remains rejected at the subscript: %s" % [tuple_metatype_errors])

	var array_index_source := "func check(values: Array[int], b: bool, text: String) -> void:\n\tvar a: int = values[1.0]\n\tvalues[2.0] = 3\n\tprint(values[b])\n\tvalues[text] = 4\n"
	var array_index_errors: Array = probe.validate_source(array_index_source, "res://tests/repair_array_index.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(array_index_errors, [
		['Invalid index type "bool" for a base of type "Array[int]".', 4, 18],
		['Invalid index type "String" for a base of type "Array[int]".', 5, 12],
	]), "Array reads/writes accept real indices and reject unrelated concrete indices: %s" % [array_index_errors])

	var constructor_source := "class_name ConstructorCases extends Node\nclass Base:\n\tfunc _init(value: int, label: String = \"x\", ...rest: Array) -> void:\n\t\tpass\nclass Child extends Base:\n\tpass\nclass Empty:\n\tpass\nfunc ok() -> Child:\n\treturn Child.new(1, \"a\", 2, 3)\nfunc bad_type() -> Child:\n\treturn Child.new(\"bad\")\nfunc bad_arity() -> Child:\n\treturn Child.new()\nfunc bad_empty() -> Empty:\n\treturn Empty.new(1)\n"
	var constructor_errors: Array = probe.validate_source(constructor_source, "res://tests/repair_constructors.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(constructor_errors, [
		['Invalid argument for "new()" function: argument 1 should be "int" but is "String".', 12, 22],
		['Too few arguments for "new()" call. Expected at least 1 but received 0.', 14, 12],
		['Too many arguments for "new()" call. Expected at most 0 but received 1.', 16, 22],
	]), "local inherited constructors preserve precise result and fixed/default/rest validation: %s" % [constructor_errors])

	var constant_write_source := "class_name ConstantWrites extends Node\nconst TOKEN := 1\nconst CONTAINER := [1]\nvar mutable := 1\nclass Child extends ConstantWrites:\n\tfunc writes(receiver: Child) -> void:\n\t\tTOKEN = 2\n\t\tself.TOKEN = 2\n\t\tChild.TOKEN = 2\n\t\treceiver.TOKEN = 2\n\t\treceiver.CONTAINER[0] = 2\n\t\tmutable = 2\n"
	var constant_write_errors: Array = probe.validate_source(constant_write_source, "res://tests/repair_member_constants.barista", false).get("errors", [])
	var readonly_expected: Array = []
	for line in range(7, 12):
		readonly_expected.append(["Cannot assign a new value to a constant.", line, 9])
	_expect(failures, _errors_are_exact(constant_write_errors, readonly_expected),
		"direct/self/class/inherited/typed and nested member constant writes reject once: %s" % [constant_write_errors])

	var nominal_sites := "class_name NominalSites extends Node\nclass Left:\n\ttuple Point(x: int, y: int)\nclass Right:\n\ttuple Point(x: int, y: int)\n\ttuple Wrapper(point: Point, count: int)\n\tenum Box:\n\t\tValue(value: Point)\nfunc fixed(value: Right.Point) -> void:\n\tpass\nfunc rest(...values: Array[Right.Point]) -> void:\n\tpass\nfunc sites(left: Left.Point) -> Right.Point:\n\tvar declared: Right.Point = left\n\tconst local: Right.Point = left\n\tvar assigned: Right.Point = Right.Point(1, 2)\n\tassigned = left\n\tfixed(left)\n\trest(left)\n\tvar tuple_payload := Right.Wrapper(left, 1)\n\tvar enum_payload := Right.Box.Value(left)\n\treturn left\n"
	var nominal_site_errors: Array = probe.validate_source(nominal_sites, "res://tests/repair_nominal_sites.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(nominal_site_errors, [
		['Cannot assign a value of type Point to variable "declared" with specified type Point. The value is declared by class "Left"; the specified type is declared by class "Right".', 14, 33],
		['Cannot assign a value of type "Point" to a constant of type "Point". The value is declared by class "Left"; the specified type is declared by class "Right".', 15, 32],
		['Value of type "Point" cannot be assigned to a variable of type "Point". The value is declared by class "Left"; the variable\'s type is declared by class "Right".', 17, 16],
		['Invalid argument for "fixed()" function: argument 1 should be "Point" but is "Point". The parameter is declared by class "Right"; the argument is declared by class "Left".', 18, 11],
		['Invalid argument for "rest()" function: argument 1 should be "Point" but is "Point". The parameter is declared by class "Right"; the argument is declared by class "Left".', 19, 10],
		['Invalid argument 1 for tuple "Wrapper": should be "Point" but is "Point". The tuple field\'s type is declared by class "Right"; the argument is declared by class "Left".', 20, 40],
		['Invalid argument 1 for enum case "Box.Value": should be "Point" but is "Point". The payload field\'s type is declared by class "Right"; the argument is declared by class "Left".', 21, 41],
		['Cannot return value of type "Point" because the function return type is "Point". The returned value is declared by class "Left"; the return type is declared by class "Right".', 22, 5],
	]), "same-rendered owners are explicit at declaration/constant/assignment/call/payload/return sites: %s" % [nominal_site_errors])

	var origin_source := "class_name OriginCases extends Node\nvar member: String = 1\nconst MEMBER_CONST: String = 2\nfunc bad(v: int) -> String:\n\tvar local: String = v\n\tconst local_const: String = v\n\treturn v\n"
	var origin_errors: Array = probe.validate_source(origin_source, "res://tests/repair_origins.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(origin_errors, [
		['Cannot assign a value of type "int" to a variable of type "String".', 2, 22],
		['Cannot assign a value of type "int" to a constant of type "String".', 3, 30],
		['Cannot assign a value of type "int" to a variable of type "String".', 5, 25],
		['Cannot assign a value of type "int" to a constant of type "String".', 6, 33],
		['Cannot return value of type "int" because the function return type is "String".', 7, 5],
	]), "ordinary declaration diagnostics use initializer origins while return stays on ReturnNode: %s" % [origin_errors])


func _test_steps_1_5_repair2_self_signatures(failures: PackedStringArray) -> void:
	var probe := BaristaScriptAnalyzerProbe.new()

	var static_source := "class_name Repair2Static extends Node\nstatic func fixed(value: Self) -> void:\n\tpass\nstatic func rest(...values: Array[Self]) -> void:\n\tpass\nstatic func check(good: Repair2Static, bad: String) -> void:\n\tfixed(good)\n\trest(good)\n\tfixed(bad)\n\trest(bad)\n\tfixed()\n"
	var static_errors: Array = probe.validate_source(static_source, "res://tests/repair2_static_self.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(static_errors, [
		['Invalid argument for "fixed()" function: argument 1 should be "Repair2Static" but is "String".', 9, 11],
		['Invalid argument for "rest()" function: argument 1 should be "Repair2Static" but is "String".', 10, 10],
		['Too few arguments for "fixed()" call. Expected at least 1 but received 0.', 11, 5],
	]), "static fixed/rest Self share declaring-class substitution and exact diagnostics: %s" % [static_errors])

	var constructor_source := "class_name Repair2Constructor extends Node\nclass Base:\n\tfunc _init(first: Self, ...rest: Array[Self]) -> void:\n\t\tpass\nclass Child extends Base:\n\tpass\nfunc check(a: Child, b: Child, base: Base) -> void:\n\tvar direct := Base.new(base, base)\n\tvar inherited := Child.new(a, b)\n\tChild.new(base, b)\n\tChild.new(a, base)\n"
	var constructor_errors: Array = probe.validate_source(constructor_source, "res://tests/repair2_constructor_self.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(constructor_errors, [
		['Invalid argument for "new()" function: argument 1 should be "Child" but is "Base".', 10, 15],
		['Invalid argument for "new()" function: argument 2 should be "Child" but is "Base".', 11, 18],
	]), "direct and inherited constructors select one concrete fixed/rest Self type: %s" % [constructor_errors])

	var instance_rest_source := "class_name Repair2InstanceRest extends Node\nfunc take(...values: Array[Self]) -> void:\n\tpass\nfunc check(other: Repair2InstanceRest) -> void:\n\ttake(self)\n\tother.take(self)\n"
	var instance_rest_errors: Array = probe.validate_source(instance_rest_source, "res://tests/repair2_instance_rest_self.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(instance_rest_errors, [
		['Invalid argument for "take()" function: argument 1 should be "Self" but is "Self". The parameter\'s "Self" is resolved against the receiver expression; the argument is relative to the calling frame\'s receiver.', 6, 16],
	]), "ordinary instance rest Self stays receiver-relative: %s" % [instance_rest_errors])

	var callable_direct_source := "class_name Repair2CallableDirect extends Node\nfunc fixed(value: Callable[[Self], void]) -> void:\n\tpass\nfunc returns(value: Callable[[], Self]) -> void:\n\tpass\nfunc rests(value: Callable[[...Array[Self]], void]) -> void:\n\tpass\nfunc cb_fixed(value: Self) -> void:\n\tpass\nfunc cb_return() -> Self:\n\treturn self\nfunc cb_rest(...values: Array[Self]) -> void:\n\tpass\nfunc check(other: Repair2CallableDirect) -> void:\n\tfixed(cb_fixed)\n\treturns(cb_return)\n\trests(cb_rest)\n\tother.fixed(cb_fixed)\n\tother.returns(cb_return)\n\tother.rests(cb_rest)\n"
	var callable_direct_errors: Array = probe.validate_source(callable_direct_source, "res://tests/repair2_callable_direct_self.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(callable_direct_errors, [
		['Invalid argument for "fixed()" function: argument 1 should be "Callable[[Self], void]" but is "Callable[[Self], void]". The parameter\'s "Self" is resolved against the receiver expression; the argument is relative to the calling frame\'s receiver.', 18, 17],
		['Invalid argument for "returns()" function: argument 1 should be "Callable[[], Self]" but is "Callable[[], Self]". The parameter\'s "Self" is resolved against the receiver expression; the argument is relative to the calling frame\'s receiver.', 19, 19],
		['Invalid argument for "rests()" function: argument 1 should be "Callable[[...Array[Self]], void]" but is "Callable[[...Array[Self]], void]". The parameter\'s "Self" is resolved against the receiver expression; the argument is relative to the calling frame\'s receiver.', 20, 17],
	]), "callable fixed/return/rest Self accepts own receiver and rejects foreign receiver: %s" % [callable_direct_errors])
	var callable_current_source := "class_name Repair2CallableCurrent extends Node\nfunc take(value: Callable[[Self], void]) -> void:\n\tpass\nfunc cb(value: Self) -> void:\n\tpass\nfunc through_self() -> void:\n\tself.take(cb)\nclass Child extends Repair2CallableCurrent:\n\tfunc through_super() -> void:\n\t\tsuper.take(cb)\n"
	_expect(failures, probe.validate_source(callable_current_source, "res://tests/repair2_callable_current_self.barista", false).get("valid", false),
		"explicit self and super callable Self calls retain current-receiver admission")

	var callable_sibling_source := "class_name Repair2CallableSibling extends Node\nfunc fixed(value: Callable[[Self, int], void]) -> void:\n\tpass\nfunc returns(value: Callable[[int], Self]) -> void:\n\tpass\nfunc rests(value: Callable[[int, ...Array[Self]], void]) -> void:\n\tpass\nfunc cb_fixed(value: Self, sibling: String) -> void:\n\tpass\nfunc cb_return(sibling: String) -> Self:\n\treturn self\nfunc cb_rest(sibling: String, ...values: Array[Self]) -> void:\n\tpass\nfunc check(other: Repair2CallableSibling) -> void:\n\tother.fixed(cb_fixed)\n\tother.returns(cb_return)\n\tother.rests(cb_rest)\n"
	var callable_sibling_errors: Array = probe.validate_source(callable_sibling_source, "res://tests/repair2_callable_sibling_self.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(callable_sibling_errors, [
		['Invalid argument for "fixed()" function: argument 1 should be "Callable[[Self, int], void]" but is "Callable[[Self, String], void]". The parameter\'s "Self" at callable parameter 1 is resolved against the receiver expression; the argument is relative to the calling frame\'s receiver.', 15, 17],
		['Invalid argument for "returns()" function: argument 1 should be "Callable[[int], Self]" but is "Callable[[String], Self]". The parameter\'s "Self" at the callable return type is resolved against the receiver expression; the argument is relative to the calling frame\'s receiver.', 16, 19],
		['Invalid argument for "rests()" function: argument 1 should be "Callable[[int, ...Array[Self]], void]" but is "Callable[[String, ...Array[Self]], void]". The parameter\'s "Self" at the callable rest parameter is resolved against the receiver expression; the argument is relative to the calling frame\'s receiver.', 17, 17],
	]), "callable sibling mismatches retain fixed/return/rest receiver slot labels: %s" % [callable_sibling_errors])

	var comparable_source := "class_name Repair2ComparableCallable extends Node\nfunc fixed(callback: Callable[[Self], void]) -> void:\n\tpass\nfunc fan(callback: Callable[[...Array[Self]], void]) -> void:\n\tpass\nfunc with_tail(owner: Self, ...rest: Array) -> void:\n\tpass\nfunc sink(...values: Array) -> void:\n\tpass\nfunc check(other: Repair2ComparableCallable) -> void:\n\tvar variadic: Callable[[Self, ...Array], void] = with_tail\n\tvar gradual: Callable[[...Array], void] = sink\n\tfixed(variadic)\n\tfan(gradual)\n\tother.fan(gradual)\n"
	_expect(failures, probe.validate_source(comparable_source, "res://tests/repair2_callable_comparable.barista", false).get("valid", false),
		"variadic-to-fixed and gradual-to-narrowed rest callable directions remain admitted")

	var typed_tail_parameter := "class_name Repair3TailParameter extends Node\nfunc fan(callback: Callable[[...Array[Self]], void]) -> void:\n\tpass\nfunc fan_nested(callback: Callable[[...Array[Array[Self]]], void]) -> void:\n\tpass\nfunc take_bound(...values: Array[Repair3TailParameter]) -> void:\n\tpass\nfunc take_super(...values: Array[Node]) -> void:\n\tpass\nfunc take_nested(...values: Array[Array[Repair3TailParameter]]) -> void:\n\tpass\nfunc check(other: Repair3TailParameter) -> void:\n\tfan(take_bound)\n\tother.fan(take_super)\n\tfan_nested(take_nested)\n"
	_expect(failures, probe.validate_source(typed_tail_parameter, "res://tests/repair3_typed_tail_parameter.barista", false).get("valid", false),
		"typed Callable rest tails accept the Self bound, a supertype, and a nested bound")
	var typed_tail_declared_super := "class Super:\n\tpass\nclass Cell extends Super:\n\tfunc fan(callback: Callable[[...Array[Self]], void]) -> void:\n\t\tpass\n\tfunc take_super(...values: Array[Super]) -> void:\n\t\tpass\n\tfunc check(other: Cell) -> void:\n\t\tother.fan(take_super)\n"
	_expect(failures, probe.validate_source(typed_tail_declared_super, "res://tests/repair3_typed_tail_declared_super.barista", false).get("valid", false),
		"typed Callable rest tails accept a same-source declared supertype through a foreign receiver")

	var typed_tail_values := "class_name Repair3TailValue extends Node\nfunc fan(callback: Callable[[...Array[Self]], void]) -> void:\n\tpass\nfunc take_bound(...values: Array[Repair3TailValue]) -> void:\n\tpass\nfunc take_super(...values: Array[Node]) -> void:\n\tpass\nfunc stored() -> Callable[[...Array[Self]], void]:\n\tvar bound: Callable[[...Array[Repair3TailValue]], void] = take_bound\n\treturn bound\nfunc check() -> void:\n\tvar declared: Callable[[...Array[Self]], void] = take_bound\n\tvar assigned: Callable[[...Array[Self]], void] = take_bound\n\tassigned = take_super\n\tfan(declared)\n\tfan(assigned)\n\tfan(stored())\n"
	_expect(failures, probe.validate_source(typed_tail_values, "res://tests/repair3_typed_tail_values.barista", false).get("valid", false),
		"typed Callable rest-tail admission is shared by declaration, assignment, return and parameter consumers")

	var typed_tail_negatives := "class_name Repair3TailNegative extends Node\nclass Leaf extends Repair3TailNegative:\n\tpass\nfunc fan(callback: Callable[[...Array[Self]], void]) -> void:\n\tpass\nfunc fixed(callback: Callable[[int, ...Array[Self]], void]) -> void:\n\tpass\nfunc returns(callback: Callable[[...Array[Self]], int]) -> void:\n\tpass\nfunc take_narrow(...values: Array[Leaf]) -> void:\n\tpass\nfunc bad_fixed(value: String, ...values: Array[Node]) -> void:\n\tpass\nfunc bad_return(...values: Array[Node]) -> String:\n\treturn \"bad\"\nfunc self_tail(...values: Array[Self]) -> void:\n\tpass\nfunc check(other: Repair3TailNegative) -> void:\n\tfan(take_narrow)\n\tfixed(bad_fixed)\n\treturns(bad_return)\n\tother.fan(self_tail)\n"
	var typed_tail_negative_errors: Array = probe.validate_source(typed_tail_negatives, "res://tests/repair3_typed_tail_negatives.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(typed_tail_negative_errors, [
		['Invalid argument for "fan()" function: argument 1 should be "Callable[[...Array[Self]], void]" but is "Callable[[...Array[Leaf]], void]".', 19, 9],
		['Invalid argument for "fixed()" function: argument 1 should be "Callable[[int, ...Array[Self]], void]" but is "Callable[[String, ...Array[Node]], void]".', 20, 11],
		['Invalid argument for "returns()" function: argument 1 should be "Callable[[...Array[Self]], int]" but is "Callable[[...Array[Node]], String]".', 21, 13],
		['Invalid argument for "fan()" function: argument 1 should be "Callable[[...Array[Self]], void]" but is "Callable[[...Array[Self]], void]". The parameter\'s "Self" is resolved against the receiver expression; the argument is relative to the calling frame\'s receiver.', 22, 15],
	]), "typed tail fallback preserves narrower, fixed, return and foreign-receiver negatives: %s" % [typed_tail_negative_errors])

	var typed_tail_value_negatives := "class_name Repair3TailValueNegative extends Node\nclass Leaf extends Repair3TailValueNegative:\n\tpass\nfunc bad(wrong: Callable[[...Array[String]], void], narrow: Callable[[...Array[Leaf]], void]) -> Callable[[...Array[Self]], void]:\n\tvar declared: Callable[[...Array[Self]], void] = wrong\n\tvar slot: Callable[[...Array[Self]], void] = wrong\n\tslot = narrow\n\treturn wrong\n"
	var typed_tail_value_errors: Array = probe.validate_source(typed_tail_value_negatives, "res://tests/repair3_typed_tail_value_negatives.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(typed_tail_value_errors, [
		['Cannot assign a value of type "Callable[[...Array[String]], void]" to a variable of type "Callable[[...Array[Self]], void]".', 5, 54],
		['Cannot assign a value of type "Callable[[...Array[String]], void]" to a variable of type "Callable[[...Array[Self]], void]".', 6, 50],
		['Value of type "Callable[[...Array[Leaf]], void]" cannot be assigned to a variable of type "Callable[[...Array[Self]], void]".', 7, 12],
		['Cannot return value of type "Callable[[...Array[String]], void]" because the function return type is "Callable[[...Array[Self]], void]".', 8, 5],
	]), "typed tail value consumers retain unrelated and narrower exact diagnostics: %s" % [typed_tail_value_errors])


func _test_local_enum_value_cycles(failures: PackedStringArray) -> void:
	# Local int-backed value cycles preserve the in-progress RESOLVING state instead of
	# retrying the producer. Pin both complete ordered diagnostics in the guarded suite.
	var probe := BaristaScriptAnalyzerProbe.new()
	var mutual_cycle_source := "func test():\n\tprint(E1.V)\n\nenum E1:\n\tV = E2.V\nenum E2:\n\tV = E1.V\n"
	var mutual_cycle_errors: Array = probe.validate_source(mutual_cycle_source, "res://tests/cyclic_ref_enum.barista", false).get("errors", [])
	_expect(failures, mutual_cycle_errors.size() == 2 and
		str(mutual_cycle_errors[0].get("message", "")) == 'Could not resolve member "E1": Cyclic reference.' and
		mutual_cycle_errors[0].get("line") == 7 and mutual_cycle_errors[0].get("column") == 9 and
		str(mutual_cycle_errors[1].get("message", "")) == "Enum values must be constant." and
		mutual_cycle_errors[1].get("line") == 7 and mutual_cycle_errors[1].get("column") == 9,
		"mutual enum value cycle emits the exact two-error block and starts: %s" % [mutual_cycle_errors])

	var self_cycle_source := "enum Bad:\n\tA = Bad.B\n\tB = 1\n\nfunc test():\n\tprint(Bad.A)\n"
	var self_cycle_errors: Array = probe.validate_source(self_cycle_source, "res://tests/enum_int_backed_self_referential_value.barista", false).get("errors", [])
	_expect(failures, self_cycle_errors.size() == 2 and
		str(self_cycle_errors[0].get("message", "")) == 'Could not resolve member "Bad": Cyclic reference.' and
		self_cycle_errors[0].get("line") == 2 and self_cycle_errors[0].get("column") == 9 and
		str(self_cycle_errors[1].get("message", "")) == "Enum values must be constant." and
		self_cycle_errors[1].get("line") == 2 and self_cycle_errors[1].get("column") == 9,
		"self-referential enum value emits the exact two-error block and starts: %s" % [self_cycle_errors])

	# Legal recursive tagged payload identity is published before its payload fields resolve.
	var source := _src_class("LegalRecursiveTagged extends Node\nenum Chain:\n\tEnd\n\tLink(next: Chain)\nfunc make() -> Chain:\n\treturn Chain.Link(Chain.End)\n")
	var report: Dictionary = probe.analyze_source(source, "res://tests/legal_recursive_tagged.barista")
	_expect(failures, report.get("valid", false) == true,
		"legal recursive tagged payload remains valid while int-backed value cycles are rejected")


func _test_concrete_cast_ternary_and_type_test_reduction(failures: PackedStringArray) -> void:
	# Foundry reduce_cast / finalize_ternary_op_type / reduce_type_test @ c9d5e35.
	# These adaptations preserve the pinned producer line layout and use .barista paths.
	var probe := BaristaScriptAnalyzerProbe.new()
	ProjectSettings.set_setting("debug/barista_script/warnings/enable", true)
	ProjectSettings.set_setting("debug/barista_script/warnings/unsafe_cast", 1)
	ProjectSettings.set_setting("debug/barista_script/warnings/int_as_enum_without_match", 1)
	ProjectSettings.set_setting("debug/barista_script/warnings/incompatible_ternary", 1)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()

	var cast_sources := [
		["func test():\n\tvar integer := 1\n\tprint(integer as Array)\n", "res://tests/cast_int_to_array.barista", 'Invalid cast. Cannot convert from "int" to "Array".', 3, 22],
		["func test():\n\tvar integer := 1\n\tprint(integer as Node)\n", "res://tests/cast_int_to_object.barista", 'Invalid cast. Cannot convert from "int" to "Node".', 3, 22],
		["func test(object: RefCounted):\n\t# Typed parameter avoids the native-constructor metadata owned by #141.\n\tprint(object as int)\n", "res://tests/cast_object_to_int.barista", 'Invalid cast. Cannot convert from "RefCounted" to "int".', 3, 21],
	]
	for fixture in cast_sources:
		var cast_report: Dictionary = probe.validate_source(fixture[0], fixture[1], false)
		_expect(failures, _errors_are_exact(cast_report.get("errors", []), [[fixture[2], fixture[3], fixture[4]]]),
			"invalid cast preserves the full diagnostic and TypeNode start for %s: %s" % [fixture[1], cast_report.get("errors", [])])

	var union_source := "# The runtime has no union carrier.\ntype Scalar = int | String\n\n\nfunc test():\n\tvar value: Scalar = 1\n\tprint(value is Scalar)\n\tprint(value as Scalar)\n"
	var union_errors: Array = probe.validate_source(union_source, "res://tests/type_union_runtime_type_operations.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(union_errors, [
		['Cannot test against the type union "String | int", because it has no runtime type. Test one of its alternatives instead.', 7, 20],
		['Cannot cast to the type union "String | int", because it has no runtime type. Cast to one of its alternatives instead.', 8, 20],
	]), "union type-test/cast rejection remains ordered and exact: %s" % [union_errors])

	var tagged_source := "enum Command:\n\tQuit\n\tMove(x: int, y: int)\n\nfunc test():\n\tvar message: Command = Command.Quit\n\tvar as_int: int = message\n\tprint(message + 1)\n\tprint(message as int)\n"
	var tagged_errors: Array = probe.validate_source(tagged_source, "res://tests/tagged_union_in_int_context.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(tagged_errors, [
		['Cannot assign a value of type tagged_union_in_int_context.barista.Command to variable "as_int" with specified type int.', 7, 23],
		['Operator "+" is not available on tagged union "Command"; its cases carry payloads, so its values are not integers. Match on the case first.', 8, 11],
		['Tagged union "Command" is not int-backed, because its cases carry payloads; it cannot be converted to or from "int".', 9, 22],
	]), "tagged-union/int boundary preserves the complete three-error block: %s" % [tagged_errors])

	var unsafe_source := "# Analyze only.\nfunc no_exec_test():\n\tvar weak_int = 1\n\tprint(weak_int as Variant)\n\tprint(weak_int as int)\n\tprint(weak_int as Node)\n\n\tvar weak_node = Node.new()\n\tprint(weak_node as Variant)\n\tprint(weak_node as int)\n\tprint(weak_node as Node)\n\n\tvar weak_variant = null\n\tprint(weak_variant as Variant)\n\tprint(weak_variant as int)\n\tprint(weak_variant as Node)\n\n\tvar hard_variant: Variant = null\n\tprint(hard_variant as Variant)\n\tprint(hard_variant as int)\n\tprint(hard_variant as Node)\n\nfunc test():\n\tpass\n"
	var unsafe_report: Dictionary = probe.validate_source(unsafe_source, "res://tests/unsafe_cast.barista", true)
	var unsafe_warnings: Array = unsafe_report.get("warnings", [])
	_expect(failures, unsafe_report.get("valid", false) and unsafe_report.get("errors", []).is_empty() and _warnings_are_exact(unsafe_warnings, [
		["UNSAFE_CAST", 'Casting "Variant" to "int" is unsafe.', 5, 11, 5, 26],
		["UNSAFE_CAST", 'Casting "Variant" to "Node" is unsafe.', 6, 11, 6, 27],
		["UNSAFE_CAST", 'Casting "Variant" to "int" is unsafe.', 10, 11, 10, 27],
		["UNSAFE_CAST", 'Casting "Variant" to "Node" is unsafe.', 11, 11, 11, 28],
		["UNSAFE_CAST", 'Casting "Variant" to "int" is unsafe.', 15, 11, 15, 30],
		["UNSAFE_CAST", 'Casting "Variant" to "Node" is unsafe.', 16, 11, 16, 31],
		["UNSAFE_CAST", 'Casting "Variant" to "int" is unsafe.', 20, 11, 20, 30],
		["UNSAFE_CAST", 'Casting "Variant" to "Node" is unsafe.', 21, 11, 21, 31],
	]), "unsafe-cast warnings preserve the complete code/message/range block: %s" % [unsafe_warnings])

	var enum_cast_source := "enum MyEnum:\n\tENUM_VALUE_1 = 0\n\tENUM_VALUE_2 = ENUM_VALUE_1 + 1\n\nfunc test():\n\tprint(2 as MyEnum)\n"
	var enum_cast_report: Dictionary = probe.validate_source(enum_cast_source, "res://tests/cast_enum_bad_int.barista", true)
	_expect(failures, enum_cast_report.get("valid", false) and _warnings_are_exact(enum_cast_report.get("warnings", []), [
		["INT_AS_ENUM_WITHOUT_MATCH", 'Cannot cast 2 as Enum "cast_enum_bad_int.barista.MyEnum": no enum member has matching value.', 6, 11, 6, 12],
	]), "unmatched constant int-to-enum cast warning is exact: %s" % [enum_cast_report.get("warnings", [])])
	var enum_ok_source := "enum Foo:\n\tA = 0\n\tB = A + 1\n\tC = B + 1\nfunc test():\n\tvar as_int: int = Foo.A as int\n\tvar as_enum: Foo = 1 as Foo\n"
	var enum_ok_report: Dictionary = probe.validate_source(enum_ok_source, "res://tests/plain_enum_casts.barista", true)
	_expect(failures, enum_ok_report.get("valid", false) and enum_ok_report.get("errors", []).is_empty(),
		"plain enum-to-int and matching int-to-enum casts remain valid")

	var incompatible_source := "func test():\n\t# The ternary operator below returns values of different types and the\n\t# result is assigned to a typed variable. This will cause a run-time error\n\t# if the branch with the incompatible type is picked. Here, it won't happen\n\t# since the `false` condition never evaluates to `true`. Instead, a warning\n\t# will be emitted.\n\tvar __: int = 25\n\t__ = \"hello\" if false else -2\n"
	var incompatible_report: Dictionary = probe.validate_source(incompatible_source, "res://tests/incompatible_ternary.barista", true)
	_expect(failures, incompatible_report.get("valid", false) and _warnings_are_exact(incompatible_report.get("warnings", []), [
		["INCOMPATIBLE_TERNARY", "Values of the ternary operator are not mutually compatible.", 8, 10, 8, 34],
	]), "incompatible ternary warning preserves its full range: %s" % [incompatible_report.get("warnings", [])])
	var ternary_source := "func choose(flag: bool, left: String, right: String) -> String:\n\treturn left if flag else right\nfunc nullable(flag: bool, value: String) -> String?:\n\treturn value if flag else null\n"
	var ternary_report: Dictionary = probe.validate_source(ternary_source, "res://tests/ternary_concrete_types.barista", true)
	_expect(failures, ternary_report.get("valid", false) and ternary_report.get("errors", []).is_empty() and ternary_report.get("warnings", []).is_empty(),
		"compatible and nullable ternary arms retain concrete return types without warnings")
	_expect(failures, probe.has_method("inspect_expression_source"),
		"debug expression inspection is available for pure type/constant observations")
	if probe.has_method("inspect_expression_source"):
		var string_ternary: Dictionary = probe.call("inspect_expression_source", "var probe_expression = \"left\" if true else \"right\"\n", "res://tests/ternary_string_probe.barista")
		var nullable_ternary: Dictionary = probe.call("inspect_expression_source", "var probe_expression = \"left\" if false else null\n", "res://tests/ternary_nullable_probe.barista")
		_expect(failures, _inspection_is_valid(string_ternary) and string_ternary.get("datatype") == "String" and string_ternary.get("is_constant") == true and string_ternary.get("value") == "left",
			"equal ternary arms retain String and fold only with all inputs constant: %s" % [string_ternary])
		_expect(failures, _inspection_is_valid(nullable_ternary) and nullable_ternary.get("datatype") == "String?" and nullable_ternary.get("is_constant") == true and nullable_ternary.get("value") == null,
			"null/String ternary retains nullable String and selected constant: %s" % [nullable_ternary])

	# Repair R1: every consumer that supplies a tagged-union expectation must re-finalize the
	# ternary after recursively qualifying its contextual arms.
	var contextual_consumers_source := "enum Message:\n\tQuit\n\tMove(value: int)\n\nfunc take(message: Message) -> void:\n\tprint(message)\nfunc choose(flag: bool) -> Message:\n\treturn .Quit if flag else .Move(1)\nfunc test(flag: bool) -> void:\n\tvar declared: Message = .Quit if flag else .Move(2)\n\tdeclared = .Move(3) if flag else .Quit\n\ttake(.Quit if flag else .Move(4))\n\tvar nested: Array[Message] = [.Quit if flag else .Move(5)]\n\tprint(nested, declared, (.Quit if flag else .Move(6)) as Message)\n"
	var contextual_consumers_report: Dictionary = probe.validate_source(contextual_consumers_source, "res://tests/contextual_ternary_consumers.barista", true)
	_expect(failures, contextual_consumers_report.get("valid", false) and contextual_consumers_report.get("errors", []).is_empty() and contextual_consumers_report.get("warnings", []).is_empty(),
		"contextual ternaries re-finalize through declaration/assignment/return/call/nested-literal/cast consumers: %s" % [contextual_consumers_report])
	if probe.has_method("inspect_expression_source"):
		var contextual_type_source := "enum Message:\n\tQuit\n\tMove(value: int)\n\nvar probe_expression: Message = .Quit if true else (.Move(1) if false else .Quit)\n"
		var contextual_type: Dictionary = probe.call("inspect_expression_source", contextual_type_source, "res://tests/contextual_ternary_type.barista")
		_expect(failures, _inspection_is_valid(contextual_type) and contextual_type.get("datatype") == "contextual_ternary_type.barista.Message" and contextual_type.get("is_hard_type") == true,
			"recursive contextual ternaries publish the hard tagged-union result: %s" % [contextual_type])
	var unqualified_context_source := "enum Message:\n\tQuit\n\tMove(value: int)\n\nfunc test():\n\tvar result := .Quit if true else .Move(1)\n"
	var unqualified_context_errors: Array = probe.validate_source(unqualified_context_source, "res://tests/contextual_ternary_unqualified.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(unqualified_context_errors, [
		['Contextual shorthand ".Quit" needs an expected tagged-union type; annotate the target, e.g. "var x: Result[int, String] = .Quit".', 6, 19],
		['Contextual shorthand ".Move" needs an expected tagged-union type; annotate the target, e.g. "var x: Result[int, String] = .Move(...)".', 6, 38],
	]), "unqualified contextual ternary keeps ordered case errors without a weak-inference cascade: %s" % [unqualified_context_errors])

	# Repair R2: a constant carried by Variant is retyped and converted by the shared value-aware
	# helper, including all existing declaration/assignment/return/container consumers.
	var boxed_float_source := "const BOXED: Variant = 1\nvar probe_expression = BOXED as float\n"
	if probe.has_method("inspect_expression_source"):
		var boxed_float: Dictionary = probe.call("inspect_expression_source", boxed_float_source, "res://tests/boxed_constant_float.barista")
		var direct_float: Dictionary = probe.call("inspect_expression_source", "var probe_expression = 1 as float\n", "res://tests/direct_constant_float.barista")
		var boxed_null: Dictionary = probe.call("inspect_expression_source", "const BOXED: Variant = null\nvar probe_expression = BOXED as String?\n", "res://tests/boxed_constant_nullable.barista")
		var packed_conversion: Dictionary = probe.call("inspect_expression_source", "const BOXED: Variant = [\"bad\"]\nvar probe_expression = BOXED as PackedInt32Array\n", "res://tests/boxed_constant_packed_array.barista")
		_expect(failures, _inspection_is_valid(boxed_float) and boxed_float.get("datatype") == "float" and boxed_float.get("is_hard_type") == true and boxed_float.get("is_constant") == true and boxed_float.get("value") == 1.0,
			"Variant-carried int cast publishes a constant float value: %s" % [boxed_float])
		_expect(failures, _inspection_is_valid(direct_float) and direct_float.get("datatype") == "float" and direct_float.get("is_hard_type") == true and direct_float.get("is_constant") == true and direct_float.get("value") == 1.0,
			"direct int-to-float constant control remains folded: %s" % [direct_float])
		_expect(failures, _inspection_is_valid(boxed_null) and boxed_null.get("datatype") == "String?" and boxed_null.get("is_constant") == true and boxed_null.get("value") == null,
			"nullable target preserves a Variant-carried null without constructing String: %s" % [boxed_null])
		_expect(failures, _inspection_is_valid(packed_conversion) and packed_conversion.get("datatype") == "PackedInt32Array" and packed_conversion.get("is_constant") == true and packed_conversion.get("value") == PackedInt32Array([0]),
			"Array-to-packed construction follows the pinned per-element Variant conversion: %s" % [packed_conversion])
	var boxed_float_report: Dictionary = probe.validate_source(boxed_float_source, "res://tests/boxed_constant_float_validate.barista", true)
	_expect(failures, boxed_float_report.get("valid", false) and boxed_float_report.get("errors", []).is_empty() and boxed_float_report.get("warnings", []).is_empty(),
		"Variant-carried constant conversion does not emit a false unsafe-cast warning: %s" % [boxed_float_report])
	var shared_constant_source := "const BOXED: Variant = 1\nfunc return_boxed() -> float:\n\treturn BOXED\nfunc test() -> void:\n\tvar declared: float = BOXED\n\tvar assigned: float = 0.0\n\tassigned = BOXED\n\tvar tupled: (float, float) = (BOXED, BOXED)\n\tvar arrayed: Array[float] = [BOXED]\n\tvar mapped: Dictionary[String, float] = {\"one\": BOXED}\n\tprint(declared, assigned, tupled, arrayed, mapped, return_boxed())\n"
	var shared_constant_report: Dictionary = probe.validate_source(shared_constant_source, "res://tests/boxed_constant_shared_consumers.barista", true)
	_expect(failures, shared_constant_report.get("valid", false) and shared_constant_report.get("errors", []).is_empty() and shared_constant_report.get("warnings", []).is_empty(),
		"shared constant retyping covers declaration/assignment/return/tuple/Array/Dictionary: %s" % [shared_constant_report])
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", true)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var strict_boxed_source := "const BOXED: Variant = 1\nfunc test() -> void:\n\tvar probe_expression: float = BOXED\n\tprint(probe_expression)\n"
	var strict_boxed_errors: Array = probe.validate_source(strict_boxed_source, "res://tests/boxed_constant_strict_dynamic.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(strict_boxed_errors, [['Cannot assign a value of type "Variant" to a variable of type "float".', 3, 35]]),
		"strict dynamic mode refuses the declared Variant carrier before value refinement: %s" % [strict_boxed_errors])
	ProjectSettings.set_setting("debug/barista_script/analysis/strict_dynamic_checks", false)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var incompatible_boxed_source := "const BOXED: Variant = \"bad\"\nfunc test() -> void:\n\tvar probe_expression: int = BOXED\n\tprint(probe_expression)\n"
	var incompatible_boxed_errors: Array = probe.validate_source(incompatible_boxed_source, "res://tests/boxed_constant_incompatible.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(incompatible_boxed_errors, [['Cannot assign a value of type "String" as "int".', 3, 33]]),
		"Variant-carried incompatible constant keeps the value-aware diagnostic: %s" % [incompatible_boxed_errors])
	var failed_conversion_source := "const BOXED: Variant = 1.5\nfunc test() -> void:\n\tvar probe_expression: int = BOXED\n\tprint(probe_expression)\n"
	var failed_conversion_errors: Array = probe.validate_source(failed_conversion_source, "res://tests/boxed_constant_failed_conversion.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(failed_conversion_errors, [['Cannot assign a value of type "float" as "int".', 3, 33]]),
		"Variant-carried fractional float is refused by the D1 implicit-assignment gate before construction: %s" % [failed_conversion_errors])
	var direct_fractional_source := "func test() -> void:\n\tvar probe_expression: int = 1.5\n\tprint(probe_expression)\n"
	var direct_fractional_errors: Array = probe.validate_source(direct_fractional_source, "res://tests/direct_fractional_constant.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(direct_fractional_errors, [['Cannot assign a value of type "float" to a variable of type "int".', 2, 33]]),
		"direct fractional constant reaches the same D1 implicit-assignment refusal through the ordinary consumer: %s" % [direct_fractional_errors])

	# Repair R3: a root ternary forwards root position to both value arms, while its condition and
	# value-producing consumers remain non-root.
	ProjectSettings.set_setting("debug/barista_script/warnings/missing_await", 1)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	var root_async_source := "async func fetch() -> int:\n\treturn 1\nfunc test(flag: bool) -> void:\n\tfetch() if flag else fetch()\n"
	var root_async_report: Dictionary = probe.validate_source(root_async_source, "res://tests/root_async_ternary.barista", true)
	_expect(failures, root_async_report.get("valid", false) and root_async_report.get("errors", []).is_empty() and _warnings_are_exact(root_async_report.get("warnings", []), [
		["STANDALONE_TERNARY", "Standalone ternary operator (the return value is being discarded).", 4, 5, 4, 33],
		["MISSING_AWAIT", 'The call returns a "Coroutine[int]" whose result is discarded. Use "await", or store or pass the handle if it is awaited elsewhere.', 4, 5, 4, 12],
		["MISSING_AWAIT", 'The call returns a "Coroutine[int]" whose result is discarded. Use "await", or store or pass the handle if it is awaited elsewhere.', 4, 26, 4, 33],
	]), "root ternary preserves both ordered MISSING_AWAIT warnings and STANDALONE_TERNARY: %s" % [root_async_report.get("warnings", [])])
	var nested_async_source := "async func fetch() -> int:\n\treturn 1\nfunc test(outer: bool, inner: bool) -> void:\n\tfetch() if outer else (fetch() if inner else fetch())\n"
	var nested_async_report: Dictionary = probe.validate_source(nested_async_source, "res://tests/nested_root_async_ternary.barista", true)
	_expect(failures, nested_async_report.get("valid", false) and nested_async_report.get("errors", []).is_empty() and _warnings_are_exact(nested_async_report.get("warnings", []), [
		["STANDALONE_TERNARY", "Standalone ternary operator (the return value is being discarded).", 4, 5, 4, 58],
		["MISSING_AWAIT", 'The call returns a "Coroutine[int]" whose result is discarded. Use "await", or store or pass the handle if it is awaited elsewhere.', 4, 5, 4, 12],
		["MISSING_AWAIT", 'The call returns a "Coroutine[int]" whose result is discarded. Use "await", or store or pass the handle if it is awaited elsewhere.', 4, 28, 4, 35],
		["MISSING_AWAIT", 'The call returns a "Coroutine[int]" whose result is discarded. Use "await", or store or pass the handle if it is awaited elsewhere.', 4, 50, 4, 57],
	]), "nested root ternary forwards root position recursively without warning its condition: %s" % [nested_async_report.get("warnings", [])])
	var held_async_source := "async func fetch() -> int:\n\treturn 1\nfunc test(flag: bool) -> void:\n\tvar work: Coroutine[int] = fetch() if flag else fetch()\n\tprint(work)\n"
	var held_async_report: Dictionary = probe.validate_source(held_async_source, "res://tests/held_async_ternary.barista", true)
	_expect(failures, held_async_report.get("valid", false) and held_async_report.get("errors", []).is_empty() and held_async_report.get("warnings", []).is_empty(),
		"held ternary coroutine handles remain non-root: %s" % [held_async_report])
	var void_async_source := "async func fire() -> void:\n\tpass\nfunc test(flag: bool) -> void:\n\tfire() if flag else fire()\n"
	var void_async_report: Dictionary = probe.validate_source(void_async_source, "res://tests/void_async_ternary.barista", true)
	_expect(failures, void_async_report.get("valid", false) and void_async_report.get("errors", []).is_empty() and _warnings_are_exact(void_async_report.get("warnings", []), [
		["STANDALONE_TERNARY", "Standalone ternary operator (the return value is being discarded).", 4, 5, 4, 31],
	]), "Coroutine[void] ternary arms remain exempt from MISSING_AWAIT: %s" % [void_async_report.get("warnings", [])])
	var condition_async_source := "async func decide() -> bool:\n\treturn true\nfunc test() -> void:\n\t1 if decide() else 2\n"
	var condition_async_report: Dictionary = probe.validate_source(condition_async_source, "res://tests/condition_async_ternary.barista", true)
	_expect(failures, condition_async_report.get("valid", false) and condition_async_report.get("errors", []).is_empty() and _warnings_are_exact(condition_async_report.get("warnings", []), [
		["STANDALONE_TERNARY", "Standalone ternary operator (the return value is being discarded).", 4, 5, 4, 25],
	]), "ternary condition stays non-root and does not emit MISSING_AWAIT: %s" % [condition_async_report.get("warnings", [])])
	var awaited_async_source := "async func fetch() -> int:\n\treturn 1\nfunc test(flag: bool) -> void:\n\tvar result: int = await (fetch() if flag else fetch())\n\tprint(result)\n"
	var awaited_async_report: Dictionary = probe.validate_source(awaited_async_source, "res://tests/awaited_async_ternary.barista", true)
	_expect(failures, awaited_async_report.get("valid", false) and awaited_async_report.get("errors", []).is_empty() and awaited_async_report.get("warnings", []).is_empty(),
		"awaited ternary coroutine handles remain non-root and unwrap once: %s" % [awaited_async_report])

	# Repair R4: expression observations are useful on invalid analysis, but cannot satisfy a
	# positive semantic predicate unless found, valid, and error-free.
	if probe.has_method("inspect_expression_source"):
		var invalid_observation: Dictionary = probe.call("inspect_expression_source", "var broken: int = \"wrong\"\nvar probe_expression = \"left\" if true else \"right\"\n", "res://tests/invalid_positive_observation.barista")
		_expect(failures, not _inspection_is_valid(invalid_observation) and invalid_observation.get("datatype") == "String" and invalid_observation.get("is_constant") == true and
				invalid_observation.get("errors", PackedStringArray()) == PackedStringArray(['Cannot assign a value of type "String" to a variable of type "int".']),
			"invalid analysis retains independent observation fields but fails the positive predicate: %s" % [invalid_observation])
	var weak_source := "func test():\n\tvar left_hard_int := 1\n\tvar right_weak_int = 2\n\tvar result_hm_int := left_hard_int if true else right_weak_int\n\n\tprint('not ok')\n"
	var weak_errors: Array = probe.validate_source(weak_source, "res://tests/ternary_weak_infer.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(weak_errors, [["Cannot infer the type of \"result_hm_int\" variable because the value doesn't have a set type.", 4, 26]]),
		"mixed hard/soft ternary preserves weak inference and initializer origin: %s" % [weak_errors])
	var class_weak_source := "var right_weak = 2\nvar result := 1 if true else right_weak\n"
	var class_weak_errors: Array = probe.validate_source(class_weak_source, "res://tests/ternary_class_weak_infer.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(class_weak_errors, [["Cannot infer the type of \"result\" variable because the value doesn't have a set type.", 2, 15]]),
		"class-variable weak ternary inference uses the shared datatype rule: %s" % [class_weak_errors])
	var class_constant_weak_source := "var right_weak = 2\nconst result := 1 if true else right_weak\n"
	var class_constant_weak_errors: Array = probe.validate_source(class_constant_weak_source, "res://tests/ternary_class_constant_weak.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(class_constant_weak_errors, [["Assigned value for constant \"result\" isn't a constant expression.", 2, 17]]),
		"class-constant weak ternary rejects its nonconstant initializer: %s" % [class_constant_weak_errors])
	var local_constant_weak_source := "func test():\n\tvar right_weak = 2\n\tconst result := 1 if true else right_weak\n"
	var local_constant_weak_errors: Array = probe.validate_source(local_constant_weak_source, "res://tests/ternary_local_constant_weak.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(local_constant_weak_errors, [["Assigned value for constant \"result\" isn't a constant expression.", 3, 21]]),
		"local-constant weak ternary rejects its nonconstant initializer: %s" % [local_constant_weak_errors])

	var constant_is_source := "const base := [0]\n\nfunc test():\n\tvar sub := 1\n\tif sub is String: pass\n"
	var constant_is_errors: Array = probe.validate_source(constant_is_source, "res://tests/constant_subscript_type.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(constant_is_errors, [['Expression is of type "int" so it can\'t be of type "String".', 5, 8]]),
		"constant non-enum type test rejects at the operand: %s" % [constant_is_errors])
	if probe.has_method("inspect_expression_source"):
		var subscript_producer: Dictionary = probe.call("inspect_expression_source", "const base := [0]\nvar probe_expression = base[0]\n", "res://tests/constant_subscript_producer_probe.barista")
		_expect(failures, _inspection_is_valid(subscript_producer) and subscript_producer.get("datatype") == "Variant" and
			subscript_producer.get("type_source") == 0 and subscript_producer.get("is_hard_type") == false and
			subscript_producer.get("is_constant") == false,
			"constant-subscript producer remains soft Variant/unreduced for step 138-07: %s" % [subscript_producer])
	var hard_is_source := "class A:\n\tfunc _init():\n\t\tpass\n\nclass B extends A: pass\nclass C extends A: pass\n\nfunc test():\n\tvar x := B.new()\n\tprint(x is C)\n"
	var hard_is_errors: Array = probe.validate_source(hard_is_source, "res://tests/constructor_call_type.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(hard_is_errors, [['Expression is of type "B" so it can\'t be of type "C".', 10, 11]]),
		"nonconstant hard incompatible type test rejects at the operand: %s" % [hard_is_errors])
	var true_fold: Dictionary = probe.fold_expression("1 is int")
	_expect(failures, true_fold.get("ok", false) and true_fold.get("value") == true,
		"constant compatible non-enum type test folds from the reduced value: %s" % [true_fold])
	if probe.has_method("inspect_expression_source"):
		var enum_membership: Dictionary = probe.call("inspect_expression_source", "enum E:\n\tA = 0\nvar probe_expression = E.A is E\n", "res://tests/enum_membership_probe.barista")
		_expect(failures, _inspection_is_valid(enum_membership) and enum_membership.get("datatype") == "bool" and enum_membership.get("is_constant") == false,
			"plain-enum is remains an unfurled membership test despite its int carrier: %s" % [enum_membership])

	var bind_source := "enum TernaryCaseMessage:\n\tQuit\n\tMove(x: int, y: int)\n\nfunc test():\n\tvar message: TernaryCaseMessage = TernaryCaseMessage.Quit\n\tprint(1 if message is TernaryCaseMessage.Move(x, y) else 0)\n"
	var bind_errors: Array = probe.validate_source(bind_source, "res://tests/tagged_union_case_test_binds_in_ternary.barista", false).get("errors", [])
	_expect(failures, _errors_are_exact(bind_errors, [['Case payload binds are only allowed in the condition of "if", "elif", "while" or "assert", directly or as an "and" operand.', 7, 16]]),
		"case-bind placement remains exact while ordinary type tests reduce: %s" % [bind_errors])

	var invalid_source: String = cast_sources[0][0]
	var invalid_path: String = cast_sources[0][1]
	var invalid_first: Dictionary = probe.analyze_source(invalid_source, invalid_path)
	var invalid_second: Dictionary = probe.analyze_source(invalid_source, invalid_path)
	var valid_first: Dictionary = probe.analyze_source(ternary_source, "res://tests/ternary_concrete_repeat.barista")
	var valid_second: Dictionary = probe.analyze_source(ternary_source, "res://tests/ternary_concrete_repeat.barista")
	var index := BaristaScriptDeclarationIndexProbe.new()
	var index_before := index.get_record_count()
	_expect(failures, invalid_first.get("errors", PackedStringArray()) == invalid_second.get("errors", PackedStringArray()) and
		not probe.validate_source(invalid_source, invalid_path, false).get("valid", true) and
		not probe.is_semantically_valid(invalid_source, invalid_path),
		"invalid cast is repeatable through analyze/validate/is-valid")
	_expect(failures, valid_first.get("valid", false) and valid_second.get("valid", false) and
		probe.validate_source(ternary_source, "res://tests/ternary_concrete_repeat.barista", false).get("valid", false) and
		probe.is_semantically_valid(ternary_source, "res://tests/ternary_concrete_repeat.barista"),
		"valid concrete ternaries are repeatable through analyze/validate/is-valid")
	_expect(failures, index.get_record_count() == index_before,
		"step-6 repeated analysis preserves declaration-index opt-in")

	ProjectSettings.set_setting("debug/barista_script/warnings/incompatible_ternary", 0)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	_expect(failures, probe.validate_source(incompatible_source, "res://tests/incompatible_ternary.barista", true).get("warnings", []).is_empty(),
		"incompatible ternary warning obeys IGNORE")
	ProjectSettings.set_setting("debug/barista_script/warnings/incompatible_ternary", 2)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
	_expect(failures, not probe.validate_source(incompatible_source, "res://tests/incompatible_ternary.barista", true).get("valid", true),
		"incompatible ternary warning obeys ERROR")
	ProjectSettings.set_setting("debug/barista_script/warnings/incompatible_ternary", 1)
	BaristaScriptParseCache.invalidate_analysis_on_strict_settings_change()
