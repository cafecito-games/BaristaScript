/**************************************************************************/
/*  bs_global_class.h                                                     */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_platform.h"

namespace barista_script {

/**
 * What a `.barista` file's head declaration is, as one closed vocabulary.
 *
 * A file contributes at most one global name, and what that name *is* -- a script you can attach
 * to a node, a contract, or a type -- decides whether the editor may offer it. Writing that
 * question down once is the point: the engine sees the answer twice, through
 * `BaristaScriptLanguage::_get_global_class_name()`'s `is_abstract` and through
 * `BaristaScript::_is_abstract()`, and two independent answers is exactly the defect that would
 * put an `enum_name` file in the Create Node dialog.
 *
 * `MAX` is the count, never a kind. Every `switch` over this enum handles every enumerator with no
 * `default:` label; the build promotes the compiler's unhandled-enumerator diagnostic to an error
 * (`-Werror=switch` in SConstruct and CMakeLists.txt), so adding a kind fails the build at each
 * consumer rather than falling through to a wrong answer at one of them.
 */
enum class BSDeclarationKind {
	NONE, // No head declaration; the file is a script, not a global class.
	CLASS, // `class_name`, non-generic. The only kind the engine may instantiate.
	GENERIC_CLASS, // `class_name Box[T]`. D2 monomorphizes it, so the bare name has no runtime type.
	TRAIT, // `trait_name`.
	ENUM, // `enum_name`.
	TUPLE, // `tuple_name`.
	MAX,
};

/** The name of a declaration kind. The only place a kind is spelled in text. */
String bs_declaration_kind_name(BSDeclarationKind p_kind);

/**
 * Whether a file declaring this kind is something the editor may instantiate.
 *
 * `trait_name` declares a contract and `enum_name`/`tuple_name` declare types
 * (`docs/namespace-engine-support.md` section 5), so none of the three is a script a node can
 * carry. Nor is a generic `class_name`: `docs/GRAMMAR.md` section 3.2 (D6) keeps it out of the
 * engine's table because D2's monomorphized specializations are the things that exist at runtime,
 * and the table has one slot per name. This is the single predicate behind both the reported
 * `is_abstract` and `BaristaScript::_is_abstract()`.
 */
bool bs_declaration_kind_is_instantiable(BSDeclarationKind p_kind);

/**
 * Whether a file declaring this kind is a script at all, and therefore has a base class.
 *
 * Strictly weaker than instantiability, and the distinction is load-bearing. An `enum_name` or
 * `tuple_name` file declares a type and a `trait_name` file a contract: none of them is a script,
 * so none has a base, which is the inheritance gate of
 * `docs/namespace-engine-support.md` section 5. A generic `class_name` *is* a script -- D2
 * monomorphizes it, so only the bare name has no runtime type -- and it keeps its base, both
 * because the base is true and because a concrete class specializing it through a path
 * (`extends "res://box.barista"[int]`) resolves its own base by walking through it. Blanking a
 * generic base would silently strip that derived class of its native base.
 */
bool bs_declaration_kind_declares_a_script(BSDeclarationKind p_kind);

/**
 * The qualified global name a namespace and an identifier make.
 *
 * Namespaces are dot-joined and class identifiers cannot contain dots, so the qualified name is the
 * registry key and splitting it back apart is a `rfind`
 * (`docs/namespace-engine-support.md` section 1). This is the only place the two parts are joined:
 * the parser builds `ClassNode::qualified_global_name` with it, and so does everything that reports
 * a global name. An empty namespace yields the bare identifier -- never a leading dot, never an
 * empty segment.
 */
String bs_build_qualified_global_name(const String &p_namespace, const StringName &p_identifier);

/**
 * Everything one `.barista` file tells the engine's global class registry.
 *
 * This is the whole set stock reads: `ScriptLanguageExtension::_get_global_class_name` returns
 * `name`, `base_type`, `icon_path`, `is_abstract` and `is_tool`, and
 * `EditorFileSystem::_register_global_class_script`
 * (`editor/file_system/editor_file_system.cpp:2571` at 4.7.2-stable) passes them through
 * unexamined. `kind` is not reported to the engine -- stock's class cache writes a fixed key set,
 * so a GDExtension cannot persist it (`docs/foundry-reuse-plan.md` section 5.6) -- it is what
 * `is_abstract` and `base_type` were computed *from*, kept so a single resolution answers every
 * consumer.
 */
struct BSGlobalClass {
	/**
	 * Whether the *declaration* parse this resolution performs accepted the source.
	 *
	 * Everything else here is empty or false when this is false, so it is the field that tells
	 * "this file declares nothing" from "this file's head did not parse". It is deliberately not
	 * the same question as `Script::is_valid()`: resolution parses declarations only
	 * (`p_parse_body = false`), because a class must keep its name, base and icon while its
	 * function bodies are mid-edit -- that is the error tolerance the warning block at
	 * `foundry_script.cpp:4697-4708` demands, and what stock GDScript does. `bs_source_parses()`
	 * answers the stricter question.
	 */
	bool declarations_parsed = false;
	String name;
	String base_type;
	String icon_path;
	bool is_abstract = false;
	bool is_tool = false;
	BSDeclarationKind kind = BSDeclarationKind::NONE;

	/** The `Dictionary` shape `ScriptLanguageExtension::_get_global_class_name` returns. */
	Dictionary to_dictionary() const;
};

/**
 * The global class `p_source` declares, resolved without the analyzer.
 *
 * Error tolerance is the contract, not a nicety: this runs during the editor's first scan, when a
 * file's dependencies may not exist yet, so it must never fail and never reach for the analyzer
 * (the warning at `foundry_script.cpp:4697-4708` @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6).
 * Where BaristaScript diverges from upstream is which way it fails: a source that reports any
 * diagnostic contributes **no** name at all, rather than a best-effort one. A half-parsed file can
 * have lost the `namespace` line that qualifies its name, and a global class registered under a
 * name that is a prefix of its real one is worse than a global class that is missing until the file
 * compiles.
 *
 * `p_path` is used for diagnostics and to resolve a relative `extends` path; it is not read from
 * disk.
 */
BSGlobalClass bs_resolve_global_class_from_source(const String &p_source, const String &p_path);

/**
 * The same, reading `p_path` from disk. A file that cannot be opened declares nothing.
 *
 * Two calls on unchanged source return identical results: nothing here caches, mutates, or
 * registers anything, and a recursive `extends` chain terminates on a cycle through a visited list
 * (`foundry_script.cpp:4670`).
 */
BSGlobalClass bs_resolve_global_class(const String &p_path);

/**
 * Whether the whole source parses, function bodies included.
 *
 * This is `Script::is_valid()`'s question, and it is strictly stronger than
 * `BSGlobalClass::declarations_parsed`: a file whose head is well formed over a body that is not
 * still contributes a global class (so the editor does not lose it mid-edit) but is not a script
 * anything may run. `ClassDB::can_instantiate` short-circuits on `Script::is_valid()` before it
 * ever reaches `Script::is_abstract()` (`core/object/class_db.cpp:865` at 4.7.2-stable), so this is
 * what keeps a broken file from being offered as instantiable -- exactly as an uncompilable
 * GDScript is registered but not instantiable.
 *
 * At M2 "valid" means "the front end accepted it"; M4 tightens it to "it compiled".
 */
bool bs_source_parses(const String &p_source, const String &p_path);

} // namespace barista_script
