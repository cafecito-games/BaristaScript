/**************************************************************************/
/*  bs_corpus_evaluation.cpp                                              */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_corpus_evaluation.h"

#ifdef DEBUG_ENABLED

#include "barista_script_language.h"
#include "bs_analyzer.h"
#include "bs_cache.h"
#include "bs_conformance_registry.h"
#include "bs_corpus_sentinels.h"
#include "bs_declaration_index.h"
#include "bs_global_class.h"
#include "bs_parser.h"
#include "bs_tokenizer.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>

using namespace godot;

namespace barista_script {

namespace {
// Synchronous corpus evaluation never yields with this process-global profile installed.
class CorpusWarningProfile {
	struct Setting {
		String path;
		bool present;
		Variant value;
	};
	Vector<Setting> saved;
	ProjectSettings *settings = ProjectSettings::get_singleton();
	void remember(const String &p_path) {
		Setting entry;
		entry.path = p_path;
		entry.present = settings->has_setting(p_path);
		entry.value = entry.present ? settings->get_setting(p_path) : Variant();
		if (entry.value.get_type() == Variant::DICTIONARY) {
			entry.value = Dictionary(entry.value).duplicate(true);
		}
		if (entry.value.get_type() == Variant::ARRAY) {
			entry.value = Array(entry.value).duplicate(true);
		}
		saved.push_back(entry);
	}

public:
	CorpusWarningProfile() {
		HashMap<String, Variant> profile;
		profile["debug/barista_script/warnings/enable"] = true;
		profile["debug/barista_script/warnings/directory_rules"] = Dictionary();
		profile["debug/barista_script/analysis/strict_null_checks"] = false;
		profile["debug/barista_script/analysis/strict_dynamic_checks"] = false;
		for (int code = 0; code < BSWarning::WARNING_MAX; code++) {
			profile[BSWarning::get_setting_path_from_code((BSWarning::Code)code)] =
					code == BSWarning::UNTYPED_DECLARATION || code == BSWarning::INFERRED_DECLARATION ? BSWarning::IGNORE : BSWarning::WARN;
		}
		const Array properties = settings->get_property_list();
		for (int i = 0; i < properties.size(); i++) {
			const String path = Dictionary(properties[i])["name"];
			for (const KeyValue<String, Variant> &entry : profile) {
				if (path.begins_with(entry.key + String("."))) {
					remember(path);
					settings->clear(path);
					break;
				}
			}
		}
		for (const KeyValue<String, Variant> &entry : profile) {
			remember(entry.key);
			settings->set_setting(entry.key, entry.value);
		}
		BSParser::update_project_settings();
	}
	~CorpusWarningProfile() {
		for (const Setting &entry : saved) {
			if (entry.present) {
				settings->set_setting(entry.path, entry.value);
			} else {
				settings->clear(entry.path);
			}
		}
		BSParser::update_project_settings();
		// Refresh declares missing base warning settings; restore their absence afterward.
		for (const Setting &entry : saved) {
			if (!entry.present && settings->has_setting(entry.path)) {
				settings->clear(entry.path);
			}
		}
	}
};

CorpusResult corpus_result(const String &p_output, bool p_ok, bool p_analysis_ran, bool p_infrastructure = false) {
	CorpusResult result;
	result.output = p_output;
	result.ok = p_ok;
	result.analysis_ran = p_analysis_ran;
	result.infrastructure_error = p_infrastructure;
	return result;
}
} // namespace

CorpusResult format_corpus_result(const BSParser &p_parser, Error p_error, bool p_analysis_ran) {
	PackedStringArray lines;
	if (!p_parser.get_errors().is_empty()) {
		if (!p_analysis_ran) {
			return corpus_result(p_parser.get_errors().front()->get().message, false, false);
		}
		for (const BSParser::ParserError *error : p_parser.get_errors_in_source_order()) {
			lines.push_back(vformat(">> ERROR at line %d: %s", error->line, error->message));
		}
		return corpus_result(String("\n").join(lines), false, true);
	}
	if (p_error != OK) {
		return corpus_result("Frontend failed without a diagnostic.", false, p_analysis_ran, true);
	}
	if (p_analysis_ran) {
		for (const BSWarning &warning : p_parser.get_warnings()) {
			lines.push_back(vformat("~~ WARNING at line %d: (%s) %s", warning.start_line, warning.get_name(), warning.get_message()));
		}
	}
	return corpus_result(lines.is_empty() ? String(BaristaScriptCorpusSentinels::SUCCESS_SENTINEL) : String("\n").join(lines), true, p_analysis_ran);
}

CorpusResult evaluate_corpus_case(const PackedByteArray &p_bytes, const String &p_path, const String &p_stage, const PackedStringArray &p_fixture_paths) {
	if ((p_stage != "parser" && p_stage != "analyzer") || !p_path.begins_with("res://") || p_path != p_path.simplify_path() || !p_path.ends_with(".barista")) {
		return corpus_result("Invalid corpus stage or res:// case path: " + p_path, false, false, true);
	}
	String source;
	String diagnostic;
	if (!BSTokenizer::decode_source(p_bytes, &source, &diagnostic)) {
		return corpus_result(diagnostic, false, false);
	}
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	if (language == nullptr || ProjectSettings::get_singleton() == nullptr) {
		return corpus_result("Missing frontend language/settings.", false, false, true);
	}
	CorpusWarningProfile profile;
	BSDeclarationIndex::ScopedCorpusState declarations(language->get_declaration_index());
	BSConformanceRegistry::ScopedCorpusState conformances;
	BSCache::ScopedCorpusState cache;
	int fixture_heads = 0;
	int fixture_annotation_providers = 0;
	int fixture_conformance_providers = 0;
	// Fixture discovery publishes only production declaration heads into the
	// existing scoped index. Full namespace identities remain distinct; no
	// ScriptServer global-class table, semantic lookup or script body execution.
	if (!p_fixture_paths.is_empty()) {
		// This is the temporary index selected by ScopedCorpusState, never the
		// ambient editor index. Fixture-only lookup must not inherit editor heads.
		language->get_declaration_index().clear();
	}
	HashMap<String, String> overrides;
	for (int i = 0; i < p_fixture_paths.size(); i++) {
		const String path = p_fixture_paths[i];
		if (!path.begins_with("res://") || path != path.simplify_path() || !path.ends_with(".barista") || (i > 0 && path <= p_fixture_paths[i - 1])) {
			return corpus_result("Invalid/unsorted corpus fixture path: " + path, false, false, true);
		}
		const Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
		if (file.is_null()) {
			return corpus_result("Unreadable corpus fixture source: " + path, false, false, true);
		}
		String fixture_source;
		if (!BSTokenizer::decode_source(file->get_buffer(file->get_length()), &fixture_source, &diagnostic)) {
			return corpus_result("Invalid corpus fixture source: " + path, false, false, true);
		}
		overrides[path] = fixture_source;
	}
	overrides[p_path] = source;
	BSCacheSourceOverrideGuard override_guard(overrides);
	for (int i = 0; i < p_fixture_paths.size(); i++) {
		const String path = p_fixture_paths[i];
		const String fixture_source = overrides[path];
		const BSGlobalClass head = bs_resolve_global_class_from_source(fixture_source, path);
		BSParser declaration_parser;
		const Error parsed = declaration_parser.parse(fixture_source, path, false);
		const BSParser::ClassNode *tree = declaration_parser.get_tree();
		if ((head.declarations_parsed && !head.name.is_empty()) || (parsed == OK && tree != nullptr)) {
			BSDeclarationIndex &index = language->get_declaration_index();
			BSDeclarationRecord record = BSDeclarationIndex::record_from_global_class(path, fixture_source, head);
			// The pinned producer indexes these declaration-only sources even
			// without a named head. This copies AST declaration identities; it
			// neither resolves annotations nor registers semantic conformances.
			if (parsed == OK && tree != nullptr) {
				record.namespace_name = tree->namespace_name;
				record.declares_retroactive_conformances = !tree->conformances.is_empty();
				for (const BSParser::AnnotationDeclarationNode *annotation : tree->annotation_declarations) {
					if (annotation != nullptr && annotation->identifier != nullptr) {
						record.global_annotations.push_back(annotation->qualified_name);
					}
				}
			}
			if (!index.commit_record(index.claim_refresh(path), record)) {
				return corpus_result("Corpus declaration fixture registration rejected: " + path, false, false, true);
			}
			fixture_heads += int(record.has_head_declaration());
			fixture_annotation_providers += int(!record.global_annotations.is_empty());
			fixture_conformance_providers += int(record.declares_retroactive_conformances);
		}
	}
	// Applied only at the terminal returns: every infrastructure error above this point
	// must report no counts at all, including one raised partway through fixture discovery.
	auto with_fixture_index = [&](CorpusResult p_result) {
		p_result.fixture_index_reported = !p_fixture_paths.is_empty();
		p_result.fixture_sources = p_fixture_paths.size();
		p_result.fixture_heads = fixture_heads;
		p_result.fixture_annotation_providers = fixture_annotation_providers;
		p_result.fixture_conformance_providers = fixture_conformance_providers;
		return p_result;
	};
	BSParser parser;
	Error error = parser.parse(source, p_path, false);
	if (error != OK || !parser.get_errors().is_empty() || p_stage == "parser") {
		return with_fixture_index(format_corpus_result(parser, error, false));
	}
	BSAnalyzer analyzer(&parser);
	error = analyzer.analyze();
	return with_fixture_index(format_corpus_result(parser, error, true));
}

} // namespace barista_script

#endif // DEBUG_ENABLED
