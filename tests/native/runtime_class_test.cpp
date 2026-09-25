/**************************************************************************/
/*  runtime_class_test.cpp                                                */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "runtime_helpers.h"
#include "bs_script_instance.h"
#include "test_require.h"

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/ref_counted.hpp>

using namespace barista_script;
using namespace barista_script::native_tests;

namespace {

Dictionary find_named(const TypedArray<Dictionary> &p_entries, const StringName &p_name) {
	for (const Dictionary &entry : p_entries) {
		if (StringName(entry.get("name", StringName())) == p_name) {
			return entry;
		}
	}
	return Dictionary();
}

Ref<RefCounted> attach(const Ref<BaristaScript> &p_script) {
	Ref<RefCounted> owner;
	owner.instantiate();
	owner->set_script(p_script);
	return owner;
}

} // namespace

TEST_SUITE("runtime_class") {
	TEST_CASE("compiled class metadata preserves declarations and varargs") {
		const Ref<BaristaScript> script = compile_script(
				"const ANSWER: int = 42\n"
				"var count: int = 7\n"
				"@export_range(0.0, 10.0, 0.5) var speed: float = 2.5\n"
				"signal changed(value: int)\n"
				"func combine(first: int, second: int = 3, ...rest: Array[int]) -> int:\n"
				"\treturn first + second + rest.size()\n",
				"res://runtime_class/metadata.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->_can_instantiate(), readable(script->get_compile_error()));

		const Dictionary constants = script->_get_constants();
		CHECK(constants.get("ANSWER", Variant()) == Variant(42));
		CHECK(script->_get_member_line("count") == 2);
		CHECK(script->_get_member_line("changed") == 4);
		CHECK(script->_get_member_line("combine") == 5);

		CHECK(script->_has_property_default_value("count"));
		CHECK(script->_get_property_default_value("count") == Variant(7));
		CHECK(script->_has_property_default_value("speed"));
		CHECK(script->_get_property_default_value("speed") == Variant(2.5));

		const Dictionary speed = find_named(script->_get_script_property_list(), "speed");
		BS_TEST_REQUIRE(!speed.is_empty());
		CHECK(int(speed.get("type", -1)) == Variant::FLOAT);
		CHECK(int(speed.get("hint", -1)) == PROPERTY_HINT_RANGE);
		const String range_hint = speed.get("hint_string", "");
		CHECK_MESSAGE(range_hint == "0.0,10.0,0.5", readable(range_hint));
		CHECK((int(speed.get("usage", 0)) & PROPERTY_USAGE_SCRIPT_VARIABLE) != 0);

		CHECK(script->_has_script_signal("changed"));
		const Dictionary changed = find_named(script->_get_script_signal_list(), "changed");
		BS_TEST_REQUIRE(!changed.is_empty());
		const Array signal_arguments = changed.get("args", Array());
		BS_TEST_REQUIRE(signal_arguments.size() == 1);
		CHECK(StringName(Dictionary(signal_arguments[0]).get("name", StringName())) == StringName("value"));
		CHECK(int(Dictionary(signal_arguments[0]).get("type", -1)) == Variant::INT);

		const Dictionary combine = script->_get_method_info("combine");
		BS_TEST_REQUIRE(!combine.is_empty());
		CHECK((int(combine.get("flags", 0)) & METHOD_FLAG_VARARG) != 0);
		CHECK(Array(combine.get("args", Array())).size() == 2);
		CHECK(Array(combine.get("default_args", Array())).size() == 1);
		CHECK(Array(combine.get("default_args", Array()))[0] == Variant(3));
		CHECK(int(Dictionary(combine.get("return", Dictionary())).get("type", -1)) == Variant::INT);
		CHECK(!find_named(script->_get_script_method_list(), "combine").is_empty());

		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		BSInstance *instance = BSInstance::from_owner(owner.ptr());
		BS_TEST_REQUIRE(instance != nullptr);
		uint32_t method_count = 0;
		const GDExtensionMethodInfo *methods = BSInstance::get_vtable()->get_method_list_func(instance, &method_count);
		const GDExtensionMethodInfo *compiled_combine = nullptr;
		for (uint32_t index = 0; index < method_count; index++) {
			if (*reinterpret_cast<const StringName *>(methods[index].name) == SNAME("combine")) {
				compiled_combine = &methods[index];
				break;
			}
		}
		CHECK(compiled_combine != nullptr);
		if (compiled_combine != nullptr) {
			CHECK((compiled_combine->flags & METHOD_FLAG_VARARG) != 0);
			CHECK(compiled_combine->argument_count == 2);
			CHECK(*reinterpret_cast<const StringName *>(compiled_combine->arguments[0].name) == SNAME("first"));
			CHECK(compiled_combine->arguments[0].type == GDEXTENSION_VARIANT_TYPE_INT);
			CHECK(compiled_combine->default_argument_count == 1);
			CHECK(*reinterpret_cast<const Variant *>(compiled_combine->default_arguments[0]) == Variant(3));
			CHECK(compiled_combine->return_value.type == GDEXTENSION_VARIANT_TYPE_INT);
		}
		BSInstance::get_vtable()->free_method_list_func(instance, methods, method_count);
	}

	TEST_CASE("property accessors and revert hooks use the compiled member surface") {
		const Ref<BaristaScript> script = compile_script(
				"var stored: int = 2\n"
				"var value: int:\n"
				"\tget:\n"
				"\t\treturn stored * 2\n"
				"\tset(next):\n"
				"\t\tstored = next + 1\n"
				"func _property_can_revert(name: StringName) -> bool:\n"
				"\treturn name == &\"value\"\n"
				"func _property_get_revert(name: StringName) -> Variant:\n"
				"\tif name == &\"value\":\n"
				"\t\treturn 8\n"
				"\treturn null\n"
				"func _validate_property(property: Dictionary) -> void:\n"
				"\tif property.name == &\"value\":\n"
				"\t\tproperty.hint = PROPERTY_HINT_RANGE\n"
				"\t\tproperty.hint_string = \"0,20,1\"\n"
				"func _to_string() -> String:\n"
				"\treturn \"accessor-instance\"\n"
				"func write_value(next: int) -> void:\n"
				"\tvalue = next\n"
				"func read_value() -> int:\n"
				"\treturn value\n",
				"res://runtime_class/accessors.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->_can_instantiate(), readable(script->get_compile_error()));

		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK(owner->get("value") == Variant(4));
		owner->set("value", 5);
		CHECK(owner->get("stored") == Variant(6));
		CHECK(owner->get("value") == Variant(12));
		owner->call("write_value", 9);
		CHECK(owner->get("stored") == Variant(10));
		CHECK(owner->call("read_value") == Variant(20));
		CHECK(owner->property_can_revert("value"));
		CHECK(owner->property_get_revert("value") == Variant(8));

		const Dictionary value = find_named(owner->get_property_list(), "value");
		BS_TEST_REQUIRE(!value.is_empty());
		CHECK(int(value.get("hint", -1)) == PROPERTY_HINT_RANGE);
		CHECK(String(value.get("hint_string", "")) == "0,20,1");
		CHECK(String(Variant(owner).operator String()) == "accessor-instance");
	}

	TEST_CASE("an accessor can retain and release an object through its backing slot") {
		const Ref<BaristaScript> script = compile_script(
				"var node: Node:\n"
				"\tget:\n"
				"\t\treturn node\n"
				"\tset(next):\n"
				"\t\tnode = next\n"
				"func assign() -> void:\n"
				"\tnode = Node.new()\n"
				"func release_and_check() -> bool:\n"
				"\tnode.free()\n"
				"\treturn !is_instance_valid(node)\n",
				"res://runtime_class/object_accessor.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->_can_instantiate(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		owner->call("assign");
		BSInstance *instance = BSInstance::from_owner(owner.ptr());
		BS_TEST_REQUIRE(instance != nullptr);
		BS_TEST_REQUIRE(instance->members.size() == 1);
		CHECK(instance->members[0].get_type() == Variant::OBJECT);
		CHECK(instance->members[0].get_validated_object() != nullptr);
		RuntimeErrorScope errors;
		CHECK(owner->call("release_and_check") == Variant(true));
		CHECK_MESSAGE(errors.errors().is_empty(), readable(errors.joined()));
	}

	TEST_CASE("static initialization runs once and survives instance destruction") {
		const Ref<BaristaScript> script = compile_script(
				"static var count: int = 2\n"
				"static func _static_init() -> void:\n"
				"\tcount += 3\n"
				"static func bump() -> int:\n"
				"\tcount += 1\n"
				"\treturn count\n"
				"func next() -> int:\n"
				"\treturn bump()\n",
				"res://runtime_class/statics.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->_can_instantiate(), readable(script->get_compile_error()));

		{
			const Ref<RefCounted> first = attach(script);
			BS_TEST_REQUIRE(first.is_valid());
			CHECK(first->call("next") == Variant(6));
			const Ref<RefCounted> second = attach(script);
			BS_TEST_REQUIRE(second.is_valid());
			CHECK(second->call("next") == Variant(7));
		}
		const Ref<RefCounted> replacement = attach(script);
		BS_TEST_REQUIRE(replacement.is_valid());
		CHECK(replacement->call("next") == Variant(8));
	}

	TEST_CASE("static initialization follows inheritance and fails closed") {
		const Ref<BaristaScript> ordered = compile_script(
				"class Child extends Base:\n"
				"\tstatic var derived: int = Base.base_value + 1\n"
				"class Base:\n"
				"\tstatic var base_value: int = 4\n"
				"func read() -> int:\n"
				"\treturn Child.derived\n",
				"res://runtime_class/static_order.barista");
		BS_TEST_REQUIRE(ordered.is_valid());
		CHECK_MESSAGE(ordered->_can_instantiate(), readable(ordered->get_compile_error()));
		const Ref<RefCounted> owner = attach(ordered);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK(owner->call("read") == Variant(5));

		RuntimeErrorScope errors;
		const Ref<BaristaScript> failed = compile_script(
				"static var value: int = 1\n"
				"static func _static_init() -> void:\n"
				"\tvar empty: Array[int] = []\n"
				"\tvalue = empty[0]\n",
				"res://runtime_class/static_failure.barista");
		BS_TEST_REQUIRE(failed.is_valid());
		CHECK_FALSE(failed->_can_instantiate());
		CHECK(failed->get_compile_error().contains("static initializer"));
		Variant stale;
		CHECK_FALSE(failed->get_static_property("value", stale));
		CHECK_MESSAGE(errors.has_error_containing("Out of bounds"), readable(errors.joined()));
	}

	TEST_CASE("inner classes instantiate and super dispatches across three levels") {
		const Ref<BaristaScript> script = compile_script(
				"class Base:\n"
				"\tvar base_value: int = 1\n"
				"\tfunc describe() -> String:\n"
				"\t\treturn \"base\"\n"
				"class Middle extends Base:\n"
				"\tvar middle_value: int = 2\n"
				"\tfunc describe() -> String:\n"
				"\t\treturn super.describe() + \"-middle\"\n"
				"class Child extends Middle:\n"
				"\tvar child_value: int = 3\n"
				"\tfunc describe() -> String:\n"
				"\t\treturn super.describe() + \"-child\"\n"
				"func make() -> Child:\n"
				"\treturn Child.new()\n",
				"res://runtime_class/inner_inheritance.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->_can_instantiate(), readable(script->get_compile_error()));
		CHECK(script->_get_constants().has("Base"));
		CHECK(script->_get_constants().has("Middle"));
		CHECK(script->_get_constants().has("Child"));

		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		const Variant result = owner->call("make");
		BS_TEST_REQUIRE(result.get_type() == Variant::OBJECT);
		Object *child = result;
		BS_TEST_REQUIRE(child != nullptr);
		CHECK(child->get("base_value") == Variant(1));
		CHECK(child->get("middle_value") == Variant(2));
		CHECK(child->get("child_value") == Variant(3));
		CHECK(child->call("describe") == Variant("base-middle-child"));
	}

	TEST_CASE("an extracted inner class refuses a stale base with a named diagnostic") {
		Ref<BaristaScript> child_script;
		{
			const Ref<BaristaScript> outer = compile_script(
					"class Base:\n"
					"\tvar value: int = 1\n"
					"class Child extends Base:\n"
					"\tpass\n",
					"res://runtime_class/stale_base.barista");
			BS_TEST_REQUIRE(outer.is_valid() && outer->_can_instantiate());
			const Variant child_value = outer->_get_constants().get("Child", Variant());
			Object *child_object = child_value;
			child_script = Ref<BaristaScript>(Object::cast_to<BaristaScript>(child_object));
			BS_TEST_REQUIRE(child_script.is_valid());
			CHECK(child_script->_get_base_script().is_valid());
		}

		CHECK(child_script->_get_base_script().is_null());
		RuntimeErrorScope errors;
		GDExtensionCallError error;
		const Variant result = child_script->instantiate(nullptr, 0, error);
		CHECK(result.get_type() == Variant::NIL);
		CHECK(error.error == GDEXTENSION_CALL_ERROR_INVALID_METHOD);
		CHECK_MESSAGE(errors.has_error_containing("base class is no longer alive"), readable(errors.joined()));
	}

	TEST_CASE("runnable class declarations instantiate through the normal Script contract") {
		const Ref<BaristaScript> script = compile_script(
				"class_name RuntimeConcrete245 extends RefCounted\n"
				"var value: int = 9\n",
				"res://runtime_class/runnable_declaration.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->_can_instantiate(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK(script->_instance_has(owner.ptr()));
		CHECK(owner->get("value") == Variant(9));
	}

	TEST_CASE("declaration-only files are refused by the extension") {
		struct Row {
			const char *source;
			const char *path;
			const char *diagnostic;
		};
		const Row rows[] = {
			{ "enum_name RuntimeEnum245:\n\tREADY = 1\n", "res://runtime_class/refuse_enum.barista", "enum" },
			{ "tuple_name RuntimeTuple245(value: int, label: String)\n", "res://runtime_class/refuse_tuple.barista", "tuple" },
			{ "trait_name RuntimeTrait245\nfunc marker() -> int:\n\treturn 1\n", "res://runtime_class/refuse_trait.barista", "trait" },
		};
		for (const Row &row : rows) {
			const Ref<BaristaScript> script = compile_script(row.source, row.path);
			BS_TEST_REQUIRE(script.is_valid());
			CHECK_FALSE(script->_can_instantiate());
			const std::string abstract_diagnostic = std::string(row.path) + ": " + readable(script->get_compile_error());
			CHECK_MESSAGE(script->_is_abstract(), abstract_diagnostic);
			CHECK_MESSAGE(script->get_compile_error().to_lower().contains(row.diagnostic), readable(script->get_compile_error()));
			Ref<RefCounted> owner;
			owner.instantiate();
			CHECK(script->_instance_create(owner.ptr()) == nullptr);
			owner->set_script(script);
			CHECK_FALSE(script->_instance_has(owner.ptr()));
		}
	}

	TEST_CASE("property state round trips every named member") {
		const String source = "var count: int = 1\nvar label: String = \"initial\"\n";
		const Ref<BaristaScript> original = compile_script(source, "res://runtime_class/state_original.barista");
		BS_TEST_REQUIRE(original.is_valid() && original->_can_instantiate());
		const Ref<RefCounted> owner = attach(original);
		BS_TEST_REQUIRE(owner.is_valid());
		owner->set("count", 41);
		owner->set("label", "saved");

		Dictionary state;
		BSInstance *instance = BSInstance::from_owner(owner.ptr());
		BS_TEST_REQUIRE(instance != nullptr);
		BSInstance::get_vtable()->get_property_state_func(
				instance,
				[](GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value, void *p_userdata) {
					Dictionary *captured = reinterpret_cast<Dictionary *>(p_userdata);
					(*captured)[*reinterpret_cast<const StringName *>(p_name)] =
							*reinterpret_cast<const Variant *>(p_value);
				},
				&state);
		CHECK(state.size() == 2);

		const Ref<BaristaScript> replacement = compile_script(source, "res://runtime_class/state_replacement.barista");
		BS_TEST_REQUIRE(replacement.is_valid() && replacement->_can_instantiate());
		const Ref<RefCounted> restored = attach(replacement);
		BS_TEST_REQUIRE(restored.is_valid());
		for (const Variant &key : state.keys()) {
			restored->set(key, state[key]);
		}
		CHECK(restored->get("count") == Variant(41));
		CHECK(restored->get("label") == Variant("saved"));
	}

	TEST_CASE("placeholder exports update idempotently and erase their handles") {
		const Ref<BaristaScript> script = compile_script(
				"@export_range(0, 10, 1) var amount: int = 4\n",
				"res://runtime_class/placeholder.barista");
		BS_TEST_REQUIRE(script.is_valid() && script->_can_instantiate());
		{
			Ref<RefCounted> owner;
			owner.instantiate();
			void *placeholder = script->_placeholder_instance_create(owner.ptr());
			BS_TEST_REQUIRE(placeholder != nullptr);
			godot::gdextension_interface::object_set_script_instance(owner->_owner, placeholder);
			CHECK(script->get_placeholder_count() == 1);
			script->_update_exports();
			script->_update_exports();
			CHECK(script->_get_property_default_value("amount") == Variant(4));
			owner->set("amount", 7);
			CHECK(owner->get("amount") == Variant(7));

			script->_set_source_code("var broken: =\n");
			CHECK(script->_reload(false) != OK);
			CHECK(script->_get_script_property_list().is_empty());
			CHECK(find_named(owner->get_property_list(), "amount").is_empty());
		}
		CHECK(script->get_placeholder_count() == 0);
	}
}
