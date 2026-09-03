/**************************************************************************/
/*  bs_warning.h                                                          */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_platform.h"

#ifdef DEBUG_ENABLED

/**
 * BaristaScript's warning registry, hard-forked from Foundry
 * `modules/foundry_script/fs_warning.h` @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6.
 *
 * The registry is a vocabulary, not a policy. It owns exactly three facts about every warning --
 * its enumerator, its human-readable name, and its declared default level -- plus the message
 * template that turns a code and its symbols into text. Deciding when to raise one belongs to the
 * analyzer (M3); deciding what to do with a raised one belongs to the language's validate surface.
 *
 * Single source of truth. Each fact is written down once:
 *
 *   - `Code` is the only enumeration of warnings.
 *   - `default_warning_levels` is the only default table; a `static_assert` ties its length to
 *     `WARNING_MAX`.
 *   - `get_name_from_code()` holds the only name table, likewise `static_assert`-ed.
 *   - `get_message()` is one exhaustive `switch` with no `default:` label, so a code with no
 *     message is a compile diagnostic at the switch rather than a runtime "unknown warning" string.
 *
 * There is no second switch and no parallel array. Deleting an enumerator -- which is what a
 * language delta does -- makes every surviving `BSWarning::THAT_CODE` reference an error at its own
 * call site ("no member named ... in 'BSWarning'"), which is the fail-closed behaviour the
 * milestone asks for. Verified by hand rather than by a test, because the proof is the absence of a
 * translation unit, in the manner of `bs_platform.h`'s NumericType note:
 *
 *     BSWarning warning;
 *     warning.code = BSWarning::REMOVED_CODE;  // error: no member named 'REMOVED_CODE' in
 *                                              // 'barista_script::BSWarning'
 *
 * As of this milestone no delta in `docs/GRAMMAR.md` §0.2 removes a code. D1 (one integer type)
 * deletes Foundry's numeric tower, but Foundry spends no warning on it -- there is no `uint`,
 * `ulong`, `as!` or literal-suffix warning in the upstream enumeration -- so `INTEGER_DIVISION` and
 * `NARROWING_CONVERSION`, which are about `int` and `float`, survive unchanged. D7 (no native
 * namespaces) does not remove `MIXED_NAMESPACE_DIRECTORY` either: D7 removes namespaces from
 * *native engine classes* only, and `namespace`/`import` still govern BaristaScript declarations
 * (`docs/namespace-engine-support.md` §5), so global script classes in one directory can still
 * disagree about their namespace. The enumeration is therefore ported whole.
 *
 * ---------------------------------------------------------------------------------------------
 * Decision: column spans are kept internally and narrowed at the engine boundary.
 * ---------------------------------------------------------------------------------------------
 *
 * Foundry widened its own engine's `ScriptLanguage::Warning` to carry `start_column`/`end_column`
 * (Foundry `core/object/script_language.h:272-281` @ c9d5e35). BaristaScript runs on unmodified
 * Godot, and stock 4.7-stable's struct has no columns at all:
 *
 *     struct Warning {
 *         /// One-based.
 *         int start_line = 0;
 *         /// One-based.
 *         int end_line = 0;
 *         int code;
 *         String string_code;
 *         String message;
 *     };
 *     -- godotengine/godot 4.7-stable, core/object/script_language.h:224-232
 *
 * and the only door a GDExtension has into it, `ScriptLanguageExtension::validate()`, reads exactly
 * those five keys out of the returned dictionary and no others
 * (4.7-stable `core/object/script_language_extension.h:336-352`).
 *
 * `BSWarning` keeps the full span anyway. Three reasons, in order of weight:
 *
 *   1. Deterministic ordering needs the column. The milestone requires a stable order for several
 *      warnings on one line, and (line, code) alone does not distinguish them. Without columns the
 *      order would depend on the order the analyzer happened to walk the AST.
 *   2. BaristaScript's own consumers are not the engine. The corpus harness compares diagnostics as
 *      text, and future tooling (LSP, the editor integration milestone) wants the span. Discarding
 *      it in the producer would be unrecoverable.
 *   3. Narrowing is cheap and total. `to_validate_dictionary()` is the single place the span is
 *      dropped, and it is dropped rather than encoded under an invented key: a key stock Godot does
 *      not read is dead weight that would rot silently.
 *
 * The cost is honest: the editor underlines whole lines, not spans. Reinstating spans needs an
 * engine change, not an extension change, so it is not a BaristaScript decision to revisit.
 *
 * ---------------------------------------------------------------------------------------------
 * Decision: the TextServer confusable-identifier check is reinstated, through the public API.
 * ---------------------------------------------------------------------------------------------
 *
 * The M1 platform seam guarded out `servers/text/text_server.h`
 * (`src/bs_platform_manifest.json`), which dropped Foundry's `FEATURE_UNICODE_SECURITY` spoof check
 * behind `CONFUSABLE_IDENTIFIER` (Foundry `fs_parser.cpp:4898` @ c9d5e35, and the sibling
 * keyword-confusable *error* at `fs_tokenizer.cpp:672`). M1 recorded the omission explicitly and
 * routed the decision to this milestone. The decision is to reinstate it, here, because
 * `CONFUSABLE_IDENTIFIER` is a member of this registry's vocabulary and a code that no consumer can
 * ever raise is a code the registry is lying about.
 *
 * The route is public and reachable from a GDExtension: `TextServerManager::get_primary_interface()`
 * yields a `Ref<TextServer>` with `has_feature()`, `spoof_check()` and `is_confusable()` -- the same
 * three calls Foundry makes through the engine-internal `TS` macro, and the reason the engine header
 * is not needed. The concrete cost is paid in `build_profile.json`, which now enables `TextServer`
 * and `TextServerManager`; the manifest entry moves from `guarded-out` to `mapped` to match.
 *
 * Scope: this file supplies `is_confusable_identifier()`, the predicate behind the warning. The
 * tokenizer's keyword-confusable check raises a parse *error*, not a warning, so it stays a
 * tokenizer decision (issue #6 / the parser milestone); the header it needs is now mapped either
 * way. The predicate is fail-open by construction -- no TextServer, or a build of it without the
 * ICU-backed feature, means no confusable warnings -- because the alternative, refusing to analyze,
 * would make identifier analysis depend on how the host was compiled.
 *
 * ---------------------------------------------------------------------------------------------
 * Port edits, exhaustively.
 * ---------------------------------------------------------------------------------------------
 *
 *   - `FSWarning` -> `BSWarning`, and the file is namespaced `barista_script` to match the rest of
 *     the extension.
 *   - `PNAME()` around each name literal is dropped. In core it expands to `(m_value)` and exists
 *     only to mark the string for the editor's translation extractor
 *     (Godot `core/string/ustring.h:763`); godot-cpp has no extractor and no macro. Dropping the
 *     marker is a port edit at the call site, not the seam expanding a missing macro to nothing.
 *   - `std_size()` (Foundry `core/typedefs.h`) has no godot-cpp counterpart; the two
 *     `static_assert`s use `sizeof(table) / sizeof(table[0])`, which is what `std_size` computes.
 *   - The setting prefix becomes `debug/barista_script/warnings/`.
 *   - Everything below `get_code_from_name()` is new: position validation, level resolution,
 *     deterministic ordering, the engine-boundary narrowing, and the TextServer predicate. Foundry
 *     spreads those across its parser and analyzer; a registry that owns its own fail-closed
 *     behaviour is the point of this milestone.
 */

namespace barista_script {

class BSWarning {
public:
	enum WarnLevel {
		IGNORE,
		WARN,
		ERROR
	};

	enum Code {
		UNASSIGNED_VARIABLE, // Variable used but never assigned.
		UNASSIGNED_VARIABLE_OP_ASSIGN, // Variable never assigned but used in an assignment operation (+=, *=, etc).
		UNUSED_VARIABLE, // Local variable is declared but never used.
		UNUSED_LOCAL_CONSTANT, // Local constant is declared but never used.
		UNUSED_PRIVATE_CLASS_VARIABLE, // Class variable is declared private ("_" prefix) but never used in the class.
		UNUSED_PARAMETER, // Function parameter is never used.
		UNUSED_SIGNAL, // Signal is defined but never explicitly used in the class.
		SHADOWED_VARIABLE, // A local variable/constant shadows a current class member.
		SHADOWED_VARIABLE_BASE_CLASS, // A local variable/constant shadows a base class member.
		SHADOWED_GLOBAL_IDENTIFIER, // A global class or function has the same name as variable.
		MIXED_NAMESPACE_DIRECTORY, // Global script classes in the same directory declare different namespaces.
		UNREACHABLE_CODE, // Code after a return statement.
		UNREACHABLE_PATTERN, // Pattern in a match statement after a catch all pattern (wildcard or bind).
		STANDALONE_EXPRESSION, // Expression not assigned to a variable.
		STANDALONE_TERNARY, // Return value of ternary expression is discarded.
		INCOMPATIBLE_TERNARY, // Possible values of a ternary if are not mutually compatible.
		UNTYPED_DECLARATION, // Variable/parameter/function has no static type, explicitly specified or implicitly inferred.
		INFERRED_DECLARATION, // Variable/constant/parameter has an implicitly inferred static type.
		UNSAFE_PROPERTY_ACCESS, // Property not found in the detected type (but can be in subtypes).
		UNSAFE_METHOD_ACCESS, // Function not found in the detected type (but can be in subtypes).
		UNSAFE_CAST, // Casting a `Variant` value to non-`Variant`.
		UNSAFE_CALL_ARGUMENT, // Function call argument is of a supertype of the required type.
		UNSAFE_VOID_RETURN, // Function returns void but returned a call to a function that can't be type checked.
		RETURN_VALUE_DISCARDED, // Function call returns something but the value isn't used.
		STATIC_CALLED_ON_INSTANCE, // A static method was called on an instance of a class instead of on the class itself.
		MISSING_TOOL, // The base class script has the "@tool" annotation, but this script does not have it.
		REDUNDANT_STATIC_UNLOAD, // The `@static_unload` annotation is used but the class does not have static data.
		REDUNDANT_AWAIT, // await is used but expression is synchronous (not a signal nor a coroutine).
		MISSING_AWAIT, // Root-position non-void coroutine discard (Coroutine[void] fire-and-forget is exempt).
		ASSERT_ALWAYS_TRUE, // Expression for assert argument is always true.
		ASSERT_ALWAYS_FALSE, // Expression for assert argument is always false.
		INTEGER_DIVISION, // Integer divide by integer, decimal part is discarded.
		NARROWING_CONVERSION, // Float value into an integer slot, precision is lost.
		INT_AS_ENUM_WITHOUT_CAST, // An integer value was used as an enum value without casting.
		INT_AS_ENUM_WITHOUT_MATCH, // An integer value was used as an enum value without matching enum member.
		ENUM_VARIABLE_WITHOUT_DEFAULT, // A variable with an enum type does not have a default value. The default will be set to `0` instead of the first enum value.
		EMPTY_FILE, // A script file is empty.
		DEPRECATED_KEYWORD, // The keyword is deprecated and should be replaced.
		CONFUSABLE_IDENTIFIER, // The identifier contains misleading characters that can be confused. E.g. "usеr" (has Cyrillic "е" instead of Latin "e").
		CONFUSABLE_LOCAL_DECLARATION, // The parent block declares an identifier with the same name below.
		CONFUSABLE_LOCAL_USAGE, // The identifier will be shadowed below in the block.
		CONFUSABLE_CAPTURE_REASSIGNMENT, // Reassigning lambda capture does not modify the outer local variable.
		INFERENCE_ON_VARIANT, // The declaration uses type inference but the value is typed as Variant.
		NATIVE_METHOD_OVERRIDE, // The script method overrides a native one, this may not work as intended.
		GET_NODE_DEFAULT_WITHOUT_ONREADY, // A class variable uses `get_node()` (or the `$` notation) as its default value, but does not use the @onready annotation.
		ONREADY_WITH_EXPORT, // The `@onready` annotation will set the value after `@export` which is likely not intended.
		NON_EXHAUSTIVE_MATCH, // A `match` over an enum or `bool` does not handle all values and has no wildcard `_` branch.
		MATCH_WITHOUT_DEFAULT, // A `match` over a non-finite-domain value has no wildcard `_` branch.
		OPEN_ENUM_MATCH_WITHOUT_DEFAULT, // A `match` over a plain (integer-backed) enum has no unguarded `_` or bind branch.
		WARNING_MAX,
	};

	/**
	 * Where the effective level of a warning came from. Every caller of `resolve_level()` handles
	 * all three: a malformed setting is a distinct outcome from an absent one precisely so that it
	 * cannot be coerced into the declared default.
	 */
	enum LevelSource {
		LEVEL_FROM_DECLARED_DEFAULT, // No setting exists; `default_warning_levels` decides.
		LEVEL_FROM_PROJECT_SETTING, // A well-formed setting overrode the default.
		LEVEL_SETTING_MALFORMED, // A setting exists but is not a level; no level is produced.
	};

	constexpr static WarnLevel default_warning_levels[] = {
		WARN, // UNASSIGNED_VARIABLE
		WARN, // UNASSIGNED_VARIABLE_OP_ASSIGN
		WARN, // UNUSED_VARIABLE
		WARN, // UNUSED_LOCAL_CONSTANT
		WARN, // UNUSED_PRIVATE_CLASS_VARIABLE
		WARN, // UNUSED_PARAMETER
		WARN, // UNUSED_SIGNAL
		WARN, // SHADOWED_VARIABLE
		WARN, // SHADOWED_VARIABLE_BASE_CLASS
		WARN, // SHADOWED_GLOBAL_IDENTIFIER
		WARN, // MIXED_NAMESPACE_DIRECTORY
		WARN, // UNREACHABLE_CODE
		WARN, // UNREACHABLE_PATTERN
		WARN, // STANDALONE_EXPRESSION
		WARN, // STANDALONE_TERNARY
		WARN, // INCOMPATIBLE_TERNARY
		IGNORE, // UNTYPED_DECLARATION // Static typing is optional, we don't want to spam warnings.
		IGNORE, // INFERRED_DECLARATION // Static typing is optional, we don't want to spam warnings.
		IGNORE, // UNSAFE_PROPERTY_ACCESS // Too common in untyped scenarios.
		IGNORE, // UNSAFE_METHOD_ACCESS // Too common in untyped scenarios.
		IGNORE, // UNSAFE_CAST // Too common in untyped scenarios.
		IGNORE, // UNSAFE_CALL_ARGUMENT // Too common in untyped scenarios.
		WARN, // UNSAFE_VOID_RETURN
		IGNORE, // RETURN_VALUE_DISCARDED // Too spammy by default on common cases (connect, Tween, etc.).
		WARN, // STATIC_CALLED_ON_INSTANCE
		WARN, // MISSING_TOOL
		WARN, // REDUNDANT_STATIC_UNLOAD
		WARN, // REDUNDANT_AWAIT
		WARN, // MISSING_AWAIT // Only root-position non-void coroutine discards; Coroutine[void] fire-and-forget is exempt.
		WARN, // ASSERT_ALWAYS_TRUE
		WARN, // ASSERT_ALWAYS_FALSE
		WARN, // INTEGER_DIVISION
		WARN, // NARROWING_CONVERSION
		WARN, // INT_AS_ENUM_WITHOUT_CAST
		WARN, // INT_AS_ENUM_WITHOUT_MATCH
		WARN, // ENUM_VARIABLE_WITHOUT_DEFAULT
		WARN, // EMPTY_FILE
		WARN, // DEPRECATED_KEYWORD
		WARN, // CONFUSABLE_IDENTIFIER
		WARN, // CONFUSABLE_LOCAL_DECLARATION
		WARN, // CONFUSABLE_LOCAL_USAGE
		WARN, // CONFUSABLE_CAPTURE_REASSIGNMENT
		ERROR, // INFERENCE_ON_VARIANT // Most likely done by accident, usually inference is trying for a particular type.
		ERROR, // NATIVE_METHOD_OVERRIDE // May not work as expected.
		ERROR, // GET_NODE_DEFAULT_WITHOUT_ONREADY // May not work as expected.
		ERROR, // ONREADY_WITH_EXPORT // May not work as expected.
		WARN, // NON_EXHAUSTIVE_MATCH
		IGNORE, // MATCH_WITHOUT_DEFAULT // Requiring a default branch on open-domain matches is noisy; opt-in.
		WARN, // OPEN_ENUM_MATCH_WITHOUT_DEFAULT // Handling every declared member looks exhaustive but is not.
	};

	static_assert(sizeof(default_warning_levels) / sizeof(default_warning_levels[0]) == WARNING_MAX,
			"Amount of default levels does not match the amount of warnings.");

	/**
	 * The span. One-based, start inclusive, end exclusive -- Foundry's convention
	 * (`core/object/script_language.h:273` @ c9d5e35), kept because the analyzer that will fill
	 * these in is Foundry's. `-1` is the "not set yet" sentinel and is never a valid emitted
	 * position; see `has_valid_position()`.
	 */
	Code code = WARNING_MAX;
	int start_line = -1;
	int start_column = -1;
	int end_line = -1;
	int end_column = -1;
	Vector<String> symbols;

	String get_name() const;
	String get_message() const;
	static int get_default_value(Code p_code);
	static PropertyInfo get_property_info(Code p_code);
	static String get_name_from_code(Code p_code);
	static String get_setting_path_from_code(Code p_code);
	static Code get_code_from_name(const String &p_name);

	/**
	 * True when the span names a real region of a source of `p_source_line_count` lines.
	 *
	 * The registry never clamps. A caller holding an out-of-range span has a producer bug, and the
	 * only two honest answers are to fix the producer or to drop the warning -- silently moving it
	 * to line 1 would put a diagnostic on code that did not cause it. `to_validate_dictionary()`
	 * refuses rather than emits.
	 */
	bool has_valid_position(int p_source_line_count) const;

	/**
	 * The effective level for `p_code` given `p_setting`, the raw value read from the project
	 * settings (a `nil` Variant when the setting is absent).
	 *
	 * Absent -> the declared default, from the one table that declares it. Well-formed -> the
	 * setting. Anything else -- a string, a float, an out-of-range integer -- is malformed:
	 * `r_level` is left untouched and the caller gets `LEVEL_SETTING_MALFORMED`, because a project
	 * that misconfigures a warning should be told, not quietly given the default it did not ask
	 * for.
	 */
	static LevelSource resolve_level(Code p_code, const Variant &p_setting, WarnLevel &r_level);

	/// `resolve_level()` against the live `ProjectSettings`, which is where the setting really lives.
	static LevelSource resolve_level_from_project_settings(Code p_code, WarnLevel &r_level);

	/**
	 * The warning as stock Godot's `ScriptLanguageExtension::validate()` wants it: `start_line`,
	 * `end_line`, `code`, `string_code`, `message`, and nothing else
	 * (godotengine/godot 4.7-stable `core/object/script_language_extension.h:341-352`). This is the
	 * single place the column span is dropped. Returns an empty dictionary, loudly, when the span
	 * does not fit `p_source_line_count`.
	 */
	Dictionary to_validate_dictionary(int p_source_line_count) const;

	/**
	 * Total order over warnings: (start_line, start_column, code) first, as the milestone requires,
	 * then (end_line, end_column) and finally the symbols, so that two warnings the first three
	 * keys cannot separate still order the same way on every run. Returns -1, 0 or 1.
	 *
	 * The tail keys are not decoration. `Vector::sort_custom()` is an introsort and is not stable,
	 * so a comparator that reports "equal" for distinguishable warnings would let their relative
	 * order depend on the input permutation, and analyzing the same source twice could print them
	 * in different orders. Warnings that tie on every key are indistinguishable in the output.
	 */
	static int compare(const BSWarning &p_left, const BSWarning &p_right);

	/// Sorts in place by `compare()`. The only ordering the registry blesses.
	static void sort_warnings(Vector<BSWarning> &p_warnings);

	/// True when the host's TextServer offers `FEATURE_UNICODE_SECURITY`.
	static bool unicode_security_available();

	/**
	 * The predicate behind `CONFUSABLE_IDENTIFIER`: true when `p_identifier` mixes scripts in a way
	 * Unicode security profiles call a spoof. False when the feature is unavailable, which is a
	 * deliberate fail-open -- see the TextServer decision above.
	 */
	static bool is_confusable_identifier(const String &p_identifier);
};

/**
 * The registry's test seam.
 *
 * godot-cpp's `String`, `StringName` and `Variant` are engine-backed
 * (`godot-cpp/gen/include/godot_cpp/variant/string.hpp:84`), so there is no standalone C++ test
 * binary in this repository and every assertion about `BSWarning` has to be made from inside a
 * loaded Godot runtime. This class is the only door: it binds the registry's queries, and nothing
 * else, so `project/tests/warning_registry_test.gd` can iterate the whole vocabulary.
 *
 * It deliberately exposes no enumerator constants. The test derives every code from
 * `get_warning_count()` and asks the registry for the rest, so a second copy of the vocabulary
 * cannot drift out of step with the first.
 *
 * Registered only under `DEBUG_ENABLED`, like the registry it wraps.
 */
class BaristaScriptWarningRegistry : public RefCounted {
	GDCLASS(BaristaScriptWarningRegistry, RefCounted)

protected:
	static void _bind_methods();

public:
	int get_warning_count() const;
	String get_name_from_code(int p_code) const;
	int get_code_from_name(const String &p_name) const;
	String get_setting_path_from_code(int p_code) const;
	int get_default_level(int p_code) const;
	Dictionary get_property_info(int p_code) const;
	String get_message(int p_code, const PackedStringArray &p_symbols) const;
	Dictionary resolve_level(int p_code, const Variant &p_setting) const;
	Dictionary resolve_level_from_project_settings(int p_code) const;
	bool has_valid_position(int p_code, int p_start_line, int p_start_column, int p_end_line, int p_end_column, int p_source_line_count) const;
	Dictionary to_validate_dictionary(const Dictionary &p_warning, int p_source_line_count) const;
	Array sort_warnings(const Array &p_warnings) const;
	bool unicode_security_available() const;
	bool is_confusable_identifier(const String &p_identifier) const;
};

} // namespace barista_script

#endif // DEBUG_ENABLED
