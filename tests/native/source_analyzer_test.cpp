/**************************************************************************/
/*  source_analyzer_test.cpp                                              */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_analyzer_probe.h"
#include "bs_conformance_registry.h"
#include "bs_global_class.h"
#include "bs_script_server.h"
#include "storage_fixture.h"
#include "test_require.h"
#include <godot_cpp/classes/project_settings.hpp>
#include <string>
using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;
struct SourceReadTestAccess {
	explicit SourceReadTestAccess(std::function<Error(const String &, PackedByteArray *)> hook) { BSCache::source_read_hook = std::move(hook); }
	~SourceReadTestAccess() { BSCache::source_read_hook = {}; }
};
namespace {
bool seed(StorageFixture &fixture, const String &path, const String &source) {
	BSParser syntax;
	if (syntax.parse(source, path, false) != OK) {
		CHECK_MESSAGE(false, "provider setup failed to parse");
		return false;
	}
	BSCache::set_source_override(path, source);
	auto record = BSDeclarationIndex::record_from_global_class(path, source, bs_resolve_global_class_from_source(source, path));
	record.declares_retroactive_conformances = !syntax.get_tree()->conformances.is_empty();
	return fixture.index().commit_record(fixture.index().claim_refresh(path), record);
}
void diagnostic(const BSParser &parser, const String &message, const BSParser::Node *site) {
	for (const auto &error : parser.get_errors())
		INFO(std::string(error.message.utf8().get_data()));
	BS_TEST_REQUIRE(parser.get_errors().size() == 1);
	const auto &error = parser.get_errors().front()->get();
	CHECK(std::string(error.message.utf8().get_data()) == std::string(message.utf8().get_data()));
	CHECK(error.line == site->start_line);
	CHECK(error.column == site->start_column);
	CHECK(error.end_line == site->end_line);
	CHECK(error.end_column == site->end_column);
}
struct WarningScope {
	String path = BSWarning::get_setting_path_from_code(BSWarning::MIXED_NAMESPACE_DIRECTORY);
	Variant previous = ProjectSettings::get_singleton()->get_setting(path);
	WarningScope(int level) {
		ProjectSettings::get_singleton()->set_setting(path, level);
		BSParser::update_project_settings();
	}
	~WarningScope() {
		ProjectSettings::get_singleton()->set_setting(path, previous);
		BSParser::update_project_settings();
	}
};
} //namespace
TEST_SUITE("source_analyzer") {
	TEST_CASE("missing_hard_metatype_static_call_uses_pinned_call_span") {
		StorageFixture fixture;
		for (bool named : { false, true }) {
			const String path = "res://tests/source/empty.barista";
			BSCache::remove_script(path);
			BS_TEST_REQUIRE(seed(fixture, path, named ? "class_name Empty\n" : ""));
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("const P = preload(\"empty.barista\")\nvar value: int = P.value()\n", "res://tests/source/consumer.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostic(parser, named ? "Static function \"value()\" not found in base \"Empty\"." : "Static function \"value()\" not found in base \"empty.barista\".", parser.get_tree()->get_member("value").variable->initializer);
		}
	}
	TEST_CASE("ordinary_language_and_script_server_lookup_reject_without_repair") {
		StorageFixture fixture;
		const String path = "res://tests/source/provider.barista";
		const String original = "class_name Provider extends Node\n";
		BS_TEST_REQUIRE(seed(fixture, path, original));
		const uint64_t revision = fixture.index().get_refresh_revision(path);
		CHECK(fixture.index().flush(fixture.path("readonly.index")) == OK);
		const auto before = read_bytes(fixture.path("readonly.index"));
		BSCache::set_source_override(path, "class_name Renamed extends RefCounted\n");
		BSDeclarationRecord record;
		CHECK_FALSE(BaristaScriptLanguage::get_singleton()->try_resolve_declaration("Provider", record));
		CHECK_FALSE(ScriptServer::is_global_class("Provider"));
		CHECK(ScriptServer::get_global_class_path("Provider").is_empty());
		CHECK(ScriptServer::get_global_class_native_base("Provider") == StringName());
		CHECK(fixture.index().get_refresh_revision(path) == revision);
		BS_TEST_REQUIRE(fixture.index().try_get_by_path(path, record));
		CHECK(record.qualified_name == "Provider");
		CHECK(record.source_digest == BSDeclarationIndex::compute_source_digest(original));
		CHECK(fixture.index().flush(fixture.path("readonly.index")) == OK);
		CHECK(read_bytes(fixture.path("readonly.index")) == before);
	}
	TEST_CASE("typed_source_rejects_malformed_utf8_instead_of_replacement_text") {
		StorageFixture fixture;
		const String path = fixture.path("malformed.barista");
		auto malformed = bytes("# bad ");
		malformed.push_back(0xff);
		malformed.push_back('\n');
		BS_TEST_REQUIRE(write_bytes(path, malformed));
		const auto read = BSCache::read_source_code(path);
		CHECK(read.error == ERR_INVALID_DATA);
		CHECK(read.source.is_empty());
		Error error = OK;
		auto parser = BSCache::get_parser(path, BSParserRef::PARSED, error);
		CHECK(error != OK);
	}
	TEST_CASE("public_source_entry_points_restore_empty_and_nonempty_prior_input") {
		StorageFixture fixture;
		const String path = "res://tests/source/input.barista";
		const String source = "class_name Input\nconst P = preload(\"input.barista\")\nclass Nested:\n\tvar value: int\nvar item: P.Nested\n";
		Ref<BaristaScriptAnalyzerProbe> probe;
		probe.instantiate();
		for (const String &previous : { String(), String("class_name Previous\n") }) {
			for (int api : { 0, 1, 2 }) {
				BSCache::clear();
				BSCache::set_source_override(path, previous);
				if (api == 0)
					CHECK(bool(probe->analyze_source(source, path).get("valid", false)));
				if (api == 1)
					CHECK(bool(BaristaScriptLanguage::get_singleton()->_validate(source, path, true, true, true, true).get("valid", false)));
				if (api == 2) {
					Ref<BaristaScript> script;
					script.instantiate();
					script->_set_source_code(source);
					script->set_path(path);
					CHECK(script->_is_valid());
				}
				CHECK(BSCache::has_source_override(path));
				if (BSCache::has_source_override(path))
					CHECK(BSCache::get_source_code(path) == previous);
			}
		}
	}
	TEST_CASE("public_empty_and_invalid_input_restore_exact_prior_presence") {
		StorageFixture fixture;
		const String path = "res://tests/source/buffer.barista";
		Ref<BaristaScriptAnalyzerProbe> probe;
		probe.instantiate();
		for (int prior : { 0, 1, 2 })
			for (bool valid : { false, true })
				for (int api : { 0, 1, 2 }) {
					BSCache::clear();
					const String previous = prior == 2 ? String("class_name Previous\n") : String();
					if (prior != 0)
						BSCache::set_source_override(path, previous);
					const String input = valid ? String() : String("var value: Missing\n");
					bool result = false;
					if (api == 0)
						result = probe->analyze_source(input, path).get("valid", false);
					if (api == 1)
						result = BaristaScriptLanguage::get_singleton()->_validate(input, path, true, true, true, true).get("valid", false);
					if (api == 2) {
						Ref<BaristaScript> script;
						script.instantiate();
						script->_set_source_code(input);
						script->set_path(path);
						result = script->_is_valid();
					}
					CHECK(result == valid);
					CHECK(BSCache::has_source_override(path) == (prior != 0));
					if (prior != 0)
						CHECK(BSCache::get_source_code(path) == previous);
				}
	}

	TEST_CASE("mixed_directory_warning_uses_sorted_fresh_namespace_snapshot") {
		StorageFixture fixture;
		WarningScope warnings(BSWarning::WARN);
		BS_TEST_REQUIRE(seed(fixture, "res://tests/source/z.barista", "namespace zeta\nclass_name Zed\n"));
		BS_TEST_REQUIRE(seed(fixture, "res://tests/source/global.barista", "class_name Global\n"));
		BS_TEST_REQUIRE(seed(fixture, "res://tests/source/z2.barista", "namespace zeta\nclass_name DuplicateNamespace\n"));
		BS_TEST_REQUIRE(seed(fixture, "res://tests/elsewhere/u.barista", "namespace unrelated\nclass_name Unrelated\n"));
		BSParser parser;
		BS_TEST_REQUIRE(parser.parse("namespace alpha\nclass_name Alpha\n", "res://tests/source/alpha.barista", false) == OK);
		BSAnalyzer analyzer(&parser);
		CHECK(analyzer.analyze() == OK);
		int count = 0;
		for (const auto &warning : parser.get_warnings()) {
			if (warning.code != BSWarning::MIXED_NAMESPACE_DIRECTORY)
				continue;
			++count;
			CHECK(warning.get_message() == "Directory \"res://tests/source\" contains global script classes from mixed namespaces: \"<global>\", \"alpha\", and \"zeta\".");
			CHECK(warning.start_line == parser.get_tree()->start_line);
			CHECK(warning.start_column == parser.get_tree()->start_column);
			CHECK(warning.end_line == parser.get_tree()->end_line);
			CHECK(warning.end_column == parser.get_tree()->end_column);
		}
		CHECK(count == 1);
	}
	TEST_CASE("new_acquisitions_reject_changed_source_but_retained_generation_remains_usable") {
		StorageFixture fixture;
		const String path = "res://tests/source/generation.barista";
		const String old_source = "class_name Generation\nvar value: int\n";
		BS_TEST_REQUIRE(seed(fixture, path, old_source));
		BSParser owner;
		BS_TEST_REQUIRE(owner.parse("", "res://tests/source/old_owner.barista", false) == OK);
		auto old = owner.get_depended_parser_for(path);
		BS_TEST_REQUIRE(old.is_valid() && old->raise_status(BSParserRef::INTERFACE_SOLVED) == OK);
		BSCache::set_source_override(path, "class_name Generation\nvar value: String\n");
		Error error = OK;
		auto fresh = BSCache::get_parser(path, BSParserRef::PARSED, error);
		CHECK(error != OK);
		CHECK(fresh.is_null());
		CHECK(owner.get_depended_parser_for(path) == old);
		CHECK(old->get_parser()->analyzed_source == old_source);
		CHECK(old->get_parser()->get_tree()->get_member("value").get_datatype().builtin_type == Variant::INT);
		BSParser consumer;
		BS_TEST_REQUIRE(consumer.parse("const P = preload(\"generation.barista\")\n", "res://tests/source/new_owner.barista", false) == OK);
		BSAnalyzer analyzer(&consumer);
		CHECK(analyzer.analyze() != OK);
		diagnostic(consumer, "Could not preload resource script \"res://tests/source/generation.barista\".", static_cast<BSParser::PreloadNode *>(consumer.get_tree()->get_member("P").constant->initializer)->path);
		BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, "class_name Generation\nvar value: String\n") == OK);
		fresh = BSCache::get_parser(path, BSParserRef::INTERFACE_SOLVED, error);
		BS_TEST_REQUIRE(error == OK && fresh.is_valid());
		CHECK(fresh != old);
		CHECK(fresh->get_parser()->get_tree()->get_member("value").get_datatype().builtin_type == Variant::STRING);
	}
	TEST_CASE("public_self_input_does_not_consume_a_different_cached_source") {
		StorageFixture fixture;
		const String path = "res://tests/source/input.barista";
		const String previous = "class_name Previous\n";
		const String source = "class_name Input\nconst P = preload(\"input.barista\")\nclass Nested:\n\tvar value: int\nvar item: P.Nested\n";
		BSCache::set_source_override(path, previous);
		Error error = OK;
		auto old = BSCache::get_parser(path, BSParserRef::INHERITANCE_SOLVED, error);
		BS_TEST_REQUIRE(old.is_valid() && error == OK);
		CHECK(bool(BaristaScriptLanguage::get_singleton()->_validate(source, path, true, true, true, true).get("valid", false)));
		Ref<BaristaScriptAnalyzerProbe> probe;
		probe.instantiate();
		CHECK(bool(probe->analyze_source(source, path).get("valid", false)));
		Ref<BaristaScript> script;
		script.instantiate();
		script->_set_source_code(source);
		script->set_path(path);
		CHECK(script->_is_valid());
		CHECK(BSCache::get_source_code(path) == previous);
		CHECK(old->get_parser()->analyzed_source == previous);
	}
	TEST_CASE("static_lookup_keeps_admitted_methods_and_dynamic_receivers") {
		StorageFixture fixture;
		const String path = "res://tests/source/methods.barista";
		BS_TEST_REQUIRE(seed(fixture, path, "class_name Methods\nstatic func value() -> int:\n\treturn 1\nfunc instance() -> int:\n\treturn 2\n"));
		for (const String &source : {
					 String("const P = preload(\"methods.barista\")\nvar result: int = P.value()\n"),
					 String("var instance: Methods\nvar result: int = instance.instance()\n"),
					 String("var dynamic: Variant\nvar result = dynamic.missing()\n"),
					 String("var callback: Callable\nvar result = callback.call()\n") }) {
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(source, "res://tests/source/controls.barista", false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() == OK);
			CHECK(parser.get_errors().is_empty());
		}
	}
	TEST_CASE("read_only_rejection_matrix_preserves_metadata_and_recovers_explicitly") {
		for (int state : { 0, 1, 2, 3, 4 }) {
			StorageFixture fixture;
			const String path = "res://tests/source/readonly.barista";
			const String original = "class_name ReadOnly\nvar count: int\n";
			const String input = "var value: ReadOnly\n";
			const String consumer_path = "res://tests/source/consumer.barista";
			BS_TEST_REQUIRE(seed(fixture, path, original));
			Ref<BaristaScriptAnalyzerProbe> probe;
			probe.instantiate();
			BS_TEST_REQUIRE(bool(probe->analyze_source(input, consumer_path).get("valid", false)));
			String changed = state == 0 ? "class_name ReadOnly\nvar count: String\n" : state == 1 ? "class_name Renamed\n"
					: state == 2																  ? "trait_name ReadOnly\n"
																								  : "";
			if (state == 4)
				BSCache::clear_source_override(path);
			else
				BSCache::set_source_override(path, changed);
			// Name and kind must be checked even when the stored digest matches new bytes.
			if (state == 1 || state == 2) {
				BSDeclarationRecord record;
				BS_TEST_REQUIRE(fixture.index().try_get_by_path(path, record));
				record.source_digest = BSDeclarationIndex::compute_source_digest(changed);
				BS_TEST_REQUIRE(fixture.index().commit_record(fixture.index().claim_refresh(path), record));
			}
			const auto revision = fixture.index().get_refresh_revision(path);
			BS_TEST_REQUIRE(fixture.index().flush(fixture.path("before.index")) == OK);
			const auto before = read_bytes(fixture.path("before.index"));
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse(input, consumer_path, false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostic(parser, "Could not resolve type \"ReadOnly\": declaration \"ReadOnly\" from \"res://tests/source/readonly.barista\" is stale or invalid.", parser.get_tree()->get_member("value").variable->datatype_specifier);
			for (bool repaired : { false, true }) {
				if (repaired) {
					BSCache::set_source_override(path, original);
					BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, original) == OK);
				}
				BSCache::set_source_override(consumer_path, "");
				CHECK(bool(probe->analyze_source(input, consumer_path).get("valid", false)) == repaired);
				CHECK(bool(BaristaScriptLanguage::get_singleton()->_validate(input, consumer_path, true, true, true, true).get("valid", false)) == repaired);
				Ref<BaristaScript> resource;
				resource.instantiate();
				resource->_set_source_code(input);
				resource->set_path(consumer_path);
				CHECK(resource->_is_valid() == repaired);
				CHECK(BSCache::has_source_override(consumer_path));
				CHECK(BSCache::get_source_code(consumer_path).is_empty());
				if (!repaired) {
					CHECK(fixture.index().get_refresh_revision(path) == revision);
					BS_TEST_REQUIRE(fixture.index().flush(fixture.path("after.index")) == OK);
					CHECK(read_bytes(fixture.path("after.index")) == before);
				}
			}
		}
	}
	TEST_CASE("bad_conformance_replays_both_paths_and_repairs_both_consumers") {
		StorageFixture fixture;
		BSConformanceRegistry::ScopedCorpusState registry;
		const String path = "res://tests/source/conformance.barista";
		const String bad = "class_name Conformance\nextend int uses MissingTrait:\n\tfunc marker() -> int:\n\t\treturn 1\n";
		BS_TEST_REQUIRE(seed(fixture, path, bad));
		for (bool repaired : { false, true }) {
			if (repaired) {
				const String good = "class_name Conformance\ntrait Marker:\n\tabstract func marker() -> int\nextend int uses Marker:\n\tfunc marker() -> int:\n\t\treturn 1\n";
				BSCache::set_source_override(path, good);
				BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, good) == OK);
			}
			for (int dependent : { 0, 1 }) {
				BSParser parser;
				BS_TEST_REQUIRE(parser.parse("const A = preload(\"conformance.barista\")\nconst B = preload(\"conformance.barista\")\n", "res://tests/source/consumer" + itos(dependent) + ".barista", false) == OK);
				BSAnalyzer analyzer(&parser);
				CHECK((analyzer.analyze() == OK) == repaired);
				if (repaired)
					CHECK(parser.get_errors().is_empty());
				else {
					BS_TEST_REQUIRE(parser.get_errors().size() == 2);
					const auto ordered = parser.get_errors_in_source_order();
					for (int i = 0; i < 2; i++) {
						const auto *site = static_cast<BSParser::PreloadNode *>(parser.get_tree()->get_member(i == 0 ? "A" : "B").constant->initializer)->path;
						CHECK(ordered[i]->message == "Could not preload resource script \"res://tests/source/conformance.barista\".");
						CHECK(ordered[i]->line == site->start_line);
						CHECK(ordered[i]->column == site->start_column);
						CHECK(ordered[i]->end_line == site->end_line);
						CHECK(ordered[i]->end_column == site->end_column);
					}
				}
			}
		}
	}
	TEST_CASE("namespace_warning_modes_and_fresh_replacements") {
		StorageFixture fixture;
		const String path = "res://tests/source/other.barista";
		const String message = "Directory \"res://tests/source\" contains global script classes from mixed namespaces: \"alpha\" and \"zeta\".";
		for (int level : { int(BSWarning::WARN), int(BSWarning::IGNORE), int(BSWarning::ERROR) }) {
			WarningScope policy(level);
			for (int round : { 0, 1, 2 }) {
				const String other = round == 0 ? "namespace zeta\nclass_name Other\n" : round == 1 ? "namespace alpha\nclass_name Other\n"
																									: "namespace zeta\n";
				BSCache::set_source_override(path, other);
				BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, other) == OK);
				BSParser parser;
				BS_TEST_REQUIRE(parser.parse("namespace alpha\nclass_name Current\n", "res://tests/source/current.barista", false) == OK);
				BSAnalyzer analyzer(&parser);
				const bool hard_error = level == BSWarning::ERROR && round == 0;
				CHECK((analyzer.analyze() != OK) == hard_error);
				int count = 0;
				for (const auto &warning : parser.get_warnings())
					if (warning.code == BSWarning::MIXED_NAMESPACE_DIRECTORY) {
						count++;
						CHECK(warning.get_message() == message);
					}
				CHECK(count == (level == BSWarning::WARN && round == 0 ? 1 : 0));
				if (hard_error)
					diagnostic(parser, message + String(" (Warning treated as error.)"), parser.get_tree());
			}
		}
	}
	TEST_CASE("indexed_and_preloaded_enum_tuple_edits_change_verdict_and_repair") {
		for (bool tuple : { false, true })
			for (bool preload : { false, true }) {
				StorageFixture fixture;
				const String path = "res://tests/source/packet.barista";
				const String original = tuple ? (preload ? "class_name Container\ntuple Packet(value: int, count: int)\n" : "tuple_name Packet(value: int, count: int)\n") : "enum_name Packet:\n\tEmpty\n\tValue(value: int)\n";
				const String changed = tuple ? (preload ? "class_name Container\ntuple Packet(value: String, count: int)\n" : "tuple_name Packet(value: String, count: int)\n") : "enum_name Packet:\n\tEmpty\n\tValue(value: String)\n";
				BS_TEST_REQUIRE(seed(fixture, path, original));
				const String input = (preload ? String("const P = preload(\"packet.barista\")\n") : String()) + "var packet := " + (preload ? String("P.") : String()) + (tuple ? "Packet(1, 2)\n" : "Packet.Value(1)\n");
				BSParser retained;
				BS_TEST_REQUIRE(retained.parse(input, "res://tests/source/old.barista", false) == OK);
				BSAnalyzer old_analyzer(&retained);
				BS_TEST_REQUIRE(old_analyzer.analyze() == OK);
				BS_TEST_REQUIRE(retained.get_depended_parsers().has(path));
				auto old = retained.get_depended_parsers()[path];
				for (bool repaired : { false, true }) {
					const String current = repaired ? original : changed;
					BSCache::set_source_override(path, current);
					BS_TEST_REQUIRE(BaristaScriptLanguage::get_singleton()->synchronize_declaration_path_from_source(path, current) == OK);
					BSParser consumer;
					BS_TEST_REQUIRE(consumer.parse(input, "res://tests/source/current.barista", false) == OK);
					BSAnalyzer analyzer(&consumer);
					CHECK((analyzer.analyze() == OK) == repaired);
					if (!repaired) {
						const auto *call = static_cast<BSParser::CallNode *>(consumer.get_tree()->get_member("packet").variable->initializer);
						BS_TEST_REQUIRE(!call->arguments.is_empty());
						diagnostic(consumer, tuple ? "Invalid argument 1 for tuple \"Packet\": should be \"String\" but is \"int\"." : "Invalid argument 1 for enum case \"Packet.Value\": should be \"String\" but is \"int\".", call->arguments[0]);
					} else
						CHECK(consumer.get_errors().is_empty());
					CHECK(old->get_parser()->analyzed_source == original);
				}
			}
	}
	TEST_CASE("typed_reads_distinguish_empty_overrides_open_failure_and_short_read") {
		StorageFixture fixture;
		const String path = fixture.path("source.barista");
		const auto assert_read = [&](Error expected, const String &source) {
			const auto read = BSCache::read_source_code(path);
			CHECK(read.error == expected);
			CHECK(read.source == source);
		};
		BS_TEST_REQUIRE(write_bytes(path, bytes("class_name Disk\n")));
		BSCache::set_source_override(path, "");
		assert_read(OK, "");
		Error error = OK;
		auto empty = BSCache::get_parser(path, BSParserRef::PARSED, error);
		BS_TEST_REQUIRE(error == OK && empty.is_valid());
		CHECK(empty->get_parser()->analyzed_source.is_empty());
		BSCache::clear_source_override(path);
		BSCache::remove_parser(path);
		assert_read(OK, "class_name Disk\n");
		{
			// Deterministic permission failure exercises acquisition independently of host UID.
			SourceReadTestAccess fault([&](const String &candidate, PackedByteArray *buffer) { return candidate == path && buffer == nullptr ? ERR_FILE_NO_PERMISSION : OK; });
			assert_read(ERR_FILE_NO_PERMISSION, "");
			auto denied = BSCache::get_parser(path, BSParserRef::PARSED, error);
			CHECK(error == ERR_FILE_NO_PERMISSION);
			BSParser parser;
			BS_TEST_REQUIRE(parser.parse("const P = preload(\"" + path + "\")\n", fixture.path("consumer.barista"), false) == OK);
			BSAnalyzer analyzer(&parser);
			CHECK(analyzer.analyze() != OK);
			diagnostic(parser, "Could not preload resource script \"" + path + "\".", static_cast<BSParser::PreloadNode *>(parser.get_tree()->get_member("P").constant->initializer)->path);
		}
		BSCache::remove_parser(path);
		{
			// Inject a short FileAccess buffer; filesystem stdio can retain prefetched bytes after truncation.
			SourceReadTestAccess fault([&](const String &candidate, PackedByteArray *buffer) { if (candidate == path && buffer != nullptr) buffer->resize(0); return OK; });
			const auto read = BSCache::read_source_code(path);
			CHECK(read.error == ERR_FILE_CORRUPT);
			CHECK(read.source.is_empty());
			CHECK(read.error_message == "Script '" + path + "' was truncated while being read.");
		}
		BS_TEST_REQUIRE(write_bytes(path, {}));
		assert_read(OK, "");
		empty = BSCache::get_parser(path, BSParserRef::PARSED, error);
		BS_TEST_REQUIRE(error == OK && empty.is_valid());
		CHECK(empty->get_parser()->analyzed_source.is_empty());
		const auto missing = BSCache::read_source_code(fixture.path("missing.barista"));
		CHECK(missing.error != OK);
		CHECK(missing.source.is_empty());
	}
}
