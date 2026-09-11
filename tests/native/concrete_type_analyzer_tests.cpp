/**************************************************************************/
/*  concrete_type_analyzer_tests.cpp                                       */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                         */
/*  This file is part of BaristaScript, a Godot GDExtension.               */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_conformance_registry.h"
#include "bs_type.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <godot_cpp/classes/project_settings.hpp>
#include <string>

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
struct TypeProfile {
	Dictionary saved;
	void set(const String &key, const Variant &value) {
		auto *settings = ProjectSettings::get_singleton();
		if (!saved.has(key))
			saved[key] = settings->has_setting(key) ? settings->get_setting(key) : Variant();
		settings->set_setting(key, value);
	}
	TypeProfile(bool strict_null = false) {
		set("debug/barista_script/analysis/strict_null_checks", strict_null);
		set("debug/barista_script/analysis/strict_dynamic_checks", false);
		for (int i = 0; i < BSWarning::WARNING_MAX; ++i)
			set(BSWarning::get_setting_path_from_code(BSWarning::Code(i)), BSWarning::IGNORE);
		BSParser::update_project_settings();
	}
	~TypeProfile() {
		const Array keys = saved.keys();
		for (int i = 0; i < keys.size(); ++i)
			ProjectSettings::get_singleton()->set_setting(keys[i], saved[keys[i]]);
		BSParser::update_project_settings();
		for (int i = 0; i < keys.size(); ++i)
			if (Variant(saved[keys[i]]).get_type() == Variant::NIL && ProjectSettings::get_singleton()->has_setting(keys[i]))
				ProjectSettings::get_singleton()->clear(keys[i]);
	}
};
void diagnostics(const BSParser &parser) {
	for (const auto &e : parser.get_errors())
		MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column);
}
String error_block(const BSParser &parser) {
	String block;
	for (const auto &e : parser.get_errors()) {
		if (!block.is_empty())
			block += "\n";
		block += vformat(">> ERROR at line %d: %s", e.line, e.message);
	}
	return block;
}
void original(const char *name, const char *source, const char *expected) {
	StorageFixture storage;
	BSConformanceRegistry::ScopedCorpusState conformances;
	TypeProfile profile;
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(source, String("res://tests/concrete_types/") + name + ".barista", false) == OK);
	BSAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
	diagnostics(parser);
	CHECK(std::string(error_block(parser).utf8().get_data()) == std::string(expected));
	const String case_name(name);
	const BSParser::Node *site = nullptr;
	if (case_name.begins_with("tuple_destructure_")) {
		const auto *body = parser.get_tree()->get_member("test").function->body;
		BS_TEST_REQUIRE(body && !body->statements.is_empty() && body->statements[0]->type == BSParser::Node::VARIABLE_DESTRUCTURE);
		site = static_cast<BSParser::VariableDestructureNode *>(body->statements[0])->initializer;
	} else if (case_name.begins_with("type_metatype_argument_") || case_name.begins_with("type_metatype_assignment_statement_")) {
		const auto *body = parser.get_tree()->get_member("test").function->body;
		BS_TEST_REQUIRE(body && !body->statements.is_empty());
		const auto *last = body->statements[body->statements.size() - 1];
		if (case_name.begins_with("type_metatype_argument_")) {
			BS_TEST_REQUIRE(last->type == BSParser::Node::CALL);
			const auto *call = static_cast<const BSParser::CallNode *>(last);
			BS_TEST_REQUIRE(call->arguments.size() == 1);
			site = call->arguments[0];
		} else {
			BS_TEST_REQUIRE(last->type == BSParser::Node::ASSIGNMENT);
			site = static_cast<const BSParser::AssignmentNode *>(last)->assigned_value;
		}
	} else if (case_name.begins_with("type_metatype_")) {
		const auto member = parser.get_tree()->get_member(case_name.begins_with("type_metatype_assignment_") ? "user_handle" : "bad_type");
		BS_TEST_REQUIRE(member.type == BSParser::ClassNode::Member::VARIABLE && member.variable);
		site = member.variable->initializer ? static_cast<const BSParser::Node *>(member.variable->initializer) : member.variable->datatype_specifier;
	}
	if (site) {
		BS_TEST_REQUIRE(parser.get_errors().size() == 1);
		const auto &e = parser.get_errors().front()->get();
		CHECK(e.line == site->start_line);
		CHECK(e.column == site->start_column);
		CHECK(e.end_line == site->end_line);
		CHECK(e.end_column == site->end_column);
	}
}
const BSParser::VariableNode *local(const BSParser &parser, const StringName &name) {
	const auto member = parser.get_tree()->get_member("test");
	if (member.type != BSParser::ClassNode::Member::FUNCTION || !member.function || !member.function->body)
		return nullptr;
	for (auto *statement : member.function->body->statements) {
		if (statement && statement->type == BSParser::Node::VARIABLE) {
			const auto *v = static_cast<const BSParser::VariableNode *>(statement);
			if (v->identifier && v->identifier->name == name)
				return v;
		}
	}
	return nullptr;
}
} // namespace

TEST_SUITE("concrete_type_analyzer") {
	TEST_CASE("original_assymetric_assignment_bad") {
		// Foundry c9d5e35: analyzer/assymetric_assignment_bad.fs; unchanged complete static block.
		original("assymetric_assignment_bad", R"SOURCE(func test():
	var var_color: String = Color.RED
	print('not ok')
)SOURCE",
				R"EXPECT(>> ERROR at line 2: Cannot assign a value of type Color to variable "var_color" with specified type String.)EXPECT");
	}
	TEST_CASE("original_async_callable_bare_call_requires_await") {
		// Foundry c9d5e35: analyzer/async_callable_bare_call_requires_await.fs; unchanged complete static block.
		original("async_callable_bare_call_requires_await", R"SOURCE(# Invoking a bare AsyncCallable yields a Coroutine[Variant]. Without "await" you hold the coroutine
# handle, not its result, so binding it to a concretely-typed variable is a type error.
async func _async() -> int:
	return 1


func test() -> void:
	var cb: AsyncCallable = _async
	var value: int = cb.call()
	print(value)
)SOURCE",
				R"EXPECT(>> ERROR at line 9: Cannot assign a value of type Coroutine[Variant] to variable "value" with specified type int.)EXPECT");
	}
	TEST_CASE("original_async_callable_bare_rejects_sync") {
		// Foundry c9d5e35: analyzer/async_callable_bare_rejects_sync.fs; unchanged complete static block.
		original("async_callable_bare_rejects_sync", R"SOURCE(# A bare AsyncCallable target requires an async value: assigning a synchronous callable is rejected
# even though neither side carries an explicit method signature.
func _sync() -> int:
	return 1


func test() -> void:
	var cb: AsyncCallable = _sync
	print(cb)
)SOURCE",
				R"EXPECT(>> ERROR at line 8: Cannot assign a value of type Callable to variable "cb" with specified type AsyncCallable.)EXPECT");
	}
	TEST_CASE("original_async_callable_call_requires_await") {
		// Foundry c9d5e35: analyzer/async_callable_call_requires_await.fs; unchanged complete static block.
		original("async_callable_call_requires_await", R"SOURCE(# Invoking an AsyncCallable produces a Coroutine[String]. Without "await" the value is the in-flight
# coroutine handle, not its result, so binding it to a String-typed variable is a type error.
async func _work(value: int) -> String:
	return str(value)


func test() -> void:
	var handler: AsyncCallable[[int], String] = _work
	var result: String = handler.call(7)
	print(result)
)SOURCE",
				R"EXPECT(>> ERROR at line 9: Cannot assign a value of type Coroutine[String] to variable "result" with specified type String.)EXPECT");
	}
	TEST_CASE("original_async_callable_not_interchangeable") {
		// Foundry c9d5e35: analyzer/async_callable_not_interchangeable.fs; unchanged complete static block.
		original("async_callable_not_interchangeable", R"SOURCE(# AsyncCallable and plain Callable are not interchangeable even when their parameter/return slots
# match: the async marker is part of signature equality. Assigning across the two is rejected in
# both directions, and async-aware stringification names each side distinctly in the diagnostic.
func _sync(value: int) -> String:
	return str(value)


async func _async(value: int) -> String:
	return str(value)


func test() -> void:
	var sync_cb: Callable[[int], String] = _sync
	var async_cb: AsyncCallable[[int], String] = _async
	var bad_async: AsyncCallable[[int], String] = sync_cb
	var bad_sync: Callable[[int], String] = async_cb
	print(bad_async)
	print(bad_sync)
)SOURCE",
				R"EXPECT(>> ERROR at line 15: Cannot assign a value of type Callable[[int], String] to variable "bad_async" with specified type AsyncCallable[[int], String].
>> ERROR at line 16: Cannot assign a value of type AsyncCallable[[int], String] to variable "bad_sync" with specified type Callable[[int], String].)EXPECT");
	}
	TEST_CASE("original_async_method_reference_not_sync_callable") {
		// Foundry c9d5e35: analyzer/async_method_reference_not_sync_callable.fs; unchanged complete static block.
		original("async_method_reference_not_sync_callable", R"SOURCE(# A reference to an async method infers an AsyncCallable, which is not assignable to a plain
# (synchronous) Callable target with otherwise-matching slots.
async func _fetch() -> int:
	return 42


func test() -> void:
	var typed: Callable[[], int] = _fetch
	print(typed)
)SOURCE",
				R"EXPECT(>> ERROR at line 8: Cannot assign a value of type AsyncCallable to variable "typed" with specified type Callable[[], int].)EXPECT");
	}
	TEST_CASE("original_callable_gradual_tail_rendering") {
		// Foundry c9d5e35: analyzer/callable_gradual_tail_rendering.fs; unchanged complete static block.
		original("callable_gradual_tail_rendering", R"SOURCE(func gradual_tail(_callback: Callable[[...Array], void]) -> void:
	pass


func typed_tail(_callback: Callable[[...Array[int]], void]) -> void:
	pass


func fixed(_callback: Callable[[int], void]) -> void:
	pass


func test() -> void:
	var wrong: int = 0
	gradual_tail(wrong)
	typed_tail(wrong)
	fixed(wrong)
	var gradual: Callable[[...Array], void] = func(..._values: Array) -> void: pass
	var bad: int = gradual
	print(bad)
)SOURCE",
				R"EXPECT(>> ERROR at line 15: Invalid argument for "gradual_tail()" function: argument 1 should be "Callable[[...Array], void]" but is "int".
>> ERROR at line 16: Invalid argument for "typed_tail()" function: argument 1 should be "Callable[[...Array[int]], void]" but is "int".
>> ERROR at line 17: Invalid argument for "fixed()" function: argument 1 should be "Callable[[int], void]" but is "int".
>> ERROR at line 19: Cannot assign a value of type Callable[[...Array], void] to variable "bad" with specified type int.)EXPECT");
	}
	TEST_CASE("original_signal_value_connect_lambda_mismatch") {
		// Foundry c9d5e35: analyzer/signal_value_connect_lambda_mismatch.fs; unchanged complete static block.
		original("signal_value_connect_lambda_mismatch", R"SOURCE(# A lambda handler is validated through the Signal-value `connect()` spelling too.
signal registered(node: Node)


func test() -> void:
	registered.connect(func(resource: Resource) -> void:
		print(resource))
)SOURCE",
				R"EXPECT(>> ERROR at line 6: Cannot connect signal "Signal[[Node]]" to callable "Callable[[Resource], void]": signal argument 1 of type "Node" cannot be passed to callable parameter of type "Resource".)EXPECT");
	}
	TEST_CASE("original_tuple_destructure_arity_mismatch") {
		// Foundry c9d5e35: analyzer/tuple_destructure_arity_mismatch.fs; unchanged complete static block.
		original("tuple_destructure_arity_mismatch", R"SOURCE(func test():
	var (x, y, z) = (1, 2)
	print(x + y + z)
)SOURCE",
				R"EXPECT(>> ERROR at line 2: Cannot destructure the tuple "(int, int)" into 3 bindings; it has 2 elements.)EXPECT");
	}
	TEST_CASE("original_tuple_destructure_meta_type") {
		// Foundry c9d5e35: analyzer/tuple_destructure_meta_type.fs; unchanged complete static block.
		original("tuple_destructure_meta_type", R"SOURCE(tuple Vec2(x: float, y: float)

func test():
	var (x, y) = Vec2
	print(x)
	print(y)
)SOURCE",
				R"EXPECT(>> ERROR at line 4: Cannot destructure the tuple type "Vec2"; construct a value first.)EXPECT");
	}
	TEST_CASE("original_tuple_destructure_non_tuple") {
		// Foundry c9d5e35: analyzer/tuple_destructure_non_tuple.fs; unchanged complete static block.
		original("tuple_destructure_non_tuple", R"SOURCE(func test():
	var (x, y) = [1, 2]
	print(x + y)
)SOURCE",
				R"EXPECT(>> ERROR at line 2: Cannot destructure a value of type "Array"; only a tuple with a statically known shape can be destructured.)EXPECT");
	}
	TEST_CASE("original_tuple_destructure_variant") {
		// Foundry c9d5e35: analyzer/tuple_destructure_variant.fs; unchanged complete static block.
		original("tuple_destructure_variant", R"SOURCE(func make_variant() -> Variant:
	return (1, 2)

func test():
	var (x, y) = make_variant()
	print(x)
	print(y)
)SOURCE",
				R"EXPECT(>> ERROR at line 5: Cannot destructure a value of type "Variant"; only a tuple with a statically known shape can be destructured.)EXPECT");
	}
	TEST_CASE("original_type_metatype_argument_instance") {
		// Foundry c9d5e35: analyzer/type_metatype_argument_instance.fs; unchanged complete static block.
		original("type_metatype_argument_instance", R"SOURCE(class User:
	pass


func accept_user(_klass: Type[User]) -> void:
	pass


func test():
	accept_user(User.new())
)SOURCE",
				R"EXPECT(>> ERROR at line 10: Cannot pass instance value of type "User" as argument 1 of "accept_user()"; expected a class handle whose represented instance type is "User" for "Type[User]".)EXPECT");
	}
	TEST_CASE("original_type_metatype_argument_wrong_class") {
		// Foundry c9d5e35: analyzer/type_metatype_argument_wrong_class.fs; unchanged complete static block.
		original("type_metatype_argument_wrong_class", R"SOURCE(class User:
	pass


func accept_user(_klass: Type[User]) -> void:
	pass


func test():
	accept_user(Node)
)SOURCE",
				R"EXPECT(>> ERROR at line 10: Cannot pass class handle "Node" as argument 1 of "accept_user()"; handle represents "Node", which is not compatible with expected represented instance type "User" for "Type[User]".)EXPECT");
	}
	TEST_CASE("original_type_metatype_assignment_instance") {
		// Foundry c9d5e35: analyzer/type_metatype_assignment_instance.fs; unchanged complete static block.
		original("type_metatype_assignment_instance", R"SOURCE(class User:
	pass


var user_handle: Type[User] = User.new()
)SOURCE",
				R"EXPECT(>> ERROR at line 5: Cannot assign instance value of type "User" to variable "user_handle" with specified type "Type[User]"; expected a class handle whose represented instance type is "User".)EXPECT");
	}
	TEST_CASE("original_type_metatype_assignment_statement_instance") {
		// Foundry c9d5e35: analyzer/type_metatype_assignment_statement_instance.fs; unchanged complete static block.
		original("type_metatype_assignment_statement_instance", R"SOURCE(class User:
	pass


func test():
	var user_handle: Type[User] = User
	user_handle = User.new()
)SOURCE",
				R"EXPECT(>> ERROR at line 7: Cannot assign instance value of type "User" to variable "user_handle" of type "Type[User]"; expected a class handle whose represented instance type is "User".)EXPECT");
	}
	TEST_CASE("original_type_metatype_assignment_statement_wrong_class") {
		// Foundry c9d5e35: analyzer/type_metatype_assignment_statement_wrong_class.fs; unchanged complete static block.
		original("type_metatype_assignment_statement_wrong_class", R"SOURCE(class User:
	pass


func test():
	var user_handle: Type[User] = User
	user_handle = Node
)SOURCE",
				R"EXPECT(>> ERROR at line 7: Cannot assign class handle "Node" to variable "user_handle" of type "Type[User]"; handle represents "Node", which is not compatible with "User".)EXPECT");
	}
	TEST_CASE("original_type_metatype_assignment_wrong_class") {
		// Foundry c9d5e35: analyzer/type_metatype_assignment_wrong_class.fs; unchanged complete static block.
		original("type_metatype_assignment_wrong_class", R"SOURCE(class User:
	pass


var user_handle: Type[User] = Node
)SOURCE",
				R"EXPECT(>> ERROR at line 5: Cannot assign class handle "Node" to variable "user_handle" with specified type "Type[User]"; handle represents "Node", which is not compatible with "User".)EXPECT");
	}
	TEST_CASE("original_type_metatype_assignment_wrong_script_class") {
		// Foundry c9d5e35: analyzer/type_metatype_assignment_wrong_script_class.fs; unchanged complete static block.
		original("type_metatype_assignment_wrong_script_class", R"SOURCE(class User:
	pass


class Admin:
	pass


var user_handle: Type[User] = Admin
)SOURCE",
				R"EXPECT(>> ERROR at line 9: Cannot assign class handle "Admin" to variable "user_handle" with specified type "Type[User]"; handle represents "Admin", which is not compatible with "User".)EXPECT");
	}
	TEST_CASE("original_type_metatype_builtin_scalar") {
		// Foundry c9d5e35: analyzer/type_metatype_builtin_scalar.fs; unchanged complete static block.
		original("type_metatype_builtin_scalar", R"SOURCE(var bad_type: Type[int]
)SOURCE",
				R"EXPECT(>> ERROR at line 1: Builtin metatypes such as "Type[int]" are not supported yet.)EXPECT");
	}
	TEST_CASE("original_type_metatype_incompatible_native_handle") {
		// Foundry c9d5e35: analyzer/type_metatype_incompatible_native_handle.fs; unchanged complete static block.
		original("type_metatype_incompatible_native_handle", R"SOURCE(var bad_type: Type[Control] = Node
)SOURCE",
				R"EXPECT(>> ERROR at line 1: Cannot assign class handle "Node" to variable "bad_type" with specified type "Type[Control]"; handle represents "Node", which is not compatible with "Control".)EXPECT");
	}
	TEST_CASE("original_type_metatype_nested") {
		// Foundry c9d5e35: analyzer/type_metatype_nested.fs; unchanged complete static block.
		original("type_metatype_nested", R"SOURCE(class User:
	pass

var bad_type: Type[Type[User]]
)SOURCE",
				R"EXPECT(>> ERROR at line 4: Type[T] requires an object, script, class, trait, or type-parameter argument.)EXPECT");
	}

	TEST_CASE("legacy_null_returns_preserve_strict_and_nullable_boundaries") {
		StorageFixture storage;
		for (bool strict : { false, true }) {
			TypeProfile profile(strict);
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("class User:\n\tpass\nfunc node() -> Node:\n\treturn null\nfunc object() -> Object:\n\treturn null\nfunc user() -> User:\n\treturn null\nfunc nullable() -> Node?:\n\treturn null\nfunc dynamic() -> Variant:\n\treturn null\n", "res://tests/concrete_types/null.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == !strict);
			diagnostics(parser);
			CHECK(parser.get_errors().size() == (strict ? 3 : 0));
		}
	}
	TEST_CASE("matching_handles_publish_identity_across_member_local_store_and_call") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("class User:\n\tpass\nvar member: Type[User] = User\nfunc accept(_value: Type[User]):\n\tpass\nfunc test():\n\tvar handle: Type[User] = User\n\thandle = User\n\taccept(handle)\n\tvar native_handle: Type[Node] = Control\n\tprint(native_handle)\n", "res://tests/concrete_types/handles.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		const auto *handle = local(parser, "handle");
		BS_TEST_REQUIRE(handle);
		CHECK(handle->get_datatype().is_type_handle_annotation);
		CHECK_FALSE(handle->get_datatype().is_meta_type);
		CHECK(handle->get_datatype().class_type == parser.get_tree()->get_member("User").m_class);
		CHECK(handle->get_datatype().to_string() == "Type[User]");
	}
	TEST_CASE("tuple_discard_publishes_existing_bindings_and_mutable_element_types") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func test():\n\tvar (x, _, y) = (1, true, \"name\")\n\tx = 2\n\tprint(x, y)\n", "res://tests/concrete_types/tuple.barista", false) == OK);
		const auto *body = parser.get_tree()->get_member("test").function->body;
		BS_TEST_REQUIRE(body && body->statements.size() == 3);
		BS_TEST_REQUIRE(body->statements[0]->type == BSParser::Node::VARIABLE_DESTRUCTURE);
		auto *binding = static_cast<BSParser::VariableDestructureNode *>(body->statements[0]);
		BS_TEST_REQUIRE(binding->bindings.size() == 3 && binding->bindings[0] && binding->bindings[2]);
		CHECK(binding->bindings[1] == nullptr);
		auto *first = binding->bindings[0];
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(binding->bindings[0] == first);
		CHECK(first->get_datatype().builtin_type == Variant::INT);
		CHECK(first->get_datatype().type_source == BSParser::DataType::ANNOTATED_INFERRED);
		CHECK_FALSE(first->get_datatype().is_constant);
		CHECK_FALSE(first->get_datatype().is_read_only);
		CHECK(binding->bindings[2]->get_datatype().builtin_type == Variant::STRING);
	}
	TEST_CASE("callable_neighbors_keep_matching_async_and_bare_sync_admission") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("async func work(v: int) -> String:\n\treturn str(v)\nfunc sync(v: int) -> String:\n\treturn str(v)\nfunc test():\n\tvar bare: Callable = work\n\tvar async_cb: AsyncCallable[[int], String] = work\n\tvar sync_cb: Callable[[int], String] = sync\n\tprint(bare, async_cb, sync_cb)\n", "res://tests/concrete_types/callables.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		const auto *async_cb = local(parser, "async_cb");
		const auto *sync_cb = local(parser, "sync_cb");
		BS_TEST_REQUIRE(async_cb && sync_cb);
		CHECK(async_cb->initializer->get_datatype().signature_is_async);
		CHECK(async_cb->initializer->get_datatype().has_method_signature);
		CHECK_FALSE(sync_cb->initializer->get_datatype().signature_is_async);
		CHECK_FALSE(BSTypeCompatibility::check(sync_cb->get_datatype(), async_cb->get_datatype()).compatible);
	}
	TEST_CASE("signal_connect_accepts_matching_lambda_and_named_reference") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("signal registered(node: Node)\nfunc take(node: Node) -> void:\n\tprint(node)\nfunc test():\n\tregistered.connect(func(node: Node) -> void: print(node))\n\tregistered.connect(take)\n", "res://tests/concrete_types/lambda.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		const auto *body = parser.get_tree()->get_member("test").function->body;
		BS_TEST_REQUIRE(body && body->statements.size() == 2 && body->statements[0]->type == BSParser::Node::CALL);
		const auto *call = static_cast<BSParser::CallNode *>(body->statements[0]);
		BS_TEST_REQUIRE(call->arguments.size() == 1 && call->arguments[0]->type == BSParser::Node::LAMBDA);
		const auto &type = call->arguments[0]->get_datatype();
		CHECK(type.has_method_signature);
		BS_TEST_REQUIRE(type.method_parameter_types.size() == 1);
		CHECK(type.method_parameter_types[0].native_type == StringName("Node"));
	}
	TEST_CASE("actual_type_producers_preserve_nil_dynamic_and_native_handle_order") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("enum E:\n\tA = 0\nfunc types(handle: Type[Node], control: Type[Control], node: Node, nullable: Node?, number: int, e: E, dynamic: Variant):\n\tpass\nfunc test():\n\tvar nil = null\n", "res://tests/concrete_types/ordering.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		const auto *function = parser.get_tree()->get_member("types").function;
		const auto *nil = local(parser, "nil");
		BS_TEST_REQUIRE(function && function->parameters.size() == 7 && nil && nil->initializer);
		const auto &null_type = nil->initializer->get_datatype();
		BSTypeCompatibility::Options options;
		options.strict_null = true;
		CHECK(BSTypeCompatibility::check(function->parameters[0]->get_datatype(), null_type, options).compatible);
		CHECK_FALSE(BSTypeCompatibility::check(function->parameters[2]->get_datatype(), null_type, options).compatible);
		CHECK(BSTypeCompatibility::check(function->parameters[3]->get_datatype(), null_type, options).compatible);
		CHECK_FALSE(BSTypeCompatibility::check(function->parameters[4]->get_datatype(), null_type, options).compatible);
		CHECK_FALSE(BSTypeCompatibility::check(function->parameters[5]->get_datatype(), null_type, options).compatible);
		CHECK(BSTypeCompatibility::check(function->parameters[6]->get_datatype(), null_type, options).compatible);
		CHECK(BSTypeCompatibility::check(function->parameters[0]->get_datatype(), function->parameters[1]->get_datatype(), options).compatible);
		CHECK_FALSE(BSTypeCompatibility::check(function->parameters[1]->get_datatype(), function->parameters[0]->get_datatype(), options).compatible);
		CHECK_FALSE(BSTypeCompatibility::check(function->parameters[0]->get_datatype(), function->parameters[2]->get_datatype(), options).compatible);
		const auto gradual = BSTypeCompatibility::check(function->parameters[0]->get_datatype(), function->parameters[6]->get_datatype(), options);
		CHECK(gradual.compatible);
		CHECK(gradual.requires_runtime_check);
		options.strict_dynamic = true;
		CHECK_FALSE(BSTypeCompatibility::check(function->parameters[0]->get_datatype(), function->parameters[6]->get_datatype(), options).compatible);
	}
	TEST_CASE("signatureless_sources_require_runtime_check_without_losing_async_identity") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func types(bare: Callable, async_bare: AsyncCallable, typed: Callable[[], int], async_typed: AsyncCallable[[], int]):\n\tpass\n", "res://tests/concrete_types/unsigned.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		const auto *function = parser.get_tree()->get_member("types").function;
		BS_TEST_REQUIRE(function && function->parameters.size() == 4);
		const auto &parameters = function->parameters;
		for (int i : { 0, 1 }) {
			const auto result = BSTypeCompatibility::check(parameters[i + 2]->get_datatype(), parameters[i]->get_datatype());
			CHECK(result.compatible);
			CHECK(result.requires_runtime_check);
		}
		CHECK_FALSE(BSTypeCompatibility::check(parameters[3]->get_datatype(), parameters[0]->get_datatype()).compatible);
		CHECK_FALSE(BSTypeCompatibility::check(parameters[2]->get_datatype(), parameters[1]->get_datatype()).compatible);
		CHECK(BSTypeCompatibility::check(parameters[0]->get_datatype(), parameters[3]->get_datatype()).compatible);
	}
	TEST_CASE("signed_slots_preserve_nested_signature_conflicts_and_rest_direction") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func types(a: Callable[[Callable[[int], void]], void], b: Callable[[Callable[[String], void]], void], gradual: Callable[[...Array], void], typed: Callable[[...Array[int]], void]):\n\tpass\n", "res://tests/concrete_types/signature_slots.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		const auto *function = parser.get_tree()->get_member("types").function;
		BS_TEST_REQUIRE(function && function->parameters.size() == 4);
		const auto &p = function->parameters;
		CHECK_FALSE(BSTypeCompatibility::check(p[0]->get_datatype(), p[1]->get_datatype()).compatible);
		CHECK(BSTypeCompatibility::check(p[3]->get_datatype(), p[2]->get_datatype()).compatible);
		CHECK_FALSE(BSTypeCompatibility::check(p[2]->get_datatype(), p[3]->get_datatype()).compatible);
	}
	TEST_CASE("nullable_tuple_dereference_recovers_once_and_narrowing_admits") {
		StorageFixture storage;
		for (bool strict : { false, true }) {
			TypeProfile profile(strict);
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("func test(pair: (int, String)?):\n\tvar (x, y) = pair\n\tprint(x, y)\n", "res://tests/concrete_types/nullable_tuple.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == !strict);
			CHECK(parser.get_errors().size() == (strict ? 1 : 0));
			const auto *body = parser.get_tree()->get_member("test").function->body;
			BS_TEST_REQUIRE(body && body->statements[0]->type == BSParser::Node::VARIABLE_DESTRUCTURE);
			const auto *destructure = static_cast<BSParser::VariableDestructureNode *>(body->statements[0]);
			if (strict) {
				BS_TEST_REQUIRE(parser.get_errors().size() == 1);
				const auto &error = parser.get_errors().front()->get();
				CHECK(error.message == "Cannot destructure the nullable value of type \"(int, String)?\"; check for null first.");
				CHECK(error.line == destructure->initializer->start_line);
				CHECK(error.column == destructure->initializer->start_column);
				CHECK(error.end_line == destructure->initializer->end_line);
				CHECK(error.end_column == destructure->initializer->end_column);
				CHECK(destructure->bindings[0]->get_datatype().is_variant());
				CHECK(destructure->bindings[0]->get_datatype().type_source == BSParser::DataType::UNDETECTED);
			}
		}
		TypeProfile strict(true);
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func test(pair: (int, String)?):\n\tif pair != null:\n\t\tvar (x, y) = pair\n\t\tprint(x, y)\n", "res://tests/concrete_types/narrowed_tuple.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
	}
	TEST_CASE("const_destructure_capture_and_write_once_keep_parser_binding_identity") {
		StorageFixture storage;
		TypeProfile profile;
		for (bool write : { false, true }) {
			BSParser parser;
			const String source = String("func test(pair: (int, String)):\n\tconst (x, y) = pair\n\tvar read = func(): print(x, y)\n\tread.call()\n") + (write ? "\tx = 3\n" : "");
			BS_TEST_REQUIRE(parser.parse(source, "res://tests/concrete_types/const_tuple.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == !write);
			diagnostics(parser);
			CHECK(parser.get_errors().size() == (write ? 1 : 0));
			const auto *body = parser.get_tree()->get_member("test").function->body;
			BS_TEST_REQUIRE(body && body->statements[0]->type == BSParser::Node::VARIABLE_DESTRUCTURE);
			const auto *destructure = static_cast<BSParser::VariableDestructureNode *>(body->statements[0]);
			BS_TEST_REQUIRE(destructure->bindings.size() == 2 && destructure->bindings[0]);
			CHECK(destructure->bindings[0]->is_final);
			CHECK_FALSE(destructure->bindings[0]->get_datatype().is_constant);
			CHECK_FALSE(destructure->bindings[0]->get_datatype().is_read_only);
			if (!write) {
				CHECK(analyzer.analyze() == OK);
				CHECK(parser.get_errors().is_empty());
				CHECK(parser.get_warnings().is_empty());
			}
		}
	}
	TEST_CASE("zero_parameter_rich_sources_still_invoke_and_transform") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func target() -> int:\n\treturn 1\nfunc test():\n\tvar value = target.call()\n\tvar bound = target.bind()\n\tvar lambda = func() -> int: return 2\n\tvar got = lambda.call()\n\tprint(value, bound, got)\n", "res://tests/concrete_types/zero_rich.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		const auto *value = local(parser, "value"), *bound = local(parser, "bound"), *lambda = local(parser, "lambda"), *got = local(parser, "got");
		BS_TEST_REQUIRE(value && bound && lambda && got);
		CHECK(value->get_datatype().builtin_type == Variant::INT);
		CHECK(got->get_datatype().builtin_type == Variant::INT);
		CHECK(bound->get_datatype().has_explicit_method_signature);
		CHECK(bound->get_datatype().method_parameter_types.is_empty());
		CHECK_FALSE(lambda->initializer->get_datatype().has_explicit_method_signature);
		CHECK(lambda->initializer->get_datatype().has_method_signature);
		CHECK(lambda->initializer->get_datatype().method_parameter_types.is_empty());
		CHECK(lambda->initializer->get_datatype().method_return_type.size() == 1);
	}
	TEST_CASE("destructure_unused_and_shadow_warnings_are_once_only_on_reanalysis") {
		StorageFixture storage;
		TypeProfile profile;
		profile.set(BSWarning::get_setting_path_from_code(BSWarning::UNUSED_VARIABLE), BSWarning::WARN);
		profile.set(BSWarning::get_setting_path_from_code(BSWarning::SHADOWED_VARIABLE), BSWarning::WARN);
		BSParser::update_project_settings();
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("var x: int\nfunc test(pair: (int, int)):\n\tvar (x, y) = pair\n", "res://tests/concrete_types/tuple_warnings.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		int unused = 0, shadow = 0;
		for (const auto &w : parser.get_warnings()) {
			unused += w.code == BSWarning::UNUSED_VARIABLE;
			shadow += w.code == BSWarning::SHADOWED_VARIABLE;
		}
		CHECK(unused == 2);
		CHECK(shadow == 1);
		const int warnings = parser.get_warnings().size();
		CHECK(analyzer.analyze() == OK);
		CHECK(parser.get_warnings().size() == warnings);
	}
	TEST_CASE("repair1_call_strict_variant") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		for (bool enabled : { false, true }) {
			TypeProfile profile(false);
			profile.set("debug/barista_script/analysis/strict_dynamic_checks", enabled);
			BSParser::update_project_settings();
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("func take(_value: Type[Node]):\n\tpass\nfunc test(value: Variant):\n\ttake(value)\n", "res://tests/concrete_types/repair1_call_strict_variant.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			const bool rejected = enabled;
			CHECK((analyzer.analyze() != OK) == rejected);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (rejected ? 1 : 0));
			CHECK(parser.get_warnings().is_empty());
			if (rejected) {
				const auto &error = parser.get_errors().front()->get();
				CHECK(error.message == "Cannot pass Variant value as argument 1 of \"take()\" in strict dynamic mode; expected \"Type[Node]\".");
				CHECK(error.line == 4);
				CHECK(error.column == 10);
			}
		}
	}
	TEST_CASE("repair1_call_strict_nullable") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		for (bool enabled : { false, true }) {
			TypeProfile profile(enabled);
			profile.set("debug/barista_script/analysis/strict_dynamic_checks", false);
			BSParser::update_project_settings();
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("func take(_value: Type[Node]):\n\tpass\nfunc test(value: Type[Node]?):\n\ttake(value)\n", "res://tests/concrete_types/repair1_call_strict_nullable.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			const bool rejected = enabled;
			CHECK((analyzer.analyze() != OK) == rejected);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (rejected ? 1 : 0));
			CHECK(parser.get_warnings().is_empty());
			if (rejected) {
				const auto &error = parser.get_errors().front()->get();
				CHECK(error.message == "Cannot pass nullable value of type \"Type[Node]?\" as argument 1 of \"take()\"; expected non-nullable \"Type[Node]\".");
				CHECK(error.line == 4);
				CHECK(error.column == 10);
			}
		}
	}
	TEST_CASE("repair1_store_strict_nullable") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		for (bool enabled : { false, true }) {
			TypeProfile profile(enabled);
			profile.set("debug/barista_script/analysis/strict_dynamic_checks", false);
			BSParser::update_project_settings();
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("func test(value: Type[Node]?):\n\tvar target: Type[Node] = Node\n\ttarget = value\n", "res://tests/concrete_types/repair1_store_strict_nullable.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			const bool rejected = enabled;
			CHECK((analyzer.analyze() != OK) == rejected);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (rejected ? 1 : 0));
			CHECK(parser.get_warnings().is_empty());
			if (rejected) {
				const auto &error = parser.get_errors().front()->get();
				CHECK(error.message == "Cannot assign nullable value of type \"Type[Node]?\" to variable \"target\"; expected non-nullable \"Type[Node]\".");
				CHECK(error.line == 3);
				CHECK(error.column == 14);
			}
		}
	}
	TEST_CASE("repair1_local_nullable_Node") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		for (bool enabled : { false, true }) {
			TypeProfile profile(enabled);
			profile.set("debug/barista_script/analysis/strict_dynamic_checks", false);
			BSParser::update_project_settings();
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("func test(value: Node?):\n\tvar target: Node = value\n", "res://tests/concrete_types/repair1_local_nullable_Node.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			const bool rejected = enabled;
			CHECK((analyzer.analyze() != OK) == rejected);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (rejected ? 1 : 0));
			CHECK(parser.get_warnings().is_empty());
			if (rejected) {
				const auto &error = parser.get_errors().front()->get();
				CHECK(error.message == "Cannot assign nullable value of type \"Node?\" to variable \"target\"; expected non-nullable \"Node\".");
				CHECK(error.line == 2);
				CHECK(error.column == 24);
			}
		}
	}
	TEST_CASE("repair1_local_nullable_Type_Node") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		for (bool enabled : { false, true }) {
			TypeProfile profile(enabled);
			profile.set("debug/barista_script/analysis/strict_dynamic_checks", false);
			BSParser::update_project_settings();
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("func test(value: Type[Node]?):\n\tvar target: Type[Node] = value\n", "res://tests/concrete_types/repair1_local_nullable_Type_Node.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			const bool rejected = enabled;
			CHECK((analyzer.analyze() != OK) == rejected);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (rejected ? 1 : 0));
			CHECK(parser.get_warnings().is_empty());
			if (rejected) {
				const auto &error = parser.get_errors().front()->get();
				CHECK(error.message == "Cannot assign nullable value of type \"Type[Node]?\" to variable \"target\"; expected non-nullable \"Type[Node]\".");
				CHECK(error.line == 2);
				CHECK(error.column == 30);
			}
		}
	}
	TEST_CASE("repair1_local_strict_variant") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		for (bool enabled : { false, true }) {
			TypeProfile profile(false);
			profile.set("debug/barista_script/analysis/strict_dynamic_checks", enabled);
			BSParser::update_project_settings();
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("func test(value: Variant):\n\tvar target: Type[Node] = value\n", "res://tests/concrete_types/repair1_local_strict_variant.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			const bool rejected = enabled;
			CHECK((analyzer.analyze() != OK) == rejected);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (rejected ? 1 : 0));
			CHECK(parser.get_warnings().is_empty());
			if (rejected) {
				const auto &error = parser.get_errors().front()->get();
				CHECK(error.message == "Cannot assign Variant value to variable \"target\" in strict dynamic mode; expected \"Type[Node]\".");
				CHECK(error.line == 2);
				CHECK(error.column == 30);
			}
		}
	}
	TEST_CASE("repair1_member_strict_variant") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		for (bool enabled : { false, true }) {
			TypeProfile profile(false);
			profile.set("debug/barista_script/analysis/strict_dynamic_checks", enabled);
			BSParser::update_project_settings();
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("var target: Type[Node] = source()\nfunc source() -> Variant:\n\treturn null\n", "res://tests/concrete_types/repair1_member_strict_variant.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			const bool rejected = enabled;
			CHECK((analyzer.analyze() != OK) == rejected);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (rejected ? 1 : 0));
			CHECK(parser.get_warnings().is_empty());
			if (rejected) {
				const auto &error = parser.get_errors().front()->get();
				CHECK(error.message == "Cannot assign Variant value to variable \"target\" in strict dynamic mode; expected \"Type[Node]\".");
				CHECK(error.line == 1);
				CHECK(error.column == 26);
			}
		}
	}
	TEST_CASE("repair1_member_strict_nullable") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		for (bool enabled : { false, true }) {
			TypeProfile profile(enabled);
			profile.set("debug/barista_script/analysis/strict_dynamic_checks", false);
			BSParser::update_project_settings();
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("var target: Type[Node] = source()\nfunc source() -> Type[Node]?:\n\treturn null\n", "res://tests/concrete_types/repair1_member_strict_nullable.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			const bool rejected = enabled;
			CHECK((analyzer.analyze() != OK) == rejected);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (rejected ? 1 : 0));
			CHECK(parser.get_warnings().is_empty());
			if (rejected) {
				const auto &error = parser.get_errors().front()->get();
				CHECK(error.message == "Cannot assign nullable value of type \"Type[Node]?\" to variable \"target\"; expected non-nullable \"Type[Node]\".");
				CHECK(error.line == 1);
				CHECK(error.column == 26);
			}
		}
	}
	TEST_CASE("repair1_profile_off_and_nullable_destination") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		for (bool enabled : { false, true }) {
			TypeProfile profile(enabled);
			profile.set("debug/barista_script/analysis/strict_dynamic_checks", false);
			BSParser::update_project_settings();
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("func take(_value: Type[Node]?):\n\tpass\nfunc test(value: Type[Node]?):\n\tvar target: Type[Node]? = value\n\ttarget = value\n\ttake(target)\n", "res://tests/concrete_types/repair1_profile_off_and_nullable_destination.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			CHECK(parser.get_errors().is_empty());
			CHECK(parser.get_warnings().is_empty());
		}
	}
	TEST_CASE("repair1_store_strict_variant") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		for (bool enabled : { false, true }) {
			TypeProfile profile;
			profile.set("debug/barista_script/analysis/strict_dynamic_checks", enabled);
			BSParser::update_project_settings();
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("func test(value: Variant):\n\tvar target: Type[Node] = Node\n\ttarget = value\n", "res://tests/concrete_types/repair1_store_strict_variant.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			const bool rejected = enabled;
			CHECK((analyzer.analyze() != OK) == rejected);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == (rejected ? 1 : 0));
			CHECK(parser.get_warnings().is_empty());
			if (rejected) {
				const auto &error = parser.get_errors().front()->get();
				CHECK(error.message == "Cannot assign Variant value to variable \"target\" in strict dynamic mode; expected \"Type[Node]\".");
				CHECK(error.line == 3);
				CHECK(error.column == 14);
			}
		}
	}
}
