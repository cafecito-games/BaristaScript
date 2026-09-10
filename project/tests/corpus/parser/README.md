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

## Static frontend coverage

Exactly 33 cases run through the analyzer (the four original analyzer-error cases and all
29 warning cases); the remaining 307 assert parser coverage.
`BS_TEST_OK` means acceptance at that case's explicit stage. The deprecated-operators control
asserts analyzer acceptance with zero warnings. No runtime transcript or case function executes.

The original blocks contain 53 warnings and 9 errors. The single documented D1 fractional
float-to-int expectation override projects these to 52 warnings and 10 errors. `source_map.json`
binds each selected original source/output hash, full original block, adapted bytes and final expectation.

The real `utils.notest.barista` auxiliary lives at `res://tests/corpus_support/parser/`, outside
aggregate discovery. Its source map records the pinned bytes and four exact D1 annotation patches.
The parser importer owns that support tree transactionally alongside this corpus and its baseline.
