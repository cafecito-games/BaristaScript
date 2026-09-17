/**************************************************************************/
/*  runtime_test.cpp                                                      */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "runtime_helpers.h"
#include "test_require.h"

#include "bs_function.h"
#include "bs_script_instance.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace barista_script;
using namespace barista_script::native_tests;

namespace barista_script {

/**
 * Assembles a function the emitter refuses to write.
 *
 * Every reserved opcode is unreachable from source by construction, so the only way to prove the
 * runtime refuses one by name is to hand it a function that contains one.
 */
struct BSFunctionTestAccess {
	static BSFunction *make_single_opcode_function(BSFunction::Opcode p_opcode) {
		BSFunction *function = memnew(BSFunction);
		function->name = SNAME("probe");
		function->source = "res://runtime_probe.barista";
		function->stack_size = BSFunction::FIXED_ADDRESSES_MAX;
		function->instruction_arguments_size = 1;
		function->code.push_back(p_opcode);
		function->code.push_back(BSFunction::OPCODE_END);
		return function;
	}

	/** A single `super.<name>()` whose result is discarded, for the handler's own resolution path. */
	static BSFunction *make_super_call_function(const StringName &p_name) {
		BSFunction *function = memnew(BSFunction);
		function->name = SNAME("probe_super");
		function->source = "res://runtime_probe.barista";
		function->stack_size = BSFunction::FIXED_ADDRESSES_MAX;
		function->instruction_arguments_size = 1;
		function->global_names.push_back(p_name);
		function->code.push_back(BSFunction::OPCODE_CALL_SELF_BASE);
		function->code.push_back(1);
		function->code.push_back(BSFunction::ADDR_NIL);
		function->code.push_back(0);
		function->code.push_back(0);
		function->code.push_back(BSFunction::OPCODE_END);
		return function;
	}
};

} // namespace barista_script

namespace {

Ref<RefCounted> attach(const Ref<BaristaScript> &p_script) {
	Ref<RefCounted> owner;
	owner.instantiate();
	owner->set_script(p_script);
	return owner;
}

} // namespace

TEST_SUITE("runtime") {
	TEST_CASE("a compiled function runs and returns a value") {
		const Ref<BaristaScript> script = compile_script(
				"func add(a: int, b: int) -> int:\n"
				"\treturn a + b\n",
				"res://runtime/add.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		BS_TEST_REQUIRE(script->_can_instantiate());
		CHECK(script->_get_instance_base_type() == StringName("RefCounted"));

		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK(owner->call("add", 2, 40) == Variant(42));
	}

	TEST_CASE("a failed compilation leaves nothing instantiable") {
		const Ref<BaristaScript> script = compile_script(
				"func broken() -> int:\n"
				"\treturn \"not an int\"\n",
				"res://runtime/broken.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_FALSE(script->_can_instantiate());
		CHECK_FALSE(script->get_compile_error().is_empty());
		CHECK(script->_instance_create(nullptr) == nullptr);
		CHECK(script->_get_members().is_empty());
	}

	TEST_CASE("members, locals, control flow and iteration execute") {
		const Ref<BaristaScript> script = compile_script(
				"var total: int = 0\n"
				"var label: String = \"idle\"\n"
				"\n"
				"func accumulate(limit: int) -> int:\n"
				"\tvar running: int = 0\n"
				"\tfor step in range(limit):\n"
				"\t\trunning += step\n"
				"\tvar guard: int = 0\n"
				"\twhile guard < 3:\n"
				"\t\tguard += 1\n"
				"\tif running > 0:\n"
				"\t\tself.label = \"counted\"\n"
				"\telse:\n"
				"\t\tself.label = \"empty\"\n"
				"\tself.total = running + guard\n"
				"\treturn self.total\n"
				"\n"
				"func sum_list(values: Array) -> int:\n"
				"\tvar sum: int = 0\n"
				"\tfor value in values:\n"
				"\t\tsum += value\n"
				"\treturn sum\n",
				"res://runtime/flow.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		BS_TEST_REQUIRE(script->_can_instantiate());

		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK(owner->call("accumulate", 4) == Variant(9));
		CHECK(owner->get("total") == Variant(9));
		CHECK(owner->get("label") == Variant("counted"));

		Array values;
		values.push_back(1);
		values.push_back(2);
		values.push_back(3);
		CHECK(owner->call("sum_list", values) == Variant(6));
	}

	TEST_CASE("member initializers run when the instance is created") {
		const Ref<BaristaScript> script = compile_script(
				"var greeting: String = \"hello\"\n"
				"var count: int = 7\n"
				"\n"
				"func describe() -> String:\n"
				"\treturn self.greeting\n",
				"res://runtime/initializers.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK(owner->get("greeting") == Variant("hello"));
		CHECK(owner->get("count") == Variant(7));
		CHECK(owner->call("describe") == Variant("hello"));
	}

	TEST_CASE("a declared slot with no initializer holds a value of its own carrier") {
		const Ref<BaristaScript> script = compile_script(
				"var member_without_initializer: int\n"
				"\n"
				"func local_without_initializer() -> int:\n"
				"\tvar value: int\n"
				"\treturn value\n"
				"\n"
				"func text_without_initializer() -> String:\n"
				"\tvar value: String\n"
				"\treturn value\n",
				"res://runtime/uninitialized.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		const Variant zero = owner->call("local_without_initializer");
		CHECK(zero.get_type() == Variant::INT);
		CHECK(zero == Variant(0));
		const Variant empty = owner->call("text_without_initializer");
		CHECK(empty.get_type() == Variant::STRING);
		CHECK(empty == Variant(""));
		const Variant member = owner->get("member_without_initializer");
		CHECK(member.get_type() == Variant::INT);
		CHECK(member == Variant(0));
	}

	TEST_CASE("optional parameters take their declared defaults") {
		const Ref<BaristaScript> script = compile_script(
				"func greet(name: String, greeting: String = \"hello\", mark: String = \"!\") -> String:\n"
				"\treturn greeting + \" \" + name + mark\n",
				"res://runtime/defaults.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK(owner->call("greet", "world") == Variant("hello world!"));
		CHECK(owner->call("greet", "world", "hi") == Variant("hi world!"));
		CHECK(owner->call("greet", "world", "hi", "?") == Variant("hi world?"));
	}

	TEST_CASE("the logical operators short-circuit") {
		const Ref<BaristaScript> script = compile_script(
				"var right_hand_evaluations: int = 0\n"
				"\n"
				"func right_hand(value: bool) -> bool:\n"
				"\tself.right_hand_evaluations += 1\n"
				"\treturn value\n"
				"\n"
				"func both(left: bool, right: bool) -> bool:\n"
				"\treturn left and self.right_hand(right)\n"
				"\n"
				"func either(left: bool, right: bool) -> bool:\n"
				"\treturn left or self.right_hand(right)\n",
				"res://runtime/short_circuit.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());

		CHECK(owner->call("both", false, true) == Variant(false));
		CHECK(owner->get("right_hand_evaluations") == Variant(0));
		CHECK(owner->call("both", true, true) == Variant(true));
		CHECK(owner->call("both", true, false) == Variant(false));
		CHECK(owner->get("right_hand_evaluations") == Variant(2));

		CHECK(owner->call("either", true, false) == Variant(true));
		CHECK(owner->get("right_hand_evaluations") == Variant(2));
		CHECK(owner->call("either", false, true) == Variant(true));
		CHECK(owner->call("either", false, false) == Variant(false));
		CHECK(owner->get("right_hand_evaluations") == Variant(4));
	}

	TEST_CASE("print reaches the engine's utility functions") {
		const Ref<BaristaScript> script = compile_script(
				"func announce() -> void:\n"
				"\tprint(\"BS_RUNTIME_PRINT\")\n",
				"res://runtime/print.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		RuntimeErrorScope errors;
		owner->call("announce");
		CHECK_MESSAGE(errors.errors().is_empty(), readable(errors.joined()));
	}

	TEST_CASE("a member initializer can call the script's own functions") {
		const Ref<BaristaScript> script = compile_script(
				"var ratio: float = int_factory()\n"
				"var doubled: int = 0\n"
				"\n"
				"func int_factory() -> int:\n"
				"\treturn 2\n"
				"\n"
				"func _init() -> void:\n"
				"\tself.doubled = self.int_factory() * 2\n",
				"res://runtime/self_call_initializer.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		RuntimeErrorScope errors;
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK_MESSAGE(errors.errors().is_empty(), readable(errors.joined()));
		CHECK(owner->get("ratio").get_type() == Variant::FLOAT);
		CHECK(owner->get("ratio") == Variant(2.0));
		CHECK(owner->get("doubled") == Variant(4));
	}

	TEST_CASE("an initializer that raises leaves no instance behind") {
		const Ref<BaristaScript> script = compile_script(
				"var slot: int = 0\n"
				"\n"
				"func _init() -> void:\n"
				"\tself.slot = self.missing_thing()\n"
				"\n"
				"func missing_thing() -> int:\n"
				"\tvar values: Array = []\n"
				"\treturn values[4]\n",
				"res://runtime/raising_initializer.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));

		RuntimeErrorScope errors;
		Ref<RefCounted> owner;
		owner.instantiate();
		CHECK(script->_instance_create(owner.ptr()) == nullptr);
		CHECK(script->get_instance_count() == 0);
		CHECK_FALSE(errors.errors().is_empty());
	}

	TEST_CASE("a slot the runtime cannot check is refused by name") {
		struct Row {
			const char *source;
			const char *path;
		};
		const Row rows[] = {
			{ "func take(value: (int, String)) -> int:\n\treturn 7\n",
					"res://runtime/tuple_parameter.barista" },
			{ "func take(value: Variant) -> int:\n\tvar copy: (int, String) = value\n\treturn 7\n",
					"res://runtime/tuple_local.barista" },
			{ "var pair: (int, String)\n\nfunc run() -> int:\n\treturn 7\n",
					"res://runtime/tuple_member.barista" },
		};
		for (const Row &row : rows) {
			const Ref<BaristaScript> script = compile_script(row.source, row.path);
			BS_TEST_REQUIRE(script.is_valid());
			CHECK_MESSAGE(!script->_can_instantiate(), row.path);
			CHECK_FALSE(script->get_compile_error().is_empty());
		}
	}

	TEST_CASE("a super call with no script implementation reports instead of recursing") {
		const Ref<BaristaScript> script = compile_script(
				"var slot: int = 0\n"
				"\n"
				"func probe() -> int:\n"
				"\treturn 1\n",
				"res://runtime/super_fallback.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		BSInstance *instance = BSInstance::from_owner(owner.ptr());
		BS_TEST_REQUIRE(instance != nullptr);

		// The analyzer refuses every `super` form this runtime can reach, so the handler's own
		// resolution is exercised directly. The script declares `probe`, and `Object::has_method`
		// answers for the script instance before ClassDB does, so a fallback that asks the owner
		// would call this very function again.
		RuntimeErrorScope errors;
		BSFunction *function = BSFunctionTestAccess::make_super_call_function(SNAME("probe"));
		BS_TEST_REQUIRE(function != nullptr);
		GDExtensionCallError error;
		function->call(instance, nullptr, 0, error);
		memdelete(function);
		CHECK_MESSAGE(errors.has_error_containing("probe"), readable(errors.joined()));
		CHECK_MESSAGE(!errors.has_error_containing("Stack overflow"), readable(errors.joined()));
		CHECK(errors.errors().size() == 1);
	}

	TEST_CASE("a fault under a call does not abandon the frame that made it") {
		const Ref<BaristaScript> healthy = compile_script(
				"var after: int = 0\n"
				"\n"
				"func run() -> int:\n"
				"\tself.emit_signal(\"script_changed\")\n"
				"\tself.after = 7\n"
				"\treturn 7\n",
				"res://runtime/healthy_frame.barista");
		const Ref<BaristaScript> raiser = compile_script(
				"func boom() -> int:\n"
				"\tvar values: Array = []\n"
				"\treturn values[4]\n",
				"res://runtime/raising_listener.barista");
		BS_TEST_REQUIRE(healthy->_can_instantiate());
		BS_TEST_REQUIRE(raiser->_can_instantiate());

		const Ref<RefCounted> owner = attach(healthy);
		const Ref<RefCounted> listener = attach(raiser);
		BS_TEST_REQUIRE(owner.is_valid());
		BS_TEST_REQUIRE(listener.is_valid());
		CHECK(owner->connect("script_changed", Callable(listener.ptr(), "boom")) == OK);

		RuntimeErrorScope errors;
		// The listener raises while `run` is emitting. That is the listener's fault to report, and
		// `run` has no reason to stop: a shared "something failed" bit read as "my callee failed"
		// abandons a frame whose own call completed.
		CHECK(owner->call("run") == Variant(7));
		CHECK(owner->get("after") == Variant(7));
		CHECK(errors.errors().size() == 1);
		CHECK_MESSAGE(errors.has_error_containing("Invalid index"), readable(errors.joined()));
	}

	TEST_CASE("subscripting reads and writes a sequence and a map") {
		const Ref<BaristaScript> script = compile_script(
				"func first(values: Array) -> int:\n"
				"\treturn values[0]\n"
				"\n"
				"func replace_first(values: Array, value: int) -> Array:\n"
				"\tvalues[0] = value\n"
				"\treturn values\n"
				"\n"
				"func look_up(entries: Dictionary, key: String) -> int:\n"
				"\treturn entries[key]\n"
				"\n"
				"func sum_all(values: Array) -> int:\n"
				"\tvar total: int = 0\n"
				"\tvar position: int = 0\n"
				"\twhile position < 3:\n"
				"\t\ttotal += values[position]\n"
				"\t\tposition += 1\n"
				"\treturn total\n",
				"res://runtime/subscript.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());

		RuntimeErrorScope errors;
		Array values;
		values.push_back(10);
		values.push_back(20);
		values.push_back(30);
		CHECK(owner->call("first", values) == Variant(10));
		CHECK(owner->call("sum_all", values) == Variant(60));
		const Array replaced = owner->call("replace_first", values, 99);
		CHECK(replaced[0] == Variant(99));

		Dictionary entries;
		entries["gold"] = 3;
		CHECK(owner->call("look_up", entries, "gold") == Variant(3));
		CHECK_MESSAGE(errors.errors().is_empty(), readable(errors.joined()));
	}

	TEST_CASE("a native slot refuses a value that is not of its class") {
		const Ref<BaristaScript> script = compile_script(
				"var holder: RefCounted\n"
				"\n"
				"func store(value: Variant) -> int:\n"
				"\tself.holder = value\n"
				"\treturn 1\n",
				"res://runtime/native_slot.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		// An object slot with nothing in it holds null, not the placeholder an untyped slot takes.
		CHECK(owner->get("holder").get_type() == Variant::NIL);

		Ref<RefCounted> stored;
		stored.instantiate();
		CHECK(owner->call("store", stored) == Variant(1));
		CHECK(owner->get("holder") == Variant(stored));

		RuntimeErrorScope errors;
		owner->call("store", 5);
		CHECK_MESSAGE(errors.has_error_containing("RefCounted"), readable(errors.joined()));
		CHECK(owner->get("holder") == Variant(stored));
	}

	TEST_CASE("a declared function keeps its name from an engine utility") {
		const Ref<BaristaScript> script = compile_script(
				"func abs(value: int) -> int:\n"
				"\tif value < 0:\n"
				"\t\treturn 0 - value\n"
				"\treturn value\n"
				"\n"
				"func run() -> int:\n"
				"\treturn abs(-5)\n",
				"res://runtime/shadowed_utility.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		RuntimeErrorScope errors;
		CHECK(owner->call("run") == Variant(5));
		CHECK_MESSAGE(errors.errors().is_empty(), readable(errors.joined()));
	}

	TEST_CASE("a utility this runtime cannot reach is refused where it is written") {
		const Ref<BaristaScript> script = compile_script(
				"func run() -> int:\n"
				"\treturn abs(-5)\n",
				"res://runtime/unreachable_utility.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_FALSE(script->_can_instantiate());
		const String diagnostic = script->get_compile_error();
		CHECK_MESSAGE(diagnostic.contains("abs"), readable(diagnostic));
	}

	TEST_CASE("an initializer can call through a local that holds the receiver") {
		const Ref<BaristaScript> script = compile_script(
				"var value: int = 0\n"
				"\n"
				"func helper() -> int:\n"
				"\treturn 3\n"
				"\n"
				"func _init() -> void:\n"
				"\tvar me: RefCounted = self\n"
				"\tself.value = me.helper()\n",
				"res://runtime/aliased_self.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		RuntimeErrorScope errors;
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK_MESSAGE(errors.errors().is_empty(), readable(errors.joined()));
		CHECK(owner->get("value") == Variant(3));
	}

	TEST_CASE("an opcode with no handler names itself") {
		RuntimeErrorScope errors;
		// A reserved validated opcode: the emitter never writes one, and the runtime must refuse it
		// rather than fall through to whatever follows in the switch.
		BSFunction *function = BSFunctionTestAccess::make_single_opcode_function(BSFunction::OPCODE_OPERATOR_VALIDATED);
		BS_TEST_REQUIRE(function != nullptr);
		GDExtensionCallError error;
		const Variant result = function->call(nullptr, nullptr, 0, error);
		memdelete(function);

		CHECK(result == Variant());
		// The call itself completed; the fault is reported on the script-error channel, which is why
		// the call error stays OK -- a call-error code would surface a second time as a missing
		// method that in fact exists.
		CHECK(error.error == GDEXTENSION_CALL_OK);
		CHECK(bs_runtime_error_was_reported());
		CHECK_MESSAGE(errors.has_error_containing("Opcode not implemented: OPCODE_OPERATOR_VALIDATED."),
				readable(errors.joined()));
		CHECK(errors.errors().size() == 1);
	}

	TEST_CASE("every opcode enumerator has a name") {
		for (int opcode = 0; opcode < BSFunction::OPCODE_MAX; opcode++) {
			const String name = BSFunction::get_opcode_name(opcode);
			CHECK(name.begins_with("OPCODE_"));
			CHECK_FALSE(name.contains("UNKNOWN"));
		}
		CHECK(BSFunction::get_opcode_name(BSFunction::OPCODE_MAX).contains("UNKNOWN"));
		// The frozen count: every non-numeric, non-generic opcode plus the list's terminator.
		CHECK(BSFunction::OPCODE_MAX == 172);
		CHECK(BSFunction::OPCODE_END == BSFunction::OPCODE_MAX - 1);
	}

	TEST_CASE("a script whose base is a GDScript class is refused by name") {
		// Resolving a base by path leaves that file's parser in the shared cache; the case puts the
		// process back the way it found it.
		RuntimeCacheScope cache_scope;
		const Ref<BaristaScript> script = compile_script(
				"extends \"res://tests/runtime_fixtures/gdscript_base.gd\"\n"
				"\n"
				"func run() -> int:\n"
				"\treturn 1\n",
				"res://runtime/gdscript_base.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_FALSE(script->_can_instantiate());
		const String diagnostic = script->get_compile_error();
		CHECK_MESSAGE(diagnostic.contains("gdscript_base.gd"), readable(diagnostic));
		CHECK_MESSAGE(diagnostic.contains("GDScript"), readable(diagnostic));
		CHECK(script->_instance_create(nullptr) == nullptr);
	}

	TEST_CASE("an object holds one script instance at a time") {
		// What the engine does when a second script is attached, recorded rather than assumed: the
		// new instance replaces the old one outright. There is no arrangement in which a
		// BaristaScript instance and a GDScript instance both answer for the same object, which is
		// why a GDScript base is refused rather than delegated to.
		const Ref<BaristaScript> first = compile_script(
				"var marker: int = 1\n",
				"res://runtime/one_instance_first.barista");
		const Ref<BaristaScript> second = compile_script(
				"var other_marker: int = 2\n",
				"res://runtime/one_instance_second.barista");
		BS_TEST_REQUIRE(first->_can_instantiate());
		BS_TEST_REQUIRE(second->_can_instantiate());

		Ref<RefCounted> owner;
		owner.instantiate();
		owner->set_script(first);
		CHECK(owner->get("marker") == Variant(1));
		CHECK(first->_instance_has(owner.ptr()));

		owner->set_script(second);
		CHECK(owner->get("other_marker") == Variant(2));
		CHECK(owner->get("marker") == Variant());
		CHECK_FALSE(first->_instance_has(owner.ptr()));
		CHECK(second->_instance_has(owner.ptr()));
	}

	TEST_CASE("every script-instance callback is reachable and the owner round-trips") {
		const Ref<BaristaScript> script = compile_script(
				"var slot: int = 3\n"
				"\n"
				"func _notification(what: int) -> void:\n"
				"\tself.slot = what\n"
				"\n"
				"func twice(value: int) -> int:\n"
				"\treturn value * 2\n",
				"res://runtime/instance_abi.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());

		// The round-trip: the engine hands back exactly the instance data pointer it was given.
		BSInstance *instance = BSInstance::from_owner(owner.ptr());
		BS_TEST_REQUIRE(instance != nullptr);
		CHECK(instance->owner == owner.ptr());
		CHECK(script->_instance_has(owner.ptr()));

		const GDExtensionScriptInstanceInfo3 *vtable = BSInstance::get_vtable();
		BS_TEST_REQUIRE(BSInstance::vtable_is_complete());

		const StringName slot_name("slot");
		const Variant stored = 11;
		CHECK(vtable->set_func(instance, &slot_name, &stored));
		Variant read;
		CHECK(vtable->get_func(instance, &slot_name, &read));
		CHECK(read == Variant(11));

		uint32_t property_count = 0;
		const GDExtensionPropertyInfo *properties = vtable->get_property_list_func(instance, &property_count);
		CHECK(property_count == 1);
		vtable->free_property_list_func(instance, properties, property_count);

		GDExtensionPropertyInfo category = {};
		CHECK_FALSE(vtable->get_class_category_func(instance, &category));

		GDExtensionBool type_is_valid = false;
		CHECK(vtable->get_property_type_func(instance, &slot_name, &type_is_valid) == GDEXTENSION_VARIANT_TYPE_INT);
		CHECK(type_is_valid);

		GDExtensionPropertyInfo validated = {};
		CHECK_FALSE(vtable->validate_property_func(instance, &validated));
		CHECK_FALSE(vtable->property_can_revert_func(instance, &slot_name));
		Variant revert;
		CHECK_FALSE(vtable->property_get_revert_func(instance, &slot_name, &revert));

		CHECK(vtable->get_owner_func(instance) == owner->_owner);

		int state_entries = 0;
		vtable->get_property_state_func(
				instance,
				[](GDExtensionConstStringNamePtr, GDExtensionConstVariantPtr, void *p_userdata) {
					(*reinterpret_cast<int *>(p_userdata))++;
				},
				&state_entries);
		CHECK(state_entries == 1);

		uint32_t method_count = 0;
		const GDExtensionMethodInfo *methods = vtable->get_method_list_func(instance, &method_count);
		CHECK(method_count == 2);
		vtable->free_method_list_func(instance, methods, method_count);

		const StringName twice_name("twice");
		CHECK(vtable->has_method_func(instance, &twice_name));
		GDExtensionBool argument_count_is_valid = false;
		CHECK(vtable->get_method_argument_count_func(instance, &twice_name, &argument_count_is_valid) == 1);
		CHECK(argument_count_is_valid);

		const Variant argument = 21;
		const GDExtensionConstVariantPtr call_arguments[1] = { &argument };
		Variant call_result;
		GDExtensionCallError call_error;
		vtable->call_func(instance, &twice_name, call_arguments, 1, &call_result, &call_error);
		CHECK(call_error.error == GDEXTENSION_CALL_OK);
		CHECK(call_result == Variant(42));

		vtable->notification_func(instance, 1234, false);
		CHECK(owner->get("slot") == Variant(1234));
		vtable->notification_func(instance, 4321, true);
		CHECK(owner->get("slot") == Variant(4321));

		GDExtensionBool to_string_is_valid = false;
		String text;
		vtable->to_string_func(instance, &to_string_is_valid, &text);
		CHECK(to_string_is_valid);
		CHECK(text.contains("BaristaScript"));

		vtable->refcount_incremented_func(instance);
		CHECK(vtable->refcount_decremented_func(instance));
		CHECK(vtable->get_script_func(instance) == script->_owner);
		CHECK_FALSE(vtable->is_placeholder_func(instance));
		CHECK_FALSE(vtable->set_fallback_func(instance, &slot_name, &stored));
		Variant fallback;
		CHECK_FALSE(vtable->get_fallback_func(instance, &slot_name, &fallback));
		CHECK(vtable->get_language_func(instance) != nullptr);
		CHECK(vtable->free_func != nullptr);
	}

	TEST_CASE("freeing an instance releases it from its script") {
		const Ref<BaristaScript> script = compile_script(
				"var slot: int = 1\n",
				"res://runtime/lifetime.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK(script->get_instance_count() == 0);
		{
			const Ref<RefCounted> owner = attach(script);
			BS_TEST_REQUIRE(owner.is_valid());
			CHECK(script->_instance_has(owner.ptr()));
			CHECK(script->get_instance_count() == 1);
		}
		// The owner is gone, so the engine has run the vtable's free callback. The count is what
		// proves it: asking whether some other object has an instance would pass even if the script
		// were still holding the freed pointer, which is the defect this rules out.
		CHECK(script->get_instance_count() == 0);

		// A positive control, so a count that is always zero cannot pass this case either.
		Ref<RefCounted> replacement;
		replacement.instantiate();
		replacement->set_script(script);
		CHECK(script->get_instance_count() == 1);
		CHECK(script->_instance_has(replacement.ptr()));
	}

	TEST_CASE("a declared carrier converts the value it is initialized with") {
		// The initializers are non-constant on purpose: a constant one is folded to the slot's own
		// carrier by the analyzer, so only a value produced at run time can show whether the store
		// converts.
		const Ref<BaristaScript> script = compile_script(
				"var whole: int = 1\n"
				"var ratio: float = self.whole\n"
				"\n"
				"func halved() -> float:\n"
				"\treturn self.ratio / 2\n"
				"\n"
				"func local_ratio(from: int) -> float:\n"
				"\tvar value: float = from\n"
				"\treturn value / 2\n",
				"res://runtime/conversion.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		// An integer 1 stored in a float member would divide as an integer and answer 0.
		CHECK(owner->get("ratio").get_type() == Variant::FLOAT);
		CHECK(owner->call("halved") == Variant(0.5));
		CHECK(owner->call("local_ratio", 1) == Variant(0.5));
	}

	TEST_CASE("a loop variable converts the element it is given") {
		const Ref<BaristaScript> script = compile_script(
				"func halve_each(values: Array) -> float:\n"
				"\tvar total: float = 0.0\n"
				"\tfor value: float in values:\n"
				"\t\ttotal += value / 2\n"
				"\treturn total\n"
				"\n"
				"func halve_range(limit: int) -> float:\n"
				"\tvar total: float = 0.0\n"
				"\tfor step: float in range(limit):\n"
				"\t\ttotal += step / 2\n"
				"\treturn total\n",
				"res://runtime/loop_conversion.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());

		Array values;
		values.push_back(1);
		values.push_back(3);
		// Integer elements left in a float loop variable would halve to 0 and 1 rather than 0.5 and 1.5.
		CHECK(owner->call("halve_each", values) == Variant(2.0));
		CHECK(owner->call("halve_range", 4) == Variant(3.0));
	}

	TEST_CASE("a member kind the runtime cannot compile is refused by name") {
		struct Row {
			const char *source;
			const char *path;
			const char *named;
		};
		const Row rows[] = {
			{ "signal done(value: int)\n\nfunc run() -> int:\n\treturn 1\n",
					"res://runtime/member_signal.barista", "done" },
			{ "enum Kind:\n\tA = 0\n\tB = 1\n\nfunc run() -> int:\n\treturn 1\n",
					"res://runtime/member_enum.barista", "Kind" },
			{ "class Inner:\n\tvar value: int = 1\n\nfunc run() -> int:\n\treturn 1\n",
					"res://runtime/member_class.barista", "Inner" },
		};
		for (const Row &row : rows) {
			const Ref<BaristaScript> script = compile_script(row.source, row.path);
			BS_TEST_REQUIRE(script.is_valid());
			CHECK_FALSE(script->_can_instantiate());
			const String diagnostic = script->get_compile_error();
			CHECK_MESSAGE(diagnostic.contains(row.named), readable(diagnostic));
			CHECK(script->_instance_create(nullptr) == nullptr);
		}
	}

	TEST_CASE("an abstract class cannot be instantiated through the engine's own entry point") {
		const Ref<BaristaScript> script = compile_script(
				"abstract class_name RuntimeAbstractProbe\n"
				"\n"
				"func run() -> int:\n"
				"\treturn 1\n",
				"res://runtime/abstract.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK(script->_is_abstract());
		CHECK_FALSE(script->_can_instantiate());

		// `Object::set_script` reaches `_instance_create` directly and never consults
		// `_can_instantiate`, so the refusal has to live in both.
		Ref<RefCounted> owner;
		owner.instantiate();
		CHECK(script->_instance_create(owner.ptr()) == nullptr);
		owner->set_script(script);
		CHECK(script->get_instance_count() == 0);
		CHECK_FALSE(script->_instance_has(owner.ptr()));
	}
}
