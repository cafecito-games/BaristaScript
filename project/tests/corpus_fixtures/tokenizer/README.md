# Tokenizer corpus fixtures

One case per row of the fail-closed contract in
[issue #6](https://github.com/cafecito-games/BaristaScript/issues/6), plus the positive controls
that keep each rejection from being a blanket one. Each `.barista` is real source; each `.out` holds
the exact diagnostic the **front end** must produce, compared byte for byte by `corpus_harness.gd`.

Since [issue #10](https://github.com/cafecito-games/BaristaScript/issues/10) the harness evaluates a
case through the tokenizer *and* the parser, because a tokenizer-only evaluation would have accepted
every one of the imported corpus's 340 cases whose source merely lexes. Tokenizer diagnostics still
arrive unchanged -- the parser reports them as its own as it consumes the token stream -- so every
rejection row below still pins the tokenizer's exact wording. What changed is the five cases whose
point was that the *tokenizer* stays silent: their expectation is now the diagnostic the parser goes
on to produce, which is a stronger assertion than the `BS_TEST_OK` they held before. Each is called
out below.

```
godot --headless --path project --script res://tests/corpus_runner.gd -- \
    --corpus res://tests/corpus_fixtures/tokenizer
```

| Fail-closed row | Cases |
|---|---|
| `uint`/`ulong`/`long` reserved in a type | `reserved_type_return`, `reserved_type_cast`, `reserved_type_test` for the positions the tokenizer settles; `deferred_to_parser_var_annotation`, `deferred_to_parser_ulong_annotation`, `deferred_to_parser_long_annotation`, `deferred_to_parser_parameter` for the positions it hands to the parser; with `reserved_name_as_identifier` and `reserved_name_in_dictionary` proving they stay usable as ordinary names |
| `U`/`L`/`UL` literal suffix reserved | `suffix_unsigned`, `suffix_long`, `suffix_unsigned_long`, `suffix_lowercase_misordered` (each diagnostic names the suffix as the source wrote it) |
| `as!` reserved | `as_bang`, with `as_with_whitespace` proving `as !x` is untouched: it is not the `as!` diagnostic but the ordinary `Expected type specifier after "as".`, which only a stream that never produced an `as!` token can reach |
| Integer literal out of signed 64-bit range | `integer_above_range`, `integer_below_range`, `integer_hex_above_range`, `integer_spaced_minimum` (unary minus plus the positive magnitude that only the folded signed literal can hold), with `integer_range_boundaries` proving both endpoints are accepted |
| Malformed UTF-8 | `malformed_utf8` (the byte offset is named, and no replacement character is substituted) |
| Unterminated string/comment at EOF | `unterminated_string`, `unterminated_multiline_string`, with `comment_at_eof` |

`uint`, `ulong` and `long` are reserved **type names**, not keywords (docs/GRAMMAR.md sections 2.5
and 7.1): they are rejected wherever a type is meant and stay ordinary names everywhere else,
exactly as `int` does. Either way they lex as their own `RESERVED_TYPE_NAME` token, never as an
anonymous identifier a type could later swallow. The tokenizer rejects them in the type positions
the token stream itself settles -- after `->`, `as` and `is`, where nothing but a type may follow.

Every other type position is the parser's, and the four `deferred_to_parser_*` cases are the
positive controls that pin that boundary: the tokenizer produces no diagnostic there, so the
diagnostic each of them now expects is the parser's own D1 rejection, worded by the single
definition in `BSTokenizer::removed_type_name_diagnostic()`. That the *parser* is what rejects there
is the boundary; that the wording is identical to the tokenizer's is the single definition. A `:` is on the parser's side
because docs/GRAMMAR.md section 6 gives `block` a single-line alternative -- `func g(): uint()`
spells an ordinary expression right after the `:`, so a tokenizer that rejected there would reject a
legal name. A type argument (`Array[uint]`), a `type` alias, and every position that declares a type
name are on the parser's side for the same reason: only a parser knows which it is looking at.

`comment_at_eof` is a positive control rather than a rejection: BaristaScript has no block-comment
form (docs/GRAMMAR.md section 2.1), so a `#` comment running to end of file is terminated, not
unterminated. There is no source that produces an unterminated-comment diagnostic, and inventing one
would be inventing a rule the grammar does not have.

The last fail-closed row -- a surviving `NumericType` reference -- has no fixture because it is not
a runtime condition: `src/bs_platform.h` poisons the identifier, so the reference fails to compile.
`grep -rn "NumericType\|numeric_type" src/` returning nothing is its check.
