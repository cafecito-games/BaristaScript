/**************************************************************************/
/*  bs_analyzer_finalization.cpp                                         */
/*                                                                        */
/*  Hard fork of Foundry `modules/foundry_script/                         */
/*  fs_analyzer_finalization.cpp` @                                      */
/*  c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6. FS* -> BS*; engine contact */
/*  through bs_platform.h. Carries pending-warning application and the   */
/*  depended-parser inheritance finalization loop.                       */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"

#include "barista_script.h"
#include "bs_cache.h"
#include "bs_script_server.h"

namespace barista_script {

#ifdef DEBUG_ENABLED
void BSAnalyzer::validate_mixed_namespace_directory() {
	const BSParser::ClassNode *head = parser->get_tree();
	if (head == nullptr || head->get_global_name() == StringName() || parser->script_path.is_empty())
		return;
	const String current_path = BaristaScript::canonicalize_path(parser->script_path);
	const String directory = current_path.get_base_dir();
	if (directory.is_empty())
		return;
	Vector<String> namespaces;
	const auto add_namespace = [&](const String &p_namespace) {
		const String label = p_namespace.is_empty() ? String("<global>") : p_namespace;
		if (!namespaces.has(label))
			namespaces.push_back(label);
	};
	add_namespace(head->namespace_name);
	HashSet<String> indexed_paths;
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	if (language != nullptr) {
		// A fresh metadata snapshot, including anonymous replacements, shadows stale engine rows.
		for (const BSDeclarationRecord &record : language->get_declaration_index().get_records()) {
			const String path = BaristaScript::canonicalize_path(record.path);
			indexed_paths.insert(path);
			if (path != current_path && path.get_base_dir() == directory && !record.qualified_name.is_empty())
				add_namespace(record.namespace_name);
		}
	}
	for (const Dictionary &entry : ScriptServer::_engine_global_class_list()) {
		const String path = BaristaScript::canonicalize_path(entry.get("path", String()));
		const String name = entry.get("class", String());
		if (name.is_empty() || path == current_path || path.get_base_dir() != directory || indexed_paths.has(path))
			continue;
		String name_space;
		ScriptServer::get_global_class_name_parts(name, nullptr, &name_space);
		add_namespace(name_space);
	}
	if (namespaces.size() < 2)
		return;
	namespaces.sort();
	String names;
	for (int i = 0; i < namespaces.size(); i++) {
		if (i > 0)
			names += i == namespaces.size() - 1 ? (namespaces.size() == 2 ? " and " : ", and ") : ", ";
		names += "\"" + namespaces[i] + "\"";
	}
	parser->push_warning(head, BSWarning::MIXED_NAMESPACE_DIRECTORY, directory, names);
}
#endif

Error BSAnalyzer::run_phase_finalize() {
#ifdef DEBUG_ENABLED
	validate_mixed_namespace_directory();
	parser->apply_pending_warnings();
#endif
	for (const KeyValue<String, Ref<BSParserRef>> &dependency : parser->get_depended_parsers()) {
		if (dependency.value.is_null()) {
			return ERR_PARSE_ERROR;
		}
		// Foundry finalizes every dependency's inheritance surface even when the use site only
		// needed it parsed (for example a standalone preload expression).
		dependency.value->raise_status(BSParserRef::INHERITANCE_SOLVED);
	}
	mark_phase(AnalyzerPhase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

} // namespace barista_script
