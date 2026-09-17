/**************************************************************************/
/*  runtime_scene_test.cpp                                                */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "runtime_helpers.h"
#include "test_require.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/scene_tree.hpp>

using namespace barista_script;
using namespace barista_script::native_tests;

namespace {

SceneTree *running_tree() {
	return Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
}

} // namespace

TEST_SUITE("runtime_scene") {
	TEST_CASE("a script drives the node it is attached to") {
		SceneTree *tree = running_tree();
		BS_TEST_REQUIRE(tree != nullptr);

		const Ref<BaristaScript> script = compile_script(
				"extends Node\n"
				"\n"
				"var ready_ran: bool = false\n"
				"var ticks: int = 0\n"
				"\n"
				"func _ready() -> void:\n"
				"\tself.ready_ran = true\n"
				"\tself.name = \"DrivenByBarista\"\n"
				"\n"
				"func tick() -> int:\n"
				"\tself.ticks += 1\n"
				"\treturn self.ticks\n",
				"res://runtime/scene_node.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK(script->_get_instance_base_type() == StringName("Node"));
		BS_TEST_REQUIRE(script->_can_instantiate());

		// The host project's main scene is the tree's current scene; a child of it is inside the tree,
		// which is what makes the engine deliver the ready notification.
		Node *host = tree->get_current_scene();
		BS_TEST_REQUIRE(host != nullptr);

		RuntimeErrorScope errors;
		Node *node = memnew(Node);
		node->set_script(script);
		host->add_child(node);

		CHECK(node->get("ready_ran") == Variant(true));
		CHECK(node->get_name() == StringName("DrivenByBarista"));
		CHECK(node->call("tick") == Variant(1));
		CHECK(node->call("tick") == Variant(2));
		CHECK_MESSAGE(errors.errors().is_empty(), readable(errors.joined()));

		host->remove_child(node);
		memdelete(node);
	}

	TEST_CASE("a script cannot be attached to an object its base does not cover") {
		const Ref<BaristaScript> script = compile_script(
				"extends Node\n"
				"\n"
				"func run() -> int:\n"
				"\treturn 1\n",
				"res://runtime/scene_base.barista");
		BS_TEST_REQUIRE(script.is_valid());
		BS_TEST_REQUIRE(script->_can_instantiate());

		Ref<RefCounted> wrong_owner;
		wrong_owner.instantiate();
		CHECK(script->_instance_create(wrong_owner.ptr()) == nullptr);
	}
}
