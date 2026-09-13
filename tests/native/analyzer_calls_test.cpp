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
} // namespace

TEST_SUITE("analyzer_calls") {
	TEST_CASE("call_arity_and_types") { scenario_call_arity_and_types(); }
	TEST_CASE("call_validation_methodinfo_and_signals") { scenario_call_validation_methodinfo_and_signals(); }
	TEST_CASE("named_arg_and_connect_callable") { scenario_named_arg_and_connect_callable(); }
	TEST_CASE("callable_signal_constructor_and_typed_receiver_depth") { scenario_callable_signal_constructor_and_typed_receiver_depth(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_call_arity_and_types, scenario_call_validation_methodinfo_and_signals, scenario_named_arg_and_connect_callable, scenario_callable_signal_constructor_and_typed_receiver_depth });
	}
}
