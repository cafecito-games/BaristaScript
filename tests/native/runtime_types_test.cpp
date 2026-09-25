/**************************************************************************/
/*  runtime_types_test.cpp                                                */
/*                                                                        */
/*  Runtime descriptors and the typed boundaries that consume them.      */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "runtime_helpers.h"
#include "test_require.h"

#include "bs_runtime_type.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace barista_script;
using namespace barista_script::native_tests;

namespace {

Ref<RefCounted> attach(const Ref<BaristaScript> &p_script) {
	Ref<RefCounted> owner;
	owner.instantiate();
	owner->set_script(p_script);
	return owner;
}

} // namespace

TEST_SUITE("runtime_types") {
	TEST_CASE("runtime descriptors compare and hash by value") {
		BSRuntimeType integer = BSRuntimeType::builtin(Variant::INT);
		BSRuntimeType same_integer = BSRuntimeType::builtin(Variant::INT);
		BSRuntimeType nullable_integer = BSRuntimeType::builtin(Variant::INT, true);
		CHECK(integer == same_integer);
		CHECK(integer.hash() == same_integer.hash());
		CHECK(integer != nullable_integer);

		BSRuntimeType array = BSRuntimeType::builtin(Variant::ARRAY);
		array.container_element_types.push_back(integer);
		BSRuntimeType same_array = BSRuntimeType::builtin(Variant::ARRAY);
		same_array.container_element_types.push_back(same_integer);
		CHECK(array == same_array);
		CHECK(array.hash() == same_array.hash());
	}

	TEST_CASE("builtin assignment return casts tests and nullable values execute") {
		const Ref<BaristaScript> script = compile_script(
				"func store(value: Variant) -> int:\n"
				"\tvar number: int = value\n"
				"\treturn number\n"
				"\n"
				"func cast_nullable(value: Variant) -> int?:\n"
				"\treturn value as int?\n"
				"\n"
				"func cast_required(value: Variant) -> int:\n"
				"\treturn value as int\n"
				"\n"
				"func test(value: Variant) -> bool:\n"
				"\treturn value is int\n"
				"\n"
				"func test_variant(value: Variant) -> bool:\n"
				"\treturn value is Variant\n",
				"res://runtime_types/builtin.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		CHECK(owner->call("store", 4.8) == Variant(int64_t(4)));
		CHECK(owner->call("test", 4) == Variant(true));
		CHECK(owner->call("test", "four") == Variant(false));
		CHECK(owner->call("test_variant", Variant()) == Variant(true));
		CHECK(owner->call("cast_nullable", 5) == Variant(int64_t(5)));
		CHECK(owner->call("cast_nullable", Variant()) == Variant());
		Ref<RefCounted> not_a_number;
		not_a_number.instantiate();
		CHECK(owner->call("cast_nullable", not_a_number) == Variant());

		RuntimeErrorScope errors;
		CHECK(owner->call("store", "four") == Variant());
		CHECK(owner->call("cast_required", not_a_number) == Variant());
		CHECK_MESSAGE(errors.errors().size() == 2, readable(errors.joined()));
	}

	TEST_CASE("native assignment casts tests and nullable values execute") {
		const Ref<BaristaScript> script = compile_script(
				"func store(value: Variant) -> RefCounted:\n"
				"\tvar object: RefCounted = value\n"
				"\treturn object\n"
				"\n"
				"func cast_nullable(value: Variant) -> RefCounted?:\n"
				"\treturn value as RefCounted?\n"
				"\n"
				"func test(value: Variant) -> bool:\n"
				"\treturn value is RefCounted\n",
				"res://runtime_types/native.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		Ref<RefCounted> value;
		value.instantiate();
		CHECK(owner->call("store", value) == Variant(value));
		CHECK(owner->call("test", value) == Variant(true));
		CHECK(owner->call("test", 4) == Variant(false));
		CHECK(owner->call("cast_nullable", value) == Variant(value));
		CHECK(owner->call("cast_nullable", Variant()) == Variant());
		CHECK(owner->call("cast_nullable", 4) == Variant());
		RuntimeErrorScope errors;
		CHECK(owner->call("store", 4) == Variant());
		CHECK_MESSAGE(errors.errors().size() == 1, readable(errors.joined()));
	}

	TEST_CASE("script assignment casts and tests use the declaring script identity") {
		const Ref<BaristaScript> script = compile_script(
				"func store(value: Self) -> Self:\n"
				"\treturn value\n"
				"\n"
				"func cast_nullable(value: Variant) -> Self?:\n"
				"\treturn value as Self?\n"
				"\n"
				"func test(value: Variant) -> bool:\n"
				"\treturn value is Self\n",
				"res://runtime_types/script.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());
		Ref<RefCounted> other;
		other.instantiate();
		CHECK(owner->call("store", owner) == Variant(owner));
		CHECK(owner->call("test", owner) == Variant(true));
		CHECK(owner->call("test", other) == Variant(false));
		CHECK(owner->call("cast_nullable", owner) == Variant(owner));
		CHECK(owner->call("cast_nullable", Variant()) == Variant());
		CHECK(owner->call("cast_nullable", other) == Variant());
		RuntimeErrorScope errors;
		CHECK(owner->call("store", other) == Variant());
		CHECK_MESSAGE(!errors.errors().is_empty(), readable(errors.joined()));
	}

	TEST_CASE("native and script type handles preserve represented identity") {
		const Ref<BaristaScript> script = compile_script(
				"func store_native(value: Type[Node]) -> Type[Node]:\n"
				"\treturn value\n"
				"\n"
				"func cast_native(value: Variant) -> Type[Node]?:\n"
				"\treturn value as Type[Node]?\n"
				"\n"
				"func test_native(value: Variant) -> bool:\n"
				"\treturn value is Type[Node]\n"
				"\n"
				"func build_native_handles() -> Array:\n"
				"\tvar handles: Array[Type[Node]] = [Node, Button]\n"
				"\treturn handles\n"
				"\n"
				"func store_script(value: Type[Self]) -> Type[Self]:\n"
				"\treturn value\n"
				"\n"
				"func cast_script(value: Variant) -> Type[Self]?:\n"
				"\treturn value as Type[Self]?\n"
				"\n"
				"func test_script(value: Variant) -> bool:\n"
				"\treturn value is Type[Self]\n",
				"res://runtime_types/type_handles.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());

		const Variant node = StringName("Node");
		const Variant resource = StringName("Resource");
		CHECK(owner->call("store_native", node) == node);
		CHECK(owner->call("cast_native", node) == node);
		CHECK(owner->call("cast_native", resource) == Variant());
		CHECK(owner->call("test_native", node) == Variant(true));
		CHECK(owner->call("test_native", resource) == Variant(false));
		const Array handles = owner->call("build_native_handles");
		CHECK(handles.is_typed());
		CHECK(handles.get_typed_builtin() == Variant::STRING_NAME);
		BS_TEST_REQUIRE(handles.size() == 2);
		CHECK(handles[0] == node);
		CHECK(handles[1] == Variant(StringName("Button")));
		CHECK(owner->call("store_script", script) == Variant(script));
		CHECK(owner->call("cast_script", script) == Variant(script));
		CHECK(owner->call("test_script", script) == Variant(true));

		const Ref<BaristaScript> other_script = compile_script(
				"func marker() -> void:\n"
				"\tpass\n",
				"res://runtime_types/other_handle.barista");
		BS_TEST_REQUIRE(other_script.is_valid());
		CHECK(owner->call("cast_script", other_script) == Variant());
		CHECK(owner->call("test_script", other_script) == Variant(false));
		RuntimeErrorScope errors;
		CHECK(owner->call("store_native", resource) == Variant());
		CHECK(owner->call("store_script", other_script) == Variant());
		CHECK_MESSAGE(errors.errors().size() == 2, readable(errors.joined()));
	}

	TEST_CASE("typed arrays and dictionaries construct convert and reject atomically") {
		BSRuntimeType array_type = BSRuntimeType::builtin(Variant::ARRAY);
		array_type.container_element_types.push_back(BSRuntimeType::builtin(Variant::INT));
		Array erased_good;
		erased_good.push_back(3);
		Variant retyped;
		String conversion_error;
		CHECK(array_type.convert(erased_good, retyped, conversion_error, true));
		const Array retyped_array = retyped;
		CHECK(retyped_array.is_typed());
		CHECK(retyped_array.get_typed_builtin() == Variant::INT);
		BSRuntimeType dictionary_type = BSRuntimeType::builtin(Variant::DICTIONARY);
		dictionary_type.container_element_types.push_back(BSRuntimeType::builtin(Variant::STRING));
		dictionary_type.container_element_types.push_back(BSRuntimeType::builtin(Variant::INT));
		Dictionary erased_good_dictionary;
		erased_good_dictionary["two"] = 2;
		Variant retyped_dictionary_value;
		CHECK(dictionary_type.convert(erased_good_dictionary, retyped_dictionary_value, conversion_error, true));
		const Dictionary retyped_dictionary = retyped_dictionary_value;
		CHECK(retyped_dictionary.is_typed());
		CHECK(retyped_dictionary.get_typed_key_builtin() == Variant::STRING);
		CHECK(retyped_dictionary.get_typed_value_builtin() == Variant::INT);

		const Ref<BaristaScript> script = compile_script(
				"var numbers: Array[int] = [1]\n"
				"var lookup: Dictionary[String, int] = {\"one\": 1}\n"
				"\n"
				"func build(value: Variant) -> Array:\n"
				"\tvar made: Array[int] = [value]\n"
				"\treturn made\n"
				"\n"
				"func build_dictionary(value: Variant) -> Dictionary:\n"
				"\tvar made: Dictionary[String, int] = {\"value\": value}\n"
				"\treturn made\n"
				"\n"
				"func build_nested(value: Variant) -> Array:\n"
				"\tvar made: Array[Dictionary[String, int]] = []\n"
				"\tmade.append(value)\n"
				"\treturn made\n"
				"\n"
				"func write_empty_dictionary(value: Variant) -> int:\n"
				"\tvar made: Dictionary[int, int]\n"
				"\tmade[value] = 0\n"
				"\treturn made.size()\n"
				"\n"
				"func build_nullable(value: Variant) -> Array:\n"
				"\tvar made: Array[int?] = [null, 1]\n"
				"\tmade.append(value)\n"
				"\treturn made\n"
				"\n"
				"func replace_numbers(value: Variant) -> void:\n"
				"\tself.numbers = value\n"
				"\n"
				"func replace_lookup(value: Variant) -> void:\n"
				"\tself.lookup = value\n",
				"res://runtime_types/containers.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Ref<RefCounted> owner = attach(script);
		BS_TEST_REQUIRE(owner.is_valid());

		const Array made = owner->call("build", 2);
		CHECK(made.is_typed());
		CHECK(made.get_typed_builtin() == Variant::INT);
		BS_TEST_REQUIRE(made.size() == 1);
		CHECK(made[0] == Variant(2));
		const Dictionary dictionary = owner->call("build_dictionary", 2);
		CHECK(dictionary.is_typed());
		CHECK(dictionary.get_typed_key_builtin() == Variant::STRING);
		CHECK(dictionary.get_typed_value_builtin() == Variant::INT);
		const Array nested = owner->call("build_nested", erased_good_dictionary);
		BS_TEST_REQUIRE(nested.size() == 1);
		CHECK(Dictionary(nested[0]).is_typed());
		CHECK(Dictionary(nested[0]).get_typed_key_builtin() == Variant::STRING);
		CHECK(Dictionary(nested[0]).get_typed_value_builtin() == Variant::INT);
		const Array nullable = owner->call("build_nullable", Variant());
		CHECK_FALSE(nullable.is_typed());
		BS_TEST_REQUIRE(nullable.size() == 3);
		CHECK(nullable[0] == Variant());
		CHECK(nullable[1] == Variant(1));
		CHECK(nullable[2] == Variant());
		Array erased_bad;
		erased_bad.push_back("wrong");
		Dictionary erased_bad_dictionary;
		erased_bad_dictionary["two"] = "wrong";
		{
			RuntimeErrorScope errors;
			CHECK(owner->call("build_nested", erased_bad_dictionary) == Variant());
			CHECK(owner->call("write_empty_dictionary", "wrong") == Variant());
			CHECK(owner->call("build_nullable", "wrong") == Variant());
			CHECK_MESSAGE(errors.errors().size() == 3, readable(errors.joined()));
		}
		RuntimeErrorScope errors;
		owner->call("replace_numbers", erased_bad);
		owner->call("replace_lookup", erased_bad_dictionary);
		CHECK_MESSAGE(errors.errors().size() == 2, readable(errors.joined()));
		const Array numbers = owner->get("numbers");
		CHECK(numbers.size() == 1);
		CHECK(numbers[0] == Variant(1));
		const Dictionary lookup = owner->get("lookup");
		CHECK(lookup.size() == 1);
		CHECK(lookup["one"] == Variant(1));
	}

	TEST_CASE("reserved D1 numeric spellings never reach a runtime descriptor") {
		for (const String &spelling : { String("uint"), String("long"), String("ulong") }) {
			const Ref<BaristaScript> script = compile_script(
					"func rejected(value: " + spelling + "):\n"
					"\tpass\n",
					"res://runtime_types/reserved_" + spelling + ".barista");
			BS_TEST_REQUIRE(script.is_valid());
			CHECK_FALSE(script->_can_instantiate());
			CHECK_MESSAGE(script->get_compile_error().contains(spelling), readable(script->get_compile_error()));
		}
	}
}
