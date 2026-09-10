/**************************************************************************/
/*  storage_fixture_test.cpp                                              */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "storage_fixture.h"

#include <iostream>

using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
void fail_inside_storage_scope() {
	StorageFixture fixture;
	BSCache::set_source_override("res://tests/cache_fixtures/script_a.barista", "# temporary override");
	BSCache::record_dependency("res://tests/cache_fixtures/script_a.barista", "res://temporary_owner.barista");
	BSAnalyzer::set_bootstrap_allowed_dependency_root("res://temporary/");
	BSDeclarationRecord value;
	value.path = "res://temporary.barista";
	value.source_digest = 123;
	value.namespace_name = "temporary";
	value.declares_retroactive_conformances = true;
	CHECK(fixture.index().commit_record(fixture.index().claim_refresh(value.path), value));
	if (!write_bytes(fixture.path("interrupted.tmp"), bytes("interrupted test output"))) {
		return;
	}
	CHECK_MESSAGE(false, "Intentional assertion before early return; native fixture cleanup must still run");
	return;
}
} // namespace

TEST_CASE("storage assertion failure restores state" * doctest::test_suite("runner_failure")) {
	if (verify_case_isolation(fail_inside_storage_scope)) {
		std::cout << "BS_STORAGE_CLEANUP_OK" << std::endl;
	}
}

TEST_CASE("unscoped storage mutation is rejected" * doctest::test_suite("runner_failure")) {
	CHECK(verify_case_isolation([]() {
		BSAnalyzer::set_bootstrap_allowed_dependency_root("res://deliberately_leaked/");
	}));
}
