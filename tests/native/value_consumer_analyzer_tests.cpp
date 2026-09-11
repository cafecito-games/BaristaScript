/**************************************************************************/
/*  value_consumer_analyzer_tests.cpp                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer_probe.h"
#include "bs_conformance_registry.h"
#include "bs_type.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <godot_cpp/classes/project_settings.hpp>
#include <string>
#include <utility>
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
void original(const char *name, const char *source, const char *expected, std::initializer_list<std::pair<int, int>> starts, const char *helper = nullptr) {
	StorageFixture storage;
	BSConformanceRegistry::ScopedCorpusState conformances;
	TypeProfile profile;
	if (helper) {
		BS_TEST_REQUIRE(write_bytes(storage.path("external_inner_class_as_constant_external.notest.barista"), bytes(helper)));
	}
	BSParser parser;
	BS_TEST_REQUIRE(parser.parse(source, storage.path(String(name) + ".barista"), false) == OK);
	BSAnalyzer analyzer(&parser);
	const bool valid = String(expected) == "BS_TEST_OK";
	CHECK((analyzer.analyze() == OK) == valid);
	String actual;
	for (const auto &e : parser.get_errors()) {
		MESSAGE(std::string(e.message.utf8().get_data()), " at ", e.line, ":", e.column);
		if (!actual.is_empty())
			actual += "\n";
		actual += vformat(">> ERROR at line %d: %s", e.line, e.message);
	}
	if (actual.is_empty())
		actual = "BS_TEST_OK";
	CHECK(std::string(actual.utf8().get_data()) == std::string(expected));
	BS_TEST_REQUIRE(parser.get_errors().size() == int(starts.size()));
	auto position = starts.begin();
	for (const auto &e : parser.get_errors()) {
		CHECK(e.line == position->first);
		CHECK(e.column == position->second);
		++position;
	}
	CHECK(parser.get_warnings().is_empty());
}
} //namespace
TEST_SUITE("value_consumer_analyzer") {
	TEST_CASE("tuple_constant_member_and_local_keep_named_raw_and_public_errors") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		TypeProfile profile;
		for (bool local : { false, true }) {
			const String source = local ? "func test():\n\tconst VALUE: (int, int) = (1, \"x\")\n\tconst GOOD: (int, int) = (1, 2)\n" : "const VALUE: (int, int) = (1, \"x\")\nconst GOOD: (int, int) = (1, 2)\n";
			const String path = storage.path(local ? "local_tuple_constant.barista" : "member_tuple_constant.barista");
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			BSParser::ConstantNode *bad = nullptr, *good = nullptr;
			if (local) {
				auto *body = parser.get_tree()->get_member("test").function->body;
				BS_TEST_REQUIRE(body && body->statements.size() == 2);
				bad = static_cast<BSParser::ConstantNode *>(body->statements[0]);
				good = static_cast<BSParser::ConstantNode *>(body->statements[1]);
			} else {
				bad = parser.get_tree()->get_member("VALUE").constant;
				good = parser.get_tree()->get_member("GOOD").constant;
			}
			BS_TEST_REQUIRE(bad && good && bad->initializer && good->initializer);
			BS_TEST_REQUIRE(bad->initializer->type == BSParser::Node::TUPLE_LITERAL);
			auto *tuple = static_cast<BSParser::TupleLiteralNode *>(bad->initializer);
			BS_TEST_REQUIRE(tuple->elements.size() == 2);
			const auto *element = tuple->elements[1];
			const int line = local ? 2 : 1, declaration_column = local ? 31 : 27, element_column = local ? 35 : 31;
			const String include_error = "Cannot include a value of type \"String\" as \"int\".";
			const String declaration_error = "Cannot assign a value of type (int, String) to constant \"VALUE\" with specified type (int, int).";
			BS_TEST_REQUIRE(parser.get_errors().size() == 2);
			int index = 0;
			for (const auto &error : parser.get_errors()) {
				const auto *origin = index == 0 ? element : bad->initializer;
				CHECK(error.message == (index == 0 ? include_error : declaration_error));
				CHECK(error.line == line);
				CHECK(error.column == (index == 0 ? element_column : declaration_column));
				CHECK(error.line == origin->start_line);
				CHECK(error.column == origin->start_column);
				CHECK(error.end_line == origin->end_line);
				CHECK(error.end_column == origin->end_column);
				++index;
			}
			CHECK_FALSE(BSAnalyzer::has_materialized_constant_value(bad->initializer));
			CHECK_FALSE(bad->initializer->is_constant);
			CHECK_FALSE(bad->initializer->get_datatype().is_constant);
			CHECK(bad->initializer->reduced_value.get_type() == Variant::NIL);
			CHECK(BSAnalyzer::has_materialized_constant_value(good->initializer));
			CHECK(parser.get_warnings().is_empty());
			CHECK(analyzer.analyze() != OK);
			CHECK(parser.get_errors().size() == 2);
			Ref<BaristaScriptAnalyzerProbe> probe;
			probe.instantiate();
			const Dictionary report = probe->validate_source(source, path, true);
			CHECK_FALSE(bool(report.get("valid", true)));
			const Array errors = report.get("errors", Array());
			BS_TEST_REQUIRE(errors.size() == 2);
			for (int i = 0; i < 2; ++i) {
				const Dictionary error = errors[i];
				CHECK(String(error.get("path", "")) == path);
				CHECK(String(error.get("message", "")) == (i == 0 ? declaration_error : include_error));
				CHECK(int(error.get("line", -1)) == line);
				CHECK(int(error.get("column", -1)) == (i == 0 ? declaration_column : element_column));
			}
			CHECK(Array(report.get("warnings", Array())).is_empty());
		}
	}

	TEST_CASE("original_type_self_named_tuple_argument_other_receiver") { original("type_self_named_tuple_argument_other_receiver", "# A named tuple with a `Self` field renders only its declared name, so the receiver-identity\n# rejection used to read \"should be \"Pair\" but is \"Pair\"\"; the clause names the `Self` binding\n# that actually differs.\nclass Receiver:\n\ttuple Pair(index: int, owner: Self)\n\n\tfunc take_pair(_pair: Pair) -> void:\n\t\tpass\n\n\nfunc test() -> void:\n\tvar receiver := Receiver.new()\n\tvar other := Receiver.new()\n\tvar pair := other.Pair(1, other)\n\treceiver.take_pair(pair)\n", ">> ERROR at line 15: Invalid argument for \"take_pair()\" function: argument 1 should be \"Pair\" but is \"Pair\". The parameter's \"Self\" stands for the exact receiver at this use; the argument has \"Receiver\" as field \"owner\".", { { 15, 24 } }); }
	TEST_CASE("original_type_self_named_tuple_assignment_foreign_value") { original("type_self_named_tuple_assignment_foreign_value", "# Assigning a named tuple whose `Self` field was bound at construction to a variable whose\n# specified type still reads `Self` contrasts identically; the clause names the binding.\nclass Receiver:\n\ttuple Pair(index: int, owner: Self)\n\n\tfunc stash(other: Receiver) -> void:\n\t\tvar mine: Pair = other.Pair(1, other)\n\t\tprint(mine.index)\n\n\nfunc test() -> void:\n\tpass\n", ">> ERROR at line 7: Cannot assign a value of type Pair to variable \"mine\" with specified type Pair. The specified type's \"Self\" stands for the exact receiver at this use; the value has \"Receiver\" as field \"owner\".", { { 7, 26 } }); }
	TEST_CASE("original_type_self_named_tuple_field_equal_bound_carrier_open_receiver") { original("type_self_named_tuple_field_equal_bound_carrier_open_receiver", "# A named tuple's `Self` field is the constructing receiver's, and a carrier's element type is checked\n# invariantly, so a carrier typed with the calling frame's `Self` is admissible only when the\n# construction runs through that same receiver.\nclass Cell:\n\ttuple Crate(index: int, items: Array[Self])\n\n\tfunc construct_via_base(cell: Cell) -> void:\n\t\tvar items: Array[Self] = []\n\t\tvar made := cell.Crate(1, items)\n\t\tprint(made.index)\n\n\nfunc test() -> void:\n\tpass\n", ">> ERROR at line 9: Invalid argument 2 for tuple \"Crate\": should be \"Array[Self]\" but is \"Array[Self]\". The tuple field's \"Self\" is resolved against the receiver expression; the argument is relative to the calling frame's receiver.", { { 9, 35 } }); }
	TEST_CASE("original_type_self_named_tuple_field_instance_base_foreign") { original("type_self_named_tuple_field_instance_base_foreign", "# Constructing a named tuple through an instance base answers `Self` identity against the base\n# expression, not the calling frame: another instance is rejected, and so is the frame's own `self`.\nclass Receiver:\n\ttuple Pair(index: int, owner: Self)\n\n\tfunc construct_via_base(receiver: Receiver, other: Receiver) -> void:\n\t\tvar first := receiver.Pair(1, other)\n\t\tvar second := receiver.Pair(2, self)\n\t\tprint(first, second)\n\n\nfunc test() -> void:\n\tpass\n", ">> ERROR at line 7: Invalid argument 2 for tuple \"Pair\": should be \"Self\" but is \"Receiver\".\n>> ERROR at line 8: Invalid argument 2 for tuple \"Pair\": should be \"Self\" but is \"Self\". The tuple field's \"Self\" is resolved against the receiver expression; the argument is relative to the calling frame's receiver.", { { 7, 39 }, { 8, 40 } }); }
	TEST_CASE("original_type_self_named_tuple_return_foreign_value") { original("type_self_named_tuple_return_foreign_value", "# A returned named tuple whose `Self` field was bound at construction contrasts identically\n# against the declared return type; the clause names the binding that differs.\nclass Receiver:\n\ttuple Pair(index: int, owner: Self)\n\n\tfunc remake(pair: Pair, other: Receiver) -> Pair:\n\t\treturn other.Pair(pair.index, other)\n\n\nfunc test() -> void:\n\tpass\n", ">> ERROR at line 7: Cannot return value of type \"Pair\" because the function return type is \"Pair\". The return type's \"Self\" stands for the exact receiver at this use; the returned value has \"Receiver\" as field \"owner\".", { { 7, 9 } }); }
	TEST_CASE("original_typed_array_assignment") { original("typed_array_assignment", "func test():\n\tconst arr: Array[int] = [\"Hello\", \"World\"]\n", ">> ERROR at line 2: Cannot include a value of type \"String\" as \"int\".\n>> ERROR at line 2: Cannot have an element of type \"String\" in an array of type \"Array[int]\".", { { 2, 30 }, { 2, 30 } }); }
	TEST_CASE("original_typed_container_mutation_methods") { original("typed_container_mutation_methods", "func test():\n\tvar values: Array[int] = [1]\n\tvar texts: Array[String] = [\"bad\"]\n\tvalues.append(\"bad\")\n\tvalues.insert(0, \"bad\")\n\tvalues.set(0, \"bad\")\n\tvalues.assign(texts)\n\n\tvar scores: Dictionary[String, int] = { \"one\": 1 }\n\tvar text_scores: Dictionary[String, String] = { \"one\": \"bad\" }\n\tvar int_scores: Dictionary[int, int] = { 1: 1 }\n\tscores.set(1, 1)\n\tscores.set(\"one\", \"bad\")\n\tscores.assign(text_scores)\n\tscores.assign(int_scores)\n\tscores.merge(text_scores)\n\tscores.merge(int_scores)\n", ">> ERROR at line 4: Invalid argument for \"append()\" function: argument 1 should be \"int\" but is \"String\".\n>> ERROR at line 5: Invalid argument for \"insert()\" function: argument 2 should be \"int\" but is \"String\".\n>> ERROR at line 6: Invalid argument for \"set()\" function: argument 2 should be \"int\" but is \"String\".\n>> ERROR at line 7: Invalid argument for \"assign()\" function: argument 1 should be \"Array[int]\" but is \"Array[String]\".\n>> ERROR at line 12: Invalid argument for \"set()\" function: argument 1 should be \"String\" but is \"int\".\n>> ERROR at line 13: Invalid argument for \"set()\" function: argument 2 should be \"int\" but is \"String\".\n>> ERROR at line 14: Invalid argument for \"assign()\" function: argument 1 should be \"Dictionary[String, int]\" but is \"Dictionary[String, String]\".\n>> ERROR at line 15: Invalid argument for \"assign()\" function: argument 1 should be \"Dictionary[String, int]\" but is \"Dictionary[int, int]\".\n>> ERROR at line 16: Invalid argument for \"merge()\" function: argument 1 should be \"Dictionary[String, int]\" but is \"Dictionary[String, String]\".\n>> ERROR at line 17: Invalid argument for \"merge()\" function: argument 1 should be \"Dictionary[String, int]\" but is \"Dictionary[int, int]\".", { { 4, 19 }, { 5, 22 }, { 6, 19 }, { 7, 19 }, { 12, 16 }, { 13, 23 }, { 14, 19 }, { 15, 19 }, { 16, 18 }, { 17, 18 } }); }
	TEST_CASE("original_typed_dictionary_assignment") { original("typed_dictionary_assignment", "func test():\n\tconst dict: Dictionary[int, int] = { \"Hello\": \"World\" }\n", ">> ERROR at line 2: Cannot include a value of type \"String\" as \"int\".\n>> ERROR at line 2: Cannot have a key of type \"String\" in a dictionary of type \"Dictionary[int, int]\".", { { 2, 42 }, { 2, 42 } }); }
	TEST_CASE("original_assign_signal") { original("assign_signal", "signal your_base\nsignal my_base\nfunc test():\n\tyour_base = my_base\n", ">> ERROR at line 4: Cannot assign a new value to a constant.", { { 4, 5 } }); }
	TEST_CASE("original_assign_to_read_only_property") { original("assign_to_read_only_property", "func test():\n\tvar tree := SceneTree.new()\n\ttree.root = Window.new()\n\ttree.free()\n", ">> ERROR at line 3: Cannot assign a new value to a read-only property.", { { 3, 5 } }); }
	TEST_CASE("original_assign_to_read_only_property_indirectly") { original("assign_to_read_only_property_indirectly", "func test():\n\tvar state := PhysicsDirectBodyState3DExtension.new()\n\tstate.center_of_mass.x += 1.0\n\tstate.free()\n", ">> ERROR at line 3: Cannot assign a new value to a read-only property.", { { 3, 5 } }); }
	TEST_CASE("original_tuple_named_construction") { original("tuple_named_construction", "# A named tuple is constructed by calling its declaration and its fields are readable by name and\n# by index. Both erase to the same read-only Array shape at runtime.\ntuple Vec2(x: float, y: float)\n\nfunc test():\n\tvar point := Vec2(1.5, 2.5)\n\tprint(point)\n\tprint(point.x)\n\tprint(point.y)\n\tprint(point.0)\n\tvar erased: (float, float) = point\n\tprint(erased)\n", "BS_TEST_OK", {}); }
	TEST_CASE("original_tuple_typing_named_norun") { original("tuple_typing_named.norun", "# A named tuple is nominal: it is built by calling its declaration, its fields are reachable by\n# name and by index, and it erases safely to the unnamed tuple of its element types.\ntuple Vec2(x: float, y: float)\ntuple Mixed(name: String, int, bool)\n\nfunc construct() -> Vec2:\n\treturn Vec2(1.0, 2.0)\n\nfunc read_fields(point: Vec2) -> float:\n\tvar by_name: float = point.x\n\tvar by_index: float = point.1\n\treturn by_name + by_index\n\nfunc erase(point: Vec2) -> (float, float):\n\treturn point\n\nfunc mixed_fields(value: Mixed) -> String:\n\tvar name: String = value.name\n\tvar count: int = value.1\n\tvar flag: bool = value.2\n\treturn name + str(count) + str(flag)\n", "BS_TEST_OK", {}); }
	TEST_CASE("original_final_static_var_reassigned_qualified") { original("final_static_var_reassigned_qualified", "# A `final static var` has a single shared slot, so a qualified write outside its\n# `_static_init()` slot is rejected just like a bare-name write.\nclass Holder:\n\tfinal static var VALUE := 1\n\nfunc test() -> void:\n\tHolder.VALUE = 2\n", ">> ERROR at line 7: Final variable \"VALUE\" can only be assigned in its declaration or in \"_static_init()\".", { { 7, 5 } }); }
	TEST_CASE("original_external_inner_class_as_constant") { original("external_inner_class_as_constant", "const External = preload(\"external_inner_class_as_constant_external.notest.barista\")\nconst ExternalInnerClass = External.InnerClass\n\nfunc test():\n\tvar inst_external: ExternalInnerClass = ExternalInnerClass.new()\n\tinst_external.x = 4.0\n\tprint(inst_external.x)\n", "BS_TEST_OK", {}, "class InnerClass:\n\tvar x: = 3.0\n"); }
	TEST_CASE("structural_tuple_declaration_call_store_return") { original("structural_tuple_declaration_call_store_return", "tuple Vec(x: int, y: int)\nfunc take(_pair: (int, int)):\n\tpass\nfunc erase(value: Vec) -> (int, int):\n\treturn value\nfunc test(value: Vec):\n\tvar pair: (int, int) = value\n\tpair = value\n\ttake(value)\n\tprint(pair)\n", "BS_TEST_OK", {}); }
	TEST_CASE("final_and_nullable_named_tuple_fields") { original("final_and_nullable_named_tuple_fields", "final class Cell:\n\ttuple Pair(index: int, owner: Self?)\n\tfunc test(other: Cell):\n\t\tvar first := other.Pair(1, other)\n\t\tvar second := other.Pair(1, null)\n\t\tprint(first, second)\n", "BS_TEST_OK", {}); }
	TEST_CASE("literal_self_carrier_current_receiver_and_return") { original("literal_self_carrier_current_receiver_and_return", "class Cell:\n\ttuple Bag(index: int, items: Array[Self])\n\tfunc values() -> Array[Self]:\n\t\treturn [self]\n\tfunc test():\n\t\tvar made := Bag(1, [self])\n\t\tprint(made)\n", "BS_TEST_OK", {}); }
	TEST_CASE("literal_self_carrier_foreign_receiver") { original("literal_self_carrier_foreign_receiver", "class Cell:\n\ttuple Bag(index: int, items: Array[Self])\n\tfunc test(other: Cell):\n\t\tvar made := other.Bag(1, [self])\n\t\tprint(made)\n", ">> ERROR at line 4: Invalid argument 2 for tuple \"Bag\": should be \"Array[Self]\" but is \"Array[Cell]\". The tuple field's \"Self\" is resolved against the receiver expression; the argument is relative to the calling frame's receiver.", { { 4, 34 } }); }
	TEST_CASE("readonly_native_class_and_bare_paths") { original("readonly_native_class_and_bare_paths", "class ConsumerTree extends SceneTree:\n\tfunc bare():\n\t\troot = Window.new()\nfunc test(tree: ConsumerTree, native: SceneTree):\n\ttree.root = Window.new()\n\tnative.root = Window.new()\n", ">> ERROR at line 3: Cannot assign a new value to a read-only property.\n>> ERROR at line 5: Cannot assign a new value to a read-only property.\n>> ERROR at line 6: Cannot assign a new value to a read-only property.", { { 3, 9 }, { 5, 5 }, { 6, 5 } }); }
	TEST_CASE("readonly_shared_references_allow_nested_mutation") { original("readonly_shared_references_allow_nested_mutation", "func test(tree: SceneTree, capture: RegExMatch, feed: CameraFeed):\n\ttree.root.name = \"renamed\"\n\tcapture.names[\"x\"] = 1\n\tfeed.formats[0] = {}\n", "BS_TEST_OK", {}); }
	TEST_CASE("readonly_packed_array_uses_getter_value_type") { original("readonly_packed_array_uses_getter_value_type", "func test(capture: RegExMatch):\n\tcapture.strings[0] = \"value\"\n", ">> ERROR at line 2: Cannot assign a new value to a read-only property.", { { 2, 5 } }); }
	TEST_CASE("readonly_shared_slots_reject_replacement") { original("readonly_shared_slots_reject_replacement", "func test(tree: SceneTree, capture: RegExMatch):\n\ttree.root = Window.new()\n\tcapture.names = {}\n\tcapture.strings = []\n", ">> ERROR at line 2: Cannot assign a new value to a read-only property.\n>> ERROR at line 3: Cannot assign a new value to a read-only property.\n>> ERROR at line 4: Cannot assign a new value to a read-only property.", { { 2, 5 }, { 3, 5 }, { 4, 5 } }); }
	TEST_CASE("readonly_value_copy_out_and_writable_native_properties") { original("readonly_value_copy_out_and_writable_native_properties", "extends PhysicsDirectBodyState3DExtension\nvar member = center_of_mass\nfunc test(node: Node, window: Window):\n\tvar local = center_of_mass\n\tlocal.x = 1.0\n\tmember.x = 2.0\n\tnode.name = \"value\"\n\twindow.title = \"title\"\n", "BS_TEST_OK", {}); }
	TEST_CASE("inherited_signal_and_mutable_signal_slots") { original("inherited_signal_and_mutable_signal_slots", "class Parent:\n\tsignal ping\nclass Child extends Parent:\n\tvar held = ping\n\tfunc test():\n\t\tvar local: Signal = ping\n\t\theld = ping\n\t\tlocal = ping\n\t\tping = ping\n", ">> ERROR at line 9: Cannot assign a new value to a constant.", { { 9, 9 } }); }
	TEST_CASE("constant_containers_and_tuple_value_keep_distinct_errors") { original("constant_containers_and_tuple_value_keep_distinct_errors", "func test():\n\tconst values = [1]\n\tvalues[0] = 2\n\tvar pair = (1, 2)\n\tpair.0 = 3\n", ">> ERROR at line 3: Cannot assign a new value to a constant.\n>> ERROR at line 5: Cannot assign to an element of tuple \"(int, int)\"; tuples are immutable.", { { 3, 5 }, { 5, 5 } }); }
	TEST_CASE("mutable_and_final_static_initialization") { original("mutable_and_final_static_initialization", "class Holder:\n\tstatic var value := 1\n\tfinal static var once: int\n\tstatic func _static_init():\n\t\tonce = 2\nfunc test():\n\tHolder.value = 3\n", "BS_TEST_OK", {}); }
	TEST_CASE("typed_mutation_default_and_untyped_neighbors") { original("typed_mutation_default_and_untyped_neighbors", "func test():\n\tvar values: Array[int] = [1]\n\tvalues.append(2)\n\tvalues.push_back(3)\n\tvalues.push_front(0)\n\tvalues.fill(1)\n\tvalues.insert(0, 2)\n\tvalues.set(0, 2)\n\tvalues.assign([1])\n\tvalues.append_array([2])\n\tvar scores: Dictionary[String, int] = {}\n\tscores.set(\"one\", 1)\n\tscores.assign({\"one\": 2})\n\tscores.merge({\"two\": 2})\n\tscores.merge({\"three\": 3}, true)\n\tvar gradual: Array = []\n\tgradual.append(\"text\")\n\tvar mapping: Dictionary = {}\n\tmapping.set(1, \"text\")\n", "BS_TEST_OK", {}); }
	TEST_CASE("typed_mutation_arity_and_overwrite_errors") { original("typed_mutation_arity_and_overwrite_errors", "func test(values: Array[int], scores: Dictionary[String, int]):\n\tvalues.append()\n\tvalues.append(1, 2)\n\tscores.merge()\n\tscores.merge({}, true, false)\n\tscores.merge({}, \"bad\")\n", ">> ERROR at line 2: Too few arguments for \"append()\" call. Expected at least 1 but received 0.\n>> ERROR at line 3: Too many arguments for \"append()\" call. Expected at most 1 but received 2.\n>> ERROR at line 4: Too few arguments for \"merge()\" call. Expected at least 1 but received 0.\n>> ERROR at line 5: Too many arguments for \"merge()\" call. Expected at most 2 but received 3.\n>> ERROR at line 6: Invalid argument for \"merge()\" function: argument 2 should be \"bool\" but is \"String\".", { { 2, 5 }, { 3, 22 }, { 4, 5 }, { 5, 28 }, { 6, 22 } }); }
	TEST_CASE("bad_constant_array_local") { original("bad_constant_array_local", "func test():\n\tconst value: Array[int] = [\"bad\"]\n", ">> ERROR at line 2: Cannot include a value of type \"String\" as \"int\".\n>> ERROR at line 2: Cannot have an element of type \"String\" in an array of type \"Array[int]\".", { { 2, 32 }, { 2, 32 } }); }
	TEST_CASE("bad_constant_key_local") { original("bad_constant_key_local", "func test():\n\tconst value: Dictionary[int, int] = {\"bad\": 1}\n", ">> ERROR at line 2: Cannot include a value of type \"String\" as \"int\".\n>> ERROR at line 2: Cannot have a key of type \"String\" in a dictionary of type \"Dictionary[int, int]\".", { { 2, 42 }, { 2, 42 } }); }
	TEST_CASE("bad_constant_value_local") { original("bad_constant_value_local", "func test():\n\tconst value: Dictionary[String, int] = {\"key\": \"bad\"}\n", ">> ERROR at line 2: Cannot include a value of type \"String\" as \"int\".\n>> ERROR at line 2: Cannot have a value of type \"String\" in a dictionary of type \"Dictionary[String, int]\".", { { 2, 52 }, { 2, 52 } }); }
	TEST_CASE("nonconstant_array_local") { original("nonconstant_array_local", "func source() -> int:\n\treturn 1\nfunc test():\n\tconst value: Array[int] = [source()]\n", ">> ERROR at line 4: Assigned value for constant \"value\" isn't a constant expression.", { { 4, 31 } }); }
	TEST_CASE("nonconstant_dictionary_local") { original("nonconstant_dictionary_local", "func source() -> int:\n\treturn 1\nfunc test():\n\tconst value: Dictionary[int, int] = {1: source()}\n", ">> ERROR at line 4: Assigned value for constant \"value\" isn't a constant expression.", { { 4, 41 } }); }
	TEST_CASE("bad_constant_array_member") { original("bad_constant_array_member", "const value: Array[int] = [\"bad\"]\n", ">> ERROR at line 1: Cannot include a value of type \"String\" as \"int\".\n>> ERROR at line 1: Cannot have an element of type \"String\" in an array of type \"Array[int]\".", { { 1, 28 }, { 1, 28 } }); }
	TEST_CASE("bad_constant_key_member") { original("bad_constant_key_member", "const value: Dictionary[int, int] = {\"bad\": 1}\n", ">> ERROR at line 1: Cannot include a value of type \"String\" as \"int\".\n>> ERROR at line 1: Cannot have a key of type \"String\" in a dictionary of type \"Dictionary[int, int]\".", { { 1, 38 }, { 1, 38 } }); }
	TEST_CASE("bad_constant_value_member") { original("bad_constant_value_member", "const value: Dictionary[String, int] = {\"key\": \"bad\"}\n", ">> ERROR at line 1: Cannot include a value of type \"String\" as \"int\".\n>> ERROR at line 1: Cannot have a value of type \"String\" in a dictionary of type \"Dictionary[String, int]\".", { { 1, 48 }, { 1, 48 } }); }
	TEST_CASE("nonconstant_array_member") { original("nonconstant_array_member", "func source() -> int:\n\treturn 1\nconst value: Array[int] = [source()]\n", ">> ERROR at line 3: Assigned value for constant \"value\" isn't a constant expression.", { { 3, 27 } }); }
	TEST_CASE("nonconstant_dictionary_member") { original("nonconstant_dictionary_member", "func source() -> int:\n\treturn 1\nconst value: Dictionary[int, int] = {1: source()}\n", ">> ERROR at line 3: Assigned value for constant \"value\" isn't a constant expression.", { { 3, 37 } }); }

	TEST_CASE("tuple_result_provenance_and_arity_recovery") {
		StorageFixture storage;
		BSConformanceRegistry::ScopedCorpusState conformances;
		TypeProfile profile;
		for (bool wrong_arity : { false, true }) {
			BSParser parser;
			const String source = String("class Cell:\n\ttuple Pair(index: int, owner: Self)\n\tfunc test(other: Cell):\n\t\tvar own := self.Pair(1, self)\n\t\tvar foreign := other.Pair(") + (wrong_arity ? "" : "1, other") + ")\n";
			BS_TEST_REQUIRE(parser.parse(source, storage.path("tuple_provenance.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() != OK) == wrong_arity);
			auto *cell = parser.get_tree()->get_member("Cell").m_class;
			BS_TEST_REQUIRE(cell);
			auto *body = cell->get_member("test").function->body;
			BS_TEST_REQUIRE(body && body->statements.size() == 2);
			auto *own = static_cast<BSParser::VariableNode *>(body->statements[0]);
			auto *foreign = static_cast<BSParser::VariableNode *>(body->statements[1]);
			BS_TEST_REQUIRE(own->initializer && foreign->initializer);
			const auto a = own->initializer->get_datatype(), b = foreign->initializer->get_datatype();
			CHECK(a.kind == BSParser::DataType::TUPLE);
			CHECK(b.kind == BSParser::DataType::TUPLE);
			CHECK(a.native_type == b.native_type);
			CHECK(a.script_path == b.script_path);
			CHECK(a.is_read_only);
			CHECK(b.is_read_only);
			CHECK_FALSE(a.is_meta_type);
			CHECK_FALSE(b.is_meta_type);
			BS_TEST_REQUIRE(a.container_element_types.size() == 2 && b.container_element_types.size() == 2);
			CHECK(a.container_element_types[1].kind == BSParser::DataType::TYPE_PARAMETER);
			CHECK(b.container_element_types[1].kind == BSParser::DataType::CLASS);
			CHECK(b.container_element_types[1].class_type == cell);
			CHECK_FALSE(b.container_element_types[1].is_substituted_self);
			CHECK_FALSE(foreign->get_datatype().is_constant);
			CHECK_FALSE(foreign->get_datatype().is_read_only);
			if (wrong_arity) {
				BS_TEST_REQUIRE(parser.get_errors().size() == 1);
				const auto &e = parser.get_errors().front()->get();
				CHECK(e.message == "Tuple \"Pair\" expects 2 argument(s), but 0 were given.");
				CHECK(e.line == foreign->initializer->start_line);
				CHECK(e.column == foreign->initializer->start_column);
			} else
				CHECK(parser.get_errors().is_empty());
		}
	}
	TEST_CASE("structural_tuple_compatibility_retains_nominal_and_profile_boundaries") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("tuple Pair(x: int, y: int)\ntuple Other(x: int, y: int)\nvar named: Pair\nvar other: Other\nvar plain: (int, int)\n", storage.path("tuple_boundaries.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		auto named = parser.get_tree()->get_member("named").get_datatype(), other = parser.get_tree()->get_member("other").get_datatype(), plain = parser.get_tree()->get_member("plain").get_datatype();
		BSTypeCompatibility::Options options;
		options.allow_implicit_conversion = false;
		CHECK(BSTypeCompatibility::check(plain, named, options).compatible);
		CHECK_FALSE(BSTypeCompatibility::check(named, plain, options).compatible);
		CHECK_FALSE(BSTypeCompatibility::check(named, other, options).compatible);
		auto meta = named;
		meta.is_meta_type = true;
		CHECK_FALSE(BSTypeCompatibility::check(plain, meta, options).compatible);
		auto wrong = plain;
		wrong.container_element_types.resize(1);
		CHECK_FALSE(BSTypeCompatibility::check(plain, wrong, options).compatible);
		wrong = plain;
		wrong.container_element_types.write[0].builtin_type = Variant::FLOAT;
		CHECK_FALSE(BSTypeCompatibility::check(plain, wrong, options).compatible);
		auto array = plain;
		array.kind = BSParser::DataType::BUILTIN;
		array.builtin_type = Variant::ARRAY;
		CHECK_FALSE(BSTypeCompatibility::check(array, named, options).compatible);
		CHECK_FALSE(BSTypeCompatibility::check(plain, array, options).compatible);
		auto nullable = named;
		nullable.is_nullable = true;
		options.strict_null = true;
		CHECK_FALSE(BSTypeCompatibility::check(plain, nullable, options).compatible);
		options.strict_null = false;
		CHECK(BSTypeCompatibility::check(plain, nullable, options).compatible);
		auto gradual = named;
		gradual.container_element_types.write[0].kind = BSParser::DataType::VARIANT;
		auto result = BSTypeCompatibility::check(plain, gradual, options);
		CHECK(result.compatible);
		CHECK(result.requires_runtime_check);
		options.strict_dynamic = true;
		CHECK_FALSE(BSTypeCompatibility::check(plain, gradual, options).compatible);
	}
	TEST_CASE("mutable_slots_preserve_initializers_and_native_getter_metadata") {
		StorageFixture storage;
		TypeProfile profile;
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("extends PhysicsDirectBodyState3DExtension\nsignal ping\nvar count = 1\nvar held = ping\nvar copy = center_of_mass\nfunc test(tree: SceneTree):\n\tvar local = center_of_mass\n\tvar window = tree.root\n\tlocal.x = 1.0\n", storage.path("slot_metadata.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		for (const char *name : { "count", "held", "copy" }) {
			auto *v = parser.get_tree()->get_member(name).variable;
			BS_TEST_REQUIRE(v && v->initializer);
			CHECK_FALSE(v->get_datatype().is_constant);
			CHECK_FALSE(v->get_datatype().is_read_only);
		}
		CHECK(parser.get_tree()->get_member("count").variable->initializer->is_constant);
		CHECK(parser.get_tree()->get_member("held").variable->initializer->get_datatype().is_constant);
		CHECK(parser.get_tree()->get_member("copy").variable->initializer->get_datatype().is_read_only);
		CHECK(parser.get_tree()->get_member("copy").variable->get_datatype().builtin_type == Variant::VECTOR3);
		auto *body = parser.get_tree()->get_member("test").function->body;
		BS_TEST_REQUIRE(body && body->statements.size() == 3);
		auto *local = static_cast<BSParser::VariableNode *>(body->statements[0]);
		auto *window = static_cast<BSParser::VariableNode *>(body->statements[1]);
		CHECK(local->initializer->get_datatype().is_read_only);
		CHECK_FALSE(local->get_datatype().is_read_only);
		CHECK(window->initializer->get_datatype().is_read_only);
		CHECK(window->get_datatype().native_type == StringName("Window"));
		CHECK_FALSE(window->get_datatype().is_read_only);
	}
	TEST_CASE("typed_literal_failure_never_publishes_a_materialized_carrier") {
		StorageFixture storage;
		TypeProfile profile;
		for (bool valid : { false, true }) {
			BSParser parser;
			const String source = String("const values: Array[int] = [") + (valid ? "1" : "\"bad\"") + "]\n";
			BS_TEST_REQUIRE(parser.parse(source, storage.path("literal_state.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == valid);
			auto *constant = parser.get_tree()->get_member("values").constant;
			BS_TEST_REQUIRE(constant && constant->initializer);
			CHECK(BSAnalyzer::has_materialized_constant_value(constant->initializer) == valid);
			CHECK(constant->initializer->is_constant == valid);
			if (valid) {
				CHECK(constant->initializer->reduced_value.get_type() == Variant::ARRAY);
				Array values = constant->initializer->reduced_value;
				CHECK(values.is_read_only());
			} else {
				CHECK(constant->initializer->reduced_value.get_type() == Variant::NIL);
				CHECK_FALSE(constant->initializer->get_datatype().is_constant);
				CHECK(parser.get_errors().size() == 2);
			}
			const int errors = parser.get_errors().size();
			CHECK((analyzer.analyze() == OK) == valid);
			CHECK(parser.get_errors().size() == errors);
		}
	}
	TEST_CASE("typed_mutation_parameters_returns_and_warning_metadata_survive") {
		StorageFixture storage;
		TypeProfile profile;
		profile.set(BSWarning::get_setting_path_from_code(BSWarning::RETURN_VALUE_DISCARDED), BSWarning::WARN);
		BSParser::update_project_settings();
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("func test(values: Array[int], scores: Dictionary[String, int]):\n\tvalues.insert(0, 1)\n\tscores.set(\"key\", 1)\n\tscores.merge({}, true)\n", storage.path("mutation_metadata.barista"), false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		auto *body = parser.get_tree()->get_member("test").function->body;
		BS_TEST_REQUIRE(body && body->statements.size() == 3);
		for (int i = 0; i < 3; ++i) {
			BS_TEST_REQUIRE(body->statements[i]->type == BSParser::Node::CALL);
			auto *call = static_cast<BSParser::CallNode *>(body->statements[i]);
			BS_TEST_REQUIRE(call->resolved_parameter_types.size() == 2);
			if (i == 0) {
				CHECK(call->resolved_parameter_types[0].builtin_type == Variant::INT);
				CHECK(call->resolved_parameter_types[1].builtin_type == Variant::INT);
				CHECK(call->get_datatype().builtin_type == Variant::INT);
			}
			if (i == 1) {
				CHECK(call->resolved_parameter_types[0].builtin_type == Variant::STRING);
				CHECK(call->resolved_parameter_types[1].builtin_type == Variant::INT);
				CHECK(call->get_datatype().builtin_type == Variant::BOOL);
			}
			if (i == 2) {
				CHECK(call->resolved_parameter_types[0].builtin_type == Variant::DICTIONARY);
				CHECK(call->resolved_parameter_types[0].container_element_types.size() == 2);
				CHECK(call->resolved_parameter_types[1].builtin_type == Variant::BOOL);
			}
		}
		BS_TEST_REQUIRE(parser.get_warnings().size() == 2);
		int line = 2;
		for (const auto &warning : parser.get_warnings()) {
			CHECK(warning.code == BSWarning::RETURN_VALUE_DISCARDED);
			CHECK(warning.start_line == line);
			CHECK(warning.start_column == 5);
			CHECK(warning.end_line == line);
			CHECK(warning.end_column == (line == 2 ? 24 : 25));
			++line;
		}
		const int count = parser.get_warnings().size();
		CHECK(analyzer.analyze() == OK);
		CHECK(parser.get_warnings().size() == count);
	}
	TEST_CASE("value_consumer_scopes_restore_ambient_state") {
		CHECK(verify_case_isolation([]() { original("isolated_mutable", "signal ping\nvar held = ping\nfunc test():\n\theld = ping\n", "BS_TEST_OK", {}); }));
	}
}
