/**************************************************************************/
/*  enum_call_dispatch_analyzer_tests.cpp                                  */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                          */
/*  This file is part of BaristaScript, a Godot GDExtension.                */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"
#include "bs_conformance_registry.h"
#include "bs_native_db.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <string>
using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;
namespace {
struct TestParser : BSParser {
	using BSParser::get_allocated_nodes;
};
void check_source(const String &source, const String &expected) {
	StorageFixture storage;
	BSConformanceRegistry::ScopedCorpusState registry;
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(source, "res://enum_call_dispatch.barista", false) == OK);
	BSAnalyzer analyzer(&parser);
	analyzer.analyze();
	String actual;
	for (const auto &error : parser.get_errors()) {
		if (!actual.is_empty())
			actual += "\n";
		actual += vformat(">> ERROR at line %d: %s", error.line, error.message);
	}
	if (actual.is_empty())
		actual = "BS_TEST_OK";
	CHECK(std::string(actual.utf8().get_data()) == std::string(expected.utf8().get_data()));
}
} //namespace
TEST_SUITE("enum_call_dispatch_analyzer") {
	TEST_CASE("enum_outer_signal_access_in_lambda_is_rejected_without_reading_other_source_union_arms") {
		check_source(R"BS(signal changed
enum Status:
	READY = 1
	func callback() -> Signal:
		var read = func() -> Signal: return changed
		return read.call()
)BS",
				R"BS(>> ERROR at line 5: Enum function "callback()" cannot access containing class instance member "changed".)BS");
	}
	TEST_CASE("enum_dictionary_fallback_rejects_mutation_and_missing_methods") {
		check_source(R"BS(enum Status:
	READY = 1
func test():
	Status.clear()
)BS",
				R"BS(>> ERROR at line 4: Cannot call non-const Dictionary function "clear()" on enum "Status".)BS");
		check_source(R"BS(enum Status:
	READY = 1
func test():
	Status.absent()
)BS",
				R"BS(>> ERROR at line 4: Function "absent()" does not exist for enum "Status" or its Dictionary methods.)BS");
	}
	TEST_CASE("enum_instance_dictionary_names_and_nested_self_keep_exact_signatures") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState registry;
		TestParser parser;
		BS_TEST_REQUIRE(parser.parse(R"BS(enum Status:
	READY = 1
	func keys() -> String:
		return "ready"
	func identity() -> Self:
		var callback = func() -> Self: return self
		return callback.call()
	static func normalize(value: Self) -> Self:
		return value
func test():
	var keys: Array = Status.keys()
	var label: String = Status.READY.keys()
	var value: Status = Status.READY.identity()
	var method: Callable[[Status], Status] = Status.normalize
	var normalized: Status = method.call(value)
	print(keys, label, normalized)
)BS",
								storage.path("enum_signatures.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		for (const auto &error : parser.get_errors())
			MESSAGE(std::string(error.message.utf8().get_data()));
		const auto *declaration = parser.get_tree()->get_member("Status").m_enum;
		BS_TEST_REQUIRE(declaration != nullptr);
		int calls = 0, references = 0, selves = 0;
		for (auto *node = parser.get_allocated_nodes(); node; node = node->next) {
			if (node->type == BSParser::Node::CALL) {
				const auto *call = static_cast<const BSParser::CallNode *>(node);
				if (call->enum_call_kind != BSParser::CallNode::ENUM_CALL_NONE) {
					CHECK(call->enum_call_kind == BSParser::CallNode::ENUM_CALL_INSTANCE);
					CHECK(call->enum_call_enum_type == StringName("Status"));
					CHECK(call->enum_call_owner_class == StringName(parser.get_tree()->fqcn));
					++calls;
				}
			}
			if (node->type == BSParser::Node::SUBSCRIPT) {
				const auto *subscript = static_cast<const BSParser::SubscriptNode *>(node);
				if (subscript->attribute && subscript->attribute->name == StringName("normalize")) {
					const auto type = subscript->get_datatype();
					BS_TEST_REQUIRE(type.has_method_signature && type.method_parameter_types.size() == 1 && type.method_return_type.size() == 1);
					CHECK(type.method_parameter_types[0].kind == BSParser::DataType::ENUM);
					CHECK(type.method_return_type[0].kind == BSParser::DataType::ENUM);
					CHECK(type.method_return_type[0].class_type == parser.get_tree());
					CHECK(type.method_return_type[0].enum_type == StringName("Status"));
					CHECK(subscript->attribute->function_source->owner_enum == declaration);
					++references;
				}
			}
			if (node->type == BSParser::Node::SELF) {
				CHECK(node->get_datatype().kind == BSParser::DataType::ENUM);
				CHECK(node->get_datatype().enum_type == StringName("Status"));
				++selves;
			}
		}
		CHECK(calls == 2);
		CHECK(references == 1);
		CHECK(selves == 1);
	}
	TEST_CASE("native_abstract_pair_keeps_call_and_callee_spans_and_no_success_type") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState registry;
		TestParser parser;
		BS_TEST_REQUIRE(parser.parse("func test():\n\tInstancePlaceholder.new()\n", storage.path("abstract_spans.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		BS_TEST_REQUIRE(parser.get_errors().size() == 2);
		const BSParser::CallNode *call = nullptr;
		for (auto *node = parser.get_allocated_nodes(); node; node = node->next)
			if (node->type == BSParser::Node::CALL)
				call = static_cast<const BSParser::CallNode *>(node);
		BS_TEST_REQUIRE(call && call->callee);
		CHECK(call->get_datatype().is_variant());
		auto *entry = parser.get_errors().front();
		for (const auto *origin : { static_cast<const BSParser::Node *>(call), static_cast<const BSParser::Node *>(call->callee) }) {
			CHECK(entry->get().line == origin->start_line);
			CHECK(entry->get().column == origin->start_column);
			CHECK(entry->get().end_line == origin->end_line);
			CHECK(entry->get().end_column == origin->end_column);
			entry = entry->next();
		}
		CHECK(parser.get_warnings().is_empty());
	}
	TEST_CASE("stock_core_abstract_admission_is_metadata_only") {
		CHECK(ClassDB::class_exists("InstancePlaceholder"));
		CHECK(ClassDB::is_class_enabled("InstancePlaceholder"));
		CHECK(ClassDB::class_get_api_type("InstancePlaceholder") == ClassDB::API_CORE);
		CHECK_FALSE(ClassDB::can_instantiate("InstancePlaceholder"));
		CHECK(BSNativeDB::is_abstract_core_class("InstancePlaceholder"));
		CHECK_FALSE(BSNativeDB::is_abstract_core_class("Node"));
		CHECK_FALSE(BSNativeDB::is_abstract_core_class("Object"));
		CHECK_FALSE(BSNativeDB::is_abstract_core_class("MissingCallDispatchClass"));
		CHECK_FALSE(BSNativeDB::is_abstract_core_class("EditorPlugin"));
	}
	TEST_CASE("errors/abstract_class_instantiate.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(class A extends InstancePlaceholder:
	func _init():
		print('no')

class B extends A:
	pass

func test():
	InstancePlaceholder.new()
	B.new()
)BS",
				R"BS(>> ERROR at line 9: Native class "InstancePlaceholder" cannot be constructed as it is abstract.
>> ERROR at line 9: Name "new" is a Callable. You can call it with "new.call()" instead.
>> ERROR at line 10: Class "B" cannot be constructed as it is based on abstract native class "InstancePlaceholder".
>> ERROR at line 10: Name "new" is a Callable. You can call it with "new.call()" instead.)BS");
	}
	TEST_CASE("errors/enum_host_function_calls.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(enum Status:
	READY = 1
	DONE = 2

	func label() -> String:
		return "ready" if self == READY else "done"

	static func parse(text: String) -> Self:
		return READY if text == "ready" else DONE


func test() -> void:
	var value: Status = Status.DONE
	var literal_label: String = Status.READY.label()
	var typed_label: String = value.label()
	var parsed: Status = Status.parse("ready")
	var instance_callable: Callable[[], String] = value.label
	var static_callable: Callable[[String], Status] = Status.parse
	var keys: Array = Status.keys()
	print(literal_label, typed_label, parsed, instance_callable, static_callable, keys)
	var _analyzer_only_stop: EnumCallAnalyzerSentinel
)BS",
				R"BS(>> ERROR at line 21: Could not find type "EnumCallAnalyzerSentinel" in the current scope.)BS");
	}
	TEST_CASE("errors/enum_host_function_dictionary_conflict.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(enum Status:
	READY = 1

	static func keys() -> Array:
		return []
)BS",
				R"BS(>> ERROR at line 4: Static enum function "keys" conflicts with Dictionary method "keys()".)BS");
	}
	TEST_CASE("errors/enum_host_function_duplicate.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(enum Status:
	READY = 1

	func parse() -> Status:
		return self

	static func parse() -> Status:
		return READY
)BS",
				R"BS(>> ERROR at line 7: Enum function "parse()" is declared more than once.)BS");
	}
	TEST_CASE("errors/enum_host_function_instance_on_type.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(enum Status:
	READY = 1

	func label() -> String:
		return "ready"


func test() -> void:
	Status.label()
)BS",
				R"BS(>> ERROR at line 9: Cannot call instance enum function "label()" on enum type "Status".)BS");
	}
	TEST_CASE("errors/enum_host_function_other_enum.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(enum Status:
	READY = 1

	func label() -> String:
		return "ready"


enum Other:
	READY = 1


func test() -> void:
	Other.READY.label()
)BS",
				R"BS(>> ERROR at line 13: Function "label()" does not exist for enum value "Other".)BS");
	}
	TEST_CASE("errors/enum_host_function_outer_instance_access.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(var instance_value := 1

func instance_method() -> int:
	return instance_value

enum Status:
	READY = 1

	func read_value() -> int:
		return instance_value

	func call_method() -> int:
		return instance_method()
)BS",
				R"BS(>> ERROR at line 10: Enum function "read_value()" cannot access containing class instance member "instance_value".
>> ERROR at line 13: Enum function "call_method()" cannot access containing class instance member "instance_method".)BS");
	}
	TEST_CASE("errors/enum_host_function_static_on_value.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(enum Status:
	READY = 1

	static func parse() -> Self:
		return READY


func test() -> void:
	Status.READY.parse()
)BS",
				R"BS(>> ERROR at line 9: Cannot call static enum function "parse()" on enum value "Status".)BS");
	}
	TEST_CASE("errors/enum_host_function_value_conflict.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(enum Status:
	READY = 1

	func READY() -> Status:
		return self
)BS",
				R"BS(>> ERROR at line 4: Enum function "READY()" conflicts with enum value "READY".)BS");
	}
	TEST_CASE("features/enum_as_const.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(class Outer:
	enum OuterEnum:
		OuterValue = 3
	const OuterConst := OuterEnum

	class Inner:
		enum InnerEnum:
			InnerValue = 7
		const InnerConst := InnerEnum

		static func test() -> void:
			print(OuterEnum.size());
			print(OuterEnum.OuterValue);
			print(OuterConst.size());
			print(OuterConst.OuterValue);
			print(Outer.OuterEnum.size());
			print(Outer.OuterEnum.OuterValue);
			print(Outer.OuterConst.size());
			print(Outer.OuterConst.OuterValue);

			print(InnerEnum.size());
			print(InnerEnum.InnerValue);
			print(InnerConst.size());
			print(InnerConst.InnerValue);
			print(Inner.InnerEnum.size());
			print(Inner.InnerEnum.InnerValue);
			print(Inner.InnerConst.size());
			print(Inner.InnerConst.InnerValue);

func test():
	Outer.Inner.test()
)BS",
				R"BS(BS_TEST_OK)BS");
	}
	TEST_CASE("features/enum_duplicate_into_dict.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(enum Enum:
	V1 = 0
	V2 = V1 + 1

func test():
	var enumAsDict: Dictionary = Enum.duplicate()
	var enumAsVariant = Enum.duplicate()
	print(Enum.has("V1"))
	print(enumAsDict.has("V1"))
	print(enumAsVariant.has("V1"))
	enumAsDict.clear()
	enumAsVariant.clear()
	print(Enum.has("V1"))
	print(enumAsDict.has("V1"))
	print(enumAsVariant.has("V1"))
)BS",
				R"BS(BS_TEST_OK)BS");
	}
	TEST_CASE("features/enum_host_function_declarations.norun.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(enum Status:
	READY = 1
	DONE = 2

	func identity() -> Self:
		match self:
			READY:
				return self
			_:
				return Status.DONE

	static func normalize(value: Status) -> Status:
		return value


func test() -> void:
	var keys: Array = Status.keys()
	print(keys)
)BS",
				R"BS(BS_TEST_OK)BS");
	}
	TEST_CASE("features/enum_type_is_treated_as_dictionary.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(enum MyEnum:
	ZERO = 0
	ONE = ZERO + 1
	TWO = ONE + 1

func test():
	for key in MyEnum.keys():
		prints(key, MyEnum[key])

	# https://github.com/godotengine/godot/issues/55491
	for key in MyEnum:
		prints(key, MyEnum[key])
)BS",
				R"BS(BS_TEST_OK)BS");
	}
	TEST_CASE("errors/typed_array_assignment.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(func test():
	const arr: Array[int] = ["Hello", "World"]
)BS",
				R"BS(>> ERROR at line 2: Cannot include a value of type "String" as "int".
>> ERROR at line 2: Cannot have an element of type "String" in an array of type "Array[int]".)BS");
	}
	TEST_CASE("errors/typed_dictionary_assignment.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(func test():
	const dict: Dictionary[int, int] = { "Hello": "World" }
)BS",
				R"BS(>> ERROR at line 2: Cannot include a value of type "String" as "int".
>> ERROR at line 2: Cannot have a key of type "String" in a dictionary of type "Dictionary[int, int]".)BS");
	}
	TEST_CASE("features/const_array_and_dictionary_constructors.barista") {
		// Immutable Foundry c9d5e35 original; runtime output is outside the static oracle.
		check_source(R"BS(const A1 = Array()
const A2 = Array(Array())
const A3 = Array([])
const A4 = [Array()]
const A5 = [[]]
const A6 = Array([1], TYPE_INT, &"", null)

const D1 = Dictionary()
const D2 = Dictionary(Dictionary())
const D3 = Dictionary({})
const D4 = { Dictionary(): Dictionary() }
const D5 = { {}: {} }
const D6 = Dictionary({ 1: 1 }, TYPE_INT, &"", null, TYPE_INT, &"", null)

var a1 = Array()
var a2 = Array(Array())
var a3 = Array([])
var a4 = [Array()]
var a5 = [[]]
var a6 = Array([1], TYPE_INT, &"", null)

var d1 = Dictionary()
var d2 = Dictionary(Dictionary())
var d3 = Dictionary({})
var d4 = { Dictionary(): Dictionary() }
var d5 = { {}: {} }
var d6 = Dictionary({ 1: 1 }, TYPE_INT, &"", null, TYPE_INT, &"", null)

func test_value(value: Variant) -> void:
	@warning_ignore("unsafe_method_access")
	prints(value.is_read_only(), var_to_str(value).replace("\n", " "))

func test():
	print('---')
	test_value(A1)
	test_value(A2)
	test_value(A3)
	test_value(A4)
	test_value(A5)
	test_value(A6)

	print('---')
	test_value(D1)
	test_value(D2)
	test_value(D3)
	test_value(D4)
	test_value(D5)
	test_value(D6)

	print('---')
	test_value(a1)
	test_value(a2)
	test_value(a3)
	test_value(a4)
	test_value(a5)
	test_value(a6)

	print('---')
	test_value(d1)
	test_value(d2)
	test_value(d3)
	test_value(d4)
	test_value(d5)
	test_value(d6)
)BS",
				R"BS(BS_TEST_OK)BS");
	}
}
