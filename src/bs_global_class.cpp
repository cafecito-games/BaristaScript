/**************************************************************************/
/*  bs_global_class.cpp                                                   */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_global_class.h"

#include "barista_script.h"
#include "bs_parser.h"

namespace barista_script {

namespace {

/**
 * The kind `p_class` declares.
 *
 * A well-formed file matches exactly one arm: `parse_enum_name()` and `parse_tuple_name()` refuse
 * to combine with `class_name` or `trait_name` and report a diagnostic when a source tries
 * (bs_parser.cpp:1711, :1748), and a source that reported anything never reaches here because
 * `resolve()` refuses it. The order is therefore a tie-break that no well-formed input exercises,
 * kept so that a malformed tree still lands on the more restrictive kind.
 */
BSDeclarationKind declaration_kind_of(const BSParser::ClassNode *p_class) {
	if (p_class->is_enum_file) {
		return BSDeclarationKind::ENUM;
	}
	if (p_class->is_tuple_file) {
		return BSDeclarationKind::TUPLE;
	}
	if (p_class->trait_name_used) {
		return BSDeclarationKind::TRAIT;
	}
	if (p_class->identifier != nullptr) {
		// D6: a generic `class_name` is not the engine's to instantiate. See the header.
		return p_class->type_parameters.is_empty() ? BSDeclarationKind::CLASS : BSDeclarationKind::GENERIC_CLASS;
	}
	return BSDeclarationKind::NONE;
}

/**
 * `p_extends_path` as an absolute path, relative to the file that wrote it.
 *
 * Stock resolves a relative `extends` path in one of its two branches and not in the other
 * (`modules/gdscript/gdscript.cpp:2779` at 4.7.2-stable resolves it; the recursion at :2768 passes
 * the raw path), so on the branch that forgot, `extends "sibling.gd"` silently loses its base type.
 * BaristaScript resolves it in both, from here.
 */
String resolve_extends_path(const String &p_owner_path, const String &p_extends_path) {
	if (p_extends_path.is_relative_path()) {
		return p_owner_path.get_base_dir().path_join(p_extends_path).simplify_path();
	}
	return p_extends_path;
}

BSGlobalClass resolve(const String *p_source, const String &p_path, LocalVector<String> &r_visited);

/**
 * The base type of the class `p_head` heads, resolved syntactically.
 *
 * A port of stock's walk (`modules/gdscript/gdscript.cpp:2753-2818` at 4.7.2-stable, identical in
 * Foundry at `foundry_script.cpp:4716-4781` @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6) with two
 * changes: the relative-path fix above, and the visited list consulted on the inner-class branch as
 * well as the recursive one, so a cycle of `extends "…".Inner` paths terminates instead of
 * reparsing the same two files forever.
 *
 * It resolves what it can see and leaves `r_base_type` empty otherwise;
 * `ScriptServer::get_global_class_native_base` walks the rest. The analyzer is not permitted here
 * at any price -- reaching for it deadlocks the editor's first scan, when a file's dependencies may
 * not exist yet.
 */
void resolve_base_type(const BSParser::ClassNode *p_head, const String &p_path, String &r_base_type, LocalVector<String> &r_visited) {
	const BSParser::ClassNode *subclass = p_head;
	String path = p_path;
	BSParser subparser;
	while (subclass != nullptr) {
		if (!subclass->extends_used) {
			r_base_type = "RefCounted";
			return;
		}

		if (subclass->extends_path.is_empty()) {
			if (subclass->extends.size() == 1) {
				r_base_type = subclass->extends[0]->name;
			}
			// `extends A.B` names an inner class of another declaration. Which native class that
			// bottoms out at is an analyzer question, so nothing is reported rather than a guess.
			return;
		}

		const String extends_path = BaristaScript::canonicalize_path(resolve_extends_path(path, subclass->extends_path));
		if (subclass->extends.is_empty()) {
			// `extends "res://base.barista"`: the registry wants the base file's *own* base, which
			// is what stock's discarded recursive call leaves behind in `r_base_type`. The visited
			// list is threaded through, so a cycle of `extends` paths stops there.
			const BSGlobalClass base = resolve(nullptr, extends_path, r_visited);
			if (!base.base_type.is_empty()) {
				r_base_type = base.base_type;
			}
			return;
		}

		// `extends "res://base.barista".Inner`: walk into the named inner class of that file.
		if (r_visited.has(extends_path)) {
			return;
		}
		r_visited.push_back(extends_path);

		Vector<BSParser::IdentifierNode *> extend_classes = subclass->extends;
		const Ref<FileAccess> subfile = FileAccess::open(extends_path, FileAccess::READ);
		if (subfile.is_null()) {
			return;
		}
		const String subsource = subfile->get_as_text();
		if (subsource.is_empty()) {
			return;
		}
		if (subparser.parse(subsource, extends_path, false) != OK) {
			return;
		}
		path = extends_path;
		subclass = subparser.get_tree();
		if (subclass == nullptr) {
			return;
		}

		while (extend_classes.size() > 0) {
			bool found = false;
			for (int i = 0; i < subclass->members.size(); i++) {
				if (subclass->members[i].type != BSParser::ClassNode::Member::CLASS) {
					continue;
				}
				const BSParser::ClassNode *inner_class = subclass->members[i].m_class;
				if (inner_class->identifier != nullptr && inner_class->identifier->name == extend_classes[0]->name) {
					extend_classes.remove_at(0);
					found = true;
					subclass = inner_class;
					break;
				}
			}
			if (!found) {
				return;
			}
		}
	}
}

/** `p_source` is the source text, or `nullptr` to read `p_path` from disk. */
BSGlobalClass resolve(const String *p_source, const String &p_path, LocalVector<String> &r_visited) {
	BSGlobalClass global_class;

	const String canonical_path = BaristaScript::canonicalize_path(p_path);
	if (r_visited.has(canonical_path)) {
		// A cycle of `extends` paths. Stopping here is what makes the walk terminate; the caller
		// keeps whatever it had already resolved, which for a cycle is nothing.
		return global_class;
	}
	r_visited.push_back(canonical_path);

	String source;
	if (p_source != nullptr) {
		source = *p_source;
	} else {
		const Ref<FileAccess> file = FileAccess::open(canonical_path, FileAccess::READ);
		if (file.is_null()) {
			return global_class;
		}
		source = file->get_as_text();
	}

	BSParser parser;
	if (parser.parse(source, canonical_path, false, false) != OK) {
		// Fail closed. See the header: a file that reported a diagnostic may have lost the
		// `namespace` line that qualifies its name, and a class registered under a truncated name
		// is worse than one that is absent until the file compiles. `parse()` returns
		// `ERR_PARSE_ERROR` exactly when it reported something (bs_parser.cpp:609-613), and it
		// reports into its own diagnostic list rather than the engine's error stream, so this path
		// prints nothing.
		return global_class;
	}

	const BSParser::ClassNode *head = parser.get_tree();
	if (head == nullptr) {
		return global_class;
	}

	global_class.parsed = true;
	global_class.kind = declaration_kind_of(head);
	global_class.icon_path = head->simplified_icon_path;
	global_class.is_tool = parser.is_tool();
	// The one place instantiability becomes a reported flag. `Script::is_abstract()` is the gate the
	// editor actually consults -- the cached flag in `global_script_class_cache.cfg` is ignored
	// (docs/namespace-engine-support.md section 5) -- and `BaristaScript::_is_abstract()` answers
	// with this same value rather than deciding again.
	global_class.is_abstract = !bs_declaration_kind_is_instantiable(global_class.kind) || head->is_abstract;

	if (bs_declaration_kind_is_instantiable(global_class.kind)) {
		resolve_base_type(head, canonical_path, global_class.base_type, r_visited);
	}
	// Otherwise the base stays empty: an `enum_name` or `tuple_name` file declares a type and a
	// `trait_name` file a contract, so none of them has a base class.
	// `EditorData::script_class_is_parent` (editor/editor_data.cpp:1038) resolves an empty base to
	// `false`, which is the second of the two gates that keep such a declaration out of the Create
	// Node dialog.

	if (head->identifier != nullptr) {
		global_class.name = bs_build_qualified_global_name(head->namespace_name, head->identifier->name);
	}

	return global_class;
}

} // namespace

String bs_declaration_kind_name(BSDeclarationKind p_kind) {
	switch (p_kind) {
		case BSDeclarationKind::NONE:
			return "none";
		case BSDeclarationKind::CLASS:
			return "class_name";
		case BSDeclarationKind::GENERIC_CLASS:
			return "generic class_name";
		case BSDeclarationKind::TRAIT:
			return "trait_name";
		case BSDeclarationKind::ENUM:
			return "enum_name";
		case BSDeclarationKind::TUPLE:
			return "tuple_name";
		case BSDeclarationKind::MAX:
			break;
	}
	ERR_FAIL_V_MSG(String(), "BSDeclarationKind::MAX is the enumerator count, not a declaration kind.");
}

bool bs_declaration_kind_is_instantiable(BSDeclarationKind p_kind) {
	switch (p_kind) {
		case BSDeclarationKind::NONE:
		case BSDeclarationKind::CLASS:
			return true;
		case BSDeclarationKind::GENERIC_CLASS:
		case BSDeclarationKind::TRAIT:
		case BSDeclarationKind::ENUM:
		case BSDeclarationKind::TUPLE:
			return false;
		case BSDeclarationKind::MAX:
			break;
	}
	ERR_FAIL_V_MSG(false, "BSDeclarationKind::MAX is the enumerator count, not a declaration kind.");
}

String bs_build_qualified_global_name(const String &p_namespace, const StringName &p_identifier) {
	const String identifier = String(p_identifier);
	if (identifier.is_empty()) {
		// No identifier, no name. A namespace on its own is not a global class, and returning the
		// namespace here would register one under a name no source ever wrote.
		return String();
	}
	if (p_namespace.is_empty()) {
		return identifier;
	}
	// The `.` is spelled as a `String` because godot-cpp declares `operator+` for both
	// `String` and `StringName`, so a bare literal is ambiguous rather than missing.
	return p_namespace + String(".") + identifier;
}

Dictionary BSGlobalClass::to_dictionary() const {
	Dictionary reported;
	reported["name"] = name;
	reported["base_type"] = base_type;
	reported["icon_path"] = icon_path;
	reported["is_abstract"] = is_abstract;
	reported["is_tool"] = is_tool;
	return reported;
}

BSGlobalClass bs_resolve_global_class_from_source(const String &p_source, const String &p_path) {
	LocalVector<String> visited;
	return resolve(&p_source, p_path, visited);
}

BSGlobalClass bs_resolve_global_class(const String &p_path) {
	LocalVector<String> visited;
	return resolve(nullptr, p_path, visited);
}

} // namespace barista_script
