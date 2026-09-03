# Parser conformance corpus

Imported from Foundry `cafecito-games/Foundry` @ `c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6`,
`modules/foundry_script/tests/scripts/parser`, by `scripts/import_parser_corpus.py`. Do not edit these files by
hand: the script is the single copy of the D1 triage table and `--check` proves this tree is what it
produces.

| Category | Cases |
|---|---|
| `features/` | 133 |
| `errors/` | 178 |
| `warnings/` | 29 |
| **total** | **340** |

Plus 2 `.notest.barista` helper sources, which are skipped and never counted.

Each `.out` holds one line: the success sentinel, or the exact diagnostic the front end must
produce, compared byte for byte. Upstream's `.out` files carry a status word and then the *runtime*
transcript of the case; neither survives the import, because M2 has no runtime to produce a
transcript with and the status word is not a diagnostic.

## What these cases assert at M2

The harness evaluates a case through the tokenizer and the parser. So a `BS_TEST_OK` expectation
here means **"this source parses without a diagnostic"**, not "this source behaves correctly" --
the value it printed upstream is not checked, and neither are the warnings the `warnings/` cases are
named for. That is not a gap this milestone can close: the analyzer does not exist until M3, and
inventing warning expectations now would be inventing the analyzer's output. The 29 `warnings/`
cases earn their place regardless: they are 29 more real sources the parser has to accept.

## Cases whose expectation M3 must restore

Upstream marks 4 of these `FS_TEST_ANALYZER_ERROR`: the parser
accepts them and the *analyzer* rejects them. With no analyzer their honest M2 expectation is the
parse outcome, so each is listed in `tests/corpus_baseline.json` under `analyzer_deferred` together
with the upstream diagnostic it owes:

- `errors/export_enum_wrong_array_type.barista`
- `errors/export_enum_wrong_type.barista`
- `errors/export_tool_button_requires_tool_mode.barista`
- `features/contextual_tagged_union_shorthand.norun.barista`
