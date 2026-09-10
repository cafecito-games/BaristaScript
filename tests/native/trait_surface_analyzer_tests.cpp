/**************************************************************************/
/*  trait_surface_analyzer_tests.cpp                                      */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_analyzer_probe.h"
#include "bs_conformance_registry.h"
#include "bs_global_class.h"
#include "bs_platform.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <godot_cpp/classes/project_settings.hpp>
#include <string>
using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;
namespace {
void diagnostics(const BSParser &parser) {
	for (const auto &error : parser.get_errors()) {
		MESSAGE(std::string(error.message.utf8().get_data()), " at ", error.line, ":", error.column, "-", error.end_line, ":", error.end_column);
	}
}
void error_at(const BSParser &parser, int index, const String &message, const BSParser::Node *site) {
	BS_TEST_REQUIRE(index >= 0 && index < parser.get_errors().size() && site != nullptr);
	auto *entry = parser.get_errors().front();
	for (int i = 0; i < index; i++)
		entry = entry->next();
	const auto &error = entry->get();
	CHECK(std::string(error.message.utf8().get_data()) == std::string(message.utf8().get_data()));
	CHECK(error.line == site->start_line);
	CHECK(error.column == site->start_column);
	CHECK(error.end_line == site->end_line);
	CHECK(error.end_column == site->end_column);
}
void exact_error(const BSParser &parser, const String &message, const BSParser::Node *site) {
	BS_TEST_REQUIRE(parser.get_errors().size() == 1);
	error_at(parser, 0, message, site);
}
void no_warnings(const BSParser &parser) {
	for (const auto &warning : parser.get_warnings()) {
		MESSAGE(std::string(warning.get_name().utf8().get_data()), ": ", std::string(warning.get_message().utf8().get_data()), " at ", warning.start_line, ":", warning.start_column, "-", warning.end_line, ":", warning.end_column);
	}
	CHECK(parser.get_warnings().is_empty());
}
Dictionary settings_snapshot() {
	Dictionary result;
	const TypedArray<Dictionary> properties = ProjectSettings::get_singleton()->get_property_list();
	for (int i = 0; i < properties.size(); i++) {
		const Dictionary item = properties[i];
		const String name = item["name"];
		if (ProjectSettings::get_singleton()->has_setting(name))
			result[name] = ProjectSettings::get_singleton()->get_setting(name);
	}
	return result;
}
struct SettingsRestore {
	Dictionary original = settings_snapshot();
	~SettingsRestore() {
		const Array current = settings_snapshot().keys();
		for (int i = 0; i < current.size(); i++)
			if (!original.has(current[i]))
				ProjectSettings::get_singleton()->set_setting(current[i], Variant());
		const Array keys = original.keys();
		for (int i = 0; i < keys.size(); i++)
			ProjectSettings::get_singleton()->set_setting(keys[i], original[keys[i]]);
	}
};
bool seed(StorageFixture &fixture, const String &path, const String &source) {
	BSParser parser;
	const Error parsed = parser.parse(source, path, false);
	if (parsed != OK) {
		diagnostics(parser);
		CHECK(parsed == OK);
		return false;
	}
	BSCache::set_source_override(path, source);
	const auto record = BSDeclarationIndex::record_from_global_class(path, source, bs_resolve_global_class_from_source(source, path));
	return fixture.index().commit_record(fixture.index().claim_refresh(path), record);
}
void public_agreement(const String &source, const String &path, bool valid) {
	Ref<BaristaScriptAnalyzerProbe> probe;
	probe.instantiate();
	CHECK(bool(probe->analyze_source(source, path).get("valid", !valid)) == valid);
	CHECK(bool(BaristaScriptLanguage::get_singleton()->_validate(source, path, true, true, true, true).get("valid", !valid)) == valid);
	Ref<BaristaScript> script;
	script.instantiate();
	script->_set_source_code(source);
	script->set_path(path);
	CHECK(script->_is_valid() == valid);
}
void pinned_case(const String &name, const String &source, const String &message, int site_kind) {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	BSParser parser;
	const Error parsed = parser.parse(source, "res://tests/trait_surface/" + name + ".barista", false);
	if (parsed != OK)
		diagnostics(parser);
	BS_TEST_REQUIRE(parsed == OK);
	BSAnalyzer analyzer(&parser);
	const Error result = analyzer.analyze();
	diagnostics(parser);
	if (message.is_empty()) {
		CHECK(result == OK);
		CHECK(parser.get_errors().is_empty());
		no_warnings(parser);
		return;
	}
	CHECK(result != OK);
	const BSParser::Node *site = nullptr;
	auto *head = parser.get_tree();
	if (site_kind == 0) {
		BS_TEST_REQUIRE(head->used_traits.size() == 2);
		BS_TEST_REQUIRE(!head->used_traits[1].name.is_empty());
		site = head->used_traits[1].name[0];
	} else if (site_kind == 1) {
		BS_TEST_REQUIRE(head->has_member("health"));
		site = head->get_member("health").get_source_node();
	} else {
		BS_TEST_REQUIRE(head->has_member("Positioned"));
		auto *trait = head->get_member("Positioned").m_class;
		BS_TEST_REQUIRE(trait != nullptr && trait->has_member("position"));
		site = trait->get_member("position").get_source_node();
	}
	exact_error(parser, message, site);
}
} //namespace
TEST_SUITE("trait_surface_analyzer") {
	TEST_CASE("pin_trait_method_conflict") {
		pinned_case("trait_method_conflict", R"SOURCE(extends RefCounted
uses TraitA, TraitB

trait TraitA:
	func collide(value: int) -> int:
		return value

trait TraitB:
	func collide(value: int) -> int:
		return value
)SOURCE",
				R"ERROR(Trait method "collide()" from "TraitA" conflicts with trait method "collide()" from "TraitB"; override it in "trait_method_conflict.barista" to disambiguate.)ERROR", 0);
	}
	TEST_CASE("pin_trait_state_conflict") {
		pinned_case("trait_state_conflict", R"SOURCE(extends RefCounted
uses TraitA, TraitB

trait TraitA:
	var health: int

trait TraitB:
	var health: int
)SOURCE",
				R"ERROR(Trait member "health" from "TraitA" conflicts with trait member "health" from "TraitB"; redeclare it in "trait_state_conflict.barista" with type "int" to disambiguate.)ERROR", 0);
	}
	TEST_CASE("pin_trait_state_conflict_bad_redeclare") {
		pinned_case("trait_state_conflict_bad_redeclare", R"SOURCE(extends RefCounted
uses TraitA

trait TraitA:
	var health: int

var health: String
)SOURCE",
				R"ERROR(Class "trait_state_conflict_bad_redeclare.barista" redeclares trait member "health" from "TraitA" with incompatible type. Expected "int", got "String".)ERROR", 1);
	}
	TEST_CASE("pin_trait_state_conflict_inferred_redeclare") {
		pinned_case("trait_state_conflict_inferred_redeclare", R"SOURCE(extends RefCounted
uses TraitA

trait TraitA:
	var health: int

var health = "not an int"
)SOURCE",
				R"ERROR(Class "trait_state_conflict_inferred_redeclare.barista" redeclares trait member "health" from "TraitA" with incompatible type. Expected "int", got "String".)ERROR", 1);
	}
	TEST_CASE("pin_trait_member_conflicts_with_native") {
		pinned_case("trait_member_conflicts_with_native", R"SOURCE(extends Node2D
uses Positioned

trait Positioned:
	var position := 123

func test() -> void:
	pass
)SOURCE",
				R"ERROR(Member "position" redefined (original in native class 'Node2D'))ERROR", 2);
	}
	TEST_CASE("pin_trait_conflict_resolution") {
		pinned_case("trait_conflict_resolution", R"SOURCE(extends RefCounted
uses TraitA, TraitB

trait TraitA:
	var health: int
	abstract func collide(value: int) -> int

trait TraitB:
	var health: int
	abstract func collide(value: int) -> int

var health: int

func collide(value: int) -> int:
	return value

func test() -> void:
	print(collide(1) + health)
)SOURCE",
				R"ERROR()ERROR", 0);
	}
	TEST_CASE("pin_trait_conflict_deferred_by_abstract") {
		pinned_case("trait_conflict_deferred_by_abstract", R"SOURCE(extends RefCounted

trait TraitA:
	func collide(value: int) -> int:
		return value

trait TraitB:
	func collide(value: int) -> int:
		return value

# Abstract classes and composing traits may defer disambiguation to a concrete
# subclass, so an unresolved trait method conflict is not an error for them.
abstract class AbstractUser:
	uses TraitA, TraitB

trait CompositeTrait uses TraitA, TraitB:
	pass

func test() -> void:
	print("ok")
)SOURCE",
				R"ERROR()ERROR", 0);
	}
	TEST_CASE("direct_and_transitive_base_constraints_apply_to_uses_and_extend") {
		for (bool retro : { false, true })
			for (bool transitive : { false, true })
				for (bool valid : { false, true }) {
					StorageFixture fixture;
					BSConformanceRegistry::ScopedCorpusState registry;
					const String trait = transitive ? "Composed" : "NeedsNode";
					const String declarations = "trait NeedsNode extends Node:\n\tpass\n" + (transitive ? String("trait Composed extends Node uses NeedsNode:\n\tpass\n") : String());
					const String target = "class Target extends " + String(valid ? "Node2D" : "RefCounted");
					const String source = declarations + (retro ? target + String(":\n\tpass\nextend Target uses ") + trait + ":\n\tpass\n" : target + String(" uses ") + trait + ":\n\tpass\n");
					BSParser parser;
					const Error parsed = parser.parse(source, "res://tests/trait_surface/constraint.barista", false);
					if (parsed != OK)
						diagnostics(parser);
					BS_TEST_REQUIRE(parsed == OK);
					BSAnalyzer analyzer(&parser);
					CHECK((analyzer.analyze() == OK) == valid);
					diagnostics(parser);
					if (valid) {
						CHECK(parser.get_errors().is_empty());
						no_warnings(parser);
						continue;
					}
					BS_TEST_REQUIRE(parser.get_tree()->has_member("Target"));
					const auto *target_node = parser.get_tree()->get_member("Target").m_class;
					BS_TEST_REQUIRE(target_node != nullptr);
					const BSParser::Node *site = nullptr;
					if (retro) {
						BS_TEST_REQUIRE(parser.get_tree()->conformances.size() == 1);
						site = parser.get_tree()->conformances[0];
					} else {
						BS_TEST_REQUIRE(target_node->used_traits.size() == 1 && !target_node->used_traits[0].name.is_empty());
						site = target_node->used_traits[0].name[0];
					}
					exact_error(parser, "Class \"Target\" cannot " + String(retro ? "conform to" : "use") + " trait \"" + trait + "\" because it does not inherit from \"Node\".", site);
				}
	}
	TEST_CASE("pinned_inherited_members_concrete_projection") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = "res://tests/trait_surface/inherited_trait_members_provider.notest.barista";
		const String provider = R"SOURCE(trait ExternalSurface:
	var external_value: int

	func external_fetch() -> int:
		return external_value


class ExternalBase uses ExternalSurface:
	pass
)SOURCE";
		BSParser admitted;
		const Error parsed = admitted.parse(provider, path, false);
		if (parsed != OK)
			diagnostics(admitted);
		BS_TEST_REQUIRE(parsed == OK);
		BSCache::set_source_override(path, provider);
		BSParser parser;
		const String source = R"SOURCE(const Provider = preload("inherited_trait_members_provider.notest.barista")


trait IntStorage:
	var value: int
	signal changed(item: int)
	var callback: Callable[[int], int]

	func store(item: int) -> void:
		value = item

	func fetch() -> int:
		return value

	func identity(item: int) -> int:
		return item


trait IntForward uses IntStorage:
	func forwarded() -> int:
		return fetch()


class IntBase uses IntForward:
	pass


class IntMiddle extends IntBase:
	pass


trait StringStorage:
	var value: String
	signal changed(item: String)
	var callback: Callable[[String], String]

	func store(item: String) -> void:
		value = item

	func fetch() -> String:
		return value

	func identity(item: String) -> String:
		return item


trait StringForward uses StringStorage:
	func forwarded() -> String:
		return fetch()


class StringBase uses StringForward:
	pass


class StringMiddle extends StringBase:
	pass


class IntChild extends IntMiddle:
	func via_bare(item: int) -> int:
		store(item)
		return forwarded()

	func via_self(item: int) -> int:
		self.value = item
		return self.fetch()


class StringChild extends StringMiddle:
	pass


class ConcreteBase uses IntStorage:
	pass


class ConcreteChild extends ConcreteBase:
	pass


trait DiamondRoot:
	func diamond() -> String:
		return "diamond"


trait DiamondLeft uses DiamondRoot:
	pass


trait DiamondRight uses DiamondRoot:
	pass


class DiamondBase uses DiamondLeft, DiamondRight:
	pass


class DiamondChild extends DiamondBase:
	pass


trait Shadowed:
	func label() -> String:
		return "trait"


class ShadowBase uses Shadowed:
	pass


class ShadowChild extends ShadowBase:
	func label() -> String:
		return "class"


class ExternalChild extends Provider.ExternalBase:
	pass


func plus_one(value: int) -> int:
	return value + 1


func test() -> void:
	var child := IntChild.new()
	print(child.via_bare(7))
	print(child.via_self(8))
	child.changed.emit(8)
	child.callback = plus_one
	print(child.callback.call(9))

	var strings := StringChild.new()
	strings.store("specialized")
	print(strings.fetch())
	var concrete := ConcreteChild.new()
	concrete.store(12)
	print(concrete.fetch())

	print(DiamondChild.new().diamond())
	print(ShadowChild.new().label())

	var external := ExternalChild.new()
	external.external_value = 11
	print(external.external_fetch())
	print("inherited trait members ok")
)SOURCE";
		const String typed_controls = "\nvar inferred_int = ConcreteChild.new().fetch()\nvar inferred_string = StringChild.new().fetch()\nvar inferred_external = ExternalChild.new().external_fetch()\nvar inherited_state = ConcreteChild.new().value\nvar inherited_signal = ConcreteChild.new().changed\nvar inherited_callable = ConcreteChild.new().callback\n";
		const Error consumer_parsed = parser.parse(source + typed_controls, "res://tests/trait_surface/inherited_trait_members.barista", false);
		if (consumer_parsed != OK)
			diagnostics(parser);
		BS_TEST_REQUIRE(consumer_parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		BS_TEST_REQUIRE(parser.get_warnings().size() == 1);
		const auto &warning = parser.get_warnings().front()->get();
		BS_TEST_REQUIRE(parser.get_tree()->has_member("StringStorage"));
		const auto *storage = parser.get_tree()->get_member("StringStorage").m_class;
		BS_TEST_REQUIRE(storage != nullptr && storage->has_member("changed"));
		const auto *signal = storage->get_member("changed").signal;
		BS_TEST_REQUIRE(signal != nullptr && signal->identifier != nullptr);
		CHECK(warning.code == BSWarning::UNUSED_SIGNAL);
		CHECK(warning.get_message() == "The signal \"changed\" is declared but never explicitly used in the class.");
		CHECK(warning.start_line == signal->identifier->start_line);
		CHECK(warning.start_column == signal->identifier->start_column);
		CHECK(warning.end_line == signal->identifier->end_line);
		CHECK(warning.end_column == signal->identifier->end_column);
		for (const String &name : { String("inferred_int"), String("inferred_string"), String("inferred_external"), String("inherited_state"), String("inherited_signal"), String("inherited_callable") }) {
			BS_TEST_REQUIRE(parser.get_tree()->has_member(name));
			const auto type = parser.get_tree()->get_member(name).get_datatype();
			CHECK(type.kind == BSParser::DataType::BUILTIN);
			const Variant::Type expected = name == "inferred_string" ? Variant::STRING : name == "inherited_signal" ? Variant::SIGNAL
					: name == "inherited_callable"																	? Variant::CALLABLE
																													: Variant::INT;
			CHECK(type.builtin_type == expected);
		}
	}
	TEST_CASE("default_overrides_require_compatible_parameters_and_returns") {
		for (int kind : { 0, 1, 2 }) {
			StorageFixture fixture;
			const String declaration = kind == 0 ? "func collide(value: int) -> int:\n\t\treturn value\n" : kind == 1 ? "func collide(value: String) -> int:\n\t\tprint(value)\n\t\treturn 0\n"
																													  : "func collide(value: int) -> String:\n\t\treturn str(value)\n";
			const String source = "trait Default:\n\tfunc collide(value: int) -> int:\n\t\treturn value\nclass Target uses Default:\n\t" + declaration;
			BSParser parser;
			const Error parsed = parser.parse(source, "res://tests/trait_surface/override.barista", false);
			if (parsed != OK)
				diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == (kind == 0));
			diagnostics(parser);
			if (kind == 0) {
				CHECK(parser.get_errors().is_empty());
				no_warnings(parser);
				continue;
			}
			BS_TEST_REQUIRE(parser.get_tree()->has_member("Target"));
			const auto *target = parser.get_tree()->get_member("Target").m_class;
			BS_TEST_REQUIRE(target != nullptr && target->has_function("collide"));
			exact_error(parser, "The function \"collide()\" signature does not match required trait method \"Default.collide()\".", target->get_member("collide").function);
		}
	}
	TEST_CASE("ordinary_ancestor_method_precedes_traits_for_calls_and_callable_values") {
		StorageFixture fixture;
		const String source = R"SOURCE(trait Default:
	func choose() -> int:
		return 0
class Ancestor:
	func choose(value: int = 0) -> int:
		return value
class Receiver extends Ancestor uses Default:
	func bare() -> int:
		return choose(1)
	func explicit() -> int:
		return self.choose(2)
	func captured() -> int:
		var callback = choose
		return callback.call(3)
func test(value: Receiver) -> void:
	var callback = value.choose
	print(value.choose(4), callback.call(5), Callable(value, "choose").call(6))
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/trait_surface/ordinary.barista", false);
		if (parsed != OK)
			diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		no_warnings(parser);
	}
	TEST_CASE("ordinary_non_function_claims_do_not_resurrect_native_methods") {
		for (const String &expression : { String("get_instance_id()"), String("self.get_instance_id()"), String("value.get_instance_id()") }) {
			StorageFixture fixture;
			const String source = "class_name Receiver extends RefCounted\nuses Empty\ntrait Empty:\n\tpass\nconst get_instance_id = 1\nfunc test(value: Receiver) -> void:\n\t" + expression + "\n";
			BSParser parser;
			const Error parsed = parser.parse(source, "res://tests/trait_surface/claim.barista", false);
			if (parsed != OK)
				diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_tree()->has_function("test"));
			const auto *body = parser.get_tree()->get_member("test").function->body;
			BS_TEST_REQUIRE(body != nullptr && body->statements.size() == 1);
			BS_TEST_REQUIRE(body->statements[0]->type == BSParser::Node::CALL);
			const auto *call = static_cast<const BSParser::CallNode *>(body->statements[0]);
			BS_TEST_REQUIRE(call->callee != nullptr);
			exact_error(parser, expression.begins_with("get_instance_id") ? "Cannot call \"get_instance_id\": it is not a function." : "Name \"get_instance_id\" called as a function but is a \"int\".", call->callee);
		}
	}
	TEST_CASE("abstract_receivers_expose_transitive_requirements") {
		StorageFixture fixture;
		const String source = R"SOURCE(trait Requirement:
	abstract func get_value(value: int) -> int
trait Composed uses Requirement:
	pass
abstract class Receiver uses Composed:
	pass
func test(value: Receiver, trait_value: Composed) -> int:
	var callback = value.get_value
	return value.get_value(1) + trait_value.get_value(2) + callback.call(3)
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/trait_surface/abstract.barista", false);
		if (parsed != OK)
			diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		no_warnings(parser);
	}
	TEST_CASE("lexical_outer_traits_are_not_inner_instance_members") {
		StorageFixture fixture;
		const String source = R"SOURCE(trait State:
	var secret: int
class Outer uses State:
	class Inner:
		func read() -> int:
			return secret
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/trait_surface/outer.barista", false);
		if (parsed != OK)
			diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		BS_TEST_REQUIRE(parser.get_tree()->has_member("Outer"));
		const auto *outer = parser.get_tree()->get_member("Outer").m_class;
		BS_TEST_REQUIRE(outer != nullptr && outer->has_member("Inner"));
		const auto *inner = outer->get_member("Inner").m_class;
		BS_TEST_REQUIRE(inner != nullptr && inner->has_function("read"));
		const auto *body = inner->get_member("read").function->body;
		BS_TEST_REQUIRE(body != nullptr && body->statements.size() == 1 && body->statements[0]->type == BSParser::Node::RETURN);
		const auto *result = static_cast<const BSParser::ReturnNode *>(body->statements[0]);
		BS_TEST_REQUIRE(result->return_value != nullptr);
		exact_error(parser, "Identifier \"secret\" not declared in the current scope.", result->return_value);
	}
	TEST_CASE("inherited_constants_enums_and_enum_values_keep_declaring_identity") {
		StorageFixture fixture;
		const String source = R"SOURCE(trait Surface:
	const COUNT = 7
	enum State:
		READY = 0
	enum:
		FLAG = 2
class Base uses Surface:
	pass
class Receiver extends Base:
	func read() -> int:
		return COUNT + FLAG + (State.READY as int)
var count = Receiver.new().COUNT
var flag = Receiver.new().FLAG
var state = Receiver.new().State.READY
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/trait_surface/enum_surface.barista", false);
		if (parsed != OK)
			diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		no_warnings(parser);
		const auto *head = parser.get_tree();
		BS_TEST_REQUIRE(head->has_member("Surface") && head->has_member("count") && head->has_member("flag") && head->has_member("state"));
		const auto *surface = head->get_member("Surface").m_class;
		BS_TEST_REQUIRE(surface != nullptr && surface->has_member("State"));
		CHECK(head->get_member("count").get_datatype().builtin_type == Variant::INT);
		CHECK(head->get_member("flag").get_datatype().builtin_type == Variant::INT);
		const auto state_type = head->get_member("state").get_datatype();
		CHECK(state_type.kind == BSParser::DataType::ENUM);
		CHECK(state_type.enum_type == surface->get_member("State").get_datatype().enum_type);
		CHECK(state_type.native_type == surface->get_member("State").get_datatype().native_type);
		CHECK(state_type.class_type == surface);
		CHECK(head->get_member("flag").variable->initializer->reduced_value == Variant(2));
	}
	TEST_CASE("nested_class_and_tuple_are_not_flattened") {
		for (const String &name : { String("Hidden"), String("Pair") }) {
			StorageFixture fixture;
			const String source = "trait Surface:\n\tclass Hidden:\n\t\tpass\n\ttuple Pair(first: int, second: int)\nclass Receiver uses Surface:\n\tpass\nvar selected = Receiver." + name + "\n";
			BSParser parser;
			const Error parsed = parser.parse(source, "res://tests/trait_surface/unflattened.barista", false);
			if (parsed != OK)
				diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_tree()->has_member("selected"));
			const auto *variable = parser.get_tree()->get_member("selected").variable;
			BS_TEST_REQUIRE(variable != nullptr && variable->initializer != nullptr && variable->initializer->type == BSParser::Node::SUBSCRIPT);
			const auto *attribute = static_cast<const BSParser::SubscriptNode *>(variable->initializer)->attribute;
			BS_TEST_REQUIRE(attribute != nullptr);
			exact_error(parser, "Cannot find member \"" + name + "\" in base \"Receiver\".", attribute);
		}
	}
	TEST_CASE("concrete_use_checks_deferred_composed_defaults_and_requirements") {
		for (bool abstract_requirement : { false, true }) {
			StorageFixture fixture;
			const String declarations = abstract_requirement ? "trait Left:\n\tabstract func value() -> int\ntrait Right:\n\tpass\n" : "trait Left:\n\tfunc value() -> int:\n\t\treturn 1\ntrait Right:\n\tfunc value() -> int:\n\t\treturn 2\n";
			const String source = declarations + String("trait Composed uses Left, Right:\n\tpass\nclass Receiver uses Composed:\n\tpass\n");
			BSParser parser;
			const Error parsed = parser.parse(source, "res://tests/trait_surface/deferred.barista", false);
			if (parsed != OK)
				diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_tree()->has_member("Receiver"));
			const auto *receiver = parser.get_tree()->get_member("Receiver").m_class;
			BS_TEST_REQUIRE(receiver != nullptr && receiver->used_traits.size() == 1 && !receiver->used_traits[0].name.is_empty());
			exact_error(parser, abstract_requirement ? "Class \"Receiver\" must implement trait method \"Left.value()\"." : "Trait method \"value()\" from \"Left\" conflicts with trait method \"value()\" from \"Right\"; override it in \"Receiver\" to disambiguate.", receiver->used_traits[0].name[0]);
		}
	}
	TEST_CASE("foreign_trait_signature_retains_its_generation_across_refresh") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = "res://tests/trait_surface/surface.barista";
		const String old_provider = "trait_name Surface\nfunc identity(value: int) -> int:\n\treturn value\n";
		const String new_provider = "trait_name Surface\nfunc identity(value: String) -> String:\n\treturn value\n";
		BS_TEST_REQUIRE(seed(fixture, path, old_provider));
		BSParser old_parser;
		BS_TEST_REQUIRE(old_parser.parse("uses Surface\nfunc test() -> int:\n\treturn identity(1)\n", "res://tests/trait_surface/old.barista", false) == OK);
		BSAnalyzer old_analyzer(&old_parser);
		CHECK(old_analyzer.resolve_interface() == OK);
		BS_TEST_REQUIRE(old_parser.get_tree()->resolved_traits.size() == 1);
		const auto *old_trait = old_parser.get_tree()->resolved_traits[0];
		BS_TEST_REQUIRE(old_trait != nullptr && old_trait->has_function("identity"));
		BSCache::set_source_override(path, new_provider);
		BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, new_provider) == OK);
		BSParser new_parser;
		BS_TEST_REQUIRE(new_parser.parse("uses Surface\nfunc test() -> String:\n\treturn identity(\"ok\")\n", "res://tests/trait_surface/new.barista", false) == OK);
		BSAnalyzer new_analyzer(&new_parser);
		CHECK(new_analyzer.analyze() == OK);
		diagnostics(new_parser);
		no_warnings(new_parser);
		CHECK(old_analyzer.analyze() == OK);
		diagnostics(old_parser);
		no_warnings(old_parser);
		BS_TEST_REQUIRE(new_parser.get_tree()->resolved_traits.size() == 1);
		const auto *new_trait = new_parser.get_tree()->resolved_traits[0];
		BS_TEST_REQUIRE(new_trait != nullptr && new_trait->has_function("identity"));
		CHECK(old_trait != new_trait);
		const auto *old_function = old_trait->get_member("identity").function;
		const auto *new_function = new_trait->get_member("identity").function;
		BS_TEST_REQUIRE(old_function != nullptr && new_function != nullptr && old_function->parameters.size() == 1 && new_function->parameters.size() == 1);
		CHECK(old_function->parameters[0]->get_datatype().builtin_type == Variant::INT);
		CHECK(new_function->parameters[0]->get_datatype().builtin_type == Variant::STRING);
		CHECK(old_function->get_datatype().builtin_type == Variant::INT);
		CHECK(new_function->get_datatype().builtin_type == Variant::STRING);
	}
	TEST_CASE("foreign_trait_failure_replays_then_explicit_repair_recovers") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = "res://tests/trait_surface/broken.barista";
		const String broken = "trait_name Broken\nfunc value() -> Missing:\n\tpass\n";
		BS_TEST_REQUIRE(seed(fixture, path, broken));
		const String source = "uses Broken\nfunc test():\n\tvalue()\n";
		for (int attempt : { 0, 1 }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, "res://tests/trait_surface/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_errors().size() == 2);
			const auto *head = parser.get_tree();
			BS_TEST_REQUIRE(head->used_traits.size() == 1 && !head->used_traits[0].name.is_empty() && head->has_function("test"));
			const auto *body = head->get_member("test").function->body;
			BS_TEST_REQUIRE(body != nullptr && body->statements.size() == 1 && body->statements[0]->type == BSParser::Node::CALL);
			const auto *call = static_cast<const BSParser::CallNode *>(body->statements[0]);
			// Preserve the existing cold resolution and cached-failure wrappers; both
			// blame the use site, and replay within one consumer is idempotent.
			error_at(parser, 0, attempt == 0 ? "Could not resolve trait \"Broken\"." : "Could not resolve trait \"Broken\": provider \"res://tests/trait_surface/broken.barista\" could not be parsed.", head->used_traits[0].name[0]);
			error_at(parser, 1, "Identifier \"value\" not declared in the current scope.", call->callee);
			const int errors = parser.get_errors().size();
			analyzer.resolve_interface();
			CHECK(parser.get_errors().size() == errors);
		}
		const String fixed = "trait_name Broken\nfunc value() -> int:\n\treturn 1\n";
		BSCache::set_source_override(path, fixed);
		BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, fixed) == OK);
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse(source, "res://tests/trait_surface/consumer.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		no_warnings(parser);
	}
	TEST_CASE("public_agreement_preserves_post_setup_state_and_restores_startup") {
		const Dictionary startup = settings_snapshot();
		{
			SettingsRestore restore;
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			const String provider_path = "res://tests/trait_surface/public_surface.barista";
			const String path = "res://tests/trait_surface/public_receiver.barista";
			BS_TEST_REQUIRE(seed(fixture, provider_path, "trait_name PublicSurface extends Node\nfunc value() -> int:\n\treturn 1\n"));
			const Dictionary post_setup = settings_snapshot();
			const String store = fixture.path("index.bin");
			BS_TEST_REQUIRE(fixture.index().flush(store) == OK);
			const auto original = read_bytes(store);
			const uint64_t revision = fixture.index().get_refresh_revision(provider_path);
			for (int state : { 0, 1, 2 }) {
				const String previous = state == 2 ? "class_name Unsaved\n" : "";
				if (state == 0)
					BSCache::clear_source_override(path);
				else
					BSCache::set_source_override(path, previous);
				for (bool valid : { false, true }) {
					const String source = "class_name PublicReceiver extends " + String(valid ? "Node2D" : "RefCounted") + "\nuses PublicSurface\nfunc test() -> int:\n\treturn value()\n";
					public_agreement(source, path, valid);
					CHECK(settings_snapshot() == post_setup);
					CHECK(BSCache::has_source_override(path) == (state != 0));
					if (state != 0)
						CHECK(BSCache::get_source_code(path) == previous);
					CHECK(fixture.index().get_refresh_revision(provider_path) == revision);
					CHECK(fixture.index().flush(store) == OK);
					CHECK(read_bytes(store) == original);
					CHECK(BSConformanceRegistry::get_singleton()->get_file_conformances(path).is_empty());
					CHECK(BSConformanceRegistry::get_singleton()->get_file_trait_bindings(path).is_empty());
				}
			}
		}
		CHECK(settings_snapshot() == startup);
	}
}
