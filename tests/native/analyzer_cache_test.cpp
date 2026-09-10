/**************************************************************************/
/*  analyzer_cache_test.cpp                                               */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "analyzer_helpers.h"
#include "barista_script.h"
#include "barista_script_language.h"
#include "storage_fixture.h"
#include "test_require.h"

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
constexpr const char *SCRIPT_A = "res://tests/cache_fixtures/script_a.barista";

String named_class(const String &body) {
	return "class_name " + body;
}

BSParser::DataType data_type(BSParser::DataType::Kind kind, Variant::Type builtin_type) {
	BSParser::DataType result;
	result.kind = kind;
	result.builtin_type = builtin_type;
	return result;
}

bool script_is_valid(const String &source, const String &path) {
	Ref<BaristaScript> script;
	script.instantiate();
	script->_set_source_code(source);
	script->set_path(path);
	return script->_is_valid();
}

void scenario_parser_lifecycle() {
	StorageFixture fixture;
	Error first_error = ERR_BUG;
	const Ref<BSParserRef> first = BSCache::get_parser(SCRIPT_A, BSParserRef::PARSED, first_error);
	BS_TEST_REQUIRE(first.is_valid());
	CHECK(first_error == OK);
	CHECK(first->get_status() == BSParserRef::PARSED);

	Error raised_error = ERR_BUG;
	const Ref<BSParserRef> raised = BSCache::get_parser(SCRIPT_A, BSParserRef::FULLY_SOLVED, raised_error);
	BS_TEST_REQUIRE(raised.is_valid());
	CHECK(raised_error == OK);
	CHECK(raised->get_status() == BSParserRef::FULLY_SOLVED);
	CHECK(raised->get_source_hash() == first->get_source_hash());

	Error reused_error = ERR_BUG;
	const Ref<BSParserRef> reused = BSCache::get_parser(SCRIPT_A, BSParserRef::FULLY_SOLVED, reused_error);
	CHECK(reused_error == OK);
	CHECK(reused.is_valid());
	CHECK(reused == raised);
	CHECK(BSCache::has_parser(SCRIPT_A));
}

void scenario_missing_and_self() {
	StorageFixture fixture;
	const String owner_path = "res://tests/analyzer_cache/owner.barista";
	const String missing_path = "res://tests/analyzer_cache/does_not_exist.barista";
	const String self_path = "res://tests/analyzer_cache/self.barista";
	BSCache::set_source_override(owner_path, named_class("OwnerFile extends Node\n"));
	Error owner_error = ERR_BUG;
	const Ref<BSParserRef> owner = BSCache::get_parser(owner_path, BSParserRef::EMPTY, owner_error);
	CHECK(owner_error == OK);
	CHECK(owner.is_valid());

	Error missing_error = ERR_BUG;
	const Ref<BSParserRef> missing = BSCache::get_parser(missing_path, BSParserRef::EMPTY, missing_error, owner_path);
	CHECK(missing.is_null());
	CHECK_FALSE(BSCache::has_parser(missing_path));
	CHECK(BSCache::get_inverse_dependencies(missing_path).is_empty());
	BSCache::remove_script(missing_path);
	CHECK(BSCache::has_parser(owner_path));

	BSCache::set_source_override(self_path, named_class("SelfFile extends Node\n"));
	Error self_error = ERR_BUG;
	const Ref<BSParserRef> self = BSCache::get_parser(self_path, BSParserRef::EMPTY, self_error, self_path);
	CHECK(self_error == OK);
	CHECK(self.is_valid());
	CHECK_FALSE(BSCache::get_inverse_dependencies(self_path).has(self_path));
}

void scenario_strict_settings() {
	StorageFixture fixture;
	StrictAnalyzerSettingsScope settings;
	const String path = "res://tests/analyzer_cache/strict.barista";
	BSCache::set_source_override(path, named_class("StrictOne extends Node\n"));
	Error error = ERR_BUG;
	const Ref<BSParserRef> initial = BSCache::get_parser(path, BSParserRef::PARSED, error);
	BS_TEST_REQUIRE(error == OK && initial.is_valid());
	CHECK(BSCache::has_parser(path));

	CHECK_FALSE(BSParser::invalidate_analysis_on_strict_settings_change());
	CHECK(BSCache::has_parser(path));
	CHECK_FALSE(BSParser::invalidate_analysis_on_strict_settings_change());

	settings.strict_null(true);
	CHECK(BSParser::invalidate_analysis_on_strict_settings_change());
	CHECK_FALSE(BSCache::has_parser(path));
	CHECK(BSCache::has_source_override(path));

	error = ERR_BUG;
	const Ref<BSParserRef> rebuilt = BSCache::get_parser(path, BSParserRef::PARSED, error);
	BS_TEST_REQUIRE(error == OK && rebuilt.is_valid());
	settings.strict_dynamic(true);
	CHECK(BSParser::invalidate_analysis_on_strict_settings_change());
	CHECK_FALSE(BSParser::invalidate_analysis_on_strict_settings_change());
}

void scenario_can_reference() {
	StorageFixture fixture;
	using DataType = BSParser::DataType;
	const DataType integer = data_type(DataType::BUILTIN, Variant::INT);
	const DataType floating = data_type(DataType::BUILTIN, Variant::FLOAT);
	CHECK(integer.can_reference(integer));
	CHECK_FALSE(integer.can_reference(floating));

	DataType meta_integer = integer;
	meta_integer.is_meta_type = true;
	CHECK_FALSE(integer.can_reference(meta_integer));
	CHECK_FALSE(data_type(DataType::UNION, Variant::NIL).can_reference(integer));

	DataType pair = data_type(DataType::TUPLE, Variant::ARRAY);
	pair.set_container_element_type(0, integer);
	pair.set_container_element_type(1, integer);
	CHECK(pair.can_reference(pair));
	DataType one_element = data_type(DataType::TUPLE, Variant::ARRAY);
	one_element.set_container_element_type(0, integer);
	CHECK_FALSE(one_element.can_reference(data_type(DataType::BUILTIN, Variant::ARRAY)));
	CHECK_FALSE(one_element.can_reference(pair));

	DataType node = data_type(DataType::NATIVE, Variant::OBJECT);
	node.native_type = "Node";
	DataType node_2d = data_type(DataType::NATIVE, Variant::OBJECT);
	node_2d.native_type = "Node2D";
	CHECK(node.can_reference(node_2d));
	CHECK_FALSE(node_2d.can_reference(node));

	DataType missing_class = data_type(DataType::CLASS, Variant::OBJECT);
	missing_class.native_type = "RefCounted";
	missing_class.script_path = "res://tests/analyzer_cache/missing_class.barista";
	DataType missing_other = missing_class;
	missing_other.script_path = "res://tests/analyzer_cache/missing_other.barista";
	CHECK_FALSE(missing_class.can_reference(missing_other));
}

void scenario_validate_and_is_valid_agree() {
	StorageFixture fixture;
	const String valid_path = "res://tests/analyzer_cache/analyzer_valid.barista";
	const String valid_source = named_class("AnalyzerValid extends Node\n\nfunc _ready() -> void:\n\tvar x: int = 1\n");
	const AnalysisResult analyzed = analyze_source(valid_source, valid_path);
	CHECK(analyzed.valid());
	CHECK(source_analyzes(valid_source, valid_path));
	CHECK(script_is_valid(valid_source, valid_path));

	const String bad_path = "res://tests/analyzer_cache/analyzer_bad.barista";
	const String bad_source = named_class("AnalyzerBad extends NotARealBaseClass\n");
	const AnalysisResult bad_analyzed = analyze_source(bad_source, bad_path);
	CHECK_FALSE(bad_analyzed.valid());
	CHECK_FALSE(source_analyzes(bad_source, bad_path));
	CHECK_FALSE(script_is_valid(bad_source, bad_path));
}
} // namespace

TEST_SUITE("analyzer_cache") {
	TEST_CASE("parser_lifecycle") { scenario_parser_lifecycle(); }
	TEST_CASE("missing_and_self") { scenario_missing_and_self(); }
	TEST_CASE("strict_settings") { scenario_strict_settings(); }
	TEST_CASE("can_reference") { scenario_can_reference(); }
	TEST_CASE("validate_and_is_valid_agree") { scenario_validate_and_is_valid_agree(); }

	TEST_CASE("repeated_and_reversed_cases_restore_ambient_state") {
		void (*scenarios[])() = {
			scenario_parser_lifecycle,
			scenario_missing_and_self,
			scenario_strict_settings,
			scenario_can_reference,
			scenario_validate_and_is_valid_agree,
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
}
