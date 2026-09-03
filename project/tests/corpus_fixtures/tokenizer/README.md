# Tokenizer corpus fixtures

One case per row of the fail-closed contract in
[issue #6](https://github.com/cafecito-games/BaristaScript/issues/6), plus the positive controls
that keep each rejection from being a blanket one. Each `.fs` is real source; each `.out` holds the
exact diagnostic the tokenizer must produce, compared byte for byte by `corpus_harness.gd`.

```
godot --headless --path project --script res://tests/corpus_runner.gd -- \
    --corpus res://tests/corpus_fixtures/tokenizer
```

| Fail-closed row | Cases |
|---|---|
| `uint`/`ulong`/`long` reserved in a type | `reserved_type_uint`, `reserved_type_ulong`, `reserved_type_long`, `reserved_type_return`, `reserved_type_parameter`, `reserved_type_cast`, `reserved_type_test`, with `reserved_name_as_identifier` and `reserved_name_in_dictionary` proving they stay usable as ordinary names |
| `U`/`L`/`UL` literal suffix reserved | `suffix_unsigned`, `suffix_long`, `suffix_unsigned_long`, `suffix_lowercase_misordered` (each diagnostic names the suffix as the source wrote it) |
| `as!` reserved | `as_bang`, with `as_with_whitespace` proving `as !x` is untouched |
| Integer literal out of signed 64-bit range | `integer_above_range`, `integer_below_range`, `integer_hex_above_range`, with `integer_range_boundaries` proving both endpoints are accepted |
| Malformed UTF-8 | `malformed_utf8` (the byte offset is named, and no replacement character is substituted) |
| Unterminated string/comment at EOF | `unterminated_string`, `unterminated_multiline_string`, with `comment_at_eof` |

`uint`, `ulong` and `long` are reserved **type names**, not keywords (docs/GRAMMAR.md sections 2.5
and 7.1): they are rejected wherever a type is meant and stay ordinary names everywhere else,
exactly as `int` does. Either way they lex as their own `RESERVED_TYPE_NAME` token, never as an
anonymous identifier a type could later swallow. The tokenizer rejects them in the type positions
the token stream itself settles -- after `->`, `as`, `is`, and after a `:` outside a `{`. A type
argument (`Array[uint]`) and a `type` alias are type positions only the parser can see, and are its
to reject.

`comment_at_eof` is a positive control rather than a rejection: BaristaScript has no block-comment
form (docs/GRAMMAR.md section 2.1), so a `#` comment running to end of file is terminated, not
unterminated. There is no source that produces an unterminated-comment diagnostic, and inventing one
would be inventing a rule the grammar does not have.

The last fail-closed row -- a surviving `NumericType` reference -- has no fixture because it is not
a runtime condition: `src/bs_platform.h` poisons the identifier, so the reference fails to compile.
`grep -rn "NumericType\|numeric_type" src/` returning nothing is its check.
