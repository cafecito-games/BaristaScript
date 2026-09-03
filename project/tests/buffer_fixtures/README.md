# Token-buffer fixtures

Real bytes an **older build** of this extension wrote, kept so that
`parser_test.gd::_test_a_previous_format_version_buffer_is_refused` can drive a
previous-format buffer through the same entry point a cached one arrives by.

| File | How it was produced |
|---|---|
| `legacy_v2_token_buffer.bin` | `BSTokenizerBuffer::parse_code_string(source, COMPRESS_NONE)` (`src/bs_tokenizer_buffer.cpp:260` @ commit `111efa0`, `TOKENIZER_VERSION` 2) |
| `legacy_v2_token_buffer_zstd.bin` | The same call with `COMPRESS_ZSTD` |

`source` is `parser_test.gd::_rich_source()`, reached through
`BaristaScriptParserProbe::tokenize_to_buffer`.

These are **not regenerable from this build**, and that is the point: version 2
stored a token's start line alone in a 5- or 8-byte record, and version 3 stores
the whole span in a 17- or 20-byte one, so no current writer can emit them. They
are checked in as evidence rather than rederived. A future version bump adds a
`legacy_v3_*` pair alongside these rather than replacing them; the test compares
each fixture's version tag against `BSTokenizerBuffer::TOKENIZER_VERSION`, so a
fixture that stops being one version behind fails loudly instead of quietly
testing nothing.
