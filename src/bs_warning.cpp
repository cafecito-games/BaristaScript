/**************************************************************************/
/*  bs_warning.cpp                                                        */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_warning.h"

#ifdef DEBUG_ENABLED

#include <godot_cpp/core/class_db.hpp>

namespace barista_script {

String BSWarning::get_message() const {
#define CHECK_SYMBOLS(m_amount) ERR_FAIL_COND_V(symbols.size() < m_amount, String());

	switch (code) {
		case UNASSIGNED_VARIABLE:
			CHECK_SYMBOLS(1);
			return vformat(R"(The variable "%s" is used before being assigned a value.)", symbols[0]);
		case UNASSIGNED_VARIABLE_OP_ASSIGN:
			CHECK_SYMBOLS(2);
			return vformat(R"(The variable "%s" is modified with the compound-assignment operator "%s=" but was not previously initialized.)", symbols[0], symbols[1]);
		case UNUSED_VARIABLE:
			CHECK_SYMBOLS(1);
			return vformat(R"(The local variable "%s" is declared but never used in the block. If this is intended, prefix it with an underscore: "_%s".)", symbols[0], symbols[0]);
		case UNUSED_LOCAL_CONSTANT:
			CHECK_SYMBOLS(1);
			return vformat(R"(The local constant "%s" is declared but never used in the block. If this is intended, prefix it with an underscore: "_%s".)", symbols[0], symbols[0]);
		case UNUSED_PRIVATE_CLASS_VARIABLE:
			CHECK_SYMBOLS(1);
			return vformat(R"(The class variable "%s" is declared but never used in the class.)", symbols[0]);
		case UNUSED_PARAMETER:
			CHECK_SYMBOLS(2);
			return vformat(R"*(The parameter "%s" is never used in the function "%s()". If this is intended, prefix it with an underscore: "_%s".)*", symbols[1], symbols[0], symbols[1]);
		case UNUSED_SIGNAL:
			CHECK_SYMBOLS(1);
			return vformat(R"(The signal "%s" is declared but never explicitly used in the class.)", symbols[0]);
		case SHADOWED_VARIABLE:
			CHECK_SYMBOLS(4);
			return vformat(R"(The local %s "%s" is shadowing an already-declared %s at line %s in the current class.)", symbols[0], symbols[1], symbols[2], symbols[3]);
		case SHADOWED_VARIABLE_BASE_CLASS:
			CHECK_SYMBOLS(4);
			if (symbols.size() > 4) {
				return vformat(R"(The local %s "%s" is shadowing an already-declared %s at line %s in the base class "%s".)", symbols[0], symbols[1], symbols[2], symbols[3], symbols[4]);
			}
			return vformat(R"(The local %s "%s" is shadowing an already-declared %s in the base class "%s".)", symbols[0], symbols[1], symbols[2], symbols[3]);
		case SHADOWED_GLOBAL_IDENTIFIER:
			CHECK_SYMBOLS(3);
			return vformat(R"(The %s "%s" has the same name as a %s.)", symbols[0], symbols[1], symbols[2]);
		case MIXED_NAMESPACE_DIRECTORY:
			CHECK_SYMBOLS(2);
			return vformat(R"(Directory "%s" contains global script classes from mixed namespaces: %s.)", symbols[0], symbols[1]);
		case UNREACHABLE_CODE:
			CHECK_SYMBOLS(1);
			return vformat(R"*(Unreachable code (statement after return) in function "%s()".)*", symbols[0]);
		case UNREACHABLE_PATTERN:
			return "Unreachable pattern (pattern after wildcard or bind).";
		case STANDALONE_EXPRESSION:
			return "Standalone expression (the line may have no effect).";
		case STANDALONE_TERNARY:
			return "Standalone ternary operator (the return value is being discarded).";
		case INCOMPATIBLE_TERNARY:
			return "Values of the ternary operator are not mutually compatible.";
		case UNTYPED_DECLARATION:
			CHECK_SYMBOLS(2);
			if (symbols[0] == "Function") {
				return vformat(R"*(%s "%s()" has no static return type.)*", symbols[0], symbols[1]);
			}
			return vformat(R"(%s "%s" has no static type.)", symbols[0], symbols[1]);
		case INFERRED_DECLARATION:
			CHECK_SYMBOLS(2);
			return vformat(R"(%s "%s" has an implicitly inferred static type.)", symbols[0], symbols[1]);
		case UNSAFE_PROPERTY_ACCESS:
			CHECK_SYMBOLS(2);
			return vformat(R"(The property "%s" is not present on the inferred type "%s" (but may be present on a subtype).)", symbols[0], symbols[1]);
		case UNSAFE_METHOD_ACCESS:
			CHECK_SYMBOLS(2);
			return vformat(R"*(The method "%s()" is not present on the inferred type "%s" (but may be present on a subtype).)*", symbols[0], symbols[1]);
		case UNSAFE_CAST:
			CHECK_SYMBOLS(1);
			return vformat(R"(Casting "Variant" to "%s" is unsafe.)", symbols[0]);
		case UNSAFE_CALL_ARGUMENT:
			CHECK_SYMBOLS(5);
			return vformat(R"*(The argument %s of the %s "%s()" requires the subtype "%s" but the supertype "%s" was provided.)*", symbols[0], symbols[1], symbols[2], symbols[3], symbols[4]);
		case UNSAFE_VOID_RETURN:
			CHECK_SYMBOLS(2);
			return vformat(R"*(The method "%s()" returns "void" but it's trying to return a call to "%s()" that can't be ensured to also be "void".)*", symbols[0], symbols[1]);
		case RETURN_VALUE_DISCARDED:
			CHECK_SYMBOLS(1);
			return vformat(R"*(The function "%s()" returns a value that will be discarded if not used.)*", symbols[0]);
		case STATIC_CALLED_ON_INSTANCE:
			CHECK_SYMBOLS(2);
			return vformat(R"*(The function "%s()" is a static function but was called from an instance. Instead, it should be directly called from the type: "%s.%s()".)*", symbols[0], symbols[1], symbols[0]);
		case MISSING_TOOL:
			return R"(The base class script has the "@tool" annotation, but this script does not have it.)";
		case REDUNDANT_STATIC_UNLOAD:
			return R"(The "@static_unload" annotation is redundant because the file does not have a class with static variables.)";
		case REDUNDANT_AWAIT:
			return R"("await" keyword is unnecessary because the expression isn't a coroutine nor a signal.)";
		case MISSING_AWAIT:
			CHECK_SYMBOLS(1);
			return vformat(R"(The call returns a "%s" whose result is discarded. Use "await", or store or pass the handle if it is awaited elsewhere.)", symbols[0]);
		case ASSERT_ALWAYS_TRUE:
			return "Assert statement is redundant because the expression is always true.";
		case ASSERT_ALWAYS_FALSE:
			return "Assert statement will raise an error because the expression is always false.";
		case INTEGER_DIVISION:
			return "Integer division. Decimal part will be discarded.";
		case NARROWING_CONVERSION:
			return "Narrowing conversion (float is converted to int and loses precision).";
		case INT_AS_ENUM_WITHOUT_CAST:
			return "Integer used when an enum value is expected. If this is intended, cast the integer to the enum type using the \"as\" keyword.";
		case INT_AS_ENUM_WITHOUT_MATCH:
			CHECK_SYMBOLS(3);
			return vformat(R"(Cannot %s %s as Enum "%s": no enum member has matching value.)", symbols[0], symbols[1], symbols[2]);
		case ENUM_VARIABLE_WITHOUT_DEFAULT:
			CHECK_SYMBOLS(1);
			return vformat(R"(The variable "%s" has an enum type and does not set an explicit default value. The default will be set to "0".)", symbols[0]);
		case EMPTY_FILE:
			return "Empty script file.";
		case DEPRECATED_KEYWORD:
			CHECK_SYMBOLS(2);
			return vformat(R"(The "%s" keyword is deprecated and will be removed in a future release. Please replace it with "%s".)", symbols[0], symbols[1]);
		case CONFUSABLE_IDENTIFIER:
			CHECK_SYMBOLS(1);
			return vformat(R"(The identifier "%s" has misleading characters and might be confused with something else.)", symbols[0]);
		case CONFUSABLE_LOCAL_DECLARATION:
			CHECK_SYMBOLS(2);
			return vformat(R"(The %s "%s" is declared below in the parent block.)", symbols[0], symbols[1]);
		case CONFUSABLE_LOCAL_USAGE:
			CHECK_SYMBOLS(1);
			return vformat(R"(The identifier "%s" will be shadowed below in the block.)", symbols[0]);
		case CONFUSABLE_CAPTURE_REASSIGNMENT:
			CHECK_SYMBOLS(1);
			return vformat(R"(Reassigning lambda capture does not modify the outer local variable "%s".)", symbols[0]);
		case INFERENCE_ON_VARIANT:
			CHECK_SYMBOLS(1);
			return vformat("The %s type is being inferred from a Variant value, so it will be typed as Variant.", symbols[0]);
		case NATIVE_METHOD_OVERRIDE:
			CHECK_SYMBOLS(2);
			return vformat(R"*(The method "%s()" overrides a method from native class "%s". This won't be called by the engine and may not work as expected.)*", symbols[0], symbols[1]);
		case GET_NODE_DEFAULT_WITHOUT_ONREADY:
			CHECK_SYMBOLS(1);
			return vformat(R"*(The default value uses "%s" which won't return nodes in the scene tree before "_ready()" is called. Use the "@onready" annotation to solve this.)*", symbols[0]);
		case ONREADY_WITH_EXPORT:
			return R"("@onready" will set the default value after "@export" takes effect and will override it.)";
		case NON_EXHAUSTIVE_MATCH:
			CHECK_SYMBOLS(2);
			return vformat(R"(The "match" statement does not cover all values of "%s". Unhandled: %s. Add the missing patterns or a "_" wildcard branch.)", symbols[0], symbols[1]);
		case MATCH_WITHOUT_DEFAULT:
			return R"(The "match" statement has no "_" wildcard branch; some values may go unhandled.)";
		case OPEN_ENUM_MATCH_WITHOUT_DEFAULT:
			CHECK_SYMBOLS(2);
			// The empty-list form is also reached when a non-constant pattern makes coverage unprovable,
			// so it must not claim that every declared member is handled.
			if (symbols[1].is_empty()) {
				return vformat(R"(The "match" over "%s" has no unguarded "_" or bind branch. "%s" is carried by an integer that can also hold values outside its declared members, so no set of value patterns closes it.)", symbols[0], symbols[0]);
			}
			return vformat(R"(The "match" over "%s" does not handle: %s. "%s" is also carried by an integer that can hold values outside its declared members, so add an unguarded "_" or bind branch rather than the missing patterns alone.)", symbols[0], symbols[1], symbols[0]);
		case WARNING_MAX:
			break; // Can't happen, but silences warning.
	}
	ERR_FAIL_V_MSG(String(), vformat(R"(Invalid BaristaScript warning "%s".)", get_name_from_code(code)));

#undef CHECK_SYMBOLS
}

int BSWarning::get_default_value(Code p_code) {
	ERR_FAIL_INDEX_V_MSG(p_code, WARNING_MAX, WarnLevel::IGNORE, "Getting default value of invalid warning code.");
	return default_warning_levels[p_code];
}

PropertyInfo BSWarning::get_property_info(Code p_code) {
	return PropertyInfo(Variant::INT, get_setting_path_from_code(p_code), PROPERTY_HINT_ENUM, "Ignore,Warn,Error");
}

String BSWarning::get_name() const {
	return get_name_from_code(code);
}

String BSWarning::get_name_from_code(Code p_code) {
	ERR_FAIL_COND_V(p_code < 0 || p_code >= WARNING_MAX, String());

	static const char *names[] = {
		"UNASSIGNED_VARIABLE",
		"UNASSIGNED_VARIABLE_OP_ASSIGN",
		"UNUSED_VARIABLE",
		"UNUSED_LOCAL_CONSTANT",
		"UNUSED_PRIVATE_CLASS_VARIABLE",
		"UNUSED_PARAMETER",
		"UNUSED_SIGNAL",
		"SHADOWED_VARIABLE",
		"SHADOWED_VARIABLE_BASE_CLASS",
		"SHADOWED_GLOBAL_IDENTIFIER",
		"MIXED_NAMESPACE_DIRECTORY",
		"UNREACHABLE_CODE",
		"UNREACHABLE_PATTERN",
		"STANDALONE_EXPRESSION",
		"STANDALONE_TERNARY",
		"INCOMPATIBLE_TERNARY",
		"UNTYPED_DECLARATION",
		"INFERRED_DECLARATION",
		"UNSAFE_PROPERTY_ACCESS",
		"UNSAFE_METHOD_ACCESS",
		"UNSAFE_CAST",
		"UNSAFE_CALL_ARGUMENT",
		"UNSAFE_VOID_RETURN",
		"RETURN_VALUE_DISCARDED",
		"STATIC_CALLED_ON_INSTANCE",
		"MISSING_TOOL",
		"REDUNDANT_STATIC_UNLOAD",
		"REDUNDANT_AWAIT",
		"MISSING_AWAIT",
		"ASSERT_ALWAYS_TRUE",
		"ASSERT_ALWAYS_FALSE",
		"INTEGER_DIVISION",
		"NARROWING_CONVERSION",
		"INT_AS_ENUM_WITHOUT_CAST",
		"INT_AS_ENUM_WITHOUT_MATCH",
		"ENUM_VARIABLE_WITHOUT_DEFAULT",
		"EMPTY_FILE",
		"DEPRECATED_KEYWORD",
		"CONFUSABLE_IDENTIFIER",
		"CONFUSABLE_LOCAL_DECLARATION",
		"CONFUSABLE_LOCAL_USAGE",
		"CONFUSABLE_CAPTURE_REASSIGNMENT",
		"INFERENCE_ON_VARIANT",
		"NATIVE_METHOD_OVERRIDE",
		"GET_NODE_DEFAULT_WITHOUT_ONREADY",
		"ONREADY_WITH_EXPORT",
		"NON_EXHAUSTIVE_MATCH",
		"MATCH_WITHOUT_DEFAULT",
		"OPEN_ENUM_MATCH_WITHOUT_DEFAULT",
	};

	static_assert(sizeof(names) / sizeof(names[0]) == WARNING_MAX,
			"Amount of warning types don't match the amount of warning names.");

	return names[(int)p_code];
}

String BSWarning::get_setting_path_from_code(Code p_code) {
	return "debug/barista_script/warnings/" + get_name_from_code(p_code).to_lower();
}

BSWarning::Code BSWarning::get_code_from_name(const String &p_name) {
	for (int i = 0; i < WARNING_MAX; i++) {
		if (get_name_from_code((Code)i) == p_name) {
			return (Code)i;
		}
	}

	return WARNING_MAX;
}

bool BSWarning::has_valid_position(int p_source_line_count) const {
	if (p_source_line_count < 0) {
		return false;
	}
	if (code < 0 || code >= WARNING_MAX) {
		return false;
	}
	if (start_line < 1 || start_line > p_source_line_count) {
		return false;
	}
	if (end_line < start_line || end_line > p_source_line_count) {
		return false;
	}
	if (start_column < 1 || end_column < 1) {
		return false;
	}
	// The end is exclusive, so a span that begins and ends on one line must cover at least one
	// column. Across lines the end column stands on its own line and only has to be a real column.
	if (end_line == start_line && end_column <= start_column) {
		return false;
	}
	return true;
}

BSWarning::LevelSource BSWarning::resolve_level(Code p_code, const Variant &p_setting, WarnLevel &r_level) {
	ERR_FAIL_INDEX_V_MSG(p_code, WARNING_MAX, LEVEL_SETTING_MALFORMED, "Resolving the level of an invalid warning code.");

	if (p_setting.get_type() == Variant::NIL) {
		r_level = default_warning_levels[p_code];
		return LEVEL_FROM_DECLARED_DEFAULT;
	}

	// A level is an integer in [IGNORE, ERROR]. Nothing else is accepted -- not a bool, not a float
	// that happens to be whole, not the string "warn" -- because every one of those is a project
	// that meant something the registry cannot know, and guessing would hide the mistake.
	if (p_setting.get_type() != Variant::INT) {
		return LEVEL_SETTING_MALFORMED;
	}
	const int64_t level = (int64_t)p_setting;
	if (level < (int64_t)IGNORE || level > (int64_t)ERROR) {
		return LEVEL_SETTING_MALFORMED;
	}

	r_level = (WarnLevel)level;
	return LEVEL_FROM_PROJECT_SETTING;
}

BSWarning::LevelSource BSWarning::resolve_level_from_project_settings(Code p_code, WarnLevel &r_level) {
	ERR_FAIL_INDEX_V_MSG(p_code, WARNING_MAX, LEVEL_SETTING_MALFORMED, "Resolving the level of an invalid warning code.");

	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (settings == nullptr) {
		r_level = default_warning_levels[p_code];
		return LEVEL_FROM_DECLARED_DEFAULT;
	}

	const String setting_path = get_setting_path_from_code(p_code);
	if (!settings->has_setting(setting_path)) {
		r_level = default_warning_levels[p_code];
		return LEVEL_FROM_DECLARED_DEFAULT;
	}

	const LevelSource source = resolve_level(p_code, settings->get_setting_with_override(setting_path), r_level);
	if (source == LEVEL_SETTING_MALFORMED) {
		ERR_PRINT(vformat(R"(Project setting "%s" is not a BaristaScript warning level (0 = Ignore, 1 = Warn, 2 = Error).)", setting_path));
	}
	return source;
}

Dictionary BSWarning::to_validate_dictionary(int p_source_line_count) const {
	Dictionary result;
	ERR_FAIL_COND_V_MSG(!has_valid_position(p_source_line_count), result,
			vformat(R"(BaristaScript warning "%s" has a position outside the source (lines %d-%d, columns %d-%d, source has %d line(s)); refusing to emit it.)",
					get_name_from_code(code), start_line, end_line, start_column, end_column, p_source_line_count));

	// Stock Godot reads exactly these five keys and drops the rest, so the column span stops here.
	result["start_line"] = start_line;
	result["end_line"] = end_line;
	result["code"] = (int)code;
	result["string_code"] = get_name_from_code(code);
	result["message"] = get_message();
	return result;
}

int BSWarning::compare(const BSWarning &p_left, const BSWarning &p_right) {
	if (p_left.start_line != p_right.start_line) {
		return p_left.start_line < p_right.start_line ? -1 : 1;
	}
	if (p_left.start_column != p_right.start_column) {
		return p_left.start_column < p_right.start_column ? -1 : 1;
	}
	if (p_left.code != p_right.code) {
		return p_left.code < p_right.code ? -1 : 1;
	}
	if (p_left.end_line != p_right.end_line) {
		return p_left.end_line < p_right.end_line ? -1 : 1;
	}
	if (p_left.end_column != p_right.end_column) {
		return p_left.end_column < p_right.end_column ? -1 : 1;
	}
	if (p_left.symbols.size() != p_right.symbols.size()) {
		return p_left.symbols.size() < p_right.symbols.size() ? -1 : 1;
	}
	for (int i = 0; i < p_left.symbols.size(); i++) {
		if (p_left.symbols[i] != p_right.symbols[i]) {
			return p_left.symbols[i] < p_right.symbols[i] ? -1 : 1;
		}
	}
	return 0;
}

namespace {

struct WarningLess {
	bool operator()(const BSWarning &p_left, const BSWarning &p_right) const {
		return BSWarning::compare(p_left, p_right) < 0;
	}
};

BSWarning warning_from_dictionary(const Dictionary &p_warning) {
	BSWarning warning;
	warning.code = (BSWarning::Code)(int)p_warning.get("code", BSWarning::WARNING_MAX);
	warning.start_line = (int)p_warning.get("start_line", -1);
	warning.start_column = (int)p_warning.get("start_column", -1);
	warning.end_line = (int)p_warning.get("end_line", -1);
	warning.end_column = (int)p_warning.get("end_column", -1);
	const PackedStringArray symbols = p_warning.get("symbols", PackedStringArray());
	for (int i = 0; i < symbols.size(); i++) {
		warning.symbols.push_back(symbols[i]);
	}
	return warning;
}

Dictionary warning_to_dictionary(const BSWarning &p_warning) {
	Dictionary result;
	result["code"] = (int)p_warning.code;
	result["start_line"] = p_warning.start_line;
	result["start_column"] = p_warning.start_column;
	result["end_line"] = p_warning.end_line;
	result["end_column"] = p_warning.end_column;
	PackedStringArray symbols;
	for (int i = 0; i < p_warning.symbols.size(); i++) {
		symbols.push_back(p_warning.symbols[i]);
	}
	result["symbols"] = symbols;
	return result;
}

} // namespace

void BSWarning::sort_warnings(Vector<BSWarning> &p_warnings) {
	p_warnings.sort_custom<WarningLess>();
}

bool BSWarning::unicode_security_available() {
	TextServerManager *manager = TextServerManager::get_singleton();
	if (manager == nullptr) {
		return false;
	}
	const Ref<TextServer> text_server = manager->get_primary_interface();
	if (text_server.is_null()) {
		return false;
	}
	return text_server->has_feature(TextServer::FEATURE_UNICODE_SECURITY);
}

bool BSWarning::is_confusable_identifier(const String &p_identifier) {
	TextServerManager *manager = TextServerManager::get_singleton();
	if (manager == nullptr) {
		return false;
	}
	const Ref<TextServer> text_server = manager->get_primary_interface();
	if (text_server.is_null() || !text_server->has_feature(TextServer::FEATURE_UNICODE_SECURITY)) {
		return false;
	}
	return text_server->spoof_check(p_identifier);
}

int BaristaScriptWarningRegistry::get_warning_count() const {
	return BSWarning::WARNING_MAX;
}

String BaristaScriptWarningRegistry::get_name_from_code(int p_code) const {
	return BSWarning::get_name_from_code((BSWarning::Code)p_code);
}

int BaristaScriptWarningRegistry::get_code_from_name(const String &p_name) const {
	return (int)BSWarning::get_code_from_name(p_name);
}

String BaristaScriptWarningRegistry::get_setting_path_from_code(int p_code) const {
	return BSWarning::get_setting_path_from_code((BSWarning::Code)p_code);
}

int BaristaScriptWarningRegistry::get_default_level(int p_code) const {
	return BSWarning::get_default_value((BSWarning::Code)p_code);
}

Dictionary BaristaScriptWarningRegistry::get_property_info(int p_code) const {
	return (Dictionary)BSWarning::get_property_info((BSWarning::Code)p_code);
}

String BaristaScriptWarningRegistry::get_message(int p_code, const PackedStringArray &p_symbols) const {
	BSWarning warning;
	warning.code = (BSWarning::Code)p_code;
	for (int i = 0; i < p_symbols.size(); i++) {
		warning.symbols.push_back(p_symbols[i]);
	}
	return warning.get_message();
}

Dictionary BaristaScriptWarningRegistry::resolve_level(int p_code, const Variant &p_setting) const {
	BSWarning::WarnLevel level = BSWarning::IGNORE;
	const BSWarning::LevelSource source = BSWarning::resolve_level((BSWarning::Code)p_code, p_setting, level);

	Dictionary result;
	result["source"] = (int)source;
	// A malformed setting produces no level at all; reporting one would be the coercion the
	// fail-closed contract forbids, so the key is simply absent.
	if (source != BSWarning::LEVEL_SETTING_MALFORMED) {
		result["level"] = (int)level;
	}
	return result;
}

Dictionary BaristaScriptWarningRegistry::resolve_level_from_project_settings(int p_code) const {
	BSWarning::WarnLevel level = BSWarning::IGNORE;
	const BSWarning::LevelSource source = BSWarning::resolve_level_from_project_settings((BSWarning::Code)p_code, level);

	Dictionary result;
	result["source"] = (int)source;
	if (source != BSWarning::LEVEL_SETTING_MALFORMED) {
		result["level"] = (int)level;
	}
	return result;
}

bool BaristaScriptWarningRegistry::has_valid_position(int p_code, int p_start_line, int p_start_column, int p_end_line, int p_end_column, int p_source_line_count) const {
	BSWarning warning;
	warning.code = (BSWarning::Code)p_code;
	warning.start_line = p_start_line;
	warning.start_column = p_start_column;
	warning.end_line = p_end_line;
	warning.end_column = p_end_column;
	return warning.has_valid_position(p_source_line_count);
}

Dictionary BaristaScriptWarningRegistry::to_validate_dictionary(const Dictionary &p_warning, int p_source_line_count) const {
	return warning_from_dictionary(p_warning).to_validate_dictionary(p_source_line_count);
}

Array BaristaScriptWarningRegistry::sort_warnings(const Array &p_warnings) const {
	Vector<BSWarning> warnings;
	for (int i = 0; i < p_warnings.size(); i++) {
		warnings.push_back(warning_from_dictionary(p_warnings[i]));
	}
	BSWarning::sort_warnings(warnings);

	Array result;
	for (int i = 0; i < warnings.size(); i++) {
		result.push_back(warning_to_dictionary(warnings[i]));
	}
	return result;
}

bool BaristaScriptWarningRegistry::unicode_security_available() const {
	return BSWarning::unicode_security_available();
}

bool BaristaScriptWarningRegistry::is_confusable_identifier(const String &p_identifier) const {
	return BSWarning::is_confusable_identifier(p_identifier);
}

void BaristaScriptWarningRegistry::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_warning_count"), &BaristaScriptWarningRegistry::get_warning_count);
	ClassDB::bind_method(D_METHOD("get_name_from_code", "code"), &BaristaScriptWarningRegistry::get_name_from_code);
	ClassDB::bind_method(D_METHOD("get_code_from_name", "name"), &BaristaScriptWarningRegistry::get_code_from_name);
	ClassDB::bind_method(D_METHOD("get_setting_path_from_code", "code"), &BaristaScriptWarningRegistry::get_setting_path_from_code);
	ClassDB::bind_method(D_METHOD("get_default_level", "code"), &BaristaScriptWarningRegistry::get_default_level);
	ClassDB::bind_method(D_METHOD("get_property_info", "code"), &BaristaScriptWarningRegistry::get_property_info);
	ClassDB::bind_method(D_METHOD("get_message", "code", "symbols"), &BaristaScriptWarningRegistry::get_message);
	ClassDB::bind_method(D_METHOD("resolve_level", "code", "setting"), &BaristaScriptWarningRegistry::resolve_level);
	ClassDB::bind_method(D_METHOD("resolve_level_from_project_settings", "code"), &BaristaScriptWarningRegistry::resolve_level_from_project_settings);
	ClassDB::bind_method(D_METHOD("has_valid_position", "code", "start_line", "start_column", "end_line", "end_column", "source_line_count"), &BaristaScriptWarningRegistry::has_valid_position);
	ClassDB::bind_method(D_METHOD("to_validate_dictionary", "warning", "source_line_count"), &BaristaScriptWarningRegistry::to_validate_dictionary);
	ClassDB::bind_method(D_METHOD("sort_warnings", "warnings"), &BaristaScriptWarningRegistry::sort_warnings);
	ClassDB::bind_method(D_METHOD("unicode_security_available"), &BaristaScriptWarningRegistry::unicode_security_available);
	ClassDB::bind_method(D_METHOD("is_confusable_identifier", "identifier"), &BaristaScriptWarningRegistry::is_confusable_identifier);
}

} // namespace barista_script

#endif // DEBUG_ENABLED
