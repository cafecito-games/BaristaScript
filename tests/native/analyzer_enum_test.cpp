/**************************************************************************/
/*  analyzer_enum_test.cpp                                                */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                          */
/*  This file is part of BaristaScript, a Godot GDExtension.                */
/*  SPDX-License-Identifier: MIT                                           */
/**************************************************************************/

#include "analyzer_helpers.h"
#include "bs_conformance_registry.h"
#include "storage_fixture.h"

using namespace godot;
using namespace barista_script;
using namespace barista_script::native_tests;

// Exact source/predicate migration of the 4b2439f legacy enum scenarios.
namespace {
void scenario_enum_case_match_and_case_binds() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String match_ok = "class_name EnumMatchOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\treturn dx + dy\n\t\t_:\n\t\t\treturn 0\n";
	const auto match_ok_report = analyze_source(match_ok, "res://tests/enum_match_ok.barista");
	CHECK_MESSAGE((match_ok_report.valid() == true), "Message.Move(dx, dy) match pattern is valid");
	const String match_arity = "class_name EnumMatchArity extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\tMessage.Move(dx):\n\t\t\tpass\n";
	const auto match_arity_report = analyze_source(match_arity, "res://tests/enum_match_arity.barista");
	CHECK_MESSAGE((match_arity_report.valid() == false), "Message.Move arity mismatch is invalid");
	bool saw_match_arity = false;
	for (const auto &error : match_arity_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("carries 2 payload value(s), but 1 pattern(s) were given")) {
			saw_match_arity = true;
		}
	}
	CHECK_MESSAGE((saw_match_arity), "ENUM_CASE payload arity diagnostic");
	const String wrong_subject = "class_name EnumMatchWrongSubject extends Node\nenum Message:\n\tMove(x: int, y: int)\nenum Other:\n\tGo(n: int)\nfunc handle(msg: Other) -> void:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\tpass\n";
	const auto wrong_subject_report = analyze_source(wrong_subject, "res://tests/enum_match_wrong_subject.barista");
	CHECK_MESSAGE((wrong_subject_report.valid() == false), "ENUM_CASE against unrelated subject is invalid");
	bool saw_wrong_subject = false;
	for (const auto &error : wrong_subject_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Pattern matches a case of") && message.contains("subject is of type")) {
			saw_wrong_subject = true;
		}
	}
	CHECK_MESSAGE((saw_wrong_subject), "ENUM_CASE subject-type mismatch diagnostic");
	const String case_bind_ok = "class_name EnumCaseBindOk extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> int:\n\tif msg is Message.Move(dx, dy):\n\t\treturn dx + dy\n\treturn 0\n";
	const auto case_bind_ok_report = analyze_source(case_bind_ok, "res://tests/enum_case_bind_ok.barista");
	CHECK_MESSAGE((case_bind_ok_report.valid() == true), "is Message.Move(dx, dy) case binds are valid");
	const String case_bind_arity = "class_name EnumCaseBindArity extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> void:\n\tif msg is Message.Move(dx):\n\t\tpass\n";
	const auto case_bind_arity_report = analyze_source(case_bind_arity, "res://tests/enum_case_bind_arity.barista");
	CHECK_MESSAGE((case_bind_arity_report.valid() == false), "case-bind arity mismatch is invalid");
	bool saw_bind_arity = false;
	for (const auto &error : case_bind_arity_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("carries 2 payload value(s), but 1 bind(s) were given")) {
			saw_bind_arity = true;
		}
	}
	CHECK_MESSAGE((saw_bind_arity), "case-bind payload arity diagnostic");
	const String not_case_bind = "class_name NotEnumCaseBind extends Node\nfunc handle(v: Variant) -> void:\n\tif v is Node(n):\n\t\tpass\n";
	const auto not_case_bind_report = analyze_source(not_case_bind, "res://tests/not_enum_case_bind.barista");
	CHECK_MESSAGE((not_case_bind_report.valid() == false), "non-enum case binds are invalid");
	bool saw_not_case = false;
	for (const auto &error : not_case_bind_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Only a tagged-union case can bind payload values")) {
			saw_not_case = true;
		}
	}
	CHECK_MESSAGE((saw_not_case), "non-enum case-bind diagnostic");
	const String array_ok = "class_name ArrayPatOk extends Node\nfunc handle(xs: Array[int]) -> int:\n\tmatch xs:\n\t\t[var a, var b]:\n\t\t\treturn a + b\n\t\t_:\n\t\t\treturn 0\n";
	const auto array_ok_report = analyze_source(array_ok, "res://tests/array_pat_ok.barista");
	CHECK_MESSAGE((array_ok_report.valid() == true), "Array[int] pattern binds are valid");
	const String dict_ok = "class_name DictPatOk extends Node\nfunc handle(d: Dictionary[String, int]) -> int:\n\tmatch d:\n\t\t{\"a\": var n}:\n\t\t\treturn n\n\t\t_:\n\t\t\treturn 0\n";
	const auto dict_ok_report = analyze_source(dict_ok, "res://tests/dict_pat_ok.barista");
	CHECK_MESSAGE((dict_ok_report.valid() == true), "Dictionary[String, int] pattern binds are valid");
	const String dict_key_bad = "class_name DictPatKeyBad extends Node\nfunc handle(d: Dictionary) -> void:\n\tvar k := \"a\"\n\tmatch d:\n\t\t{k: v}:\n\t\t\tpass\n";
	const auto dict_key_bad_report = analyze_source(dict_key_bad, "res://tests/dict_pat_key_bad.barista");
	CHECK_MESSAGE((dict_key_bad_report.valid() == false), "non-constant dictionary pattern key is invalid");
	bool saw_dict_key = false;
	for (const auto &error : dict_key_bad_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("dictionary pattern key must be a constant")) {
			saw_dict_key = true;
		}
	}
	CHECK_MESSAGE((saw_dict_key), "dictionary pattern key constant diagnostic");
	const String tuple_arity = "class_name TuplePatArity extends Node\nfunc handle(t: (int, String)) -> void:\n\tmatch t:\n\t\t(a, b, c):\n\t\t\tpass\n";
	const auto tuple_arity_report = analyze_source(tuple_arity, "res://tests/tuple_pat_arity.barista");
	CHECK_MESSAGE((tuple_arity_report.valid() == false), "tuple pattern arity mismatch is invalid");
	bool saw_tuple_arity = false;
	for (const auto &error : tuple_arity_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Tuple pattern has 3 element(s)") && message.contains("has 2")) {
			saw_tuple_arity = true;
		}
	}
	CHECK_MESSAGE((saw_tuple_arity), "tuple pattern arity diagnostic");
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(match_ok, "res://tests/enum_match_validate.barista", true, true, true, false);
	CHECK_MESSAGE((bool(validate_report.get("valid", false)) == true), "ENUM_CASE match remains valid under validate()");
	CHECK_MESSAGE((source_analyzes(match_ok, "res://tests/enum_match_is_valid.barista")), "ENUM_CASE match remains valid under is_semantically_valid()");
	CHECK_MESSAGE((index.get_record_count() == before), "analyze/validate/is_valid must not mutate declaration index for ENUM_CASE match");
}

void scenario_contextual_case_shorthand() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	const String match_payload = "class_name CtxMatchPayload extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\t.Move(dx, dy):\n\t\t\treturn dx + dy\n\t\t_:\n\t\t\treturn 0\n";
	const auto match_payload_report = analyze_source(match_payload, "res://tests/ctx_match_payload.barista");
	CHECK_MESSAGE((match_payload_report.valid() == true), ".Move(dx, dy) contextual match is valid");
	const String match_value = "class_name CtxMatchValue extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\t.Quit:\n\t\t\treturn 1\n\t\t_:\n\t\t\treturn 0\n";
	const auto match_value_report = analyze_source(match_value, "res://tests/ctx_match_value.barista");
	CHECK_MESSAGE((match_value_report.valid() == true), ".Quit contextual match is valid");
	const String match_arity = "class_name CtxMatchArity extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\t.Move(dx):\n\t\t\tpass\n";
	const auto match_arity_report = analyze_source(match_arity, "res://tests/ctx_match_arity.barista");
	CHECK_MESSAGE((match_arity_report.valid() == false), ".Move arity mismatch is invalid");
	bool saw_match_arity = false;
	for (const auto &error : match_arity_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("carries 2 payload value(s), but 1 pattern(s) were given")) {
			saw_match_arity = true;
		}
	}
	CHECK_MESSAGE((saw_match_arity), "contextual ENUM_CASE payload arity diagnostic");
	const String match_unknown = "class_name CtxMatchUnknown extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\t.Nope:\n\t\t\tpass\n";
	const auto match_unknown_report = analyze_source(match_unknown, "res://tests/ctx_match_unknown.barista");
	CHECK_MESSAGE((match_unknown_report.valid() == false), "unknown contextual case is invalid");
	bool saw_unknown = false;
	for (const auto &error : match_unknown_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Tagged union \"Message\" has no case \"Nope\"")) {
			saw_unknown = true;
		}
	}
	CHECK_MESSAGE((saw_unknown), "unknown contextual case diagnostic");
	const String match_bad_subject = "class_name CtxMatchBadSubject extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(n: int) -> void:\n\tmatch n:\n\t\t.Quit:\n\t\t\tpass\n";
	const auto match_bad_subject_report = analyze_source(match_bad_subject, "res://tests/ctx_match_bad_subject.barista");
	CHECK_MESSAGE((match_bad_subject_report.valid() == false), "contextual case on non-union subject is invalid");
	bool saw_bad_subject = false;
	for (const auto &error : match_bad_subject_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("needs a tagged-union match subject") && message.contains("type \"int\"")) {
			saw_bad_subject = true;
		}
	}
	CHECK_MESSAGE((saw_bad_subject), "non-union subject contextual shorthand diagnostic");
	const String is_ok = "class_name CtxIsOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tif msg is .Move(dx, dy):\n\t\treturn dx + dy\n\treturn 0\n";
	const auto is_ok_report = analyze_source(is_ok, "res://tests/ctx_is_ok.barista");
	CHECK_MESSAGE((is_ok_report.valid() == true), "is .Move(dx, dy) contextual case binds are valid");
	const String is_arity = "class_name CtxIsArity extends Node\nenum Message:\n\tMove(x: int, y: int)\nfunc handle(msg: Message) -> void:\n\tif msg is .Move(dx):\n\t\tpass\n";
	const auto is_arity_report = analyze_source(is_arity, "res://tests/ctx_is_arity.barista");
	CHECK_MESSAGE((is_arity_report.valid() == false), "contextual is-case arity mismatch is invalid");
	bool saw_is_arity = false;
	for (const auto &error : is_arity_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("carries 2 payload value(s), but 1 bind(s) were given")) {
			saw_is_arity = true;
		}
	}
	CHECK_MESSAGE((saw_is_arity), "contextual is-case payload arity diagnostic");
	const String is_bad_operand = "class_name CtxIsBadOperand extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(n: int) -> void:\n\tif n is .Quit:\n\t\tpass\n";
	const auto is_bad_operand_report = analyze_source(is_bad_operand, "res://tests/ctx_is_bad_operand.barista");
	CHECK_MESSAGE((is_bad_operand_report.valid() == false), "is .Quit on non-union operand is invalid");
	bool saw_bad_operand = false;
	for (const auto &error : is_bad_operand_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("needs a tagged-union \"is\" operand") && message.contains("type \"int\"")) {
			saw_bad_operand = true;
		}
	}
	CHECK_MESSAGE((saw_bad_operand), "non-union is-operand contextual shorthand diagnostic");
	const String match_missing = "class_name CtxMatchMissingSubject extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tmatch missing_ident:\n\t\t.Quit:\n\t\t\tpass\n\t\t.Move(x):\n\t\t\tpass\n";
	const auto match_missing_report = analyze_source(match_missing, "res://tests/ctx_match_missing_subject.barista");
	CHECK_MESSAGE((match_missing_report.valid() == false), "match missing_ident with contextual arms is invalid");
	bool saw_missing_ident = false;
	int cascade_count = 0;
	for (const auto &error : match_missing_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Identifier \"missing_ident\" not declared in the current scope.")) {
			saw_missing_ident = true;
		}
		if (message.contains("needs a tagged-union match subject")) {
			cascade_count += 1;
		}
	}
	CHECK_MESSAGE((saw_missing_ident), "match missing_ident subject diagnostic present");
	CHECK_MESSAGE((cascade_count == 0), "failed match subject must not cascade per-arm Variant tagged-union diagnostics");
	const String match_bare_payload = "class_name CtxMatchBarePayload extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\t.Move:\n\t\t\tpass\n";
	const auto match_bare_payload_report = analyze_source(match_bare_payload, "res://tests/ctx_match_bare_payload.barista");
	CHECK_MESSAGE((match_bare_payload_report.valid() == false), "bare .Move value pattern is invalid");
	bool saw_bare_payload = false;
	for (const auto &error : match_bare_payload_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("Case \"Move\" carries a payload, so it is matched with payload patterns")) {
			saw_bare_payload = true;
		}
	}
	CHECK_MESSAGE((saw_bare_payload), "bare .Move payload-form diagnostic");
	const String is_quit_ok = "class_name CtxIsQuitOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(msg: Message) -> bool:\n\treturn msg is .Quit\n";
	const auto is_quit_ok_report = analyze_source(is_quit_ok, "res://tests/ctx_is_quit_ok.barista");
	CHECK_MESSAGE((is_quit_ok_report.valid() == true), "is .Quit contextual tag test is valid");
	const String var_ok = "class_name CtxVarOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar idle: Message = .Quit\n\tvar moved: Message = .Move(1, 2)\n";
	const auto var_ok_report = analyze_source(var_ok, "res://tests/ctx_var_ok.barista");
	CHECK_MESSAGE((var_ok_report.valid() == true), "annotated var .Quit / .Move construction is valid");
	const String assign_ok = "class_name CtxAssignOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nvar member: Message = .Quit\nfunc handle() -> void:\n\tvar local: Message = .Quit\n\tlocal = .Move(3, 4)\n\tmember = .Quit\n";
	const auto assign_ok_report = analyze_source(assign_ok, "res://tests/ctx_assign_ok.barista");
	CHECK_MESSAGE((assign_ok_report.valid() == true), "assignment RHS .Case construction is valid");
	const String return_ok = "class_name CtxReturnOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc make_quit() -> Message:\n\treturn .Quit\nfunc make_move() -> Message:\n\treturn .Move(5, 6)\n";
	const auto return_ok_report = analyze_source(return_ok, "res://tests/ctx_return_ok.barista");
	CHECK_MESSAGE((return_ok_report.valid() == true), "return .Case construction is valid");
	const String param_default_ok = "class_name CtxParamDefaultOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(msg: Message = .Quit) -> Message:\n\treturn msg\n";
	const auto param_default_ok_report = analyze_source(param_default_ok, "res://tests/ctx_param_default_ok.barista");
	CHECK_MESSAGE((param_default_ok_report.valid() == true), "parameter default .Quit construction is valid");
	const String untyped = "class_name CtxUntyped extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar inferred = .Quit\n";
	const auto untyped_report = analyze_source(untyped, "res://tests/ctx_untyped.barista");
	CHECK_MESSAGE((untyped_report.valid() == false), "untyped .Quit without expected union is invalid");
	bool saw_annotate = false;
	for (const auto &error : untyped_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("needs an expected tagged-union type; annotate the target")) {
			saw_annotate = true;
		}
	}
	CHECK_MESSAGE((saw_annotate), "untyped contextual construction annotate-target diagnostic");
	const String wrong_expected = "class_name CtxWrongExpected extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar counter: int = .Quit\n";
	const auto wrong_expected_report = analyze_source(wrong_expected, "res://tests/ctx_wrong_expected.barista");
	CHECK_MESSAGE((wrong_expected_report.valid() == false), ".Quit into int expected type is invalid");
	bool saw_expects = false;
	for (const auto &error : wrong_expected_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("needs an expected tagged-union type, but this position expects \"int\"")) {
			saw_expects = true;
		}
	}
	CHECK_MESSAGE((saw_expects), "non-union expected-type contextual construction diagnostic");
	const String payload_form = "class_name CtxPayloadForm extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar bad: Message = .Move\n";
	const auto payload_form_report = analyze_source(payload_form, "res://tests/ctx_payload_form.barista");
	CHECK_MESSAGE((payload_form_report.valid() == false), "bare .Move construction is invalid");
	bool saw_payload_form = false;
	for (const auto &error : payload_form_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("carries a payload and must be constructed")) {
			saw_payload_form = true;
		}
	}
	CHECK_MESSAGE((saw_payload_form), "bare .Move construction payload-form diagnostic");
	const String arity_bad = "class_name CtxConstructArity extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar bad: Message = .Move(1)\n";
	const auto arity_bad_report = analyze_source(arity_bad, "res://tests/ctx_construct_arity.barista");
	CHECK_MESSAGE((arity_bad_report.valid() == false), ".Move arity mismatch construction is invalid");
	bool saw_construct_arity = false;
	for (const auto &error : arity_bad_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("expects 2 argument(s), but 1 were given")) {
			saw_construct_arity = true;
		}
	}
	CHECK_MESSAGE((saw_construct_arity), "contextual construction payload arity diagnostic");
	const String array_ok = "class_name CtxArrayOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar msgs: Array[Message] = [.Quit, .Move(1, 2)]\n";
	const auto array_ok_report = analyze_source(array_ok, "res://tests/ctx_array_ok.barista");
	CHECK_MESSAGE((array_ok_report.valid() == true), "Array[Message] element .Case construction is valid");
	const String array_nested = "class_name CtxArrayNested extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar nested: Array[Array[Message]] = [[.Quit, .Move(3)]]\n";
	const auto array_nested_report = analyze_source(array_nested, "res://tests/ctx_array_nested.barista");
	CHECK_MESSAGE((array_nested_report.valid() == true), "nested Array[Array[Message]] .Case construction is valid");
	const String dict_ok = "class_name CtxDictOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar by_id: Dictionary[int, Message] = {1: .Quit, 2: .Move(4)}\n\tvar labels: Dictionary[Message, String] = {.Quit: \"done\"}\n";
	const auto dict_ok_report = analyze_source(dict_ok, "res://tests/ctx_dict_ok.barista");
	CHECK_MESSAGE((dict_ok_report.valid() == true), "Dictionary key/value .Case construction is valid");
	const String cast_ok = "class_name CtxCastOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle() -> void:\n\tvar labeled = .Quit as Message\n\tvar moved = .Move(5, 6) as Message\n";
	const auto cast_ok_report = analyze_source(cast_ok, "res://tests/ctx_cast_ok.barista");
	CHECK_MESSAGE((cast_ok_report.valid() == true), "cast operand .Case construction is valid");
	const String cast_array = "class_name CtxCastArray extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar labeled = [.Quit, .Move(7)] as Array[Message]\n";
	const auto cast_array_report = analyze_source(cast_array, "res://tests/ctx_cast_array.barista");
	CHECK_MESSAGE((cast_array_report.valid() == true), "cast Array[Message] element .Case construction is valid");
	const String ternary_ok = "class_name CtxTernaryOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc choose(cond: bool) -> Message:\n\treturn .Quit if cond else .Move(8)\nfunc handle(cond: bool) -> void:\n\tvar chosen: Message = .Quit if cond else .Move(9)\n\tvar elements: Array[Message] = [.Quit if cond else .Move(10)]\n\tvar labeled = (.Quit if cond else .Move(11)) as Message\n";
	const auto ternary_ok_report = analyze_source(ternary_ok, "res://tests/ctx_ternary_ok.barista");
	CHECK_MESSAGE((ternary_ok_report.valid() == true), "ternary branch .Case construction is valid");
	const String call_arg_ok = "class_name CtxCallArgOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc consume(msg: Message) -> void:\n\tpass\nfunc handle(cond: bool) -> void:\n\tconsume(.Quit)\n\tconsume(.Quit if cond else .Move(12))\n";
	const auto call_arg_ok_report = analyze_source(call_arg_ok, "res://tests/ctx_call_arg_ok.barista");
	CHECK_MESSAGE((call_arg_ok_report.valid() == true), "call-argument .Case construction is valid");
	const String array_untyped = "class_name CtxArrayUntyped extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle() -> void:\n\tvar msgs = [.Quit]\n";
	const auto array_untyped_report = analyze_source(array_untyped, "res://tests/ctx_array_untyped.barista");
	CHECK_MESSAGE((array_untyped_report.valid() == false), "untyped array element .Quit is invalid");
	bool saw_array_annotate = false;
	for (const auto &error : array_untyped_report.parser->get_errors()) {
		const String &message = error.message;
		if (message.contains("needs an expected tagged-union type; annotate the target")) {
			saw_array_annotate = true;
		}
	}
	CHECK_MESSAGE((saw_array_annotate), "untyped array element contextual construction diagnostic");
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(match_payload, "res://tests/ctx_match_validate.barista", true, true, true, false);
	CHECK_MESSAGE((bool(validate_report.get("valid", false)) == true), "contextual match remains valid under validate()");
	CHECK_MESSAGE((source_analyzes(match_payload, "res://tests/ctx_match_is_valid.barista")), "contextual match remains valid under is_semantically_valid()");
	CHECK_MESSAGE((source_analyzes(var_ok, "res://tests/ctx_var_is_valid.barista")), "contextual assign construction remains valid under is_semantically_valid()");
	CHECK_MESSAGE((source_analyzes(array_ok, "res://tests/ctx_array_is_valid.barista")), "contextual array construction remains valid under is_semantically_valid()");
	CHECK_MESSAGE((source_analyzes(cast_ok, "res://tests/ctx_cast_is_valid.barista")), "contextual cast construction remains valid under is_semantically_valid()");
	CHECK_MESSAGE((index.get_record_count() == before), "analyze/validate/is_valid must not mutate declaration index for contextual case");
}

void scenario_tagged_union_match_exhaustiveness() {
	StorageFixture fixture;
	BSConformanceRegistry::ScopedCorpusState registry;
	AnalyzerSettings settings;
	settings.warnings_enabled(true);
	settings.warning(BSWarning::NON_EXHAUSTIVE_MATCH, BSWarning::WARN);
	settings.warning(BSWarning::OPEN_ENUM_MATCH_WITHOUT_DEFAULT, BSWarning::WARN);
	const String incomplete = "class_name TaggedMatchIncomplete extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> void:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\tpass\n";
	const Dictionary incomplete_report = BaristaScriptLanguage::get_singleton()->_validate(incomplete, "res://tests/tagged_match_incomplete.barista", true, true, true, false);
	CHECK_MESSAGE((bool(incomplete_report.get("valid", false)) == true), "non-exhaustive tagged-union void match stays valid (warning-only)");
	bool saw_non_exhaustive = false;
	bool saw_quit_uncovered = false;
	for (const Dictionary &warn : Array(incomplete_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("NON_EXHAUSTIVE_MATCH")) {
			saw_non_exhaustive = true;
		}
		if (String(warn.get("message", "")).contains("Quit")) {
			saw_quit_uncovered = true;
		}
	}
	CHECK_MESSAGE((saw_non_exhaustive), "tagged-union match emits NON_EXHAUSTIVE_MATCH");
	CHECK_MESSAGE((saw_quit_uncovered), "NON_EXHAUSTIVE_MATCH lists uncovered Quit case");
	const String incomplete_ret = "class_name TaggedMatchIncompleteRet extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\treturn dx + dy\n";
	const Dictionary incomplete_ret_report = BaristaScriptLanguage::get_singleton()->_validate(incomplete_ret, "res://tests/tagged_match_incomplete_ret.barista", true, true, true, false);
	CHECK_MESSAGE((bool(incomplete_ret_report.get("valid", true)) == false), "non-exhaustive tagged-union match / missing return is invalid");
	bool saw_flow = false;
	for (const Dictionary &err : Array(incomplete_ret_report.get("errors", Array()))) {
		if (String(err.get("message", "")).contains("Not all code paths return a value")) {
			saw_flow = true;
		}
	}
	CHECK_MESSAGE((saw_flow), "non-covering tagged-union match fails return-path flow finality");
	bool saw_ret_non_exhaustive = false;
	bool saw_ret_quit = false;
	for (const Dictionary &warn : Array(incomplete_ret_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("NON_EXHAUSTIVE_MATCH")) {
			saw_ret_non_exhaustive = true;
		}
		if (String(warn.get("message", "")).contains("Quit")) {
			saw_ret_quit = true;
		}
	}
	CHECK_MESSAGE((saw_ret_non_exhaustive), "flow-finality early exit still flushes NON_EXHAUSTIVE_MATCH");
	CHECK_MESSAGE((saw_ret_quit), "early-exit NON_EXHAUSTIVE_MATCH lists uncovered Quit case");
	const String exhaustive = "class_name TaggedMatchOk extends Node\nenum Message:\n\tMove(x: int, y: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\tMessage.Move(dx, dy):\n\t\t\treturn dx + dy\n\t\tMessage.Quit:\n\t\t\treturn 0\n";
	const auto exhaustive_report = analyze_source(exhaustive, "res://tests/tagged_match_ok.barista");
	CHECK_MESSAGE((exhaustive_report.valid() == true), "exhaustive tagged-union match with returns is valid");
	bool saw_exhaustive_warn = false;
	for (const BSWarning &warn : exhaustive_report.parser->get_warnings()) {
		if (warn.get_name().contains("NON_EXHAUSTIVE_MATCH")) {
			saw_exhaustive_warn = true;
		}
	}
	CHECK_MESSAGE((!saw_exhaustive_warn), "exhaustive tagged-union match has no NON_EXHAUSTIVE_MATCH");
	const String contextual_ok = "class_name TaggedMatchContextualOk extends Node\nenum Message:\n\tMove(x: int)\n\tQuit\nfunc handle(msg: Message) -> int:\n\tmatch msg:\n\t\t.Move(dx):\n\t\t\treturn dx\n\t\t.Quit:\n\t\t\treturn 0\n";
	const auto contextual_ok_report = analyze_source(contextual_ok, "res://tests/tagged_match_contextual_ok.barista");
	CHECK_MESSAGE((contextual_ok_report.valid() == true), "exhaustive contextual .Case match is valid");
	const String plain_enum = "class_name PlainEnumMatch extends Node\nenum Level:\n\tLow = 1\n\tHigh = 2\nfunc handle(level: Level) -> void:\n\tmatch level:\n\t\tLevel.Low:\n\t\t\tpass\n\t\tLevel.High:\n\t\t\tpass\n";
	const Dictionary plain_enum_report = BaristaScriptLanguage::get_singleton()->_validate(plain_enum, "res://tests/plain_enum_match.barista", true, true, true, false);
	CHECK_MESSAGE((bool(plain_enum_report.get("valid", false)) == true), "plain-enum match stays valid (warning-only)");
	bool saw_open_enum = false;
	for (const Dictionary &warn : Array(plain_enum_report.get("warnings", Array()))) {
		if (String(warn.get("string_code", "")).contains("OPEN_ENUM_MATCH_WITHOUT_DEFAULT")) {
			saw_open_enum = true;
		}
	}
	CHECK_MESSAGE((saw_open_enum), "plain enum match emits OPEN_ENUM_MATCH_WITHOUT_DEFAULT");
	const String bool_ok = "class_name BoolMatchStillOk extends Node\nfunc check(flag: bool) -> int:\n\tmatch flag:\n\t\ttrue:\n\t\t\treturn 1\n\t\tfalse:\n\t\t\treturn 0\n";
	const auto bool_ok_report = analyze_source(bool_ok, "res://tests/bool_match_still_ok.barista");
	CHECK_MESSAGE((bool_ok_report.valid() == true), "exhaustive bool match remains valid after exhaustiveness port");
	BSDeclarationIndex &index = fixture.index();
	const int before = index.get_record_count();
	const Dictionary validate_report = BaristaScriptLanguage::get_singleton()->_validate(exhaustive, "res://tests/tagged_match_validate.barista", true, true, true, false);
	CHECK_MESSAGE((bool(validate_report.get("valid", false)) == true), "exhaustive tagged match remains valid under validate()");
	CHECK_MESSAGE((source_analyzes(exhaustive, "res://tests/tagged_match_is_valid.barista")), "exhaustive tagged match remains valid under is_semantically_valid()");
	CHECK_MESSAGE((index.get_record_count() == before), "analyze/validate/is_valid must not mutate declaration index for tagged exhaustiveness");
}
} // namespace

TEST_SUITE("analyzer_enum") {
	TEST_CASE("enum_case_match_and_case_binds") { scenario_enum_case_match_and_case_binds(); }
	TEST_CASE("contextual_case_shorthand") { scenario_contextual_case_shorthand(); }
	TEST_CASE("tagged_union_match_exhaustiveness") { scenario_tagged_union_match_exhaustiveness(); }
	TEST_CASE("normal_reversed_shuffled_cases_restore_ambient_state") {
		check_scenario_orders({ scenario_enum_case_match_and_case_binds, scenario_contextual_case_shorthand, scenario_tagged_union_match_exhaustiveness });
	}
}
