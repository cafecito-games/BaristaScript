/**************************************************************************/
/*  global_class_test.cpp                                                 */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_global_class.h"
#include "storage_fixture.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>

using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
constexpr const char *FIXTURES = "res://tests/global_class_fixtures";
constexpr const char *CLASS_CACHE = "res://.godot/global_script_class_cache.cfg";
struct Expected {
	const char *file;
	bool parsed;
	const char *name;
	const char *base;
	const char *icon;
	bool abstract;
	bool tool;
	const char *kind;
};
constexpr Expected EXPECTED[] = {
	{ "namespaced_weapon.barista", true, "app.combat.Weapon", "Node", "res://icon.svg", false, false, "class_name" },
	{ "flat_weapon.barista", true, "FlatWeapon", "Node", "", false, false, "class_name" },
	{ "implicit_base.barista", true, "ImplicitBase", "RefCounted", "", false, false, "class_name" },
	{ "abstract_weapon.barista", true, "AbstractWeapon", "Node", "", true, false, "class_name" },
	{ "tool_weapon.barista", true, "ToolWeapon", "Node", "", false, true, "class_name" },
	{ "damage_kind.barista", true, "app.combat.DamageKind", "", "", true, false, "enum_name" },
	{ "grid_position.barista", true, "GridPosition", "", "", true, false, "tuple_name" },
	{ "damageable.barista", true, "Damageable", "", "", true, false, "trait_name" },
	{ "boxed.barista", true, "Boxed", "RefCounted", "", true, false, "generic class_name" },
	{ "plain_script.barista", true, "", "Node", "", false, false, "none" },
	{ "broken_head.barista", false, "", "", "", false, false, "none" },
	{ "broken_body.barista", true, "BrokenBody", "Node", "", false, false, "class_name" },
	{ "enum_with_extends.barista", false, "", "", "", false, false, "none" },
	{ "invalid_namespace.barista", false, "", "", "", false, false, "none" },
	{ "path_base.barista", true, "PathBase", "Node2D", "", false, false, "class_name" },
	{ "path_derived.barista", true, "PathDerived", "Node2D", "", false, false, "class_name" },
	{ "relative_derived.barista", true, "RelativeDerived", "Node2D", "", false, false, "class_name" },
	{ "chain_tail.barista", true, "ChainTail", "RefCounted", "", false, false, "class_name" },
	{ "chain_middle.barista", true, "ChainMiddle", "RefCounted", "", false, false, "class_name" },
	{ "inner_derived.barista", true, "InnerDerived", "Node2D", "", false, false, "class_name" },
	{ "chain_head.barista", true, "ChainHead", "Node2D", "", false, false, "class_name" },
	{ "generic_node.barista", true, "GenericNode", "Node", "", true, false, "generic class_name" },
	{ "specialized.barista", true, "Specialized", "Node", "", false, false, "class_name" },
	{ "cycle_a.barista", true, "CycleA", "", "", false, false, "class_name" },
	{ "cycle_b.barista", true, "CycleB", "", "", false, false, "class_name" },
};
struct Witness {
	const char *kind;
	const char *file;
};
constexpr Witness WITNESSES[] = {
	{ "none", "plain_script.barista" },
	{ "class_name", "flat_weapon.barista" },
	{ "generic class_name", "boxed.barista" },
	{ "trait_name", "damageable.barista" },
	{ "enum_name", "damage_kind.barista" },
	{ "tuple_name", "grid_position.barista" },
};
struct Instantiable {
	const char *name;
	bool value;
};
constexpr Instantiable INSTANTIABLE[] = {
	{ "app.combat.Weapon", true },
	{ "FlatWeapon", true },
	{ "ImplicitBase", true },
	{ "PathDerived", true },
	{ "RelativeDerived", true },
	{ "CycleA", false },
	{ "ChainHead", true },
	{ "InnerDerived", true },
	{ "BrokenBody", false },
	{ "Specialized", true },
	{ "GenericNode", false },
	{ "AbstractWeapon", false },
	{ "app.combat.DamageKind", false },
	{ "GridPosition", false },
	{ "Damageable", false },
	{ "Boxed", false },
};
String fixture_path(const char *file) { return String(FIXTURES).path_join(file); }
void same_resolution(const BSGlobalClass &left, const BSGlobalClass &right) {
	CHECK(left.to_dictionary() == right.to_dictionary());
	CHECK(left.declarations_parsed == right.declarations_parsed);
	CHECK(left.kind == right.kind);
}
} // namespace

TEST_SUITE("global_class") {
	static void scenario_fixture_coverage() {
		HashSet<String> present;
		for (const String &file : DirAccess::get_files_at(FIXTURES)) {
			if (file.ends_with(".barista")) {
				present.insert(file);
			}
		}
		HashSet<String> expected;
		for (const auto &row : EXPECTED) {
			CHECK_FALSE(expected.has(row.file));
			expected.insert(row.file);
			CHECK(present.has(row.file));
		}
		CHECK(present.size() == expected.size());
		for (const String &file : present) {
			CHECK(expected.has(file));
		}
	}
	TEST_CASE("fixture_coverage") { scenario_fixture_coverage(); }
	static void scenario_resolutions() {
		StorageFixture isolation;
		for (const auto &row : EXPECTED) {
			INFO(row.file);
			const String path = fixture_path(row.file);
			const BSGlobalClass resolved = bs_resolve_global_class(path);
			CHECK(resolved.declarations_parsed == row.parsed);
			CHECK(resolved.name == row.name);
			CHECK(resolved.base_type == row.base);
			CHECK(resolved.icon_path == row.icon);
			CHECK(resolved.is_abstract == row.abstract);
			CHECK(resolved.is_tool == row.tool);
			CHECK(bs_declaration_kind_name(resolved.kind) == row.kind);
			same_resolution(bs_resolve_global_class(path), resolved);
			const String source = FileAccess::get_file_as_string(path);
			same_resolution(bs_resolve_global_class_from_source(source, path), resolved);
			if (!bs_declaration_kind_declares_a_script(resolved.kind)) {
				CHECK(resolved.base_type.is_empty());
			}
			if (!bs_declaration_kind_is_instantiable(resolved.kind)) {
				CHECK(resolved.is_abstract);
			}
			if (bs_source_parses(source, path)) {
				CHECK(resolved.declarations_parsed);
			}
			CHECK_FALSE(resolved.name.begins_with("."));
			CHECK_FALSE(resolved.name.ends_with("."));
			CHECK_FALSE(resolved.name.contains(".."));
		}
	}
	TEST_CASE("resolutions") { scenario_resolutions(); }
	static void scenario_language_agrees() {
		StorageFixture isolation;
		const auto *language = BaristaScriptLanguage::get_singleton();
		CHECK(language != nullptr);
		if (!language) {
			return;
		}
		for (const auto &row : EXPECTED) {
			INFO(row.file);
			const String path = fixture_path(row.file);
			const Dictionary reported = language->_get_global_class_name(path);
			CHECK(reported == bs_resolve_global_class(path).to_dictionary());
			CHECK(reported.size() == 5);
			for (const char *key : { "base_type", "icon_path", "is_abstract", "is_tool", "name" }) {
				CHECK(reported.has(key));
			}
		}
	}
	TEST_CASE("language_agrees") { scenario_language_agrees(); }
	static void scenario_vocabulary_closure() {
		StorageFixture isolation;
		HashSet<String> seen;
		const int count = int(BSDeclarationKind::MAX);
		CHECK(count > 0);
		for (int i = 0; i < count; ++i) {
			const auto kind = static_cast<BSDeclarationKind>(i);
			const String name = bs_declaration_kind_name(kind);
			CHECK_FALSE(name.is_empty());
			CHECK_FALSE(seen.has(name));
			seen.insert(name);
			const bool instantiable = bs_declaration_kind_is_instantiable(kind);
			const bool script = bs_declaration_kind_declares_a_script(kind);
			if (instantiable) {
				CHECK((name == "none" || name == "class_name"));
				CHECK(script);
			}
			if (script) {
				CHECK((name == "none" || name == "class_name" || name == "generic class_name"));
			}
			bool witnessed = false;
			for (const auto &witness : WITNESSES) {
				if (name == witness.kind) {
					witnessed = true;
					CHECK(bs_declaration_kind_name(bs_resolve_global_class(fixture_path(witness.file)).kind) == name);
				}
			}
			CHECK(witnessed);
		}
		for (const auto &witness : WITNESSES) {
			CHECK(seen.has(witness.kind));
		}
		// The removed adapter's numeric range predicate is not a production API. Exercise the
		// production name consumer at both invalid boundaries as well as every actual kind.
		CHECK(bs_declaration_kind_name(BSDeclarationKind::MAX).is_empty());
		CHECK(bs_declaration_kind_name(static_cast<BSDeclarationKind>(-1)).is_empty());
	}
	TEST_CASE("vocabulary_closure") { scenario_vocabulary_closure(); }
	static void scenario_source_validity() {
		StorageFixture isolation;
		struct Row {
			const char *file;
			bool parses;
		};
		const Row rows[] = { { "flat_weapon.barista", true }, { "namespaced_weapon.barista", true },
			{ "damage_kind.barista", true }, { "plain_script.barista", true }, { "broken_body.barista", false },
			{ "broken_head.barista", false }, { "invalid_namespace.barista", false }, { "enum_with_extends.barista", false } };
		for (const auto &row : rows) {
			const String path = fixture_path(row.file);
			CHECK(bs_source_parses(FileAccess::get_file_as_string(path), path) == row.parses);
		}
	}
	TEST_CASE("source_validity") { scenario_source_validity(); }
	static void scenario_qualified_name_builder() {
		struct Row {
			const char *ns;
			const char *name;
			const char *expected;
		};
		const Row rows[] = { { "app.combat", "Weapon", "app.combat.Weapon" }, { "", "Weapon", "Weapon" },
			{ "app", "Weapon", "app.Weapon" }, { "app.combat", "", "" }, { "", "", "" } };
		for (const auto &row : rows) {
			CHECK(bs_build_qualified_global_name(row.ns, StringName(row.name)) == row.expected);
		}
	}
	TEST_CASE("qualified_name_builder") { scenario_qualified_name_builder(); }
	static void scenario_handled_type() {
		StorageFixture isolation;
		const auto *language = BaristaScriptLanguage::get_singleton();
		CHECK(language != nullptr);
		if (!language) {
			return;
		}
		CHECK(language->_handles_global_class_type("BaristaScript"));
		for (const char *type : { "GDScript", "Script", "", "baristascript" }) {
			CHECK_FALSE(language->_handles_global_class_type(type));
		}
		const Ref<Resource> loaded = ResourceLoader::get_singleton()->load(fixture_path("flat_weapon.barista"));
		CHECK(loaded.is_valid());
		if (loaded.is_valid()) {
			CHECK(language->_handles_global_class_type(loaded->get_class()));
		}
	}
	TEST_CASE("handled_type") { scenario_handled_type(); }
	static void scenario_script_surface() {
		StorageFixture isolation;
		struct Row {
			const char *file;
			const char *name;
			bool abstract;
		};
		const Row rows[] = { { "namespaced_weapon.barista", "app.combat.Weapon", false },
			{ "flat_weapon.barista", "FlatWeapon", false }, { "damage_kind.barista", "app.combat.DamageKind", true },
			{ "damageable.barista", "Damageable", true }, { "grid_position.barista", "GridPosition", true },
			{ "boxed.barista", "Boxed", true }, { "abstract_weapon.barista", "AbstractWeapon", true },
			{ "plain_script.barista", "", false }, { "broken_head.barista", "", false } };
		for (const auto &row : rows) {
			const Ref<Script> script = ResourceLoader::get_singleton()->load(fixture_path(row.file));
			CHECK(script.is_valid());
			if (script.is_null()) {
				continue;
			}
			CHECK(String(script->get_global_name()) == row.name);
			CHECK(script->is_abstract() == row.abstract);
			CHECK_FALSE(script->can_instantiate());
		}
	}
	TEST_CASE("script_surface") { scenario_script_surface(); }
	static void scenario_registry() {
		StorageFixture isolation;
		CHECK(FileAccess::file_exists(CLASS_CACHE));
		if (!FileAccess::file_exists(CLASS_CACHE)) {
			return;
		}
		for (const auto &row : INSTANTIABLE) {
			CHECK(ClassDBSingleton::get_singleton()->can_instantiate(row.name) == row.value);
		}
	}
	TEST_CASE("registry") { scenario_registry(); }
	static void scenario_class_cache() {
		CHECK(FileAccess::file_exists(CLASS_CACHE));
		if (!FileAccess::file_exists(CLASS_CACHE)) {
			return;
		}
		Ref<ConfigFile> config;
		config.instantiate();
		const Error loaded = config->load(CLASS_CACHE);
		CHECK(loaded == OK);
		if (loaded != OK) {
			return;
		}
		const Array entries = config->get_value("", "list", Array());
		Dictionary by_name;
		for (int i = 0; i < entries.size(); ++i) {
			const Dictionary entry = entries[i];
			by_name[String(entry.get("class", ""))] = entry;
		}
		for (const auto &row : EXPECTED) {
			if (String(row.name).is_empty()) {
				for (int i = 0; i < entries.size(); ++i) {
					const Dictionary entry = entries[i];
					CHECK(String(entry.get("path", "")) != fixture_path(row.file));
				}
				continue;
			}
			CHECK(by_name.has(row.name));
			if (!by_name.has(row.name)) {
				continue;
			}
			const Dictionary entry = by_name[row.name];
			CHECK(String(entry.get("base", "")) == row.base);
			CHECK(bool(entry.get("is_abstract", false)) == row.abstract);
			CHECK(bool(entry.get("is_tool", false)) == row.tool);
			CHECK(String(entry.get("language", "")) == "BaristaScript");
			CHECK(String(entry.get("path", "")) == fixture_path(row.file));
		}
	}
	TEST_CASE("class_cache") { scenario_class_cache(); }
	TEST_CASE("repeated_and_reversed_cases_restore_ambient_state") {
		void (*scenarios[])() = {
			scenario_fixture_coverage,
			scenario_resolutions,
			scenario_language_agrees,
			scenario_vocabulary_closure,
			scenario_source_validity,
			scenario_qualified_name_builder,
			scenario_handled_type,
			scenario_script_surface,
			scenario_registry,
			scenario_class_cache,
		};
		const int count = int(sizeof(scenarios) / sizeof(scenarios[0]));
		for (int pass = 0; pass < 2; ++pass) {
			for (int i = 0; i < count; ++i) {
				CHECK(verify_case_isolation(scenarios[i]));
			}
			for (int i = count - 1; i >= 0; --i) {
				CHECK(verify_case_isolation(scenarios[i]));
			}
		}
	}
} // TEST_SUITE
