/**************************************************************************/
/*  bs_global_class_probe.cpp                                             */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_global_class_probe.h"

#ifdef DEBUG_ENABLED

#include "barista_script_language.h"
#include "bs_global_class.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace barista_script {

namespace {

Dictionary render(const BSGlobalClass &p_global_class) {
	Dictionary report = p_global_class.to_dictionary();
	// The fields the engine never sees. `kind` is what `is_abstract` and `base_type` were
	// computed from, and stock's class cache has no key to persist it in
	// (docs/foundry-reuse-plan.md section 5.6), so it is reported here and nowhere else.
	report["parsed"] = p_global_class.parsed;
	report["kind"] = (int)p_global_class.kind;
	report["kind_name"] = bs_declaration_kind_name(p_global_class.kind);
	return report;
}

} // namespace

void BaristaScriptGlobalClassProbe::_bind_methods() {
	ClassDB::bind_method(D_METHOD("resolve_path", "path"), &BaristaScriptGlobalClassProbe::resolve_path);
	ClassDB::bind_method(D_METHOD("resolve_source", "source", "path"), &BaristaScriptGlobalClassProbe::resolve_source);
	ClassDB::bind_method(D_METHOD("language_global_class_name", "path"), &BaristaScriptGlobalClassProbe::language_global_class_name);
	ClassDB::bind_method(D_METHOD("language_handles_global_class_type", "type"), &BaristaScriptGlobalClassProbe::language_handles_global_class_type);
	ClassDB::bind_method(D_METHOD("declaration_kind_names"), &BaristaScriptGlobalClassProbe::declaration_kind_names);
	ClassDB::bind_method(D_METHOD("declaration_kind_index_is_valid", "kind"), &BaristaScriptGlobalClassProbe::declaration_kind_index_is_valid);
	ClassDB::bind_method(D_METHOD("declaration_kind_is_instantiable", "kind"), &BaristaScriptGlobalClassProbe::declaration_kind_is_instantiable);
	ClassDB::bind_method(D_METHOD("build_qualified_global_name", "namespace_name", "identifier"), &BaristaScriptGlobalClassProbe::build_qualified_global_name);
}

Dictionary BaristaScriptGlobalClassProbe::resolve_path(const String &p_path) const {
	return render(bs_resolve_global_class(p_path));
}

Dictionary BaristaScriptGlobalClassProbe::resolve_source(const String &p_source, const String &p_path) const {
	return render(bs_resolve_global_class_from_source(p_source, p_path));
}

Dictionary BaristaScriptGlobalClassProbe::language_global_class_name(const String &p_path) const {
	const BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	// An unregistered language is not an empty answer, it is the absence of one. Saying so is the
	// point: a test that read an empty dictionary here would be asserting nothing.
	ERR_FAIL_NULL_V_MSG(language, Dictionary(), "The BaristaScript language is not registered.");
	return language->_get_global_class_name(p_path);
}

bool BaristaScriptGlobalClassProbe::language_handles_global_class_type(const String &p_type) const {
	const BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	ERR_FAIL_NULL_V_MSG(language, false, "The BaristaScript language is not registered.");
	return language->_handles_global_class_type(p_type);
}

PackedStringArray BaristaScriptGlobalClassProbe::declaration_kind_names() const {
	PackedStringArray names;
	for (int kind = 0; kind < (int)BSDeclarationKind::MAX; kind++) {
		names.push_back(bs_declaration_kind_name((BSDeclarationKind)kind));
	}
	return names;
}

bool BaristaScriptGlobalClassProbe::declaration_kind_index_is_valid(int p_kind) const {
	return p_kind >= 0 && p_kind < (int)BSDeclarationKind::MAX;
}

bool BaristaScriptGlobalClassProbe::declaration_kind_is_instantiable(int p_kind) const {
	ERR_FAIL_COND_V_MSG(!declaration_kind_index_is_valid(p_kind), false,
			vformat("%d is not a BSDeclarationKind.", p_kind));
	return bs_declaration_kind_is_instantiable((BSDeclarationKind)p_kind);
}

String BaristaScriptGlobalClassProbe::build_qualified_global_name(const String &p_namespace, const String &p_identifier) const {
	return bs_build_qualified_global_name(p_namespace, StringName(p_identifier));
}

} // namespace barista_script

#endif // DEBUG_ENABLED
