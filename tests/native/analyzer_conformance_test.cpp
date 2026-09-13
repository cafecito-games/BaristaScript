/**************************************************************************/
/*  analyzer_conformance_test.cpp                                         */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                          */
/*  This file is part of BaristaScript, a Godot GDExtension.                */
/*  SPDX-License-Identifier: MIT                                           */
/**************************************************************************/

#include "analyzer_conformance_helpers.h"
#include "analyzer_helpers.h"
#include "bs_conformance_registry.h"
#include "storage_fixture.h"
#include "test_require.h"

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

namespace {
void scenario_conformance_scoped_visibility() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	class AllowOnly : public BSConformanceRegistry::Visibility {
		String path;

	public:
		explicit AllowOnly(const String &p_path) : path(p_path) {}
		bool can_see(const String &candidate) const override { return candidate == path; }
	};
	AllowOnly allow_a("res://a.barista");
	AllowOnly allow_b("res://b.barista");
	CHECK(BSConformanceRegistry::debug_is_visible("res://a.barista"));
	CHECK(BSConformanceRegistry::debug_is_visible("res://b.barista"));
	CHECK(BSConformanceRegistry::debug_is_visible("res://c.barista"));
	{
		BSConformanceRegistry::ScopedVisibility outer(&allow_a);
		CHECK(BSConformanceRegistry::debug_is_visible("res://a.barista"));
		CHECK_FALSE(BSConformanceRegistry::debug_is_visible("res://b.barista"));
		{
			BSConformanceRegistry::ScopedVisibility nested(&allow_b);
			CHECK(BSConformanceRegistry::debug_is_visible("res://b.barista"));
			CHECK_FALSE(BSConformanceRegistry::debug_is_visible("res://a.barista"));
		}
		CHECK(BSConformanceRegistry::debug_is_visible("res://a.barista"));
		CHECK_FALSE(BSConformanceRegistry::debug_is_visible("res://b.barista"));
	}
	CHECK(BSConformanceRegistry::debug_is_visible("res://a.barista"));
	CHECK(BSConformanceRegistry::debug_is_visible("res://b.barista"));
	{
		BSConformanceRegistry::ScopedVisibility outer(&allow_a);
		BSConformanceRegistry::ScopedInFlightReplacement in_flight("res://a.barista");
		CHECK_FALSE(BSConformanceRegistry::debug_is_visible("res://a.barista"));
	}
	CHECK(BSConformanceRegistry::debug_is_visible("res://a.barista"));
	BS_TEST_REQUIRE(BSConformanceRegistry::get_singleton() != nullptr);
	CHECK_FALSE(BSConformanceRegistry::get_singleton()->has_conformance("res://Widget", SNAME("SomeTrait")));
	const String own_path = "res://tests/vis_viewer.barista";
	const String dep_path = "res://tests/vis_dep.barista";
	const String unrelated_path = "res://tests/vis_unrelated.barista";
	BSCache::set_source_override(dep_path, "class_name VisDep extends Node\n");
	BSCache::set_source_override(unrelated_path, "class_name VisUnrelated extends Node\n");
	const String source = "class_name VisViewer extends \"" + dep_path + "\"\n";
	BSCache::set_source_override(own_path, source);
	BSParser parser;
	BSAnalyzer analyzer(&parser);
	BS_TEST_REQUIRE(parser.parse(source, own_path, false) == OK);
	CHECK(AnalyzerMigrationTestAccess::can_see(analyzer, own_path));
	CHECK(AnalyzerMigrationTestAccess::can_see(analyzer, dep_path));
	CHECK_FALSE(AnalyzerMigrationTestAccess::can_see(analyzer, unrelated_path));
	BSCache::clear_source_override(own_path);
	const int before = fixture.index().get_record_count();
	analyze_source("class_name VisIndexGuard extends Node\n", "res://tests/vis_index_guard.barista");
	CHECK(fixture.index().get_record_count() == before);
}

void scenario_conformance_registry_registration() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const auto report = AnalyzerMigrationTestAccess::conformance_registry_registration();
	CHECK_MESSAGE(report.analyze_ok == true, "declaring analyze publishes conformances");
	CHECK_MESSAGE(report.registered_count >= 1, "registry stores at least one Conformance");
	CHECK_MESSAGE(report.registered_after_analyze == true, "has_conformance after analyze");
	CHECK_MESSAGE(report.none_sees_registered == true, "no Visibility → membership visible");
	CHECK_MESSAGE(report.in_flight_hides_has_conformance == true, "ScopedInFlightReplacement hides has_conformance for declaring file");
	CHECK_MESSAGE(report.after_in_flight_sees_again == true, "leaving in-flight restores has_conformance");
	CHECK_MESSAGE(report.viewer_parse_ok == true, "viewer parse ok");
	CHECK_MESSAGE(report.viewer_hides_unrelated_declaring == true, "ConformanceVisibility hides unrelated declaring has_conformance");
	CHECK_MESSAGE(report.viewer_can_see_own == true, "viewer can_see own path");
	CHECK_MESSAGE(report.viewer_can_see_dep == true, "viewer can_see extends dependency");
	CHECK_MESSAGE(report.viewer_cannot_see_declaring == true, "viewer cannot_see unrelated declaring file");
	CHECK_MESSAGE(report.viewer_cannot_see_unrelated == true, "viewer cannot_see unrelated path");
	CHECK_MESSAGE(report.dep_parse_ok == true, "dependency-of-declaring parse ok");
	CHECK_MESSAGE(report.dep_can_see_declaring == true, "preload dependency can_see declaring file");
	CHECK_MESSAGE(report.dep_sees_has_conformance == true, "dependency Visibility sees has_conformance");
	CHECK_MESSAGE(report.reanalyze_clear_ok == true, "empty reanalysis ok");
	CHECK_MESSAGE(report.cleared_after_reanalyze == true, "empty reanalysis clears file conformances");
	CHECK_MESSAGE(report.reanalyze_replace_ok == true, "replacement reanalysis ok");
	CHECK_MESSAGE(report.replaced_has_other == true, "reanalysis registers new trait");
	CHECK_MESSAGE(report.replaced_dropped_old == true, "reanalysis drops previous trait");
	const int before = fixture.index().get_record_count();
	analyze_source("class_name RegIndexGuard extends Node\n", "res://tests/reg_index_guard.barista");
	CHECK(fixture.index().get_record_count() == before);
}

void scenario_conformance_witness_lookup() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const auto report = AnalyzerMigrationTestAccess::conformance_witness_lookup();
	CHECK_MESSAGE(report.same_file_analyze_ok == true, "same-file CLASS self.greet() witness call analyzes");
	CHECK_MESSAGE(report.registered_count >= 1, "registry stores Conformance with witnesses");
	CHECK_MESSAGE(report.has_greet_witness_key == true, "registered Conformance stores greet witness method-name key");
	CHECK_MESSAGE(report.find_witness_location_ok == true, "find_witness_location returns declaring file + conformance_index");
	CHECK_MESSAGE(report.class_arity_checks_witness == true, "CLASS witness signature applied (arity diagnostic on self.greet(1))");
	CHECK_MESSAGE(report.native_analyze_ok == true, "native Node extend witness call analyzes");
	CHECK_MESSAGE(report.native_arity_checks_witness == true, "NATIVE witness signature applied (arity diagnostic on wit_greet(1))");
	CHECK_MESSAGE(report.viewer_parse_ok == true, "unrelated viewer parse ok");
	CHECK_MESSAGE(report.viewer_hides_witness_location == true, "ConformanceVisibility hides find_witness_location for unrelated declaring file");
	CHECK_MESSAGE(report.viewer_cannot_see_declaring == true, "unrelated viewer cannot_see declaring file");
	CHECK_MESSAGE(report.dep_parse_ok == true, "dependency-of-declaring parse ok");
	CHECK_MESSAGE(report.dep_sees_witness_location == true, "preload dependency Visibility sees find_witness_location");
	CHECK_MESSAGE(report.reanalyze_clear_ok == true, "empty reanalysis ok");
	CHECK_MESSAGE(report.cleared_witness_location == true, "empty reanalysis clears find_witness_location");
	CHECK_MESSAGE(report.cleared_file_empty == true, "empty reanalysis clears file conformances");
	const int before = fixture.index().get_record_count();
	analyze_source("class_name WitIndexGuard extends Node\n", "res://tests/wit_index_guard.barista");
	CHECK(fixture.index().get_record_count() == before);
}

void scenario_conformance_hidden_witness() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const auto report = AnalyzerMigrationTestAccess::conformance_hidden_witness();
	CHECK_MESSAGE(report.declaring_analyze_ok == true, "builtin String extend declaring file analyzes");
	CHECK_MESSAGE(report.registered_count >= 1, "registry stores hidden-witness Conformance");
	CHECK_MESSAGE(report.has_hid_mark_key == true, "registered Conformance stores hid_mark witness method-name key");
	CHECK_MESSAGE(report.visible_find_witness_location == true, "with no Visibility, find_witness_location finds declaring file");
	CHECK_MESSAGE(report.no_visibility_hides_nothing == true, "with no Visibility, find_hidden_witness_declaration is empty");
	CHECK_MESSAGE(report.viewer_parse_ok == true, "unrelated viewer parse ok");
	CHECK_MESSAGE(report.viewer_analyze_failed == true, "unrelated viewer must not silently succeed on hidden witness call");
	CHECK_MESSAGE(report.viewer_hidden_diagnostic == true, "viewer surfaces Foundry hidden-witness diagnostic (method + trait + file)");
	CHECK_MESSAGE(report.viewer_finds_hidden_declaration == true, "ConformanceVisibility find_hidden_witness_declaration reports declaring file + trait");
	CHECK_MESSAGE(report.viewer_hides_witness_location == true, "ConformanceVisibility hides find_witness_location for unrelated viewer");
	CHECK_MESSAGE(report.dep_analyze_ok == true, "extends dependency of declaring file still resolves witness call");
	CHECK_MESSAGE(report.final_declaring_analyze_ok == true, "final CLASS same-file extend declaring file analyzes");
	CHECK_MESSAGE(report.final_has_greet_key == true, "final CLASS Conformance stores hid_greet witness method-name key");
	CHECK_MESSAGE(report.final_viewer_parse_ok == true, "final CLASS unrelated viewer parse ok");
	CHECK_MESSAGE(report.final_viewer_finds_hidden == true, "final CLASS ConformanceVisibility find_hidden_witness_declaration reports declaring file");
	CHECK_MESSAGE(report.final_viewer_hides_location == true, "final CLASS ConformanceVisibility hides find_witness_location");
	const int before = fixture.index().get_record_count();
	analyze_source("class_name HidIndexGuard extends Node\n", "res://tests/hid_index_guard.barista");
	CHECK(fixture.index().get_record_count() == before);
}

void scenario_class_trait_binding_chain_coherence() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const auto report = AnalyzerMigrationTestAccess::class_trait_binding_chain_coherence();
	CHECK_MESSAGE(report.publish_ok == true, "try_replace publishes ClassTraitBinding without conflict");
	CHECK_MESSAGE(report.binding_stored == true, "get_file_trait_bindings returns published uses binding");
	CHECK_MESSAGE(report.reduce_builtin_ok == true, "reduce_type_argument records builtin INT");
	CHECK_MESSAGE(report.agree_registered_count == 1, "matching chain args register Conformance");
	CHECK_MESSAGE(report.agree_no_chain_conflict == true, "matching ClassTraitBinding + Conformance args do not CHAIN_COHERENCE");
	CHECK_MESSAGE(report.conflict_chain_coherence == true, "contradicting uses-binding yields RegistrationConflict::CHAIN_COHERENCE");
	CHECK_MESSAGE(report.conflict_rejected == true, "CHAIN_COHERENCE rejects the whole ConformanceNode declaration");
	CHECK_MESSAGE(report.conflict_store_empty == true, "rejected Conformance is not stored for the declaring file");
	CHECK_MESSAGE(report.conflict_index == 0, "conflict names conformance_index 0");
	CHECK_MESSAGE(report.conflict_conflicting_file == "res://tests/ctb_binding.barista", "conflict points at the binding declaring file");
	CHECK_MESSAGE(report.edge_loader_binding_ok == true, "loader publishes ClassTraitBinding with load edge without conflict");
	CHECK_MESSAGE(report.edge_reverse_chain_coherence == true, "reverse load edge licenses CHAIN_COHERENCE when Visibility cannot see loader");
	CHECK_MESSAGE(report.edge_reverse_rejected == true, "reverse-edge CHAIN_COHERENCE rejects the loaded Conformance");
	CHECK_MESSAGE(report.edge_reverse_store_empty == true, "reverse-edge rejected Conformance is not stored");
	CHECK_MESSAGE(report.edge_noedge_uncompared == true, "contradicting pair with no load edge either way stays uncompared");
	const int before = fixture.index().get_record_count();
	analyze_source("class_name CtbIndexGuard extends Node\n", "res://tests/ctb_index_guard.barista");
	CHECK(fixture.index().get_record_count() == before);
}

void scenario_recorded_trait_arguments_query() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const auto report = AnalyzerMigrationTestAccess::recorded_trait_arguments_query();
	CHECK_MESSAGE(report.script_publish_ok == true, "script conformance with recorded args publishes");
	CHECK_MESSAGE(report.script_query_ok == true, "visible get_recorded_trait_arguments returns published INT args");
	CHECK_MESSAGE(report.hidden_script_query_false == true, "hidden Visibility makes get_recorded_trait_arguments return false");
	CHECK_MESSAGE(report.hidden_native_query_false == true, "hidden Visibility makes get_native_recorded_trait_arguments return false");
	CHECK_MESSAGE(report.hidden_builtin_query_false == true, "hidden Visibility makes get_builtin_recorded_trait_arguments return false");
	CHECK_MESSAGE(report.builtin_publish_ok == true, "builtin-keyed conformance publishes");
	CHECK_MESSAGE(report.builtin_query_ok == true, "get_builtin_recorded_trait_arguments returns exact-key recorded args");
	CHECK_MESSAGE(report.native_publish_ok == true, "native Object conformance publishes");
	CHECK_MESSAGE(report.native_parent_walk_ok == true, "get_native_recorded_trait_arguments walks ClassDB parents to Object");
	CHECK_MESSAGE(report.farther_direct_ok == true, "farther recorded args remain queryable directly");
	CHECK_MESSAGE(report.nearer_empty_shadows == true, "nearer empty conformance shadows farther recorded args in project_registry_trait_arguments");
	CHECK_MESSAGE(report.farther_project_ok == true, "project_registry_trait_arguments returns farther record when nearer is absent");
	CHECK_MESSAGE(report.class_to_native_project_ok == true, "CLASS chain bottoms out at NATIVE and finds parent-walk recorded args");
	const int before = fixture.index().get_record_count();
	analyze_source("class_name RtaIndexGuard extends Node\n", "res://tests/rta_index_guard.barista");
	CHECK(fixture.index().get_record_count() == before);
}

void scenario_witness_collision_arbitration() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const auto report = AnalyzerMigrationTestAccess::witness_collision_arbitration();
	CHECK_MESSAGE(report.same_file_collision_diagnostic == true, "same-file two conformances with shared witness method diagnose collision");
	CHECK_MESSAGE(report.same_file_first_registered == true, "same-file first conformance still registers after witness collision on second");
	CHECK_MESSAGE(report.same_file_second_rejected == true, "same-file colliding second conformance is not stored");
	CHECK_MESSAGE(report.cross_first_ok == true, "cross-file first witness conformance analyzes and registers");
	CHECK_MESSAGE(report.get_witness_source_first == true, "get_witness_source reports the first declaring file");
	CHECK_MESSAGE(report.cross_file_collision_diagnostic == true, "cross-file conflicting witness diagnoses collision");
	CHECK_MESSAGE(report.cross_second_store_empty == true, "cross-file colliding second conformance is not stored");
	CHECK_MESSAGE(report.distinct_ok_analyze == true, "non-colliding distinct witness methods analyze cleanly");
	CHECK_MESSAGE(report.distinct_ok_registered == true, "non-colliding distinct witness methods both register");
	CHECK_MESSAGE(report.registry_first_ok == true, "try_replace publishes first witness declaration");
	CHECK_MESSAGE(report.registry_witness_collision == true, "try_replace rejects WITNESS_COLLISION authoritatively");
	CHECK_MESSAGE(report.registry_second_rejected == true, "WITNESS_COLLISION leaves second file store empty");
	const int before = fixture.index().get_record_count();
	analyze_source("class_name WcIndexGuard extends Node\n", "res://tests/wc_index_guard.barista");
	CHECK(fixture.index().get_record_count() == before);
}

void scenario_complete_self_referential_enum_type() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const auto report = AnalyzerMigrationTestAccess::complete_self_referential_enum_type();
	CHECK_MESSAGE(report.analyze_ok == true, "recursive Chain host analyzes cleanly");
	CHECK_MESSAGE(report.found_chain == true, "Chain enum located after analyze");
	CHECK_MESSAGE(report.declared_case_count == 3, "Chain declares End/Link/Branch");
	CHECK_MESSAGE(report.shell_is_enum == true, "Link.next captured as tagged-union shell");
	CHECK_MESSAGE(report.shell_values_empty == true, "Link.next starts as empty identity shell");
	CHECK_MESSAGE(report.completed_values_count == 3, "complete fills End/Link/Branch");
	CHECK_MESSAGE(report.completed_has_end == true, "completed shell has End");
	CHECK_MESSAGE(report.completed_has_link == true, "completed shell has Link");
	CHECK_MESSAGE(report.completed_has_branch == true, "completed shell has Branch");
	CHECK_MESSAGE(report.nested_link_stays_shell == true, "recursive Link.next inside completed payloads stays an empty shell");
	CHECK_MESSAGE(report.idempotent_values == true, "completing twice keeps case count");
	CHECK_MESSAGE(report.idempotent_nested_shell == true, "completing twice keeps nested shells");
	CHECK_MESSAGE(report.array_container_size == 1, "Branch children is a one-element container");
	CHECK_MESSAGE(report.array_element_values_count == 3, "complete descends into Array[Chain] element");
	CHECK(analyze_source("class_name CompleteSelfRefNested extends Node\nenum Chain:\n\tEnd\n\tLink(next: Chain)\nfunc build() -> Chain:\n\treturn Chain.Link(Chain.Link(Chain.End))\nfunc length(node: Chain) -> int:\n\tmatch node:\n\t\tChain.End:\n\t\t\treturn 0\n\t\tChain.Link(var next):\n\t\t\tmatch next:\n\t\t\t\tChain.End:\n\t\t\t\t\treturn 1\n\t\t\t\tChain.Link(_):\n\t\t\t\t\treturn 2\n", "res://tests/complete_self_ref_nested.barista").valid());
	const int before = fixture.index().get_record_count();
	analyze_source("class_name CompleteSelfRefIndexGuard extends Node\n", "res://tests/complete_self_ref_index_guard.barista");
	CHECK(fixture.index().get_record_count() == before);
}

void scenario_trait_requirements_and_conformance_witness() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String missing = "class_name TraitReqMissing extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n";
	const auto missing_report = analyze_source(missing, "res://tests/trait_req_missing.barista");
	CHECK_MESSAGE((missing_report.valid() == false), "missing abstract trait method is invalid");
	bool saw_missing = false;
	for (const auto &error : missing_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("must implement trait method") && message.contains("Damageable.take_damage()")) {
			saw_missing = true;
		}
	}
	CHECK_MESSAGE((saw_missing), "missing trait method diagnostic");
	const String ok = "class_name TraitReqOk extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n\nfunc take_damage(amount: int) -> void:\n\tpass\n";
	const auto ok_report = analyze_source(ok, "res://tests/trait_req_ok.barista");
	CHECK_MESSAGE((ok_report.valid() == true), "implemented abstract trait method is valid");
	const String abstract_class = "abstract class_name TraitReqAbstract extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n";
	const auto abstract_report = analyze_source(abstract_class, "res://tests/trait_req_abstract.barista");
	CHECK_MESSAGE((abstract_report.valid() == true), "abstract class may defer trait requirements");
	const String native_missing = "trait NeedsCustomPing:\n\tabstract func custom_ping() -> void\n\nextend Node uses NeedsCustomPing:\n\tpass\n";
	const auto native_missing_report = analyze_source(native_missing, "res://tests/rtc_native_missing.barista");
	CHECK_MESSAGE((native_missing_report.valid() == false), "native extend missing witness is invalid");
	bool saw_native_missing = false;
	for (const auto &error : native_missing_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("must implement trait method") && message.contains("custom_ping()")) {
			saw_native_missing = true;
		}
	}
	CHECK_MESSAGE((saw_native_missing), "native extend missing-witness diagnostic");
	const String native_ok = "trait NeedsCustomPing:\n\tabstract func custom_ping() -> void\n\nextend Node uses NeedsCustomPing:\n\tfunc custom_ping() -> void:\n\t\tpass\n";
	const auto native_ok_report = analyze_source(native_ok, "res://tests/rtc_native_ok.barista");
	CHECK_MESSAGE((native_ok_report.valid() == true), "native extend with witness is valid");
	const String local_missing = "class_name RtcLocalGadget extends Node\n\ntrait NeedsPing:\n\tabstract func ping() -> void\n\nextend RtcLocalGadget uses NeedsPing:\n\tpass\n";
	const auto local_missing_report = analyze_source(local_missing, "res://tests/rtc_local_missing.barista");
	CHECK_MESSAGE((local_missing_report.valid() == false), "same-file extend missing witness is invalid");
	bool saw_local_missing = false;
	for (const auto &error : local_missing_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Conformance of") && message.contains("must implement trait method") && message.contains("ping()")) {
			saw_local_missing = true;
		}
	}
	CHECK_MESSAGE((saw_local_missing), "same-file extend missing-witness diagnostic");
	const String local_ok = "class_name RtcLocalOk extends Node\n\ntrait NeedsPing:\n\tabstract func ping() -> void\n\nextend RtcLocalOk uses NeedsPing:\n\tfunc ping() -> void:\n\t\tpass\n";
	const auto local_ok_report = analyze_source(local_ok, "res://tests/rtc_local_ok.barista");
	CHECK_MESSAGE((local_ok_report.valid() == true), "same-file extend with witness is valid");
	const String redundant = "class_name RtcRedundantTarget extends Node\nuses NeedsPing\n\ntrait NeedsPing:\n\tabstract func ping() -> void\n\nfunc ping() -> void:\n\tpass\n\nextend RtcRedundantTarget uses NeedsPing:\n\tpass\n";
	const auto redundant_report = analyze_source(redundant, "res://tests/rtc_redundant_uses.barista");
	CHECK_MESSAGE((redundant_report.valid() == false), "extend redundant with target uses is invalid");
	bool saw_redundant = false;
	for (const auto &error : redundant_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("already conforms to trait") && message.contains("through its own \"uses\"") && message.contains("NeedsPing")) {
			saw_redundant = true;
		}
	}
	CHECK_MESSAGE((saw_redundant), "redundant uses conformance diagnostic");
	BSDeclarationIndex &index = fixture.index();
	index.clear();
	const String trait_path = "res://tests/index_damageable.barista";
	const String trait_source = "trait_name IndexDamageable\nabstract func take_damage(amount: int) -> void\n";
	BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(trait_path, trait_source);
	BSCache::set_source_override(trait_path, trait_source);
	const String consumer = "class_name TraitIndexMissing extends Node\nuses IndexDamageable\n";
	const auto index_report = analyze_source(consumer, "res://tests/trait_index_missing.barista");
	CHECK_MESSAGE((index_report.valid() == false), "index-backed missing trait method is invalid");
	bool saw_index = false;
	for (const auto &error : index_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("must implement trait method") && message.contains("take_damage()")) {
			saw_index = true;
		}
	}
	CHECK_MESSAGE((saw_index), "index-backed missing trait method diagnostic");
	BSCache::clear_source_override(trait_path);
	index.clear();
	const String sig_mismatch = "class_name TraitSigMismatch extends Node\nuses Damageable\n\ntrait Damageable:\n\tabstract func take_damage(amount: int) -> void\n\nfunc take_damage(amount: String) -> void:\n\tpass\n";
	const auto sig_mismatch_report = analyze_source(sig_mismatch, "res://tests/trait_sig_mismatch.barista");
	CHECK_MESSAGE((sig_mismatch_report.valid() == false), "trait method wrong parameter type is invalid");
	bool saw_sig_mismatch = false;
	for (const auto &error : sig_mismatch_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("signature does not match required trait method") && message.contains("Damageable.take_damage()")) {
			saw_sig_mismatch = true;
		}
	}
	CHECK_MESSAGE((saw_sig_mismatch), "trait method signature mismatch diagnostic");
	const String async_required = "class_name TraitAsyncRequired extends Node\nuses RemoteLoadable\n\ntrait RemoteLoadable:\n\tabstract async func fetch() -> String\n\nfunc fetch() -> String:\n\treturn \"\"\n";
	const auto async_required_report = analyze_source(async_required, "res://tests/trait_async_required.barista");
	CHECK_MESSAGE((async_required_report.valid() == false), "sync impl of async trait method is invalid");
	bool saw_async_required = false;
	for (const auto &error : async_required_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("must be async because it implements async trait method") && message.contains("RemoteLoadable.fetch()")) {
			saw_async_required = true;
		}
	}
	CHECK_MESSAGE((saw_async_required), "async trait method requires async impl diagnostic");
	const String sync_required = "class_name TraitSyncRequired extends Node\nuses Syncable\n\ntrait Syncable:\n\tabstract func compute() -> int\n\nasync func compute() -> int:\n\treturn 0\n";
	const auto sync_required_report = analyze_source(sync_required, "res://tests/trait_sync_required.barista");
	CHECK_MESSAGE((sync_required_report.valid() == false), "async impl of sync trait method is invalid");
	bool saw_sync_required = false;
	for (const auto &error : sync_required_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("cannot be async because it implements synchronous trait method") && message.contains("Syncable.compute()")) {
			saw_sync_required = true;
		}
	}
	CHECK_MESSAGE((saw_sync_required), "sync trait method rejects async impl diagnostic");
	const String self_ok = "class_name TraitSelfOk extends Node\nuses Creatable\n\ntrait Creatable:\n\tabstract static func create() -> Self\n\nstatic func create() -> Self:\n\treturn TraitSelfOk.new()\n";
	const auto self_ok_report = analyze_source(self_ok, "res://tests/trait_self_ok.barista");
	CHECK_MESSAGE((self_ok_report.valid() == true), "Self return matching implementer is valid");
	const String self_bad = "class_name TraitSelfBad extends Node\nuses Creatable\n\ntrait Creatable:\n\tabstract static func create() -> Self\n\nstatic func create() -> String:\n\treturn \"x\"\n";
	const auto self_bad_report = analyze_source(self_bad, "res://tests/trait_self_bad.barista");
	CHECK_MESSAGE((self_bad_report.valid() == false), "Self return mismatched to String is invalid");
	bool saw_self_bad = false;
	for (const auto &error : self_bad_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("signature does not match required trait method") && message.contains("Creatable.create()")) {
			saw_self_bad = true;
		}
	}
	CHECK_MESSAGE((saw_self_bad), "Self return mismatch diagnostic");
	const String arity_bad = "class_name TraitArityBad extends Node\nuses Binary\n\ntrait Binary:\n\tabstract func combine(a: int, b: int) -> int\n\nfunc combine(a: int) -> int:\n\treturn a\n";
	const auto arity_bad_report = analyze_source(arity_bad, "res://tests/trait_arity_bad.barista");
	CHECK_MESSAGE((arity_bad_report.valid() == false), "trait method arity mismatch is invalid");
	bool saw_arity = false;
	for (const auto &error : arity_bad_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("signature does not match required trait method") && message.contains("Binary.combine()")) {
			saw_arity = true;
		}
	}
	CHECK_MESSAGE((saw_arity), "trait method arity mismatch diagnostic");
	const String rest_narrow = "class_name TraitRestNarrow extends Node\nuses AcceptsNodes\n\ntrait AcceptsNodes:\n\tabstract func accept(...nodes: Array[Node]) -> int\n\nfunc accept(...nodes: Array[String]) -> int:\n\treturn nodes.size()\n";
	const auto rest_narrow_report = analyze_source(rest_narrow, "res://tests/trait_rest_narrow.barista");
	CHECK_MESSAGE((rest_narrow_report.valid() == false), "narrower rest tail does not satisfy trait rest requirement");
	bool saw_rest = false;
	for (const auto &error : rest_narrow_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("signature does not match required trait method") && message.contains("AcceptsNodes.accept()")) {
			saw_rest = true;
		}
	}
	CHECK_MESSAGE((saw_rest), "trait rest narrowing diagnostic");
	const String array_elem = "class_name TraitArrayElem extends Node\nuses TakesNodes\n\ntrait TakesNodes:\n\tabstract func take(items: Array[Node]) -> void\n\nfunc take(items: Array[String]) -> void:\n\tpass\n";
	const auto array_elem_report = analyze_source(array_elem, "res://tests/trait_array_elem.barista");
	CHECK_MESSAGE((array_elem_report.valid() == false), "fixed Array[Node] vs Array[String] param is invalid");
	bool saw_array_elem = false;
	for (const auto &error : array_elem_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("signature does not match required trait method") && message.contains("TakesNodes.take()")) {
			saw_array_elem = true;
		}
	}
	CHECK_MESSAGE((saw_array_elem), "fixed Array element mismatch diagnostic");
	const String dict_elem = "class_name TraitDictElem extends Node\nuses TakesMap\n\ntrait TakesMap:\n\tabstract func take(items: Dictionary[String, Node]) -> void\n\nfunc take(items: Dictionary[String, String]) -> void:\n\tpass\n";
	const auto dict_elem_report = analyze_source(dict_elem, "res://tests/trait_dict_elem.barista");
	CHECK_MESSAGE((dict_elem_report.valid() == false), "fixed Dictionary value element mismatch is invalid");
	bool saw_dict_elem = false;
	for (const auto &error : dict_elem_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("signature does not match required trait method") && message.contains("TakesMap.take()")) {
			saw_dict_elem = true;
		}
	}
	CHECK_MESSAGE((saw_dict_elem), "fixed Dictionary element mismatch diagnostic");
	const String static_vs_instance = "class_name TraitStaticVsInst extends Node\nuses Factory\n\ntrait Factory:\n\tabstract static func build() -> void\n\nfunc build() -> void:\n\tpass\n";
	const auto static_vs_instance_report = analyze_source(static_vs_instance, "res://tests/trait_static_vs_inst.barista");
	CHECK_MESSAGE((static_vs_instance_report.valid() == false), "instance impl of static trait method is invalid");
	bool saw_static_vs_inst = false;
	for (const auto &error : static_vs_instance_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("signature does not match required trait method") && message.contains("Factory.build()")) {
			saw_static_vs_inst = true;
		}
	}
	CHECK_MESSAGE((saw_static_vs_inst), "static-vs-instance signature mismatch diagnostic");
	const String instance_vs_static = "class_name TraitInstVsStatic extends Node\nuses Worker\n\ntrait Worker:\n\tabstract func run() -> void\n\nstatic func run() -> void:\n\tpass\n";
	const auto instance_vs_static_report = analyze_source(instance_vs_static, "res://tests/trait_inst_vs_static.barista");
	CHECK_MESSAGE((instance_vs_static_report.valid() == false), "static impl of instance trait method is invalid");
	bool saw_inst_vs_static = false;
	for (const auto &error : instance_vs_static_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("signature does not match required trait method") && message.contains("Worker.run()")) {
			saw_inst_vs_static = true;
		}
	}
	CHECK_MESSAGE((saw_inst_vs_static), "instance-vs-static signature mismatch diagnostic");
	const String rtc_sig = "class_name RtcSigTarget extends Node\n\ntrait NeedsPing:\n\tabstract func ping(code: int) -> void\n\nextend RtcSigTarget uses NeedsPing:\n\tfunc ping(code: String) -> void:\n\t\tpass\n";
	const auto rtc_sig_report = analyze_source(rtc_sig, "res://tests/rtc_sig_mismatch.barista");
	CHECK_MESSAGE((rtc_sig_report.valid() == false), "extend witness with wrong signature is invalid");
	bool saw_rtc_sig = false;
	for (const auto &error : rtc_sig_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("signature does not match required trait method") && message.contains("NeedsPing.ping()")) {
			saw_rtc_sig = true;
		}
	}
	CHECK_MESSAGE((saw_rtc_sig), "extend witness signature mismatch diagnostic");
	const String native_sig = "class_name NativeSigBad extends Node\nuses NeedsGetNode\n\ntrait NeedsGetNode:\n\tabstract func get_node(path: int) -> Node\n";
	const auto native_sig_report = analyze_source(native_sig, "res://tests/native_sig_bad.barista");
	CHECK_MESSAGE((native_sig_report.valid() == false), "native MethodInfo wrong signature for trait is invalid");
	bool saw_native_sig = false;
	for (const auto &error : native_sig_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("native function") && message.contains("get_node()") && message.contains("NeedsGetNode.get_node()")) {
			saw_native_sig = true;
		}
	}
	CHECK_MESSAGE((saw_native_sig), "native MethodInfo signature mismatch diagnostic");
}

void scenario_self_type_parameter_compat() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String self_echo = "class_name SelfEchoOk extends Node\nfunc echo(value: Self) -> Self:\n\tvar tmp: Self = value\n\treturn tmp\n";
	const auto self_echo_report = analyze_source(self_echo, "res://tests/self_echo_ok.barista");
	CHECK_MESSAGE((self_echo_report.valid() == true), "same-class Self↔Self parameter/return/local is valid");
	const String self_widen = "class_name SelfWidenOk extends Node\nfunc widen(value: Self) -> Self?:\n\treturn value\n";
	const auto self_widen_report = analyze_source(self_widen, "res://tests/self_widen_ok.barista");
	CHECK_MESSAGE((self_widen_report.valid() == true), "Self widens to Self? via TYPE_PARAMETER identity");
	const String self_ok = "class_name CompatTraitSelfOk extends Node\nuses Creatable\n\ntrait Creatable:\n\tabstract static func create() -> Self\n\nstatic func create() -> Self:\n\treturn CompatTraitSelfOk.new()\n";
	const auto self_ok_report = analyze_source(self_ok, "res://tests/compat_trait_self_ok.barista");
	CHECK_MESSAGE((self_ok_report.valid() == true), "trait Self return matching implementer still valid");
	const String self_bad = "class_name CompatTraitSelfBad extends Node\nuses Creatable\n\ntrait Creatable:\n\tabstract static func create() -> Self\n\nstatic func create() -> String:\n\treturn \"x\"\n";
	const auto self_bad_report = analyze_source(self_bad, "res://tests/compat_trait_self_bad.barista");
	CHECK_MESSAGE((self_bad_report.valid() == false), "trait Self return mismatched to String still invalid");
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const auto _ignored = analyze_source("class_name SelfCompatIndexGuard extends Node\n", "res://tests/self_compat_index_guard.barista");
	CHECK_MESSAGE((index.get_record_count() == before), "Self TYPE_PARAMETER compat probes must not mutate declaration index");
	BSCache::clear_source_overrides();
}

} // namespace

TEST_SUITE("analyzer_conformance") {
	TEST_CASE("self_type_parameter_compat") { scenario_self_type_parameter_compat(); }
	TEST_CASE("conformance_scoped_visibility") { scenario_conformance_scoped_visibility(); }
	TEST_CASE("trait_requirements_and_conformance_witness") { scenario_trait_requirements_and_conformance_witness(); }
	TEST_CASE("conformance_registry_registration") { scenario_conformance_registry_registration(); }
	TEST_CASE("conformance_witness_lookup") { scenario_conformance_witness_lookup(); }
	TEST_CASE("conformance_hidden_witness") { scenario_conformance_hidden_witness(); }
	TEST_CASE("class_trait_binding_chain_coherence") { scenario_class_trait_binding_chain_coherence(); }
	TEST_CASE("recorded_trait_arguments_query") { scenario_recorded_trait_arguments_query(); }
	TEST_CASE("witness_collision_arbitration") { scenario_witness_collision_arbitration(); }
	TEST_CASE("complete_self_referential_enum_type") { scenario_complete_self_referential_enum_type(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_conformance_registry_registration, scenario_conformance_witness_lookup, scenario_conformance_hidden_witness, scenario_class_trait_binding_chain_coherence, scenario_recorded_trait_arguments_query, scenario_witness_collision_arbitration, scenario_complete_self_referential_enum_type, scenario_trait_requirements_and_conformance_witness, scenario_conformance_scoped_visibility, scenario_self_type_parameter_compat });
	}
}
