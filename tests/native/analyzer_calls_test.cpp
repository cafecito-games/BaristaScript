/**************************************************************************/
/*  analyzer_calls_test.cpp                                               */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                          */
/*  This file is part of BaristaScript, a Godot GDExtension.                */
/*  SPDX-License-Identifier: MIT                                           */
/**************************************************************************/

#include "analyzer_helpers.h"
#include "bs_conformance_registry.h"
#include "storage_fixture.h"

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

// Sources and every condition migrated from analyzer_test.gd at 4b2439f (PR #199).
namespace {
void scenario_call_arity_and_types() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String too_few = "class_name CallFew extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tadd(1)\n";
	const auto few_report = analyze_source(too_few, "res://tests/call_few.barista");
	CHECK_MESSAGE(few_report.valid() == false, "too few arguments invalid");
	bool saw_few = false;
	for (const auto &error : few_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Too few arguments")) {
			saw_few = true;
		}
	}
	CHECK_MESSAGE(saw_few, "too few arguments diagnostic");
	const String bad_type = "class_name CallType extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tadd(1, 1.5)\n";
	const auto type_report = analyze_source(bad_type, "res://tests/call_type.barista");
	CHECK_MESSAGE(type_report.valid() == false, "wrong call argument type invalid");
	const String ok = "class_name CallOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tvar x: int = add(1, 2)\n";
	const auto ok_report = analyze_source(ok, "res://tests/call_ok.barista");
	CHECK_MESSAGE(ok_report.valid() == true, "matching call arity/types valid");
}

void scenario_call_validation_methodinfo_and_signals() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String native_few = "class_name NativeFew extends Node\nfunc _ready() -> void:\n\tget_node()\n";
	const auto native_few_report = analyze_source(native_few, "res://tests/native_few.barista");
	CHECK_MESSAGE(native_few_report.valid() == false, "native MethodInfo too-few invalid");
	bool saw_native_few = false;
	for (const auto &error : native_few_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Too few arguments") && message.contains("get_node")) {
			saw_native_few = true;
		}
	}
	CHECK_MESSAGE(saw_native_few, "native MethodInfo too-few diagnostic");
	const String native_type = "class_name NativeType extends Node\nfunc _ready() -> void:\n\tget_node(1)\n";
	const auto native_type_report = analyze_source(native_type, "res://tests/native_type.barista");
	CHECK_MESSAGE(native_type_report.valid() == false, "native MethodInfo wrong arg type invalid");
	bool saw_native_type = false;
	for (const auto &error : native_type_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Invalid argument") && message.contains("get_node")) {
			saw_native_type = true;
		}
	}
	CHECK_MESSAGE(saw_native_type, "native MethodInfo wrong-type diagnostic");
	const String emit_few = "class_name EmitFew extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tchanged.emit()\n";
	const auto emit_few_report = analyze_source(emit_few, "res://tests/emit_few.barista");
	CHECK_MESSAGE(emit_few_report.valid() == false, "signal.emit too-few invalid");
	bool saw_emit_few = false;
	for (const auto &error : emit_few_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Too few arguments") && message.contains("emit")) {
			saw_emit_few = true;
		}
	}
	CHECK_MESSAGE(saw_emit_few, "signal.emit too-few diagnostic");
	const String emit_type = "class_name EmitType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tchanged.emit(\"bad\")\n";
	const auto emit_type_report = analyze_source(emit_type, "res://tests/emit_type.barista");
	CHECK_MESSAGE(emit_type_report.valid() == false, "signal.emit wrong type invalid");
	bool saw_emit_type = false;
	for (const auto &error : emit_type_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Invalid argument") && message.contains("emit")) {
			saw_emit_type = true;
		}
	}
	CHECK_MESSAGE(saw_emit_type, "signal.emit wrong-type diagnostic");
	const String emit_ok = "class_name EmitOk extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tchanged.emit(1)\n";
	const auto emit_ok_report = analyze_source(emit_ok, "res://tests/emit_ok.barista");
	CHECK_MESSAGE(emit_ok_report.valid() == true, "signal.emit matching payload valid");
	const String emit_signal_type = "class_name EmitSignalType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\temit_signal(\"changed\", \"bad\")\n";
	const auto emit_signal_report = analyze_source(emit_signal_type, "res://tests/emit_signal_type.barista");
	CHECK_MESSAGE(emit_signal_report.valid() == false, "emit_signal wrong payload type invalid");
	bool saw_emit_signal = false;
	for (const auto &error : emit_signal_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Invalid argument") && message.contains("emit_signal")) {
			saw_emit_signal = true;
		}
	}
	CHECK_MESSAGE(saw_emit_signal, "emit_signal wrong-type diagnostic");
	const String emit_signal_ok = "class_name EmitSignalOk extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\temit_signal(\"changed\", 1)\n";
	const auto emit_signal_ok_report = analyze_source(emit_signal_ok, "res://tests/emit_signal_ok.barista");
	CHECK_MESSAGE(emit_signal_ok_report.valid() == true, "emit_signal matching payload valid");
	const String self_emit_signal_type = "class_name SelfEmitSignalType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tself.emit_signal(\"changed\", \"bad\")\n";
	const auto self_emit_signal_report = analyze_source(self_emit_signal_type, "res://tests/self_emit_signal_type.barista");
	CHECK_MESSAGE(self_emit_signal_report.valid() == false, "self.emit_signal wrong payload type invalid");
	bool saw_self_emit_signal = false;
	for (const auto &error : self_emit_signal_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Invalid argument") && message.contains("emit_signal")) {
			saw_self_emit_signal = true;
		}
	}
	CHECK_MESSAGE(saw_self_emit_signal, "self.emit_signal wrong-type diagnostic");
	const String self_emit = "class_name SelfChangedEmitType extends Node\nsignal changed(value: int)\nfunc _ready() -> void:\n\tself.changed.emit(\"bad\")\n";
	const auto self_emit_report = analyze_source(self_emit, "res://tests/self_changed_emit_type.barista");
	CHECK_MESSAGE(self_emit_report.valid() == false, "self.changed.emit wrong payload type invalid");
	bool saw_self_emit = false;
	for (const auto &error : self_emit_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Invalid argument") && message.contains("emit")) {
			saw_self_emit = true;
		}
	}
	CHECK_MESSAGE(saw_self_emit, "self.changed.emit wrong-type diagnostic");
}

void scenario_named_arg_and_connect_callable() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String positional_after = "class_name NamedPosAfter extends Node\nfunc greet(name: String, greeting: String) -> void:\n\tpass\nfunc _ready() -> void:\n\tgreet(name = \"Bob\", \"Hi\")\n";
	const auto positional_report = analyze_source(positional_after, "res://tests/named_pos_after.barista");
	CHECK_MESSAGE(positional_report.valid() == false, "positional after named is invalid");
	bool saw_positional = false;
	for (const auto &error : positional_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Positional argument cannot follow a named argument")) {
			saw_positional = true;
		}
	}
	CHECK_MESSAGE(saw_positional, "positional-after-named diagnostic");
	const String unknown_name = "class_name NamedUnknown extends Node\nfunc greet(name: String, greeting: String) -> void:\n\tpass\nfunc _ready() -> void:\n\tgreet(name = \"Bob\", salutation = \"Hi\")\n";
	const auto unknown_report = analyze_source(unknown_name, "res://tests/named_unknown.barista");
	CHECK_MESSAGE(unknown_report.valid() == false, "unknown named parameter is invalid");
	bool saw_unknown = false;
	for (const auto &error : unknown_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("no parameter named") && message.contains("salutation")) {
			saw_unknown = true;
		}
	}
	CHECK_MESSAGE(saw_unknown, "unknown named parameter diagnostic");
	const String named_reorder = "class_name NamedReorder extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tvar x: int = add(b = 2, a = 1)\n";
	const auto reorder_report = analyze_source(named_reorder, "res://tests/named_reorder.barista");
	CHECK_MESSAGE(reorder_report.valid() == true, "named-arg reorder call is valid");
	const String named_gap = "class_name NamedGap extends Node\nfunc combine(a: int, b: int = 10, c: int = 20) -> int:\n\treturn a + b + c\nfunc _ready() -> void:\n\tvar x: int = combine(1, c = 5)\n";
	const auto gap_report = analyze_source(named_gap, "res://tests/named_gap.barista");
	CHECK_MESSAGE(gap_report.valid() == true, "named-arg constant default gap fill is valid");
	const String named_type = "class_name NamedType extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc _ready() -> void:\n\tadd(a = 1, b = 1.5)\n";
	const auto named_type_report = analyze_source(named_type, "res://tests/named_type.barista");
	CHECK_MESSAGE(named_type_report.valid() == false, "named-arg wrong type is invalid");
	const String native_named = "class_name NativeNamed extends Node\nfunc _ready() -> void:\n\tget_node(path = \".\")\n";
	const auto native_named_report = analyze_source(native_named, "res://tests/native_named.barista");
	CHECK_MESSAGE(native_named_report.valid() == false, "named args on native MethodInfo are invalid");
	bool saw_native_named = false;
	for (const auto &error : native_named_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Named arguments require a statically known BaristaScript function")) {
			saw_native_named = true;
		}
	}
	CHECK_MESSAGE(saw_native_named, "native named-arg rejection diagnostic");
	const String connect_arity = "class_name ConnectArity extends Node\nsignal registered(node: Node, index: int)\nfunc on_registered(node: Node) -> void:\n\tpass\nfunc _ready() -> void:\n\tregistered.connect(on_registered)\n";
	const auto connect_arity_report = analyze_source(connect_arity, "res://tests/connect_arity.barista");
	CHECK_MESSAGE(connect_arity_report.valid() == false, "signal.connect arity mismatch invalid");
	bool saw_connect_arity = false;
	for (const auto &error : connect_arity_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot connect signal") && message.contains("emits 2 arguments")) {
			saw_connect_arity = true;
		}
	}
	CHECK_MESSAGE(saw_connect_arity, "signal.connect arity diagnostic");
	const String connect_type = "class_name ConnectType extends Node\nsignal registered(node: Node)\nfunc on_registered(resource: Resource) -> void:\n\tpass\nfunc _ready() -> void:\n\tregistered.connect(on_registered)\n";
	const auto connect_type_report = analyze_source(connect_type, "res://tests/connect_type.barista");
	CHECK_MESSAGE(connect_type_report.valid() == false, "signal.connect type mismatch invalid");
	bool saw_connect_type = false;
	for (const auto &error : connect_type_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot connect signal") && message.contains("cannot be passed")) {
			saw_connect_type = true;
		}
	}
	CHECK_MESSAGE(saw_connect_type, "signal.connect type diagnostic");
	const String object_connect_type = "class_name ObjectConnectType extends Node\nsignal registered(node: Node)\nfunc on_registered(resource: Resource) -> void:\n\tpass\nfunc _ready() -> void:\n\tconnect(\"registered\", on_registered)\n";
	const auto object_connect_report = analyze_source(object_connect_type, "res://tests/object_connect_type.barista");
	CHECK_MESSAGE(object_connect_report.valid() == false, "Object.connect type mismatch invalid");
	bool saw_object_connect = false;
	for (const auto &error : object_connect_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot connect signal") && message.contains("cannot be passed")) {
			saw_object_connect = true;
		}
	}
	CHECK_MESSAGE(saw_object_connect, "Object.connect type diagnostic");
	const String connect_ok = "class_name ConnectOk extends Node\nsignal registered(node: Node)\nfunc on_registered(node: Node) -> void:\n\tpass\nfunc _ready() -> void:\n\tregistered.connect(on_registered)\n\tconnect(\"registered\", on_registered)\n";
	const auto connect_ok_report = analyze_source(connect_ok, "res://tests/connect_ok.barista");
	CHECK_MESSAGE(connect_ok_report.valid() == true, "matching connect callables are valid");
	const String set_name_ok = "class_name SetNameOk extends Node\nfunc _ready() -> void:\n\tset_name(\"probe\")\n";
	const auto set_name_report = analyze_source(set_name_ok, "res://tests/set_name_ok.barista");
	CHECK_MESSAGE(set_name_report.valid() == true, "String passes to StringName MethodInfo via can_convert_strict");
	const String connect_bad_name = "class_name ConnectBadName extends Node\nfunc _ready() -> void:\n\tconnect(123, Callable())\n";
	const auto connect_bad_name_report = analyze_source(connect_bad_name, "res://tests/connect_bad_name.barista");
	CHECK_MESSAGE(connect_bad_name_report.valid() == false, "int→StringName connect arg remains invalid");
	bool saw_bad_name = false;
	for (const auto &error : connect_bad_name_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("argument 1 should be \"StringName\"") && message.contains("int")) {
			saw_bad_name = true;
		}
	}
	CHECK_MESSAGE(saw_bad_name, "int→StringName argument diagnostic");
	const String float_to_int = "class_name FloatToIntReject extends Node\nfunc _ready() -> void:\n\tvar value: float = 1.5\n\tvar _narrowed: int = value\n";
	const auto float_to_int_report = analyze_source(float_to_int, "res://tests/float_to_int_reject.barista");
	CHECK_MESSAGE(float_to_int_report.valid() == false, "non-constant float→int remains invalid");
}

void scenario_callable_signal_constructor_and_typed_receiver_depth() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String callable_ctor = "class_name CallableCtorDepth extends Node\nfunc take(value: int) -> String:\n\treturn str(value)\nfunc test() -> void:\n\tvar callback := Callable(self, \"take\")\n\tcallback.call()\n";
	const auto callable_report = analyze_source(callable_ctor, "res://tests/callable_ctor_depth.barista");
	bool saw_callable_arity = false;
	for (const auto &error : callable_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Too few arguments") && message.contains("call")) {
			saw_callable_arity = true;
		}
	}
	CHECK_MESSAGE(saw_callable_arity, "Callable(Object, method) preserves target arity");
	const String signal_ctor = "class_name SignalCtorDepth extends Node\nsignal changed(value: int)\nfunc on_changed(value: String) -> void:\n\tpass\nfunc test() -> void:\n\tvar typed_signal := Signal(self, \"changed\")\n\ttyped_signal.connect(on_changed)\n";
	const auto signal_report = analyze_source(signal_ctor, "res://tests/signal_ctor_depth.barista");
	bool saw_signal_signature = false;
	for (const auto &error : signal_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot connect signal") && message.contains("cannot be passed")) {
			saw_signal_signature = true;
		}
	}
	CHECK_MESSAGE(saw_signal_signature, "Signal(Object, name) preserves payload signature");
	const String callable_bad_arity = "class_name CallableCtorBadArity extends Node\nfunc test() -> void:\n\tvar _callback := Callable(self, \"test\", 1)\n";
	const auto callable_bad_arity_report = analyze_source(callable_bad_arity, "res://tests/callable_ctor_bad_arity.barista");
	CHECK_MESSAGE(callable_bad_arity_report.valid() == false, "Callable constructor rejects unsupported arity");
	const String callable_bad_types = "class_name CallableCtorBadTypes extends Node\nfunc test() -> void:\n\tvar _callback := Callable(1, \"test\")\n";
	const auto callable_bad_types_report = analyze_source(callable_bad_types, "res://tests/callable_ctor_bad_types.barista");
	CHECK_MESSAGE(callable_bad_types_report.valid() == false, "Callable constructor rejects non-Object receiver");
	const String signal_bad_copy = "class_name SignalCtorBadCopy extends Node\nfunc test() -> void:\n\tvar _signal := Signal(self)\n";
	const auto signal_bad_copy_report = analyze_source(signal_bad_copy, "res://tests/signal_ctor_bad_copy.barista");
	CHECK_MESSAGE(signal_bad_copy_report.valid() == false, "Signal copy constructor rejects Object");
	const String signal_bad_name = "class_name SignalCtorBadName extends Node\nfunc test() -> void:\n\tvar _signal := Signal(self, 7)\n";
	const auto signal_bad_name_report = analyze_source(signal_bad_name, "res://tests/signal_ctor_bad_name.barista");
	CHECK_MESSAGE(signal_bad_name_report.valid() == false, "Signal constructor rejects non-StringName signal name");
	const String typed_receiver = "class_name TypedReceiverSignalDepth extends Node\nsignal changed(value: int)\nfunc on_changed(value: String) -> void:\n\tpass\nfunc test(child: Self) -> void:\n\tchild.emit_signal(\"changed\", \"bad\")\n\tchild.connect(\"changed\", on_changed)\n";
	const auto receiver_report = analyze_source(typed_receiver, "res://tests/typed_receiver_signal_depth.barista");
	bool saw_typed_emit = false;
	bool saw_typed_connect = false;
	for (const auto &error : receiver_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Invalid argument") && message.contains("emit_signal")) {
			saw_typed_emit = true;
		}
		if (message.contains("Cannot connect signal") && message.contains("cannot be passed")) {
			saw_typed_connect = true;
		}
	}
	CHECK_MESSAGE(saw_typed_emit, "typed receiver emit_signal validates payload");
	CHECK_MESSAGE(saw_typed_connect, "typed receiver connect validates callable");
}
void scenario_callable_bind_unbind() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String bind_type_bad = "class_name CallableBindTypeBad extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tvar _bound := one.bind(\"not an int\")\n";
	const auto bind_type_bad_report = analyze_source(bind_type_bad, "res://tests/callable_bind_type_bad.barista");
	CHECK_MESSAGE(bind_type_bad_report.valid() == true, "bare bind accepts a gradual target argument");
	bool saw_bind_type = false;
	for (const auto &error : bind_type_bad_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("argument 1 should be \"int\"") || message.contains("should be \"int\" but is \"String\"")) {
			saw_bind_type = true;
		}
	}
	CHECK_MESSAGE((!saw_bind_type && bind_type_bad_report.parser->get_errors().is_empty()), "bind_type_bad: generic Callable has no typed-target diagnostic");
	const String bind_call_ok = "class_name CallableBindCallOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> int:\n\treturn add.bind(5).call(2)\n";
	const auto bind_call_ok_report = analyze_source(bind_call_ok, "res://tests/callable_bind_call_ok.barista");
	CHECK_MESSAGE(bind_call_ok_report.valid() == true, "bind then call with remaining arity is valid");
	const String bind_call_arity = "class_name CallableBindCallArity extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tadd.bind(5).call()\n";
	const auto bind_call_arity_report = analyze_source(bind_call_arity, "res://tests/callable_bind_call_arity.barista");
	CHECK_MESSAGE(bind_call_arity_report.valid() == true, "bare bound call retains generic vararg arity");
	bool saw_too_few = false;
	for (const auto &error : bind_call_arity_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Too few arguments for \"call()\" call")) {
			saw_too_few = true;
		}
	}
	CHECK_MESSAGE((!saw_too_few && bind_call_arity_report.parser->get_errors().is_empty()), "bind_call_arity: generic Callable has no typed-target diagnostic");
	const String over_bound = "class_name CallableOverBound extends Node\nfunc zero_arg() -> int:\n\treturn 1\nfunc test() -> void:\n\tzero_arg.bind(1).call()\n";
	const auto over_bound_report = analyze_source(over_bound, "res://tests/callable_over_bound.barista");
	CHECK_MESSAGE(over_bound_report.valid() == true, "bare bind does not publish an over-bound signature");
	bool saw_over_bound = false;
	for (const auto &error : over_bound_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("over-bound")) {
			saw_over_bound = true;
		}
	}
	CHECK_MESSAGE((!saw_over_bound && over_bound_report.parser->get_errors().is_empty()), "over_bound: generic Callable has no typed-target diagnostic");
	const String unbind_bad = "class_name CallableUnbindBad extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tvar _u := one.unbind(0)\n";
	const auto unbind_bad_report = analyze_source(unbind_bad, "res://tests/callable_unbind_bad.barista");
	CHECK_MESSAGE(unbind_bad_report.valid() == true, "generic Callable unbind accepts an int count");
	bool saw_unbind = false;
	for (const auto &error : unbind_bad_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Amount of \"unbind()\" arguments must be 1 or greater")) {
			saw_unbind = true;
		}
	}
	CHECK_MESSAGE((!saw_unbind && unbind_bad_report.parser->get_errors().is_empty()), "unbind_bad: generic Callable has no typed-target diagnostic");
	const String unbind_ok = "class_name CallableUnbindOk extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> int:\n\treturn one.unbind(1).call(9, 1)\n";
	const auto unbind_ok_report = analyze_source(unbind_ok, "res://tests/callable_unbind_ok.barista");
	CHECK_MESSAGE(unbind_ok_report.valid() == true, "unbind(1) then call with unbound trailing slot is valid");
	const String bindv_type_bad = "class_name CallableBindvTypeBad extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tvar _bound := one.bindv([\"not an int\"])\n";
	const auto bindv_type_bad_report = analyze_source(bindv_type_bad, "res://tests/callable_bindv_type_bad.barista");
	CHECK_MESSAGE(bindv_type_bad_report.valid() == true, "bare bindv preserves only its outer Array contract");
	const String default_survival = "class_name CallableBindDefaultSurvival extends Node\nfunc add_with_default(p: int, q: int, s: int = 1) -> int:\n\treturn p + q + s\nfunc test() -> int:\n\treturn add_with_default.bind(5).call(2)\n";
	const auto default_survival_report = analyze_source(default_survival, "res://tests/callable_bind_default_survival.barista");
	CHECK_MESSAGE(default_survival_report.valid() == true, "bind preserves trailing default survival arity");
	const String narrow_survival = "class_name CallableBindNarrowSurvival extends Node\nfunc takes(narrow: Node2D, wide: Node = null) -> int:\n\treturn 1\nfunc test() -> int:\n\tvar n: Node = Node2D.new()\n\treturn takes.bind(n).call()\n";
	const auto narrow_survival_report = analyze_source(narrow_survival, "res://tests/callable_bind_narrow_survival.barista");
	CHECK_MESSAGE(narrow_survival_report.valid() == true, "bind preserves default survival under native subtype narrowing");
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(bind_call_ok, "res://tests/callable_bind_validate.barista", true, true, true, false);
	CHECK_MESSAGE(bool(validate_report.get("valid", false)) == true, "callable bind remains valid under validate()");
	CHECK_MESSAGE(source_analyzes(bind_call_ok, "res://tests/callable_bind_is_valid.barista"), "callable bind remains valid under is_semantically_valid()");
	CHECK_MESSAGE(index.get_record_count() == before, "analyze/validate/is_valid must not mutate declaration index for callable bind");
}

void scenario_callable_callv_rpc() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String callv_type_bad = "class_name CallableCallvTypeBad extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tone.callv([\"not an int\"])\n";
	const auto callv_type_bad_report = analyze_source(callv_type_bad, "res://tests/callable_callv_type_bad.barista");
	CHECK_MESSAGE(callv_type_bad_report.valid() == true, "bare callv accepts gradual Array elements");
	bool saw_callv_type = false;
	for (const auto &error : callv_type_bad_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("argument 1 should be \"int\"") || message.contains("should be \"int\" but is \"String\"")) {
			saw_callv_type = true;
		}
	}
	CHECK_MESSAGE((!saw_callv_type && callv_type_bad_report.parser->get_errors().is_empty()), "callv_type_bad: generic Callable has no typed-target diagnostic");
	const String callv_ok = "class_name CallableCallvOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> int:\n\treturn add.callv([2, 3])\n";
	const auto callv_ok_report = analyze_source(callv_ok, "res://tests/callable_callv_ok.barista");
	CHECK_MESSAGE(callv_ok_report.valid() == true, "callv with matching array-literal types is valid");
	const String callv_arity = "class_name CallableCallvArity extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tadd.callv([1])\n";
	const auto callv_arity_report = analyze_source(callv_arity, "res://tests/callable_callv_arity.barista");
	CHECK_MESSAGE(callv_arity_report.valid() == true, "bare callv does not impose target arity");
	bool saw_callv_few = false;
	for (const auto &error : callv_arity_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Too few arguments for \"callv()\" call")) {
			saw_callv_few = true;
		}
	}
	CHECK_MESSAGE((!saw_callv_few && callv_arity_report.parser->get_errors().is_empty()), "callv_arity: generic Callable has no typed-target diagnostic");
	const String deferred_ok = "class_name CallableDeferredOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tadd.call_deferred(2, 3)\n";
	const auto deferred_ok_report = analyze_source(deferred_ok, "res://tests/callable_deferred_ok.barista");
	CHECK_MESSAGE(deferred_ok_report.valid() == true, "call_deferred with matching arity is valid");
	const String deferred_type_bad = "class_name CallableDeferredTypeBad extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tone.call_deferred(\"not an int\")\n";
	const auto deferred_type_bad_report = analyze_source(deferred_type_bad, "res://tests/callable_deferred_type_bad.barista");
	CHECK_MESSAGE(deferred_type_bad_report.valid() == true, "bare call_deferred accepts gradual target arguments");
	const String rpc_ok = "class_name CallableRpcOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tadd.rpc(2, 3)\n";
	const auto rpc_ok_report = analyze_source(rpc_ok, "res://tests/callable_rpc_ok.barista");
	CHECK_MESSAGE(rpc_ok_report.valid() == true, "rpc with matching arity is valid");
	const String rpc_id_ok = "class_name CallableRpcIdOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tadd.rpc_id(1, 2, 3)\n";
	const auto rpc_id_ok_report = analyze_source(rpc_id_ok, "res://tests/callable_rpc_id_ok.barista");
	CHECK_MESSAGE(rpc_id_ok_report.valid() == true, "rpc_id peer_id plus matching target arity is valid");
	const String rpc_id_peer_type = "class_name CallableRpcIdPeerType extends Node\nfunc one(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tone.rpc_id(\"peer\", 1)\n";
	const auto rpc_id_peer_type_report = analyze_source(rpc_id_peer_type, "res://tests/callable_rpc_id_peer_type.barista");
	CHECK_MESSAGE(rpc_id_peer_type_report.valid() == false, "rpc_id non-int peer_id is invalid");
	bool saw_peer_type = false;
	for (const auto &error : rpc_id_peer_type_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("argument 1 should be \"int\"") || message.contains("should be \"int\" but is \"String\"")) {
			saw_peer_type = true;
		}
	}
	CHECK_MESSAGE(saw_peer_type, "rpc_id peer_id type mismatch diagnostic");
	const String rpc_id_arity = "class_name CallableRpcIdArity extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tadd.rpc_id(1, 2)\n";
	const auto rpc_id_arity_report = analyze_source(rpc_id_arity, "res://tests/callable_rpc_id_arity.barista");
	CHECK_MESSAGE(rpc_id_arity_report.valid() == true, "bare rpc_id enforces peer_id without target arity");
	bool saw_rpc_id_few = false;
	for (const auto &error : rpc_id_arity_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Too few arguments for \"rpc_id()\" call")) {
			saw_rpc_id_few = true;
		}
	}
	CHECK_MESSAGE((!saw_rpc_id_few && rpc_id_arity_report.parser->get_errors().is_empty()), "rpc_id_arity: generic Callable has no typed-target diagnostic");
	const String over_callv = "class_name CallableOverBoundCallv extends Node\nfunc zero_arg() -> int:\n\treturn 1\nfunc test() -> void:\n\tzero_arg.bind(1).callv([])\n";
	const auto over_callv_report = analyze_source(over_callv, "res://tests/callable_over_bound_callv.barista");
	CHECK_MESSAGE(over_callv_report.valid() == true, "bare bind callv remains gradual");
	const String over_deferred = "class_name CallableOverBoundDeferred extends Node\nfunc zero_arg() -> int:\n\treturn 1\nfunc test() -> void:\n\tzero_arg.bind(1).call_deferred()\n";
	const auto over_deferred_report = analyze_source(over_deferred, "res://tests/callable_over_bound_deferred.barista");
	CHECK_MESSAGE(over_deferred_report.valid() == true, "bare bind call_deferred remains gradual");
	const String over_rpc = "class_name CallableOverBoundRpc extends Node\nfunc zero_arg() -> int:\n\treturn 1\nfunc test() -> void:\n\tzero_arg.bind(1).rpc()\n";
	const auto over_rpc_report = analyze_source(over_rpc, "res://tests/callable_over_bound_rpc.barista");
	CHECK_MESSAGE(over_rpc_report.valid() == true, "bare bind rpc remains gradual");
	const String over_rpc_id = "class_name CallableOverBoundRpcId extends Node\nfunc zero_arg() -> int:\n\treturn 1\nfunc test() -> void:\n\tzero_arg.bind(1).rpc_id(1)\n";
	const auto over_rpc_id_report = analyze_source(over_rpc_id, "res://tests/callable_over_bound_rpc_id.barista");
	CHECK_MESSAGE(over_rpc_id_report.valid() == true, "bare bind rpc_id remains gradual after peer_id");
	bool saw_over_rpc_id = false;
	for (const auto &error : over_rpc_id_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("over-bound")) {
			saw_over_rpc_id = true;
		}
	}
	CHECK_MESSAGE((!saw_over_rpc_id && over_rpc_id_report.parser->get_errors().is_empty()), "over_rpc_id: generic Callable has no typed-target diagnostic");
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(callv_ok, "res://tests/callable_callv_validate.barista", true, true, true, false);
	CHECK_MESSAGE(bool(validate_report.get("valid", false)) == true, "callable callv remains valid under validate()");
	CHECK_MESSAGE(source_analyzes(callv_ok, "res://tests/callable_callv_is_valid.barista"), "callable callv remains valid under is_semantically_valid()");
	CHECK_MESSAGE(index.get_record_count() == before, "analyze/validate/is_valid must not mutate declaration index for callable callv/rpc");
}

void scenario_async_callable_coroutine_wrap() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String async_call_assign = "class_name AsyncCallableCallAssign extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = fetch.call()\n";
	const auto async_call_assign_report = analyze_source(async_call_assign, "res://tests/async_callable_call_assign.barista");
	CHECK_MESSAGE(async_call_assign_report.valid() == false, "AsyncCallable.call result is not assignable to int");
	bool saw_async_call_coro = false;
	for (const auto &error : async_call_assign_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot assign a value of type") && message.contains("Coroutine[Variant]") && message.contains("variable \"result\" with specified type int")) {
			saw_async_call_coro = true;
		}
	}
	CHECK_MESSAGE(saw_async_call_coro, "bare AsyncCallable.call diagnoses Coroutine[Variant] assign to int");
	const String async_callv_assign = "class_name AsyncCallableCallvAssign extends Node\nasync func fetch(value: int) -> String:\n\treturn \"ok\"\nfunc test() -> void:\n\tvar result: String = fetch.callv([1])\n";
	const auto async_callv_assign_report = analyze_source(async_callv_assign, "res://tests/async_callable_callv_assign.barista");
	CHECK_MESSAGE(async_callv_assign_report.valid() == false, "AsyncCallable.callv result is not assignable to String");
	bool saw_async_callv_coro = false;
	for (const auto &error : async_callv_assign_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot assign a value of type") && message.contains("Coroutine[Variant]") && message.contains("variable \"result\" with specified type String")) {
			saw_async_callv_coro = true;
		}
	}
	CHECK_MESSAGE(saw_async_callv_coro, "bare AsyncCallable.callv diagnoses Coroutine[Variant] assign to String");
	const String async_bound_call = "class_name AsyncCallableBoundCall extends Node\nasync func add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tvar result: int = add.bind(1).call(2)\n";
	const auto async_bound_call_report = analyze_source(async_bound_call, "res://tests/async_callable_bound_call.barista");
	CHECK_MESSAGE(async_bound_call_report.valid() == true, "generic bound AsyncCallable loses the explicit target signature");
	bool saw_bound_coro = false;
	for (const auto &error : async_bound_call_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot assign a value of type") && message.contains("Coroutine[int]")) {
			saw_bound_coro = true;
		}
	}
	CHECK_MESSAGE((!saw_bound_coro && async_bound_call_report.parser->get_errors().is_empty()), "async_bound_call: generic Callable has no typed-target diagnostic");
	const String async_deferred_ok = "class_name AsyncCallableDeferredOk extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tfetch.call_deferred()\n";
	const auto async_deferred_ok_report = analyze_source(async_deferred_ok, "res://tests/async_callable_deferred_ok.barista");
	CHECK_MESSAGE(async_deferred_ok_report.valid() == true, "AsyncCallable.call_deferred stays non-coroutine / valid");
	const String async_rpc_ok = "class_name AsyncCallableRpcOk extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tfetch.rpc()\n";
	const auto async_rpc_ok_report = analyze_source(async_rpc_ok, "res://tests/async_callable_rpc_ok.barista");
	CHECK_MESSAGE(async_rpc_ok_report.valid() == true, "AsyncCallable.rpc stays non-coroutine / valid");
	const String async_rpc_id_ok = "class_name AsyncCallableRpcIdOk extends Node\nasync func fetch(value: int) -> int:\n\treturn value\nfunc test() -> void:\n\tfetch.rpc_id(1, 2)\n";
	const auto async_rpc_id_ok_report = analyze_source(async_rpc_id_ok, "res://tests/async_callable_rpc_id_ok.barista");
	CHECK_MESSAGE(async_rpc_id_ok_report.valid() == true, "AsyncCallable.rpc_id stays non-coroutine / valid");
	const String async_deferred_assign = "class_name AsyncCallableDeferredAssign extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = fetch.call_deferred()\n";
	const auto async_deferred_assign_report = analyze_source(async_deferred_assign, "res://tests/async_callable_deferred_assign.barista");
	CHECK_MESSAGE(async_deferred_assign_report.valid() == false, "call_deferred NIL is not assignable to int");
	bool saw_deferred_nil = false;
	bool saw_deferred_coro = false;
	for (const auto &error : async_deferred_assign_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot assign a value of type") && message.contains("Coroutine[")) {
			saw_deferred_coro = true;
		}
		if (message.contains("Cannot assign a value of type") && (message.contains("null") || message.contains("void") || message.contains("Nil") || message.contains("nil"))) {
			saw_deferred_nil = true;
		}
	}
	CHECK_MESSAGE(!saw_deferred_coro, "call_deferred on AsyncCallable must not wrap as Coroutine");
	CHECK_MESSAGE(saw_deferred_nil, "call_deferred on AsyncCallable diagnoses NIL/null assign to int");
	const String sync_call_ok = "class_name SyncCallableCallOk extends Node\nfunc fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = fetch.call()\n";
	const auto sync_call_ok_report = analyze_source(sync_call_ok, "res://tests/sync_callable_call_ok.barista");
	CHECK_MESSAGE(sync_call_ok_report.valid() == true, "plain Callable.call return type unchanged");
	const String sync_callv_ok = "class_name SyncCallableCallvOk extends Node\nfunc add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tvar result: int = add.callv([2, 3])\n";
	const auto sync_callv_ok_report = analyze_source(sync_callv_ok, "res://tests/sync_callable_callv_ok.barista");
	CHECK_MESSAGE(sync_callv_ok_report.valid() == true, "plain Callable.callv return type unchanged");
	const String bare_async_call = "class_name BareAsyncCallableCall extends Node\nfunc test() -> void:\n\tvar cb: AsyncCallable\n\tvar result: int = cb.call()\n";
	const auto bare_async_call_report = analyze_source(bare_async_call, "res://tests/bare_async_callable_call.barista");
	CHECK_MESSAGE(bare_async_call_report.valid() == false, "bare AsyncCallable.call is not assignable to int");
	bool saw_bare_coro = false;
	for (const auto &error : bare_async_call_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot assign a value of type") && message.contains("Coroutine[")) {
			saw_bare_coro = true;
		}
	}
	CHECK_MESSAGE(saw_bare_coro, "bare AsyncCallable.call diagnose Coroutine wrap");
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(sync_call_ok, "res://tests/async_callable_wrap_validate.barista", true, true, true, false);
	CHECK_MESSAGE(bool(validate_report.get("valid", false)) == true, "async-callable wrap suite remains valid under validate()");
	CHECK_MESSAGE(source_analyzes(sync_call_ok, "res://tests/async_callable_wrap_is_valid.barista"), "async-callable wrap suite remains valid under is_semantically_valid()");
	CHECK_MESSAGE(index.get_record_count() == before, "analyze/validate/is_valid must not mutate declaration index for async-callable wrap");
}

void scenario_coroutine_annotation_decode() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String annotate_hold = "class_name CoroutineAnnotateHold extends Node\nasync func fetch() -> String:\n\treturn \"ok\"\nfunc test() -> void:\n\tvar work: Coroutine[String] = fetch.call()\n";
	const auto annotate_hold_report = analyze_source(annotate_hold, "res://tests/coroutine_annotate_hold.barista");
	CHECK_MESSAGE(annotate_hold_report.valid() == true, "var work: Coroutine[String] holds AsyncCallable.call result");
	const String annotate_await = "class_name CoroutineAnnotateAwait extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar work: Coroutine[int] = fetch.call()\n\tvar result: int = await work\n";
	const auto annotate_await_report = analyze_source(annotate_await, "res://tests/coroutine_annotate_await.barista");
	CHECK_MESSAGE(annotate_await_report.valid() == true, "await of Coroutine[int] annotation yields int");
	const String annotate_mismatch = "class_name CoroutineAnnotateMismatch extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar work: Coroutine[String] = fetch.call()\n";
	const auto annotate_mismatch_report = analyze_source(annotate_mismatch, "res://tests/coroutine_annotate_mismatch.barista");
	CHECK_MESSAGE(annotate_mismatch_report.valid() == true, "bare Coroutine[Variant] can enter Coroutine[String]");
	bool saw_coro_mismatch = false;
	for (const auto &error : annotate_mismatch_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot assign a value of type") && message.contains("Coroutine[int]") && message.contains("Coroutine[String]")) {
			saw_coro_mismatch = true;
		}
	}
	CHECK_MESSAGE((!saw_coro_mismatch && annotate_mismatch_report.parser->get_errors().is_empty()), "bare coroutine payload has no concrete int/String mismatch");
	const String annotate_no_await = "class_name CoroutineAnnotateNoAwait extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar work: Coroutine[int] = fetch.call()\n\tvar result: int = work\n";
	const auto annotate_no_await_report = analyze_source(annotate_no_await, "res://tests/coroutine_annotate_no_await.barista");
	CHECK_MESSAGE(annotate_no_await_report.valid() == false, "Coroutine[int] is not assignable to int without await");
	bool saw_no_await = false;
	for (const auto &error : annotate_no_await_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot assign a value of type") && message.contains("Coroutine[int]") && message.contains("variable \"result\" with specified type int")) {
			saw_no_await = true;
		}
	}
	CHECK_MESSAGE(saw_no_await, "annotated Coroutine[int] without await diagnoses assign to int");
	const String arity_empty = "class_name CoroutineArityEmpty extends Node\nfunc test() -> void:\n\tvar work: Coroutine[]\n";
	const auto arity_empty_report = analyze_source(arity_empty, "res://tests/coroutine_arity_empty.barista");
	CHECK_MESSAGE(arity_empty_report.valid() == false, "Coroutine[] wrong arity is invalid");
	bool saw_arity_empty = false;
	bool saw_m5_on_coro = false;
	for (const auto &error : arity_empty_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Coroutine[T]") && (message.contains("expects") || message.contains("single") || message.contains("exactly one"))) {
			saw_arity_empty = true;
		}
		if (message.contains("Generic type specialization is not available until M5")) {
			saw_m5_on_coro = true;
		}
	}
	CHECK_MESSAGE(saw_arity_empty, "Coroutine[] diagnoses wrong arity");
	CHECK_MESSAGE(!saw_m5_on_coro, "Coroutine[] must not emit M5 generic specialization error");
	const String arity_extra = "class_name CoroutineArityExtra extends Node\nfunc test() -> void:\n\tvar work: Coroutine[int, String]\n";
	const auto arity_extra_report = analyze_source(arity_extra, "res://tests/coroutine_arity_extra.barista");
	CHECK_MESSAGE(arity_extra_report.valid() == false, "Coroutine[int, String] wrong arity is invalid");
	bool saw_arity_extra = false;
	for (const auto &error : arity_extra_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Coroutine[T]") && (message.contains("more were given") || message.contains("single") || message.contains("expects"))) {
			saw_arity_extra = true;
		}
	}
	CHECK_MESSAGE(saw_arity_extra, "Coroutine[int, String] diagnoses wrong arity");
	const String array_ok = "class_name CoroutineArrayStillOk extends Node\nfunc test() -> void:\n\tvar a: Array[int] = [1]\n\tvar d: Dictionary[String, int] = {\"a\": 1}\n";
	const auto array_ok_report = analyze_source(array_ok, "res://tests/coroutine_array_still_ok.barista");
	CHECK_MESSAGE(array_ok_report.valid() == true, "Array/Dictionary containers still decode after Coroutine annotation path");
	const String other_generic = "class_name CoroutineOtherGeneric extends Node\nfunc test() -> void:\n\tvar x: NotAContainer[int]\n";
	const auto other_generic_report = analyze_source(other_generic, "res://tests/coroutine_other_generic.barista");
	CHECK_MESSAGE(other_generic_report.valid() == false, "non-Coroutine generic specialization stays invalid");
	bool saw_m5_other = false;
	for (const auto &error : other_generic_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Generic type specialization is not available until M5")) {
			saw_m5_other = true;
		}
	}
	CHECK_MESSAGE(saw_m5_other, "non-Coroutine generic still emits M5 deferred diagnostic");
	const String void_anno = "class_name CoroutineVoidAnnotate extends Node\nasync func fire() -> void:\n\tpass\nfunc test() -> void:\n\tvar work: Coroutine[void] = fire.call()\n";
	const auto void_anno_report = analyze_source(void_anno, "res://tests/coroutine_void_annotate.barista");
	CHECK_MESSAGE(void_anno_report.valid() == true, "var work: Coroutine[void] holds void async call");
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(annotate_hold, "res://tests/coroutine_annotate_validate.barista", true, true, true, false);
	CHECK_MESSAGE(bool(validate_report.get("valid", false)) == true, "coroutine annotation suite remains valid under validate()");
	CHECK_MESSAGE(source_analyzes(annotate_hold, "res://tests/coroutine_annotate_is_valid.barista"), "coroutine annotation suite remains valid under is_semantically_valid()");
	CHECK_MESSAGE(index.get_record_count() == before, "analyze/validate/is_valid must not mutate declaration index for coroutine annotation");
}
void scenario_await_reduction_and_missing_await() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	settings.warnings_enabled(true);
	settings.warning(BSWarning::MISSING_AWAIT, BSWarning::WARN);
	settings.warning(BSWarning::REDUNDANT_AWAIT, BSWarning::WARN);
	const String await_call_ok = "class_name AwaitCallOk extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = await fetch.call()\n";
	const auto await_call_ok_report = analyze_source(await_call_ok, "res://tests/await_call_ok.barista");
	CHECK_MESSAGE((await_call_ok_report.valid() == true), "await AsyncCallable.call unwraps Coroutine[int] to int");
	const String await_callv_ok = "class_name AwaitCallvOk extends Node\nasync func fetch(value: int) -> String:\n\treturn \"ok\"\nfunc test() -> void:\n\tvar result: String = await fetch.callv([1])\n";
	const auto await_callv_ok_report = analyze_source(await_callv_ok, "res://tests/await_callv_ok.barista");
	CHECK_MESSAGE((await_callv_ok_report.valid() == true), "await AsyncCallable.callv unwraps Coroutine[String] to String");
	const String await_bound_ok = "class_name AwaitBoundOk extends Node\nasync func add(a: int, b: int) -> int:\n\treturn a + b\nfunc test() -> void:\n\tvar result: int = await add.bind(1).call(2)\n";
	const auto await_bound_ok_report = analyze_source(await_bound_ok, "res://tests/await_bound_ok.barista");
	CHECK_MESSAGE((await_bound_ok_report.valid() == true), "await bound AsyncCallable.call unwraps to int");
	const String missing_await = "class_name MissingAwaitRoot extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tfetch.call()\n";
	const Dictionary missing_await_report = BaristaScriptLanguage::get_singleton()->_validate(missing_await, "res://tests/missing_await_root.barista", true, true, true, false);
	CHECK_MESSAGE((bool(missing_await_report.get("valid", false)) == true), "MISSING_AWAIT at warn level stays valid");
	bool saw_missing_await = false;
	for (const Dictionary &warn : Array(missing_await_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("MISSING_AWAIT") || (String(warn.get("message", "")).to_lower().contains("discarded") && String(warn.get("message", "")).to_lower().contains("await"))) {
			saw_missing_await = true;
		}
	}
	CHECK_MESSAGE((saw_missing_await), "root-position non-void coroutine discard emits MISSING_AWAIT");
	const String void_fire = "class_name VoidFireForget extends Node\nasync func fire() -> void:\n\tpass\nfunc test() -> void:\n\tfire.call()\n";
	const Dictionary void_fire_report = BaristaScriptLanguage::get_singleton()->_validate(void_fire, "res://tests/void_fire_forget.barista", true, true, true, false);
	CHECK_MESSAGE((bool(void_fire_report.get("valid", false)) == true), "bare Coroutine[Variant] fire-and-forget stays valid at WARN");
	bool saw_void_missing = false;
	for (const Dictionary &warn : Array(void_fire_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("MISSING_AWAIT")) {
			saw_void_missing = true;
		}
	}
	CHECK_MESSAGE((saw_void_missing), "bare Coroutine[Variant] root discard emits MISSING_AWAIT");
	const String deferred_no_missing = "class_name DeferredNoMissing extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tfetch.call_deferred()\n";
	const Dictionary deferred_no_missing_report = BaristaScriptLanguage::get_singleton()->_validate(deferred_no_missing, "res://tests/deferred_no_missing.barista", true, true, true, false);
	CHECK_MESSAGE((bool(deferred_no_missing_report.get("valid", false)) == true), "call_deferred statement stays valid");
	bool saw_deferred_missing = false;
	for (const Dictionary &warn : Array(deferred_no_missing_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("MISSING_AWAIT")) {
			saw_deferred_missing = true;
		}
	}
	CHECK_MESSAGE((!saw_deferred_missing), "call_deferred does not emit MISSING_AWAIT");
	const String redundant_await = "class_name RedundantAwaitInt extends Node\nfunc test() -> void:\n\tvar _x: int = await 1\n";
	const Dictionary redundant_await_report = BaristaScriptLanguage::get_singleton()->_validate(redundant_await, "res://tests/redundant_await_int.barista", true, true, true, false);
	CHECK_MESSAGE((bool(redundant_await_report.get("valid", false)) == true), "REDUNDANT_AWAIT at warn level stays valid");
	bool saw_redundant = false;
	for (const Dictionary &warn : Array(redundant_await_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("REDUNDANT_AWAIT") || (String(warn.get("message", "")).to_lower().contains("unnecessary") && String(warn.get("message", "")).to_lower().contains("await"))) {
			saw_redundant = true;
		}
	}
	CHECK_MESSAGE((saw_redundant), "await of plain int emits REDUNDANT_AWAIT");
	const String awaited_no_missing = "class_name AwaitedNoMissing extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar _result: int = await fetch.call()\n";
	const Dictionary awaited_no_missing_report = BaristaScriptLanguage::get_singleton()->_validate(awaited_no_missing, "res://tests/awaited_no_missing.barista", true, true, true, false);
	CHECK_MESSAGE((bool(awaited_no_missing_report.get("valid", false)) == true), "awaited AsyncCallable.call stays valid");
	bool saw_awaited_missing = false;
	for (const Dictionary &warn : Array(awaited_no_missing_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("MISSING_AWAIT")) {
			saw_awaited_missing = true;
		}
	}
	CHECK_MESSAGE((!saw_awaited_missing), "awaited coroutine call does not emit MISSING_AWAIT");
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(await_call_ok, "res://tests/await_reduce_validate.barista", true, true, true, false);
	CHECK_MESSAGE((bool(validate_report.get("valid", false)) == true), "await-reduction suite remains valid under validate()");
	CHECK_MESSAGE((source_analyzes(await_call_ok, "res://tests/await_reduce_is_valid.barista")), "await-reduction suite remains valid under is_semantically_valid()");
	CHECK_MESSAGE((index.get_record_count() == before), "analyze/validate/is_valid must not mutate declaration index for await reduction");
}

void scenario_direct_async_call_wrap() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	// The legacy predecessor enabled MISSING_AWAIT; make that profile explicit.
	settings.warnings_enabled(true);
	settings.warning(BSWarning::MISSING_AWAIT, BSWarning::WARN);
	const String bare_to_int = "class_name DirectAsyncToInt extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = fetch()\n";
	const auto bare_to_int_report = analyze_source(bare_to_int, "res://tests/direct_async_to_int.barista");
	CHECK_MESSAGE((bare_to_int_report.valid() == false), "bare async fetch() is not assignable to int");
	bool saw_bare_coro = false;
	for (const auto &error : bare_to_int_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot assign a value of type") && message.contains("Coroutine[int]") && message.contains("variable \"result\" with specified type int")) {
			saw_bare_coro = true;
		}
	}
	CHECK_MESSAGE((saw_bare_coro), "bare async fetch() diagnoses Coroutine[int] assign to int");
	const String bare_to_coro = "class_name DirectAsyncToCoro extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar work: Coroutine[int] = fetch()\n";
	const auto bare_to_coro_report = analyze_source(bare_to_coro, "res://tests/direct_async_to_coro.barista");
	CHECK_MESSAGE((bare_to_coro_report.valid() == true), "bare async fetch() is assignable to Coroutine[int]");
	const String await_bare = "class_name DirectAsyncAwait extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = await fetch()\n";
	const auto await_bare_report = analyze_source(await_bare, "res://tests/direct_async_await.barista");
	CHECK_MESSAGE((await_bare_report.valid() == true), "await bare async fetch() yields int");
	const String attr_to_int = "class_name DirectAsyncAttrToInt extends Node\nasync func fetch() -> String:\n\treturn \"ok\"\nfunc test() -> void:\n\tvar result: String = self.fetch()\n";
	const auto attr_to_int_report = analyze_source(attr_to_int, "res://tests/direct_async_attr_to_int.barista");
	CHECK_MESSAGE((attr_to_int_report.valid() == false), "self.fetch() async is not assignable to String");
	bool saw_attr_coro = false;
	for (const auto &error : attr_to_int_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Cannot assign a value of type") && message.contains("Coroutine[String]") && message.contains("variable \"result\" with specified type String")) {
			saw_attr_coro = true;
		}
	}
	CHECK_MESSAGE((saw_attr_coro), "self.fetch() diagnoses Coroutine[String] assign to String");
	const String missing_await = "class_name DirectAsyncMissingAwait extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tfetch()\n";
	const Dictionary missing_await_report = BaristaScriptLanguage::get_singleton()->_validate(missing_await, "res://tests/direct_async_missing_await.barista", true, true, true, false);
	CHECK_MESSAGE((bool(missing_await_report.get("valid", false)) == true), "MISSING_AWAIT at warn level stays valid for bare async");
	bool saw_missing_await = false;
	for (const Dictionary &warn : Array(missing_await_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("MISSING_AWAIT") || (String(warn.get("message", "")).to_lower().contains("discarded") && String(warn.get("message", "")).to_lower().contains("await"))) {
			saw_missing_await = true;
		}
	}
	CHECK_MESSAGE((saw_missing_await), "root discarded bare async call emits MISSING_AWAIT");
	const String void_fire = "class_name DirectAsyncVoidFire extends Node\nasync func fire() -> void:\n\tpass\nfunc test() -> void:\n\tfire()\n";
	const Dictionary void_fire_report = BaristaScriptLanguage::get_singleton()->_validate(void_fire, "res://tests/direct_async_void_fire.barista", true, true, true, false);
	CHECK_MESSAGE((bool(void_fire_report.get("valid", false)) == true), "void async fire-and-forget stays valid");
	bool saw_void_missing = false;
	for (const Dictionary &warn : Array(void_fire_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("MISSING_AWAIT")) {
			saw_void_missing = true;
		}
	}
	CHECK_MESSAGE((!saw_void_missing), "Coroutine[void] bare root discard does not emit MISSING_AWAIT");
	const String hold_no_missing = "class_name DirectAsyncHoldNoMissing extends Node\nasync func fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar work: Coroutine[int] = fetch()\n";
	const Dictionary hold_no_missing_report = BaristaScriptLanguage::get_singleton()->_validate(hold_no_missing, "res://tests/direct_async_hold_no_missing.barista", true, true, true, false);
	CHECK_MESSAGE((bool(hold_no_missing_report.get("valid", false)) == true), "held Coroutine[int] from bare async stays valid");
	bool saw_hold_missing = false;
	for (const Dictionary &warn : Array(hold_no_missing_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("MISSING_AWAIT")) {
			saw_hold_missing = true;
		}
	}
	CHECK_MESSAGE((!saw_hold_missing), "capture into Coroutine[int] does not emit MISSING_AWAIT");
	const String sync_ok = "class_name DirectSyncStillOk extends Node\nfunc fetch() -> int:\n\treturn 1\nfunc test() -> void:\n\tvar result: int = fetch()\n";
	const auto sync_ok_report = analyze_source(sync_ok, "res://tests/direct_sync_still_ok.barista");
	CHECK_MESSAGE((sync_ok_report.valid() == true), "sync local call still returns bare int");
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(bare_to_coro, "res://tests/direct_async_wrap_validate.barista", true, true, true, false);
	CHECK_MESSAGE((bool(validate_report.get("valid", false)) == true), "direct async-call wrap suite remains valid under validate()");
	CHECK_MESSAGE((source_analyzes(bare_to_coro, "res://tests/direct_async_wrap_is_valid.barista")), "direct async-call wrap suite remains valid under is_semantically_valid()");
	CHECK_MESSAGE((index.get_record_count() == before), "analyze/validate/is_valid must not mutate declaration index for direct async-call wrap");
}

} // namespace

TEST_SUITE("analyzer_calls") {
	TEST_CASE("direct_async_call_wrap") { scenario_direct_async_call_wrap(); }
	TEST_CASE("await_reduction_and_missing_await") { scenario_await_reduction_and_missing_await(); }
	TEST_CASE("callable_bind_unbind") { scenario_callable_bind_unbind(); }
	TEST_CASE("callable_callv_rpc") { scenario_callable_callv_rpc(); }
	TEST_CASE("async_callable_coroutine_wrap") { scenario_async_callable_coroutine_wrap(); }
	TEST_CASE("coroutine_annotation_decode") { scenario_coroutine_annotation_decode(); }
	TEST_CASE("call_arity_and_types") { scenario_call_arity_and_types(); }
	TEST_CASE("call_validation_methodinfo_and_signals") { scenario_call_validation_methodinfo_and_signals(); }
	TEST_CASE("named_arg_and_connect_callable") { scenario_named_arg_and_connect_callable(); }
	TEST_CASE("callable_signal_constructor_and_typed_receiver_depth") { scenario_callable_signal_constructor_and_typed_receiver_depth(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_call_arity_and_types, scenario_call_validation_methodinfo_and_signals, scenario_named_arg_and_connect_callable, scenario_callable_signal_constructor_and_typed_receiver_depth, scenario_callable_bind_unbind, scenario_callable_callv_rpc, scenario_async_callable_coroutine_wrap, scenario_coroutine_annotation_decode, scenario_await_reduction_and_missing_await, scenario_direct_async_call_wrap });
	}
}
