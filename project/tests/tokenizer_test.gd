# tokenizer_test.gd
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

extends SceneTree

## Contract tests for the ported tokenizer.
##
## The corpus under `res://tests/corpus_fixtures/tokenizer` covers the fail-closed table one case
## per row. What it cannot express is a claim about two runs, two tokenizers, or the whole token
## vocabulary at once, so those live here. Everything runs through
## `BaristaScriptTokenizerProbe`, because godot-cpp's String and Variant only work inside a loaded
## Godot runtime and there is no standalone C++ test binary to run the frontend in.

## Source exercising as much of `Token::Type` as one file can. It is lexically valid rather than
## grammatically meaningful: the tokenizer has no opinion about what the tokens mean.
const KITCHEN_SINK := """@export
@cafecito.test.timeout
namespace app
import other
abstract class Sink extends Node:
	class_name Sink
	trait_name Drainable
	enum_name Kind
	tuple_name Pair
	trait Drainable:
		pass
	tuple Pair:
		pass
	enum Kind:
		pass
	signal drained
	const LIMIT = 10
	static final var shared = 1
	uses Drainable
	var text = "regular"
	var raw = r"raw\\"quoted"
	var name = &"string_name"
	var path = ^"node/path"
	var block = \"\"\"multi
	line\"\"\"
	var numbers = [1, 2.5, 0x1f, 0b1010, 1_000, .5, 1e3]
	var constants = [PI, TAU, INF, NAN]
	var node = $Child
	var wildcard = _
	var nullable: int? = null
	var spread = [...numbers]
	var slice = numbers[0..2]
	var mapping = {"key": 1, "other": 2}
	func drain(value: int, ...rest: Array) -> void:
		if value == 1 and not value != 2 or value < 3 or value > 4:
			value += 1
			value -= 1
			value *= 2
			value **= 2
			value /= 2
			value %= 2
			value <<= 1
			value >>= 1
			value &= 1
			value |= 1
			value ^= 1
			value = value + 1 - 1 * 2 ** 2 / 2 % 2
			value = value & 1 | 2 ^ 3 << 1 >> 1
			value = ~value
			value = value && true || false
			value = value <= 3 >= 1
			value = self.drain
		elif value is int:
			var cast = value as int
			await drained
			assert(value)
			breakpoint
			yield
			var loaded = preload("res://other.barista")
			super.drain(value)
		else:
			for index in numbers:
				while value:
					break
				continue
			match value:
				1 when value:
					pass
				_:
					pass
		return
	func tuple_index(pair: Pair) -> void:
		var first = pair.0
		var second = pair.1;
	func void_returning() -> void:
		pass
`
=======
1abc
"""

## Token types no source can produce, each with the reason it is unreachable rather than untested.
const UNREACHABLE_TOKEN_NAMES := {
	"Empty":
		"the default-constructed Token type. Neither tokenizer ever emits it: every token leaves"
		+ " make_token(), _binary_to_token() or the buffer's layout synthesis with a real type.",
}

var failures: Array[String] = []


func _initialize() -> void:
	var probe := BaristaScriptTokenizerProbe.new()

	_test_positions_are_one_based_and_end_exclusive(probe)
	_test_token_vocabulary_is_closed(probe)
	_test_buffer_round_trips_the_token_stream(probe)
	_test_tokenizing_twice_is_identical(probe)
	_test_tokenizers_do_not_interfere(probe)
	_test_reserved_table_is_the_only_source_of_truth(probe)
	_test_removed_spellings_are_rejected_in_type_positions(probe)
	_test_removed_spellings_stay_usable_as_names(probe)
	_test_removal_diagnostics_name_the_spelling(probe)
	_test_as_bang_is_not_as_followed_by_bang(probe)
	_test_malformed_utf8_names_the_offending_byte(probe)
	_test_integer_range_is_exact(probe)

	for failure in failures:
		push_error(failure)
	if failures.is_empty():
		print("tokenizer contract: all assertions passed")
	quit(0 if failures.is_empty() else 1)


func _test_positions_are_one_based_and_end_exclusive(probe: BaristaScriptTokenizerProbe) -> void:
	var dump := probe.dump_tokens("var x = 1\n".to_utf8_buffer())
	_expect(dump.size() >= 4, "expected at least four tokens, got %s" % [dump])
	# `var` spans columns 1..3 inclusive, so the exclusive end column is 4; `x` is a single column
	# at 5..5, ending at 6. Both are on line 1, and lines are 1-based.
	_expect(dump[0].begins_with("var\t1:1-1:4\t"), "var token position: %s" % dump[0])
	_expect(dump[1].begins_with("Identifier\t1:5-1:6\t"), "identifier token position: %s" % dump[1])
	_expect(dump[3].begins_with("Literal\t1:9-1:10\t"), "literal token position: %s" % dump[3])


func _test_token_vocabulary_is_closed(probe: BaristaScriptTokenizerProbe) -> void:
	var produced := {}
	for line in probe.dump_tokens(KITCHEN_SINK.to_utf8_buffer()):
		produced[line.split("\t")[0]] = true
	for path in _fixture_case_paths():
		for line in probe.dump_tokens(FileAccess.get_file_as_bytes(path)):
			produced[line.split("\t")[0]] = true

	var unreached: Array[String] = []
	for name in probe.token_type_names():
		if produced.has(name):
			continue
		if UNREACHABLE_TOKEN_NAMES.has(name):
			continue
		unreached.append(name)
	_expect(
		unreached.is_empty(),
		"every Token::Type must be produced by a fixture or be declared unreachable; missing: %s"
		% [unreached]
	)

	# The declared-unreachable list is itself checked, so a type that starts being produced cannot
	# stay excused.
	var wrongly_excused: Array[String] = []
	for name in UNREACHABLE_TOKEN_NAMES:
		if produced.has(name):
			wrongly_excused.append(name)
	_expect(
		wrongly_excused.is_empty(),
		"declared-unreachable types are being produced and must be removed from the list: %s"
		% [wrongly_excused]
	)


func _test_buffer_round_trips_the_token_stream(probe: BaristaScriptTokenizerProbe) -> void:
	var sources: Array[PackedByteArray] = [KITCHEN_SINK.to_utf8_buffer()]
	for path in _fixture_case_paths():
		sources.append(FileAccess.get_file_as_bytes(path))

	# An empty stream would satisfy equality without proving anything, so the widest source is
	# required to be substantial before the comparison counts.
	_expect(
		probe.dump_significant_tokens(sources[0]).size() > 200,
		"the vocabulary source must produce a substantial token stream to round-trip"
	)

	for source in sources:
		var text := probe.dump_significant_tokens(source)
		for compress in [false, true]:
			var replayed := probe.dump_buffer_significant_tokens(source, compress)
			_expect(
				text == replayed,
				"token buffer (compress=%s) must replay the same token stream\n  text:   %s\n  buffer: %s"
				% [compress, text, replayed]
			)


func _test_tokenizing_twice_is_identical(probe: BaristaScriptTokenizerProbe) -> void:
	var source := _large_source(5000).to_utf8_buffer()
	var first := probe.dump_tokens(source)
	var second := probe.dump_tokens(source)
	_expect(first == second, "tokenizing the same source twice must give identical streams")
	_expect(first.size() > 5000, "the large source must produce more tokens than it has lines")


func _test_tokenizers_do_not_interfere(probe: BaristaScriptTokenizerProbe) -> void:
	var first_source := KITCHEN_SINK.to_utf8_buffer()
	var second_source := _large_source(64).to_utf8_buffer()

	var baseline_first := probe.dump_tokens(first_source)
	var baseline_second := probe.dump_tokens(second_source)

	# A second probe instance, and an interleaved order, must not change either result: the
	# tokenizer keeps no state outside the instance.
	var other := BaristaScriptTokenizerProbe.new()
	var interleaved_first := other.dump_tokens(first_source)
	var interleaved_second := probe.dump_tokens(second_source)
	var interleaved_first_again := probe.dump_tokens(first_source)

	_expect(baseline_first == interleaved_first, "a second tokenizer must produce the same stream")
	_expect(baseline_second == interleaved_second, "an interleaved tokenization must not drift")
	_expect(baseline_first == interleaved_first_again, "re-tokenizing after another source must not drift")


func _test_reserved_table_is_the_only_source_of_truth(probe: BaristaScriptTokenizerProbe) -> void:
	var reserved := probe.reserved_spellings()
	var sorted_reserved := Array(reserved)
	sorted_reserved.sort()
	_expect(
		sorted_reserved == ["long", "uint", "ulong"],
		"the D1 reserved spellings must be exactly long/uint/ulong, got %s" % [sorted_reserved]
	)

	var keywords := probe.keyword_spellings()
	var seen := {}
	for spelling in keywords:
		_expect(not seen.has(spelling), "keyword %s is listed twice in the table" % spelling)
		seen[spelling] = true
	for spelling in reserved:
		_expect(
			not seen.has(spelling),
			"%s is both a keyword and a reserved spelling; the table must give it one role" % spelling
		)

	# Every spelling the table calls reserved must actually be rejected in a type position the
	# tokenizer settles, with the message that names it. A table entry that produced no diagnostic
	# would be the exact regression D1 guards against.
	for spelling in reserved:
		var diagnostic: String = probe.first_diagnostic(("func f() -> %s:\n\tpass\n" % spelling).to_utf8_buffer())
		_expect(
			diagnostic == '"%s" is reserved. BaristaScript stores every integer on one signed 64-bit carrier; write "int".' % spelling,
			"reserved spelling %s must report its own removal, got: %s" % [spelling, diagnostic]
		)


## `uint`, `ulong` and `long` are reserved *type names*, not keywords: they are rejected wherever a
## type is meant and stay ordinary names everywhere else, exactly as `int` does
## (docs/GRAMMAR.md sections 2.5 and 7.1). What must never happen is the middle case -- a removed
## spelling arriving in a type as an anonymous `Identifier` that silently becomes a user type -- so
## they get their own token type either way.
func _test_removed_spellings_are_rejected_in_type_positions(probe: BaristaScriptTokenizerProbe) -> void:
	# The positions the token stream itself settles: nothing but a type may follow `->`, `as` or
	# `is`, so the tokenizer rejects without needing to know what is being parsed.
	var tokenizer_settled := [
		"func f() -> %s:\n\tpass\n",
		"func f(value: Variant) -> void:\n\tvar cast = value as %s\n",
		"func f(value: Variant) -> void:\n\tvar flag = value is %s\n",
	]
	for spelling in probe.reserved_spellings():
		for template in tokenizer_settled:
			var source: String = template % spelling
			var diagnostic: String = probe.first_diagnostic(source.to_utf8_buffer())
			_expect(
				diagnostic == '"%s" is reserved. BaristaScript stores every integer on one signed 64-bit carrier; write "int".' % spelling,
				"%s in a type position must be rejected: %s -> %s" % [spelling, source.strip_edges(), diagnostic]
			)

	# The other side of the boundary. A `:` is *not* a type position the tokenizer can settle:
	# docs/GRAMMAR.md section 6 gives `block` a single-line alternative, so `func g(): uint()` puts
	# an ordinary expression right after the `:` and rejecting there would reject a legal name. The
	# tokenizer therefore stays silent and hands over a `Reserved type name` token; the rejection is
	# `BSParser::reject_reserved_type_name()`'s, asserted in project/tests/parser_test.gd. Both
	# halves are checked here so the boundary cannot move in only one of the two files.
	var parser_settled := [
		"func f(value: %s) -> void:\n\tpass\n",
		"var declared: %s = 1\n",
		"const DECLARED: %s = 1\n",
		"var declared: Array[%s] = []\n",
	]
	var parser := BaristaScriptParserProbe.new()
	for spelling in probe.reserved_spellings():
		for template in parser_settled:
			var source: String = template % spelling
			_expect(
				probe.first_diagnostic(source.to_utf8_buffer()).is_empty(),
				"the tokenizer must defer this type position to the parser: %s" % source.strip_edges()
			)
			var report: Dictionary = parser.parse_text(source.to_utf8_buffer(), "res://tokenizer_test.barista")
			var expected := '"%s" is reserved. BaristaScript stores every integer on one signed 64-bit carrier; write "int".' % spelling
			var rejected := false
			for diagnostic in report["diagnostics"]:
				if (diagnostic as String).ends_with(expected):
					rejected = true
			_expect(rejected, "the parser must reject this type position: %s -> %s" % [source.strip_edges(), report["diagnostics"]])


## The other half of the same rule: a removed spelling used as an ordinary name is accepted, and is
## never handed on as a plain identifier that a type could later swallow.
func _test_removed_spellings_stay_usable_as_names(probe: BaristaScriptTokenizerProbe) -> void:
	var name_positions := [
		"var %s = 1\n",
		"func %s() -> void:\n\tpass\n",
		"func f() -> void:\n\tvar value = %s + 1\n",
		"func f(value: Variant) -> void:\n\tvar mapping = {\"key\": %s}\n",
		"func f() -> void:\n\tcall(argument = %s)\n",
		"func f() -> void:\n\tvar value = self.%s\n",
	]
	for spelling in probe.reserved_spellings():
		for template in name_positions:
			var source: String = template % spelling
			_expect(
				probe.first_diagnostic(source.to_utf8_buffer()).is_empty(),
				"%s as an ordinary name must be accepted: %s -> %s"
				% [spelling, source.strip_edges(), probe.first_diagnostic(source.to_utf8_buffer())]
			)
			var seen_reserved := false
			for line in probe.dump_tokens(source.to_utf8_buffer()):
				_expect(
					not (line.begins_with("Identifier\t") and line.ends_with(":%s" % spelling)),
					"%s must never lex as a plain identifier: %s" % [spelling, line]
				)
				if line.begins_with("Reserved type name\t") and line.ends_with(":%s" % spelling):
					seen_reserved = true
			_expect(seen_reserved, "%s must lex as a reserved type name in: %s" % [spelling, source.strip_edges()])


## Every D1 removal must name the spelling it is rejecting, in the form the source wrote it.
## A diagnostic that only proposed a replacement would leave a reader with `123lu` hunting for
## which part of the line the compiler objected to.
func _test_removal_diagnostics_name_the_spelling(probe: BaristaScriptTokenizerProbe) -> void:
	for spelling in probe.reserved_spellings():
		var diagnostic: String = probe.first_diagnostic(("func f() -> %s:\n\tpass\n" % spelling).to_utf8_buffer())
		_expect(
			diagnostic.contains('"%s"' % spelling),
			"the %s diagnostic must name the spelling, got: %s" % [spelling, diagnostic]
		)

	# Both the canonical suffixes and the lowercase/misordered spellings the grammar folds into the
	# same rejection, each named exactly as written.
	for suffix in ["U", "L", "UL", "u", "l", "ul", "lu", "LU", "Ul", "uL"]:
		var diagnostic: String = probe.first_diagnostic(("var value = 123%s\n" % suffix).to_utf8_buffer())
		_expect(
			diagnostic == 'The "%s" integer literal suffix is reserved. BaristaScript has one integer type, "int"; write "123".' % suffix,
			"the 123%s diagnostic must name the suffix as written, got: %s" % [suffix, diagnostic]
		)

	_expect(
		probe.first_diagnostic("var bits = value as!other\n".to_utf8_buffer()).contains('"as!"'),
		"the as! diagnostic must name the operator"
	)

	# A run of letters that is not one of the reserved suffixes keeps the ordinary error, so the
	# removal message cannot be proposed for something D1 never removed.
	_expect(
		probe.first_diagnostic("var value = 123abc\n".to_utf8_buffer()) == "Invalid numeric notation.",
		"a non-suffix letter run must stay an invalid-notation error"
	)


func _test_as_bang_is_not_as_followed_by_bang(probe: BaristaScriptTokenizerProbe) -> void:
	var joined := probe.first_diagnostic("var bits = value as!other\n".to_utf8_buffer())
	_expect(
		joined == '"as!" is reserved. It reinterprets between integer widths, which BaristaScript does not distinguish.',
		"as! must be rejected as one operator, got: %s" % joined
	)

	# With whitespace it is an ordinary cast of a negated expression, and must stay untouched.
	var spaced := probe.dump_significant_tokens("var bits = value as !other\n".to_utf8_buffer())
	_expect(spaced.has("as"), "as with whitespace must still lex as the as keyword: %s" % [spaced])
	_expect(spaced.has("!"), "the ! after a spaced as must still lex on its own: %s" % [spaced])
	_expect(
		probe.first_diagnostic("var bits = value as !other\n".to_utf8_buffer()).is_empty(),
		"as !x must not be diagnosed"
	)


func _test_malformed_utf8_names_the_offending_byte(probe: BaristaScriptTokenizerProbe) -> void:
	# "ab" then a two-byte lead with an invalid continuation: the offending byte is at offset 3.
	var truncated := PackedByteArray([0x61, 0x62, 0xC3, 0x28])
	_expect(
		probe.first_diagnostic(truncated) == "Invalid UTF-8 in source at byte offset 3.",
		"a bad continuation byte must be named by offset, got: %s" % probe.first_diagnostic(truncated)
	)

	# A lone continuation byte, an overlong encoding of "/" and a surrogate half are each refused
	# rather than replaced.
	_expect(
		probe.first_diagnostic(PackedByteArray([0x80])) == "Invalid UTF-8 in source at byte offset 0.",
		"a stray continuation byte must be refused"
	)
	_expect(
		probe.first_diagnostic(PackedByteArray([0xC0, 0xAF])) == "Invalid UTF-8 in source at byte offset 0.",
		"an overlong encoding must be refused"
	)
	_expect(
		probe.first_diagnostic(PackedByteArray([0xED, 0xA0, 0x80])) == "Invalid UTF-8 in source at byte offset 0.",
		"a surrogate half must be refused"
	)
	_expect(
		probe.first_diagnostic(PackedByteArray([0xE2, 0x82])) == "Invalid UTF-8 in source at byte offset 0.",
		"a truncated sequence at end of input must be refused"
	)

	# Well-formed non-ASCII source is unaffected.
	_expect(
		probe.first_diagnostic("var café = 1\n".to_utf8_buffer()).is_empty(),
		"well-formed UTF-8 must tokenize"
	)


func _test_integer_range_is_exact(probe: BaristaScriptTokenizerProbe) -> void:
	var accepted := {
		"9223372036854775807": "int:9223372036854775807",
		"-9223372036854775808": "int:-9223372036854775808",
		"0x7fffffffffffffff": "int:9223372036854775807",
		"0b1": "int:1",
	}
	for spelling in accepted:
		var dump := probe.dump_significant_tokens(("var value = %s\n" % spelling).to_utf8_buffer())
		_expect(
			dump.has("Literal\t%s" % accepted[spelling]),
			"%s must lex to %s, got %s" % [spelling, accepted[spelling], dump]
		)

	for spelling in ["9223372036854775808", "-9223372036854775809", "18446744073709551615"]:
		var diagnostic: String = probe.first_diagnostic(("var value = %s\n" % spelling).to_utf8_buffer())
		_expect(
			diagnostic.begins_with('Integer literal is out of range for "int"'),
			"%s must be out of range, got: %s" % [spelling, diagnostic]
		)


func _fixture_case_paths() -> Array[String]:
	var root := "res://tests/corpus_fixtures/tokenizer"
	var paths: Array[String] = []
	var directory := DirAccess.open(root)
	if directory == null:
		failures.append("tokenizer fixtures are unreadable at %s" % root)
		return paths
	directory.list_dir_begin()
	var entry := directory.get_next()
	while not entry.is_empty():
		if entry.ends_with(".fs"):
			paths.append("%s/%s" % [root, entry])
		entry = directory.get_next()
	directory.list_dir_end()
	paths.sort()
	if paths.is_empty():
		failures.append("no tokenizer fixtures found at %s" % root)
	return paths


## A deterministic source of `p_lines` lines, wide enough to exercise indentation, literals and
## operators rather than repeating one trivial statement.
func _large_source(p_lines: int) -> String:
	var lines: PackedStringArray = []
	var index := 0
	while lines.size() < p_lines:
		lines.append("func step_%d(value: int) -> int:" % index)
		lines.append("\tvar total = value * %d + 0x%x" % [index, index])
		lines.append("\tvar label = \"step %d\"" % index)
		lines.append("\tif total > 0 and label != \"\":")
		lines.append("\t\ttotal -= 1")
		lines.append("\treturn total")
		lines.append("")
		index += 1
	return "\n".join(lines.slice(0, p_lines)) + "\n"


func _expect(condition: bool, description: String) -> void:
	if not condition:
		failures.append(description)
