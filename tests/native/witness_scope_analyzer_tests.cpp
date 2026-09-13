/**************************************************************************/
/*  witness_scope_analyzer_tests.cpp                                      */
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
namespace barista_script {
struct WitnessScopeTestAccess {
	static bool clear(const BSAnalyzer &a) { return a.witness_target_class == nullptr && a.witness_declaration_scope == nullptr; }
	static void nested_body(BSAnalyzer &a, BSParser::ClassNode *declaration, BSParser::ClassNode *outer) {
		auto *previous = a.current_class;
		{
			BSAnalyzer::ScopedWitnessScope witness(&a, outer, declaration);
			BSAnalyzer::ScopedCurrentClass receiver(&a, outer);
			a.resolve_conformance_bodies(declaration);
			CHECK(a.current_class == outer);
			CHECK(a.witness_target_class == outer);
			CHECK(a.witness_declaration_scope == declaration);
		}
		CHECK(a.current_class == previous);
	}
	static void helper_isolation(BSAnalyzer &a, BSParser::ClassNode *target, BSParser::ClassNode *declaration, BSParser::ClassNode *helper) {
		BSAnalyzer::ScopedWitnessScope witness(&a, target, declaration);
		{
			BSAnalyzer::ScopedCurrentClass receiver(&a, target);
			CHECK(a.find_witness_declaration_type("OnlyHere", declaration) == declaration);
		}
		BSAnalyzer::ScopedCurrentClass receiver(&a, helper);
		CHECK(a.find_witness_declaration_type("OnlyHere", declaration) == nullptr);
		List<BSParser::ClassNode *> scopes;
		HashSet<BSParser::ClassNode *> appended;
		a.get_effective_scope_classes(helper, &scopes, declaration, &appended);
		CHECK(appended.is_empty());
	}
	static bool reserved(BSAnalyzer &a, BSParser::ClassNode *target, BSParser::ClassNode *declaration, const StringName &name) {
		BSAnalyzer::ScopedWitnessScope witness(&a, target, declaration);
		BSAnalyzer::ScopedCurrentClass receiver(&a, target);
		return a.witness_target_scope_declares_name(name, declaration);
	}
};
} //namespace barista_script
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
	// Public analysis intentionally publishes temporary conformance records. The shared
	// corpus guard isolates that documented effect while index/settings/source stay read-only.
	BSConformanceRegistry::ScopedCorpusState registry;
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
} //namespace
TEST_SUITE("witness_scope_analyzer") {
	TEST_CASE("pin_foreign_witness_files_keep_distinct_helpers_in_both_orders") {
		for (bool reverse : { false, true }) {
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/fws_holder.barista", "class_name FwsHolder\nextends RefCounted\n\nvar power: int = 3\n"));
			for (int position : { 0, 1 }) {
				const bool second = reverse ? position == 0 : position == 1;
				const String method = second ? "second_helper" : "first_helper";
				const String marker = second ? "second_marker" : "first_marker";
				const String trait = second ? "FwsSecondTrait" : "FwsFirstTrait";
				const String path = second ? "res://tests/witness_scope/fws_second.barista" : "res://tests/witness_scope/fws_first.barista";
				const String source = "extend FwsHolder uses " + trait + ":\n\tfunc " + method + "() -> Helper:\n\t\treturn Helper.new()\n\n\ntrait " + trait + ":\n\tabstract func " + method + "() -> Helper\n\n\nclass Helper:\n\tvar " + marker + ": int = " + (second ? "2" : "1") + "\n";
				BSParser parser;
				const Error parsed = parser.parse(source, path, false);
				diagnostics(parser);
				BS_TEST_REQUIRE(parsed == OK);
				BSAnalyzer analyzer(&parser);
				CHECK(analyzer.analyze() == OK);
				diagnostics(parser);
				CHECK(parser.get_errors().is_empty());
				CHECK(WitnessScopeTestAccess::clear(analyzer));
				BS_TEST_REQUIRE(parser.get_tree()->conformances.size() == 1 && parser.get_tree()->conformances[0]->witnesses.size() == 1);
				const auto type = parser.get_tree()->conformances[0]->witnesses[0]->get_datatype();
				CHECK(type.kind == BSParser::DataType::CLASS);
				BS_TEST_REQUIRE(type.class_type != nullptr);
				CHECK(type.script_path == path);
				CHECK(type.class_type == parser.get_tree()->get_member("Helper").m_class);
				CHECK(type.class_type->has_member(marker));
			}
		}
	}
	TEST_CASE("same_native_and_builtin_targets_reach_lexical_helper_in_signature_and_body") {
		for (const String &target : { String("Target"), String("RefCounted"), String("int"), String("Dictionary") }) {
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			const String source = "class Target:\n\tpass\nclass Helper:\n\tvar marker: int\ntrait Making:\n\tabstract static func make() -> Helper\nextend " + target + " uses Making:\n\tstatic func make() -> Helper:\n\t\tvar value: Helper = Helper.new()\n\t\treturn value\n";
			BSParser parser;
			const Error parsed = parser.parse(source, "res://tests/witness_scope/targets.barista", false);
			diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			CHECK(parser.get_errors().is_empty());
			CHECK(WitnessScopeTestAccess::clear(analyzer));
			BS_TEST_REQUIRE(parser.get_tree()->conformances.size() == 1 && parser.get_tree()->conformances[0]->witnesses.size() == 1);
			const auto *witness = parser.get_tree()->conformances[0]->witnesses[0];
			CHECK(witness->get_datatype().class_type == parser.get_tree()->get_member("Helper").m_class);
		}
	}
	TEST_CASE("pin_retroactive_conformance_witness_declaring_scope_collision") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/rtc_scope_collide.notest.barista", R"SOURCE(# Companion foreign target file for the witness declaration-scope collision fixture. The root class
# declares a `Marker` type and the conformance target is the inner class, so the target's own lexical
# outer scope supplies a `Marker` that has to win over the conformance file's same-named type.
class_name RtcScopeCollide
extends RefCounted


class Marker:
	func tag() -> String:
		return "target-marker"


class Inner extends RefCounted:
	var seed: int = 1
)SOURCE"));
		const String source = R"SOURCE(# Target scope wins on collisions. The witness's unqualified `Marker` resolves to the type the foreign
# target's own outer class declares, not to this file's same-named class, while an uncollided
# declaring-file type stays reachable through the fallback.
extend RtcScopeCollide.Inner uses RtcScopeTagging:
	func tag() -> String:
		var marker: Marker = Marker.new()
		var local: OnlyHere = OnlyHere.new()
		return marker.tag() + "/" + local.tag() + "/" + str(seed)


trait RtcScopeTagging:
	abstract func tag() -> String


class Marker:
	func tag() -> String:
		return "conformance-marker"


class OnlyHere:
	func tag() -> String:
		return "only-here"


func test() -> void:
	var tagged: RtcScopeTagging = RtcScopeCollide.Inner.new()
	print(tagged.tag())
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/witness_scope/retroactive_conformance_witness_declaring_scope_collision.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(WitnessScopeTestAccess::clear(analyzer));
	}
	TEST_CASE("pin_retroactive_conformance_witness_declaring_scope_tuple_collision") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/rtc_scope_inherited.notest.barista", R"SOURCE(# Companion foreign target chain for the tuple-collision fixture. `label()` is declared on the base,
# so a witness for the derived target reaches it only through inheritance — the case a declaring-file
# tuple of the same name must not capture.
class_name RtcScopeInheritedBase
extends RefCounted

func label(prefix: String) -> String:
	return prefix + "-inherited"
)SOURCE"));
		BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/rtc_scope_stamping.notest.barista", R"SOURCE(# Companion trait applied by the tuple-collision fixture's target. Its concrete `stamp()` is flattened
# into the target's callable surface, so it too outranks a same-named declaring-file tuple.
trait_name RtcScopeStamping

func stamp() -> String:
	return "stamped"
)SOURCE"));
		BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/rtc_scope_inherited_leaf.notest.barista", R"SOURCE(# Derived foreign target for the tuple-collision fixture. It declares nothing of its own: `label()` is
# inherited and `stamp()` arrives through the applied trait.
class_name RtcScopeInheritedLeaf
extends RtcScopeInheritedBase

uses RtcScopeStamping
)SOURCE"));
		const String source = R"SOURCE(# Tuple construction is decided before ordinary call resolution, so the declaration-site fallback has
# to yield to the target's *whole* surface: `label` names an inherited method, `stamp` a method
# flattened in from an applied trait, and `get_class` a method of the native base. All three stay
# calls even though this file declares same-named tuples, while an uncollided declaring-file tuple is
# still constructible from the witness.
extend RtcScopeInheritedLeaf uses RtcScopeLabelling:
	func described() -> String:
		var span: Span = Span(1, 2)
		return label("a") + "/" + stamp() + "/" + get_class() + "/" + str(span.high)


trait RtcScopeLabelling:
	abstract func described() -> String


tuple label(first: int, second: int)
tuple stamp(first: int, second: int)
tuple get_class(first: int, second: int)
tuple Span(low: int, high: int)


func test() -> void:
	var described: RtcScopeLabelling = RtcScopeInheritedLeaf.new()
	print(described.described())
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/witness_scope/retroactive_conformance_witness_declaring_scope_tuple_collision.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(WitnessScopeTestAccess::clear(analyzer));
	}
	TEST_CASE("lexical_enum_tuple_alias_and_class_constants_resolve_in_both_phases") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String provider_path = "res://tests/witness_scope/preloaded.barista";
		BS_TEST_REQUIRE(seed(fixture, provider_path, "class_name Preloaded\nvar marker: int\n"));
		const String source = R"SOURCE(const ScriptType = preload("preloaded.barista")
class Helper:
	var marker: int
const ClassType = Helper
type LocalAlias = Helper
enum Mode:
	READY = 0
tuple Span(low: int, high: int)
trait Surface:
	abstract static func make(input: LocalAlias) -> Span
extend RefCounted uses Surface:
	static func make(input: LocalAlias) -> Span:
		var mode: Mode = Mode.READY
		var helper: ClassType = ClassType.new()
		var script: ScriptType = ScriptType.new()
		return Span(input.marker + helper.marker, script.marker + (mode as int))
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/witness_scope/types.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.resolve_interface() == OK);
		diagnostics(parser);
		BS_TEST_REQUIRE(parser.get_tree()->conformances.size() == 1 && parser.get_tree()->conformances[0]->witnesses.size() == 1);
		const auto *witness = parser.get_tree()->conformances[0]->witnesses[0];
		BS_TEST_REQUIRE(witness->parameters.size() == 1);
		CHECK(witness->parameters[0]->get_datatype().class_type == parser.get_tree()->get_member("Helper").m_class);
		CHECK(witness->get_datatype().kind == BSParser::DataType::TUPLE);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(WitnessScopeTestAccess::clear(analyzer));
		no_warnings(parser);
	}
	TEST_CASE("declaration_values_and_alias_expressions_are_not_borrowed") {
		for (const String &kind : { String("constant"), String("variable"), String("method"), String("alias") }) {
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/target.barista", "class_name Foreign\n"));
			const String declaration = kind == "constant" ? "const secret = 9\n" : kind == "variable" ? "var secret: int = 9\n"
					: kind == "method"																  ? "func secret() -> int:\n\treturn 9\n"
																									  : "type secret = int\n";
			const String source = declaration + String("trait Surface:\n\tabstract func leak() -> int\nextend Foreign uses Surface:\n\tfunc leak() -> int:\n\t\treturn secret") + (kind == "method" ? "()\n" : "\n");
			BSParser parser;
			const Error parsed = parser.parse(source, "res://tests/witness_scope/values.barista", false);
			diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_tree()->conformances.size() == 1 && parser.get_tree()->conformances[0]->witnesses.size() == 1);
			const auto *body = parser.get_tree()->conformances[0]->witnesses[0]->body;
			BS_TEST_REQUIRE(body && body->statements.size() == 1 && body->statements[0]->type == BSParser::Node::RETURN);
			const auto *value = static_cast<const BSParser::ReturnNode *>(body->statements[0])->return_value;
			const BSParser::Node *site = value;
			if (kind == "method") {
				BS_TEST_REQUIRE(value && value->type == BSParser::Node::CALL);
				site = static_cast<const BSParser::CallNode *>(value)->callee;
			}
			exact_error(parser, kind == "alias" ? "Type alias \"secret\" can only be used in a type position. It declares no value, so it cannot be called, constructed, or read." : "Identifier \"secret\" not declared in the current scope.", site);
			CHECK(WitnessScopeTestAccess::clear(analyzer));
		}
	}
	TEST_CASE("foreign_target_alias_stays_private_and_target_method_keeps_original_scope") {
		for (bool private_alias : { false, true }) {
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			const String provider = "class_name Foreign\ntype Hidden = int\nclass Marker:\n\tvar number: int\nfunc original() -> Marker:\n\treturn Marker.new()\n";
			BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/target.barista", provider));
			const String source = private_alias ? "trait Surface:\n\tabstract func get_value() -> int\nextend Foreign uses Surface:\n\tfunc get_value() -> int:\n\t\tvar value: Hidden = 1\n\t\treturn value\n" : "class Marker:\n\tvar text: String\ntrait Surface:\n\tabstract func original() -> Foreign.Marker\nextend Foreign uses Surface:\n\tpass\n";
			BSParser parser;
			const Error parsed = parser.parse(source, "res://tests/witness_scope/privacy.barista", false);
			diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == !private_alias);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_tree()->conformances.size() == 1);
			if (private_alias) {
				BS_TEST_REQUIRE(parser.get_tree()->conformances[0]->witnesses.size() == 1);
				const auto *body = parser.get_tree()->conformances[0]->witnesses[0]->body;
				BS_TEST_REQUIRE(body && body->statements.size() == 2 && body->statements[0]->type == BSParser::Node::VARIABLE);
				exact_error(parser, "Type alias \"Hidden\" is not in scope here. A type alias is visible only inside the file and the body that declare it, so it is neither inherited nor imported.", static_cast<const BSParser::VariableNode *>(body->statements[0])->datatype_specifier);
			} else {
				const auto *target = parser.get_tree()->conformances[0]->target->get_datatype().class_type;
				BS_TEST_REQUIRE(target && target->has_member("original"));
				const auto type = target->get_member("original").function->get_datatype();
				BS_TEST_REQUIRE(type.class_type);
				CHECK(type.class_type == target->get_member("Marker").m_class);
				CHECK(type.class_type->has_member("number"));
				no_warnings(parser);
			}
			CHECK(WitnessScopeTestAccess::clear(analyzer));
		}
	}
	TEST_CASE("unrelated_helper_resolution_does_not_inherit_witness_fallback") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/target.barista", "class_name Foreign\nclass Helper:\n\tvar value: OnlyHere\n"));
		const String source = "class OnlyHere:\n\tpass\ntrait Surface:\n\tabstract func value() -> Variant\nextend Foreign uses Surface:\n\tfunc value() -> Variant:\n\t\treturn Helper.new()\n";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/witness_scope/helper.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() != OK);
		diagnostics(parser);
		// Existing resolve_conformances requests the foreign interface without a source
		// node; parser.push_error uses its EOF cursor (after the source's eight lines).
		BS_TEST_REQUIRE(parser.get_errors().size() == 1);
		const auto &error = parser.get_errors().front()->get();
		CHECK(error.message == "Could not resolve class \"Foreign\".");
		CHECK(error.line == 9);
		CHECK(error.column == 1);
		CHECK(error.end_line == 9);
		CHECK(error.end_column == 2);
		CHECK(analyzer.analyze() != OK);
		CHECK(parser.get_errors().size() == 1);
		CHECK(WitnessScopeTestAccess::clear(analyzer));
	}
	TEST_CASE("inherited_target_outer_types_precede_declaring_file_types") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/base.barista", "class_name OuterBase\nclass Marker:\n\tvar target_marker: int\n"));
		BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/target.barista", "class_name Foreign extends OuterBase\nclass Inner:\n\tpass\n"));
		const String source = "class Marker:\n\tvar local_marker: String\ntrait Surface:\n\tabstract func get_marker() -> OuterBase.Marker\nextend Foreign.Inner uses Surface:\n\tfunc get_marker() -> Marker:\n\t\treturn Marker.new()\n";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/witness_scope/outer.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		BS_TEST_REQUIRE(parser.get_tree()->conformances.size() == 1 && parser.get_tree()->conformances[0]->witnesses.size() == 1);
		const auto type = parser.get_tree()->conformances[0]->witnesses[0]->get_datatype();
		BS_TEST_REQUIRE(type.class_type != nullptr);
		CHECK(type.class_type->has_member("target_marker"));
	}
	TEST_CASE("ordinary_target_value_claim_blocks_declaring_tuple_annotation_and_call") {
		for (bool annotation : { false, true }) {
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/target.barista", "class_name Foreign\nconst Span = 3\n"));
			const String source = "tuple Span(low: int, high: int)\ntrait Surface:\n\tabstract func value() -> Variant\nextend Foreign uses Surface:\n\tfunc value() -> Variant:\n" + String(annotation ? "\t\tvar x: Span\n\t\treturn x\n" : "\t\treturn Span(1, 2)\n");
			BSParser parser;
			const Error parsed = parser.parse(source, "res://tests/witness_scope/claims.barista", false);
			diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostics(parser);
			BS_TEST_REQUIRE(parser.get_tree()->conformances.size() == 1 && parser.get_tree()->conformances[0]->witnesses.size() == 1);
			const auto *body = parser.get_tree()->conformances[0]->witnesses[0]->body;
			BS_TEST_REQUIRE(body && body->statements.size() == (annotation ? 2 : 1));
			if (annotation) {
				BS_TEST_REQUIRE(body->statements[0]->type == BSParser::Node::VARIABLE);
				exact_error(parser, "\"Span\" is a constant but does not contain a type.", static_cast<const BSParser::VariableNode *>(body->statements[0])->datatype_specifier);
			} else {
				BS_TEST_REQUIRE(body->statements[0]->type == BSParser::Node::RETURN);
				const auto *value = static_cast<const BSParser::ReturnNode *>(body->statements[0])->return_value;
				BS_TEST_REQUIRE(value && value->type == BSParser::Node::CALL);
				exact_error(parser, "Cannot call \"Span\": it is not a function.", static_cast<const BSParser::CallNode *>(value)->callee);
			}
			CHECK(WitnessScopeTestAccess::clear(analyzer));
		}
	}
	TEST_CASE("nested_witness_body_error_restores_all_scope_pointers") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String source = "class OuterTarget:\n\tpass\ntrait Surface:\n\tabstract func value() -> int\nextend RefCounted uses Surface:\n\tfunc value() -> int:\n\t\treturn missing\n";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/witness_scope/nested.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		BS_TEST_REQUIRE(analyzer.resolve_interface() == OK);
		CHECK(WitnessScopeTestAccess::clear(analyzer));
		WitnessScopeTestAccess::nested_body(analyzer, parser.get_tree(), parser.get_tree()->get_member("OuterTarget").m_class);
		CHECK(WitnessScopeTestAccess::clear(analyzer));
		diagnostics(parser);
		BS_TEST_REQUIRE(parser.get_tree()->conformances.size() == 1 && parser.get_tree()->conformances[0]->witnesses.size() == 1);
		const auto *body = parser.get_tree()->conformances[0]->witnesses[0]->body;
		BS_TEST_REQUIRE(body != nullptr && body->statements.size() == 1 && body->statements[0]->type == BSParser::Node::RETURN);
		exact_error(parser, "Identifier \"missing\" not declared in the current scope.", static_cast<const BSParser::ReturnNode *>(body->statements[0])->return_value);
	}
	TEST_CASE("builtin_reflection_reserves_real_names_and_keeps_missing_type_fallback") {
		for (const String &target : { String("Dictionary"), String("Vector2"), String("Color") }) {
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			const String source = "class Helper:\n\tpass\ntrait Surface:\n\tabstract static func make() -> Helper\nextend " + target + " uses Surface:\n\tstatic func make() -> Helper:\n\t\treturn Helper.new()\n";
			BSParser parser;
			const Error parsed = parser.parse(source, "res://tests/witness_scope/builtin.barista", false);
			diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			diagnostics(parser);
			CHECK(parser.get_errors().is_empty());
			CHECK(WitnessScopeTestAccess::clear(analyzer));
			no_warnings(parser);
			BS_TEST_REQUIRE(parser.get_tree()->conformances.size() == 1);
			auto *shim = parser.get_tree()->conformances[0]->builtin_target_shim;
			BS_TEST_REQUIRE(shim != nullptr);
			CHECK(WitnessScopeTestAccess::reserved(analyzer, shim, parser.get_tree(), target == "Dictionary" ? "size" : target == "Vector2" ? "length"
																																			: "darkened"));
			if (target != "Dictionary")
				CHECK(WitnessScopeTestAccess::reserved(analyzer, shim, parser.get_tree(), target == "Vector2" ? "x" : "r"));
			if (target != "Dictionary")
				CHECK(WitnessScopeTestAccess::reserved(analyzer, shim, parser.get_tree(), target == "Vector2" ? "ZERO" : "RED"));
			CHECK_FALSE(WitnessScopeTestAccess::reserved(analyzer, shim, parser.get_tree(), "Helper"));
			CHECK_FALSE(WitnessScopeTestAccess::reserved(analyzer, shim, parser.get_tree(), "missing_reserved_name"));
			CHECK(WitnessScopeTestAccess::clear(analyzer));
		}
	}
	TEST_CASE("retained_foreign_target_generation_survives_refresh_during_witness_analysis") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = "res://tests/witness_scope/generation.barista";
		BS_TEST_REQUIRE(seed(fixture, path, "class_name Foreign\nvar power: int = 3\n"));
		const String source = "trait Surface:\n\tabstract func value() -> int\nextend Foreign uses Surface:\n\tfunc value() -> int:\n\t\treturn power\n";
		BSParser old_parser;
		BS_TEST_REQUIRE(old_parser.parse(source, "res://tests/witness_scope/old.barista", false) == OK);
		BSAnalyzer old_analyzer(&old_parser);
		BS_TEST_REQUIRE(old_analyzer.resolve_interface() == OK);
		BS_TEST_REQUIRE(old_parser.get_tree()->conformances.size() == 1);
		const auto *old_target = old_parser.get_tree()->conformances[0]->target->get_datatype().class_type;
		BS_TEST_REQUIRE(old_target != nullptr);
		const String next = "class_name Foreign\nvar power: String = \"new\"\n";
		BSCache::set_source_override(path, next);
		BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, next) == OK);
		CHECK(old_analyzer.analyze() == OK);
		diagnostics(old_parser);
		CHECK(old_parser.get_errors().is_empty());
		CHECK(WitnessScopeTestAccess::clear(old_analyzer));
		BSParser fresh_parser;
		BS_TEST_REQUIRE(fresh_parser.parse(source.replace("Surface", "FreshSurface"), "res://tests/witness_scope/new.barista", false) == OK);
		BSAnalyzer fresh_analyzer(&fresh_parser);
		CHECK(fresh_analyzer.analyze() != OK);
		diagnostics(fresh_parser);
		CHECK(WitnessScopeTestAccess::clear(fresh_analyzer));
		BS_TEST_REQUIRE(fresh_parser.get_tree()->conformances.size() == 1);
		CHECK(fresh_parser.get_tree()->conformances[0]->target->get_datatype().class_type != old_target);
		BS_TEST_REQUIRE(fresh_parser.get_tree()->conformances[0]->witnesses.size() == 1);
		const auto *body = fresh_parser.get_tree()->conformances[0]->witnesses[0]->body;
		BS_TEST_REQUIRE(body && body->statements.size() == 1 && body->statements[0]->type == BSParser::Node::RETURN);
		exact_error(fresh_parser, "Cannot return value of type \"String\" because the function return type is \"int\".", body->statements[0]);
		CHECK(old_target->get_member("power").get_datatype().builtin_type == Variant::INT);
	}
	TEST_CASE("public_witness_analysis_preserves_post_setup_state_and_restores_startup") {
		const Dictionary startup = settings_snapshot();
		{
			SettingsRestore restore;
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			const String target = "res://tests/witness_scope/public_target.barista";
			BS_TEST_REQUIRE(seed(fixture, target, "class_name Foreign\nvar power: int\n"));
			const Dictionary setup = settings_snapshot();
			const uint64_t revision = fixture.index().get_refresh_revision(target);
			const String store = fixture.path("index.bin");
			BS_TEST_REQUIRE(fixture.index().flush(store) == OK);
			const auto original = read_bytes(store);
			const String path = "res://tests/witness_scope/public.barista";
			for (int state : { 0, 1, 2 }) {
				const String previous = state == 2 ? "class_name Unsaved\n" : "";
				if (state == 0)
					BSCache::clear_source_override(path);
				else
					BSCache::set_source_override(path, previous);
				for (bool valid : { false, true }) {
					const String source = "class Helper:\n\tvar marker: int\ntrait Surface:\n\tabstract func value() -> Helper\nextend Foreign uses Surface:\n\tfunc value() -> Helper:\n\t\treturn " + String(valid ? "Helper.new()\n" : "secret\n");
					public_agreement(source, path, valid);
					CHECK(settings_snapshot() == setup);
					CHECK(BSCache::has_source_override(path) == (state != 0));
					if (state != 0)
						CHECK(BSCache::get_source_code(path) == previous);
					CHECK(fixture.index().get_refresh_revision(target) == revision);
					CHECK(fixture.index().flush(store) == OK);
					CHECK(read_bytes(store) == original);
					CHECK(BSConformanceRegistry::get_singleton()->get_file_conformances(path).is_empty());
					CHECK(BSConformanceRegistry::get_singleton()->get_file_trait_bindings(path).is_empty());
				}
			}
		}
		CHECK(settings_snapshot() == startup);
	}
	TEST_CASE("pin_projection_retroactive_conformance_witness_declaring_scope") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String source = R"SOURCE(# A witness resolves names against its conformance target first and then falls back to the lexical
# type scope of the file that declares the `extend`. The native target's stand-in has no lexical
# outer, so without that fallback this file's own generic would be unreachable from both the witness
# signature and its body.
class Box:
	func marker() -> String:
		return "boxed"


trait Boxable:
	abstract static func boxed() -> Box


extend RefCounted uses Boxable:
	static func boxed() -> Box:
		var made: Box = Box.new()
		return made


func test() -> void:
	print(RefCounted.boxed().marker())
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/witness_scope/retroactive_conformance_witness_declaring_scope.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(WitnessScopeTestAccess::clear(analyzer));
		no_warnings(parser);
		CHECK(WitnessScopeTestAccess::clear(analyzer));
	}
	TEST_CASE("pin_projection_retroactive_conformance_witness_declaring_scope_foreign") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/rtc_scope_holder.notest.barista", R"SOURCE(# Companion foreign target class for the witness declaration-scope fixtures. It declares no trait of
# its own and lives in a different file from every `extend` that conforms it, so a witness for it has
# no lexical path back to the conformance file except the declaration-site fallback.
class_name RtcScopeHolder
extends RefCounted

var power: int = 21
)SOURCE"));
		const String source = R"SOURCE(# A foreign Foundry Script root class is the target, so its lexical outer chain belongs to another
# parse tree. The witness still reaches this file's own generic and named tuple, in the return
# annotation and in the body, and still reads the target's own `power` member. The helper types are
# declared *after* the `extend` to prove the fallback obeys the same order-independent member
# resolution an ordinary class body does.
extend RtcScopeHolder uses RtcScopeCarrying:
	func carried() -> Carrier:
		var made: Carrier = Carrier.new()
		made.value = power
		made.span = Span(power, power + 1)
		return made


trait RtcScopeCarrying:
	abstract func carried() -> Carrier


tuple Span(low: int, high: int)


class Carrier:
	var value: int
	var span: Span = Span(0, 0)

	func describe() -> String:
		return "carrier:" + str(value) + ":" + str(span.high)


func test() -> void:
	var holder: RtcScopeCarrying = RtcScopeHolder.new()
	var carrier: Carrier = holder.carried()
	print(carrier.describe())
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/witness_scope/retroactive_conformance_witness_declaring_scope_foreign.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(WitnessScopeTestAccess::clear(analyzer));
		no_warnings(parser);
		CHECK(WitnessScopeTestAccess::clear(analyzer));
	}
	TEST_CASE("pin_projection_retroactive_conformance_witness_declaring_scope_builtin_int") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String source = R"SOURCE(# The declaration-site fallback is one language rule, not a native-target exception: a builtin
# value-type target's stand-in has no lexical outer either, so its witness reaches this file's own
# generic through the same fallback, in the return annotation and in the body.
#
# Inside the witness `Self` is the conformance target, not the script that happens to own the
# compiled witness, so `Crate` reifies `T` to the builtin target at run time. Both validation
# consumers are exercised: `store()` assigns a `T`-typed parameter from inside the generic, and the
# witness writes the member directly from outside it. A direct `Crate.new()` and a stored
# `Crate` class handle must reify the same argument.
class Crate:
	var value: int

	func store(next: int) -> void:
		value = next

	func summary() -> String:
		return "crate:" + str(value)


trait Cratable:
	abstract static func packed(value: Self) -> Crate
	abstract static func aliased(value: Self) -> Crate


extend int uses Cratable:
	static func packed(value: Self) -> Crate:
		var made: Crate = Crate.new()
		made.store(value)
		return made

	static func aliased(value: Self) -> Crate:
		var handle := Crate
		var made: Crate = handle.new()
		made.value = value
		return made


# A Dictionary target treats any unresolved name as a potential key, so the fallback has to be
# consulted ahead of that catch-all or the helper type would silently widen to Variant. It also
# proves the rule is about the conformance target rather than about `int`.
func test() -> void:
	print(int.packed(7).summary())
	print(int.aliased(9).summary())
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/witness_scope/retroactive_conformance_witness_declaring_scope_builtin_int.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(WitnessScopeTestAccess::clear(analyzer));
		no_warnings(parser);
		CHECK(WitnessScopeTestAccess::clear(analyzer));
	}
	TEST_CASE("pin_projection_retroactive_conformance_witness_declaring_scope_builtin_Dictionary") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String source = R"SOURCE(# The declaration-site fallback is one language rule, not a native-target exception: a builtin
# value-type target's stand-in has no lexical outer either, so its witness reaches this file's own
# generic through the same fallback, in the return annotation and in the body.
#
# Inside the witness `Self` is the conformance target, not the script that happens to own the
# compiled witness, so `Crate` reifies `T` to the builtin target at run time. Both validation
# consumers are exercised: `store()` assigns a `T`-typed parameter from inside the generic, and the
# witness writes the member directly from outside it. A direct `Crate.new()` and a stored
# `Crate` class handle must reify the same argument.
class Crate:
	var value: Dictionary

	func store(next: Dictionary) -> void:
		value = next

	func summary() -> String:
		return "crate:" + str(value)


trait Cratable:
	abstract static func packed(value: Self) -> Crate
	abstract static func aliased(value: Self) -> Crate


extend Dictionary uses Cratable:
	static func packed(value: Self) -> Crate:
		var made: Crate = Crate.new()
		made.store(value)
		return made

	static func aliased(value: Self) -> Crate:
		var handle := Crate
		var made: Crate = handle.new()
		made.value = value
		return made


func test() -> void:
	print(Dictionary.packed({"key": 1}).summary())
	print(Dictionary.aliased({"other": 2}).summary())
)SOURCE";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/witness_scope/retroactive_conformance_witness_declaring_scope_builtin_Dictionary.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		CHECK(parser.get_errors().is_empty());
		CHECK(WitnessScopeTestAccess::clear(analyzer));
		no_warnings(parser);
		CHECK(WitnessScopeTestAccess::clear(analyzer));
	}
	TEST_CASE("active_helper_lookup_does_not_append_witness_declaration_scopes") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/isolated.barista", "class_name Foreign\nclass Helper:\n\tpass\n"));
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("class OnlyHere:\n\tpass\ntrait Surface:\n\tabstract func value() -> Variant\nextend Foreign uses Surface:\n\tfunc value() -> Variant:\n\t\treturn Helper.new()\n", "res://tests/witness_scope/isolation.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		BS_TEST_REQUIRE(parser.get_tree()->conformances.size() == 1);
		auto *target = parser.get_tree()->conformances[0]->target->get_datatype().class_type;
		BS_TEST_REQUIRE(target && target->has_member("Helper"));
		WitnessScopeTestAccess::helper_isolation(analyzer, target, parser.get_tree(), target->get_member("Helper").m_class);
		CHECK(WitnessScopeTestAccess::clear(analyzer));
		no_warnings(parser);
	}
	TEST_CASE("declaring_inherited_outer_type_is_visible_but_provider_alias_is_private") {
		for (bool alias : { false, true }) {
			StorageFixture fixture;
			BSConformanceRegistry::ScopedCorpusState registry;
			BS_TEST_REQUIRE(seed(fixture, "res://tests/witness_scope/declaration_base.barista", "class_name DeclarationBase\nclass InheritedType:\n\tvar marker: int\ntype Hidden = int\n"));
			const String source = "extends DeclarationBase\ntrait Surface:\n\tabstract static func value() -> Variant\nextend RefCounted uses Surface:\n\tstatic func value() -> Variant:\n" + String(alias ? "\t\tvar value: Hidden = 1\n\t\treturn value\n" : "\t\tvar value: InheritedType = InheritedType.new()\n\t\treturn value\n");
			BSParser parser;
			const Error parsed = parser.parse(source, "res://tests/witness_scope/declaration_outer.barista", false);
			diagnostics(parser);
			BS_TEST_REQUIRE(parsed == OK);
			BSAnalyzer analyzer(&parser);
			CHECK((analyzer.analyze() == OK) == !alias);
			diagnostics(parser);
			CHECK(WitnessScopeTestAccess::clear(analyzer));
			if (alias) {
				BS_TEST_REQUIRE(parser.get_tree()->conformances.size() == 1 && parser.get_tree()->conformances[0]->witnesses.size() == 1);
				const auto *body = parser.get_tree()->conformances[0]->witnesses[0]->body;
				BS_TEST_REQUIRE(body && body->statements.size() == 2 && body->statements[0]->type == BSParser::Node::VARIABLE);
				exact_error(parser, "Type alias \"Hidden\" is not in scope here. A type alias is visible only inside the file and the body that declare it, so it is neither inherited nor imported.", static_cast<const BSParser::VariableNode *>(body->statements[0])->datatype_specifier);
			} else
				no_warnings(parser);
		}
	}
	TEST_CASE("plain_class_constant_and_nested_type_producers_keep_semantic_identity") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String source = "class Types:\n\tclass Helper:\n\t\tvar marker: int\n\tenum Mode:\n\t\tREADY = 0\n\ttuple Span(low: int, high: int)\nconst ClassType = Types.Helper\ntrait Surface:\n\tabstract static func value() -> Types.Span\nextend RefCounted uses Surface:\n\tstatic func value() -> Types.Span:\n\t\tvar mode: Types.Mode = Types.Mode.READY\n\t\tvar helper: ClassType = ClassType.new()\n\t\treturn Types.Span(helper.marker, mode as int)\n";
		BSParser parser;
		const Error parsed = parser.parse(source, "res://tests/witness_scope/nested_types.barista", false);
		diagnostics(parser);
		BS_TEST_REQUIRE(parsed == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		diagnostics(parser);
		BS_TEST_REQUIRE(parser.get_tree()->has_member("ClassType"));
		const auto *constant = parser.get_tree()->get_member("ClassType").constant;
		BS_TEST_REQUIRE(constant && constant->initializer);
		CHECK(constant->initializer->is_constant);
		CHECK(constant->initializer->is_unmaterialized_constant);
		CHECK(constant->get_datatype().class_type == parser.get_tree()->get_member("Types").m_class->get_member("Helper").m_class);
		CHECK(WitnessScopeTestAccess::clear(analyzer));
		no_warnings(parser);
	}
}
