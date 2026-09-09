# Parser conformance corpus

Imported from Foundry `cafecito-games/Foundry` @ `c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6`,
`modules/foundry_script/tests/scripts/parser`, by `scripts/import_parser_corpus.py`. Do not edit these files by
hand: the script is the single copy of the D1 triage *reasons* (projected into
`tests/corpus_baseline.json` without duplication) and `--check` proves this tree is what it
produces.

| Category | Cases |
|---|---|
| `features/` | 133 |
| `errors/` | 178 |
| `warnings/` | 29 |
| **total** | **340** |

Plus 2 `.notest.barista` helper sources, which are skipped and never counted.
Upstream at this pin holds 344 runnable parser cases; the triage ledger in
`tests/corpus_baseline.json` accounts for every non-import disposition so
`upstream_total == imported + excluded + deferred`.

Each `.out` is a complete static diagnostic block followed by exactly one LF. The generated
`case_stages.json` assigns each runnable case its explicit frontend stage; helpers have no entries.
Do not use `--update-expectations` here: the importer owns sources, expectations and stages.

## What these cases assert at oracle checkpoint A

All 340 cases remain at **parser** stage. A `BS_TEST_OK` expectation means this
source parses without a diagnostic. It does not assert analyzer success or runtime behavior.
The four analyzer-error debts and all 29 warning cases await #31 checkpoint B after semantic
restoration. The static oracle is exercised separately by miniature analyzer fixtures. No upstream
runtime transcript is compared, and no case function executes.

## Cases whose expectation M3 must restore

Upstream marks 4 of these `FS_TEST_ANALYZER_ERROR`: the parser
accepts them and the *analyzer* rejects them. With no analyzer their honest M2 expectation is the
parse outcome, so each is listed in `tests/corpus_baseline.json` under `analyzer_deferred` together
with the upstream diagnostic it owes:

- `errors/export_enum_wrong_array_type.barista`
- `errors/export_enum_wrong_type.barista`
- `errors/export_tool_button_requires_tool_mode.barista`
- `features/contextual_tagged_union_shorthand.norun.barista`
