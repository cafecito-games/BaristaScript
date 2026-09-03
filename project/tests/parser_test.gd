# parser_test.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT
extends SceneTree

## Fail-closed contract, integrity and vocabulary tests for the parser
## (src/bs_parser.{h,cpp}, src/bs_parser_data_type.cpp), driven through the
## BaristaScriptParserProbe handle.
##
## The parser is engine-backed C++ -- its String, StringName and Variant
## dependencies only exist inside a loaded Godot runtime -- so this is the only
## place it can be exercised. Every row of issue #9's fail-closed table has a
## named test here.
##
## This suite deliberately holds no corpus. Issue #10 imports the 340 real
## parser cases; what is asserted here is the contract this port owes them.

## Printed only when every assertion passed. CI greps for it because a GDScript
## parse error exits 0 and would otherwise read as a pass.
const SUCCESS_SENTINEL := "BS_PARSER_OK"

const PATH := "res://tests/parser_fixture.barista"

## Node kinds no source in this suite can produce, each with the reason. The
## vocabulary-closure test requires every Node::Type enumerator to be either
## produced by a fixture below or named here, so a new node kind cannot be added
## without someone deciding which of the two it is.
const UNREACHABLE_NODE_TYPES := {
	"NONE":
		"The default-constructed kind. No allocated node ever carries it; a node that did would be a parser bug.",
	"NODE_TYPE_MAX":
		"The vocabulary bound, not a node kind.",
	"CONFORMANCE":
		"A retroactive `extend` declaration (docs/GRAMMAR.md section 4.8). Exercised by issue #10's corpus, which brings conformance cases; a single one here would not add coverage this suite can check.",
}


func _initialize() -> void:
	var probe := BaristaScriptParserProbe.new()
	var failures: Array[String] = []

	# The fail-closed table.
	_test_undecodable_source_produces_no_tree(probe, failures)
	_test_tokenizer_diagnostic_reaches_the_parser(probe, failures)
	_test_a_leading_tokenizer_diagnostic_has_a_real_position(probe, failures)
	_test_a_colon_without_a_type_is_rejected(probe, failures)
	_test_a_default_value_marker_without_an_expression_is_rejected(probe, failures)
	_test_syntax_error_recovers_and_marks_the_tree_incomplete(probe, failures)
	_test_import_before_namespace_names_the_rule(probe, failures)
	_test_namespace_used_twice_is_rejected(probe, failures)
	_test_global_name_kinds_are_mutually_exclusive(probe, failures)
	_test_final_trait_is_rejected(probe, failures)
	_test_removed_type_spelling_reports_once_from_one_definition(probe, failures)
	_test_removed_type_spelling_in_every_parser_owned_type_position(probe, failures)
	_test_token_buffer_from_another_format_is_refused(probe, failures)
	_test_deep_nesting_is_a_diagnostic_not_a_crash(probe, failures)

	# Integrity and idempotency.
	_test_parsing_twice_is_identical(probe, failures)
	_test_cold_and_cache_warm_parses_agree(probe, failures)
	_test_two_parsers_do_not_interfere(probe, failures)
	_test_a_reused_parser_carries_nothing_over(probe, failures)
	_test_positions_are_one_based_and_end_exclusive(probe, failures)

	# GRAMMAR section 5: the precedence table, asserted against explicit grouping.
	_test_precedence_and_associativity(probe, failures)
	_test_bracketed_types_span_lines(probe, failures)

	# Vocabulary closure.
	_test_node_type_vocabulary_is_closed(probe, failures)
	_test_node_type_names_are_distinct(probe, failures)

	for failure in failures:
		push_error(failure)
	# A GDScript parse error makes SceneTree quit 0, so CI greps for this sentinel
	# rather than trusting the exit code: it can only be printed by a suite that
	# actually loaded and ran.
	if failures.is_empty():
		print("%s %d test groups passed" % [SUCCESS_SENTINEL, 23])
	quit(0 if failures.is_empty() else 1)


# ---------------------------------------------------------------------------
# The fail-closed table
# ---------------------------------------------------------------------------


## Source that is not valid UTF-8 never reaches the parser. What matters is that
## the failure is reported rather than absorbed into U+FFFD, and that no tree is
## presented at all -- not an empty one that reads as complete.
func _test_undecodable_source_produces_no_tree(probe, failures: Array[String]) -> void:
	var report: Dictionary = probe.parse_text(PackedByteArray([0xC3, 0x28]), PATH)
	_expect(failures, not report["complete"], "undecodable source: reported as complete")
	_expect(failures, not report["has_tree"], "undecodable source: presented a tree")
	_expect(failures, report["diagnostics"].size() == 1, "undecodable source: expected exactly one diagnostic")
	_expect(failures, (report["nodes"] as PackedStringArray).is_empty(), "undecodable source: allocated nodes")


## A source the tokenizer refuses reaches the parser as an ERROR token. The
## parser reports the tokenizer's own message and never presents the tree as
## complete.
func _test_tokenizer_diagnostic_reaches_the_parser(probe, failures: Array[String]) -> void:
	# Each of these is a tokenizer diagnostic, not a parser one. Exactly one diagnostic must come
	# back: the parser reports what the tokenizer said and adds no differently worded complaint of
	# its own about the same token, which is the fail-closed row for a removed D1 spelling and holds
	# for every lexical rejection.
	var lexical_failures := [
		"var name = \"unterminated\n",
		"var count = 1L\n",
		"var count = 1UL\n",
		"var count = 99999999999999999999\n",
		"func f() -> ulong:\n\tpass\n",
		"var cast = 1 as long\n",
		"var tested = 1 is uint\n",
	]
	for source in lexical_failures:
		var report := _parse(probe, source)
		_expect(failures, not report["complete"],
			"tokenizer diagnostic: reported as complete: %s" % source.strip_edges())
		_expect(failures, report["error"] != OK,
			"tokenizer diagnostic: returned OK: %s" % source.strip_edges())
		_expect(failures, report["tokenizer_failed"],
			"tokenizer diagnostic: the run does not report a lexical failure: %s" % source.strip_edges())
		_expect(failures, report["diagnostics"].size() == 1,
			"tokenizer diagnostic: expected exactly one, got %s for %s" % [report["diagnostics"], source.strip_edges()])

	# The other side. `tokenizer_failed` names a *lexical* failure, so a source that tokenizes
	# cleanly must never claim one, however broken it is afterwards -- including a reserved spelling
	# in a type position the parser owns, which lexes perfectly well as `RESERVED_TYPE_NAME` and is
	# rejected by the parser.
	var lexically_clean := [
		"func a( -> void:\n\tpass\n",
		"var value: uint = 0\n",
		"var typed: Array[ulong] = []\n",
		"type Alias = long\n",
	]
	for source in lexically_clean:
		var report := _parse(probe, source)
		_expect(failures, not report["complete"],
			"a rejected source must not be reported as complete: %s" % source.strip_edges())
		_expect(failures, not report["tokenizer_failed"],
			"a lexically clean source must not be reported as a lexical failure: %s -- %s"
			% [source.strip_edges(), report["diagnostics"]])


## A diagnostic about the very first token has no preceding token to be anchored
## to. Reporting it at 0:0 would be a position no source has, which every consumer
## that jumps to a diagnostic then has to special-case.
func _test_a_leading_tokenizer_diagnostic_has_a_real_position(probe, failures: Array[String]) -> void:
	var report := _parse(probe, "\"unterminated")
	_expect(failures, not report["complete"], "a leading tokenizer error: reported as complete")
	_expect(failures, report["diagnostics"].size() > 0, "a leading tokenizer error: none reported")
	for diagnostic in report["diagnostics"]:
		var span: String = (diagnostic as String).split("\t")[0]
		_expect(failures, not span.begins_with("0:"),
			"a leading tokenizer diagnostic is anchored at line 0: %s" % diagnostic)


## A `:` followed by neither a type nor the `=` that means "infer from the value"
## is a missing type. The three declaration forms that admit an annotation must
## answer alike; upstream reports it for `var` and silently accepts it for `const`
## and for a parameter, which produced an untyped declaration from invalid source.
func _test_a_colon_without_a_type_is_rejected(probe, failures: Array[String]) -> void:
	for source in ["var declared: = 1\n", "const DECLARED: = 1\n", "func f(value:= 1) -> void:\n\tpass\n"]:
		var inferred := _parse(probe, source)
		_expect(failures, inferred["complete"],
			"the inferred form must stay accepted: %s -- %s" % [source.strip_edges(), inferred["diagnostics"]])

	# A `var` whose `:` is followed by a NEWLINE is the property-accessor form (docs/GRAMMAR.md
	# section 4.4), a different construct with its own diagnostic, so the `var` case is spelled with
	# a `;` -- which ends the statement and leaves the annotation with nothing to annotate.
	for source in ["var declared:;\n", "const DECLARED:\n", "func f(value:) -> void:\n\tpass\n"]:
		var report := _parse(probe, source)
		_expect(failures, not report["complete"],
			"a `:` with no type must be rejected: %s" % source.strip_edges())
		_expect(failures, _any_diagnostic_contains(report, "Expected type"),
			"a `:` with no type must name the missing type: %s -- %s" % [source.strip_edges(), report["diagnostics"]])


## `parse_expression()` returns null to mean "no prefix rule matched" and leaves
## the diagnostic to its caller by contract (see `parse_precedence`). A caller that
## forgets turns malformed source into a well-formed node -- here, a parameter that
## asked for a default value and got none.
func _test_a_default_value_marker_without_an_expression_is_rejected(probe, failures: Array[String]) -> void:
	for source in ["func f(value =) -> void:\n\tpass\n", "signal changed(value =)\n", "func f(value: int =) -> void:\n\tpass\n"]:
		var report := _parse(probe, source)
		_expect(failures, not report["complete"],
			"a default-value marker with no expression must be rejected: %s" % source.strip_edges())
		_expect(failures, _any_diagnostic_contains(report, "Expected expression"),
			"a default-value marker with no expression must say so: %s -- %s" % [source.strip_edges(), report["diagnostics"]])

	# A signal parameter is rejected for having a default at all, so only the function form is a
	# positive control here.
	for source in ["func f(value = 1) -> void:\n\tpass\n", "func f(value: int = 1) -> void:\n\tpass\n"]:
		var accepted := _parse(probe, source)
		_expect(failures, accepted["complete"],
			"a real default value must stay accepted: %s -- %s" % [source.strip_edges(), accepted["diagnostics"]])


## The parser recovers from a syntax error and keeps building, which is what
## makes completion and diagnostics usable on broken source. The tree it hands
## back must be distinguishable from a clean one.
func _test_syntax_error_recovers_and_marks_the_tree_incomplete(probe, failures: Array[String]) -> void:
	var clean := _parse(probe, "func a() -> void:\n\tpass\n\nfunc b() -> void:\n\tpass\n")
	_expect(failures, clean["complete"], "clean source: reported as incomplete: %s" % [clean["diagnostics"]])

	var broken := _parse(probe, "func a( -> void:\n\tpass\n\nfunc b() -> void:\n\tpass\n")
	_expect(failures, not broken["complete"], "recovered tree: reported as complete")
	_expect(failures, broken["has_tree"], "recovered tree: no tree produced at all")
	# Recovery means the declaration after the error is still parsed.
	_expect(failures, (broken["node_types"] as PackedStringArray).has("FUNCTION"),
		"recovered tree: stopped at the error instead of recovering")


## GRAMMAR section 3, rule 3: `import` follows `namespace`.
func _test_import_before_namespace_names_the_rule(probe, failures: Array[String]) -> void:
	var ordered := _parse(probe, "namespace game\nimport engine\n")
	_expect(failures, ordered["complete"], "namespace then import: rejected: %s" % [ordered["diagnostics"]])

	var reversed := _parse(probe, "import engine\nnamespace game\n")
	_expect(failures, not reversed["complete"], "import before namespace: accepted")
	_expect(failures, _any_diagnostic_contains(reversed, "import"),
		"import before namespace: the diagnostic does not name the rule: %s" % [reversed["diagnostics"]])


## GRAMMAR section 3, rule 2: `namespace` at most once.
func _test_namespace_used_twice_is_rejected(probe, failures: Array[String]) -> void:
	var report := _parse(probe, "namespace game\nnamespace other\n")
	_expect(failures, not report["complete"], "two namespace declarations: accepted")


## GRAMMAR section 3, rule 4: the four global-name forms are mutually exclusive.
func _test_global_name_kinds_are_mutually_exclusive(probe, failures: Array[String]) -> void:
	var pairs := [
		["class_name Alpha\ntrait_name Beta\n", "class_name + trait_name"],
		["class_name Alpha\nenum_name Beta\n", "class_name + enum_name"],
		["trait_name Alpha\ntuple_name Beta(x: int)\n", "trait_name + tuple_name"],
		["enum_name Alpha\ntuple_name Beta(x: int)\n", "enum_name + tuple_name"],
	]
	for pair in pairs:
		var report := _parse(probe, pair[0])
		_expect(failures, not report["complete"], "%s in one file: accepted" % pair[1])


## GRAMMAR section 3, rule 4: a trait cannot be `final`.
func _test_final_trait_is_rejected(probe, failures: Array[String]) -> void:
	var report := _parse(probe, "final trait_name Alpha\n")
	_expect(failures, not report["complete"], "final trait_name: accepted")
	_expect(failures, _any_diagnostic_contains(report, "final"),
		"final trait_name: the diagnostic does not name the rule: %s" % [report["diagnostics"]])


## A D1-removed spelling in a position the token stream itself settles is already
## a tokenizer diagnostic. The parser must not add a second, differently worded
## one for the same token.
func _test_removed_type_spelling_reports_once_from_one_definition(probe, failures: Array[String]) -> void:
	var expected: String = probe.removed_type_name_diagnostic("uint")
	# `as` is a type position the *tokenizer* settles, so it rejects and consumes the token itself.
	# That is where the parser could add a second, differently worded complaint about the same
	# token, and the contract says it must not.
	var report := _parse(probe, "var value = 1 as uint\n")
	var matching := 0
	for diagnostic in report["diagnostics"]:
		if (diagnostic as String).ends_with(expected):
			matching += 1
	_expect(failures, matching == 1,
		"`1 as uint`: expected exactly one removed-type-name diagnostic, got %d: %s" % [matching, report["diagnostics"]])
	_expect(failures, report["diagnostics"].size() == 1,
		"`1 as uint`: a second, different diagnostic was added: %s" % [report["diagnostics"]])


## The type positions only a parser can see. Each must be rejected, and with the
## one message BSTokenizer::removed_type_name_diagnostic() defines.
func _test_removed_type_spelling_in_every_parser_owned_type_position(probe, failures: Array[String]) -> void:
	var positions := {
		# The `:` annotation positions, which the tokenizer hands over because a `:` may also begin
		# a single-line suite (docs/GRAMMAR.md section 6).
		"var declared: uint = 1\n": "uint",
		"const DECLARED: ulong = 1\n": "ulong",
		"func f(value: long) -> void:\n\tpass\n": "long",
		"signal changed(value: uint)\n": "uint",
		# The positions no token-stream rule could reach at all.
		"var a: Array[uint] = []\n": "uint",
		"var b: Dictionary[String, ulong] = {}\n": "ulong",
		"type Alias = long\n": "long",
		"type uint = int\n": "uint",
		"class_name uint\n": "uint",
		"trait_name ulong\n": "ulong",
		"class Inner extends uint:\n\tpass\n": "uint",
		"class Box[uint]:\n\tpass\n": "uint",
		"tuple Pair(x: int, y: int)\nclass long:\n\tpass\n": "long",
	}
	for source in positions:
		var spelling: String = positions[source]
		var expected: String = probe.removed_type_name_diagnostic(spelling)
		var report := _parse(probe, source)
		_expect(failures, not report["complete"], "reserved type name accepted in: %s" % source.strip_edges())
		_expect(failures, _any_diagnostic_ends_with(report, expected),
			"reserved type name in `%s`: wrong diagnostic: %s" % [source.strip_edges(), report["diagnostics"]])


## A token buffer this build did not write is refused rather than misread. The
## cache's own fail-closed table owns the store; this is the parser's side of it:
## a payload whose schema does not match is never consumed.
func _test_token_buffer_from_another_format_is_refused(probe, failures: Array[String]) -> void:
	var buffer: PackedByteArray = probe.tokenize_to_buffer("func a() -> void:\n\tpass\n".to_utf8_buffer(), false)
	_expect(failures, buffer.size() > 8, "token buffer: nothing written")

	var corrupted := buffer.duplicate()
	# The version field follows the 4-byte magic.
	corrupted[4] = (corrupted[4] + 1) & 0xFF
	var report: Dictionary = probe.parse_token_buffer(corrupted, PATH)
	_expect(failures, report["error"] != OK, "token buffer with a foreign version: consumed")
	_expect(failures, not report["complete"], "token buffer with a foreign version: reported as complete")

	var truncated := buffer.slice(0, buffer.size() / 2)
	var truncated_report: Dictionary = probe.parse_token_buffer(truncated, PATH)
	_expect(failures, truncated_report["error"] != OK, "truncated token buffer: consumed")


## Pathological nesting is a diagnostic, not a stack overflow. The depth comes
## from BSParser::MAX_NESTING_DEPTH so the bound has one definition.
func _test_deep_nesting_is_a_diagnostic_not_a_crash(probe, failures: Array[String]) -> void:
	var depth: int = probe.max_nesting_depth() + 8
	var cases := [
		[probe.nested_source("var deep = ", "(", ")", "1\n", depth), "expression"],
		[probe.nested_source("var deep: Array[", "Array[", "]", "int]\n", depth), "type"],
	]
	for case in cases:
		var report: Dictionary = probe.parse_text(case[0], PATH)
		_expect(failures, not report["complete"], "deep %s nesting: accepted" % case[1])

	# The statement guard needs real indentation rather than a symmetric wrapper.
	var statement_source := "func f() -> void:\n"
	for level in depth:
		statement_source += "\t".repeat(level + 1) + "if true:\n"
	statement_source += "\t".repeat(depth + 1) + "pass\n"
	var statement_report := _parse(probe, statement_source)
	_expect(failures, not statement_report["complete"], "deep statement nesting: accepted")
	_expect(failures, _any_diagnostic_contains(statement_report, "too deep"),
		"deep statement nesting: not reported as a depth limit: %s" % [statement_report["diagnostics"]])


# ---------------------------------------------------------------------------
# Integrity and idempotency
# ---------------------------------------------------------------------------


func _test_parsing_twice_is_identical(probe, failures: Array[String]) -> void:
	var source := _rich_source()
	var first := _parse(probe, source)
	var second := _parse(probe, source)
	_expect(failures, first["nodes"] == second["nodes"], "parsing twice: node list or positions differ")
	_expect(failures, first["diagnostics"] == second["diagnostics"], "parsing twice: diagnostics differ")
	_expect(failures, first["tree"] == second["tree"], "parsing twice: rendered tree differs")


## The cache's contract, asserted from the parser's side: replaying a compiled
## token buffer must produce the same tree as reading the source did.
##
## "The same tree" is asserted exactly as far as the compiled format carries it.
## The buffer stores one line per token and a column only for the first token of
## each line, which is all the INDENT/DEDENT regeneration needs
## (src/bs_tokenizer_buffer.cpp, `token_lines` / `token_columns`); it is Foundry's
## format and the reason a shipped buffer is small. So a replayed tree has the
## same node kinds, in the same order, with the same *start lines*, and it does
## not have the source's columns or end positions. Asserting equality of the full
## span here would assert something the format cannot deliver, and asserting only
## the node kinds would let a real reordering pass, so both halves are checked.
##
## The AST-payload half of the cache -- serializing a parse tree into
## BSParseCache's opaque payload and reading it back -- lands with M3, which owns
## the serializer. When it does, that path can assert full span equality.
func _test_cold_and_cache_warm_parses_agree(probe, failures: Array[String]) -> void:
	var source := _rich_source()
	var cold := _parse(probe, source)
	_expect(failures, cold["complete"], "cold parse of the rich fixture failed: %s" % [cold["diagnostics"]])
	_expect(failures, (cold["node_types"] as PackedStringArray).size() >= 20,
		"the rich fixture exercises only %d node kinds; at least 20 are required" % (cold["node_types"] as PackedStringArray).size())

	for compress in [false, true]:
		var buffer: PackedByteArray = probe.tokenize_to_buffer(source.to_utf8_buffer(), compress)
		var warm: Dictionary = probe.parse_token_buffer(buffer, PATH)
		_expect(failures, warm["complete"], "cache-warm parse (compress=%s) failed: %s" % [compress, warm["diagnostics"]])
		_expect(failures, _node_kinds(cold) == _node_kinds(warm),
			"cold and cache-warm parses (compress=%s) disagree on node kinds or their order" % compress)
		_expect(failures, _node_start_lines(cold) == _node_start_lines(warm),
			"cold and cache-warm parses (compress=%s) disagree on node start lines" % compress)


## No global mutable parser state: a parse of another source in between changes
## nothing about this one.
func _test_two_parsers_do_not_interfere(probe, failures: Array[String]) -> void:
	var source := _rich_source()
	var before := _parse(probe, source)
	var _other := _parse(probe, "class_name Other\nvar x := 1\n")
	var _broken := _parse(probe, "func ( bad\n")
	var after := _parse(probe, source)
	_expect(failures, before["nodes"] == after["nodes"], "an unrelated parse changed this one's nodes")
	_expect(failures, before["diagnostics"] == after["diagnostics"], "an unrelated parse changed this one's diagnostics")


## A parser is reusable: nothing from one run reaches the next. The probe builds a
## fresh `BSParser` per call, so this drives the reuse through the one path that
## shares an instance -- the token-buffer replay of a source parsed cold first.
func _test_a_reused_parser_carries_nothing_over(probe, failures: Array[String]) -> void:
	var broken := "func a( -> void:\n\tpass\n"
	var clean := "func a() -> void:\n\tpass\n"

	var buffer: PackedByteArray = probe.tokenize_to_buffer(clean.to_utf8_buffer(), false)
	var reports: Array = probe.reused_parse_reports(broken.to_utf8_buffer(), buffer, PATH)
	_expect(failures, reports.size() == 2, "the reuse probe did not return both reports")
	if reports.size() != 2:
		return

	var first: Dictionary = reports[0]
	var second: Dictionary = reports[1]
	_expect(failures, not first["complete"], "the broken control parsed cleanly")
	_expect(failures, second["complete"],
		"the second parse carried diagnostics from the first: %s" % [second["diagnostics"]])
	_expect(failures, not second["tokenizer_failed"], "the second parse claimed the first run's lexical failure")

	var fresh: Dictionary = probe.parse_token_buffer(buffer, PATH)
	_expect(failures, _node_kinds(second) == _node_kinds(fresh),
		"the second parse exposed nodes from the first")


## Positions keep the tokenizer's convention unchanged across the boundary:
## 1-based line and column, start inclusive, end exclusive.
func _test_positions_are_one_based_and_end_exclusive(probe, failures: Array[String]) -> void:
	# `var x = 1` on line 1: the VARIABLE node starts at column 1 and ends one
	# past the last character of the declaration.
	var report := _parse(probe, "var x = 1\n")
	var variable_span := _span_of(report, "VARIABLE")
	_expect(failures, variable_span == "1:1-1:10",
		"`var x = 1` VARIABLE span: expected 1:1-1:10, got %s (nodes: %s)" % [variable_span, report["nodes"]])

	var literal_span := _span_of(report, "LITERAL")
	_expect(failures, literal_span == "1:9-1:10",
		"`var x = 1` LITERAL span: expected 1:9-1:10, got %s" % literal_span)

	# Every node built from tokens carries 1-based positions. The head class is the one exception
	# and is deliberately so: it is synthetic, anchored before the first token exists, and upstream
	# anchors it the same way (fs_parser.cpp, `parse()` allocates it before the first `advance()`).
	# Asserting it away would be inventing a position rather than preserving one.
	for node in report["nodes"]:
		var parts: PackedStringArray = (node as String).split("\t")
		if parts[0] == "CLASS":
			continue
		var span: String = parts[1]
		for component in span.replace("-", ":").split(":"):
			_expect(failures, component.to_int() >= 1, "a node carries a zero-based position: %s" % node)


# ---------------------------------------------------------------------------
# GRAMMAR section 5
# ---------------------------------------------------------------------------


## Every precedence relation and associativity the table in docs/GRAMMAR.md
## section 5.2 states, asserted the only way that cannot restate the table: the
## unparenthesized form must parse to exactly the tree the explicitly grouped
## form does, and to a different tree from the wrongly grouped one.
func _test_precedence_and_associativity(probe, failures: Array[String]) -> void:
	# [unparenthesized, the grouping it must equal, the grouping it must differ from]
	var cases := [
		# Relative precedence, adjacent levels of the table, tighter first.
		["a or b and c", "a or (b and c)", "(a or b) and c"],
		["a and b in c", "a and (b in c)", "(a and b) in c"],
		["a in b == c", "a in (b == c)", "(a in b) == c"],
		["a == b | c", "a == (b | c)", "(a == b) | c"],
		["a | b ^ c", "a | (b ^ c)", "(a | b) ^ c"],
		["a ^ b & c", "a ^ (b & c)", "(a ^ b) & c"],
		["a & b << c", "a & (b << c)", "(a & b) << c"],
		["a << b + c", "a << (b + c)", "(a << b) + c"],
		["a + b * c", "a + (b * c)", "(a + b) * c"],
		["a * b ** c", "a * (b ** c)", "(a * b) ** c"],
		# `is` sits above `**` in the table, so it binds tighter -- the surprising direction, which
		# is exactly why it is asserted.
		["a ** b is int", "a ** (b is int)", "(a ** b) is int"],
		["-a ** b", "-(a ** b)", "(-a) ** b"],
		["~a * b", "(~a) * b", "~(a * b)"],
		["not a == b", "not (a == b)", "(not a) == b"],
		["a.b(c)", "(a.b)(c)", "a.(b(c))"],
		# Left associativity: the same operator repeated groups leftward.
		["a - b - c", "(a - b) - c", "a - (b - c)"],
		["a / b / c", "(a / b) / c", "a / (b / c)"],
		["a << b << c", "(a << b) << c", "a << (b << c)"],
		["a ** b ** c", "(a ** b) ** c", "a ** (b ** c)"],
	]
	for case in cases:
		var plain := _expression_tree(probe, case[0])
		var same := _expression_tree(probe, case[1])
		var different := _expression_tree(probe, case[2])
		if plain.is_empty() or same.is_empty():
			failures.append("precedence: `%s` or `%s` did not parse" % [case[0], case[1]])
			continue
		_expect(failures, plain == same,
			"precedence: `%s` does not parse as `%s`" % [case[0], case[1]])
		if not different.is_empty():
			_expect(failures, plain != different,
				"precedence: `%s` parses the same as `%s`, so the levels are not distinguished" % [case[0], case[1]])

	# The note under the table -- `**` binds tighter than unary sign -- holds for a unary operator,
	# which `-a ** b` above asserts. It does not hold for a *literal* operand, and deliberately not:
	# the tokenizer folds a leading sign into a numeric literal when the preceding token cannot end a
	# value (docs/GRAMMAR.md section 2.6.1's last two rules, and `can_precede_bin_op()`), so `-2`
	# never becomes a unary operator for `**` to bind tighter than. `-2 ** 2` is therefore
	# `(-2) ** 2`, as it is in Foundry Script, whose tokenizer this is a port of. The example in
	# section 5.2's note reads the other way; the discrepancy is recorded rather than papered over,
	# and this asserts what the implementation actually does so a silent change cannot pass.
	_expect(failures, _expression_tree(probe, "-2 ** 2") == _expression_tree(probe, "(-2) ** 2"),
		"literal sign folding: `-2 ** 2` no longer parses as `(-2) ** 2`")
	_expect(failures, _expression_tree(probe, "-2 ** 2") != _expression_tree(probe, "-(2 ** 2)"),
		"literal sign folding: `-2 ** 2` now parses as `-(2 ** 2)`, which the tokenizer's folding rules out")

	# The ternary is right-associative.
	_expect(failures,
		_expression_tree(probe, "a if b else c if d else e") == _expression_tree(probe, "a if b else (c if d else e)"),
		"associativity: the ternary is not right-associative")


## Inside an open bracket pair the tokenizer is in multiline mode: newlines and
## indentation are ignored and no layout tokens are produced (docs/GRAMMAR.md
## section 2.1). Expression brackets get that from `parse_precedence`; a type's
## argument list has to ask for it, and every bracketed type form asks.
##
## The trailing `var after = 1` is load-bearing: the failure mode is not only that
## the bracketed type is rejected, but that the tokenizer is left with an indent
## level nothing closes, so the *next* declaration disappears too.
func _test_bracketed_types_span_lines(probe, failures: Array[String]) -> void:
	var forms := [
		"var values: Array[\n\tint\n] = []\n",
		"var table: Dictionary[\n\tString,\n\tint\n] = {}\n",
		"var call: Callable[[\n\tint\n], void]\n",
		"var sig: Signal[[\n\tint\n]]\n",
		"var work: Coroutine[\n\tint\n]\n",
		"var handle: Type[\n\tNode\n]\n",
		"var nested: Array[\n\tArray[\n\t\tint\n\t]\n]\n",
	]
	for form in forms:
		var source: String = form + "var after = 1\n"
		var report := _parse(probe, source)
		_expect(failures, report["complete"],
			"a bracketed type spanning lines must be accepted: %s -- %s" % [form.strip_edges(), report["diagnostics"]])
		# The declaration after it must survive: an unbalanced multiline mode swallows it.
		var variables := 0
		for node in report["nodes"]:
			if (node as String).begins_with("VARIABLE\t"):
				variables += 1
		_expect(failures, variables == 2,
			"the declaration after a multiline bracketed type was lost: %s" % form.strip_edges())

	# The single-line spelling must keep parsing exactly as it did.
	var single := _parse(probe, "var values: Array[int] = []\nvar after = 1\n")
	_expect(failures, single["complete"], "the single-line form regressed: %s" % [single["diagnostics"]])


# ---------------------------------------------------------------------------
# Vocabulary closure
# ---------------------------------------------------------------------------


## Every Node::Type enumerator is either produced by a fixture in this suite or
## explicitly named unreachable, with a reason. Adding a node kind without doing
## one of the two fails here.
func _test_node_type_vocabulary_is_closed(probe, failures: Array[String]) -> void:
	var produced := {}
	for source in _vocabulary_sources():
		var report := _parse(probe, source)
		_expect(failures, report["complete"],
			"vocabulary source did not parse cleanly: %s -- %s" % [source.strip_edges(), report["diagnostics"]])
		for name in report["node_types"]:
			produced[name] = true

	var names: PackedStringArray = probe.node_type_names()
	_expect(failures, names.size() > 0, "node_type_names() is empty")
	for name in names:
		var is_produced: bool = produced.has(name)
		var is_excused: bool = UNREACHABLE_NODE_TYPES.has(name)
		_expect(failures, is_produced or is_excused,
			"node kind %s is neither produced by a fixture nor named unreachable" % name)
		_expect(failures, not (is_produced and is_excused),
			"node kind %s is named unreachable but a fixture produces it" % name)


func _test_node_type_names_are_distinct(probe, failures: Array[String]) -> void:
	var seen := {}
	for name in probe.node_type_names():
		_expect(failures, not seen.has(name), "node kind name %s is used twice" % name)
		_expect(failures, name != "", "a node kind has an empty name")
		seen[name] = true


# ---------------------------------------------------------------------------
# Fixtures and helpers
# ---------------------------------------------------------------------------


## One source exercising a broad slice of the grammar, used for the identity,
## idempotency and cold/warm assertions. It must parse cleanly: an assertion
## about two parses agreeing is worth much less on source that fails.
func _rich_source() -> String:
	return """@icon("res://icon.svg")
namespace game.core
import engine

class_name Rich
extends RefCounted

signal changed(value: int)

const LIMIT := 10

enum Mode:
	IDLE = 0
	BUSY = 1

type Numbers = Array[int]

tuple Pair(first: int, second: int)

class Inner:
	var depth := 0

var items: Array[int] = [1, 2, 3]
var table: Dictionary[String, int] = {"a": 1}
var flag := true

static func make() -> Rich:
	return Rich.new()

func run(count: int = 1) -> int:
	var total := 0
	for index in items:
		if index > LIMIT:
			continue
		elif index < 0:
			break
		else:
			total += index * 2
	while total > 100:
		total -= 1
	match total:
		0:
			pass
		_:
			total = total
	var callback := func(value: int) -> int: return value + 1
	var maybe = total if flag else 0
	var casted = total as float
	var tested = self is Rich
	assert(total >= 0)
	var tuple_value := (1, 2)
	var node_value = $Child
	var loaded = preload("res://tests/parser_fixture.barista")
	total = callback.call(total)
	total = -total
	total = ~total
	total = total + (maybe as int)
	await changed
	return total
"""


## The sources whose union must cover the node vocabulary. Split from
## `_rich_source()` because a few kinds need a declaration form of their own.
func _vocabulary_sources() -> Array[String]:
	return [
		_rich_source(),
		"class_name WithTrait\ntrait Marker:\n\tpass\n",
		"trait_name Shape\nfunc area() -> int:\n\treturn 0\n",
		"enum_name Direction:\n\tNORTH = 0\n\tSOUTH = 1\n",
		"tuple_name Point(x: int, y: int)\n",
		"annotation deprecated(reason: String) targets METHOD\n",
		"func f() -> void:\n\twhile true:\n\t\tbreakpoint\n\t\tbreak\n",
		"func f(a: int, b: int) -> void:\n\tvar (x, y) = (a, b)\n\tpass\n",
		"class Box[T]:\n\tvar value: T\n",
		"func f(value: Variant) -> void:\n\tmatch value:\n\t\t[1, 2]:\n\t\t\tpass\n\t\t{\"k\": 1}:\n\t\t\tpass\n\t\tvar bound:\n\t\t\tpass\n",
	]


func _parse(probe, p_source: String) -> Dictionary:
	return probe.parse_text(p_source.to_utf8_buffer(), PATH)


## The rendered tree of `var probe_expression = <expression>`, with the wrapper's
## own text removed, so two spellings of one expression can be compared directly.
func _expression_tree(probe, p_expression: String) -> String:
	var report := _parse(probe, "var a := 1\nvar b := 2\nvar c := 3\nvar d := 4\nvar e := 5\nvar probe_expression = %s\n" % p_expression)
	if not report["complete"]:
		return ""
	return report["tree"]


func _node_kinds(p_report: Dictionary) -> PackedStringArray:
	var kinds := PackedStringArray()
	for node in p_report["nodes"]:
		kinds.append((node as String).split("\t")[0])
	return kinds


func _node_start_lines(p_report: Dictionary) -> PackedInt32Array:
	var lines := PackedInt32Array()
	for node in p_report["nodes"]:
		lines.append((node as String).split("\t")[1].split(":")[0].to_int())
	return lines


func _span_of(p_report: Dictionary, p_node_type: String) -> String:
	for node in p_report["nodes"]:
		var parts: PackedStringArray = (node as String).split("\t")
		if parts[0] == p_node_type:
			return parts[1]
	return ""


func _any_diagnostic_contains(p_report: Dictionary, p_needle: String) -> bool:
	for diagnostic in p_report["diagnostics"]:
		if (diagnostic as String).contains(p_needle):
			return true
	return false


func _any_diagnostic_ends_with(p_report: Dictionary, p_message: String) -> bool:
	for diagnostic in p_report["diagnostics"]:
		if (diagnostic as String).ends_with(p_message):
			return true
	return false


func _expect(failures: Array[String], condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
