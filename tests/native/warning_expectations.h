/**************************************************************************/
/*  warning_expectations.h                                                */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

namespace barista_script::test {
// Existing Foundry-derived oracle from warning_registry_test.gd at 1fe5964.
// The Code enum and production tables remain authoritative; this only checks
// the legacy exact names/defaults/rendered diagnostics and defines no behavior.
struct WarningExpectation {
	const char *name;
	int level;
	const char *message;
};
inline constexpr WarningExpectation warning_expectations[] = {
	{ "UNASSIGNED_VARIABLE", 1, "The variable \"S0\" is used before being assigned a value." },
	{ "UNASSIGNED_VARIABLE_OP_ASSIGN", 1, "The variable \"S0\" is modified with the compound-assignment operator \"S1=\" but was not previously initialized." },
	{ "UNUSED_VARIABLE", 1, "The local variable \"S0\" is declared but never used in the block. If this is intended, prefix it with an underscore: \"_S0\"." },
	{ "UNUSED_LOCAL_CONSTANT", 1, "The local constant \"S0\" is declared but never used in the block. If this is intended, prefix it with an underscore: \"_S0\"." },
	{ "UNUSED_PRIVATE_CLASS_VARIABLE", 1, "The class variable \"S0\" is declared but never used in the class." },
	{ "UNUSED_PARAMETER", 1, "The parameter \"S1\" is never used in the function \"S0()\". If this is intended, prefix it with an underscore: \"_S1\"." },
	{ "UNUSED_SIGNAL", 1, "The signal \"S0\" is declared but never explicitly used in the class." },
	{ "SHADOWED_VARIABLE", 1, "The local S0 \"S1\" is shadowing an already-declared S2 at line S3 in the current class." },
	{ "SHADOWED_VARIABLE_BASE_CLASS", 1, "The local S0 \"S1\" is shadowing an already-declared S2 at line S3 in the base class \"S4\"." },
	{ "SHADOWED_GLOBAL_IDENTIFIER", 1, "The S0 \"S1\" has the same name as a S2." },
	{ "MIXED_NAMESPACE_DIRECTORY", 1, "Directory \"S0\" contains global script classes from mixed namespaces: S1." },
	{ "UNREACHABLE_CODE", 1, "Unreachable code (statement after return) in function \"S0()\"." },
	{ "UNREACHABLE_PATTERN", 1, "Unreachable pattern (pattern after wildcard or bind)." },
	{ "STANDALONE_EXPRESSION", 1, "Standalone expression (the line may have no effect)." },
	{ "STANDALONE_TERNARY", 1, "Standalone ternary operator (the return value is being discarded)." },
	{ "INCOMPATIBLE_TERNARY", 1, "Values of the ternary operator are not mutually compatible." },
	{ "UNTYPED_DECLARATION", 0, "S0 \"S1\" has no static type." },
	{ "INFERRED_DECLARATION", 0, "S0 \"S1\" has an implicitly inferred static type." },
	{ "UNSAFE_PROPERTY_ACCESS", 0, "The property \"S0\" is not present on the inferred type \"S1\" (but may be present on a subtype)." },
	{ "UNSAFE_METHOD_ACCESS", 0, "The method \"S0()\" is not present on the inferred type \"S1\" (but may be present on a subtype)." },
	{ "UNSAFE_CAST", 0, "Casting \"Variant\" to \"S0\" is unsafe." },
	{ "UNSAFE_CALL_ARGUMENT", 0, "The argument S0 of the S1 \"S2()\" requires the subtype \"S3\" but the supertype \"S4\" was provided." },
	{ "UNSAFE_VOID_RETURN", 1, "The method \"S0()\" returns \"void\" but it's trying to return a call to \"S1()\" that can't be ensured to also be \"void\"." },
	{ "RETURN_VALUE_DISCARDED", 0, "The function \"S0()\" returns a value that will be discarded if not used." },
	{ "STATIC_CALLED_ON_INSTANCE", 1, "The function \"S0()\" is a static function but was called from an instance. Instead, it should be directly called from the type: \"S1.S0()\"." },
	{ "MISSING_TOOL", 1, "The base class script has the \"@tool\" annotation, but this script does not have it." },
	{ "REDUNDANT_STATIC_UNLOAD", 1, "The \"@static_unload\" annotation is redundant because the file does not have a class with static variables." },
	{ "REDUNDANT_AWAIT", 1, "\"await\" keyword is unnecessary because the expression isn't a coroutine nor a signal." },
	{ "MISSING_AWAIT", 1, "The call returns a \"S0\" whose result is discarded. Use \"await\", or store or pass the handle if it is awaited elsewhere." },
	{ "ASSERT_ALWAYS_TRUE", 1, "Assert statement is redundant because the expression is always true." },
	{ "ASSERT_ALWAYS_FALSE", 1, "Assert statement will raise an error because the expression is always false." },
	{ "INTEGER_DIVISION", 1, "Integer division. Decimal part will be discarded." },
	{ "NARROWING_CONVERSION", 1, "Narrowing conversion (float is converted to int and loses precision)." },
	{ "INT_AS_ENUM_WITHOUT_CAST", 1, "Integer used when an enum value is expected. If this is intended, cast the integer to the enum type using the \"as\" keyword." },
	{ "INT_AS_ENUM_WITHOUT_MATCH", 1, "Cannot S0 S1 as Enum \"S2\": no enum member has matching value." },
	{ "ENUM_VARIABLE_WITHOUT_DEFAULT", 1, "The variable \"S0\" has an enum type and does not set an explicit default value. The default will be set to \"0\"." },
	{ "EMPTY_FILE", 1, "Empty script file." },
	{ "DEPRECATED_KEYWORD", 1, "The \"S0\" keyword is deprecated and will be removed in a future release. Please replace it with \"S1\"." },
	{ "CONFUSABLE_IDENTIFIER", 1, "The identifier \"S0\" has misleading characters and might be confused with something else." },
	{ "CONFUSABLE_LOCAL_DECLARATION", 1, "The S0 \"S1\" is declared below in the parent block." },
	{ "CONFUSABLE_LOCAL_USAGE", 1, "The identifier \"S0\" will be shadowed below in the block." },
	{ "CONFUSABLE_CAPTURE_REASSIGNMENT", 1, "Reassigning lambda capture does not modify the outer local variable \"S0\"." },
	{ "INFERENCE_ON_VARIANT", 2, "The S0 type is being inferred from a Variant value, so it will be typed as Variant." },
	{ "NATIVE_METHOD_OVERRIDE", 2, "The method \"S0()\" overrides a method from native class \"S1\". This won't be called by the engine and may not work as expected." },
	{ "GET_NODE_DEFAULT_WITHOUT_ONREADY", 2, "The default value uses \"S0\" which won't return nodes in the scene tree before \"_ready()\" is called. Use the \"@onready\" annotation to solve this." },
	{ "ONREADY_WITH_EXPORT", 2, "\"@onready\" will set the default value after \"@export\" takes effect and will override it." },
	{ "NON_EXHAUSTIVE_MATCH", 1, "The \"match\" statement does not cover all values of \"S0\". Unhandled: S1. Add the missing patterns or a \"_\" wildcard branch." },
	{ "MATCH_WITHOUT_DEFAULT", 0, "The \"match\" statement has no \"_\" wildcard branch; some values may go unhandled." },
	{ "OPEN_ENUM_MATCH_WITHOUT_DEFAULT", 1, "The \"match\" over \"S0\" does not handle: S1. \"S0\" is also carried by an integer that can hold values outside its declared members, so add an unguarded \"_\" or bind branch rather than the missing patterns alone." },
};
} // namespace barista_script::test
